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

#include <windows.h>
#include <stdio.h>

#include <acfutils/glew.h>

FILE _iob[] = { *stdin, *stdout, *stderr };

extern "C" FILE *__cdecl
__iob_func(void)
{
	return (_iob);
}

/*
 * If your DLL has its own DllMain, define _LACF_DISABLE_DLLMAIN to avoid
 * a duplicate symbol definition. In that case, however, be sure to call
 * lacf_glew_dllmain_hook() to make sure the GLEW-MX integration works.
 */
#ifndef	_LACF_DISABLE_DLLMAIN

BOOL WINAPI
DllMain(HINSTANCE hinst, DWORD reason, LPVOID resvd)
{
	lacf_glew_dllmain_hook(reason);
	return (TRUE);
}

#endif	/* _LACF_DISABLE_DLLMAIN */
