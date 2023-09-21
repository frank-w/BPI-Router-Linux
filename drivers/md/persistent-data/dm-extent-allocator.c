// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */
#include "dm-extent-allocator.h"

/*
 * 'extents' are just ranges of blocks that we'd like to allocate from.  These
 * extents are held as the leaf nodes of a BSP tree.  An 'allocation context' points
 * to a particular leaf/extent in the tree.  Multiple allocation contexts can point
 * to the same leaf.
 *
 * If a leaf is exhausted, we update all allocation contexts that point to it and delete
 * it from the tree.
 */

struct ea_node;

struct ea_internal {
	/*
	 * An approximation to the number of free blocks below this internal node.
	 * Used to guide the walk to a new leaf.
	 */
	uint64_t nr_free;

	/* child nodes */
	struct ea_node *left;
	struct ea_node *right;
};

struct ea_leaf {
	/* contains allocation contexts */
	struct list_head holders;
};

struct ea_node {
	/*
	 * We sometimes walk up the tree from a leaf to a root, it's useful
	 * to know if we were the left or right child of the parent.
	 */
	bool is_left_child;

	/* discriminant for the union */
	bool is_leaf;

	/* nr of allocation contexts using leaves below this node */
	uint32_t nr_holders;

	/* range of blocks covered by this node */
	uint64_t begin;
	uint64_t end;

	struct ea_node *parent;

	union {
		struct ea_internal internal;
		struct ea_leaf leaf;
	} u;
};

/*
 * We preallocate this many nodes in case we can't add any when setting up
 * allocation contexts.
 */
#define INITIAL_NR_NODES 32

/*
 * Leaf nodes with fewer than this many free blocks cannot be split.
 */
#define SPLIT_THRESHOLD 16

struct dm_extent_allocator {
	spinlock_t lock;

	unsigned nr_preallocated_nodes;
	unsigned nr_free_nodes;
	struct list_head free_nodes;

	unsigned nr_allocation_contexts;
	struct list_head allocation_contexts;

	uint64_t nr_blocks;
	struct ea_node *root;
};

/**
 * __free_node - Frees a node in the extent allocator.
 * @ea: Pointer to the extent allocator.
 * @node: Node to free.
 */
static inline void __free_node(struct dm_extent_allocator *ea, struct ea_node *n)
{
	struct list_head *l = (struct list_head *)n;

	list_add(l, &ea->free_nodes);
	ea->nr_free_nodes++;
}

/**
 * __alloc_node - Allocates a node from the extent allocator.
 * @ea: Pointer to the extent allocator.
 */
static inline struct ea_node *__alloc_node(struct dm_extent_allocator *ea)
{
	struct list_head *l;

	l = ea->free_nodes.next;
	list_del(l);
	ea->nr_free_nodes--;

	return (struct ea_node *) l;
}

/**
 * @ea: Pointer to the extent allocator.
 * @node: Index of the node to query.
 *
 * Returns: The number of free blocks in the node.
 */
static inline uint64_t __nr_free_blocks(struct ea_node *n)
{
	if (!n)
		return 0;

	if (n->is_leaf)
		return n->end - n->begin;
	else
		return n->u.internal.nr_free;
}

/**
 * __free_tree - Frees all nodes in a tree.
 * @ea: Pointer to the extent allocator.
 * @n: Pointer to the root node of the tree to free.
 */
static void __free_tree(struct dm_extent_allocator *ea, struct ea_node *n)
{
	if (!n)
		return;

	if (n->is_leaf)
		__free_node(ea, n);
	else {
		__free_tree(ea, n->u.internal.left);
		__free_tree(ea, n->u.internal.right);
	}
}

/**
 * __setup_initial_root - Sets up the initial root node for the extent allocator.
 * @ea: Pointer to the extent allocator.
 *
 * The root node is a leaf node that spans the entire device.
 */
static void __setup_initial_root(struct dm_extent_allocator *ea)
{
	struct ea_node *n;
	struct ea_leaf *l;

	n = ea->root = __alloc_node(ea);
	n->is_left_child = true;
	n->is_leaf = true;
	n->nr_holders = 0;
	n->begin = 0;
	n->end = ea->nr_blocks;
	n->parent = NULL;

	l = &n->u.leaf;
	INIT_LIST_HEAD(&l->holders);
}

/**
 * free_node_list - Frees a list of nodes.
 * @l: Pointer to the list head of the nodes to free.
 */
static void free_node_list(struct list_head *l)
{
	struct list_head *e, *tmp;

	list_for_each_safe(e, tmp, l) {
		list_del(e);
		kfree(e);
	}
}

/**
 * alloc_node_list - Allocates a list of nodes.
 * @nr: Number of nodes to allocate.
 * @flags: Flags to pass to kmalloc.
 * @result: Pointer to the list head to store the allocated nodes.
 *
 * Used to initialise the free list of nodes.
 * Returns: 0 on success, or -ENOMEM if allocation failed.
 */
static int alloc_node_list(unsigned nr, int flags, struct list_head *result)
{
	int i;

	INIT_LIST_HEAD(result);

	for (i = 0; i < nr; i++) {
		struct ea_node *n = kmalloc(sizeof(*n), flags);
		struct list_head *l = (struct list_head *) n;
		if (!n) {
			free_node_list(result);
			return -ENOMEM;
		}

		list_add(l, result);
	}

	return 0;
}

/**
 * __prealloc_nodes - Preallocates nodes for allocation contexts.
 * @ea: Pointer to the extent allocator.
 * @nr: Number of nodes to preallocate.
 */
static void __prealloc_nodes(struct dm_extent_allocator *ea, unsigned nr, int flags)
{
	int r;
	struct list_head new_nodes;

	r = alloc_node_list(nr, flags, &new_nodes);
	if (!r) {
		struct list_head *e, *tmp;
		list_for_each_safe(e, tmp, &new_nodes) {
			list_del(e);
			__free_node(ea, (struct ea_node *)e);
		}
		ea->nr_preallocated_nodes += nr;
	}
}

struct dm_extent_allocator *dm_extent_allocator_create(uint64_t nr_blocks)
{
	struct dm_extent_allocator *ea = kmalloc(sizeof(*ea), GFP_KERNEL);

	if (!ea)
		return NULL;

	spin_lock_init(&ea->lock);
	ea->nr_blocks = nr_blocks;
	ea->nr_preallocated_nodes = 0;
	ea->nr_free_nodes = 0;
	ea->nr_allocation_contexts = 0;

	INIT_LIST_HEAD(&ea->free_nodes);
	__prealloc_nodes(ea, INITIAL_NR_NODES, GFP_KERNEL);
	INIT_LIST_HEAD(&ea->allocation_contexts);
	__setup_initial_root(ea);

	return ea;
}
EXPORT_SYMBOL_GPL(dm_extent_allocator_create);

void dm_extent_allocator_destroy(struct dm_extent_allocator *ea)
{
	__free_tree(ea, ea->root);
	free_node_list(&ea->free_nodes);
	kfree(ea);
}
EXPORT_SYMBOL_GPL(dm_extent_allocator_destroy);

/**
 * __reset - Resets the extent allocator.
 * @ea: Pointer to the extent allocator.
 * @nr_blocks: New number of blocks in the device in case of resize.
 *
 * Frees all nodes in the tree and sets up a new root node that spans the entire device.
 */
static void __reset(struct dm_extent_allocator *ea, uint64_t nr_blocks)
{
	struct dm_extent_alloc_context *ac, *tmp;

	list_for_each_entry_safe(ac, tmp, &ea->allocation_contexts, list) {
		if (ac->leaf)
			list_del(&ac->holders_list);
		ac->leaf = NULL;
	}

	__free_tree(ea, ea->root);

	ea->nr_blocks = nr_blocks;
	__setup_initial_root(ea);
}

void dm_extent_allocator_reset(struct dm_extent_allocator *ea)
{
	spin_lock(&ea->lock);
	__reset(ea, ea->nr_blocks);
	spin_unlock(&ea->lock);
}
EXPORT_SYMBOL_GPL(dm_extent_allocator_reset);

void dm_extent_allocator_resize(struct dm_extent_allocator *ea,
				uint64_t nr_blocks)
{
	spin_lock(&ea->lock);
	__reset(ea, nr_blocks);
	spin_unlock(&ea->lock);
}
EXPORT_SYMBOL_GPL(dm_extent_allocator_resize);

/**
 * __split_leaf - Splits a leaf node in the extent allocator.
 * @ea: Pointer to the extent allocator.
 * @node: leaf node to split.
 *
 * The split point is chosen to be the midpoint of the leaf's range. The new leaf
 * node is added as the right child of a new internal node, which is added as the
 * parent of the original leaf node. The function returns the index of the new
 * internal node on success, or NULL on failure.
 */
static struct ea_node *__split_leaf(struct dm_extent_allocator *ea, struct ea_node *n)
{
	uint64_t mid;
	struct ea_node *new_parent, *right_child;

	if (ea->nr_free_nodes < 2)
		return NULL;

	if (n->end - n->begin <= SPLIT_THRESHOLD)
		return NULL;

	new_parent = __alloc_node(ea);
	right_child = __alloc_node(ea);

	mid = n->begin + (n->end - n->begin) / 2;

	new_parent->is_left_child = false;
	right_child->is_leaf = true;
	right_child->nr_holders = 0;
	right_child->begin = mid;
	right_child->end = n->end;
	right_child->parent = new_parent;
	INIT_LIST_HEAD(&right_child->u.leaf.holders);

	new_parent->is_left_child = n->is_left_child;
	new_parent->is_leaf = false;
	new_parent->nr_holders = n->nr_holders + 1;
	new_parent->begin = n->begin;
	new_parent->end = n->end;
	new_parent->parent = n->parent;
	new_parent->u.internal.nr_free = n->end - n->begin;
	new_parent->u.internal.left = n;
	new_parent->u.internal.right = right_child;

	/* the original leaf becomes the left child */
	n->is_left_child = true;
	n->end = mid;
	n->parent = new_parent;

	return new_parent;
}

/**
 * __select_child - Selects the best child node to allocate from in the extent allocator.
 * @ea: Pointer to the extent allocator.
 * @left: left child node.
 * @right: right child node.
 *
 * The best child is the one with the highest ratio of free blocks to holders. If the
 * ratios are equal, the left child is preferred.
 */
static struct ea_node **__select_child(struct dm_extent_allocator *ea,
				       struct ea_node **left, struct ea_node **right)
{
	uint64_t left_free = __nr_free_blocks(*left);
	uint64_t right_free = __nr_free_blocks(*right);
	uint64_t left_score = do_div(left_free, (*left)->nr_holders + 1);
	uint64_t right_score = do_div(right_free, (*right)->nr_holders + 1);

	if (left_score >= right_score)
		return left;
	else
		return right;
}

/**
 * __get_leaf - Gets a leaf node from the extent allocator.
 * @ea: Pointer to the extent allocator.
 *
 * The function walks the tree from the root to a leaf, selecting the best child node at each
 * step. If the selected child node is already in use, the function will attempt to split the
 * node with __split_leaf.  If splitting fails the leaf node will be shared.
 *
 * Returns: A pointer to the leaf node on success, or NULL if there are no more free blocks.
 */
static struct ea_node *__get_leaf(struct dm_extent_allocator *ea)
{
	struct ea_node **ptr = &ea->root;
	struct ea_node *n = *ptr;

	if (!ea->root)
		return NULL;

	/* walk the tree until we get to a leaf */
	while (!n->is_leaf) {
		struct ea_internal *i = &n->u.internal;
		n->nr_holders++;
		ptr = __select_child(ea, &i->left, &i->right);
		n = *ptr;
	}

	if (n->nr_holders > 0) {
		/*
		* Someone is already using this extent.  See if we can split it.
		*/
		struct ea_node *split = __split_leaf(ea, n);
		if (split) {
			/* patch up the parent */
			*ptr = split;

			/* the new leaf is the right child */
			n = split->u.internal.right;
		}
	}

	n->nr_holders++;
	return n;
}

/**
 * __put_leaf - Releases a leaf node and updates the extent allocator's tree.
 * @ea: Pointer to the extent allocator.
 * @n: Pointer to the leaf node to release.
 * @delta: Number of holders to release.
 */
static void __put_leaf(struct dm_extent_allocator *ea, struct ea_node *n, uint32_t delta)
{
	bool empty;
	struct ea_node *parent, *grand_parent;

	if (!n)
		return;

	parent = n->parent;

	/* adjust leaf */
	n->nr_holders -= delta;

	/* see if the leaf is now empty */
	empty = n->begin == n->end;
	if (empty && !n->nr_holders) {
		bool n_is_left = n->is_left_child;
		__free_node(ea, n);

		if (parent) {
			bool is_left = parent->is_left_child;

			/*
			* We also free the parent, since every internal node
			* must have two children.
			*/
			grand_parent = parent->parent;

			/* replace the parent with the other child */
			if (n_is_left)
				n = parent->u.internal.right;
			else
				n = parent->u.internal.left;

			__free_node(ea, parent);
			parent = grand_parent;
			n->parent = parent;

			if (parent) {
				/* patch up the parent */
				if (is_left) {
					n->is_left_child = true;
					parent->u.internal.left = n;
				} else {
					n->is_left_child = false;
					parent->u.internal.right = n;
				}
			}
		} else
			n = NULL;
	}

	/* walk up the tree adjusting the counts */
	while (parent) {
		parent->nr_holders -= delta;
		parent->u.internal.nr_free =
			__nr_free_blocks(parent->u.internal.left) +
			__nr_free_blocks(parent->u.internal.right);

		n = parent;
		parent = n->parent;
	}

	ea->root = n;
}

/**
 * __next_leaf - Gets the next available leaf node for an allocation context.
 * @ac: Pointer to the allocation context.
 *
 * Returns: 0 on success, or -ENOSPC if there are no more free blocks.
 */
static int __next_leaf(struct dm_extent_alloc_context *ac)
{
	struct ea_node *l;
	struct dm_extent_allocator *ea = ac->ea;

	ac->leaf = __get_leaf(ea);
	if (!ac->leaf)
		return -ENOSPC;

	l = ac->leaf;
	list_add(&ac->holders_list, &l->u.leaf.holders);
	return 0;
}

void dm_ea_context_get(struct dm_extent_allocator *ea,
		       struct dm_extent_alloc_context *ac)
{
	spin_lock(&ea->lock);
	ac->ea = ea;

	ea->nr_allocation_contexts++;

	/*
	 * We try and maintain a couple of nodes per alloc context to avoid sharing.
	 * If allocation fails it's no big deal; we'll just get more fragmentation.
	 */
	if (ea->nr_preallocated_nodes < ea->nr_allocation_contexts * 2)
		__prealloc_nodes(ea, 2, GFP_NOIO);

	list_add(&ac->list, &ea->allocation_contexts);
	INIT_LIST_HEAD(&ac->holders_list);
	ac->leaf = NULL;
	spin_unlock(&ea->lock);
}
EXPORT_SYMBOL_GPL(dm_ea_context_get);

void dm_ea_context_put(struct dm_extent_alloc_context *ac)
{
	struct dm_extent_allocator *ea = ac->ea;

	spin_lock(&ea->lock);
	if (ac->leaf)
		list_del(&ac->holders_list);
	list_del(&ac->list);
	__put_leaf(ea, ac->leaf, 1);
	ea->nr_allocation_contexts--;
	spin_unlock(&ea->lock);
}
EXPORT_SYMBOL_GPL(dm_ea_context_put);

/**
 * __reset_contexts - Resets all allocation contexts that are currently using a leaf node.
 * @ac: Pointer to the allocation context.
 */
static void __reset_contexts(struct dm_extent_alloc_context *ac)
{
	struct ea_node *n = ac->leaf;
	struct ea_leaf *l = &n->u.leaf;
	struct dm_extent_alloc_context *ac_it, *tmp;

	list_for_each_entry_safe(ac_it, tmp, &l->holders, holders_list) {
		ac_it->leaf = NULL;
		list_del(&ac_it->holders_list);
	}
}

/**
 * __reset_and_release - Resets all allocation contexts that are currently using a leaf node
 *                       then releases the leaf node.
 * @ac: Pointer to the allocation context.
 */
static void __reset_and_release(struct dm_extent_alloc_context *ac)
{
	struct ea_node *old_leaf = ac->leaf;

	__reset_contexts(ac); /* this clobbers ac->leaf */
	__put_leaf(ac->ea, old_leaf, old_leaf->nr_holders);
}


static int __alloc(struct dm_extent_alloc_context *ac, dm_alloc_extent_fn fn,
		   void *context, uint64_t *result)
{
	int r = 0;
	struct ea_node *n;

	while (true) {
		/* do we have a leaf? */
		if (!ac->leaf) {
			r = __next_leaf(ac);
			if (r)
				return r;
		}

		n = ac->leaf;

		/* does the leaf have space? */
		if (n->begin == n->end) {
			__reset_and_release(ac);
			continue;
		}

		/* call down to the underlying allocator */
		r = fn(context, n->begin, n->end, result);
		if (r == -ENOSPC) {
			n->begin = n->end;
			__reset_and_release(ac);
			continue;
		}

		if (!r) {
			/* success */
			n->begin = *result + 1;

			if (n->begin == n->end)
				__reset_and_release(ac);
		}

		return r;
	}
}

int dm_ea_context_alloc(struct dm_extent_alloc_context *ac, dm_alloc_extent_fn fn,
			void *context, uint64_t *result)
{
	int r;
	struct dm_extent_allocator *ea = ac->ea;

	spin_lock(&ea->lock);
	r = __alloc(ac, fn, context, result);
	spin_unlock(&ea->lock);

	return r;
}
EXPORT_SYMBOL_GPL(dm_ea_context_alloc);
