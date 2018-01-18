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

#include <string.h>
#include <stdlib.h>

#include <zlib.h>

#include <7z.h>
#include <7zAlloc.h>
#include <7zBuf.h>
#include <7zCrc.h>
#include <7zFile.h>
#include <7zVersion.h>

#include <acfutils/assert.h>
#include <acfutils/compress.h>

bool_t
zlib_test(const void *in_buf, size_t len)
{
	const uint8_t *buf8 = in_buf;
	if (len < 2)
		return (B_FALSE);
	return (buf8[0] == 0x78 &&
	    (buf8[1] == 0x01 || buf8[1] == 0x9c || buf8[1] == 0xda));
}

void *
zlib_compress(void *in_buf, size_t len, size_t *out_len)
{
	enum { CHUNK = 16384 };
	z_stream strm;
	void *out_buf = malloc(len);
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
		int ret;

		out_cap += OUT_BUF_SZ;
		out_buf = realloc(out_buf, out_cap);

		strm.next_in = in_buf + in_prog;
		strm.avail_in = len - in_prog;
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
		out_len += (out_cap - out_len) - strm.avail_out;
		if (ret == Z_STREAM_END)
			break;
	}

out:
	inflateEnd(&strm);
	*out_len_p = out_len;

	return (out_buf);
}

bool_t
test_7z(const void *in_buf, size_t len)
{
	static const uint8_t magic[] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C };
	return (len >= sizeof (magic) &&
	    memcmp(in_buf, magic, sizeof (magic)) == 0);
}

void *
decompress_7z(const char *filename, size_t *out_len)
{
#define	kInputBufSize	((size_t)1 << 18)
	void *out_buf = NULL;
	static const ISzAlloc g_Alloc = { SzAlloc, SzFree };
	ISzAlloc allocImp = g_Alloc;
	ISzAlloc allocTempImp = g_Alloc;
	CFileInStream archiveStream;
	CLookToRead2 lookStream;
	CSzArEx db;
	SRes res;
	UInt32 blockIndex = 0xFFFFFFFF;
	Byte *outBuffer = NULL;
	size_t outBufferSize = 0;
	size_t offset = 0;
	size_t outSizeProcessed = 0;

	if (InFile_Open(&archiveStream.file, filename))
		return (NULL);

	FileInStream_CreateVTable(&archiveStream);
	LookToRead2_CreateVTable(&lookStream, False);
	lookStream.buf = ISzAlloc_Alloc(&allocImp, kInputBufSize);
	lookStream.bufSize = kInputBufSize;
	lookStream.realStream = &archiveStream.vt;
	LookToRead2_Init(&lookStream);

	CrcGenerateTable();
	SzArEx_Init(&db);
	res = SzArEx_Open(&db, &lookStream.vt, &allocImp, &allocTempImp);
	if (res != SZ_OK)
		goto out;

	res = SzArEx_Extract(&db, &lookStream.vt, 0, &blockIndex, &outBuffer,
	    &outBufferSize, &offset, &outSizeProcessed, &allocImp,
	    &allocTempImp);
	if (res != SZ_OK)
		goto out;

	*out_len = outBufferSize;
	out_buf = malloc(outBufferSize);
	memcpy(out_buf, outBuffer, outBufferSize);

out:
	ISzAlloc_Free(&allocImp, outBuffer);
	SzArEx_Free(&db, &allocImp);
	ISzAlloc_Free(&allocImp, lookStream.buf);

	File_Close(&archiveStream.file);

	return (out_buf);
}
