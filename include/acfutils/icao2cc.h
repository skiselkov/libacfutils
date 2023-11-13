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
/**
 * \file
 * Translation functionality to convert ICAO airport codes into
 * country codes and language codes. This is useful for building
 * flight information systems.
 */

#ifndef	_ICAO2CC_H_
#define	_ICAO2CC_H_

#include "core.h"

#ifdef	__cplusplus
extern "C" {
#endif

API_EXPORT const char *icao2cc(const char *icao);
API_EXPORT const char *icao2lang(const char *icao);

#ifdef	__cplusplus
}
#endif

#endif	/* _ICAO2CC_H_ */
