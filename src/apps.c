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
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

#if	IBM
#include <windows.h>
#endif
#include <errno.h>
#include <string.h>

#include "acfutils/apps.h"
#include "acfutils/helpers.h"

/**
 * \fn bool_t lacf_open_URL(const char *)
 * Given a URL, attempts to open it in the host operating system's
 * preferred web browser.
 * @param url A URL that can be passed on to a web browser.
 * @return `B_TRUE` if launching the browser succeeded, `B_FALSE` otherwise.
 *	This always succeeds on Windows. On macOS and Linux, this depends on
 *	the return value of the shell command (`open` on macOS and `xdg-open`
 *	on Linux).
 */

#if	IBM

bool_t
lacf_open_URL(const char *url)
{
	ASSERT(url != NULL);
	ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
	return (B_TRUE);
}

#elif	APL || LIN

bool_t
lacf_open_URL(const char *url)
{
	char *cmd;
	bool_t result = B_TRUE;

	ASSERT(url != NULL);
#if	APL
	cmd = sprintf_alloc("open \"%s\"", url);
#else	/* LIN */
	cmd = sprintf_alloc("xdg-open \"%s\"", url);
#endif
	if (system(cmd) < 0) {
		logMsg("Can't open URL %s: cannot run open command: %s", url,
		    strerror(errno));
		result = B_FALSE;
	}
	free(cmd);

	return (result);
}

#endif	/* APL || LIN */
