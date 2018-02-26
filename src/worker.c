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

#include <string.h>

#include <acfutils/assert.h>
#include <acfutils/helpers.h>
#include <acfutils/worker.h>
#include <acfutils/time.h>

static void
worker(void *ui)
{
	worker_t *wk = ui;

	thread_set_name(wk->name);

	mutex_enter(&wk->lock);
	while (wk->run) {
		if (wk->intval_us == 0) {
			cv_wait(&wk->cv, &wk->lock);
			if (!wk->run)
				break;
		}

		if (!wk->worker_func(wk->userinfo))
			break;
		/*
		 * If another thread is waiting on us to finish executing,
		 * signal it.
		 */
		cv_broadcast(&wk->cv);

		if (wk->intval_us != 0) {
			cv_timedwait(&wk->cv, &wk->lock,
			    microclock() + wk->intval_us);
		}
	}
	mutex_exit(&wk->lock);
}

void
worker_init(worker_t *wk, bool_t (*worker_func)(void *userinfo),
    uint64_t intval_us, void *userinfo, const char *thread_name)
{
	ASSERT(worker_func != NULL);

	wk->run = B_TRUE;
	mutex_init(&wk->lock);
	cv_init(&wk->cv);
	wk->worker_func = worker_func;
	wk->intval_us = intval_us;
	wk->userinfo = userinfo;
	if (thread_name != NULL)
		strlcpy(wk->name, thread_name, sizeof (wk->name));
	else
		memset(wk->name, 0, sizeof (wk->name));
	VERIFY(thread_create(&wk->thread, worker, wk));
}

void
worker_fini(worker_t *wk)
{
	if (!wk->run)
		return;

	/*
	 * Stop the thread before grabbing the lock, to shut it down
	 * while it is executing its callback.
	 */
	wk->run = B_FALSE;

	mutex_enter(&wk->lock);
	cv_broadcast(&wk->cv);
	mutex_exit(&wk->lock);

	thread_join(&wk->thread);

	mutex_destroy(&wk->lock);
	cv_destroy(&wk->cv);
}

void
worker_wake_up(worker_t *wk)
{
	mutex_enter(&wk->lock);
	cv_broadcast(&wk->cv);
	mutex_exit(&wk->lock);
}

void
worker_wake_up_wait(worker_t *wk)
{
	mutex_enter(&wk->lock);
	/* First wake up the thread */
	cv_broadcast(&wk->cv);
	/* And then wait for it to finish */
	cv_wait(&wk->cv, &wk->lock);
	mutex_exit(&wk->lock);
}
