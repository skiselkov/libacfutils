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

#ifndef	_ACF_UTILS_MT_CAIRO_RENDER_H_
#define	_ACF_UTILS_MT_CAIRO_RENDER_H_

#include <GL/glew.h>

#include <cairo.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <acfutils/geom.h>

#ifdef	__cplusplus
extern "C" {
#endif

API_EXPORT void mt_cairo_render_glob_init(void);

typedef bool_t (*mt_cairo_init_cb_t)(cairo_t *cr, void *userinfo);
typedef void (*mt_cairo_fini_cb_t)(cairo_t *cr, void *userinfo);
typedef void (*mt_cairo_render_cb_t)(cairo_t *cr, unsigned w, unsigned h,
    void *userinfo);
typedef struct mt_cairo_render_s mt_cairo_render_t;

API_EXPORT mt_cairo_render_t *mt_cairo_render_init(unsigned w, unsigned h,
    double fps, mt_cairo_init_cb_t init_cb, mt_cairo_render_cb_t render_cb,
    mt_cairo_fini_cb_t fini_cb, void *userinfo);
API_EXPORT void mt_cairo_render_fini(mt_cairo_render_t *mtcr);
API_EXPORT void mt_cairo_render_set_fps(mt_cairo_render_t *mtcr, double fps);
API_EXPORT double mt_cairo_render_get_fps(mt_cairo_render_t *mtcr);
API_EXPORT void mt_cairo_render_once(mt_cairo_render_t *mtcr);
API_EXPORT void mt_cairo_render_once_wait(mt_cairo_render_t *mtcr);
API_EXPORT void mt_cairo_render_draw(mt_cairo_render_t *mtcr, vect2_t pos,
    vect2_t size);
API_EXPORT void mt_cairo_render_draw_subrect(mt_cairo_render_t *mtcr,
    vect2_t src_pos, vect2_t src_sz, vect2_t pos, vect2_t size);
API_EXPORT GLuint mt_cairo_render_get_tex(mt_cairo_render_t *mtcr);

#define	ft_err2str	ACFSYM(ft_err2str)
API_EXPORT const char *ft_err2str(FT_Error err);
#define	try_load_font	ACFSYM(try_load_font)
API_EXPORT bool_t try_load_font(const char *fontdir, const char *fontfile,
    FT_Library ft, FT_Face *font, cairo_font_face_t **cr_font);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_MT_CAIRO_RENDER_H_ */
