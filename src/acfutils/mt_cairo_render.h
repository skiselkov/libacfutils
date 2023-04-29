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

#ifndef	_ACF_UTILS_MT_CAIRO_RENDER_H_
#define	_ACF_UTILS_MT_CAIRO_RENDER_H_

#include <cairo.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "geom.h"
#include "glew.h"
#include "log.h"
#include "font_utils.h"		/* for backwards compat with apps */
#include "cairo_utils.h"	/* for backwards compat with apps */

#ifdef	__cplusplus
extern "C" {
#endif

API_EXPORT void mt_cairo_render_glob_init(bool_t want_coherent_mem);

typedef bool_t (*mt_cairo_init_cb_t)(cairo_t *cr, void *userinfo);
typedef void (*mt_cairo_fini_cb_t)(cairo_t *cr, void *userinfo);
typedef void (*mt_cairo_render_cb_t)(cairo_t *cr, unsigned w, unsigned h,
    void *userinfo);
typedef struct mt_cairo_render_s mt_cairo_render_t;
typedef struct mt_cairo_uploader_s mt_cairo_uploader_t;

#define	mt_cairo_render_init(__w, __h, __fps, __init_cb, __render_cb, \
    __fini_cb, __userinfo) \
	mt_cairo_render_init_impl(log_basename(__FILE__), __LINE__, \
	    (__w), (__h), (__fps), (__init_cb), (__render_cb), \
	    (__fini_cb), (__userinfo))
API_EXPORT mt_cairo_render_t *mt_cairo_render_init_impl(const char *filename,
    int line, unsigned w, unsigned h, double fps, mt_cairo_init_cb_t init_cb,
    mt_cairo_render_cb_t render_cb, mt_cairo_fini_cb_t fini_cb, void *userinfo);

API_EXPORT void mt_cairo_render_fini(mt_cairo_render_t *mtcr);
API_EXPORT void mt_cairo_render_set_fps(mt_cairo_render_t *mtcr, double fps);
API_EXPORT double mt_cairo_render_get_fps(mt_cairo_render_t *mtcr);
API_EXPORT void mt_cairo_render_enable_fg_mode(mt_cairo_render_t *mtcr);
API_EXPORT void mt_cairo_render_set_texture_filter(mt_cairo_render_t *mtcr,
    unsigned gl_filter_enum);
API_EXPORT void mt_cairo_render_set_shader(mt_cairo_render_t *mtcr,
    unsigned prog);
unsigned mt_cairo_render_get_shader(mt_cairo_render_t *mtcr);
API_EXPORT void mt_cairo_render_set_monochrome(mt_cairo_render_t *mtcr,
    vect3_t color);
API_EXPORT vect3_t mt_cairo_render_get_monochrome(
    const mt_cairo_render_t *mtcr);
API_EXPORT void mt_cairo_render_once(mt_cairo_render_t *mtcr);
API_EXPORT void mt_cairo_render_once_wait(mt_cairo_render_t *mtcr);

API_EXPORT void mt_cairo_render_draw(mt_cairo_render_t *mtcr, vect2_t pos,
    vect2_t size);
API_EXPORT void mt_cairo_render_draw_subrect(mt_cairo_render_t *mtcr,
    vect2_t src_pos, vect2_t src_sz, vect2_t pos, vect2_t size);
API_EXPORT void mt_cairo_render_draw_pvm(mt_cairo_render_t *mtcr, vect2_t pos,
    vect2_t size, const GLfloat *pvm);
API_EXPORT void mt_cairo_render_draw_subrect_pvm(mt_cairo_render_t *mtcr,
    vect2_t src_pos, vect2_t src_sz, vect2_t pos, vect2_t size,
    const GLfloat *pvm);
API_EXPORT void mt_cairo_render_set_uploader(mt_cairo_render_t *mtcr,
    mt_cairo_uploader_t *mtul);
API_EXPORT mt_cairo_uploader_t *mt_cairo_render_get_uploader(
    mt_cairo_render_t *mtcr);

API_EXPORT GLuint mt_cairo_render_get_tex(mt_cairo_render_t *mtcr);
API_EXPORT unsigned mt_cairo_render_get_width(mt_cairo_render_t *mtcr);
API_EXPORT unsigned mt_cairo_render_get_height(mt_cairo_render_t *mtcr);
API_EXPORT void mt_cairo_render_set_ctx_checking_enabled(
    mt_cairo_render_t *mtcr, bool_t flag);

API_EXPORT void mt_cairo_render_set_debug(mt_cairo_render_t *mtcr, bool_t flag);
API_EXPORT bool_t mt_cairo_render_get_debug(const mt_cairo_render_t *mtcr);

/*
 * Must ONLY be called from the rendering callback.
 */
typedef struct {
	unsigned	x, y;
	unsigned	w, h;
} mtcr_rect_t;

API_EXPORT void mt_cairo_render_rounded_rectangle(cairo_t *cr, double x,
    double y, double w, double h, double radius);

API_EXPORT mt_cairo_uploader_t *mt_cairo_uploader_init(void);
API_EXPORT void mt_cairo_uploader_fini(mt_cairo_uploader_t *mtul);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_MT_CAIRO_RENDER_H_ */
