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
/**
 * \file
 */

#ifndef	_ACF_UTILS_SYSMACROS_H_
#define	_ACF_UTILS_SYSMACROS_H_

#if	IBM
#include <windows.h>
#endif	/* IBM */

#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	NO_ACF_TYPE		0
#define	FF_A320_ACF_TYPE	1

#if	IBM
#define	DIRSEP		'\\'
#define	DIRSEP_S	"\\"	/* DIRSEP as a string */
#else	/* !IBM */
#define	DIRSEP		'/'
#define	DIRSEP_S	"/"	/* DIRSEP as a string */
#ifndef	MAX_PATH
#define	MAX_PATH	512
#endif
#endif	/* !IBM */

#if	defined(__GNUC__) || defined(__clang__)
#define	DEPRECATED_FUNC(f)	f __attribute__((deprecated))
#define	DEPRECATED_ATTR		__attribute__((deprecated))
#define	PRINTF_ATTR(x)		__attribute__((format(printf, x, x + 1)))
#define	PRINTF_ATTR2(x,y)	__attribute__((format(printf, x, y)))
#define	PRINTF_FORMAT(f)	f
#define	SENTINEL_ATTR		__attribute__((sentinel))
#define	HOT_ATTR		__attribute__((hot))
#define	PURE_ATTR		__attribute__((pure))
#define	ALWAYS_INLINE_ATTR	__attribute__((always_inline))
#define	ALIGN_ATTR(x)		__attribute__((aligned(x)))

#ifndef	BSWAP32
#define	BSWAP16(x)	__builtin_bswap16((x))
#define	BSWAP32(x)	__builtin_bswap32((x))
#define	BSWAP64(x)	__builtin_bswap64((x))
#endif	/* BSWAP32 */

#define	COND_LIKELY(x)		__builtin_expect(x, 1)
#define	COND_UNLIKELY(x)	__builtin_expect(x, 0)

#else	/* !__GNUC__ && !__clang__ */

#define	DEPRECATED_ATTR
#define	PRINTF_ATTR(x)
#define	PRINTF_ATTR2(x,y)
#define	SENTINEL_ATTR
#define	HOT_ATTR
#define	PURE_ATTR
#define	ALWAYS_INLINE_ATTR

#define	COND_LIKELY(x)		x
#define	COND_UNLIKELY(x)	x

#if	defined(_MSC_VER)
#define	ALIGN_ATTR(x)		__declspec(align(x))
#else
#define	ALIGN_ATTR(x)
#endif

#if	_MSC_VER >= 1400
# define	DEPRECATED_FUNC(f)	__declspec(deprecated) f
# include <sal.h>
# if	_MSC_VER > 1400
#  define	PRINTF_FORMAT(f)	_Printf_format_string_ f
# else	/* _MSC_VER == 1400 */
#  define	PRINTF_FORMAT(f)	__format_string f
# endif /* FORMAT_STRING */
#else	/* _MSC_VER < 1400 */
# define	PRINTF_FORMAT(f)	f
#endif	/* _MSC_VER */

#ifndef	BSWAP32
#define	BSWAP16(x)	\
	((((x) & 0xff00u) >> 8) | \
	(((x) & 0x00ffu) << 8))
#define	BSWAP32(x)	\
	((((x) & 0xff000000u) >> 24) | \
	(((x) & 0x00ff0000u) >> 8) | \
	(((x) & 0x0000ff00u) << 8) | \
	(((x) & 0x000000ffu) << 24))
#define	BSWAP64(x)	\
	((((x) & 0x00000000000000ffllu) >> 56) | \
	(((x) & 0x000000000000ff00llu) << 40) | \
	(((x) & 0x0000000000ff0000llu) << 24) | \
	(((x) & 0x00000000ff000000llu) << 8) | \
	(((x) & 0x000000ff00000000llu) >> 8) | \
	(((x) & 0x0000ff0000000000llu) >> 24) | \
	(((x) & 0x00ff000000000000llu) >> 40) | \
	(((x) & 0xff00000000000000llu) << 56))
#endif	/* BSWAP32 */
#endif	/* !__GNUC__ && !__clang__ */

#if	__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define	BE64(x)	(x)
#define	BE32(x)	(x)
#define	BE16(x)	(x)
#else	/* __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__ */
#define	BE64(x)	BSWAP64(x)
#define	BE32(x)	BSWAP32(x)
#define	BE16(x)	BSWAP16(x)
#endif	/* __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__ */

#define	DESTROY(x)	do { free(x); (x) = NULL; } while (0)
#define	DESTROY_OP(var, zero_val, destroy_op) \
	do { \
		if ((var) != (zero_val)) { \
			destroy_op; \
			(var) = (zero_val); \
		} \
	} while (0)

#ifdef	WINDOWS
#define	PATHSEP	"\\"
#else
#define	PATHSEP	"/"
#endif

/* Minimum/Maximum allowable elevation AMSL of anything */
#define	MIN_ELEV	-2000.0
#define	MAX_ELEV	30000.0

/* Minimum/Maximum allowable altitude AMSL of anything */
#define	MIN_ALT		-2000.0
#define	MAX_ALT		100000.0

/* Maximum valid speed of anything */
#define	MAX_SPD		1000.0

/* Minimum/Maximum allowable arc radius on any procedure */
#define	MIN_ARC_RADIUS	0.1
#define	MAX_ARC_RADIUS	100.0

/*
 * Compile-time assertion. The condition 'x' must be constant.
 * TYPE_ASSERT is a compile-time assertion, but which checks that
 * the type of `x' is `type'.
 */
#if	__STDC_VERSION__ >= 201112L && \
    (!defined(__GNUC__) || __GNUC__ > 7 || defined(__clang__))
#define	CTASSERT(x) _Static_assert((x), #x)
#define	TYPE_ASSERT(x, type) \
	CTASSERT(_Generic((x), type: 1, default: 0) != 0)
#define	NOT_TYPE_ASSERT(x, type) \
	CTASSERT(_Generic((x), type: 0, default: 1) != 0)
#else	/* __STDC_VERSION__ < 201112L */
#define	TYPE_ASSERT(x, type)
#define	NOT_TYPE_ASSERT(x, type)
#if	defined(__GNUC__) || defined(__clang__)
#define	CTASSERT(x)		_CTASSERT(x, __LINE__)
#define	_CTASSERT(x, y)		__CTASSERT(x, y)
#define	__CTASSERT(x, y)	\
	typedef char __compile_time_assertion__ ## y [(x) ? 1 : -1] \
	    __attribute__((unused))
#else	/* !defined(__GNUC__) && !defined(__clang__) */
#define	CTASSERT(x)
#endif	/* !defined(__GNUC__) && !defined(__clang__) */
#endif	/* __STDC_VERSION__ < 201112L */

#if	defined(__GNUC__) || defined(__clang__)

#define	highbit64(x)	(64 - __builtin_clzll(x) - 1)
#define	highbit32(x)	(32 - __builtin_clzll(x) - 1)

#elif  defined(_MSC_VER)

static inline unsigned
highbit32(unsigned int x)
{
	unsigned long idx;
	_BitScanReverse(&idx, x);
	return (idx);
}

static inline unsigned
highbit64(unsigned long long x)
{
	unsigned long idx;
	_BitScanReverse64(&idx, x);
	return (idx);
}
#else
#error	"Compiler platform unsupported, please add highbit definition"
#endif

#ifndef	MIN
#define	MIN(x, y)	((x) < (y) ? (x) : (y))
#endif
#ifndef	MAX
#define	MAX(x, y)	((x) > (y) ? (x) : (y))
#endif
#ifndef	AVG
#define	AVG(x, y)	(((x) + (y)) / 2.0)
#endif
#if	defined(_MSC_VER)
#define	FILTER_IN_TYPE(x)	double
#else
#define	FILTER_IN_TYPE(x)	__typeof__(x)
#endif
/*
 * Provides a gradual method of integrating an old value until it approaches
 * a new target value. This is used in iterative processes by calling the
 * FILTER_IN macro repeatedly a certain time intervals (d_t = delta-time).
 * As time progresses, old_val will gradually be made to approach new_val.
 * The lag serves to make the approach slower or faster (e.g. a value of
 * '2' and d_t in seconds makes old_val approach new_val with a ramp that
 * is approximately 2 seconds long).
 */
#define	FILTER_IN(old_val, new_val, d_t, lag) \
	do { \
		FILTER_IN_TYPE(old_val) o = (old_val); \
		FILTER_IN_TYPE(new_val) n = (new_val); \
		ASSERT(!isnan(o)); \
		(old_val) += (n - o) * ((d_t) / (lag)); \
		/* Prevent an overshoot */ \
		if ((o < n && (old_val) > n) || \
		    (o > n && (old_val) < n)) \
			(old_val) = n; \
	} while (0)
/*
 * Same as FILTER_IN, but handles NAN values for old_val and new_val properly.
 * If new_val is NAN, old_val is set to NAN. Otherwise if old_val is NAN,
 * it is set to new_val directly (without gradual filtering). Otherwise this
 * simply calls the FILTER_IN macro as normal.
 */
#define	FILTER_IN_NAN(old_val, new_val, d_t, lag) \
	do { \
		FILTER_IN_TYPE(old_val) o = (old_val); \
		FILTER_IN_TYPE(new_val) n = (new_val); \
		if (isnan(n)) \
			(old_val) = NAN; \
		else if (isnan(o)) \
			(old_val) = (new_val); \
		else \
			FILTER_IN(old_val, new_val, d_t, lag); \
	} while (0)

/*
 * Linearly interpolates old_val until it is equal to tgt. The current
 * time delta is d_t (in seconds). The interpolation speed is step/second.
 */
#define	FILTER_IN_LIN(old_val, tgt, d_t, step) \
	do { \
		double o = (old_val); \
		double t = (tgt); \
		double s; \
		if (isnan(o)) \
			o = t; \
		if (o < t) \
			s = (d_t) * (step); \
		else \
			s = (d_t) * (-(step)); \
		if ((o <= t && o + s > t) || (o >= t && o + s < t)) \
			(old_val) = t; \
		else \
			(old_val) += s; \
	} while (0)

#define	SCANF_STR_AUTOLEN_IMPL(_str_)	#_str_
#define	SCANF_STR_AUTOLEN(_str_)	SCANF_STR_AUTOLEN_IMPL(_str_)

/**
 * \def REQ_PTR
 * Marks a pointer argument as a required (i.e. non-NULL'able). This uses
 * C11's `[static 1]` syntax to declare a pointer, which must point to
 * allocated memory. The way to use this macro is as follows:
 * ```
 * int foo(object_t REQ_PTR(arg));
 * ```
 * This declares a pointer argument named `arg` to be non-NULL'able. When
 * compiling for C11 or higher, this expands into:
 * ```
 * int foo(object_t arg[static 1]);
 * ```
 * The code in the function can then manipulate this pointer as more-or-less
 * a normal pointer, with the exception that you may not call `free()` on it,
 * as that violates the guarantee that the pointer always point to allocated
 * memory. The code may, however, freely read and write to the object. There
 * is an additional constraint, in that the compiler now treats this as a
 * pointer to a single object, rather than an indexable array. To declare a
 * non-NULL'able pointer to an array of known size, use the REQ_ARR() macro.
 *
 * If the caller doesn't support C11 constructs (e.g. it is C++ or Rust),
 * this macro expands to a plain pointer, so this safety aspect is only
 * available for C11 or newer code.
 *
 * Please also note that this doesn't guarantee that there is no combination
 * of factors possible where this pointer may be NULL even in pure C11 code.
 * If the caller is simply passing through a plain pointer from their own
 * arguments (the caller function just serving as an intermediary), then the
 * ultimate origin of the pointer might still have passed NULL. For this
 * guarantee to hold fully, all code up the stack would need to support C11
 * constructs and/or provide rubust integrity checks against stray NULLs.
 */
#ifndef	REQ_PTR
#if	__STDC_VERSION__ >= 201112L
#define	REQ_PTR(x)	x[static 1]
#else
#define	REQ_PTR(x)	*x
#endif
#endif	// !defined(REQ_PTR)

/**
 * \def REQ_ARR
 * Same as REQ_PTR(), except allows specifying a number of elements in a
 * fixed-size array. This will allow you to treat the pointer as an array
 * while letting the compiler perform static bounds checking where possible.
 * If the caller passes a fixed-size array, the compiler will also perform
 * bounds checking on the calling side.
 *
 * The way to use this macro is as follows:
 * ```
 * int foo(object_t REQ_ARR(arg, 5));
 * ```
 * This declares a pointer argument named `arg` to be a non-NULL'able
 * reference to an array of at least 5 elements. When compiling for C11
 * or higher, this expands into:
 * ```
 * int foo(object_t arg[static 5]);
 * ```
 * Please note that this doesn't imply any kind of runtime bounds checks.
 * If you use a runtime variable to perform array indexing, this is still
 * not guaranteed to catch out-of-bounds array access bugs.
 *
 * Another method to use this macro is to pass a dynamic size, such as:
 * ```
 * int foo(size_t n, object_t REQ_ARR(arg, n));
 * ```
 * This lets the callee accept variable-length arrays, while still letting
 * the caller perform compile-time bounds checks, such that a mismatched
 * size argument and actual static array size can be detected and flagged.
 * Once again, this doesn't provide dynamic bounds checks, so its function
 * is simply to serve as additional type annotations for checks which can
 * be performed at compile time.
 */
#ifndef	REQ_ARR
#if	__STDC_VERSION__ >= 201112L
#define	REQ_ARR(x, n)	x[static (n)]
#else
#define	REQ_ARR(x)	x[n]
#endif
#endif	// !defined(REQ_ARR)

/**
 * \def ENUM_BIT_WIDTH_CHECK
 * \brief Compile-time macro for checking whether an enum can be represented
 *	in a fixed number of bits.
 *
 * This macro lets you perform a simple compile-time check to validate that
 * all variants of an enum fit into a fixed bit space representation. This
 * way you can validate that no variant accidentally exceeds the amount of
 * bit space allocated in a bit field.
 *
 * This macro is used in conjunction with the ENUM_BIT_WIDTH_CHECK_VARIANT
 * macro as follows:
 * ```
 * enum foo {
 *     foo_a,
 *     foo_b,
 *     foo_c,
 *     foo_d,
 * };
 * uint8_t encode_enum_foo(enum foo bar) {
 *      // check to make sure all of enum foo fits into 2 bits
 *      ENUM_BIT_WIDTH_CHECK(enum foo, 2,
 *          ENUM_BIT_WIDTH_CHECK_VARIANT(foo_a);
 *          ENUM_BIT_WIDTH_CHECK_VARIANT(foo_b);
 *          ENUM_BIT_WIDTH_CHECK_VARIANT(foo_c);
 *          ENUM_BIT_WIDTH_CHECK_VARIANT(foo_d);
 *      );
 * }
 * ```
 * This macro performs no runtime computation and thus is complete inert
 * from a runtime standpoint. All checking is performed at compile time.
 * While you must manually list all enum variants in the macro to perform
 * all checks, your compiler should warn you about unhandled variants, in
 * case you forgot some, or added some variants later.
 */
#ifndef	ENUM_BIT_WIDTH_CHECK
#define	ENUM_BIT_WIDTH_CHECK(enum_type, num_bits, ...) \
	do { \
		enum_type __enum_check_value__ = 0; \
		CTASSERT(num_bits > 0); \
		enum { __enum_check_max_value__ = (1 << (num_bits)) - 1 }; \
		switch (__enum_check_value__) { \
		    __VA_ARGS__ \
		} \
	} while (0)
#define	ENUM_BIT_WIDTH_CHECK_VARIANT(variant_name) \
	case variant_name: { \
		CTASSERT((int)variant_name >= 0); \
		CTASSERT((unsigned long long)variant_name <= \
		    (unsigned long long)__enum_check_max_value__); \
	    } \
	    break
#endif	// !defined(ENUM_BIT_WIDTH_CHECK)

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_SYSMACROS_H_ */
