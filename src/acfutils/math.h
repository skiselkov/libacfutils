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
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */
/** \file */

#ifndef	_ACF_UTILS_MATH_H_
#define	_ACF_UTILS_MATH_H_

#include "assert.h"
#include "math_core.h"
#include "geom.h"

#ifdef	__cplusplus
extern "C" {
#endif

/** @return The 4th power of `x`. */
#define	POW4(x)	((x) * (x) * (x) * (x))
/** @return The 3th power of `x`. */
#define	POW3(x)	((x) * (x) * (x))
/** @return The 2th power of `x`. */
#define	POW2(x)	((x) * (x))
#ifndef	ABS
/** @return The absolute value of `x`. */
#define	ABS(x)	((x) > 0 ? (x) : -(x))
#endif

struct vect2_s;

API_EXPORT unsigned quadratic_solve(double a, double b, double c, double x[2]);
API_EXPORT double fx_lin(double x, double x1, double y1, double x2, double y2)
    PURE_ATTR;
API_EXPORT double fx_lin_multi(double x, const struct vect2_s *points,
    bool_t extrapolate) PURE_ATTR;
API_EXPORT double fx_lin_multi2(double x, const struct vect2_s *points,
    size_t n_points, bool_t extrapolate) PURE_ATTR;
API_EXPORT double *fx_lin_multi_inv(double y, const struct vect2_s *points,
    size_t *num_out);
API_EXPORT double *fx_lin_multi_inv2(double y, const struct vect2_s *points,
    bool_t extrapolate, size_t *num_out);
API_EXPORT double *fx_lin_multi_inv3(double y, const struct vect2_s *points,
    size_t n_points, bool_t extrapolate, size_t *num_out);

/**
 * @note Internall. Do not call directly. Use wavg() instead.
 * @see wavg()
 */
static inline double
wavg_impl(double x, double y, double w, const char *file, int line)
{
	UNUSED(file);
	UNUSED(line);
	ASSERT_MSG(!isnan(w), "%f is NAN: called from: %s:%d", w, file, line);
	ASSERT_MSG(w >= 0.0, "%f < 0.0: called from: %s:%d", w, file, line);
	ASSERT_MSG(w <= 1.0, "%f > 1.0: called from: %s:%d", w, file, line);
	return (x + (y - x) * w);
}
/**
 * Weighted average, `w` is weight fraction from 0.0 = all of x to
 * 1.0 = all of y. The `w` argument MUST be within the 0.0-1.0 range,
 * otherwise an assertion failure is triggered.
 */
#define wavg(x, y, w)	wavg_impl((x), (y), (w), __FILE__, __LINE__)

/**
 * Similar to wavg(), but performs no bounds checks. For values of `w` which
 * are outside of 0.0-1.0, the value is extrapolated beyond the bounds.
 */
static inline double
wavg2(double x, double y, double w)
{
	return (x + (y - x) * w);
}

/**
 * Given two values min_val and max_val, returns how far between min_val
 * and max_val a third value 'x' lies. If `clamp_output' is true, 'x' is
 * clamped such that it always lies between min_val and max_val. In essence,
 * this function computes:
 *```
 *      ^
 *      |
 *    1 -------------------+
 *      |               /  |
 *      |             /    |
 * f(x) ------------+      |
 *      |         / |      |
 *      |       /   |      |
 *    0 ------+     |      |
 *      |     |     |      |
 *      +-----|-----|------|----->
 *         min_val  x   max_val
 *```
 */
static inline double
iter_fract(double x, double min_val, double max_val, bool_t clamp_output)
{
	ASSERT3F(min_val, !=, max_val);
	x = (x - min_val) / (max_val - min_val);
	if (clamp_output)
		x = clamp(x, 0, 1);
	return (x);
}

#define	MAX_PN_INTERP_ORDER	64
/**
 * This is a generic polynomial interpolator/extrapolator. Given a series
 * of X-Y coordinates, pn_interp_init constructs a polynomial interpolation
 * that smoothly passes through all of the input points. Please note that
 * this function is limited to a maximum number of inputs points (mainly
 * to make memory management easy by not requiring dynamic allocation).
 * @see pn_interp_init()
 * @see pn_interp_run()
 */
typedef struct {
	unsigned	order;
	double		coeff[MAX_PN_INTERP_ORDER];
} pn_interp_t;

API_EXPORT void pn_interp_init(pn_interp_t *interp, const vect2_t *points,
    unsigned npts);

/**
 * Given an initialized pn_interp_t, calculates the Y value at a given point.
 * @param x The X point for which to calculate the interpolated Y value.
 * @param interp An initialized interpolator (see pn_interp_init()).
 * @see pn_interp_init()
 */
static inline double
pn_interp_run(double x, const pn_interp_t *interp)
{
	double y = 0, power = 1;

	ASSERT(interp != NULL);
	ASSERT(interp->order != 0);
	for (unsigned i = 0; i < interp->order; i++) {
		y += interp->coeff[i] * power;
		power *= x;
	}

	return (y);
}

/**
 * Implements the smoothstep function from GLSL. See Wikipedia
 * for more info: https://en.wikipedia.org/wiki/Smoothstep
 */
static inline double
smoothstep(double x, double edge0, double edge1)
{
	ASSERT(!isnan(x));
	ASSERT3F(edge1, >, edge0);
	x = clamp((x - edge0) / (edge1 - edge0), 0, 1);
	return (POW2(x) * (3 - 2 * x));
}

/**
 * Inverse of smoothstep(). The returned value is always in the range of 0-1.
 */
static inline double
smoothstep_inv(double x)
{
	ASSERT(!isnan(x));
	return (0.5 - sin(asin(1 - 2 * x) / 3));
}

/**
 * Hysterhesis-rounding macro. Given an old value, new value, rounding step
 * and hysterhesis range, performs the following:
 * - first rounds `newval` to the nearest multiple of `step`
 * - if `oldval` is NAN, it simply adops the new rounded value
 * - otherwise, if the new rounded value differs from `oldval` by at least
 *	half step plus the hysterhesis range fraction, then `oldval` is set
 *	to the new rounded value, otherwise it stays put.
 *
 * The purpose of this macro is to make `oldval` change in fixed steps, but
 * avoid oscillation if the new value is right in the middle between two
 * step sizes:
 *```
 *              oldval              nearly halfway to next step will cause
 *                 |    +--newval - oldval to start oscillating rapidly
 *                 |    |           between X and X+1
 *                 |    |
 *                 V    V
 * ======+=========+=========+=========+=====
 *      X-1        X        X+1       X+2
                   |         |
 *                 |<-step-->|
 *```
 * The HROUND2() macro provides an additional "buffer" zone around the
 * midpoint of the step, to avoid this oscillation:
 *```
 *              oldval
 *                 |    newval     newval must now cross beyond this
 *                 |       |      _point before it will cause oldval
 *                 |       |     / to change from X to X+1
 *                 V       V    V
 * =+==============+=======|====|==+====
 * X-1             X       |<-->| X+1
 *                 |      hyst_rng |
 *                 |               |
 *                 |<----step----->|
 *```
 */
#define	HROUND2(oldval, newval, step, hyst_rng) \
	do { \
		double tmpval = round((newval) / (step)) * (step); \
		if (isnan((oldval))) { \
			(oldval) = tmpval; \
		} else if ( \
		    (newval) > (oldval) + (step) * (0.5 + (hyst_rng)) || \
		    (newval) < (oldval) - (step) * (0.5 + (hyst_rng))) { \
			(oldval) = tmpval; \
		} \
	} while (0)

/**
 * Same as HROUND2(), but uses a default hysterhesis range value of 0.35.
 * That means the new value will only influence `oldval` if it is at least
 * step * (0.5 + 0.35) = step * 0.85 or 85% of the way to the nearest step
 * value away from `oldval`.
 */
#define	HROUND(oldval, newval, step) \
	HROUND2((oldval), (newval), (step), 0.35)

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_GEOM_H_ */
