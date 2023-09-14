// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Western Digital Corporation or its affiliates.
 */

#include <linux/btrfs_tree.h>
#include "ctree.h"
#include "fs.h"
#include "accessors.h"
#include "transaction.h"
#include "disk-io.h"
#include "raid-stripe-tree.h"
#include "volumes.h"
#include "misc.h"
#include "print-tree.h"

int btrfs_delete_raid_extent(struct btrfs_trans_handle *trans, u64 start, u64 length)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *stripe_root = fs_info->stripe_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	u64 found_start;
	u64 found_end;
	u64 end = start + length;
	int slot;
	int ret;

	if (!stripe_root)
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while (1) {
		key.objectid = start;
		key.type = BTRFS_RAID_STRIPE_KEY;
		key.offset = length;

		ret = btrfs_search_slot(trans, stripe_root, &key, path, -1, 1);
		if (ret < 0)
			break;
		if (ret > 0) {
			ret = 0;
			if (path->slots[0] == 0)
				break;
			path->slots[0]--;
		}

		leaf = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(leaf, &key, slot);
		found_start = key.objectid;
		found_end = found_start + key.offset;

		/* That stripe ends before we start, we're done. */
		if (found_end <= start)
			break;

		ASSERT(found_start >= start && found_end <= end);
		ret = btrfs_del_item(trans, stripe_root, path);
		if (ret)
			break;

		btrfs_release_path(path);
	}

	btrfs_free_path(path);
	return ret;
}

static int btrfs_insert_one_raid_extent(struct btrfs_trans_handle *trans,
					int num_stripes,
					struct btrfs_io_context *bioc)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_key stripe_key;
	struct btrfs_root *stripe_root = fs_info->stripe_root;
	u8 encoding = btrfs_bg_flags_to_raid_index(bioc->map_type);
	struct btrfs_stripe_extent *stripe_extent;
	const size_t item_size = struct_size(stripe_extent, strides, num_stripes);
	int ret;

	stripe_extent = kzalloc(item_size, GFP_NOFS);
	if (!stripe_extent) {
		btrfs_abort_transaction(trans, -ENOMEM);
		btrfs_end_transaction(trans);
		return -ENOMEM;
	}

	btrfs_set_stack_stripe_extent_encoding(stripe_extent, encoding);
	for (int i = 0; i < num_stripes; i++) {
		u64 devid = bioc->stripes[i].dev->devid;
		u64 physical = bioc->stripes[i].physical;
		u64 length = bioc->stripes[i].length;
		struct btrfs_raid_stride *raid_stride = &stripe_extent->strides[i];

		if (length == 0)
			length = bioc->size;

		btrfs_set_stack_raid_stride_devid(raid_stride, devid);
		btrfs_set_stack_raid_stride_physical(raid_stride, physical);
		btrfs_set_stack_raid_stride_length(raid_stride, length);
	}

	stripe_key.objectid = bioc->logical;
	stripe_key.type = BTRFS_RAID_STRIPE_KEY;
	stripe_key.offset = bioc->size;

	ret = btrfs_insert_item(trans, stripe_root, &stripe_key, stripe_extent,
				item_size);
	if (ret)
		btrfs_abort_transaction(trans, ret);

	kfree(stripe_extent);

	return ret;
}

static int btrfs_insert_mirrored_raid_extents(struct btrfs_trans_handle *trans,
					      struct btrfs_ordered_extent *ordered,
					      u64 map_type)
{
	int num_stripes = btrfs_bg_type_to_factor(map_type);
	struct btrfs_io_context *bioc;
	int ret;

	list_for_each_entry(bioc, &ordered->bioc_list, rst_ordered_entry) {
		ret = btrfs_insert_one_raid_extent(trans, num_stripes, bioc);
		if (ret)
			return ret;
	}

	return 0;
}

static int btrfs_insert_striped_mirrored_raid_extents(
				      struct btrfs_trans_handle *trans,
				      struct btrfs_ordered_extent *ordered,
				      u64 map_type)
{
	struct btrfs_io_context *bioc;
	struct btrfs_io_context *rbioc;
	const size_t nstripes = list_count_nodes(&ordered->bioc_list);
	const enum btrfs_raid_types index = btrfs_bg_flags_to_raid_index(map_type);
	const int substripes = btrfs_raid_array[index].sub_stripes;
	const int max_stripes = div_u64(trans->fs_info->fs_devices->rw_devices,
					substripes);
	int left = nstripes;
	int i;
	int ret = 0;
	u64 stripe_end;
	u64 prev_end;

	if (nstripes == 1)
		return btrfs_insert_mirrored_raid_extents(trans, ordered, map_type);

	rbioc = kzalloc(struct_size(rbioc, stripes, nstripes * substripes), GFP_NOFS);
	if (!rbioc)
		return -ENOMEM;

	rbioc->map_type = map_type;
	rbioc->logical = list_first_entry(&ordered->bioc_list, typeof(*rbioc),
					  rst_ordered_entry)->logical;

	stripe_end = rbioc->logical;
	prev_end = stripe_end;
	i = 0;
	list_for_each_entry(bioc, &ordered->bioc_list, rst_ordered_entry) {
		rbioc->size += bioc->size;
		for (int j = 0; j < substripes; j++) {
			int stripe = i + j;

			rbioc->stripes[stripe].dev = bioc->stripes[j].dev;
			rbioc->stripes[stripe].physical = bioc->stripes[j].physical;
			rbioc->stripes[stripe].length = bioc->size;
		}

		stripe_end += rbioc->size;
		if (i >= nstripes ||
		    (stripe_end - prev_end >= max_stripes * BTRFS_STRIPE_LEN)) {
			ret = btrfs_insert_one_raid_extent(trans, nstripes * substripes,
							   rbioc);
			if (ret)
				goto out;

			left -= nstripes;
			i = 0;
			rbioc->logical += rbioc->size;
			rbioc->size = 0;
		} else {
			i += substripes;
			prev_end = stripe_end;
		}
	}

	if (left) {
		bioc = list_prev_entry(bioc, rst_ordered_entry);
		ret = btrfs_insert_one_raid_extent(trans, substripes, bioc);
	}

out:
	kfree(rbioc);
	return ret;
}

static int btrfs_insert_striped_raid_extents(struct btrfs_trans_handle *trans,
				     struct btrfs_ordered_extent *ordered,
				     u64 map_type)
{
	struct btrfs_io_context *bioc;
	struct btrfs_io_context *rbioc;
	const size_t nstripes = list_count_nodes(&ordered->bioc_list);
	int i;
	int ret = 0;

	rbioc = kzalloc(struct_size(rbioc, stripes, nstripes), GFP_NOFS);
	if (!rbioc)
		return -ENOMEM;
	rbioc->map_type = map_type;
	rbioc->logical = list_first_entry(&ordered->bioc_list, typeof(*rbioc),
					  rst_ordered_entry)->logical;

	i = 0;
	list_for_each_entry(bioc, &ordered->bioc_list, rst_ordered_entry) {
		rbioc->size += bioc->size;
		rbioc->stripes[i].dev = bioc->stripes[0].dev;
		rbioc->stripes[i].physical = bioc->stripes[0].physical;
		rbioc->stripes[i].length = bioc->size;

		if (i == nstripes - 1) {
			ret = btrfs_insert_one_raid_extent(trans, nstripes, rbioc);
			if (ret)
				goto out;

			i = 0;
			rbioc->logical += rbioc->size;
			rbioc->size = 0;
		} else {
			i++;
		}
	}

	if (i && i < nstripes - 1)
		ret = btrfs_insert_one_raid_extent(trans, i, rbioc);

out:
	kfree(rbioc);
	return ret;
}

int btrfs_insert_raid_extent(struct btrfs_trans_handle *trans,
			     struct btrfs_ordered_extent *ordered_extent)
{
	struct btrfs_io_context *bioc;
	u64 map_type;
	int ret;

	if (!btrfs_fs_incompat(trans->fs_info, RAID_STRIPE_TREE))
		return 0;

	map_type = list_first_entry(&ordered_extent->bioc_list, typeof(*bioc),
				    rst_ordered_entry)->map_type;

	switch (map_type & BTRFS_BLOCK_GROUP_PROFILE_MASK) {
	case BTRFS_BLOCK_GROUP_DUP:
	case BTRFS_BLOCK_GROUP_RAID1:
	case BTRFS_BLOCK_GROUP_RAID1C3:
	case BTRFS_BLOCK_GROUP_RAID1C4:
		ret = btrfs_insert_mirrored_raid_extents(trans, ordered_extent, map_type);
		break;
	case BTRFS_BLOCK_GROUP_RAID0:
		ret = btrfs_insert_striped_raid_extents(trans, ordered_extent, map_type);
		break;
	case BTRFS_BLOCK_GROUP_RAID10:
		ret = btrfs_insert_striped_mirrored_raid_extents(trans, ordered_extent,
								 map_type);
		break;
	default:
		btrfs_err(trans->fs_info, "trying to insert unknown block group profile %lld",
			  map_type & BTRFS_BLOCK_GROUP_PROFILE_MASK);
		ret = -EINVAL;
		break;
	}

	while (!list_empty(&ordered_extent->bioc_list)) {
		bioc = list_first_entry(&ordered_extent->bioc_list,
					typeof(*bioc), rst_ordered_entry);
		list_del(&bioc->rst_ordered_entry);
		btrfs_put_bioc(bioc);
	}

	return ret;
}
