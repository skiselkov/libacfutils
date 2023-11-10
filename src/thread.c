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

#include "acfutils/thread.h"

bool_t	lacf_thread_list_inited = B_FALSE;
mutex_t	lacf_thread_list_lock;
list_t	lacf_thread_list;

#if	IBM

DWORD
lacf_thread_start_routine(void *arg)
{
	lacf_thread_info_t *ti = (lacf_thread_info_t *)arg;
	ti->proc(ti->arg);
	_lacf_thread_list_remove(ti);
	free(ti);
	return (0);
}

#else

void *
lacf_thread_start_routine(void *arg)
{
	lacf_thread_info_t *ti = (lacf_thread_info_t *)arg;
	ti->proc(ti->arg);
	_lacf_thread_list_remove(ti);
	free(ti);
	return (NULL);
}

#endif
