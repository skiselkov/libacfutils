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
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */
/**
 * \file
 * This module contains a header-implementation of a high-pass filter.
 * @see hp_filter_t
 * @see hp_filter_init()
 * @see hp_filter_update()
 */

#ifndef	_ACF_UTILS_HP_FILTER_H_
#define	_ACF_UTILS_HP_FILTER_H_

#include "core.h"

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * This is a generic high-pass RC filter. Use hp_filter_init() to
 * initialize the filter and hp_filter_update() to feed the filter new
 * input values to be filtered. The hp_filter_update() function returns
 * the new output value of the filter. You can also retrieve the last
 * output value of the filter using hp_filter_get().
 * @see hp_filter_init()
 * @see hp_filter_update()
 * @see hp_filter_get()
 */
typedef struct {
	double	state;	/**< Current filter state */
	double	prev;	/**< Previous measurement */
	double	RC;	/**< Time constant parameter `(1 / (2.pi.f_c))` */
} hp_filter_t;

/**
 * Initializes a high-pass filter.
 * @param filt The filter to be initialized.
 * @param f_cutoff Filter cutoff frequency in Hz.
 * @see hp_filter_t
 */
static inline void
hp_filter_init(hp_filter_t *filt, double f_cutoff)
{
	ASSERT(filt != NULL);
	ASSERT3F(f_cutoff, >, 0);
	filt->state = NAN;
	filt->prev = NAN;
	filt->RC = 1.0 / (2 * M_PI * f_cutoff);
}

/**
 * Updates a high-pass filter with a new input value. You want to
 * call this every time a new measurement is obtained, which you
 * want to filter.
 * @param filt The filter to be updated.
 * @param m The new measurement to be integrated into the filter.
 * @param d_t Delta-time in seconds since the last filter update.
 * @return The current filter output after the update has been performed.
 * @note The filter needs at least 2 measurements before it can start
 *	providing valid output. The filter's output will be `NAN` before
 *	that.
 * @see hp_filter_get()
 */
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

/**
 * @return The current output of a high-pass filter without updating
 *	the filter's state.
 * @note The filter needs at least 2 measurements before it can start
 *	providing valid output. The filter's output will be `NAN` before
 *	that.
 * @see hp_filter_update()
 */
static inline double
hp_filter_get(const hp_filter_t *filt)
{
	ASSERT(filt != NULL);
	return (filt->state);
}

/**
 * Sets a new cutoff frequency for a high-pass filter.
 * @param filt The filter for which to change the cutoff frequency.
 * @param f_cutoff New cutoff frequency to set (in Hz).
 */
static inline void
hp_filter_set_f_cutoff(hp_filter_t *filt, double f_cutoff)
{
	ASSERT(filt != NULL);
	ASSERT3F(f_cutoff, >, 0);
	filt->RC = 1.0 / (2 * M_PI * f_cutoff);
}

/**
 * @return The current cutoff frequency (in Hz) for a high-pass filter.
 */
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
