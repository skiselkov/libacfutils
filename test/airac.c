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

#include <time.h>

#include <acfutils/helpers.h>

int
main(void)
{
	int airac;

	for (time_t now = time(NULL); (airac = airac_time2cycle(now)) != -1;
	    now += 86400) {
		char buf[32];
		char exp_date[32];

		strftime(buf, sizeof (buf), "%Y-%m-%d", gmtime(&now));
		airac_cycle2exp_date(airac, exp_date);
		printf("%-24s %d %16s %16s\n", buf, airac,
		    airac_cycle2eff_date(airac), exp_date);
	}

	return (0);
}
