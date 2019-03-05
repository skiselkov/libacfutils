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
 * Copyright 2019 Saso Kiselkov. All rights reserved.
 */

#include <stdlib.h>

#include "acfutils/glew.h"

THREAD_LOCAL GLEWContext lacf_glew_per_thread_ctx;

#if	APL || LIN

pthread_key_t lacf_glew_ctx_key;
pthread_once_t lacf_glew_ctx_once = PTHREAD_ONCE_INIT;

void
lacf_glew_ctx_make_key(void)
{
	(void) pthread_key_create(&lacf_glew_ctx_key, free);
}

#endif	/* APL || LIN */
