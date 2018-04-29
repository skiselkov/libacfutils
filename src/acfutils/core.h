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
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACFUTILS_CORE_H_
#define	_ACFUTILS_CORE_H_

#ifdef	__cplusplus
extern "C" {
#endif

#define	UNUSED_ATTR	__attribute__((unused))
#define	UNUSED(x)	(void)(x)

#define	ACFSYM(__sym__)	__libacfutils_ ## __sym__

#if	(IBM || defined(_MSC_VER)) && ACFUTILS_DLL
#define	API_EXPORT	__declspec(dllexport)
#define	API_EXPORT_DATA	__declspec(dllexport)
#else	/* !IBM && !defined(_MSC_VER) */
#define	API_EXPORT
#define	API_EXPORT_DATA
#endif	/* !IBM && !defined(_MSC_VER) */

API_EXPORT extern const char *libacfutils_version;

#define	lacf_free	ACFSYM(lacf_free)
API_EXPORT void lacf_free(void *buf);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACFUTILS_CORE_H_ */
