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

#ifndef	_ACFUTILS_TASKQ_H_
#define	_ACFUTILS_TASKQ_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct taskq_s taskq_t;

typedef void *(*taskq_init_thr_t)(void *userinfo);
typedef void (*taskq_fini_thr_t)(void *userinfo, void *thr_info);
typedef void (*taskq_proc_task_t)(void *userinfo, void *thr_info, void *task);
typedef void (*taskq_discard_task_t)(void *userinfo, void *task);

API_EXPORT taskq_t *taskq_alloc(unsigned num_threads_min,
    unsigned num_threads_max, uint64_t thr_stop_delay_us,
    taskq_init_thr_t init_func,taskq_fini_thr_t fini_func,
    taskq_proc_task_t proc_func, taskq_discard_task_t discard_func,
    void *userinfo);
API_EXPORT void taskq_free(taskq_t *tq);

API_EXPORT void taskq_submit(taskq_t *tq, void *task);
API_EXPORT bool taskq_wants_shutdown(taskq_t *tq);

API_EXPORT void taskq_set_num_threads_min(taskq_t *tq, unsigned n_threads_min);
API_EXPORT unsigned taskq_get_num_threads_min(const taskq_t *tq);
API_EXPORT void taskq_set_num_threads_max(taskq_t *tq, unsigned n_threads_max);
API_EXPORT unsigned taskq_get_num_threads_max(const taskq_t *tq);
API_EXPORT void taskq_set_thr_stop_delay(taskq_t *tq, uint64_t stop_delay_us);
API_EXPORT uint64_t taskq_get_thr_stop_delay(const taskq_t *tq);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACFUTILS_TASKQ_H_ */
