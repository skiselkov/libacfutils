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

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>

#include "acfutils/assert.h"
#include "acfutils/hexcode.h"

/**
 * Encodes binary data into a string of hex digits.
 * @param in_raw Input binary data buffer to be encoded.
 * @param len Input binary buffer length in bytes.
 * @param out_enc Output buffer to hold the string of encoded hex characters.
 *	The output is guaranteed to always be zero-terminated (provided the
 *	buffer can hold at least 1 byte).
 * @param out_cap Output buffer capacity. To properly encode all input into
 *	the output, the output buffer must be able to hold at least twice
 *	`len` bytes plus 1 (for the terminating NUL byte).
 */
API_EXPORT void
hex_enc(const void *in_raw, size_t len, void *out_enc, size_t out_cap)
{
	const uint8_t *in_8b = in_raw;
	char *out = out_enc;

	ASSERT(in_raw != NULL || len == 0);
	ASSERT(out_enc != NULL || out_cap == 0);

	/* guarantee zero-termination of output */
	if (out_cap != 0)
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

/**
 * Decodes a string of hex characters into a binary output buffer.
 * @param in_enc Input string of hex characters. This must NOT contain
 *	any whitespace. It must be strictly ONLY the hex characters.
 *	The input doesn't need to be NUL-terminated (length is provided
 *	explicitly in the `len` parameter).
 * @param len Number of characters in the `in_enc` buffer (excluding any
 *	terminating NUL bytes). This must be a multiple of 2.
 * @param out_raw Output binary buffer, which will be filled with the
 *	decoded data.
 * @param out_cap Output buffer length in bytes. This must be at least
 *	half of `len`.
 * @return `B_TRUE` if decoding succeeded, `B_FALSE` otherwise. Decoding
 *	failure can occur if the input doesn't contain an even number of
 *	characters, the output buffer is too small to contain the entire
 *	decoded input, or if an invalid (non-hex) character is encountered
 *	in the input buffer.
 */
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
