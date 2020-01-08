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

#include <windows.h>

#include "acfutils/cursor.h"
#include "acfutils/helpers.h"
#include "acfutils/safe_alloc.h"

struct cursor_s {
	HCURSOR		crs;
};

cursor_t *
cursor_read_from_file(const char *filename_png)
{
	cursor_t *cursor = safe_calloc(1, sizeof (*cursor));
	char *filename_cur, *extension;

	ASSERT(filename_png != NULL);

	/*
	 * On Windows, we need to grab a cursor file (.cur), so substitute
	 * the path extension in the filename.
	 */
	filename_cur = safe_calloc(1, strlen(filename_png) + 8);
	strlcpy(filename_cur, filename_png, strlen(filename_png) + 8);
	extension = strrchr(filename_cur, '.');
	if (extension != NULL)
		strlcpy(extension, ".cur", 8);
	else
		strlcpy(&filename_cur[strlen(filename_cur)], ".cur", 8);
	cursor->crs = LoadCursorFromFileA(filename_cur);
	if (cursor->crs == NULL) {
		win_perror(GetLastError(), "Error loading cursor file %s",
		    filename_cur);
		free(cursor);
		free(filename_cur);
		return (NULL);
	}
	free(filename_cur);

	return (cursor);
}

void
cursor_free(cursor_t *cursor)
{
	if (cursor == NULL)
		return;
	ASSERT(cursor->crs != NULL);
	DestroyCursor(cursor->crs);
	free(cursor);
}

void
cursor_make_current(cursor_t *cursor)
{
	ASSERT(cursor != NULL);
	ASSERT(cursor->crs != NULL);
	SetCursor(cursor->crs);
}
