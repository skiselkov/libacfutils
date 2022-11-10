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

#ifndef	_ACF_UTILS_CHART_PROV_AUTOROUTER_H_
#define	_ACF_UTILS_CHART_PROV_AUTOROUTER_H_

#include "acfutils/core.h"
#include "acfutils/chartdb.h"
#include "chartdb_impl.h"

#ifdef	__cplusplus
extern "C" {
#endif

bool_t chart_autorouter_init(chartdb_t *cdb);
void chart_autorouter_fini(chartdb_t *cdb);
bool_t chart_autorouter_get_chart(chart_t *chart);
void chart_autorouter_arpt_lazyload(chart_arpt_t *arpt);
bool_t chart_autorouter_test_conn(const chart_prov_info_login_t *creds,
    const char *proxy);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_CHART_PROV_AUTOROUTER_H_ */
