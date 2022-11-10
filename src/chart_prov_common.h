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

#ifndef	_ACF_UTILS_CHART_PROV_COMMON_H_
#define	_ACF_UTILS_CHART_PROV_COMMON_H_

#include <curl/curl.h>

#include "acfutils/core.h"
#include "acfutils/chartdb.h"
#include "chartdb_impl.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct {
	const char	*url;
	chartdb_t	*cdb;
	uint8_t		*buf;
	size_t		bufcap;
	size_t		bufsz;
} chart_dl_info_t;

bool_t chart_download_multi(CURL **curl_p, chartdb_t *cdb, const char *url,
    const char *filepath, const char *method,
    const chart_prov_info_login_t *login, int timeout,
    const char *error_prefix, chart_dl_info_t *raw_output);
bool_t chart_download_multi2(CURL **curl_p, const char *proxy, const char *url,
    const char *filepath, const char *method,
    const chart_prov_info_login_t *login, int timeout,
    const char *error_prefix, chart_dl_info_t *raw_output);
bool_t chart_download(chartdb_t *cdb, const char *url, const char *filepath,
    const chart_prov_info_login_t *login, const char *error_prefix,
    chart_dl_info_t *raw_output);
void chart_setup_curl(CURL *curl, const char *cainfo);
void word_subst(char *name, const char **subst);
bool_t chartdb_want_to_stop(chartdb_t *cdb);

/*
 * writefunction for curl that uses a chart_dl_info_t
 */
void chart_dl_info_init(chart_dl_info_t *info, chartdb_t *cdb, const char *url);
void chart_dl_info_fini(chart_dl_info_t *info);
size_t chart_dl_info_write(char *ptr, size_t size, size_t nmemb,
    void *userdata);

void *chart_get_prov_info(const chart_t *chart, chartdb_t **cdb_p,
    chart_arpt_t **arpt_p);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_CHART_PROV_COMMON_H_ */
