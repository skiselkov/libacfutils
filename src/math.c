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
