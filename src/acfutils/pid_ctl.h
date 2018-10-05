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
 * Copyright 2018 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACFUTILS_PID_CTL_H_
#define	_ACFUTILS_PID_CTL_H_

#include <math.h>
#include <acfutils/sysmacros.h>

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
	double	e_prev;		/* previous error value */
	double	e_integ;	/* integrated error value */
	double	e_deriv;	/* derivative error value */

	double	k_p;		/* proportional coefficient */
	double	k_i;		/* integral coefficient */
	double	r_i;		/* integral update rate */
	double	k_d;		/* derivative coefficient */
	double	r_d;		/* derivative update rate */
} pid_ctl_t;

/*
 * Initializes a PID controller.
 *
 * @param pid PID controller structure to be initialized.
 * @param k_p Proportional coefficient (multiplier of how much the
 *	proportional input contributes to the output)..
 * @param k_i Integral coefficient (multiplier of how much the integral
 *	input contributes to the output).
 * @param r_i Rate at which we update the integral to the current error
 *	value. This a FILTER_IN rate argument. Roughly what is expresses
 *	is how quickly the integral approaches the new error value per
 *	unit time. The higher the value, the slower the integral
 *	approaches the current error value.
 * @param k_d Derivative coefficient (multiplier of how much the derivative
 *	input contributes to the output).
 * @param r_d Same as r_i, but for the derivative update. The target
 *	derivative value is computed from a delta-time between updates.
 */
static inline void
pid_ctl_init(pid_ctl_t *pid, double k_p, double k_i, double r_i, double k_d,
    double r_d)
{
	pid->e_prev = NAN;
	pid->e_integ = NAN;
	pid->e_deriv = NAN;
	pid->k_p = k_p;
	pid->k_i = k_i;
	pid->r_i = r_i;
	pid->k_d = k_d;
	pid->r_d = r_d;
}

/*
 * Updates the PID controller with a new error value.
 *
 * @param e New error value with which to update the PID controller.
 *	If you want to reset the PID controller to a nil state, pass
 *	a NAN for this parameter.
 * @param d_t Delta-time elapsed since last update (arbitrary units,
 *	but usually seconds). This is used to control the rate at which
 *	the integral and derivative values are updated.
 */
static inline void
pid_ctl_update(pid_ctl_t *pid, double e, double d_t)
{
	double delta_e = (e - pid->e_prev) / d_t;
	FILTER_IN_NAN(pid->e_integ, e, d_t, pid->r_i);
	FILTER_IN_NAN(pid->e_deriv, delta_e, d_t, pid->r_d);
	pid->e_prev = e;
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
	return (pid->k_p * pid->e_prev + pid->k_i * pid->e_integ +
	    pid->k_d * pid->e_deriv);
}

#ifdef __cplusplus
}
#endif

#endif	/* _ACFUTILS_PID_CTL_H_ */
