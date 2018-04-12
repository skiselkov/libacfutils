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

/*
 * Loads a GLSL shader from a file and returns the shader program ID.
 * The shader type is passed in `shader_type'. The remainder of the variadic
 * arguments are path name components to the file containing the shader
 * source code. The list of path name components must be terminated by a
 * NULL argument.
 */
GLuint
shader_from_file(GLenum shader_type, const char *filename)
{
	GLchar *shader_text = NULL;
	GLint shader = 0;
	GLint compile_result;

	shader_text = file2str(filename, NULL);
	if (shader_text == NULL) {
		logMsg("Cannot load shader %s: %s", filename, strerror(errno));
		goto errout;
	}
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

	free(shader_text);

	return (shader);
errout:
	if (shader != 0)
		glDeleteShader(shader);
	free(shader_text);
	return (0);
}

GLuint
shader_prog_from_file(const char *progname, const char *vtx_file,
    const char *frag_file)
{
	GLuint vtx_shader = 0, frag_shader = 0, prog = 0;
	GLint linked;

	if (vtx_file != NULL) {
		vtx_shader = shader_from_file(GL_VERTEX_SHADER, vtx_file);
		if (vtx_shader == 0)
			return (0);
	}
	if (frag_file != NULL) {
		frag_shader = shader_from_file(GL_FRAGMENT_SHADER, frag_file);
		if (frag_shader == 0) {
			if (vtx_shader != 0)
				glDeleteShader(vtx_shader);
			return (0);
		}
	}
	prog = glCreateProgram();
	if (vtx_shader != 0)
		glAttachShader(prog, vtx_shader);
	if (frag_shader != 0)
		glAttachShader(prog, frag_shader);

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
		if (vtx_shader != 0)
			glDeleteShader(vtx_shader);
		if (frag_shader != 0)
			glDeleteShader(frag_shader);
		return (0);
	}
	if (vtx_shader != 0) {
		glDetachShader(prog, vtx_shader);
		glDeleteShader(vtx_shader);
	}
	if (frag_shader != 0) {
		glDetachShader(prog, frag_shader);
		glDeleteShader(frag_shader);
	}

	return (prog);
}
