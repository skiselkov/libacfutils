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

#include <acfutils/helpers.h>
#include <acfutils/types.h>
#include <acfutils/avl.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct conf conf_t;

API_EXPORT conf_t *conf_create_empty(void);
API_EXPORT void conf_free(conf_t *conf);

API_EXPORT conf_t *conf_read_file(const char *filename, int *errline);
API_EXPORT conf_t *conf_read(FILE *fp, int *errline);
API_EXPORT bool_t conf_write_file(const conf_t *conf, const char *filename);
API_EXPORT bool_t conf_write(const conf_t *conf, FILE *fp);

API_EXPORT bool_t conf_get_str(const conf_t *conf, const char *key,
    const char **value);
API_EXPORT bool_t conf_get_i(const conf_t *conf, const char *key,
    int *value);
API_EXPORT bool_t conf_get_lli(const conf_t *conf, const char *key,
    long long *value);
API_EXPORT bool_t conf_get_f(const conf_t *conf, const char *key,
    float *value);
API_EXPORT bool_t conf_get_d(const conf_t *conf, const char *key,
    double *value);
API_EXPORT bool_t conf_get_da(const conf_t *conf, const char *key,
    double *value);
API_EXPORT bool_t conf_get_b(const conf_t *conf, const char *key,
    bool_t *value);

API_EXPORT void conf_set_str(conf_t *conf, const char *key, const char *value);
API_EXPORT void conf_set_i(conf_t *conf, const char *key, int value);
API_EXPORT void conf_set_lli(conf_t *conf, const char *key, long long value);
API_EXPORT void conf_set_f(conf_t *conf, const char *key, float value);
API_EXPORT void conf_set_d(conf_t *conf, const char *key, double value);
API_EXPORT void conf_set_da(conf_t *conf, const char *key, double value);
API_EXPORT void conf_set_b(conf_t *conf, const char *key, bool_t value);

API_EXPORT bool_t conf_get_str_v(const conf_t *conf,
    PRINTF_FORMAT(const char *fmt), const char **value, ...) PRINTF_ATTR2(2, 4);
API_EXPORT bool_t conf_get_i_v(const conf_t *conf,
    PRINTF_FORMAT(const char *fmt), int *value, ...) PRINTF_ATTR2(2, 4);
API_EXPORT bool_t conf_get_lli_v(const conf_t *conf,
    PRINTF_FORMAT(const char *fmt), long long *value, ...) PRINTF_ATTR2(2, 4);
API_EXPORT bool_t conf_get_f_v(const conf_t *conf,
    PRINTF_FORMAT(const char *fmt), float *value, ...) PRINTF_ATTR2(2, 4);
API_EXPORT bool_t conf_get_d_v(const conf_t *conf,
    PRINTF_FORMAT(const char *fmt), double *value, ...) PRINTF_ATTR2(2, 4);
API_EXPORT bool_t conf_get_da_v(const conf_t *conf,
    PRINTF_FORMAT(const char *fmt), double *value, ...) PRINTF_ATTR2(2, 4);
API_EXPORT bool_t conf_get_b_v(const conf_t *conf,
    PRINTF_FORMAT(const char *fmt), bool_t *value, ...) PRINTF_ATTR2(2, 4);

API_EXPORT void conf_set_str_v(conf_t *conf, PRINTF_FORMAT(const char *fmt),
    const char *value, ...) PRINTF_ATTR2(2, 4);
API_EXPORT void conf_set_i_v(conf_t *conf, PRINTF_FORMAT(const char *fmt),
    int value, ...) PRINTF_ATTR2(2, 4);
API_EXPORT void conf_set_lli_v(conf_t *conf, PRINTF_FORMAT(const char *fmt),
    long long value, ...) PRINTF_ATTR2(2, 4);
API_EXPORT void conf_set_f_v(conf_t *conf, PRINTF_FORMAT(const char *fmt),
    double value, ...) PRINTF_ATTR2(2, 4);
API_EXPORT void conf_set_d_v(conf_t *conf, PRINTF_FORMAT(const char *fmt),
    double value, ...) PRINTF_ATTR2(2, 4);
API_EXPORT void conf_set_da_v(conf_t *conf, PRINTF_FORMAT(const char *fmt),
    double value, ...) PRINTF_ATTR2(2, 4);
API_EXPORT void conf_set_b_v(conf_t *conf, PRINTF_FORMAT(const char *fmt),
    bool_t value, ...) PRINTF_ATTR2(2, 4);

API_EXPORT bool_t conf_walk(const conf_t *conf, const char **key,
    const char **value, void **cookie);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACFUTILS_CONF_H_ */
