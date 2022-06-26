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
 * Copyright 2019 Saso Kiselkov. All rights reserved.
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

#define	ft_err2str	ACFSYM(ft_err2str)
API_EXPORT const char *ft_err2str(FT_Error err);
#define	try_load_font	ACFSYM(try_load_font)
API_EXPORT bool_t try_load_font(const char *fontdir, const char *fontfile,
    FT_Library ft, FT_Face *font, cairo_font_face_t **cr_font);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_FONT_UTILS_H_ */
