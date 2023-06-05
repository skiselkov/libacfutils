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
 * This file contains automation machinery to help bulk dataref and command
 * destruction on plugin unload time. This avoids leaving dangling datarefs
 * and commands around and promotes code cleanliness. ("DCR" = Dataref and
 * Command Registration)
 *
 * The automation works using two functions. On plugin startup, you should
 * first call dcr_init() before attempting to use any other function or
 * macro in this file. Then use any of the `DCR_CREATE_*` family of macros,
 * or `dcr_*_cmd` functions to create/register datarefs and commands. The
 * DCR machinery will keep track of any datarefs and commands you've
 * created and/or registered. On plugin shutdown, you must call dcr_fini()
 * after you are done with any dataref manipulations. This will proceed
 * to delete and unregister any previously registered datarefs and
 * commands using any of the DCR family of macros and functions. Thus, you
 * won't need to keep track of making sure to clean up after each dataref.
 * You can simply create datarefs and commands without worrying about
 * immediately writing cleanup routines for all of them.
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

API_EXPORT void *dcr_alloc_rdr(void);
API_EXPORT dr_t *dcr_get_dr(void *token);
API_EXPORT void dcr_insert_rdr(void *token);
#ifdef	__clang
#pragma	GCC	diagnostic	push
#pragma	GCC	diagnostic	ignored "-Wnull-dereference"
#endif

/**
 * Internal, do not call, use the type-specific `DCR_CREATE` macros instead,
 * such as DR_CREATE_I() or DDR_CREATE_F().
 */
#define	DCR_CREATE_COMMON(type, dr_ptr, ...) \
	do { \
		void *__rdr = dcr_alloc_rdr(); \
		dr_t *__dr = dcr_get_dr(__rdr); \
		dr_create_ ## type(__dr, __VA_ARGS__); \
		dcr_insert_rdr(__rdr); \
		if ((dr_ptr) != NULL) \
			*(dr_t **)(dr_ptr) = __dr; \
	} while (0)
/**
 * Similar to the dr_create_i() function call from \ref dr.h, but the
 * first argument is of type `dr_t **`, instead of a plain `dr_t *`.
 * @param dr_p Optional pointer of type `dr_t **`. If not NULL, this
 *	pointer is set to point to the `dr_t` structure that was created
 *	as part of this dataref registration. The actual `dr_t` is held
 *	internally by the DCR machinery. If you don't wish to perform
 *	any further setup of the dataref, you may safely pass `NULL` here
 *	and more-or-less forget about where the `dr_t` is kept.
 * @param __value The `value` pointer passed to dr_create_i().
 * @param __writable The `writable` flag passed to dr_create_i().
 *
 * All remaining parameters are passed to the dr_create_i() function
 * as-is to specify the name format and format arguments to the dataref's
 * printf-style name creation.
 */
#define	DCR_CREATE_I(dr_p, __value, __writable, ...)		\
	DCR_CREATE_COMMON(i, dr_p, __value, __writable, __VA_ARGS__)
/**
 * Same as DCR_CREATE_I(), except creates a dataref using dr_create_f().
 */
#define	DCR_CREATE_F(dr_p, __value, __writable, ...)		\
	DCR_CREATE_COMMON(f, dr_p, __value, __writable, __VA_ARGS__)
/**
 * Same as DCR_CREATE_I(), except creates a dataref using dr_create_f64().
 */
#define	DCR_CREATE_F64(dr_p, __value, __writable, ...)	\
	DCR_CREATE_COMMON(f64, dr_p, __value, __writable, __VA_ARGS__)
/**
 * Same as DCR_CREATE_I(), except creates an array dataref using dr_create_vi()
 * and takes an additional number-of-elements argument in `__n`.
 */
#define	DCR_CREATE_VI(dr_p, __value, __n, __writable, ...)	\
	DCR_CREATE_COMMON(vi, dr_p, __value, __n, __writable, __VA_ARGS__)
/**
 * Same as DCR_CREATE_VI(), except creates an array dataref using
 * dr_create_vf().
 */
#define	DCR_CREATE_VF(dr_p, __value, __n, __writable, ...)	\
	DCR_CREATE_COMMON(vf, dr_p, __value, __n, __writable, __VA_ARGS__)
/**
 * Same as DCR_CREATE_VI(), except creates an array dataref using
 * dr_create_vf64().
 */
#define	DCR_CREATE_VF64(dr_p, __value, __n, __writable, ...)	\
	DCR_CREATE_COMMON(vf64, dr_p, __value, __n, __writable, __VA_ARGS__)
/**
 * Same as DCR_CREATE_VI(), except creates an auto-scalar array dataref
 * using dr_create_vi_autoscalar().
 */
#define	DCR_CREATE_VI_AUTOSCALAR(dr_p, __value, __n, __writable, ...) \
	DCR_CREATE_COMMON(vi_autoscalar, dr_p, __value, __n, __writable, \
	    __VA_ARGS__)
/**
 * Same as DCR_CREATE_VI(), except creates an auto-scalar array dataref
 * using dr_create_vf_autoscalar().
 */
#define	DCR_CREATE_VF_AUTOSCALAR(dr_p, __value, __n, __writable, ...) \
	DCR_CREATE_COMMON(vf_autoscalar, dr_p, __value, __n, __writable, \
	    __VA_ARGS__)
/**
 * Same as DCR_CREATE_VI(), except creates an auto-scalar array dataref
 * using dr_create_vf64_autoscalar().
 */
#define	DCR_CREATE_VF64_AUTOSCALAR(dr_p, __value, __n, __writable, ...) \
	DCR_CREATE_COMMON(vf64_autoscalar, dr_p, __value, __n, __writable, \
	    __VA_ARGS__)
/**
 * Same as DCR_CREATE_VI(), except creates a byte array dataref using
 * dr_create_b().
 */
#define	DCR_CREATE_B(dr_p, __value, __n, __writable, ...)		\
	DCR_CREATE_COMMON(b, dr_p, __value, __n, __writable, __VA_ARGS__)

#ifdef	__clang
#pragma	GCC	diagnostic	pop
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_DR_CMD_REG_H_ */
