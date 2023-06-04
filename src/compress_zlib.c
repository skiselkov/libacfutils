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

#include <string.h>
#include <stdlib.h>

#include <stdio.h>

#include <zlib.h>

#include "acfutils/assert.h"
#include "acfutils/compress.h"
#include "acfutils/safe_alloc.h"

/**
 * Performs a light-weight & quick test to see if some data might constitute
 * zlib-compressed data. Please note that zlib data is NOT the same as a Gzip
 * (.gz) file. A .gz file contains additional metadata that these functions
 * do not understand.
 * @param in_buf Input buffer to check.
 * @param len Input buffer length in bytes.
 * @return B_TRUE if the data MAY contain zlib data, B_FALSE if definitely not.
 */
bool_t
zlib_test(const void *in_buf, size_t len)
{
	const uint8_t *buf8 = in_buf;
	if (len < 2)
		return (B_FALSE);
	return (buf8[0] == 0x78 &&
	    (buf8[1] == 0x01 || buf8[1] == 0x9c || buf8[1] == 0xda));
}

/**
 * Compresses a chunk of data using the zlib algorithm and returns it.
 * The compression ratio applied is the default zlib value (equivalent
 * to "gzip -6").
 *
 * @param in_buf Input data buffer to be compressed.
 * @param len Number of bytes in `in_buf'.
 * @param out_len Output variable which will be filled the number of
 *	bytes contained in the compressed output.
 *
 * @return Returns a pointer to the output buffer containing the
 *	zlib-compressed data. Returns `NULL` on an internal compressor
 *	error.
 * @return Use lacf_free() to free the returned buffer.
 */
void *
zlib_compress(void *in_buf, size_t len, size_t *out_len)
{
	enum { CHUNK = 16384 };
	z_stream strm;
	void *out_buf = safe_malloc(len);
	size_t consumed = 0;
	size_t output = 0;

	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	VERIFY3S(deflateInit(&strm, Z_DEFAULT_COMPRESSION), ==, Z_OK);

	for (;;) {
		int flush;
		int res;
		int have_in = MIN(len - consumed, CHUNK);
		int have_out = len - output;

		strm.next_in = in_buf + consumed;
		strm.avail_in = have_in;
		strm.next_out = out_buf + output;
		strm.avail_out = have_out;

		flush = (have_in == 0 ? Z_FINISH : Z_NO_FLUSH);

		res = deflate(&strm, flush);
		switch (res) {
		case Z_OK:
		case Z_STREAM_END:
			consumed += have_in;
			output += have_out - strm.avail_out;
			*out_len = output;
			if (res == Z_STREAM_END)
				goto out;
			break;
		case Z_BUF_ERROR:
			/*
			 * Compression would expand the data,
			 * so don't compress.
			 */
			free(out_buf);
			out_buf = NULL;
			*out_len = 0;
			goto out;
		default:
			VERIFY_MSG(0, "Invalid result: %d", res);
		}
	}
out:
	deflateEnd(&strm);

	return (out_buf);
}

/**
 * Decompresses a chunk of data previously compressed using the zlib
 * algorithm. Same arguments as zlib_compress().
 * @return The decompressed data, or `NULL` on error, or if the input
 *	data doesn't look like valid zlib compressed data.
 * @return Use lacf_free() to free the returned buffer.
 */
void *
zlib_decompress(void *in_buf, size_t len, size_t *out_len_p)
{
	enum { OUT_BUF_SZ = 32768 };
	z_stream strm;
	void *out_buf = NULL;
	size_t out_len = 0, out_cap = 0;
	size_t in_prog = 0;

	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.next_in = Z_NULL;
	strm.avail_in = 0;
	VERIFY3S(inflateInit(&strm), ==, Z_OK);

	for (;;) {
		int ret, avail_in;

		out_cap += OUT_BUF_SZ;
		out_buf = realloc(out_buf, out_cap);

		strm.next_in = in_buf + in_prog;
		avail_in = len - in_prog;
		strm.avail_in = avail_in;
		strm.next_out = out_buf + out_len;
		strm.avail_out = out_cap - out_len;
		ret = inflate(&strm, Z_NO_FLUSH);
		ASSERT(ret != Z_STREAM_ERROR);
		switch (ret) {
		case Z_NEED_DICT:
			ret = Z_DATA_ERROR;
			/* fallthrough */
		case Z_DATA_ERROR:
		case Z_MEM_ERROR:
			free(out_buf);
			out_buf = NULL;
			out_len = 0;
			goto out;
		}
		in_prog += (avail_in - strm.avail_in);
		out_len += (out_cap - out_len) - strm.avail_out;
		if (ret == Z_STREAM_END)
			break;
	}

out:
	inflateEnd(&strm);
	*out_len_p = out_len;

	return (out_buf);
}
