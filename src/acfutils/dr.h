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
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */
/**
 * \file
 * This file provides a facility for more conveniently interacting
 * with X-Plane's dataref system. Rather than having to write accessors
 * for every piece of data you wish to expose, this subsystem takes care
 * of most of the heavy lifting, while providing a much neater and
 * easier-to-use interface.
 *
 * If you want to expose your internal data as datarefs, you will
 * register the datarefs using the `dr_create_*` family of functions.
 * Please note that the returned `dr_t` object doesn't actually contain
 * the data. Instead, you pass the `dr_create_*` functions a pointer to
 * your data, and they simply reference it.
 *
 * To access foreign datarefs, use either dr_find() or fdr_find().
 * The returned `dr_t` object can then be passed to the various `dr_get*`
 * and `dr_set*` family of macros to access the data exposed through
 * those datarefs. The macros take care of automatically converting
 * types as appropriate, so you don't have to worry about always matching
 * the data type of the dataref. The only exception to these are the
 * dr_gets(), dr_sets(), dr_getbytes() and dr_setbytes() macros, which
 * must only ever be used with datarefs of type `xplmType_Data`.
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

typedef struct dr_s dr_t;

/**
 * This is the object representing a dataref. It is a convenience
 * wrapper and a pointer to it is the first argument to all `dr_*`
 * functions.
 */
struct dr_s {
	/** The name of the dataref. Must NOT be altered after creation. */
	char		name[DR_MAX_NAME_LEN];
	/** X-Plane dataref handle. Must NOT be altered after creation. */
	XPLMDataRef	dr;
	/** Type(s) of the dataref. Must NOT be altered after creation. */
	XPLMDataTypeID	type;
	/** Is the dataref writable? Must NOT be altered after creation. */
	bool_t		writable;
	/** Is it a 64-bit type? Must NOT be altered after creation. */
	bool_t		wide_type;
	/**
	 * For datarefs we expose, this points to the raw data, supplied
	 * in the `dr_create_*` call. For datarefs located using dr_find(),
	 * this field is ignored.
	 */
	void		*value;
	/**
	 * For array datarefs we expose, this denotes the number of elements
	 * in the array. For any other kind of dataref, this field is ignored.
	 * This shouldn't be manipulated from outside after creation.
	 */
	ssize_t		count;
	size_t		stride;
	/**
	 * For scalar datarefs we expose, setting this field to a callback
	 * will make that callback be called *after* a read attempt to the
	 * dataref is made. The dr machinery first reads the dataref from
	 * the memory location in `value` and then calls `read_cb` here
	 * (if not `NULL`), with a copy of the dataref value in `value_out`,
	 * allowing your callback to modify it before being returned to the
	 * initiator of the dataref read operation. The type of data in
	 * `value_out` will be the same as that which you used to create
	 * the dataref (e.g. if you created the dataref using `dr_create_i`,
	 * the value pointed to by the second argument will be an `int`).
	 * Please note that modifying the value* in the callback's second
	 * argument won't alter anything at the dataref's `value` location.
	 *
	 * IMPORTANT: you cannot use this function to substitute the
	 * preceding dataref `value` pointer read. You cannot pass `NULL`
	 * in `dr_create_*` for the memory location and expect to simply
	 * synthesize the value in this callback - this will result in a
	 * NULL pointer dereference and assertion failure. To synthesize
	 * dataref values on the fly, use the `read_scalar_cb`, and
	 * `read_array_cb` callbacks.
	 */
	void		(*read_cb)(dr_t *dr, void *value_out);
	/**
	 * Same as the `read_cb` callback, but called *before* a new value
	 * is written into the memory location referenced from the dataref.
	 * You can use this callback to modify the value to be written.
	 */
	void		(*write_cb)(dr_t *dr, void *value_in);
	/**
	 * If not NULL, this callback gets called *before* a scalar read
	 * is attempted from the dataref's memory location. The `value_out`
	 * argument will point to a new memory location to which you can
	 * write a new value to be returned to the originator of the
	 * dataref read. The size of the value must match the size of the
	 * type originally used to create the dataref (e.g. `int` if you
	 * created the dataref using `dr_create_i`).
	 *
	 * If you wish to synthesize a new value, return `B_TRUE` from
	 * this callback. This bypasses any attempt to read the dataref's
	 * memory location directly and instead just returns the value
	 * you've written to `value_out`. Otherwise, if you return
	 * `B_FALSE`, the dr machinery will read the memory pointed to
	 * by the `value` field in the dataref.
	 */
	bool_t		(*read_scalar_cb)(dr_t *dr, void *value_out);
	/**
	 * Same as read_scalar_cb, but instead is called before any attempt
	 * is made to write to the dataref's `value` memory location. The
	 * `value_in` argument points to the value being attempted to be
	 * written. If you return `B_TRUE` from this callback, the write
	 * is stopped and no actual dereference of the `value` pointer in
	 * this dataref is performed. Otherwise, the value being written
	 * is written as normal. You can still use this callback to alter
	 * the value being written though.
	 */
	bool_t		(*write_scalar_cb)(dr_t *dr, void *value_in);
	/**
	 * Same as `read_scalar_cb`, but for array datarefs. To take over
	 * the dataref read, return a value >= 0 from this callback. If you
	 * return a value <0, the normal array read functions get executed.
	 */
	int		(*read_array_cb)(dr_t *dr, void *values_out,
	    int offset, int count);
	/**
	 * Same as `write_scalar_cb`, but for array datarefs. If this
	 * callback is present, the write is aborted after it is called.
	 * There is no facility provided to "continue" the regular array
	 * dataref write path as normal.
	 */
	void		(*write_array_cb)(dr_t *dr, void *values_in,
	    int offset, int count);
	/**
	 * Optional user info field that you can set to whatever you want.
	 * Use it to stash any extra information you need in your
	 * callbacks and read it from the `dr` argument.
	 */
	void		*cb_userinfo;
};
/**
 * Attempts to find a dataref in the X-Plane dataref system and store
 * a handle to it in `dr'. The dataref must exist (have been created)
 * at the time of this call. There is no support for late (lazy)
 * resolving of datarefs.
 * @param dr Mandatory return-parameter, which will be filled with
 *	an initialized \ref dr_t object that can be used to read the
 *	dataref (and write to it, if the dataref is writable).
 * @param fmt A printf-style format string that will be used to construct
 *	the name of the dataref to looked up. The remainder of the variadic
 *	arguments must match the format string's format specifiers.
 * @return B_TRUE if the dataref was found, B_FALSE otherwise. If you
 *	want to hard-assert that the dataref must always exist, use
 *	the fdr_find() wrapper macro instead.
 */
API_EXPORT bool_t dr_find(dr_t *dr, PRINTF_FORMAT(const char *fmt), ...)
    PRINTF_ATTR(2);
/**
 * Same as dr_find(), but does a "forcible" lookup. That means, if the
 * dataref doesn't exist, instead of returning `B_FALSE`, this macro
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

/**
 * @return `B_TRUE` if the dataref is writable, `B_FALSE` if it is read-only.
 */
API_EXPORT bool_t dr_writable(dr_t *dr);

#define	DR_DEBUG(__varstr)	log_basename(__FILE__), __LINE__, __varstr
#define	DR_DEBUG_VARS	\
	const char *filename, int line, const char *varname
/**
 * Reads an integer dataref. If the dataref is not an integer, this function
 * automatically converts the types as necessary, so you can read a float
 * dataref as an int without having to worry about type conversions.
 *
 * This function can also work with array datarefs. If the dataref is an
 * array, this function simply reads the array's first element.
 *
 * @param __dr The dataref to be read. Must be a `dr_t *` obtained using
 *	dr_find().
 * @return The integer value of the dataref, converting between types
 *	as necessary.
 */
#define	dr_geti(__dr)		dr_geti_impl((__dr), DR_DEBUG(#__dr))
/** Implementation of the dr_geti() macro. Use dr_geti() instead. */
API_EXPORT int dr_geti_impl(const dr_t *dr, DR_DEBUG_VARS) HOT_ATTR;

/**
 * Writes an integer dataref. If the dataref is not an integer, this function
 * automatically converts the types as necessary, so you can write to a float
 * dataref as an int without having to worry about type conversions.
 *
 * This function can also work with array datarefs. If the dataref is an
 * array, theis function simply writes the array's first element.
 *
 * CAUTION: do NOT write to a read-only dataref. Attempting to do so will
 * trip an assertion failure.
 *
 * @param __dr The dataref to be written. Must be a `dr_t *` obtained using
 *	dr_find() and be writable (see dr_writable()).
 * @param __i The integer value to be written to the dataref.
 *
 * @see dr_writable()
 */
#define	dr_seti(__dr, __i)	dr_seti_impl((__dr), DR_DEBUG(#__dr), (__i))
/** Implementation of the dr_seti() macro. Use dr_seti() instead. */
API_EXPORT void dr_seti_impl(const dr_t *dr, DR_DEBUG_VARS, int i) HOT_ATTR;
/**
 * Same as dr_geti(), but for double floating point data.
 *
 * Please note that if the dataref contains a `NAN` value, this macro will
 * return it unaltered.
 */
#define	dr_getf(__dr)		dr_getf_impl((__dr), DR_DEBUG(#__dr))
/** Implementation of the dr_getf() macro. Use dr_getf() instead. */
API_EXPORT double dr_getf_impl(const dr_t *dr, DR_DEBUG_VARS) HOT_ATTR;
/**
 * Same as dr_seti(), but for double floating point data.
 *
 * Please note that attempting to write a `NAN` value using dr_setf()
 * will result in an assertion failure. This is a deliberate
 * check to avoid polluting X-Plane's dataref system with bad data.
 */
#define	dr_setf(__dr, __f)	dr_setf_impl((__dr), DR_DEBUG(#__dr), (__f))
/** Implementation of the dr_setf() macro. Use dr_setf() instead. */
API_EXPORT void dr_setf_impl(const dr_t *dr, DR_DEBUG_VARS, double f) HOT_ATTR;
/**
 * Extra error-checking version of dr_getf(). If the read value is
 * a `NAN`, this trips an assertion failure. This helps protect
 * critical sections of code from being fed unexpected garbage.
 */
#define	dr_getf_prot(__dr)	dr_getf_prot_impl((__dr), DR_DEBUG(#__dr))
/** Implementation of the dr_getf_prot() macro. Use dr_getf_prot() instead. */
HOT_ATTR static inline double
dr_getf_prot_impl(const dr_t *dr, DR_DEBUG_VARS)
{
	double x = dr_getf_impl(dr, filename, line, varname);
	ASSERT_MSG(!isnan(x) && isfinite(x), "%s:%d: Dataref %s (varname %s) "
	    "contains a garbage value (%f). We didn't write that, somebody "
	    "else did! Remove extraneous plugins and try to isolate the cause.",
	    filename, line, dr->name, varname, x);
	return (x);
}
/**
 * Reads a 32-bit integer array dataref. If the dataref is an array of floats,
 * this function automatically converts the float data to `int`s by truncation.
 * @param __dr The dataref to be read. Must be a `dr_t *` obtained using
 *	dr_find().
 * @param __i Pointer to an array of `int`s that's supposed to be filled
 *	with the read integer data. Must be large enough to hold all the
 *	data that was requested to be read.
 * @param __off Offset in the dataref to start the read at.
 *	(Caution: NOT an offset into the `__i` array above!)
 * @param __num Number of items in the dataref to read, starting at
 *	offset `__off`.
 * @return The number of elements actually read from the dataref. This can
 *	be less than `__num`, if the dataref was shorter than anticipated.
 * @return By convention in the X-Plane SDK, you can pass `NULL` for the
 * 	`__i` pointer and 0 for both `__off` and `__num` to obtain the
 *	actual length of the array.
 */
#define	dr_getvi(__dr, __i, __off, __num) \
	dr_getvi_impl((__dr), DR_DEBUG(#__dr), (__i), (__off), (__num))
/** Implementation of the dr_getvi() macro. Use dr_getvi() instead. */
API_EXPORT int dr_getvi_impl(const dr_t *dr, DR_DEBUG_VARS,
    int *i, unsigned off, unsigned num) HOT_ATTR;
/**
 * Writes a 32-bit integer array dataref. If the dataref is an array of
 * floats, this function automatically converts the integer data to
 * `float`s by casting.
 * @param __dr The dataref to be written. Must be a `dr_t *` obtained using
 *	dr_find() and be writable (see dr_writable()).
 * @param __i Pointer to an array of `int`s that's supposed to be written
 *	to the dataref. Must contain `__num` integers.
 * @param __off Offset in the dataref to start the write at.
 *	(Caution: NOT an offset into the `__i` array above!)
 * @param __num Number of items in `__i` to be written to the dataref
 *	at offset `__off`.
 * @return The number of elements actually written to the dataref. This can
 *	be less than `__num`, if the dataref was shorter than anticipated.
 */
#define	dr_setvi(__dr, __i, __off, __num) \
	dr_setvi_impl((__dr), DR_DEBUG(#__dr), (__i), (__off), (__num))
/** Implementation of the dr_setvi() macro. Use dr_setvi() instead. */
API_EXPORT void dr_setvi_impl(const dr_t *dr, DR_DEBUG_VARS, int *i,
    unsigned off, unsigned num) HOT_ATTR;
/**
 * Same as dr_getvi(), but expects a double-precision floating point array
 * in `__df` instead.
 */
#define	dr_getvf(__dr, __df, __off, __num) \
	dr_getvf_impl((__dr), DR_DEBUG(#__dr), (__df), (__off), (__num))
/** Implementation of the dr_getvf() macro. Use dr_getvf() instead. */
API_EXPORT int dr_getvf_impl(const dr_t *dr, DR_DEBUG_VARS, double *df,
    unsigned off, unsigned num) HOT_ATTR;
/**
 * Same as dr_setvi(), but expects a double-precision floating point array
 * in `__df` instead.
 */
#define	dr_setvf(__dr, __df, __off, __num) \
	dr_setvf_impl((__dr), DR_DEBUG(#__dr), (__df), (__off), (__num))
/** Implementation of the dr_setvf() macro. Use dr_setvf() instead. */
API_EXPORT void dr_setvf_impl(const dr_t *dr, DR_DEBUG_VARS, double *df,
    unsigned off, unsigned num) HOT_ATTR;
/**
 * Same as dr_getvi(), but expects a single-precision floating point array
 * in `__ff` instead.
 */
#define	dr_getvf32(__dr, __ff, __off, __num) \
	dr_getvf32_impl((__dr), DR_DEBUG(#__dr), (__ff), (__off), (__num))
/** Implementation of the dr_getvf32() macro. Use dr_getvf32() instead. */
API_EXPORT int dr_getvf32_impl(const dr_t *dr, DR_DEBUG_VARS, float *ff,
    unsigned off, unsigned num) HOT_ATTR;
/**
 * Same as dr_setvi(), but expects a single-precision floating point array
 * in `__ff` instead.
 */
#define	dr_setvf32(__dr, __ff, __off, __num) \
	dr_setvf32_impl((__dr), DR_DEBUG(#__dr), (__ff), (__off), (__num))
/** Implementation of the dr_setvf32() macro. Use dr_setvf32() instead. */
API_EXPORT void dr_setvf32_impl(const dr_t *dr, DR_DEBUG_VARS, float *ff,
    unsigned off, unsigned num) HOT_ATTR;
/**
 * Reads a string dataref. String datarefs are byte arrays that contain a
 * NUL-terminated value. This function cannot be invoked on anything other
 * than a dataref that is of type `xplmType_Data`. This function doesn't
 * support automatic type conversion from different dataref types.
 * @param __dr The dataref to be read. Must be a `dr_t *` obtained using
 *	dr_find() and be of type `xplmType_Data`.
 * @param __str A `char *` buffer to hold the string to be read. This must
 *	be allocated by the caller to have sufficient capacity to hold the
 *	returned string. If the buffer doesn't have sufficient capacity, the
 *	string is truncated and is guaranteed to always be NUL terminated.
 * @param __cap The capacity of the buffer in `__str` in bytes. The function
 *	will never write more than `__cap` bytes, including the terminating
 *	NUL character.
 * @return The number of bytes required to hold the returned string,
 *	including the terminating NUL byte.
 *
 * Example of how to read a string of unknown length:
 *```
 *	int len = dr_gets(dataref, NULL, 0);
 *	char *str = safe_malloc(len);
 *	dr_gets(dataref, str, len);
 *```
 */
#define	dr_gets(__dr, __str, __cap) \
	dr_gets_impl((__dr), DR_DEBUG(#__dr), (__str), (__cap))
/** Implementation of the dr_gets() macro. Use dr_gets() instead. */
API_EXPORT int dr_gets_impl(const dr_t *dr, DR_DEBUG_VARS, char *str,
    size_t cap) HOT_ATTR;
/**
 * Writes a string dataref. String datarefs are byte arrays that contain a
 * NUL-terminated value. This function cannot be invoked on anything other
 * than a dataref that is of type `xplmType_Data`. This function doesn't
 * support automatic type conversion from different dataref types.
 * @param __dr The dataref to be written. Must be a `dr_t *` obtained using
 *	dr_find(), be of type `xplmType_Data` and be writable
 *	(see dr_writable()).
 * @param __str A NUL-terminated `char *` string to be written. The length
 *	of the string is automatically obtained using strlen().
 */
#define	dr_sets(__dr, __str) \
	dr_sets_impl((__dr), DR_DEBUG(#__dr), (__str))
/** Implementation of the dr_sets() macro. Use dr_sets() instead. */
API_EXPORT void dr_sets_impl(const dr_t *dr, DR_DEBUG_VARS, char *str) HOT_ATTR;
/*
 * To read/write raw byte array datarefs, use dr_getbytes/dr_setbytes.
 * The arguments, return values and behavior are identical to XPLMGetDatab
 * and XPLMSetDatab.
 */

/**
 * Reads a byte array dataref. This function cannot be invoked on anything
 * other than a dataref that is of type `xplmType_Data`. This function
 * doesn't support automatic type conversion from different dataref types.
 * @param __dr The dataref to be read. Must be a `dr_t *` obtained using
 *	dr_find() and be of type `xplmType_Data`.
 * @param __data A `void *` buffer to hold the data to be read. This must
 *	be allocated by the caller to have sufficient capacity to hold at
 *	least `__num` bytes.
 * @param __off Byte offset in the dataref (NOT the `__data` buffer) from
 *	where the read should be performed.
 * @param __num Number of bytes to read in the dataref at offset `__off`.
 * @return The number of bytes actually read (which may be less than
 *	`__num`). By convention in the X-Plane SDK, if you pass `NULL`
 *	for the `__data` pointer and 0 for both `__off` and `__num`, this
 *	will make the dr_getbytes() macro return the actual number of
 *	bytes available to be read in the dataref.
 *
 * Example of how to read the entire contents of a dataref of unknown length:
 *```
 *	int len = dr_getbytes(dataref, NULL, 0, 0);
 *	uint8_t *buf = safe_malloc(len);
 *	dr_getbytes(dataref, buf, 0, len);
 *```
 */
#define	dr_getbytes(__dr, __data, __off, __num) \
	dr_getbytes_impl((__dr), DR_DEBUG(#__dr), (__data), (__off), (__num))
/** Implementation of the dr_getbytes() macro. Use dr_getbytes() instead. */
API_EXPORT int dr_getbytes_impl(const dr_t *dr, DR_DEBUG_VARS, void *data,
    unsigned off, unsigned num) HOT_ATTR;
/**
 * Writes a byte array dataref. This function cannot be invoked on anything
 * other than a dataref that is of type `xplmType_Data`. This function
 * doesn't support automatic type conversion from different dataref types.
 * @param __dr The dataref to be written. Must be a `dr_t *` obtained using
 *	dr_find(), be of type `xplmType_Data` and be writable
 *	(see dr_writable()).
 * @param __data A byte buffer containing the data to be written and must
 *	contain at least `__num` bytes.
 * @param __off The byte offset into the dataref (NOT the `__data` buffer)
 *	where the data buffer should start to be written.
 * @param __num Number of bytes to be written in `__data`.
 */
#define	dr_setbytes(__dr, __data, __off, __num) \
	dr_setbytes_impl((__dr), DR_DEBUG(#__dr), (__data), (__off), (__num))
/** Implementation of the dr_setbytes() macro. Use dr_setbytes() instead. */
API_EXPORT void dr_setbytes_impl(const dr_t *dr, DR_DEBUG_VARS, void *data,
    unsigned off, unsigned num) HOT_ATTR;

/**
 * Creates an integer dataref. The data is held by the caller, and the
 * dataref simply receives a reference to the data to be exposed. The
 * dr.h machinery takes care of registering all the appropriate data
 * accessors, as well as handling type conversions as appropriate.
 * After initialization, may want to further customize the \ref dr_t
 * reference by installing a read or write callback. See \ref dr_t.
 *
 * This function automatically registers the created dataref with
 * DataRefEditor (and DataRefTool, or any 3rd party dataref monitoring
 * plugin, which responds to dataref registration interplugin messages).
 * Thus you do not need to perform the registration yourself.
 *
 * @param dr A pointer to an unused \ref dr_t structure, which will be
 *	initialized to contain the information of the dataref.
 * @param value A pointer to the integer value to be read (and possibly
 *	written). If you plan on installing your own `read_scalar_cb`
 *	and/or `write_scalar_cb` callbacks, you may pass `NULL` here and
 *	manipulate the data entirely dynamically. However, for the vast
 *	majority of datarefs, a valid pointer here must be provided and
 *	the memory this points to must remain allocated and available
 *	throghout the entire existence of the dataref.
 * @param writable Flag designating the dataref registration as being
 *	either writable or read-only.
 * @param fmt A printf-style format string, from which the name of the
 *	dataref will be constructed. The remaining extra variadic
 *	arguments are used for this printf-style string construction.
 */
API_EXPORT void dr_create_i(dr_t *dr, int *value, bool_t writable,
    PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(4);
/**
 * Same as dr_create_i(), but creates a single-precision floating point
 * dataref and takes a `float *` data pointer argument.
 */
API_EXPORT void dr_create_f(dr_t *dr, float *value, bool_t writable,
    PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(4);
/**
 * Same as dr_create_i(), but creates a double-precision floating point
 * dataref and takes a `double *` data pointer argument.
 */
API_EXPORT void dr_create_f64(dr_t *dr, double *value, bool_t writable,
    PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(4);
/**
 * Creates an integer array ("vi" = vector of int's) dataref. In general
 * this behaves the same as dr_create_i(), except if you wanted to
 * dynamically override the data being read/written through this dataref,
 * you would set the `read_array_cb` and `write_array_cb` callback fields.
 *
 * @param dr A pointer to an unused \ref dr_t structure, which will be
 *	initialized to contain the information of the dataref.
 * @param value A pointer to the array of integer values to be read (and
 *	possibly written). If you plan on installing your own `read_array_cb`
 *	and/or `write_array_cb` callbacks, you may pass `NULL` here and
 *	manipulate the data entirely dynamically. However, for the vast
 *	majority of datarefs, a valid pointer here must be provided and
 *	the memory this points to must remain allocated and available
 *	throghout the entire existence of the dataref.
 * @param n The number of elements in the `value` array above.
 * @param writable Flag designating the dataref registration as being
 *	either writable or read-only.
 * @param fmt A printf-style format string, from which the name of the
 *	dataref will be constructed. The remaining extra variadic
 *	arguments are used for this printf-style string construction.
 */
API_EXPORT void dr_create_vi(dr_t *dr, int *value, size_t n, bool_t writable,
    PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(5);
/**
 * Same as dr_create_vi(), but creates a single-precision floating point
 * array dataref and takes a `float *` array pointer argument.
 */
API_EXPORT void dr_create_vf(dr_t *dr, float *value, size_t n, bool_t writable,
    PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(5);
/**
 * Same as dr_create_vi(), but creates a double-precision floating point
 * array dataref and takes a `double *` array pointer argument.
 *
 * Please note that X-Plane's SDK doesn't natively support a double-precision
 * floating point array dataref. Instead, the dataref will be registered
 * as a single-precision floating point array and type conversions will
 * be performed automatically on read and write. The `read_array_cb` and
 * `write_array_cb` will still be treated as dealing with double-precision
 * floating point values, but the values will be type-cast right at the
 * "edge" of the interface to X-Plane.
 */
API_EXPORT void dr_create_vf64(dr_t *dr, double *value, size_t n,
    bool_t writable, PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(5);
/**
 * Similar to dr_create_vi(), but creates a combo dataref that holds both
 * a single scalar integer value, as well as an array of integers. Internally
 * the dataref is registered as a bitwise-OR of both `xplmType_Int` and
 * `xplmType_IntArray`, The dataref has read (and write, if `writable` is
 * set to `B_TRUE`) handlers implemented for both scalar and array data
 * manipulation. You may also install your own custom callback handlers
 * for `read_scalar_cb`, `write_scalar_cb`, `read_array_cb` and
 * `write_array_cb`.
 *
 * This type of dataref is useful for cases where you want to provide both
 * the ability to treat the data as an array, while still providing
 * meaningful results when the dataref is accessed as a scalar (certain
 * lighting control data accesses in X-Plane require this).
 */
API_EXPORT void dr_create_vi_autoscalar(dr_t *dr, int *value, size_t n,
    bool_t writable, PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(5);
/**
 * Same as dr_create_vi_autoscalar(), but creates a single-precision
 * floating point combo scalar/array dataref and takes a `float *` array
 * pointer argument.
 */
API_EXPORT void dr_create_vf_autoscalar(dr_t *dr, float *value, size_t n,
    bool_t writable, PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(5);
/**
 * Same as dr_create_vi_autoscalar(), but creates a double-precision
 * floating point combo scalar/array dataref and takes a `double *` array
 * pointer argument. Same type conversion caveats apply as in the case
 * of dr_create_vf64(), when the dataref is being accessed as an array.
 */
API_EXPORT void dr_create_vf64_autoscalar(dr_t *dr, double *value, size_t n,
    bool_t writable, PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(5);
/**
 * Same as dr_create_vi(), but creates an arbitrary byte array dataref
 * and takes an anonymous `void *` buffer pointer argument.
 */
API_EXPORT void dr_create_b(dr_t *dr, void *value, size_t n, bool_t writable,
    PRINTF_FORMAT(const char *fmt), ...) PRINTF_ATTR(5);
/**
 * For array datarefs you create, you can specify a data access stride
 * value. if set to non-zero after creation, the built-in array read
 * functions of the `dr.h` machinery will use the stride calculate the
 * actual offset in the dataref's data buffer for elements past the [0]
 * index. If set to 0 (the default), the stride is automatically
 * determined from the type of the dataref. You can use this to expose
 * array data that is strided in a bigger struct, for example as follows:
 *```
 *	struct my_struct {
 *		int   foo;
 *		float bar;
 *	};
 *	struct my_struct baz[8];
 *
 *	dr_t baz_dr;
 *	dr_create_vi(&baz_dr, &baz[0].foo, ARRAY_NUM_ELEM(baz),
 *	    B_FALSE, "my/dataref/name");
 *	dr_array_set_stride(&baz_dr, sizeof (struct my_struct));
 *```
 */
API_EXPORT void dr_array_set_stride(dr_t *dr, size_t stride);
/**
 * Destroys a dataref previously created using one of the `dr_create_*`
 * functions. You MUST destroy every dataref you create prior to your
 * plugin being unloaded.
 *
 * libacfutils ships with automated macros that help automate the dataref
 * destruction process on plugin unload. See \ref dr_cmd_reg.h for more
 * information.
 */
API_EXPORT void dr_delete(dr_t *dr);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACFUTILS_DR_H_ */
