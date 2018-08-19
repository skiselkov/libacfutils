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

typedef struct {
	const char	*name;
	GLuint		idx;
} shader_attr_bind_t;

/*
 * A specialization constant to pass to the shader loading routines as part
 * of a shader_info_t structure to specialize SPIR-V shaders. If not necessary,
 * you can provide NULL in the spec_const field in shader_info_t to mean "no
 * specialization constants required". If you do want to employ specialization
 * constants, set `is_last' on the last entry in the list of specialization
 * constants. Please note that the last entry is ignored, so simply use the
 * last entry as a list terminator, not as an actual specialization constants
 * that you want to pass to the loader. To enforce this, the loader checks
 * that the last entry in the list has both idx and val set to zero.
 */
typedef struct {
	bool_t		is_last;
	GLuint		idx;
	GLuint		val;
} shader_spec_const_t;

/*
 * Shader construction information structure. This lets you specify a shader
 * to be used in the construction of a shader program. The fields have the
 * following meanings:
 *
 * @field filename An optional filename (set to NULL if not used). This
 *	attempts to load the shader from the provided filename. The filename
 *	extension and case IS significant. Use ".spv" for SPIR-V shaders.
 *	If a filename ends in any other extension other than ".spv", it is
 *	treated as a GLSL shader.
 *	If you provide a SPIR-V shader and SPIR-V is not supported by the
 *	driver, we search for a fallback shader with the extension replaced
 *	".vert" for vertex shaders and ".frag" for fragment shaders. The
 *	second fallback filename optiona ttempted is ".glsl.vert" for vertex
 *	shaders and ".glsl.frag" for fragment shaders.
 *	For example, if you are loading a vertex shader named "myshader.spv",
 *	if the driver doesn't support SPIR-V, the library also looks for
 *	"myshader.vert" and "myshader.glsl".
 *	If SPIR-V is supported, the library ONLY attempts to load the SPIR-V
 *	shader.
 * @field glsl Direct GLSL program text to use in compiling the shader.
 *	This field must ONLY be used in place of `filename'. It is NOT legal
 *	set both `filename' and `glsl'. However, you MUST provide either a
 *	`filename' or `glsl'.
 * @field entry_pt A SPIR-V shader entry point. If the shader isn't SPIR-V,
 *	this field is ignored. If the shader IS SPIR-V and entry_pt = NULL,
 *	the loader falls back using "main" as the SPIR-V shader entry point.
 * @field spec_const An optional array of specialization constants to be used
 *	during specialization of a SPIR-V shader. If specialization constants
 *	aren't required, set this field to NULL. Otherwise set it to a list
 *	of shader_spec_const_t structures. This list MUST be terminated by
 *	a shader_spec_const_t structure with the `is_last' field set to
 *	B_TRUE and both the `idx' and `val' fields set to 0.
 */
typedef struct {
	const char			*filename;
	const char			*glsl;
	const char			*entry_pt;
	const shader_spec_const_t	*spec_const;
} shader_info_t;

/*
 * Shader program construction information structure. You must pass this
 * to shader_prog_from_info to construct a shader program ready for use in
 * render passes. Please note that you MUST provide at least one of `vert'
 * or `frag'. The fields have the following meanings:
 *
 * @field progname Readable program name that can be used in error messages
 *	to identify the shader that encountered a loading problem. This is
 *	not used during shader execution.
 * @field vert Vertex shader specification. Set to NULL if not used.
 *	See shader_info_t for details.
 * @field vert Fragment shader specification. Set to NULL if not used.
 *	See shader_info_t for details.
 * @field attr_binds Vertex attribute array bindings. Set to NULL if not used.
 *	See shader_attr_bind_t for details.
 */
typedef struct {
	const char			*progname;
	const shader_info_t		*vert;
	const shader_info_t		*frag;
	const shader_attr_bind_t	*attr_binds;
} shader_prog_info_t;

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

/* MSVC doesn't speak C99 */
#if	defined(__GNUC__) || defined(__clang__)
static shader_attr_bind_t UNUSED_ATTR default_vtx_attr_binds[] = {
	{ .name = "vtx_pos", .idx = VTX_ATTRIB_POS },
	{ .name = "vtx_norm", .idx = VTX_ATTRIB_NORM },
	{ .name = "vtx_tex0", .idx = VTX_ATTRIB_TEX0 },
	{ .name = "vtx_tex1", .idx = VTX_ATTRIB_TEX1 },
	{ /* list terminator */ }
};
#endif	/* defined(__GNUC__) || defined(__clang__) */

#define	shader_prog_from_file	ACFSYM(shader_prog_from_file)
API_EXPORT GLuint shader_prog_from_file(const char *progname,
    const char *vert_file, const char *frag_file, ...);

#define	shader_prog_from_text	ACFSYM(shader_prog_from_text)
API_EXPORT GLuint shader_prog_from_text(const char *progname,
    const char *vert_shader_text, const char *frag_shader_text, ...);

#define	shader_prog_from_info	ACFSYM(shader_prog_from_info)
API_EXPORT GLuint shader_prog_from_info(const char *dirpath,
    const shader_prog_info_t *info);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_SHADER_H_ */
