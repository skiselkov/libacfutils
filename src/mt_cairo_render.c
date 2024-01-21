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

#include <XPLMGraphics.h>
#include <XPLMUtilities.h>

#include "acfutils/assert.h"
#include "acfutils/dr.h"
#include "acfutils/geom.h"
#include "acfutils/glctx.h"
#include "acfutils/glew.h"
#include "acfutils/list.h"
#include "acfutils/mt_cairo_render.h"
#include "acfutils/safe_alloc.h"
#include "acfutils/glutils.h"
#include "acfutils/shader.h"
#include "acfutils/thread.h"
#include "acfutils/time.h"

#ifdef	_USE_MATH_DEFINES
#undef	_USE_MATH_DEFINES
#endif

#if	IBM
#ifdef	__SSE2__
#undef	__SSE2__
#endif
#endif	/* IBM */

#include <cglm/cglm.h>

TEXSZ_MK_TOKEN(mt_cairo_render_tex);
TEXSZ_MK_TOKEN(mt_cairo_render_pbo);

typedef struct {
	GLfloat		pos[3];
	GLfloat		tex0[2];
} vtx_t;

typedef struct {
	cairo_t			*cr;
	cairo_surface_t		*surf;
} render_surf_t;

struct mt_cairo_render_s {
	char			*init_filename;
	int			init_line;
	vect3_t			monochrome;
	bool_t			use_ffp;

	mt_cairo_uploader_t	*mtul;
	list_node_t		mtul_queue_node;

	unsigned		w, h;
	double			fps;
	mt_cairo_render_cb_t	render_cb;
	mt_cairo_init_cb_t	init_cb;
	mt_cairo_fini_cb_t	fini_cb;
	void			*userinfo;

	int			render_rs;
	int			present_rs;
	render_surf_t		rs[2];
	bool_t			dirty;	/* changed the surface, reupload */
	bool_t			texed;	/* has glTexImage2D been applied? */
	GLuint			tex;
	GLuint			pbo;
	GLint			filter;
	list_node_t		ul_inprog_node;
	GLsync			sync;
	void			*coherent_data;

	thread_t		thr;
	condvar_t		cv;
	condvar_t		render_done_cv;
	bool_t			one_shot_block;
	mutex_t			lock;
	bool_t			started;
	bool_t			shutdown;
	bool_t			fg_mode;

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
	bool_t			shader_is_custom;
	GLint			shader_loc_pvm;
	GLint			shader_loc_tex;
	GLint			shader_loc_vtx_pos;
	GLint			shader_loc_vtx_tex0;
	GLint			shader_loc_color_in;

	bool_t			ctx_checking;
	glctx_t			*create_ctx;
};

struct mt_cairo_uploader_s {
	uint64_t	refcnt;
	glctx_t		*ctx;
	mutex_t		lock;
	condvar_t	cv_queue;
	condvar_t	cv_done;
	list_t		queue;
	bool_t		shutdown;
	thread_t	worker;
};

static const char *vert_shader =
    "#version 120\n"
    "#extension GL_EXT_gpu_shader4 : require\n"
    "uniform mat4	pvm;\n"
    "attribute vec3	vtx_pos;\n"
    "attribute vec2	vtx_tex0;\n"
    "varying vec2	tex_coord;\n"
    "void main() {\n"
    "	tex_coord = vtx_tex0;\n"
    "	gl_Position = pvm * vec4(vtx_pos, 1.0f);\n"
    "}\n";

static const char *frag_shader =
    "#version 120\n"
    "uniform sampler2D	tex;\n"
    "varying vec2	tex_coord;\n"
    "void main() {\n"
    "	gl_FragColor = texture2D(tex, tex_coord);\n"
    "}\n";

static const char *frag_shader_mono =
    "#version 120\n"
    "uniform sampler2D	tex;\n"
    "uniform vec3	color_in;\n"
    "varying vec2	tex_coord;\n"
    "void main() {\n"
    "	gl_FragColor = vec4(color_in, texture2D(tex, tex_coord).r);\n"
    "}\n";

static const char *vert_shader410 =
    "#version 410\n"
    "uniform mat4			pvm;\n"
    "layout(location = 0) in vec3	vtx_pos;\n"
    "layout(location = 1) in vec2	vtx_tex0;\n"
    "layout(location = 0) out vec2	tex_coord;\n"
    "void main() {\n"
    "	tex_coord = vtx_tex0;\n"
    "	gl_Position = pvm * vec4(vtx_pos, 1.0f);\n"
    "}\n";

static const char *frag_shader410 =
    "#version 410\n"
    "uniform sampler2D			tex;\n"
    "layout(location = 0) in vec2	tex_coord;\n"
    "layout(location = 0) out vec4	color_out;\n"
    "void main() {\n"
    "	color_out = texture(tex, tex_coord);\n"
    "}\n";

static const char *frag_shader410_mono =
    "#version 410\n"
    "uniform sampler2D			tex;\n"
    "uniform vec3			color_in;\n"
    "layout(location = 0) in vec2	tex_coord;\n"
    "layout(location = 0) out vec4	color_out;\n"
    "void main() {\n"
    "	color_out = vec4(color_in, texture(tex, tex_coord).r);\n"
    "}\n";

static bool_t glob_inited = B_FALSE;
static bool_t coherent = B_FALSE;
static thread_id_t mtcr_main_thread;

static struct {
	dr_t	viewport;
	dr_t	proj_matrix;
	dr_t	mv_matrix;
	dr_t	draw_call_type;
} drs;

static void mtcr_gl_formats(const mt_cairo_render_t *mtcr, GLint *intfmt,
    GLint *format);

static bool_t
cr_init(mt_cairo_render_t *mtcr, render_surf_t *rs)
{
	cairo_format_t cr_fmt;

	ASSERT(mtcr != NULL);
	ASSERT(rs != NULL);

	cr_fmt = (!IS_NULL_VECT(mtcr->monochrome) ? CAIRO_FORMAT_A8 :
	    CAIRO_FORMAT_ARGB32);
	rs->surf = cairo_image_surface_create(cr_fmt, mtcr->w, mtcr->h);
	rs->cr = cairo_create(rs->surf);
	if (mtcr->init_cb != NULL && !mtcr->init_cb(rs->cr, mtcr->userinfo))
		goto errout;
	cairo_set_operator(rs->cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(rs->cr);
	cairo_set_operator(rs->cr, CAIRO_OPERATOR_OVER);
	return (B_TRUE);
errout:
	cairo_destroy(rs->cr);
	rs->cr = NULL;
	cairo_surface_destroy(rs->surf);
	rs->surf = NULL;
	return (B_FALSE);
}

static void
cr_destroy(const mt_cairo_render_t *mtcr, render_surf_t *rs)
{
	ASSERT(mtcr != NULL);
	ASSERT(rs != NULL);

	if (rs->cr == NULL)
		return;
	ASSERT(rs->surf != NULL);

	if (mtcr->fini_cb != NULL)
		mtcr->fini_cb(rs->cr, mtcr->userinfo);
	cairo_destroy(rs->cr);
	rs->cr = NULL;
	cairo_surface_destroy(rs->surf);
	rs->surf = NULL;
}

/*
 * Recalculates the absolute cv_timedwait sleep target based on our framerate.
 */
static uint64_t
recalc_sleep_time(mt_cairo_render_t *mtcr)
{
	double fps;
	ASSERT(mtcr != NULL);
	mutex_enter(&mtcr->lock);
	fps = mtcr->fps;
	mutex_exit(&mtcr->lock);
	if (fps <= 0)
		return (0);
	return (microclock() + SEC2USEC(1.0 / fps));
}

static void
mtul_submit_mtcr(mt_cairo_uploader_t *mtul, mt_cairo_render_t *mtcr)
{
	ASSERT(mtul != NULL);
	ASSERT(mtcr != NULL);
	ASSERT(mtcr->dirty);

	mutex_enter(&mtul->lock);
	if (!list_link_active(&mtcr->mtul_queue_node)) {
		list_insert_tail(&mtul->queue, mtcr);
		cv_broadcast(&mtul->cv_queue);
	}
	while (mtcr->dirty)
		cv_wait(&mtul->cv_done, &mtul->lock);
	/* render_done_cv will be signalled by the uploader */
	mutex_exit(&mtul->lock);
}

static size_t
mtcr_get_surf_sz(const mt_cairo_render_t *mtcr)
{
	cairo_format_t cr_fmt;
	ASSERT(mtcr != NULL);
	cr_fmt = (!IS_NULL_VECT(mtcr->monochrome) ? CAIRO_FORMAT_A8 :
	    CAIRO_FORMAT_ARGB32);
	return (cairo_format_stride_for_width(cr_fmt, mtcr->w) * mtcr->h);
}

static void
worker_render_once(mt_cairo_render_t *mtcr)
{
	render_surf_t *rs;
	mt_cairo_uploader_t *mtul;

	ASSERT(mtcr != NULL);
	ASSERT(mtcr->render_rs != -1);
	ASSERT_MUTEX_HELD(&mtcr->lock);

	if (mtcr->coherent_data != NULL) {
		size_t sz = mtcr_get_surf_sz(mtcr);

		rs = &mtcr->rs[0];
		mutex_exit(&mtcr->lock);

		mtcr->render_cb(rs->cr, mtcr->w, mtcr->h, mtcr->userinfo);
		cairo_surface_flush(rs->surf);

		mutex_enter(&mtcr->lock);
		memcpy(mtcr->coherent_data,
		    cairo_image_surface_get_data(rs->surf), sz);
		mtcr->dirty = B_TRUE;
		mtcr->texed = B_FALSE;
		mtcr->present_rs = mtcr->render_rs;
		cv_broadcast(&mtcr->render_done_cv);
	} else {
		ASSERT3S(mtcr->render_rs, >=, 0);
		ASSERT3S(mtcr->render_rs, <, ARRAY_NUM_ELEM(mtcr->rs));
		rs = &mtcr->rs[mtcr->render_rs];
		mutex_exit(&mtcr->lock);

		mtcr->render_cb(rs->cr, mtcr->w, mtcr->h, mtcr->userinfo);
		cairo_surface_flush(rs->surf);

		mutex_enter(&mtcr->lock);
		mtcr->dirty = B_TRUE;

		mtul = mtcr->mtul;
		if (mtul != NULL) {
			ASSERT(!coherent);
			mutex_exit(&mtcr->lock);
			/* render_done_cv will be signalled by the uploader */
			mtul_submit_mtcr(mtul, mtcr);
			mutex_enter(&mtcr->lock);
		} else {
			mtcr->present_rs = mtcr->render_rs;
			cv_broadcast(&mtcr->render_done_cv);
		}
		mtcr->render_rs = !mtcr->render_rs;
	}
}

/*
 * Main mt_cairo_render_t worker thread. Simply waits around for the
 * required interval and fires off the rendering callback. This performs
 * no canvas clearing between calls, so the callback is responsible for
 * making sure its output canvas looks right.
 */
static void
worker(void *arg)
{
	mt_cairo_render_t *mtcr;
	char name[32];
	char shortname[7];
	uint64_t next_time = 0;

	ASSERT(arg != NULL);
	mtcr = arg;
	ASSERT(mtcr->render_cb != NULL);

	strlcpy(shortname, mtcr->init_filename, sizeof (shortname));
	snprintf(name, sizeof (name), "mtcr:%s:%d", shortname, mtcr->init_line);
	thread_set_name(name);

	mutex_enter(&mtcr->lock);
	mtcr->render_rs = 0;
	/*
	 * Render the first frame immediately to make sure we have something
	 * to show ASAP.
	 */
	if (mtcr->fps > 0)
		worker_render_once(mtcr);
	while (!mtcr->shutdown) {
		if (!mtcr->one_shot_block) {
			if (mtcr->fps > 0) {
				if (next_time == 0) {
					/*
					 * If we were in fps=0 mode before,
					 * this will be zero. So reset the
					 * timer ahead of time to avoid
					 * rendering two consecutive frames.
					 */
					next_time = recalc_sleep_time(mtcr);
				}
				cv_timedwait(&mtcr->cv, &mtcr->lock, next_time);
				/*
				 * Recalc the next frame time now to maintain
				 * near as possible constant framerate that
				 * isn't affected by the cairo render time.
				 */
				next_time = recalc_sleep_time(mtcr);
			} else {
				cv_wait(&mtcr->cv, &mtcr->lock);
				next_time = 0;
			}
		}
		if (mtcr->shutdown)
			break;
		worker_render_once(mtcr);
	}
	mutex_exit(&mtcr->lock);
}

/**
 * Performs global initialization of the mt_cairo_render backend logic.
 * @param want_coherent_mem If set to `B_TRUE`, this enables the use of
 *	persistent coherent memory for OpenGL texture uploading. This is
 *	generally recommended for most deployments, unless your OpenGL
 *	driver has bugs which make this mechanism not work well. If the
 *	underlying driver doesn't support persistent coherent memory
 *	(such as on macOS), mt_cairo_render_t automatically falls back
 *	to a different mechanism.
 * @note Only the first call of mt_cairo_render_glob_init() is honored.
 *	If you want your call to take precedence, you MUST call this
 *	function before creating any renderers or an uploader using
 *	mt_cairo_render_init() or mt_cairo_uploader_init() respectively.
 */
void
mt_cairo_render_glob_init(bool_t want_coherent_mem)
{
	if (glob_inited)
		return;
	cairo_surface_destroy(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
	    1, 1));
	mtcr_main_thread = curthread_id;
	fdr_find(&drs.viewport, "sim/graphics/view/viewport");
	fdr_find(&drs.proj_matrix, "sim/graphics/view/projection_matrix");
	fdr_find(&drs.mv_matrix, "sim/graphics/view/modelview_matrix");
	fdr_find(&drs.draw_call_type, "sim/graphics/view/draw_call_type");
	/*
	 * Important caveat: in Zink mode we MUST utilize coherent memory
	 * (or just avoid using an uploader). The uploader tries to map
	 * a buffer using glMapBuffer while holding the mtcr->lock. This
	 * can cause a deadlock, because Zink's internal architecture seems
	 * to sometimes require main thread progress before a background
	 * thread's buffer mapping request can succeed.
	 */
	coherent = ((want_coherent_mem || glutils_in_zink_mode()) &&
	    GLEW_ARB_buffer_storage);
	glob_inited = B_TRUE;
}

static void
mtcr_gl_init(mt_cairo_render_t *mtcr)
{
	GLint old_vao = 0;
	bool_t on_main_thread = (curthread_id == mtcr_main_thread);
	GLint intfmt, gl_fmt;

	ASSERT(mtcr != NULL);

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

	ASSERT0(mtcr->pbo);
	glGenBuffers(1, &mtcr->pbo);
	ASSERT(mtcr->pbo != 0);
	mtcr_gl_formats(mtcr, &intfmt, &gl_fmt);
	IF_TEXSZ(TEXSZ_ALLOC_INSTANCE(mt_cairo_render_pbo,
	    mtcr, mtcr->init_filename, mtcr->init_line, gl_fmt,
	    GL_UNSIGNED_BYTE, mtcr->w, mtcr->h));

	if (coherent) {
		const GLuint flags = (GL_MAP_WRITE_BIT |
		    GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
		size_t sz = mtcr_get_surf_sz(mtcr);;

		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, mtcr->pbo);
		glBufferStorage(GL_PIXEL_UNPACK_BUFFER, sz, 0, flags);
		mtcr->coherent_data = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER,
		    0, sz, flags);
		if (mtcr->coherent_data == NULL) {
			logMsg("WARNING: cannot grab coherent memory buffer "
			    "with glMapBufferRange(). You may be running out "
			    "of VRAM. Switching to non-coherent mode for this "
			    "renderer (%s:%d).", mtcr->init_filename,
			    mtcr->init_line);
		}
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	}
	glGenTextures(1, &mtcr->tex);
	ASSERT(mtcr->tex != 0);
	glBindTexture(GL_TEXTURE_2D, mtcr->tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mtcr->filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mtcr->filter);
	if (on_main_thread)
		XPLMBindTexture2d(0, 0);

	IF_TEXSZ(TEXSZ_ALLOC_INSTANCE(mt_cairo_render_tex, mtcr,
	    mtcr->init_filename, mtcr->init_line, gl_fmt,
	    GL_UNSIGNED_BYTE, mtcr->w, mtcr->h));
	mtcr->texed = B_FALSE;
}

/*
 * Frees a texture & PBO previously allocated using rs_tex_alloc. If the
 * texture or PBO have already been freed, this function does nothing.
 * The targets of the `tex' and `pbo' arguments are set to 0 after freeing.
 */
static void
mtcr_gl_fini(mt_cairo_render_t *mtcr)
{
	GLint intfmt, format;

	ASSERT(mtcr != NULL);

	if (mtcr->vao != 0) {
		glDeleteVertexArrays(1, &mtcr->vao);
		mtcr->vao = 0;
	}
	if (mtcr->vtx_buf != 0) {
		glDeleteBuffers(1, &mtcr->vtx_buf);
		mtcr->vtx_buf = 0;
		mtcr->last_draw.pos = NULL_VECT2;
	}
	if (mtcr->idx_buf != 0) {
		glDeleteBuffers(1, &mtcr->idx_buf);
		mtcr->idx_buf = 0;
	}
	mtcr_gl_formats(mtcr, &intfmt, &format);
	if (mtcr->tex != 0) {
		glDeleteTextures(1, &mtcr->tex);
		mtcr->tex = 0;
		IF_TEXSZ(TEXSZ_FREE_INSTANCE(mt_cairo_render_tex, mtcr,
		    format, GL_UNSIGNED_BYTE, mtcr->w, mtcr->h));
	}
	if (mtcr->pbo != 0) {
		if (mtcr->coherent_data) {
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, mtcr->pbo);
			glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
			mtcr->coherent_data = NULL;
		}
		glDeleteBuffers(1, &mtcr->pbo);
		mtcr->pbo = 0;
		IF_TEXSZ(TEXSZ_FREE_INSTANCE(mt_cairo_render_pbo, mtcr,
		    format, GL_UNSIGNED_BYTE, mtcr->w, mtcr->h));
	}
	if (mtcr->sync != NULL) {
		glDeleteSync(mtcr->sync);
		mtcr->sync = NULL;
	}
	if (mtcr->shader != 0 && !mtcr->shader_is_custom) {
		glDeleteProgram(mtcr->shader);
		mtcr->shader = 0;
	}
}

/*
 * Due to a bug in AMD's drivers on Windows (at least as of 23.04.2), when
 * running in XP12 on top of Zink, we cannot utilize shader rendering,
 * because texture coordinates get flipped on input to the vertex shader for
 * the second triangle. So for that specific platform, we need to revert to
 * the fixed-function pipeline (which doesn't seem to exhibit this bug).
 * This will only work when using our own built-in shader.
 */
static bool_t
check_use_ffp(const mt_cairo_render_t *mtcr)
{
	ASSERT(mtcr != NULL);
	return (IBM && !mtcr->shader_is_custom &&
	    IS_NULL_VECT(mtcr->monochrome) &&
	    curthread_id == mtcr_main_thread && glutils_in_zink_mode() &&
	    strstr((const char *)glGetString(GL_RENDERER), "Radeon") != NULL);
}

/**
 * Implementation of mt_cairo_render_init() macro. Don't call this function
 * directly use the macro instead.
 * @see mt_cairo_render_init()
 */
mt_cairo_render_t *
mt_cairo_render_init_impl(const char *filename, int line,
    unsigned w, unsigned h, double fps, mt_cairo_init_cb_t init_cb,
    mt_cairo_render_cb_t render_cb, mt_cairo_fini_cb_t fini_cb, void *userinfo)
{
	mt_cairo_render_t *mtcr = safe_calloc(1, sizeof (*mtcr));

	ASSERT(w != 0);
	ASSERT(h != 0);
	ASSERT(render_cb != NULL);

	mt_cairo_render_glob_init(B_TRUE);

	mtcr->init_filename = strdup(filename);
	mtcr->init_line = line;
	mtcr->w = w;
	mtcr->h = h;
	mtcr->render_rs = -1;
	mtcr->present_rs = -1;
	mtcr->render_cb = render_cb;
	mtcr->init_cb = init_cb;
	mtcr->fini_cb = fini_cb;
	mtcr->userinfo = userinfo;
	mtcr->fps = fps;
	mtcr->monochrome = NULL_VECT3;
	mtcr->filter = GL_LINEAR;
	mtcr->last_draw.pos = NULL_VECT2;

	mutex_init(&mtcr->lock);
	cv_init(&mtcr->cv);
	cv_init(&mtcr->render_done_cv);

	mtcr_gl_init(mtcr);
	if (!cr_init(mtcr, &mtcr->rs[0])) {
		mt_cairo_render_fini(mtcr);
		return (NULL);
	}
	if (mtcr->coherent_data == NULL) {
		if (!cr_init(mtcr, &mtcr->rs[1])) {
			mt_cairo_render_fini(mtcr);
			return (NULL);
		}
	}
	mt_cairo_render_set_shader(mtcr, 0);

	if (!glutils_in_zink_mode())
		mtcr->create_ctx = glctx_get_current();
	mtcr->use_ffp = check_use_ffp(mtcr);

	VERIFY(thread_create(&mtcr->thr, worker, mtcr));
	mtcr->started = B_TRUE;

	return (mtcr);
}

/**
 * Destroys a previously created mt_cairo_render_t instance.
 * @see mt_cairo_render_init()
 */
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
	if (mtcr->mtul != NULL) {
		mutex_enter(&mtcr->mtul->lock);
		ASSERT(mtcr->mtul->refcnt != 0);
		mtcr->mtul->refcnt--;
		if (list_link_active(&mtcr->mtul_queue_node))
			list_remove(&mtcr->mtul->queue, mtcr);
		mutex_exit(&mtcr->mtul->lock);
	}
	for (size_t i = 0; i < ARRAY_NUM_ELEM(mtcr->rs); i++)
		cr_destroy(mtcr, &mtcr->rs[i]);
	mtcr_gl_fini(mtcr);

	free(mtcr->init_filename);

	mutex_destroy(&mtcr->lock);
	cv_destroy(&mtcr->cv);
	cv_destroy(&mtcr->render_done_cv);

	if (mtcr->create_ctx != NULL)
		glctx_destroy(mtcr->create_ctx);

	free(mtcr);
}

/**
 * Changes the rendering FPS of an mt_cairo_render_t instance.
 */
void
mt_cairo_render_set_fps(mt_cairo_render_t *mtcr, double fps)
{
	if (mtcr->fps != fps) {
		ASSERT(!mtcr->fg_mode);
		mutex_enter(&mtcr->lock);
		mtcr->fps = fps;
		cv_broadcast(&mtcr->cv);
		mutex_exit(&mtcr->lock);
	}
}

/**
 * @return The rendering FPS of the mt_cairo_render_t.
 */
double
mt_cairo_render_get_fps(mt_cairo_render_t *mtcr)
{
	return (mtcr->fps);
}

/**
 * Enables foreground rendering mode for the mt_cairo_render_t (mtcr)
 * instance. Normally, an mtcr performs rendering in a separate
 * background thread. There cases where you don't want or need that to
 * happen (e.g. if you are already controlling the mtcr from a
 * background thread and want to have the rendering happen in that
 * background thread, instead of an extra one). Calling this function
 * will shut down the background rendering thread of the mtcr and
 * reconfigure it for rendering in the mt_cairo_render_once_wait()
 * call.
 * @note The mtcr MUST have been configured with an FPS of 0 (i.e.
 *	manual rendering invocation) before calling this function.
 *
 * After this function returns, you must manually invoke
 * mt_cairo_render_once_wait() whenever you want the mtcr to actually
 * perform rendering. The rendering and surface swap operations will
 * be done directly on the calling thread, so there are no background
 * thread synchronization issues.
 */
void
mt_cairo_render_enable_fg_mode(mt_cairo_render_t *mtcr)
{
	ASSERT(mtcr != NULL);
	ASSERT3F(mtcr->fps, ==, 0);
	ASSERT(mtcr->started);

	if (!mtcr->fg_mode) {
		mutex_enter(&mtcr->lock);
		mtcr->shutdown = B_TRUE;
		cv_broadcast(&mtcr->cv);
		mutex_exit(&mtcr->lock);
		thread_join(&mtcr->thr);

		mtcr->fg_mode = B_TRUE;
		mtcr->started = B_FALSE;
	}
}

/**
 * @return `B_TRUE` if the passed renderer is configure for foreground
 *	rendering, `B_FALSE` otherwise.
 * @see mt_cairo_render_enable_fg_mode()
 */
bool_t
mt_cairo_render_get_fg_mode(const mt_cairo_render_t *mtcr)
{
	ASSERT(mtcr != NULL);
	return (mtcr->fg_mode);
}

/**
 * Sets the texture filtering used in compositing done by
 * mt_cairo_render_draw().
 * @param gl_filter_enum Must be one of the OpenGL texture filtering enums.
 *	The default on a newly created mt_cairo_render_t is GL_LINEAR.
 */
void
mt_cairo_render_set_texture_filter(mt_cairo_render_t *mtcr,
    unsigned gl_filter_enum)
{
	ASSERT(mtcr != NULL);
	ASSERT(mtcr->tex != 0);
	glBindTexture(GL_TEXTURE_2D, mtcr->tex);
	mtcr->filter = gl_filter_enum;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_enum);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_enum);
	glBindTexture(GL_TEXTURE_2D, 0);
}

static void
set_shader_impl(mt_cairo_render_t *mtcr, unsigned prog, bool_t force)
{
	/* Forcibly reload our own shader */
	if (force && !mtcr->shader_is_custom && mtcr->shader != 0) {
		ASSERT0(prog);
		glDeleteProgram(mtcr->shader);
		mtcr->shader = 0;
	}
	if (prog != 0) {
		if (!mtcr->shader_is_custom && mtcr->shader != 0)
			glDeleteProgram(mtcr->shader);
		mtcr->shader = prog;
		mtcr->shader_is_custom = B_TRUE;
	} else if (mtcr->shader_is_custom || mtcr->shader == 0) {
		/* Reinstall our standard shader */
		mtcr->shader_is_custom = B_FALSE;
		if (GLEW_VERSION_4_1) {
			const char *frag_shader_text = (IS_NULL_VECT(
			    mtcr->monochrome) ? frag_shader410 :
			    frag_shader410_mono);

			mtcr->shader = shader_prog_from_text(
			    "mt_cairo_render_shader",
			    vert_shader410, frag_shader_text, NULL);
		} else {
			const char *frag_shader_text = (IS_NULL_VECT(
			    mtcr->monochrome) ? frag_shader :
			    frag_shader_mono);

			mtcr->shader = shader_prog_from_text(
			    "mt_cairo_render_shader",
			    vert_shader, frag_shader_text, NULL);
		}
	}
	VERIFY(mtcr->shader != 0);
	mtcr->shader_loc_vtx_pos =
	    glGetAttribLocation(mtcr->shader, "vtx_pos");
	mtcr->shader_loc_vtx_tex0 =
	    glGetAttribLocation(mtcr->shader, "vtx_tex0");
	mtcr->shader_loc_pvm = glGetUniformLocation(mtcr->shader, "pvm");
	mtcr->shader_loc_tex = glGetUniformLocation(mtcr->shader, "tex");
	mtcr->shader_loc_color_in =
	    glGetUniformLocation(mtcr->shader, "color_in");
	mtcr->use_ffp = check_use_ffp(mtcr);
}

/**
 * Sets a custom compositing shader program for use during
 * mt_cairo_render_draw() and related functions. This lets you control
 * how the compositing happens and apply arbitrary effects.
 * @param mtcr The mt_cairo_render_t into which to install the shader.
 * @param prog The shader program handle to set. If you set this to 0,
 *	the shader program used by the mtcr is reset back to the default.
 *
 * ## Shader Uniforms and Vertex Attributes
 *
 * Your custom shader program must contain at least a vertex and fragment
 * shader. The mt_cairo_render_t automatically resolves attribute and
 * uniform bindings using the following names:
 * - The vertex position attribute as a `vec3` named "vtx_pos". The
 *	Z coordinate is always set to zero and only. X and Y correspond
 *	to the input vertex coordinates based on the `pos` and `size`
 *	arguments passed to mt_cairo_render_draw().
 * - The vertex texture UV attribute is passed as a `vec2` named "vtx_tex0".
 * - The combined projection-model-view matrix is passed in a uniform
 *	of type `mat4` named "pvm".
 * - The texture containing the cairo surface is passed in a uniform
 *	of type `sampler2D` named "tex". If the mtcr is configured for
 *	RGBA rendering (the default), the texture format and internal
 *	format are both `GL_RGBA`. If the mtcr is configured for monochrome
 *	rendering, the texture is of format `GL_RED` and internal format
 *	`GL_R8` (meaning, the surface information is contained in the red
 *	channel only). In all cases, the texture data is of type
 *	`GL_UNSIGNED_BYTE`.
 * - If configured for monochrome rendering, the monochrome color
 *	passed in the `color` argument of mt_cairo_render_set_monochrome()
 *	is passed in a uniform of type `vec3` named "color_in". In
 *	RGBA mode, this uniform isn't filled.
 */
void
mt_cairo_render_set_shader(mt_cairo_render_t *mtcr, unsigned prog)
{
	ASSERT(mtcr != NULL);
	set_shader_impl(mtcr, prog, B_FALSE);
}

/**
 * @return The custom shader program handle which was passed to
 *	mt_cairo_render_set_shader(). If no custom shader program was
 *	installed, this function returns 0 instead, to indicate that
 *	the mtcr is using the default shader program.
 */
unsigned
mt_cairo_render_get_shader(mt_cairo_render_t *mtcr)
{
	ASSERT(mtcr != NULL);
	return (mtcr->shader_is_custom ? mtcr->shader : 0);
}

/**
 * Enables or disables monochrome rendering mode. The default is disabled
 * (i.e. the image is rendered in RGBA mode). Monochrome rendering in
 * Cairo is controlled using the alpha channel. If you are using the
 * default mt_cairo_render compositing shader, the `color` vector
 * argument to this function sets what RGB color is used in the final
 * render. Passing any non-NULL vector here enables monochrome mode. Pass
 * a `NULL_VECT3` to disable monochrome mode.
 *
 * Switching rendering modes stops & restarts the worker thread (when
 * not in foreground rendering mode) and calls any surface fini and
 * init callbacks you passed mt_cairo_render_init().
 */
void
mt_cairo_render_set_monochrome(mt_cairo_render_t *mtcr, vect3_t color)
{
	ASSERT(mtcr != NULL);
	/*
	 * If the monochrome status hasn't changed, there's no need to
	 * rebuild the surface. Just store the potentially new color.
	 */
	if (IS_NULL_VECT(mtcr->monochrome) == IS_NULL_VECT(color)) {
		mtcr->monochrome = color;
		return;
	}
	/*
	 * Stop the worker thread.
	 */
	if (mtcr->started) {
		mutex_enter(&mtcr->lock);
		mtcr->shutdown = B_TRUE;
		cv_broadcast(&mtcr->cv);
		mutex_exit(&mtcr->lock);
		thread_join(&mtcr->thr);
	}
	for (size_t i = 0; i < ARRAY_NUM_ELEM(mtcr->rs); i++)
		cr_destroy(mtcr, &mtcr->rs[i]);
	mtcr_gl_fini(mtcr);

	mtcr->monochrome = color;
	mtcr->render_rs = -1;
	mtcr->present_rs = -1;

	mtcr_gl_init(mtcr);
	VERIFY(cr_init(mtcr, &mtcr->rs[0]));
	if (mtcr->coherent_data == NULL)
		VERIFY(cr_init(mtcr, &mtcr->rs[1]));
	/*
	 * If we were set up to use our own shader, reload it to switch
	 * to the monochrome version.
	 */
	if (!mtcr->shader_is_custom)
		set_shader_impl(mtcr, 0, B_TRUE);
	/*
	 * Restart the worker if not in FG mode.
	 */
	if (!mtcr->fg_mode) {
		mtcr->shutdown = B_FALSE;
		VERIFY(thread_create(&mtcr->thr, worker, mtcr));
		mtcr->started = B_TRUE;
	}
}

/**
 * @return The color used for monochrome rendering and compositing. If
 * monochrome rendering is not in use (the default), returns `NULL_VECT3`.
 */
vect3_t
mt_cairo_render_get_monochrome(const mt_cairo_render_t *mtcr)
{
	ASSERT(mtcr != NULL);
	return (mtcr->monochrome);
}

/**
 * Fires the renderer off once to produce a new frame. This can be
 * especially useful for renderers with fps = 0, which are only invoked
 * on request. This doesn't cause the caller to block until the render
 * is complete. Instead, it simply does an asynchronous render request
 * to the background thread. If the background thread is currently in
 * the process of rendering a frame, this request might get ignored.
 * If you need to make absolutely sure that a new image is drawn and
 * that rendering is finished, use mt_cairo_render_once_wait().
 * @note Calling this function on an mt_cairo_render_t which has been
 *	configured for foreground rendering using
 *	mt_cairo_render_enable_fg_mode() will cause an assertion failure.
 *	Foreground-mode renderers MUST be driven using
 *	mt_cairo_render_once_wait() instead.
 * @see mt_cairo_render_once_wait()
 */
void
mt_cairo_render_once(mt_cairo_render_t *mtcr)
{
	ASSERT(mtcr != NULL);
	ASSERT0(mtcr->fg_mode);
	mutex_enter(&mtcr->lock);
	cv_broadcast(&mtcr->cv);
	mutex_exit(&mtcr->lock);
}

/**
 * Same as mt_cairo_render_once(), but waits for the new frame to finish
 * rendering. This guarantees that the image is complete and ready for
 * compositing.
 *
 * If the renderer is set to foreground mode
 * (mt_cairo_render_enable_fg_mode()), you must use this function to
 * drive the rendering.
 */
void
mt_cairo_render_once_wait(mt_cairo_render_t *mtcr)
{
	ASSERT(mtcr != NULL);
	if (mtcr->fg_mode) {
		ASSERT(mtcr->render_cb != NULL);

		mutex_enter(&mtcr->lock);
		if (mtcr->render_rs == -1)
			mtcr->render_rs = 0;
		worker_render_once(mtcr);
		mutex_exit(&mtcr->lock);
	} else {
		mutex_enter(&mtcr->lock);
		mtcr->one_shot_block = B_TRUE;
		cv_broadcast(&mtcr->cv);
		cv_wait(&mtcr->render_done_cv, &mtcr->lock);
		mtcr->one_shot_block = B_FALSE;
		mutex_exit(&mtcr->lock);
	}
}

static void
mtcr_gl_formats(const mt_cairo_render_t *mtcr, GLint *intfmt, GLint *format)
{
	ASSERT(mtcr != NULL);
	ASSERT(intfmt != NULL);
	ASSERT(format != NULL);

	if (!IS_NULL_VECT(mtcr->monochrome)) {
		*intfmt = GL_R8;
		*format = GL_RED;
	} else {
		*intfmt = GL_RGBA;
		*format = GL_BGRA;
	}
}

/*
 * Uploads a finished cairo surface render to the provided texture & PBO.
 * The upload is normally done async via the PBO, but if that fails, the
 * upload is performed synchronously.
 */
static void
rs_upload(mt_cairo_render_t *mtcr, render_surf_t *rs)
{
	void *src, *dest;
	size_t sz;

	if (mtcr->coherent_data != NULL)
		return;

	ASSERT(mtcr != NULL);
	ASSERT(rs != NULL);
	ASSERT(rs->surf != NULL);
	ASSERT(mtcr->tex != 0);
	ASSERT(mtcr->pbo != 0);

	sz = mtcr_get_surf_sz(mtcr);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, mtcr->pbo);
	/* Buffer respecification - makes sure to orphan the old buffer! */
	glBufferData(GL_PIXEL_UNPACK_BUFFER, sz, NULL, GL_STREAM_DRAW);
	src = cairo_image_surface_get_data(rs->surf);
	dest = glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
	if (dest != NULL) {
		memcpy(dest, src, sz);
		glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
		/*
		 * We MUSTN'T call glTexImage2D yet, because if we're running
		 * on a background uploader thread, the OpenGL renderer can
		 * break and update the texture with old memory contents.
		 * Don't ask me why, I have NO CLUE why the driver messes
		 * this up. So we need to do the glTexImage2D synchronously
		 * on the rendering thread. That seems to pick up the buffer
		 * orphaning operation correctly.
		 */
		mtcr->texed = B_FALSE;
	} else {
		GLint intfmt, format;

		mtcr_gl_formats(mtcr, &intfmt, &format);
		logMsg("Error asynchronously updating mt_cairo_render "
		    "surface %p(%s:%d): glMapBuffer returned NULL",
		    mtcr, mtcr->init_filename, mtcr->init_line);
		glBindTexture(GL_TEXTURE_2D, mtcr->tex);
		glTexImage2D(GL_TEXTURE_2D, 0, intfmt, mtcr->w, mtcr->h, 0,
		    format, GL_UNSIGNED_BYTE, src);
		mtcr->texed = B_TRUE;
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

/*
 * After an MT-uploader async-uploads the new surface data, we still
 * need to apply it to the texture itself. Otherwise, it will only
 * sit in the orphaned buffer. This must be done from the thread
 * which plans to use the texture in actual rendering (otherwise the
 * drivers spaz out).
 * Careful, any texture binding point used previously is unbound by
 * this function. This is to facilitate interop with
 * mt_cairo_render_get_tex to avoid leaving bound textures lying
 * around.
 */
static void
mtcr_tex_apply(mt_cairo_render_t *mtcr, bool_t bind)
{
	ASSERT(mtcr != NULL);

	if (!mtcr->texed) {
		GLint intfmt, format;

		mtcr_gl_formats(mtcr, &intfmt, &format);
		ASSERT(mtcr->tex != 0);
		glBindTexture(GL_TEXTURE_2D, mtcr->tex);
		ASSERT(mtcr->pbo != 0);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, mtcr->pbo);
		glTexImage2D(GL_TEXTURE_2D, 0, intfmt, mtcr->w, mtcr->h, 0,
		    format, GL_UNSIGNED_BYTE, NULL);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		mtcr->texed = B_TRUE;
		if (!bind)
			glBindTexture(GL_TEXTURE_2D, 0);
	} else if (bind) {
		glBindTexture(GL_TEXTURE_2D, mtcr->tex);
	}
}

/*
 * Binds the current render_surf_t's texture to the current OpenGL context.
 * This is called from the foreground renderer to start drawing a finished
 * render frame.
 *
 * @return The render_surf_t that was bound, or NULL if none is available
 *	for display.
 */
static bool_t
bind_cur_tex(mt_cairo_render_t *mtcr)
{
	ASSERT(mtcr != NULL);
	ASSERT_MUTEX_HELD(&mtcr->lock);

	/* Nothing ready for present yet */
	if (mtcr->present_rs == -1)
		return (B_FALSE);
	glActiveTexture(GL_TEXTURE0);
	if (mtcr->dirty && mtcr->mtul == NULL) {
		rs_upload(mtcr, &mtcr->rs[mtcr->present_rs]);
		mtcr->dirty = B_FALSE;
	}
	/* NOW we can safely update the texture */
	mtcr_tex_apply(mtcr, B_TRUE);

	return (B_TRUE);
}

static void
prepare_vtx_buffer(mt_cairo_render_t *mtcr, vect2_t pos, vect2_t size,
    double x1, double x2, double y1, double y2, vtx_t buf[4])
{
	ASSERT(mtcr != NULL);
	ASSERT(buf != NULL);

	if (VECT2_EQ(mtcr->last_draw.pos, pos) &&
	    VECT2_EQ(mtcr->last_draw.size, size) &&
	    mtcr->last_draw.x1 == x1 && mtcr->last_draw.x2 == x2 &&
	    mtcr->last_draw.y1 == y1 && mtcr->last_draw.y2 == y2 &&
	    !mtcr->use_ffp) {
		return;
	}
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

	if (!mtcr->use_ffp) {
		ASSERT(mtcr->vtx_buf != 0);
		glBindBuffer(GL_ARRAY_BUFFER, mtcr->vtx_buf);
		glBufferData(GL_ARRAY_BUFFER, 4 * sizeof (vtx_t), buf,
		    GL_STATIC_DRAW);
	} else {
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glVertexPointer(3, GL_FLOAT, sizeof(vtx_t),
		    ((void *)buf) + offsetof(vtx_t, pos));
		glTexCoordPointer(2, GL_FLOAT, sizeof(vtx_t),
		    ((void *)buf) + offsetof(vtx_t, tex0));
	}
	mtcr->last_draw.pos = pos;
	mtcr->last_draw.size = size;
	mtcr->last_draw.x1 = x1;
	mtcr->last_draw.x2 = x2;
	mtcr->last_draw.y1 = y1;
	mtcr->last_draw.y2 = y2;
}

/**
 * Same as mt_cairo_render_draw_subrect(), but renders the entire
 * surface to the passed coordinates. For the vast majority of cases,
 * this is the call you want to use to composite the entire surface
 * onto the target framebuffer.
 */
void
mt_cairo_render_draw(mt_cairo_render_t *mtcr, vect2_t pos, vect2_t size)
{
	mt_cairo_render_draw_subrect(mtcr, ZERO_VECT2, VECT2(1, 1), pos, size);
}

/**
 * Same as mt_cairo_render_draw_subrect_pvm(), but always draws the
 * entire surface, rather than just a subrect of it.
 */
void
mt_cairo_render_draw_pvm(mt_cairo_render_t *mtcr, vect2_t pos, vect2_t size,
    const float *pvm)
{
	mt_cairo_render_draw_subrect_pvm(mtcr, ZERO_VECT2, VECT2(1, 1), pos,
	    size, pvm);
}

/**
 * Same as mt_cairo_render_draw_subrect_pvm(), but automatically
 * determines the projection-view-model matrix from X-Plane's datarefs.
 */
void
mt_cairo_render_draw_subrect(mt_cairo_render_t *mtcr,
    vect2_t src_pos, vect2_t src_sz, vect2_t pos, vect2_t size)
{
	mat4 pvm;
	/*
	 * Until X-Plane 12.06, XP wrongly reported `sim/view/draw_call_type`
	 * as `0` in window callbacks. 12.06 fixes that, but the viewport
	 * method doesn't work for Windows (the viewport is always reported
	 * in pixels, whereas the OGL matrices that XP provides as datarefs
	 * take interface scaling into account.) On XP > 12.06, we always
	 * use the matrices rather than the viewport to decide how to draw.
	 */
	int xp_version;
	XPLMGetVersions(&xp_version, NULL, NULL);

	if (xp_version < 12060 && dr_geti(&drs.draw_call_type) != 0) {
		int vp[4];

		VERIFY3S(dr_getvi(&drs.viewport, vp, 0, 4), ==, 4);
		ASSERT3S(vp[2] - vp[0], >, 0);
		ASSERT3S(vp[3] - vp[1], >, 0);
		glm_ortho(vp[0], vp[2], vp[1], vp[3], 0, 1, pvm);
	} else {
		mat4 proj, mv;

		VERIFY3S(dr_getvf32(&drs.mv_matrix, (float *)mv, 0, 16),
		    ==, 16);
		VERIFY3S(dr_getvf32(&drs.proj_matrix, (float *)proj, 0, 16),
		    ==, 16);
		glm_mat4_mul(proj, mv, pvm);
	}
	mt_cairo_render_draw_subrect_pvm(mtcr, src_pos, src_sz, pos, size,
	    (float *)pvm);
}

/**
 * Composites the rendered surface onto the currently bound OpenGL
 * drawing target.
 * This should be called at regular intervals to draw the results of
 * the cairo render (though not necessarily in lockstep with it). 
 *
 * @param mtcr Renderer instance to be drawn. If a new frame hasn't
 *	been rendered yet, this function simply renders the old buffer
 *	again (or nothing at all if no surface has completed internal
 *	Cairo rendering). You can guarantee that a surface is ready by
 *	first calling mt_cairo_render_once_wait().
 * @param src_pos Origin position within the surface from which to
 *	perform the compositing. This is referenced to the top left
 *	coordinate origin of the Cairo image surface.
 * @param src_sz Size of area to composite from the source image
 *	surface. If you pass `src_pos=ZERO_VECT2` and `src_sz` set to
 *	the entire image surface size to composite the entirety of the
 *	image surface.
 * @param pos Target rendering position of the origin of the rectangle
 *	to which to composite. This is specified in OpenGL coordinates
 *	and is transformed using the provided matrix for final rendering.
 * @param size Target rendering rectangle size to which to composite.
 *	This is specified in OpenGL coordinates and is transformed
 *	using the provided matrix for final rendering.
 * @param pvm The projection-View-Model matrix, which will be used by
 *	the vertex shader to transform the coordinates of the final
 *	rendering rectangle. If you've installed your own custom shader,
 *	this parameter will be passed to your shader in a mat4 uniform
 *	named "pvm".
 */
void
mt_cairo_render_draw_subrect_pvm(mt_cairo_render_t *mtcr,
    vect2_t src_pos, vect2_t src_sz, vect2_t pos, vect2_t size,
    const float *pvm)
{
	GLint old_vao = 0;
	bool_t use_vao;
	double x1 = src_pos.x, x2 = src_pos.x + src_sz.x;
	double y1 = src_pos.y, y2 = src_pos.y + src_sz.y;
	bool cull_front = false;
	vtx_t vtx_buf[4] = {};

	mutex_enter(&mtcr->lock);
	if (!bind_cur_tex(mtcr)) {
		mutex_exit(&mtcr->lock);
		return;
	}
	mutex_exit(&mtcr->lock);

	use_vao = (mtcr->vao != 0 && (!mtcr->ctx_checking ||
	    (mtcr->create_ctx != NULL && glctx_is_current(mtcr->create_ctx))));

	if (use_vao) {
		glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vao);

		glBindVertexArray(mtcr->vao);
		glEnable(GL_BLEND);
	} else if (mtcr->use_ffp) {
		XPLMSetGraphicsState(0, 1, 0, 1, 1, 0, 0);
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadMatrixf(pvm);
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();
		glUseProgram(0);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mtcr->idx_buf);
	} else {
#if	APL
		/*
		 * Leaving this on on MacOS breaks glDrawElements
		 * and makes it perform horribly.
		 */
		glDisableClientState(GL_VERTEX_ARRAY);
#endif	/* !APL */
		XPLMSetGraphicsState(1, 1, 1, 1, 1, 1, 1);
		glBindBuffer(GL_ARRAY_BUFFER, mtcr->vtx_buf);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mtcr->idx_buf);

		glutils_enable_vtx_attr_ptr(mtcr->shader_loc_vtx_pos, 3,
		    GL_FLOAT, GL_FALSE, sizeof (vtx_t), offsetof(vtx_t, pos));
		glutils_enable_vtx_attr_ptr(mtcr->shader_loc_vtx_tex0, 2,
		    GL_FLOAT, GL_FALSE, sizeof (vtx_t), offsetof(vtx_t, tex0));
	}
	prepare_vtx_buffer(mtcr, pos, size, x1, x2, y1, y2, vtx_buf);
	if (!mtcr->use_ffp) {
		ASSERT(mtcr->shader != 0);
		glUseProgram(mtcr->shader);

		glUniformMatrix4fv(mtcr->shader_loc_pvm,
		    1, GL_FALSE, (const float *)pvm);
		glUniform1i(mtcr->shader_loc_tex, 0);
		if (!IS_NULL_VECT(mtcr->monochrome)) {
			glUniform3f(mtcr->shader_loc_color_in,
			    mtcr->monochrome.x, mtcr->monochrome.y,
			    mtcr->monochrome.z);
		}
	}
	if ((size.x < 0 && size.y >= 0) || (size.x >= 0 && size.y < 0)) {
		glCullFace(GL_FRONT);
		cull_front = true;
	}
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);

	/*
	 * State cleanup
	 */
	if (cull_front)
		glCullFace(GL_BACK);

	if (use_vao) {
		glBindVertexArray(old_vao);
	} else if (mtcr->use_ffp) {
		glPopMatrix();
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_COLOR_ARRAY);
		XPLMBindTexture2d(0, 0);
	} else {
		glutils_disable_vtx_attr_ptr(mtcr->shader_loc_vtx_pos);
		glutils_disable_vtx_attr_ptr(mtcr->shader_loc_vtx_tex0);
		/*
		 * X-Plane needs to know that we have unbound the texture
		 * previously bound in slot #0. Otherwise we can cause
		 * glitchy window rendering.
		 */
		XPLMBindTexture2d(0, 0);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glUseProgram(0);
}

/**
 * Configures the mt_cairo_render_t instance to use an asynchronous uploader.
 * See mt_cairo_uploader_new() for more information.
 *
 * @param mtcr Renderer to configure for asynchronous uploading.
 * @param mtul Uploader to use for renderer. If you pass NULL here,
 *	the renderer is returned to synchronous uploading.
 */
void
mt_cairo_render_set_uploader(mt_cairo_render_t *mtcr, mt_cairo_uploader_t *mtul)
{
	mt_cairo_uploader_t *mtul_old;

	ASSERT(mtcr != NULL);

	if (mtul == mtcr->mtul || coherent)
		return;

	mtul_old = mtcr->mtul;
	if (mtul_old != NULL) {
		mutex_enter(&mtul_old->lock);
		ASSERT(mtul_old->refcnt != 0);
		mtul_old->refcnt--;
		if (list_link_active(&mtcr->mtul_queue_node))
			list_remove(&mtul_old->queue, mtcr);
		mutex_exit(&mtul_old->lock);
	}

	mutex_enter(&mtcr->lock);
	mtcr->mtul = mtul;
	mutex_exit(&mtcr->lock);

	/*
	 * Because now the main rendering thread will no longer cause us
	 * to upload in-sync, in case we have a pending frame, immediately
	 * add us to the MTUL.
	 */
	if (mtul != NULL) {
		mutex_enter(&mtul->lock);
		mtul->refcnt++;
		if (!list_link_active(&mtcr->mtul_queue_node)) {
			list_insert_tail(&mtul->queue, mtcr);
			cv_broadcast(&mtcr->mtul->cv_queue);
		}
		mutex_exit(&mtul->lock);
	}
}

/**
 * Returns the asynchronous uploader used by an mt_cairo_render_t,
 * or NULL if the renderer doesn't utilize asynchronous uploading.
 */
mt_cairo_uploader_t *
mt_cairo_render_get_uploader(mt_cairo_render_t *mtcr)
{
	mt_cairo_uploader_t *mtul;

	ASSERT(mtcr != NULL);
	mutex_enter(&mtcr->lock);
	mtul = mtcr->mtul;
	mutex_exit(&mtcr->lock);

	return (mtul);
}

/**
 * @return The OpenGL texture object of the surface that has currently
 * completed rendering. If no surface is ready yet, returns 0 instead.
 */
GLuint
mt_cairo_render_get_tex(mt_cairo_render_t *mtcr)
{
	GLuint tex;

	mutex_enter(&mtcr->lock);

	if (mtcr->present_rs != -1) {
		/* Upload & apply the texture if it has changed */
		if (mtcr->dirty && mtcr->mtul == NULL) {
			rs_upload(mtcr, &mtcr->rs[!mtcr->present_rs]);
			mtcr->dirty = B_FALSE;
		}
		mtcr_tex_apply(mtcr, B_FALSE);
		tex = mtcr->tex;
	} else {
		/* No texture ready yet */
		tex = 0;
	}

	mutex_exit(&mtcr->lock);

	return (tex);
}

/**
 * @return The width of the cairo image surface in pixels. This is equal
 *	to the "w" argument originally passed to mt_cairo_render_init().
 */
unsigned
mt_cairo_render_get_width(mt_cairo_render_t *mtcr)
{
	ASSERT(mtcr != NULL);
	return (mtcr->w);
}

/**
 * @return The height of the cairo image surface in pixels. This is equal
 *	to the "h" argument originally passed to mt_cairo_render_init().
 */
unsigned
mt_cairo_render_get_height(mt_cairo_render_t *mtcr)
{
	ASSERT(mtcr != NULL);
	return (mtcr->h);
}

void
mt_cairo_render_blit_back2front(mt_cairo_render_t *mtcr,
    const mtcr_rect_t *rects, size_t num)
{
	LACF_UNUSED(mtcr);
	LACF_UNUSED(rects);
	LACF_UNUSED(num);
}

/**
 * Enables context integrity checking for the mt_cairo_render_t internals.
 * @note This is an internal development option for libacfutils, so unless
 *	you are debugging libacfutils itself, you probably won't need to
 *	call this.
 */
void
mt_cairo_render_set_ctx_checking_enabled(mt_cairo_render_t *mtcr,
    bool_t flag)
{
	mtcr->ctx_checking = flag;
}

static void
mtul_upload(mt_cairo_render_t *mtcr, list_t *ul_inprog_list)
{
	ASSERT(mtcr != NULL);
	ASSERT(ul_inprog_list != NULL);

	mutex_enter(&mtcr->lock);

	ASSERT(!coherent);
	if (mtcr->render_rs == -1) {
		/*
		 * No frame ready, this happens if we got added to the
		 * uploader's work queue in mt_cairo_render_set_uploader.
		 */
		mutex_exit(&mtcr->lock);
		return;
	}
	if (mtcr->dirty) {
		render_surf_t *rs = &mtcr->rs[mtcr->render_rs];
		rs_upload(mtcr, rs);
		ASSERT3P(mtcr->sync, ==, NULL);
		mtcr->sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		ASSERT(!list_link_active(&mtcr->ul_inprog_node));
		list_insert_tail(ul_inprog_list, mtcr);
	}
	mutex_exit(&mtcr->lock);
}

static bool_t
mtul_try_complete_ul(mt_cairo_render_t *mtcr, list_t *ul_inprog_list)
{
	enum { UL_TIMEOUT = 500000 /* ns */ };

	ASSERT(mtcr != NULL);
	ASSERT(mtcr->sync != NULL);
	ASSERT(ul_inprog_list != NULL);

	if (glClientWaitSync(mtcr->sync, GL_SYNC_FLUSH_COMMANDS_BIT,
	    UL_TIMEOUT) == GL_TIMEOUT_EXPIRED) {
		return (B_FALSE);
	}
	/*
	 * We need to remove the surface from the ul_inprog_list BEFORE
	 * resetting rs->dirty, otherwise the mtcr could attempt to emit
	 * another frame. This could try to double-add the surface while
	 * it's still active on the ul_inprog_list.
	 */
	list_remove(ul_inprog_list, mtcr);
	ASSERT(!coherent);

	mutex_enter(&mtcr->lock);

	glDeleteSync(mtcr->sync);
	mtcr->sync = NULL;
	ASSERT(mtcr->dirty);
	mtcr->dirty = B_FALSE;
	mtcr->texed = B_FALSE;
	mtcr->present_rs = mtcr->render_rs;
	cv_broadcast(&mtcr->render_done_cv);
	mutex_exit(&mtcr->lock);

	return (B_TRUE);
}

static void
mtul_drain_queue(mt_cairo_uploader_t *mtul)
{
	list_t ul_inprog_list;

	ASSERT(mtul != NULL);
	ASSERT_MUTEX_HELD(&mtul->lock);

	list_create(&ul_inprog_list, sizeof (mt_cairo_render_t),
	    offsetof(mt_cairo_render_t, ul_inprog_node));

	do {
		mt_cairo_render_t *mtcr;
		/*
		 * Dequeue new work assignments and start the upload.
		 */
		while ((mtcr = list_remove_head(&mtul->queue)) != NULL) {
			mutex_exit(&mtul->lock);
			mtul_upload(mtcr, &ul_inprog_list);
			mutex_enter(&mtul->lock);
		}
		/*
		 * No more uploads pending for start. Now see if we can
		 * complete an upload.
		 */
		mtcr = list_head(&ul_inprog_list);
		if (mtcr != NULL) {
			bool_t ul_done;

			mutex_exit(&mtul->lock);
			ul_done = mtul_try_complete_ul(mtcr, &ul_inprog_list);
			mutex_enter(&mtul->lock);
			if (ul_done) {
				/*
				 * The rs has already been removed from
				 * the ul_inprog_list.
				 */
				cv_broadcast(&mtul->cv_done);
			}
		}
	} while (list_count(&ul_inprog_list) != 0);

	list_destroy(&ul_inprog_list);
}

/*
 * Actual upload worker thread main function.
 *
 * There is some nonsense in the way Nvidia synchronizes texture resources
 * between multiple shared OpenGL contexts which means we could get flickering
 * of the old render if we simply pushed new pixels into the PBO & texture
 * that was being used by the foreground renderer.
 *
 * To avoid this, we generate a COMPLETELY new texture + PBO set for the
 * upload. When these have completed uploading, we do an atomic texture & PBO
 * swap in the render surface. At the same time, the foreground renderer
 * is isolated from this by taking full ownership of the textures while it
 * is using them to render.
 */
static void
mtul_worker(void *arg)
{
	mt_cairo_uploader_t *mtul;

	thread_set_name("mtul_worker");

	ASSERT(arg != NULL);
	mtul = arg;

	ASSERT(mtul->ctx != NULL);
	VERIFY(glctx_make_current(mtul->ctx));
	VERIFY3U(glewInit(), ==, GLEW_OK);

	mutex_enter(&mtul->lock);

	while (!mtul->shutdown) {
		mtul_drain_queue(mtul);
		/* pause for more work */
		if (list_head(&mtul->queue) == NULL)
			cv_wait(&mtul->cv_queue, &mtul->lock);
	}

	mutex_exit(&mtul->lock);

	VERIFY(glctx_make_current(NULL));
}

/**
 * Initializes an asynchronous render surface uploader. This should be
 * called from the main X-Plane thread.
 *
 * ## Background
 *
 * An mt_cairo_render_t runs its Cairo rendering operations in a background
 * thread. However, to present the final rendered image on screen, the
 * resulting image must first be uploaded to the GPU. This requires having
 * an OpenGL context bound to the current thread, however, OpenGL contexts
 * cannot be shared between threads. The workaround would be to create a
 * new context in every mt_cairo_render_t worker thread, however, given that
 * there can be dozens of render workers, this would generate dozens of
 * OpenGL contexts, which isn't very efficient either. In lieu of a better
 * mechanism, mt_cairo_render would simply perform the final image upload
 * to the GPU during the mt_cairo_render_draw operation. Which this was
 * utilizing pixel-buffer-objects to streamline the uploading process and
 * avoid OpenGL pipeline stalls, there was still some memcpy'ing involved,
 * which had a non-trivial cost when invoked from the main X-Plane thread.
 *
 * mt_cairo_uploader_t solves all of these issues. It is a separate
 * background worker thread with its own OpenGL context dedicated to doing
 * nothing but uploading of finished renders to the GPU. Once created, the
 * uploader goes into a wait-sleep, awaiting new work assignments from
 * renderers. Once a render is finished, the uploader is notified,
 * immediately wakes up and uploads the finished render to the GPU. This
 * happens without waiting for the main X-Plane thread to come around
 * wanting to draw a particular mt_cairo_render_t. With this infrastructure
 * in place, calls to mt_cairo_render_draw no longer block for texture
 * uploading.
 *
 * This machinery isn't automatically enabled on all instances of
 * mt_cairo_render_t however. To utilize this mechanism, you should create
 * an mt_cairo_uploader and associate it with all your mt_cairo_render
 * instances using mt_cairo_render_set_uploader. You generally only ever
 * need a single uploader for all renderers you create. Thus, following
 * this pattern is a good idea:
 *```
 *	mt_cairo_uploader_t *uploader = mt_cairo_uploader_new();
 *	mt_cairo_render_t *mtcr1 = mt_cairo_render_init(...);
 *	mt_cairo_render_set_uploader(mtcr1, uploader);
 *	mt_cairo_render_t *mtcr2 = mt_cairo_render_init(...);
 *	mt_cairo_render_set_uploader(mtcr2, uploader);
 *	mt_cairo_render_t *mtcr3 = mt_cairo_render_init(...);
 *	mt_cairo_render_set_uploader(mtcr3, uploader);
 *	// ...use the renderers as normal...
 *	mt_cairo_render_fini(mtcr3);
 *	mt_cairo_render_fini(mtcr2);
 *	mt_cairo_render_fini(mtcr1);
 *	mt_cairo_uploader_fini(uploader);	<- uploader fini must go last
 *```
 */
mt_cairo_uploader_t *
mt_cairo_uploader_init(void)
{
	mt_cairo_uploader_t *mtul = safe_calloc(1, sizeof (*mtul));
	glctx_t *ctx_main;

	mt_cairo_render_glob_init(B_TRUE);
	if (coherent) {
		/*
		 * In coherent mode, just create a stub uploader and return.
		 * Don't actually do any uploading.
		 */
		return (mtul);
	}
	ctx_main = glctx_get_current();
	ASSERT(ctx_main != NULL);

	mtul->ctx = glctx_create_invisible(glctx_get_xplane_win_ptr(),
	    ctx_main, 2, 1, B_FALSE, B_FALSE);
	glctx_destroy(ctx_main);
	if (mtul->ctx == NULL) {
		free(mtul);
		return (NULL);
	}
	mutex_init(&mtul->lock);
	cv_init(&mtul->cv_queue);
	cv_init(&mtul->cv_done);
	list_create(&mtul->queue, sizeof (mt_cairo_render_t),
	    offsetof(mt_cairo_render_t, mtul_queue_node));

	VERIFY(thread_create(&mtul->worker, mtul_worker, mtul));

	return (mtul);
}

/**
 * Frees an mt_cairo_uploader_t and disposes of all of its resources.
 * This must be called after all mt_cairo_render_t instances using it
 * have either been destroyed, or have had their uploader link removed
 * by a call to mt_cairo_render_set_uploader(mtcr, NULL).
 */
void
mt_cairo_uploader_fini(mt_cairo_uploader_t *mtul)
{
	ASSERT(mtul != NULL);

	if (coherent) {
		ZERO_FREE(mtul);
		return;
	}
	ASSERT0(mtul->refcnt);
	ASSERT0(list_count(&mtul->queue));

	mutex_enter(&mtul->lock);
	mtul->shutdown = B_TRUE;
	cv_broadcast(&mtul->cv_queue);
	mutex_exit(&mtul->lock);
	thread_join(&mtul->worker);

	ASSERT(mtul->ctx != NULL);
	glctx_destroy(mtul->ctx);
	list_destroy(&mtul->queue);
	mutex_destroy(&mtul->lock);
	cv_destroy(&mtul->cv_queue);
	cv_destroy(&mtul->cv_done);

	memset(mtul, 0, sizeof (*mtul));
	ZERO_FREE(mtul);
}
