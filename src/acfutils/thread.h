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
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACF_UTILS_THREAD_H_
#define	_ACF_UTILS_THREAD_H_

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if APL || LIN
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#else	/* !APL && !LIN */
#include <windows.h>
#endif	/* !APL && !LIN */

#include <acfutils/assert.h>
#include <acfutils/helpers.h>
#include <acfutils/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Basic portable multi-threading API. We have 3 kinds of objects and
 * associated manipulation functions here:
 * 1) threat_t - A generic thread identification structure.
 * 2) mutex_t - A generic mutual exclusion lock.
 * 3) condvar_t - A generic condition variable.
 *
 * The actual implementation is all contained in this header and simply
 * works as a set of macro expansions on top of the OS-specific threading
 * API (pthreads on Unix/Linux, winthreads on Win32).
 *
 * Example of how to create a thread:
 *	thread_t my_thread;
 *	if (!thread_create(&my_thread, thread_main_func, thread_arg))
 *		fprintf(stderr, "thread create failed!\n");
 *
 * Example of how to wait for a thread to exit:
 *	thread_t my_thread;
 *	thread_join(my_thread);
 *	... thread disposed of, no need for further cleanup ...
 *
 * Example of how to use a mutex_t:
 *	mutex_t my_lock;		-- the lock object itself
 *	mutex_init(&my_lock);		-- create a lock
 *	mutex_enter(&my_lock);		-- grab a lock
 *	... do some critical, exclusiony-type stuff ...
 *	mutex_exit(&my_lock);		-- release a lock
 *	mutex_destroy(&my_lock);	-- free a lock
 *
 * Example of how to use a condvar_t:
 *	mutex_t my_lock;
 *	condvar_t my_cv;
 *
 *	mutex_init(&my_lock);		-- create a lock to control the CV
 *	cv_init(&my_cv);		-- create the condition variable
 *
 *	-- thread that's going to signal the condition:
 *		mutex_enter(&my_lock);	-- grab the lock
 *		... set up some resource that others might be waiting on ...
 *		cv_broadcast(&my_lock);	-- wake up all waiters
 *		mutex_exit(&my_lock);	-- release the lock
 *
 *	-- thread that's going to wait on the condition:
 *		mutex_enter(&my_lock);			-- grab the lock
 *		while (!condition_met())
 *			cv_wait(&my_cv, &my_lock);	-- wait for the CV
 *							-- to be signalled
 *		... condition fulfilled, use the resource ...
 *		mutex_exit(&my_lock);			-- release the lock
 *
 * You can also performed a "timed" wait on a CV using cv_timedwait. The
 * function will exit when either the condition has been signalled, or the
 * timer has expired. The return value of the function indicates whether
 * the condition was signalled before the timer expired (returns zero),
 * or if the wait timed out (returns ETIMEDOUT) or another error occurred
 * (returns -1).
 *
 *		mutex_enter(&my_lock);			-- grab the lock
 *		-- Wait for the CV to be signalled. Time argument is an
 *		-- absolute time as returned by the 'microclock' function +
 *		-- whatever extra time delay you want to apply.
 *		uint64_t deadline = microclock() + timeout_usecs;
 *		while (!condition_met()) {
 *			if (cv_timedwait(&my_cv, &my_lock, deadline) ==
 *			    ETIMEDOUT) {
 *				-- timed out waiting for CV to signal
 *				break;
 *			}
 *		}
 *		mutex_exit(&my_lock);			-- release the lock
 */

#if	defined(__GNUC__) || defined(__clang__)
#define	THREAD_LOCAL	__thread
#elif	defined(_MSC_VER)
#define	THREAD_LOCAL	__declspec(thread)
#endif

#if	APL || LIN

#define	thread_t		pthread_t
#define	thread_id_t		pthread_t
#define	mutex_t			pthread_mutex_t
#define	condvar_t		pthread_cond_t
#define	curthread		pthread_self()

#define	mutex_init(mtx)	\
	do { \
		pthread_mutexattr_t attr; \
		pthread_mutexattr_init(&attr); \
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE); \
		pthread_mutex_init((mtx), &attr); \
	} while (0)
#define	mutex_destroy(mtx)	pthread_mutex_destroy((mtx))
#define	mutex_enter(mtx)	pthread_mutex_lock((mtx))
#define	mutex_exit(mtx)		pthread_mutex_unlock((mtx))

#define	thread_create(thrp, proc, arg) \
	(pthread_create(thrp, NULL, (void *(*)(void *))proc, \
	    arg) == 0)
#define	thread_join(thrp)	pthread_join(*(thrp), NULL)

#if	LIN
#define	thread_set_name(name)	pthread_setname_np(pthread_self(), (name))
#else	/* APL */
#define	thread_set_name(name)	pthread_setname_np((name))
#endif	/* APL */

#define	cv_wait(cv, mtx)	pthread_cond_wait((cv), (mtx))
static inline int
cv_timedwait(condvar_t *cv, mutex_t *mtx, uint64_t limit)
{
	struct timespec ts = { .tv_sec = (time_t)(limit / 1000000),
	    .tv_nsec = (long)((limit % 1000000) * 1000) };
	return (pthread_cond_timedwait(cv, mtx, &ts));
}
#define	cv_init(cv)		pthread_cond_init((cv), NULL)
#define	cv_destroy(cv)		pthread_cond_destroy((cv))
#define	cv_broadcast(cv)	pthread_cond_broadcast((cv))

#else	/* !APL && !LIN */

#define	thread_t	HANDLE
#define	thread_id_t	DWORD
typedef struct {
	bool_t			inited;
	CRITICAL_SECTION	cs;
} mutex_t;
#define	condvar_t	CONDITION_VARIABLE
#define	curthread	GetCurrentThreadId()

#define	mutex_init(x) \
	do { \
		(x)->inited = B_TRUE; \
		InitializeCriticalSection(&(x)->cs); \
	} while (0)
#define	mutex_destroy(x) \
	do { \
		ASSERT((x)->inited); \
		DeleteCriticalSection(&(x)->cs); \
		(x)->inited = B_FALSE; \
	} while (0)
#define	mutex_enter(x) \
	do { \
		ASSERT((x)->inited); \
		EnterCriticalSection(&(x)->cs); \
	} while (0)
#define	mutex_exit(x) \
	do { \
		ASSERT((x)->inited); \
		LeaveCriticalSection(&(x)->cs); \
	} while (0)

#define	thread_create(thrp, proc, arg) \
	((*(thrp) = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)proc, arg, \
	    0, NULL)) != NULL)
#define	thread_join(thrp) \
	VERIFY3S(WaitForSingleObject(*(thrp), INFINITE), ==, WAIT_OBJECT_0)
#define	thread_set_name(name)		UNUSED(name)

#define	cv_wait(cv, mtx) \
	VERIFY(SleepConditionVariableCS((cv), &(mtx)->cs, INFINITE))
static inline int
cv_timedwait(condvar_t *cv, mutex_t *mtx, uint64_t limit)
{
	uint64_t now = microclock();
	if (now < limit) {
		if (SleepConditionVariableCS(cv, &mtx->cs,
		    (limit - now) / 1000) != 0) {
			return (0);
		}
		if (GetLastError() == ERROR_TIMEOUT)
			return (ETIMEDOUT);
		return (-1);
	}
	return (ETIMEDOUT);
}
#define	cv_init		InitializeConditionVariable
#define	cv_destroy(cv)	/* no-op */
#define	cv_broadcast	WakeAllConditionVariable

#endif	/* !APL && !LIN */

API_EXPORT void lacf_mask_sigpipe(void);

typedef struct {
	mutex_t			lock;
	condvar_t		cv;
	bool_t			write_locked;
	thread_id_t		writer;
	unsigned		refcount;
	unsigned		waiters;
} rwmutex_t;

static void rwmutex_init(rwmutex_t *rw) UNUSED_ATTR;
static void rwmutex_destroy(rwmutex_t *rw) UNUSED_ATTR;
static void rwmutex_enter(rwmutex_t *rw, bool_t write) UNUSED_ATTR;
static void rwmutex_exit(rwmutex_t *rw) UNUSED_ATTR;

static void
rwmutex_init(rwmutex_t *rw)
{
	memset(rw, 0, sizeof (*rw));
	mutex_init(&rw->lock);
	cv_init(&rw->cv);
}

static void
rwmutex_destroy(rwmutex_t *rw)
{
	ASSERT3U(rw->refcount, ==, 0);
	cv_destroy(&rw->cv);
	mutex_destroy(&rw->lock);
}

static void
rwmutex_enter(rwmutex_t *rw, bool_t write)
{
	mutex_enter(&rw->lock);

	if (rw->write_locked && rw->writer != curthread) {
		rw->waiters++;
		while (rw->write_locked)
			cv_wait(&rw->cv, &rw->lock);
		rw->waiters--;
	}
	if (write) {
		if (rw->refcount > 0 && rw->writer != curthread) {
			rw->waiters++;
			while (rw->refcount > 0)
				cv_wait(&rw->cv, &rw->lock);
			rw->waiters--;
		}
		rw->write_locked = B_TRUE;
		rw->writer = curthread;
	}
	rw->refcount++;

	mutex_exit(&rw->lock);
}

static void
rwmutex_exit(rwmutex_t *rw)
{
	mutex_enter(&rw->lock);
	ASSERT3U(rw->refcount, >, 0);
	rw->refcount--;
	if (rw->refcount == 0 && rw->write_locked) {
		ASSERT3U(rw->writer, ==, curthread);
		rw->write_locked = B_FALSE;
		rw->writer = 0;
	}
	if (rw->waiters > 0)
		cv_broadcast(&rw->cv);
	mutex_exit(&rw->lock);
}

static inline void
rwmutex_upgrade(rwmutex_t *rw)
{
	rwmutex_exit(rw);
	rwmutex_enter(rw, B_TRUE);
}

static inline bool_t
rwmutex_held_write(rwmutex_t *rw)
{
	return (rw->writer == curthread);
}

#ifdef __cplusplus
}
#endif

#endif	/* _ACF_UTILS_THREAD_H_ */
