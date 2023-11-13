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
 * `mt_cairo_render` is a multi-threaded [Cairo](https://www.cairographics.org/)
 * rendering surface with built-in buffering and OpenGL compositing.
 * You only need to provide a callback that renders into the surface
 * using a passed `cairo_t` and then call mt_cairo_render_draw() at
 * regular intervals to display the rendered result.
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

/**
 * An optional initialization callback which can be passed to
 * mt_cairo_render_init() in the init_cb argument. This callback can be
 * used to allocate custom resources for each surface, or to configure
 * the default state of the renderer. This will be called for each
 * surface that is being deinitialized and may be called multiple times
 * during surface reinit.
 * @param cr The `cairo_t` for which this callback is being invoked.
 * @param userinfo The argument passed to mt_cairo_render_init() in the
 *	`userinfo` argument.
 * @return `B_TRUE` if you want initialization to proceed, or `B_FALSE`
 *	if you need it to abort. Please note that returning `B_FALSE`
 *	is only allowed during mt_cairo_render_init(), as that's the
 *	only time the renderer can abort cleanly. You must NOT return
 *	`B_FALSE` on subsequent surface reinit operations (this only
 *	happens if mt_cairo_render_set_monochrome() is called).
 */
typedef bool_t (*mt_cairo_init_cb_t)(cairo_t *cr, void *userinfo);
/**
 * An optional finalization callback which can be passed to
 * mt_cairo_render_init() in the fini_cb argument. This callback can be
 * used to free resources allocated during initialization. This will be
 * called for each surface that is being deinitialized and may be called
 * multiple times during surface reinit.
 * @param cr The `cairo_t` for which this callback is being invoked.
 * @param userinfo The argument passed to mt_cairo_render_init() in the
 *	`userinfo` argument.
 */
typedef void (*mt_cairo_fini_cb_t)(cairo_t *cr, void *userinfo);
/**
 * Mandatory rendering callback, which must be provided to
 * mt_cairo_render_init() in the `render_cb` argument. This will be called
 * every time a new frame is to be rendered.
 * @param cr The cairo_t into which you should perform rendering.
 *	The renderer is not reinitialized between frames and no blanking
 *	of the rendered image is performed automatically, so if you want
 *	redraw the entire contents, perform your own screen clearing first.
 * @param w The width of the rendering surface in pixels.
 * @param h The height of the rendering surface in pixels.
 * @param userinfo The argument passed to mt_cairo_render_init() in the
 *	`userinfo` argument.
 */
typedef void (*mt_cairo_render_cb_t)(cairo_t *cr, unsigned w, unsigned h,
    void *userinfo);
typedef struct mt_cairo_render_s mt_cairo_render_t;
typedef struct mt_cairo_uploader_s mt_cairo_uploader_t;

/**
 * Creates a new mt_cairo_render_t surface.
 * @param w Width of the rendered surface (in pixels).
 * @param h Height of the rendered surface (in pixels).
 * @param fps Framerate at which the surface should be rendered.
 *	This can be changed at any time later. Pass a zero fps value
 *	to make the renderer only run on-request (see mt_cairo_render_once()).
 * @param init_cb An optional initialization callback of type
 *	mt_cairo_init_cb_t. This can be used to initialize private resources
 *	needed during rendering. This gets called once for every cairo_t
 *	instance (two instances for every mt_cairo_render_t, due to
 *	double-buffering). Please note that this can be called even after
 *	calling mt_cairo_render_init, since mt_cairo_render_set_monochrome()
 *	will re-allocate the cairo instances. If you don't want to receive
 *	this callback, pass NULL here.
 * @param render_cb A mandatory rendering callback of type
 *	mt_cairo_render_cb_t. This is called for each rendered frame.
 * @param fini_cb An optional finalization callback of type
 *	mt_cairo_fini_cb_t, which can be used to free resources allocated
 *	during init_cb. Similarly to `init_cb` above, this will be called
 *	for each surface that is being deinitialized and may be called
 *	multiple times during surface reinit. If you don't want to receive
 *	this callback, pass NULL here.
 * @param userinfo An optional user info pointer for the `init_cb`,
 *	`render_cb` and `fini_cb` callbacks.
 * @return The initialized and started mt_cairo_render_t instance.
 *	Returns NULL if initialization failed.
 */
#define	mt_cairo_render_init(w, h, fps, init_cb, render_cb, fini_cb, userinfo) \
	mt_cairo_render_init_impl(log_basename(__FILE__), __LINE__, \
	    (w), (h), (fps), (init_cb), (render_cb), (fini_cb), (userinfo))
API_EXPORT mt_cairo_render_t *mt_cairo_render_init_impl(const char *filename,
    int line, unsigned w, unsigned h, double fps, mt_cairo_init_cb_t init_cb,
    mt_cairo_render_cb_t render_cb, mt_cairo_fini_cb_t fini_cb, void *userinfo);

API_EXPORT void mt_cairo_render_fini(mt_cairo_render_t *mtcr);
API_EXPORT void mt_cairo_render_set_fps(mt_cairo_render_t *mtcr, double fps);
API_EXPORT double mt_cairo_render_get_fps(mt_cairo_render_t *mtcr);
API_EXPORT void mt_cairo_render_enable_fg_mode(mt_cairo_render_t *mtcr);
API_EXPORT bool_t mt_cairo_render_get_fg_mode(const mt_cairo_render_t *mtcr);
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
typedef struct {
	unsigned	x, y;
	unsigned	w, h;
} mtcr_rect_t;
API_EXPORT void mt_cairo_render_blit_back2front(mt_cairo_render_t *mtcr,
    const mtcr_rect_t *rects, size_t num);

#ifdef	LACF_MTCR_DEBUG
API_EXPORT void mt_cairo_render_set_ctx_checking_enabled(
    mt_cairo_render_t *mtcr, bool_t flag);
#endif	/* defined(LACF_MTCR_DEBUG) */

/*
 * Must ONLY be called from the rendering callback.
 */
API_EXPORT void mt_cairo_render_rounded_rectangle(cairo_t *cr, double x,
    double y, double w, double h, double radius);

API_EXPORT mt_cairo_uploader_t *mt_cairo_uploader_init(void);
API_EXPORT void mt_cairo_uploader_fini(mt_cairo_uploader_t *mtul);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_MT_CAIRO_RENDER_H_ */
