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

#ifndef	_ACF_UTILS_MATH_CORE_H_
#define	_ACF_UTILS_MATH_CORE_H_

#include <math.h>

#include "assert.h"
#include "core.h"

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * @note Internal. Do not use.
 */
#define	DEFN_CLAMP(name, type, assert_chk) \
static inline type \
name(type x, type min_val, type max_val) \
{ \
	if (COND_LIKELY(min_val < max_val)) { \
		if (x < min_val) \
			return (min_val); \
		if (x > max_val) \
			return (max_val); \
	} else { \
		if (x > min_val) \
			return (min_val); \
		if (x < max_val) \
			return (max_val); \
	} \
	return (x); \
}

/**
 * Clamps a number between two double precision floating point numbers.
 * @param x The number to clamp.
 * @param min_val Minimum value. If `x < min_val`, `min_val` is returned.
 * @param min_val Maximum value. If `x > max_val`, `max_val` is returned.
 * @return Input `x` clamped so to fit between `min_val` and `max_val`.
 */
DEFN_CLAMP(clamp, double, ASSERT3F)
/**
 * Same as clamp(), but takes `long int` arguments and returns a `long int`.
 */
DEFN_CLAMP(clampl, long, ASSERT3S)
/**
 * Same as clamp(), but takes `iint` arguments and returns an `int`.
 */
DEFN_CLAMP(clampi, int, ASSERT3S)

#undef	DEFN_CLAMP

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_GEOM_H_ */
