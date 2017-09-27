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

#include "acfutils/assert.h"
#include "acfutils/log.h"
#include "acfutils/png.h"

/*
 * This is a simplified PNG file loading routine that avoids having to deal
 * with all that libpng nonsense.
 */
uint8_t *
png_load_from_file_rgba(const char *filename, int *width, int *height)
{
	FILE *fp;
	size_t rowbytes;
	png_bytep *volatile rowp = NULL;
	png_structp pngp = NULL;
	png_infop infop = NULL;
	uint8_t header[8];
	uint8_t *volatile pixels = NULL;
	volatile int w, h;

	fp = fopen(filename, "rb");
	if (fp == NULL) {
		logMsg("Cannot open file %s: %s", filename, strerror(errno));
		goto out;
	}
	if (fread(header, 1, sizeof (header), fp) != 8 ||
	    png_sig_cmp(header, 0, sizeof (header)) != 0) {
		logMsg("Cannot open file %s: invalid PNG header", filename);
		goto out;
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
	png_init_io(pngp, fp);
	png_set_sig_bytes(pngp, 8);

	if (setjmp(png_jmpbuf(pngp))) {
		logMsg("Cannot open file %s: libpng read info failed",
		    filename);
		goto out;
	}
	png_read_info(pngp, infop);
	w = png_get_image_width(pngp, infop);
	h = png_get_image_height(pngp, infop);

	if (png_get_color_type(pngp, infop) != PNG_COLOR_TYPE_RGBA) {
		logMsg("Bad image file %s: need color type RGBA", filename);
		goto out;
	}
	if (png_get_bit_depth(pngp, infop) != 8) {
		logMsg("Bad icon file %s: need 8-bit depth", filename);
		goto out;
	}
	rowbytes = png_get_rowbytes(pngp, infop);

	rowp = malloc(sizeof (*rowp) * h);
	VERIFY(rowp != NULL);
	for (int i = 0; i < h; i++) {
		rowp[i] = malloc(rowbytes);
		VERIFY(rowp[i] != NULL);
	}

	if (setjmp(png_jmpbuf(pngp))) {
		logMsg("Bad icon file %s: error reading image file", filename);
		goto out;
	}
	png_read_image(pngp, rowp);
	pixels = malloc(h * rowbytes);
	for (int i = 0; i < h; i++)
		memcpy(&pixels[i * rowbytes], rowp[i], rowbytes);

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
