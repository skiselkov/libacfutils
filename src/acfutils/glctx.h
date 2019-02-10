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

API_EXPORT glctx_t *glctx_create_invisible(void *win_ptr, void *share_ctx,
    int major_ver, int minor_ver, bool_t fwd_compat, bool_t debug);
API_EXPORT void *glctx_get_window_system_handle(glctx_t *ctx);
API_EXPORT void *glctx_get_xplane_win_ptr(void);

API_EXPORT glctx_t *glctx_create_current(void);
API_EXPORT bool_t glctx_make_current(glctx_t *ctx);

API_EXPORT void glctx_destroy(glctx_t *ctx);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_GLCTX_H_ */
