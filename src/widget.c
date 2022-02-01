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
 * Copyright 2021 Saso Kiselkov. All rights reserved.
 */

#include <ctype.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#include <XPLMGraphics.h>
#include <XPLMDisplay.h>
#include <XPLMProcessing.h>
#include <XPStandardWidgets.h>

#include "acfutils/assert.h"
#include "acfutils/dr.h"
#include "acfutils/geom.h"
#include "acfutils/helpers.h"
#include "acfutils/list.h"
#include "acfutils/safe_alloc.h"
#include "acfutils/widget.h"
#include "acfutils/time.h"

enum {
	TOOLTIP_LINE_HEIGHT =	13,
	TOOLTIP_WINDOW_OFFSET =	15,
	TOOLTIP_WINDOW_MARGIN = 10,
	TOOLTIP_WINDOW_WIDTH = 450
};

#define	TOOLTIP_INTVAL		0.1
#define	DEFAULT_DISPLAY_DELAY	1	/* secs */

typedef struct {
	int		x, y, w, h;
	const char	*text;
	list_node_t	node;
} tooltip_t;

struct tooltip_set {
	XPLMWindowID	window;
	int		orig_w;
	int		orig_h;
	list_t		tooltips;
	list_node_t	node;
	double		display_delay;
};

#define	NUM_TOOLTIP_SUBWINDOWS	3

static list_t tooltip_sets;
static bool inited = false;
static tooltip_t *cur_tt = NULL;
static XPWidgetID cur_tt_subwin[NUM_TOOLTIP_SUBWINDOWS] = { NULL };
static XPWidgetID cur_tt_win = NULL;
static int last_mouse_x, last_mouse_y;
static uint64_t mouse_moved_time;

XPWidgetID
create_widget_rel(int x, int y, bool_t y_from_bottom, int width, int height,
    int visible, const char *descr, int root, XPWidgetID container,
    XPWidgetClass cls)
{
	return (create_widget_rel2(x, y, y_from_bottom, width, height, visible,
	    descr, root, container, container, cls));
}

XPWidgetID
create_widget_rel2(int x, int y, bool_t y_from_bottom, int width, int height,
    int visible, const char *descr, int root, XPWidgetID container,
    XPWidgetID coord_ref, XPWidgetClass cls)
{
	int wleft = 0, wtop = 0, wright = 0, wbottom = 0;
	int bottom, right;

	if (container != NULL) {
		XPGetWidgetGeometry(coord_ref, &wleft, &wtop, &wright,
		    &wbottom);
	} else {
		XPLMGetScreenSize(&wright, &wtop);
		if (!y_from_bottom && y + height > wtop)
			y = wtop - height;
		else if (y_from_bottom && y - height < 0)
			y = height;
		if (x + width > wright)
			x = wright - width;
	}

	x += wleft;
	if (!y_from_bottom) {
		y = wtop - y;
		bottom = y - height;
	} else {
		y = wbottom + y;
		bottom = y - height;
	}
	right = x + width;

	return (XPCreateWidget(x, y, right, bottom, visible, descr, root,
	    container, cls));
}

static void
find_first_monitor(int idx, int left, int top, int right, int bottom,
    void *refcon)
{
	monitor_t *mon = refcon;

	UNUSED(idx);

	if (mon->left == 0 && mon->right == 0 &&
	    mon->top == 0 && mon->bottom == 0) {
		mon->left = left;
		mon->right = right;
		mon->top = top;
		mon->bottom = bottom;
	}
}

monitor_t
lacf_get_first_monitor_bounds(void)
{
	monitor_t mon = {};

	memset(&mon, 0, sizeof (mon));

	XPLMGetAllMonitorBoundsGlobal(find_first_monitor, &mon);
	if (mon.left == 0 && mon.right == 0 && mon.top == 0 &&
	    mon.bottom == 0) {
		XPLMGetScreenBoundsGlobal(&mon.left, &mon.top, &mon.right,
		    &mon.bottom);
	}

	return (mon);
}

static void
center_window_coords(int *left, int *top, int *right, int *bottom)
{
	monitor_t mon = lacf_get_first_monitor_bounds();
	int width = (*right) - (*left);
	int height = (*top) - (*bottom);

	*left = (mon.right + mon.left - width) / 2;
	*right = (*left) + width;
	*bottom = (mon.bottom + mon.top - height) / 2;
	*top = (*bottom) + height;
}

API_EXPORT void
widget_win_center(XPWidgetID window)
{
	ASSERT(window != NULL);
	classic_win_center(XPGetWidgetUnderlyingWindow(window));
}

API_EXPORT void
classic_win_center(XPLMWindowID window)
{
	int left, top, right, bottom;
	ASSERT(window != NULL);
	XPLMGetWindowGeometry(window, &left, &top, &right, &bottom);
	center_window_coords(&left, &top, &right, &bottom);
	XPLMSetWindowGeometry(window, left, top, right, bottom);
}

tooltip_set_t *
tooltip_set_new(XPWidgetID window)
{
	return (tooltip_set_new_native(XPGetWidgetUnderlyingWindow(window)));
}

tooltip_set_t *
tooltip_set_new_native(XPLMWindowID window)
{
	int left, top, right, bottom;

	tooltip_set_t *tts = safe_malloc(sizeof (*tts));
	tts->window = window;
	list_create(&tts->tooltips, sizeof (tooltip_t),
	    offsetof(tooltip_t, node));
	list_insert_tail(&tooltip_sets, tts);
	XPLMGetWindowGeometry(window, &left, &top, &right, &bottom);
	tts->orig_w = right - left;
	tts->orig_h = top - bottom;
	tts->display_delay = DEFAULT_DISPLAY_DELAY;

	return (tts);
}

void
tooltip_set_orig_win_size(tooltip_set_t *tts, unsigned orig_w, unsigned orig_h)
{
	ASSERT(tts != NULL);
	ASSERT(orig_w != 0);
	ASSERT(orig_h != 0);
	tts->orig_w = orig_w;
	tts->orig_h = orig_h;
}

void
tooltip_set_delay(tooltip_set_t *set, double secs)
{
	ASSERT(set != NULL);
	set->display_delay = secs;
}

static void
destroy_cur_tt(void)
{
	ASSERT(cur_tt_subwin[0] != NULL);
	ASSERT(cur_tt_win != NULL);
	ASSERT(cur_tt != NULL);
	for (int i = 0; i < NUM_TOOLTIP_SUBWINDOWS; i++)
		XPDestroyWidget(cur_tt_subwin[i], 1);
	XPDestroyWidget(cur_tt_win, 1);
	memset(cur_tt_subwin, 0, sizeof (cur_tt_subwin));
	cur_tt_win = NULL;
	cur_tt = NULL;
}

void
tooltip_set_destroy(tooltip_set_t *tts)
{
	tooltip_t *tt;
	while ((tt = list_head(&tts->tooltips)) != NULL) {
		if (cur_tt == tt)
			destroy_cur_tt();
		list_remove(&tts->tooltips, tt);
		free(tt);
	}
	list_destroy(&tts->tooltips);
	list_remove(&tooltip_sets, tts);
	free(tts);
}

void
tooltip_new(tooltip_set_t *tts, int x, int y, int w, int h, const char *text)
{
	ASSERT(text != NULL);

	tooltip_t *tt = safe_malloc(sizeof (*tt));
	tt->x = x;
	tt->y = y;
	tt->w = w;
	tt->h = h;
	tt->text = text;
	list_insert_tail(&tts->tooltips, tt);
}

static inline const char *
find_whitespace(const char *p, const char *end)
{
	while (!isspace(*p) && p < end)
		p++;
	return (p);
}

static char **
auto_wrap_text(const char *text, double max_width, size_t *n_lines)
{
	char **lines = NULL;
	const char *start, *end;
	size_t n = 0;

	ASSERT(text != NULL);
	ASSERT(n_lines != NULL);

	start = text;
	end = text + strlen(text);

	for (const char *p = text, *sp_prev = text; p < end;) {
		const char *const sp_here = find_whitespace(p, end);
		double width = XPLMMeasureString(xplmFont_Proportional,
		    start, sp_here - start);

		if (width > max_width || *sp_prev == '\n') {
			lines = safe_realloc(lines, (n + 1) * sizeof (*lines));
			lines[n] = safe_malloc(sp_prev - start + 1);
			lacf_strlcpy(lines[n], start, sp_prev - start + 1);
			n++;
			start = sp_prev + 1;
		}
		p = sp_here + 1;
		sp_prev = sp_here;
	}
	/* Append any remaining stuff as a new line */
	if (start < end) {
		lines = safe_realloc(lines, (n + 1) * sizeof (*lines));
		lines[n] = safe_malloc(end - start + 1);
		lacf_strlcpy(lines[n], start, end - start + 1);
		n++;
	}
	*n_lines = n;
	return (lines);
}

static void
set_cur_tt(tooltip_t *tt, int mouse_x, int mouse_y)
{
	int width = 2 * TOOLTIP_WINDOW_MARGIN;
	int height = 2 * TOOLTIP_WINDOW_MARGIN;
	size_t n_lines;
	char **lines;

	ASSERT(tt != NULL);
	ASSERT(cur_tt == NULL);
	ASSERT(cur_tt_subwin[0] == NULL);
	ASSERT(cur_tt_win == NULL);

	lines = auto_wrap_text(tt->text, TOOLTIP_WINDOW_WIDTH, &n_lines);
	for (size_t i = 0; i < n_lines; i++) {
		width = MAX(XPLMMeasureString(xplmFont_Proportional, lines[i],
		    strlen(lines[i])) + 2 * TOOLTIP_WINDOW_MARGIN, width);
		height += TOOLTIP_LINE_HEIGHT;
	}

	cur_tt = tt;

	/*
	 * This is just a pair of backing windows to make the main
	 * window appear to have a darker background.
	 */
	for (int i = 0; i < NUM_TOOLTIP_SUBWINDOWS; i++) {
		cur_tt_subwin[i] = create_widget_rel(mouse_x +
		    TOOLTIP_WINDOW_OFFSET, mouse_y - TOOLTIP_WINDOW_OFFSET,
		    B_TRUE, width, height, 0, "", 1, NULL,
		    xpWidgetClass_MainWindow);
		XPSetWidgetProperty(cur_tt_subwin[i], xpProperty_MainWindowType,
		    xpMainWindowStyle_Translucent);
	}

	cur_tt_win = create_widget_rel(mouse_x + TOOLTIP_WINDOW_OFFSET,
	    mouse_y - TOOLTIP_WINDOW_OFFSET, B_TRUE, width, height,
	    0, "", 1, NULL, xpWidgetClass_MainWindow);
	XPSetWidgetProperty(cur_tt_win, xpProperty_MainWindowType,
	    xpMainWindowStyle_Translucent);

	for (size_t i = 0, y = TOOLTIP_WINDOW_MARGIN; i < n_lines; i++,
	    y += TOOLTIP_LINE_HEIGHT) {
		XPWidgetID line_caption;

		line_caption = create_widget_rel(TOOLTIP_WINDOW_MARGIN, y,
		    B_FALSE, width - 2 * TOOLTIP_WINDOW_MARGIN,
		    TOOLTIP_LINE_HEIGHT, 1, lines[i], 0, cur_tt_win,
		    xpWidgetClass_Caption);
		XPSetWidgetProperty(line_caption, xpProperty_CaptionLit, 1);
	}

	free_strlist(lines, n_lines);
	for (int i = 0; i < NUM_TOOLTIP_SUBWINDOWS; i++)
		XPShowWidget(cur_tt_subwin[i]);
	XPShowWidget(cur_tt_win);
}

static float
tooltip_floop_cb(float elapsed_since_last_call, float elapsed_since_last_floop,
    int counter, void *refcon)
{
	int mouse_x, mouse_y;
	long long now = microclock();
	tooltip_t *hit_tt = NULL;

	UNUSED(elapsed_since_last_call);
	UNUSED(elapsed_since_last_floop);
	UNUSED(counter);
	UNUSED(refcon);

	XPLMGetMouseLocation(&mouse_x, &mouse_y);

	if (last_mouse_x != mouse_x || last_mouse_y != mouse_y) {
		last_mouse_x = mouse_x;
		last_mouse_y = mouse_y;
		mouse_moved_time = now;
		if (cur_tt != NULL)
			destroy_cur_tt();
		return (TOOLTIP_INTVAL);
	}

	if (cur_tt != NULL)
		return (TOOLTIP_INTVAL);

	for (tooltip_set_t *tts = list_head(&tooltip_sets); tts != NULL;
	    tts = list_next(&tooltip_sets, tts)) {
		int wleft, wtop, wright, wbottom;
		double scalex, scaley;

		if (now - mouse_moved_time < SEC2USEC(tts->display_delay))
			continue;

		XPLMGetWindowGeometry(tts->window, &wleft, &wtop, &wright,
		    &wbottom);
		if (!XPLMGetWindowIsVisible(tts->window) ||
		    !XPLMIsWindowInFront(tts->window) ||
		    mouse_x < wleft || mouse_x > wright ||
		    mouse_y < wbottom || mouse_y > wtop)
			continue;

		scalex = (wright - wleft) / (double)tts->orig_w;
		scaley = (wtop - wbottom) / (double)tts->orig_h;

		for (tooltip_t *tt = list_head(&tts->tooltips); tt != NULL;
		    tt = list_next(&tts->tooltips, tt)) {
			int x1 = wleft + tt->x * scalex;
			int x2 = wleft + (tt->x + tt->w) * scalex;
			int y1 = wtop - (tt->y + tt->h) * scaley;
			int y2 = wtop - tt->y * scaley;

			if (mouse_x >= x1 && mouse_x <= x2 &&
			    mouse_y >= y1 && mouse_y <= y2) {
				hit_tt = tt;
				goto out;
			}
		}
	}
out:
	if (hit_tt != NULL)
		set_cur_tt(hit_tt, mouse_x, mouse_y);

	return (TOOLTIP_INTVAL);
}

void
tooltip_init(void)
{
	ASSERT(!inited);
	inited = true;

	list_create(&tooltip_sets, sizeof (tooltip_set_t),
	    offsetof(tooltip_set_t, node));

	XPLMRegisterFlightLoopCallback(tooltip_floop_cb, TOOLTIP_INTVAL, NULL);
}

void
tooltip_fini(void)
{
	tooltip_set_t *tts;

	if (!inited)
		return;
	inited = false;

	while ((tts = list_head(&tooltip_sets)) != NULL)
		tooltip_set_destroy(tts);
	list_destroy(&tooltip_sets);

	XPLMUnregisterFlightLoopCallback(tooltip_floop_cb, NULL);
}

static bool_t
is_in_VR(void)
{
	static bool_t dr_looked_up = B_FALSE;
	static dr_t VR_enabled;

	if (!dr_looked_up) {
		dr_looked_up = B_TRUE;
		fdr_find(&VR_enabled, "sim/graphics/VR/enabled");
	}

	return (dr_geti(&VR_enabled) != 0);
}

bool_t
window_follow_VR(XPLMWindowID win)
{
	bool_t vr = is_in_VR();
	XPLMWindowPositioningMode mode =
	    (vr ? xplm_WindowVR : xplm_WindowPositionFree);

	ASSERT(win != NULL);
	XPLMSetWindowPositioningMode(win, mode, -1);

	return (vr);
}

bool_t
widget_follow_VR(XPWidgetID win)
{
	ASSERT(win != NULL);
	return (window_follow_VR(XPGetWidgetUnderlyingWindow(win)));
}

typedef struct {
	int	left, top, right, bottom;
	bool	on_screen;
} find_window_t;

static void
find_window(int idx, int left, int top, int right, int bottom, void *refcon)
{
	enum { MARGIN = 50 };
	find_window_t *info;

	UNUSED(idx);
	ASSERT(refcon != NULL);
	info = refcon;

	if (info->right > left + MARGIN && info->left < right - MARGIN &&
	    info->top > bottom + MARGIN && info->bottom < top - MARGIN) {
		info->on_screen = B_TRUE;
	}
}

bool_t
window_is_on_screen(XPLMWindowID win)
{
	int left, top, right, bottom;
	find_window_t info = {};
	ASSERT(win != NULL);

	XPLMGetWindowGeometry(win, &info.left, &info.top, &info.right,
	    &info.bottom);
	XPLMGetAllMonitorBoundsGlobal(find_window, &info);
	/*
	 * Fallback - maybe we don't have any fullscreen windows at all,
	 * so try the global X-Plane desktop space.
	 */
	if (!info.on_screen) {
		enum { MARGIN = 50 };
		XPLMGetScreenBoundsGlobal(&left, &top, &right, &bottom);

		return (info.right > left + MARGIN &&
		    info.left < right - MARGIN &&
		    info.top > bottom + MARGIN &&
		    info.bottom < top - MARGIN);
	} else {
		return (B_TRUE);
	}
}

void
win_resize_ctl_init(win_resize_ctl_t *ctl, XPLMWindowID win,
    unsigned norm_w, unsigned norm_h)
{
	ASSERT(ctl != NULL);
	ASSERT(win != NULL);
	ASSERT3F(norm_w / 10, >, 0);
	ASSERT3F(norm_h / 10, >, 0);

	ctl->win = win;
	ctl->norm_w = norm_w;
	ctl->norm_h = norm_h;
	ctl->w_h_ratio = norm_w / (double)norm_h;

	XPLMGetWindowGeometry(win, &ctl->left, &ctl->top, &ctl->right,
	    &ctl->bottom);
	/*
	 * We need a resizing limit to avoid div-by-zero when trying to
	 * calculate the required scaling factor.
	 */
	XPLMSetWindowResizingLimits(win, norm_w / 10, norm_h / 10,
	    norm_w * 10, norm_h * 10);
	/*
	 * X-Plane's resizing controls are kinda weird in that they always
	 * work relative to the window's "current" size. That means if we
	 * keep the window snapped to the normal size, it would be very
	 * hard for the user to make an input large enough to "unsnap" it
	 * out of the snapping range. So we instead turn the snapping
	 * behavior on/off based on a time delay. When the user first moves
	 * into the size snapping region (within 5% of the window's normal
	 * size), we snap the size for up to 0.75 seconds. If the user keeps
	 * resizing the window past that timeout, we follow their resizing
	 * input, even if it is in the snapping region. When they move
	 * outside of the snapping region, we reset the snapping behavior,
	 * to allow us to snap to the normal size again.
	 */
	delay_line_init(&ctl->snap_hold_delay, SEC2USEC(0.75));
	delay_line_push_imm_u64(&ctl->snap_hold_delay, B_TRUE);
}

static void
calc_resize_geometry(win_resize_ctl_t *ctl, int top, int right,
    int *left_p, int *top_p, int *right_p, int *bottom_p,
    vect2_t grav_pt, bool_t grav_horiz)
{
	int w, h;

	ASSERT(ctl != NULL);
	ASSERT(left_p != NULL);
	ASSERT(top_p != NULL);
	ASSERT(right_p != NULL);
	ASSERT(bottom_p != NULL);
	ASSERT(!IS_NULL_VECT(grav_pt));

	w = *right_p - *left_p;
	h = *top_p - *bottom_p;

	if (grav_horiz) {
		int new_h = w / ctl->w_h_ratio;
		double scale;

		if (fabs(w / (double)ctl->norm_w - 1.0) < 0.05) {
			if (!delay_line_push_u64(&ctl->snap_hold_delay,
			    B_TRUE)) {
				scale = ctl->norm_w / (double)w;
				*right_p = grav_pt.x +
				    (right - grav_pt.x) * scale;
				*left_p = *right_p - ctl->norm_w;
				new_h = ctl->norm_h;
			}
		} else {
			delay_line_push_imm_u64(&ctl->snap_hold_delay, B_FALSE);
		}
		scale = new_h / (double)h;
		*top_p = grav_pt.y + (top - grav_pt.y) * scale;
		*bottom_p = *top_p - new_h;
	} else {
		int new_w = h * ctl->w_h_ratio;
		double scale;

		if (fabs(h / (double)ctl->norm_h - 1.0) < 0.05) {
			if (!delay_line_push_u64(&ctl->snap_hold_delay,
			    B_TRUE)) {
				scale = ctl->norm_h / (double)h;
				*top_p = grav_pt.y + (top - grav_pt.y) * scale;
				*bottom_p = *top_p - ctl->norm_h;
				new_w = ctl->norm_w;
			}
		} else {
			delay_line_push_imm_u64(&ctl->snap_hold_delay, B_FALSE);
		}
		scale = new_w / (double)w;
		*right_p = grav_pt.x + (right - grav_pt.x) * scale;
		*left_p = *right_p - new_w;
	}
}

void
win_resize_ctl_update(win_resize_ctl_t *ctl)
{
	int left, top, right, bottom;
	int new_left, new_top, new_right, new_bottom;

	ASSERT(ctl != NULL);
	ASSERT(ctl->win != NULL);

	XPLMGetWindowGeometry(ctl->win, &left, &top, &right, &bottom);
	new_left = left;
	new_top = top;
	new_right = right;
	new_bottom = bottom;

	if (left != ctl->left && top != ctl->top && right != ctl->right &&
	    bottom != ctl->bottom) {
		/*
		 * If all 4 coordinates changed, it means the window is
		 * being moved diagonally. We ignore this and simply store
		 * the new window geometry.
		 */
		ctl->left = new_left = left;
		ctl->top = new_top = top;
		ctl->right = new_right = right;
		ctl->bottom = new_bottom = bottom;
	} else if (left != ctl->left && top != ctl->top) {
		/* Resizing from top left corner, lock to lower right corner */
		calc_resize_geometry(ctl, top, right,
		    &new_left, &new_top, &new_right, &new_bottom,
		    VECT2(right, bottom), B_TRUE);
	} else if (left != ctl->left && bottom != ctl->bottom) {
		/* Resizing from bottom left corner, lock to upper right */
		calc_resize_geometry(ctl, top, right,
		    &new_left, &new_top, &new_right, &new_bottom,
		    VECT2(right, top), B_TRUE);
	} else if (right != ctl->right && top != ctl->top) {
		/* Resizing from top right corner, lock to lower left */
		calc_resize_geometry(ctl, top, right,
		    &new_left, &new_top, &new_right, &new_bottom,
		    VECT2(left, bottom), B_TRUE);
	} else if (right != ctl->right && bottom != ctl->bottom) {
		/* Resizing from bottom right corner, lock to upper left */
		calc_resize_geometry(ctl, top, right,
		    &new_left, &new_top, &new_right, &new_bottom,
		    VECT2(left, top), B_TRUE);
	} else if (left != ctl->left && right == ctl->right) {
		/* Resizing from bottom left edge, lock to right edge center */
		calc_resize_geometry(ctl, top, right,
		    &new_left, &new_top, &new_right, &new_bottom,
		    VECT2(right, AVG(top, bottom)), B_TRUE);
	} else if (right != ctl->right && left == ctl->left) {
		/* Resizing from bottom right edge, lock to left edge center */
		calc_resize_geometry(ctl, top, right,
		    &new_left, &new_top, &new_right, &new_bottom,
		    VECT2(left, AVG(top, bottom)), B_TRUE);
	} else if (top != ctl->top && bottom == ctl->bottom) {
		/* Resizing from top edge, lock to bottom edge center */
		calc_resize_geometry(ctl, top, right,
		    &new_left, &new_top, &new_right, &new_bottom,
		    VECT2(AVG(left, right), bottom), B_FALSE);
	} else if (bottom != ctl->bottom && top == ctl->top) {
		/* Resizing from bottom edge, lock to top edge center */
		calc_resize_geometry(ctl, top, right,
		    &new_left, &new_top, &new_right, &new_bottom,
		    VECT2(AVG(left, right), top), B_FALSE);
	} else {
		/* Horizontal/vertical window movement along a single axis. */
		ctl->left = new_left = left;
		ctl->top = new_top = top;
		ctl->right = new_right = right;
		ctl->bottom = new_bottom = bottom;
	}
	if (ctl->left != new_left || left != new_left ||
	    ctl->top != new_top || top != new_top ||
	    ctl->right != new_right || right != new_right ||
	    ctl->bottom != new_bottom || bottom != new_bottom) {
		ctl->left = new_left;
		ctl->right = new_right;
		ctl->top = new_top;
		ctl->bottom = new_bottom;
		XPLMSetWindowGeometry(ctl->win, new_left, new_top, new_right,
		    new_bottom);
	}
}
