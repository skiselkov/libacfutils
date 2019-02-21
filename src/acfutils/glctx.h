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

#ifndef	_ACF_UTILS_GLCTX_H_
#define	_ACF_UTILS_GLCTX_H_

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct glctx_s glctx_t;

/*
 * OpenGL VERSIONS
 *
 * OpenGL comes in two major flavors:
 *
 * 1) Modern OpenGL (3.2+)
 * 2) Legacy OpenGL (2.0 and earlier)
 *
 * There are major incompatibilities between the two branches and
 * you SHOULD always strive to write modern OpenGL code, not legacy.
 * On Windows and Linux, you can request a modern OpenGL context
 * (version 4.0 or later) and most reasonably recent drivers will
 * transparently support legacy OpenGL constructs (unless you set
 * fwd_compat=B_TRUE, in which case we will instruct the driver to
 * explicitly reject legacy features).
 *
 * However, on MacOS this separation is enforced with an iron hand.
 * So on MacOS, glctx_create_invisible ignores the version arguments
 * except for major_ver:
 *	1) if you specify major_ver <= 2, you will get OpenGL legacy
 *	   (version 1.0 up to 2.1 core features)
 *	2) if you specify major_ver = 3, you will get OpenGL 3.2+
 *	   feature compatibility
 *	3) in all other cases, you will get up to OpenGL 4.1
 *	   feature compatibility
 * Please note that MacOS doesn't support any later OpenGL revisions,
 * so the maximum you can get reliably is the OpenGL 4.1 core profile.
 */

/*
 * Creates an invisible OpenGL context of a specified version in a
 * platform-independent way. Please note that this must be called on
 * the main X-Plane thread. To create a background rendering thread,
 * call glctx_create_invisible() on the main thread, then hand off the
 * created glctx_t to the background thread, which will subsequently
 * set it as its current context using glctx_make_current().
 *
 * @param win_ptr An opaque window pointer to a backing window that the
 *	OpenGL will be based on. The context will NOT be drawing to this
 *	window. The window handle is only used to fetch hardware context
 *	information such as the target GPU and supported capabilities.
 *	Unless you know exactly what you are doing, you should always
 *	pass the return value of glctx_get_xplane_win_ptr() here.
 * @param share_ctx An optional OpenGL context that you want the new
 *	context to share resources with. Please note that this will also
 *	share the command pipeline between the contexts, which can
 *	introduced undesirable command contention, so use with care.
 * @param major_ver Major OpenGL version requested. See "OpenGL VERSIONS".
 * @param minor_ver Major OpenGL version requested. See "OpenGL VERSIONS".
 * @param fwd_compat If set to true, the returned OpenGL context will NOT
 *	be backwards compatible. See "OpenGL VERSIONS" for more info.
 * @param debug Enables additional error checking and reporting on
 *	platforms that support it (Linux & Windows), often at the cost of
 *	some performance. Enable on debug code, disable on production code.
 *
 * @return On success, an initialized OpenGL context. On failure, the
 *	function returns NULL and prints an error reason to the log.
 *	The returned context will NOT yet have been made the current
 *	context. To make the context current, use glctx_make_current.
 */
API_EXPORT glctx_t *glctx_create_invisible(void *win_ptr, glctx_t *share_ctx,
    int major_ver, int minor_ver, bool_t fwd_compat, bool_t debug);

/*
 * Returns an opaque handle to the X-Plane main window that can be used
 * in glctx_create_invisible to create a context with the same hardware
 * target. This function must ONLY be called on the main X-Plane thread.
 * The returned value is platform-specific as follows:
 *	1) On Windows, this returns the HWND of the main X-Plane window.
 *	2) On Linux, this returns the value of the "DISPLAY" environment
 *	   variable (or NULL if the variable isn't defined), which can
 *	   then be passed to XOpenDisplay to create an X-Windows connection
 *	   on the same server as where X-Plane is running.
 *	3) On Mac, this function always returns NULL.
 */
API_EXPORT void *glctx_get_xplane_win_ptr(void);

/*
 * Sets a glctx_t as the calling thread's current context. To remove any
 * context as the current context, pass NULL for `ctx' in this function.
 */
API_EXPORT bool_t glctx_make_current(glctx_t *ctx);

/*
 * Returns an opaque window system connection handle that can be used to
 * interrogate various hardware properties. This is highly platform
 * specific:
 *	1) On Windows, this returns the HWND of the associated context.
 *	2) On Linux, this returns the Display pointer.
 *	3) On Mac, this function always returns NULL.
 */
API_EXPORT void *glctx_get_window_system_handle(glctx_t *ctx);

/*
 * Constructs a glctx_t from the current context and returns it.
 * If there is no current context, this function returns NULL.
 * Please note that you MUST release the returned glctx_t structure
 * using glctx_destroy when you are done with it. This does NOT
 * destroy the underlying context. You should only use this to grab
 * X-Plane's main rendering context for the purpose of passing it
 * to glctx_create_invisible as a share_ctx.
 */
API_EXPORT glctx_t *glctx_get_current(void);

/*
 * Returns B_TRUE if `ctx' is the current rendering context, B_FALSE otheriwse.
 */
API_EXPORT bool_t glctx_is_current(glctx_t *ctx);

/*
 * Gets the underlying OpenGL context handle from a glctx_t. This is
 * highly platform-specific:
 * 1) On Windows, this returns an HGLRC
 * 2) On Linux, this returns a GLXContext
 * 3) On Mac, this returns a CGLContextObj
 */
API_EXPORT void *glctx_get_handle(const glctx_t *ctx);

/*
 * Destroys a previously constructed glctx_t structure. For contexts
 * created using glctx_get_current, this DOESN'T destroy the underlying
 * context. Contexts created using glctx_create_invisible, this DOES
 * destroy the underlying context and MUST be called when you are done
 * using the context.
 */
API_EXPORT void glctx_destroy(glctx_t *ctx);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_GLCTX_H_ */
