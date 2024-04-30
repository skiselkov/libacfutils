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
 * Copyright 2024 Saso Kiselkov. All rights reserved.
 */
/**
 * \file
 * This file implements a general purpose C++-like vector for C.
 * The vector is intended as a general-purpose holder of references
 * to other data. It is NOT intended to hold the data itself, because
 * the vector employs reallocation of a contiguous region to hold its
 * buffer, which can cause stuff to move around in memory. This would
 * cause any contained list_t and avl_tree_t fields to break.
 *
 * The vector employs automatic growth management using the plain old
 * strategy of doubling its capacity when the previous capacity has
 * been exceeded. So if the vector currently contains 4 elements, its
 * capacity is 4, and attempting to insert a 5th element doubles its
 * capacity to 8 elements. Please note that this capacity management
 * strategy is mostly invisible to you and the vector takes care of
 * nearly everything by itself. You can, however, inspect the vector's
 * current capacity and instruct it to shrink, if you feel the need
 * to reclaim space.
 *
 * @see vector_init()
 * @see vector_fini()
 * @see vector_insert()
 * @see vector_remove()
 */

#ifndef	_ACF_UTILS_VECTOR_H_
#define	_ACF_UTILS_VECTOR_H_

#include <acfutils/optional.h>
#include <acfutils/sysmacros.h>

#include "vector_impl.h"

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * \brief Initializes a new `vector_t` to a blank state, with zero
 * starting capacity.
 *
 * The vector automatically grows as you insert new elements.
 *
 * @note You MUST dispose of a created vector by calling vector_destroy().
 */
void vector_create(vector_t REQ_PTR(v));
/**
 * \brief Initializes a new `vector_t` with a capacity hint.
 *
 * You can use this to hint the vector ahead of time, if the number
 * of elements which will be inserted into the vector is known.
 * This can help prevent large numbers of reallocations, as the
 * vector grows in response to insertions.
 *
 * @note You MUST dispose of a created vector by calling vector_destroy().
 */
void vector_create_cap(vector_t REQ_PTR(v), size_t cap_hint);
/**
 * \brief Destroys a vector previously created using vector_create().
 *
 * You MUST call this function to properly dispose of a vector and
 * its internal buffers. You must first make sure the vector is empty
 * of all its contents before attempting to destroy it.
 */
void vector_destroy(vector_t REQ_PTR(v));
/**
 * \brief Returns the current number of elements contained inside the vector.
 *
 * This is NOT the vector's capacity. It is the number of items you have
 * currently inserted into the vector. Thus, this represents the highest
 * index you can use in vector_insert() to add new elements, and it is +1
 * the highest index you can retrieve using vector_get().
 */
size_t vector_len(const vector_t REQ_PTR(v));
/**
 * \brief Retrieves elements within the vector.
 *
 * Retrieves the contents of the vector at `index`. You must NOT attempt
 * to access an index which is beyond the vector's current length. So
 * `index` must ALWAYS be less than the value returned by vector_len().
 */
void *vector_get(const vector_t REQ_PTR(v), size_t index);
/**
 * \brief Retrieves the element at the head of the vector, if any.
 *
 * Retrieves the first element in the vector, if one is present,
 * without removing it from the vector. If vector is empty, returns
 * NULL instead.
 */
void *vector_head(const vector_t REQ_PTR(v));
/**
 * \brief Retrieves the element at the tail of the vector, if any.
 *
 * Retrieves the last element in the vector, if one is present,
 * without removing it from the vector. If vector is empty, returns
 * NULL instead.
 */
void *vector_tail(const vector_t REQ_PTR(v));
/**
 * \brief Locates an element in the vector by value.
 *
 * Attempts to locate an element by pointer value. If the element was
 * found, its index is returned, wrapped in a SOME() type. Otherwise,
 * NONE is returned.
 */
opt_size_t vector_find(const vector_t REQ_PTR(v), const void *item);
/**
 * \brief Inserts a new element into the vector at a given index.
 *
 * Any elements inside of the vector after `index` will be pushed
 * back by 1 index. The `index` argument MUST be less-than-or-equal
 * to vector_len(). You can also use the more concise
 * vector_insert_tail() function to insert elements at the tail of
 * the vector, rather than the slightly more verbose
 * `vector_insert(v, new_elem, vector_len())`.
 */
void vector_insert(vector_t REQ_PTR(v), void *elem, size_t index);
/**
 * \brief Inserts a new element at the tail of the vector.
 *
 * This is equivalent to calling `vector_insert(v, new_elem, vector_len())`,
 * but is a bit more concise and neater.
 */
void vector_insert_tail(vector_t REQ_PTR(v), void *elem);
/**
 * \brief Replaces an element in the vector in-place.
 *
 * This allows you to substitute elements in the vector without first
 * inserting the new and then removing the old value, and thus avoiding
 * any resizing of the vector. The `index` argument MUST be less than
 * the return value of vector_len().
 *
 * This function returns the previous element contained at `index`.
 */
void *vector_replace(vector_t REQ_PTR(v), void *new_elem, size_t index);
/**
 * \brief Removes an element from the vector at a given index.
 *
 * This causes any elements behind the removed value to shift forward
 * by one index. The previously contained element at `index` is
 * returned from this function. The `index` argument MUST be less than
 * the return value of vector_len().
 *
 * @note The vector internal capacity doesn't automatically shrink after
 *	removing elements. If you want to force the vector to shrink its
 *	buffer, use vector_shrink().
 */
void *vector_remove(vector_t REQ_PTR(v), size_t index);
/**
 * \brief Removes and returns the first element in a vector, if any.
 *
 * This behaves almost the same was as vector_remove() with a zero index
 * argument, except that calling vector_remove_head() on an empty vector
 * is a valid operation. The element previously contained at index 0 is
 * returned, or if the vector was empty, returns NULL. Any elements
 * following the first element are shifted forward by one index.
 *
 * @note The most efficient way to remove all elements from a vector
 *	is to employ a loop of vector_remove_tail() instead of
 *	vector_remove_head(), because the tail-removing version
 *	avoids any calls to `memmove()` to shift elements around.
 *	Simply call vector_remove_tail() until the function returns
 *	NULL.
 */
void *vector_remove_head(vector_t REQ_PTR(v));
/**
 * \brief Removes and returns the last element in a vector, if any.
 *
 * This behaves almost the same was as vector_remove() with a
 * `vector_len()-1` index argument, except that calling
 * vector_remove_tail() on an empty vector is a valid operation.
 * The element previously contained at the end of the vector is
 * returned, or if the vector was empty, returns NULL.
 *
 * @note The most efficient way to remove all elements from a vector
 *	is to employ a loop of vector_remove_tail() instead of
 *	vector_remove_head(), because the tail-removing version
 *	avoids any calls to `memmove()` to shift elements around.
 *	Simply call vector_remove_tail() until the function returns
 *	NULL.
 */
void *vector_remove_tail(vector_t REQ_PTR(v));
/**
 * \brief Orders the vector to shrink to the nearest power-of-2
 * capacity suitable to hold its current contents.
 *
 * You can use this function to force a vector to release excess
 * memory after removing a large number of elements from it. It
 * is generally NOT required to call this removing items from the
 * vector. You should only attempt to shrink vectors if you have
 * determined that doing so will provide significant memory
 * savings by reducing the vector's memory footprint.
 *
 * Returns the new capacity of the vector after the shrink.
 */
size_t vector_shrink(vector_t REQ_PTR(v));
/**
 * \brief Returns the current element capacity of the vector.
 *
 * The vector automatically grows by doubling capacity whenever
 * it needs to  add more elements than current has room to hold.
 * You should generally only be manually calling vector_cap() and
 * vector_shrink() if you have removed a large number of elements
 * and have determined that the small amount of memory savings
 * are worth the reallocation.
 */
size_t vector_cap(const vector_t REQ_PTR(v));
/**
 * \brief Moves all elements from the `src` vector to the `dest` vector.
 *
 * The `dest` vector MUST NOT contain any elements. The `src`
 * vector is emptied after this operation and reinitialized to
 * zero capacity. In essence, the `dest` vector "takes over"
 * all of the elements from `src`.
 */
void vector_move_all(vector_t REQ_PTR(src), vector_t REQ_PTR(dest));
/**
 * \brief Sorts a vector using the and a sorting predicate.
 *
 * The `sort_func` function will receive the values of elements
 * in the vector for comparison purposes, and return one of the
 * following 3 values:
 * - `<0` if `a > b` (elements ordered descending)
 * - `0` if `a = b`
 * - `>0` if `a < b` (elements ordered ascending)
 * For example, the return value of the `strcmp()` function follows
 * this same pattern, and if used for values in vector_t, would
 * result in alphabetically sorting the vector's contents.
 */
void vector_sort(vector_t REQ_PTR(v),
    int (*sort_func)(const void *a, const void *b));
/**
 * \brief Sorts a vector using the and a thread-safe sorting predicate.
 *
 * This is similar to vector_sort(), but this function and the sorting
 * predicate take an additional `userinfo` argument, which can be any
 * arbitrary data you want. You can use this to construct a thread-safe
 * sorting predicate, or simply to pass additional information to the
 * sorting predicate.
 *
 * @note This does not imply any kind of thread-safety of vector_t
 *	itself. It is generally NOT safe to access a vector_t from
 *	multiple threads, unless external locking is employed.
 */
void vector_sort_r(vector_t REQ_PTR(v),
    int (*sort_func)(const void *a, const void *b, void *userinfo),
    void *userinfo);

#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_VECTOR_H_ */
