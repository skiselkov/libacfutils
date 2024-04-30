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

#include <XPLMGraphics.h>

#include "acfutils/dr.h"
#include "acfutils/geom.h"
#include "acfutils/glew.h"
#include "acfutils/glutils.h"
#include "acfutils/png.h"
#include "acfutils/shader.h"

#include "acfutils/lacf_gl_pic.h"

#define	LACF_GL_PIC_CACHE_SIZE	(1 << 10)	/* 1 KiB */

struct lacf_gl_pic_s {
	char		*path;
	GLuint		tex;
	uint8_t		*pixels;
	int		w, h;
	double		in_use;
	glutils_cache_t	*cache;
	dr_t		proj_matrix;
	dr_t		mv_matrix;
	GLuint		shader;
};

static const char *vert_shader =
    "#version 120\n"
    "uniform mat4       pvm;\n"
    "attribute vec3     vtx_pos;\n"
    "attribute vec2     vtx_tex0;\n"
    "varying vec2       tex_coord;\n"
    "void main() {\n"
    "   tex_coord = vtx_tex0;\n"
    "   gl_Position = pvm * vec4(vtx_pos, 1.0);\n"
    "}\n";

static const char *frag_shader =
    "#version 120\n"
    "uniform sampler2D  tex;\n"
    "uniform float      alpha;\n"
    "varying vec2       tex_coord;\n"
    "void main() {\n"
    "   gl_FragColor = texture2D(tex, tex_coord);\n"
    "   gl_FragColor.a *= alpha;\n"
    "}\n";

static bool_t
load_image(lacf_gl_pic_t *pic)
{
	uint8_t *buf;

	ASSERT(pic != NULL);
	ASSERT0(pic->tex);
	ASSERT(pic->path != NULL);

	buf = png_load_from_file_rgba(pic->path, &pic->w, &pic->h);
	if (buf == NULL)
		return (B_FALSE);

	glGenTextures(1, &pic->tex);
	ASSERT(pic->tex != 0);
	glBindTexture(GL_TEXTURE_2D, pic->tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pic->w, pic->h, 0, GL_RGBA,
	    GL_UNSIGNED_BYTE, buf);
	lacf_free(buf);

	return (B_TRUE);
}

/**
 * Initializes a new gl_pic_t with a PNG image file on disk.
 * @note This doesn't perform any disk I/O. gl_pic_t's are lazy-loaded
 *	on the first attempt to draw. If you want to pre-load a gl_pic_t
 *	ahead of time, use lacf_gl_pic_load().
 * @return A newly allocated gl_pic_t ready for drawing. Use
 *	gl_pic_destroy() to free the image after you are done with it.
 */
lacf_gl_pic_t *
lacf_gl_pic_new(const char *path)
{
	lacf_gl_pic_t *pic = safe_calloc(1, sizeof (*pic));

	ASSERT(path != NULL);
	pic->path = safe_strdup(path);
	fdr_find(&pic->proj_matrix, "sim/graphics/view/projection_matrix");
	fdr_find(&pic->mv_matrix, "sim/graphics/view/modelview_matrix");

	return (pic);
}

/**
 * This is a convenience front-end function to lacf_gl_pic_new(), which
 * lets you provide a containing directory and image filename separately.
 * This can be useful when the directory is subject to change, but the
 * filename isn't. The function concatenates the path components before
 * passing them on to lacf_gl_pic_new().
 * @see lacf_gl_pic_new()
 */
lacf_gl_pic_t *
lacf_gl_pic_new_from_dir(const char *dirpath, const char *filename)
{
	char *path;
	lacf_gl_pic_t *pic;

	ASSERT(dirpath != NULL);
	ASSERT(filename != NULL);
	path = mkpathname(dirpath, filename, NULL);
	pic = lacf_gl_pic_new(path);
	lacf_free(path);

	return (pic);
}

/**
 * Destroys and frees the memory associated with a gl_pic_t which was
 * previously returned by lacf_gl_pic_new() or lacf_gl_pic_new_from_dir().
 */
void
lacf_gl_pic_destroy(lacf_gl_pic_t *pic)
{
	if (pic == NULL)
		return;
	lacf_gl_pic_unload(pic);
	free(pic->path);
	ZERO_FREE(pic);
}

/**
 * A newly initialized gl_pic_t is normally lazy-loaded and no disk I/O
 * is performed until the image is to be drawn. This function allows you
 * to pre-load the image into VRAM before it is needed.
 * @return `B_TRUE` if loading the image was successful (or the image was
 *	loaded already). `B_FALSE` if loading the image failed, either
 *	due to disk I/O or an issue with the image format on disk.
 */
bool_t
lacf_gl_pic_load(lacf_gl_pic_t *pic)
{
	ASSERT(pic != NULL);
	if (pic->tex != 0)
		return (B_TRUE);
	return (load_image(pic));
}

/**
 * If the image was loaded, unloads the image and frees GPU-side VRAM
 * buffers. This can be used to reduce the memory footprint of images
 * you don't plan to use for a longer time. Do **not** call this
 * function just between each frame. If you plan on drawing the image
 * repeatedly, just keep it loaded. If you do not plan to draw the
 * image for a long time, unloading it can save on VRAM usage.
 *
 * If the image was unloaded already, this function does nothing.
 */
void
lacf_gl_pic_unload(lacf_gl_pic_t *pic)
{
	ASSERT(pic != NULL);

	if (pic->tex != 0) {
		glDeleteTextures(1, &pic->tex);
		pic->tex = 0;
	}
	if (pic->cache != NULL) {
		glutils_cache_destroy(pic->cache);
		pic->cache = NULL;
	}
	if (pic->shader != 0) {
		glDeleteProgram(pic->shader);
		pic->shader = 0;
	}
}

/**
 * @return The pixel width of the image. This may need to perform disk
 *	I/O to load the image and determine its dimensions. If loading
 *	the image failed, returns 0.
 */
int
lacf_gl_pic_get_width(lacf_gl_pic_t *pic)
{
	ASSERT(pic != NULL);
	if (pic->w == 0)
		load_image(pic);
	return (pic->w);
}

/**
 * @return The pixel height of the image. This may need to perform disk
 *	I/O to load the image and determine its dimensions. If loading
 *	the image failed, returns 0.
 */
int
lacf_gl_pic_get_height(lacf_gl_pic_t *pic)
{
	ASSERT(pic != NULL);
	if (pic->h == 0)
		load_image(pic);
	return (pic->h);
}

/**
 * @return The texture holding the image data on the GPU. This may need
 *	to perform disk I/O to load the image and upload it to the GPU.
 *	If loading the image failed, returns 0.
 */
GLuint
lacf_gl_pic_get_tex(lacf_gl_pic_t *pic)
{
	ASSERT(pic != NULL);
	if (pic->tex == 0) {
		load_image(pic);
	}
	return (pic->tex);
}

/**
 * Draws the image using the current X-Plane projection and modelview
 * matrices. This makes it possible to use to draw either gauges into
 * the panel texture, or windows during a window draw callback.
 *
 * @param pos Position of the lower left corner of the image relative
 *	to the coordinate system origin.
 * @param size The size of the image for drawing. You can pass
 *	NULL_VECT2 here to make the image draw using its native size.
 * @param alpha Floating point value 0-1 for partial alpha compositing.
 *	This is passed to the fragment shader in a uniform, to let the
 *	fragment shader perform partial alpha blending.
 */
void
lacf_gl_pic_draw(lacf_gl_pic_t *pic, vect2_t pos, vect2_t size,
    float alpha)
{
	mat4 proj_matrix, mv_matrix, pvm;

	ASSERT(pic != NULL);

	VERIFY3F(dr_getvf32(&pic->proj_matrix, (float *)proj_matrix, 0, 16),
	    ==, 16);
	VERIFY3F(dr_getvf32(&pic->mv_matrix, (float *)mv_matrix, 0, 16),
	    ==, 16);
	glm_mat4_mul(proj_matrix, mv_matrix, pvm);

	if (pic->shader == 0) {
		pic->shader = shader_prog_from_text("lacf_gl_pic_shader",
		    vert_shader, frag_shader,
		    "vtx_pos", VTX_ATTRIB_POS,
		    "vtx_tex0", VTX_ATTRIB_TEX0, NULL);
		ASSERT(pic->shader != 0);
	}
	glUseProgram(pic->shader);
	glUniform1f(glGetUniformLocation(pic->shader, "alpha"), alpha);
	glUniformMatrix4fv(glGetUniformLocation(pic->shader, "pvm"),
	    1, GL_FALSE, (const GLfloat *)pvm);
	lacf_gl_pic_draw_custom(pic, pos, size, pic->shader);
	glUseProgram(0);
}

/**
 * Draws the image using a custom OpenGL program. You can use this
 * function to perform custom image compositing using your own shaders.
 *
 * @param pos Position of the lower left corner of the image relative
 *	to the coordinate system origin.
 * @param size The size of the image for drawing. You can pass
 *	NULL_VECT2 here to make the image draw using its native size.
 * @param prog The OpenGL program to use for drawing. You **must** bind
 *	this program before calling this function using glUseProgram().
 *	The program must take the same inputs as glutils_draw_quads()
 *	for vertex positions.
 */
void
lacf_gl_pic_draw_custom(lacf_gl_pic_t *pic, vect2_t pos, vect2_t size,
    GLuint prog)
{
	glutils_quads_t *quads;
	vect2_t p[4];
	const vect2_t t[4] = {
	    VECT2(0, 1), VECT2(0, 0), VECT2(1, 0), VECT2(1, 1)
	};

	ASSERT(pic != NULL);

	if (pic->tex == 0 && !load_image(pic))
		return;
	if (IS_NULL_VECT(size))
		size = VECT2(pic->w, pic->h);
	if (pic->cache == NULL)
		pic->cache = glutils_cache_new(LACF_GL_PIC_CACHE_SIZE);

	p[0] = VECT2(pos.x, pos.y);
	p[1] = VECT2(pos.x, pos.y + size.y);
	p[2] = VECT2(pos.x + size.x, pos.y + size.y);
	p[3] = VECT2(pos.x + size.x, pos.y);
	quads = glutils_cache_get_2D_quads(pic->cache, p, t, 4);

	XPLMBindTexture2d(pic->tex, 0);
	glUniform1i(glGetUniformLocation(prog, "vtx_tex0"), 0);
	glutils_draw_quads(quads, prog);
	XPLMBindTexture2d(0, 0);
}
