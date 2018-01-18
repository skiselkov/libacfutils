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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_ACF_UTILS_CRC64_H_
#define	_ACF_UTILS_CRC64_H_

#include <stdint.h>
#include <acfutils/core.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	crc64_init	ACFSYM(crc64_init)
API_EXPORT void crc64_init();
#define	crc64		ACFSYM(crc64)
API_EXPORT uint64_t crc64(const void *input, size_t sz);

#define	crc64_srand	ACFSYM(crc64_srand)
API_EXPORT void crc64_srand(uint64_t seed);
#define	crc64_rand	ACFSYM(crc64_rand)
API_EXPORT uint64_t crc64_rand(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_CRC64_H_ */
