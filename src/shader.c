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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

#include "acfutils/glutils.h"
#include "acfutils/helpers.h"
#include "acfutils/log.h"
#include "acfutils/safe_alloc.h"
#include "acfutils/shader.h"
#include "acfutils/stat.h"

#define	EXTRA_2D_DEFINES \
	"#define textureSize2D textureSize\n" \
	"#define texture2D texture\n"

static GLuint shader_from_file(GLenum shader_type, const char *filename,
    const char *entry_pt, const shader_spec_const_t *spec_const);
static GLuint shader_from_text(GLenum shader_type,
    const GLchar *shader_text, const char *filename,
    const shader_spec_const_t *spec_const);
static GLuint shaders2prog(const char *progname, GLuint vert_shader,
    GLuint frag_shader, GLuint comp_shader, const shader_attr_bind_t *attr_binds);
static GLuint shader_prog_from_file_v(const char *progname,
    const char *vert_file, const char *frag_file,
    const shader_attr_bind_t *binds);
static GLuint shader_prog_from_text_v(const char *progname,
    const char *vert_text, const char *frag_text,
    const shader_attr_bind_t *binds);

static shader_attr_bind_t *
attr_binds_from_args(va_list ap)
{
	va_list ap2;
	int n_attrs = 0;
	shader_attr_bind_t *binds;

	va_copy(ap2, ap);
	for (;;) {
		const char *name = va_arg(ap2, const char *);

		if (name == NULL)
			break;
		va_arg(ap2, GLuint);
		n_attrs++;
	}
	va_end(ap2);
	/* +1 to terminate the list with an empty bind */
	binds = safe_calloc(n_attrs + 1, sizeof (*binds));
	for (int i = 0; i < n_attrs; i++) {
		binds[i].name = va_arg(ap, const char *);
		binds[i].idx = va_arg(ap, GLuint);
	}

	return (binds);
}

/*
 * Attempts to load a fallback shader to a SPIR-V shader, in case SPIR-V
 * isn't supported. This attempts to locate a shader by replacing the filename
 * extension of the original shader with:
 * - .vert or .frag
 * - .glsl
 * If found, the shader is compiled and returned. Otherwise returns 0.
 */
static GLuint
shader_from_spirv_fallback(GLenum shader_type, const char *filename,
    const shader_spec_const_t *spec_const)
{
	char *alt_filename, *alt_ext, *new_ext;
	bool_t is_dir;
	bool_t loaded = B_FALSE;
	GLuint shader = 0;

	switch (shader_type) {
	case GL_VERTEX_SHADER:
		alt_ext = "vert";
		break;
	case GL_FRAGMENT_SHADER:
		alt_ext = "frag";
		break;
	case GL_COMPUTE_SHADER:
		alt_ext = "comp";
		break;
	default:
		VERIFY_MSG(0, "Unknown shader type %d", shader_type);
	}

	alt_filename = safe_calloc(strlen(filename) + 16, 1);
	strcpy(alt_filename, filename);
	new_ext = strrchr(alt_filename, '.');
	ASSERT(new_ext != NULL);
	new_ext++;
	strcpy(new_ext, alt_ext);
	if (!loaded && file_exists(alt_filename, &is_dir) && !is_dir) {
		shader = shader_from_file(shader_type, alt_filename,
		    NULL, spec_const);
		loaded = (shader != 0);
	}
#define	TEST_GLSL_VERSION(version, extension) \
	do { \
		if (!loaded && (version)) { \
			strcpy(new_ext, (extension)); \
			if (file_exists(alt_filename, &is_dir) && !is_dir) { \
				shader = shader_from_file(shader_type, \
				    alt_filename, NULL, spec_const); \
				loaded = (shader != 0); \
			} \
		} \
	} while (0)
	TEST_GLSL_VERSION(GLEW_VERSION_4_6, "glsl460");
	TEST_GLSL_VERSION(GLEW_VERSION_4_5, "glsl450");
	TEST_GLSL_VERSION(GLEW_VERSION_4_4, "glsl440");
	TEST_GLSL_VERSION(GLEW_VERSION_4_3, "glsl430");
	TEST_GLSL_VERSION(GLEW_VERSION_4_2, "glsl420");
	TEST_GLSL_VERSION(GLEW_VERSION_4_1, "glsl410");
	TEST_GLSL_VERSION(GLEW_VERSION_4_0, "glsl400");
	if (!loaded) {
		strcpy(new_ext, "glsl");
		if (file_exists(alt_filename, &is_dir) && !is_dir) {
			shader = shader_from_file(shader_type,
			    alt_filename, NULL, spec_const);
			loaded = (shader != 0);
		} else {
			logMsg("Error loading shader %s: SPIR-V shaders "
			    "not supported and no fallback shader found.",
			    filename);
		}
	}
	free(alt_filename);

	return (shader);
}

static bool_t
is_nvidia(void)
{
	const char *vendor = (char *)glGetString(GL_VENDOR);
	return (lacf_strcasestr(vendor, "nvidia") != NULL);
}

static bool_t
have_shader_binary_format(GLint fmt)
{
	GLint num;
	GLint *formats;
	bool_t found = B_FALSE;

	glGetIntegerv(GL_NUM_SHADER_BINARY_FORMATS, &num);
	if (glGetError() != GL_NO_ERROR || num > 1024 * 1024)
		return (B_FALSE);

	formats = safe_calloc(num, sizeof (*formats));
	glGetIntegerv(GL_SHADER_BINARY_FORMATS, formats);
	if (glGetError() != GL_NO_ERROR) {
		free(formats);
		return (B_FALSE);
	}
	for (int i = 0; i < num; i++) {
		if (formats[i] == fmt) {
			found = B_TRUE;
			break;
		}
	}
	free(formats);

	return (found);
}

static bool_t
force_spv(void)
{
#if	!APL
	for (int i = 0; environ[i] != NULL; i++) {
		if (strncmp(environ[i], "_LACF_SHADERS_FORCE_SPV", 23) == 0) {
			logMsg("SPIR-V force load");
			return (B_TRUE);
		}
	}
#endif	/* !defined(APL) */
	return (B_FALSE);
}

/*
 * Attemps to load a SPIR-V shader. If SPIR-V is not supported, calls
 * shader_from_spirv_fallback to attempt to load a backup alternate shader.
 * The entry point must be provided in `entry_pt'. If present, set spec_const
 * to an array of specialization constants. This array MUST be terminated by
 * an element that has is_last set to B_TRUE. If no specialization constants
 * are needed, set spec_const = NULL.
 */
static GLuint
shader_from_spirv(GLenum shader_type, const char *filename,
    const char *entry_pt, const shader_spec_const_t *spec_const)
{
	GLuint shader = 0;
	GLint compile_result;
	void *buf = NULL;
	size_t bufsz, n_spec;
	GLuint *spec_indices = NULL, *spec_values = NULL;
	GLenum err;
	/* clear error state */
	glGetError();

	if (entry_pt == NULL)
		entry_pt = "main";

	/*
	 * AMD's SPIR-V loader is b0rked AF (crashes in glSpecializeShader).
	 * Zink declares SPIR-V support, but the resultant shader doesn't
	 * actually work.
	 * Also, on Linux with Mesa's stock GL driver, glGetUniformLocation
	 * returns -1 even for shaders that work. So only try SPIR-V on Nvidia.
	 * When running under Nvidia Nsight, avoid loading binary shaders
	 * unless forced to. That way, we can see the source in the source
	 * explorer.
	 */
	if (!GLEW_ARB_gl_spirv ||
	    !have_shader_binary_format(GL_SHADER_BINARY_FORMAT_SPIR_V) ||
	    !is_nvidia() || glutils_in_zink_mode() ||
	    (glutils_nsight_debugger_present() && !force_spv())) {
		/* SPIR-V shaders not supported. Try fallback shader. */
		return (shader_from_spirv_fallback(shader_type, filename,
		    spec_const));
	}
	for (n_spec = 0; spec_const != NULL && !spec_const[n_spec].is_last;
	    n_spec++)
		;
	ASSERT(spec_const == NULL ||
	    (spec_const[n_spec].idx == 0 && spec_const[n_spec].val == 0));
	if (n_spec > 0) {
		/*
		 * Windows such a pile of fuck that it returns non-NULL
		 * for zero-length allocations. And that fucks over the
		 * AMD driver.
		 */
		spec_indices = safe_calloc(n_spec, sizeof (*spec_indices));
		spec_values = safe_calloc(n_spec, sizeof (*spec_values));
	}
	for (size_t i = 0; i < n_spec; i++) {
		spec_indices[i] = spec_const[i].idx;
		spec_values[i] = spec_const[i].val;
	}

	buf = file2buf(filename, &bufsz);
	if (buf == NULL) {
		logMsg("Cannot load shader %s: %s", filename, strerror(errno));
		goto errout;
	}

	shader = glCreateShader(shader_type);
	if (shader == 0) {
		logMsg("Cannot load shader %s: glCreateShader failed with "
		    "error 0x%x", filename, glGetError());
		goto errout;
	}
	glShaderBinary(1, &shader, GL_SHADER_BINARY_FORMAT_SPIR_V, buf, bufsz);
	if ((err = glGetError()) != GL_NO_ERROR) {
		logMsg("Cannot load SPIR-V %s: error %x", filename, err);
		goto errout;
	}
	glSpecializeShader(shader, entry_pt, n_spec, spec_indices, spec_values);

	glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_result);
	if (compile_result == GL_FALSE) {
		GLint len;
		GLchar *buf;

		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
		buf = safe_calloc(len + 1, sizeof (*buf));
		glGetShaderInfoLog(shader, len, NULL, buf);
		logMsg("Cannot load shader %s: specialization error: %s",
		    filename, buf);

		goto errout;
	}

	free(buf);
	free(spec_indices);
	free(spec_values);

	return (shader);
errout:
	free(buf);
	free(spec_indices);
	free(spec_values);
	if (shader != 0)
		glDeleteShader(shader);

	return (0);
}

/*
 * Loads and compiles GLSL shader from a file and returns the shader object
 * ID. The shader type is passed in `shader_type'. Returns 0 if the shader
 * failed to load (an error description is printed to the X-Plane log).
 */
static GLuint
shader_from_file(GLenum shader_type, const char *filename,
    const char *entry_pt, const shader_spec_const_t *spec_const)
{
	const char *ext;
	GLchar *shader_text = NULL;
	GLuint shader;

	ASSERT(shader_type == GL_VERTEX_SHADER ||
	    shader_type == GL_FRAGMENT_SHADER ||
	    shader_type == GL_COMPUTE_SHADER);

	ext = strrchr(filename, '.');
	if (ext == NULL) {
		logMsg("Cannot load shader %s: filename missing required "
		    "extension", filename);
		return (0);
	}
	ext++;

	if (strcmp(ext, "spv") == 0) {
		return (shader_from_spirv(shader_type, filename,
		    entry_pt, spec_const));
	}
	shader_text = file2str(filename, NULL);
	if (shader_text == NULL) {
		logMsg("Cannot load shader %s: %s", filename, strerror(errno));
		return (0);
	}
	shader = shader_from_text(shader_type, shader_text, filename,
	    spec_const);
	lacf_free(shader_text);

	return (shader);
}

static void
construct_defines(const GLchar *shader_text, GLchar **defines,
    GLchar **shader_proc, const shader_spec_const_t *spec_const)
{
	/* just a default guess */
	int version = 120;
	GLchar *mod_text = strdup(shader_text);
	GLchar *ver_start = strstr(mod_text, "#version");
	size_t len;

	/*
	 * Grab the version number from the '#version' directive and slap it
	 * at the start of our preamble. Then delete the old one.
	 */
	if (ver_start != NULL && sscanf(&ver_start[9], "%d", &version) == 1) {
		for (; *ver_start != '\0' && *ver_start != '\n'; ver_start++)
			*ver_start = ' ';
	}

	*shader_proc = mod_text;
	*defines = sprintf_alloc(
	    "#version %d\n"
	    "#define IBM=%d\n"
	    "#define APL=%d\n"
	    "#define LIN=%d\n"
	    "%s",
	    version, IBM, APL, LIN,
	    version == 420 ? EXTRA_2D_DEFINES : "");
	len = strlen(*defines);
	for (const shader_spec_const_t *sc = spec_const;
	    sc != NULL && !sc->is_last; sc++) {
		if (sc->is_float) {
			float *value = (float *)&sc->val;
			append_format(defines, &len,
			    "#define SPIRV_CROSS_CONSTANT_ID_%d %f\n",
			    sc->idx, *value);
		} else {
			append_format(defines, &len,
			    "#define SPIRV_CROSS_CONSTANT_ID_%d %d\n",
			    sc->idx, sc->val);
		}
	}
	append_format(defines, &len, "#line 0\n");
}

/*
 * Loads and compiles GLSL shader from a string of text and returns the
 * shader object ID. The shader type is passed in `shader_type'. Returns
 * 0 if the shader failed to load (an error description is printed to the
 * X-Plane log).
 */
static GLuint
shader_from_text(GLenum shader_type, const GLchar *shader_text,
    const char *filename, const shader_spec_const_t *spec_const)
{
	GLint shader = 0;
	GLint compile_result;
	GLchar *defines;
	const GLchar *text[2];
	GLchar *shader_proc;

	ASSERT(shader_text != NULL);
	if (filename == NULL)
		filename = "<cstring>";

	shader = glCreateShader(shader_type);
	if (shader == 0) {
		logMsg("Cannot load shader %s: glCreateShader failed with "
		    "error 0x%x", filename, glGetError());
		goto errout;
	}

	construct_defines(shader_text, &defines, &shader_proc, spec_const);
	text[0] = defines;
	text[1] = shader_proc;
	glShaderSource(shader, 2, text, NULL);
	free(defines);
	free(shader_proc);

	glCompileShader(shader);

	glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_result);
	if (compile_result == GL_FALSE) {
		GLint len;
		GLchar *buf;

		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
		buf = safe_calloc(len + 1, sizeof (*buf));
		glGetShaderInfoLog(shader, len, NULL, buf);
		logMsg("Cannot load shader %s: compile error: %s", filename,
		    buf);
		free(buf);

		goto errout;
	}

	return (shader);
errout:
	if (shader != 0)
		glDeleteShader(shader);
	return (0);
}

/*
 * Loads, compiles and links a GLSL shader program composed of a vertex
 * shader and fragment shader object.
 *
 * @param prog_name Shader program name. This is used for diagnostic
 *	purposes only.
 * @param vert_file Full file path to the file containing the vertex shader.
 *	Set to NULL if the program contains no vertex shader.
 * @param frag_file Full file path to the file containing the fragment shader.
 *	Set to NULL if the program contains no fragment shader.
 * @param ... Additional parameters contain vertex attribute array index
 *	bindings. These must be passed in pairs of "char *" for the name
 *	of the vertex shader attribute, and GLuint for the array index. To
 *	terminate the list, pass NULL. Example:
 *	GLuint program = shader_prog_from_file("my_test_prog",
 *	    "/file/path/to/shader.vert", "/file/path/to/shader.frag",
 *	    "vertex_pos", 0, "tex_coord", 1, NULL);
 *
 * @return The compiled and linked shader program, ready for use in
 *	glUseProgram. Returns 0 if compiling or linking of the shader
 *	program failed (an error description is printed to the X-Plane log).
 */
API_EXPORT GLuint
shader_prog_from_file(const char *progname, const char *vert_file,
    const char *frag_file, ...)
{
	GLuint res;
	va_list ap;
	shader_attr_bind_t *binds;

	va_start(ap, frag_file);
	binds = attr_binds_from_args(ap);
	va_end(ap);
	res = shader_prog_from_file_v(progname, vert_file, frag_file, binds);
	free(binds);

	return (res);
}

/*
 * Same as shader_prog_from_file, except takes a va_list for the variadic
 * vertex attribute binding list.
 */
static GLuint
shader_prog_from_file_v(const char *progname, const char *vert_file,
    const char *frag_file, const shader_attr_bind_t *binds)
{
	GLuint vert_shader = 0, frag_shader = 0;

	if (vert_file != NULL) {
		vert_shader = shader_from_file(GL_VERTEX_SHADER, vert_file,
		    NULL, NULL);
		if (vert_shader == 0)
			goto errout;
	}
	if (frag_file != NULL) {
		frag_shader = shader_from_file(GL_FRAGMENT_SHADER, frag_file,
		    NULL, NULL);
		if (frag_shader == 0)
			goto errout;
	}

	return (shaders2prog(progname, vert_shader, frag_shader, 0, binds));
errout:
	if (vert_shader != 0)
		glDeleteShader(vert_shader);
	if (frag_shader != 0)
		glDeleteShader(frag_shader);
	return (0);
}

/*
 * Loads, compiles and links a GLSL shader program composed of a vertex
 * shader and fragment shader object.
 *
 * @param prog_name Shader program name. This is used for diagnostic
 *	purposes only.
 * @param vert_text GLSL program text representing the contents of the
 *	vertex shader. Set to NULL if the program contains no vertex shader.
 * @param frag_text GLSL program text representing the contents of the
 *	fragment shader. Set to NULL if the program contains no fragment shader.
 * @param ... Additional parameters contain vertex attribute array index
 *	bindings. These must be passed in pairs of "char *" for the name
 *	of the vertex shader attribute, and GLuint for the array index. To
 *	terminate the list, pass NULL. Example:
 *	GLuint program = shader_prog_from_file("my_test_prog",
 *	    "#version 120\n attribute...", "#version 130\n void main(){...",
 *	    "vertex_pos", 0, "tex_coord", 1, NULL);
 *
 * @return The compiled and linked shader program, ready for use in
 *	glUseProgram. Returns 0 if compiling or linking of the shader
 *	program failed (an error description is printed to the X-Plane log).
 */
API_EXPORT GLuint
shader_prog_from_text(const char *progname, const char *vert_text,
    const char *frag_text, ...)
{
	GLuint res;
	va_list ap;
	shader_attr_bind_t *binds;

	va_start(ap, frag_text);
	binds = attr_binds_from_args(ap);
	va_end(ap);
	res = shader_prog_from_text_v(progname, vert_text, frag_text, binds);
	free(binds);

	return (res);
}

/*
 * Same as shader_prog_from_text, except takes a va_list for the variadic
 * vertex attribute binding list.
 */
static GLuint
shader_prog_from_text_v(const char *progname, const char *vert_text,
    const char *frag_text, const shader_attr_bind_t *binds)
{
	GLuint vert_shader = 0, frag_shader = 0;

	if (vert_text != NULL) {
		vert_shader = shader_from_text(GL_VERTEX_SHADER, vert_text,
		    NULL, NULL);
		if (vert_shader == 0)
			return (0);
	}
	if (frag_text != NULL) {
		frag_shader = shader_from_text(GL_FRAGMENT_SHADER, frag_text,
		    NULL, NULL);
		if (frag_shader == 0) {
			if (vert_shader != 0)
				glDeleteShader(vert_shader);
			return (0);
		}
	}

	return (shaders2prog(progname, vert_shader, frag_shader, 0, binds));
}

/*
 * Handles the loading of a shader from the components of a shader_prog_info_t
 * structure. The shader type must be passed in `shader_type' (currently must
 * be GL_VERTEX_SHADER or GL_FRAGMENT_SHADER). The program name is only used
 * for referring to text-based GLSL fallback shaders in case compiling them
 * fails and to give the user an idea of the error that occurred. `dirpath'
 * is the name of the directory from which to load the shaders. The rest of
 * the arguments are the respective vert_ or frag_ fields from a
 * shader_prog_info_t structure.
 */
static bool_t
shader_from_file_or_text(GLenum shader_type, const char *dirpath,
    const shader_prog_info_t *prog_info, const shader_info_t *shader_info,
    GLuint *shader)
{
	ASSERT(shader_info->filename != NULL || shader_info->glsl != NULL);

	if (shader_info->filename != NULL) {
		char *path = mkpathname(dirpath, shader_info->filename, NULL);
		ASSERT3P(shader_info->glsl, ==, NULL);
		*shader = shader_from_file(shader_type, path,
		    shader_info->entry_pt, shader_info->spec_const);
		free(path);
		if ((*shader) == 0)
			return (B_FALSE);
	} else if (shader_info->glsl != NULL) {
		*shader = shader_from_text(shader_type, shader_info->glsl,
		    prog_info->progname, NULL);
		if ((*shader) == 0)
			return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * Loads, specializes/compiles and links a shader program a shader_prog_info_t
 * structure. The info structure is a structure designed to allow loading a
 * range of shader types with automatic fallback in case support for the
 * given shader type is limited.
 *
 * @param dirpath The absolute directory path to which all paths in `info'
 *	are relative.
 * @param info The shader program construction structure. See shader.h for
 *	a description of its fields.
 *
 * @return The shader, ready for use in rendering, or 0 in case an errors
 *	occurs during the loading process. An error message is appended to
 *	the X-Plane log explaining the error.
 */
API_EXPORT GLuint
shader_prog_from_info(const char *dirpath, const shader_prog_info_t *info)
{
	GLuint vert_shader = 0, frag_shader = 0, comp_shader = 0;
	bool_t debugger = glutils_nsight_debugger_present();
	GLuint prog;

	/* Caller must have provided at least one! */
	ASSERT(info->vert != NULL || info->frag != NULL || info->comp != NULL);
	/* Vertex & fragment shaders aren't allowed in compute shaders. */
	ASSERT((info->vert == NULL && info->frag == NULL) || info->comp == NULL);

	if (info->vert != NULL && !shader_from_file_or_text(GL_VERTEX_SHADER,
	    dirpath, info, info->vert, &vert_shader))
		goto errout;
	if (info->frag != NULL && !shader_from_file_or_text(GL_FRAGMENT_SHADER,
	    dirpath, info, info->frag, &frag_shader))
		goto errout;
	if (info->comp != NULL && !shader_from_file_or_text(GL_COMPUTE_SHADER,
	    dirpath, info, info->comp, &comp_shader))
		goto errout;

	if (debugger) {
		logMsg("loading %s  vert: %d  frag: %d  comp: %d",
		    info->progname, vert_shader, frag_shader, comp_shader);
	}

	prog = shaders2prog(info->progname, vert_shader, frag_shader,
	    comp_shader, info->attr_binds);
	if (debugger && prog != 0)
		logMsg("loaded %s  progID: %d", info->progname, prog);

	return (prog);
errout:
	if (vert_shader != 0)
		glDeleteShader(vert_shader);
	if (frag_shader != 0)
		glDeleteShader(frag_shader);
	if (comp_shader != 0)
		glDeleteShader(comp_shader);
	return (0);
}

/*
 * Takes a vertex and fragment shader object and links them together, applying
 * vertex attribute array bindings as specified in the `ap' variadic list.
 * Returns the compiled and linked shader program, or 0 on error (error
 * description is appended to X-Plane log file). The passed vertex and fragment
 * shader objects are *always* consumed and released at the end of the
 * function (regardless if an error occurred or not), so the caller needn't
 * dispose of them on its own.
 */
static GLuint
shaders2prog(const char *progname, GLuint vert_shader, GLuint frag_shader,
    GLuint comp_shader, const shader_attr_bind_t *attr_binds)
{
	GLuint prog = 0;
	GLint linked;

	ASSERT(progname != NULL);
	ASSERT((vert_shader == 0 && frag_shader == 0) || comp_shader == 0);

	prog = glCreateProgram();
	if (vert_shader != 0)
		glAttachShader(prog, vert_shader);
	if (frag_shader != 0)
		glAttachShader(prog, frag_shader);
	if (comp_shader != 0)
		glAttachShader(prog, comp_shader);

	while (attr_binds != NULL && attr_binds->name != NULL) {
		ASSERT(vert_shader != 0);
		glBindAttribLocation(prog, attr_binds->idx, attr_binds->name);
		attr_binds++;
	}

	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &linked);
	if (linked == GL_FALSE) {
		GLint len;
		GLchar *buf;

		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
		buf = safe_calloc(len + 1, sizeof (*buf));
		glGetProgramInfoLog(prog, len, NULL, buf);
		logMsg("Failed to link GLSL program %s: %s", progname, buf);
		free(buf);

		glDeleteProgram(prog);
		if (vert_shader != 0)
			glDeleteShader(vert_shader);
		if (frag_shader != 0)
			glDeleteShader(frag_shader);
		return (0);
	}
	if (vert_shader != 0) {
		glDetachShader(prog, vert_shader);
		glDeleteShader(vert_shader);
	}
	if (frag_shader != 0) {
		glDetachShader(prog, frag_shader);
		glDeleteShader(frag_shader);
	}
	if (comp_shader != 0) {
		glDetachShader(prog, comp_shader);
		glDeleteShader(comp_shader);
	}

	return (prog);
}

static void
shader_obj_refresh_loc(shader_obj_t *obj)
{
	ASSERT(obj != NULL);
	ASSERT(obj->prog != 0);
	ASSERT(obj->attr_names != NULL || obj->num_attrs == 0);
	ASSERT(obj->uniform_names != NULL || obj->num_uniforms == 0);

	for (unsigned i = 0; i < obj->num_attrs; i++) {
		ASSERT(obj->attr_names[i] != NULL);
		obj->attr_loc[i] = glGetAttribLocation(obj->prog,
		    obj->attr_names[i]);
	}
	for (unsigned i = 0; i < obj->num_uniforms; i++) {
		ASSERT(obj->uniform_names[i] != NULL);
		obj->uniform_loc[i] = glGetUniformLocation(obj->prog,
		    obj->uniform_names[i]);
	}
}

bool_t
shader_obj_init(shader_obj_t *obj,
    const char *dirpath, const shader_prog_info_t *info,
    const char **attr_names, unsigned num_attrs,
    const char **uniform_names, unsigned num_uniforms)
{
	ASSERT(obj != NULL);
	ASSERT(dirpath != NULL);
	ASSERT(info != NULL);
	ASSERT3U(num_attrs, <, ARRAY_NUM_ELEM(obj->attr_loc));
	ASSERT3U(num_uniforms, <, ARRAY_NUM_ELEM(obj->uniform_loc));

	obj->prog = shader_prog_from_info(dirpath, info);
	if (obj->prog == 0)
		return (B_FALSE);
	obj->info = info;
	obj->dirpath = safe_strdup(dirpath);
	obj->attr_names = attr_names;
	obj->num_attrs = num_attrs;
	obj->uniform_names = uniform_names;
	obj->num_uniforms = num_uniforms;
	shader_obj_refresh_loc(obj);

	delay_line_init(&obj->check_delay, SEC2USEC(2));
	obj->load_time = time(NULL);

	return (B_TRUE);
}

void
shader_obj_fini(shader_obj_t *obj)
{
	ASSERT(obj != NULL);
	if (obj->prog != 0)
		glDeleteProgram(obj->prog);
	free(obj->dirpath);
	memset(obj, 0, sizeof (*obj));
}

bool_t
shader_obj_reload(shader_obj_t *obj)
{
	GLuint prog;

	ASSERT(obj != NULL);
	ASSERT(obj->dirpath != NULL);
	ASSERT(obj->info != NULL);
	prog = shader_prog_from_info(obj->dirpath, obj->info);
	if (prog == 0)
		return (B_FALSE);
	if (obj->prog != 0)
		glDeleteProgram(obj->prog);
	obj->prog = prog;
	shader_obj_refresh_loc(obj);
	obj->load_time = time(NULL);

	return (B_TRUE);
}

static bool_t
check_shader_outdated(const shader_obj_t *obj, const shader_info_t *info)
{
	char *filepath;
	struct stat st;

	ASSERT(obj != NULL);
	ASSERT(info != NULL);
	filepath = mkpathname(obj->dirpath, info->filename, NULL);

	if (stat(filepath, &st) < 0) {
		LACF_DESTROY(filepath);
		return (B_FALSE);
	}
	LACF_DESTROY(filepath);

	return (st.st_mtime > obj->load_time);
}

bool_t
shader_obj_reload_check(shader_obj_t *obj)
{
	ASSERT(obj != NULL);

	if (DELAY_LINE_PUSH(&obj->check_delay, true)) {
		DELAY_LINE_PUSH_IMM(&obj->check_delay, false);

		ASSERT(obj->info != NULL);
		if ((obj->info->vert != NULL &&
		    check_shader_outdated(obj, obj->info->vert)) ||
		    (obj->info->frag != NULL &&
		    check_shader_outdated(obj, obj->info->frag)) ||
		    (obj->info->comp != NULL &&
		    check_shader_outdated(obj, obj->info->comp))) {
			return (shader_obj_reload(obj));
		} else {
			return (B_FALSE);
		}
	} else {
		return (B_FALSE);
	}
}
