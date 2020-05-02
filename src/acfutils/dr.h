/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
*/
/*
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#ifndef	_DR_H_
#define	_DR_H_

#include <stdlib.h>
#include <string.h>

#include <XPLMDataAccess.h>

#include <acfutils/helpers.h>
#include <acfutils/log.h>
#include <acfutils/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	DR_MAX_NAME_LEN	128

typedef struct dr_s dr_t;

struct dr_s {
	char		name[DR_MAX_NAME_LEN];
	XPLMDataRef	dr;
	XPLMDataTypeID	type;
	bool_t		writable;
	bool_t		wide_type;
	void		*value;
	ssize_t		count;
	size_t		stride;
	void		(*read_cb)(dr_t *, void *);
	void		(*write_cb)(dr_t *, void *);
	int		(*read_array_cb)(dr_t *, void *, int, int);
	void		(*write_array_cb)(dr_t *, void *, int, int);
	void		*cb_userinfo;
};
#define	DATAREF_UNINIT	lacf_uninit_dataref

API_EXPORT bool_t dr_find(dr_t *dr, PRINTF_FORMAT(const char *fmt), ...)
    PRINTF_ATTR(2);
#define	fdr_find(dr, ...) \
	do { \
		if (!dr_find(dr, __VA_ARGS__)) { \
			char drname[DR_MAX_NAME_LEN]; \
			snprintf(drname, sizeof (drname), __VA_ARGS__); \
			VERIFY_MSG(0, "dataref \"%s\" not found", drname); \
		} \
	} while (0)

API_EXPORT bool_t dr_writable(dr_t *dr);

#define	DR_DEBUG(__varstr)	log_basename(__FILE__), __LINE__, __varstr
#define	DR_DEBUG_VARS	\
	const char *filename, int line, const char *varname

#define	dr_geti(__dr)		dr_geti_impl((__dr), DR_DEBUG(#__dr))
API_EXPORT int dr_geti_impl(dr_t *dr, DR_DEBUG_VARS) HOT_ATTR;
#define	dr_seti(__dr, __i)	dr_seti_impl((__dr), DR_DEBUG(#__dr), (__i))
API_EXPORT void dr_seti_impl(dr_t *dr, DR_DEBUG_VARS, int i) HOT_ATTR;

#define	dr_getf(__dr)		dr_getf_impl((__dr), DR_DEBUG(#__dr))
API_EXPORT double dr_getf_impl(dr_t *dr, DR_DEBUG_VARS) HOT_ATTR;
#define	dr_setf(__dr, __f)	dr_setf_impl((__dr), DR_DEBUG(#__dr), (__f))
API_EXPORT void dr_setf_impl(dr_t *dr, DR_DEBUG_VARS, double f) HOT_ATTR;

#define	dr_getvi(__dr, __i, __off, __num) \
	dr_getvi_impl((__dr), DR_DEBUG(#__dr), (__i), (__off), (__num))
API_EXPORT int dr_getvi_impl(dr_t *dr, DR_DEBUG_VARS, int *i,
    unsigned off, unsigned num) HOT_ATTR;
#define	dr_setvi(__dr, __i, __off, __num) \
	dr_setvi_impl((__dr), DR_DEBUG(#__dr), (__i), (__off), (__num))
API_EXPORT void dr_setvi_impl(dr_t *dr, DR_DEBUG_VARS, int *i,
    unsigned off, unsigned num) HOT_ATTR;

#define	dr_getvf(__dr, __i, __off, __num) \
	dr_getvf_impl((__dr), DR_DEBUG(#__dr), (__i), (__off), (__num))
API_EXPORT int dr_getvf_impl(dr_t *dr, DR_DEBUG_VARS, double *df,
    unsigned off, unsigned num) HOT_ATTR;
#define	dr_setvf(__dr, __i, __off, __num) \
	dr_setvf_impl((__dr), DR_DEBUG(#__dr), (__i), (__off), (__num))
API_EXPORT void dr_setvf_impl(dr_t *dr, DR_DEBUG_VARS, double *df,
    unsigned off, unsigned num) HOT_ATTR;

#define	dr_getvf32(__dr, __i, __off, __num) \
	dr_getvf32_impl((__dr), DR_DEBUG(#__dr), (__i), (__off), (__num))
API_EXPORT int dr_getvf32_impl(dr_t *dr, DR_DEBUG_VARS, float *ff,
    unsigned off, unsigned num) HOT_ATTR;
#define	dr_setvf32(__dr, __i, __off, __num) \
	dr_setvf32_impl((__dr), DR_DEBUG(#__dr), (__i), (__off), (__num))
API_EXPORT void dr_setvf32_impl(dr_t *dr, DR_DEBUG_VARS, float *ff,
    unsigned off, unsigned num) HOT_ATTR;

#define	dr_gets(__dr, __str, __cap) \
	dr_gets_impl((__dr), DR_DEBUG(#__dr), (__str), (__cap))
API_EXPORT int dr_gets_impl(dr_t *dr, DR_DEBUG_VARS, char *str,
    size_t cap) HOT_ATTR;
#define	dr_sets(__dr, __str) \
	dr_sets_impl((__dr), DR_DEBUG(#__dr), (__str))
API_EXPORT void dr_sets_impl(dr_t *dr, DR_DEBUG_VARS, char *str) HOT_ATTR;

API_EXPORT void dr_create_i(dr_t *dr, int *value, bool_t writable,
    PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(4);
API_EXPORT void dr_create_f(dr_t *dr, float *value, bool_t writable,
    PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(4);
API_EXPORT void dr_create_f64(dr_t *dr, double *value, bool_t writable,
    PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(4);
API_EXPORT void dr_create_vi(dr_t *dr, int *value, size_t n, bool_t writable,
    PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(5);
API_EXPORT void dr_create_vf(dr_t *dr, float *value, size_t n, bool_t writable,
    PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(5);
API_EXPORT void dr_create_vf64(dr_t *dr, double *value, size_t n,
    bool_t writable, PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(5);
API_EXPORT void dr_create_b(dr_t *dr, void *value, size_t n, bool_t writable,
    PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(5);
API_EXPORT void dr_array_set_stride(dr_t *dr, size_t stride);
API_EXPORT void dr_delete(dr_t *dr);

API_EXPORT extern dr_t lacf_uninit_dataref;

#ifdef	__cplusplus
}
#endif

#endif	/* _DR_H_ */
