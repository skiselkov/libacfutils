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
 * Copyright 2018 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACFUTILS_WORKER_H_
#define	_ACFUTILS_WORKER_H_

#include <acfutils/thread.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct {
	mutex_t		lock;
	condvar_t	cv;
	uint64_t	intval_us;
	bool_t		run;
	thread_t	thread;
	bool_t		(*worker_func)(void *userinfo);
	void		*userinfo;
} worker_t;

void worker_init(worker_t *wk, bool_t (*worker_func)(void *userinfo),
    uint64_t intval_us, void *userinfo);
void worker_fini(worker_t *wk);

void worker_wake_up(worker_t *wk);


#ifdef	__cplusplus
}
#endif

#endif	/* _ACFUTILS_WORKER_H_ */
