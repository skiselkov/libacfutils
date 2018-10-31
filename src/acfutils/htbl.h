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

#ifndef	_ACFUTILS_HTBL_H_
#define	_ACFUTILS_HTBL_H_

#include <stdint.h>

#include <acfutils/list.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct {
	size_t		tbl_sz;
	size_t		key_sz;
	list_t		*buckets;
	size_t		num_values;
	int		multi_value;
} htbl_t;

void htbl_create(htbl_t *htbl, size_t tbl_sz, size_t key_sz, int multi_value);
void htbl_destroy(htbl_t *htbl);
void htbl_empty(htbl_t *htbl, void (*func)(void *, void *), void *arg);
size_t htbl_count(const htbl_t *htbl);

void htbl_set(htbl_t *htbl, void *key, void *value);
void htbl_remove(htbl_t *htbl, void *key, int nil_ok);
void htbl_remove_multi(htbl_t *htbl, void *key, void *list_item);

void *htbl_lookup(const htbl_t *htbl, const void *key);
#define	HTBL_VALUE_MULTI(x)	(((htbl_multi_value_t *)(x))->value)
const list_t *htbl_lookup_multi(const htbl_t *htbl, const void *key);

void htbl_foreach(const htbl_t *htbl,
    void (*func)(const void *, void *, void *), void *arg);

char *htbl_dump(const htbl_t *htbl, bool_t printable_keys);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACFUTILS_HTBL_H_ */
