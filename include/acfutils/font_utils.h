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
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */
/**
 * \file
 * This file contains support utilities to help with font handling
 * and Cairo font operations.
 */

#ifndef	_ACF_UTILS_FONT_UTILS_H_
#define	_ACF_UTILS_FONT_UTILS_H_

#include <cairo.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

/** For backwards compatibility with older code */
#define	ft_err2str	font_utils_ft_err2str
/**
 * Translates an `FT_Error` error code into a human-readable string.
 */
API_EXPORT const char *ft_err2str(FT_Error err);

/** For backwards compatibility with older code */
#define	try_load_font	font_utils_try_load_font

/**
 * Simple font loading front-end.
 *
 * @param fontdir A path to the directory from which to load the font.
 * @param fontfile A font file name. This is concatenated onto the fontdir
 *	with a path separator. If you only want to provide one string with
 *	a full path to the font file, pass that in fontdir and set
 *	fontfile = NULL.
 * @param ft FreeType library handle.
 * @param font Return FreeType font face object pointer. Release this after
 *	the cairo font face object using FT_DoneFace.
 * @param cr_font Return cairo font face object pointer. Release this before
 *	the freetype font face using cairo_font_face_destroy.
 *
 * @return `B_TRUE` if loading the font was successfull, `B_FALSE` otherwise.
 *	In case of error, the reason is logged using logMsg.
 */
API_EXPORT bool_t font_utils_try_load_font(const char *fontdir,
    const char *fontfile, FT_Library ft, FT_Face *font,
    cairo_font_face_t **cr_font);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_FONT_UTILS_H_ */
