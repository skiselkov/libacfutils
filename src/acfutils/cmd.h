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
 * This file contains convenience functions to interact with X-Plane's
 * command handling machinery.
 */

#ifndef	_ACFUTILS_CMD_H_
#define	_ACFUTILS_CMD_H_

#include <stdarg.h>

#include <XPLMUtilities.h>

#include "sysmacros.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef int (*cmd_cb_t)(XPLMCommandRef ref, XPLMCommandPhase phase,
    void *refcon);

/**
 * Performs the equivalent of XPLMFindCommand(), but with printf-style
 * auto-formatting support for the command name.
 * @param fmt A printf-style format string that specifies the command
 *	name to search for. The remaining arguments must comply with the
 *	format string's format specifiers.
 * @return The XPLMCommandRef for the command, if it was found, otherwise
 *	`NULL`.
 * @see https://www.man7.org/linux/man-pages/man3/printf.3.html
 */
API_EXPORT XPLMCommandRef cmd_find(PRINTF_FORMAT(const char *fmt), ...)
    PRINTF_ATTR(1);
/**
 * Same as cmd_find(), but if the command doesn't exist, causes an
 * assertion failure ("f" prefix meaning "force").
 */
API_EXPORT XPLMCommandRef fcmd_find(PRINTF_FORMAT(const char *fmt), ...)
    PRINTF_ATTR(1);
/**
 * Same as cmd_find(), but takes a `va_list` for the format arguments,
 * instead of being variadic.
 */
API_EXPORT XPLMCommandRef cmd_find_v(const char *fmt, va_list ap);
/**
 * Same as fcmd_find(), but takes a `va_list` for the format arguments,
 * instead of being variadic.
 */
API_EXPORT XPLMCommandRef fcmd_find_v(const char *fmt, va_list ap);
/**
 * Performs a combination of an `XPLMFindCommand` followed by an
 * `XPLMRegisterCommandHandler`, with support for printf-style command
 * name construction.
 * @param fmt A printf-style format string that specifies the command
 *	name to search for. The variadic arguments must comply with the
 *	format string's format specifiers.
 * @param cb The callback to register for the command handler.
 * @param before Specifies whether the callback should be invoked before
 *	X-Plane handles the command, or after.
 * @param refcon Reference constant that will be passed by X-Plane to the
 *	command handler every time `cb` is called.
 * @return The XPLMCommandRef for the command, if it was found, otherwise
 *	`NULL`. If the command doesn't exist, no callback registration takes
 *	place.
 * @see https://www.man7.org/linux/man-pages/man3/printf.3.html
 */
API_EXPORT XPLMCommandRef cmd_bind(PRINTF_FORMAT(const char *fmt),
    cmd_cb_t cb, bool_t before, void *refcon, ...) PRINTF_ATTR2(1, 5);
/**
 * Same as cmd_bind(), but takes a `va_list` for the format arguments,
 * instead of being variadic.
 */
API_EXPORT XPLMCommandRef cmd_bind_v(const char *fmt, cmd_cb_t cb,
    bool_t before, void *refcon, va_list ap);
/**
 * Same as cmd_bind(), but if the command doesn't exist, causes an
 * assertion failure ("f" prefix meaning "force").
 */
API_EXPORT XPLMCommandRef fcmd_bind(const char *fmt,
    cmd_cb_t cb, bool_t before, void *refcon, ...) PRINTF_ATTR2(1, 5);
/**
 * Same as cmd_bind(), but instead of registering the callback,
 * unregisters it.
 */
API_EXPORT bool_t cmd_unbind(PRINTF_FORMAT(const char *fmt), cmd_cb_t cb,
    bool_t before, void *refcon, ...) PRINTF_ATTR2(1, 5);
/**
 * Same as cmd_bind_v(), but instead of registering the callback,
 * unregisters it.
 */
API_EXPORT bool_t cmd_unbind_v(const char *fmt, cmd_cb_t cb, bool_t before,
    void *refcon, va_list ap);
/**
 * Same as fcmd_bind(), but instead of registering the callback,
 * unregisters it.
 */
API_EXPORT void fcmd_unbind(PRINTF_FORMAT(const char *fmt), cmd_cb_t cb,
    bool_t before, void *refcon, ...) PRINTF_ATTR2(1, 5);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACFUTILS_CMD_H_ */
