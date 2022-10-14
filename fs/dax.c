// SPDX-License-Identifier: GPL-2.0-only
/*
 * fs/dax.c - Direct Access filesystem code
 * Copyright (c) 2013-2014 Intel Corporation
 * Author: Matthew Wilcox <matthew.r.wilcox@intel.com>
 * Author: Ross Zwisler <ross.zwisler@linux.intel.com>
 */

#include <linux/atomic.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/dax.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/memcontrol.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/pagevec.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/uio.h>
#include <linux/vmstat.h>
#include <linux/pfn_t.h>
#include <linux/sizes.h>
#include <linux/mmu_notifier.h>
#include <linux/iomap.h>
#include <linux/rmap.h>
#include <asm/pgalloc.h>

#include <trace/events/fs_dax.h>

static pgoff_t dax_iomap_pgoff(const struct iomap *iomap, loff_t pos)
{
	return PHYS_PFN(iomap->addr + (pos & PAGE_MASK) - iomap->offset);
}

static int copy_cow_page_dax(struct vm_fault *vmf, const struct iomap_iter *iter)
{
	pgoff_t pgoff = dax_iomap_pgoff(&iter->iomap, iter->pos);
	void *vto, *kaddr;
	long rc;
	int id;

	id = dax_read_lock();
	rc = dax_direct_access(iter->iomap.dax_dev, pgoff, 1, DAX_ACCESS,
				&kaddr, NULL);
	if (rc < 0) {
		dax_read_unlock(id);
		return rc;
	}
	vto = kmap_atomic(vmf->cow_page);
	copy_user_page(vto, kaddr, vmf->address, vmf->cow_page);
	kunmap_atomic(vto);
	dax_read_unlock(id);
	return 0;
}

/*
 * Flush the mapping to the persistent domain within the byte range of [start,
 * end]. This is required by data integrity operations to ensure file data is
 * on persistent storage prior to completion of the operation.
 */
int dax_writeback_mapping_range(struct address_space *mapping,
		struct dax_device *dax_dev, struct writeback_control *wbc)
{
	XA_STATE(xas, &mapping->i_pages, wbc->range_start >> PAGE_SHIFT);
	struct inode *inode = mapping->host;
	pgoff_t end_index = wbc->range_end >> PAGE_SHIFT;
	void *entry;
	int ret = 0;
	unsigned int scanned = 0;

	if (WARN_ON_ONCE(inode->i_blkbits != PAGE_SHIFT))
		return -EIO;

	if (mapping_empty(mapping) || wbc->sync_mode != WB_SYNC_ALL)
		return 0;

	trace_dax_writeback_range(inode, xas.xa_index, end_index);

	tag_pages_for_writeback(mapping, xas.xa_index, end_index);

	xas_lock_irq(&xas);
	xas_for_each_marked(&xas, entry, end_index, PAGECACHE_TAG_TOWRITE) {
		ret = dax_writeback_one(&xas, dax_dev, mapping, entry);
		if (ret < 0) {
			mapping_set_error(mapping, ret);
			break;
		}
		if (++scanned % XA_CHECK_SCHED)
			continue;

		xas_pause(&xas);
		xas_unlock_irq(&xas);
		cond_resched();
		xas_lock_irq(&xas);
	}
	xas_unlock_irq(&xas);
	trace_dax_writeback_range_done(inode, xas.xa_index, end_index);
	return ret;
}
EXPORT_SYMBOL_GPL(dax_writeback_mapping_range);

static int dax_iomap_direct_access(const struct iomap *iomap, loff_t pos,
		size_t size, void **kaddr, pfn_t *pfnp)
{
	pgoff_t pgoff = dax_iomap_pgoff(iomap, pos);
	long length;
	int rc = 0;

	length = dax_direct_access(iomap->dax_dev, pgoff, PHYS_PFN(size),
				   DAX_ACCESS, kaddr, pfnp);
	if (length < 0) {
		rc = length;
		goto out;
	}
	if (!pfnp)
		goto out_check_addr;
	rc = -EINVAL;
	if (PFN_PHYS(length) < size)
		goto out;
	if (pfn_t_to_pfn(*pfnp) & (PHYS_PFN(size)-1))
		goto out;
	/* For larger pages we need devmap */
	if (length > 1 && !pfn_t_devmap(*pfnp))
		goto out;
	rc = 0;

out_check_addr:
	if (!kaddr)
		goto out;
	if (!*kaddr)
		rc = -EFAULT;
out:
	return rc;
}

/**
 * dax_iomap_cow_copy - Copy the data from source to destination before write
 * @pos:	address to do copy from.
 * @length:	size of copy operation.
 * @align_size:	aligned w.r.t align_size (either PMD_SIZE or PAGE_SIZE)
 * @srcmap:	iomap srcmap
 * @daddr:	destination address to copy to.
 *
 * This can be called from two places. Either during DAX write fault (page
 * aligned), to copy the length size data to daddr. Or, while doing normal DAX
 * write operation, dax_iomap_actor() might call this to do the copy of either
 * start or end unaligned address. In the latter case the rest of the copy of
 * aligned ranges is taken care by dax_iomap_actor() itself.
 */
static int dax_iomap_cow_copy(loff_t pos, uint64_t length, size_t align_size,
		const struct iomap *srcmap, void *daddr)
{
	loff_t head_off = pos & (align_size - 1);
	size_t size = ALIGN(head_off + length, align_size);
	loff_t end = pos + length;
	loff_t pg_end = round_up(end, align_size);
	bool copy_all = head_off == 0 && end == pg_end;
	void *saddr = 0;
	int ret = 0;

	ret = dax_iomap_direct_access(srcmap, pos, size, &saddr, NULL);
	if (ret)
		return ret;

	if (copy_all) {
		ret = copy_mc_to_kernel(daddr, saddr, length);
		return ret ? -EIO : 0;
	}

	/* Copy the head part of the range */
	if (head_off) {
		ret = copy_mc_to_kernel(daddr, saddr, head_off);
		if (ret)
			return -EIO;
	}

	/* Copy the tail part of the range */
	if (end < pg_end) {
		loff_t tail_off = head_off + length;
		loff_t tail_len = pg_end - end;

		ret = copy_mc_to_kernel(daddr + tail_off, saddr + tail_off,
					tail_len);
		if (ret)
			return -EIO;
	}
	return 0;
}

/*
 * MAP_SYNC on a dax mapping guarantees dirty metadata is
 * flushed on write-faults (non-cow), but not read-faults.
 */
static bool dax_fault_is_synchronous(const struct iomap_iter *iter,
				     struct vm_area_struct *vma)
{
	return (iter->flags & IOMAP_WRITE) && (vma->vm_flags & VM_SYNC) &&
	       (iter->iomap.flags & IOMAP_F_DIRTY);
}

static bool dax_fault_is_cow(const struct iomap_iter *iter)
{
	return (iter->flags & IOMAP_WRITE) &&
	       (iter->iomap.flags & IOMAP_F_SHARED);
}

static unsigned long dax_iter_flags(const struct iomap_iter *iter,
				    struct vm_fault *vmf)
{
	unsigned long flags = 0;

	if (!dax_fault_is_synchronous(iter, vmf->vma))
		flags |= DAX_DIRTY;

	if (dax_fault_is_cow(iter))
		flags |= DAX_COW;

	return flags;
}

/*
 * The user has performed a load from a hole in the file.  Allocating a new
 * page in the file would cause excessive storage usage for workloads with
 * sparse files.  Instead we insert a read-only mapping of the 4k zero page.
 * If this page is ever written to we will re-fault and change the mapping to
 * point to real DAX storage instead.
 */
static vm_fault_t dax_load_hole(struct xa_state *xas, struct vm_fault *vmf,
		const struct iomap_iter *iter, void **entry)
{
	struct inode *inode = iter->inode;
	unsigned long vaddr = vmf->address;
	pfn_t pfn = pfn_to_pfn_t(my_zero_pfn(vaddr));
	vm_fault_t ret;

	ret = dax_insert_entry(xas, vmf, entry, pfn,
			       DAX_ZERO_PAGE | dax_iter_flags(iter, vmf));
	if (ret)
		goto out;

	ret = vmf_insert_mixed(vmf->vma, vaddr, pfn);
out:
	trace_dax_load_hole(inode, vmf, ret);
	return ret;
}

#ifdef CONFIG_FS_DAX_PMD
static vm_fault_t dax_pmd_load_hole(struct xa_state *xas, struct vm_fault *vmf,
		const struct iomap_iter *iter, void **entry)
{
	struct address_space *mapping = vmf->vma->vm_file->f_mapping;
	unsigned long pmd_addr = vmf->address & PMD_MASK;
	struct vm_area_struct *vma = vmf->vma;
	struct inode *inode = mapping->host;
	pgtable_t pgtable = NULL;
	struct page *zero_page;
	spinlock_t *ptl;
	pmd_t pmd_entry;
	vm_fault_t ret;
	pfn_t pfn;

	zero_page = mm_get_huge_zero_page(vmf->vma->vm_mm);

	if (unlikely(!zero_page))
		goto fallback;

	pfn = page_to_pfn_t(zero_page);
	ret = dax_insert_entry(xas, vmf, entry, pfn,
			       DAX_PMD | DAX_ZERO_PAGE |
				       dax_iter_flags(iter, vmf));
	if (ret)
		return ret;

	if (arch_needs_pgtable_deposit()) {
		pgtable = pte_alloc_one(vma->vm_mm);
		if (!pgtable)
			return VM_FAULT_OOM;
	}

	ptl = pmd_lock(vmf->vma->vm_mm, vmf->pmd);
	if (!pmd_none(*(vmf->pmd))) {
		spin_unlock(ptl);
		goto fallback;
	}

	if (pgtable) {
		pgtable_trans_huge_deposit(vma->vm_mm, vmf->pmd, pgtable);
		mm_inc_nr_ptes(vma->vm_mm);
	}
	pmd_entry = mk_pmd(zero_page, vmf->vma->vm_page_prot);
	pmd_entry = pmd_mkhuge(pmd_entry);
	set_pmd_at(vmf->vma->vm_mm, pmd_addr, vmf->pmd, pmd_entry);
	spin_unlock(ptl);
	trace_dax_pmd_load_hole(inode, vmf, zero_page, *entry);
	return VM_FAULT_NOPAGE;

fallback:
	if (pgtable)
		pte_free(vma->vm_mm, pgtable);
	trace_dax_pmd_load_hole_fallback(inode, vmf, zero_page, *entry);
	return VM_FAULT_FALLBACK;
}
#else
static vm_fault_t dax_pmd_load_hole(struct xa_state *xas, struct vm_fault *vmf,
		const struct iomap_iter *iter, void **entry)
{
	return VM_FAULT_FALLBACK;
}
#endif /* CONFIG_FS_DAX_PMD */

static int dax_memzero(struct iomap_iter *iter, loff_t pos, size_t size)
{
	const struct iomap *iomap = &iter->iomap;
	const struct iomap *srcmap = iomap_iter_srcmap(iter);
	unsigned offset = offset_in_page(pos);
	pgoff_t pgoff = dax_iomap_pgoff(iomap, pos);
	void *kaddr;
	long ret;

	ret = dax_direct_access(iomap->dax_dev, pgoff, 1, DAX_ACCESS, &kaddr,
				NULL);
	if (ret < 0)
		return ret;
	memset(kaddr + offset, 0, size);
	if (srcmap->addr != iomap->addr) {
		ret = dax_iomap_cow_copy(pos, size, PAGE_SIZE, srcmap,
					 kaddr);
		if (ret < 0)
			return ret;
		dax_flush(iomap->dax_dev, kaddr, PAGE_SIZE);
	} else
		dax_flush(iomap->dax_dev, kaddr + offset, size);
	return ret;
}

static s64 dax_zero_iter(struct iomap_iter *iter, bool *did_zero)
{
	const struct iomap *iomap = &iter->iomap;
	const struct iomap *srcmap = iomap_iter_srcmap(iter);
	loff_t pos = iter->pos;
	u64 length = iomap_length(iter);
	s64 written = 0;

	/* already zeroed?  we're done. */
	if (srcmap->type == IOMAP_HOLE || srcmap->type == IOMAP_UNWRITTEN)
		return length;

	do {
		unsigned offset = offset_in_page(pos);
		unsigned size = min_t(u64, PAGE_SIZE - offset, length);
		pgoff_t pgoff = dax_iomap_pgoff(iomap, pos);
		long rc;
		int id;

		id = dax_read_lock();
		if (IS_ALIGNED(pos, PAGE_SIZE) && size == PAGE_SIZE)
			rc = dax_zero_page_range(iomap->dax_dev, pgoff, 1);
		else
			rc = dax_memzero(iter, pos, size);
		dax_read_unlock(id);

		if (rc < 0)
			return rc;
		pos += size;
		length -= size;
		written += size;
	} while (length > 0);

	if (did_zero)
		*did_zero = true;
	return written;
}

int dax_zero_range(struct inode *inode, loff_t pos, loff_t len, bool *did_zero,
		const struct iomap_ops *ops)
{
	struct iomap_iter iter = {
		.inode		= inode,
		.pos		= pos,
		.len		= len,
		.flags		= IOMAP_DAX | IOMAP_ZERO,
	};
	int ret;

	while ((ret = iomap_iter(&iter, ops)) > 0)
		iter.processed = dax_zero_iter(&iter, did_zero);
	return ret;
}
EXPORT_SYMBOL_GPL(dax_zero_range);

int dax_truncate_page(struct inode *inode, loff_t pos, bool *did_zero,
		const struct iomap_ops *ops)
{
	unsigned int blocksize = i_blocksize(inode);
	unsigned int off = pos & (blocksize - 1);

	/* Block boundary? Nothing to do */
	if (!off)
		return 0;
	return dax_zero_range(inode, pos, blocksize - off, did_zero, ops);
}
EXPORT_SYMBOL_GPL(dax_truncate_page);

static loff_t dax_iomap_iter(const struct iomap_iter *iomi,
		struct iov_iter *iter)
{
	const struct iomap *iomap = &iomi->iomap;
	const struct iomap *srcmap = &iomi->srcmap;
	loff_t length = iomap_length(iomi);
	loff_t pos = iomi->pos;
	struct dax_device *dax_dev = iomap->dax_dev;
	loff_t end = pos + length, done = 0;
	bool write = iov_iter_rw(iter) == WRITE;
	ssize_t ret = 0;
	size_t xfer;
	int id;

	if (!write) {
		end = min(end, i_size_read(iomi->inode));
		if (pos >= end)
			return 0;

		if (iomap->type == IOMAP_HOLE || iomap->type == IOMAP_UNWRITTEN)
			return iov_iter_zero(min(length, end - pos), iter);
	}

	/*
	 * In DAX mode, enforce either pure overwrites of written extents, or
	 * writes to unwritten extents as part of a copy-on-write operation.
	 */
	if (WARN_ON_ONCE(iomap->type != IOMAP_MAPPED &&
			!(iomap->flags & IOMAP_F_SHARED)))
		return -EIO;

	/*
	 * Write can allocate block for an area which has a hole page mapped
	 * into page tables. We have to tear down these mappings so that data
	 * written by write(2) is visible in mmap.
	 */
	if (iomap->flags & IOMAP_F_NEW) {
		invalidate_inode_pages2_range(iomi->inode->i_mapping,
					      pos >> PAGE_SHIFT,
					      (end - 1) >> PAGE_SHIFT);
	}

	id = dax_read_lock();
	while (pos < end) {
		unsigned offset = pos & (PAGE_SIZE - 1);
		const size_t size = ALIGN(length + offset, PAGE_SIZE);
		pgoff_t pgoff = dax_iomap_pgoff(iomap, pos);
		ssize_t map_len;
		bool recovery = false;
		void *kaddr;

		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		map_len = dax_direct_access(dax_dev, pgoff, PHYS_PFN(size),
				DAX_ACCESS, &kaddr, NULL);
		if (map_len == -EIO && iov_iter_rw(iter) == WRITE) {
			map_len = dax_direct_access(dax_dev, pgoff,
					PHYS_PFN(size), DAX_RECOVERY_WRITE,
					&kaddr, NULL);
			if (map_len > 0)
				recovery = true;
		}
		if (map_len < 0) {
			ret = map_len;
			break;
		}

		if (write &&
		    srcmap->type != IOMAP_HOLE && srcmap->addr != iomap->addr) {
			ret = dax_iomap_cow_copy(pos, length, PAGE_SIZE, srcmap,
						 kaddr);
			if (ret)
				break;
		}

		map_len = PFN_PHYS(map_len);
		kaddr += offset;
		map_len -= offset;
		if (map_len > end - pos)
			map_len = end - pos;

		if (recovery)
			xfer = dax_recovery_write(dax_dev, pgoff, kaddr,
					map_len, iter);
		else if (write)
			xfer = dax_copy_from_iter(dax_dev, pgoff, kaddr,
					map_len, iter);
		else
			xfer = dax_copy_to_iter(dax_dev, pgoff, kaddr,
					map_len, iter);

		pos += xfer;
		length -= xfer;
		done += xfer;

		if (xfer == 0)
			ret = -EFAULT;
		if (xfer < map_len)
			break;
	}
	dax_read_unlock(id);

	return done ? done : ret;
}

/**
 * dax_iomap_rw - Perform I/O to a DAX file
 * @iocb:	The control block for this I/O
 * @iter:	The addresses to do I/O from or to
 * @ops:	iomap ops passed from the file system
 *
 * This function performs read and write operations to directly mapped
 * persistent memory.  The callers needs to take care of read/write exclusion
 * and evicting any page cache pages in the region under I/O.
 */
ssize_t
dax_iomap_rw(struct kiocb *iocb, struct iov_iter *iter,
		const struct iomap_ops *ops)
{
	struct iomap_iter iomi = {
		.inode		= iocb->ki_filp->f_mapping->host,
		.pos		= iocb->ki_pos,
		.len		= iov_iter_count(iter),
		.flags		= IOMAP_DAX,
	};
	loff_t done = 0;
	int ret;

	if (!iomi.len)
		return 0;

	if (iov_iter_rw(iter) == WRITE) {
		lockdep_assert_held_write(&iomi.inode->i_rwsem);
		iomi.flags |= IOMAP_WRITE;
	} else {
		lockdep_assert_held(&iomi.inode->i_rwsem);
	}

	if (iocb->ki_flags & IOCB_NOWAIT)
		iomi.flags |= IOMAP_NOWAIT;

	while ((ret = iomap_iter(&iomi, ops)) > 0)
		iomi.processed = dax_iomap_iter(&iomi, iter);

	done = iomi.pos - iocb->ki_pos;
	iocb->ki_pos = iomi.pos;
	return done ? done : ret;
}
EXPORT_SYMBOL_GPL(dax_iomap_rw);

static vm_fault_t dax_fault_return(int error)
{
	if (error == 0)
		return VM_FAULT_NOPAGE;
	return vmf_error(error);
}

/*
 * When handling a synchronous page fault and the inode need a fsync, we can
 * insert the PTE/PMD into page tables only after that fsync happened. Skip
 * insertion for now and return the pfn so that caller can insert it after the
 * fsync is done.
 */
static vm_fault_t dax_fault_synchronous_pfnp(pfn_t *pfnp, pfn_t pfn)
{
	if (WARN_ON_ONCE(!pfnp))
		return VM_FAULT_SIGBUS;
	*pfnp = pfn;
	return VM_FAULT_NEEDDSYNC;
}

static vm_fault_t dax_fault_cow_page(struct vm_fault *vmf,
		const struct iomap_iter *iter)
{
	vm_fault_t ret;
	int error = 0;

	switch (iter->iomap.type) {
	case IOMAP_HOLE:
	case IOMAP_UNWRITTEN:
		clear_user_highpage(vmf->cow_page, vmf->address);
		break;
	case IOMAP_MAPPED:
		error = copy_cow_page_dax(vmf, iter);
		break;
	default:
		WARN_ON_ONCE(1);
		error = -EIO;
		break;
	}

	if (error)
		return dax_fault_return(error);

	__SetPageUptodate(vmf->cow_page);
	ret = finish_fault(vmf);
	if (!ret)
		return VM_FAULT_DONE_COW;
	return ret;
}

/**
 * dax_fault_iter - Common actor to handle pfn insertion in PTE/PMD fault.
 * @vmf:	vm fault instance
 * @iter:	iomap iter
 * @pfnp:	pfn to be returned
 * @xas:	the dax mapping tree of a file
 * @entry:	an unlocked dax entry to be inserted
 * @pmd:	distinguish whether it is a pmd fault
 */
static vm_fault_t dax_fault_iter(struct vm_fault *vmf,
		const struct iomap_iter *iter, pfn_t *pfnp,
		struct xa_state *xas, void **entry, bool pmd)
{
	const struct iomap *iomap = &iter->iomap;
	const struct iomap *srcmap = &iter->srcmap;
	size_t size = pmd ? PMD_SIZE : PAGE_SIZE;
	loff_t pos = (loff_t)xas->xa_index << PAGE_SHIFT;
	bool write = iter->flags & IOMAP_WRITE;
	unsigned long entry_flags = pmd ? DAX_PMD : 0;
	int err = 0, id;
	vm_fault_t ret;
	pfn_t pfn;
	void *kaddr;

	if (!pmd && vmf->cow_page)
		return dax_fault_cow_page(vmf, iter);

	/* if we are reading UNWRITTEN and HOLE, return a hole. */
	if (!write &&
	    (iomap->type == IOMAP_UNWRITTEN || iomap->type == IOMAP_HOLE)) {
		if (!pmd)
			return dax_load_hole(xas, vmf, iter, entry);
		return dax_pmd_load_hole(xas, vmf, iter, entry);
	}

	if (iomap->type != IOMAP_MAPPED && !(iomap->flags & IOMAP_F_SHARED)) {
		WARN_ON_ONCE(1);
		return pmd ? VM_FAULT_FALLBACK : VM_FAULT_SIGBUS;
	}

	id = dax_read_lock();
	err = dax_iomap_direct_access(iomap, pos, size, &kaddr, &pfn);
	if (err) {
		dax_read_unlock(id);
		return pmd ? VM_FAULT_FALLBACK : dax_fault_return(err);
	}

	ret = dax_insert_entry(xas, vmf, entry, pfn,
			       entry_flags | dax_iter_flags(iter, vmf));
	dax_read_unlock(id);
	if (ret)
		return ret;

	if (write &&
	    srcmap->type != IOMAP_HOLE && srcmap->addr != iomap->addr) {
		err = dax_iomap_cow_copy(pos, size, size, srcmap, kaddr);
		if (err)
			return dax_fault_return(err);
	}

	if (dax_fault_is_synchronous(iter, vmf->vma))
		return dax_fault_synchronous_pfnp(pfnp, pfn);

	/* insert PMD pfn */
	if (pmd)
		return vmf_insert_pfn_pmd(vmf, pfn, write);

	/* insert PTE pfn */
	if (write)
		return vmf_insert_mixed_mkwrite(vmf->vma, vmf->address, pfn);
	return vmf_insert_mixed(vmf->vma, vmf->address, pfn);
}

static vm_fault_t dax_iomap_pte_fault(struct vm_fault *vmf, pfn_t *pfnp,
			       int *iomap_errp, const struct iomap_ops *ops)
{
	struct address_space *mapping = vmf->vma->vm_file->f_mapping;
	XA_STATE(xas, &mapping->i_pages, vmf->pgoff);
	struct iomap_iter iter = {
		.inode		= mapping->host,
		.pos		= (loff_t)vmf->pgoff << PAGE_SHIFT,
		.len		= PAGE_SIZE,
		.flags		= IOMAP_DAX | IOMAP_FAULT,
	};
	vm_fault_t ret = 0;
	void *entry;
	int error;

	trace_dax_pte_fault(iter.inode, vmf, ret);
	/*
	 * Check whether offset isn't beyond end of file now. Caller is supposed
	 * to hold locks serializing us with truncate / punch hole so this is
	 * a reliable test.
	 */
	if (iter.pos >= i_size_read(iter.inode)) {
		ret = VM_FAULT_SIGBUS;
		goto out;
	}

	if ((vmf->flags & FAULT_FLAG_WRITE) && !vmf->cow_page)
		iter.flags |= IOMAP_WRITE;

	entry = dax_grab_mapping_entry(&xas, mapping, 0);
	if (is_dax_err(entry)) {
		ret = dax_err_to_vmfault(entry);
		goto out;
	}

	/*
	 * It is possible, particularly with mixed reads & writes to private
	 * mappings, that we have raced with a PMD fault that overlaps with
	 * the PTE we need to set up.  If so just return and the fault will be
	 * retried.
	 */
	if (pmd_trans_huge(*vmf->pmd) || pmd_devmap(*vmf->pmd)) {
		ret = VM_FAULT_NOPAGE;
		goto unlock_entry;
	}

	while ((error = iomap_iter(&iter, ops)) > 0) {
		if (WARN_ON_ONCE(iomap_length(&iter) < PAGE_SIZE)) {
			iter.processed = -EIO;	/* fs corruption? */
			continue;
		}

		ret = dax_fault_iter(vmf, &iter, pfnp, &xas, &entry, false);
		if (ret != VM_FAULT_SIGBUS &&
		    (iter.iomap.flags & IOMAP_F_NEW)) {
			count_vm_event(PGMAJFAULT);
			count_memcg_event_mm(vmf->vma->vm_mm, PGMAJFAULT);
			ret |= VM_FAULT_MAJOR;
		}

		if (!(ret & VM_FAULT_ERROR))
			iter.processed = PAGE_SIZE;
	}

	if (iomap_errp)
		*iomap_errp = error;
	if (!ret && error)
		ret = dax_fault_return(error);

unlock_entry:
	dax_unlock_entry(&xas, entry);
out:
	trace_dax_pte_fault_done(iter.inode, vmf, ret);
	return ret;
}

#ifdef CONFIG_FS_DAX_PMD
static bool dax_fault_check_fallback(struct vm_fault *vmf, struct xa_state *xas,
		pgoff_t max_pgoff)
{
	unsigned long pmd_addr = vmf->address & PMD_MASK;
	bool write = vmf->flags & FAULT_FLAG_WRITE;

	/*
	 * Make sure that the faulting address's PMD offset (color) matches
	 * the PMD offset from the start of the file.  This is necessary so
	 * that a PMD range in the page table overlaps exactly with a PMD
	 * range in the page cache.
	 */
	if ((vmf->pgoff & PG_PMD_COLOUR) !=
	    ((vmf->address >> PAGE_SHIFT) & PG_PMD_COLOUR))
		return true;

	/* Fall back to PTEs if we're going to COW */
	if (write && !(vmf->vma->vm_flags & VM_SHARED))
		return true;

	/* If the PMD would extend outside the VMA */
	if (pmd_addr < vmf->vma->vm_start)
		return true;
	if ((pmd_addr + PMD_SIZE) > vmf->vma->vm_end)
		return true;

	/* If the PMD would extend beyond the file size */
	if ((xas->xa_index | PG_PMD_COLOUR) >= max_pgoff)
		return true;

	return false;
}

static vm_fault_t dax_iomap_pmd_fault(struct vm_fault *vmf, pfn_t *pfnp,
			       const struct iomap_ops *ops)
{
	struct address_space *mapping = vmf->vma->vm_file->f_mapping;
	XA_STATE_ORDER(xas, &mapping->i_pages, vmf->pgoff, PMD_ORDER);
	struct iomap_iter iter = {
		.inode		= mapping->host,
		.len		= PMD_SIZE,
		.flags		= IOMAP_DAX | IOMAP_FAULT,
	};
	vm_fault_t ret = VM_FAULT_FALLBACK;
	pgoff_t max_pgoff;
	void *entry;
	int error;

	if (vmf->flags & FAULT_FLAG_WRITE)
		iter.flags |= IOMAP_WRITE;

	/*
	 * Check whether offset isn't beyond end of file now. Caller is
	 * supposed to hold locks serializing us with truncate / punch hole so
	 * this is a reliable test.
	 */
	max_pgoff = DIV_ROUND_UP(i_size_read(iter.inode), PAGE_SIZE);

	trace_dax_pmd_fault(iter.inode, vmf, max_pgoff, 0);

	if (xas.xa_index >= max_pgoff) {
		ret = VM_FAULT_SIGBUS;
		goto out;
	}

	if (dax_fault_check_fallback(vmf, &xas, max_pgoff))
		goto fallback;

	/*
	 * dax_grab_mapping_entry() will make sure we get an empty PMD entry,
	 * a zero PMD entry or a DAX PMD.  If it can't (because a PTE
	 * entry is already in the array, for instance), it will return
	 * VM_FAULT_FALLBACK.
	 */
	entry = dax_grab_mapping_entry(&xas, mapping, PMD_ORDER);
	if (is_dax_err(entry)) {
		ret = dax_err_to_vmfault(entry);
		goto fallback;
	}

	/*
	 * It is possible, particularly with mixed reads & writes to private
	 * mappings, that we have raced with a PTE fault that overlaps with
	 * the PMD we need to set up.  If so just return and the fault will be
	 * retried.
	 */
	if (!pmd_none(*vmf->pmd) && !pmd_trans_huge(*vmf->pmd) &&
			!pmd_devmap(*vmf->pmd)) {
		ret = 0;
		goto unlock_entry;
	}

	iter.pos = (loff_t)xas.xa_index << PAGE_SHIFT;
	while ((error = iomap_iter(&iter, ops)) > 0) {
		if (iomap_length(&iter) < PMD_SIZE)
			continue; /* actually breaks out of the loop */

		ret = dax_fault_iter(vmf, &iter, pfnp, &xas, &entry, true);
		if (ret != VM_FAULT_FALLBACK)
			iter.processed = PMD_SIZE;
	}

unlock_entry:
	dax_unlock_entry(&xas, entry);
fallback:
	if (ret == VM_FAULT_FALLBACK) {
		split_huge_pmd(vmf->vma, vmf->pmd, vmf->address);
		count_vm_event(THP_FAULT_FALLBACK);
	}
out:
	trace_dax_pmd_fault_done(iter.inode, vmf, max_pgoff, ret);
	return ret;
}
#else
static vm_fault_t dax_iomap_pmd_fault(struct vm_fault *vmf, pfn_t *pfnp,
			       const struct iomap_ops *ops)
{
	return VM_FAULT_FALLBACK;
}
#endif /* CONFIG_FS_DAX_PMD */

/**
 * dax_iomap_fault - handle a page fault on a DAX file
 * @vmf: The description of the fault
 * @pe_size: Size of the page to fault in
 * @pfnp: PFN to insert for synchronous faults if fsync is required
 * @iomap_errp: Storage for detailed error code in case of error
 * @ops: Iomap ops passed from the file system
 *
 * When a page fault occurs, filesystems may call this helper in
 * their fault handler for DAX files. dax_iomap_fault() assumes the caller
 * has done all the necessary locking for page fault to proceed
 * successfully.
 */
vm_fault_t dax_iomap_fault(struct vm_fault *vmf, enum page_entry_size pe_size,
		    pfn_t *pfnp, int *iomap_errp, const struct iomap_ops *ops)
{
	switch (pe_size) {
	case PE_SIZE_PTE:
		return dax_iomap_pte_fault(vmf, pfnp, iomap_errp, ops);
	case PE_SIZE_PMD:
		return dax_iomap_pmd_fault(vmf, pfnp, ops);
	default:
		return VM_FAULT_FALLBACK;
	}
}
EXPORT_SYMBOL_GPL(dax_iomap_fault);

/**
 * dax_finish_sync_fault - finish synchronous page fault
 * @vmf: The description of the fault
 * @pe_size: Size of entry to be inserted
 * @pfn: PFN to insert
 *
 * This function ensures that the file range touched by the page fault is
 * stored persistently on the media and handles inserting of appropriate page
 * table entry.
 */
vm_fault_t dax_finish_sync_fault(struct vm_fault *vmf,
		enum page_entry_size pe_size, pfn_t pfn)
{
	int err;
	loff_t start = ((loff_t)vmf->pgoff) << PAGE_SHIFT;
	unsigned int order = pe_order(pe_size);
	size_t len = PAGE_SIZE << order;

	err = vfs_fsync_range(vmf->vma->vm_file, start, start + len - 1, 1);
	if (err)
		return VM_FAULT_SIGBUS;
	return dax_insert_pfn_mkwrite(vmf, pfn, order);
}
EXPORT_SYMBOL_GPL(dax_finish_sync_fault);

static loff_t dax_range_compare_iter(struct iomap_iter *it_src,
		struct iomap_iter *it_dest, u64 len, bool *same)
{
	const struct iomap *smap = &it_src->iomap;
	const struct iomap *dmap = &it_dest->iomap;
	loff_t pos1 = it_src->pos, pos2 = it_dest->pos;
	void *saddr, *daddr;
	int id, ret;

	len = min(len, min(smap->length, dmap->length));

	if (smap->type == IOMAP_HOLE && dmap->type == IOMAP_HOLE) {
		*same = true;
		return len;
	}

	if (smap->type == IOMAP_HOLE || dmap->type == IOMAP_HOLE) {
		*same = false;
		return 0;
	}

	id = dax_read_lock();
	ret = dax_iomap_direct_access(smap, pos1, ALIGN(pos1 + len, PAGE_SIZE),
				      &saddr, NULL);
	if (ret < 0)
		goto out_unlock;

	ret = dax_iomap_direct_access(dmap, pos2, ALIGN(pos2 + len, PAGE_SIZE),
				      &daddr, NULL);
	if (ret < 0)
		goto out_unlock;

	*same = !memcmp(saddr, daddr, len);
	if (!*same)
		len = 0;
	dax_read_unlock(id);
	return len;

out_unlock:
	dax_read_unlock(id);
	return -EIO;
}

int dax_dedupe_file_range_compare(struct inode *src, loff_t srcoff,
		struct inode *dst, loff_t dstoff, loff_t len, bool *same,
		const struct iomap_ops *ops)
{
	struct iomap_iter src_iter = {
		.inode		= src,
		.pos		= srcoff,
		.len		= len,
		.flags		= IOMAP_DAX,
	};
	struct iomap_iter dst_iter = {
		.inode		= dst,
		.pos		= dstoff,
		.len		= len,
		.flags		= IOMAP_DAX,
	};
	int ret;

	while ((ret = iomap_iter(&src_iter, ops)) > 0) {
		while ((ret = iomap_iter(&dst_iter, ops)) > 0) {
			dst_iter.processed = dax_range_compare_iter(&src_iter,
						&dst_iter, len, same);
		}
		if (ret <= 0)
			src_iter.processed = ret;
	}
	return ret;
}

int dax_remap_file_range_prep(struct file *file_in, loff_t pos_in,
			      struct file *file_out, loff_t pos_out,
			      loff_t *len, unsigned int remap_flags,
			      const struct iomap_ops *ops)
{
	return __generic_remap_file_range_prep(file_in, pos_in, file_out,
					       pos_out, len, remap_flags, ops);
}
EXPORT_SYMBOL_GPL(dax_remap_file_range_prep);
