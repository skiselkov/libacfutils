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
 * Copyright 2018 Saso Kiselkov. All rights reserved.
 */

#include <stdio.h>

#include <acfutils/assert.h>
#include <acfutils/cmd.h>
#include <acfutils/safe_alloc.h>

API_EXPORT XPLMCommandRef
cmd_bind_v(const char *fmt, cmd_cb_t cb, bool_t before, void *refcon,
    va_list ap)
{
	XPLMCommandRef ref;
	va_list ap2;
	char *name;
	int len;

	va_copy(ap2, ap);
	len = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);
	name = safe_malloc(len + 1);

	ref = XPLMFindCommand(name);
	if (ref != NULL)
		XPLMRegisterCommandHandler(ref, cb, before, refcon);

	free(name);

	return (ref);
}

API_EXPORT XPLMCommandRef
cmd_bind(const char *fmt, cmd_cb_t cb, bool_t before, void *refcon, ...)
{
	va_list ap;
	XPLMCommandRef ref;

	va_start(ap, refcon);
	ref = cmd_bind_v(fmt, cb, before, refcon, ap);
	va_end(ap);

	return (ref);
}

API_EXPORT XPLMCommandRef
fcmd_bind(const char *fmt, cmd_cb_t cb, bool_t before, void *refcon, ...)
{
	va_list ap;
	XPLMCommandRef ref;

	va_start(ap, refcon);
	ref = cmd_bind_v(fmt, cb, before, refcon, ap);
	va_end(ap);

	VERIFY_MSG(ref != NULL, "Command %s not found", fmt);
	return (ref);
}

API_EXPORT bool_t
cmd_unbind_v(const char *fmt, cmd_cb_t cb, bool_t before, void *refcon,
    va_list ap)
{
	XPLMCommandRef ref;
	va_list ap2;
	char *name;
	int len;

	va_copy(ap2, ap);
	len = snprintf(NULL, 0, fmt, ap);
	va_end(ap2);
	name = safe_malloc(len + 1);

	ref = XPLMFindCommand(name);

	free(name);

	if (ref != NULL) {
		XPLMUnregisterCommandHandler(ref, cb, before, refcon);
		return (B_TRUE);
	}

	return (B_FALSE);
}

API_EXPORT bool_t
cmd_unbind(const char *fmt, cmd_cb_t cb, bool_t before, void *refcon, ...)
{
	va_list ap;
	bool_t res;

	va_start(ap, refcon);
	res = cmd_unbind_v(fmt, cb, before, refcon, ap);
	va_end(ap);

	return (res);
}

API_EXPORT void
fcmd_unbind(const char *fmt, cmd_cb_t cb, bool_t before, void *refcon, ...)
{
	va_list ap;
	bool_t res;

	va_start(ap, refcon);
	res = cmd_unbind_v(fmt, cb, before, refcon, ap);
	va_end(ap);

	VERIFY_MSG(res, "Command %s not found", fmt);
}
