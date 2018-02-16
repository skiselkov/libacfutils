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

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>

#include <acfutils/hexcode.h>

API_EXPORT void
hex_enc(const void *in_raw, size_t len, void *out_enc, size_t out_cap)
{
	const uint8_t *in_8b = in_raw;
	char *out = out_enc;

	/* guarantee zero-termination of output */
	*out = 0;
	for (size_t i = 0, j = 0; i < len && j + 3 <= out_cap; i++, j += 2)
		sprintf(&out[j], "%02x", (unsigned)in_8b[i]);
}

static inline int
xdigit2int(char c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	return (-1);
}

API_EXPORT bool_t
hex_dec(const void *in_enc, size_t len, void *out_raw, size_t out_cap)
{
	const char *in_str = in_enc;
	uint8_t *out = out_raw;

	/* Input length must be a multiple of two and fit into output */
	if ((len % 2 != 0) || (out_cap < len / 2))
		return (B_FALSE);

	for (size_t i = 0, j = 0; i < len && j < out_cap; i += 2, j++) {
		int a = xdigit2int(in_str[i]);
		int b = xdigit2int(in_str[i + 1]);

		if (a == -1 || b == -1)
			return (B_FALSE);
		out[j] = (a << 4) | b;
	}

	return (B_TRUE);
}
