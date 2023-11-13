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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_ACF_UTILS_PASTE_H_
#define	_ACF_UTILS_PASTE_H_

#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

API_EXPORT bool_t paste_init(void);
API_EXPORT void paste_fini(void);
API_EXPORT bool_t paste_get_str(char *str, size_t cap);
API_EXPORT bool_t paste_set_str(const char *str);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_PASTE_H_ */
