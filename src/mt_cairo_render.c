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
/*
 * mt_cairo_render is a multi-threaded cairo rendering surface with
 * built-in double-buffering and OpenGL compositing. You only need to
 * provide a callback that renders into the surface using a passed
 * cairo_t and then call mt_cairo_render_draw at regular intervals to
 * display the rendered result.
 */

#include <XPLMGraphics.h>

#include <acfutils/assert.h>
#include <acfutils/dr.h>
#include <acfutils/geom.h>
#include <acfutils/glctx.h>
#include <acfutils/glew.h>
#include <acfutils/mt_cairo_render.h>
#include <acfutils/safe_alloc.h>
#include <acfutils/glutils.h>
#include <acfutils/shader.h>
#include <acfutils/thread.h>
#include <acfutils/time.h>

#ifdef	_USE_MATH_DEFINES
#undef	_USE_MATH_DEFINES
#endif

#if	IBM
#ifdef	__SSE2__
#undef	__SSE2__
#endif
#endif	/* IBM */

#include <cglm/cglm.h>

#define	MTCR_DEBUG(...) \
	do { \
		if (mtcr->debug) \
			logMsg(__VA_ARGS__); \
	} while (0)

TEXSZ_MK_TOKEN(mt_cairo_render_tex);
TEXSZ_MK_TOKEN(mt_cairo_render_pbo);

typedef struct {
	GLfloat		pos[3];
	GLfloat		tex0[2];
} vtx_t;

typedef	struct {
	bool_t		chg;

	GLuint		tex;
	cairo_surface_t	*surf;
	cairo_t		*cr;
} render_surf_t;

struct mt_cairo_render_s {
	char			*init_filename;
	int			init_line;
	bool_t			debug;

	GLenum			tex_filter;
	unsigned		w, h;
	double			fps;
	mt_cairo_render_cb_t	render_cb;
	mt_cairo_fini_cb_t	fini_cb;
	void			*userinfo;

	int			cur_rs;
	render_surf_t		rs[2];
	GLuint			pbo;

	thread_t		thr;
	condvar_t		cv;
	condvar_t		render_done_cv;
	bool_t			one_shot_block;
	mutex_t			lock;
	bool_t			started;
	bool_t			shutdown;

	/* Only accessed from OpenGL drawing thread, so no locking req'd */
	struct {
		double		x1, x2, y1, y2;
		vect2_t		pos;
		vect2_t		size;
	} last_draw;
	GLuint			vao;
	GLuint			vtx_buf;
	GLuint			idx_buf;
	GLuint			shader;
	GLint			shader_loc_pvm;
	GLint			shader_loc_tex;

	bool_t			ctx_checking;
	glctx_t			*create_ctx;
};

static const char *vert_shader =
    "#version 120\n"
    "uniform mat4	pvm;\n"
    "attribute vec3	vtx_pos;\n"
    "attribute vec2	vtx_tex0;\n"
    "varying vec2	tex_coord;\n"
    "void main() {\n"
    "	tex_coord = vtx_tex0;\n"
    "	gl_Position = pvm * vec4(vtx_pos, 1.0);\n"
    "}\n";

static const char *frag_shader =
    "#version 120\n"
    "uniform sampler2D	tex;\n"
    "varying vec2	tex_coord;\n"
    "void main() {\n"
    "	gl_FragColor = texture2D(tex, tex_coord);\n"
    "}\n";

static const char *vert_shader410 =
    "#version 410\n"
    "uniform mat4			pvm;\n"
    "layout(location = %d) in vec3	vtx_pos;\n"
    "layout(location = %d) in vec2	vtx_tex0;\n"
    "layout(location = 0) out vec2	tex_coord;\n"
    "void main() {\n"
    "	tex_coord = vtx_tex0;\n"
    "	gl_Position = pvm * vec4(vtx_pos, 1.0);\n"
    "}\n";

static const char *frag_shader410 =
    "#version 410\n"
    "uniform sampler2D			tex;\n"
    "layout(location = 0) in vec2	tex_coord;\n"
    "layout(location = 0) out vec4	color_out;\n"
    "void main() {\n"
    "	color_out = texture(tex, tex_coord);\n"
    "}\n";

/*
 * This weird macro construct is needed to implement freetype error code
 * to string translation. It defines a static ft_errors table that we can
 * traverse to translate an error code into a string.
 */
#undef	FTERRORS_H_
#define	FT_ERRORDEF(e, v, s)	{ e, s },
#define	FT_ERROR_START_LIST	{
#define	FT_ERROR_END_LIST	{ 0, NULL } };
static const struct {
	int		err_code;
	const char	*err_msg;
} ft_errors[] =
#include FT_ERRORS_H

static bool_t glob_inited = B_FALSE;
static thread_id_t mtcr_main_thread;

static struct {
	dr_t	viewport;
	dr_t	proj_matrix;
	dr_t	mv_matrix;
	dr_t	draw_call_type;
} drs;

const char *
ft_err2str(FT_Error err)
{
	for (int i = 0; ft_errors[i].err_msg != NULL; i++)
		if (ft_errors[i].err_code == err)
			return (ft_errors[i].err_msg);
	return (NULL);
}

/*
 * Main mt_cairo_render_t worker thread. Simply waits around for the
 * required interval and fires off the rendering callback. This performs
 * no canvas clearing between calls, so the callback is responsible for
 * making sure its output canvas looks right.
 */
static void
worker(mt_cairo_render_t *mtcr)
{
	char name[32];
	char shortname[7];

	strlcpy(shortname, mtcr->init_filename, sizeof (shortname));
	snprintf(name, sizeof (name), "mtcr:%s:%d", shortname, mtcr->init_line);
	thread_set_name(name);

	/*
	 * Render the first frame immediately to make sure we have
	 * something to show ASAP.
	 */
	ASSERT(mtcr->render_cb != NULL);
	mtcr->render_cb(mtcr->rs[0].cr, mtcr->w, mtcr->h, mtcr->userinfo);
	mtcr->rs[0].chg = B_TRUE;

	mutex_enter(&mtcr->lock);
	mtcr->cur_rs = 0;

	while (!mtcr->shutdown) {
		render_surf_t *rs;

		if (!mtcr->one_shot_block) {
			if (mtcr->fps > 0) {
				cv_timedwait(&mtcr->cv, &mtcr->lock,
				    microclock() + SEC2USEC(1.0 / mtcr->fps));
			} else {
				cv_wait(&mtcr->cv, &mtcr->lock);
			}
		}
		if (mtcr->shutdown)
			break;
		mutex_exit(&mtcr->lock);

		/* always draw into the non-current texture */
		rs = &mtcr->rs[!mtcr->cur_rs];

		ASSERT(mtcr->render_cb != NULL);
		mtcr->render_cb(rs->cr, mtcr->w, mtcr->h, mtcr->userinfo);
		rs->chg = B_TRUE;

		mutex_enter(&mtcr->lock);
		mtcr->cur_rs = !mtcr->cur_rs;
		cv_broadcast(&mtcr->render_done_cv);
	}
	mutex_exit(&mtcr->lock);
}

/*
 * Initializes the mt_cairo_render state. You should call this before doing
 * *ANY* Cairo or mt_cairo_render calls. Notably, the font machinery of
 * Cairo isn't thread-safe before this call, so place a call to this function
 * at the start of your bootstrap code.
 */
void
mt_cairo_render_glob_init(void)
{
	if (glob_inited)
		return;
	cairo_surface_destroy(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
	    1, 1));
	mtcr_main_thread = curthread;
	fdr_find(&drs.viewport, "sim/graphics/view/viewport");
	fdr_find(&drs.proj_matrix, "sim/graphics/view/projection_matrix");
	fdr_find(&drs.mv_matrix, "sim/graphics/view/modelview_matrix");
	fdr_find(&drs.draw_call_type, "sim/graphics/view/draw_call_type");
	glob_inited = B_TRUE;
}

static void
setup_vao(mt_cairo_render_t *mtcr)
{
	GLint old_vao = 0;
	bool_t on_main_thread = (curthread == mtcr_main_thread);

	if (GLEW_VERSION_3_0 && !on_main_thread) {
		glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vao);

		glGenVertexArrays(1, &mtcr->vao);
		glBindVertexArray(mtcr->vao);
	}

	glGenBuffers(1, &mtcr->vtx_buf);

	if (GLEW_VERSION_3_0 && !on_main_thread) {
		glBindBuffer(GL_ARRAY_BUFFER, mtcr->vtx_buf);
		glutils_enable_vtx_attr_ptr(VTX_ATTRIB_POS, 3, GL_FLOAT,
		    GL_FALSE, sizeof (vtx_t), offsetof(vtx_t, pos));
		glutils_enable_vtx_attr_ptr(VTX_ATTRIB_TEX0, 2, GL_FLOAT,
		    GL_FALSE, sizeof (vtx_t), offsetof(vtx_t, tex0));
	}

	mtcr->idx_buf = glutils_make_quads_IBO(4);
	if (GLEW_VERSION_3_0 && !on_main_thread)
		glBindVertexArray(old_vao);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

/*
 * Creates a new mt_cairo_render_t surface.
 * @param w Width of the rendered surface (in pixels).
 * @param h Height of the rendered surface (in pixels).
 * @param fps Framerate at which the surface should be rendered.
 *	This can be changed at any time later. Pass a zero fps value
 *	to make the renderer only run on-request (see mt_cairo_render_once).
 * @param init_cb An optional initialization callback that can be
 *	used to initialize private resources needed during rendering.
 *	Called once for every cairo_t instance (two instances for
 *	every mt_cairo_render_t, due to double-buffering).
 * @param render_cb A mandatory rendering callback. This is called for
 *	each rendered frame.
 * @param fini_cb An optional finalization callback that can be used
 *	to free resources allocated during init_cb.
 * @param userinfo An optional user info pointer for the callbacks.
 */
mt_cairo_render_t *
mt_cairo_render_init_impl(const char *filename, int line,
    unsigned w, unsigned h, double fps, mt_cairo_init_cb_t init_cb,
    mt_cairo_render_cb_t render_cb, mt_cairo_fini_cb_t fini_cb, void *userinfo)
{
	mt_cairo_render_t *mtcr = safe_calloc(1, sizeof (*mtcr));

	mt_cairo_render_glob_init();

	ASSERT(w != 0);
	ASSERT(h != 0);
	ASSERT(render_cb != NULL);

	mtcr->init_filename = strdup(filename);
	mtcr->init_line = line;
	mtcr->w = w;
	mtcr->h = h;
	mtcr->cur_rs = -1;
	mtcr->render_cb = render_cb;
	mtcr->fini_cb = fini_cb;
	mtcr->userinfo = userinfo;
	mtcr->fps = fps;
	mtcr->tex_filter = GL_LINEAR;

	mutex_init(&mtcr->lock);
	cv_init(&mtcr->cv);
	cv_init(&mtcr->render_done_cv);

	for (int i = 0; i < 2; i++) {
		render_surf_t *rs = &mtcr->rs[i];

		rs->surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		    mtcr->w, mtcr->h);
		rs->cr = cairo_create(rs->surf);
		if (init_cb != NULL && !init_cb(rs->cr, userinfo)) {
			mt_cairo_render_fini(mtcr);
			return (NULL);
		}
	}
	/* empty both surfaces to assure their data is populated */
	for (int i = 0; i < 2; i++) {
		cairo_set_operator(mtcr->rs[i].cr, CAIRO_OPERATOR_CLEAR);
		cairo_paint(mtcr->rs[i].cr);
		cairo_set_operator(mtcr->rs[i].cr, CAIRO_OPERATOR_OVER);
	}

	mtcr->last_draw.pos = NULL_VECT2;
	if (GLEW_VERSION_4_1) {
		char *vert_shader_text = sprintf_alloc(vert_shader410,
		    VTX_ATTRIB_POS, VTX_ATTRIB_TEX0);
		mtcr->shader = shader_prog_from_text("mt_cairo_render_shader",
		    vert_shader_text, frag_shader410, NULL);
		free(vert_shader_text);
	} else {
		mtcr->shader = shader_prog_from_text("mt_cairo_render_shader",
		    vert_shader, frag_shader, "vtx_pos", VTX_ATTRIB_POS,
		    "vtx_tex0", VTX_ATTRIB_TEX0, NULL);
	}
	VERIFY(mtcr->shader != 0);
	mtcr->shader_loc_pvm = glGetUniformLocation(mtcr->shader, "pvm");
	mtcr->shader_loc_tex = glGetUniformLocation(mtcr->shader, "tex");

	setup_vao(mtcr);
	mtcr->create_ctx = glctx_get_current();

	VERIFY(thread_create(&mtcr->thr, worker, mtcr));
	mtcr->started = B_TRUE;

	return (mtcr);
}

void
mt_cairo_render_fini(mt_cairo_render_t *mtcr)
{
	if (mtcr->started) {
		mutex_enter(&mtcr->lock);
		mtcr->shutdown = B_TRUE;
		cv_broadcast(&mtcr->cv);
		mutex_exit(&mtcr->lock);
		thread_join(&mtcr->thr);
	}

	if (mtcr->vao != 0)
		glDeleteVertexArrays(1, &mtcr->vao);
	if (mtcr->vtx_buf != 0)
		glDeleteBuffers(1, &mtcr->vtx_buf);
	if (mtcr->idx_buf != 0)
		glDeleteBuffers(1, &mtcr->idx_buf);

	for (int i = 0; i < 2; i++) {
		render_surf_t *rs = &mtcr->rs[i];

		if (rs->cr != NULL) {
			if (mtcr->fini_cb != NULL)
				mtcr->fini_cb(rs->cr, mtcr->userinfo);
			cairo_destroy(rs->cr);
			cairo_surface_destroy(rs->surf);
		}
		if (rs->tex != 0) {
			glDeleteTextures(1, &rs->tex);
			IF_TEXSZ(TEXSZ_FREE_INSTANCE(mt_cairo_render_tex, mtcr,
			    GL_BGRA, GL_UNSIGNED_BYTE, mtcr->w, mtcr->h));
		}
	}
	if (mtcr->pbo != 0) {
		glDeleteBuffers(1, &mtcr->pbo);
		IF_TEXSZ(TEXSZ_FREE_INSTANCE(mt_cairo_render_pbo, mtcr,
		    GL_BGRA, GL_UNSIGNED_BYTE, mtcr->w, mtcr->h));
	}
	if (mtcr->shader != 0)
		glDeleteProgram(mtcr->shader);

	free(mtcr->init_filename);

	mutex_destroy(&mtcr->lock);
	cv_destroy(&mtcr->cv);
	cv_destroy(&mtcr->render_done_cv);

	if (mtcr->create_ctx != NULL)
		glctx_destroy(mtcr->create_ctx);

	free(mtcr);
}

void
mt_cairo_render_set_fps(mt_cairo_render_t *mtcr, double fps)
{
	mutex_enter(&mtcr->lock);
	if (mtcr->fps != fps) {
		mtcr->fps = fps;
		cv_broadcast(&mtcr->cv);
	}
	mutex_exit(&mtcr->lock);
}

double
mt_cairo_render_get_fps(mt_cairo_render_t *mtcr)
{
	return (mtcr->fps);
}

void
mt_cairo_render_set_texture_filter(mt_cairo_render_t *mtcr,
    unsigned gl_filter_enum)
{
	/*
	 * Must be called before any drawing was done, otherwise
	 * the filtering won't be applied.
	 */
	ASSERT(mtcr != NULL);
	ASSERT0(mtcr->rs[0].tex);
	ASSERT0(mtcr->rs[1].tex);
	mtcr->tex_filter = gl_filter_enum;
}

/*
 * Fires the renderer off once to produce a new frame. This can be especially
 * useful for renderers with fps = 0, which are only invoked on request.
 */
void
mt_cairo_render_once(mt_cairo_render_t *mtcr)
{
	mutex_enter(&mtcr->lock);
	cv_broadcast(&mtcr->cv);
	mutex_exit(&mtcr->lock);
}

/*
 * Same as mt_cairo_render_once, but waits for the new frame to finish
 * rendering.
 */
void
mt_cairo_render_once_wait(mt_cairo_render_t *mtcr)
{
	mutex_enter(&mtcr->lock);
	mtcr->one_shot_block = B_TRUE;
	cv_broadcast(&mtcr->cv);
	cv_wait(&mtcr->render_done_cv, &mtcr->lock);
	mtcr->one_shot_block = B_FALSE;
	mutex_exit(&mtcr->lock);
}

static void
bind_tex_sync(mt_cairo_render_t *mtcr, render_surf_t *rs)
{
	ASSERT(rs->tex != 0);
	glBindTexture(GL_TEXTURE_2D, rs->tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mtcr->w, mtcr->h, 0, GL_BGRA,
	    GL_UNSIGNED_BYTE, cairo_image_surface_get_data(rs->surf));
}

static void
rs_tex_alloc(mt_cairo_render_t *mtcr, render_surf_t *rs)
{
	if (rs->tex == 0) {
		MTCR_DEBUG("rs %d alloc tex", rs == &mtcr->rs[0] ? 0 : 1);
		glGenTextures(1, &rs->tex);
		glBindTexture(GL_TEXTURE_2D, rs->tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
		    mtcr->tex_filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
		    mtcr->tex_filter);
		IF_TEXSZ(TEXSZ_ALLOC_INSTANCE(mt_cairo_render_tex, mtcr,
		    mtcr->init_filename, mtcr->init_line, GL_BGRA,
		    GL_UNSIGNED_BYTE, mtcr->w, mtcr->h));
	}
	if (mtcr->pbo == 0) {
		size_t sz = mtcr->w * mtcr->h * 4;

		MTCR_DEBUG("rs %d alloc pbo", rs == &mtcr->rs[0] ? 0 : 1);
		glGenBuffers(1, &mtcr->pbo);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, mtcr->pbo);
		glBufferData(GL_PIXEL_UNPACK_BUFFER, sz, 0, GL_STREAM_DRAW);
		IF_TEXSZ(TEXSZ_ALLOC_INSTANCE(mt_cairo_render_pbo, mtcr,
		    mtcr->init_filename, mtcr->init_line, GL_BGRA,
		    GL_UNSIGNED_BYTE, mtcr->w, mtcr->h));
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	}
}

static void
upload_surface(mt_cairo_render_t *mtcr, render_surf_t *rs)
{
	void *ptr;

	MTCR_DEBUG("rs %d upload tex %d", rs == &mtcr->rs[0] ? 0 : 1, rs->tex);

	cairo_surface_flush(rs->surf);

	/* the back-buffer is ready, set up an async xfer */
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, mtcr->pbo);
	ptr = glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
	if (ptr != NULL) {
		size_t sz = mtcr->w * mtcr->h * 4;

		memcpy(ptr, cairo_image_surface_get_data(rs->surf), sz);
		glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);

		ASSERT(rs->tex != 0);
		glBindTexture(GL_TEXTURE_2D, rs->tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mtcr->w, mtcr->h, 0,
		    GL_BGRA, GL_UNSIGNED_BYTE, NULL);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	} else {
		logMsg("Error updating mt_cairo_render surface %p(%s:%d): "
		    "glMapBuffer returned NULL", mtcr, mtcr->init_filename,
		    mtcr->init_line);
		bind_tex_sync(mtcr, rs);
	}
	rs->chg = B_FALSE;
}

static bool_t
bind_cur_tex(mt_cairo_render_t *mtcr)
{
	render_surf_t *rs = &mtcr->rs[mtcr->cur_rs];

	glActiveTexture(GL_TEXTURE0);
	rs_tex_alloc(mtcr, rs);
	if (rs->chg)
		upload_surface(mtcr, rs);
	glBindTexture(GL_TEXTURE_2D, rs->tex);

	return (B_TRUE);
}

static void
prepare_vtx_buffer(mt_cairo_render_t *mtcr, vect2_t pos, vect2_t size,
    double x1, double x2, double y1, double y2)
{
	vtx_t buf[4];

	if (VECT2_EQ(mtcr->last_draw.pos, pos) &&
	    VECT2_EQ(mtcr->last_draw.size, size) &&
	    mtcr->last_draw.x1 == x1 && mtcr->last_draw.x2 == x2 &&
	    mtcr->last_draw.y1 == y1 && mtcr->last_draw.y2 == y2)
		return;

	buf[0].pos[0] = pos.x;
	buf[0].pos[1] = pos.y;
	buf[0].pos[2] = 0;
	buf[0].tex0[0] = x1;
	buf[0].tex0[1] = y2;

	buf[1].pos[0] = pos.x;
	buf[1].pos[1] = pos.y + size.y;
	buf[1].pos[2] = 0;
	buf[1].tex0[0] = x1;
	buf[1].tex0[1] = y1;

	buf[2].pos[0] = pos.x + size.x;
	buf[2].pos[1] = pos.y + size.y;
	buf[2].pos[2] = 0;
	buf[2].tex0[0] = x2;
	buf[2].tex0[1] = y1;

	buf[3].pos[0] = pos.x + size.x;
	buf[3].pos[1] = pos.y;
	buf[3].pos[2] = 0;
	buf[3].tex0[0] = x2;
	buf[3].tex0[1] = y2;

	ASSERT(mtcr->vtx_buf != 0);
	glBindBuffer(GL_ARRAY_BUFFER, mtcr->vtx_buf);
	glBufferData(GL_ARRAY_BUFFER, sizeof (buf), buf, GL_STATIC_DRAW);

	mtcr->last_draw.pos = pos;
	mtcr->last_draw.size = size;
	mtcr->last_draw.x1 = x1;
	mtcr->last_draw.x2 = x2;
	mtcr->last_draw.y1 = y1;
	mtcr->last_draw.y2 = y2;
}

/*
 * Same as mt_cairo_render_draw_subrect, but renders the entire surface
 * to the passed coordinates.
 */
void
mt_cairo_render_draw(mt_cairo_render_t *mtcr, vect2_t pos, vect2_t size)
{
	mt_cairo_render_draw_subrect(mtcr, ZERO_VECT2, VECT2(1, 1), pos, size);
}

void
mt_cairo_render_draw_pvm(mt_cairo_render_t *mtcr, vect2_t pos, vect2_t size,
    const GLfloat *pvm)
{
	mt_cairo_render_draw_subrect_pvm(mtcr, ZERO_VECT2, VECT2(1, 1), pos,
	    size, pvm);
}

void
mt_cairo_render_draw_subrect(mt_cairo_render_t *mtcr,
    vect2_t src_pos, vect2_t src_sz, vect2_t pos, vect2_t size)
{
	mat4 pvm;

	if (dr_geti(&drs.draw_call_type) != 0) {
		int vp[4];

		VERIFY3S(dr_getvi(&drs.viewport, vp, 0, 4), ==, 4);
		glm_ortho(vp[0], vp[2] - vp[0], vp[1], vp[3] - vp[1], 0, 1,
		    pvm);
	} else {
		mat4 proj, mv;

		VERIFY3S(dr_getvf32(&drs.mv_matrix, (float *)mv, 0, 16),
		    ==, 16);
		VERIFY3S(dr_getvf32(&drs.proj_matrix, (float *)proj, 0, 16),
		    ==, 16);
		glm_mat4_mul(proj, mv, pvm);
	}
	mt_cairo_render_draw_subrect_pvm(mtcr, src_pos, src_sz, pos, size,
	    (GLfloat *)pvm);
}

/*
 * Draws the rendered surface at offset pos.xy, at a size of size.xy.
 * src_pos and src_sz allow you to specify only a sub-rectangle of the
 * surface to be rendered.
 *
 * This should be called at regular intervals to draw the results of
 * the cairo render (though not necessarily in lockstep with it). If
 * a new frame hasn't been rendered yet, this function simply renders
 * the old buffer again (or nothing at all if no surface has completed
 * internal Cairo rendering). You can guarantee that a surface is ready
 * by first calling mt_cairo_render_once_wait.
 */
void
mt_cairo_render_draw_subrect_pvm(mt_cairo_render_t *mtcr,
    vect2_t src_pos, vect2_t src_sz, vect2_t pos, vect2_t size,
    const GLfloat *pvm)
{
	GLint old_vao = 0;
	bool_t use_vao;
	double x1 = src_pos.x, x2 = src_pos.x + src_sz.x;
	double y1 = src_pos.y, y2 = src_pos.y + src_sz.y;

	if (mtcr->cur_rs == -1)
		return;
	ASSERT3S(mtcr->cur_rs, >=, 0);
	ASSERT3S(mtcr->cur_rs, <, 2);

	mutex_enter(&mtcr->lock);
	if (!bind_cur_tex(mtcr)) {
		mutex_exit(&mtcr->lock);
		return;
	}
	mutex_exit(&mtcr->lock);

	use_vao = (mtcr->vao != 0 &&
	    (!mtcr->ctx_checking || glctx_is_current(mtcr->create_ctx)));

	if (use_vao) {
		glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vao);

		glBindVertexArray(mtcr->vao);
		glEnable(GL_BLEND);
	} else {
		XPLMSetGraphicsState(1, 1, 1, 1, 1, 1, 1);
		glBindBuffer(GL_ARRAY_BUFFER, mtcr->vtx_buf);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mtcr->idx_buf);

		glutils_enable_vtx_attr_ptr(VTX_ATTRIB_POS, 3, GL_FLOAT,
		    GL_FALSE, sizeof (vtx_t), offsetof(vtx_t, pos));
		glutils_enable_vtx_attr_ptr(VTX_ATTRIB_TEX0, 2, GL_FLOAT,
		    GL_FALSE, sizeof (vtx_t), offsetof(vtx_t, tex0));
	}

	glUseProgram(mtcr->shader);

	prepare_vtx_buffer(mtcr, pos, size, x1, x2, y1, y2);

	glUniformMatrix4fv(mtcr->shader_loc_pvm,
	    1, GL_FALSE, (const GLfloat *)pvm);
	glUniform1i(mtcr->shader_loc_tex, 0);

	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);

	if (use_vao) {
		glBindVertexArray(old_vao);
	} else {
		glDisableVertexAttribArray(VTX_ATTRIB_POS);
		glDisableVertexAttribArray(VTX_ATTRIB_TEX0);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glUseProgram(0);
}

/*
 * Retrieves the OpenGL texture object of the surface that has currently
 * completed rendering. If no surface is ready yet, returns 0 instead.
 */
GLuint
mt_cairo_render_get_tex(mt_cairo_render_t *mtcr)
{
	GLuint tex;

	mutex_enter(&mtcr->lock);

	if (mtcr->cur_rs != -1) {
		render_surf_t *rs = &mtcr->rs[mtcr->cur_rs];
		/* Upload the texture if it has changed */

		rs_tex_alloc(mtcr, rs);
		if (rs->chg)
			upload_surface(mtcr, rs);
		tex = rs->tex;
	} else {
		/* No texture ready yet */
		tex = 0;
	}

	mutex_exit(&mtcr->lock);

	return (tex);
}

API_EXPORT unsigned
mt_cairo_render_get_width(mt_cairo_render_t *mtcr)
{
	ASSERT(mtcr != NULL);
	return (mtcr->w);
}

API_EXPORT unsigned
mt_cairo_render_get_height(mt_cairo_render_t *mtcr)
{
	ASSERT(mtcr != NULL);
	return (mtcr->h);
}

API_EXPORT void
mt_cairo_render_blit_back2front(mt_cairo_render_t *mtcr,
    mtcr_rect_t *rects, size_t num)
{
	/*
	 * We should be safe to access the surface ordering here, because
	 * this should ONLY be called from the worker thread.
	 */
	cairo_surface_t *surf1, *surf2;
	uint8_t *buf1, *buf2;
	int stride;

	if (mtcr->cur_rs == -1)
		return;

	surf1 = mtcr->rs[!mtcr->cur_rs].surf;	/* target */
	surf2 = mtcr->rs[mtcr->cur_rs].surf;	/* source */
	buf1 = cairo_image_surface_get_data(surf1);
	buf2 = cairo_image_surface_get_data(surf2);
	stride = cairo_image_surface_get_stride(surf1);

	cairo_surface_flush(surf1);
	cairo_surface_flush(surf2);
	for (size_t i = 0; i < num; i++) {
		ASSERT3U(rects[i].x, <, mtcr->w);
		ASSERT3U(rects[i].y, <, mtcr->h);
		ASSERT3U(rects[i].x + rects[i].w, <=, mtcr->w);
		ASSERT3U(rects[i].y + rects[i].h, <=, mtcr->h);
		for (unsigned row = rects[i].y; row < rects[i].y + rects[i].h;
		    row++) {
			uint8_t *p1 = &buf1[4 * rects[i].x +
			    row * stride];
			const uint8_t *p2 = &buf2[4 * rects[i].x +
			    row * stride];
			memcpy(p1, p2, 4 * rects[i].w);
		}
	}
	cairo_surface_mark_dirty(surf1);
}

/*
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
 * Return B_TRUE if loading the font was successfull, B_FALSE otherwise. In
 *	case of error, the reason is logged using logMsg.
 */
bool_t
try_load_font(const char *fontdir, const char *fontfile, FT_Library ft,
    FT_Face *font, cairo_font_face_t **cr_font)
{
	char *fontpath = mkpathname(fontdir, fontfile, NULL);
	FT_Error err;

	mt_cairo_render_glob_init();

	if ((err = FT_New_Face(ft, fontpath, 0, font)) != 0) {
		logMsg("Error loading font file %s: %s", fontpath,
		    ft_err2str(err));
		free(fontpath);
		return (B_FALSE);
	}

	*cr_font = cairo_ft_font_face_create_for_ft_face(*font, 0);

	free(fontpath);

	return (B_TRUE);
}

void
mt_cairo_render_set_ctx_checking_enabled(mt_cairo_render_t *mtcr,
    bool_t flag)
{
	mtcr->ctx_checking = flag;
}

API_EXPORT void
mt_cairo_render_set_debug(mt_cairo_render_t *mtcr, bool_t flag)
{
	ASSERT(mtcr != NULL);
	mtcr->debug = flag;
}

API_EXPORT bool_t mt_cairo_render_get_debug(const mt_cairo_render_t *mtcr)
{
	ASSERT(mtcr != NULL);
	return (mtcr->debug);
}

void
mt_cairo_render_rounded_rectangle(cairo_t *cr, double x, double y,
    double w, double h, double radius)
{
	cairo_move_to(cr, x + radius, y);
	cairo_line_to(cr, x + w - radius, y);
	cairo_arc(cr, x + w - radius, y + radius, radius,
	    DEG2RAD(270), DEG2RAD(360));
	cairo_line_to(cr, x + w, y + h - radius);
	cairo_arc(cr, x + w - radius, y + h - radius, radius,
	    DEG2RAD(0), DEG2RAD(90));
	cairo_line_to(cr, x + radius, y + h);
	cairo_arc(cr, x + radius, y + h - radius, radius,
	    DEG2RAD(90), DEG2RAD(180));
	cairo_line_to(cr, x, y + radius);
	cairo_arc(cr, x + radius, y + radius, radius,
	    DEG2RAD(180), DEG2RAD(270));
}
