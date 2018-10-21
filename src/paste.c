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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <string.h>
#include <stdio.h>

#include <acfutils/assert.h>
#include <acfutils/helpers.h>
#include <acfutils/paste.h>

#if	IBM
#include <windows.h>
#elif	LIN
#include <libclipboard.h>
static clipboard_c *cb = NULL;
#endif

#if	IBM

bool_t
paste_init(void)
{
	return (B_TRUE);
}

void
paste_fini(void)
{
}

bool_t
paste_get_str(char *str, size_t cap)
{
	HANDLE h;

	ASSERT(str != NULL);
	if (!OpenClipboard(NULL))
		return (B_FALSE);
	h = GetClipboardData(CF_TEXT);
	if (h == NULL) {
		CloseClipboard();
		return (B_FALSE);
	}
	lacf_strlcpy(str, h, cap);
	CloseClipboard();

	return (B_TRUE);
}

bool_t
paste_set_str(const char *str)
{
	HGLOBAL h_result;
	char *glob_str;

	ASSERT(str != NULL);
	h_result = GlobalAlloc(GMEM_MOVEABLE, strlen(str) + 1);
	glob_str = GlobalLock(h_result);
	lacf_strlcpy(glob_str, str, strlen(str) + 1);
	GlobalUnlock(h_result);

	if (!OpenClipboard(NULL))
		return (B_FALSE);
	if (!EmptyClipboard()) {
		win_perror(GetLastError(), "Error clearing clipboard contents");
		CloseClipboard();
		return (B_FALSE);
	}
	if (SetClipboardData(CF_TEXT, h_result) == NULL) {
		win_perror(GetLastError(), "Error setting clipboard contents");
		GlobalFree(h_result);
	}
	CloseClipboard();

	return (B_TRUE);
}

#elif	APL

bool_t
paste_init(void)
{
	return (B_TRUE);
}

void
paste_fini(void)
{
}

bool_t
paste_get_str(char *str, size_t cap)
{
	FILE	*fp = popen("pbpaste", "r");
	size_t	n = 0;

	if (fp == NULL)
		return (B_FALSE);
	while (n + 1 < cap) {
		size_t b = fread(&str[n], 1, cap - 1, fp);

		if (b == 0)
			break;
		n += b;
	}
	str[n] = '\0';
	fclose(fp);

	return (B_TRUE);
}

bool_t
paste_set_str(const char *str)
{
	FILE	*fp = popen("pbcopy", "w");

	if (fp == NULL)
		return (B_FALSE);
	for (size_t n = 0, cap = strlen(str); n < cap;) {
		size_t b = fwrite(&str[n], 1, cap - n, fp);

		if (b == 0)
			break;
		n += b;
	}
	fclose(fp);

	return (B_TRUE);
}

#else	/* LIN */

bool_t
paste_init(void)
{
	ASSERT3P(cb, ==, NULL);
	cb = clipboard_new(NULL);
	return (cb != NULL);
}

void
paste_fini(void)
{
	if (cb != NULL) {
		clipboard_free(cb);
		cb = NULL;
	}
}

bool_t
paste_get_str(char *str, size_t cap)
{
	const char *text;

	if (cb == NULL)
		return (B_FALSE);
	text = clipboard_text(cb);
	if (text == NULL)
		return (B_FALSE);
	lacf_strlcpy(str, text, cap);

	return (B_TRUE);
}

bool_t
paste_set_str(const char *str)
{
	if (cb == NULL)
		return (B_FALSE);
	clipboard_set_text(cb, str);

	return (B_TRUE);
}

#endif	/* LIN */
