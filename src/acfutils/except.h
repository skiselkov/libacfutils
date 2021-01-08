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
 * Copyright 2020 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACFUTILS_EXCEPT_H_
#define	_ACFUTILS_EXCEPT_H_

#include "core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define	except_init	ACFSYM(except_init)
API_EXPORT void except_init(void);
#define	except_fini	ACFSYM(except_fini)
API_EXPORT void except_fini(void);

#ifdef __cplusplus
}
#endif

#endif	/* _ACFUTILS_EXCEPT_H_ */
