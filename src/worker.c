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

#if	!IBM
#include <signal.h>
#endif

/**
 * Configures the calling thread to block SIGPIPE signals on macOS and Linux.
 * Does nothing on Windows.
 */
void
lacf_mask_sigpipe(void)
{
#if	!IBM
	sigset_t set;

	pthread_sigmask(SIG_BLOCK, NULL, &set);
	if (!sigismember(&set, SIGPIPE)) {
		sigaddset(&set, SIGPIPE);
		pthread_sigmask(SIG_BLOCK, &set, NULL);
	}
#endif	/* !IBM */
}

static void
worker(void *ui)
{
	worker_t *wk = ui;
	uint64_t now = microclock();
#if	IBM
	TIMECAPS tc;
#endif
	thread_set_name(wk->name);
	/*
	 * SIGPIPE is almost never desired on worker threads (typically due
	 * to writing to broken network sockets).
	 */
	lacf_mask_sigpipe();

	if (wk->init_func != NULL) {
		if (!wk->init_func(wk->userinfo))
			return;
	}
#if	IBM
	/*
	 * On Windows, the sleep interval is constrained to a multiple of
	 * the default clock tick. This is often something really crude,
	 * like 20ms or so. That is way too crude for our needs, so we'll
	 * drop the time tick to the minimum the hardware can support
	 * (usually 1ms).
	 */
	timeGetDevCaps(&tc, sizeof (tc));
	timeBeginPeriod(tc.wPeriodMin);
#endif	/* IBM */

	mutex_enter(&wk->lock);
	while (wk->run) {
		bool_t result;
		uint64_t intval_us = wk->intval_us;

		if (intval_us == 0 && !wk->dontstop) {
			cv_wait(&wk->cv, &wk->lock);
			if (!wk->run)
				break;
			now = microclock();
		}
		/*
		 * Avoid holding the worker lock in the user callback, as
		 * that can result in locking inversions if the worker needs
		 * to grab locks, while external threads might be holding
		 * those locks and trying to wake us up.
		 */
		wk->inside_cb = B_TRUE;
		mutex_exit(&wk->lock);
		result = wk->worker_func(wk->userinfo);
		mutex_enter(&wk->lock);
		wk->inside_cb = B_FALSE;
		if (!result)
			break;
		/*
		 * If another thread is waiting on us to finish executing,
		 * signal it.
		 */
		cv_broadcast(&wk->cv);

		if (intval_us != 0 && !wk->dontstop) {
			uint64_t new_now;

			cv_timedwait(&wk->cv, &wk->lock, now + intval_us);
			/*
			 * If the timeout expired, we have waited for the
			 * full duration. So jump our idea of current time
			 * forward in increments of the interval. This
			 * maintains a fixed execution schedule, but allows
			 * for skipped intervals in case the callback took
			 * too long to execute.
			 */
			new_now = microclock();
			if (new_now >= now + intval_us) {
				uint64_t d_t = new_now - now;
				now += (d_t / intval_us) * intval_us;
			}
		}
		wk->dontstop = B_FALSE;
	}
	mutex_exit(&wk->lock);

	if (wk->fini_func != NULL)
		wk->fini_func(wk->userinfo);

#if	IBM
	timeEndPeriod(tc.wPeriodMin);
#endif	/* IBM */
}

void
worker_init(worker_t *wk, bool_t (*worker_func)(void *userinfo),
    uint64_t intval_us, void *userinfo, const char *thread_name)
{
	worker_init2(wk, NULL, worker_func, NULL, intval_us, userinfo,
	    thread_name);
}

API_EXPORT void
worker_init2(worker_t *wk,
    bool_t (*init_func)(void *userinfo),
    bool_t (*worker_func)(void *userinfo),
    void (*fini_func)(void *userinfo),
    uint64_t intval_us, void *userinfo, const char *thread_name)
{
	ASSERT(worker_func != NULL);

	wk->run = B_TRUE;
	mutex_init(&wk->lock);
	cv_init(&wk->cv);
	wk->init_func = init_func;
	wk->worker_func = worker_func;
	wk->fini_func = fini_func;
	wk->intval_us = intval_us;
	wk->userinfo = userinfo;
	if (thread_name != NULL)
		lacf_strlcpy(wk->name, thread_name, sizeof (wk->name));
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
worker_set_interval(worker_t *wk, uint64_t intval_us)
{
	mutex_enter(&wk->lock);
	/* If the worker is in the callback, wait for it to exit first */
	while (wk->inside_cb)
		cv_wait(&wk->cv, &wk->lock);
	if (wk->intval_us != intval_us) {
		wk->intval_us = intval_us;
		cv_broadcast(&wk->cv);
	}
	mutex_exit(&wk->lock);
}

/*
 * Same as worker_set_interval, but doesn't cause the worker to
 * immediately wake up and run another loop.
 */
void
worker_set_interval_nowake(worker_t *wk, uint64_t intval_us)
{
	mutex_enter(&wk->lock);
	wk->intval_us = intval_us;
	mutex_exit(&wk->lock);
}

void
worker_wake_up(worker_t *wk)
{
	mutex_enter(&wk->lock);
	wk->dontstop = B_TRUE;
	cv_broadcast(&wk->cv);
	mutex_exit(&wk->lock);
}

void
worker_wake_up_wait(worker_t *wk)
{
	mutex_enter(&wk->lock);
	/* If the worker is in the callback, wait for it to exit first */
	while (wk->inside_cb)
		cv_wait(&wk->cv, &wk->lock);
	/* Now we are certain the worker is sleeping, wake it up again */
	cv_broadcast(&wk->cv);
	/* And then wait for it to finish */
	cv_wait(&wk->cv, &wk->lock);
	mutex_exit(&wk->lock);
}
