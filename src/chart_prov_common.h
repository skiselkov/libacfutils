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
} dl_info_t;

bool_t chart_download_multi(CURL **curl_p, chartdb_t *cdb, const char *url,
    const char *filepath, const char *method, const char *username,
    const char *password, int timeout, const char *error_prefix,
    dl_info_t *raw_output);
bool_t chart_download(chartdb_t *cdb, const char *url,
    const char *filepath, const char *error_prefix, dl_info_t *raw_output);
void word_subst(char *name, const char **subst);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_CHART_PROV_COMMON_H_ */
