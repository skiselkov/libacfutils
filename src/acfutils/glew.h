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

#ifndef	_ACF_UTILS_GLEW_H_
#define	_ACF_UTILS_GLEW_H_

#if	APL || LIN
#include <pthread.h>
#endif

/*
 * Includes & properly defines the context handler function for the
 * GLEW OS-independent bindings (WGL/GLX).
 * This is needed since libacfutils uses GLEW-MX (multi-context) to
 * support multi-threaded rendering, where each context can have
 * different context caps (primarily for MacOS OpenGL 2.1/4.1
 * multi-context support).
 */

#include <GL/glew.h>

#include <acfutils/safe_alloc.h>
#include <acfutils/tls.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	LACF_GLEW_USE_NATIVE_TLS
#define	LACF_GLEW_USE_NATIVE_TLS	1
#endif

#if	LACF_GLEW_USE_NATIVE_TLS

extern THREAD_LOCAL GLEWContext lacf_glew_per_thread_ctx;

static inline GLEWContext *
glewGetContext(void)
{
	return (&lacf_glew_per_thread_ctx);
}

#else	/* !LACF_GLEW_USE_NATIVE_TLS */

#if	LIN || APL

extern pthread_key_t lacf_glew_ctx_key;
extern pthread_once_t lacf_glew_ctx_once;

void lacf_glew_ctx_make_key(void);

static inline GLEWContext *
glewGetContext(void)
{
	GLEWContext *ctx;

	(void) pthread_once(&lacf_glew_ctx_once, lacf_glew_ctx_make_key);
	ctx = pthread_getspecific(lacf_glew_ctx_key);
	if (ctx == NULL) {
		ctx = safe_malloc(sizeof (*ctx));
		(void) pthread_setspecific(lacf_glew_ctx_key, ctx);
	}

	return (ctx);
}

#endif	/* APL || LIN */

#endif	/* !LACF_GLEW_USE_NATIVE_TLS */

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_GLEW_H_ */
