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
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */
/**
 * \file
 * Generic implementation to let you directly set the OS-provided mouse
 * cursor. For Mac & Linux, you need a PNG of your cursor. The cursor
 * image is assumed to be symmetrical with its hotspot in the center of
 * the image. For Windows, the provided filename's '.png' extension is
 * automatically replcated with '.cur' and you must provide a cursor
 * in Windows .cur cursor format. Simply place both the PNG and CUR
 * versions of the cursor side-by-side and the library will select the
 * correct one depending on the host platform.
 */

#ifndef	_ACF_UTILS_CURSOR_H_
#define	_ACF_UTILS_CURSOR_H_

#include "core.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct cursor_s cursor_t;

/**
 * Loads a cursor from a PNG file. The cursor hotspot is assumed to be
 * directly in the center of the image. On Windows, the PNG extension
 * is automatically replaced by '.cur', so you need to provide both a
 * PNG and CUR version of your cursor.
 *
 * You must free the returned cursor object using cursor_free().
 */
API_EXPORT cursor_t *cursor_read_from_file(const char *filename_png);

/**
 * Frees a cursor previously created using cursor_read_from_file().
 */
API_EXPORT void cursor_free(cursor_t *cursor);

/**
 * Sets the cursor to be the current cursor. Be sure to ask X-Plane not
 * to draw its own cursor. This function should be called in the window
 * mouse cursor handler callback.
 */
API_EXPORT void cursor_make_current(cursor_t *cursor);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_CURSOR_H_ */
