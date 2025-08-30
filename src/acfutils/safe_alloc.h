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
/** \file */

#ifndef	_ACF_UTILS_SAFE_ALLOC_H_
#define	_ACF_UTILS_SAFE_ALLOC_H_

#include <errno.h>
#include <string.h>
#include <stdlib.h>

#if	IBM
#include <malloc.h>
#endif

#if	defined(__cplusplus) && __cplusplus >= 201703L
#include <cstdlib>
#endif

#include "assert.h"
#include "sysmacros.h"

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Provides a front-end to your compiler's malloc() function, but with
 * automated allocation success checking. In general, you can just replace
 * any calls to malloc() with calls to safe_malloc() and it will work
 * exactly the same, except in cases where the allocation fails. If the
 * allocation fails, this generates an assertion failure crash with a
 * diagnostic message telling you how many byte failed to be allocated.
 *
 * N.B. unlike any allocation that you might get returned from within
 * libacfutils, which you should free using lacf_free(), the pointer
 * returned from this function is allocated your compiler's allocator.
 * Thus you MUST use your normal free() function to free this one.
 */
static inline void *
safe_malloc(size_t size)
{
	void *p = malloc(size);
	if (size > 0) {
		VERIFY_MSG(p != NULL, "Cannot allocate %lu bytes: "
		    "out of memory", (long unsigned)size);
	}
	return (p);
}

/**
 * Same as safe_malloc(), except it calls calloc() on the back-end and
 * behaves exactly the standard calloc() function.
 */
static inline void *
safe_calloc(size_t nmemb, size_t size)
{
	void *p = calloc(nmemb, size);
	if (nmemb > 0 && size > 0) {
		VERIFY_MSG(p != NULL, "Cannot allocate %lu bytes: "
		    "out of memory", (long unsigned)(nmemb * size));
	}
	return (p);
}

/**
 * Same as safe_malloc(), except it calls realloc() on the back-end and
 * behaves exactly the standard realloc() function.
 */
static inline void *
safe_realloc(void *oldptr, size_t size)
{
	void *p = realloc(oldptr, size);
	if (size > 0) {
		VERIFY_MSG(p != NULL, "Cannot allocate %lu bytes: "
		    "out of memory", (long unsigned)size);
	}
	return (p);
}

#if	!APL || __MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_15
#if	IBM || __STDC_VERSION__ >= 201112L || __cplusplus >= 201703L || \
    _POSIX_C_SOURCE >= 200112L || defined(__DOXYGEN__)

/**
 * Same as safe_malloc(), but allows you to specify an alignment requirement
 * for the buffer.
 *
 * @param alignment The byte alignment of the buffer. This must be a
 *	power-of-two and no smaller than `sizeof (void *)`.
 *
 * @return If successful, the returned address is guaranteed to be aligned
 *	to multiples of `alignment` bytes. The contents of the memory buffer
 *	are undefined. The returned pointer must be passed to aligned_free()
 *	to dispose of the allocation and avoid leaking memory.
 * @note You must NOT use the normal C library free() function, as doing so
 *	is NOT portable.
 * @return If the memory allocation cannot be satisfied, this function
 *	triggers an assertion failure with an out-of-memory error.
 * @note If the requested buffer size is 0, this function may return `NULL`,
 *	or an unusable non-`NULL` pointer which is safe to pass to free().
 */
static inline void *
safe_aligned_malloc(size_t alignment, size_t size)
{
	void *p = NULL;
	int err = 0;
	ASSERT3U(alignment, >=, sizeof (void *));
	ASSERT0(alignment ^ (1 << highbit64(alignment)));
#if	IBM
	p = _aligned_malloc(size, alignment);
	if (size > 0 && p == NULL)
		err = ENOMEM;
#elif	__STDC_VERSION__ >= 201112L
	p = aligned_alloc(alignment, size);
	if (size > 0 && p == NULL)
		err = ENOMEM;
#elif	__cplusplus >= 201703L
	p = std::aligned_alloc(alignment, size);
	if (size > 0 && p == NULL)
		err = ENOMEM;
#elif	defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
	err = posix_memalign(&p, alignment, size);
#else	/* !(__STDC_VERSION__ >= 201112L || _POSIX_C_SOURCE >= 200112L) */
	VERIFY_FAIL();
#endif	/* !(__STDC_VERSION__ >= 201112L || _POSIX_C_SOURCE >= 200112L) */
	VERIFY_MSG(err == 0, "Cannot allocate %lu bytes (align %lu): %s",
	    (long unsigned)size, (long unsigned)alignment, strerror(err));

	return (p);
}

/**
 * Same as safe_calloc(), but allows you to specify an alignment requirement
 * for the buffer.
 *
 * @param alignment The byte alignment of the buffer. This must be a
 *	power-of-two and no smaller than `sizeof (void *)`.
 *
 * @return If successful, the returned address is guaranteed to be aligned
 *	to multiples of `alignment` bytes. The contents of the memory buffer
 *	are zero-initialized. The returned pointer must be passed to
 *	aligned_free() to dispose of the allocation and avoid leaking memory.
 * @note You must NOT use the normal C library free() function, as doing so
 *	is NOT portable.
 * @return If the memory allocation cannot be satisfied, this function
 *	triggers an assertion failure with an out-of-memory error.
 * @note If the requested buffer size is 0, this function may return `NULL`,
 *	or an unusable non-`NULL` pointer which is safe to pass to free().
 */
static inline void *
safe_aligned_calloc(size_t alignment, size_t nmemb, size_t size)
{
	void *p = safe_aligned_malloc(alignment, nmemb * size);
	memset(p, 0, size);
	return (p);
}

/**
 * Frees memory previously allocated using safe_aligned_malloc() or
 * safe_aligned_calloc().
 * @note You must NOT use the normal C library free() function, as doing so
 *	is NOT portable.
 */
static inline void
aligned_free(void *ptr)
{
#if	IBM
	_aligned_free(ptr);
#else
	free(ptr);
#endif
}

#endif \
/* __STDC_VERSION__ || __cplusplus || IBM || _POSIX_C_SOURCE || __DOXYGEN__ */

#endif	/* !APL || CMAKE_OSX_DEPLOYMENT_TARGET >= MAC_OS_X_VERSION_10_15 */

/**
 * Provides an allocation-safe version of strdup(). If the allocation
 * of the required number of bytes fails, this trips an assertion check
 * and causes the application to crash due to having run out of memory.
 */
static inline char *
safe_strdup(const char *str2)
{
	char *str;
	size_t l;
	ASSERT(str2 != NULL);
	l = strlen(str2);
	str = (char *)safe_malloc(l + 1);
	memcpy(str, str2, l);
	str[l] = '\0';
	return (str);
}

/**
 * Concatenates `str` onto the end of `buf`, enlarging it as necessary.
 * If memory cannot be allocated to hold the new string, this crashes
 * with an assertion failure. This is using the safe_malloc() machinery
 * underneath, so all the same rules apply.
 * @param buf Buffer to append to. If this argument is `NULL`, a new
 *	buffer is allocated. Otherwise, the buffer is safe_realloc()d to
 *	contain the new concatenated string.
 * @param str The input string to append onto the end of `buf`. This must
 *	NOT be `NULL`.
 * @return The new combined buffer holding a concatenation of `buf` and
 *	`str`. Please note that since reallocation may occur, you must not
 *	reuse the old `buf` pointer value after calling safe_append_realloc().
 *	You should reassign the pointer using the return value of this
 *	function.
 */
static inline char *
safe_append_realloc(char *buf, const char *str)
{
	char *newbuf;

	ASSERT(str != NULL);
	if (buf == NULL)
		return (safe_strdup(str));
	newbuf = (char *)safe_realloc(buf, strlen(buf) + strlen(str) + 1);
	memcpy(&newbuf[strlen(newbuf)], str, strlen(str) + 1);
	return (newbuf);
}

/**
 * If `ptr` is not a `NULL` pointer, the contained data is first zeroed,
 * and subsequently deallocated using free(). This is useful for making
 * sure nothing remains of a buffer after freeing, thus preventing
 * potentially attempting to read its contents and seeing valid data
 * (use-after-free).
 *
 * N.B. this macro relies on `sizeof` returning the correct size of this
 * data buffer, so it should only be used on single struct buffers, not
 * arrays.
 */
#define	ZERO_FREE(ptr) \
	do { \
		NOT_TYPE_ASSERT(ptr, void *); \
		NOT_TYPE_ASSERT(ptr, char *); \
		if ((ptr) != NULL) \
			memset((ptr), 0, sizeof (*(ptr))); \
		free(ptr); \
	} while (0)
/**
 * Same as ZERO_FREE(), but also sets `ptr` to NULL after freeing.
 */
#define	DESTROY_FREE(ptr) \
	do { \
		ZERO_FREE(ptr); \
		(ptr) = NULL; \
	} while (0)

/**
 * Same as \ref ZERO_FREE, but takes an explicit array element count
 * argument. This is the variant of the \ref ZERO_FREE macro to be used
 * on arrays of objects, rather than single objects.
 */
#define	ZERO_FREE_N(ptr, num) \
	do { \
		NOT_TYPE_ASSERT(ptr, void *); \
		if ((ptr) != NULL) \
			memset((ptr), 0, sizeof (*(ptr)) * (num)); \
		free(ptr); \
	} while (0)
/**
 * Same as ZERO_FREE_N(), but also sets `ptr` to NULL after freeing.
 */
#define	DESTROY_FREE_N(ptr, num) \
	do { \
		ZERO_FREE_N((ptr), (num)); \
		(ptr) = NULL; \
	while (0)

/**
 * Performs a zeroing of the `data` buffer. Please note that `sizeof`
 * must return the correct size of the data to be zeroed. Thus this
 * is intended to be used fixed-size arrays.
 */
#define	BZERO(data)	memset((data), 0, sizeof (*(data)))

/**
 * If `data` is not `NULL`, performs a \ref BZERO on the data.
 */
#define	SAFE_BZERO(data) \
	do { \
		if ((data) != NULL) \
			BZERO(data); \
	} while (0)

#if	!defined(BOX_NEW) && \
    (defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER) || \
    __STDC_VERSION__ >= 202311L)

#if	__STDC_VERSION__ >= 202311L
// C23 supports the `typeof()` operator as a built-in feature
#define	BOX_NEW(...) \
	(typeof(__VA_ARGS__) *)box_new_impl((void *)&(__VA_ARGS__), \
	    sizeof (__VA_ARGS__))
#else	// __STDC_VERSION__ < 202311L
// Fallback to the GCC/Clang/MSVC __typeof__ operator.
#define	BOX_NEW(...) \
	(__typeof__(__VA_ARGS__) *)box_new_impl((void *)&(__VA_ARGS__), \
	    sizeof (__VA_ARGS__))
#endif	// __STDC_VERSION__ < 202311L

static inline void *
box_new_impl(void *value, size_t value_sz) {
	void *ptr = safe_malloc(value_sz);
	memcpy(ptr, value, value_sz);
	return (ptr);
}

#endif	// !defined(BOX_NEW) &&
	// (defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER) ||
	// __STDC_VERSION__ >= 202311L)

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_SAFE_ALLOC_H_ */
