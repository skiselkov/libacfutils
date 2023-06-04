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
 * This file contains various utility and helper functions for cairo graphics.
 */

#ifndef	_ACF_UTILS_CAIRO_UTILS_H_
#define	_ACF_UTILS_CAIRO_UTILS_H_

#include <cairo.h>

#include "core.h"

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * If `surf` is not NULL, calls cairo_surface_destroy() on it and
 * resets the `surf` pointer to NULL afterwards.
 */
#define	CAIRO_SURFACE_DESTROY(surf) \
	do {\
		if ((surf) != NULL) { \
			cairo_surface_destroy((surf)); \
			(surf) = NULL; \
		} \
	} while (0)

/** For backwards compatibility with legacy apps. */
#define	mt_cairo_render_rounded_rectangle	cairo_utils_rounded_rect
/**
 * Appends a rounded rectangle to the current path. In essence operates
 * exactly as cairo_rectangle(), except the corners can be rounded over.
 * @param x X coordinate origin of the rectangle.
 * @param y Y coordinate origin of the rectangle.
 * @param w Width of rectangle.
 * @param h Height of rectangle.
 * @param radius Radius of the rounded arcs comprising the corners.
 */
API_EXPORT void cairo_utils_rounded_rect(cairo_t *cr, double x, double y,
    double w, double h, double radius);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_CAIRO_UTILS_H_ */
