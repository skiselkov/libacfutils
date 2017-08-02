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
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ACFUTILS_CONF_H_
#define	_ACFUTILS_CONF_H_

#include <stdio.h>
#include <stdint.h>

#include <acfutils/types.h>
#include <acfutils/avl.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct conf conf_t;

conf_t *conf_create_empty(void);
void conf_free(conf_t *conf);

conf_t *conf_read(FILE *fp, int *errline);
bool_t conf_write(const conf_t *conf, FILE *fp);

bool_t conf_get_str(const conf_t *conf, const char *key, const char **value);
bool_t conf_get_i(const conf_t *conf, const char *key, int *value);
bool_t conf_get_d(const conf_t *conf, const char *key, double *value);
bool_t conf_get_b(const conf_t *conf, const char *key, bool_t *value);

void conf_set_str(conf_t *conf, const char *key, const char *value);
void conf_set_i(conf_t *conf, const char *key, int value);
void conf_set_d(conf_t *conf, const char *key, double value);
void conf_set_b(conf_t *conf, const char *key, bool_t value);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACFUTILS_CONF_H_ */
