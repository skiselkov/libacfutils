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

#ifndef	_ACF_UTILS_TIME_H_
#define	_ACF_UTILS_TIME_H_

#include <stdint.h>
#include <time.h>

#include "assert.h"
#include "core.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	USEC2SEC(usec)	((usec) / 1000000.0)
#define	SEC2USEC(sec)	((sec) * 1000000ll)
#define	NSEC2SEC(usec)	((usec) / 1000000000.0)
#define	SEC2NSEC(sec)	((sec) * 1000000000ll)

static inline uint64_t
microclock(void)
{
#if	IBM
	LARGE_INTEGER val, freq;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&val);
	return ((val.QuadPart * 1000000llu) / freq.QuadPart);
#else	/* !IBM */
	struct timespec ts;
	VERIFY0(clock_gettime(CLOCK_REALTIME, &ts));
	return ((ts.tv_sec * 1000000llu) + (ts.tv_nsec / 1000llu));
#endif
}

/* Not portable */
static inline uint64_t
nanoclock(void)
{
#if	IBM
	return (microclock() * 1000llu);
#else	/* !IBM */
	struct timespec ts;
	VERIFY0(clock_gettime(CLOCK_MONOTONIC, &ts));
	return (ts.tv_sec * 1000000000llu + ts.tv_nsec);
#endif	/* !IBM */
}

/*
 * Converts a struct tm specified in UTC (not local time) into unixtime.
 * This only considers the following fields from the tm structure, tm_year,
 * tm_yday, tm_hour, tm_min and tm_sec. Any other fields are ignored.
 */
time_t lacf_timegm(const struct tm *tm);

#if	IBM
#define	timegm lacf_timegm
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_TIME_H_ */
