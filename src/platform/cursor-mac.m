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

#include <errno.h>

#import <AppKit/NSCursor.h>
#import <AppKit/NSImage.h>

#include "acfutils/cursor.h"
#include "acfutils/helpers.h"
#include "acfutils/safe_alloc.h"

struct cursor_s {
	NSCursor	*crs;
};

cursor_t *
cursor_read_from_file(const char *filename_png)
{
	cursor_t *cursor = safe_calloc(1, sizeof (*cursor));
	NSString *path;
	NSImage *img;
	NSImageRep *rep;
	NSPoint p;

	ASSERT(filename_png != NULL);
	path = [NSString stringWithUTF8String: filename_png];
	img = [[NSImage alloc] initWithContentsOfFile: path];
	if (img == nil) {
		logMsg("Can't open image %s: %s", filename_png,
		    strerror(errno));
		free(cursor);
		return (NULL);
	}
	rep = [[img representations] objectAtIndex: 0];
	ASSERT(rep != nil);
	p = NSMakePoint([rep pixelsWide] / 2, [rep pixelsHigh] / 2);
	cursor->crs = [[NSCursor alloc] initWithImage: img hotSpot: p];
	ASSERT(cursor->crs != NULL);
	[img release];

	return (cursor);
}

void
cursor_free(cursor_t *cursor)
{
	ASSERT(cursor != NULL);
	ASSERT(cursor->crs != NULL);
	[cursor->crs release];
	free(cursor);
}

void
cursor_make_current(cursor_t *cursor)
{
	ASSERT(cursor != NULL);
	ASSERT(cursor->crs != NULL);
	[cursor->crs set];
}
