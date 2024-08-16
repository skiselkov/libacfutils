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

#include "acfutils/assert.h"
#include "acfutils/compress.h"
#include "acfutils/helpers.h"
#include "acfutils/safe_alloc.h"

#include "zip/zip.h"

typedef struct {
	void		*buf;
	size_t		len;
} outbuf_t;

static size_t
read_cb(void *arg, uint64_t offset, const void *data, size_t size)
{
	ASSERT(arg != NULL);
	outbuf_t *buf = arg;
	UNUSED(offset);
	ASSERT(data != NULL || size != 0);

	buf->buf = safe_realloc(buf->buf, buf->len + size);
	memcpy(buf->buf + buf->len, data, size);
	buf->len += size;

	return (size);
}

void *
decompress_zip(void *in_buf, size_t len, size_t *out_len)
{
	ASSERT(in_buf != NULL || len == 0);
	ASSERT(out_len != NULL);
	*out_len = 0;
	if (len == 0) {
		return (NULL);
	}

	struct zip_t *zip = zip_stream_open(in_buf, len, 0, 'r');
	if (zip == NULL) {
		return (NULL);
	}
	if (zip_entries_total(zip) <= 0) {
		goto errout;
	}
	outbuf_t buf = {};
	if (zip_entry_extract(zip, read_cb, &buf) != 0) {
		goto errout;
	}
	zip_stream_close(zip);

	*out_len = buf.len;
	return (buf.buf);

errout:
	zip_stream_close(zip);
	if (buf.buf != NULL) {
		free(buf.buf);
	}
	return (NULL);
}
