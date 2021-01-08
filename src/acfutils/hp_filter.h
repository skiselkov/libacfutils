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

#ifndef	_ACF_UTILS_HP_FILTER_H_
#define	_ACF_UTILS_HP_FILTER_H_

#include "core.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct {
	double	state;	/* Filter state */
	double	prev;	/* revious measurement */
	double	RC;	/* Time constant parameter (1/(2.pi.f_c)) */
} hp_filter_t;

static inline void
hp_filter_init(hp_filter_t *filt, double f_cutoff)
{
	ASSERT(filt != NULL);
	ASSERT3F(f_cutoff, >, 0);
	filt->state = NAN;
	filt->prev = NAN;
	filt->RC = 1.0 / (2 * M_PI * f_cutoff);
}

static inline double
hp_filter_update(hp_filter_t *filt, double m, double d_t)
{
	ASSERT(filt != NULL);
	ASSERT3F(d_t, >, 0);
	if (isnan(filt->state)) {
		filt->state = m;
		filt->prev = m;
	} else {
		double alpha = filt->RC / (filt->RC + d_t);
		ASSERT(!isnan(filt->prev));
		filt->state = alpha * filt->state + alpha * (m - filt->prev);
		filt->prev = m;
	}
	return (filt->state);
}

static inline double
hp_filter_get(const hp_filter_t *filt)
{
	ASSERT(filt != NULL);
	return (filt->state);
}

static inline void
hp_filter_set_f_cutoff(hp_filter_t *filt, double f_cutoff)
{
	ASSERT(filt != NULL);
	ASSERT3F(f_cutoff, >, 0);
	filt->RC = 1.0 / (2 * M_PI * f_cutoff);
}

static inline double
hp_filter_get_f_cutoff(const hp_filter_t *filt)
{
	ASSERT(filt != NULL);
	return (1.0 / (2 * M_PI * filt->RC));
}

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_HP_FILTER_H_ */
