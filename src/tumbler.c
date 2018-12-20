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
 * Copyright 2018 Saso Kiselkov. All rights reserved.
 */

#include <stdio.h>
#include <math.h>

#include "acfutils/assert.h"
#include "acfutils/geom.h"
#include "acfutils/helpers.h"
#include "acfutils/math.h"
#include "acfutils/tumbler.h"

static double
wrap(const tumbler_t *tumbler, double v)
{
	ASSERT(tumbler != NULL);
	ASSERT3F(tumbler->quant, >, 0);

	while (v < 0)
		v += tumbler->mod;
	while (v >= tumbler->mod)
		v -= tumbler->mod;

	return (ABS(v));
}

static void
handle_leading_chars(const tumbler_t *tumbler, const tumbler_t *prev_tumbler,
    double value, char *out_str)
{
	if (*out_str == '0' && ABS(value) < tumbler->div)
		*out_str = '\0';

	if (prev_tumbler != NULL && value < 0 && *out_str == '\0' &&
	    ABS(value) <= tumbler->div &&
	    ABS(value) >= prev_tumbler->div) {
		strlcpy(out_str, "-", TUMBLER_CAP);
	}
}

int
tumbler_solve(const tumbler_t *tumblers, int idx, double value,
    double prev_fract, char out_str[3][TUMBLER_CAP], double *fract)
{
	const tumbler_t *tumbler, *prev_tumbler;

	ASSERT(tumblers != NULL);
	ASSERT(out_str != NULL);
	ASSERT(fract != NULL);

	tumbler = &tumblers[idx];
	if (idx > 0)
		prev_tumbler = &tumblers[idx - 1];
	else
		prev_tumbler = NULL;

	if (prev_tumbler == NULL) {
		double v = fabs(fmod(value / tumbler->div, tumbler->mod));
		double nearest = round(v / tumbler->quant) * tumbler->quant;
		double lower = nearest - tumbler->quant;
		double upper = nearest + tumbler->quant;

		*fract = iter_fract(v, lower, nearest, B_FALSE);

		lower = wrap(tumbler, lower);
		nearest = wrap(tumbler, nearest);
		upper = wrap(tumbler, upper);
		if (value < 0) {
			double tmp = lower;
			lower = upper;
			upper = tmp;
			*fract = 2 - (*fract);
		}

		snprintf(out_str[0], TUMBLER_CAP, tumbler->fmt, fabs(lower));
		snprintf(out_str[1], TUMBLER_CAP, tumbler->fmt, fabs(nearest));
		snprintf(out_str[2], TUMBLER_CAP, tumbler->fmt, fabs(upper));

		return (3);
	} else {
		double prev_v, v, lower, upper, rollover;
		prev_v = fmod(value / prev_tumbler->div, prev_tumbler->mod);
		rollover = 1 - prev_tumbler->quant / prev_tumbler->mod;
		if (ABS(prev_v) >= prev_tumbler->mod * rollover &&
		    ABS(prev_v) <= prev_tumbler->mod) {
			if (prev_fract > 1.0)
				*fract = fmod(prev_fract, 1);
			else
				*fract = prev_fract;
		} else if (value >= 0) {
			*fract = 0;
		} else {
			*fract = 1;
		}

		v = fmod(value / tumbler->div, tumbler->mod);
		lower = floor(v);
		upper = ceil(v);

		snprintf(out_str[0], TUMBLER_CAP, tumbler->fmt,
		    fmod(fabs(lower), tumbler->mod));
		snprintf(out_str[1], TUMBLER_CAP, tumbler->fmt,
		    fmod(fabs(upper), tumbler->mod));
		*out_str[2] = '\0';

		handle_leading_chars(tumbler, prev_tumbler, value, out_str[0]);
		handle_leading_chars(tumbler, prev_tumbler, value, out_str[1]);

		return (2);
	}
}
