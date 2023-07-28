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
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */

#include <stdbool.h>
#include <stdint.h>

#include <acfutils/assert.h>
#include <acfutils/log.h>
#include <acfutils/thread.h>

enum { NUM_WORKERS = 8 };

static bool shutdown = false;
static rwmutex_t mutex;
static uint64_t watchdog[NUM_WORKERS] = {0};
static thread_t threads[NUM_WORKERS] = {0};
static atomic64_t lock_ops[NUM_WORKERS] = {0};
static long long common_counter = 0;

static void
log_func(const char *str)
{
	fputs(str, stderr);
}

static void
worker_func(void *arg)
{
	unsigned thread_nr = (uintptr_t)arg;

	ASSERT3U(thread_nr, <, NUM_WORKERS);

	while (!shutdown) {
		rwmutex_enter(&mutex, thread_nr % 2 == 0);
		atomic_inc_64(&lock_ops[thread_nr]);
		if (thread_nr % 2 == 0)
			common_counter++;
		rwmutex_exit(&mutex);
		watchdog[thread_nr] = microclock();
	}
}

int
main(void)
{
	log_init(log_func, "rwmutex");

	rwmutex_init(&mutex);
	for (int i = 0; i < NUM_WORKERS; i++) {
		watchdog[i] = microclock();
		VERIFY(thread_create(&threads[i], worker_func,
		    (void *)(uintptr_t)i));
	}
	for (int i = 0; i < 100; i++) {
		uint64_t now = microclock();
		long long lock_ops_total = 0;
		for (int i = 0; i < NUM_WORKERS; i++) {
			VERIFY_MSG(now - watchdog[i] < 500000llu,
			    "Thread %d watchdog timeout", i);
			lock_ops_total += lock_ops[i];
		}
		for (int i = 0; i < 48; i++)
			printf("\b");
		printf("Lock Ops: %lld  Common counter: %lld",
		    lock_ops_total, common_counter);
		fflush(stdout);
		usleep(100000);
	}
	printf("\n");

	shutdown = true;
	for (int i = 0; i < NUM_WORKERS; i++)
		thread_join(&threads[i]);
	rwmutex_destroy(&mutex);

	log_fini();

	return (0);
}
