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

#ifndef	_ACF_UTILS_SAFE_ALLOC_H_
#define	_ACF_UTILS_SAFE_ALLOC_H_

#include <stdlib.h>

#include "assert.h"

#ifdef	__cplusplus
extern "C" {
#endif

static inline void *
safe_malloc(size_t size)
{
	void *p = malloc(size);
	VERIFY_MSG(p != NULL, "Cannot allocate %lu bytes: out of memory",
	    (long unsigned)size);
	return (p);
}

static inline void *
safe_calloc(size_t nmemb, size_t size)
{
	void *p = calloc(nmemb, size);
	VERIFY_MSG(p != NULL, "Cannot allocate %lu bytes: out of memory",
	    (long unsigned)(nmemb * size));
	return (p);
}

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_SAFE_ALLOC_H_ */
