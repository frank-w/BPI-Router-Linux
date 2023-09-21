/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#ifndef _LINUX_DM_EXTENT_ALLOCATOR_H
#define _LINUX_DM_EXTENT_ALLOCATOR_H

#include "dm-block-manager.h"

/*
 * This extent allocator is used to supervise allocations of data blocks for
 * thin provisioning.  It enhances data locality and reduces fragmentation by
 * allocating contiguous extents of blocks.
 *
 * An extent allocator instance is created with a fixed number of blocks. An
 * 'allocation context' abstraction is provided to manage contiguous allocations.
 * Each allocation context aims to allocate as few linear ranges of blocks as
 * possible, keeping different contexts isolated from each other.
 *
 * There are three categories of operations supported:
 * 1. Allocator-wide operations: create, destroy and reset.
 * 2. Context-related operations: getting and putting allocation contexts.
 * 3. Block allocation within a context: allocation of a new block.
 *
 * All methods provided in this interface except create/destroy are thread-safe.
 */

struct dm_extent_allocator;

// Treat this structure as opaque
struct dm_extent_alloc_context {
	struct dm_extent_allocator *ea;

	struct list_head list;
	struct list_head holders_list;
	void *leaf;
};

/**
 * dm_extent_allocator_create - Creates a new extent allocator.
 * @nr_blocks: Number of blocks in the device.
 *
 * The allocator is initialized with a root node that spans the entire device and
 * has no holders. The function returns a pointer to the new extent allocator on
 * success, or NULL on failure.
 */
struct dm_extent_allocator *dm_extent_allocator_create(uint64_t nr_blocks);

/**
 * dm_extent_allocator_destroy - Destroys an extent allocator.
 * @ea: Pointer to the extent allocator.
 *
 * Assumes that there are no active allocation contexts.
 */
void dm_extent_allocator_destroy(struct dm_extent_allocator *ea);

/**
 * dm_extent_allocator_reset - Resets an extent allocator to its initial state.
 * @ea: Pointer to the extent allocator.
 *
 * Resets an extent allocator to its initial state by freeing all of its nodes
 * and resetting allocation contexts, it then sets up a new root node that spans the
 * entire extent and has no holders.
 */
void dm_extent_allocator_reset(struct dm_extent_allocator *ea);

/**
 * dm_extent_allocator_resize - Resizes an extent allocator to a new size.
 * @ea: Pointer to the extent allocator.
 * @nr_blocks: New number of blocks in the device.
 */
void dm_extent_allocator_resize(struct dm_extent_allocator *ea, uint64_t nr_blocks);

/**
 * dm_ea_context_get - Gets a new allocation context for the extent allocator.
 * @ea: Pointer to the extent allocator.
 * @ac: Pointer to the allocation context to initialize.
 */
void dm_ea_context_get(struct dm_extent_allocator *ea, struct dm_extent_alloc_context *ac);

/**
 * dm_ea_context_put - Releases an allocation context for the extent allocator.
 * @ac: Pointer to the allocation context to release.
 */
void dm_ea_context_put(struct dm_extent_alloc_context *ac);

/**
 * dm_ea_context_alloc - Allocates a new block in the extent allocator.
 * @ac: Pointer to the allocation context.
 * @result: Pointer to a variable to store the allocated block number.
 *
 * A callback is used for the fine grain allocation decision (eg, using a space map).
 * The function returns 0 on success, or -ENOSPC if there are no more free blocks.
 * The allocated block number is stored in the variable pointed to by @result.
 */
typedef int (*dm_alloc_extent_fn)(void *context, uint64_t b, uint64_t e,
				  uint64_t *result);
int dm_ea_context_alloc(struct dm_extent_alloc_context *ac, dm_alloc_extent_fn fn,
			void *context, uint64_t *result);

#endif
