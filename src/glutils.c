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

#include <acfutils/assert.h>
#include <acfutils/glutils.h>
#include <acfutils/shader.h>

typedef struct {
	GLfloat	pos[3];
	GLfloat	tex0[2];
} vtx_t;

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

	for (int i = 0; disable_caps[i] != 0; i++)
		glDisableClientState(disable_caps[i]);
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
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	return (buf);
}

API_EXPORT void
glutils_init_2D_quads(glutils_quads_t *quads, vect2_t *p, vect2_t *t,
    size_t num_pts)
{
	vect3_t p_3d[num_pts];

	for (size_t i = 0; i < num_pts; i++)
		p_3d[i] = VECT3(p[i].x, p[i].y, 0);

	glutils_init_3D_quads(quads, p_3d, t, num_pts);
}

API_EXPORT void
glutils_init_3D_quads(glutils_quads_t *quads, vect3_t *p, vect2_t *t,
    size_t num_pts)
{
	vtx_t vtx_data[2 * num_pts];
	size_t i, n;

	ASSERT0(num_pts & 3);
	ASSERT(p != NULL);

	memset(vtx_data, 0, sizeof (vtx_data));

	for (i = 0, n = 0; i < num_pts; i += 4, n += 6) {
		vtx_data[n + 0].pos[0] = p[i + 0].x;
		vtx_data[n + 0].pos[1] = p[i + 0].y;
		vtx_data[n + 0].pos[2] = p[i + 0].z;

		vtx_data[n + 1].pos[0] = p[i + 1].x;
		vtx_data[n + 1].pos[1] = p[i + 1].y;
		vtx_data[n + 1].pos[2] = p[i + 1].z;

		vtx_data[n + 2].pos[0] = p[i + 2].x;
		vtx_data[n + 2].pos[1] = p[i + 2].y;
		vtx_data[n + 2].pos[2] = p[i + 2].z;

		vtx_data[n + 3].pos[0] = p[i + 0].x;
		vtx_data[n + 3].pos[1] = p[i + 0].y;
		vtx_data[n + 3].pos[2] = p[i + 0].z;

		vtx_data[n + 4].pos[0] = p[i + 2].x;
		vtx_data[n + 4].pos[1] = p[i + 2].y;
		vtx_data[n + 4].pos[2] = p[i + 2].z;

		vtx_data[n + 5].pos[0] = p[i + 3].x;
		vtx_data[n + 5].pos[1] = p[i + 3].y;
		vtx_data[n + 5].pos[2] = p[i + 3].z;

		if (t != NULL) {
			vtx_data[n + 0].tex0[0] = t[i + 0].x;
			vtx_data[n + 0].tex0[1] = t[i + 0].y;

			vtx_data[n + 1].tex0[0] = t[i + 1].x;
			vtx_data[n + 1].tex0[1] = t[i + 1].y;

			vtx_data[n + 2].tex0[0] = t[i + 2].x;
			vtx_data[n + 2].tex0[1] = t[i + 2].y;

			vtx_data[n + 3].tex0[0] = t[i + 0].x;
			vtx_data[n + 3].tex0[1] = t[i + 0].y;

			vtx_data[n + 4].tex0[0] = t[i + 2].x;
			vtx_data[n + 4].tex0[1] = t[i + 2].y;

			vtx_data[n + 5].tex0[0] = t[i + 3].x;
			vtx_data[n + 5].tex0[1] = t[i + 3].y;
		}
	}

	glGenBuffers(1, &quads->vbo);
	quads->num_vtx = n;

	VERIFY(quads->vbo != 0);
	glBindBuffer(GL_ARRAY_BUFFER, quads->vbo);
	glBufferData(GL_ARRAY_BUFFER, n * sizeof (*vtx_data),
	    vtx_data, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

API_EXPORT void
glutils_destroy_quads(glutils_quads_t *quads)
{
	if (quads->vbo != 0) {
		glDeleteBuffers(1, &quads->vbo);
		memset(quads, 0, sizeof (*quads));
	}
}

API_EXPORT void
glutils_draw_quads(const glutils_quads_t *quads, GLint prog)
{
	GLint pos_loc, tex0_loc;

	ASSERT(prog != 0);

	pos_loc = glGetAttribLocation(prog, "vtx_pos");
	tex0_loc = glGetAttribLocation(prog, "vtx_tex0");

	glEnableVertexAttribArray(pos_loc);
	glEnableVertexAttribArray(tex0_loc);

	glBindBuffer(GL_ARRAY_BUFFER, quads->vbo);
	glVertexAttribPointer(pos_loc, 3, GL_FLOAT, GL_FALSE,
	    sizeof (vtx_t), (void *)(offsetof(vtx_t, pos)));
	glVertexAttribPointer(tex0_loc, 2, GL_FLOAT, GL_FALSE,
	    sizeof (vtx_t), (void *)(offsetof(vtx_t, tex0)));

	glDrawArrays(GL_TRIANGLES, 0, quads->num_vtx);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDisableVertexAttribArray(pos_loc);
	glDisableVertexAttribArray(tex0_loc);
}

API_EXPORT void
glutils_enable_vtx_attrs(GLuint first_idx, ...)
{
	va_list ap;
	va_start(ap, first_idx);
	for (GLuint idx = first_idx; idx != (GLuint)-1;
	    idx = va_arg(ap, GLuint))
		glEnableVertexAttribArray(idx);
	va_end(ap);
}

API_EXPORT void
glutils_disable_vtx_attrs(GLuint first_idx, ...)
{
	va_list ap;
	va_start(ap, first_idx);
	for (GLuint idx = first_idx; idx != (GLuint)-1;
	    idx = va_arg(ap, GLuint))
		glDisableVertexAttribArray(idx);
	va_end(ap);
}
