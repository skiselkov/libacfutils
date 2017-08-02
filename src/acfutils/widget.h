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
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACFUTILS_WIDGET_H_
#define	_ACFUTILS_WIDGET_H_

#include <XPWidgets.h>

#include <acfutils/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct tooltip_set tooltip_set_t;

XPWidgetID create_widget_rel(int x, int y, bool_t y_from_bottom,
    int width, int height, int visible, const char *descr, int root,
    XPWidgetID container, XPWidgetClass cls);

void tooltip_init(void);
void tooltip_fini(void);

tooltip_set_t *tooltip_set_new(XPWidgetID window);
void tooltip_set_destroy(tooltip_set_t *tts);

void tooltip_new(tooltip_set_t *tts, int x, int y, int w, int h,
    const char **lines);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACFUTILS_WIDGET_H_ */
