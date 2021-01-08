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
 * Copyright 2020 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACFUTILS_DR_H_
#define	_ACFUTILS_DR_H_

#include <stdlib.h>
#include <string.h>

#include <XPLMDataAccess.h>

#include "helpers.h"
#include "log.h"
#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	DR_MAX_NAME_LEN	128

/*
 * This is the object representing a dataref. It is a convenience
 * wrapper and a pointer to it is the first argument to all dr_*
 * functions.
 */
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
/*
 * Attempts to find a dataref in the X-Plane dataref system and store
 * a handle to it in `dr'. The dataref must exist (have been created)
 * at the time of this call. There is no support for late (lazy)
 * resolving of datarefs.
 * Returns B_TRUE if the dataref was found, B_FALSE otherwise.
 */
API_EXPORT bool_t dr_find(dr_t *dr, PRINTF_FORMAT(const char *fmt), ...)
    PRINTF_ATTR(2);
/*
 * Same as `dr_find', but does a "forcible" lookup. That means, if
 * the dataref doesn't exist, instead of returning B_FALSE, this macro
 * causes a hard assertion failure. Use this to look up datarefs that
 * you absolutely require to exist (such as those that are part of
 * X-Plane itself).
 */
#define	fdr_find(dr, ...) \
	do { \
		if (!dr_find(dr, __VA_ARGS__)) { \
			char drname[DR_MAX_NAME_LEN]; \
			snprintf(drname, sizeof (drname), __VA_ARGS__); \
			VERIFY_MSG(0, "dataref \"%s\" not found", drname); \
		} \
	} while (0)

/*
 * Returns B_TRUE if the dataref is writable, B_FALSE if it is read-only.
 */
API_EXPORT bool_t dr_writable(dr_t *dr);

#define	DR_DEBUG(__varstr)	log_basename(__FILE__), __LINE__, __varstr
#define	DR_DEBUG_VARS	\
	const char *filename, int line, const char *varname
/*
 * Reads and writes an integer dataref. If the dataref is not an integer,
 * these functions automatically convert the types as necessary, so you
 * can read a float dataref as an int without having to worry about
 * dataref types. These functions can also work with array datarefs. If
 * the dataref is an array, these functions simply read/write the array's
 * first element.
 * Caution: do NOT write to a read-only dataref. Attempting to do so will
 * trip an assertion failure.
 */
#define	dr_geti(__dr)		dr_geti_impl((__dr), DR_DEBUG(#__dr))
API_EXPORT int dr_geti_impl(const dr_t *dr, DR_DEBUG_VARS) HOT_ATTR;
#define	dr_seti(__dr, __i)	dr_seti_impl((__dr), DR_DEBUG(#__dr), (__i))
API_EXPORT void dr_seti_impl(const dr_t *dr, DR_DEBUG_VARS, int i) HOT_ATTR;
/*
 * Same as dr_geti/dr_seti, but for double floating point data.
 * Please note that attempting to write a NAN value using dr_setf
 * will result in an assertion failure. This is a deliberate
 * check to avoid polluting X-Plane's dataref system with bad data.
 * When reading, however, dr_getf, will pass through a NAN value.
 */
#define	dr_getf(__dr)		dr_getf_impl((__dr), DR_DEBUG(#__dr))
API_EXPORT double dr_getf_impl(const dr_t *dr, DR_DEBUG_VARS) HOT_ATTR;
#define	dr_setf(__dr, __f)	dr_setf_impl((__dr), DR_DEBUG(#__dr), (__f))
API_EXPORT void dr_setf_impl(const dr_t *dr, DR_DEBUG_VARS, double f) HOT_ATTR;
/*
 * Extra error-checking version of dr_getf. If the read value is
 * a NAN, this trips an assertion failure. This helps protect
 * critical sections of code from being fed unexpected garbage.
 */
#define	dr_getf_prot(__dr)	dr_getf_prot_impl((__dr), DR_DEBUG(#__dr))
static inline double dr_getf_prot_impl(const dr_t *dr, DR_DEBUG_VARS) HOT_ATTR;
static inline double
dr_getf_prot_impl(const dr_t *dr, DR_DEBUG_VARS)
{
	double x = dr_getf_impl(dr, filename, line, varname);
	ASSERT_MSG(!isnan(x), "%s:%d: Dataref %s (varname %s) contains a "
	    "garbage (NAN) value. We didn't write that, somebody else did! "
	    "Remove extraneous plugins and try to isolate the cause.",
	    filename, line, dr->name, varname);
	return (x);
}
/*
 * Reads and writes a 32-bit integer array dataref. The `i' argument to
 * the macros is the integer array. `off' and `num' represents offset
 * and number of elements in the array to be read or written.
 * The dr_getvi macro returns the number of elements actually read as the
 * function return value. By convention in the X-Plane SDK, you can pass
 * NULL for the array pointer and 0 for both the length and number to
 * obtain the actual length of the array.
 */
#define	dr_getvi(__dr, __i, __off, __num) \
	dr_getvi_impl((__dr), DR_DEBUG(#__dr), (__i), (__off), (__num))
API_EXPORT int dr_getvi_impl(const dr_t *dr, DR_DEBUG_VARS,
    int *i, unsigned off, unsigned num) HOT_ATTR;
#define	dr_setvi(__dr, __i, __off, __num) \
	dr_setvi_impl((__dr), DR_DEBUG(#__dr), (__i), (__off), (__num))
API_EXPORT void dr_setvi_impl(const dr_t *dr, DR_DEBUG_VARS, int *i,
    unsigned off, unsigned num) HOT_ATTR;
/*
 * Reads and writes a 64-bit float array dataref. The off and num
 * arguments are identical to dr_getvi/dr_setvi.
 */
#define	dr_getvf(__dr, __df, __off, __num) \
	dr_getvf_impl((__dr), DR_DEBUG(#__dr), (__df), (__off), (__num))
API_EXPORT int dr_getvf_impl(const dr_t *dr, DR_DEBUG_VARS, double *df,
    unsigned off, unsigned num) HOT_ATTR;
#define	dr_setvf(__dr, __df, __off, __num) \
	dr_setvf_impl((__dr), DR_DEBUG(#__dr), (__df), (__off), (__num))
API_EXPORT void dr_setvf_impl(const dr_t *dr, DR_DEBUG_VARS, double *df,
    unsigned off, unsigned num) HOT_ATTR;
/*
 * Reads and writes a 32-bit float array dataref. The off and num
 * arguments are identical to dr_getvi/dr_setvi.
 */
#define	dr_getvf32(__dr, __ff, __off, __num) \
	dr_getvf32_impl((__dr), DR_DEBUG(#__dr), (__ff), (__off), (__num))
API_EXPORT int dr_getvf32_impl(const dr_t *dr, DR_DEBUG_VARS, float *ff,
    unsigned off, unsigned num) HOT_ATTR;
#define	dr_setvf32(__dr, __ff, __off, __num) \
	dr_setvf32_impl((__dr), DR_DEBUG(#__dr), (__ff), (__off), (__num))
API_EXPORT void dr_setvf32_impl(const dr_t *dr, DR_DEBUG_VARS, float *ff,
    unsigned off, unsigned num) HOT_ATTR;
/*
 * Reads and writes a string dataref. String datarefs are byte arrays
 * that contain a NUL-terminated value.
 * The dr_gets macro takes the string and its maximum capacity as an
 * argument. The returned string is always NUL-terminated.
 * The dr_sets macro automatically determines the length of the string
 * using strlen.
 */
#define	dr_gets(__dr, __str, __cap) \
	dr_gets_impl((__dr), DR_DEBUG(#__dr), (__str), (__cap))
API_EXPORT int dr_gets_impl(const dr_t *dr, DR_DEBUG_VARS, char *str,
    size_t cap) HOT_ATTR;
#define	dr_sets(__dr, __str) \
	dr_sets_impl((__dr), DR_DEBUG(#__dr), (__str))
API_EXPORT void dr_sets_impl(const dr_t *dr, DR_DEBUG_VARS, char *str) HOT_ATTR;
/*
 * To read/write raw byte array datarefs, use dr_getbytes/dr_setbytes.
 * The arguments, return values and behavior are identical to XPLMGetDatab
 * and XPLMSetDatab.
 */
#define	dr_getbytes(__dr, __data, __off, __num) \
	dr_getbytes_impl((__dr), DR_DEBUG(#__dr), (__data), (__off), (__num))
API_EXPORT int dr_getbytes_impl(const dr_t *dr, DR_DEBUG_VARS, void *data,
    unsigned off, unsigned num) HOT_ATTR;
#define	dr_setbytes(__dr, __data, __off, __num) \
	dr_setbytes_impl((__dr), DR_DEBUG(#__dr), (__data), (__off), (__num))
API_EXPORT void dr_setbytes_impl(const dr_t *dr, DR_DEBUG_VARS, void *data,
    unsigned off, unsigned num) HOT_ATTR;

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
API_EXPORT void dr_create_vi_autoscalar(dr_t *dr, int *value, size_t n,
    bool_t writable, PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(5);
API_EXPORT void dr_create_vf_autoscalar(dr_t *dr, float *value, size_t n,
    bool_t writable, PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(5);
API_EXPORT void dr_create_vf64_autoscalar(dr_t *dr, double *value, size_t n,
    bool_t writable, PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(5);
API_EXPORT void dr_create_b(dr_t *dr, void *value, size_t n, bool_t writable,
    PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(5);
API_EXPORT void dr_array_set_stride(dr_t *dr, size_t stride);
API_EXPORT void dr_delete(dr_t *dr);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACFUTILS_DR_H_ */
