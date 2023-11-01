/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license in the file COPYING
 * or http://www.opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file COPYING.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACF_UTILS_PNG_H_
#define	_ACF_UTILS_PNG_H_

#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

API_EXPORT uint8_t *png_load_from_file_rgb_auto(const char *filename,
    int *width, int *height, int *color_type, int *bit_depth);
API_EXPORT uint8_t *png_load_from_file_rgba_auto(const char *filename,
    int *width, int *height, int *color_type, int *bit_depth);
API_EXPORT uint8_t *png_load_from_file_rgba(const char *filename,
    int *width, int *height);
API_EXPORT uint8_t *png_load_from_file_grey(const char *filename,
    int *width, int *height);
API_EXPORT uint8_t *png_load_from_file_grey16(const char *filename,
    int *width, int *height);
API_EXPORT uint8_t *png_load_from_buffer(const void *buf, size_t len,
    int *width, int *height);
API_EXPORT uint8_t *png_load_from_buffer_rgb_auto(const void *buf, size_t len,
    int *width, int *height, int *color_type, int *bit_depth);
API_EXPORT uint8_t *png_load_from_buffer_cairo_argb32(const void *buf,
    size_t len, int *width, int *height);
API_EXPORT bool_t png_write_to_file_grey8(const char *filename,
    int width, int height, const void *data);
API_EXPORT bool_t png_write_to_file_grey16(const char *filename,
    int width, int height, const void *data);
API_EXPORT bool_t png_write_to_file_rgba(const char *filename,
    int width, int height, const void *data);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_PNG_H_ */
