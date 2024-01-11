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
 *
 * # Optional Types
 *
 * This file provides a simplified Rust-like optional type machinery for
 * C11 code. In Rust, the `Option<T>` type allows expressing the concept
 * of a value being optionally present, with support from the type
 * checker. This helps avoid inadvertently using uninitialized or invalid
 * data without employing proper checks.
 *
 * ## Principles of Operation
 *
 * An optional type is a structure which wraps an underlying type and
 * enforces explicit checks for value presence at compile time. Rather
 * than relying on checking the value of a variable at runtime for
 * validity (e.g. checking a pointer for `NULL`), the validity check is
 * enforced by preventing access to the wrapped value without first
 * performing a validity-checking unwrapping operation. We refer to the
 * state of the value being valid as the "SOME" state, whereas the
 * invalid state is called "NONE."
 *
 * For example, to wrap an `int32_t` as an optional type, we declare a
 * structure of type `opt_int32_t` and store both the integer value as
 * well as the validity state in the structure (don't worry, you don't
 * need declare this structure yourself, `optional.h` provides lots of
 * canned types, as well as simple construction macros for your own
 * types, see below). Users of the optional type are then forced to
 * perform one of the several available unwrapping operations on the
 * optional, which perform the validity check.
 *
 * ## Constructing Optional Values
 *
 * Use one of the following two macros to construct new optional values:
 *
 * 1. `SOME(T expr) -> opt_T`: returns an optional value of type `opt_T`
 *	in the SOME state from a provided expression argument. Please note
 *	that the optional type may perform integrity checks on the value,
 *	to make sure that it really conforms to the SOME specification
 *	on the type (what constitutes a valid SOME value depends on the
 *	type, see below).
 * 2. `NONE(T) -> opt_T`: returns an optional value of type `opt_T` in
 *	the NONE state. The argument to this macro is the type name of
 *	the optional (the part after the `opt_` prefix of the optional
 *	type, not necessarily the contained C type).
 *
 * ## Manipulating Optional Values
 *
 * To manipulate an optional value, use one of the following macros:
 *
 * 1. `IS_SOME(opt_T opt) -> bool`: returns true if the optional `opt`
 *	is in the SOME state.
 * 2. `IS_NONE(opt_T opt) -> bool`: returns true if the optional `opt`
 *	is in the NONE state.
 * 3. `MATCH(opt_T opt, T &out_value) -> optional_state_t`: this
 *	operation checks the state of the optional `opt`. If it is in
 *	the SOME state, the wrapped value is written to `&out_value`
 *	and the entire `MATCH` operation returns the `OPT_SOME`
 *	enumeration. If the optional is in the NONE state, the
 *	`&out_value` reference is filled with an invalid value and the
 *	`MATCH` operation returns `OPT_NONE`.
 * 4. `UNWRAP(opt_T opt) -> T`: this operation returns the wrapped
 *	value contained within the optional type, if the optional is
 *	in the SOME state. If the state is NONE, then attempting this
 *	operation causes a runtime assertion failure and panic.
 *	`UNWRAP()` should only be used for testing, or for cases where
 *	another check has already been performed, guaranteeing that
 *	the value is the SOME state.
 * 5. `UNWRAP_OR(opt_T opt, T dfl_value) -> T`: if the optional `opt`
 *	is in the SOME state, this operation returns the wrapped value
 *	of type `T`. If the optional value is in the NONE state, this
 *	operation returns `dfl_value` instead.
 * 6. `UNWRAP_OR_ELSE(opt_T opt, T (*func), void *arg) -> T`: if the
 *	optional `opt` is in the SOME state, returns the wrapped value
 *	of type `T`. If the optional value is in the NONE state, lazily
 *	calls `func` with `arg` and returns its return value.
 * 7. `OPT_OR(opt_T a, opt_T b) -> opt_T`: if the state of the optional
 *	`a` is SOME, returns `a`. Otherwise, returns `b`.
 * 8. `OPT_OR_ELSE(opt_T a, opt_T (*func), void *arg) -> opt_T`: if
 *	the state of `a` is SOME, returns `a`. Otherwise, lazily calls
 *	`func` with `arg` and returns its return value.
 *
 * ## Interoperating with C++ Code
 *
 * While the `optional.h` functionality is primarily designed for C11
 * code, optional values can be both manipulated as well as constructed
 * from C++ code. This bridge is primarily provided for compatibility
 * in heterogeneous codebases.
 *
 * C++ doesn't have the same kind of type-matching mechanism as C11's
 * `_Generic` statement has, so when called from C++ code, the above
 * macros need an explicit type name argument. For example, for a
 * optional value of type `opt_float`, you must modify the macro
 * invocations from C++ code as follows:
 *
 * 1. `SOME(x)` becomes `SOME(float, x)`
 * 2. `MATCH(opt, out_val)` becomes `MATCH(float, opt, out_val)`
 * 3. (etc.)
 *
 * ## Available Built-In Optional Types
 *
 * This list summarizes all automatically provided optional types
 * directly from libacfutils. See the next section on how to add extra
 * custom optional types for your own needs.
 *
 * 1. Most sized standard C integer types: `opt_int8_t`, `opt_uint8_t`,
 *	etc. through to `opt_uint64_t`. The type `opt_size_t` is
 *	a type alias, which can be used for wrapping a `size_t`.
 * 2. Single-precision and double-precision floating point types:
 *	`opt_float` and `opt_double`. These types are explicitly
 *	disallow `NAN` values in the SOME state.
 * 3. Vector types from `geom.h`: `opt_vect2_t`, `opt_vect3_t` and
 *	`opt_vect3l_t`. These types explicitly disallow the matching
 *	`NULL_VECT*` values in the SOME state.
 * 4. Geographic coordinate types from `geom.h`: `opt_geo_pos2_t`,
 *	`opt_geo_pos3_t`, `opt_geo_pos2_32_t` and `opt_geo_pos3_32_t`.
 *	These types explicitly disallow the matching `NULL_GEO_POS*`
 *	values in the SOME state.
 * 5. C string types `opt_str` (containing a `char *`) and
 *	`opt_str_const` (containing a `char const *`). These types
 *	explicitly disallow passing a `NULL` pointer in the SOME state.
 *
 * \anchor custom_opt_types
 * ## Adding Your Own Custom Optional Types
 *
 * Due to the preprocessor-driven nature of the `optional.h` machinery,
 * adding custom optional types is a bit tricky, but possible. To do
 * so, you must first create your header file, which will
 * `#include <acfutils/optional.h>` at the start. It should then
 * include any headers for declarations of the types you wish to wrap
 * into new optional types. Any code that wants to use these new
 * optional types must then include this custom header, to allow for
 * properly selecting the implementation using the standard `SOME`,
 * `MATCH` and similar macros. Example header template so far:
 * ```
 * #ifndef _my_optional_h_
 * #define _my_optional_h_
 *
 * #include <acfutils/optional.h>
 *
 * #include "my_type_1.h"
 * #include "my_type_2.h"
 *
 * #endif  // _my_optional_h_
 * ```
 * With the headers included, we can proceed to implement each optional
 * type using the IMPL_OPTIONAL_IMPLICIT() or IMPL_OPTIONAL_EXPLICIT()
 * macros. See each macro for details on what they do.
 * ```
 * #ifndef _my_optional_h_
 * #define _my_optional_h_
 *
 * #include <acfutils/optional.h>
 *
 * #include "my_type_1.h"
 * #include "my_type_2.h"
 *
 * IMPL_OPTIONAL_EXPLICIT(my_type_1, my_type_1, my_1_none_value)
 * IMPL_OPTIONAL_IMPLICIT(my_type_2, my_type_2, my_2_none_value,
 *     check_type_2_is_none(x))
 *
 * #endif  // _my_optional_h_
 * ```
 * We have now constructed all of the necessary typedefs and inline
 * functions to manipulate each optional type. Lastly, we need to
 * link these new types into the master auto-selection macro inside
 * of `optional.h`. We do this by simply redefining the
 * `OPTIONAL_TYPE_LIST` macro, filling it with our types:
 * ```
 * #ifndef _my_optional_h_
 * #define _my_optional_h_
 *
 * #include <acfutils/optional.h>
 *
 * #include "my_type_1.h"
 * #include "my_type_2.h"
 *
 * IMPL_OPTIONAL_EXPLICIT(my_type_1, my_type_1, my_1_none_value)
 * IMPL_OPTIONAL_IMPLICIT(my_type_2, my_type_2, my_2_none_value,
 *     check_type_2_is_none(x))
 *
 * #undef OPTIONAL_TYPE_LIST
 * #define OPTIONAL_TYPE_LIST(op_name) \
 *     OPTIONAL_TYPE_LIST_ADD(my_type_1, my_type_1, op_name), \
 *     OPTIONAL_TYPE_LIST_ADD(my_type_2, my_type_2, op_name)
 *
 * #endif  // _my_optional_h_
 * ```
 * Please note that the OPTIONAL_TYPE_LIST() macro must be defined
 * to take an `op_name` argument and must pass it through to the
 * OPTIONAL_TYPE_LIST_ADD() macro invocations within. This is
 * required to correctly construct the invoked function names when
 * an optional operation macro is instantiated.
 *
 * We have now extended the optional list with our new custom
 * optional types, `opt_my_type_1` and `opt_my_type_2` (containing
 * values of type `my_type_1` and `my_type_2` respectively). All
 * the standard optional types from libacfutils remain available
 * as well.
 *
 * There are some important caveats to consider when adding custom
 * optional types:
 *
 * - You can only add optional types for base C types which the
 *	compiler can distinguish during type determination using
 *	the `_Generic` operator. This means you cannot add another
 *	optional type for something like `int32_t`, which is
 *	already in the library, as the compiler couldn't decide
 *	which implementation to call. If you want you can however
 *	add a type alias to an existing known optional type using
 *	the IMPL_OPTIONAL_ALIAS() macro. This is how `opt_size_t`
 *	is implemented (it is an alias for `opt_uint64_t`).
 * - You must only define the `OPTIONAL_TYPE_LIST` macro once.
 *	You cannot spread its definition over multiple files, each
 *	adding its own optional types. Only the last definition of
 *	the macro would actually get used.
 * - While it is technically possible to declare an optional void
 *	pointer type, doing so is strongly discouraged, as that
 *	bypasses the compiler's type checks. You should almost
 *	always define an as narrowly scoped type-specific optional
 *	pointer as possible.
 */

#ifndef	_ACF_UTILS_OPTIONAL_H_
#define	_ACF_UTILS_OPTIONAL_H_

#if	defined(__STDC_VERSION__) && !(__STDC_VERSION__ >= 201112L)
#error	"Including <optional.h> from C code requires support for C11 or higher"
#endif

#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "assert.h"
#include "log.h"
#include "geom.h"
#include "sysmacros.h"

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Describes the state of an optional type. This enumeration is returned
 * by the MATCH() macro.
 */
typedef enum {
    OPT_NONE,	///< State denoting that the optional contains no valid value
    OPT_SOME,	///< State denoting that the optional contains a valid value
} optional_state_t;

/**
 * \brief Declares a custom optional type with explicit NONE encoding.
 *
 * This macro constructs all the necessary typedefs and inline
 * functions for your custom optional type, allowing it to be
 * linked into the optional type selection logic in macros such as
 * `SOME(x)` or `MATCH(x, out)`. The "explicit" nature of this
 * optional means that the SOME/NONE state of the value cannot be
 * inferred and checked from the value itself, but rather must be
 * stored directly in the optional state. This inflates the stored
 * type by an extra 32 bits on most platforms, because the value of
 * the `optional_state_t` enum must be embedded. If your value
 * supports implicit SOME/NONE distinction (such as a floating
 * point number with `NAN`, or a pointer being `NULL`), then you
 * can use the IMPL_OPTIONAL_IMPLICIT() macro instead. This
 * provides additional integrity checking, to make sure the caller
 * doesn't attempt to embed an invalid value when constructing the
 * optional type using the SOME() macro.
 *
 * @note Don't forget to also add the type to your OPTIONAL_TYPE_LIST().
 *
 * @param type_name The type name of the optional type. This will
 *	be the name which will appear after the `opt_` prefix. For
 *	example, passing `foobar` here will result in an optional
 *	type named `opt_foobar`. This string must constitute a
 *	well-formed C identifier, as it is used as part of the
 *	typedef name, as well as manipulation function names.
 * @param c_type The actual C type being wrapped by the optional
 *	type. This can be any valid C type.
 */
#define IMPL_OPTIONAL_EXPLICIT(type_name, c_type) \
	typedef struct { \
		optional_state_t	state; \
		c_type			value; \
	} opt_ ## type_name; \
	static inline opt_ ## type_name \
	opt_some_ ## type_name(c_type x) { \
		opt_ ## type_name value = { \
		    .state = OPT_SOME, \
		    .value = x \
		}; \
		return (value); \
	} \
	static inline opt_ ## type_name \
	opt_none_ ## type_name() { \
		opt_ ## type_name value = { \
		    .state = OPT_NONE, \
		}; \
		return (value); \
	} \
	static inline bool \
	opt_is_some_ ## type_name(opt_ ## type_name opt) { \
		return (opt.state == OPT_SOME); \
	} \
	static inline optional_state_t \
	opt_match_ ## type_name(opt_ ## type_name opt, \
	    c_type REQ_PTR(out_value)) { \
		switch (opt.state) { \
		case OPT_SOME: \
			*out_value = opt.value; \
			return (OPT_SOME); \
		case OPT_NONE: \
			memset(out_value, 0, sizeof (*out_value)); \
			return (OPT_NONE); \
		} \
		VERIFY_FAIL(); \
	} \
	static inline optional_state_t \
	opt_match_as_ref_ ## type_name(opt_ ## type_name const REQ_PTR(opt), \
	    c_type const *REQ_PTR(out_value)) { \
		switch (opt->state) { \
		case OPT_SOME: \
			*out_value = &opt->value; \
			return (OPT_SOME); \
		case OPT_NONE: \
			*out_value = NULL; \
			return (OPT_NONE); \
		} \
		VERIFY_FAIL(); \
	} \
	static inline optional_state_t \
	opt_match_as_mut_ ## type_name(opt_ ## type_name REQ_PTR(opt), \
	    c_type *REQ_PTR(out_value)) { \
		switch (opt->state) { \
		case OPT_SOME: \
			*out_value = &opt->value; \
			return (OPT_SOME); \
		case OPT_NONE: \
			*out_value = NULL; \
			return (OPT_NONE); \
		} \
		VERIFY_FAIL(); \
	} \
	static inline c_type \
	opt_unwrap_ ## type_name(opt_ ## type_name opt, const char *filename, \
	    int line, const char *expr) { \
		switch (opt.state) { \
		case OPT_SOME: \
			return (opt.value); \
		case OPT_NONE: \
			logMsg("%s:%d: Attempted to unwrap None value in %s", \
			    filename, line, expr); \
			VERIFY_FAIL(); \
		} \
		VERIFY_FAIL(); \
	} \
	static inline c_type const * \
	opt_unwrap_as_ref_ ## type_name(opt_ ## type_name const REQ_PTR(opt), \
	    const char *filename, int line, const char *expr) { \
		switch (opt->state) { \
		case OPT_SOME: \
			return (&opt->value); \
		case OPT_NONE: \
			logMsg("%s:%d: Attempted to unwrap None value in %s", \
			    filename, line, expr); \
			VERIFY_FAIL(); \
		} \
		VERIFY_FAIL(); \
	} \
	static inline c_type * \
	opt_unwrap_as_mut_ ## type_name(opt_ ## type_name REQ_PTR(opt), \
	    const char *filename, int line, const char *expr) { \
		switch (opt->state) { \
		case OPT_SOME: \
			return (&opt->value); \
		case OPT_NONE: \
			logMsg("%s:%d: Attempted to unwrap None value in %s", \
			    filename, line, expr); \
			VERIFY_FAIL(); \
		} \
		VERIFY_FAIL(); \
	} \
	static inline c_type \
	opt_unwrap_or_ ## type_name(opt_ ## type_name opt, c_type dfl_value) { \
		switch (opt.state) { \
		case OPT_SOME: \
			return (opt.value); \
		case OPT_NONE: \
			return (dfl_value); \
		} \
		VERIFY_FAIL(); \
	} \
	static inline c_type \
	opt_unwrap_or_else_ ## type_name(opt_ ## type_name opt, \
	    c_type (*dfl_func)(void *), void *dfl_arg) { \
		switch (opt.state) { \
		case OPT_SOME: \
			return (opt.value); \
		case OPT_NONE: \
			return (dfl_func(dfl_arg)); \
		} \
		VERIFY_FAIL(); \
	} \
	static inline opt_ ## type_name \
	opt_or_ ## type_name(opt_ ## type_name a, opt_ ## type_name b) { \
		switch (a.state) { \
		case OPT_SOME: \
			return (a); \
		case OPT_NONE: \
			return (b); \
		} \
		VERIFY_FAIL(); \
	} \
	static inline opt_ ## type_name \
	opt_or_else_ ## type_name(opt_ ## type_name a, \
	    opt_ ## type_name (*func_b)(void *), void *arg_b) { \
		switch (a.state) { \
		case OPT_SOME: \
			return (a); \
		case OPT_NONE: \
			return (func_b(arg_b)); \
		} \
		VERIFY_FAIL(); \
	} \
	/* \
	 * This function cannot exist for explicit optional types, but in \
	 * order for this to work for aliases to implicit optional types, \
	 * to make the same aliasing function work for all, we fake that \
	 * there is an explicit type equivalent somewhere. We don't actually \
	 * ever provide an implementation, so attempting to call this with \
	 * an explicit optional type is still safe and won't link, but it \
	 * will not throw a compile-time error due to a call to a \
	 * non-existent function, or a runtime error if we did have an \
	 * non-functional faux implementation. \
	 */ \
	opt_ ## type_name opt_into_ ## type_name(c_type);

/**
 * \brief Declares a custom optional type with implicit NONE encoding.
 *
 * This macro constructs all the necessary typedefs and inline
 * functions for your custom optional type, allowing it to be
 * linked into the optional type selection logic in macros such as
 * `SOME(x)` or `MATCH(x, out)`. The "implicit" nature of this
 * optional means that the SOME/NONE state of the value is encoded
 * directly in the value itself and can be checked at runtime. This
 * also saves a bit of storage space, since the `optional_state_t`
 * enum doesn't have to be embedded in the resultant optional type.
 * If the type you want to embed has no specific "NONE" encoded in
 * its representation (for example, integers do not have any
 * specific numerical value which could be considered per-se
 * "invalid"), use the IMPL_OPTIONAL_EXPLICIT() macro instead.
 *
 * @note Don't forget to also add the type to your OPTIONAL_TYPE_LIST().
 *
 * @param type_name The type name of the optional type. This will
 *	be the name which will appear after the `opt_` prefix. For
 *	example, passing `foobar` here will result in an optional
 *	type named `opt_foobar`. This string must constitute a
 *	well-formed C identifier, as it is used as part of the
 *	typedef name, as well as manipulation function names.
 * @param c_type The actual C type being wrapped by the optional
 *	type. This can be any valid C type.
 * @param none_value The actual value of the C type which denotes
 *	a NONE state.
 * @param none_check An expression which must return true if the
 *	value is NONE. The expression must be formed using a variable
 *	named "x", which holds the value to be tested. For example,
 *	for floating point numbers, this argument is `isnan(x)`.
 *	For pointers, a good none_check is `x == NULL`.
 */
#define IMPL_OPTIONAL_IMPLICIT(type_name, c_type, none_value, none_check) \
	typedef struct { \
		c_type			value; \
	} opt_ ## type_name; \
	static inline opt_ ## type_name \
	opt_some_ ## type_name(c_type x) { \
		ASSERT(!(none_check)); \
		opt_ ## type_name value = { \
		    .value = x \
		}; \
		return (value); \
	} \
	static inline opt_ ## type_name \
	opt_none_ ## type_name() { \
		opt_ ## type_name value = { \
		    .value = (c_type)none_value \
		}; \
		return (value); \
	} \
	static inline bool \
	opt_is_some_ ## type_name(opt_ ## type_name opt) { \
		c_type x = opt.value; \
		return (!(none_check)); \
	} \
	static inline optional_state_t \
	opt_match_ ## type_name(opt_ ## type_name opt, \
	    c_type REQ_PTR(out_value)) { \
		c_type x = opt.value; \
		if (!(none_check)) { \
			*out_value = x; \
			return (OPT_SOME); \
		} else { \
			*out_value = (c_type)none_value; \
			return (OPT_NONE); \
		} \
	} \
	static inline optional_state_t \
	opt_match_as_ref_ ## type_name(opt_ ## type_name const REQ_PTR(opt), \
	    c_type const *REQ_PTR(out_value)) { \
		c_type x = opt->value; \
		if (!(none_check)) { \
			*out_value = &opt->value; \
			return (OPT_SOME); \
		} else { \
			*out_value = NULL; \
			return (OPT_NONE); \
		} \
	} \
	static inline optional_state_t \
	opt_match_as_mut_ ## type_name(opt_ ## type_name REQ_PTR(opt), \
	    c_type *REQ_PTR(out_value)) { \
		c_type x = opt->value; \
		if (!(none_check)) { \
			*out_value = &opt->value; \
			return (OPT_SOME); \
		} else { \
			*out_value = NULL; \
			return (OPT_NONE); \
		} \
	} \
	static inline c_type \
	opt_unwrap_ ## type_name(opt_ ## type_name opt, const char *filename, \
	    int line, const char *expr) { \
		c_type x = opt.value; \
		if (!(none_check)) { \
			return (opt.value); \
		} else { \
			logMsg("%s:%d: Attempted to unwrap None value in %s", \
			    filename, line, expr); \
			VERIFY_FAIL(); \
		} \
	} \
	static inline c_type const * \
	opt_unwrap_as_ref_ ## type_name(opt_ ## type_name const REQ_PTR(opt), \
	    const char *filename, int line, const char *expr) { \
		c_type x = opt->value; \
		if (!(none_check)) { \
			return (&opt->value); \
		} else { \
			logMsg("%s:%d: Attempted to unwrap None value in %s", \
			    filename, line, expr); \
			VERIFY_FAIL(); \
		} \
	} \
	static inline c_type * \
	opt_unwrap_as_mut_ ## type_name(opt_ ## type_name REQ_PTR(opt), \
	    const char *filename, int line, const char *expr) { \
		c_type x = opt->value; \
		if (!(none_check)) { \
			return (&opt->value); \
		} else { \
			logMsg("%s:%d: Attempted to unwrap None value in %s", \
			    filename, line, expr); \
			VERIFY_FAIL(); \
		} \
	} \
	static inline c_type \
	opt_unwrap_or_ ## type_name(opt_ ## type_name opt, c_type dfl_value) { \
		c_type x = opt.value; \
		if (!(none_check)) { \
			return (opt.value); \
		} else { \
			return (dfl_value); \
		} \
	} \
	static inline c_type \
	opt_unwrap_or_else_ ## type_name(opt_ ## type_name opt, \
	    c_type (*dfl_func)(void *), void *dfl_arg) { \
		c_type x = opt.value; \
		if (!(none_check)) { \
			return (opt.value); \
		} else { \
			return (dfl_func(dfl_arg)); \
		} \
	} \
	static inline opt_ ## type_name \
	opt_or_ ## type_name(opt_ ## type_name a, opt_ ## type_name b) { \
		c_type x = a.value; \
		if (!(none_check)) { \
			return (a); \
		} else { \
			return (b); \
		} \
	} \
	static inline opt_ ## type_name \
	opt_or_else_ ## type_name(opt_ ## type_name a, \
	    opt_ ## type_name (*func_b)(void *), void *arg_b) { \
		c_type x = a.value; \
		if (!(none_check)) { \
			return (a); \
		} else { \
			return (func_b(arg_b)); \
		} \
	} \
	static inline opt_ ## type_name \
	opt_into_ ## type_name(c_type x) { \
		if (!(none_check)) { \
			return (opt_some_ ## type_name(x)); \
		} else { \
			return (opt_none_ ## type_name()); \
		} \
	}

/**
 * \brief Declares a type alias for an existing optional type.
 *
 * This function constructs a `typedef`, as well as support functions
 * to make the new type name function equally to the old one.
 *
 * @param orig_type_name The type name of the original type.
 *	The type name is the part following the `opt_` prefix
 *	in the optional type and need not exactly correspond
 *	to the C type of the embedded value.
 * @param alias_type_name The new aliased type name for the
 *	optional type. This can subsequently be used in any
 *	place where the original type name would have been.
 * @param alias_c_type The C type of the aliased type. This
 *	type must be directly compatible to the embedded type
 *	of the original optional type.
 */
#define	IMPL_OPTIONAL_ALIAS(orig_type_name, alias_type_name, alias_c_type) \
	TYPE_ASSERT(((opt_ ## orig_type_name *)0)->value, alias_c_type); \
	typedef opt_ ## orig_type_name opt_ ## alias_type_name; \
	static inline opt_ ## alias_type_name \
	opt_some_ ## alias_type_name(alias_c_type x) { \
		return (opt_some_ ## orig_type_name(x)); \
	} \
	static inline opt_ ## alias_type_name \
	opt_none_ ## alias_type_name() { \
		return (opt_none_ ## orig_type_name()); \
	} \
	static inline bool \
	opt_is_some_ ## alias_type_name(opt_ ## alias_type_name opt) { \
		return (opt_is_some_ ## orig_type_name(opt)); \
	} \
	static inline optional_state_t \
	opt_match_ ## alias_type_name(opt_ ## alias_type_name opt, \
	    alias_c_type REQ_PTR(out_value)) { \
		return (opt_match_ ## orig_type_name(opt, out_value)); \
	} \
	static inline optional_state_t \
	opt_match_as_ref_ ## alias_type_name(opt_ ## alias_type_name const \
	    REQ_PTR(opt), alias_c_type const *REQ_PTR(out_value)) { \
		return (opt_match_as_ref_ ## orig_type_name(opt, out_value)); \
	} \
	static inline optional_state_t \
	opt_match_as_mut_ ## alias_type_name(opt_ ## alias_type_name \
	    REQ_PTR(opt), alias_c_type *REQ_PTR(out_value)) { \
		return (opt_match_as_mut_ ## orig_type_name(opt, out_value)); \
	} \
	static inline alias_c_type \
	opt_unwrap_ ## alias_type_name(opt_ ## alias_type_name opt, \
	    const char *filename, int line, const char *expr) { \
		return (opt_unwrap_ ## orig_type_name(opt, \
		    filename, line, expr)); \
	} \
	static inline alias_c_type const * \
	opt_unwrap_as_ref_ ## alias_type_name(opt_ ## alias_type_name \
	    const REQ_PTR(opt), const char *filename, int line, \
	    const char *expr) { \
		return (opt_unwrap_as_ref_ ## orig_type_name(opt, \
		    filename, line, expr)); \
	} \
	static inline alias_c_type * \
	opt_unwrap_as_mut_ ## alias_type_name(opt_ ## alias_type_name \
	    REQ_PTR(opt), const char *filename, int line, const char *expr) { \
		return (opt_unwrap_as_mut_ ## orig_type_name(opt, \
		    filename, line, expr)); \
	} \
	static inline alias_c_type \
	opt_unwrap_or_ ## alias_type_name(opt_ ## alias_type_name opt, \
	    alias_c_type dfl_value) { \
		return (opt_unwrap_or_ ## orig_type_name(opt, dfl_value)); \
	} \
	static inline alias_c_type \
	opt_unwrap_or_else_ ## alias_type_name(opt_ ## alias_type_name opt, \
	    alias_c_type (*dfl_func)(void *), void *dfl_arg) { \
		return (opt_unwrap_or_else_ ## orig_type_name(opt, dfl_func, \
		    dfl_arg)); \
	} \
	static inline opt_ ## alias_type_name \
	opt_or_ ## alias_type_name(opt_ ## alias_type_name a, \
	    opt_ ## alias_type_name b) { \
		return (opt_or_ ## orig_type_name(a, b)); \
	} \
	static inline opt_ ## alias_type_name \
	opt_or_else_ ## alias_type_name(opt_ ## alias_type_name a, \
	    opt_ ## alias_type_name (*func_b)(void *), void *arg_b) { \
		return (opt_or_else_ ## orig_type_name(a, func_b, arg_b)); \
	} \
	static inline opt_ ## alias_type_name \
	opt_into_ ## alias_type_name(alias_c_type value) { \
		return (opt_into_ ## orig_type_name(value)); \
	}

/// An optional type for wrapping an `int8_t` value.
IMPL_OPTIONAL_EXPLICIT(int8_t, int8_t)
/// An optional type for wrapping a `uint8_t` value.
IMPL_OPTIONAL_EXPLICIT(uint8_t, uint8_t)
/// An optional type for wrapping an `int16_t` value.
IMPL_OPTIONAL_EXPLICIT(int16_t, int16_t)
/// An optional type for wrapping a `uint16_t` value.
IMPL_OPTIONAL_EXPLICIT(uint16_t, uint16_t)
/// An optional type for wrapping an `int32_t` value.
IMPL_OPTIONAL_EXPLICIT(int32_t, int32_t)
/// An optional type for wrapping a `uint32_t` value.
IMPL_OPTIONAL_EXPLICIT(uint32_t, uint32_t)
/// An optional type for wrapping an `int64_t` value.
IMPL_OPTIONAL_EXPLICIT(int64_t, int64_t)
/// An optional type for wrapping a `uint64_t` value.
IMPL_OPTIONAL_EXPLICIT(uint64_t, uint64_t)
#if	!APL
/// An optional type for wrapping a `size_t` value.
IMPL_OPTIONAL_ALIAS(uint64_t, size_t, size_t)
#endif	// !APL

/**
 * \brief An optional type for wrapping a non-NAN `float` value.
 * @note This optional may NOT be used to wrap a `NAN` value. A `NAN`
 *	value is defined to be the OPT_NONE state for this optional.
 */
IMPL_OPTIONAL_IMPLICIT(float, float, NAN, isnan(x))
/**
 * \brief An optional type for wrapping a non-NAN `double` value.
 * @note This optional may NOT be used to wrap a `NAN` value. A `NAN`
 *	value is defined to be the OPT_NONE state for this optional.
 */
IMPL_OPTIONAL_IMPLICIT(double, double, NAN, isnan(x))

#if	defined(OPTIONALS_WITH_RUSTY_NAMES) || defined(__DOXYGEN__)

/// Alias for opt_int8_t available if `OPTIONALS_WITH_RUSTY_NAMES` is defined.
IMPL_OPTIONAL_ALIAS(int8_t, i8, int8_t)
/// Alias for opt_uint8_t available if `OPTIONALS_WITH_RUSTY_NAMES` is defined.
IMPL_OPTIONAL_ALIAS(uint8_t, u8, uint8_t)
/// Alias for opt_int16_t available if `OPTIONALS_WITH_RUSTY_NAMES` is defined.
IMPL_OPTIONAL_ALIAS(int16_t, i16, int16_t)
/// Alias for opt_uint16_t available if `OPTIONALS_WITH_RUSTY_NAMES` is defined.
IMPL_OPTIONAL_ALIAS(uint16_t, u16, uint16_t)
/// Alias for opt_int32_t available if `OPTIONALS_WITH_RUSTY_NAMES` is defined.
IMPL_OPTIONAL_ALIAS(int32_t, i32, int32_t)
/// Alias for opt_uint32_t available if `OPTIONALS_WITH_RUSTY_NAMES` is defined.
IMPL_OPTIONAL_ALIAS(uint32_t, u32, uint32_t)
/// Alias for opt_int64_t available if `OPTIONALS_WITH_RUSTY_NAMES` is defined.
IMPL_OPTIONAL_ALIAS(int64_t, i64, int64_t)
/// Alias for opt_uint64_t available if `OPTIONALS_WITH_RUSTY_NAMES` is defined.
IMPL_OPTIONAL_ALIAS(uint64_t, u64, uint64_t)
/// Alias for opt_size_t available if `OPTIONALS_WITH_RUSTY_NAMES` is defined.
IMPL_OPTIONAL_ALIAS(size_t, usize, size_t)
/// Alias for opt_float available if `OPTIONALS_WITH_RUSTY_NAMES` is defined.
IMPL_OPTIONAL_ALIAS(float, f32, float)
/// Alias for opt_double available if `OPTIONALS_WITH_RUSTY_NAMES` is defined.
IMPL_OPTIONAL_ALIAS(double, f64, double)

#endif	// defined(OPTIONALS_WITH_RUSTY_NAMES) || defined(__DOXYGEN__)
/**
 * \brief An optional type for wrapping a non-NULL `char *` value.
 * @note This optional may NOT be used to wrap a `NULL` pointer.
 *	A `NULL` value is defined to be the OPT_NONE state for
 *	this optional. You can thus be sure that this optional
 *	will never contain a `NULL` pointer.
 */
IMPL_OPTIONAL_IMPLICIT(str, char *, NULL, x == NULL)
/**
 * \brief An optional type for wrapping a non-NULL `char const *` value.
 * @note This optional may NOT be used to wrap a `NULL` pointer.
 *	A `NULL` value is defined to be the OPT_NONE state for
 *	this optional. You can thus be sure that this optional
 *	will never contain a `NULL` pointer.
 */
IMPL_OPTIONAL_IMPLICIT(str_const, const char *, NULL, x == NULL)

/**
 * \brief An optional type for wrapping a non-NULL `vect2_t` value.
 * @note This optional may NOT be used to wrap a `NULL_VECT2` value.
 *	A null vector value is defined to be the OPT_NONE state for
 *	this optional.
 */
IMPL_OPTIONAL_IMPLICIT(vect2_t, vect2_t, NULL_VECT2, IS_NULL_VECT2(x))
/**
 * \brief An optional type for wrapping a non-NULL `vect3_t` value.
 * @note This optional may NOT be used to wrap a `NULL_VECT3` value.
 *	A null vector value is defined to be the OPT_NONE state for
 *	this optional.
 */
IMPL_OPTIONAL_IMPLICIT(vect3_t, vect3_t, NULL_VECT3, IS_NULL_VECT3(x))
/**
 * \brief An optional type for wrapping a non-NULL `vect3l_t` value.
 * @note This optional may NOT be used to wrap a `NULL_VECT3L` value.
 *	A null vector value is defined to be the OPT_NONE state for
 *	this optional.
 */
IMPL_OPTIONAL_IMPLICIT(vect3l_t, vect3l_t, NULL_VECT3L, IS_NULL_VECT3(x))
/**
 * \brief An optional type for wrapping a non-NULL `geo_pos2_t` value.
 * @note This optional may NOT be used to wrap a `NULL_GEO_POS2` value.
 *	A null vector value is defined to be the OPT_NONE state for
 *	this optional.
 */
IMPL_OPTIONAL_IMPLICIT(geo_pos2_t, geo_pos2_t, NULL_GEO_POS2,
    IS_NULL_GEO_POS2(x))
/**
 * \brief An optional type for wrapping a non-NULL `geo_pos3_t` value.
 * @note This optional may NOT be used to wrap a `NULL_GEO_POS3` value.
 *	A null vector value is defined to be the OPT_NONE state for
 *	this optional.
 */
IMPL_OPTIONAL_IMPLICIT(geo_pos3_t, geo_pos3_t, NULL_GEO_POS3,
    IS_NULL_GEO_POS3(x))
/**
 * \brief An optional type for wrapping a non-NULL `geo_pos2_32_t` value.
 * @note This optional may NOT be used to wrap a `NULL_GEO_POS2_32` value.
 *	A null vector value is defined to be the OPT_NONE state for
 *	this optional.
 */
IMPL_OPTIONAL_IMPLICIT(geo_pos2_32_t, geo_pos2_32_t, NULL_GEO_POS2_32,
    IS_NULL_GEO_POS2(x))
/**
 * \brief An optional type for wrapping a non-NULL `geo_pos3_32_t` value.
 * @note This optional may NOT be used to wrap a `NULL_GEO_POS3_32` value.
 *	A null vector value is defined to be the OPT_NONE state for
 *	this optional.
 */
IMPL_OPTIONAL_IMPLICIT(geo_pos3_32_t, geo_pos3_32_t, NULL_GEO_POS3_32,
    IS_NULL_GEO_POS3(x))

UNUSED_ATTR static inline void
_unknown_optional_type_you_need_to_include_your_custom_optional_h_(void)
{
}

/**
 * \brief Appends a new entry to the definition of the OPTIONAL_TYPE_LIST()
 * macro.
 *
 * Use this macro to append entries to your OPTIONAL_TYPE_LIST() macro,
 * which contains a list of your custom optional types.
 *
 * @see \ref custom_opt_types "Adding Your Own Custom Optional Types"
 */
#define	OPTIONAL_TYPE_LIST_ADD(type_name, c_type, op_name)	\
	c_type: op_name ## type_name

/**
 * \def OPTIONAL_TYPE_LIST
 * \brief Overridable list of custom optional types provided by your code.
 *
 * If you want to specify a list of your own custom optional types, you
 * must redefine this macro somewhere in your headers, to add the optional
 * types using the OPTIONAL_TYPE_LIST_ADD() macro.
 *
 * Example:
 * ```
 * #undef OPTIONAL_TYPE_LIST
 * #define OPTIONAL_TYPE_LIST(op_name) \
 *     OPTIONAL_TYPE_LIST_ADD(my_type_1, my_type_1, op_name), \
 *     OPTIONAL_TYPE_LIST_ADD(my_type_2, my_type_2 *, op_name)
 * ```
 *
 * @see \ref custom_opt_types "Adding Your Own Custom Optional Types"
 */
#ifndef	OPTIONAL_TYPE_LIST
#define	OPTIONAL_TYPE_LIST(op_name) \
    default: _unknown_optional_type_you_need_to_include_your_custom_optional_h_
#endif

#define	OPTIONAL_TYPE_SELECTOR(op_name, expr)	\
	_Generic((expr), \
	OPTIONAL_TYPE_LIST_ADD(int8_t, int8_t, op_name), \
	OPTIONAL_TYPE_LIST_ADD(uint8_t, uint8_t, op_name), \
	OPTIONAL_TYPE_LIST_ADD(int16_t, int16_t, op_name), \
	OPTIONAL_TYPE_LIST_ADD(uint16_t, uint16_t, op_name), \
	OPTIONAL_TYPE_LIST_ADD(int32_t, int32_t, op_name), \
	OPTIONAL_TYPE_LIST_ADD(uint32_t, uint32_t, op_name), \
	OPTIONAL_TYPE_LIST_ADD(int64_t, int64_t, op_name), \
	OPTIONAL_TYPE_LIST_ADD(uint64_t, uint64_t, op_name), \
	OPTIONAL_TYPE_LIST_ADD(float, float, op_name), \
	OPTIONAL_TYPE_LIST_ADD(double, double, op_name), \
	OPTIONAL_TYPE_LIST_ADD(str, char *, op_name), \
	OPTIONAL_TYPE_LIST_ADD(str_const, char const *, op_name), \
	OPTIONAL_TYPE_LIST_ADD(vect2_t, vect2_t, op_name), \
	OPTIONAL_TYPE_LIST_ADD(vect3_t, vect3_t, op_name), \
	OPTIONAL_TYPE_LIST_ADD(vect3l_t, vect3l_t, op_name), \
	OPTIONAL_TYPE_LIST_ADD(geo_pos2_t, geo_pos2_t, op_name), \
	OPTIONAL_TYPE_LIST_ADD(geo_pos3_t, geo_pos3_t, op_name), \
	OPTIONAL_TYPE_LIST_ADD(geo_pos2_32_t, geo_pos2_32_t, op_name), \
	OPTIONAL_TYPE_LIST_ADD(geo_pos3_32_t, geo_pos3_32_t, op_name), \
	OPTIONAL_TYPE_LIST(op_name))

/**
 * \def SOME
 * \brief Constructs a new optional value in the SOME state.
 *
 * The type of the optional is determined by the result type of the
 * expression in the `expr` argument.
 *
 * Example usage:
 * ```
 * opt_float my_func(void) {
 *     if (some_condition()) {
 *          return SOME(5.0f);
 *     } else {
 *          return NONE(float);
 *     }
 * }
 * ```
 * Because the type is determined solely by the macro argument
 * expression and not by the overall return type, you must make sure
 * the expression is typed exactly to the desired optional type. This
 * can require an explicit cast. For example `SOME(5.0)` always results
 * in an `opt_double`, even if you want assign to or return an
 * `opt_float`. To force the correct type inference, use `SOME(5.0f)`
 * or `SOME((float)5.0)`.
 *
 * @note C++ doesn't support the C11 `_Generic` construct and thus
 *	automatic type inference cannot be used for the `SOME` macro.
 *	When used from C++ code, you must provide an explicit optional
 *	type name in the first argument, such as: `SOME(float, 5.0f)`.
 *	The optional type name is the latter part of the name of the
 *	optional type, e.g. for an `opt_str` the type name is `str`
 *	and not necessarily the C type designator (which is `char *`
 *	in the case of `opt_str`).
 */
#ifndef	SOME
#if	__STDC_VERSION__ >= 201112L || defined(__DOXYGEN__)
#define	SOME(expr)		OPTIONAL_TYPE_SELECTOR(opt_some_, expr)(expr)
#else	// !(__STDC_VERSION__ >= 201112L)
#define	SOME(type_name, expr)	opt_some_ ## type_name(expr)
#endif	// !(__STDC_VERSION__ >= 201112L)
#endif	// !defined(SOME)

/**
 * \def NONE
 * \brief Constructs a new optional value in the NONE state.
 *
 * Due to C's lack of type inference support from return values, you
 * must provide an explicit type name annotation in the `type_name`
 * argument. This is the latter part of the name of the optional
 * type, e.g. for an `opt_str` the type name is `str` and not
 * necessarily the C type designator (which is `char *` in the case
 * of `opt_str`).
 *
 * Example usage:
 * ```
 * opt_float my_func(void) {
 *     if (some_condition()) {
 *          return SOME(5.0f);
 *     } else {
 *          return NONE(float);
 *     }
 * }
 * ```
 */
#ifndef	NONE
#define	NONE(type_name)		opt_none_ ## type_name()
#endif

/**
 * \def IS_SOME
 * \brief Returns true if the optional is in the SOME state.
 */
#ifndef	IS_SOME
#if	__STDC_VERSION__ >= 201112L || defined(__DOXYGEN__)
#define	IS_SOME(opt) \
	OPTIONAL_TYPE_SELECTOR(opt_is_some_, (opt).value)(opt)
#else	// !(__STDC_VERSION__ >= 201112L)
#define	IS_SOME(type_name, opt) \
	opt_is_some_ ## type_name(opt)
#endif	// !(__STDC_VERSION__ >= 201112L)
#endif

/**
 * \def IS_NONE
 * \brief Returns true if the optional is in the NONE state.
 */
#ifndef	IS_NONE
#define	IS_NONE(x)	(!IS_SOME(x))
#endif

/**
 * \def MATCH
 * \brief Extracts the value embedded in the optional and returns
 * the `optional_state_t` enum corresponding to the state.
 *
 * If the optional is in the SOME state, the wrapped value is written
 * to the `out_value` argument. If the optional is in the NONE state,
 * the `out_value` argument is filled with an invalid value.
 *
 * @note The `out_value` argument is mandatory and may not be NULL.
 *
 * This macro is intended to be used in one of the following ways:
 *
 * 1. As a switch condition, which is similar to the Rust `match`
 *	value destructuring pattern:
 * ```
 * float my_value;
 * switch (MATCH(my_opt_float, &my_value)) {
 * case OPT_SOME:
 *     operate_with(my_value);
 *     break;
 * case OPT_NONE:
 *     operate_without_any_value();
 *     break;
 * }
 * ```
 * 2. As an if condition, which is similar to a Rust "if-let" statement:
 * ```
 * float my_value;
 * if (MATCH(my_opt_float, &my_value)) {
 *     operate_with(my_value);
 * }
 * ```
 */
#ifndef	MATCH
#if	__STDC_VERSION__ >= 201112L || defined(__DOXYGEN__)
#define	MATCH(opt, out_value) \
	OPTIONAL_TYPE_SELECTOR(opt_match_, (opt).value)((opt), (out_value))
#else	// !(__STDC_VERSION__ >= 201112L)
#define	MATCH(type_name, opt, out_value) \
	opt_match_ ## type_name((opt), (out_value))
#endif	// !(__STDC_VERSION__ >= 201112L)
#endif	// !defined(MATCH)

/**
 * \def MATCH_AS_REF
 * \brief Extracts an immutable reference to the value embedded in the
 * optional and returns the `optional_state_t` enum corresponding to
 * the state.
 *
 * This is similar to MATCH(), except the second argument is a reference
 * to an immutable pointer. If the optional value is OPT_SOME, the pointer
 * is filled with a reference to the value contained internally in the
 * optional value. If the optional value is OPT_NONE, the pointer is set
 * to NULL.
 *
 * The purpose of this macro is to facilitate returning an internal value
 * by immutable reference. This may be necessary, in case a stack-allocated
 * copy of the object cannot be returned from the calling function.
 *
 * @note If you need a mutable reference to the value contained inside
 *	of the optional, use MATCH_AS_MUT() instead.
 *
 * ```
 * float *my_value_ptr;
 * switch (MATCH(my_opt_float, &my_value)) {
 * case OPT_SOME:
 *     printf("my_value is %f\n", *my_value_ptr);
 *     break;
 * case OPT_NONE:
 *     // my_value_ptr is now NULL
 *     break;
 * }
 * ```
 * @see MATCH_AS_MUT()
 */
#ifndef	MATCH_AS_REF
#if	__STDC_VERSION__ >= 201112L || defined(__DOXYGEN__)
#define	MATCH_AS_REF(opt, out_value_p) \
	OPTIONAL_TYPE_SELECTOR(opt_match_as_ref_, (*(opt)).value)\
	    ((opt), (out_value_p))
#else	// !(__STDC_VERSION__ >= 201112L)
#define	MATCH_AS_REF(type_name, opt, out_value_p) \
	opt_match_as_ref_ ## type_name((opt), (out_value_p))
#endif	// !(__STDC_VERSION__ >= 201112L)
#endif	// !defined(MATCH_AS_REF)

/**
 * \def MATCH_AS_MUT
 * \brief Extracts a reference to the value embedded in the optional
 * and returns the `optional_state_t` enum corresponding to the state.
 *
 * This is similar to MATCH(), except the second argument is a reference
 * to a pointer. If the optional value is OPT_SOME, the pointer is filled
 * with a reference to the value contained internally in the optional
 * value. If the optional value is OPT_NONE, the pointer is set to NULL.
 *
 * The purpose of this macro is to facilitate returning an internal value
 * by reference. This may be necessary, in case a stack-allocated
 * copy of the object cannot be returned from the calling function.
 *
 * ```
 * float *my_value_ptr;
 * switch (MATCH(my_opt_float, &my_value)) {
 * case OPT_SOME:
 *     printf("my_value is %f\n", *my_value_ptr);
 *     break;
 * case OPT_NONE:
 *     // my_value_ptr is now NULL
 *     break;
 * }
 * ```
 */
#ifndef	MATCH_AS_MUT
#if	__STDC_VERSION__ >= 201112L || defined(__DOXYGEN__)
#define	MATCH_AS_MUT(opt, out_value_p) \
	OPTIONAL_TYPE_SELECTOR(opt_match_as_mut_, (*(opt)).value)\
	    ((opt), (out_value_p))
#else	// !(__STDC_VERSION__ >= 201112L)
#define	MATCH_AS_MUT(type_name, opt, out_value_p) \
	opt_match_as_mut_ ## type_name((opt), (out_value_p))
#endif	// !(__STDC_VERSION__ >= 201112L)
#endif	// !defined(MATCH_AS_MUT)

/**
 * \def UNWRAP
 * \brief Extracts the value of the optional unconditionally.
 *
 * If the optional was in the SOME state, returns the wrapped value.
 * If the optional was in the NONE state, causes a runtime assertion
 * failure and panic. You should only use this macro during testing,
 * or after you have made sure using other means that the optional
 * is in the SOME state.
 *
 * Example usage:
 * ```
 * float my_value = UNWRAP(my_opt_float);
 * ```
 */
#ifndef	UNWRAP
#if	__STDC_VERSION__ >= 201112L || defined(__DOXYGEN__)
#define	UNWRAP(opt)	\
	OPTIONAL_TYPE_SELECTOR(opt_unwrap_, (opt).value)((opt), \
	    log_basename(__FILE__), __LINE__, #opt)
#else	// !(__STDC_VERSION__ >= 201112L)
#define	UNWRAP(type_name, opt)	\
	opt_unwrap_ ## type_name((opt), log_basename(__FILE__), __LINE__, #opt)
#endif	// !(__STDC_VERSION__ >= 201112L)
#endif	// !defined(UNWRAP)

/**
 * \def UNWRAP_AS_REF
 * \brief Extracts an immutable reference to the value of the optional
 * unconditionally.
 *
 * If the optional was in the SOME state, returns a pointer to the
 * wrapped value. If the optional was in the NONE state, causes a runtime
 * assertion failure and panic. You should only use this macro during
 * testing, or after you have made sure using other means that the
 * optional is in the SOME state.
 *
 * Example usage:
 * ```
 * const float *my_value_ref = UNWRAP_AS_REF(&my_opt_float);
 * ```
 */
#ifndef	UNWRAP_AS_REF
#if	__STDC_VERSION__ >= 201112L || defined(__DOXYGEN__)
#define	UNWRAP_AS_REF(opt)	\
	OPTIONAL_TYPE_SELECTOR(opt_unwrap_as_ref_, (*(opt)).value)((opt), \
	    log_basename(__FILE__), __LINE__, #opt)
#else	// !(__STDC_VERSION__ >= 201112L)
#define	UNWRAP_AS_REF(type_name, opt)	\
	opt_unwrap_as_ref_ ## type_name((opt), \
	    log_basename(__FILE__), __LINE__, #opt)
#endif	// !(__STDC_VERSION__ >= 201112L)
#endif	// !defined(UNWRAP_AS_REF)

/**
 * \def UNWRAP_AS_MUT
 * \brief Extracts a mutable reference to the value of the optional
 * unconditionally.
 *
 * If the optional was in the SOME state, returns a pointer to the
 * wrapped value. If the optional was in the NONE state, causes a runtime
 * assertion failure and panic. You should only use this macro during
 * testing, or after you have made sure using other means that the
 * optional is in the SOME state.
 *
 * Example usage:
 * ```
 * float *my_value_ref = UNWRAP_AS_REF(&my_opt_float);
 * ```
 */
#ifndef	UNWRAP_AS_MUT
#if	__STDC_VERSION__ >= 201112L || defined(__DOXYGEN__)
#define	UNWRAP_AS_MUT(opt)	\
	OPTIONAL_TYPE_SELECTOR(opt_unwrap_as_mut_, (*(opt)).value)((opt), \
	    log_basename(__FILE__), __LINE__, #opt)
#else	// !(__STDC_VERSION__ >= 201112L)
#define	UNWRAP_AS_MUT(type_name, opt)	\
	opt_unwrap_as_mut_ ## type_name((opt), \
	    log_basename(__FILE__), __LINE__, #opt)
#endif	// !(__STDC_VERSION__ >= 201112L)
#endif	// !defined(UNWRAP_AS_MUT)

/**
 * \def UNWRAP_OR
 * \brief Extracts the value of the optional, or evaluates to a fallback value.
 *
 * If `opt` is in the SOME state, evaluates to the wrapped value. If `opt`
 * is in the NONE state, evaluates to the `dfl_value` argument. This can be
 * used to safely unwrap an optional, providing a default value in case the
 * optional was in the NONE state.
 *
 * Example usage:
 * ```
 * float my_value = UNWRAP_OR(my_opt_float, 0.0f);
 * ```
 * @note The second argument to this macro is eagerly evaluated, even
 *	if the first argument is in the SOME state. You must take care
 *	to avoid unintended side effects of the second argument. If
 *	you want to force lazy execution, you must use the
 *	UNWRAP_OR_ELSE() macro instead.
 * @see UNWRAP_OR_ELSE()
 */
#ifndef	UNWRAP_OR
#if	__STDC_VERSION__ >= 201112L || defined(__DOXYGEN__)
#define	UNWRAP_OR(opt, dfl_value)	\
	OPTIONAL_TYPE_SELECTOR(opt_unwrap_or_, (opt).value)((opt), (dfl_value))
#else	// !(__STDC_VERSION__ >= 201112L)
#define	UNWRAP_OR(type_name, opt, dfl_value)	\
	opt_unwrap_or_ ## type_name((opt), (dfl_value))
#endif	// !(__STDC_VERSION__ >= 201112L)
#endif	// !defined(UNWRAP_OR)

/**
 * \def UNWRAP_OR_ELSE
 * \brief Extracts the value of the optional, or yields a fallback value
 * with lazy evaluation.
 *
 * If `opt` is in the SOME state, evaluates to the wrapped value. If `opt`
 * is in the NONE state, calls `dfl_func` with `dfl_arg` in its argument
 * and evaluates to the return value of this call. The function must have
 * a signature conforming to:
 * ```
 * c_type func(void *);
 * ```
 * where `c_type` is the native C type wrapped in the optional type.
 * This can be used to safely unwrap an optional, providing a lazily
 * evaluated default value in case the optional was in the NONE state.
 *
 * Example usage:
 * ```
 * float calc_fallback(void *arg);
 *
 * float my_value = UNWRAP_OR_ELSE(my_opt_float, calc_fallback, foo);
 * ```
 * @note The purpose of this macro is to provide a mechanism for lazy
 *	evaluation of the default case. The function is not called
 *	unless the first argument is in the NONE state. If you want
 *	the second argument to be eagerly evaluated (as well as avoid
 *	having to write a callback function), use UNWRAP_OR().
 * @see UNWRAP_OR()
 */
#ifndef	UNWRAP_OR_ELSE
#if	__STDC_VERSION__ >= 201112L || defined(__DOXYGEN__)
#define	UNWRAP_OR_ELSE(opt, dfl_func, dfl_arg)	\
	OPTIONAL_TYPE_SELECTOR(opt_unwrap_or_else_, (opt).value)\
	    ((opt), (dfl_func), (dfl_arg))
#else	// !(__STDC_VERSION__ >= 201112L)
#define	UNWRAP_OR_ELSE(type_name, opt, dfl_func, dfl_arg)	\
	opt_unwrap_or_else_ ## type_name((opt), (dfl_func), (dfl_arg))
#endif	// !(__STDC_VERSION__ >= 201112L)
#endif	// !defined(UNWRAP_OR)

/**
 * \def UNWRAP_OR_RET
 * \brief Unwraps a SOME value, or in case of a NONE value, returns from
 * the current function.
 *
 * This macro lets you emulate the Rust '?' operator.
 * 1. If the first argument is in the SOME state, the contained value is
 *	yielded.
 * 2. If the first argument is in the NONE state, returns from the current
 *	function, optionally with a return value of your choosing.
 *
 * The way to use this macro is as follows:
 * ```
 * bool my_func(opt_float wrapped_value) {
 *     // if `wrapped_value` is NONE, we return from `my_func` with `false`:
 *     float value = UNWRAP_OR_RET(wrapped_value, false);
 *     // ... do something with `value`, it is guaranteed to be valid ...
 *     return true;
 * }
 * ```
 * To return with no value from the current function, simply invoke the
 * macro with just one argument:
 * ```
 * float foo = UNWRAP_OR_RET(wrapped_foo);
 * ```
 */
#ifndef	UNWRAP_OR_RET
#define	UNWRAP_OR_RET(opt, ...) \
	({ __typeof(opt) __tmp = (opt); \
	    if (IS_NONE(__tmp)) { return __VA_ARGS__; } \
	    UNWRAP(__tmp); })
#endif	// !defined(UNWRAP_OR_RET)

/**
 * \def UNWRAP_OR_GOTO
 * \brief Unwraps a SOME value, or in case of a NONE value, goes to the label.
 *
 * This macro helps with unwinding when a NONE variant is encountered,
 * requiring cleanup of the current function.
 *
 * Example usage:
 * ```
 * opt_float foo(void);	// can return NONE variant
 *
 * void bar() {
 *     // setup some resources that need cleanup
 *     ...
 *     float need_valid_value_here = UNWRAP_OR_GOTO(foo(), out);
 *     ...
 *     // use need_valid_value_here
 *     ...
 * out:
 *     // perform cleanup of resources acquired during setup
 * }
 * ```
 */
#ifndef	UNWRAP_OR_GOTO
#define	UNWRAP_OR_GOTO(opt, label) \
	({ __typeof(opt) __tmp = (opt); \
	    if (IS_NONE(__tmp)) { goto label; } UNWRAP(__tmp); })
#endif	// !defined(UNWRAP_OR_GOTO)

/**
 * \def UNWRAP_OR_BREAK
 * \brief Unwraps a SOME value, or in case of a NONE value, breaks out of
 * the current loop or switch case (invokes the `break` keyword).
 *
 * This macro helps with flow control when a NONE variant is encountered.
 *
 * Example usage:
 * ```
 * for (int i = 0; i < 10; i++) {
 *     ...
 *     // break out of loop if we get a NONE variant here
 *     float need_valid_value_here = UNWRAP_OR_BREAK(this_can_be_NONE());
 *     ...
 * }
 * ```
 */
#ifndef	UNWRAP_OR_BREAK
#define	UNWRAP_OR_BREAK(opt) \
	({ __typeof(opt) __tmp = (opt); \
	    if (IS_NONE(__tmp)) { break; } UNWRAP(__tmp); })
#endif	// !defined(UNWRAP_OR_BREAK)

/**
 * \def UNWRAP_OR_CONTINUE
 * \brief Unwraps a SOME value, or in case of a NONE value, restarts the
 * current loop iteration (invokes the `continue` keyword).
 *
 * This macro helps with flow control when a NONE variant is encountered.
 *
 * Example usage:
 * ```
 * for (int i = 0; i < 10; i++) {
 *     ...
 *     // continue to next loop iteration if we get a NONE variant here
 *     float need_valid_value_here = UNWRAP_OR_CONTINUE(this_can_be_NONE());
 *     ...
 * }
 * ```
 */
#ifndef	UNWRAP_OR_CONTINUE
#define	UNWRAP_OR_CONTINUE(opt) \
	({ __typeof(opt) __tmp = (opt); \
	    if (IS_NONE(__tmp)) { continue; } UNWRAP(__tmp); })
#endif	// !defined(UNWRAP_OR_CONTINUE)

/**
 * \def OPT_OR
 * \brief Selects between two optional values based on whether the
 * first optional is in the SOME state.
 *
 * If the first argument is in the SOME state, returns the first
 * optional. Otherwise returns the second optional. This macro
 * can be used to provide a fallback optional value without
 * actually unwrapping the value contained in the optional.
 *
 * Example usage:
 * ```
 * opt_float result = OPT_OR(my_opt_float, fallback_opt_float);
 * ```
 *
 * @note The second argument to this macro is eagerly evaluated, even
 *	if the first argument is in the SOME state. You must take care
 *	to avoid unintended side effects of the second argument. If
 *	you want to force lazy execution, you must use the OPT_OR_ELSE()
 *	macro instead.
 * @see OPT_OR_ELSE
 */
#ifndef	OPT_OR
#if	__STDC_VERSION__ >= 201112L || defined(__DOXYGEN__)
#define	OPT_OR(a, b)		\
	OPTIONAL_TYPE_SELECTOR(opt_or_, (a).value)((a), (b))
#else	// !(__STDC_VERSION__ >= 201112L)
#define	OPT_OR(type_name, a, b)	\
	opt_or_ ## type_name((a), (b))
#endif	// !(__STDC_VERSION__ >= 201112L)
#endif	// !defined(OPT_OR)

/**
 * \def OPT_OR_ELSE
 * \brief Selects between two optional values based on whether the
 * first optional is in the SOME state. Provides lazy evaluation.
 *
 * If the `a` argument is in the SOME state, returns `a`. Otherwise
 * calls `func_b` with argument `arg_b` and returns its return value.
 * The `func_b` function must conform to this signature:
 * ```
 * opt_type func(void *);
 * ```
 * where `opt_type` is the same type as the type of `a`.
 *
 * Example usage:
 * ```
 * opt_float build_fallback(void *arg);
 *
 * opt_float result = OPT_OR_ELSE(my_opt_float, build_fallback, foo);
 * ```
 * @note The purpose of this macro is to provide a mechanism for lazy
 *	evaluation of the fallback case. The `func_b` argument is not
 *	called unless the first argument is in the NONE state. If
 *	you want the second argument to be eagerly evaluated (as well
 *	as avoid having to write a callback function), use OPT_OR().
 * @see OPT_OR
 */
#ifndef	OPT_OR_ELSE
#if	__STDC_VERSION__ >= 201112L || defined(__DOXYGEN__)
#define	OPT_OR_ELSE(a, func_b, arg_b)		\
	OPTIONAL_TYPE_SELECTOR(opt_or_else_, (a).value)((a), (func_b), (arg_b))
#else	// !(__STDC_VERSION__ >= 201112L)
#define	OPT_OR_ELSE(type_name, a, func_b, arg_b)	\
	opt_or_ ## type_name((a), (func_b), (arg_b))
#endif	// !(__STDC_VERSION__ >= 201112L)
#endif	// !defined(OPT_OR)

/**
 * \def IF_LET
 * \brief Allows constructing Rust-like if-let statements in C.
 *
 * This macro helps in assembling Rust-like if-let statements, which scope
 * their variables to prevent inadvertently leaking them outside of the
 * properly matched scope (as might otherwise happen with the MATCH() macro).
 *
 * This macro, along with its accompanying IF_LET_END and IF_LET_ELSE macros
 * replaces the normal control flow constructs, to provide automatic scoping.
 *
 * ```
 * opt_float my_option;
 * IF_LET(float, foo, my_option)
 *     // place your condition-met code into this block
 *     // `foo' is available here (and only here) to contain the
 *     // SOME state value of `my_option'
 *     use_foo(foo);
 * IF_LET_END
 * ```
 * In the above example, `foo` is prevented from leaking outside of the
 * if block scope, thus stopping accidentally using it when no value was
 * contained in the optional type. To add an else block to this kind of
 * block, use:
 * ```
 * IF_LET(float, foo, my_option)
 *     use_foo(foo);
 * IF_LET_ELSE
 *     // `foo' doesn't exist here
 *     do_something_else();
 * IF_LET_END
 * ```
 */
#ifndef	IF_LET
#define	IF_LET(vartype, varname, opt) \
	{ \
		vartype __if_let_tmp_##varname; \
		/* \
		 * The void pointer cast here gets rid of a C language quirk \
		 * where const and non-const pointers aren't technically \
		 * compatible for referencing. Thus to avoid having to force \
		 * the caller to use a mutable reference when all they want \
		 * is an immutable reference, we perform type erasure here. \
		 */ \
		if (MATCH((opt), (void *)&__if_let_tmp_##varname) == \
		    OPT_SOME) { \
			vartype varname = __if_let_tmp_##varname;
/**
 * \def IF_LET_AS_REF
 * \brief Same as IF_LET() macro, but returns reference to the value.
 *
 * This macro works in a similar manner as IF_LET(), but instead returning
 * the optional in the SOME case by value, the optional is returned by
 * reference (as in MATCH_AS_REF()). Example:
 * ```
 * opt_object_t maybe_obj;
 * IF_LET_AS_REF(object_t *, obj, maybe_obj)
 *     // `obj' points into `maybe_obj'
 *     use_obj_ref(obj);
 * IF_LET_END
 * ```
 */
#define	IF_LET_AS_REF(vartype, varname, opt) \
	{ \
		vartype __if_let_tmp_##varname; \
		if (MATCH_AS_REF((opt), (void *)&__if_let_tmp_##varname) == \
		    OPT_SOME) { \
			vartype varname = __if_let_tmp_##varname;
#define	IF_LET_ELSE	} else {
#define	IF_LET_END	}}
#endif	// !defined(IF_LET)

/**
 * \def OPT_INTO
 * \brief Allows wrapping an implicitly representable value into a suitable
 * optional.
 *
 * This macro works by performing a NONE-check on the value itself, to then
 * determine whether the value should be wrapped in a SOME variant, or a
 * NONE variant.
 *
 * \note This *only* works for values which can be implicitly represented
 * in an optional (i.e. they are defined using IMPL_OPTIONAL_IMPLICIT).
 * This is because we rely on the value itself containing enough information
 * to convey its state. Only the following types implemented in libacfutils
 * support implicit conversion into an optional type:
 * - floating point types `opt_float` and `opt_double`
 * - string types `opt_str` and `opt_str_const`
 * - vector geometric types `opt_vect*_t`
 * - geographic coordinate types `opt_geo_pos*_t`
 *
 * Example usage:
 * ```
 * float foo();		// may return NAN to indicate an invalid value
 * IF_LET(float, valid_value, OPT_INTO(foo()))
 *	// valid_value is guaranteed to be non-NAN here
 * IF_LET_END
 * ```
 */
#ifndef	OPT_INTO
#if	__STDC_VERSION__ >= 201112L || defined(__DOXYGEN__)
#define	OPT_INTO(value)	OPTIONAL_TYPE_SELECTOR(opt_into_, (value))(value)
#else	// !(__STDC_VERSION__ >= 201112L)
#define	OPT_INTO(type_name, opt)	opt_into_ ## type_name(value)
#endif	// !(__STDC_VERSION__ >= 201112L)
#endif	// !defined(OPT_INTO)

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_OPTIONAL_H_ */
