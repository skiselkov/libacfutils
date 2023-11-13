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
 * Copyright 2023 Saso Kiselkov. All rights reserved.
 */
/**
 * \file
 * Implements a generic variable that changes after a short delay.
 * You need to initialize the variable using delay_line_init(). Subsequently,
 * you can push changes to it and read the current value. When a change is
 * made, it is propagated into the variable after the delay with which the
 * variable was initialized.
 */

#ifndef	_ACF_UTILS_DELAY_LINE_H_
#define	_ACF_UTILS_DELAY_LINE_H_

#include <string.h>
#include <stdint.h>

#include "assert.h"
#include "core.h"
#include "crc64.h"
#include "time.h"

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Callback function that can be passed to delay_line_init_time_func().
 * This allows you to provide a custom timing function, instead of relaying
 * on the OS's real time clock.
 * \see delay_line_init_time_func()
 */
typedef uint64_t (*delay_line_time_func_t)(void *userinfo);

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
	uint64_t		delay_base_us;
	double			delay_rand_fract;
#ifndef	_MSC_VER
	int			__serialize_marker[0];
#else
	/* MSVC really hates a zero-length array */
	int			__serialize_marker[1];
#endif
	delay_line_time_func_t	time_func;
	void			*time_func_userinfo;
} delay_line_t;

/**
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
	line->delay_base_us = delay_us;
	line->delay_rand_fract = 0;
}

/**
 * Same as delay_line_init(), but provides a custom timing function,
 * instead of using the operating system's real time clock. You can use
 * this to implement time delays that respect X-Plane variable simulation
 * rate and thus operate correctly when the sim is running time-accelerated.
 * @param time_func A callback that the delay line uses for timing. This
 *	must return the current time in microseconds (the starting point
 *	doesn't matter, but it must never wrap around).
 * @param time_func_userinfo Optional argument which will be passed to
 *	`time_func` every time it is called.
 */
static inline void
delay_line_init_time_func(delay_line_t *line, uint64_t delay_us,
    delay_line_time_func_t time_func, void *time_func_userinfo)
{
	ASSERT(line != NULL);
	memset(line, 0, sizeof (*line));
	line->delay_us = delay_us;
	line->delay_base_us = delay_us;
	line->delay_rand_fract = 0;
	line->time_func = time_func;
	line->time_func_userinfo = time_func_userinfo;
}

/**
 * For delay lines which utilize a randomized factor, causes them to
 * recompute the next firing delay. For delay lines without any randomness
 * to their delay, this does nothing.
 */
static inline void
delay_line_refresh_delay(delay_line_t *line)
{
	ASSERT(line != NULL);
	if (line->delay_rand_fract == 0) {
		line->delay_us = line->delay_base_us;
	} else {
		uint64_t rand_us = line->delay_rand_fract * line->delay_base_us;
		line->delay_us = line->delay_base_us +
		    (uint64_t)((crc64_rand_fract() - 0.5) * rand_us);
	}
}

/**
 * Changes the time delay of the delay line.
 */
static inline void
delay_line_set_delay(delay_line_t *line, uint64_t delay_us)
{
	ASSERT(line != NULL);
	line->delay_base_us = delay_us;
	delay_line_refresh_delay(line);
}

/**
 * @return The base time delay in microseconds of the delay line.
 *	This doesn't include any randomness you may have specified
 *	using delay_line_set_rand().
 */
static inline uint64_t
delay_line_get_delay(const delay_line_t *line)
{
	ASSERT(line != NULL);
	return (line->delay_base_us);
}

/**
 * @return The actual time delay in microseconds of the delay line.
 *	This includes the randomness of randomized delay lines, configured
 *	through delay_line_set_rand(). Subsequent firings of a randomized
 *	delay line will return different values here.
 */
static inline uint64_t
delay_line_get_delay_act(const delay_line_t *line)
{
	ASSERT(line != NULL);
	return (line->delay_us);
}

/**
 * Configures randomness for the delay line. Initially, all delay lines
 * are completely fixed-length and deterministic. Sometimes, it is useful
 * to simulate a some variability in the delay of the delay line. The
 * random time delay is recomputed every time the delay line changes
 * state.
 *
 * @param rand_fract The fraction of randomness that should be applied
 *	to the delay line's delay. This is applied as a fraction of the
 *	base time delay, in both directions equally and linearly. So
 *	if you pass `rand_fract=0.4`, that means the delay line will fire
 *	randomly between 0.6x and 1.4x its base time delay.
 */
static inline void
delay_line_set_rand(delay_line_t *line, double rand_fract)
{
	ASSERT(line != NULL);
	ASSERT3F(rand_fract, >=, 0);
	ASSERT3F(rand_fract, <=, 1);
	line->delay_rand_fract = rand_fract;
	delay_line_refresh_delay(line);
}

/**
 * @return The randomness factor of the delay line, as previously set
 *	using delay_line_set_rand(). Newly created delay lines will
 *	always return 0 here.
 */
static inline double
delay_line_get_rand(const delay_line_t *line)
{
	return (line->delay_rand_fract);
}

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
		delay_line_refresh_delay(line); \
	} \
	return (line->abbrev_type); \
}

/**
 * Accessor function to pull the current value from a delay line as an
 * int64_t. If a new value has been pushed to the delay line, this function
 * keeps returning the old value until the delay line's delay has elapsed,
 * after which it will start returning the new value.
 */
DEF_DELAY_LINE_PULL(int64_t, i64)
/**
 * Same as delay_line_pull_i64(), but returns the current value as a `uint64_t`.
 */
DEF_DELAY_LINE_PULL(uint64_t, u64)
/**
 * Same as delay_line_pull_i64(), but returns the current value as a `double`.
 */
DEF_DELAY_LINE_PULL(double, f64)

#define	DEF_DELAY_LINE_PEEK(typename, abbrev_type) \
static inline typename \
delay_line_peek_ ## abbrev_type(const delay_line_t *line) \
{ \
	ASSERT(line != NULL); \
	return (line->abbrev_type); \
}
/**
 * Accessor function to peek at the current value of a delay line as an
 * int64_t. Unlike delay_line_pull_i64(), this will never cause the value
 * to change. Can be used in combination with delay_line_push_i64 to look
 * for a state change in a delay line in response to the passage of time.
 */
DEF_DELAY_LINE_PEEK(int64_t, i64)
/**
 * Same as delay_line_peek_i64(), but returns the current value as a `uint64_t`.
 */
DEF_DELAY_LINE_PEEK(uint64_t, u64)
/**
 * Same as delay_line_peek_i64(), but returns the current value as a `double`.
 */
DEF_DELAY_LINE_PEEK(double, f64)

/**
 * Same as delay_line_peek_i64(), but instead of looking at the current
 * value in the delay line, this looks at a new incoming value, without
 * causing the delay line to change.
 */
DEF_DELAY_LINE_PEEK(int64_t, i64_new)
/**
 * Same as delay_line_peek_i64_new(), but returns the new value as a `uint64_t`.
 */
DEF_DELAY_LINE_PEEK(uint64_t, u64_new)
/**
 * Same as delay_line_peek_i64_new(), but returns the new value as a `double`.
 */
DEF_DELAY_LINE_PEEK(double, f64_new)

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
		delay_line_refresh_delay(line); \
	} \
	line->abbrev_type ## _new = value; \
	return (delay_line_pull_ ## abbrev_type(line)); \
}
/**
 * This function pushes a new `int64_t` value to a delay line.
 * If the new value is different from the current value of the delay line,
 * the new value will become the delay line's current value after the
 * delay line's time delay has elapsed.
 * @return The current value of the delay line (equivalent to calling
 *	delay_line_pull_i64()).
 */
DEF_DELAY_LINE_PUSH(int64_t, i64)
/**
 * Same as delay_line_push_i64(), but pushes a new `uint64_t` value.
 */
DEF_DELAY_LINE_PUSH(uint64_t, u64)
/**
 * Same as delay_line_push_i64(), but pushes a new `double` value.
 */
DEF_DELAY_LINE_PUSH(double, f64)

#define	DEF_DELAY_LINE_PUSH_IMM(typename, abbrev_type) \
static inline typename \
delay_line_push_imm_ ## abbrev_type(delay_line_t *line, typename value) \
{ \
	ASSERT(line != NULL); \
	line->abbrev_type = value; \
	line->abbrev_type ## _new = value; \
	return (line->abbrev_type); \
}
/**
 * Same as delay_line_push_i64(), but doesn't wait for the time delay.
 * The new value immediately becomes the delay line's current value.
 */
DEF_DELAY_LINE_PUSH_IMM(int64_t, i64)
/**
 * Same as delay_line_push_imm_i64(), but pushes a new `uint64_t` value.
 */
DEF_DELAY_LINE_PUSH_IMM(uint64_t, u64)
/**
 * Same as delay_line_push_imm_i64(), but pushes a new `double` value.
 */
DEF_DELAY_LINE_PUSH_IMM(double, f64)

/**
 * \def DELAY_LINE_PUSH
 * Generic shorthand for delay_line_push_*. Determines the type of push
 * function to call automatically based on the type of the value passed.
 * Note: this relies on C11 generics and thus requires at least C11 support.
 */
/**
 * \def DELAY_LINE_PUSH_IMM
 * Generic shorthand for delay_line_push_imm_*. Determines the type of push
 * function to call automatically based on the type of the value passed.
 * Note: this relies on C11 generics and thus requires at least C11 support.
 */

/*
 * Generics require at least C11.
 */
#if	__STDC_VERSION__ >= 201112L
#define	DELAY_LINE_PUSH(line, value) \
	_Generic((value), \
	    float:		delay_line_push_f64((line), (value)), \
	    double:		delay_line_push_f64((line), (value)), \
	    uint32_t:		delay_line_push_u64((line), (value)), \
	    uint64_t:		delay_line_push_u64((line), (value)), \
	    default:		delay_line_push_i64((line), (value)))
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

/**
 * @return The amount of time elapsed since the delay line has last changed
 *	to reflect a new value. This uses the delay line's own timing
 *	function (in case one is configured), or the OS's real time clock.
 *
 * CAUTION: do NOT use this for precise interval timing. Delay lines can be
 * used as simple timed triggers, but they don't keep time accurately, or
 * account for triggering-overshoot. If you try this, your clock will end
 * up slipping and running too slow!
 */
static inline uint64_t
delay_line_get_time_since_change(const delay_line_t *line)
{
	uint64_t now;
	ASSERT(line != NULL);
	now = (line->time_func != NULL ?
	    line->time_func(line->time_func_userinfo) : microclock());
	return (now - line->changed_t);
}

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_DELAY_LINE_H_ */
