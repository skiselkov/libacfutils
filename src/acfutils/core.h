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

#ifndef	_ACFUTILS_CORE_H_
#define	_ACFUTILS_CORE_H_

#include <stdlib.h>

#include "libconfig.h"
#include "mslibs.h"

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
#define	API_EXPORT_DATA	extern
#endif	/* !IBM && !defined(_MSC_VER) */

#ifdef	__cplusplus
# if		defined(_MSC_VER)
#  define	restrict	__restrict
# else
#  define	restrict	__restrict__
# endif
#elif	__STDC_VERSION__ < 199901L
# if	defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
#  define	restrict	__restrict
# else
#  define	restrict
# endif
# if		defined(_MSC_VER)
#  define	inline	__inline
# else
#  define	inline
# endif
#endif	/* __STDC_VERSION__ < 199901L */

API_EXPORT extern const char *libacfutils_version;

API_EXPORT void *lacf_malloc(size_t n);
#define	LACF_DESTROY(ptr) \
	do { \
		if ((ptr) != NULL) { \
			lacf_free((ptr)); \
			(ptr) = NULL; \
		} \
	} while (0)
API_EXPORT void lacf_free(void *buf);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACFUTILS_CORE_H_ */
