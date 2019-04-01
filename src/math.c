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
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#include <math.h>

#include <acfutils/geom.h>
#include <acfutils/helpers.h>
#include <acfutils/math.h>
#include <acfutils/safe_alloc.h>

/*
 * Solves quadratic equation ax^2 + bx + c = 0. Solutions are placed in 'x'.
 * Returns the number of solutions (0, 1 or 2).
 */
unsigned
quadratic_solve(double a, double b, double c, double x[2])
{
	double tmp;

	/* Actually just a linear equation. */
	if (a == 0) {
		if (b == 0)
			return (0);
		x[0] = -c / b;
		return (1);
	}

	tmp = POW2(b) - 4 * a * c;
	if (tmp > ROUND_ERROR) {
		double tmp_sqrt = sqrt(tmp);
		x[0] = (-b + tmp_sqrt) / (2 * a);
		x[1] = (-b - tmp_sqrt) / (2 * a);
		return (2);
	} else if (tmp > -ROUND_ERROR) {
		x[0] = -b / (2 * a);
		return (1);
	} else {
		return (0);
	}
}

/*
 * Interpolates a linear function defined by two points.
 *
 * @param x Point who's 'y' value we're looking for on the function.
 * @param x1 First reference point's x coordinate.
 * @param y1 First reference point's y coordinate.
 * @param x2 Second reference point's x coordinate.
 * @param y2 Second reference point's y coordinate.
 */
double
fx_lin(double x, double x1, double y1, double x2, double y2)
{
	ASSERT3F(x1, !=, x2);
	return (((x - x1) / (x2 - x1)) * (y2 - y1) + y1);
}

/*
 * Multi-segment version of fx_lin. The segments are defined as a series of
 * x-y coordinate points (list terminated with a NULL_VECT2). The list must
 * contain AT LEAST 2 points. The value of 'x' is then computed using the
 * fx_lin function from the appropriate segment. If 'x' falls outside of the
 * curve range, the `extrapolate' argument controls behavior. If extrapolate
 * is B_TRUE, the nearest segment is extrapolated to the value of 'x'.
 * Otherwise the function returns NAN.
 */
double
fx_lin_multi(double x, const struct vect2_s *points, bool_t extrapolate)
{
	ASSERT(!IS_NULL_VECT(points[0]));
	ASSERT(!IS_NULL_VECT(points[1]));

	for (;;) {
		vect2_t p1 = points[0], p2 = points[1];

		ASSERT3F(p1.x, <, p2.x);

		if (x < p1.x) {
			/* X outside of range to the left */
			if (extrapolate)
				return (fx_lin(x, p1.x, p1.y, p2.x, p2.y));
			break;
		}
		/* X in range of current segment */
		if (x <= p2.x)
			return (fx_lin(x, p1.x, p1.y, p2.x, p2.y));
		/* X outside of range to the right */
		if (IS_NULL_VECT(points[2])) {
			if (extrapolate)
				return (fx_lin(x, p1.x, p1.y, p2.x, p2.y));
			break;
		}

		points++;
	}

	return (NAN);
}

double *
fx_lin_multi_inv(double y, const struct vect2_s *points, size_t *num_out)
{
	double *out = NULL;
	size_t cap = 0, num = 0;

	ASSERT(points != NULL);
	ASSERT(num_out != NULL);
	ASSERT(!IS_NULL_VECT(points[0]));
	ASSERT(!IS_NULL_VECT(points[1]));

	for (int i = 0; !IS_NULL_VECT(points[i + 1]); i++) {
		vect2_t p1 = points[i], p2 = points[i + 1];
		double min_val = MIN(p1.y, p2.y);
		double max_val = MAX(p1.y, p2.y);

		if (min_val <= y && y <= max_val)
			cap++;
	}
	if (cap == 0) {
		*num_out = 0;
		return (NULL);
	}

	out = safe_calloc(cap, sizeof (*out));
	for (int i = 0; !IS_NULL_VECT(points[i + 1]); i++) {
		vect2_t p1 = points[i], p2 = points[i + 1];
		double min_val = MIN(p1.y, p2.y);
		double max_val = MAX(p1.y, p2.y);

		if (min_val <= y && y <= max_val)
			out[num++] = fx_lin(y, p1.y, p1.x, p2.y, p2.x);
	}
	ASSERT3U(cap, ==, num);

	*num_out = num;

	return (out);
}
