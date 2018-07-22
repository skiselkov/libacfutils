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

#include <acfutils/core.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct chartdb_s chartdb_t;

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

API_EXPORT chartdb_t *chartdb_init(const char *cache_path,
    const char *imagemagick_path, unsigned airac,
    const char *provider_name, void *provider_info);
API_EXPORT void chartdb_fini(chartdb_t *cdb);

API_EXPORT void chartdb_purge(chartdb_t *cdb);

API_EXPORT char **chartdb_get_chart_names(chartdb_t *cdb, const char *icao,
    chart_type_t type, size_t *num_charts);
API_EXPORT void chartdb_get_chart_codename(chartdb_t *cdb, const char *icao,
    const char *chartname, char codename[32]);
API_EXPORT void chartdb_free_str_list(char **l, size_t num);

API_EXPORT uint8_t *chartdb_get_chart_image(chartdb_t *cdb, const char *icao,
    const char *chart_name, double zoom, double rotate,
    unsigned *width, unsigned *height);
API_EXPORT void chartdb_release_chart_image(chartdb_t *cdb,
    const char *icao, const char *chart_name);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_CHARTDB_H_ */