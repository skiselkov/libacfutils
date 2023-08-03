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

#ifndef	_ACF_UTILS_THREAD_H_
#define	_ACF_UTILS_THREAD_H_

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if	APL || LIN
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#else	/* !APL && !LIN */
#include <windows.h>
#endif	/* !APL && !LIN */

#if	__STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>
#elif	APL
#include <libkern/OSAtomic.h>
#endif

#if	LIN
#include <sys/syscall.h>
#include <unistd.h>
#endif	/* LIN */

#include "assert.h"
#include "helpers.h"
#include "list.h"
#include "time.h"
#include "safe_alloc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \file
 * Basic portable multi-threading API. We have 3 kinds of objects and
 * associated manipulation functions here:
 * 1. \ref thread_t - A generic thread identification structure.
 * 2. \ref mutex_t - A generic mutual exclusion lock.
 * 3. \ref condvar_t - A generic condition variable.
 *
 * The actual implementation is large contained in this header and mostly
 * works as a set of pre-processor switching on top of the OS-specific
 * threading API (pthreads on Unix/Linux, winthreads on Win32).
 *
 * ## Thread Handling
 *
 * Example of how to create a thread:
 *```
 *	thread_t my_thread;
 *	if (!thread_create(&my_thread, thread_main_func, thread_arg))
 *		fprintf(stderr, "thread create failed!\n");
 *```
 * Example of how to wait for a thread to exit:
 *```
 *	thread_t my_thread;
 *	thread_join(my_thread);
 *	// ... thread disposed of, no need for further cleanup ...
 *
 * ## Locking
 *```
 * Example of how to use a mutex_t:
 *```
 *	mutex_t my_lock;                // the lock object itself
 *	mutex_init(&my_lock);           // create the lock
 *	mutex_enter(&my_lock);          // grab the lock
 *	// ... do some critical, exclusiony-type stuff ...
 *	mutex_exit(&my_lock);           // release the lock
 *	mutex_destroy(&my_lock);        // free the lock
 *
 * ## Condition Variables
 *```
 * Example of how to use a condvar_t:
 *```
 *	mutex_t my_lock;
 *	condvar_t my_cv;
 *
 *	mutex_init(&my_lock);		// create a lock to control the CV
 *	cv_init(&my_cv);		// create the condition variable
 *
 *	// thread that's going to signal the condition:
 *		mutex_enter(&my_lock);	// grab the lock
 *		// ... set up some resource that others might be waiting on ...
 *		cv_broadcast(&my_lock);	// wake up all waiters
 *		mutex_exit(&my_lock);	// release the lock
 *
 *	// thread that's going to wait on the condition:
 *		mutex_enter(&my_lock);			// grab the lock
 *		while (!condition_met())
 *			cv_wait(&my_cv, &my_lock);	// wait for the CV
 *							// to be signalled
 *		// ... condition fulfilled, use the resource ...
 *		mutex_exit(&my_lock);			// release the lock
 *```
 * You can also performed a "timed" wait on a CV using cv_timedwait(). The
 * function will exit when either the condition has been signalled, or the
 * timer has expired. The return value of the function indicates whether
 * the condition was signalled before the timer expired (returns zero),
 * or if the wait timed out (returns ETIMEDOUT) or another error occurred
 * (returns -1).
 *```
 *	mutex_enter(&my_lock);			// grab the lock
 *	// Wait for the CV to be signalled. Time argument is an
 *	// absolute time as returned by the 'microclock' function +
 *	// whatever extra time delay you want to apply.
 *	uint64_t deadline = microclock() + timeout_usecs;
 *	while (!condition_met()) {
 *		if (cv_timedwait(&my_cv, &my_lock, deadline) ==
 *		    ETIMEDOUT) {
 *			// timed out waiting for CV to signal
 *			break;
 *		}
 *	}
 *	mutex_exit(&my_lock);			// release the lock
 *```
 *
 * ## Atomics
 * The atomic32_t and atomic64_t types describe signed 32- and 64-bit
 * integers respectively, which are suitable for being passed to the
 * relevant `atomic_*` functions. Example of how use an atomic function:
 *```
 * atomic32_t	my_value;		// declare an atomic 32-bit signed int
 * atomic_set_32(&my_value, 1234);	// set an integer atomically
 * atomic_inc_32(&my_value);		// increment integer atomically
 * atomic_dec_32(&my_value);		// decrement integer atomically
 * atomic_add_32(&my_value, -352);	// add a value to the atomic integer
 *```
 */

/**
 * \def thread_create()
 * Creates a new thread.
 * @param thrp Return pointer to a thread_t, which will be filled with the
 *	thread handle if the thread was started successfully.
 * @param proc Start function, which will be called by the new thread.
 *	When this function returns, the thread terminates.
 * @param arg Optional pointer, which will be passed to the start function
 *	in its first argument.
 * @return `B_TRUE` if starting the thread was successful, `B_FALSE` otherwise.
 *	You MUST check the return value and not just assume that starting
 *	the thread is always succesful.
 */

typedef struct {
	void		(*proc)(void *);
	void		*arg;
	const char	*filename;
	int		linenum;
	list_node_t	node;
} lacf_thread_info_t;

/*
 * !!!! CAUTION !!!!
 * MacOS's kernel API contains a function named `thread_create' that'll
 * clash with the naming below. To work around this, we declare the
 * function name to be a macro to our own scoped version. The user must
 * include /usr/include/mach/task.h before us, otherwise we could be
 * rewriting the function name in the header too. The flip side is that
 * the user will get calls to libacfutils' thread_create instead.
 */
#if	!APL || !defined(_task_user_)
#define	thread_create(thrp, start_proc, arg) \
	lacf_thread_create((thrp), (start_proc), (arg), \
	    log_basename(__FILE__), __LINE__)
#endif

/**
 * \def atomic32_t
 * Atomic signed 32-bit integer. You must use this type with the
 * `atomic_*_32()` family of functions, such as atomic_inc_32(),
 * atomic_dec_32(), atomic_set_32() and atomic_add_32().
 *
 * \def atomic_inc_32()
 * Increments an atomic 32-bit integer by 1.
 * @param x Pointer to the atomic32_t to be incremented.
 *
 * \def atomic_dec_32()
 * Decrements an atomic 32-bit integer by 1.
 * @param x Pointer to the atomic32_t to be decremented.
 *
 * \def atomic_add_32()
 * Adds an arbitrary value to an atomic 32-bit.
 * @param x Pointer to the atomic32_t to be added to.
 * @param y 32-bit signed integer value to be added to `x`. Can be negative
 *	to perform subtraction instead.
 *
 * \def atomic_set_32()
 * Sets an atomic 32-bit integer to a new value.
 * @param x Pointer to the atomic32_t to be set.
 * @param y New 32-bit signed integer value to be set in `x`.
 *
 * \def atomic64_t
 * Atomic signed 64-bit integer. You must use this type with the
 * `atomic_*_64()` family of functions, such as atomic_inc_64(),
 * atomic_dec_64(), atomic_set_64() and atomic_add_64().
 *
 * \def atomic_inc_64()
 * Increments an atomic 64-bit integer by 1.
 * @param x Pointer to the atomic64_t to be incremented.
 *
 * \def atomic_dec_64()
 * Decrements an atomic 64-bit integer by 1.
 * @param x Pointer to the atomic64_t to be decremented.
 *
 * \def atomic_add_64()
 * Adds an arbitrary value to an atomic 64-bit.
 * @param x Pointer to the atomic64_t to be added to.
 * @param y 64-bit signed integer value to be added to `x`. Can be negative
 *	to perform subtraction instead.
 *
 * \def atomic_set_64()
 * Sets an atomic 64-bit integer to a new value.
 * @param x Pointer to the atomic64_t to be set.
 * @param y New 64-bit signed integer value to be set in `x`.
 *
 * \def curthread
 * @return The calling thread's thread_t handle.
 * @see thread_t
 * @see curthread_id
 *
 * \def curthread_id
 * @return The calling thread's thread_id_t.
 * @see thread_id_t
 * @see curthread
 *
 * \def VERIFY_MUTEX_HELD()
 * Verifies that a mutex_t is held by the calling thread. If it isn't,
 * this trips an assertion failure (as if a VERIFY() check had failed).
 * @see VERIFY_MUTEX_NOT_HELD()
 * @see ASSERT_MUTEX_HELD()
 *
 * \def VERIFY_MUTEX_NOT_HELD()
 * The opposite of VERIFY_MUTEX_HELD(). Due to varying platform support
 * for checking mutex ownership, this cannot be done using a simple
 * stacking such as `VERIFY(!MUTEX_HELD(mtx))`, as the inner macro
 * wouldn't know what the desired output was and would thus be
 * error-prone when used.
 * @see VERIFY_MUTEX_HELD()
 * @see ASSERT_MUTEX_NOT_HELD()
 *
 * \def ASSERT_MUTEX_HELD()
 * If compiling with the `DEBUG` macro set, expands to VERIFY_MUTEX_HELD().
 * Otherwise expands to nothing.
 * @see VERIFY_MUTEX_HELD()
 * @see ASSERT_MUTEX_NOT_HELD()
 *
 * \def ASSERT_MUTEX_NOT_HELD()
 * If compiling with the `DEBUG` macro set, expands to VERIFY_MUTEX_NOT_HELD().
 * Otherwise expands to nothing.
 * @see VERIFY_MUTEX_NOT_HELD()
 * @see ASSERT_MUTEX_HELD()
 */
#if	__STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
#define	atomic32_t		_Atomic int32_t
#define	atomic_inc_32(x)	atomic_fetch_add((x), 1)
#define	atomic_dec_32(x)	atomic_fetch_add((x), -1)
#define	atomic_set_32(x, y)	atomic_store((x), (y))
#define	atomic_add_32(x, y)	atomic_fetch_add((x), (y))
#define	atomic64_t		_Atomic int64_t
#define	atomic_inc_64(x)	atomic_fetch_add((x), 1)
#define	atomic_dec_64(x)	atomic_fetch_add((x), -1)
#define	atomic_set_64(x, y)	atomic_store((x), (y))
#define	atomic_add_64(x, y)	atomic_fetch_add((x), (y))
#elif	IBM
#define	atomic32_t		volatile LONG
#define	atomic_inc_32(x)	InterlockedIncrement((x))
#define	atomic_dec_32(x)	InterlockedDecrement((x))
#define	atomic_add_32(x, y)	InterlockedAdd((x), (y))
#define	atomic_set_32(x, y)	InterlockedExchange((x), (y))
#define	atomic64_t		volatile LONG64
#define	atomic_inc_64(x)	InterlockedIncrement64((x))
#define	atomic_dec_64(x)	InterlockedDecrement64((x))
#define	atomic_add_64(x, y)	InterlockedAdd64((x), (y))
#define	atomic_set_64(x, y)	InterlockedExchange64((x), (y))
#elif	APL
#define	atomic32_t		volatile int32_t
#define	atomic_inc_32(x)	OSAtomicAdd32(1, (x))
#define	atomic_dec_32(x)	OSAtomicAdd32(-1, (x))
#define	atomic_add_32(x, y)	OSAtomicAdd32((y), (x))
#define	atomic_set_32(x, y)	(x) = (y)	/* No such op on OSX */
#define	atomic64_t		volatile int64_t
#define	atomic_inc_64(x)	OSAtomicAdd64(1, (x))
#define	atomic_dec_64(x)	OSAtomicAdd64(-1, (x))
#define	atomic_add_64(x, y)	OSAtomicAdd64((y), (x))
#define	atomic_set_64(x, y)	(x) = (y)	/* No such op on OSX */
#else	/* LIN */
#define	atomic32_t		volatile int32_t
#define	atomic_inc_32(x)	__sync_add_and_fetch((x), 1)
#define	atomic_dec_32(x)	__sync_add_and_fetch((x), -1)
#define	atomic_add_32(x, y)	__sync_add_and_fetch((x), (y))
#define	atomic_set_32(x, y)	__atomic_store_n((x), (y), __ATOMIC_RELAXED)
#define	atomic64_t		volatile int64_t
#define	atomic_inc_64(x)	__sync_add_and_fetch((x), 1)
#define	atomic_dec_64(x)	__sync_add_and_fetch((x), -1)
#define	atomic_add_64(x, y)	__sync_add_and_fetch((x), (y))
#define	atomic_set_64(x, y)	__atomic_store_n((x), (y), __ATOMIC_RELAXED)
#endif	/* LIN */

#if	APL || LIN

#if	APL
#define	thread_t	pthread_t
#define	thread_id_t	pthread_t
#define	mutex_t	pthread_mutex_t
#define	condvar_t	pthread_cond_t
#else	/* !APL */
typedef pthread_t thread_t;
typedef pthread_t thread_id_t;
typedef pthread_mutex_t mutex_t;
typedef pthread_cond_t condvar_t;
#endif	/* !APL */
#define	curthread_id		pthread_self()
#define	curthread		pthread_self()

static inline void
mutex_init(mutex_t *mtx)
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(mtx, &attr);
}

static inline void
mutex_destroy(mutex_t *mtx)
{
	pthread_mutex_destroy(mtx);
}

static inline void
mutex_enter(mutex_t *mtx)
{
	pthread_mutex_lock(mtx);
}

static inline void
mutex_exit(mutex_t *mtx)
{
	pthread_mutex_unlock(mtx);
}

#if	LIN
#define	VERIFY_MUTEX_HELD(mtx)	\
	VERIFY((mtx)->__data.__owner == syscall(SYS_gettid))
#define	VERIFY_MUTEX_NOT_HELD(mtx) \
	VERIFY((mtx)->__data.__owner != syscall(SYS_gettid))
#else	/* APL */
#define	VERIFY_MUTEX_HELD(mtx)		(void)1
#define	VERIFY_MUTEX_NOT_HELD(mtx)	(void)1
#endif	/* APL */

#else	/* !APL && !LIN */

/**
 * Thread handle object. This gets initialized with a thread handle by
 * a call to thread_create() and is consumed by a variety of other
 * thread-related operations (such as thread_destroy()).
 * @see thread_create()
 */
typedef HANDLE thread_t;
/**
 * Thread ID object. This gets returned by macros such as curthread_id.
 * Certain operations require the thread ID, instead of the handle, so
 * this type encapsulates that detail on platforms where there is a
 * distinction between thread handles and IDs (Windows).
 * @see curthread_id
 */
typedef DWORD thread_id_t;
/**
 * A mutual-exclusion lock. This can be initialized using mutex_init()
 * and afterwards must be disposed of using mutex_destroy(). Mutexes
 * are used to protect critical sections of code, where you want to
 * prevent multiple threads from entering at the same time. Use the
 * mutex_enter() and mutex_exit() functions for that.
 * @see mutex_init()
 * @see mutex_destroy()
 * @see mutex_enter()
 * @see mutex_exit()
 */
typedef struct {
	bool_t			inited;
	CRITICAL_SECTION	cs;
} mutex_t;
/**
 * A condition variable is an object which can be waited on by any
 * number of threads, and signalled by another thread, to notify the
 * waiting threads that a certain condition has been met and/or that
 * the waiting threads' attention is required. A condition variable
 * is always used in conjuction with a mutex. The waiting thread(s)
 * first acquire a mutex to protect a critical section of code. They
 * then wait on a condition variable, which also atomatically
 * relinquishes the mutex, allowing another thread to come acquire
 * the lock and signal the condition. Once signalled, the waiting
 * thread wakes up and atomically acquires the lock (once the
 * signalling thread has relinquished it).
 *
 * Use cv_init() to initialize a condition variable. Once initialized,
 * the object must be destroyed using cv_destroy(). Waiting on the
 * condition variable can be accomplished using either cv_wait() or
 * cv_timedwait(). Signalling the condition is performed using either
 * cv_signal() or cv_broadcast().
 *
 * Example of how to use a condvar_t:
 *```
 *	mutex_t my_lock;
 *	condvar_t my_cv;
 *
 *	mutex_init(&my_lock);		// create a lock to control the CV
 *	cv_init(&my_cv);		// create the condition variable
 *
 *	// thread that's going to signal the condition:
 *		mutex_enter(&my_lock);	// grab the lock
 *		// ... set up some resource that others might be waiting on ...
 *		cv_broadcast(&my_lock);	// wake up all waiters
 *		mutex_exit(&my_lock);	// release the lock
 *
 *	// thread that's going to wait on the condition:
 *		mutex_enter(&my_lock);			// grab the lock
 *		while (!condition_met())
 *			cv_wait(&my_cv, &my_lock);	// wait for the CV
 *							// to be signalled
 *		// ... condition fulfilled, use the resource ...
 *		mutex_exit(&my_lock);			// release the lock
 *```
 * @see cv_init()
 * @see cv_destroy()
 * @see cv_wait()
 * @see cv_timedwait()
 * @see cv_signal()
 * @see cv_broadcast()
 */
typedef CONDITION_VARIABLE condvar_t;

#define	curthread_id	GetCurrentThreadId()
#define	curthread	GetCurrentThread()

/**
 * Initializes a new mutex_t object. The mutex MUST be destroyed using
 * mutex_destroy() to avoid leaking memory.
 * @see mutex_destroy()
 */
static inline void
mutex_init(mutex_t *mtx)
{
	mtx->inited = B_TRUE;
	InitializeCriticalSection(&mtx->cs);
}

/**
 * Destroys mutex_t object previously initialized by a call to mutex_init().
 * @see mutex_init()
 */
static inline void
mutex_destroy(mutex_t *mtx)
{
	if (mtx->inited) {
		DeleteCriticalSection(&mtx->cs);
		mtx->inited = B_FALSE;
	}
}

/**
 * Acquires a mutex, which has previously been initialized using mutex_init().
 * If the mutex cannot be acquired exclusively by the calling thread, the
 * thread blocks until it can be acquired. Once acquired, the mutex MUST be
 * relinquished by a call to mutex_exit().
 * @note mutex_enter() and mutex_exit() support recursive locking, so once a
 *	thread acquires a mutex, it can re-acquire it in nested subroutines
 *	without risk of deadlock:
 *```
 *	mutex_enter(mtx);
 *	...
 *		// subroutine acquires the lock again - this is safe to do
 *		mutex_enter(mtx);
 *		...
 *		mutex_exit(mtx);
 *	...
 *	mutex_exit(mtx);
 *```
 *	All calls to mutex_enter() must be balanced by matching calls to
 *	mutex_exit() before the mutex is finally completely relinquished.
 * @see mutex_exit()
 */
static inline void
mutex_enter(mutex_t *mtx)
{
	ASSERT(mtx->inited);
	EnterCriticalSection(&mtx->cs);
}

/**
 * Relinquishes a mutex previously acquired by a call to mutex_enter().
 * @note mutex_enter() and mutex_exit() support recursive locking, so once a
 *	thread acquires a mutex, it can re-acquire it in nested subroutines
 *	without risk of deadlock:
 *```
 *	mutex_enter(mtx);
 *	...
 *		// subroutine acquires the lock again - this is safe to do
 *		mutex_enter(mtx);
 *		...
 *		mutex_exit(mtx);
 *	...
 *	mutex_exit(mtx);
 *```
 *	All calls to mutex_enter() must be balanced by matching calls to
 *	mutex_exit() before the mutex is finally completely relinquished.
 * @see mutex_enter()
 */
static inline void
mutex_exit(mutex_t *mtx)
{
	ASSERT(mtx->inited);
	LeaveCriticalSection(&mtx->cs);
}

#define	VERIFY_MUTEX_HELD(mtx)		(void)1
#define	VERIFY_MUTEX_NOT_HELD(mtx)	(void)1

#endif	/* !APL && !LIN */

/*
 * This is the thread cleanup tracking machinery.
 */
API_EXPORT extern bool_t	lacf_thread_list_inited;
API_EXPORT extern mutex_t	lacf_thread_list_lock;
API_EXPORT extern list_t	lacf_thread_list;

/**
 * @note Internal. Do not call directly.
 */
static inline void
_lacf_thread_list_init(void)
{
	if (!lacf_thread_list_inited) {
		mutex_init(&lacf_thread_list_lock);
		list_create(&lacf_thread_list, sizeof (lacf_thread_info_t),
		    offsetof(lacf_thread_info_t, node));
		lacf_thread_list_inited = B_TRUE;
	}
}

/**
 * @note Internal. Do not call directly.
 */
static inline void
_lacf_thread_list_fini(void)
{
	if (lacf_thread_list_inited) {
		mutex_enter(&lacf_thread_list_lock);
		if (list_count(&lacf_thread_list) == 0) {
			list_destroy(&lacf_thread_list);
			mutex_exit(&lacf_thread_list_lock);
			mutex_destroy(&lacf_thread_list_lock);
			lacf_thread_list_inited = B_FALSE;
		} else {
			mutex_exit(&lacf_thread_list_lock);
		}
	}
}

/**
 * @note Internal. Do not call directly.
 */
static inline void
_lacf_thread_list_add(lacf_thread_info_t *ti)
{
	ASSERT(ti != NULL);
	ASSERT(lacf_thread_list_inited);
	mutex_enter(&lacf_thread_list_lock);
	list_insert_tail(&lacf_thread_list, ti);
	mutex_exit(&lacf_thread_list_lock);
}

/**
 * @note Internal. Do not call directly.
 */
static inline void
_lacf_thread_list_remove(lacf_thread_info_t *ti)
{
	ASSERT(ti != NULL);
	ASSERT(lacf_thread_list_inited);
	mutex_enter(&lacf_thread_list_lock);
	list_remove(&lacf_thread_list, ti);
	mutex_exit(&lacf_thread_list_lock);
}

/**
 * Checks to see if all threads that were created using thread_create()
 * have been properly disposed of. If not, this trips an assertion
 * failure and lists all threads (including filenames and line numbers
 * where they have been spawned) that weren't properly stopped. You
 * should call this just as your plugin is exiting, to check for leaked
 * threads.
 */
static inline void
lacf_threads_fini(void)
{
	if (!lacf_thread_list_inited)
		return;

	mutex_enter(&lacf_thread_list_lock);
	for (lacf_thread_info_t *ti =
	    (lacf_thread_info_t *)list_head(&lacf_thread_list); ti != NULL;
	    ti = (lacf_thread_info_t *)list_next(&lacf_thread_list, ti)) {
		logMsg("Leaked thread, created here %s:%d", ti->filename,
		    ti->linenum);
	}
	VERIFY0(list_count(&lacf_thread_list));
	mutex_exit(&lacf_thread_list_lock);

	_lacf_thread_list_fini();
}

#if	APL || LIN

UNUSED_ATTR static void *
_lacf_thread_start_routine(void *arg)
{
	lacf_thread_info_t *ti = (lacf_thread_info_t *)arg;
	ti->proc(ti->arg);
	_lacf_thread_list_remove(ti);
	free(ti);
	return (NULL);
}

WARN_UNUSED_RES_ATTR static inline bool_t
lacf_thread_create(thread_t *thrp, void (*proc)(void *), void *arg,
    const char *filename, int linenum)
{
	lacf_thread_info_t *ti =
	    (lacf_thread_info_t *)safe_calloc(1, sizeof (*ti));
	ti->proc = proc;
	ti->arg = arg;
	ti->filename = filename;
	ti->linenum = linenum;
	_lacf_thread_list_init();
	_lacf_thread_list_add(ti);
	if (pthread_create(thrp, NULL, _lacf_thread_start_routine, ti) == 0) {
		/* Start success */
		return (B_TRUE);
	}
	/* Start failure - remove from list and try to destroy the list */
	_lacf_thread_list_remove(ti);
	_lacf_thread_list_fini();
	free(ti);
	return (B_FALSE);
}

static inline void
thread_join(thread_t *thrp)
{
	pthread_join(*thrp, NULL);
	_lacf_thread_list_fini();
}

#if	LIN

static inline void
thread_set_name(const char *name)
{
	pthread_setname_np(pthread_self(), name);
}

#else	/* APL */

static inline void
thread_set_name(const char *name)
{
	pthread_setname_np(name);
}

#endif	/* APL */

static inline void
cv_wait(condvar_t *cv, mutex_t *mtx)
{
	pthread_cond_wait(cv, mtx);
}

static inline int
cv_timedwait(condvar_t *cv, mutex_t *mtx, uint64_t limit)
{
	struct timespec ts = { .tv_sec = (time_t)(limit / 1000000),
	    .tv_nsec = (long)((limit % 1000000) * 1000) };
	return (pthread_cond_timedwait(cv, mtx, &ts));
}

static inline void
cv_init(condvar_t *cv)
{
	pthread_cond_init(cv, NULL);
}

static inline void
cv_destroy(condvar_t *cv)
{
	pthread_cond_destroy(cv);
}

static inline void
cv_signal(condvar_t *cv)
{
	pthread_cond_signal(cv);
}

static inline void
cv_broadcast(condvar_t *cv)
{
	pthread_cond_broadcast(cv);
}

#if	!APL
#define	THREAD_PRIO_IDLE	sched_get_priority_min()
#define	THREAD_PRIO_VERY_LOW	(THREAD_PRIO_NORM - 2)
#define	THREAD_PRIO_LOW		(THREAD_PRIO_NORM - 1)
#define	THREAD_PRIO_NORM	0	/* Default priority on Linux */
#define	THREAD_PRIO_HIGH	(THREAD_PRIO_NORM + 1)
#define	THREAD_PRIO_VERY_HIGH	(THREAD_PRIO_NORM + 2)
#define	THREAD_PRIO_RT		sched_get_priority_max()
static inline void
thread_set_prio(thread_t thr, int prio)
{
	struct sched_param param = {0};
	param.sched_priority = (prio);
	pthread_setschedparam(thr, SCHED_OTHER, &param);
}

#else	/* APL */
/*
 * BIG CAVEAT: Apparently idle thread prioritization is causing massive
 * thread scheduling stability issues on MacOS Monterey with its Rosetta
 * x86 emulation. Threads either don't get scheduled, or they run in
 * "slow mo", gradually speeding up and generally just behave entirely
 * erratically.
 */
#define	THREAD_PRIO_IDLE	0
#define	THREAD_PRIO_VERY_LOW	0
#define	THREAD_PRIO_LOW		0
#define	THREAD_PRIO_NORM	0
#define	THREAD_PRIO_HIGH	0
#define	THREAD_PRIO_VERY_HIGH	0
#define	THREAD_PRIO_RT		0
static inline void
thread_set_prio(thread_t thr, int prio)
{
	UNUSED(thr);
	UNUSED(prio);
}

#endif	/* APL */

#else	/* !APL && !LIN */

/**
 * @note Internal. Do not call directly.
 */
UNUSED_ATTR static DWORD
_lacf_thread_start_routine(void *arg)
{
	lacf_thread_info_t *ti = (lacf_thread_info_t *)arg;
	ti->proc(ti->arg);
	_lacf_thread_list_remove(ti);
	free(ti);
	return (0);
}

/**
 * Implementation of thread_create(). Do not call directly, use
 * the thread_create() macro.
 * @see thread_create()
 */
WARN_UNUSED_RES_ATTR static inline bool_t
lacf_thread_create(thread_t *thrp, void (*proc)(void *), void *arg,
    const char *filename, int linenum)
{
	lacf_thread_info_t *ti =
	    (lacf_thread_info_t *)safe_calloc(1, sizeof (*ti));
	ti->proc = proc;
	ti->arg = arg;
	ti->filename = filename;
	ti->linenum = linenum;
	_lacf_thread_list_init();
	_lacf_thread_list_add(ti);
	if ((*(thrp) = CreateThread(NULL, 0, _lacf_thread_start_routine, ti,
	    0, NULL)) != NULL) {
		/* Start success */
		return (B_TRUE);
	}
	/* Start failure - remove from list and try to destroy the list */
	_lacf_thread_list_remove(ti);
	_lacf_thread_list_fini();
	free(ti);
	return (B_FALSE);
}

/**
 * Waits for a thread to exit. After this function returns, the passed
 * thread has exited and its resources can be safely disposed of.
 * @param thrp A pointer to a thread_t identifying the thread for which to wait.
 * @note This function doesn't *cause* the target thread to exit, it only
 *	blocks the calling thread until the target thread has exited. You
 *	should notify the target thread using other means to exit before
 *	calling thread_join().
 */
static inline void
thread_join(thread_t *thrp)
{
	VERIFY3S(WaitForSingleObject(*thrp, INFINITE), ==, WAIT_OBJECT_0);
	_lacf_thread_list_fini();
}

/**
 * Sets the name of the calling thread. This is useful for debugging
 * purposes, since the thread name is easily visible in a debugger or
 * process analysis tool.
 * @note This function is only supported on macOS and Linux. Furthermore,
 *	on Linux, names longer than 16 bytes (including the terminating
 *	NUL character) will be truncated to 16 bytes. On Windows, calling
 *	this function does nothing.
 */
static inline void
thread_set_name(const char *name)
{
	UNUSED(name);
}

/**
 * Blocks the calling thread until the condition variable is signalled.
 * @param cv The condition variable to wait on.
 * @param mtx A mutex_t which MUST be currently held by the calling thread.
 *	The calling thread atomically relinquishes this mutex and starts
 *	monitoring the condition variable. Once the condition is signalled,
 *	the thread atomically wakes up and acquires the mutex again, so
 *	that when cv_wait() returns, the lock is acquired again by only
 *	the calling thread.
 */
static inline void
cv_wait(condvar_t *cv, mutex_t *mtx)
{
	VERIFY(SleepConditionVariableCS(cv, &mtx->cs, INFINITE));
}

/**
 * Blocks the calling thread until the condition variable is signalled, or
 * until a timeout limit is reached.
 * @param cv The condition variable to wait on.
 * @param mtx A mutex_t which MUST be currently held by the calling thread.
 *	The calling thread atomically relinquishes this mutex and starts
 *	monitoring the condition variable. Once the condition is signalled,
 *	the thread atomically wakes up and acquires the mutex again, so
 *	that when cv_wait() returns, the lock is acquired again by only
 *	the calling thread.
 * @param limit A deadline in microseconds, by which time the thread will
 *	wake up, regardless if the condition has been signalled or not.
 *	The limit must be calculated from the time value as returned by
 *	the microclock() function.
 * @return If the condition has been signalled before the timeout expired,
 *	this function returns zero. Please note that re-acquiring the mutex
 *	might extend beyond this time, so don't depend on the return value
 *	indicating a hard condition of the timeout limit not having been
 *	exceeded.
 * @return If the timeout has been reached without the condition becoming
 *	signalled, this function returns ETIMEDOUT. If an error occurred,
 *	this function returns -1. In both cases, the mutex is re-acquired
 *	before returning.
 */
static inline int
cv_timedwait(condvar_t *cv, mutex_t *mtx, uint64_t limit)
{
	uint64_t now = microclock();
	if (now < limit) {
		/*
		 * The only way to guarantee that when we return due to a
		 * timeout the full microsecond-accurate quantum has elapsed
		 * is to round-up to the nearest millisecond.
		 */
		if (SleepConditionVariableCS(cv, &mtx->cs,
		    ceil((limit - now) / 1000.0)) != 0) {
			return (0);
		}
		if (GetLastError() == ERROR_TIMEOUT)
			return (ETIMEDOUT);
		return (-1);
	}
	return (ETIMEDOUT);
}

/**
 * Initializes a condition variable. Once initialized, a condition variable
 * MUST be destroyed using cv_destroy().
 * @see cv_destroy()
 * @see cv_wait()
 * @see cv_timedwait()
 */
static inline void
cv_init(condvar_t *cv)
{
	InitializeConditionVariable(cv);
}

/**
 * Destroys a condition variable which has been previously initialized
 * using cv_init().
 */
static inline void
cv_destroy(condvar_t *cv)
{
	UNUSED(cv);
}

/**
 * Signals a condition variable to a single waiting thread. If there are
 * multiple threads currently blocked waiting on the condition variable
 * (either using cv_wait() or cv_timedwait()), only *ONE* of the threads
 * is signalled (which one is unpredictable).
 */
static inline void
cv_signal(condvar_t *cv)
{
	WakeConditionVariable(cv);
}

/**
 * Signals a condition variable to *ALL* threads waiting on that
 * condition variable (either using cv_wait() or cv_timedwait()). The
 * threads will wake up (in an unpredictable order), and re-acquire
 * the mutex associated with their cv_wait() or cv_timedwait() call.
 */
static inline void
cv_broadcast(condvar_t *cv)
{
	WakeAllConditionVariable(cv);
}

/**
 * Minimum thread scheduling priority - only use for threads which can
 * can accept very long periods of not getting CPU time if the CPU is busy.
 * @see thread_set_prio()
 */
#define	THREAD_PRIO_IDLE		THREAD_PRIORITY_IDLE
/**
 * Very low thread scheduling priority.
 * @see thread_set_prio()
 */
#define	THREAD_PRIO_VERY_LOW		THREAD_PRIORITY_LOWEST
/**
 * Reduced thread scheduling priority, below normal priority.
 * @see thread_set_prio()
 */
#define	THREAD_PRIO_LOW			THREAD_PRIORITY_BELOW_NORMAL
/**
 * Normal thread scheduling priority. This is the default for newly
 * created threads.
 * @see thread_set_prio()
 */
#define	THREAD_PRIO_NORM		THREAD_PRIORITY_NORMAL
/**
 * Higher than normal thread scheduling priority.
 * @see thread_set_prio()
 */
#define	THREAD_PRIO_HIGH		THREAD_PRIORITY_ABOVE_NORMAL
/**
 * Very high thread scheduling priority.
 * @see thread_set_prio()
 */
#define	THREAD_PRIO_VERY_HIGH		THREAD_PRIORITY_HIGHEST
/**
 * Highest possible thread scheduling priority. Be very careful when
 * using this, as if your thread does a lot of work at this priority,
 * it can starve other threads of CPU time. Use sparingly and only
 * for threads with a known bounded execution time between yields.
 * @see thread_set_prio()
 */
#define	THREAD_PRIO_RT			THREAD_PRIORITY_TIME_CRITICAL
/**
 * Sets the scheduling priority of a thread. The exact implementation is
 * platform dependant:
 * - On Linux, this calls `pthread_setschedparam()` with a policy of
 *	`SCHED_OTHER`.
 * - On macOS, due to a bug in the thread schedling behavior under
 *	Rosetta x86 emulation, thread scheduling is disabled.
 * - On Windows, this calls `SetThreadPriority()`.
 *
 * @param prio Must be one of the `THREAD_PRIO_*` constants.
 */
static inline void
thread_set_prio(thread_t *thr, int prio)
{
	SetThreadPriority(thr, prio);
}

#endif	/* !APL && !LIN */

API_EXPORT void lacf_mask_sigpipe(void);

/**
 * This is a read-write mutex. RWMutexes are mutexes which allow multiple
 * threads to acquire a read lock, but only a single thread to acquire a
 * write lock.
 *
 * Use rwmutex_init() to initialize a new rwmutex_t object. To destroy an
 * rwmutex_t, use rwmutex_destroy(). Acquiring and relinquishing an
 * rwmutex_t is done using rwmutex_enter() and rwmutex_exit().
 *
 * @see rwmutex_init()
 * @see rwmutex_destroy()
 * @see rwmutex_enter()
 * @see rwmutex_exit()
 * @see rwmutex_held_write()
 */
typedef struct {
	mutex_t			lock;
	condvar_t		cv;
	bool_t			write_locked;
	thread_id_t		writer;
	unsigned		refcount;
	list_t			waiters;
} rwmutex_t;

typedef struct {
	bool_t			write;
	list_node_t		node;
} rwlock_waiter_t;

/**
 * Initializes a new rwmutex_t. The mutex must be destroyed using
 * rwmutex_destroy().
 * @see rwmutex_destroy()
 */
UNUSED_ATTR static void
rwmutex_init(rwmutex_t *rw)
{
	memset(rw, 0, sizeof (*rw));
	mutex_init(&rw->lock);
	cv_init(&rw->cv);
	list_create(&rw->waiters, sizeof (rwlock_waiter_t),
	    offsetof(rwlock_waiter_t, node));
}

/**
 * Destroys an rwmutex_t that was previously initialized rwmutex_init().
 * @see rwmutex_init()
 */
UNUSED_ATTR static void
rwmutex_destroy(rwmutex_t *rw)
{
	ASSERT3U(rw->refcount, ==, 0);
	list_destroy(&rw->waiters);
	cv_destroy(&rw->cv);
	mutex_destroy(&rw->lock);
}

/**
 * @return `B_TRUE` if the rwmutex_t is currently held by the calling
 *	thread in write mode, `B_FALSE` otherwise.
 * @note This doesn't determine whether the calling thread is currently
 *	holding the rwmutex_t in read mode. Read mode acquisitions of
 *	the rwmutex_t do not retain any ownership information.
 */
static inline bool_t
rwmutex_held_write(rwmutex_t *rw)
{
	return (rw->write_locked && rw->writer == curthread_id);
}

/**
 * @note Internal. Do not call directly.
 */
static inline bool_t
rwmutex_can_enter_impl(const rwmutex_t *rw, const rwlock_waiter_t *wt_self)
{
	for (const rwlock_waiter_t *wt = (rwlock_waiter_t *)list_head(
	    &rw->waiters);;
	    wt = (rwlock_waiter_t *)list_next(&rw->waiters, wt)) {
		/* Our wt_self MUST be somewhere in rw->waiters! */
		VERIFY(wt != NULL);
		if (wt == wt_self)
			return (B_TRUE);
		if (wt->write)
			return (B_FALSE);
	}
}

/**
 * Acquires an rwmutex_t in either read or write mode. The lock can be
 * simultaneously held in read mode by any number of threads. However,
 * in write mode, the lock can only be held by a single thread. If the
 * lock cannot be acquired immediately, the calling thread is blocked
 * until successful.
 *
 * @note rwmutex_t does NOT support recursion. An attempt to acquire the
 *	lock multiple times from the same thread can cause an assertion
 *	failure or even deadlock.
 *
 * ### Locking Order
 *
 * rwmutex_t implements deterministic locking order. If the lock is
 * currently held by one or more readers and another thread attempts to
 * acquire the lock in write (exclusive) mode, the calling thread is
 * blocked until all existing read locks are relinquished. Furthermore,
 * any newly arriving locking attempts will queue up "behind" any
 * preceding attempts and block. The queue of pending locks is then
 * cleared in order of arrival. Writers can only enter one by one, while
 * a "batched up" group of readers can enter simultaneously. This
 * prevents lock starvation of writers in the presence of a large number
 * of readers and vice versa.
 *
 * @param rw The mutex to acquire.
 * @param write Sets whether the lock should be acquired in write
 *	(`write=B_TRUE`) or read (`write=B_FALSE`) mode.
 */
UNUSED_ATTR static void
rwmutex_enter(rwmutex_t *rw, bool_t write)
{
	rwlock_waiter_t wt = {0};

	wt.write = write;
	/*
	 * No recursion allowed! We can't check for recursive read attempts,
	 * only write (since readers don't retain any ownership information),
	 * so it's best to avoid recursion altogether.
	 */
	ASSERT_MSG(!rwmutex_held_write(rw), "%s", "Attempted to recursively "
	    "acquire an rwmutex_t. This is NOT supported!");

	mutex_enter(&rw->lock);
	/*
	 * Enter the queue of threads waiting to acquire the mutex.
	 */
	list_insert_tail(&rw->waiters, &wt);

	if (write) {
		/*
		 * Wait until everybody else is out of the mutex
		 * and we're next to enter.
		 */
		while (rw->refcount != 0 ||
		    list_head(&rw->waiters) != (void *)&wt) {
			cv_wait(&rw->cv, &rw->lock);
		}
		/*
		 * We're clear to proceed, mark the mutex as
		 * write-locked by us.
		 */
		rw->writer = curthread_id;
		rw->write_locked = B_TRUE;
	} else {
		/*
		 * If the mutex is currently held by a writer, or
		 * there's another writer ahead of us, wait.
		 */
		while (rw->write_locked ||
		    !rwmutex_can_enter_impl(rw, &wt)) {
			cv_wait(&rw->cv, &rw->lock);
		}
	}
	/*
	 * Exit the wait queue. We've now acquired the mutex.
	 */
	list_remove(&rw->waiters, &wt);
	rw->refcount++;

	mutex_exit(&rw->lock);
}

/**
 * Relinquishes a previously acquired read- or write lock of an rwmutex_t.
 */
UNUSED_ATTR static void
rwmutex_exit(rwmutex_t *rw)
{
	mutex_enter(&rw->lock);
	ASSERT(rw->refcount != 0);
	rw->refcount--;
	if (rw->refcount == 0 && rw->write_locked) {
		ASSERT3U(rw->writer, ==, curthread_id);
		rw->write_locked = B_FALSE;
	}
	if (list_head(&rw->waiters) != NULL)
		cv_broadcast(&rw->cv);
	mutex_exit(&rw->lock);
}

/**
 * "Upgrades" a currently held read lock into a write lock.
 */
static inline void
rwmutex_upgrade(rwmutex_t *rw)
{
	rwmutex_exit(rw);
	rwmutex_enter(rw, B_TRUE);
}

#ifdef	DEBUG
#define	ASSERT_MUTEX_HELD(mtx)		VERIFY_MUTEX_HELD(mtx)
#define	ASSERT_MUTEX_NOT_HELD(mtx)	VERIFY_MUTEX_NOT_HELD(mtx)
#else	/* !DEBUG */
#define	ASSERT_MUTEX_HELD(mtx)
#define	ASSERT_MUTEX_NOT_HELD(mtx)
#endif	/* !DEBUG */

#ifdef __cplusplus
}
#endif

#endif	/* _ACF_UTILS_THREAD_H_ */
