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
 * A simple subsystem that provides the ability to load and draw PNG
 * images in an OpenGL context.
 *
 * Use lacf_gl_pic_new() or lacf_gl_pic_new_from_dir() to load a gl_pic
 * object from disk. When done with the image, use lacf_gl_pic_destroy()
 * to free all resources associated with the pic.
 *
 * To draw the image, use lacf_gl_pic_draw() or lacf_gl_pic_draw_custom().
 */

#ifndef	_LIBACFUTILS_GL_PIC_H_
#define	_LIBACFUTILS_GL_PIC_H_

#include "core.h"
#include "geom.h"
#include "glew.h"

#include <cglm/cglm.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lacf_gl_pic_s lacf_gl_pic_t;

API_EXPORT lacf_gl_pic_t *lacf_gl_pic_new(const char *path);
API_EXPORT lacf_gl_pic_t *lacf_gl_pic_new_from_dir(const char *dirpath,
    const char *filename);
API_EXPORT void lacf_gl_pic_destroy(lacf_gl_pic_t *pic);

API_EXPORT bool_t lacf_gl_pic_load(lacf_gl_pic_t *pic);
API_EXPORT void lacf_gl_pic_unload(lacf_gl_pic_t *pic);

API_EXPORT int lacf_gl_pic_get_width(lacf_gl_pic_t *pic);
API_EXPORT int lacf_gl_pic_get_height(lacf_gl_pic_t *pic);

API_EXPORT void lacf_gl_pic_draw(lacf_gl_pic_t *pic, vect2_t pos,
    vect2_t size, float alpha);
API_EXPORT void lacf_gl_pic_draw_custom(lacf_gl_pic_t *pic, vect2_t pos,
    vect2_t size, GLuint prog);

#ifdef __cplusplus
}
#endif

#endif	/* _LIBACFUTILS_GLPIC_H_ */
