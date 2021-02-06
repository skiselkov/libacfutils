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
 * Copyright 2019 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACF_UTILS_DR_CMD_REG_H_
#define	_ACF_UTILS_DR_CMD_REG_H_

#include <stdarg.h>
#include <stdbool.h>

#include "cmd.h"
#include "dr.h"

#ifdef	__cplusplus
extern "C" {
#endif

API_EXPORT void dcr_init(void);
API_EXPORT void dcr_fini(void);

API_EXPORT XPLMCommandRef dcr_find_cmd(PRINTF_FORMAT(const char *fmt),
    XPLMCommandCallback_f cb, bool before, void *refcon, ...)
    PRINTF_ATTR2(1, 5);
API_EXPORT XPLMCommandRef dcr_find_cmd_v(const char *fmt,
    XPLMCommandCallback_f cb, bool before, void *refcon, va_list ap);
API_EXPORT XPLMCommandRef f_dcr_find_cmd(PRINTF_FORMAT(const char *fmt),
    XPLMCommandCallback_f cb, bool before, void *refcon, ...)
    PRINTF_ATTR2(1, 5);
API_EXPORT XPLMCommandRef f_dcr_find_cmd_v(const char *fmt,
    XPLMCommandCallback_f cb, bool before, void *refcon, va_list ap);
API_EXPORT XPLMCommandRef dcr_create_cmd(const char *cmdname,
    const char *cmddesc, XPLMCommandCallback_f cb, bool before, void *refcon);

/* Internal, do not call, use the DCR_CREATE_ macros instead */
API_EXPORT void *dcr_alloc_rdr(void);
API_EXPORT dr_t *dcr_get_dr(void *token);
API_EXPORT void dcr_insert_rdr(void *token);
#define	DCR_CREATE_COMMON(type, dr_ptr, ...) \
	do { \
		void *rdr = dcr_alloc_rdr(); \
		dr_t *dr = dcr_get_dr(rdr); \
		dr_create_ ## type(dr, __VA_ARGS__); \
		dcr_insert_rdr(rdr); \
		if ((dr_ptr) != NULL) \
			*(dr_t **)(dr_ptr) = dr; \
	} while (0)
#define	DCR_CREATE_I(dr_p, ...)		\
	DCR_CREATE_COMMON(i, dr_p, __VA_ARGS__)
#define	DCR_CREATE_F(dr_p, ...)		\
	DCR_CREATE_COMMON(f, dr_p, __VA_ARGS__)
#define	DCR_CREATE_F64(dr_p, ...)	\
	DCR_CREATE_COMMON(f64, dr_p, __VA_ARGS__)
#define	DCR_CREATE_VI(dr_p, ...)	\
	DCR_CREATE_COMMON(vi, dr_p, __VA_ARGS__)
#define	DCR_CREATE_VF(dr_p, ...)	\
	DCR_CREATE_COMMON(vf, dr_p, __VA_ARGS__)
#define	DCR_CREATE_VF64(dr_p, ...)	\
	DCR_CREATE_COMMON(vf64, dr_p, __VA_ARGS__)
#define	DCR_CREATE_VI_AUTOSCALAR(dr_p, ...) \
	DCR_CREATE_COMMON(vi_autoscalar, dr_p, __VA_ARGS__)
#define	DCR_CREATE_VF_AUTOSCALAR(dr_p, ...) \
	DCR_CREATE_COMMON(vf_autoscalar, dr_p, __VA_ARGS__)
#define	DCR_CREATE_VF64_AUTOSCALAR(dr_p, ...) \
	DCR_CREATE_COMMON(vf64_autoscalar, dr_p, __VA_ARGS__)
#define	DCR_CREATE_B(dr_p, ...)		\
	DCR_CREATE_COMMON(b, dr_p, __VA_ARGS__)

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_DR_CMD_REG_H_ */
