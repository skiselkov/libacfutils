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

#ifndef	GLEW_MX
#define	GLEW_MX
#endif
/*
 * We use static linking on Linux, Apple and MinGW. Everywhere else
 * (notably Windows & MSVC), we use dynamic linking.
 */
#if	LIN || APL || defined(__MINGW32__) || defined(ACFUTILS_DLL)
# ifndef	GLEW_STATIC
#  define	GLEW_STATIC
# endif
#else	/* !LIN && !APL && !defined(__MINGW32__) && !defined(ACFUTILS_DLL) */
# ifndef	GLEW_BUILD
#  define	GLEW_BUILD
# endif
#endif	/* !LIN && !APL && !defined(__MINGW32__) && !defined(ACFUTILS_DLL) */
#include <GL/glew.h>

#include "core.h"
#include "safe_alloc.h"
#include "tls.h"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Only use native TLS support on Linux (where it works properly).
 * On Windows, native TLS isn't reliably available to DLLs and on
 * Mac, any kind of symbol-remapping DRM breaks the native TLS.
 */
#ifndef	LACF_GLEW_USE_NATIVE_TLS
#if	LIN
#define	LACF_GLEW_USE_NATIVE_TLS	1
#else	/* !LIN */
#define	LACF_GLEW_USE_NATIVE_TLS	0
#endif	/* !LIN */
#endif	/* LACF_GLEW_USE_NATIVE_TLS */

#if	LACF_GLEW_USE_NATIVE_TLS

extern THREAD_LOCAL GLEWContext lacf_glew_per_thread_ctx;

#define	lacf_glew_dllmain_hook(reason)
#define	lacf_glew_init()
#define	lacf_glew_thread_fini()
#define	lacf_glew_fini()

static inline GLEWContext *
glewGetContext(void)
{
	return (&lacf_glew_per_thread_ctx);
}

#else	/* !LACF_GLEW_USE_NATIVE_TLS */

#if	LIN || APL

/*
 * The pthread TLS implementation doesn't rely on any need for external
 * cooperation from the caller, so we don't need any of the init/fini
 * functions.
 */
extern pthread_key_t lacf_glew_ctx_key;
extern pthread_once_t lacf_glew_ctx_once;

void lacf_glew_ctx_make_key(void);

#define	lacf_glew_dllmain_hook(reason)
#define	lacf_glew_init()
#define	lacf_glew_thread_fini()
#define	lacf_glew_fini()

static inline GLEWContext *
glewGetContext(void)
{
	GLEWContext *ctx;

	(void) pthread_once(&lacf_glew_ctx_once, lacf_glew_ctx_make_key);
	ctx = (GLEWContext *)pthread_getspecific(lacf_glew_ctx_key);
	if (ctx == NULL) {
		ctx = (GLEWContext *)safe_malloc(sizeof (*ctx));
		(void) pthread_setspecific(lacf_glew_ctx_key, ctx);
	}

	return (ctx);
}

#else	/* !APL && !LIN */

API_EXPORT_DATA DWORD lacf_glew_ctx_key;

API_EXPORT void lacf_glew_dllmain_hook(DWORD reason);
API_EXPORT void lacf_glew_init(void);
API_EXPORT void lacf_glew_thread_fini(void);
API_EXPORT void lacf_glew_fini(void);

static inline GLEWContext *
glewGetContext(void)
{
	GLEWContext *ctx;

	ASSERT(lacf_glew_ctx_key != 0);
	ctx = (GLEWContext *)TlsGetValue(lacf_glew_ctx_key);
	if (ctx == NULL) {
		ctx = (GLEWContext *)lacf_malloc(sizeof (*ctx));
		VERIFY(TlsSetValue(lacf_glew_ctx_key, (void *)ctx));
	}

	return (ctx);
}

#endif	/* !APL && !LIN */

#endif	/* !LACF_GLEW_USE_NATIVE_TLS */

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_GLEW_H_ */
