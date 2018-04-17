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

#include <stdarg.h>
#include <GL/glew.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Apparently these are the standard vertex attribute indices:
 * gl_Vertex            0
 * gl_Normal            2
 * gl_Color             3
 * gl_SecondaryColor    4
 * gl_FogCoord          5
 * gl_MultiTexCoord0    8
 * gl_MultiTexCoord1    9
 * gl_MultiTexCoord2    10
 * gl_MultiTexCoord3    11
 * gl_MultiTexCoord4    12
 * gl_MultiTexCoord5    13
 * gl_MultiTexCoord6    14
 * gl_MultiTexCoord7    15
 */
enum {
	VTX_ATTRIB_POS =	0,
	VTX_ATTRIB_NORM =	2,
	VTX_ATTRIB_TEX0 =	8,
	VTX_ATTRIB_TEX1 =	9
};

#define	DEFAULT_VTX_ATTRIB_BINDINGS \
	"vtx_pos", VTX_ATTRIB_POS, "vtx_norm", VTX_ATTRIB_NORM, \
	"vtx_tex0", VTX_ATTRIB_TEX0, "vtx_tex1", VTX_ATTRIB_TEX1

#define	shader_from_file	ACFSYM(shader_from_file)
API_EXPORT GLuint shader_from_file(GLenum shader_type, const char *filename);

#define	shader_prog_from_file	ACFSYM(shader_prog_from_file)
API_EXPORT GLuint shader_prog_from_file(const char *progname,
    const char *vtx_file, const char *frag_file, ...);

#define	shader_prog_from_file_v	ACFSYM(shader_prog_from_file_v)
API_EXPORT GLuint shader_prog_from_file_v(const char *progname,
    const char *vtx_file, const char *frag_file, va_list ap);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_SHADER_H_ */
