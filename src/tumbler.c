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
 * Copyright 2020 Saso Kiselkov. All rights reserved.
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
    double prev_fract, char out_str[TUMBLER_LINES][TUMBLER_CAP], double *fract)
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
		double lower2 = nearest - 2 * tumbler->quant;
		double lower = nearest - tumbler->quant;
		double upper = nearest + tumbler->quant;
		double upper2 = nearest + 2 * tumbler->quant;

		*fract = iter_fract(v, lower, nearest, B_FALSE) + 1;

		lower2 = wrap(tumbler, lower2);
		lower = wrap(tumbler, lower);
		nearest = wrap(tumbler, nearest);
		upper = wrap(tumbler, upper);
		upper2 = wrap(tumbler, upper2);
		if (value < 0) {
			double tmp = lower;
			lower = upper;
			upper = tmp;
			tmp = lower2;
			lower2 = upper2;
			upper2 = tmp;
			*fract = 4 - (*fract);
		}

		snprintf(out_str[0], TUMBLER_CAP, tumbler->fmt, fabs(lower2));
		snprintf(out_str[1], TUMBLER_CAP, tumbler->fmt, fabs(lower));
		snprintf(out_str[2], TUMBLER_CAP, tumbler->fmt, fabs(nearest));
		snprintf(out_str[3], TUMBLER_CAP, tumbler->fmt, fabs(upper));
		snprintf(out_str[4], TUMBLER_CAP, tumbler->fmt, fabs(upper2));

		return (5);
	} else {
		double prev_v, v, lower, lower2, upper, upper2;
		double rollover_fract;

		prev_v = fmod(value / prev_tumbler->div, prev_tumbler->mod);
		rollover_fract = 1 - prev_tumbler->quant / prev_tumbler->mod;
		/*
		 * If the value of the previous tumbler is between the
		 * rollover begin value and the rollover end, then calculate
		 * the display
		 */
		if (ABS(prev_v) > prev_tumbler->mod * rollover_fract &&
		    ABS(prev_v) <= prev_tumbler->mod) {
			if (prev_fract > 2.0)
				*fract = fmod(prev_fract, 1) + 1;
			else
				*fract = prev_fract;
		} else if (value >= 0) {
			*fract = 1;
		} else {
			*fract = 2;
		}

		v = fmod(value / tumbler->div, tumbler->mod);
		lower = floor(v);
		upper = ceil(v);
		lower2 = lower - (upper - lower);
		upper2 = upper + (upper - lower);

		snprintf(out_str[0], TUMBLER_CAP, tumbler->fmt,
		    fmod(fabs(lower2), tumbler->mod));
		snprintf(out_str[1], TUMBLER_CAP, tumbler->fmt,
		    fmod(fabs(lower), tumbler->mod));
		snprintf(out_str[2], TUMBLER_CAP, tumbler->fmt,
		    fmod(fabs(upper), tumbler->mod));
		snprintf(out_str[3], TUMBLER_CAP, tumbler->fmt,
		    fmod(fabs(upper2), tumbler->mod));
		memset(out_str[4], 0, TUMBLER_CAP);

		handle_leading_chars(tumbler, prev_tumbler, value, out_str[0]);
		handle_leading_chars(tumbler, prev_tumbler, value, out_str[1]);
		handle_leading_chars(tumbler, prev_tumbler, value, out_str[2]);
		handle_leading_chars(tumbler, prev_tumbler, value, out_str[3]);

		return (4);
	}
}
