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
 * Copyright 2022 Saso Kiselkov. All rights reserved.
 */

#include "acfutils/assert.h"
#include "acfutils/geom.h"
#include "acfutils/cairo_utils.h"

void
cairo_utils_rounded_rect(cairo_t *cr, double x, double y,
    double w, double h, double radius)
{
	ASSERT(cr != NULL);
	cairo_move_to(cr, x + radius, y);
	cairo_line_to(cr, x + w - radius, y);
	cairo_arc(cr, x + w - radius, y + radius, radius,
	    DEG2RAD(270), DEG2RAD(360));
	cairo_line_to(cr, x + w, y + h - radius);
	cairo_arc(cr, x + w - radius, y + h - radius, radius,
	    DEG2RAD(0), DEG2RAD(90));
	cairo_line_to(cr, x + radius, y + h);
	cairo_arc(cr, x + radius, y + h - radius, radius,
	    DEG2RAD(90), DEG2RAD(180));
	cairo_line_to(cr, x, y + radius);
	cairo_arc(cr, x + radius, y + radius, radius,
	    DEG2RAD(180), DEG2RAD(270));
}
