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
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACF_UTILS_ACF_FILE_H_
#define	_ACF_UTILS_ACF_FILE_H_

#include <stdint.h>

#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct acf_file acf_file_t;

#define	acf_file_read	ACFSYM(acf_file_read)
API_EXPORT acf_file_t *acf_file_read(const char *filename);

#define	acf_file_free	ACFSYM(acf_file_free)
API_EXPORT void acf_file_free(acf_file_t *acf);

#define	acf_prop_find	ACFSYM(acf_prop_find)
API_EXPORT const char *acf_prop_find(const acf_file_t *acf,
    const char *prop_path);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_ACF_FILE_H_ */
