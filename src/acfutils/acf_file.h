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
 * This module contains functionality to parse and traverse X-Plane
 * .acf files. This can be used to interrogate aircraft properties,
 * as well as to read physics model shape outlines.
 * @see acf_file_read()
 * @see acf_prop_find()
 */

#ifndef	_ACF_UTILS_ACF_FILE_H_
#define	_ACF_UTILS_ACF_FILE_H_

#include <stdint.h>

#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct acf_file acf_file_t;

API_EXPORT acf_file_t *acf_file_read(const char *filename);
API_EXPORT void acf_file_free(acf_file_t *acf);
API_EXPORT const char *acf_prop_find(const acf_file_t *acf,
    const char *prop_path);

API_EXPORT int acf_file_get_version(const acf_file_t *acf);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_ACF_FILE_H_ */
