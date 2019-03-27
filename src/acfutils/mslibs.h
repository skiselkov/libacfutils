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

#ifndef	_ACFUTILS_MSLIBS_H_
#define	_ACFUTILS_MSLIBS_H_

/*
 * This pulls in all the required MSVC linker lib inputs for a static build.
 */
#ifdef	_MSC_VER

#pragma comment(lib, "libacfutils.a")
#pragma comment(lib, "libcairo.a")
#pragma comment(lib, "libfreetype.a")
#pragma comment(lib, "libpixman-1.a")
#pragma comment(lib, "libopusfile.a")
#pragma comment(lib, "libopus.a")
#pragma comment(lib, "libopusurl.a")
#pragma comment(lib, "libogg.a")
#pragma comment(lib, "libshp.a")
#pragma comment(lib, "libproj.a")
#pragma comment(lib, "libcurl.a")
#pragma comment(lib, "libssl.a")
#pragma comment(lib, "libcrypto.a")
#pragma comment(lib, "libpng16.a")
#pragma comment(lib, "libxml2.a")
#pragma comment(lib, "libpcre2-8.a")
#pragma comment(lib, "libglew32mx.a")
#pragma comment(lib, "libz.a")
#pragma comment(lib, "liblzma.a")

#pragma comment(lib, "libmingwex.a")
#pragma comment(lib, "libmingw32.a")

/* Needed for stack protectors in our dependencies */
#pragma comment(lib, "libgcc.a")
#pragma comment(lib, "libgcc_eh.a")

/* Needed for the __iob_func implementation in lacf_msvc_compat.cpp */
#pragma comment(lib, "legacy_stdio_definitions.lib")

/* Needed for networking and log_backtrace function */
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")

#endif	/* _MSC_VER */

#endif	/* _ACFUTILS_MSLIBS_H_ */
