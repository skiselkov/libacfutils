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

#ifndef	_ACF_UTILS_MATH_CORE_H_
#define	_ACF_UTILS_MATH_CORE_H_

#include "assert.h"
#include "core.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	DEFN_CLAMP(name, type, assert_chk) \
static inline type \
name(const char *filename, int line, type x, type min_val, type max_val) \
{ \
	if ((min_val) > (max_val)) { \
		logMsg("Actual assert location: %s:%d", filename, line); \
		assert_chk(min_val, <=, max_val); \
	} \
	if (x < min_val) \
		return (min_val); \
	if (x > max_val) \
		return (max_val); \
	return (x); \
}

DEFN_CLAMP(clamp_impl, double, VERIFY3F)

DEFN_CLAMP(clampl_impl, long, VERIFY3S)
DEFN_CLAMP(clampi_impl, int, VERIFY3S)

#define	clamp(x, min_val, max_val)	\
	clamp_impl(__FILE__, __LINE__, (x), (min_val), (max_val))
#define	clampl(x, min_val, max_val)	\
	clampl_impl(__FILE__, __LINE__, (x), (min_val), (max_val))
#define	clampi(x, min_val, max_val)	\
	clampi_impl(__FILE__, __LINE__, (x), (min_val), (max_val))

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_GEOM_H_ */
