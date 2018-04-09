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

#ifndef	_ACF_UTILS_SHADER_H_
#define	_ACF_UTILS_SHADER_H_

#include <GL/glew.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	shader_from_file	ACFSYM(shader_from_file)
API_EXPORT GLuint shader_from_file(GLenum shader_type, const char *filename);
#define	shader_prog_from_file	ACFSYM(shader_prog_from_file)
API_EXPORT GLuint shader_prog_from_file(const char *progname,
    const char *vtx_file, const char *frag_file);

API_EXPORT void glutils_disable_all_client_state(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_SHADER_H_ */
