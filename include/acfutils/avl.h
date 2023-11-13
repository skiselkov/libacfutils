/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_ACF_UTILS_AVL_H_
#define	_ACF_UTILS_AVL_H_

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include "types.h"

#define	_AVL_IMPL_INCLUDED_FROM_AVL_H
#include "avl_impl.h"

/**
 * \file
 *
 * This is a generic implemenatation of AVL trees for use in the Solaris kernel.
 * The interfaces provide an efficient way of implementing an ordered set of
 * data structures.
 *
 * AVL trees provide an alternative to using an ordered linked list. Using AVL
 * trees will usually be faster, however they requires more storage. An ordered
 * linked list in general requires 2 pointers in each data structure. The
 * AVL tree implementation uses 3 pointers. The following chart gives the
 * approximate performance of operations with the different approaches:
 *
 *	Operation	 Link List	AVL tree
 *	---------	 --------	--------
 *	lookup		   O(n)		O(log(n))
 *
 *	insert 1 node	 constant	constant
 *
 *	delete 1 node	 constant	between constant and O(log(n))
 *
 *	delete all nodes   O(n)		O(n)
 *
 *	visit the next
 *	or prev node	 constant	between constant and O(log(n))
 *
 *
 * The data structure nodes are anchored at an \ref avl_tree_t (the equivalent
 * of a list header) and the individual nodes will have a field of
 * type "avl_node_t" (corresponding to list pointers).
 *
 * The type \ref avl_index_t is used to indicate a position in the list for
 * certain calls.
 *
 * The usage scenario is generally:
 *
 * 1. Create the list/tree with: avl_create()
 *
 * followed by any mixture of:
 *
 * 2a. Insert nodes with: avl_add(), or avl_find() and avl_insert()
 *
 * 2b. Visited elements with:
 *	 avl_first() - returns the lowest valued node
 *	 avl_last() - returns the highest valued node
 *	 AVL_NEXT() - given a node go to next higher one
 *	 AVL_PREV() - given a node go to previous lower one
 *
 * 2c.  Find the node with the closest value either less than or greater
 *	than a given value with avl_nearest().
 *
 * 2d. Remove individual nodes from the list/tree with avl_remove().
 *
 * and finally when the list is being destroyed
 *
 * 3. Use avl_destroy_nodes() to quickly process/free up any remaining nodes.
 *    Note that once you use avl_destroy_nodes(), you can no longer
 *    use any routine except avl_destroy_nodes() and avl_destoy().
 *
 * 4. Use avl_destroy() to destroy the AVL tree itself.
 *
 * Any locking for multiple thread access is up to the user to provide, just
 * as is needed for any linked list implementation.
 */


/**
 * Type used for the root of the AVL tree.
 */
typedef struct avl_tree avl_tree_t;

/**
 * The data nodes in the AVL tree must have a field of this type.
 */
typedef struct avl_node avl_node_t;

/**
 * An opaque type used to locate a position in the tree where a node
 * would be inserted.
 */
typedef uintptr_t avl_index_t;


/**
 * Direction constants used for avl_nearest().
 */
#define	AVL_BEFORE	(0)
/**
 * Direction constants used for avl_nearest().
 */
#define	AVL_AFTER	(1)


/*
 * Prototypes
 *
 * Where not otherwise mentioned, "void *" arguments are a pointer to the
 * user data structure which must contain a field of type avl_node_t.
 *
 * Also assume the user data structures looks like:
 *	stuct my_type {
 *		...
 *		avl_node_t	my_link;
 *		...
 *	};
 */

/**
 * Initialize an AVL tree.
 *
 * @param tree the tree to be initialized
 * @param compar function to compare two nodes, it must return exactly:
 * -1, 0, or +1. -1 for <, 0 for ==, and +1 for >
 * @param size the value of sizeof(struct my_type)
 * @param offset the value of OFFSETOF(struct my_type, my_link)
 */
API_EXPORT extern void avl_create(avl_tree_t *tree,
	int (*compar) (const void *, const void *), size_t size, size_t offset);


/**
 * Find a node with a matching value in the tree.
 *
 * @param node node that has the value being looked for
 * @param where position for use with avl_nearest() or avl_insert(), may be NULL
 *
 * @return The matching node found. If not found, it returns NULL and
 * then if "where" is not NULL it sets "where" for use with avl_insert()
 * or avl_nearest().
 */
API_EXPORT extern void *avl_find(const avl_tree_t *tree, const void *node,
    avl_index_t *where);

/**
 * Insert a node into the tree.
 *
 * @param node the node to insert
 * @param where position as returned from avl_find()
 */
API_EXPORT extern void avl_insert(avl_tree_t *tree, void *node,
    avl_index_t where);

/**
 * Insert "new_data" in "tree" in the given "direction" either after
 * or before the data "here".
 *
 * This might be useful for avl clients caching recently accessed
 * data to avoid doing avl_find() again for insertion.
 *
 * @param new_data new data to insert
 * @param here existing node in "tree"
 * @param direction either AVL_AFTER or AVL_BEFORE the data "here".
 */
API_EXPORT extern void avl_insert_here(avl_tree_t *tree, void *new_data,
    void *here, int direction);


/**
 * @return The first valued node in the tree. Will return NULL
 * if the tree is empty.
 */
API_EXPORT extern void *avl_first(const avl_tree_t *tree);
/**
 * @return The last valued node in the tree. Will return NULL if the
 * tree is empty.
 */
API_EXPORT extern void *avl_last(const avl_tree_t *tree);

/**
 * @return the next or previous valued node in the tree. Will return NULL
 * if at the last node.
 * @param node the node from which the next or previous node is found
 */
#define	AVL_NEXT(tree, node)	avl_walk(tree, node, AVL_AFTER)
/**
 * @return the previous valued node in the tree. Will return NULL if at
 * the first node.
 * @param node the node from which the next or previous node is found
 */
#define	AVL_PREV(tree, node)	avl_walk(tree, node, AVL_BEFORE)

/**
 * Find the node with the nearest value either greater or less than
 * the value from a previous avl_find().
 *
 * @param where position as returned from avl_find()
 * @param direction either AVL_BEFORE or AVL_AFTER
 * @return the node or NULL if there isn't a matching one.
 *
 * Example: get the greatest node that is less than a given value:
 *```
 *	avl_tree_t *tree;
 *	struct my_data look_for_value = {....};
 *	struct my_data *node;
 *	struct my_data *less;
 *	avl_index_t where;
 *
 *	node = avl_find(tree, &look_for_value, &where);
 *	if (node != NULL)
 *		less = AVL_PREV(tree, node);
 *	else
 *		less = avl_nearest(tree, where, AVL_BEFORE);
 *```
 */
API_EXPORT extern void *avl_nearest(const avl_tree_t *tree, avl_index_t where,
    int direction);


/**
 * Add a single node to the tree.
 *
 * The node must not be in the tree, and it must not
 * compare equal to any other node already in the tree.
 *
 * @param node the node to add
 */
API_EXPORT extern void avl_add(avl_tree_t *tree, void *node);


/**
 * Remove a single node from the tree.  The node must be in the tree.
 *
 * @param node the node to remove
 */
API_EXPORT extern void avl_remove(avl_tree_t *tree, void *node);

/**
 * Reinsert a node only if its order has changed relative to its nearest
 * neighbors. To optimize performance avl_update_lt() checks only the previous
 * node and avl_update_gt() checks only the next node. Use avl_update_lt() and
 * avl_update_gt() only if you know the direction in which the order of the
 * node may change.
 */
API_EXPORT extern bool_t avl_update(avl_tree_t *, void *);
/**
 * @see avl_update()
 */
API_EXPORT extern bool_t avl_update_lt(avl_tree_t *, void *);
/**
 * @see avl_update()
 */
API_EXPORT extern bool_t avl_update_gt(avl_tree_t *, void *);

/**
 * @return the number of nodes in the tree
 */
API_EXPORT extern unsigned long avl_numnodes(const avl_tree_t *tree);

/**
 * @return B_TRUE if there are zero nodes in the tree, B_FALSE otherwise.
 */
API_EXPORT extern bool_t avl_is_empty(avl_tree_t *tree);

/**
 * Used to destroy any remaining nodes in a tree. The cookie argument should
 * be initialized to NULL before the first call. Returns a node that has been
 * removed from the tree and may be free()'d. Returns NULL when the tree is
 * empty.
 *
 * Once you call avl_destroy_nodes(), you can only continuing calling it and
 * finally avl_destroy(). No other AVL routines will be valid.
 *
 * @param cookie a "void *" used to save state between calls to
 *	avl_destroy_nodes()
 *
 * Example:
 *```
 *	avl_tree_t *tree;
 *	struct my_data *node;
 *	void *cookie;
 *
 *	cookie = NULL;
 *	while ((node = avl_destroy_nodes(tree, &cookie)) != NULL)
 *		free(node);
 *	avl_destroy(tree);
 *```
 */
API_EXPORT extern void *avl_destroy_nodes(avl_tree_t *tree, void **cookie);


/**
 * Final destroy of an AVL tree.
 *
 * @param tree the empty tree to destroy
 */
API_EXPORT extern void avl_destroy(avl_tree_t *tree);



#ifdef	__cplusplus
}
#endif

#endif	/* _ACF_UTILS_AVL_H_ */
