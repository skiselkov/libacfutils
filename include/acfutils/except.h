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
 * This file contains an exception/crash catching machinery. Please note
 * that this system may override any other crash handlers and on Linux &
 * MacOS, there can usually only be a single crash handler. So you should
 * definitely NOT use this if you're not the loaded aircraft addon, or
 * you're not using this for development & testing.
 *
 * Once the custom crash handlers are installed, if a crash or unexpected
 * exception occurs, our custom crash handler catches that condition and
 * attempts to print a suitable error message. The crash handler performs
 * a stack backtrace and attempts to analyze the symbols of the crashed
 * binary to provide as much debug information as possible.
 *
 * @see except_init()
 * @see except_fini()
 */

#ifndef	_ACFUTILS_EXCEPT_H_
#define	_ACFUTILS_EXCEPT_H_

#include "core.h"

#ifdef __cplusplus
extern "C" {
#endif

API_EXPORT void except_init(void);
API_EXPORT void except_fini(void);

#ifdef __cplusplus
}
#endif

#endif	/* _ACFUTILS_EXCEPT_H_ */
