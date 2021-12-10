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
 * Copyright 2021 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACF_UTILS_SHADER_H_
#define	_ACF_UTILS_SHADER_H_

#include <stdarg.h>
#include <time.h>

#include "delay_line.h"
#include "glew.h"

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
 * `is_float' is a hint used when constructing defines for a GLSL fallback
 * program. If set to true, then `val' is interpreted as an IEEE-754-encoded
 * floating point value.
 */
typedef struct {
	bool_t		is_last;
	GLuint		idx;
	GLuint		val;
	bool_t		is_float;
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
 * @field comp Compute shader specification. Set to NULL if not used.
 *	See shader_info_t for details.
 * @field attr_binds Vertex attribute array bindings. Set to NULL if not used.
 *	See shader_attr_bind_t for details.
 */
typedef struct {
	const char			*progname;
	const shader_info_t		*vert;
	const shader_info_t		*frag;
	const shader_info_t		*comp;
	const shader_attr_bind_t	*attr_binds;
} shader_prog_info_t;

/*
 * Apparently these are the standard vertex attribute indices:
 * gl_Vertex		0
 * gl_Normal		2
 * gl_Color		3
 * gl_SecondaryColor	4
 * gl_FogCoord		5
 * gl_MultiTexCoord0	8
 * gl_MultiTexCoord1	9
 * gl_MultiTexCoord2	10
 * gl_MultiTexCoord3	11
 * gl_MultiTexCoord4	12
 * gl_MultiTexCoord5	13
 * gl_MultiTexCoord6	14
 * gl_MultiTexCoord7	15
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

/*
 * Shader Objects
 *
 * A shader object (shader_obj_t) is a convenient shader control mechanism
 * that can be used to efficiently perform attribute and uniform location
 * lookups. Rather than having to store each location in your own code
 * and work with a raw GLuint shader program, a shader_obj_t takes care
 * of loading the shader from disk from a shader_prog_info_t and also
 * performs all attribute and uniform lookups ahead of time. You then
 * simply refer to attributes and uniforms by an enum value, rather than
 * by name, removing the costly name lookup in the driver. This facility
 * is generic, so it allows for a single code path of utilizing a shader
 * in your code, while allowing for substituting the shader as needed
 * based on rendering needs.
 *
 * To initialize a shader object, use `shader_obj_init' and then destroy
 * the object using `shader_obj_fini'. On allocation, make sure the
 * shader_obj_t is set to all zeros (e.g. using safe_calloc). This way,
 * you can call `shader_obj_fini' even if `shader_obj_init' was never
 * called.
 *
 * Once initialized, you can then bind the shader program using
 * `shader_obj_bind_prog', which is equivalent to calling `glUseProgram'
 * with the shader's program number. To fetch attribute and uniform
 * locations, you use `shader_obj_get_a' and `shader_obj_get_u' with
 * the enum describing the attribute or uniform you want the location of.
 * Please note that the enum list must be contiguous. For efficiency
 * reasons, the getter functions simply use the enum value as an index
 * into the array of cached locations.
 *
 * CODE SAMPLE
 *
 *	-- Static shader info --
 * static const shader_info_t foo_vert_info = { .filename = "foo.vert.spv" };
 * static const shader_info_t foo_frag_info = { .filename = "foo.frag.spv" };
 * static const shader_prog_info_t foo_prog_info = {
 *	.progname = "foo_prog",
 *	.vert = &foo_vert_info,
 *	.frag = &foo_frag_info
 * };
 *	-- Attribute definitions --
 * enum {
 *	A_VTX_POS,
 *	A_VTX_NORM,
 *	A_VTX_TEX0
 *	NUM_ATTRS
 * };
 * static const char *attr_names[NUM_ATTRS] = {
 *	[A_VTX_POS] = "vtx_pos",
 *	[A_VTX_NORM] = "vtx_norm",
 *	[A_VTX_TEX0] = "vtx_tex0"
 * };
 *	-- Uniform definitions --
 * enum {
 *	U_PROJ_MATRIX,
 *	U_MV_MATRIX,
 *	NUM_UNIFORMS
 * };
 * static const char *uniform_names[NUM_UNIFORMS] = {
 *	[U_PROJ_MATRIX] = "proj_matrix",
 *	[U_MV_MATRIX] = "mv_matrix"
 * };
 *
 *	-- Initializing a shader_obj_t
 * static shader_obj_t foo_so;
 * if (!shader_obj_init(&foo_so, "/foo/shader/dir", &foo_prog_info,
 *	attr_names, NUM_ATTRS, uniform_names, NUM_UNIFORMS)) {
 *	logMsg("shader %s load error", foo_prog_info.progname);
 * }
 *
 *	-- Utilizing a shader_obj_t during rendering
 * shader_obj_bind(&foo_so);
 * glUniformMatrix4fv(shader_obj_get_u(&foo_so, U_PROJ_MATRIX),
 *	1, GL_FALSE, proj_matrix);
 * glUniformMatrix4fv(shader_obj_get_u(&foo_so, U_MV_MATRIX),
 *	1, GL_FALSE, mv_matrix);
 *
 *	-- Destroying a shader_obj_t
 * shader_obj_fini(&foo_so);
 */
#define	SHADER_OBJ_MAX_ATTRS		128
#define	SHADER_OBJ_MAX_UNIFORMS		128
typedef struct {
	const shader_prog_info_t	*info;
	char				*dirpath;
	GLuint				prog;
	const char			**attr_names;
	unsigned			num_attrs;
	GLint				attr_loc[SHADER_OBJ_MAX_ATTRS];
	const char			**uniform_names;
	unsigned			num_uniforms;
	GLint				uniform_loc[SHADER_OBJ_MAX_UNIFORMS];
	delay_line_t			check_delay;
	time_t				load_time;
} shader_obj_t;

/*
 * Initializes a shader_obj_t.
 *
 * @param obj The shader object to be initialized.
 * @param dirpath The directory path containing the files of the shader.
 *	You can free this after calling shader_obj_init. The shader object
 *	copies it.
 * @param info The shader_prog_info_t structure describing how the shader
 *	is to be constructed. You must NOT free this structure until calling
 *	`shader_obj_fini', the shader_obj_t doesn't copy it. Ideally this
 *	should be a `static const' object in the program.
 * @param attr_names An optional array of attribute names. This parameter
 *	can be NULL, provided `num_attrs' is zero. You must NOT free this
 *	array until calling `shader_obj_fini', the shader_obj_t doesn't copy
 *	it. Ideally this should be a `static const' array in the program.
 *	ALL the elements of this name array must be valid strings (not NULL).
 * @param num_attrs Number of elements in `attr_names'. This must be
 *	less than SHADER_OBJ_MAX_ATTRS (128).
 * @param uniform_names An optional array of uniform names. This parameter
 *	can be NULL, provided `num_uniforms' is zero. You must NOT free this
 *	array until calling `shader_obj_fini', the shader_obj_t doesn't copy
 *	it. Ideally this should be a `static const' array in the program.
 *	ALL the elements of this name array must be valid strings (not NULL).
 * @param num_uniforms Number of elements in `uniform_names'. This must be
 *	less than SHADER_OBJ_MAX_UNIFORMS (128).
 */
API_EXPORT bool_t shader_obj_init(shader_obj_t *obj,
    const char *dirpath, const shader_prog_info_t *info,
    const char **attr_names, unsigned num_attrs,
    const char **uniform_names, unsigned num_uniforms);
/*
 * Destroys a shader_obj_t. If you allocated the the shader_obj_t so that its
 * storage is zero-initialized, you can safely call this function even if you
 * didn't call `shader_obj_init'. The `obj' argument must NOT be NULL.
 */
API_EXPORT void shader_obj_fini(shader_obj_t *obj);
/*
 * Performs a reload of a shader_obj_t from disk and refreshes all attribute
 * and uniform locations. Use this if you have altered the shader on disk
 * and want to start using the new version for rendering.
 * This function returns B_TRUE if the reload was successful, B_FALSE if not.
 * The old shader program is automatically destroyed only if the reload was
 * successful.
 */
API_EXPORT bool_t shader_obj_reload(shader_obj_t *obj);
/*
 * Similar to shader_obj_reload, but instead only reloads the shader if
 * the on-disk version has changed. To avoid excessive disk I/O, this only
 * does the check every few seconds using an internal timer.
 * The function returns B_TRUE if the shader was reloaded successfully.
 * If the reload wasn't necessary, or failed, B_FALSE is returned instead.
 */
API_EXPORT bool_t shader_obj_reload_check(shader_obj_t *obj);

/*
 * Binds the shader object's program to the current OpenGL context. This
 * is equivalent to calling `glUseProgram(shader_obj_get_prog(&shader_obj))'.
 * The shader_obj_t must have previous been successfully initialized using
 * shader_obj_init, otherwise this function trips an assertion.
 */
static inline void
shader_obj_bind(const shader_obj_t *obj)
{
	ASSERT(obj != NULL);
	ASSERT(obj->prog != 0);
	glUseProgram(obj->prog);
}

/*
 * Returns the OpenGL shader program number in a shader_obj_t. The
 * shader_obj_t must have previous been successfully initialized using
 * shader_obj_init, otherwise this function trips an assertion.
 */
static inline GLuint
shader_obj_get_prog(const shader_obj_t *obj)
{
	ASSERT(obj != NULL);
	ASSERT(obj->prog != 0);
	return (obj->prog);
}

/*
 * Returns an attribute location in the shader_obj_t. The shader_obj_t must
 * have previously been successfully initialized using shader_obj_init. The
 * attr_ID must be an index into the attr_names array previously used in
 * `shader_obj_init'.
 */
static inline GLint
shader_obj_get_a(shader_obj_t *obj, unsigned attr_ID)
{
	ASSERT(obj != NULL);
	ASSERT3U(attr_ID, <, obj->num_attrs);
	return (obj->attr_loc[attr_ID]);
}

/*
 * Returns a uniform location in the shader_obj_t. The shader_obj_t must
 * must have previously been successfully initialized using shader_obj_init.
 * The uniform_ID must be a valid index into the uniform_names array
 * previously used in `shader_obj_init'.
 */
static inline GLint
shader_obj_get_u(shader_obj_t *obj, unsigned uniform_ID)
{
	ASSERT(obj != NULL);
	ASSERT3U(uniform_ID, <, obj->num_uniforms);
	return (obj->uniform_loc[uniform_ID]);
}

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_SHADER_H_ */
