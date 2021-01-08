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

#ifndef	_ACF_UTILS_DELAY_LINE_H_
#define	_ACF_UTILS_DELAY_LINE_H_

#include <string.h>
#include <stdint.h>

#include "assert.h"
#include "core.h"
#include "time.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef uint64_t (*delay_line_time_func_t)(void *userinfo);

/*
 * Implements a generic variable that changes after a short delay.
 * You need to initialize the variable using delay_line_init. Subsequently,
 * you can push changes to it and read the current value. When a change is
 * made, it is propagated into the variable after the delay with which the
 * variable was initialized.
 */
typedef struct {
	union {
		int64_t		i64;
		uint64_t	u64;
		double		f64;
	};
	union {
		int64_t		i64_new;
		uint64_t	u64_new;
		double		f64_new;
	};
	uint64_t		changed_t;
	uint64_t		delay_us;
#ifndef	_MSC_VER
	int			__serialize_marker[0];
#else
	/* MSVC really hates a zero-length array */
	int			__serialize_marker[1];
#endif
	delay_line_time_func_t	time_func;
	void			*time_func_userinfo;
} delay_line_t;

/*
 * Initializes a delay line variable.
 * @param line Pointer to the delay line to initialize.
 * @param delay_us Microsecond delay between pushing a new value to the
 *	delay line before the new value takes effect on read-back.
 */
static inline void
delay_line_init(delay_line_t *line, uint64_t delay_us)
{
	ASSERT(line != NULL);
	memset(line, 0, sizeof (*line));
	line->delay_us = delay_us;
}

static inline void
delay_line_init_time_func(delay_line_t *line, uint64_t delay_us,
    delay_line_time_func_t time_func, void *time_func_userinfo)
{
	ASSERT(line != NULL);
	memset(line, 0, sizeof (*line));
	line->delay_us = delay_us;
	line->time_func = time_func;
	line->time_func_userinfo = time_func_userinfo;
}

static inline void
delay_line_set_delay(delay_line_t *line, uint64_t delay_us)
{
	ASSERT(line != NULL);
	line->delay_us = delay_us;
}

static inline uint64_t
delay_line_get_delay(const delay_line_t *line)
{
	ASSERT(line != NULL);
	return (line->delay_us);
}

/*
 * Functions to pull the current value from a delay line:
 *	delay_line_pull_i64	- reads the delay line as an int64_t
 *	delay_line_pull_u64	- reads the delay line as a uint64_t
 *	delay_line_pull_f64	- reads the delay line as a double
 * If a new value has been pushed to the delay line, these functions
 * keep returning the old value until `delay_us' microseconds have
 * elapsed, after which they start returning the new value.
 */
#define	DEF_DELAY_LINE_PULL(typename, abbrev_type) \
static inline typename \
delay_line_pull_ ## abbrev_type(delay_line_t *line) \
{ \
	uint64_t now; \
	ASSERT(line != NULL); \
	now = (line->time_func != NULL ? \
	    line->time_func(line->time_func_userinfo) : microclock()); \
	if (line->abbrev_type ## _new != line->abbrev_type && \
	    now - line->changed_t >= line->delay_us) { \
		line->abbrev_type = line->abbrev_type ## _new; \
	} \
	return (line->abbrev_type); \
}
DEF_DELAY_LINE_PULL(int64_t, i64)
DEF_DELAY_LINE_PULL(uint64_t, u64)
DEF_DELAY_LINE_PULL(double, f64)

/*
 * Functions to peek at the old value in a delay line without ever
 * affecting its state change to a new value. Can be used in combination
 * with delay_line_push_* to look for a state change in a delay line in
 * response to the passage of time.
 *	delay_line_pull_i64	- peeks at the delay line as an int64_t
 *	delay_line_pull_u64	- peeks at the delay line as a uint64_t
 *	delay_line_pull_f64	- peeks at the delay line as a double
 */
#define	DEF_DELAY_LINE_PEEK(typename, abbrev_type) \
static inline typename \
delay_line_peek_ ## abbrev_type(const delay_line_t *line) \
{ \
	ASSERT(line != NULL); \
	return (line->abbrev_type); \
}
DEF_DELAY_LINE_PEEK(int64_t, i64)
DEF_DELAY_LINE_PEEK(uint64_t, u64)
DEF_DELAY_LINE_PEEK(double, f64)

/*
 * Functions that push a new value to a delay line:
 *	delay_line_push_i64	- pushes an int64_t to the delay line
 *	delay_line_push_u64	- pushes a uint64_t to the delay line
 *	delay_line_push_f64	- pushes a double to the delay line
 * In each case, the current value of the delay line is returned
 * (equivalent to calling `delay_line_pull_*'). If the new value is
 * different from the current value of the delay line, the new value
 * will become the delay line's current value after `delay_us' microsecs.
 */
#define	DEF_DELAY_LINE_PUSH(typename, abbrev_type) \
static inline typename \
delay_line_push_ ## abbrev_type(delay_line_t *line, typename value) \
{ \
	uint64_t now; \
	ASSERT(line != NULL); \
	now = (line->time_func != NULL ? \
	    line->time_func(line->time_func_userinfo) : microclock()); \
	if (line->abbrev_type == line->abbrev_type ## _new && \
	    value != line->abbrev_type ## _new) { \
		line->changed_t = now; \
	} \
	line->abbrev_type ## _new = value; \
	return (delay_line_pull_ ## abbrev_type(line)); \
}
DEF_DELAY_LINE_PUSH(int64_t, i64)
DEF_DELAY_LINE_PUSH(uint64_t, u64)
DEF_DELAY_LINE_PUSH(double, f64)

/*
 * Functions that push a new value to a delay line immediately:
 *	delay_line_push_imm_i64	- pushes an int64_t to the delay line
 *	delay_line_push_imm_u64	- pushes a uint64_t to the delay line
 *	delay_line_push_imm_f64	- pushes a double to the delay line
 * Unlike the delay_line_push_* family of functions, these functions
 * change the delay line immediately to the new value and return the
 * new value.
 */
#define	DEF_DELAY_LINE_PUSH_IMM(typename, abbrev_type) \
static inline typename \
delay_line_push_imm_ ## abbrev_type(delay_line_t *line, typename value) \
{ \
	ASSERT(line != NULL); \
	line->abbrev_type = value; \
	line->abbrev_type ## _new = value; \
	return (line->abbrev_type); \
}
DEF_DELAY_LINE_PUSH_IMM(int64_t, i64)
DEF_DELAY_LINE_PUSH_IMM(uint64_t, u64)
DEF_DELAY_LINE_PUSH_IMM(double, f64)

/*
 * Generics require at least C11.
 */
#if	__STDC_VERSION__ >= 201112L
/*
 * Generic shorthand for delay_line_push_*. Determines the type of push
 * function to call automatically based on the type of the value passed.
 */
#define	DELAY_LINE_PUSH(line, value) \
	_Generic((value), \
	    float:		delay_line_push_f64((line), (value)), \
	    double:		delay_line_push_f64((line), (value)), \
	    uint32_t:		delay_line_push_u64((line), (value)), \
	    uint64_t:		delay_line_push_u64((line), (value)), \
	    default:		delay_line_push_i64((line), (value)))
/*
 * Generic shorthand for delay_line_push_imm_*. Determines the type of push
 * function to call automatically based on the type of the value passed.
 */
#define	DELAY_LINE_PUSH_IMM(line, value) \
	_Generic((value), \
	    float:		delay_line_push_imm_f64((line), (value)), \
	    double:		delay_line_push_imm_f64((line), (value)), \
	    uint32_t:		delay_line_push_imm_u64((line), (value)), \
	    uint64_t:		delay_line_push_imm_u64((line), (value)), \
	    default:		delay_line_push_imm_i64((line), (value)))
#else	/* __STDC_VERSION__ < 201112L */
#define	DELAY_LINE_PUSH(line, value) \
	DELAY_LINE_PUSH_macro_requires_C11_or_greater
#define	DELAY_LINE_PUSH_IMM(line, value) \
	DELAY_LINE_PUSH_IMM_macro_requires_C11_or_greater
#endif	/* __STDC_VERSION__ < 201112L */

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_DELAY_LINE_H_ */
