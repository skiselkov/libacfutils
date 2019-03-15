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
 * Copyright 2018 Saso Kiselkov. All rights reserved.
 */

#include <stddef.h>
#include <string.h>

#include <acfutils/avl.h>
#include <acfutils/assert.h>
#include <acfutils/glutils.h>
#include <acfutils/safe_alloc.h>
#include <acfutils/shader.h>
#include <acfutils/thread.h>

#ifdef	_USE_MATH_DEFINES
#undef	_USE_MATH_DEFINES
#endif

#include <cglm/cglm.h>

TEXSZ_MK_TOKEN(glutils_quads_vbo);
TEXSZ_MK_TOKEN(glutils_lines_vbo);

typedef struct {
	GLfloat	pos[3];
	GLfloat	tex0[2];
} vtx_t;

typedef struct {
	const void	*instance;
	char		allocd_at[32];
	int64_t		bytes;
	avl_node_t	node;
} texsz_instance_t;

typedef struct {
	const char	*token;
	int64_t		bytes;
	avl_tree_t	instances;
	avl_node_t	node;
} texsz_alloc_t;

static struct {
	bool_t		inited;
	mutex_t		lock;
	int64_t		bytes;
	avl_tree_t	allocs;
} texsz = { .inited = B_FALSE };

static thread_id_t main_thread;

void
glutils_sys_init(void)
{
	main_thread = curthread;
}

/*
 * Kills all OpenGL client state arrays. Call this before enabling the OpenGL
 * client arrays you will need to draw, as otherwise you don't know if some
 * other plugin left them in some misconfigured state.
 */
API_EXPORT void
glutils_disable_all_client_state(void)
{
	static const GLenum disable_caps[] = {
	    GL_COLOR_ARRAY,
	    GL_EDGE_FLAG_ARRAY,
	    GL_FOG_COORD_ARRAY,
	    GL_INDEX_ARRAY,
	    GL_NORMAL_ARRAY,
	    GL_SECONDARY_COLOR_ARRAY,
	    GL_TEXTURE_COORD_ARRAY,
	    GL_VERTEX_ARRAY,
	    0
	};

	if (GLEW_VERSION_3_1)
		return;

	for (int i = 0; disable_caps[i] != 0; i++)
		glDisableClientState(disable_caps[i]);
}

/*
 * Disables all (but at most 32) vertex attribute arrays. This should be
 * used to make sure all OpenGL state is clean at the start of an X-Plane
 * draw callback when using shared vertex array objects.
 */
API_EXPORT void
glutils_disable_all_vtx_attrs(void)
{
	GLint n_attrs;

	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &n_attrs);
	for (int i = 0; i < MIN(n_attrs, 32); i++)
		glDisableVertexAttribArray(i);
}

/*
 * This function is a shorthand for creating an IBO to triangulate an
 * old-style GL_QUADS object (which isn't supported in modern OpenGL anymore).
 * The quad is triangulated in this order: p0-p1-p2 & p0-p2-p3.
 * The IBO is constructed for quads composed num_vtx vertices. This number
 * must be a multiple of 4.
 */
API_EXPORT GLuint
glutils_make_quads_IBO(size_t num_vtx)
{
	GLuint idx_data[num_vtx * 2];
	size_t i, n;
	GLuint buf;

	ASSERT0(num_vtx & 3);

	for (i = 0, n = 0; i < num_vtx; i += 4, n += 6) {
		idx_data[n + 0] = i + 0;
		idx_data[n + 1] = i + 1;
		idx_data[n + 2] = i + 2;

		idx_data[n + 3] = i + 0;
		idx_data[n + 4] = i + 2;
		idx_data[n + 5] = i + 3;
	}

	glGenBuffers(1, &buf);
	VERIFY(buf != 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, n * sizeof (*idx_data),
	    idx_data, GL_STATIC_DRAW);

	return (buf);
}

API_EXPORT void
glutils_init_2D_quads_impl(glutils_quads_t *quads, const char *filename,
    int line, const vect2_t *p, const vect2_t *t, size_t num_pts)
{
	vect3_t p_3d[num_pts];

	for (size_t i = 0; i < num_pts; i++)
		p_3d[i] = VECT3(p[i].x, p[i].y, 0);

	glutils_init_3D_quads_impl(quads, filename, line, p_3d, t, num_pts);
}

API_EXPORT void
glutils_init_3D_quads_impl(glutils_quads_t *quads, const char *filename,
    int line, const vect3_t *p, const vect2_t *t, size_t num_pts)
{
	vtx_t vtx_data[num_pts];
	GLint old_vao = 0;

	ASSERT0(num_pts & 3);
	ASSERT(p != NULL);

	memset(quads, 0, sizeof (*quads));
	memset(vtx_data, 0, sizeof (vtx_data));

	for (size_t i = 0; i < num_pts; i++) {
		vtx_data[i].pos[0] = p[i].x;
		vtx_data[i].pos[1] = p[i].y;
		vtx_data[i].pos[2] = p[i].z;
		if (t != NULL) {
			vtx_data[i].tex0[0] = t[i].x;
			vtx_data[i].tex0[1] = t[i].y;
		}
	}

	if (GLEW_VERSION_3_0 && curthread != main_thread) {
		glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vao);

		glGenVertexArrays(1, &quads->vao);
		glBindVertexArray(quads->vao);
		VERIFY(quads->vao != 0);
	} else {
		quads->vao = 0;
	}

	glGenBuffers(1, &quads->vbo);
	VERIFY(quads->vbo != 0);
	quads->num_vtx = num_pts;

	glBindBuffer(GL_ARRAY_BUFFER, quads->vbo);
	glBufferData(GL_ARRAY_BUFFER, quads->num_vtx * sizeof (vtx_t),
	    vtx_data, GL_STATIC_DRAW);
	quads->ibo = glutils_make_quads_IBO(num_pts);

	IF_TEXSZ(TEXSZ_ALLOC_BYTES_INSTANCE(glutils_quads_vbo, quads,
	    filename, line, quads->num_vtx * sizeof (vtx_t)));

	if (GLEW_VERSION_3_0 && curthread != main_thread)
		glBindVertexArray(old_vao);
}

API_EXPORT void
glutils_destroy_quads(glutils_quads_t *quads)
{
	if (quads->vao != 0)
		glDeleteVertexArrays(1, &quads->vao);
	if (quads->vbo != 0) {
		IF_TEXSZ(TEXSZ_FREE_BYTES_INSTANCE(glutils_quads_vbo, quads,
		    quads->num_vtx * sizeof (vtx_t)));
		glDeleteBuffers(1, &quads->vbo);
	}
	if (quads->ibo != 0)
		glDeleteBuffers(1, &quads->ibo);
	memset(quads, 0, sizeof (*quads));
}

static void
glutils_draw_common(GLenum mode, GLuint vao, GLuint vbo, GLuint ibo,
    bool_t *setup, size_t num_vtx, GLint prog)
{
	GLint pos_loc = -1, tex0_loc = -1;

	ASSERT(vbo != 0);
	ASSERT(prog != 0);

	if (vao != 0) {
		/* Vertex arrays supported */
		glBindVertexArray(vao);
	}

	if (vao == 0 || !(*setup)) {
		GLint pos_loc, tex0_loc;

		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

		pos_loc = glGetAttribLocation(prog, "vtx_pos");
		tex0_loc = glGetAttribLocation(prog, "vtx_tex0");

		glutils_enable_vtx_attr_ptr(pos_loc, 3, GL_FLOAT, GL_FALSE,
		    sizeof (vtx_t), offsetof(vtx_t, pos));
		glutils_enable_vtx_attr_ptr(tex0_loc, 2, GL_FLOAT, GL_FALSE,
		    sizeof (vtx_t), offsetof(vtx_t, tex0));
		*setup = B_TRUE;
	}

	if (ibo != 0)
		glDrawElements(mode, num_vtx, GL_UNSIGNED_INT, NULL);
	else
		glDrawArrays(mode, 0, num_vtx);

	if (vao != 0) {
		glBindVertexArray(0);
	} else {
		glutils_disable_vtx_attr_ptr(pos_loc);
		glutils_disable_vtx_attr_ptr(tex0_loc);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

API_EXPORT void
glutils_draw_quads(glutils_quads_t *quads, GLint prog)
{
	/*
	 * num_vtx is the number of underlying vertex data array entries,
	 * but glDrawElements needs the number of indices, which is always
	 * exactly 1.5x as many.
	 */
	glutils_draw_common(GL_TRIANGLES, quads->vao, quads->vbo, quads->ibo,
	    &quads->setup, quads->num_vtx + quads->num_vtx / 2, prog);
}


API_EXPORT void
glutils_init_3D_lines_impl(glutils_lines_t *lines, const char *filename,
    int line, vect3_t *p, size_t num_pts)
{
	vtx_t vtx_data[num_pts];
	GLint old_vao = 0;

	ASSERT(p != NULL);

	memset(lines, 0, sizeof (*lines));
	memset(vtx_data, 0, sizeof (vtx_data));
	for (size_t i = 0; i < num_pts; i++) {
		vtx_data[i].pos[0] = p[i].x;
		vtx_data[i].pos[1] = p[i].y;
		vtx_data[i].pos[2] = p[i].z;
	}

	if (GLEW_VERSION_3_0 && curthread != main_thread) {
		glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vao);

		glGenVertexArrays(1, &lines->vao);
		glBindVertexArray(lines->vao);
		VERIFY(lines->vao != 0);
	} else {
		lines->vao = 0;
	}

	glGenBuffers(1, &lines->vbo);
	VERIFY(lines->vbo != 0);
	lines->num_vtx = num_pts;

	glBindBuffer(GL_ARRAY_BUFFER, lines->vbo);
	glBufferData(GL_ARRAY_BUFFER, lines->num_vtx * sizeof (vtx_t),
	    vtx_data, GL_STATIC_DRAW);

	if (glutils_texsz_inited()) {
		TEXSZ_ALLOC_BYTES_INSTANCE(glutils_lines_vbo, lines,
		    filename, line, lines->num_vtx * sizeof (vtx_t));
	}

	if (GLEW_VERSION_3_0 && curthread != main_thread)
		glBindVertexArray(old_vao);
	else
		glBindBuffer(GL_ARRAY_BUFFER, 0);
}

API_EXPORT void
glutils_draw_lines(glutils_lines_t *lines, GLint prog)
{
	glutils_draw_common(GL_LINE_STRIP, lines->vao, lines->vbo, 0,
	    &lines->setup, lines->num_vtx, prog);
}

API_EXPORT void
glutils_destroy_lines(glutils_lines_t *lines)
{
	if (lines->vbo != 0) {
		if (glutils_texsz_inited()) {
			TEXSZ_FREE_BYTES_INSTANCE(glutils_lines_vbo, lines,
			    lines->num_vtx * sizeof (vtx_t));
		}
		glDeleteBuffers(1, &lines->vbo);
		memset(lines, 0, sizeof (*lines));
	}
}

API_EXPORT void
glutils_vp2pvm(GLfloat pvm[16])
{
	int vp[4];
	mat4 pvm_mat;

	glGetIntegerv(GL_VIEWPORT, vp);
	glm_ortho(vp[0], vp[0] + vp[2], vp[1], vp[1] + vp[3], 0, 1, pvm_mat);
	memcpy(pvm, pvm_mat, sizeof (GLfloat) * 16);
}

static int
texsz_alloc_compar(const void *a, const void *b)
{
	const texsz_alloc_t *ta = a, *tb = b;

	if (ta->token < tb->token)
		return (-1);
	if (ta->token > tb->token)
		return (1);

	return (0);
}

static int
texsz_instance_compar(const void *a, const void *b)
{
	const texsz_instance_t *ta = a, *tb = b;

	if (ta->instance < tb->instance)
		return (-1);
	if (ta->instance > tb->instance)
		return (1);

	return (0);
}

API_EXPORT void
glutils_texsz_init(void)
{
	texsz.inited = B_TRUE;
	mutex_init(&texsz.lock);
	texsz.bytes = 0;
	avl_create(&texsz.allocs, texsz_alloc_compar,
	    sizeof (texsz_alloc_t), offsetof(texsz_alloc_t, node));
}

API_EXPORT void
glutils_texsz_fini(void)
{
	void *cookie;
	texsz_alloc_t *ta;

	if (!texsz.inited)
		return;
	mutex_destroy(&texsz.lock);
	cookie = NULL;
	while ((ta = avl_destroy_nodes(&texsz.allocs, &cookie)) != NULL) {
		void *cookie2 = NULL;
		texsz_instance_t *ti;

		ASSERT(ta->token != NULL);
		if (ta->bytes != 0) {
			for (ti = avl_first(&ta->instances); ti != NULL;
			    ti = AVL_NEXT(&ta->instances, ti)) {
				logMsg("%s:  %p  %ld  (at: %s)\n", ta->token,
				    ti->instance, (long)ti->bytes,
				    ti->allocd_at);
			}
			VERIFY_MSG(0, "Texture allocation leak: "
			    "%s leaked %ld bytes", ta->token, (long)ta->bytes);
		}
		cookie2 = NULL;
		while ((ti = avl_destroy_nodes(&ta->instances, &cookie2)) !=
		    NULL)
			free(ti);
		avl_destroy(&ta->instances);
		free(ta);
	}
	avl_destroy(&texsz.allocs);
}

static inline void
texsz_incr(const char *token, const void *instance, const char *filename,
    int line, int64_t bytes)
{
	texsz_alloc_t srch = { .token = token };
	texsz_alloc_t *ta;
	avl_index_t where;

	mutex_enter(&texsz.lock);

	ASSERT_MSG(texsz.bytes + bytes >= 0, "Texture size accounting error "
	    "(incr %ld bytes)", (long)bytes);
	texsz.bytes += bytes;
	ta = avl_find(&texsz.allocs, &srch, &where);
	if (ta == NULL) {
		ta = safe_calloc(1, sizeof (*ta));
		ta->token = token;
		avl_create(&ta->instances, texsz_instance_compar,
		    sizeof (texsz_instance_t),
		    offsetof(texsz_instance_t, node));
		avl_insert(&texsz.allocs, ta, where);
	}
	ASSERT_MSG(ta->bytes + bytes >= 0, "Texture size accounting zone "
	    "underflow error (incr %ld bytes in zone %s instance %p)",
	    (long)bytes, ta->token, instance);
	ta->bytes += bytes;

	if (instance != NULL) {
		texsz_instance_t srch_ti = { .instance = instance };
		texsz_instance_t *ti;
		avl_index_t where_ti;

		ti = avl_find(&ta->instances, &srch_ti, &where_ti);
		if (ti == NULL) {
			ASSERT_MSG(bytes >= 0, "Texture size accounting error "
			    "(incr %ld bytes in zone %s instance %p, but "
			    "instance is empty).", (long)bytes, ta->token,
			    instance);
			ti = safe_calloc(1, sizeof (*ti));
			ti->instance = instance;
			avl_insert(&ta->instances, ti, where_ti);
		}
		ASSERT_MSG(ti->bytes + bytes >= 0, "Texture size accounting "
		    "instance underflow error (incr %ld bytes in zone %s "
		    "instance %p)", (long)bytes, ta->token, instance);
		ti->bytes += bytes;
		if (filename != NULL && snprintf(ti->allocd_at,
		    sizeof (ti->allocd_at), "%s:%d", filename, line) >=
		    (int)sizeof (ti->allocd_at)) {
			int l = strlen(filename);
			int off = MAX(l - 26, 0);
			snprintf(ti->allocd_at, sizeof (ti->allocd_at),
			    "%s:%d", &filename[off], line);
		}
		if (ti->bytes == 0) {
			avl_remove(&ta->instances, ti);
			free(ti);
		}
	}

	mutex_exit(&texsz.lock);
}

static inline int64_t
texsz_bytes(GLenum format, GLenum type, unsigned w, unsigned h)
{
	int channels, bpc;

	switch (format) {
	case GL_RG:
	case GL_RG_INTEGER:
		channels = 2;
		break;
	case GL_RGB:
	case GL_BGR:
	case GL_RGB_INTEGER:
	case GL_BGR_INTEGER:
		channels = 3;
		break;
	case GL_RGBA:
	case GL_BGRA:
	case GL_RGBA_INTEGER:
	case GL_BGRA_INTEGER:
		channels = 4;
		break;
	default:
		channels = 1;
		break;
	}

	switch (type) {
	case GL_UNSIGNED_BYTE:
	case GL_BYTE:
	case GL_UNSIGNED_SHORT:
	case GL_SHORT:
		bpc = 2;
		break;
	case GL_UNSIGNED_INT:
	case GL_INT:
	case GL_FLOAT:
		bpc = 4;
		break;
	default:
		bpc = 1;
		break;
	}

	return (channels * bpc * w * h);
}

API_EXPORT void
glutils_texsz_alloc(const char *token, const void *instance,
    const char *filename, int line, GLenum format, GLenum type,
    unsigned w, unsigned h)
{
	ASSERT(texsz.inited);
	texsz_incr(token, instance, filename, line,
	    texsz_bytes(format, type, w, h));
}

API_EXPORT void
glutils_texsz_free(const char *token, const void *instance,
    GLenum format, GLenum type, unsigned w, unsigned h)
{
	ASSERT(texsz.inited);
	texsz_incr(token, instance, NULL, -1, -texsz_bytes(format, type, w, h));
}

API_EXPORT void
glutils_texsz_alloc_bytes(const char *token, const void *instance,
    const char *filename, int line, int64_t bytes)
{
	ASSERT(texsz.inited);
	texsz_incr(token, instance, filename, line, bytes);
}

API_EXPORT void
glutils_texsz_free_bytes(const char *token, const void *instance, int64_t bytes)
{
	ASSERT(texsz.inited);
	texsz_incr(token, instance, NULL, -1, -bytes);
}

API_EXPORT uint64_t
glutils_texsz_get(void)
{
	ASSERT(texsz.inited);
	return (texsz.bytes);
}

API_EXPORT void
glutils_texsz_enum(glutils_texsz_enum_cb_t cb, void *userinfo)
{
	ASSERT(cb != NULL);

	for (texsz_alloc_t *ta = avl_first(&texsz.allocs); ta != NULL;
	    ta = AVL_NEXT(&texsz.allocs, ta)) {
		cb(ta->token, ta->bytes, userinfo);
	}
}

API_EXPORT bool_t
glutils_texsz_inited(void)
{
	return (texsz.inited);
}

bool_t
glutils_nsight_debugger_present(void)
{
#if	!APL
	for (int i = 0; environ[i] != NULL; i++) {
		if (strncmp(environ[i], "NSIGHT", 6) == 0 ||
		    strncmp(environ[i], "NVIDIA_PROCESS", 14) == 0)
			return (B_TRUE);
	}
#endif	/* !APL */
	/* NSight doesn't exist for MacOS */
	return (B_FALSE);
}

struct glutils_nl_s {
	size_t		num_pts;
	GLuint		vao;
	GLuint		vbo;
	GLuint		ibo;

	GLuint		last_prog;
	struct {
		/* program uniforms locations */
		GLint	vp;
		GLint	semi_width;
		/* program attribute locations */
		GLint	seg_here;
		GLint	seg_start;
		GLint	seg_end;
	} loc;
};

typedef struct {
	vec3	seg_here;
	vec3	seg_start;
	vec3	seg_end;
} nl_vtx_data_t;

glutils_nl_t *
glutils_nl_alloc_2D(const vec2 *pts, size_t num_pts)
{
	vec3 *pts_3d = safe_calloc(num_pts, sizeof (*pts_3d));
	glutils_nl_t *nl;

	ASSERT(pts != NULL);
	ASSERT3U((num_pts & 1), ==, 0);

	for (size_t i = 0; i < num_pts; i++) {
		pts_3d[i][0] = pts[i][0];
		pts_3d[i][1] = pts[i][1];
		pts_3d[i][2] = 0.0;
	}
	nl = glutils_nl_alloc_3D(pts_3d, num_pts);
	free(pts_3d);

	return (nl);
}

glutils_nl_t *
glutils_nl_alloc_3D(const vec3 *pts, size_t num_pts)
{
	size_t data_bytes;
	nl_vtx_data_t *data;
	glutils_nl_t *nl = safe_calloc(1, sizeof (*nl));

	ASSERT(pts != NULL);
	ASSERT3U((num_pts & 1), ==, 0);

	data_bytes = 2 * num_pts * sizeof (*data);
	data = safe_malloc(data_bytes);

	for (size_t i = 0; i < num_pts; i += 2) {
		size_t off = i * 2;
		memcpy(&data[off + 0].seg_here, pts[i], sizeof (vec3));
		memcpy(&data[off + 1].seg_here, pts[i], sizeof (vec3));
		memcpy(&data[off + 2].seg_here, pts[i + 1], sizeof (vec3));
		memcpy(&data[off + 3].seg_here, pts[i + 1], sizeof (vec3));

		for (size_t j = 0; j < 4; j++) {
			memcpy(&data[off + j].seg_start, pts[i],
			    sizeof (vec3));
			memcpy(&data[off + j].seg_end, pts[i + 1],
			    sizeof (vec3));
		}
	}

	nl->num_pts = num_pts;
	if (GLEW_VERSION_3_0 && curthread != main_thread) {
		glGenVertexArrays(1, &nl->vao);
		VERIFY(nl->vao != 0);
		glBindVertexArray(nl->vao);
	}
	glGenBuffers(1, &nl->vbo);
	VERIFY(nl->vbo != 0);
	glBindBuffer(GL_ARRAY_BUFFER, nl->vbo);
	glBufferData(GL_ARRAY_BUFFER, data_bytes, data, GL_STATIC_DRAW);

	nl->ibo = glutils_make_quads_IBO(num_pts * 2);
	/* glutils_make_quads_IBO binds the generated element buffer */

	nl->loc.vp = -1;
	nl->loc.semi_width = -1;
	nl->loc.seg_here = -1;
	nl->loc.seg_start = -1;
	nl->loc.seg_end = -1;

	if (GLEW_VERSION_3_0 && curthread != main_thread)
		glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	free(data);

	return (nl);
}

void
glutils_nl_free(glutils_nl_t *nl)
{
	if (nl == NULL)
		return;
	if (nl->vao != 0)
		glDeleteVertexArrays(1, &nl->vao);
	if (nl->vbo != 0)
		glDeleteBuffers(1, &nl->vbo);
	if (nl->ibo != 0)
		glDeleteBuffers(1, &nl->ibo);
	free(nl);
}

static void
nl_setup_vertex_attribs(glutils_nl_t *nl, GLuint prog)
{
	if (nl->vao != 0 && nl->last_prog == prog)
		return;

	if (nl->vao != 0) {
		/*
		 * Disable our previously-used attribute pointers.
		 */
		glutils_disable_vtx_attr_ptr(nl->loc.seg_here);
		glutils_disable_vtx_attr_ptr(nl->loc.seg_start);
		glutils_disable_vtx_attr_ptr(nl->loc.seg_end);
	}

	/* Uniforms */
	nl->loc.vp = glGetUniformLocation(prog, "_nl_vp");
	nl->loc.semi_width = glGetUniformLocation(prog, "_nl_semi_width");

	/* Attributes */
	nl->loc.seg_here = glGetAttribLocation(prog, "_nl_seg_here");
	nl->loc.seg_start = glGetAttribLocation(prog, "_nl_seg_start");
	nl->loc.seg_end = glGetAttribLocation(prog, "_nl_seg_end");

	glBindBuffer(GL_ARRAY_BUFFER, nl->vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, nl->ibo);
	glutils_enable_vtx_attr_ptr(nl->loc.seg_here, 3, GL_FLOAT, GL_FALSE,
	    sizeof (nl_vtx_data_t), offsetof(nl_vtx_data_t, seg_here));
	glutils_enable_vtx_attr_ptr(nl->loc.seg_start, 3, GL_FLOAT, GL_FALSE,
	    sizeof (nl_vtx_data_t), offsetof(nl_vtx_data_t, seg_start));
	glutils_enable_vtx_attr_ptr(nl->loc.seg_end, 3, GL_FLOAT, GL_FALSE,
	    sizeof (nl_vtx_data_t), offsetof(nl_vtx_data_t, seg_end));

	nl->last_prog = prog;
}

void
glutils_nl_draw(glutils_nl_t *nl, float width, GLuint prog)
{
	int vp[4];
#if	APL
	GLenum winding;
#endif

	ASSERT(nl != NULL);
	ASSERT3F(width, >=, 0);
	ASSERT(prog != 0);

	glGetIntegerv(GL_VIEWPORT, vp);

	if (nl->vao != 0) {
		glBindVertexArray(nl->vao);
	} else {
		glBindBuffer(GL_ARRAY_BUFFER, nl->vbo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, nl->ibo);
	}

	nl_setup_vertex_attribs(nl, prog);

	if (nl->loc.vp != -1)
		glUniform2f(nl->loc.vp, vp[2], vp[3]);
	if (nl->loc.semi_width != -1)
		glUniform1f(nl->loc.semi_width, width / 2);

	/*
	 * We need to disable backface culling, because sometimes we end up
	 * drawing vertices in the opposite winding sense when the line is
	 * going right-to-left on the screen.
	 */
#if	APL
	/*
	 * Mac OpenGL resets its winding rules after glEnable(GL_CULL_FACE),
	 * so save & restore those too.
	 */
	glGetIntegerv(GL_FRONT_FACE, (GLint *)&winding);
#endif	/* APL */
	glDisable(GL_CULL_FACE);
	glDrawElements(GL_TRIANGLES, nl->num_pts * 3, GL_UNSIGNED_INT, NULL);
	glEnable(GL_CULL_FACE);
#if	APL
	glFrontFace(winding);
#endif

	if (nl->vao != 0) {
		glBindVertexArray(0);
	} else {
		glutils_disable_vtx_attr_ptr(nl->loc.seg_here);
		glutils_disable_vtx_attr_ptr(nl->loc.seg_start);
		glutils_disable_vtx_attr_ptr(nl->loc.seg_end);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}
