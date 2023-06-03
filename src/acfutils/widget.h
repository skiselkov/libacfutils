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
 * Copyright 2021 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACFUTILS_WIDGET_H_
#define	_ACFUTILS_WIDGET_H_

#include <XPLMDisplay.h>
#include <XPWidgets.h>

#include "delay_line.h"
#include "mt_cairo_render.h"
#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct tooltip_set tooltip_set_t;

typedef struct {
	XPLMWindowID	win;
	unsigned	norm_w;
	unsigned	norm_h;
	double		w_h_ratio;
	int		left;
	int		top;
	int		right;
	int		bottom;
	delay_line_t	snap_hold_delay;
} win_resize_ctl_t;

typedef struct {
	int		left;
	int		top;
	int		right;
	int		bottom;
} monitor_t;

#define	create_widget_rel	ACFSYM(create_widget_rel)
API_EXPORT XPWidgetID create_widget_rel(int x, int y, bool_t y_from_bottom,
    int width, int height, int visible, const char *descr, int root,
    XPWidgetID container, XPWidgetClass cls);

#define	create_widget_rel2	ACFSYM(create_widget_rel2)
API_EXPORT XPWidgetID create_widget_rel2(int x, int y, bool_t y_from_bottom,
    int width, int height, int visible, const char *descr, int root,
    XPWidgetID container, XPWidgetID coord_ref, XPWidgetClass cls);

#define	widget_win_center	ACFSYM(widget_win_center)
API_EXPORT void widget_win_center(XPWidgetID window);
#define	classic_win_center	ACFSYM(classic_win_center)
API_EXPORT void classic_win_center(XPLMWindowID window);
API_EXPORT monitor_t lacf_get_first_monitor_bounds(void);

API_EXPORT void tooltip_init(void);
API_EXPORT void tooltip_fini(void);

API_EXPORT tooltip_set_t *tooltip_set_new(XPWidgetID window);
API_EXPORT tooltip_set_t *tooltip_set_new_native(XPLMWindowID window);
API_EXPORT void tooltip_set_orig_win_size(tooltip_set_t *tts,
    unsigned orig_w, unsigned orig_h);
API_EXPORT void tooltip_set_delay(tooltip_set_t *set, double secs);
API_EXPORT void tooltip_set_destroy(tooltip_set_t *tts);
API_EXPORT void tooltip_set_opaque(tooltip_set_t *tts, bool_t opaque);

API_EXPORT void tooltip_new(tooltip_set_t *tts, int x, int y, int w, int h,
    const char *text);

API_EXPORT void tooltip_set_font_face(tooltip_set_t *tts,
    cairo_font_face_t *font);
API_EXPORT cairo_font_face_t *tooltip_get_font_face(const tooltip_set_t *tts);

API_EXPORT void tooltip_set_font_size(tooltip_set_t *tts, double size);
API_EXPORT double tooltip_get_font_size(const tooltip_set_t *tts);

#define	window_follow_VR	ACFSYM(window_follow_VR)
API_EXPORT bool_t window_follow_VR(XPLMWindowID win);

#define	widget_follow_VR	ACFSYM(widget_follow_VR)
API_EXPORT bool_t widget_follow_VR(XPWidgetID win);

#define	window_is_on_screen	ACFSYM(window_is_on_screen)
API_EXPORT bool_t window_is_on_screen(XPLMWindowID win);

/*
 * These define an automatic window resizing controller that keeps the
 * aspect ratio of the window constant. This is useful for windows where
 * the contents might get squashed in undesirable ways.
 *
 * Use win_resize_ctl_init to initialize the controller, passing the
 * window handle and the window's "normal" width and height in boxels.
 * The controller will store the aspect ratio of these two numbers, as
 * well as the normal sizes (providing automatic snapping to normal size
 * when the user resizes the window to nearly its normal size).
 *
 * The normal size values MUST be greater than 10 boxels. Please note
 * that the controller automatically sets the window resizing limits to
 * between 10% and 1000% of its normal size. If this isn't desired,
 * change the window's resizing limits after calling win_resize_ctl_init.
 * Do NOT, however, allow the window to resize down to zero size, or
 * a divide-by-zero will occur.
 */
#define	win_resize_ctl_init	ACFSYM(win_resize_ctl_init)
API_EXPORT void win_resize_ctl_init(win_resize_ctl_t *ctl, XPLMWindowID win,
    unsigned norm_w, unsigned norm_h);
/*
 * Call win_resize_ctl_update in your window drawing function.
 * The controller will check the window for proper aspect ratio
 * and/or snapping to its normal size and performs window
 * geometry changes as necessary.
 */
#define	win_resize_ctl_update	ACFSYM(win_resize_ctl_update)
API_EXPORT void win_resize_ctl_update(win_resize_ctl_t *ctl);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACFUTILS_WIDGET_H_ */
