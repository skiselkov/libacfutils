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
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */
/**
 * \file
 * This module is an aggregation of various OpenGL utility functions
 * and features. Check out the individual functions for what is available.
 * Before you start using the functionality in this module, be sure to
 * call glutils_sys_init(). There is no corresponding deinit function,
 * so once initialized, there's nothing else to do.
 */

#ifndef	_ACF_UTILS_GLUTILS_H_
#define	_ACF_UTILS_GLUTILS_H_

#include <stdio.h>

#include "assert.h"
#include "geom.h"
#include "glew.h"
#include "glutils_zink.h"
#include "log.h"
#include "safe_alloc.h"
#include "sysmacros.h"

#include <cglm/cglm.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	__AVX__
#define	MAT4_ALLOC_ALIGN	32
#else
#define	MAT4_ALLOC_ALIGN	16
#endif

/**
 * Utility system to emulate `GL_QUADS` drawing using modern OpenGL core
 * functionality. This object encapsulates the GPU-side buffers to draw
 * a number of quads, with automatic triangulation and support for the
 * programmable shader pipeline.
 *
 * Use glutils_init_2D_quads() or glutils_init_3D_quads() to initialize
 * a new quads object. You can then render the quads using
 * glutils_draw_quads(). If you want to update the vertex data of the
 * quads, use glutils_update_2D_quads() or glutils_update_3D_quads().
 * Use glutils_destroy_quads() to destroy the object if you don't need
 * it anymore.
 *
 * @see glutils_init_2D_quads()
 * @see glutils_init_3D_quads()
 * @see glutils_update_2D_quads()
 * @see glutils_update_3D_quads()
 * @see glutils_draw_quads()
 * @see glutils_destroy_quads()
 */
typedef struct {
	GLuint	vao;
	GLuint	vbo;
	GLuint	ibo;
	bool_t	setup;
	size_t	num_vtx;
} glutils_quads_t;

/**
 * @deprecated The glutils_lines_t functionality is deprecated, as it relies
 *	on legacy `GL_LINE_STRIP` functionality of the OpenGL driver.
 *	See glutils_nl_t for a modern replacement.
 *
 * Utility system to allow you to use the programmable rendering pipeline
 * while drawing lines using the `GL_LINE_STRIP` rendering mode. To
 * initialize a glutils_lines_t object, use glutils_init_3D_lines(). The
 * created object can then be drawn using glutils_draw_lines(). After you
 * are done with it, destroy it using glutils_destroy_lines().
 */
typedef struct {
	GLuint	vao;
	GLuint	vbo;
	bool_t	setup;
	size_t	num_vtx;
} glutils_lines_t;

typedef struct glutils_cache_s glutils_cache_t;

/**
 * This callback is what you need to pass to glutils_texsz_enum().
 * @param token This is the name of texture allocation token in which
 *	this allocation was performed.
 * @param bytes Number of bytes allocated in the respective token
 * @param userinfo User info pointer passed to glutils_texsz_enum()
 *	in the `userinfo` argument.
 */
typedef void (*glutils_texsz_enum_cb_t)(const char *token, int64_t bytes,
    void *userinfo);

API_EXPORT void glutils_sys_init(void);

API_EXPORT void glutils_disable_all_client_state(void);
API_EXPORT void glutils_disable_all_vtx_attrs(void);

API_EXPORT GLuint glutils_make_quads_IBO(size_t num_vtx);

/**
 * @return `B_TRUE` if the `quads` have been initialized. This basically just
 *	checks if the vertex buffer is non-zero, so to make sure this works
 *	correctly, you should statically zero-initialize any glutils_quads_t
 *	objects. In fact, just zero-initialize *everything* you create.
 * @see glutils_quads_t
 */
static inline bool_t
glutils_quads_inited(const glutils_quads_t *quads)
{
	ASSERT(quads != NULL);
	return (quads->vbo != 0);
}

/**
 * Same as glutils_init_3D_quads(), but expects an array of vect2_t points
 * in the `__p` argument. The input data to the shader program will still
 * be vec3's, but the Z coordinate will be zero.
 */
#define	glutils_init_2D_quads(__quads, __p, __t, __num_pts) \
	glutils_init_2D_quads_impl((__quads), log_basename(__FILE__), \
	    __LINE__, (__p), (__t), (__num_pts))
API_EXPORT void glutils_init_2D_quads_impl(glutils_quads_t *quads,
    const char *filename, int line, const vect2_t *p, const vect2_t *t,
    size_t num_pts);

/** Same as glutils_update_3D_quads(), but expects 2D points. */
#define	glutils_update_2D_quads(__quads, __p, __t, __num_pts) \
	glutils_update_2D_quads_impl((__quads), log_basename(__FILE__), \
	    __LINE__, (__p), (__t), (__num_pts))
API_EXPORT void glutils_update_2D_quads_impl(glutils_quads_t *quads,
    const char *filename, int line, const vect2_t *p, const vect2_t *t,
    size_t num_pts);
/**
 * Initializes a glutils_quads_t object.
 * @param __quads A pointer to the glutils_quads_t object to be initialized.
 * @param __p A mandatory pointer to an array of vect3_t points, which will
 *	form the corners of the individual quads. This data will be passed to
 *	the shader program as vec3's.
 * @param __t A optional pointer to an array of vect2_t UV coordinates, which
 *	will be passed to the shader as an additional input of vec2's. If you
 *	don't need this, simply pass `NULL` here.
 * @param __num_pts Number of points in `__p` and `__t`.
 * @see glutils_quads_t
 */
#define	glutils_init_3D_quads(__quads, __p, __t, __num_pts) \
	glutils_init_3D_quads_impl((__quads), log_basename(__FILE__), \
	    __LINE__, (__p), (__t), (__num_pts))
API_EXPORT void glutils_init_3D_quads_impl(glutils_quads_t *quads,
    const char *filename, int line, const vect3_t *p, const vect2_t *t,
    size_t num_pts);
/**
 * Updates the vertex data in a glutils_quad_t object, which was previously
 * initialized using glutils_init_3D_quads(). This replaces all vertex data
 * in the quads with the new data provided in the arguments here. The
 * meaning of the arguments is exactly the same as in
 * glutils_init_3D_quads().
 */
#define	glutils_update_3D_quads(__quads, __p, __t, __num_pts) \
	glutils_update_3D_quads_impl((__quads), log_basename(__FILE__), \
	    __LINE__, (__p), (__t), (__num_pts))
API_EXPORT void glutils_update_3D_quads_impl(glutils_quads_t *quads,
    const char *filename, int line, const vect3_t *p, const vect2_t *t,
    size_t num_pts);

API_EXPORT void glutils_destroy_quads(glutils_quads_t *quads);
API_EXPORT void glutils_draw_quads(glutils_quads_t *quads, GLint prog);
/**
 * @deprecated The glutils_lines_t functionality is deprecated, as it relies
 *	on legacy `GL_LINE_STRIP` functionality of the OpenGL driver.
 *	See glutils_nl_t for a modern replacement.
 *
 * Similar to glutils_init_3D_quads(), but utilizing a glutils_lines_t object.
 * The initialized glutils_lines_t object can be passed to
 * glutils_draw_lines() for rendering. When you are done with the object,
 * you should dispose of it using glutils_destroy_lines().
 * @param __lines A pointer to a glutils_lines_t object to be initialized.
 * @param __p A pointer to an array of vect3_t coordinates, which will be
 *	used as the points along the line strip.
 * @param __num_pts Number of points in `__p`.
 * @see glutils_lines_t
 * @see glutils_draw_lines()
 * @see glutils_destroy_lines()
 */
#define	glutils_init_3D_lines(__lines, __p, __num_pts) \
	glutils_init_3D_lines_impl((__lines), log_basename(__FILE__), \
	    __LINE__, (__p), (__num_pts))
API_EXPORT void glutils_init_3D_lines_impl(glutils_lines_t *lines,
    const char *filename, int line, const vect3_t *p, size_t num_pts);

API_EXPORT void glutils_destroy_lines(glutils_lines_t *lines);
API_EXPORT void glutils_draw_lines(glutils_lines_t *lines, GLint prog);

/*
 * glutils cache is a generic quads/lines object cache.
 */
API_EXPORT glutils_cache_t *glutils_cache_new(size_t cap_bytes);
API_EXPORT void glutils_cache_destroy(glutils_cache_t *cache);
API_EXPORT glutils_quads_t *glutils_cache_get_2D_quads(
    glutils_cache_t *cache, const vect2_t *p, const vect2_t *t, size_t num_pts);
API_EXPORT glutils_quads_t *glutils_cache_get_3D_quads(
    glutils_cache_t *cache, const vect3_t *p, const vect2_t *t, size_t num_pts);
API_EXPORT glutils_lines_t *glutils_cache_get_3D_lines(
    glutils_cache_t *cache, const vect3_t *p, size_t num_pts);

API_EXPORT void glutils_vp2pvm(GLfloat pvm[16]);

#define	GLUTILS_VALIDATE_INDICES(indices, num_idx, num_vtx) \
	do { \
		for (unsigned i = 0; i < (num_idx); i++) { \
			VERIFY_MSG((indices)[i] < (num_vtx), "invalid index " \
			    "specification encountered, index %d (value %d) " \
			    "is outside of vertex range %d", i, (indices)[i], \
			    (num_vtx)); \
		} \
	} while (0)

/**
 * The TEXSZ infrastructure is for debugging GPU VRAM memory leaks.
 *
 * At plugin load time (in XPluginStart and before doing any libacfutils
 * calls that might generate OpenGL calls), you must first initialize the
 * system using a call to glutils_texsz_init(). At plugin exit time, and
 * after having torn down all resources, call glutils_texsz_fini(). This
 * collects all garbage and crashes the app with diagnostic information
 * in case any leaks have been detected.
 *
 * Each allocation can be tracked in a two-level hierarchy:
 * - using a symbolic token name
 *   - each token can track allocations to a particular anonymous pointer
 *     (plus a `filename:line` tuple where it occurred)
 *
 * The tokens are used to identify large blocks of functionality. You'd
 * use a token for, for example, "efis_textures" or "custom_drawing_pbo",
 * etc. These must be declared ahead at the top of each module file using
 * the TEXSZ_MK_TOKEN macro, for example:
 *```
 * TEXSZ_MK_TOKEN(efis_textures);
 *```
 * Don't put spaces into the token name, the name must be a valid C
 * identifier. You can subsequently track allocations to this token using
 * the TEXSZ_ALLOC() and TEXSZ_FREE() macros.
 *
 * Please note that TEXSZ_MK_TOKEN() creates a static (single-module) token
 * that cannot be shared between multiple C files. If you plan on using a
 * TEXSZ token from more than one C/C++ file, you must declare the token
 * in a header file using the TEXSZ_DECL_TOKEN_GLOB() macro instead. Then
 * define the token in a single C file using TEXSZ_DEF_TOKEN_GLOB:
 *```
 *	TEXSZ_DECL_TOKEN_GLOB(efis_textures);	<--- goes in a header file
 *```
 *```
 *	TEXSZ_DEF_TOKEN_GLOB(efis_textures);	<--- goes in a *single* C file
 *```
 * In case you are using a generic facility (e.g. a picture loader) to
 * provide texturing service to other parts of the code, a simple token
 * name might not be specific enough to pinpoint the offending allocation.
 * In that case, you can use the TEXSZ_ALLOC_INSTANCE() and
 * TEXSZ_FREE_INSTANCE() macros to generate per-pointer statistics.
 *
 * In addition to the TEXSZ_ALLOC_* and TEXSZ_FREE_* macros, there are
 * variations of these macros with the "_BYTES" suffix. These let you
 * pass the raw byte size of the allocation directly, instead of having
 * to pass texture formats and sizes. Use these when the data isn't a
 * texture, but instead something more generic, like a vertex buffer.
 *
 * @see TEXSZ_ALLOC_BYTES()
 * @see TEXSZ_FREE_BYTES()
 * @see TEXSZ_ALLOC_BYTES_INSTANCE()
 * @see TEXSZ_FREE_BYTES_INSTANCE()
 */
#define	TEXSZ_MK_TOKEN(name) \
	static const char *__texsz_token_ ## name = #name
/**
 * Declares a global TEXSZ system tracking token. Place this into a header
 * which will be included from all modules which will need to use this token.
 * @see TEXSZ_MK_TOKEN
 */
#define	TEXSZ_DECL_TOKEN_GLOB(name) \
	extern const char *__texsz_token_ ## name
/**
 * Defines a global TEXSZ system tracking token. Place this into a single
 * implementation modules, which is where the token will live.
 * @see TEXSZ_MK_TOKEN
 */
#define	TEXSZ_DEF_TOKEN_GLOB(name) \
	const char *__texsz_token_ ## name = #name
/**
 * Notifies the TEXSZ system of a texture allocation by incrementing the
 * token's byte counter. Every call to TEXSZ_ALLOC() must be balanced
 * by call to TEXSZ_FREE(). If a token is non-zero at the time
 * glutils_texsz_fini() is called, the offending token name(s) are
 * printed in sequence, with the amount of bytes leaked in them. In
 * this mode, no filenames or line numbers are
 * printed.
 * @param __token_id The token name previously created using TEXSZ_MK_TOKEN().
 * @param __format The texture format (e.g. `GL_RGBA`).
 * @param __type The texture data type (e.g. `GL_UNSIGNED_BYTE`).
 * @param __w Texture width in pixels.
 * @param __h Texture height in pixels.
 */
#define	TEXSZ_ALLOC(__token_id, __format, __type, __w, __h) \
	TEXSZ_ALLOC_INSTANCE(__token_id, NULL, NULL, -1, (__format), \
	    (__type), (__w), (__h))
/**
 * Notifies the TEXSZ system of a texture deallocation. The resouce must
 * have previously been registered using the TEXSZ_ALLOC() macro. All
 * arguments in this macro have the same meaning as in TEXSZ_ALLOC().
 */
#define	TEXSZ_FREE(__token_id, __format, __type, __w, __h) \
	TEXSZ_FREE_INSTANCE(__token_id, NULL, (__format), \
	    (__type), (__w), (__h))
/**
 * Performs a similar function to TEXSZ_ALLOC(), but allows for more granular
 * tracking than per-whole-token. To allow you to keep track of individual
 * allocations, this macro takes two additional parameters over TEXSZ_ALLOC():
 * @param __instance An instance pointer - this is used to discriminate
 *	individual allocations. Usually you'd want to pass a containing
 *	structure pointer or something similar in here.
 * @param __filename An allocation point filename.
 * @param __line An allocation point line number.
 *
 * The filename should be shortened at build time using log_backtrace()
 * to only contain the last portion of the filename of the call site.
 * You should wrap your functions which use TEXSZ_ALLOC_INSTANCE() into a
 * macro and extract the call site information automatically using the
 * `__FILE__` and `__LINE__` built-in pre-processor variables (see the
 * `glutils_init_*_quads()` functions for an example on how to do that).
 * With instancing, glutils_texsz_fini() will print a list of leaked
 * instance pointers and call sites in your code where that resource was
 * leaked.
 *
 * Allocations registered using TEXSZ_ALLOC_INSTANCE() must be freed using
 * the TEXSZ_FREE_INSTANCE() macro, with the original pointer used in the
 * `__instance` argument.
 */
#define	TEXSZ_ALLOC_INSTANCE(__token_id, __instance, __filename, __line, \
    __format, __type, __w, __h) \
	glutils_texsz_alloc(__texsz_token_ ## __token_id, (__instance), \
	    (__filename), (__line), (__format), (__type), (__w), (__h))
/**
 * Frees an instanced allocation, previously registered using
 * TEXSZ_ALLOC_INSTANCE(). You must use the same `__instance` pointer
 * as was used for the allocation. There is no filename or line argument
 * anymore, since they are only stored in the allocation information.
 * The allocation MUST exist, otherwise this macro causes an assertion
 * failure.
 */
#define	TEXSZ_FREE_INSTANCE(__token_id, __instance, __format, __type, \
    __w, __h) \
	glutils_texsz_free(__texsz_token_ ## __token_id, (__instance), \
	    (__format), (__type), (__w), (__h))
/**
 * Same as TEXSZ_ALLOC(), but rather than taking texture information to
 * calculate the allocation size, takes an explicit byte count as an argument.
 * This lets you use the TEXSZ machinery to track allocations which are not
 * textures (e.g. vertex buffers).
 */
#define	TEXSZ_ALLOC_BYTES(__token_id, __bytes) \
	TEXSZ_ALLOC_BYTES_INSTANCE(__token_id, NULL, NULL, -1, (__bytes))
/**
 * Same as TEXSZ_FREE(), but rather than taking texture information to
 * calculate the allocation size, takes an explicit byte count as an argument.
 * This basically undoes a TEXSZ_ALLOC_BYTES() operation.
 */
#define	TEXSZ_FREE_BYTES(__token_id, __bytes) \
	TEXSZ_FREE_BYTES_INSTANCE(__token_id, NULL, (__bytes))
/**
 * Instanced variant of TEXSZ_ALLOC_BYTES(). The `__instance` argument
 * matches the behavior of TEXSZ_ALLOC_INSTANCE() instance argument.
 * @see TEXSZ_ALLOC_BYTES()
 * @see TEXSZ_ALLOC_INSTANCE()
 */
#define	TEXSZ_ALLOC_BYTES_INSTANCE(__token_id, __instance, __filename, \
    __line, __bytes) \
	glutils_texsz_alloc_bytes(__texsz_token_ ## __token_id, (__instance), \
	    (__filename), (__line), (__bytes))
/**
 * Instanced variant of TEXSZ_FREE_BYTES(). The `__instance` argument
 * matches the behavior of TEXSZ_FREE_INSTANCE() instance argument.
 * @see TEXSZ_FREE_BYTES()
 * @see TEXSZ_FREE_INSTANCE()
 */
#define	TEXSZ_FREE_BYTES_INSTANCE(__token_id, __instance, __bytes) \
	glutils_texsz_free_bytes(__texsz_token_ ## __token_id, (__instance), \
	    (__bytes))

API_EXPORT void glutils_texsz_init(void);
API_EXPORT void glutils_texsz_fini(void);
API_EXPORT void glutils_texsz_alloc(const char *token, const void *instance,
    const char *filename, int line, GLenum format, GLenum type,
    unsigned w, unsigned h);
API_EXPORT void glutils_texsz_free(const char *token, const void *instance,
    GLenum format, GLenum type, unsigned w, unsigned h);
API_EXPORT void glutils_texsz_alloc_bytes(const char *token,
    const void *instance, const char *filename, int line, int64_t bytes);
API_EXPORT void glutils_texsz_free_bytes(const char *token,
    const void *instance, int64_t bytes);

API_EXPORT uint64_t glutils_texsz_get(void);
API_EXPORT void glutils_texsz_enum(glutils_texsz_enum_cb_t cb, void *userinfo);

/**
 * Wrapper macro to execute an optional bit of code only if the TEXSZ
 * debug system is in use. Wrap any usage of the `TEXSZ_ALLOC*` and
 * `TEXSZ_FREE*` macros in this macro, to only enable them when the
 * TEXSZ system has been initialized. You can also use this to control
 * execution of your own reporting code.
 */
#define	IF_TEXSZ(__xxx) \
	do { \
		if (glutils_texsz_inited()) { \
			__xxx; \
		} \
	} while (0)
API_EXPORT bool_t glutils_texsz_inited(void);

#ifndef	_LACF_RENDER_DEBUG
/**
 * This macro controls whether the `GLUTILS_ASSERT*` and
 * GLUTILS_RESET_ERRORS() macros are enabled. The default state is that
 * these macros are disabled and do not get compiled into your code. If
 * you want to enable them, define this macro to a non-zero value in your
 * build system.
 */
#define	_LACF_RENDER_DEBUG	0
#endif

/**
 * \def GLUTILS_ASSERT_NO_ERROR()
 * If enabled, this macro compiles to a hard assertion check, which verifies
 * that the glGetError() functio returns no error. This can be used for
 * debugging of drawing code, to make sure it generated no error. Enablement
 * is controlled using the \ref _LACF_RENDER_DEBUG macro.
 * @see _LACF_RENDER_DEBUG
 *
 * \def GLUTILS_ASSERT()
 * Equivalent of a VERIFY() check, but only enabled if
 * \ref _LACF_RENDER_DEBUG is non-zero.
 * @see _LACF_RENDER_DEBUG
 *
 * \def GLUTILS_ASSERT_MSG()
 * Equivalent of a VERIFY_MSG() check, but only enabled if
 * \ref _LACF_RENDER_DEBUG is non-zero.
 * @see _LACF_RENDER_DEBUG
 *
 * \def GLUTILS_ASSERT3S()
 * Equivalent of a VERIFY3S() check, but only enabled if
 * \ref _LACF_RENDER_DEBUG is non-zero.
 * @see _LACF_RENDER_DEBUG
 *
 * \def GLUTILS_ASSERT3U()
 * Equivalent of a VERIFY3U() check, but only enabled if
 * \ref _LACF_RENDER_DEBUG is non-zero.
 * @see _LACF_RENDER_DEBUG
 *
 * \def GLUTILS_ASSERT3P()
 * Equivalent of a VERIFY3P() check, but only enabled if
 * \ref _LACF_RENDER_DEBUG is non-zero.
 * @see _LACF_RENDER_DEBUG
 *
 * \def GLUTILS_RESET_ERRORS()
 * If enabled, calls glutils_reset_errors() to drain the OpenGL error
 * stack for debugging purposes. Enablement is controlled by
 * \ref _LACF_RENDER_DEBUG.
 * @see _LACF_RENDER_DEBUG
 */

#if	_LACF_RENDER_DEBUG
#define	GLUTILS_ASSERT_NO_ERROR()	VERIFY3U(glGetError(), ==, GL_NO_ERROR)
#define	GLUTILS_ASSERT(_x_)		VERIFY(_x_)
#define	GLUTILS_ASSERT_MSG(_x_, ...)	VERIFY(_x_, __VA_ARGS__)
#define	GLUTILS_ASSERT3S(_x_, _y_, _z_)	VERIFY3S(_x_, _y_, _z_)
#define	GLUTILS_ASSERT3U(_x_, _y_, _z_)	VERIFY3U(_x_, _y_, _z_)
#define	GLUTILS_ASSERT3P(_x_, _y_, _z_)	VERIFY3P(_x_, _y_, _z_)
#define	GLUTILS_RESET_ERRORS()		glutils_reset_errors()
#else	/* !_LACF_RENDER_DEBUG */
#define	GLUTILS_ASSERT_NO_ERROR()
#define	GLUTILS_ASSERT(_x_)
#define	GLUTILS_ASSERT_MSG(_x_, ...)
#define	GLUTILS_ASSERT3S(_x_, _y_, _z_)
#define	GLUTILS_ASSERT3U(_x_, _y_, _z_)
#define	GLUTILS_ASSERT3P(_x_, _y_, _z_)
#define	GLUTILS_RESET_ERRORS()
#endif	/* !_LACF_RENDER_DEBUG */

API_EXPORT bool_t glutils_nsight_debugger_present(void);

/**
 * Drains the OpenGL error stack by repeatedly calling glGetError() until
 * no more errors remain. Use this for development debugging.
 */
static inline void
glutils_reset_errors(void)
{
	while (glGetError() != GL_NO_ERROR)
		;
}

/**
 * \fn static inline void glutils_debug_push(GLuint, const char *, ...)
 * If \ref _LACF_RENDER_DEBUG is enabled, this compiles to a function
 * which calls glPushDebugGroup() for draw call debugging. The source
 * of the message is always `GL_DEBUG_SOURCE_APPLICATION`.
 *
 * You must pair this call with a subsequent call to glutils_debug_pop()
 * to close out the debug group.
 * @param msgid The message ID that will be passed in the second argument
 *	of glPushDebugGroup().
 * @param format A printf-style format string, which will be used to
 *	dynamically control the message for the glPushDebugGroup() function.
 *	The remainder of the variadic arguments must match to the format
 *	specifiers.
 * @see _LACF_RENDER_DEBUG
 * @see glutils_debug_pop()
 * @see [glPushDebugGroup()](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glPushDebugGroup.xhtml)
 *
 * \fn static inline void glutils_debug_pop(void)
 * If \ref _LACF_RENDER_DEBUG is enabled, this compiles to a call to
 * glPopDebugGroup(). You should use this in combination with
 * glutils_debug_push(), set up debug groups for your rendering code.
 * @see _LACF_RENDER_DEBUG
 * @see glutils_debug_push()
 * @see [glPopDebugGroup()](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glPopDebugGroup.xhtml)
 */

/*
 * MacOS doesn't support OpenGL 4.3.
 */
#if	_LACF_RENDER_DEBUG && !APL

PRINTF_ATTR(2) static inline void
glutils_debug_push(GLuint msgid, const char *format, ...)
{
	char buf_stack[128];
	int len;
	va_list ap;

	va_start(ap, format);
	len = vsnprintf(buf_stack, sizeof (buf_stack), format, ap);
	va_end(ap);

	if (len >= (int)sizeof (buf_stack)) {
		char *buf_heap = (char *)safe_malloc(len + 1);

		va_start(ap, format);
		vsnprintf(buf_heap, len + 1, format, ap);
		va_end(ap);

		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, msgid, len,
		    buf_heap);

		free(buf_heap);
	} else {
		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, msgid, len,
		    buf_stack);
	}
}

static inline void
glutils_debug_pop(void)
{
	glPopDebugGroup();
}

#else	/* !_LACF_RENDER_DEBUG || APL */

ALWAYS_INLINE_ATTR PRINTF_ATTR(2) static inline void
glutils_debug_push(GLuint msgid, const char *format, ...)
{
	UNUSED(msgid);
	UNUSED(format);
	/* no-op */
}

ALWAYS_INLINE_ATTR static inline void
glutils_debug_pop(void)
{
	/* no-op */
}

#endif	/* !_LACF_RENDER_DEBUG || APL */

typedef struct glutils_nl_s glutils_nl_t;

API_EXPORT glutils_nl_t *glutils_nl_alloc_2D(const vec2 *pts, size_t num_pts);
API_EXPORT glutils_nl_t *glutils_nl_alloc_3D(const vec3 *pts, size_t num_pts);
API_EXPORT void glutils_nl_free(glutils_nl_t *nl);
API_EXPORT void glutils_nl_draw(glutils_nl_t *nl, float width, GLuint prog);

/**
 * A wrapper for glEnableVertexAttribArray() and glVertexAttribPointer().
 * In addition to performing both operations at the same time, this only
 * gets executed if `index` is NOT -1, indicating that the shader program
 * you are setting up the inputs for actually does take the input.
 * @param index The vertex attribute array index to enable. If this is -1,
 *	this function turns into a no-op. This happens, when the shader
 *	program doesn't actually use the input you are attempting to bind
 *	(glGetAttribLocation() returns this for non-existent attributes).
 *
 * The remainder of the arguments are passed on as-is to
 * glVertexAttribPointer().
 * @see [glEnableVertexAttribArray()](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glEnableVertexAttribArray.xhtml)
 * @see [glVertexAttribPointer()](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glVertexAttribPointer.xhtml)
 */
static inline void
glutils_enable_vtx_attr_ptr(GLint index, GLint size, GLenum type,
    GLboolean normalized, size_t stride, size_t offset)
{
	if (index != -1) {
		glEnableVertexAttribArray(index);
		glVertexAttribPointer(index, size, type, normalized,
		    stride, (void *)offset);
	}
}

/**
 * Disables the vertex attribute array at `index` by calling
 * glDisableVertexAttribArray(), but only if `index` is NOT -1.
 * See glutils_enable_vtx_attr_ptr() for more information.
 * @see glutils_enable_vtx_attr_ptr()
 * @see [glDisableVertexAttribArray()](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glEnableVertexAttribArray.xhtml)
 */
static inline void
glutils_disable_vtx_attr_ptr(GLint index)
{
	if (index != -1)
		glDisableVertexAttribArray(index);
}

API_EXPORT bool_t glutils_png2gltexfmt(int png_color_type, int png_bit_depth,
    GLint *int_fmt, GLint *fmt, GLint *type);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_GLUTILS_H_ */
