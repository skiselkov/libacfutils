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

static XPLMCommandRef
cmd_find_v_impl(const char *fmt, va_list ap, bool_t force)
{
	va_list ap2;
	int l;
	char *name;
	XPLMCommandRef ref;

	va_copy(ap2, ap);
	l = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);

	name = malloc(l + 1);
	vsnprintf(name, l + 1, fmt, ap);
	ref = XPLMFindCommand(name);
	if (ref == NULL && force)
		VERIFY_MSG(0, "Command \"%s\" not found", name);
	free(name);

	return (ref);
}

API_EXPORT XPLMCommandRef
cmd_find(const char *fmt, ...)
{
	va_list ap;
	XPLMCommandRef ref;

	va_start(ap, fmt);
	ref = cmd_find_v_impl(fmt, ap, B_FALSE);
	va_end(ap);

	return (ref);
}

API_EXPORT XPLMCommandRef
fcmd_find(const char *fmt, ...)
{
	va_list ap;
	XPLMCommandRef ref;

	va_start(ap, fmt);
	ref = cmd_find_v_impl(fmt, ap, B_TRUE);
	va_end(ap);

	return (ref);
}

API_EXPORT XPLMCommandRef
cmd_find_v(const char *fmt, va_list ap)
{
	return (cmd_find_v_impl(fmt, ap, B_FALSE));
}

API_EXPORT XPLMCommandRef
fcmd_find_v(const char *fmt, va_list ap)
{
	return (cmd_find_v_impl(fmt, ap, B_TRUE));
}

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
	VERIFY3S(vsnprintf(name, len + 1, fmt, ap), ==, len);

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

	if (ref == NULL) {
		char name[256];
		va_start(ap, refcon);
		vsnprintf(name, sizeof (name), fmt, ap);
		va_end(ap);
		VERIFY_MSG(0, "Command %s not found", name);
	}

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
	len = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);
	name = safe_malloc(len + 1);
	VERIFY3S(vsnprintf(name, len + 1, fmt, ap), ==, len);

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

	if (!res) {
		char name[256];
		va_start(ap, refcon);
		vsnprintf(name, sizeof (name), fmt, ap);
		va_end(ap);
		VERIFY_MSG(0, "Command %s not found", name);
	}
}
