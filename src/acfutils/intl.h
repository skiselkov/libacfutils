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

#ifndef	_ACF_UTILS_INTL_H_
#define	_ACF_UTILS_INTL_H_

#include <stdint.h>

#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	_(str)	acfutils_xlate(str)

API_EXPORT bool_t acfutils_xlate_init(const char *po_file);
API_EXPORT void acfutils_xlate_fini(void);
API_EXPORT const char *acfutils_xlate(const char *msgid);

API_EXPORT const char *acfutils_xplang2code(int lang);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_INTL_H_ */
