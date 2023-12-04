/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
*/
/*
 * Copyright 2019 Saso Kiselkov. All rights reserved.
 */

#include <stdlib.h>

#include "acfutils/glew.h"

THREAD_LOCAL GLEWContext lacf_glew_per_thread_ctx;

#if	APL || LIN

pthread_key_t lacf_glew_ctx_key;
pthread_once_t lacf_glew_ctx_once = PTHREAD_ONCE_INIT;

void
lacf_glew_ctx_make_key(void)
{
	(void) pthread_key_create(&lacf_glew_ctx_key, free);
}

#else	/* !APL && !LIN */

DWORD lacf_glew_ctx_key = 0;

/*
 * Windows doesn't have a thread-exit and DLL-unload facility, so
 * we need to hook into the DllMain function. One is provided by
 * lacf_msvc_compat.cpp, however if the module dev wants to define
 * their own, they'll need to call lacf_glew_dllmain_hook manually.
 */
void
lacf_glew_dllmain_hook(DWORD reason)
{
	switch (reason) {
	case DLL_PROCESS_ATTACH:
		lacf_glew_init();
		break;
	case DLL_THREAD_DETACH:
		lacf_glew_thread_fini();
		break;
	case DLL_PROCESS_DETACH:
		lacf_glew_fini();
		break;
	}
}

void
lacf_glew_init(void)
{
	VERIFY3U(lacf_glew_ctx_key, ==, 0);
	lacf_glew_ctx_key = TlsAlloc();
	VERIFY(lacf_glew_ctx_key != TLS_OUT_OF_INDEXES);
}

void
lacf_glew_thread_fini(void)
{
	GLEWContext *ctx;

	VERIFY(lacf_glew_ctx_key != 0);
	ctx = TlsGetValue(lacf_glew_ctx_key);
	if (ctx != NULL) {
		lacf_free(ctx);
		TlsSetValue(lacf_glew_ctx_key, NULL);
	}
}

void
lacf_glew_fini(void)
{
	ASSERT(lacf_glew_ctx_key != 0);
	TlsFree(lacf_glew_ctx_key);
	lacf_glew_ctx_key = 0;
}

#ifdef	ACFUTILS_DLL

BOOL WINAPI
DllMain(HINSTANCE hinst, DWORD reason, LPVOID resvd)
{
	LACF_UNUSED(hinst);
	LACF_UNUSED(resvd);
	lacf_glew_dllmain_hook(reason);
	return (TRUE);
}

#endif	/* defined(ACFUTILS_DLL) */

#endif	/* !APL && !LIN */
