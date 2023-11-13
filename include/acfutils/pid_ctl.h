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

#ifndef	_ACFUTILS_PID_CTL_H_
#define	_ACFUTILS_PID_CTL_H_

#include <math.h>
#include <stdio.h>

#include "sysmacros.h"
#include "math.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Generic proportional-integral-derivative (PID) controller implementation.
 * PID controllers are useful tools for aircraft control problems such as
 * autopilot control of flight control surfaces. See Wikipedia for more info.
 *
 * Initialize the controller with pid_ctl_init. Update the controller with
 * a new error value using pid_ctl_update. Read controller outputs via
 * pid_ctl_get. See respective functions for more info.
 */

typedef struct {
	double	e_prev;		/* current error value */
	double	V_prev;		/* previous actual value */
	double	integ;		/* integrated value */
	double	deriv;		/* derivative value */

	double	k_p_gain;
	double	k_p;		/* proportional coefficient */
	double	k_i_gain;
	double	k_i;		/* integral coefficient */
	double	lim_i;
	double	k_d;		/* derivative coefficient */
	double	k_d_gain;
	double	r_d;		/* derivative update rate */

	bool_t	integ_clamp;
} pid_ctl_t;

static inline void pid_ctl_reset(pid_ctl_t *pid);
static inline void pid_ctl_set_k_p(pid_ctl_t *pid, double k_p);
static inline void pid_ctl_set_k_i(pid_ctl_t *pid, double k_i);
static inline void pid_ctl_set_lim_i(pid_ctl_t *pid, double lim_i);
static inline void pid_ctl_set_k_d(pid_ctl_t *pid, double k_d);
static inline void pid_ctl_set_r_d(pid_ctl_t *pid, double r_d);

/*
 * Initializes a PID controller.
 *
 * @param pid PID controller structure to be initialized.
 * @param k_p Proportional coefficient (multiplier of how much the
 *	proportional input contributes to the output)..
 * @param k_i Integral coefficient (multiplier of how much the integral
 *	input contributes to the output).
 * @param lim_i Integration limit value of the controller. The integrated
 *	error value will be clamped to (-lim_i,+lim_i) inclusive. If you
 *	want your PID controller to be unclamped in the integration error
 *	value, call pid_ctl_set_integ_clamp with B_FALSE after init.
 * @param k_d Derivative coefficient (multiplier of how much the derivative
 *	input contributes to the output).
 * @param r_d Rate at which we update the derivative to the current rate
 *	value. This a FILTER_IN rate argument. Roughly what is expresses
 *	is how quickly the derivative approaches the new delta-error value
 *	per unit time. The higher the value, the slower the derivative
 *	approaches the current delta-error value.
 */
static inline void
pid_ctl_init_noreset(pid_ctl_t *pid, double k_p, double k_i, double lim_i,
    double k_d, double r_d)
{
	ASSERT(pid != NULL);
	pid->k_p = k_p;
	pid->k_p_gain = 1;
	pid->k_i = k_i;
	pid->k_i_gain = 1;
	pid->lim_i = lim_i;
	pid->k_d = k_d;
	pid->k_d_gain = 1;
	pid->r_d = r_d;
	pid->integ_clamp = B_TRUE;
}
static inline void
pid_ctl_init(pid_ctl_t *pid, double k_p, double k_i, double lim_i, double k_d,
    double r_d)
{
	ASSERT(pid != NULL);
	pid_ctl_init_noreset(pid, k_p, k_i, lim_i, k_d, r_d);
	pid_ctl_reset(pid);
}

static inline void
pid_ctl_set_integ_clamp(pid_ctl_t *pid, bool_t flag)
{
	ASSERT(pid != NULL);
	pid->integ_clamp = flag;
}

/*
 * Updates the PID controller with a new error value and a new "current"
 * value. The error is used to calculate the proportional and integral
 * response, whereas the current value is used to calculate the derivate
 * response. Passing a separate current value is typically used to avoid
 * derivate response kick when the system's set point is changed.
 *
 * @param e New error value with which to update the PID controller's
 *	proportional and integral responses.
 * @param V New "current" process value which is used to update the
 *	controller's derivate response.
 * @param d_t Delta-time elapsed since last update (arbitrary units,
 *	but usually seconds). This is used to control the rate at which
 *	the integral and derivative values are updated.
 */
static inline void
pid_ctl_update_dV(pid_ctl_t *pid, double e, double V, double d_t)
{
	double delta_V;

	ASSERT(pid != NULL);

	delta_V = (V - pid->V_prev) / d_t;
	if (isnan(pid->integ))
		pid->integ = 0;
	pid->integ = clamp(pid->integ + e * d_t, -pid->lim_i, pid->lim_i);
	/*
	 * Clamp the integrated value to the current proportional value. This
	 * prevents excessive over-correcting when the value returns to center.
	 */
	if (pid->integ_clamp) {
		if (e < 0)
			pid->integ = MAX(pid->integ, e);
		else
			pid->integ = MIN(pid->integ, e);
	}
	if (!isnan(delta_V))
		FILTER_IN_NAN(pid->deriv, delta_V, d_t, pid->r_d);
	pid->e_prev = e;
	pid->V_prev = V;
}

/*
 * Same as pid_ctl_update_dV, but passes the error value as the current
 * value as well, which means all three responses from the PID controller
 * are based only on the error value.
 */
static inline void
pid_ctl_update(pid_ctl_t *pid, double e, double d_t)
{
	pid_ctl_update_dV(pid, e, e, d_t);
}

/*
 * Reads the current output of a PID controller. You should call this
 * after calling pid_ctl_update with a new value for the current
 * simulator frame. Please note that the first call to a freshly
 * initialized PID controller, or one that was reset by passing a NAN
 * error value in pid_ctl_update, this function will return NAN. That's
 * because the PID controller needs at least two update calls to
 * establish value trends. So be prepared to test for (via isnan())
 * and reject a NAN value from the PID controller.
 */
static inline double
pid_ctl_get(const pid_ctl_t *pid)
{
	ASSERT(pid != NULL);
	ASSERT(!isnan(pid->e_prev));
	return (pid->k_p_gain * pid->k_p * pid->e_prev +
	    pid->k_i_gain * pid->k_i * pid->integ +
	    pid->k_d_gain * pid->k_d * pid->deriv);
}

/*
 * Sets a PID controller to its initial "reset" state. After this, you
 * must call pid_ctl_update at least twice before the PID controller
 * starts returning non-NAN values from pid_ctl_get.
 */
static inline void
pid_ctl_reset(pid_ctl_t *pid)
{
	ASSERT(pid != NULL);
	pid->e_prev = NAN;
	pid->V_prev = NAN;
	pid->integ = NAN;
	pid->deriv = NAN;
}

/*
 * Sets the PID controller's proportional coefficient. Use this to
 * dynamic reconfigure the PID controller after initializing it.
 */
static inline void
pid_ctl_set_k_p(pid_ctl_t *pid, double k_p)
{
	ASSERT(pid != NULL);
	pid->k_p = k_p;
}

static inline double
pid_ctl_get_k_p(const pid_ctl_t *pid)
{
	ASSERT(pid != NULL);
	return (pid->k_p);
}

static inline void
pid_ctl_set_k_p_gain(pid_ctl_t *pid, double k_p_gain)
{
	ASSERT(pid != NULL);
	pid->k_p_gain = k_p_gain;
}

static inline double
pid_ctl_get_k_p_gain(const pid_ctl_t *pid)
{
	ASSERT(pid != NULL);
	return (pid->k_p_gain);
}

/*
 * Sets the PID controller's integral coefficient. Use this to
 * dynamic reconfigure the PID controller after initializing it.
 */
static inline void
pid_ctl_set_k_i(pid_ctl_t *pid, double k_i)
{
	ASSERT(pid != NULL);
	pid->k_i = k_i;
}

static inline double
pid_ctl_get_k_i(const pid_ctl_t *pid)
{
	ASSERT(pid != NULL);
	return (pid->k_i);
}

static inline void
pid_ctl_set_k_i_gain(pid_ctl_t *pid, double k_i_gain)
{
	ASSERT(pid != NULL);
	pid->k_i_gain = k_i_gain;
}

static inline double
pid_ctl_get_k_i_gain(const pid_ctl_t *pid)
{
	ASSERT(pid != NULL);
	return (pid->k_i_gain);
}

/*
 * Sets the PID controller's integration error value limit. Use
 * pid_ctl_set_integ_clamp to disable integration error clamping.
 */
static inline void
pid_ctl_set_lim_i(pid_ctl_t *pid, double lim_i)
{
	ASSERT(pid != NULL);
	pid->lim_i = lim_i;
}

static inline double
pid_ctl_get_lim_i(const pid_ctl_t *pid)
{
	ASSERT(pid != NULL);
	return (pid->lim_i);
}

/*
 * Sets the PID controller's integral coefficient. Use this to
 * dynamic reconfigure the PID controller after initializing it.
 */
static inline void
pid_ctl_set_k_d(pid_ctl_t *pid, double k_d)
{
	ASSERT(pid != NULL);
	pid->k_d = k_d;
}

static inline double
pid_ctl_get_k_d(const pid_ctl_t *pid)
{
	ASSERT(pid != NULL);
	return (pid->k_d);
}

static inline void
pid_ctl_set_k_d_gain(pid_ctl_t *pid, double k_d_gain)
{
	ASSERT(pid != NULL);
	pid->k_d_gain = k_d_gain;
}

static inline double
pid_ctl_get_k_d_gain(const pid_ctl_t *pid)
{
	ASSERT(pid != NULL);
	return (pid->k_d_gain);
}

static inline void
pid_ctl_set_r_d(pid_ctl_t *pid, double r_d)
{
	ASSERT(pid != NULL);
	pid->r_d = r_d;
}

static inline double
pid_ctl_get_r_d(const pid_ctl_t *pid)
{
	ASSERT(pid != NULL);
	return (pid->r_d);
}

static inline void
pid_ctl_set_integ(pid_ctl_t *pid, double integ)
{
	ASSERT(pid != NULL);
	pid->integ = integ;
}

static inline double
pid_ctl_get_integ(const pid_ctl_t *pid)
{
	ASSERT(pid != NULL);
	return (pid->integ);
}

static inline void
pid_ctl_set_deriv(pid_ctl_t *pid, double deriv)
{
	ASSERT(pid != NULL);
	pid->deriv = deriv;
}

static inline double
pid_ctl_get_deriv(const pid_ctl_t *pid)
{
	ASSERT(pid != NULL);
	return (pid->deriv);
}

/*
 * Sets all 3 gain values in one call.
 */
static inline void
pid_ctl_set_gain(pid_ctl_t *pid, double gain)
{
	pid_ctl_set_k_p_gain(pid, gain);
	pid_ctl_set_k_d_gain(pid, gain);
	pid_ctl_set_k_i_gain(pid, gain);
}

#define	PID_CTL_DEBUG(pid_ptr) \
	do { \
		const pid_ctl_t *pid = (pid_ptr); \
		printf(#pid_ptr ": e: %f  integ: %f  deriv: %f\n", \
		    pid->e_prev, pid->integ, pid->deriv); \
	} while (0)

#ifdef __cplusplus
}
#endif

#endif	/* _ACFUTILS_PID_CTL_H_ */
