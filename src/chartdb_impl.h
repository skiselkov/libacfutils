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
 * Copyright 2018 Saso Kiselkov. All rights reserved.
 */

#ifndef	_CHARTDB_IMPL_H_
#define	_CHARTDB_IMPL_H_

#include <cairo.h>
#include <time.h>

#include "acfutils/avl.h"
#include "acfutils/chartdb.h"
#include "acfutils/list.h"
#include "acfutils/thread.h"
#include "acfutils/worker.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct chart_arpt_s chart_arpt_t;
typedef struct chart_s chart_t;

typedef cairo_surface_t *(*chart_load_cb_t)(chart_t *chart);

typedef enum {
	PROV_AERONAV_FAA_GOV,
	PROV_AUTOROUTER_AERO,
	PROV_NAVIGRAPH,
	NUM_PROVIDERS
} chart_prov_id_t;

struct chart_s {
	/* immutable once created */
	chart_arpt_t	*arpt;
	char		*name;
	char		*codename;
	chart_type_t	type;
	char		*filename;
	char		*filename_night;
	chart_procs_t	procs;

	chart_georef_t	georef;

	chart_load_cb_t	load_cb;

	/* protected by chartdb_t->lock */
	cairo_surface_t	*surf;
	double		zoom;
	int		cur_page;
	int		load_page;
	int		num_pages;
	bool_t		load_error;
	bool_t		night;
	bool_t		night_prev;
	bool_t		refreshed;
	/* Only present when `disallow_caching' is set in chartdb_t */
	void		*png_data;
	size_t		png_data_len;

	avl_node_t	node;
	list_node_t	loader_node;
	list_node_t	load_seq_node;
};

struct chart_arpt_s {
	/* immutable once created */
	chartdb_t	*db;
	char		icao[8];
	char		*name;
	char		*city;
	char		state[4];
	avl_tree_t	charts;
	char		*metar;
	time_t		metar_load_t;
	char		*taf;
	time_t		taf_load_t;
	char		*codename;
	bool_t		load_complete;

	avl_node_t	node;
	list_node_t	loader_node;
};

struct chartdb_s {
	mutex_t		lock;
	worker_t	loader;
	bool_t		loader_stop;	/* set only once on exit */

	/* immutable once created */
	unsigned	airac;
	char		*path;
	char		*pdftoppm_path;
	char		*pdfinfo_path;
	chart_prov_id_t	prov;
	char		prov_name[32];
	avl_tree_t	arpts;
	bool_t		normalize_non_icao;

	/* immutable after provider init */
	bool_t		flat_db;
	bool_t		disallow_caching;
	int		(*chart_sort_func)(const void *, const void *, void *);

	/* private to chart provider */
	void		*prov_info;
	void		*prov_priv;
	bool_t		init_complete;

	/* protected by `lock' */
	list_t		loader_queue;
	list_t		loader_arpt_queue;
	list_t		load_seq;
	uint64_t	load_limit;

	/* protected by `lock' */
	char		*proxy;

	chart_t		loader_cmd_purge;
	chart_t		loader_cmd_metar;
	chart_t		loader_cmd_taf;
};

typedef struct {
	const char	*name;
	bool_t		(*init)(chartdb_t *cdb);
	void		(*fini)(chartdb_t *cdb);
	bool_t		(*get_chart)(chart_t *chart);
	void		(*watermark_chart)(chart_t *chart,
	    cairo_surface_t *surf);
	chart_arpt_t 	*(*arpt_lazy_discover)(chartdb_t *cdb, const char *icao);
	void		(*arpt_lazyload)(chart_arpt_t *arpt);
	bool_t		(*test_conn)(const chart_prov_info_login_t *creds,
	    const char *proxy);
	bool_t		(*pending_ext_account_setup)(chartdb_t *cdb);
} chart_prov_t;

chart_arpt_t *chartdb_add_arpt(chartdb_t *cdb, const char *icao,
    const char *name, const char *city_name, const char *state_id);
bool_t chartdb_add_chart(chart_arpt_t *arpt, chart_t *chart);
void chartdb_chart_destroy(chart_t *chart);
char *chartdb_mkpath(chart_t *chart);

char *chartdb_pdf_convert_file(const char *pdftoppm_path, char *old_path,
    int page, double zoom);
uint8_t *chartdb_pdf_convert_direct(const char *pdftoppm_path,
    const uint8_t *pdf_data, size_t len, int page, double zoom,
    size_t *out_len);
int chartdb_pdf_count_pages_file(const char *pdfinfo_path, const char *path);
int chartdb_pdf_count_pages_direct(const char *pdfinfo_path,
    const uint8_t *buf, size_t len);

#ifdef	__cplusplus
}
#endif

#endif	/* _CHARTDB_IMPL_H_ */
