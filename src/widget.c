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
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include <XPLMGraphics.h>
#include <XPLMDisplay.h>
#include <XPLMProcessing.h>
#include <XPStandardWidgets.h>

#include <acfutils/assert.h>
#include <acfutils/helpers.h>
#include <acfutils/list.h>
#include <acfutils/widget.h>
#include <acfutils/time.h>

enum {
	TOOLTIP_LINE_HEIGHT =	13,
	TOOLTIP_WINDOW_OFFSET =	5,
	TOOLTIP_WINDOW_MARGIN = 10
};

#define	TOOLTIP_INTVAL		0.1
#define	TOOLTIP_DISPLAY_DELAY	SEC2USEC(1)

typedef struct {
	int		x, y, w, h;
	const char	*text;
	list_node_t	node;
} tooltip_t;

struct tooltip_set {
	XPWidgetID	window;
	list_t		tooltips;
	list_node_t	node;
};

static list_t tooltip_sets;
static tooltip_t *cur_tt = NULL;
static XPWidgetID cur_tt_win = NULL;
static int last_mouse_x, last_mouse_y;
static uint64_t mouse_moved_time;

XPWidgetID
create_widget_rel(int x, int y, bool_t y_from_bottom, int width, int height,
    int visible, const char *descr, int root, XPWidgetID container,
    XPWidgetClass cls)
{
	int wleft = 0, wtop = 0, wright = 0, wbottom = 0;
	int bottom, right;

	if (container != NULL) {
		XPGetWidgetGeometry(container, &wleft, &wtop, &wright,
		    &wbottom);
	} else {
		XPLMGetScreenSize(&wright, &wtop);
	}

	x += wleft;
	if (!y_from_bottom) {
		y = wtop - y;
		bottom = y - height;
	} else {
		bottom = y;
		y = y + height;
	}
	right = x + width;

	return (XPCreateWidget(x, y, right, bottom, visible, descr, root,
	    container, cls));
}

tooltip_set_t *
tooltip_set_new(XPWidgetID window)
{
	tooltip_set_t *tts = malloc(sizeof (*tts));
	tts->window = window;
	list_create(&tts->tooltips, sizeof (tooltip_t),
	    offsetof(tooltip_t, node));
	list_insert_tail(&tooltip_sets, tts);
	return (tts);
}

static void
destroy_cur_tt(void)
{
	ASSERT(cur_tt_win != NULL);
	ASSERT(cur_tt != NULL);
	XPDestroyWidget(cur_tt_win, 1);
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

	tooltip_t *tt = malloc(sizeof (*tt));
	tt->x = x;
	tt->y = y;
	tt->w = w;
	tt->h = h;
	tt->text = text;
	list_insert_tail(&tts->tooltips, tt);
}

static void
set_cur_tt(tooltip_t *tt, int mouse_x, int mouse_y)
{
	int width = 2 * TOOLTIP_WINDOW_MARGIN;
	int height = 2 * TOOLTIP_WINDOW_MARGIN;
	size_t n_lines;
	char **lines = strsplit(tt->text, "\n", B_FALSE, &n_lines);

	ASSERT(cur_tt == NULL);
	ASSERT(cur_tt_win == NULL);

	for (size_t i = 0; i < n_lines; i++) {
		width = MAX(XPLMMeasureString(xplmFont_Proportional, lines[i],
		    strlen(lines[i])) + 2 * TOOLTIP_WINDOW_MARGIN, width);
		height += TOOLTIP_LINE_HEIGHT;
	}

	cur_tt = tt;
	cur_tt_win = create_widget_rel(mouse_x + TOOLTIP_WINDOW_OFFSET,
	    mouse_y - height - TOOLTIP_WINDOW_OFFSET, B_TRUE, width, height,
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

	if (now - mouse_moved_time < TOOLTIP_DISPLAY_DELAY || cur_tt != NULL)
		return (TOOLTIP_INTVAL);

	for (tooltip_set_t *tts = list_head(&tooltip_sets); tts != NULL;
	    tts = list_next(&tooltip_sets, tts)) {
		int wleft, wtop, wright, wbottom;

		XPGetWidgetGeometry(tts->window, &wleft, &wtop, &wright,
		    &wbottom);
		if (!XPIsWidgetVisible(tts->window) ||
		    mouse_x < wleft || mouse_x > wright ||
		    mouse_y < wbottom || mouse_y > wtop)
			continue;
		for (tooltip_t *tt = list_head(&tts->tooltips); tt != NULL;
		    tt = list_next(&tts->tooltips, tt)) {
			int x1 = wleft + tt->x, x2 = wleft + tt->x + tt->w,
			    y1 = wtop - tt->y - tt->h, y2 = wtop - tt->y;

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
	list_create(&tooltip_sets, sizeof (tooltip_set_t),
	    offsetof(tooltip_set_t, node));

	XPLMRegisterFlightLoopCallback(tooltip_floop_cb, TOOLTIP_INTVAL, NULL);
}

void
tooltip_fini(void)
{
	tooltip_set_t *tts;
	while ((tts = list_head(&tooltip_sets)) != NULL)
		tooltip_set_destroy(tts);
	list_destroy(&tooltip_sets);

	XPLMUnregisterFlightLoopCallback(tooltip_floop_cb, NULL);
}
