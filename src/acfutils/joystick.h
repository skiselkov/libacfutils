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
 * Copyright 2019 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACF_UTILS_JOYSTICK_H_
#define	_ACF_UTILS_JOYSTICK_H_

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * X-Plane joystick axis assignment values. These are essentialy the list
 * in X-Plane's joystick axis assignment droplist.
 */
typedef enum {
    XPJOY_AXIS_UNASSIGNED =		0,
    XPJOY_AXIS_PITCH =			1,
    XPJOY_AXIS_ROLL =			2,
    XPJOY_AXIS_YAW =			3,
    XPJOY_AXIS_THROTTLE =		4,
    XPJOY_AXIS_COLLECTIVE =		5,
    XPJOY_AXIS_LEFT_TOE_BRAKE =		6,
    XPJOY_AXIS_RIGHT_TOE_BRAKE =	7,
    XPJOY_AXIS_PROP =			8,
    XPJOY_AXIS_MIXTURE =		9,
    XPJOY_AXIS_CARB_HEAT =		10,
    XPJOY_AXIS_FLAPS =			11,
    XPJOY_AXIS_THRUST_VECTOR =		12,
    XPJOY_AXIS_WING_SWEEP =		13,
    XPJOY_AXIS_SPEEDBRAKES =		14,
    XPJOY_AXIS_DISPLACEMENT =		15,
    XPJOY_AXIS_REVERSE =		16,
    XPJOY_AXIS_ELEVATOR_TRIM =		17,
    XPJOY_AXIS_AILERON_TRIM =		18,
    XPJOY_AXIS_RUDDER_TRIM =		19,
    XPJOY_AXIS_THROTTLE_1 =		20,
    XPJOY_AXIS_THROTTLE_2 =		21,
    XPJOY_AXIS_THROTTLE_3 =		22,
    XPJOY_AXIS_THROTTLE_4 =		23,
    XPJOY_AXIS_PROP_1 =			24,
    XPJOY_AXIS_PROP_2 =			25,
    XPJOY_AXIS_PROP_3 =			26,
    XPJOY_AXIS_PROP_4 =			27,
    XPJOY_AXIS_MIXTURE_1 =		28,
    XPJOY_AXIS_MIXTURE_2 =		29,
    XPJOY_AXIS_MIXTURE_3 =		30,
    XPJOY_AXIS_MIXTURE_4 =		31,
    XPJOY_AXIS_REVERSE_1 =		32,
    XPJOY_AXIS_REVERSE_2 =		33,
    XPJOY_AXIS_REVERSE_3 =		34,
    XPJOY_AXIS_REVERSE_4 =		35,
    XPJOY_AXIS_LANDING_GEAR =		36,
    XPJOY_AXIS_NOSEWHEEL_TILLER =	37,
    XPJOY_AXIS_BACKUP_THROTTLE =	38,
    XPJOY_AXIS_THROTTLE_HORIZONTAL =	39,
    XPJOY_AXIS_THROTTLE_VERTICAL =	40,
    XPJOY_AXIS_VIEW_LEFT_RIGHT =	41,
    XPJOY_AXIS_VIEW_UP_DOWN =		42,
    XPJOY_AXIS_VIEW_ZOOM =		43
} xpjoy_axis_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_JOYSTICK_H_ */
