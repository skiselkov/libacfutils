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

#ifndef	_ACFUTILS_PID_CTL_PARSING_H_
#define	_ACFUTILS_PID_CTL_PARSING_H_

#include "assert.h"
#include "conf.h"
#include "pid_ctl.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void
pid_ctl_parse2(pid_ctl_t *pid, const conf_t *conf, const char *prefix,
    bool noreset)
{
	double k_p = 0, k_i = 0, lim_i = 0, k_d = 0, r_d = 0;
	bool_t integ_clamp;

	ASSERT(pid != NULL);
	ASSERT(conf != NULL);
	ASSERT(prefix != NULL);

	conf_get_d_v(conf, "%s/k_p", &k_p, prefix);
	conf_get_d_v(conf, "%s/k_i", &k_i, prefix);
	conf_get_d_v(conf, "%s/lim_i", &lim_i, prefix);
	conf_get_d_v(conf, "%s/k_d", &k_d, prefix);
	conf_get_d_v(conf, "%s/r_d", &r_d, prefix);
	conf_get_b_v(conf, "%s/integ_clamp", &integ_clamp, prefix);

	if (noreset)
		pid_ctl_init_noreset(pid, k_p, k_i, lim_i, k_d, r_d);
	else
		pid_ctl_init(pid, k_p, k_i, lim_i, k_d, r_d);
	pid_ctl_set_integ_clamp(pid, integ_clamp);
}

static inline void
pid_ctl_parse(pid_ctl_t *pid, const conf_t *conf, const char *prefix)
{
	pid_ctl_parse2(pid, conf, prefix, false);
}

#ifdef __cplusplus
}
#endif

#endif	/* _ACFUTILS_PID_CTL_PARSING_H_ */
