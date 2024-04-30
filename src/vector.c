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
 * Copyright 2024 Saso Kiselkov. All rights reserved.
 */

#include "acfutils/helpers.h"
#include "acfutils/safe_alloc.h"
#include "acfutils/vector.h"
#include "acfutils/thread.h"

static inline void
vector_grow(vector_t REQ_PTR(v), size_t new_size)
{
	if (new_size <= v->cap) {
		return;
	}
	v->cap = P2ROUNDUP(new_size);
	v->buf = safe_realloc(v->buf, v->cap * sizeof (*v->buf));
}

void
vector_create(vector_t REQ_PTR(v))
{
	*v = (vector_t){};
}

void
vector_create_cap(vector_t REQ_PTR(v), size_t cap_hint)
{
	*v = (vector_t){};
	if (cap_hint != 0) {
		v->cap = P2ROUNDUP(cap_hint);
		v->buf = safe_malloc(v->cap * sizeof (*v->buf));
	}
}

void
vector_destroy(vector_t REQ_PTR(v))
{
	VERIFY0(v->fill);
	free(v->buf);
	*v = (vector_t){};
}

size_t
vector_len(const vector_t REQ_PTR(v))
{
	return (v->fill);
}

void *
vector_get(const vector_t REQ_PTR(v), size_t index)
{
	VERIFY3U(index, <, v->fill);
	return (v->buf[index]);
}

void *
vector_head(const vector_t REQ_PTR(v))
{
	if (v->fill != 0) {
		return (v->buf[0]);
	}
	return (NULL);
}

void *
vector_tail(const vector_t REQ_PTR(v))
{
	if (v->fill != 0) {
		return (v->buf[v->fill - 1]);
	}
	return (NULL);
}

opt_size_t
vector_find(const vector_t REQ_PTR(v), const void *item)
{
	for (size_t i = 0; i < v->fill; i++) {
		if (v->buf[i] == item) {
			return (SOME(i));
		}
	}
	return (NONE(size_t));
}

void
vector_insert(vector_t REQ_PTR(v), void *elem, size_t index)
{
	VERIFY(elem != NULL);
	VERIFY3U(index, <=, v->fill);
	vector_grow(v, v->fill + 1);
	memmove(&v->buf[index + 1], &v->buf[index],
	    (v->fill - index) * sizeof (*v->buf));
	v->buf[index] = elem;
	v->fill++;
}

void
vector_insert_tail(vector_t REQ_PTR(v), void *elem)
{
	VERIFY(elem != NULL);
	vector_grow(v, v->fill + 1);
	v->buf[v->fill] = elem;
	v->fill++;
}

void *
vector_replace(vector_t REQ_PTR(v), void *new_elem, size_t index)
{
	VERIFY(new_elem != NULL);
	VERIFY3U(index, <, v->fill);
	void *old_elem = v->buf[index];
	ASSERT(old_elem != NULL);
	v->buf[index] = new_elem;
	return (old_elem);
}

void *
vector_remove(vector_t REQ_PTR(v), size_t index)
{
	VERIFY3U(index, <, v->fill);
	void *old = v->buf[index];
	memmove(&v->buf[index], &v->buf[index + 1],
	    (v->fill - index - 1) * sizeof (*v->buf));
	v->fill--;
	return (old);
}

void *
vector_remove_head(vector_t REQ_PTR(v))
{
	if (v->fill != 0) {
		return (vector_remove(v, 0));
	}
	return (NULL);
}

void *
vector_remove_tail(vector_t REQ_PTR(v))
{
	if (v->fill != 0) {
		void *old = v->buf[v->fill - 1];
		v->fill--;
		return (old);
	}
	return (NULL);
}

size_t
vector_shrink(vector_t REQ_PTR(v))
{
	if (v->fill == 0) {
		if (v->cap != 0) {
			free(v->buf);
			v->buf = NULL;
			v->cap = 0;
		}
	} else {
		size_t new_cap = P2ROUNDUP(v->fill);
		if (new_cap != v->cap) {
			v->cap = new_cap;
			v->buf = safe_realloc(v->buf,
			    v->cap * sizeof (*v->buf));
		}
	}
	return (v->cap);
}

size_t
vector_cap(const vector_t REQ_PTR(v))
{
	return (v->cap);
}

void
vector_move_all(vector_t REQ_PTR(src), vector_t REQ_PTR(dest))
{
	ASSERT0(dest->fill);
	free(dest->buf);
	dest->buf = src->buf;
	dest->cap = src->cap;
	dest->fill = src->fill;
	*src = (vector_t){};
}

typedef struct {
	int (*sort_func)(const void *a, const void *b);
	int (*sort_func_r)(const void *a, const void *b, void *userinfo);
	void *userinfo;
} sort_info_t;

static int
sort_cb(const void *a, const void *b, void *userinfo)
{
	const sort_info_t *info = userinfo;
	return (info->sort_func(*(void **)a, *(void **)b));
}

void
vector_sort(vector_t REQ_PTR(v), int (*sort_func)(const void *a, const void *b))
{
	ASSERT(sort_func != NULL);
	sort_info_t sort_info = { .sort_func = sort_func };
	lacf_qsort_r(v->buf, v->fill, sizeof (*v->buf), sort_cb, &sort_info);
}

static int
sort_cb_r(const void *a, const void *b, void *userinfo)
{
	const sort_info_t *info = userinfo;
	return (info->sort_func_r(*(void **)a, *(void **)b, info->userinfo));
}

void
vector_sort_r(vector_t REQ_PTR(v),
    int (*sort_func)(const void *a, const void *b, void *userinfo),
    void *userinfo)
{
	ASSERT(sort_func != NULL);
	sort_info_t sort_info = {
	    .sort_func_r = sort_func,
	    .userinfo = userinfo,
	};
	lacf_qsort_r(v->buf, v->fill, sizeof (*v->buf), sort_cb_r, &sort_info);
}
