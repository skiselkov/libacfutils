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
 * Copyright 2018 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACF_UTILS_CHARTDB_H_
#define	_ACF_UTILS_CHARTDB_H_

#include <stdlib.h>

#include <cairo.h>

#include "geom.h"
#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	MAX_CHART_INSETS	16
#define	MAX_CHART_PROCS		24

typedef struct chartdb_s chartdb_t;

typedef enum {
	CHART_TYPE_UNKNOWN = 0,		/* Unknown chart type */
	CHART_TYPE_APD = 1 << 0,	/* Airport Diagram */
	CHART_TYPE_IAP = 1 << 1,	/* Instrument Approach Procedure */
	CHART_TYPE_DP = 1 << 2,		/* Departure Procedure */
	CHART_TYPE_ODP = 1 << 3,	/* Obstacle Departure Procedure */
	CHART_TYPE_STAR = 1 << 4,	/* Standard Terminal Arrival */
	CHART_TYPE_MIN = 1 << 5,	/* Takeoff Minimums */
	CHART_TYPE_INFO = 1 << 6,	/* Airport Information */
	CHART_TYPE_ALL = 0xffffffffu
} chart_type_t;

typedef struct {
	char	*username;
	char	*password;
	char	*cainfo;
} chart_prov_info_login_t;

typedef struct {
	vect2_t		pts[2];
} chart_bbox_t;

typedef struct {
	bool_t		present;
	vect2_t		pixels[2];
	geo_pos2_t	pos[2];
	size_t		n_insets;
	chart_bbox_t	insets[MAX_CHART_INSETS];
} chart_georef_t;

typedef struct {
	size_t		n_procs;
	char		procs[MAX_CHART_PROCS][8];
} chart_procs_t;

API_EXPORT chartdb_t *chartdb_init(const char *cache_path,
    const char *pdftoppm_path, const char *pdfinfo_path,
    unsigned airac, const char *provider_name, void *provider_info);
API_EXPORT void chartdb_fini(chartdb_t *cdb);
API_EXPORT bool_t chartdb_test_connection(const char *provider_name,
    const chart_prov_info_login_t *creds);
API_EXPORT bool_t chartdb_test_connection2(const char *provider_name,
    const chart_prov_info_login_t *creds, const char *proxy);

API_EXPORT void chartdb_set_load_limit(chartdb_t *cdb, uint64_t bytes);
API_EXPORT void chartdb_purge(chartdb_t *cdb);

API_EXPORT void chartdb_set_proxy(chartdb_t *cdb, const char *proxy);
API_EXPORT size_t chartdb_get_proxy(chartdb_t *cdb, char *proxy, size_t cap);

API_EXPORT char **chartdb_get_chart_names(chartdb_t *cdb, const char *icao,
    chart_type_t type, size_t *num_charts);
API_EXPORT void chartdb_free_str_list(char **l, size_t num);

API_EXPORT char *chartdb_get_chart_codename(chartdb_t *cdb,
    const char *icao, const char *chart_name);
API_EXPORT chart_type_t chartdb_get_chart_type(chartdb_t *cdb,
    const char *icao, const char *chart_name);
API_EXPORT chart_georef_t chartdb_get_chart_georef(chartdb_t *cdb,
    const char *icao, const char *chart_name);
API_EXPORT chart_procs_t chartdb_get_chart_procs(chartdb_t *cdb,
    const char *icao, const char *chart_name);

API_EXPORT bool_t chartdb_get_chart_surface(chartdb_t *cdb,
    const char *icao, const char *chart_name, int page, double zoom,
    bool_t night, cairo_surface_t **surf, int *num_pages);

API_EXPORT bool_t chartdb_is_ready(chartdb_t *cdb);
API_EXPORT bool_t chartdb_is_arpt_known(chartdb_t *cdb, const char *icao);
API_EXPORT char *chartdb_get_arpt_name(chartdb_t *cdb, const char *icao);
API_EXPORT char *chartdb_get_arpt_city(chartdb_t *cdb, const char *icao);
API_EXPORT char *chartdb_get_arpt_state(chartdb_t *cdb, const char *icao);
API_EXPORT char *chartdb_get_metar(chartdb_t *cdb, const char *icao);
API_EXPORT char *chartdb_get_taf(chartdb_t *cdb, const char *icao);
API_EXPORT bool_t chartdb_pending_ext_account_setup(chartdb_t *cdb);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_CHARTDB_H_ */
