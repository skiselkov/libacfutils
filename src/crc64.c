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

#include <acfutils/assert.h>

#define	CRC64_POLY	0xC96C5795D7870F42ULL	/* ECMA-182, reflected form */

static uint64_t crc64_table[256];
static uint64_t rand_seed = 0;

void
crc64_init(void)
{
	for (int i = 0; i < 256; i++) {
		uint64_t *ct;
		int j;
		for (ct = crc64_table + i, *ct = i, j = 8; j > 0; j--)
			*ct = (*ct >> 1) ^ (-(*ct & 1) & CRC64_POLY);
	}
}

uint64_t
crc64(const void *input, size_t sz)
{
	const uint8_t *in_bytes = input;
	uint64_t crc = -1ULL;

	ASSERT3U(crc64_table[128], ==, CRC64_POLY);
	for (size_t i = 0; i < sz; i++)
		crc = (crc >> 8) ^ crc64_table[(crc ^ in_bytes[i]) & 0xFF];

	return (crc);
}

void
crc64_srand(uint64_t seed)
{
	rand_seed = seed;
}

uint64_t
crc64_rand(void)
{
	rand_seed = crc64(&rand_seed, sizeof (rand_seed));
	return (rand_seed);
}
