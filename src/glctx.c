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
 * Copyright 2018 Saso Kiselkov. All rights reserved.
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
#endif	/* APl */

#include <acfutils/assert.h>
#include <acfutils/dr.h>
#include <acfutils/glctx.h>
#include <acfutils/log.h>
#include <acfutils/safe_alloc.h>

#if	defined(__WGLEW_H__) || defined(__GLEW_H__)
#error	"MUST NOT include GLEW headers from glctx.c"
#endif

struct glctx_s {
#if	LIN
	Display		*dpy;
	GLXContext	glc;
	GLXPbuffer	pbuf;
#elif	IBM
	HWND		win;
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
	typedef Bool (*glXMakeContextCurrentARBProc)(Display*,
	    GLXDrawable, GLXDrawable, GLXContext);
	static glXCreateContextAttribsARBProc glXCreateContextAttribsARB = NULL;
	static glXMakeContextCurrentARBProc glXMakeContextCurrentARB = NULL;
	static int visual_attribs[] = { None };
	int context_attribs[] = {
	    GLX_CONTEXT_MAJOR_VERSION_ARB, major_ver,
	    GLX_CONTEXT_MINOR_VERSION_ARB, minor_ver,
	    GLX_CONTEXT_FLAGS_ARB,
	        (fwd_compat ? GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB : 0) |
	        (debug ? GLX_CONTEXT_DEBUG_BIT_ARB : 0),
	    None
	};
	int pbuffer_attribs[] = {
	    GLX_PBUFFER_WIDTH, 16,
	    GLX_PBUFFER_HEIGHT, 16,
	    None
	};
	int fbcount = 0;
	GLXFBConfig *fbc = NULL;
	glctx_t *ctx = safe_calloc(1, sizeof (*ctx));

	ASSERT(share_ctx == NULL || share_ctx->glc != NULL);

	ctx->created = B_TRUE;

	/* open display */
	ctx->dpy = XOpenDisplay(win_ptr);
	if (ctx->dpy == NULL){
		logMsg("Failed to open display");
		goto errout;
	}

	/*
	 * get framebuffer configs, any is usable (might want to add proper
	 * attribs)
	 */
	fbc = glXChooseFBConfig(ctx->dpy, DefaultScreen(ctx->dpy),
	    visual_attribs, &fbcount);
	if (fbc == NULL){
		logMsg("Failed to get FBConfig");
		goto errout;
	}

	/* Get the required extensions */
	glXCreateContextAttribsARB = (glXCreateContextAttribsARBProc)
	    glXGetProcAddressARB((GLubyte *)"glXCreateContextAttribsARB");
	glXMakeContextCurrentARB = (glXMakeContextCurrentARBProc)
	    glXGetProcAddressARB((GLubyte *)"glXMakeContextCurrent");
	if (glXCreateContextAttribsARB == NULL ||
	    glXMakeContextCurrentARB == NULL) {
		logMsg("Missing support for GLX_ARB_create_context");
		goto errout;
	}

	/* Create a context using glXCreateContextAttribsARB */
	ctx->glc = glXCreateContextAttribsARB(ctx->dpy, fbc[0],
	    share_ctx != NULL ? share_ctx->glc : NULL, True, context_attribs);
	if (ctx->glc == NULL){
		logMsg("Failed to create opengl context");
		goto errout;
	}

	/* Create a temporary pbuffer */
	ctx->pbuf = glXCreatePbuffer(ctx->dpy, fbc[0], pbuffer_attribs);

	XFree(fbc);
	fbc = NULL;
	XSync(ctx->dpy, False);

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
	HWND window;
	glctx_t *ctx;

	ASSERT(share_ctx == NULL || share_ctx->hgl != NULL);

	ASSERT(win_ptr != NULL);
	window = win_ptr;

	/* Get the required extensions */
	wglCreateContextAttribsARB = (wglCreateContextAttribsProc)
	    wglGetProcAddress("wglCreateContextAttribsARB");
	if (wglCreateContextAttribsARB == NULL) {
		logMsg("Missing support for WGL_ARB_create_context");
		return (NULL);
	}

	ctx = safe_calloc(1, sizeof (*ctx));
	ctx->created = B_TRUE;
	ctx->dc = GetDC(window);
	if (ctx->dc == NULL) {
		logMsg("Failed to get window device context");
		goto errout;
	}
	ctx->hgl = wglCreateContextAttribsARB(ctx->dc,
	    share_ctx != NULL ? share_ctx->hgl : NULL, attrs);
	if (ctx->hgl == NULL) {
		logMsg("Failed to create context");
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
	GLint num;
	glctx_t *ctx = safe_calloc(1, sizeof (*ctx));

	UNUSED(win_ptr);
	UNUSED(minor_ver);
	UNUSED(fwd_compat);
	UNUSED(debug);
	ASSERT(share_ctx == NULL || share_ctx->cgl != NULL);

	ctx->created = B_TRUE;
	error = CGLChoosePixelFormat(attrs, &pix, &num);
	if (error != kCGLNoError) {
		logMsg("CGLChoosePixelFormat failed with error %d", error);
		goto errout;
	}
	error = CGLCreateContext(pix,
	    share_ctx != NULL ? share_ctx->cgl : NULL, &ctx->cgl);
	if (error != kCGLNoError) {
		logMsg("CGLCreateContext failed with error %d", error);
		goto errout;
	}
	CGLDestroyPixelFormat(pix);

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
	if (ctx->dpy == NULL){
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

bool_t
glctx_make_current(glctx_t *ctx)
{
#if	LIN
	if (ctx != NULL) {
		ASSERT(ctx->dpy != NULL);
		ASSERT(ctx->glc != NULL);
		if (!glXMakeContextCurrent(ctx->dpy, ctx->pbuf, ctx->pbuf,
		    ctx->glc)) {
			/*
			 * Some drivers doesn't like contexts without a default
			 * framebuffer, so fallback on using the default window.
			 */
			if (!glXMakeContextCurrent(ctx->dpy,
			    DefaultRootWindow(ctx->dpy),
			    DefaultRootWindow(ctx->dpy), ctx->glc)) {
				logMsg("Failed to make context current");
				return (B_FALSE);
			}
		}
	} else {
		glXMakeContextCurrent(NULL, None, None, NULL);
	}
#elif	IBM
	if (ctx != NULL) {
		ASSERT(ctx->dc != NULL);
		ASSERT(ctx->hgl != NULL);
		if (!wglMakeCurrent(ctx->dc, ctx->hgl)) {
			logMsg("Failed to make context current");
			return (B_FALSE);
		}
	} else {
		wglMakeCurrent(NULL, NULL);
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
		if (ctx->pbuf != 0)
			glXDestroyPbuffer(ctx->dpy, ctx->pbuf);
	}
	if (ctx->dpy != NULL)
		XCloseDisplay(ctx->dpy);
#elif	IBM
	if (ctx->created) {
		wglDeleteContext(ctx->hgl);
	}
#elif	APL
	if (ctx->created) {
		CGLDestroyContext(ctx->cgl);
	}
#endif	/* APL */
	free(ctx);
}
