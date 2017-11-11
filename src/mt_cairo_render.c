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
/*
 * mt_cairo_render is a multi-threaded cairo rendering surface with
 * built-in double-buffering and OpenGL compositing. You only need to
 * provide a callback that renders into the surface using a passed
 * cairo_t and then call mt_cairo_render_draw at regular intervals to
 * display the rendered result.
 */

#include <acfutils/assert.h>
#include <acfutils/geom.h>
#include <acfutils/mt_cairo_render.h>
#include <acfutils/thread.h>
#include <acfutils/time.h>

#include <XPLMGraphics.h>

#if	IBM
#include <gl.h>
#elif	APL
#include <OpenGL/gl.h>
#else	/* LIN */
#include <GL/gl.h>
#endif	/* LIN */

typedef	struct {
	bool_t		chg;

	GLuint		tex;
	cairo_surface_t	*surf;
	cairo_t		*cr;
} render_surf_t;

struct mt_cairo_render_s {
	unsigned		w, h;
	double			fps;
	mt_cairo_render_cb_t	render_cb;
	mt_cairo_fini_cb_t	fini_cb;
	void			*userinfo;

	int			cur_rs;
	render_surf_t		rs[2];

	thread_t		thr;
	condvar_t		cv;
	mutex_t			lock;
	bool_t			started;
	bool_t			shutdown;
};

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

const char *
ft_err2str(FT_Error err)
{
	for (int i = 0; ft_errors[i].err_msg != NULL; i++)
		if (ft_errors[i].err_code == err)
			return (ft_errors[i].err_msg);
	return (NULL);
}

static void
worker(mt_cairo_render_t *mtcr)
{
	mutex_enter(&mtcr->lock);

	while (!mtcr->shutdown) {
		render_surf_t *rs;

		mutex_exit(&mtcr->lock);

		/* always draw into the non-current texture */
		rs = &mtcr->rs[!mtcr->cur_rs];

		ASSERT(mtcr->render_cb != NULL);
		mtcr->render_cb(rs->cr, mtcr->w, mtcr->h, mtcr->userinfo);
		rs->chg = B_TRUE;

		mutex_enter(&mtcr->lock);
		mtcr->cur_rs = !mtcr->cur_rs;

		cv_timedwait(&mtcr->cv, &mtcr->lock, microclock() +
		    SEC2USEC(1.0 / mtcr->fps));
	}
	mutex_exit(&mtcr->lock);
}

/*
 * Creates a new mt_cairo_render_t surface.
 * @param w Width of the rendered surface (in pixels).
 * @param h Height of the rendered surface (in pixels).
 * @param fps Framerate at which the surface should be rendered.
 *	This can be changed at any time later.
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
mt_cairo_render_init(unsigned w, unsigned h, double fps,
    mt_cairo_init_cb_t init_cb, mt_cairo_render_cb_t render_cb,
    mt_cairo_fini_cb_t fini_cb, void *userinfo)
{
	mt_cairo_render_t *mtcr = calloc(1, sizeof (*mtcr));

	ASSERT(w != 0);
	ASSERT(h != 0);
	ASSERT3F(fps, >, 0);
	ASSERT(render_cb != NULL);

	mtcr->w = w;
	mtcr->h = h;
	mtcr->cur_rs = -1;
	mtcr->render_cb = render_cb;
	mtcr->fini_cb = fini_cb;
	mtcr->userinfo = userinfo;
	mtcr->fps = fps;

	mutex_init(&mtcr->lock);
	cv_init(&mtcr->cv);

	for (int i = 0; i < 2; i++) {
		render_surf_t *rs = &mtcr->rs[i];

		glGenTextures(1, &rs->tex);
		glBindTexture(GL_TEXTURE_2D, rs->tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
		    GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
		    GL_LINEAR);

		rs->surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		    mtcr->w, mtcr->h);
		rs->cr = cairo_create(rs->surf);
		if (init_cb != NULL && !init_cb(rs->cr, userinfo)) {
			mt_cairo_render_fini(mtcr);
			return (NULL);
		}
	}

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

	for (int i = 0; i < 2; i++) {
		render_surf_t *rs = &mtcr->rs[i];

		if (rs->cr != NULL) {
			if (mtcr->fini_cb != NULL)
				mtcr->fini_cb(rs->cr, mtcr->userinfo);
			cairo_destroy(rs->cr);
			cairo_surface_destroy(rs->surf);
		}
		if (rs->tex != 0)
			glDeleteTextures(1, &rs->tex);
	}

	mutex_destroy(&mtcr->lock);
	cv_destroy(&mtcr->cv);

	free(mtcr);
}

void
mt_cairo_render_set_fps(mt_cairo_render_t *mtcr, double fps)
{
	ASSERT3F(fps, >, 0);
	mtcr->fps = fps;
}

/*
 * Draws the rendered surface at offset pos.xy, at a size of size.xy.
 * This should be called at regular intervals to draw the results of
 * the cairo render (though not necessarily in lockstep with it). If
 * a new frame hasn't been rendered yet, this function simply renders
 * the old buffer again.
 */
void
mt_cairo_render_draw(mt_cairo_render_t *mtcr, vect2_t pos, vect2_t size)
{
	render_surf_t *rs;

	mutex_enter(&mtcr->lock);

	if (mtcr->cur_rs == -1) {
	        mutex_exit(&mtcr->lock);
		return;
	}
	ASSERT3S(mtcr->cur_rs, >=, 0);
	ASSERT3S(mtcr->cur_rs, <, 2);

	XPLMSetGraphicsState(0, 1, 0, 0, 1, 0, 0);

	rs = &mtcr->rs[mtcr->cur_rs];
	ASSERT(rs->tex != 0);
	glBindTexture(GL_TEXTURE_2D, rs->tex);
	if (rs->chg) {
		cairo_surface_flush(rs->surf);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mtcr->w, mtcr->h, 0,
		    GL_BGRA, GL_UNSIGNED_BYTE,
		    cairo_image_surface_get_data(rs->surf));
		rs->chg = B_FALSE;
	}

	mutex_exit(&mtcr->lock);

	glBegin(GL_QUADS);
	glTexCoord2f(0, 1);
	glVertex2f(pos.x, pos.y);
	glTexCoord2f(0, 0);
	glVertex2f(pos.x, pos.y + size.y);
	glTexCoord2f(1, 0);
	glVertex2f(pos.x + size.x, pos.y + size.y);
	glTexCoord2f(1, 1);
	glVertex2f(pos.x + size.x, pos.y);
	glEnd();
}

bool_t
try_load_font(const char *fontdir, const char *fontfile, FT_Library ft,
    FT_Face *font, cairo_font_face_t **cr_font)
{
	char *fontpath = mkpathname(fontdir, fontfile, NULL);
	FT_Error err;

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
