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

#include "thread.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct {
	mutex_t		lock;
	condvar_t	cv;
	uint64_t	intval_us;
	bool_t		run;
	bool_t		inside_cb;
	bool_t		dontstop;
	thread_t	thread;
	bool_t		(*init_func)(void *userinfo);
	bool_t		(*worker_func)(void *userinfo);
	void		(*fini_func)(void *userinfo);
	void		*userinfo;
	char		name[32];
} worker_t;

API_EXPORT void worker_init(worker_t *wk, bool_t (*worker_func)(void *userinfo),
    uint64_t intval_us, void *userinfo, const char *thread_name);
API_EXPORT void worker_init2(worker_t *wk,
    bool_t (*init_func)(void *userinfo),
    bool_t (*worker_func)(void *userinfo),
    void (*fini_func)(void *userinfo),
    uint64_t intval_us, void *userinfo, const char *thread_name);
API_EXPORT void worker_fini(worker_t *wk);

API_EXPORT void worker_set_interval(worker_t *wk, uint64_t intval_us);
API_EXPORT void worker_set_interval_nowake(worker_t *wk, uint64_t intval_us);
API_EXPORT void worker_wake_up(worker_t *wk);
API_EXPORT void worker_wake_up_wait(worker_t *wk);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACFUTILS_WORKER_H_ */
