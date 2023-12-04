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

#include <string.h>

#include <zlib.h>
#include <junzip.h>

#include "acfutils/assert.h"
#include "acfutils/compress.h"
#include "acfutils/helpers.h"
#include "acfutils/safe_alloc.h"

typedef struct {
	JZFile		ops;
	void		*buf;
	size_t		cap;
	uint64_t	off;
} memfile_t;

typedef struct {
	void		*buf;
	size_t		len;
} outbuf_t;

static size_t mem_read(JZFile *file, void *buf, size_t size);
static size_t mem_tell(JZFile *file);
static int mem_seek(JZFile *file, size_t offset, int whence);
static int mem_error(JZFile *file);
static void mem_close(JZFile *file);

static struct JZFile mem_ops = {
    .read = mem_read,
    .tell = mem_tell,
    .seek = mem_seek,
    .error = mem_error,
    .close = mem_close
};

static size_t
mem_read(JZFile *file, void *buf, size_t size)
{
	memfile_t *mf = (memfile_t *)file;

	size = MIN(mf->cap - mf->off, size);
	memcpy(buf, mf->buf + mf->off, size);
	mf->off += size;

	return (size);
}

static size_t
mem_tell(JZFile *file)
{
	memfile_t *mf = (memfile_t *)file;
	return (mf->off);
}

static int
mem_seek(JZFile *file, size_t offset, int whence)
{
	memfile_t *mf = (memfile_t *)file;

	switch (whence) {
	case SEEK_SET:
		mf->off = MIN(offset, mf->cap);
		break;
	case SEEK_CUR:
		mf->off = MIN(mf->off + (int)offset, mf->cap);
		break;
	case SEEK_END:
		mf->off = (offset <= mf->cap ? mf->cap - offset : 0);
		break;
	}

	return (0);
}

static int
mem_error(JZFile *file)
{
	LACF_UNUSED(file);
	return (0);
}

static void
mem_close(JZFile *file)
{
	LACF_UNUSED(file);
}

static void
proc_file(JZFile *zip, outbuf_t *buf)
{
	JZFileHeader header;
	char filename[1024];

	VERIFY3P(buf->buf, ==, NULL);

	if (jzReadLocalFileHeader(zip, &header, filename, sizeof(filename)))
		return;

	buf->buf = safe_malloc(header.uncompressedSize);
	buf->len = header.uncompressedSize;

	if (jzReadData(zip, &header, buf->buf) != Z_OK) {
		free(buf->buf);
		memset(buf, 0, sizeof (*buf));
	}
}

int
rec_cb(JZFile *zip, int idx, JZFileHeader *header, char *filename,
    void *userinfo)
{
	long offset;

	LACF_UNUSED(idx);
	LACF_UNUSED(filename);

	/* store current position */
	offset = zip->tell(zip);
	zip->seek(zip, header->offset, SEEK_SET);
	/* alters file offset */
	proc_file(zip, userinfo);
	zip->seek(zip, offset, SEEK_SET);

	/* Don't continue extracting files, we only support 1 subfile */
	return (0);
}

/**
 * Decompresses the first file contained in a .zip archive and returns its
 * contents.
 *
 * @param in_buf A memory buffer containing the entire .zip file.
 * @param len Number of bytes in `in_buf`.
 * @param out_len Return argument, which will be filled with the amount of
 *	bytes contained in the returned decompressed buffer.
 *
 * @return A buffer containing the decompressed file data, or NULL if
 *	decompression failed.
 * @return Free the returned data using lacf_free().
 */
void *
decompress_zip(void *in_buf, size_t len, size_t *out_len)
{
	memfile_t mf = {
	    .ops = mem_ops,
	    .buf = in_buf,
	    .cap = len
	};
	outbuf_t buf = { .buf = NULL };
	JZFile *zip = (JZFile *)&mf;
	JZEndRecord endrec;

	if (jzReadEndRecord(zip, &endrec) != 0)
		return (NULL);

	if (jzReadCentralDirectory(zip, &endrec, rec_cb, &buf) != 0)
		return (NULL);

	*out_len = buf.len;

	return (buf.buf);
}
