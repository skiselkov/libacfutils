/*
 * Copyright 2020 Saso Kiselkov
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Example of how to do an altitude-style rolling display, which shows
 * 10000s, 1000s and 100s of feet individually and then has a final
 * column showing increments of 20 feet.
 */
enum { NUM_ALT_TUMBLERS = 4 };
static tumbler_t alt_tumblers[NUM_ALT_TUMBLERS] = {
	{ .mod = 100, .div = 1, .quant = 20, .fmt = "%02.0f" },
	{ .mod = 10, .div = 100, .quant = 1, .fmt = "%.0f" },
	{ .mod = 10, .div = 1000, .quant = 1, .fmt = "%.0f" },
	{ .mod = 10, .div = 10000, .quant = 1, .fmt = "%.0f" }
};

/*
 * Example of how to do a simple airspeed-style rolling display with
 * three single digits.
 */
enum { NUM_IAS_TUMBLERS = 3 };
static tumbler_t ias_tumblers[NUM_IAS_TUMBLERS] = {
	{ .mod = 10, .div = 1, .quant = 1, .fmt = "%.0f" },
	{ .mod = 10, .div = 10, .quant = 1, .fmt = "%.0f" },
	{ .mod = 10, .div = 100, .quant = 1, .fmt = "%.0f" }
};

/*
 * Example of a tumbler drawing function. You will ideally want to extend
 * this with things like font size selection, variable column widths, etc.
 *
 * @param tumblers	The tumbler set you defined ahead of time.
 * @param n_tumblers	The number of tumblers in the tumbler set.
 * @param display_value	The actual numerical value of the rolling display.
 * @param display_x	The X position of the right-most column of the display.
 * @param display_y	The Y position of the center of the display.
 *
 * Please note that COLUMN_WIDTH and LINE_HEIGHT below aren't defined.
 * They are meant to represent the concept of moving between columns
 * and line heights in the displays. You should adjust those to match
 * your particular needs.
 */
void
draw_tumblers(const tumbler_t *tumblers, size_t n_tumblers,
    double display_value, double display_x, double display_y)
{
	/*
	 * This is used for storing fractional line offsets between columns.
	 */
	double fract[n_tumblers] = {};

	for (unsigned i = 0; i < n_tumblers; i++) {
		/*
		 * The lines of text we're going to draw. The tumbler solver
		 * will put the text it wants us to draw in here.
		 */
		char out_str[TUMBLER_LINES][TUMBLER_CAP];
		int n = tumbler_solve(tumblers, i, display_value,
		    i > 0 ? fract[i - 1] : 0, out_str, &fract[i]);
		/*
		 * If no digits were emitted, we can stop.
		 */
		if (n == 0)
			break;
		/*
		 * The solver gave us one or more lines of text it wants us
		 * to show and their fractional line height offset.
		 */
		for (int j = 0; j < n; j++) {
			double text_x, text_y;

			cairo_text_extents(cr, out_str[j], &te);
			/*
			 * Center horizontally on the column.
			 */
			text_x = display_x - te.width / 2;
			/*
			 * We apply a fractional line height offset (fract[i]),
			 * and then draw lines one-by-one (j) on top of each
			 * other.
			 * We round the line height, because cairo_show_text
			 * always aligns text on pixel boundaries. To avoid
			 * each line jumping around independently as they try
			 * to align to pixels, we need to make sure all the
			 * rounding for all lines is the same.
			 * We also center vertically on the line.
			 */
			text_y = display_y +
			    (fract[i] - j) * round(1.5 * LINE_HEIGHT) -
			    te.height / 2 - te.y_bearing;
			cairo_move_to(cr, text_x, text_y);
			cairo_show_text(cr, out_str[j]);
		}
		/*
		 * Draw the next column to the left. If your columns are
		 * of unequal width, you will want to use a more complex
		 * algorithm.
		 */
		display_x -= COLUMN_WIDTH;
	}
}
