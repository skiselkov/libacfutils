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

#ifndef	_ACF_UTILS_GLUTILS_H_
#define	_ACF_UTILS_GLUTILS_H_

#include <GL/glew.h>

#include <acfutils/assert.h>
#include <acfutils/geom.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct {
	GLuint	vbo;
	size_t	num_vtx;
} glutils_quads_t;

API_EXPORT void glutils_disable_all_client_state(void);

API_EXPORT GLuint glutils_make_quads_IBO(size_t num_vtx);
API_EXPORT void glutils_init_2D_quads(glutils_quads_t *quads,
    vect2_t *p, vect2_t *t, size_t num_pts);
API_EXPORT void glutils_init_3D_quads(glutils_quads_t *quads,
    vect3_t *p, vect2_t *t, size_t num_pts);
API_EXPORT void glutils_destroy_quads(glutils_quads_t *quads);
API_EXPORT void glutils_draw_quads(const glutils_quads_t *quads, GLint prog);

API_EXPORT void glutils_enable_vtx_attrs(GLuint first_idx, ...);
API_EXPORT void glutils_disable_vtx_attrs(GLuint first_idx, ...);

#define	GLUTILS_VALIDATE_INDICES(indices, num_idx, num_vtx) \
	do { \
		for (unsigned i = 0; i < (num_idx); i++) { \
			VERIFY_MSG((indices)[i] < (num_vtx), "invalid index " \
			    "specification encountered, index %d (value %d) " \
			    "is outside of vertex range %d", i, (indices)[i], \
			    (num_vtx)); \
		} \
	} while (0)

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_GLUTILS_H_ */
