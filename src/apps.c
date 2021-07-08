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
 * Copyright 2021 Saso Kiselkov. All rights reserved.
 */

#include <errno.h>
#include <string.h>

#include "acfutils/apps.h"
#include "acfutils/helpers.h"

bool_t
lacf_open_URL(const char *url)
{
	char *cmd;
	bool_t result = B_TRUE;

#if	IBM || APL
	cmd = sprintf_alloc("open \"%s\"", url);
#else	/* LIN */
	cmd = sprintf_alloc("xdg-open \"%s\"", url);
#endif	/* LIN */
	if (system(cmd) < 0) {
		logMsg("Can't open URL %s: cannot run open command: %s", url,
		    strerror(errno));
		result = B_FALSE;
	}
	free(cmd);

	return (result);
}
