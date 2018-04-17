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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

#include <acfutils/helpers.h>
#include <acfutils/log.h>
#include <acfutils/safe_alloc.h>
#include <acfutils/shader.h>

static GLuint shaders2prog(const char *progname, GLuint vert_shader,
    GLuint frag_shader, va_list ap);

/*
 * Loads and compiles GLSL shader from a file and returns the shader object
 * ID. The shader type is passed in `shader_type'. Returns 0 if the shader
 * failed to load (an error description is printed to the X-Plane log).
 */
API_EXPORT GLuint
shader_from_file(GLenum shader_type, const char *filename)
{
	GLchar *shader_text = NULL;
	GLuint shader;

	shader_text = file2str(filename, NULL);
	if (shader_text == NULL) {
		logMsg("Cannot load shader %s: %s", filename, strerror(errno));
		return (0);
	}
	shader = shader_from_text(shader_type, shader_text, filename);
	lacf_free(shader_text);

	return (shader);
}

/*
 * Loads and compiles GLSL shader from a string of text and returns the
 * shader object ID. The shader type is passed in `shader_type'. Returns
 * 0 if the shader failed to load (an error description is printed to the
 * X-Plane log).
 */
API_EXPORT GLuint
shader_from_text(GLenum shader_type, const GLchar *shader_text,
    const char *filename)
{
	GLint shader = 0;
	GLint compile_result;

	ASSERT(shader_text != NULL);
	if (filename == NULL)
		filename = "<cstring>";

	shader = glCreateShader(shader_type);
	if (shader == 0) {
		logMsg("Cannot load shader %s: glCreateShader failed with "
		    "error 0x%x", filename, glGetError());
		goto errout;
	}
	glShaderSource(shader, 1, (const GLchar *const*)&shader_text, NULL);
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

	va_start(ap, frag_file);
	res = shader_prog_from_file_v(progname, vert_file, frag_file, ap);
	va_end(ap);

	return (res);
}

/*
 * Same as shader_prog_from_file, except takes a va_list for the variadic
 * vertex attribute binding list.
 */
API_EXPORT GLuint
shader_prog_from_file_v(const char *progname, const char *vert_file,
    const char *frag_file, va_list ap)
{
	GLuint vert_shader = 0, frag_shader = 0;

	if (vert_file != NULL) {
		vert_shader = shader_from_file(GL_VERTEX_SHADER, vert_file);
		if (vert_shader == 0)
			return (0);
	}
	if (frag_file != NULL) {
		frag_shader = shader_from_file(GL_FRAGMENT_SHADER, frag_file);
		if (frag_shader == 0) {
			if (vert_shader != 0)
				glDeleteShader(vert_shader);
			return (0);
		}
	}

	return (shaders2prog(progname, vert_shader, frag_shader, ap));
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

	va_start(ap, frag_text);
	res = shader_prog_from_text_v(progname, vert_text, frag_text, ap);
	va_end(ap);

	return (res);
}

/*
 * Same as shader_prog_from_text, except takes a va_list for the variadic
 * vertex attribute binding list.
 */
API_EXPORT GLuint
shader_prog_from_text_v(const char *progname, const char *vert_text,
    const char *frag_text, va_list ap)
{
	GLuint vert_shader = 0, frag_shader = 0;

	if (vert_text != NULL) {
		vert_shader = shader_from_text(GL_VERTEX_SHADER, vert_text,
		    NULL);
		if (vert_shader == 0)
			return (0);
	}
	if (frag_text != NULL) {
		frag_shader = shader_from_text(GL_FRAGMENT_SHADER, frag_text,
		    NULL);
		if (frag_shader == 0) {
			if (vert_shader != 0)
				glDeleteShader(vert_shader);
			return (0);
		}
	}

	return (shaders2prog(progname, vert_shader, frag_shader, ap));
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
    va_list ap)
{
	GLuint prog = 0;
	GLint linked;

	prog = glCreateProgram();
	if (vert_shader != 0)
		glAttachShader(prog, vert_shader);
	if (frag_shader != 0)
		glAttachShader(prog, frag_shader);

	for (;;) {
		const char *attr_name = va_arg(ap, const char *);
		GLuint attr_idx;

		if (attr_name == NULL)
			break;
		attr_idx = va_arg(ap, GLuint);

		ASSERT(vert_shader != 0);
		glBindAttribLocation(prog, attr_idx, attr_name);
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

	return (prog);
}
