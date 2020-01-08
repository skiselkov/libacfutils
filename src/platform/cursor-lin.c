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

#include <X11/Xcursor/Xcursor.h>

#include "acfutils/cursor.h"
#include "acfutils/dr.h"
#include "acfutils/helpers.h"
#include "acfutils/png.h"
#include "acfutils/safe_alloc.h"

struct cursor_s {
	Display		*dpy;
	Cursor		crs;
};

cursor_t *
cursor_read_from_file(const char *filename_png)
{
	cursor_t *cursor = safe_calloc(1, sizeof (*cursor));
	uint8_t *buf;
	int w, h;
	XcursorImage img = { .pixels = NULL };

	ASSERT(filename_png != NULL);

	cursor->dpy = XOpenDisplay(NULL);
	if (cursor->dpy == NULL) {
		logMsg("Can't open display");
		free(cursor);
		return (NULL);
	}
	buf = png_load_from_file_rgba(filename_png, &w, &h);

	if (buf == NULL) {
		free(cursor);
		return (NULL);
	}
	img.size = w;
	img.width = w;
	img.height = h;
	img.xhot = w / 2;
	img.yhot = h / 2;
	img.pixels = (XcursorPixel *)buf;
	cursor->crs = XcursorImageLoadCursor(cursor->dpy, &img);

	free(buf);

	return (cursor);
}

void
cursor_free(cursor_t *cursor)
{
	if (cursor == NULL)
		return;
	ASSERT(cursor->dpy != NULL);
	XFreeCursor(cursor->dpy, cursor->crs);
	XCloseDisplay(cursor->dpy);
	free(cursor);
}

void
cursor_make_current(cursor_t *cursor)
{
	dr_t system_window_dr;
	int win_ptr[2];
	Window win;

	ASSERT(cursor != NULL);
	ASSERT(cursor->dpy != NULL);
	fdr_find(&system_window_dr, "sim/operation/windows/system_window_64");
	VERIFY3S(dr_getvi(&system_window_dr, win_ptr, 0, 2), ==, 2);
	memcpy(&win, win_ptr, sizeof (void *));

	XDefineCursor(cursor->dpy, win, cursor->crs);
	XFlush(cursor->dpy);
}
