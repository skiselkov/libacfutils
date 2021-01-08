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

#ifndef	_ACFUTILS_XPFAIL_H_
#define	_ACFUTILS_XPFAIL_H_

#include <stdbool.h>

#include "dr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define	XPFAIL(_name)	"sim/operation/failures/rel_" _name

static inline bool_t
xpfail_is_active(dr_t *dr)
{
	ASSERT(dr != NULL);
	return (dr_geti(dr) == 6);
}

#ifdef __cplusplus
}
#endif

#endif	/* _ACFUTILS_XPFAIL_H_ */
