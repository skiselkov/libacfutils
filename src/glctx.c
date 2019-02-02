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
#endif	/* IBM */

#include <acfutils/assert.h>
#include <acfutils/glctx.h>
#include <acfutils/log.h>
#include <acfutils/safe_alloc.h>

struct glctx_s {
#if	LIN
	Display		*dpy;
	GLXContext	glc;
	GLXPbuffer	pbuf;
#elif	IBM
	HGLRC		hgl;
#endif	/* IBM */
	bool_t		created;
};

#if    LIN
typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*,
    GLXFBConfig, GLXContext, Bool, const int*);
typedef Bool (*glXMakeContextCurrentARBProc)(Display*,
    GLXDrawable, GLXDrawable, GLXContext);
static glXCreateContextAttribsARBProc glXCreateContextAttribsARB = NULL;
static glXMakeContextCurrentARBProc glXMakeContextCurrentARB = NULL;
#endif /* LIN */

API_EXPORT glctx_t *
glctx_create_invisible(unsigned width, unsigned height, void *share_ctx)
{
#if	LIN
	static int visual_attribs[] = { None };
	int context_attribs[] = {
		GLX_CONTEXT_MAJOR_VERSION_ARB, 4,
		GLX_CONTEXT_MINOR_VERSION_ARB, 0,
		None
	};
	int pbuffer_attribs[] = {
		GLX_PBUFFER_WIDTH, width,
		GLX_PBUFFER_HEIGHT, height,
		None
	};
	int fbcount = 0;
	GLXFBConfig *fbc = NULL;
	glctx_t *ctx = safe_calloc(1, sizeof (*ctx));

	ctx->created = B_TRUE;

	/* open display */
	ctx->dpy = XOpenDisplay(0);
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
	    glXGetProcAddressARB((const GLubyte *)"glXCreateContextAttribsARB");
	glXMakeContextCurrentARB = (glXMakeContextCurrentARBProc)
	    glXGetProcAddressARB((const GLubyte *)"glXMakeContextCurrent");
	if (glXCreateContextAttribsARB == NULL ||
	    glXMakeContextCurrentARB == NULL) {
		logMsg("Missing support for GLX_ARB_create_context");
		goto errout;
	}

	/* Create a context using glXCreateContextAttribsARB */
	ctx->glc = glXCreateContextAttribsARB(ctx->dpy, fbc[0], share_ctx,
	    True, context_attribs);
	if (ctx->glc == NULL){
		logMsg("Failed to create opengl context");
		goto errout;
	}

	/* Create a temporary pbuffer */
	ctx->pbuf = glXCreatePbuffer(ctx->dpy, fbc[0], pbuffer_attribs);

	XFree(fbc);
	fbc = NULL;
	XSync(ctx->dpy, False);

	/* Try to make it the current context */
	if (!glXMakeContextCurrent(ctx->dpy, ctx->pbuf, ctx->pbuf, ctx->glc)) {
		/*
		 * Some drivers doesn't like contexts without a default
		 * framebuffer, so fallback on using the default window.
		 */
		if (!glXMakeContextCurrent(ctx->dpy,
		    DefaultRootWindow(ctx->dpy), DefaultRootWindow(ctx->dpy),
		    ctx->glc)) {
			logMsg("Failed to make context current");
			goto errout;
		}
	}

	return (ctx);
errout:
	if (fbc != NULL)
		XFree(fbc);
	glctx_destroy(ctx);

	return (NULL);
#else	/* !LIN */
	UNUSED(width);
	UNUSED(height);
	UNUSED(share_ctx);
	return (NULL);
#endif	/* !LIN */
}

API_EXPORT glctx_t *
glctx_create_current(void)
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

	return (ctx);
errout:
	glctx_destroy(ctx);
	return (NULL);
#elif	IBM
	ctx->hgl = wglGetCurrentContext();
	if (ctx->hgl == 0)
		goto errout;
	return (ctx);
errout:
	glctx_destroy(ctx);
	return (NULL);
#else	/* !LIN && !IBM */
	return (NULL);
#endif	/* !LIN && !IBM */
}

API_EXPORT void
glctx_destroy(glctx_t *ctx)
{
#if	LIN
	if (ctx->created) {
		if (ctx->glc != NULL)
			glXDestroyContext(ctx->dpy, ctx->glc);
		if (ctx->pbuf != 0)
			glXDestroyPbuffer(ctx->dpy, ctx->pbuf);
	}
	if (ctx->dpy != NULL)
		XCloseDisplay(ctx->dpy);
#endif	/* LIN */
	free(ctx);
}

#if	LIN
void *
glctx_get_display(glctx_t *ctx)
{
	ASSERT(ctx != NULL);
	ASSERT(ctx->dpy != NULL);
	return (ctx->dpy);
}
#endif	/* LIN */
