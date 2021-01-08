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

#ifndef	_ACF_UTILS_COMPRESS_H_
#define	_ACF_UTILS_COMPRESS_H_

#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	zlib_test	ACFSYM(zlib_test)
API_EXPORT bool_t zlib_test(const void *in_buf, size_t len);
#define	zlib_compress	ACFSYM(zlib_compress)
API_EXPORT void *zlib_compress(void *in_buf, size_t len, size_t *out_len);
#define	zlib_decompress	ACFSYM(zlib_decompress)
API_EXPORT void *zlib_decompress(void *in_buf, size_t len, size_t *out_len_p);

#define	test_7z		ACFSYM(test_7z)
API_EXPORT bool_t test_7z(const void *in_buf, size_t len);
#define	decompress_7z	ACFSYM(decompress_7z)
API_EXPORT void *decompress_7z(const char *filename, size_t *out_len);

#define	decompress_zip	ACFSYM(decompress_zip)
API_EXPORT void *decompress_zip(void *in_buf, size_t len, size_t *out_len);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_COMPRESS_H_ */
