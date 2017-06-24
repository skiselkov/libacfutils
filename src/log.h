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

#ifndef	_ACF_UTILS_LOG_H_
#define	_ACF_UTILS_LOG_H_

#include <stdarg.h>

#include "helpers.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This lets us chop out the basename (last path component) from __FILE__
 * at compile time. This works on GCC and Clang. The fallback mechanism
 * below just chops it out at compile time.
 */
#if	defined(__GNUC__) || defined(__clang__)
#define	log_basename(f) (__builtin_strrchr(f, BUILD_DIRSEP) ? \
	__builtin_strrchr(f, BUILD_DIRSEP) + 1 : f)
#else	/* !__GNUC__ && !__clang__ */
const char *log_basename(const char *filename);
#endif	/* !__GNUC__ && !__clang__ */

#define	logMsg(...) \
	log_impl(NULL, 0, log_basename(__FILE__), __LINE__, __VA_ARGS__)
void log_impl(const char *filename, int line, const char *fmt, ...) 
    PRINTF_ATTR(3);
void log_impl_v(const char *filename, int line, const char *fmt, va_list ap);
void log_backtrace(void);

#ifdef __cplusplus
}
#endif

#endif	/* _ACF_UTILS_LOG_H_ */
