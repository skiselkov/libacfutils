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

#include <7z.h>
#include <7zAlloc.h>
#include <7zBuf.h>
#include <7zCrc.h>
#include <7zFile.h>
#include <7zVersion.h>

#include "acfutils/assert.h"
#include "acfutils/compress.h"
#include "acfutils/safe_alloc.h"

/**
 * Performs a light-weight & quick test to see if some data might constitute
 * a 7-zip archive.
 * @param in_buf Input buffer to test.
 * @param len Number of bytes in `in_buf`.
 * @return B_TRUE if the data MAY be a 7-zip archive, B_FALSE if definitely not.
 */
bool_t
test_7z(const void *in_buf, size_t len)
{
	static const uint8_t magic[] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C };
	return (len >= sizeof (magic) &&
	    memcmp(in_buf, magic, sizeof (magic)) == 0);
}

/**
 * Decompresses the first file contained in a 7-zip archive and returns its
 * contents.
 *
 * @param filename The full path to the file holding the 7-zip archive.
 * @param out_len Return argument, which will be filled with the amount of
 *	bytes contained in the returned decompressed buffer.
 *
 * @return A buffer containing the decompressed file data, or NULL if
 *	decompression failed.
 * @return Free the returned data using lacf_free().
 */
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
	out_buf = safe_malloc(outBufferSize);
	memcpy(out_buf, outBuffer, outBufferSize);

out:
	ISzAlloc_Free(&allocImp, outBuffer);
	SzArEx_Free(&db, &allocImp);
	ISzAlloc_Free(&allocImp, lookStream.buf);

	File_Close(&archiveStream.file);

	return (out_buf);
}
