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

#include <stdlib.h>

#if APL || LIN
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#else	/* !APL && !LIN */
#include <windows.h>
#endif	/* !APL && !LIN */

#include "helpers.h"

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
 * timer has expired. You have to check yourself which of these occurred:
 *
 *		mutex_enter(&my_lock);			-- grab the lock
 *		-- Wait for the CV to be signalled. Time argument is an
 *		-- absolute time as returned by the 'microclock' function +
 *		-- whatever extra time delay you want to apply.
 *		uint64_t deadline = microclock() + timeout_usecs;
 *		while (!condition_met()) {
 *			cv_timedwait(&my_cv, &my_lock, deadline);
 *			if (microclock() > deadline)
 *				-- timed out waiting for CV to signal
 *				goto bail_out;
 *		}
 *		mutex_exit(&my_lock);			-- release the lock
 */

#if	APL || LIN

#define	thread_t		pthread_t
#define	mutex_t			pthread_mutex_t
#define	condvar_t		pthread_cond_t

#define	mutex_init(mtx)		pthread_mutex_init((mtx), NULL)
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
#define	cv_timedwait(cond, mtx, microtime) \
	do { \
		uint64_t t = (microtime); \
		struct timespec ts = { .tv_sec = t / 1000000, \
		    .tv_nsec = (t % 1000000) * 1000 }; \
		(void) pthread_cond_timedwait((cond), (mtx), &ts); \
	} while (0)
#define	cv_init(cv)		pthread_cond_init((cv), NULL)
#define	cv_destroy(cv)		pthread_cond_destroy((cv))
#define	cv_broadcast(cv)	pthread_cond_broadcast((cv))

#else	/* !APL && !LIN */

#define	thread_t	HANDLE
typedef struct {
	bool_t			inited;
	CRITICAL_SECTION	cs;
} mutex_t;
#define	condvar_t	CONDITION_VARIABLE

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
#define	cv_timedwait(cv, mtx, limit) \
	do { \
		uint64_t __now = microclock(); \
		if (__now < (limit)) { \
			(void) SleepConditionVariableCS((cv), &(mtx)->cs, \
			    ((limit) - __now) / 1000); \
		} \
	} while (0)
#define	cv_init		InitializeConditionVariable
#define	cv_destroy(cv)	/* no-op */
#define	cv_broadcast	WakeAllConditionVariable

#endif	/* !APL && !LIN */

#ifdef __cplusplus
}
#endif

#endif	/* _ACF_UTILS_THREAD_H_ */
