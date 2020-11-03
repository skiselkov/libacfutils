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
 * Copyright 2020 Saso Kiselkov. All rights reserved.
 */

#include <stddef.h>

#include "acfutils/assert.h"
#include "acfutils/list.h"
#include "acfutils/safe_alloc.h"
#include "acfutils/taskq.h"
#include "acfutils/thread.h"
#include "acfutils/time.h"

typedef struct {
	void		*task;
	list_node_t	node;
} taskq_task_t;

typedef struct {
	taskq_t		*tq;
	thread_t	thr;
	void		*thr_info;
	list_node_t	node;
} taskq_thr_t;

struct taskq_s {
	/* immutable */
	taskq_init_thr_t	init_func;
	taskq_fini_thr_t	fini_func;
	taskq_proc_task_t	proc_func;
	taskq_discard_task_t	discard_func;
	void			*userinfo;

	mutex_t			lock;
	/* protected by lock */
	unsigned		num_threads_min;
	unsigned		num_threads_max;
	uint64_t		thr_stop_delay_us;
	condvar_t		cv;
	bool			shutdown;
	list_t			tasks;
	list_t			threads;
	unsigned		num_thr_ready;
};

static bool
task_wait_for_work(taskq_t *tq)
{
	ASSERT(tq != NULL);

	if (tq->thr_stop_delay_us != 0) {
		uint64_t limit = microclock() + tq->thr_stop_delay_us;
		return (cv_timedwait(&tq->cv, &tq->lock, limit) != ETIMEDOUT);
	} else {
		cv_wait(&tq->cv, &tq->lock);
		return (true);
	}
}

static void
taskq_worker(void *info)
{
	taskq_thr_t *thr;
	taskq_t *tq;

	ASSERT(info != NULL);
	thr = info;
	ASSERT(thr->tq != NULL);
	tq = thr->tq;
	ASSERT(tq->proc_func != NULL);

	if (tq->init_func != NULL)
		thr->thr_info = tq->init_func(tq->userinfo);

	mutex_enter(&tq->lock);
	tq->num_thr_ready++;
	while (!tq->shutdown) {
		taskq_task_t *task;

		/* Too many threads spawned? Stop. */
		if (list_count(&tq->threads) > tq->num_threads_max)
			break;
		task = list_remove_head(&tq->tasks);
		if (task == NULL) {
			/* No work to be done */
			if (task_wait_for_work(tq) ||
			    list_count(&tq->threads) <= tq->num_threads_min) {
				continue;
			} else {
				break;
			}
		}
		tq->num_thr_ready--;
		mutex_exit(&tq->lock);

		/* Process the task */
		tq->proc_func(tq->userinfo, thr->thr_info, task->task);
		free(task);

		mutex_enter(&tq->lock);
		tq->num_thr_ready++;
	}
	ASSERT(tq->num_thr_ready != 0);
	tq->num_thr_ready--;
	/*
	 * Cannot relinquish the lock here until we are completely removed
	 * from the tq->threads list, otherwise taskq_submit might thing we
	 * were just busy and we might still process work. But we are
	 * definitely on our way out.
	 */
	if (tq->fini_func != NULL)
		tq->fini_func(tq->userinfo, thr->thr_info);

	ASSERT(list_link_active(&thr->node));
	list_remove(&tq->threads, thr);
	if (list_count(&tq->threads) == 0)
		cv_broadcast(&tq->cv);
	/*
	 * Mustn't touch `tq' after this, as on a taskq_free, it can become
	 * freed after releasing this lock
	 */
	mutex_exit(&tq->lock);

	memset(thr, 0, sizeof (*thr));
	free(thr);
}

taskq_t *
taskq_alloc(unsigned num_threads_min, unsigned num_threads_max,
    uint64_t thr_stop_delay_us, taskq_init_thr_t init_func,
    taskq_fini_thr_t fini_func, taskq_proc_task_t proc_func,
    taskq_discard_task_t discard_func, void *userinfo)
{
	taskq_t *tq = safe_calloc(1, sizeof (*tq));

	ASSERT3U(num_threads_min, <=, num_threads_max);
	ASSERT(num_threads_max != 0);
	ASSERT(proc_func != NULL);
	ASSERT(discard_func != NULL);

	mutex_init(&tq->lock);
	cv_init(&tq->cv);
	list_create(&tq->tasks, sizeof (taskq_task_t),
	    offsetof(taskq_task_t, node));
	list_create(&tq->threads, sizeof (taskq_thr_t),
	    offsetof(taskq_thr_t, node));

	tq->num_threads_min = num_threads_min;
	tq->num_threads_max = num_threads_max;
	tq->thr_stop_delay_us = thr_stop_delay_us;
	tq->init_func = init_func;
	tq->fini_func = fini_func;
	tq->proc_func = proc_func;
	tq->discard_func = discard_func;
	tq->userinfo = userinfo;

	return (tq);
}

void
taskq_free(taskq_t *tq)
{
	taskq_task_t *task;

	ASSERT(tq != NULL);
	ASSERT(tq->discard_func != NULL);
	/*
	 * Notify all parked workers to stop.
	 */
	mutex_enter(&tq->lock);
	tq->shutdown = true;
	/* The worker threads will empty out the `tq->threads' list */
	while (list_count(&tq->threads) != 0) {
		cv_broadcast(&tq->cv);
		cv_wait(&tq->cv, &tq->lock);
	}
	mutex_exit(&tq->lock);
	ASSERT0(list_count(&tq->threads));
	list_destroy(&tq->threads);
	/*
	 * Discard incomplete work.
	 */
	while ((task = list_remove_head(&tq->tasks)) != NULL) {
		tq->discard_func(tq->userinfo, task->task);
		free(task);
	}
	list_destroy(&tq->tasks);
	/*
	 * Destroy threading primitives.
	 */
	cv_destroy(&tq->cv);
	mutex_destroy(&tq->lock);

	free(tq);
}

void
taskq_submit(taskq_t *tq, void *task)
{
	taskq_task_t *t = safe_calloc(1, sizeof (*t));

	ASSERT(tq != NULL);
	t->task = task;

	mutex_enter(&tq->lock);
	list_insert_tail(&tq->tasks, t);
	if (tq->num_thr_ready != 0) {
		/* Only wake up a single worker */
		cv_signal(&tq->cv);
	} else if (list_count(&tq->threads) < tq->num_threads_max) {
		/* No worker ready and we can still add more, spawn a new one */
		taskq_thr_t *thr = safe_calloc(1, sizeof (*thr));
		thr->tq = tq;
		list_insert_tail(&tq->threads, thr);
		VERIFY(thread_create(&thr->thr, taskq_worker, thr));
	}
	mutex_exit(&tq->lock);
}

bool
taskq_wants_shutdown(taskq_t *tq)
{
	bool shutdown;

	ASSERT(tq != NULL);
	mutex_enter(&tq->lock);
	shutdown = tq->shutdown;
	mutex_exit(&tq->lock);

	return (shutdown);
}

void
taskq_set_num_threads_min(taskq_t *tq, unsigned num_threads_min)
{
	ASSERT(tq != NULL);
	if (tq->num_threads_min != num_threads_min) {
		mutex_enter(&tq->lock);
		tq->num_threads_min = num_threads_min;
		cv_broadcast(&tq->cv);	/* wake up all workers to re-adjust */
		mutex_exit(&tq->lock);
	}
}

unsigned
taskq_get_num_threads_min(const taskq_t *tq)
{
	ASSERT(tq != NULL);
	return (tq->num_threads_min);
}

void
taskq_set_num_threads_max(taskq_t *tq, unsigned num_threads_max)
{
	ASSERT(tq != NULL);
	if (tq->num_threads_max != num_threads_max) {
		mutex_enter(&tq->lock);
		tq->num_threads_max = num_threads_max;
		cv_broadcast(&tq->cv);	/* wake up all workers to re-adjust */
		mutex_exit(&tq->lock);
	}
}

unsigned
taskq_get_num_threads_max(const taskq_t *tq)
{
	ASSERT(tq != NULL);
	return (tq->num_threads_max);
}

void
taskq_set_thr_stop_delay(taskq_t *tq, uint64_t thr_stop_delay_us)
{
	ASSERT(tq != NULL);
	if (tq->thr_stop_delay_us != thr_stop_delay_us) {
		mutex_enter(&tq->lock);
		tq->thr_stop_delay_us = thr_stop_delay_us;
		cv_broadcast(&tq->cv);	/* wake up all workers to re-adjust */
		mutex_exit(&tq->lock);
	}
}

uint64_t
taskq_get_thr_stop_delay(const taskq_t *tq)
{
	ASSERT(tq != NULL);
	return (tq->thr_stop_delay_us);
}
