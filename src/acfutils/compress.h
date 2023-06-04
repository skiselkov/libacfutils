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
/**
 * \file
 * This file contains various compression/decompression utility functions.
 */

#ifndef	_ACF_UTILS_COMPRESS_H_
#define	_ACF_UTILS_COMPRESS_H_

#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

API_EXPORT bool_t zlib_test(const void *in_buf, size_t len);
API_EXPORT void *zlib_compress(void *in_buf, size_t len, size_t *out_len);
API_EXPORT void *zlib_decompress(void *in_buf, size_t len, size_t *out_len_p);

API_EXPORT bool_t test_7z(const void *in_buf, size_t len);
API_EXPORT void *decompress_7z(const char *filename, size_t *out_len);

API_EXPORT void *decompress_zip(void *in_buf, size_t len, size_t *out_len);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_COMPRESS_H_ */
