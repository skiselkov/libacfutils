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

#include <acfutils/cmd.h>
#include <acfutils/dr.h>

#ifdef	__cplusplus
extern "C" {
#endif

void dcr_init(void);
void dcr_fini(void);

void dcr_add_dr(dr_t *dr);

XPLMCommandRef dcr_find_cmd(PRINTF_FORMAT(const char *fmt),
    XPLMCommandCallback_f cb, bool before, void *refcon, ...)
    PRINTF_ATTR2(1, 5);
XPLMCommandRef dcr_find_cmd_v(const char *fmt, XPLMCommandCallback_f cb,
    bool before, void *refcon, va_list ap);
XPLMCommandRef f_dcr_find_cmd(PRINTF_FORMAT(const char *fmt),
    XPLMCommandCallback_f cb, bool before, void *refcon, ...)
    PRINTF_ATTR2(1, 5);
XPLMCommandRef f_dcr_find_cmd_v(const char *fmt, XPLMCommandCallback_f cb,
    bool before, void *refcon, va_list ap);
XPLMCommandRef dcr_create_cmd(const char *cmdname, const char *cmddesc,
    XPLMCommandCallback_f cb, bool before, void *refcon);


#define	DCR_CREATE_COMMON(type, dr, ...) \
	do { \
		dr_create_ ## type((dr), __VA_ARGS__); \
		dcr_add_dr((dr)); \
	} while (0)
#define	DCR_CREATE_I(dr, ...)		DCR_CREATE_COMMON(i, dr, __VA_ARGS__)
#define	DCR_CREATE_F(dr, ...)		DCR_CREATE_COMMON(f, dr, __VA_ARGS__)
#define	DCR_CREATE_F64(dr, ...)		DCR_CREATE_COMMON(f64, dr, __VA_ARGS__)
#define	DCR_CREATE_VI(dr, ...)		DCR_CREATE_COMMON(vi, dr, __VA_ARGS__)
#define	DCR_CREATE_VF(dr, ...)		DCR_CREATE_COMMON(vf, dr, __VA_ARGS__)
#define	DCR_CREATE_VF64(dr, ...)	DCR_CREATE_COMMON(vf64, dr, __VA_ARGS__)
#define	DCR_CREATE_B(dr, ...)		DCR_CREATE_COMMON(b, dr, __VA_ARGS__)

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_DR_CMD_REG_H_ */
