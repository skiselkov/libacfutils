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

#include <string.h>
#include <errno.h>
#include <stdio.h>

#include <png.h>

#include <acfutils/assert.h>
#include <acfutils/log.h>
#include <acfutils/png.h>
#include <acfutils/safe_alloc.h>

typedef struct {
	const void	*bufp;
	size_t		len;
	size_t		cur;
} bufread_t;

/*
 * This is a simplified PNG file loading routine that avoids having to deal
 * with all that libpng nonsense.
 */
uint8_t *
png_load_impl(png_rw_ptr readfunc, void *arg, int *width, int *height,
    int *color_type, int *bit_depth, bool_t bgr, bool_t add_alpha)
{
	FILE *volatile fp = NULL;
	size_t rowbytes;
	png_bytep *volatile rowp = NULL;
	png_structp pngp = NULL;
	png_infop infop = NULL;
	uint8_t header[8];
	uint8_t *volatile pixels = NULL;
	volatile int w, h;
	const char *volatile filename;
	volatile bool_t added_alpha = B_FALSE;

	ASSERT(color_type != NULL);
	ASSERT(bit_depth != NULL);

	if (readfunc == NULL) {
		filename = arg;

		fp = fopen(filename, "rb");
		if (fp == NULL) {
			logMsg("Cannot open file %s: %s", filename,
			    strerror(errno));
			goto out;
		}
		if (fread(header, 1, sizeof (header), fp) != 8 ||
		    png_sig_cmp(header, 0, sizeof (header)) != 0) {
			logMsg("Cannot open file %s: invalid PNG header",
			    filename);
			goto out;
		}
	} else {
		filename = "<memstream>";
	}
	pngp = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	VERIFY(pngp != NULL);
	infop = png_create_info_struct(pngp);
	VERIFY(infop != NULL);
	if (setjmp(png_jmpbuf(pngp))) {
		logMsg("Cannot open file %s: libpng error in init_io",
		    filename);
		goto out;
	}
	if (readfunc == NULL) {
		png_init_io(pngp, fp);
		png_set_sig_bytes(pngp, 8);
	} else {
		png_set_read_fn(pngp, arg, readfunc);
	}
	if (setjmp(png_jmpbuf(pngp))) {
		logMsg("Cannot open file %s: libpng read info failed",
		    filename);
		goto out;
	}
	/*
	 * Initialize info struct and get some basic color & bit depth info.
	 */
	png_read_info(pngp, infop);
	/*
	 * Apply transforms.
	 */
	if (*color_type == -1 && *bit_depth == -1) {
		int ct = png_get_color_type(pngp, infop);
		int depth = png_get_bit_depth(pngp, infop);
		/*
		 * Convert paletted images to RGB.
		 */
		if (ct == PNG_COLOR_TYPE_PALETTE) {
			png_set_palette_to_rgb(pngp);
		}
		if (bgr)
			png_set_bgr(pngp);
		if (add_alpha && (ct & PNG_COLOR_MASK_ALPHA) == 0) {
			png_set_filler(pngp, 0xff, PNG_FILLER_AFTER);
			added_alpha = B_TRUE;
		}
		/*
		 * Expand sub-8-bit grayscale to full 8-bpp.
		 */
		if (ct == PNG_COLOR_TYPE_GRAY && depth < 8)
			png_set_expand_gray_1_2_4_to_8(pngp);
		/*
		 * Transform paletted alpha to full 8-bit alpha.
		 */
		if (png_get_valid(pngp, infop, PNG_INFO_tRNS))
			png_set_tRNS_to_alpha(pngp);
		/*
		 * Convert grayscale to RGB
		 */
		if (ct == PNG_COLOR_TYPE_GRAY ||
		    ct == PNG_COLOR_TYPE_GRAY_ALPHA) {
			png_set_gray_to_rgb(pngp);
		}
		png_read_update_info(pngp, infop);
	}
	w = png_get_image_width(pngp, infop);
	h = png_get_image_height(pngp, infop);

	if (*color_type == -1) {
		*color_type = png_get_color_type(pngp, infop);
		if (added_alpha)
			*color_type = (*color_type) | PNG_COLOR_MASK_ALPHA;
	} else if (png_get_color_type(pngp, infop) != *color_type) {
		logMsg("Bad image file %s: need color type %d, got %d",
		    filename, *color_type, png_get_color_type(pngp, infop));
		goto out;
	}
	if (*bit_depth == -1) {
		*bit_depth = png_get_bit_depth(pngp, infop);
	} else if (png_get_bit_depth(pngp, infop) != *bit_depth) {
		logMsg("Bad image file %s: need %d-bit depth", filename,
		    *bit_depth);
		goto out;
	}
	rowbytes = png_get_rowbytes(pngp, infop);

	rowp = safe_malloc(sizeof (*rowp) * h);
	VERIFY(rowp != NULL);
	for (int i = 0; i < h; i++) {
		rowp[i] = safe_malloc(rowbytes);
		VERIFY(rowp[i] != NULL);
	}
	if (setjmp(png_jmpbuf(pngp))) {
		logMsg("Bad icon file %s: error reading image file", filename);
		goto out;
	}
	png_read_image(pngp, rowp);
	pixels = safe_malloc(h * rowbytes);
	for (int i = 0; i < h; i++)
		memcpy(&pixels[i * rowbytes], rowp[i], rowbytes);
	png_read_end(pngp, NULL);
out:
	if (pngp != NULL)
		png_destroy_read_struct(&pngp, &infop, NULL);
	if (rowp != NULL) {
		for (int i = 0; i < h; i++)
			free(rowp[i]);
		free(rowp);
	}
	if (fp != NULL)
		fclose(fp);

	if (pixels != NULL) {
		*width = w;
		*height = h;
	}

	return (pixels);
}

uint8_t *
png_load_from_file_rgb_auto(const char *filename, int *width, int *height,
    int *color_type, int *bit_depth)
{
	ASSERT(color_type != NULL);
	ASSERT(bit_depth != NULL);
	*color_type = -1;
	*bit_depth = -1;
	return (png_load_impl(NULL, (void *)filename, width, height,
	    color_type, bit_depth, B_FALSE, B_FALSE));
}

uint8_t *
png_load_from_file_rgba_auto(const char *filename, int *width, int *height,
    int *color_type, int *bit_depth)
{
	ASSERT(color_type != NULL);
	ASSERT(bit_depth != NULL);
	*color_type = -1;
	*bit_depth = -1;
	return (png_load_impl(NULL, (void *)filename, width, height,
	    color_type, bit_depth, B_FALSE, B_TRUE));
}

uint8_t *
png_load_from_file_rgba(const char *filename, int *width, int *height)
{
	int color_type = PNG_COLOR_TYPE_RGBA;
	int bit_depth = 8;
	return (png_load_impl(NULL, (void *)filename, width, height,
	    &color_type, &bit_depth, B_FALSE, B_FALSE));
}

uint8_t *
png_load_from_file_grey(const char *filename, int *width, int *height)
{
	int color_type = PNG_COLOR_TYPE_GRAY;
	int bit_depth = 8;
	return (png_load_impl(NULL, (void *)filename, width, height,
	    &color_type, &bit_depth, B_FALSE, B_FALSE));
}

uint8_t *
png_load_from_file_grey16(const char *filename, int *width, int *height)
{
	int color_type = PNG_COLOR_TYPE_GRAY;
	int bit_depth = 16;
	return (png_load_impl(NULL, (void *)filename, width, height,
	    &color_type, &bit_depth, B_FALSE, B_FALSE));
}

static void
bufread(png_structp pngp, png_bytep out_buf, png_size_t len)
{
	bufread_t *br = png_get_io_ptr(pngp);
	size_t to_read = MIN(br->len - br->cur, len);

	memcpy(out_buf, br->bufp + br->cur, to_read);
	br->cur += to_read;
}

uint8_t *
png_load_from_buffer(const void *buf, size_t len, int *width, int *height)
{
	bufread_t br = { .bufp = buf, .len = len, .cur = 0 };
	int color_type = PNG_COLOR_TYPE_RGBA;
	int bit_depth = 8;
	return (png_load_impl(bufread, &br, width, height,
	    &color_type, &bit_depth, B_FALSE, B_FALSE));
}

uint8_t *
png_load_from_buffer_rgb_auto(const void *buf, size_t len, int *width,
    int *height, int *color_type, int *bit_depth)
{
	bufread_t br = { .bufp = buf, .len = len, .cur = 0 };
	ASSERT(color_type != NULL);
	ASSERT(bit_depth != NULL);
	*color_type = -1;
	*bit_depth = -1;
	return (png_load_impl(bufread, &br, width, height,
	    color_type, bit_depth, B_FALSE, B_FALSE));
}

uint8_t *
png_load_from_buffer_cairo_argb32(const void *buf, size_t len,
    int *width, int *height)
{
	bufread_t br = { .bufp = buf, .len = len, .cur = 0 };
	int color_type = -1, bit_depth = -1;
	uint8_t *pixels;

	pixels = png_load_impl(bufread, &br, width, height, &color_type,
	    &bit_depth, B_TRUE, B_TRUE);
	if (bit_depth == 8 && color_type == PNG_COLOR_TYPE_RGBA) {
		return (pixels);
	} else {
		free(pixels);
		return (NULL);
	}
}

static bool_t
png_write_to_file_common(const char *filename, int width, int height,
    const uint8_t *data, int color_type, int bpp, int bit_depth)
{
	volatile bool_t result = B_FALSE;
	png_structp png_ptr = NULL;
	volatile png_infop info_ptr = NULL;
	FILE *fp = fopen(filename, "wb");

	if (fp == NULL) {
		logMsg("Error writing PNG file %s: %s", filename,
		    strerror(errno));
		goto out;
		return (B_FALSE);
	}
	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL,
	    NULL);
	if (png_ptr == NULL) {
		logMsg("Error writing PNG file %s: couldn't allocate "
		    "write struct", filename);
		goto out;
	}
	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		logMsg("Error writing PNG file %s: couldn't allocate "
		    "info struct", filename);
		goto out;
	}

	if (setjmp(png_jmpbuf(png_ptr))) {
		logMsg("Error writing PNG file %s: error during png creation",
		    filename);
		goto out;
	}

	png_init_io(png_ptr, fp);

	/* Write header (8/16 bit color depth) */
	png_set_IHDR(png_ptr, info_ptr, width, height, bit_depth, color_type,
	    PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE,
	    PNG_FILTER_TYPE_BASE);

	png_write_info(png_ptr, info_ptr);
	for (int i = 0; i < height; i++) {
		png_write_row(png_ptr, &data[i * width * bpp]);
	}
	png_write_end(png_ptr, NULL);

	result = B_TRUE;
out:
	if (info_ptr != NULL)
		png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
	if (png_ptr != NULL)
		png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
	if (fp != NULL)
		fclose(fp);

	return (result);
}

bool_t
png_write_to_file_grey8(const char *filename, int width, int height,
    const void *data)
{
	return (png_write_to_file_common(filename, width, height, data,
	    PNG_COLOR_TYPE_GRAY, 1, 8));
}

bool_t
png_write_to_file_grey16(const char *filename, int width, int height,
    const void *data)
{
	return (png_write_to_file_common(filename, width, height, data,
	    PNG_COLOR_TYPE_GRAY, 2, 16));
}

bool_t
png_write_to_file_rgba(const char *filename, int width, int height,
    const void *data)
{
	return (png_write_to_file_common(filename, width, height, data,
	    PNG_COLOR_TYPE_RGB_ALPHA, 4, 8));
}
