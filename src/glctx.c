/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license in the file COPYING
 * or http://www.opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file COPYING.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2020 Saso Kiselkov. All rights reserved.
 */

#include <string.h>
#include <stdlib.h>

#if	LIN
#include <X11/X.h>
#include <X11/Xlib.h>
#include <GL/gl.h>
#include <GL/glx.h>
#elif	IBM
#include <windows.h>
#include <wingdi.h>
#else	/* APL */
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLTypes.h>
#include <OpenGL/CGLCurrent.h>
#endif	/* APL */

#include "acfutils/assert.h"
#include "acfutils/dr.h"
#include "acfutils/glctx.h"
#include "acfutils/glutils_zink.h"
#include "acfutils/log.h"
#include "acfutils/safe_alloc.h"
#include "acfutils/thread.h"

#if	defined(__WGLEW_H__) || defined(__GLEW_H__)
#error	"MUST NOT include GLEW headers from glctx.c"
#endif

struct glctx_s {
#if	LIN
	Display		*dpy;
	GLXContext	glc;
#elif	IBM
	char		win_cls_name[32];
	HWND		win;
	bool_t		release_dc;
	HDC		dc;
	HGLRC		hgl;
#else	/* APL */
	CGLContextObj	cgl;
#endif	/* APL */
	bool_t		created;
};

#if	LIN

glctx_t *
glctx_create_invisible(void *win_ptr, glctx_t *share_ctx, int major_ver,
    int minor_ver, bool_t fwd_compat, bool_t debug)
{
	typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*,
	    GLXFBConfig, GLXContext, Bool, const int*);
	typedef GLXFBConfig *(*glXChooseFBConfigARBProc)(Display *, int,
	    const int *, int *);

	glXCreateContextAttribsARBProc glXCreateContextAttribsARB = NULL;
	glXChooseFBConfigARBProc glXChooseFBConfigARB = NULL;

	static int visual_attribs[] = { None };
	int context_attribs[] = {
	    GLX_CONTEXT_MAJOR_VERSION_ARB, major_ver,
	    GLX_CONTEXT_MINOR_VERSION_ARB, minor_ver,
	    GLX_CONTEXT_FLAGS_ARB,
	        (fwd_compat ? GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB : 0) |
	        (debug ? GLX_CONTEXT_DEBUG_BIT_ARB : 0),
	    None
	};
	int fbcount = 0;
	GLXFBConfig *fbc = NULL;
	glctx_t *ctx = safe_calloc(1, sizeof (*ctx));

	ASSERT(share_ctx == NULL || share_ctx->glc != NULL);

	ctx->created = B_TRUE;

	/* open display */
	ctx->dpy = XOpenDisplay(win_ptr);
	if (ctx->dpy == NULL) {
		logMsg("Failed to open display");
		goto errout;
	}
	/* Get the function pointers */
	glXCreateContextAttribsARB = (glXCreateContextAttribsARBProc)
	    glXGetProcAddressARB((GLubyte *)"glXCreateContextAttribsARB");
	glXChooseFBConfigARB = (glXChooseFBConfigARBProc)
	    glXGetProcAddress((GLubyte *)"glXChooseFBConfig");
	if (glXCreateContextAttribsARB == NULL ||
	    glXChooseFBConfigARB == NULL) {
		logMsg("Missing support for GLX_ARB_create_context");
		goto errout;
	}
	/*
	 * get framebuffer configs, any is usable (might want to add proper
	 * attribs)
	 */
	fbc = glXChooseFBConfigARB(ctx->dpy, DefaultScreen(ctx->dpy),
	    visual_attribs, &fbcount);
	if (fbc == NULL) {
		logMsg("Failed to get FBConfig");
		goto errout;
	}
	/* Create a context using glXCreateContextAttribsARB */
	ctx->glc = glXCreateContextAttribsARB(ctx->dpy, fbc[0],
	    share_ctx != NULL ? share_ctx->glc : NULL, True, context_attribs);
	if (ctx->glc == NULL) {
		logMsg("Failed to create opengl context");
		goto errout;
	}
	XFree(fbc);
	fbc = NULL;

	return (ctx);
errout:
	if (fbc != NULL)
		XFree(fbc);
	glctx_destroy(ctx);

	return (NULL);
}

void *
glctx_get_xplane_win_ptr(void)
{
	return (getenv("DISPLAY"));
}

#elif	IBM

#define	WGL_CONTEXT_MAJOR_VERSION_ARB		0x2091
#define	WGL_CONTEXT_MINOR_VERSION_ARB		0x2092
#define	WGL_CONTEXT_LAYER_PLANE_ARB		0x2093
#define	WGL_CONTEXT_FLAGS_ARB			0x2094
#define	WGL_CONTEXT_PROFILE_MASK_ARB		0x9126

#define	WGL_CONTEXT_DEBUG_BIT_ARB		0x0001
#define	WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB	0x0002

/*
 * Creates a private window with a private device context that we
 * utilize when we don't need sharelists. This is more stable under
 * Vulkan, which may not set up the main window device context
 * pixel format to be OpenGL-compatible.
 */
static bool_t
glctx_create_priv_window(glctx_t *ctx)
{
	const PIXELFORMATDESCRIPTOR pfd = {
	    .nSize = sizeof(pfd),
	    .nVersion = 1,
	    .iPixelType = PFD_TYPE_RGBA,
	    .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL |
	    PFD_DOUBLEBUFFER,
	    .cColorBits = 32,
	    .cAlphaBits = 8,
	    .iLayerType = PFD_MAIN_PLANE,
	    .cDepthBits = 24,
	    .cStencilBits = 8,
	};
	WNDCLASSA wc = {
	    .style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC,
	    .lpfnWndProc = DefWindowProcA,
	    .hInstance = GetModuleHandle(NULL)
	};
	int pixel_format;

	ASSERT(ctx != NULL);

	/*
	 * Using the context struct pointer should be unique enough
	 * for the class name. Allocating two different structs with
	 * the same address should be impossible (and if it happens,
	 * we're dealing with MUCH bigger problems).
	 */
	snprintf(ctx->win_cls_name, sizeof (ctx->win_cls_name),
	    "glctx-%p", ctx);
	wc.lpszClassName = ctx->win_cls_name;
	if (!RegisterClassA(&wc)) {
		win_perror(GetLastError(), "Failed to register window class");
		memset(ctx->win_cls_name, 0, sizeof (ctx->win_cls_name));
		return (B_FALSE);
	}
	ctx->win = CreateWindowA(ctx->win_cls_name, ctx->win_cls_name,
	    WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
	    0, 0, 32, 32, NULL, NULL, GetModuleHandle(NULL), NULL);
	if (ctx->win == NULL) {
		win_perror(GetLastError(), "Failed to create window");
		return (B_FALSE);
	}
	ctx->dc = GetDC(ctx->win);
	if (ctx->dc == NULL) {
		win_perror(GetLastError(),
		    "Failed to get window device context");
		return (B_FALSE);
	}
	ctx->release_dc = B_TRUE;
	pixel_format = ChoosePixelFormat(ctx->dc, &pfd);
	if (pixel_format == 0) {
		logMsg("Couldn't find a suitable pixel format");
		return (B_FALSE);
	}
	if (!SetPixelFormat(ctx->dc, pixel_format, &pfd)) {
		win_perror(GetLastError(), "Couldn't set pixel format");
		return (B_FALSE);
	}

	return (B_TRUE);
}

glctx_t *
glctx_create_invisible(void *win_ptr, glctx_t *share_ctx, int major_ver,
    int minor_ver, bool_t fwd_compat, bool_t debug)
{
	typedef HGLRC (*wglCreateContextAttribsProc)(HDC, HGLRC, const int *);
	wglCreateContextAttribsProc wglCreateContextAttribsARB;
	const int attrs[] = {
	    WGL_CONTEXT_MAJOR_VERSION_ARB, major_ver,
	    WGL_CONTEXT_MINOR_VERSION_ARB, minor_ver,
	    WGL_CONTEXT_FLAGS_ARB,
		(fwd_compat ? WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB : 0) |
		(debug ? WGL_CONTEXT_DEBUG_BIT_ARB : 0),
	    0 /* list terminator */
	};
	glctx_t *ctx;

	ASSERT(share_ctx == NULL || share_ctx->hgl != NULL);
	UNUSED(win_ptr);

	/* Get the required extensions */
	wglCreateContextAttribsARB = (wglCreateContextAttribsProc)
	    wglGetProcAddress("wglCreateContextAttribsARB");
	if (wglCreateContextAttribsARB == NULL) {
		logMsg("Missing support for WGL_ARB_create_context");
		return (NULL);
	}
	ctx = safe_calloc(1, sizeof (*ctx));
	ctx->created = B_TRUE;
	/*
	 * We used to attempt to reuse X-Plane's window's device context
	 * here. But since Vulkan, that is just too unreliable, so we
	 * just always use a private window and call it a day.
	 */
	if (!glctx_create_priv_window(ctx))
		goto errout;
	ASSERT(ctx->dc != NULL);
	ctx->hgl = wglCreateContextAttribsARB(ctx->dc,
	    share_ctx != NULL ? share_ctx->hgl : NULL, attrs);
	if (ctx->hgl == NULL) {
		win_perror(GetLastError(),
		    "Failed to create invisible OpenGL context");
		goto errout;
	}

	return (ctx);
errout:
	glctx_destroy(ctx);
	return (NULL);
}

void *
glctx_get_xplane_win_ptr(void)
{
	dr_t win_dr;
	unsigned int win_i[2];
	HWND xp_window;

	fdr_find(&win_dr, "sim/operation/windows/system_window_64");
	VERIFY3U(dr_getvi(&win_dr, (int *)win_i, 0, 2), ==, 2);

	xp_window = (HWND)((uintptr_t)win_i[1] << 32 | (uintptr_t)win_i[0]);
	VERIFY(xp_window != INVALID_HANDLE_VALUE);

	return (xp_window);
}

#else	/* APL */

static CGLOpenGLProfile
get_gl_profile(int major_ver)
{
	if (major_ver <= 2)
		return (kCGLOGLPVersion_Legacy);
	if (major_ver == 3)
		return (kCGLOGLPVersion_GL3_Core);
	return (kCGLOGLPVersion_GL4_Core);
}

glctx_t *
glctx_create_invisible(void *win_ptr, glctx_t *share_ctx, int major_ver,
    int minor_ver, bool_t fwd_compat, bool_t debug)
{
	CGLOpenGLProfile profile = get_gl_profile(major_ver);
	const CGLPixelFormatAttribute attrs[] = {
	    /* always request hardware acceleration */
	    kCGLPFAAccelerated,
	    kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)profile,
	    0	/* list terminator */
	};
	CGLPixelFormatObj pix;
	CGLError error;
	glctx_t *ctx = safe_calloc(1, sizeof (*ctx));

	UNUSED(win_ptr);
	UNUSED(minor_ver);
	UNUSED(fwd_compat);
	UNUSED(debug);
	ASSERT(share_ctx == NULL || share_ctx->cgl != NULL);

	ctx->created = B_TRUE;
	if (share_ctx != NULL) {
		pix = CGLGetPixelFormat(share_ctx->cgl);
		error = CGLCreateContext(pix, share_ctx->cgl, &ctx->cgl);
	} else {
		GLint num;
		error = CGLChoosePixelFormat(attrs, &pix, &num);
		if (error != kCGLNoError) {
			logMsg("CGLChoosePixelFormat failed with error %d",
			    error);
			goto errout;
		}
		error = CGLCreateContext(pix, NULL, &ctx->cgl);
		CGLDestroyPixelFormat(pix);
	}
	if (error != kCGLNoError) {
		logMsg("CGLCreateContext failed with error %d", error);
		goto errout;
	}

	return (ctx);
errout:
	glctx_destroy(ctx);
	return (NULL);
}

void *
glctx_get_xplane_win_ptr(void)
{
	/* unnecessary on MacOS */
	return (NULL);
}

#endif	/* APL */

glctx_t *
glctx_get_current(void)
{
	glctx_t *ctx = safe_calloc(1, sizeof (*ctx));

#if	LIN
	/* open display */
	ctx->dpy = XOpenDisplay(0);
	if (ctx->dpy == NULL) {
		logMsg("Failed to open display");
		goto errout;
	}
	ctx->glc = glXGetCurrentContext();
	if (ctx->glc == NULL) {
		glctx_destroy(ctx);
		return (NULL);
	}

	return (ctx);
errout:
	glctx_destroy(ctx);
	return (NULL);
#elif	IBM
	ctx->hgl = wglGetCurrentContext();
	if (ctx->hgl == 0) {
		glctx_destroy(ctx);
		return (NULL);
	}
	ctx->dc = wglGetCurrentDC();
	if (ctx->dc == NULL) {
		logMsg("Current context had no DC?!");
		glctx_destroy(ctx);
		return (NULL);
	}
	return (ctx);
#else	/* APL */
	ctx->cgl = CGLGetCurrentContext();
	if (ctx->cgl == NULL) {
		glctx_destroy(ctx);
		return (NULL);
	}
	return (ctx);
#endif	/* APL */
}

API_EXPORT bool_t
glctx_is_current(glctx_t *ctx)
{
	ASSERT(ctx != NULL);
#if	LIN
	return (ctx->glc == glXGetCurrentContext());
#elif	IBM
	return (ctx->hgl == wglGetCurrentContext());
#else	/* APL */
	return (ctx->cgl == CGLGetCurrentContext());
#endif	/* APL */
}

void *
glctx_get_handle(const glctx_t *ctx)
{
#if	LIN
	return (ctx->glc);
#elif	IBM
	return (ctx->hgl);
#else	/* APL */
	return (ctx->cgl);
#endif	/* APL */
}

bool_t
glctx_make_current(glctx_t *ctx)
{
#if	LIN
	typedef Bool (*glXMakeContextCurrentARBProc)(Display*,
	    GLXDrawable, GLXDrawable, GLXContext);
	static glXMakeContextCurrentARBProc glXMakeContextCurrentARB = NULL;

	if (glXMakeContextCurrentARB == NULL) {
		glXMakeContextCurrentARB = (glXMakeContextCurrentARBProc)
		    glXGetProcAddressARB((GLubyte *)"glXMakeContextCurrent");
		/*
		 * We should never have gotten here without a working
		 * GLX_ARB_create_context.
		 */
		VERIFY(glXMakeContextCurrentARB != NULL);
	}
	if (ctx != NULL) {
		GLXDrawable tgt = (glutils_in_zink_mode() ? None :
		    DefaultRootWindow(ctx->dpy));
		ASSERT(ctx->dpy != NULL);
		ASSERT(ctx->glc != NULL);
		if (!glXMakeContextCurrentARB(ctx->dpy, tgt, tgt, ctx->glc)) {
			logMsg("Failed to make context current");
			return (B_FALSE);
		}
	} else {
		glXMakeContextCurrentARB(NULL, None, None, NULL);
	}
#elif	IBM
	typedef BOOL (*wglMakeCurrentARBProc)(HDC, HGLRC);
	static wglMakeCurrentARBProc wglMakeCurrentARB = NULL;

	if (wglMakeCurrentARB == NULL) {
		wglMakeCurrentARB = (wglMakeCurrentARBProc)
		    wglGetProcAddress("wglMakeCurrent");
		/*
		 * We should never have gotten here without a working
		 * WGL_ARB_create_context.
		 */
		VERIFY(wglMakeCurrentARB != NULL);
	}
	if (ctx != NULL) {
		ASSERT(ctx->dc != NULL);
		ASSERT(ctx->hgl != NULL);
		if (!wglMakeCurrentARB(ctx->dc, ctx->hgl)) {
			win_perror(GetLastError(),
			    "Failed to make context current");
			return (B_FALSE);
		}
	} else {
		wglMakeCurrentARB(NULL, NULL);
	}
#else	/* APL */
	if (ctx != NULL) {
		CGLError error;

		ASSERT(ctx->cgl != NULL);
		error = CGLSetCurrentContext(ctx->cgl);
		if (error != kCGLNoError) {
			logMsg("CGLSetCurrentContext failed with error %d",
			    error);
			return (B_FALSE);
		}
	} else {
		CGLSetCurrentContext(NULL);
	}
#endif	/* APL */
	return (B_TRUE);
}

void *
glctx_get_window_system_handle(glctx_t *ctx)
{
	ASSERT(ctx != NULL);
#if	LIN
	ASSERT(ctx->dpy != NULL);
	return (ctx->dpy);
#elif	IBM
	ASSERT(ctx->win != NULL);
	return (ctx->win);
#else	/* APL */
	return (NULL);
#endif	/* APL */
}

API_EXPORT void
glctx_destroy(glctx_t *ctx)
{
	if (ctx == NULL)
		return;
#if	LIN
	if (ctx->created) {
		if (ctx->glc != NULL)
			glXDestroyContext(ctx->dpy, ctx->glc);
	}
	if (ctx->dpy != NULL)
		XCloseDisplay(ctx->dpy);
#elif	IBM
	if (ctx->created) {
		glctx_t *cur_ctx;

		/*
		 * Due to a bug in ReShade, deleting a context instead installs
		 * it as the "current" context, which means this immediately
		 * invalidates the current context. To work around this bug,
		 * we grab our current context before deleting the other one
		 * and then we reinstall it back as the current context.
		 */
		cur_ctx = glctx_get_current();
		if (!wglDeleteContext(ctx->hgl))
			win_perror(GetLastError(), "wglDeleteContext failed");
		if (cur_ctx != NULL) {
			glctx_make_current(cur_ctx);
			/*
			 * This won't recurse again, because the output of
			 * glctx_get_current doesn't return a context with
			 * the `created' flag set.
			 */
			glctx_destroy(cur_ctx);
		}
		ASSERT(ctx->win != NULL);
		if (ctx->dc != NULL && ctx->release_dc) {
			ASSERT(ctx->win != NULL);
			ReleaseDC(ctx->win, ctx->dc);
		}
		if (ctx->win != NULL)
			DestroyWindow(ctx->win);
		if (ctx->win_cls_name[0] != '\0') {
			UnregisterClassA(ctx->win_cls_name,
			    GetModuleHandle(NULL));
		}
	}
#elif	APL
	if (ctx->created) {
		CGLDestroyContext(ctx->cgl);
	}
#endif	/* APL */
	memset(ctx, 0, sizeof (*ctx));
	free(ctx);
}
