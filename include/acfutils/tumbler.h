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
 * Copyright 2018 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACF_UTILS_TUMBLER_H_
#define	_ACF_UTILS_TUMBLER_H_

#include "core.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct {
	double		mod;
	double		div;
	double		quant;
	const char	*fmt;
} tumbler_t;

#define	TUMBLER_CAP	8
#define	TUMBLER_LINES	5

API_EXPORT int tumbler_solve(const tumbler_t *tumblers, int idx, double value,
    double prev_fract, char out_str[TUMBLER_LINES][TUMBLER_CAP], double *fract);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_TUMBLER_H_ */
