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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stddef.h>

#include <acfutils/assert.h>
#include <acfutils/avl.h>
#include <acfutils/geom.h>
#include <acfutils/math.h>
#include <acfutils/list.h>
#include <acfutils/log.h>
#include <acfutils/helpers.h>
#include <acfutils/perf.h>
#include <acfutils/safe_alloc.h>

#define	SECS_PER_HR	3600		/* Number of seconds in an hour */

#define	ACFT_PERF_MIN_VERSION	1
#define	ACFT_PERF_MAX_VERSION	1
#define	MAX_LINE_COMPS		2

/*
 * Simulation step for accelclb2dist in seconds. 5 seems to be a good
 * compromise between performance and accuracy (~1% error vs running
 * 1-second steps, but 5x faster).
 */
#define	SECS_PER_STEP		10
/*
 * Higher accuracy in the departure segment.
 */
#define	SECS_PER_STEP_TAKEOFF	1
/*
 * Higher accuracy in the deceleration phase.
 */
#define	SECS_PER_STEP_DECEL	1
/*
 * Cruise phase doesn't need high accuracy.
 */
#define	SECS_PER_STEP_CRZ	10
#define	ALT_THRESH		1
#define	KCAS_THRESH		0.1
#define	KCAS_TABLE_THRESH	5

#define	MAX_ITER_STEPS		100000

typedef struct {
	double		vs;		/* vertical speed in m/s */
	double		fused;		/* fuel used in kg */
	double		fused_t;	/* fuel used time in seconds */
	double		ff;		/* fuel flow in kg/s */
} perf_table_cell_t;

typedef struct {
	double			isa;		/* ISA deviation in degrees C */
	double			ias;		/* IAS in m/s */
	double			mach;		/* mach limit */
	unsigned		num_wts;	/* number of weights */
	unsigned		num_alts;	/* number of altitudes */
	double			*wts;		/* weights in kg */
	double			*alts;		/* altitudes in meters */
	perf_table_cell_t	**rows;		/* one pointer set per alt */

	avl_node_t		ias_node;
	avl_node_t		mach_node;
	list_node_t		isa_node;
} perf_table_t;

struct perf_table_set_s {
	avl_tree_t	by_isa;
};

typedef struct {
	double		isa;
	avl_tree_t	by_ias;
	avl_tree_t	by_mach;
	list_t		tables;
	avl_node_t	ts_node;
} perf_table_isa_t;

static bool_t step_debug = B_FALSE;

static bool_t perf_table_parse(FILE *fp, perf_table_set_t *set,
    unsigned num_eng, double ff_corr, unsigned *line_num);
static void perf_table_free(perf_table_t *table);

void
lacf_set_perf_step_debug(bool_t flag)
{
	step_debug = flag;
}

bool_t
lacf_get_perf_step_debug(void)
{
	return (step_debug);
}

static int
perf_isas_compar(const void *a, const void *b)
{
	const perf_table_isa_t *ia = a, *ib = b;

	if (ia->isa < ib->isa)
		return (-1);
	if (ia->isa > ib->isa)
		return (1);

	return (0);
}

static int
perf_tables_ias_compar(const void *a, const void *b)
{
	const perf_table_t *ta = a, *tb = b;

	ASSERT(!isnan(ta->ias));
	ASSERT(!isnan(tb->ias));
	if (ta->ias < tb->ias)
		return (-1);
	if (ta->ias > tb->ias)
		return (1);

	return (0);
}

static int
perf_tables_mach_compar(const void *a, const void *b)
{
	const perf_table_t *ta = a, *tb = b;

	ASSERT(!isnan(ta->mach));
	ASSERT(!isnan(tb->mach));
	if (ta->mach < tb->mach)
		return (-1);
	if (ta->mach > tb->mach)
		return (1);

	return (0);
}

static perf_table_set_t *
perf_table_set_alloc(void)
{
	perf_table_set_t *ts = safe_calloc(1, sizeof (*ts));

	avl_create(&ts->by_isa, perf_isas_compar, sizeof (perf_table_isa_t),
	    offsetof(perf_table_isa_t, ts_node));

	return (ts);
}

static void
perf_table_set_free(perf_table_set_t *ts)
{
	void *cookie = NULL;
	perf_table_isa_t *isa;

	while ((isa = avl_destroy_nodes(&ts->by_isa, &cookie)) != NULL) {
		void *cookie2;
		perf_table_t *table;

		cookie2 = NULL;
		while ((avl_destroy_nodes(&isa->by_ias, &cookie2)) != NULL)
			;
		avl_destroy(&isa->by_ias);
		cookie2 = NULL;
		while ((avl_destroy_nodes(&isa->by_mach, &cookie2)) != NULL)
			;
		avl_destroy(&isa->by_mach);
		while ((table = list_remove_head(&isa->tables)) != NULL)
			perf_table_free(table);
		list_destroy(&isa->tables);
		free(isa);
	}
	avl_destroy(&ts->by_isa);
	free(ts);
}

static double
parse_table_alt(const char *str)
{
	unsigned l, mtr;

	ASSERT(str != NULL);
	l = strlen(str);
	ASSERT(l != 0);

	if (l >= 3 && str[0] == 'F' && str[1] == 'L')
		return (FEET2MET(atoi(&str[2]) * 100));
	if (strcmp(str, "0") == 0)
		return (0);
	mtr = atoi(str);
	if (mtr != 0)
		return (mtr);

	return (NAN);
}

static double
perf_table_extrapolate(perf_table_t *table, unsigned col,
    unsigned last_data_col, size_t offset)
{
	double v1, v2, m1, m2, m;
	perf_table_cell_t *cell1, *cell2;

	ASSERT(table != NULL);
	ASSERT3U(col, <, table->num_wts);
	ASSERT3U(last_data_col, <, col);
	ASSERT3U(offset, <, sizeof (perf_table_cell_t));

	if (last_data_col == 0) {
		/*
		 * Single data element in table, can't extrapolate.
		 * So just copy the value.
		 */
		cell1 = &table->rows[table->num_alts - 1][last_data_col];
		return (*(double *)((void *)(cell1) + offset));
	}

	cell1 = &table->rows[table->num_alts - 1][last_data_col - 1];
	cell2 = &table->rows[table->num_alts - 1][last_data_col];
	v1 = *(double *)((void *)(cell1) + offset);
	m1 = table->wts[last_data_col - 1];
	v2 = *(double *)((void *)(cell2) + offset);
	m2 = table->wts[last_data_col];
	m = table->wts[col];

	return (fx_lin(m, m1, v1, m2, v2));
}

static void
perf_table_cells_populate(char ** comps, size_t n_comps, perf_table_t *table,
    size_t offset, double conv_factor)
{
	ASSERT(comps != NULL);
	ASSERT3U(n_comps, >, 1);
	ASSERT(table != NULL);
	ASSERT3U(offset, <, sizeof (perf_table_cell_t));

	comps++;
	n_comps--;

	for (size_t i = 0; i < table->num_wts; i++) {
		perf_table_cell_t *cell = &table->rows[table->num_alts - 1][i];
		double *p = ((void *)(cell)) + offset;

		if (i < n_comps) {
			*p = atof(comps[i]) * conv_factor;
		} else {
			*p = perf_table_extrapolate(table, i, n_comps - 1,
			    offset);
		}
	}
}

static bool_t
perf_table_parse(FILE *fp, perf_table_set_t *ts, unsigned num_eng,
    double ff_corr, unsigned *line_num)
{
	perf_table_isa_t *isa, srch_isa;
	perf_table_t *table = safe_calloc(1, sizeof (*table));
	char *line = NULL;
	size_t line_cap = 0;
	avl_index_t where;

	ASSERT(fp != NULL);
	ASSERT(ts != NULL);
	ASSERT(line_num != NULL);

	table->isa = 0;
	table->ias = NAN;
	table->mach = NAN;

	for (;;) {
		ssize_t line_len =
		    parser_get_next_line(fp, &line, &line_cap, line_num);
		size_t n_comps;
		char **comps;
		double alt;

		if (line_len == 0)
			break;
		comps = strsplit(line, " ", B_TRUE, &n_comps);
		if (strcmp(comps[0], "ISA") == 0 && n_comps == 2) {
			table->isa = atof(comps[1]);
		} else if (strcmp(comps[0], "IAS") == 0 && n_comps == 2) {
			table->ias = atof(comps[1]);
		} else if (strcmp(comps[0], "KIAS") == 0 && n_comps == 2) {
			table->ias = KT2MPS(atof(comps[1]));
		} else if (strcmp(comps[0], "MACH") == 0 && n_comps == 2) {
			table->mach = atof(comps[1]);
		} else if (strcmp(comps[0], "GWLBK") == 0 && n_comps >= 2) {
			table->num_wts = n_comps - 1;
			table->wts = safe_calloc(table->num_wts,
			    sizeof (*table->wts));
			for (size_t i = 1; i < n_comps; i++) {
				table->wts[i - 1] =
				    LBS2KG(1000 * atof(comps[i]));
			}
		} else if (!isnan(alt = parse_table_alt(comps[0]))) {
			table->num_alts++;
			table->alts = realloc(table->alts, table->num_alts *
			    sizeof (*table->alts));
			table->alts[table->num_alts - 1] = alt;
			table->rows = realloc(table->rows, table->num_alts *
			    sizeof (*table->rows));
			table->rows[table->num_alts - 1] = safe_calloc(
			    table->num_wts, sizeof (perf_table_cell_t));
		} else if (strcmp(comps[0], "FPM") == 0) {
			perf_table_cells_populate(comps, n_comps, table,
			    offsetof(perf_table_cell_t, vs),
			    FPM2MPS(1));
		} else if (strcmp(comps[0], "TIMM") == 0) {
			perf_table_cells_populate(comps, n_comps, table,
			    offsetof(perf_table_cell_t, fused_t), 60);
		} else if (strcmp(comps[0], "FULB") == 0) {
			perf_table_cells_populate(comps, n_comps, table,
			    offsetof(perf_table_cell_t, fused),
			    LBS2KG(1) * ff_corr);
		} else if (strcmp(comps[0], "FFLB/ENG") == 0) {
			perf_table_cells_populate(comps, n_comps, table,
			    offsetof(perf_table_cell_t, ff),
			    (LBS2KG(1) / SECS_PER_HR) * num_eng * ff_corr);
		} else if (strcmp(comps[0], "ENDTABLE") == 0) {
			free_strlist(comps, n_comps);
			break;
		} else {
			free_strlist(comps, n_comps);
			goto errout;
		}
		free_strlist(comps, n_comps);
	}

	if ((isnan(table->ias) && isnan(table->mach)) || table->num_wts == 0 ||
	    table->num_alts == 0)
		goto errout;

	/*
	 * For climb/descent tables, we need to calculate the immediate
	 * fuel flow. The table contains aggregate climb time & fuel use
	 * figures. So to compute local fuel flow, we first subtract the
	 * fuel use and time-to-reach from one altitude lower. This then
	 * gives the fuel use & time delta to go from the lower altitude
	 * bracket to the altitude being examined. It isn't super-duper
	 * accurate, but should be reasonably close to immediate FF.
	 */
	for (unsigned i_alt = 0; i_alt < table->num_alts; i_alt++) {
		for (unsigned i_wt = 0; i_wt < table->num_wts; i_wt++) {
			perf_table_cell_t *cell = &table->rows[i_alt][i_wt];

			if (cell->fused_t == 0)
				continue;
			if (i_alt == 0) {
				cell->ff = cell->fused / cell->fused_t;
			} else {
				perf_table_cell_t *subcell =
				    &table->rows[i_alt - 1][i_wt];
				double fused, fused_t;

				ASSERT3F(subcell->fused, >, 0);
				ASSERT3F(subcell->fused_t, >, 0);
				fused = cell->fused - subcell->fused;
				fused_t = cell->fused_t - subcell->fused_t;
				cell->ff = fused / fused_t;
			}
			ASSERT_MSG(cell->ff >= 0, "Malformed table with "
			    "negative fuel flow: ISA=%.0f KIAS=%.0f Mach=%.2f "
			    "ALT=%.0fft WT=%.0flbs", table->isa,
			    MPS2KT(table->ias), table->mach,
			    MET2FEET(table->alts[i_alt]),
			    KG2LBS(table->wts[i_wt]));
		}
	}

	srch_isa.isa = table->isa;
	isa = avl_find(&ts->by_isa, &srch_isa, &where);
	if (isa == NULL) {
		isa = safe_calloc(1, sizeof (*isa));
		isa->isa = table->isa;
		avl_create(&isa->by_ias, perf_tables_ias_compar,
		    sizeof (perf_table_t), offsetof(perf_table_t, ias_node));
		avl_create(&isa->by_mach, perf_tables_mach_compar,
		    sizeof (perf_table_t), offsetof(perf_table_t, mach_node));
		list_create(&isa->tables, sizeof (perf_table_t),
		    offsetof(perf_table_t, isa_node));
		avl_insert(&ts->by_isa, isa, where);
	}
	if (!isnan(table->ias)) {
		if (avl_find(&isa->by_ias, table, &where) != NULL) {
			logMsg("Duplicate table for ISA %.1f/IAS %.1f",
			    table->isa, MPS2KT(table->ias));
			goto errout;
		}
		avl_insert(&isa->by_ias, table, where);
	}
	if (!isnan(table->mach)) {
		if (avl_find(&isa->by_mach, table, &where) != NULL) {
			logMsg("Duplicate table for ISA %.1f/Mach %.3f",
			    table->isa, MPS2KT(table->mach));
			goto errout;
		}
		avl_insert(&isa->by_mach, table, where);
	}

	return (B_TRUE);
errout:
	perf_table_free(table);
	return (B_FALSE);
}

static void
perf_table_free(perf_table_t *table)
{
	free(table->wts);
	free(table->alts);
	for (unsigned i = 0; i < table->num_alts; i++)
		free(table->rows[i]);
	free(table->rows);

	free(table);
}

static bool_t
parse_curve_lin(FILE *fp, vect2_t **curvep, size_t numpoints,
    unsigned *line_num)
{
	vect2_t	*curve;
	char	*line = NULL;
	size_t	line_cap = 0;
	ssize_t	line_len = 0;

	ASSERT(curvep != NULL);
	ASSERT3P(*curvep, ==, NULL);
	curve = safe_calloc(numpoints + 1, sizeof (*curve));

	for (size_t i = 0; i < numpoints; i++) {
		char *comps[2];

		line_len = parser_get_next_line(fp, &line, &line_cap, line_num);
		if (line_len <= 0)
			goto errout;
		if (explode_line(line, ',', comps, 2) != 2)
			goto errout;
		curve[i] = VECT2(atof(comps[0]), atof(comps[1]));
		if (i > 0 && curve[i - 1].x >= curve[i].x)
			goto errout;
	}
	/* curve terminator */
	curve[numpoints] = NULL_VECT2;

	*curvep = curve;
	free(line);

	return (B_TRUE);
errout:
	free(line);
	free(curve);
	return (B_FALSE);
}

static void
perf_tables_find_spd(perf_table_isa_t *isa, double spd, bool_t is_mach,
    perf_table_t **t_min_p, perf_table_t **t_max_p)
{
	avl_index_t where;
	perf_table_t srch;
	perf_table_t *t_min, *t_max;
	avl_tree_t *tree;

	ASSERT(isa != NULL);
	ASSERT(t_min_p != NULL);
	ASSERT(t_max_p != NULL);

	if (is_mach) {
		srch.mach = spd;
		tree = &isa->by_mach;
	} else {
		srch.ias = spd;
		tree = &isa->by_ias;
	}
	ASSERT(avl_numnodes(tree) != 0);
	t_min = avl_find(tree, &srch, &where);

	if (t_min != NULL) {
		t_max = t_min;
	} else {
		t_min = avl_nearest(tree, where, AVL_BEFORE);
		t_max = avl_nearest(tree, where, AVL_AFTER);
		ASSERT(t_min != NULL || t_max != NULL);
		if (t_min == NULL) {
			if (AVL_NEXT(tree, t_max) != NULL) {
				t_min = t_max;
				t_max = AVL_NEXT(tree, t_max);
			} else {
				t_min = t_max;
			}
		}
		if (t_max == NULL) {
			if (AVL_PREV(tree, t_min) != NULL) {
				t_max = t_min;
				t_min = AVL_PREV(tree, t_min);
			} else {
				t_max = t_min;
			}
		}
		ASSERT(t_min != NULL);
		ASSERT(t_max != NULL);
	}

	*t_min_p = t_min;
	*t_max_p = t_max;
}

static bool_t
perf_tables_find(perf_table_set_t *ts,
    double isadev, double spd, bool_t is_mach,
    perf_table_t **isa0_min, perf_table_t **isa0_max,
    perf_table_t **isa1_min, perf_table_t **isa1_max)
{
	avl_index_t where;
	perf_table_isa_t srch = { .isa = isadev };
	perf_table_isa_t *isa0, *isa1;

	ASSERT(ts != NULL);
	ASSERT(isa0_min != NULL);
	ASSERT(isa0_max != NULL);
	ASSERT(isa1_min != NULL);
	ASSERT(isa1_max != NULL);

	if (avl_numnodes(&ts->by_isa) == 0)
		return (B_FALSE);

	isa0 = avl_find(&ts->by_isa, &srch, &where);
	if (isa0 != NULL) {
		/* Exact hit */
		isa1 = isa0;
	} else {
		/*
		 * Try to find nearest two data points and interpolate, or
		 * even extrapolate from the nearest two data points.
		 */
		isa0 = avl_nearest(&ts->by_isa, where, AVL_BEFORE);
		isa1 = avl_nearest(&ts->by_isa, where, AVL_AFTER);
		ASSERT(isa0 != NULL || isa1 != NULL);
		/*
		 * We are at the edge of the data range. If we have more than
		 * one adjacent table to either side, we can try extrapolating.
		 */
		if (isa0 == NULL) {
			if (AVL_NEXT(&ts->by_isa, isa1) != NULL) {
				isa0 = isa1;
				isa1 = AVL_NEXT(&ts->by_isa, isa1);
			} else {
				isa0 = isa1;
			}
		}
		if (isa1 == NULL) {
			if (AVL_PREV(&ts->by_isa, isa0) != NULL) {
				isa1 = isa0;
				isa0 = AVL_PREV(&ts->by_isa, isa0);
			} else {
				isa1 = isa0;
			}
		}
		ASSERT(isa0 != NULL);
		ASSERT(isa1 != NULL);
	}

	perf_tables_find_spd(isa0, spd, is_mach, isa0_min, isa0_max);
	perf_tables_find_spd(isa1, spd, is_mach, isa1_min, isa1_max);

	return (B_FALSE);
}

static double
perf_table_lookup_row(perf_table_t *table, perf_table_cell_t *row,
    double mass, size_t field_offset)
{
	unsigned col1 = 0, col2 = 1;
	double v1, v2, v;

	ASSERT(table != NULL);
	ASSERT(table->num_wts != 0);
	ASSERT(row != NULL);
	ASSERT3U(field_offset, <, sizeof (perf_table_cell_t));

	/* clamp the mass to our tabulated range */
	mass = clamp(mass, table->wts[0], table->wts[table->num_wts - 1] - 1);

	for (unsigned i = 0; i + 1 < table->num_wts; i++) {
		if (mass >= table->wts[i] && mass <= table->wts[i + 1]) {
			col1 = i;
			col2 = i + 1;
			break;
		}
	}

	v1 = *(double *)((void *)(&row[col1]) + field_offset);
	v2 = *(double *)((void *)(&row[col2]) + field_offset);

	if (col1 != col2)
		v = fx_lin(mass, table->wts[col1], v1, table->wts[col2], v2);
	else
		v = v1;
	ASSERT(!isnan(v));

	return (v);
}

static double
perf_table_lookup(perf_table_t *table, double mass, double alt,
    size_t field_offset)
{
	unsigned row1 = UINT32_MAX, row2 = UINT32_MAX;
	double row1_val, row2_val, value;

	ASSERT(table != NULL);
	ASSERT3U(table->num_alts, >, 1);
	ASSERT3U(field_offset, <, sizeof (perf_table_cell_t));

	/*
	 * If the requested altitude lies outside of our tabulated range,
	 * extrapolate to it from the nearest pair of rows.
	 */
	if (alt > table->alts[0]) {
		row1 = 0;
		row2 = 1;
	} else if (alt < table->alts[table->num_alts - 1]) {
		row1 = table->num_alts - 2;
		row2 = table->num_alts - 1;
	} else {
		for (unsigned i = 0; i + 1 < table->num_alts; i++) {
			if (alt <= table->alts[i] &&
			    alt >= table->alts[i + 1]) {
				row1 = i;
				row2 = i + 1;
				break;
			}
		}
	}
	ASSERT3U(row1 + 1, ==, row2);
	ASSERT3U(row2, <, table->num_alts);

	row1_val = perf_table_lookup_row(table, table->rows[row1], mass,
	    field_offset);
	row2_val = perf_table_lookup_row(table, table->rows[row2], mass,
	    field_offset);
	value = fx_lin(alt, table->alts[row1], row1_val, table->alts[row2],
	    row2_val);
	ASSERT(!isnan(value));

	return (value);
}

static double
table_lookup_common(perf_table_set_t *ts, double isadev, double mass,
    double spd_mps_or_mach, bool_t is_mach, double alt, size_t offset)
{
	perf_table_t *isa0_min = NULL, *isa0_max = NULL;
	perf_table_t *isa1_min = NULL, *isa1_max = NULL;
	double isa0_min_param, isa0_max_param, isa1_min_param, isa1_max_param;
	double isa0_param, isa1_param, param;

	ASSERT(ts != NULL);
	ASSERT3U(offset + sizeof (double), <=, sizeof (perf_table_cell_t));

	perf_tables_find(ts, isadev, spd_mps_or_mach, is_mach,
	    &isa0_min, &isa0_max, &isa1_min, &isa1_max);

	if (isa0_min != isa0_max) {
		double x0 = (is_mach ? isa0_min->mach : isa0_min->ias);
		double x1 = (is_mach ? isa0_max->mach : isa0_max->ias);
		double rat = iter_fract(spd_mps_or_mach, x0, x1, B_FALSE);
		isa0_min_param = perf_table_lookup(isa0_min, mass, alt, offset);
		isa0_max_param = perf_table_lookup(isa0_max, mass, alt, offset);
		/*
		 * We need to be careful about extrapolating speed estimates
		 * too much. There are drag-nonlinearities inherent in this,
		 * so we limit the estimator to reasonable ranges only.
		 */
		isa0_param = wavg2(isa0_min_param, isa0_max_param,
		    clamp(rat, -0.25, 2));
		ASSERT(!isnan(isa0_param));
	} else {
		isa0_param = perf_table_lookup(isa0_min, mass, alt, offset);
	}
	if (isa1_min != isa1_max) {
		double x0 = (is_mach ? isa1_min->mach : isa1_min->ias);
		double x1 = (is_mach ? isa1_max->mach : isa1_max->ias);
		double rat = iter_fract(spd_mps_or_mach, x0, x1, B_FALSE);
		isa1_min_param = perf_table_lookup(isa1_min, mass, alt, offset);
		isa1_max_param = perf_table_lookup(isa1_max, mass, alt, offset);
		isa1_param = wavg2(isa1_min_param, isa1_max_param,
		    clamp(rat, -0.25, 2));
		ASSERT(!isnan(isa1_param));
	} else {
		isa1_param = perf_table_lookup(isa1_min, mass, alt, offset);
	}

	if (isa0_param != isa1_param) {
		double rat = clamp(iter_fract(isadev, isa0_min->isa,
		    isa1_min->isa, B_FALSE), -0.5, 1.5);
		param = wavg2(isa0_param, isa1_param, rat);
	} else {
		param = isa0_param;
	}

	return (param);
}

#define	PARSE_SCALAR(name, var) \
	if (strcmp(comps[0], name) == 0) { \
		if (ncomps != 2) { \
			logMsg("Error parsing acft perf file %s:%d: " \
			    "malformed or duplicate " name " line.", \
			    filename, line_num); \
			goto errout; \
		} \
		(var) = atof(comps[1]); \
		if ((var) <= 0.0) { \
			logMsg("Error parsing acft perf file %s:%d: " \
			    "invalid value for " name, filename, line_num); \
			goto errout; \
		} \
	}

/*
 * Checks that comps[0] contains `name' and if it does, parses comps[1] number
 * of curve points into `var'. The curve parsed is assumed to be composed of
 * a sequential series of linear segments.
 */
#define	PARSE_CURVE(name, var) \
	if (strcmp(comps[0], name) == 0) { \
		if (ncomps != 2 || atoi(comps[1]) < 2 || (var) != NULL) { \
			logMsg("Error parsing acft perf file %s:%d: " \
			    "malformed or duplicate " name " line.", \
			    filename, line_num); \
			goto errout; \
		} \
		if (!parse_curve_lin(fp, &(var), atoi(comps[1]), &line_num)) { \
			logMsg("Error parsing acft perf file %s:%d: " \
			    "malformed or missing lines.", filename, \
			    line_num); \
			goto errout; \
		} \
	}

#define	PARSE_TABLE(name, table_set) \
	if (strcmp(comps[0], (name)) == 0) { \
		if ((table_set) == NULL) \
			(table_set) = perf_table_set_alloc(); \
		if (!perf_table_parse(fp, (table_set), acft->num_eng, \
		    table_ff_corr, &line_num)) { \
			logMsg("Error parsing acft perf file %s:%d: " \
			    "malformed or missing lines.", filename, \
			    line_num); \
			goto errout; \
		} \
	}

acft_perf_t *
acft_perf_parse(const char *filename)
{
	acft_perf_t	*acft = safe_calloc(sizeof (*acft), 1);
	FILE		*fp = fopen(filename, "r");
	char		*line = NULL;
	size_t		line_cap = 0;
	unsigned	line_num = 0;
	ssize_t		line_len = 0;
	char		*comps[MAX_LINE_COMPS];
	bool_t		version_check_completed = B_FALSE;
	double		table_ff_corr = 1;

	if (fp == NULL)
		goto errout;

	for (int i = 0; i < FLT_PERF_NUM_SPD_LIMS; i++) {
		acft->ref.clb_spd_lim[i] = (flt_spd_lim_t){NAN, NAN};
		acft->ref.des_spd_lim[i] = (flt_spd_lim_t){NAN, NAN};
	}
	while ((line_len = parser_get_next_line(fp, &line, &line_cap,
	    &line_num)) != -1) {
		ssize_t ncomps;

		if (line_len == 0)
			continue;
		ncomps = explode_line(line, ',', comps, MAX_LINE_COMPS);
		if (ncomps < 0) {
			logMsg("Error parsing acft perf file %s:%d: "
			    "malformed line, too many line components.",
			    filename, line_num);
			goto errout;
		}
		ASSERT(ncomps > 0);
		if (strcmp(comps[0], "VERSION") == 0) {
			int vers;

			if (version_check_completed) {
				logMsg("Error parsing acft perf file %s:%d: "
				    "duplicate VERSION line.", filename,
				    line_num);
				goto errout;
			}
			if (ncomps != 2) {
				logMsg("Error parsing acft perf file %s:%d: "
				    "malformed VERSION line.", filename,
				    line_num);
				goto errout;
			}
			vers = atoi(comps[1]);
			if (vers < ACFT_PERF_MIN_VERSION ||
			    vers > ACFT_PERF_MAX_VERSION) {
				logMsg("Error parsing acft perf file %s:%d: "
				    "unsupported file version %d.", filename,
				    line_num, vers);
				goto errout;
			}
			version_check_completed = B_TRUE;
			continue;
		}
		if (!version_check_completed) {
			logMsg("Error parsing acft perf file %s:%d: first "
			    "line was not VERSION.", filename, line_num);
			goto errout;
		}
		if (strcmp(comps[0], "ACFTTYPE") == 0) {
			if (ncomps != 2 || acft->acft_type != NULL) {
				logMsg("Error parsing acft perf file %s:%d: "
				    "malformed or duplicate ACFTTYPE line.",
				    filename, line_num);
				goto errout;
			}
			acft->acft_type = strdup(comps[1]);
		} else if (strcmp(comps[0], "ENGTYPE") == 0) {
			if (ncomps != 2 || acft->eng_type != NULL) {
				logMsg("Error parsing acft perf file %s:%d: "
				    "malformed or duplicate ENGTYPE line.",
				    filename, line_num);
				goto errout;
			}
			acft->eng_type = strdup(comps[1]);
		}
		else PARSE_SCALAR("NUMENG", acft->num_eng)
		else PARSE_SCALAR("MAXTHR", acft->eng_max_thr)
		else PARSE_SCALAR("MINTHR", acft->eng_min_thr)
		else PARSE_SCALAR("SFC", acft->eng_sfc)
		else PARSE_SCALAR("REFZFW", acft->ref.zfw)
		else PARSE_SCALAR("REFFUEL", acft->ref.fuel)
		else PARSE_SCALAR("REFCRZLVL", acft->ref.crz_lvl)
		else PARSE_SCALAR("REFCLBIAS", acft->ref.clb_ias)
		else PARSE_SCALAR("REFCLBIASINIT", acft->ref.clb_ias_init)
		else PARSE_SCALAR("REFCLBMACH", acft->ref.clb_mach)
		else PARSE_SCALAR("REFCRZIAS", acft->ref.crz_ias)
		else PARSE_SCALAR("REFCRZMACH", acft->ref.crz_mach)
		else PARSE_SCALAR("REFDESIAS", acft->ref.des_ias)
		else PARSE_SCALAR("REFDESMACH", acft->ref.des_mach)
		else PARSE_SCALAR("REFTOFLAP", acft->ref.to_flap)
		else PARSE_SCALAR("REFACCELHT", acft->ref.accel_hgt)
		else PARSE_SCALAR("REFCLBSPDLIM[0]",
		    acft->ref.clb_spd_lim[0].kias)
		else PARSE_SCALAR("REFCLBSPDLIMALT[0]",
		    acft->ref.clb_spd_lim[0].alt_ft)
		else PARSE_SCALAR("REFCLBSPDLIM[1]",
		    acft->ref.clb_spd_lim[1].kias)
		else PARSE_SCALAR("REFCLBSPDLIMALT[1]",
		    acft->ref.clb_spd_lim[1].alt_ft)
		else PARSE_SCALAR("REFDESSPDLIM[0]",
		    acft->ref.des_spd_lim[0].kias)
		else PARSE_SCALAR("REFDESSPDLIMALT[0]",
		    acft->ref.des_spd_lim[0].alt_ft)
		else PARSE_SCALAR("REFDESSPDLIM[1]",
		    acft->ref.des_spd_lim[1].kias)
		else PARSE_SCALAR("REFDESSPDLIMALT[1]",
		    acft->ref.des_spd_lim[1].alt_ft)
		else PARSE_SCALAR("WINGAREA", acft->wing_area)
		else PARSE_SCALAR("CLMAX", acft->cl_max_aoa)
		else PARSE_SCALAR("CLFLAPMAX", acft->cl_flap_max_aoa)
		else PARSE_SCALAR("TABLEFFCORR", table_ff_corr)
		else PARSE_CURVE("THRDENS", acft->thr_dens_curve)
		else PARSE_CURVE("THRMACH", acft->thr_mach_curve)
		else PARSE_CURVE("SFCTHRO", acft->sfc_thro_curve)
		else PARSE_CURVE("SFCISA", acft->sfc_isa_curve)
		else PARSE_CURVE("CL", acft->cl_curve)
		else PARSE_CURVE("CLFLAP", acft->cl_flap_curve)
		else PARSE_CURVE("CD", acft->cd_curve)
		else PARSE_CURVE("CDFLAP", acft->cd_flap_curve)
		else PARSE_CURVE("HALFBANK", acft->half_bank_curve)
		else PARSE_CURVE("FULLBANK", acft->full_bank_curve)
		else PARSE_TABLE("CLBTABLE", acft->clb_tables)
		else PARSE_TABLE("CRZTABLE", acft->crz_tables)
		else PARSE_TABLE("DESTABLE", acft->des_tables)
		else {
			logMsg("Error parsing acft perf file %s:%d: unknown "
			    "line", filename, line_num);
			goto errout;
		}
	}

	if (acft->acft_type == NULL ||
	    acft->ref.clb_ias <= 0 ||
	    acft->ref.clb_ias_init <= 0 ||
	    acft->ref.clb_mach <= 0 ||
	    acft->ref.crz_ias <= 0 ||
	    acft->ref.crz_mach <= 0 ||
	    acft->ref.des_ias <= 0 ||
	    acft->ref.des_mach <= 0 ||
	    acft->eng_type == NULL ||
	    acft->eng_max_thr <= 0 ||
	    acft->eng_min_thr <= 0 ||
	    acft->eng_sfc <= 0 ||
	    acft->num_eng <= 0 ||
	    acft->thr_mach_curve == NULL ||
	    acft->sfc_thro_curve == NULL ||
	    acft->sfc_isa_curve == NULL ||
	    acft->cl_curve == NULL ||
	    acft->cl_flap_curve == NULL ||
	    acft->cd_curve == NULL ||
	    acft->cd_flap_curve == NULL ||
	    acft->wing_area == 0 ||
	    acft->half_bank_curve == NULL ||
	    acft->full_bank_curve == NULL) {
		logMsg("Error parsing acft perf file %s: missing or corrupt "
		    "data fields.", filename);
		goto errout;
	}

	fclose(fp);
	free(line);

	acft->ref.thr_derate = 1;

	return (acft);
errout:
	if (fp)
		fclose(fp);
	if (acft)
		acft_perf_destroy(acft);
	free(line);
	return (NULL);
}

void
acft_perf_destroy(acft_perf_t *acft)
{
	if (acft->acft_type)
		free(acft->acft_type);
	if (acft->eng_type)
		free(acft->eng_type);
	free(acft->thr_dens_curve);
	free(acft->thr_mach_curve);
	free(acft->sfc_thro_curve);
	free(acft->sfc_isa_curve);
	free(acft->cl_curve);
	free(acft->cl_flap_curve);
	free(acft->cd_curve);
	free(acft->cd_flap_curve);
	free(acft->half_bank_curve);
	free(acft->full_bank_curve);
	if (acft->clb_tables != NULL)
		perf_table_set_free(acft->clb_tables);
	if (acft->crz_tables != NULL)
		perf_table_set_free(acft->crz_tables);
	if (acft->des_tables != NULL)
		perf_table_set_free(acft->des_tables);
	free(acft);
}

flt_perf_t *
flt_perf_new(const acft_perf_t *acft)
{
	flt_perf_t *flt = safe_calloc(sizeof (*flt), 1);
	memcpy(flt, &acft->ref, sizeof (*flt));
	return (flt);
}

void
flt_perf_destroy(flt_perf_t *flt)
{
	free(flt);
}

static double
get_num_eng(const flt_perf_t *flt, const acft_perf_t *acft)
{
	ASSERT(flt != NULL);
	ASSERT(acft != NULL);
	if (flt->num_eng > 0 && flt->num_eng <= acft->num_eng)
		return (flt->num_eng);
	return (acft->num_eng);
}

/*
 * Estimates maximum available engine thrust in a given flight situation.
 * This takes into account atmospheric conditions as well as any currently
 * effective engine derates. Number of engines running is configured via
 * flt_perf->num_eng.
 *
 * @param flt Flight performance configuration.
 * @param acft Aircraft performance tables.
 * @param throttle Relative linear throttle position (0.0 to 1.0).
 * @param alt Altitude in feet.
 * @param ktas True air speed in knots.
 * @param qnh Barometric altimeter setting in hPa.
 * @param isadev ISA temperature deviation in degrees C.
 * @param tp_alt Altitude of the tropopause in feet.
 *
 * @return Maximum available engine thrust in Newtons.
 */
double
eng_get_thrust(const flt_perf_t *flt, const acft_perf_t *acft, double throttle,
    double alt, double ktas, double qnh, double isadev, double tp_alt)
{
	double Ps, D, dmod, mmod, mach, sat;
	unsigned num_eng;
	double min_thr, max_thr;

	ASSERT(flt != NULL);
	ASSERT(acft != NULL);
	ASSERT3F(throttle, >=, 0);
	ASSERT3F(throttle, <=, 1);

	num_eng = get_num_eng(flt, acft);

	Ps = alt2press(alt, qnh);
	sat = isadev2sat(alt2fl(alt < tp_alt ? alt : tp_alt, qnh), isadev);
	D = air_density(Ps, isadev);
	dmod = D / ISA_SL_DENS;
	if (acft->thr_dens_curve != NULL)
		dmod *= fx_lin_multi(dmod, acft->thr_dens_curve, B_TRUE);
	mach = ktas2mach(ktas, sat);
	mmod = fx_lin_multi(mach, acft->thr_mach_curve, B_TRUE);

	max_thr = num_eng * acft->eng_max_thr * dmod * mmod * flt->thr_derate;
	min_thr = num_eng * acft->eng_min_thr * dmod * mmod * flt->thr_derate;

	return (wavg(min_thr, max_thr, throttle));
}

double
eng_get_min_thr(const flt_perf_t *flt, const acft_perf_t *acft)
{
	ASSERT(flt != NULL);
	ASSERT(acft != NULL);
	return (get_num_eng(flt, acft) * acft->eng_min_thr);
}

/*
 * Returns the maximum average thrust that the engines can attain between
 * two altitudes during a climb.
 *
 * @param flt Flight performance limits.
 * @param acft Aircraft performance limit curves.
 * @param alt1 First (lower) altitude in feet.
 * @param temp1 Estimated TAT in C at altitude alt1.
 * @param alt1 Second (higher) altitude in feet.
 * @param temp1 Estimated TAT in C at altitude alt2.
 * @param tp_alt Altitude of the tropopause in feet.
 *
 * @return The maximum average engine thrust (in Kilonewtons) attainable
 *	between alt1 and alt2 while keeping the flight and aircraft limits.
 */
double
eng_max_thr_avg(const flt_perf_t *flt, const acft_perf_t *acft, double alt1,
    double alt2, double ktas, double qnh, double isadev, double tp_alt)
{
	double Ps, D, dmod, mmod, avg_temp, thr;
	double avg_alt = AVG(alt1, alt2);
	/* convert altitudes to flight levels to calculate avg temp */
	double alt1_fl = alt2fl(alt1, qnh);
	double alt2_fl = alt2fl(alt2, qnh);
	double tp_fl = alt2fl(tp_alt, qnh);
	unsigned num_eng = get_num_eng(flt, acft);
	double mach;

	/*
	 * FIXME: correctly weight the temp average when tp_alt < alt2.
	 */
	avg_temp = AVG(isadev2sat(alt1_fl, isadev),
	    isadev2sat(alt2_fl < tp_fl ? alt2_fl : tp_fl, isadev));

	mach = ktas2mach(ktas, avg_temp);
	mmod = fx_lin_multi(mach, acft->thr_mach_curve, B_TRUE);
	/* Ps is the average static air pressure between alt1 and alt2. */
	Ps = alt2press(avg_alt, qnh);
	/*
	 * Finally grab effective air density.
	 */
	isadev = isadev2sat(alt2fl(avg_alt, qnh), avg_temp);
	D = air_density(Ps, isadev);
	dmod = D / ISA_SL_DENS;
	if (acft->thr_dens_curve != NULL)
		dmod *= fx_lin_multi(dmod, acft->thr_dens_curve, B_TRUE);
	/*
	 * Derive engine performance.
	 */
	thr = num_eng * dmod * mmod * flt->thr_derate;

	return (thr);
}

/*
 * Given a curve mapping angle-of-attack (AoA) to an aircraft's coefficient of
 * lift (Cl) and a target Cl, we attempt to find the lowest AoA on the curve
 * where the required Cl is produced. If no candidate can be found, we return
 * DEFAULT_AOA.
 *
 * @param Cl Required coefficient of lift.
 * @param curve Curve mapping AoA to Cl.
 *
 * @return The angle of attack (in degrees) at which the Cl is produced.
 */
static double
cl_curve_get_aoa(double Cl, const vect2_t *curve)
{
	double aoa = 2.5;
	double *candidates = NULL;
	size_t n;

	ASSERT(curve != NULL);

	candidates = fx_lin_multi_inv(Cl, curve, &n);
	if (n == 0 || n == SIZE_MAX) {
		/* No AoA will provide enough lift, guess at some value */
		return (10);
	}

	aoa = candidates[0];
	for (size_t i = 1; i < n; i++) {
		if (aoa > candidates[i]) {
			ASSERT(!isnan(candidates[i]));
			aoa = candidates[i];
		}
	}
	lacf_free(candidates);

	return (aoa);
}

/*
 * Calculates total (kinetic + potential) energy of a moving object.
 * This simply computes: E = m.g.h + (1/2).m.v^2
 *
 * @param mass Mass in kg.
 * @param altm Altitude above sea level in meters.
 * @param tas True airspeed in m/s.
 *
 * @return The object's total energy in Joules.
 */
static inline double
calc_total_E(double mass, double altm, double tas)
{
	return (mass * EARTH_GRAVITY * altm + 0.5 * mass * POW2(tas));
}

/*
 * Calculates the altitude above sea level an object needs to be at to have
 * a given total (kinetic + potential) energy. This simply computes:
 * h = (E - (1/2).m.v^2) / (m.g)
 *
 * @param E Total energy in Joules.
 * @param m Mass in kg.
 * @param tas True airspeed in m/s.
 *
 * @return The object's required elevation above sea level in meters.
 */
static inline double
total_E_to_alt(double E, double m, double tas)
{
	return ((E - (0.5 * m * POW2(tas))) / (m * EARTH_GRAVITY));
}

/*
 * Calculates the angle of attack required to maintain level flight.
 *
 * @param Pd Dynamic pressure on the aircraft in Pa (see dyn_press()).
 * @param mass Aircrat mass in kg.
 * @param flap_ratio Active flat setting between 0 and 1 inclusive
 *	(0 - flaps up, 1 - flaps fully deployed).
 * @param acft Performance tables of the aircraft.
 *
 * @return Angle of attack to airstream in degrees required to produce
 *	lift equivalent to the weight of `mass' on Earth. If the aircraft
 *	is unable to produce sufficient lift at any angle of attack to
 *	sustain flight, returns NAN instead.
 */
static double
get_aoa(double Pd, double mass, double flap_ratio, const acft_perf_t *acft)
{
	double lift, Cl;

	lift = MASS2GFORCE(mass);
	Cl = lift / (Pd * acft->wing_area);
	if (flap_ratio == 0) {
		return (cl_curve_get_aoa(Cl, acft->cl_curve));
	} else {
		double aoa_no_flap = cl_curve_get_aoa(Cl, acft->cl_curve);
		double aoa_flap = cl_curve_get_aoa(Cl, acft->cl_flap_curve);
		ASSERT3F(flap_ratio, <=, 1.0);
		return (wavg(aoa_no_flap, aoa_flap, flap_ratio));
	}
}

/*
 * Calculates the amount of drag experienced by an aircraft.
 *
 * @param Pd Dynamic pressure on the airframe in Pa (see dyn_press()).
 * @param aoa Current angle of attack to the airstream in degrees.
 * @param flap_ratio Active flat setting between 0 and 1 inclusive
 *	(0 - flaps up, 1 - flaps fully deployed).
 * @param acft Performance tables of the aircraft.
 *
 * @return Drag force on the aircraft's airframe in N.
 */
static inline double
get_drag(double Pd, double aoa, double flap_ratio, const acft_perf_t *acft)
{
	if (flap_ratio == 0)
		return (fx_lin_multi(aoa, acft->cd_curve, B_TRUE) * Pd *
		    acft->wing_area);
	else
		return (wavg(fx_lin_multi(aoa, acft->cd_curve, B_TRUE),
		    fx_lin_multi(aoa, acft->cd_flap_curve, B_TRUE),
		    flap_ratio) * Pd * acft->wing_area);
}

/*
 * Performs a level acceleration simulation step.
 *
 * @param isadev ISA temperature deviation in degrees C.
 * @param tp_alt Altitude of the tropopause.
 * @param qnh Barometric altimeter setting in Pa.
 * @param gnd Flag indicating whether the aircraft is currently positioned
 *	on the ground (and hence no lift generation is required).
 * @param alt Current aircraft altitude above mean sea level in feet.
 * @param kcasp Input/output argument containing the aircraft's current
 *	calibrated airspeed in knots. This will be updated with the new
 *	airspeed after the function returns with success.
 * @param kcas_targp Input/output argument containing the acceleration
 *	target calibrated airspeed in knots. If the airspeed exceeds
 *	mach_lim, it will be down-adjusted, otherwise it remains unchanged.
 * @param mach_lim Limiting Mach number. If either the current airspeed
 *	or target airspeeds exceed this value at the provided altitude,
 *	we down-adjust both. This allows for continuously accelerating while
 *	climbing until reaching the Mach limit and then slowly bleeding off
 *	speed to maintain the Mach number.
 * @param wind_mps Wind component along flight path in m/s.
 * @param mass Aircraft total mass in kg.
 * @param flap_ratio Active flat setting between 0 and 1 inclusive
 *	(0 - flaps up, 1 - flaps fully deployed).
 * @param acft Performance tables of the aircraft.
 * @param flt Flight performance settings.
 * @param distp Return parameter that will be incremented with the distance
 *	covered during the acceleration phase. As such, it must be
 *	pre-initialized.
 * @param timep Input/output argument that initially contains the number of
 *	seconds that the acceleration step can be allowed to happen. This is
 *	then adjusted with the actual amount of time that the acceleration
 *	was happening. If the target airspeed is greater than what can be
 *	achieved during the acceleration step, timep is left unmodified
 *	(all of the available time was used for acceleration). If the
 *	target speed is achievable, timep is modified to the proportion of
 *	the time taken to accelerate to that speed. If the target speed is
 *	less than the current speed, timep is set proportionally negative
 *	(and kcasp adjusted to it), indicating to the caller that the energy
 *	can be used in a climb.
 * @param burnp Return parameter that will be filled with the amount of
 *	fuel consumed during the acceleration phase. If no acceleration was
 *	performed or speed was even given up for reuse for climbing, no
 *	adjustment to this value will be made.
 *
 * @return B_TRUE on success, B_FALSE on failure (insufficient thrust
 *	to overcome drag and accelerate).
 */
static void
spd_chg_step(bool_t accel, double isadev, double tp_alt, double qnh, bool_t gnd,
    double alt, double *kcasp, double kcas_targ, double wind_mps, double mass,
    double flap_ratio, const acft_perf_t *acft, const flt_perf_t *flt,
    double *distp, double *timep, double *burnp)
{
	double aoa, drag, delta_v, E_now, E_lim, E_targ, tas_lim;
	double fl = alt2fl(alt, qnh);
	double Ps = alt2press(alt, qnh);
	double oat = isadev2sat(fl, isadev);
	ASSERT3F(*kcasp, >, 0);
	double ktas_now = kcas2ktas(*kcasp, Ps, oat);
	double tas_now = KT2MPS(ktas_now);
	double tas_targ = KT2MPS(kcas2ktas(kcas_targ, Ps, oat));
	double Pd = dyn_press(ktas_now, Ps, oat);
	double throttle = accel ? 1.0 : 0.0;
	double thr = eng_get_thrust(flt, acft, throttle, alt, ktas_now, qnh,
	    isadev, tp_alt);
	double burn = *burnp;
	double t = *timep;
	double altm = FEET2MET(alt);

	if (gnd) {
		aoa = 0;
	} else {
		aoa = get_aoa(Pd, mass, flap_ratio, acft);
	}
	ASSERT(!isnan(aoa));
	drag = get_drag(Pd, aoa, flap_ratio, acft);
	/* Prevent deceleration */
	delta_v = MAX((thr - drag) / mass, 0);

	tas_lim = tas_now + delta_v * t;
	E_now = calc_total_E(mass, altm, tas_now);
	E_lim = calc_total_E(mass, altm, tas_lim);
	E_targ = calc_total_E(mass, altm, tas_targ);

	if (accel ? E_targ > E_lim : E_targ < E_lim) {
		*kcasp = ktas2kcas(MPS2KT(tas_lim), Ps, oat);
	} else {
		t *= ((E_targ - E_now) / (E_lim - E_now));
		*kcasp = ktas2kcas(MPS2KT(tas_targ), Ps, oat);
		*timep = t;
	}

	if (t > 0) {
		burn += acft_get_sfc(flt, acft, thr, alt, ktas_now, qnh,
		    isadev, tp_alt) * (t / SECS_PER_HR);
	}

	*burnp = burn;
	double dist = MET2NM(tas_now * t + 0.5 * delta_v * POW2(t) +
	    wind_mps * t);
	(*distp) += MAX(dist, 0);
}

static void
clb_table_step(const acft_perf_t *acft, double isadev, double qnh, double alt,
    double spd, bool_t is_mach, double mass, double wind_mps, double d_t,
    double *nalt, double *nburn, double *ndist)
{
	double ff, vs;
	double kcas, ktas_now, tas_now;
	double fl = alt2fl(alt, qnh);
	double Ps = alt2press(alt, qnh);
	double oat = isadev2sat(fl, isadev);

	ASSERT(acft != NULL);
	ASSERT(acft->clb_tables != NULL);
	ASSERT3F(spd, >, 0);
	ASSERT(nalt != NULL);
	ASSERT(nburn != NULL);
	ASSERT3F(d_t, >=, 0);

	ff = table_lookup_common(acft->clb_tables, isadev, mass, spd, is_mach,
	    alt, offsetof(perf_table_cell_t, ff));
	ff = MAX(ff, 0);
	vs = table_lookup_common(acft->clb_tables, isadev, mass, spd, is_mach,
	    alt, offsetof(perf_table_cell_t, vs));
	vs = MAX(vs, 0);

	if (is_mach) {
		ktas_now = mach2ktas(spd, oat);
		kcas = ktas2kcas(ktas_now, Ps, oat);
	} else {
		kcas = MPS2KT(spd);
		ktas_now = kcas2ktas(kcas, Ps, oat);
	}
	tas_now = KT2MPS(ktas_now);

	*nalt = alt + vs * d_t;
	*nburn = ff * d_t;
	*ndist = MAX(tas_now + wind_mps, 0) * d_t;
}

static void
crz_step(double isadev, double tp_alt, double qnh, double alt_ft,
    double spd_mps_or_mach, bool_t is_mach, double wind_mps, double mass,
    const acft_perf_t *acft, const flt_perf_t *flt, double dist_nm,
    double d_t, double *distp, double *burnp, double *ttg_out)
{
	double burn, kcas, ktas_now, tas_now, burn_step, dist_step;
	double fl = alt2fl(alt_ft, qnh);
	double Ps = alt2press(alt_ft, qnh);
	double oat = isadev2sat(fl, isadev);

	ASSERT(acft != NULL);
	ASSERT(flt != NULL);
	ASSERT3F(dist_nm, >=, 0);
	ASSERT(distp != NULL);
	ASSERT(burnp != NULL);
	ASSERT3F(mass, >, 0);
	burn = *burnp;

	if (is_mach) {
		ktas_now = mach2ktas(spd_mps_or_mach, oat);
		kcas = ktas2kcas(ktas_now, Ps, oat);
	} else {
		kcas = MPS2KT(spd_mps_or_mach);
		ktas_now = kcas2ktas(kcas, Ps, oat);
	}
	tas_now = KT2MPS(ktas_now);

	if (acft->crz_tables != NULL) {
		double ff = table_lookup_common(acft->crz_tables, isadev, mass,
		    spd_mps_or_mach, is_mach, FEET2MET(alt_ft),
		    offsetof(perf_table_cell_t, ff));
		ff = MAX(ff, 0);
		burn_step = ff * d_t;
		if (step_debug) {
			double spd_kias_or_mach = (is_mach ? spd_mps_or_mach :
			    MPS2KT(spd_mps_or_mach));
			printf("CRZ:%5.0f ft m:%5.0f spd:%.*f lb ff:%4.0f "
			    "lb/hr/eng\n", alt_ft, KG2LBS(mass),
			    is_mach ? 3 : 0, spd_kias_or_mach,
			    KG2LBS(ff) * SECS_PER_HR / get_num_eng(flt, acft));
		}
	} else {
		double aoa, drag, thr, sfc, Pd;

		Pd = dyn_press(ktas_now, Ps, oat);
		aoa = get_aoa(Pd, mass, 0, acft);
		ASSERT(!isnan(aoa));
		drag = get_drag(Pd, aoa, 0, acft);
		thr = drag;
		sfc = acft_get_sfc(flt, acft, thr, alt_ft, ktas_now, qnh,
		    isadev, tp_alt);
		printf("Ps: %.0f  Pd: %.0f  kcas: %.0f  aoa: %.3f  drag: %.2f  "
		    "sfc: %.1f  gw: %.1f\n", Ps, Pd, kcas, aoa, drag / 1000,
		    KG2LBS(sfc) / get_num_eng(flt, acft), KG2LBS(mass) / 1000);
		burn_step = sfc * (d_t / SECS_PER_HR);
	}
	/*
	 * The MAX here is important to make sure we keep making forward
	 * progress, otherwise the solver can soft-lock.
	 */
	dist_step = MAX(tas_now + wind_mps, KT2MPS(60)) * d_t;
	if ((*distp) + MET2NM(dist_step) > dist_nm) {
		double rat = (dist_nm - (*distp)) / MET2NM(dist_step);
		burn_step *= rat;
		dist_step = NM2MET(dist_nm - (*distp));
		if (ttg_out != NULL)
			(*ttg_out) += d_t * rat;
	} else {
		if (ttg_out != NULL)
			(*ttg_out) += d_t;
	}
	burn += burn_step;
	*burnp = burn;
	(*distp) += MET2NM(dist_step);
}

/*
 * Performs a climb simulation step.
 *
 * @param isadev Same as `isadev' in spd_chg_step.
 * @param tp_alt Same as `tp_alt' in spd_chg_step.
 * @param qnh Same as `qnh' in spd_chg_step.
 * @param altp Input/output argument containing the aircraft's current
 *	altitude in feet. It will be adjusted with the altitude achieved
 *	during the climb.
 * @param kcasp Input/output argument containing the aircraft's current
 *	calibrated airspeed in knots. After climb, we recalculate the
 *	effective calibrated airspeed at the new altitude and update kcasp.
 * @param alt_targ Climb target altitude in feet.
 * @param wind_mps Same as `wind_mps' in spd_chg_step.
 * @param mass Same as `mass' in spd_chg_step.
 * @param flap_ratio Same as `flap_ratio' in spd_chg_step.
 * @param acft Same as `acft' in spd_chg_step.
 * @param flt Same as `flt' in spd_chg_step.
 * @param distp Same as `distp' in spd_chg_step.
 * @param timep Same as `timep' in spd_chg_step.
 * @param burnp Same as `burnp' in spd_chg_step.
 *
 * @return B_TRUE on success, B_FALSE on failure (insufficient speed to
 *	sustain flight or thrust to maintain speed).
 */
static void
alt_chg_step(bool_t clb, double isadev, double tp_alt, double qnh,
    double *altp, double *vsp, double *kcasp, double alt_targ, double wind_mps,
    double mass, double flap_ratio, const acft_perf_t *acft,
    const flt_perf_t *flt, double *distp, double *timep, double *burnp)
{
	double aoa, drag, E_now, E_lim, E_targ;
	double alt = *altp;
	double fl = alt2fl(alt, qnh);
	double Ps = alt2press(alt, qnh);
	double oat = isadev2sat(fl, isadev);
	ASSERT3F(*kcasp, >, 0);
	double ktas_now = kcas2ktas(*kcasp, Ps, oat);
	double tas_now = KT2MPS(ktas_now);
	double Pd = dyn_press(ktas_now, Ps, oat);
	double throttle = clb ? 1.0 : 0.0;
	double thr = eng_get_thrust(flt, acft, throttle, alt, ktas_now, qnh,
	    isadev, tp_alt);
	double burn = *burnp;
	double t = *timep;
	double altm = FEET2MET(alt);

	aoa = get_aoa(Pd, mass, flap_ratio, acft);
	ASSERT(!isnan(aoa));
	drag = get_drag(Pd, aoa, flap_ratio, acft);
	/* Prevent a trend reversal - worst case guesses */
	if (clb) {
		thr = MAX(thr, drag);
	} else {
		thr = MIN(thr, drag);
	}

	E_now = calc_total_E(mass, altm, tas_now);
	E_lim = E_now + (thr - drag) * tas_now * t;
	E_targ = calc_total_E(mass, FEET2MET(alt_targ), tas_now);

	if (clb ? E_targ > E_lim : E_targ < E_lim) {
		double nalt = total_E_to_alt(E_lim, mass, tas_now);
		double vs_tgt = (nalt - FEET2MET(*altp)) / t;
		double v_accel = (vs_tgt - (*vsp)) / t;

		v_accel = clamp(v_accel, -2.5, 2.5);
		vs_tgt = (*vsp) + (v_accel * t);
		*altp = MET2FEET(FEET2MET(*altp) + vs_tgt * t);
		*vsp = vs_tgt;
	} else {
		t *= ((E_targ - E_now) / (E_lim - E_now));
		*altp = alt_targ;
		*timep = t;
	}

	/* adjust kcas to new altitude */
	Ps = alt2press(*altp, qnh);
	fl = alt2fl(*altp, qnh);
	oat = isadev2sat(fl, isadev);
	*kcasp = ktas2kcas(ktas_now, Ps, oat);

	/* use average air density to use in burn estimation */
	burn += acft_get_sfc(flt, acft, thr, alt, ktas_now, qnh, isadev,
	    tp_alt) * (t / SECS_PER_HR);

	*burnp = burn;
	double dist = MET2NM(sqrt(POW2(tas_now * t) +
	    FEET2MET(POW2((*altp) - alt))) + wind_mps * t);
	(*distp) += MAX(dist, 0);
}

static double
des_burn_step(double isadev, double alt_m, double vs_act_mps,
    double spd_mps_or_mach, bool_t is_mach, double mass,
    const acft_perf_t *acft, double d_t)
{
	double ff_des = table_lookup_common(acft->des_tables, isadev, mass,
	    spd_mps_or_mach, is_mach, alt_m, offsetof(perf_table_cell_t, ff));
	double ff_crz = table_lookup_common(acft->crz_tables, isadev, mass,
	    spd_mps_or_mach, is_mach, alt_m, offsetof(perf_table_cell_t, ff));
	double vs_des_mps = table_lookup_common(acft->des_tables, isadev, mass,
	    spd_mps_or_mach, is_mach, alt_m, offsetof(perf_table_cell_t, vs));
	double rat = iter_fract(vs_act_mps, 0, vs_des_mps, B_TRUE);
	double burn;
	/*
	 * Prevent sliding off the tables into nonsense.
	 */
	ff_des = MAX(ff_des, 0);
	ff_crz = MAX(ff_crz, 0);
	burn = wavg(ff_crz, ff_des, rat) * d_t;
	if (step_debug) {
		printf("DES:%-5.0f ft m:%-5.0f lb vs:%-5.0f fpm "
		    "ff_crz:%-4.0f lbs/hr ff_des:%-4.0f rat:%.3f\n",
		    MET2FEET(alt_m), KG2LBS(mass), MPS2FPM(vs_des_mps),
		    KG2LBS(ff_crz) * SECS_PER_HR, KG2LBS(ff_des) * SECS_PER_HR,
		    rat);
	}
	ASSERT3F(burn, >=, 0);
	return (burn);
}

/*
 * ACCEL_THEN_CLB first accelerates to kcas2 and then climbs.
 * ACCEL_TAKEOFF first accelerates to flt->clb_ias_init, then climbs until
 * reaching accel_alt, then does a 50/50 time split to reach target climb spd.
 * ACCEL_AND_CLB does a 50/50 time split.
 * If `flap_ratio_act' is not NULL, it is set to:
 * 1) `flap_ratio_takeoff', when in 1st and 2nd segment climb, or
 * 2) `flap_ratio', otherwise
 */
static double
accel_time_split(accelclb_t type, double kcas, double clbias, double alt,
    double accel_alt, double t, double flap_ratio, double flap_ratio_takeoff,
    double *flap_ratio_act)
{
	if (flap_ratio_act != NULL)
		*flap_ratio_act = flap_ratio;

	switch (type) {
	case ACCEL_THEN_CLB:
		return (t);
	case ACCEL_TAKEOFF:
		if (kcas < clbias) {
			if (flap_ratio_act != NULL)
				*flap_ratio_act = flap_ratio_takeoff;
			return (t);
		}
		if (alt < accel_alt) {
			if (flap_ratio_act != NULL)
				*flap_ratio_act = flap_ratio_takeoff;
			return (0);
		}
		return (t / 2);
	default:
		VERIFY3U(type, ==, ACCEL_AND_CLB);
		return (t / 2);
	}
}

static double
select_step(accelclb_t type)
{
	if (type == ACCEL_TAKEOFF)
		return (SECS_PER_STEP_TAKEOFF);
	else
		return (SECS_PER_STEP);
}

static bool_t
should_use_clb_tables(const acft_perf_t *acft, accelclb_t type,
    double kcas, double kcas_lim)
{
	return (acft->clb_tables != NULL &&
	    (type == ACCEL_AND_CLB || type == ACCEL_THEN_CLB ||
	    kcas_lim - kcas < KCAS_TABLE_THRESH));
}

/*
 * Calculates the linear distance covered by an aircraft in wings-level
 * flight attempting to climb and accelerate. This is used in climb distance
 * performance estimates, especially when constructing altitude-terminated
 * procedure legs. This function assumes the engines will be running at
 * maximum thrust during the climb/acceleration phase (subject to
 * environmental limitations and configured performance derates).
 *
 * @param flt Flight performance settings.
 * @param acft Aircraft performance tables.
 * @param isadev Estimated average ISA deviation during climb phase.
 * @param qnh Estimated average QNH during climb phase.
 * @param tp_alt Altitude of the tropopause in feet AMSL.
 * @param fuel Current fuel state in kg.
 * @param dir Unit vector pointing in the flight direction of the aircraft.
 * @param alt1 Climb/acceleration starting altitude in feet AMSL.
 * @param kcas1 Climb/acceleration starting calibrated airspeed in knots.
 * @param wind1 Wind vector at start of climb/acceleration phase. The
 *	vector's direction expresses the wind direction and its magnitude
 *	the wind's true speed in knots.
 * @param alt2 Climb/acceleration target altitude AMSL in feet. Must be
 *	greater than or equal to alt1.
 * @param kcas2 Climb/acceleration target calibrated airspeed in knots.
 * @param wind2 Wind vector at end of climb/acceleration phase. The wind is
 *	assumed to vary smoothly between alt1/wind1 and alt2/wind2.
 * @param flap_ratio Average flap setting between 0 and 1 (inclusive) during
 *	climb/acceleration phase. This expresses how muct lift and drag is
 *	produced by the wings as a fraction between CL/CLFLAP and CD/CDFLAP.
 *	A setting of 0 means flaps up, whereas 1 is flaps fully extended.
 * @param type Type of acceleration/climb procedure to execute.
 * @param burnp Return parameter which if not NULL will be filled with the
 *	amount of fuel consumed during the acceleration/climb phase (in kg).
 *
 * @return Distance over the ground covered during acceleration/climb
 *	maneuver in NM.
 */
double
accelclb2dist(const flt_perf_t *flt, const acft_perf_t *acft, double isadev,
    double qnh, double tp_alt, double accel_alt, double fuel, vect2_t dir,
    double alt1_ft, double kcas1, vect2_t wind1,
    double alt2_ft, double kcas2, vect2_t wind2,
    double flap_ratio, double mach_lim, accelclb_t type, double *burnp,
    double *kcas_out)
{
	double alt = alt1_ft, kcas = kcas1, burn = 0, dist = 0;
	double step = select_step(type);
	double flap_ratio_act;
	int iter_counter = 0;
	double vs = 0;

	ASSERT3F(alt1_ft, <=, alt2_ft);
	ASSERT3F(fuel, >=, 0);
	ASSERT(!isnan(accel_alt) || type != ACCEL_TAKEOFF);
	dir = vect2_unit(dir, NULL);

	/* Iterate in steps of `step'. */
	while (alt2_ft - alt > ALT_THRESH && kcas2 - kcas > KCAS_THRESH) {
		double wind_mps, alt_fract, accel_t, clb_t, ktas_lim_mach,
		    kcas_lim_mach, oat, kcas_lim;
		double Ps;
		vect2_t wind;
		/* debugging support */
		double old_alt = alt;
		double old_kcas = kcas;
		bool_t table = B_FALSE;

		ASSERT3S(iter_counter, <, MAX_ITER_STEPS);

		oat = isadev2sat(alt2fl(alt, qnh), isadev);
		Ps = alt2press(alt, qnh);
		ktas_lim_mach = mach2ktas(mach_lim, oat);
		kcas_lim_mach = ktas2kcas(ktas_lim_mach, Ps, oat);

		kcas_lim = kcas2;
		for (int i = 0; i < FLT_PERF_NUM_SPD_LIMS; i++) {
			if (alt < flt->clb_spd_lim[i].alt_ft) {
				kcas_lim = MIN(kcas_lim,
				    flt->clb_spd_lim[i].kias);
			}
		}
		if (kcas_lim > kcas_lim_mach)
			kcas_lim = kcas_lim_mach;
		if (alt2_ft - alt < ALT_THRESH && kcas_lim < kcas2)
			kcas2 = kcas_lim;

		/*
		 * Calculate the directional wind component. This will be
		 * factored into the distance traveled estimation below.
		 */
		alt_fract = (alt - alt1_ft) / (alt2_ft - alt1_ft);
		wind = VECT2(wavg(wind1.x, wind2.x, alt_fract),
		    wavg(wind1.y, wind2.y, alt_fract));
		wind_mps = KT2MPS(vect2_dotprod(wind, dir));
		/*
		 * Swap to accel-and-climb tabulated profiles when we're
		 * 1000ft above the acceleration altitude.
		 */
		if (type == ACCEL_TAKEOFF && alt > accel_alt + 1000)
			type = ACCEL_AND_CLB;

		accel_t = accel_time_split(type, kcas, flt->clb_ias_init,
		    alt, accel_alt, step, flap_ratio, flt->to_flap,
		    &flap_ratio_act);

		/*
		 * We can try to use climb performance tables for a more
		 * accurate estimate, provided that all of the following
		 * conditions are satisfied:
		 * 1) climb tables are available
		 * 2) no more acceleration is required (in normal climb)
		 * 3) our speed is within the airspeed target (acceleration
		 *    phase complete)
		 */
		if (should_use_clb_tables(acft, type, kcas, kcas_lim)) {
			bool_t is_mach = (kcas2 > kcas_lim_mach);
			double spd = (is_mach ? kcas_lim_mach : KT2MPS(kcas2));
			double nalt, nburn, ndist;

			clb_table_step(acft, isadev, qnh, FEET2MET(alt),
			    spd, is_mach, flt->zfw + fuel - burn, wind_mps,
			    step, &nalt, &nburn, &ndist);
			alt = MET2FEET(nalt);
			burn += nburn;
			dist += MET2NM(ndist);
			clb_t = step - accel_t;
			if (is_mach)
				kcas = kcas_lim_mach;
			if (step_debug)
				table = B_TRUE;
		} else {
			if (accel_t > 0) {
				spd_chg_step(B_TRUE, isadev, tp_alt,
				    qnh, type == ACCEL_TAKEOFF &&
				    alt == alt1_ft, alt, &kcas, kcas_lim,
				    wind_mps, flt->zfw + fuel - burn,
				    flap_ratio_act, acft, flt, &dist,
				    &accel_t, &burn);
			}

			clb_t = step - accel_t;
			if (clb_t > 0 && alt2_ft - alt > ALT_THRESH) {
				alt_chg_step(B_TRUE, isadev, tp_alt, qnh,
				    &alt, &vs, &kcas, alt2_ft, wind_mps,
				    flt->zfw + fuel - burn, flap_ratio_act,
				    acft, flt, &dist, &clb_t, &burn);
			}
		}

		if (step_debug) {
			double total_t;

			total_t = accel_t + clb_t;
			oat = isadev2sat(alt2fl(alt, qnh), isadev);

			printf("V:%3.0f KT  +V:%5.02lf  H:%5.0lf  fpm:%4.0lf  "
			    "s:%6.0lf  M:%5.03lf  tab:%d\n", kcas,
			    (kcas - old_kcas) / total_t, alt,
			    ((alt - old_alt) / total_t) * 60, NM2MET(dist),
			    ktas2mach(kcas2ktas(kcas, alt2press(alt, qnh), oat),
			    oat), table);
		}

		iter_counter++;
	}
	if (burnp != NULL)
		*burnp = burn;
	if (kcas_out != NULL)
		*kcas_out = kcas;
	ASSERT3F(dist, >=, 0);

	return (dist);
}

opt_double
dist2accelclb(const flt_perf_t REQ_PTR(flt), const acft_perf_t REQ_PTR(acft),
    double isadev, double qnh, double tp_alt, double accel_alt,
    double fuel, vect2_t dir, double flap_ratio, double REQ_PTR(alt_ft_p),
    double REQ_PTR(kcas_p), vect2_t wind, double alt_tgt_ft, double kcas_tgt,
    double mach_lim, double dist_tgt, accelclb_t type, double *burnp,
    double *ttg_out)
{
	ASSERT3F(*alt_ft_p, <=, alt_tgt_ft);
	double alt_ft = *alt_ft_p;
	double alt1_ft = alt_ft;
	double dist = 0, burn = 0;
	double wind_mps = KT2MPS(vect2_dotprod(wind, dir));
	double step = select_step(type);
	double flap_ratio_act;
	int iter_counter = 0;
	double vs = 0;

	ASSERT3F(*kcas_p, >, 0);
	ASSERT3F(*kcas_p, <=, kcas_tgt);
	double kcas = *kcas_p;
	ASSERT(!isnan(accel_alt) || type != ACCEL_TAKEOFF);
	double ttg = 0;
	/*
	 * If the dist_tgt is very large, or we're flying very slowly, we
	 * might run up against MAX_ITER_STEPS too early. So allow adjusting
	 * the step size to hopefully make sure we reach the target before
	 * running up against the iteration limit.
	 */
	{
		double oat_guess = isadev2sat(alt2fl(alt_tgt_ft,
		    ISA_SL_PRESS), 0);
		double pressure_guess = alt2press(alt_tgt_ft, ISA_SL_PRESS);
		double ktas_guess = kcas2ktas(kcas_tgt, pressure_guess,
		    oat_guess);
		double min_step = ((dist_tgt / ktas_guess) * 3600) /
		    MAX_ITER_STEPS;
		step = MAX(step, min_step * 2);
	}
	while (dist < dist_tgt && alt_tgt_ft - alt_ft > ALT_THRESH) {
		double tas_mps = KT2MPS(kcas2ktas(kcas, alt2press(alt_ft, qnh),
		    isadev2sat(alt2fl(alt_ft, qnh), isadev)));
		double rmng = NM2MET(dist_tgt - dist);
		ASSERT3F(tas_mps, >, 0);
		double t_rmng = MIN(rmng / tas_mps, step);
		double accel_t, clb_t, oat, Ps, ktas_lim_mach, kcas_lim_mach,
		    kcas_lim;
		/* step debug support */
		double old_alt = alt_ft;
		double old_kcas = kcas;
		bool_t table = B_FALSE;

		if (iter_counter >= MAX_ITER_STEPS) {
			// Solution didn't converge, abort
			return (NONE(double));
		}
		oat = isadev2sat(alt2fl(alt_ft, qnh), isadev);
		Ps = alt2press(alt_ft, qnh);
		ktas_lim_mach = mach2ktas(mach_lim, oat);
		kcas_lim_mach = ktas2kcas(ktas_lim_mach, Ps, oat);

		kcas_lim = kcas_tgt;
		for (int i = 0; i < FLT_PERF_NUM_SPD_LIMS; i++) {
			if (alt_ft < flt->clb_spd_lim[i].alt_ft) {
				kcas_lim = MIN(kcas_lim,
				    flt->clb_spd_lim[i].kias);
			}
		}
		if (kcas_lim > kcas_lim_mach)
			kcas_lim = kcas_lim_mach;
		if (alt_tgt_ft - alt_ft < ALT_THRESH && kcas_lim < kcas_tgt)
			kcas_tgt = kcas_lim;
		/*
		 * Swap to accel-and-climb tabulated profiles when we're
		 * 1000ft above the acceleration altitude.
		 */
		if (type == ACCEL_TAKEOFF && alt_ft > accel_alt + 1000)
			type = ACCEL_AND_CLB;
		/*
		 * ACCEL_THEN_CLB and ACCEL_TAKEOFF first accelerate to kcas2
		 * and then climb. ACCEL_AND_CLB does a 50/50 time split.
		 */
		accel_t = accel_time_split(type, kcas, flt->clb_ias_init,
		    alt_ft, accel_alt, t_rmng, flap_ratio, flt->to_flap,
		    &flap_ratio_act);

		if (should_use_clb_tables(acft, type, kcas, kcas_lim)) {
			bool_t is_mach = (kcas_tgt >= kcas_lim_mach);
			double spd = (is_mach ? mach_lim :
			    KT2MPS(kcas_tgt));
			double nalt, nburn, ndist;

			clb_table_step(acft, isadev, qnh, FEET2MET(alt_ft),
			    spd, is_mach, flt->zfw + fuel - burn, wind_mps,
			    step, &nalt, &nburn, &ndist);
			alt_ft = MET2FEET(nalt);
			burn += nburn;
			dist += MET2NM(ndist);
			clb_t = step - accel_t;
			if (is_mach)
				kcas = kcas_lim_mach;
			else
				kcas = kcas_lim;
			if (step_debug)
				table = B_TRUE;
		} else {
			if (accel_t > 0) {
				spd_chg_step(B_TRUE, isadev, tp_alt,
				    qnh, type == ACCEL_TAKEOFF &&
				    alt_ft == alt1_ft, alt_ft, &kcas, kcas_lim,
				    wind_mps, flt->zfw + fuel - burn,
				    flap_ratio_act, acft, flt, &dist,
				    &accel_t, &burn);
			}

			clb_t = t_rmng - accel_t;
			if (clb_t > 0 && alt_tgt_ft - alt_ft > ALT_THRESH) {
				alt_chg_step(B_TRUE, isadev, tp_alt, qnh,
				    &alt_ft, &vs, &kcas, alt_tgt_ft, wind_mps,
				    flt->zfw + fuel - burn, flap_ratio_act,
				    acft, flt, &dist, &clb_t, &burn);
			}
		}

		if (step_debug) {
			double total_t;

			total_t = accel_t + clb_t;
			oat = isadev2sat(alt2fl(alt_ft, qnh), isadev);

			printf("V:%5.01lf  +V:%5.02lf  H:%5.0lf  fpm:%4.0lf  "
			    "s:%6.0lf  M:%5.03lf  tab:%d\n", kcas,
			    ((kcas) - old_kcas) / total_t, alt_ft,
			    (((alt_ft) - old_alt) / total_t) * 60,
			    NM2MET(dist), ktas2mach(kcas2ktas(kcas,
			    alt2press(alt_ft, qnh), oat), oat), table);
		}
		ttg += step;
		iter_counter++;
	}
	// write out state variables
	*alt_ft_p = alt_ft;
	*kcas_p = kcas;
	if (burnp != NULL)
		*burnp = burn;
	/* ttg_out can be NULL */
	if (ttg_out != NULL)
		*ttg_out = ttg;

	ASSERT(!isnan(dist));
	ASSERT(isfinite(dist));
	ASSERT3F(dist, >=, 0);
	return (SOME(dist));
}

double
decel2dist(const flt_perf_t *flt, const acft_perf_t *acft,
    double isadev, double qnh, double tp_alt, double fuel,
    double alt, double kcas1, double kcas2, double dist_tgt,
    double *kcas_out, double *burn_out)
{
	double dist = 0, burn = 0;
	double step = SECS_PER_STEP_DECEL;
	double kcas = kcas1;
	double oat = isadev2sat(alt2fl(alt, qnh), isadev);

	while (dist < dist_tgt && kcas + KCAS_THRESH > kcas2) {
		double t = step;
		double old_kcas = kcas;
		double mach;

		spd_chg_step(B_FALSE, isadev, tp_alt, qnh, B_FALSE,
		    alt, &kcas, kcas2, 0, flt->zfw + fuel - burn,
		    0, acft, flt, &dist, &t, &burn);

		if (step_debug) {
			mach = ktas2mach(kcas2ktas(kcas, alt2press(alt, qnh),
			    oat), oat);
			printf("V:%5.01lf  +V:%5.02lf  H:%5.0lf  s:%6.0lf  "
			    "M:%5.03lf\n", kcas, (kcas - old_kcas) / t, alt,
			    NM2MET(dist), mach);
		}
	}

	if (kcas_out != NULL)
		*kcas_out = kcas;
	if (burn_out != NULL)
		*burn_out = burn;

	ASSERT(!isnan(dist));
	ASSERT(isfinite(dist));
	ASSERT3F(dist, >=, 0);
	return (dist);
}

/*
 * Estimates fuel burn in level, non-accelerated flight (cruise).
 * Flaps are assumed up in this configuration.
 *
 * @param isadev Average ISA deviation in degree C.
 * @param qnh Average QNH in Pa.
 * @param alt Cruise altitude in feet.
 * @param spd Airspeed, either as calibrated airspeed in knots, or Mach number.
 * @param is_mach B_TRUE if spd is mach, B_FALSE if spd is kcas.
 * @param hdg Cruise heading in degrees.
 * @param wind1 Cruise wind component at start of leg (direction of
 *	vector is direction of wind, magnitude is wind velocity in knots).
 * @param wind2 Cruise wind component at end of leg.
 * @param fuel Fuel status at start of leg in kilograms.
 * @param dist_nm Cruise flight leg length in nautical miles.
 * @param acft Aircraft performance data structure.
 * @param flt Flight performance data structure.
 *
 * @return Amount of fuel burned in kilograms.
 */
double
perf_crz2burn(double isadev, double tp_alt, double qnh, double alt_ft,
    double spd, bool_t is_mach, double hdg, vect2_t wind1, vect2_t wind2,
    double fuel, double dist_nm, const acft_perf_t *acft, const flt_perf_t *flt,
    double *ttg_out)
{
	vect2_t fltdir;
	double burn = 0;

	ASSERT(is_valid_alt_ft(alt_ft));
	ASSERT3F(spd, >, 0);
	ASSERT3F(spd, <, 1000);
	ASSERT(is_valid_hdg(hdg));
	ASSERT(!IS_NULL_VECT(wind1));
	ASSERT(!IS_NULL_VECT(wind2));
	ASSERT3F(dist_nm, >=, 0);
	ASSERT3F(dist_nm, <, 1e6);
	ASSERT(acft != NULL);
	ASSERT(flt != NULL);
	ASSERT3F(flt->zfw, >, 0);
	/* ttg_out can be NULL */
	if (ttg_out != NULL)
		*ttg_out = 0;

	fltdir = hdg2dir(hdg);
	if (!is_mach)
		spd = KT2MPS(spd);

	for (double dist_done = 0; dist_done < dist_nm;) {
		double rat = dist_done / dist_nm;
		double mass = flt->zfw + MAX(fuel - burn, 0);
		vect2_t wind = VECT2(wavg(wind1.x, wind2.y, rat),
		    wavg(wind1.y, wind2.y, rat));
		double wind_mps = KT2MPS(vect2_dotprod(fltdir, wind));

		crz_step(isadev, tp_alt, qnh, alt_ft, spd, is_mach,
		    wind_mps, mass, acft, flt, dist_nm, SECS_PER_STEP_CRZ,
		    &dist_done, &burn, ttg_out);
	}

	ASSERT(!isnan(burn));
	ASSERT(isfinite(burn));
	ASSERT3F(burn, >=, 0);
	return (burn);
}

double
perf_des2burn(const flt_perf_t *flt, const acft_perf_t *acft,
    double isadev, double qnh, double fuel, double hdgt,
    double dist_nm, double mach_lim,
    double alt1_ft, double kcas1, vect2_t wind1,
    double alt2_ft, double kcas2, vect2_t wind2,
    double *ttg_out)
{
	vect2_t fltdir;
	double burn = 0;

	ASSERT(flt != NULL);
	ASSERT3F(flt->zfw, >, 0);
	ASSERT(acft != NULL);
	ASSERT(!isnan(isadev));
	ASSERT(!isnan(qnh));
	ASSERT(!isnan(fuel));
	ASSERT(is_valid_hdg(hdgt));
	ASSERT3F(dist_nm, >=, 0);
	ASSERT3F(mach_lim, >=, 0);
	ASSERT(is_valid_alt_ft(alt1_ft));
	ASSERT3F(kcas1, >, 0);
	ASSERT3F(kcas1, <, 1000);
	ASSERT(!IS_NULL_VECT(wind1));
	ASSERT(is_valid_alt_ft(alt2_ft));
	ASSERT3F(kcas2, >, 0);
	ASSERT3F(kcas2, <, 1000);
	ASSERT(!IS_NULL_VECT(wind2));
	ASSERT3F(alt1_ft, >=, alt2_ft);
	/* ttg_out can be NULL */
	if (ttg_out != NULL)
		*ttg_out = 0;

	fltdir = hdg2dir(hdgt);

	for (double dist_done = 0; dist_done < NM2MET(dist_nm);) {
		double rat = dist_done / NM2MET(dist_nm);
		double alt_ft = wavg(alt1_ft, alt2_ft, rat);
		double kcas = wavg(kcas1, kcas2, rat);
		double mass = flt->zfw + MAX(fuel - burn, 0);
		vect2_t wind = VECT2(wavg(wind1.x, wind2.y, rat),
		    wavg(wind1.y, wind2.y, rat));
		double wind_mps = KT2MPS(vect2_dotprod(fltdir, wind));
		double p = alt2press(alt_ft, qnh);
		double fl = alt2fl(alt_ft, qnh);
		double oat = isadev2sat(fl, isadev);
		double kcas_lim_mach = mach2kcas(mach_lim, alt_ft, qnh, oat);
		bool_t is_mach;
		double tgt_spd, tas_mps, gs_mps, vs_mps, burn_step;
		double spd_mps_or_mach, dist_step;

		for (int i = 0; i < FLT_PERF_NUM_SPD_LIMS; i++) {
			if (alt_ft <= flt->des_spd_lim[i].alt_ft)
				kcas = MIN(kcas, flt->des_spd_lim[i].kias);
		}
		is_mach = (kcas > kcas_lim_mach);
		tgt_spd = (is_mach ? mach_lim : kcas);
		if (is_mach) {
			tgt_spd = mach_lim;
			tas_mps = KT2MPS(kcas2ktas(kcas_lim_mach, p, oat));
		} else {
			tgt_spd = kcas;
			tas_mps = KT2MPS(kcas2ktas(kcas, p, oat));
		}
		/*
		 * We must make sure we make forward progress,
		 * otherwise the solver can soft-lock.
		 */
		gs_mps = MAX(tas_mps + wind_mps, KT2MPS(60));
		vs_mps = (FEET2MET(alt2_ft) - FEET2MET(alt1_ft)) /
		    (NM2MET(dist_nm) / gs_mps);
		spd_mps_or_mach = (is_mach ? tgt_spd : KT2MPS(tgt_spd));

		burn_step = des_burn_step(isadev, FEET2MET(alt_ft), vs_mps,
		    spd_mps_or_mach, is_mach, mass, acft, SECS_PER_STEP);
		ASSERT3F(burn_step, >=, 0);
		dist_step = gs_mps * SECS_PER_STEP;
		if (dist_done + dist_step > NM2MET(dist_nm)) {
			double act_dist_step = NM2MET(dist_nm) - dist_done;
			double rat = act_dist_step / dist_step;
			burn += burn_step * rat;
			dist_done += act_dist_step;
			if (ttg_out != NULL)
				(*ttg_out) += SECS_PER_STEP * rat;
			break;
		}
		burn += burn_step;
		dist_done += dist_step;
		if (ttg_out != NULL)
			(*ttg_out) += SECS_PER_STEP;
	}

	return (burn);
}

double
perf_TO_spd(const flt_perf_t *flt, const acft_perf_t *acft)
{
	double mass = flt->zfw + flt->fuel;
	double lift = MASS2GFORCE(mass);
	double Cl = wavg(fx_lin_multi(acft->cl_max_aoa, acft->cl_curve, B_TRUE),
	    fx_lin_multi(acft->cl_flap_max_aoa, acft->cl_flap_curve, B_TRUE),
	    flt->to_flap);
	double Pd = lift / (Cl * acft->wing_area);
	double tas = sqrt((2 * Pd) / ISA_SL_DENS);
	return (MPS2KT(tas));
}

/*
 * Calculates the specific fuel consumption of the aircraft engines in
 * a given instant.
 *
 * @param acft Aircraft performance specification structure.
 * @param thr Total thrust requirement.
 *
 * @return The aircraft's engine's specific fuel consumption at the specified
 *	conditions in kg/hr.
 */
double
acft_get_sfc(const flt_perf_t *flt, const acft_perf_t *acft, double thr,
    double alt, double ktas, double qnh, double isadev, double tp_alt)
{
	/* "_AE" means "all-engines" */
	double max_thr_AE, min_thr_AE, throttle;
	double ff_hr;

	ASSERT(flt != NULL);
	ASSERT(acft != NULL);

	ff_hr = acft->eng_sfc * thr * SECS_PER_HR;
	max_thr_AE = eng_get_thrust(flt, acft, 1.0, alt, ktas, qnh, isadev,
	    tp_alt);
	min_thr_AE = eng_get_thrust(flt, acft, 0.0, alt, ktas, qnh, isadev,
	    tp_alt);
	throttle = iter_fract(thr, min_thr_AE, max_thr_AE, B_TRUE);

	return (ff_hr *
	    fx_lin_multi(throttle, acft->sfc_thro_curve, B_TRUE) *
	    fx_lin_multi(isadev, acft->sfc_isa_curve, B_TRUE));
}

double
perf_get_turn_rate(double bank_ratio, double gs_kts, const flt_perf_t *flt,
    const acft_perf_t *acft)
{
	double half_bank_rate, full_bank_rate;

	ASSERT3F(gs_kts, >=, 0);
	/* flt can be NULL */
	ASSERT(acft != NULL);

	if (bank_ratio == 0) {
		ASSERT(flt != NULL);
		ASSERT3F(flt->bank_ratio, >, 0);
		ASSERT3F(flt->bank_ratio, <=, 1.0);
		bank_ratio = flt->bank_ratio;
	} else {
		ASSERT3F(bank_ratio, >, 0);
		ASSERT3F(bank_ratio, <=, 1.0);
	}

	half_bank_rate = fx_lin_multi(gs_kts, acft->half_bank_curve, B_TRUE);
	if (bank_ratio <= 0.5)
		return (2 * bank_ratio * half_bank_rate);
	full_bank_rate = fx_lin_multi(gs_kts, acft->full_bank_curve, B_TRUE);

	return (wavg(half_bank_rate, full_bank_rate,
	    clamp(2 * (bank_ratio - 0.5), 0, 1)));
}

/*
 * Converts a true airspeed to Mach number.
 *
 * @param ktas True airspeed in knots.
 * @param oat Static outside air temperature in degrees C.
 *
 * @return Mach number.
 */
double
ktas2mach(double ktas, double oat)
{
	return (KT2MPS(ktas) / speed_sound(oat));
}

/*
 * Converts Mach number to true airspeed.
 *
 * @param ktas Mach number.
 * @param oat Static outside air temperature in degrees C.
 *
 * @return True airspeed in knots.
 */
double
mach2ktas(double mach, double oat)
{
	return (MPS2KT(mach * speed_sound(oat)));
}

/*
 * Converts true airspeed to calibrated airspeed.
 *
 * @param ktas True airspeed in knots.
 * @param pressure Static air pressure in Pa.
 * @param oat Static outside air temperature in degrees C.
 *
 * @return Calibrated airspeed in knots.
 */
double
ktas2kcas(double ktas, double pressure, double oat)
{
	return (impact_press2kcas(impact_press(ktas2mach(ktas, oat),
	    pressure)));
}

/*
 * Converts impact pressure to calibrated airspeed.
 *
 * @param impact_pressure Impact air pressure in Pa.
 *
 * @return Calibrated airspeed in knots.
 */
double
impact_press2kcas(double impact_pressure)
{
	return (MPS2KT(ISA_SPEED_SOUND * sqrt(5 *
	    (pow(impact_pressure / ISA_SL_PRESS + 1, 0.2857142857) - 1))));
}

double
kcas2mach(double kcas, double alt_ft, double qnh, double oat)
{
	double p = alt2press(alt_ft, qnh);
	double ktas = kcas2ktas(kcas, p, oat);
	return (ktas2mach(ktas, oat));
}

double mach2kcas(double mach, double alt_ft, double qnh, double oat)
{
	double ktas = mach2ktas(mach, oat);
	double p = alt2press(alt_ft, qnh);
	return (ktas2kcas(ktas, p, oat));
}

/*
 * Converts calibrated airspeed to true airspeed.
 *
 * @param ktas Calibrated airspeed in knots.
 * @param pressure Static air pressure in Pa.
 * @param oat Static outside air temperature in degrees C.
 *
 * @return True airspeed in knots.
 */
double
kcas2ktas(double kcas, double pressure, double oat)
{
	double	qc, mach;

	/*
	 * Take the CAS equation and solve for qc (impact pressure):
	 *
	 * qc = P0(((cas^2 / 5* a0^2) + 1)^3.5 - 1)
	 *
	 * Where P0 is pressure at sea level, cas is calibrated airspeed
	 * in m.s^-1 and a0 speed of sound at ISA temperature.
	 */
	qc = ISA_SL_PRESS * (pow((POW2(KT2MPS(kcas)) / (5 *
	    POW2(ISA_SPEED_SOUND))) + 1, 3.5) - 1);

	/*
	 * Next take the impact pressure equation and solve for Mach number:
	 *
	 * M = sqrt(5 * (((qc / P) + 1)^(2/7) - 1))
	 *
	 * Where qc is impact pressure and P is local static pressure.
	 */
	mach = sqrt(5 * (pow((qc / pressure) + 1, 0.2857142857142) - 1));

	/*
	 * Finally convert Mach number to true airspeed at local temperature.
	 */
	return (mach2ktas(mach, oat));
}

/*
 * Converts Mach number to equivalent airspeed (calibrated airspeed corrected
 * for compressibility).
 *
 * @param mach Mach number.
 * @param oat Static outside static air pressure in Pa.
 *
 * @return Equivalent airspeed in knots.
 */
double
mach2keas(double mach, double press)
{
	return (MPS2KT(ISA_SPEED_SOUND * mach * sqrt(press / ISA_SL_PRESS)));
}

/*
 * Converts equivalent airspeed (calibrated airspeed corrected for
 * compressibility) to Mach number.
 *
 * @param mach Equivalent airspeed in knots.
 * @param oat Static outside static air pressure in Pa.
 *
 * @return Mach number.
 */
double
keas2mach(double keas, double press)
{
	/*
	 * Take the mach-to-EAS equation and solve for Mach number:
	 *
	 * M = Ve / (a0 * sqrt(P / P0))
	 *
	 * Where Ve is equivalent airspeed in m.s^-1, P is local static
	 * pressure and P0 is ISA sea level pressure (in Pa).
	 */
	return (KT2MPS(keas) / (ISA_SPEED_SOUND * sqrt(press / ISA_SL_PRESS)));
}

/*
 * Calculates static air pressure from pressure altitude under ISA conditions.
 *
 * @param alt Pressure altitude in feet.
 * @param qnh Local QNH in Pa.
 *
 * @return Air pressure in Pa.
 */
double
alt2press(double alt_ft, double qnh_Pa)
{
	return (alt2press_baro(FEET2MET(alt_ft), qnh_Pa, ISA_SL_TEMP_K,
	    EARTH_GRAVITY));
}

/*
 * Calculates pressure altitude from static air pressure under ISA conditions.
 *
 * @param press Static air pressure in Pa.
 * @param qnh Local QNH in Pa.
 *
 * @return Pressure altitude in feet.
 */
double
press2alt(double press_Pa, double qnh_Pa)
{
	return (MET2FEET(press2alt_baro(press_Pa, qnh_Pa, ISA_SL_TEMP_K,
	    EARTH_GRAVITY)));
}

double
alt2press_baro(double alt_m, double p0_Pa, double T0_K, double g_mss)
{
	ASSERT3F(p0_Pa, >, 0);
	ASSERT3F(T0_K, >, 0);
	ASSERT3F(g_mss, >, 0);
	/*
	 * Standard barometric formula:
	 *                       g.M
	 *           /    L.h \^ ----
	 * p = p0 * ( 1 - ---  ) R0.L
	 *           \     T0 /
	 * Where:
	 * p	current outside pressure [Pa]
	 * p0	reference sea level pressure [Pa] (NOT QNH!)
	 * L	temperature lapse rate [K/m]
	 * h	height above mean sea level [m]
	 * T0	reference sea level temperature [K]
	 * g	gravitational acceleration [m/s^2]
	 * M	molar mass of dry air [kg/mol]
	 * R0	universal gas constant [J/(mol.K)]
	 */
	return (p0_Pa * pow(1 - ((ISA_TLR_PER_1M * alt_m) / T0_K),
	    (g_mss * DRY_AIR_MOL) / (R_univ * ISA_TLR_PER_1M)));
}

double
press2alt_baro(double p_Pa, double p0_Pa, double T0_K, double g_mss)
{
	ASSERT3F(p0_Pa, >, 0);
	ASSERT3F(T0_K, >, 0);
	ASSERT3F(g_mss, >, 0);
	/*
	 * This is the barometric formula, solved for 'h':
	 *                          R0.L
	 *           /      / p  \^ ---- \
	 *     T0 * (  1 - ( ---- ) g.M   )
	 *           \      \ p0 /       /
	 * h = ----------------------------
	 *                 L
	 * Where:
	 * h	height above mean sea level [m]
	 * p	current outside pressure [Pa]
	 * p0	reference sea level pressure [Pa] (NOT QNH!)
	 * R0	universal gas constant [J/(mol.K)]
	 * L	temperature lapse rate [K/m]
	 * g	gravitational acceleration [m/s^2]
	 * M	molar mass of dry air [kg/mol]
	 * T0	reference sea level temperature [K]
	 */
	return ((T0_K * (1 - pow(p_Pa / p0_Pa, (R_univ * ISA_TLR_PER_1M) /
	    (g_mss * DRY_AIR_MOL)))) / ISA_TLR_PER_1M);
}

/*
 * Converts pressure altitude to flight level.
 *
 * @param alt Pressure altitude in feet.
 * @param qnh Local QNH in hPa.
 *
 * @return Flight level number.
 */
double
alt2fl(double alt_ft, double qnh)
{
	return (press2alt(alt2press(alt_ft, qnh), ISA_SL_PRESS) / 100);
}

/*
 * Converts flight level to pressure altitude.
 *
 * @param fl Flight level number.
 * @param qnh Local QNH in hPa.
 *
 * @return Pressure altitude in feet.
 */
double
fl2alt(double fl, double qnh)
{
	return (press2alt(alt2press(fl * 100, ISA_SL_PRESS), qnh));
}

/*
 * Converts static air temperature to total air temperature.
 *
 * @param sat Static air temperature in degrees C.
 * @param mach Flight mach number.
 *
 * @return Total air temperature in degrees C.
 */
double
sat2tat(double sat, double mach)
{
	return (KELVIN2C(C2KELVIN(sat) * (1 + ((GAMMA - 1) / 2) * POW2(mach))));
}

/*
 * Converts total air temperature to static air temperature.
 *
 * @param tat Total air temperature in degrees C.
 * @param mach Flight mach number.
 *
 * @return Static air temperature in degrees C.
 */
double
tat2sat(double tat, double mach)
{
	return (KELVIN2C(C2KELVIN(tat) / (1 + ((GAMMA - 1) / 2) * POW2(mach))));
}

/*
 * Converts static air temperature to ISA deviation. This function makes
 * no assumptions about a tropopause. To implement a tropopause, clamp
 * the passed `fl' value at the tropopause level (ISA_TP_ALT).
 *
 * @param fl Flight level (barometric altitude at QNE in 100s of ft).
 * @param sat Static air temperature in degrees C.
 *
 * @return ISA deviation in degress C.
 */
double
sat2isadev(double fl, double sat)
{
	fl = MIN(fl, ISA_TP_ALT / 100);
	return (sat - (ISA_SL_TEMP_C - ((fl / 10) * ISA_TLR_PER_1000FT)));
}

/*
 * Converts ISA deviation to static air temperature.
 *
 * @param fl Flight level (barometric altitude at QNE in 100s of ft).
 * @param isadev ISA deviation in degrees C.
 *
 * @return Local static air temperature.
 */
double
isadev2sat(double fl, double isadev)
{
	fl = MIN(fl, ISA_TP_ALT / 100);
	return (isadev + ISA_SL_TEMP_C - ((fl / 10) * ISA_TLR_PER_1000FT));
}

/*
 * Returns the speed of sound in m/s in dry air at `oat' degrees C (static).
 */
double
speed_sound(double oat)
{
	return (speed_sound_gas(C2KELVIN(oat), GAMMA, R_spec));
}

/*
 * Returns the speed of sound in a specific gas. Unlike the speed_sound()
 * function, this function takes absolute temperature in Kelvin. You must
 * also pass the gas-specific constants (ratio of specific heats and gas
 * constant). These depend upon the molecular composition of the gas. For
 * dry air, these are defined in perf.h as GAMMA (1.4) and R_spec (287.058).
 */
double
speed_sound_gas(double T, double gamma, double R)
{
	return (sqrt(gamma * R * T));
}

/*
 * Calculates air density of dry air.
 *
 * @param pressure Static air pressure in Pa.
 * @param oat Static outside air temperature in degrees C.
 *
 * @return Local air density in kg.m^-3.
 */
double
air_density(double pressure, double oat)
{
	return (gas_density(pressure, oat, R_spec));
}

/*
 * Calculates density of an arbitrary gas.
 *
 * @param pressure Static air pressure in Pa.
 * @param oat Static outside air temperature in degrees C.
 * @param gas_const Specific gas constant of the gas in question (J/kg/K).
 *
 * @return Local gas density in kg.m^-3.
 */
double
gas_density(double pressure, double oat, double gas_const)
{
	/*
	 * Density of a gas is:
	 *
	 * rho = p / (gas_const * T)
	 *
	 * Where p is local static gas pressure, gas_const is the specific gas
	 * constant for the gas in question and T is absolute temperature.
	 */
	return (pressure / (gas_const * C2KELVIN(oat)));
}

/*
 * Calculates impact pressure. This is dynamic pressure with air
 * compressibility considered.
 *
 * @param pressure Static air pressure in Pa.
 * @param mach Flight mach number.
 *
 * @return Impact pressure in Pa.
 */
double
impact_press(double mach, double pressure)
{
	/*
	 * In isentropic flow, impact pressure for air (gamma = 1.4) is:
	 *
	 * qc = P((1 + 0.2 * M^2)^(7/2) - 1)
	 *
	 * Where P is local static air pressure and M is flight mach number.
	 */
	return (pressure * (pow(1 + 0.2 * POW2(mach), 3.5) - 1));
}

/*
 * Calculates dynamic pressure in dry air.
 *
 * @param pressure True airspeed in knots.
 * @param press Static air pressure in Pa.
 * @param oat Static outside air temperature in degrees C.
 *
 * @return Dynamic pressure in Pa.
 */
double
dyn_press(double ktas, double press, double oat)
{
	return (dyn_gas_press(ktas, press, oat, R_spec));
}

/*
 * Same as dyn_press, but takes an exclicit specific gas constant parameter
 * to allow for calculating dynamic pressure in other gases.
 */
double
dyn_gas_press(double ktas, double press, double oat, double gas_const)
{
	double p = (0.5 * gas_density(press, oat, gas_const) *
	    POW2(KT2MPS(ktas)));
	if (ktas < 0)
		return (-p);
	return (p);
}

/*
 * Computes static dry air pressure from air density and temperature.
 *
 * @param rho Air density in kg/m^3.
 * @param oat Static air temperature in Celsius.
 *
 * @return Static air pressure in Pascals.
 */
double
static_press(double rho, double oat)
{
	return (static_gas_press(rho, oat, R_spec));
}

/*
 * Same as static_press, but takes an explicit specific gas constant
 * argument to allow computing static pressure in any gas.
 */
double
static_gas_press(double rho, double oat, double gas_const)
{
	/*
	 * Static pressure of a gas is:
	 *
	 * p = rho * gas_const * T
	 *
	 * Where rho is the local gas density, gas_const is the specific gas
	 * constant for the gas in question and T is absolute temperature.
	 */
	return (rho * gas_const * C2KELVIN(oat));
}

/*
 * Same as `adiabatic_heating_gas', but using the ratio of specific heats
 * for dry air at approximately room temperature.
 */
double
adiabatic_heating(double press_ratio, double start_temp)
{
	return (adiabatic_heating_gas(press_ratio, start_temp, GAMMA));
}

/*
 * Computes the adiabatic heating experienced by air when compressed in a
 * turbine engine's compressor. The P-T relation for adiabatic heating is:
 *
 *	P1^(1 - gamma) T1^(gamma) = P2^(1 - gamma) T2^(gamma)
 *
 * Solving for T2:
 *
 *	T2 = ((P1^(1 - Gamma) T1^(Gamma)) / P2^(1 - Gamma))^(1 / Gamma)
 *
 * Since P2 / P1 is the compressor pressure ratio, we can cancel out P1
 * to '1' and simply replace P2 by the pressure ratio P_r:
 *
 *	T2 = (T1^(Gamma) / P_r^(1 - Gamma))^(1 / Gamma)
 *
 * @param press_ratio Pressure ratio of the compressor (dimensionless).
 * @param start_temp Starting gas temperature (in degrees Celsius).
 * @param gamma The gas' ratio of specific heats.
 *
 * @return Compressed gas temperature (in degrees Celsius).
 */
double
adiabatic_heating_gas(double press_ratio, double start_temp, double gamma)
{
	return (KELVIN2C(pow(pow(C2KELVIN(start_temp), gamma) /
	    pow(press_ratio, 1 - gamma), 1 / gamma)));
}

double
air_kin_visc(double temp_K)
{
	const vect2_t table[] = {
	    VECT2(200, 0.753e-5),
	    VECT2(225, 0.935e-5),
	    VECT2(250, 1.132e-5),
	    VECT2(275, 1.343e-5),
	    VECT2(300, 1.568e-5),
	    VECT2(325, 1.807e-5),
	    VECT2(350, 2.056e-5),
	    VECT2(375, 2.317e-5),
	    VECT2(400, 2.591e-5),
	    NULL_VECT2	/* list terminator */
	};
	ASSERT3F(temp_K, >, 0);
	return (fx_lin_multi(temp_K, table, B_TRUE));
}

double
air_reynolds(double vel, double chord, double temp_K)
{
	ASSERT(!isnan(vel));
	ASSERT3F(chord, >, 0);
	ASSERT3F(temp_K, >, 0);
	return ((vel * chord) / air_kin_visc(temp_K));
}

double
lacf_gamma_air(double T)
{
	const vect2_t curve[] = {
	    VECT2(250,	1.401),
	    VECT2(300,	1.4),
	    VECT2(350,	1.398),
	    VECT2(400,	1.395),
	    VECT2(450,	1.391),
	    VECT2(500,	1.387),
	    VECT2(550,	1.381),
	    VECT2(600,	1.376),
	    VECT2(650,	1.37),
	    VECT2(700,	1.364),
	    VECT2(750,	1.359),
	    VECT2(800,	1.354),
	    VECT2(900,	1.344),
	    VECT2(1000,	1.336),
	    VECT2(1100,	1.331),
	    VECT2(1200,	1.324),
	    VECT2(1300,	1.318),
	    VECT2(1400,	1.313),
	    VECT2(1500,	1.309),
	    NULL_VECT2
	};
	return (fx_lin_multi(T, curve, B_TRUE));
}

double
lacf_therm_cond_air(double T)
{
	/*
	 * The thermal conductivity of air remains relatively constant
	 * throughout its pressure range and deviates by less than 1%
	 * down to about 1% of sea level pressure. Since we never really
	 * encounter such pressures in aircraft (1% SL_p = ~84,000 ft),
	 * we can just ignore the pressure component.
	 */
	ASSERT3F(T, >, 0);
	return (fx_lin(T, 233.2, 0.0209, 498.15, 0.0398));
}

double
lacf_therm_cond_aluminum(double T)
{
	const vect2_t curve[] = {
	    VECT2(C2KELVIN(200), 237),
	    VECT2(C2KELVIN(273), 236),
	    VECT2(C2KELVIN(400), 240),
	    VECT2(C2KELVIN(600), 232),
	    VECT2(C2KELVIN(800), 220),
	    NULL_VECT2	/* list terminator */
	};
	ASSERT3F(T, >, 0);
	return (fx_lin_multi(T, curve, B_TRUE));
}

double
lacf_therm_cond_glass(double T)
{
	/* Based on Pyrex 7740, NBS, 1966 */
	const vect2_t curve[] = {
	    VECT2(100, 0.58),
	    VECT2(200, 0.90),
	    VECT2(300, 1.11),
	    VECT2(400, 1.25),
	    VECT2(500, 1.36),
	    VECT2(600, 1.50),
	    VECT2(700, 1.62),
	    VECT2(800, 1.89),
	    NULL_VECT2	/* list terminator */
	};
	ASSERT3F(T, >, 0);
	return (fx_lin_multi(T, curve, B_TRUE));
}

double
earth_gravity_accurate(double lat, double alt)
{
	/*
	 * Based on https://www.engineeringtoolbox.com/docs/documents/1554/
	 * acceleration-gravity-latitude-meter-second-second.png
	 */
	const vect2_t lat_curve[] = {
	    VECT2(0, 9.781),
	    VECT2(10, 9.782),
	    VECT2(20, 9.787),
	    VECT2(30, 9.793),
	    VECT2(60, 9.819),
	    VECT2(70, 9.826),
	    VECT2(80, 9.831),
	    VECT2(90, 9.833),
	    NULL_VECT2  /* list terminator */
	};
	const double delta_per_m = -0.00000305;

	ASSERT3F(lat, >=, -90);
	ASSERT3F(lat, <=, 90);
	ASSERT(isfinite(alt));

	return (fx_lin_multi(ABS(lat), lat_curve, B_FALSE) + alt * delta_per_m);
}
