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
 * Copyright 2019 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACF_UTILS_GLEW_OS_H_
#define	_ACF_UTILS_GLEW_OS_H_

/*
 * Includes & properly defines the context handler function for the
 * GLEW OS-specific bindings (WGL/GLX).
 * This is needed since libacfutils uses GLEW-MX (multi-context) to
 * support multi-threaded rendering, where each context can have
 * different context caps (primarily for MacOS OpenGL 2.1/4.1
 * multi-context support).
 */

#include <GL/glew.h>
#if	LIN
#include <GL/glxew.h>
#elif	IBM
#include <GL/wglew.h>
#endif	/* IBM */

#include "tls.h"

#ifdef	__cplusplus
extern "C" {
#endif

#if	LIN

extern THREAD_LOCAL GLXEWContext lacf_glxew_per_thread_ctx;

static inline GLXEWContext *
glxewGetContext(void)
{
	return (&lacf_glxew_per_thread_ctx);
}

#elif	IBM

extern THREAD_LOCAL WGLEWContext lacf_wglew_per_thread_ctx;

static inline WGLEWContext *
wglewGetContext(void)
{
	return (&lacf_wglew_per_thread_ctx);
}

#endif	/* IBM */

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_GLEW_OS_H_ */
