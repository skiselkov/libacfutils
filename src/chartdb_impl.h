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

#include "acfutils/avl.h"
#include "acfutils/list.h"
#include "acfutils/thread.h"
#include "acfutils/worker.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	MAX_CHART_NAME		48
#define	MAX_CHART_FILENAME	32

typedef enum {
	CHART_TYPE_UNKNOWN,	/* Unknown chart type */
	CHART_TYPE_APD,		/* Airport Diagram */
	CHART_TYPE_IAP,		/* Instrument Approach Procedure */
	CHART_TYPE_DP,		/* Departure Procedure */
	CHART_TYPE_ODP,		/* Obstacle Departure Procedure */
	CHART_TYPE_STAR,	/* Standard Terminal Arrival */
	CHART_TYPE_MIN,		/* Takeoff Minimums */
	NUM_CHART_TYPES
} chart_type_t;

typedef struct chart_arpt_s chart_arpt_t;
typedef struct chartdb_s chartdb_t;

typedef enum {
	PROV_AERONAV_FAA_GOV,
	NUM_PROVIDERS
} chart_prov_id_t;

typedef struct {
	/* immutable once created */
	chart_arpt_t	*arpt;
	char		name[MAX_CHART_NAME];
	char		codename[MAX_CHART_NAME];
	chart_type_t	type;
	char		filename[MAX_CHART_FILENAME];

	/* protected by chartdb_t->lock */
	unsigned	refcnt;
	condvar_t	refcnt_cv;
	uint8_t		*pixels;
	unsigned	width;
	unsigned	height;
	double		zoom;
	double		rotate;

	avl_node_t	node;
	list_node_t	loader_node;
} chart_t;

struct chart_arpt_s {
	/* immutable once created */
	chartdb_t	*db;
	char		icao[8];
	avl_tree_t	charts;

	avl_node_t	node;
};

struct chartdb_s {
	mutex_t		lock;
	worker_t	loader;

	/* immutable once created */
	unsigned	airac;
	char		*path;
	char		*imagemagick_path;
	chart_prov_id_t	prov;
	char		prov_name[32];
	avl_tree_t	arpts;

	/* private to chart provider */
	void		*prov_info;
	void		*prov_priv;

	/* protected by `lock' */
	list_t		loader_queue;
};

typedef struct {
	const char	*name;
	bool_t		(*init)(chartdb_t *cdb);
	void		(*fini)(chartdb_t *cdb);
	bool_t		(*get_chart)(chart_t *chart);
} chart_prov_t;

chart_arpt_t *chartdb_add_arpt(chartdb_t *cdb, const char *icao);
bool_t chartdb_add_chart(chart_arpt_t *arpt, chart_t *chart);
char *chartdb_mkpath(chart_t *chart);

#ifdef	__cplusplus
}
#endif

#endif	/* _CHARTDB_IMPL_H_ */
