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
 * Internationalization (i18n) support routines. Use acfutils_xlate_init()
 * to initialize the i18n engine and acfutils_xlate_fini() to deinitialize
 * it at shutdown (and free allocated resources).
 *
 * To translate a string, you can either use the acfutils_xlate() function,
 * or the shorthand `_("string")` macro.
 *
 * @see acfutils_xlate_init()
 * @see acfutils_xlate_fini()
 * @see acfutils_xlate()
 */

#ifndef	_ACF_UTILS_INTL_H_
#define	_ACF_UTILS_INTL_H_

#include <stdint.h>

#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * This macro is a shorthand for invoking acfutils_xlate(). This provides
 * a convenient in-line method to localize messages, e.g.:
 *```
 * // The message below will automatically be translated
 * logMsg(_("Hello World!"));
 *```
 * If acfutils_xlate_init() was called and a suitable translation exists,
 * the translated text will be printed. Otherwise, "Hello World!" will be
 * printed.
 */
#define	_(str)	acfutils_xlate(str)

API_EXPORT bool_t acfutils_xlate_init(const char *po_file);
API_EXPORT void acfutils_xlate_fini(void);
API_EXPORT const char *acfutils_xlate(const char *msgid);

API_EXPORT const char *acfutils_xplang2code(int lang);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_INTL_H_ */
