// SPDX-License-Identifier: GPL-2.0-only
/*
 * Direct Access mapping infrastructure split from fs/dax.c
 * Copyright (c) 2013-2014 Intel Corporation
 * Author: Matthew Wilcox <matthew.r.wilcox@intel.com>
 * Author: Ross Zwisler <ross.zwisler@linux.intel.com>
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/dax.h>
#include <linux/rmap.h>
#include <linux/pfn_t.h>
#include <linux/sizes.h>
#include <linux/pagemap.h>
#include <linux/huge_mm.h>

#include "dax-private.h"

#define CREATE_TRACE_POINTS
#include <trace/events/fs_dax.h>

/* We choose 4096 entries - same as per-zone page wait tables */
#define DAX_WAIT_TABLE_BITS 12
#define DAX_WAIT_TABLE_ENTRIES (1 << DAX_WAIT_TABLE_BITS)

static wait_queue_head_t wait_table[DAX_WAIT_TABLE_ENTRIES];

void __init dax_mapping_init(void)
{
	int i;

	for (i = 0; i < DAX_WAIT_TABLE_ENTRIES; i++)
		init_waitqueue_head(wait_table + i);
}

static unsigned long dax_to_pfn(void *entry)
{
	return xa_to_value(entry) >> DAX_SHIFT;
}

static void *dax_make_entry(pfn_t pfn, unsigned long flags)
{
	return xa_mk_value((flags & DAX_MASK) |
			   (pfn_t_to_pfn(pfn) << DAX_SHIFT));
}

static bool dax_is_locked(void *entry)
{
	return xa_to_value(entry) & DAX_LOCKED;
}

static bool dax_is_zapped(void *entry)
{
	return xa_to_value(entry) & DAX_ZAP;
}

static unsigned int dax_entry_order(void *entry)
{
	if (xa_to_value(entry) & DAX_PUD)
		return PUD_ORDER;
	if (xa_to_value(entry) & DAX_PMD)
		return PMD_ORDER;
	return 0;
}

static unsigned long dax_is_pmd_entry(void *entry)
{
	return xa_to_value(entry) & DAX_PMD;
}

static unsigned long dax_is_pud_entry(void *entry)
{
	return xa_to_value(entry) & DAX_PUD;
}

static bool dax_is_pte_entry(void *entry)
{
	return !(xa_to_value(entry) & (DAX_PMD|DAX_PUD));
}

static int dax_is_zero_entry(void *entry)
{
	return xa_to_value(entry) & DAX_ZERO_PAGE;
}

static int dax_is_empty_entry(void *entry)
{
	return xa_to_value(entry) & DAX_EMPTY;
}

/*
 * true if the entry that was found is of a smaller order than the entry
 * we were looking for
 */
static bool dax_is_conflict(void *entry)
{
	return entry == XA_RETRY_ENTRY;
}

/*
 * DAX page cache entry locking
 */
struct exceptional_entry_key {
	struct xarray *xa;
	pgoff_t entry_start;
};

struct wait_exceptional_entry_queue {
	wait_queue_entry_t wait;
	struct exceptional_entry_key key;
};

/**
 * enum dax_wake_mode: waitqueue wakeup behaviour
 * @WAKE_ALL: wake all waiters in the waitqueue
 * @WAKE_NEXT: wake only the first waiter in the waitqueue
 */
enum dax_wake_mode {
	WAKE_ALL,
	WAKE_NEXT,
};

static wait_queue_head_t *dax_entry_waitqueue(struct xa_state *xas, void *entry,
					      struct exceptional_entry_key *key)
{
	unsigned long hash;
	unsigned long index = xas->xa_index;

	/*
	 * If 'entry' is a PMD, align the 'index' that we use for the wait
	 * queue to the start of that PMD.  This ensures that all offsets in
	 * the range covered by the PMD map to the same bit lock.
	 */
	if (dax_is_pmd_entry(entry))
		index &= ~PG_PMD_COLOUR;
	key->xa = xas->xa;
	key->entry_start = index;

	hash = hash_long((unsigned long)xas->xa ^ index, DAX_WAIT_TABLE_BITS);
	return wait_table + hash;
}

static int wake_exceptional_entry_func(wait_queue_entry_t *wait,
				       unsigned int mode, int sync, void *keyp)
{
	struct exceptional_entry_key *key = keyp;
	struct wait_exceptional_entry_queue *ewait =
		container_of(wait, struct wait_exceptional_entry_queue, wait);

	if (key->xa != ewait->key.xa ||
	    key->entry_start != ewait->key.entry_start)
		return 0;
	return autoremove_wake_function(wait, mode, sync, NULL);
}

/*
 * @entry may no longer be the entry at the index in the mapping.
 * The important information it's conveying is whether the entry at
 * this index used to be a PMD entry.
 */
static void dax_wake_entry(struct xa_state *xas, void *entry,
			   enum dax_wake_mode mode)
{
	struct exceptional_entry_key key;
	wait_queue_head_t *wq;

	wq = dax_entry_waitqueue(xas, entry, &key);

	/*
	 * Checking for locked entry and prepare_to_wait_exclusive() happens
	 * under the i_pages lock, ditto for entry handling in our callers.
	 * So at this point all tasks that could have seen our entry locked
	 * must be in the waitqueue and the following check will see them.
	 */
	if (waitqueue_active(wq))
		__wake_up(wq, TASK_NORMAL, mode == WAKE_ALL ? 0 : 1, &key);
}

/*
 * Look up entry in page cache, wait for it to become unlocked if it
 * is a DAX entry and return it.  The caller must subsequently call
 * put_unlocked_entry() if it did not lock the entry or dax_unlock_entry()
 * if it did.  The entry returned may have a larger order than @order.
 * If @order is larger than the order of the entry found in i_pages, this
 * function returns a dax_is_conflict entry.
 *
 * Must be called with the i_pages lock held.
 */
static void *get_unlocked_entry(struct xa_state *xas, unsigned int order)
{
	void *entry;
	struct wait_exceptional_entry_queue ewait;
	wait_queue_head_t *wq;

	init_wait(&ewait.wait);
	ewait.wait.func = wake_exceptional_entry_func;

	for (;;) {
		entry = xas_find_conflict(xas);
		if (!entry || WARN_ON_ONCE(!xa_is_value(entry)))
			return entry;
		if (dax_entry_order(entry) < order)
			return XA_RETRY_ENTRY;
		if (!dax_is_locked(entry))
			return entry;

		wq = dax_entry_waitqueue(xas, entry, &ewait.key);
		prepare_to_wait_exclusive(wq, &ewait.wait,
					  TASK_UNINTERRUPTIBLE);
		xas_unlock_irq(xas);
		xas_reset(xas);
		schedule();
		finish_wait(wq, &ewait.wait);
		xas_lock_irq(xas);
	}
}

/*
 * The only thing keeping the address space around is the i_pages lock
 * (it's cycled in clear_inode() after removing the entries from i_pages)
 * After we call xas_unlock_irq(), we cannot touch xas->xa.
 */
static void wait_entry_unlocked(struct xa_state *xas, void *entry) __releases(xas)
{
	struct wait_exceptional_entry_queue ewait;
	wait_queue_head_t *wq;

	init_wait(&ewait.wait);
	ewait.wait.func = wake_exceptional_entry_func;

	wq = dax_entry_waitqueue(xas, entry, &ewait.key);
	/*
	 * Unlike get_unlocked_entry() there is no guarantee that this
	 * path ever successfully retrieves an unlocked entry before an
	 * inode dies. Perform a non-exclusive wait in case this path
	 * never successfully performs its own wake up.
	 */
	prepare_to_wait(wq, &ewait.wait, TASK_UNINTERRUPTIBLE);
	xas_unlock_irq(xas);
	schedule();
	finish_wait(wq, &ewait.wait);
}

static void put_unlocked_entry(struct xa_state *xas, void *entry,
			       enum dax_wake_mode mode)
{
	if (entry && !dax_is_conflict(entry))
		dax_wake_entry(xas, entry, mode);
}

/*
 * We used the xa_state to get the entry, but then we locked the entry and
 * dropped the xa_lock, so we know the xa_state is stale and must be reset
 * before use.
 */
void dax_unlock_entry(struct xa_state *xas, void *entry)
{
	void *old;

	WARN_ON(dax_is_locked(entry));
	xas_reset(xas);
	xas_lock_irq(xas);
	old = xas_store(xas, entry);
	xas_unlock_irq(xas);
	WARN_ON(!dax_is_locked(old));
	dax_wake_entry(xas, entry, WAKE_NEXT);
}
EXPORT_SYMBOL_NS_GPL(dax_unlock_entry, DAX);

/*
 * Return: The entry stored at this location before it was locked.
 */
static void *dax_lock_entry(struct xa_state *xas, void *entry)
{
	unsigned long v = xa_to_value(entry);

	return xas_store(xas, xa_mk_value(v | DAX_LOCKED));
}

static unsigned long dax_entry_size(void *entry)
{
	if (dax_is_zero_entry(entry))
		return 0;
	else if (dax_is_empty_entry(entry))
		return 0;
	else if (dax_is_pmd_entry(entry))
		return PMD_SIZE;
	else if (dax_is_pud_entry(entry))
		return PUD_SIZE;
	else
		return PAGE_SIZE;
}

/*
 * Until fsdax constructs compound folios it needs to be prepared to
 * support multiple folios per entry where each folio is a single page
 */
static struct folio *dax_entry_to_folio(void *entry, int idx)
{
	unsigned long pfn, size = dax_entry_size(entry);
	struct page *page;
	struct folio *folio;

	if (!size)
		return NULL;

	pfn = dax_to_pfn(entry);
	page = pfn_to_page(pfn);
	folio = page_folio(page);

	/*
	 * Are there multiple folios per entry, and has the iterator
	 * passed the end of that set?
	 */
	if (idx >= size / folio_size(folio))
		return NULL;

	VM_WARN_ON_ONCE(!IS_ALIGNED(size, folio_size(folio)));

	return page_folio(page + idx);
}

/*
 * Iterate through all folios associated with a given entry
 */
#define dax_for_each_folio(entry, folio, i)                      \
	for (i = 0, folio = dax_entry_to_folio(entry, i); folio; \
	     folio = dax_entry_to_folio(entry, ++i))

static bool dax_mapping_is_cow(struct address_space *mapping)
{
	return (unsigned long)mapping == PAGE_MAPPING_DAX_COW;
}

/*
 * Set the page->mapping with FS_DAX_MAPPING_COW flag, increase the refcount.
 */
static void dax_mapping_set_cow(struct folio *folio)
{
	if ((uintptr_t)folio->mapping != PAGE_MAPPING_DAX_COW) {
		/*
		 * Reset the index if the folio was already mapped
		 * regularly before.
		 */
		if (folio->mapping)
			folio->index = 1;
		folio->mapping = (void *)PAGE_MAPPING_DAX_COW;
	}
	folio->index++;
}

static struct dev_pagemap *folio_pgmap(struct folio *folio)
{
	return folio_page(folio, 0)->pgmap;
}

/*
 * When it is called in dax_insert_entry(), the cow flag will indicate that
 * whether this entry is shared by multiple files.  If so, set the page->mapping
 * FS_DAX_MAPPING_COW, and use page->index as refcount.
 */
static vm_fault_t dax_associate_entry(void *entry,
				      struct address_space *mapping,
				      struct vm_fault *vmf, unsigned long flags)
{
	unsigned long size = dax_entry_size(entry), index;
	struct folio *folio;
	int i;

	if (IS_ENABLED(CONFIG_FS_DAX_LIMITED))
		return 0;

	index = linear_page_index(vmf->vma, ALIGN(vmf->address, size));
	dax_for_each_folio(entry, folio, i)
		if (flags & DAX_COW) {
			dax_mapping_set_cow(folio);
		} else {
			struct dev_pagemap *pgmap = folio_pgmap(folio);
			unsigned long pfn = page_to_pfn(&folio->page);

			WARN_ON_ONCE(folio->mapping);
			if (folio !=
			    pgmap_request_folio(pgmap,
						pfn_to_pgmap_offset(pgmap, pfn),
						folio_order(folio)))
				return VM_FAULT_SIGBUS;
			folio->mapping = mapping;
			folio->index = index + i;
		}

	return 0;
}

static void dax_disassociate_entry(void *entry, struct address_space *mapping,
		bool trunc)
{
	struct folio *folio;
	int i;

	if (IS_ENABLED(CONFIG_FS_DAX_LIMITED))
		return;

	dax_for_each_folio(entry, folio, i) {
		if (dax_mapping_is_cow(folio->mapping)) {
			/* keep the CoW flag if this folio is still shared */
			if (folio->index-- > 0)
				continue;
		} else {
			WARN_ON_ONCE(trunc && !dax_is_zapped(entry));
			WARN_ON_ONCE(trunc && !dax_folio_idle(folio));
			WARN_ON_ONCE(folio->mapping && folio->mapping != mapping);
		}
		folio->mapping = NULL;
		folio->index = 0;
	}
}

/*
 * dax_lock_page - Lock the DAX entry corresponding to a page
 * @page: The page whose entry we want to lock
 *
 * Context: Process context.
 * Return: A cookie to pass to dax_unlock_page() or 0 if the entry could
 * not be locked.
 */
dax_entry_t dax_lock_page(struct page *page)
{
	XA_STATE(xas, NULL, 0);
	void *entry;

	/* Ensure page->mapping isn't freed while we look at it */
	rcu_read_lock();
	for (;;) {
		struct address_space *mapping = READ_ONCE(page->mapping);

		entry = NULL;
		if (!mapping || !dax_mapping(mapping))
			break;

		/*
		 * In the device-dax case there's no need to lock, a
		 * struct dev_pagemap pin is sufficient to keep the
		 * inode alive, and we assume we have dev_pagemap pin
		 * otherwise we would not have a valid pfn_to_page()
		 * translation.
		 */
		entry = (void *)~0UL;
		if (S_ISCHR(mapping->host->i_mode))
			break;

		xas.xa = &mapping->i_pages;
		xas_lock_irq(&xas);
		if (mapping != page->mapping) {
			xas_unlock_irq(&xas);
			continue;
		}
		xas_set(&xas, page->index);
		entry = xas_load(&xas);
		if (dax_is_locked(entry)) {
			rcu_read_unlock();
			wait_entry_unlocked(&xas, entry);
			rcu_read_lock();
			continue;
		}
		dax_lock_entry(&xas, entry);
		xas_unlock_irq(&xas);
		break;
	}
	rcu_read_unlock();
	return (dax_entry_t)entry;
}

void dax_unlock_page(struct page *page, dax_entry_t cookie)
{
	struct address_space *mapping = page->mapping;
	XA_STATE(xas, &mapping->i_pages, page->index);

	if (S_ISCHR(mapping->host->i_mode))
		return;

	dax_unlock_entry(&xas, (void *)cookie);
}

/*
 * dax_lock_mapping_entry - Lock the DAX entry corresponding to a mapping
 * @mapping: the file's mapping whose entry we want to lock
 * @index: the offset within this file
 * @page: output the dax page corresponding to this dax entry
 *
 * Return: A cookie to pass to dax_unlock_mapping_entry() or 0 if the entry
 * could not be locked.
 */
dax_entry_t dax_lock_mapping_entry(struct address_space *mapping, pgoff_t index,
				   struct page **page)
{
	XA_STATE(xas, NULL, 0);
	void *entry;

	rcu_read_lock();
	for (;;) {
		entry = NULL;
		if (!dax_mapping(mapping))
			break;

		xas.xa = &mapping->i_pages;
		xas_lock_irq(&xas);
		xas_set(&xas, index);
		entry = xas_load(&xas);
		if (dax_is_locked(entry)) {
			rcu_read_unlock();
			wait_entry_unlocked(&xas, entry);
			rcu_read_lock();
			continue;
		}
		if (!entry || dax_is_zero_entry(entry) ||
		    dax_is_empty_entry(entry)) {
			/*
			 * Because we are looking for entry from file's mapping
			 * and index, so the entry may not be inserted for now,
			 * or even a zero/empty entry.  We don't think this is
			 * an error case.  So, return a special value and do
			 * not output @page.
			 */
			entry = (void *)~0UL;
		} else {
			*page = pfn_to_page(dax_to_pfn(entry));
			dax_lock_entry(&xas, entry);
		}
		xas_unlock_irq(&xas);
		break;
	}
	rcu_read_unlock();
	return (dax_entry_t)entry;
}

void dax_unlock_mapping_entry(struct address_space *mapping, pgoff_t index,
			      dax_entry_t cookie)
{
	XA_STATE(xas, &mapping->i_pages, index);

	if (cookie == ~0UL)
		return;

	dax_unlock_entry(&xas, (void *)cookie);
}

/*
 * Find page cache entry at given index. If it is a DAX entry, return it
 * with the entry locked. If the page cache doesn't contain an entry at
 * that index, add a locked empty entry.
 *
 * When requesting an entry with size DAX_PMD, dax_grab_mapping_entry() will
 * either return that locked entry or will return VM_FAULT_FALLBACK.
 * This will happen if there are any PTE entries within the PMD range
 * that we are requesting.
 *
 * We always favor PTE entries over PMD entries. There isn't a flow where we
 * evict PTE entries in order to 'upgrade' them to a PMD entry.  A PMD
 * insertion will fail if it finds any PTE entries already in the tree, and a
 * PTE insertion will cause an existing PMD entry to be unmapped and
 * downgraded to PTE entries.  This happens for both PMD zero pages as
 * well as PMD empty entries.
 *
 * The exception to this downgrade path is for PMD entries that have
 * real storage backing them.  We will leave these real PMD entries in
 * the tree, and PTE writes will simply dirty the entire PMD entry.
 *
 * Note: Unlike filemap_fault() we don't honor FAULT_FLAG_RETRY flags. For
 * persistent memory the benefit is doubtful. We can add that later if we can
 * show it helps.
 *
 * On error, this function does not return an ERR_PTR.  Instead it returns
 * a VM_FAULT code, encoded as an xarray internal entry.  The ERR_PTR values
 * overlap with xarray value entries.
 */
void *dax_grab_mapping_entry(struct xa_state *xas,
			     struct address_space *mapping, unsigned int order)
{
	unsigned long index = xas->xa_index;
	bool size_downgrade; /* splitting entry into PTE entries? */
	void *entry;

retry:
	size_downgrade = false;
	xas_lock_irq(xas);
	entry = get_unlocked_entry(xas, order);

	if (entry) {
		if (dax_is_conflict(entry))
			goto fallback;
		if (!xa_is_value(entry)) {
			xas_set_err(xas, -EIO);
			goto out_unlock;
		}

		if (order == 0) {
			if (!dax_is_pte_entry(entry) &&
			    (dax_is_zero_entry(entry) ||
			     dax_is_empty_entry(entry))) {
				size_downgrade = true;
			}
		}
	}

	if (size_downgrade) {
		unsigned long colour, nr;

		if (dax_is_pmd_entry(entry)) {
			colour = PG_PMD_COLOUR;
			nr = PG_PMD_NR;
		} else {
			colour = PG_PUD_COLOUR;
			nr = PG_PUD_NR;
		}

		/*
		 * Make sure 'entry' remains valid while we drop
		 * the i_pages lock.
		 */
		dax_lock_entry(xas, entry);

		/*
		 * Besides huge zero pages the only other thing that gets
		 * downgraded are empty entries which don't need to be
		 * unmapped.
		 */
		if (dax_is_zero_entry(entry)) {
			xas_unlock_irq(xas);
			unmap_mapping_pages(mapping, xas->xa_index & ~colour,
					    nr, false);
			xas_reset(xas);
			xas_lock_irq(xas);
		}

		dax_disassociate_entry(entry, mapping, false);
		xas_store(xas, NULL); /* undo the PMD join */
		dax_wake_entry(xas, entry, WAKE_ALL);
		mapping->nrpages -= nr;
		entry = NULL;
		xas_set(xas, index);
	}

	if (entry) {
		dax_lock_entry(xas, entry);
	} else {
		unsigned long flags = DAX_EMPTY;

		if (order == PUD_SHIFT - PAGE_SHIFT)
			flags |= DAX_PUD;
		else if (order == PMD_SHIFT - PAGE_SHIFT)
			flags |= DAX_PMD;
		entry = dax_make_entry(pfn_to_pfn_t(0), flags);
		dax_lock_entry(xas, entry);
		if (xas_error(xas))
			goto out_unlock;
		mapping->nrpages += 1UL << order;
	}

out_unlock:
	xas_unlock_irq(xas);
	if (xas_nomem(xas, mapping_gfp_mask(mapping) & ~__GFP_HIGHMEM))
		goto retry;
	if (xas->xa_node == XA_ERROR(-ENOMEM))
		return vmfault_to_dax_err(VM_FAULT_OOM);
	if (xas_error(xas))
		return vmfault_to_dax_err(VM_FAULT_SIGBUS);
	return entry;
fallback:
	xas_unlock_irq(xas);
	return vmfault_to_dax_err(VM_FAULT_FALLBACK);
}
EXPORT_SYMBOL_NS_GPL(dax_grab_mapping_entry, DAX);

static void *dax_zap_entry(struct xa_state *xas, void *entry)
{
	unsigned long v = xa_to_value(entry);

	return xas_store(xas, xa_mk_value(v | DAX_ZAP));
}

/*
 * Return NULL if the entry is zapped and all pages in the entry are
 * idle, otherwise return the non-idle page in the entry
 */
static struct page *dax_zap_pages(struct xa_state *xas, void *entry)
{
	struct page *ret = NULL;
	struct folio *folio;
	bool zap;
	int i;

	if (!dax_entry_size(entry))
		return NULL;

	zap = !dax_is_zapped(entry);

	dax_for_each_folio(entry, folio, i) {
		if (zap)
			folio_put(folio);
		if (!ret && !dax_folio_idle(folio))
			ret = folio_page(folio, 0);
	}

	if (zap)
		dax_zap_entry(xas, entry);

	return ret;
}

/**
 * dax_zap_mappings_range - find first pinned page in @mapping
 * @mapping: address space to scan for a page with ref count > 1
 * @start: Starting offset. Page containing 'start' is included.
 * @end: End offset. Page containing 'end' is included. If 'end' is LLONG_MAX,
 *       pages from 'start' till the end of file are included.
 *
 * DAX requires ZONE_DEVICE mapped pages. These pages are never
 * 'onlined' to the page allocator so they are considered idle when
 * page->count == 1. A filesystem uses this interface to determine if
 * any page in the mapping is busy, i.e. for DMA, or other
 * get_user_pages() usages.
 *
 * It is expected that the filesystem is holding locks to block the
 * establishment of new mappings in this address_space. I.e. it expects
 * to be able to run unmap_mapping_range() and subsequently not race
 * mapping_mapped() becoming true.
 */
struct page *dax_zap_mappings_range(struct address_space *mapping, loff_t start,
				    loff_t end)
{
	void *entry;
	unsigned int scanned = 0;
	struct page *page = NULL;
	pgoff_t start_idx = start >> PAGE_SHIFT;
	pgoff_t end_idx;
	XA_STATE(xas, &mapping->i_pages, start_idx);

	/*
	 * In the 'limited' case get_user_pages() for dax is disabled.
	 */
	if (IS_ENABLED(CONFIG_FS_DAX_LIMITED))
		return NULL;

	if (!dax_mapping(mapping))
		return NULL;

	/* If end == LLONG_MAX, all pages from start to till end of file */
	if (end == LLONG_MAX)
		end_idx = ULONG_MAX;
	else
		end_idx = end >> PAGE_SHIFT;
	/*
	 * If we race get_user_pages_fast() here either we'll see the
	 * elevated page count in the iteration and wait, or
	 * get_user_pages_fast() will see that the page it took a reference
	 * against is no longer mapped in the page tables and bail to the
	 * get_user_pages() slow path.  The slow path is protected by
	 * pte_lock() and pmd_lock(). New references are not taken without
	 * holding those locks, and unmap_mapping_pages() will not zero the
	 * pte or pmd without holding the respective lock, so we are
	 * guaranteed to either see new references or prevent new
	 * references from being established.
	 */
	unmap_mapping_pages(mapping, start_idx, end_idx - start_idx + 1, 0);

	xas_lock_irq(&xas);
	xas_for_each(&xas, entry, end_idx) {
		if (WARN_ON_ONCE(!xa_is_value(entry)))
			continue;
		if (unlikely(dax_is_locked(entry)))
			entry = get_unlocked_entry(&xas, 0);
		if (entry)
			page = dax_zap_pages(&xas, entry);
		put_unlocked_entry(&xas, entry, WAKE_NEXT);
		if (page)
			break;
		if (++scanned % XA_CHECK_SCHED)
			continue;

		xas_pause(&xas);
		xas_unlock_irq(&xas);
		cond_resched();
		xas_lock_irq(&xas);
	}
	xas_unlock_irq(&xas);
	return page;
}
EXPORT_SYMBOL_GPL(dax_zap_mappings_range);

struct page *dax_zap_mappings(struct address_space *mapping)
{
	return dax_zap_mappings_range(mapping, 0, LLONG_MAX);
}
EXPORT_SYMBOL_GPL(dax_zap_mappings);

static int __dax_invalidate_entry(struct address_space *mapping,
					  pgoff_t index, bool trunc)
{
	XA_STATE(xas, &mapping->i_pages, index);
	int ret = 0;
	void *entry;

	xas_lock_irq(&xas);
	entry = get_unlocked_entry(&xas, 0);
	if (!entry || WARN_ON_ONCE(!xa_is_value(entry)))
		goto out;
	if (!trunc && (xas_get_mark(&xas, PAGECACHE_TAG_DIRTY) ||
		       xas_get_mark(&xas, PAGECACHE_TAG_TOWRITE)))
		goto out;
	dax_disassociate_entry(entry, mapping, trunc);
	xas_store(&xas, NULL);
	mapping->nrpages -= 1UL << dax_entry_order(entry);
	ret = 1;
out:
	put_unlocked_entry(&xas, entry, WAKE_ALL);
	xas_unlock_irq(&xas);
	return ret;
}

/*
 * wait indefinitely for all pins to drop, the alternative to waiting is
 * a potential use-after-free scenario
 */
void dax_break_layouts(struct address_space *mapping, pgoff_t index,
		       pgoff_t end)
{
	struct inode *inode = mapping->host;

	/*
	 * To do this without filesystem locks, the inode needs to be
	 * unreferenced, or device-dax.
	 */
	WARN_ON(atomic_read(&inode->i_count) && !S_ISCHR(inode->i_mode));
	do {
		struct page *page;

		page = dax_zap_mappings_range(mapping, index << PAGE_SHIFT,
					      end << PAGE_SHIFT);
		if (!page)
			return;
		wait_var_event(page, dax_page_idle(page));
	} while (true);
}

/*
 * Delete DAX entry at @index from @mapping.  Wait for it
 * to be unlocked before deleting it.
 */
int dax_delete_mapping_entry(struct address_space *mapping, pgoff_t index)
{
	int ret;

	if (mapping_exiting(mapping))
		dax_break_layouts(mapping, index, index + 1);

	ret = __dax_invalidate_entry(mapping, index, true);

	/*
	 * This gets called from truncate / punch_hole path. As such, the caller
	 * must hold locks protecting against concurrent modifications of the
	 * page cache (usually fs-private i_mmap_sem for writing). Since the
	 * caller has seen a DAX entry for this index, we better find it
	 * at that index as well...
	 */
	WARN_ON_ONCE(!ret);
	return ret;
}

/*
 * Invalidate DAX entry if it is clean.
 */
int dax_invalidate_mapping_entry_sync(struct address_space *mapping,
				      pgoff_t index)
{
	return __dax_invalidate_entry(mapping, index, false);
}

/*
 * By this point grab_mapping_entry() has ensured that we have a locked entry
 * of the appropriate size so we don't have to worry about downgrading PMDs to
 * PTEs.  If we happen to be trying to insert a PTE and there is a PMD
 * already in the tree, we will skip the insertion and just dirty the PMD as
 * appropriate.
 */
vm_fault_t dax_insert_entry(struct xa_state *xas, struct vm_fault *vmf,
			    void **pentry, pfn_t pfn, unsigned long flags)
{
	struct address_space *mapping = vmf->vma->vm_file->f_mapping;
	void *new_entry = dax_make_entry(pfn, flags);
	bool dirty = flags & DAX_DIRTY;
	bool cow = flags & DAX_COW;
	void *entry = *pentry;
	vm_fault_t ret = 0;

	if (dirty)
		__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);

	if (cow || (dax_is_zero_entry(entry) && !(flags & DAX_ZERO_PAGE))) {
		unsigned long index = xas->xa_index;
		/* we are replacing a zero page with block mapping */
		if (dax_is_pud_entry(entry))
			unmap_mapping_pages(mapping, index & ~PG_PUD_COLOUR,
					    PG_PUD_NR, false);
		else if (dax_is_pmd_entry(entry))
			unmap_mapping_pages(mapping, index & ~PG_PMD_COLOUR,
					PG_PMD_NR, false);
		else /* pte entry */
			unmap_mapping_pages(mapping, index, 1, false);
	}

	xas_reset(xas);
	xas_lock_irq(xas);
	if (cow || dax_is_zero_entry(entry) || dax_is_empty_entry(entry)) {
		void *old;

		dax_disassociate_entry(entry, mapping, false);
		ret = dax_associate_entry(new_entry, mapping, vmf, flags);
		if (ret)
			goto out;
		/*
		 * Only swap our new entry into the page cache if the current
		 * entry is a zero page or an empty entry.  If a normal PTE or
		 * PMD entry is already in the cache, we leave it alone.  This
		 * means that if we are trying to insert a PTE and the
		 * existing entry is a PMD, we will just leave the PMD in the
		 * tree and dirty it if necessary.
		 */
		old = dax_lock_entry(xas, new_entry);
		WARN_ON_ONCE(old != xa_mk_value(xa_to_value(entry) |
					DAX_LOCKED));
		entry = new_entry;
	} else {
		xas_load(xas);	/* Walk the xa_state */
	}

	if (dirty)
		xas_set_mark(xas, PAGECACHE_TAG_DIRTY);

	if (cow)
		xas_set_mark(xas, PAGECACHE_TAG_TOWRITE);

	*pentry = entry;
out:
	xas_unlock_irq(xas);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(dax_insert_entry, DAX);

int dax_writeback_one(struct xa_state *xas, struct dax_device *dax_dev,
		      struct address_space *mapping, void *entry) __must_hold(xas)
{
	unsigned long pfn, index, count, end;
	long ret = 0;
	struct vm_area_struct *vma;

	/*
	 * A page got tagged dirty in DAX mapping? Something is seriously
	 * wrong.
	 */
	if (WARN_ON(!xa_is_value(entry)))
		return -EIO;

	if (unlikely(dax_is_locked(entry))) {
		void *old_entry = entry;

		entry = get_unlocked_entry(xas, 0);

		/* Entry got punched out / reallocated? */
		if (!entry || WARN_ON_ONCE(!xa_is_value(entry)))
			goto put_unlocked;
		/*
		 * Entry got reallocated elsewhere? No need to writeback.
		 * We have to compare pfns as we must not bail out due to
		 * difference in lockbit or entry type.
		 */
		if (dax_to_pfn(old_entry) != dax_to_pfn(entry))
			goto put_unlocked;
		if (WARN_ON_ONCE(dax_is_empty_entry(entry) ||
					dax_is_zero_entry(entry))) {
			ret = -EIO;
			goto put_unlocked;
		}

		/* Another fsync thread may have already done this entry */
		if (!xas_get_mark(xas, PAGECACHE_TAG_TOWRITE))
			goto put_unlocked;
	}

	/* Lock the entry to serialize with page faults */
	dax_lock_entry(xas, entry);

	/*
	 * We can clear the tag now but we have to be careful so that concurrent
	 * dax_writeback_one() calls for the same index cannot finish before we
	 * actually flush the caches. This is achieved as the calls will look
	 * at the entry only under the i_pages lock and once they do that
	 * they will see the entry locked and wait for it to unlock.
	 */
	xas_clear_mark(xas, PAGECACHE_TAG_TOWRITE);
	xas_unlock_irq(xas);

	/*
	 * If dax_writeback_mapping_range() was given a wbc->range_start
	 * in the middle of a PMD, the 'index' we use needs to be
	 * aligned to the start of the PMD.
	 * This allows us to flush for PMD_SIZE and not have to worry about
	 * partial PMD writebacks.
	 */
	pfn = dax_to_pfn(entry);
	count = 1UL << dax_entry_order(entry);
	index = xas->xa_index & ~(count - 1);
	end = index + count - 1;

	/* Walk all mappings of a given index of a file and writeprotect them */
	i_mmap_lock_read(mapping);
	vma_interval_tree_foreach(vma, &mapping->i_mmap, index, end) {
		pfn_mkclean_range(pfn, count, index, vma);
		cond_resched();
	}
	i_mmap_unlock_read(mapping);

	dax_flush(dax_dev, page_address(pfn_to_page(pfn)), count * PAGE_SIZE);
	/*
	 * After we have flushed the cache, we can clear the dirty tag. There
	 * cannot be new dirty data in the pfn after the flush has completed as
	 * the pfn mappings are writeprotected and fault waits for mapping
	 * entry lock.
	 */
	xas_reset(xas);
	xas_lock_irq(xas);
	xas_store(xas, entry);
	xas_clear_mark(xas, PAGECACHE_TAG_DIRTY);
	dax_wake_entry(xas, entry, WAKE_NEXT);

	trace_dax_writeback_one(mapping->host, index, count);
	return ret;

 put_unlocked:
	put_unlocked_entry(xas, entry, WAKE_NEXT);
	return ret;
}

/*
 * dax_insert_pfn_mkwrite - insert PTE or PMD entry into page tables
 * @vmf: The description of the fault
 * @pfn: PFN to insert
 * @order: Order of entry to insert.
 *
 * This function inserts a writeable PTE or PMD entry into the page tables
 * for an mmaped DAX file.  It also marks the page cache entry as dirty.
 */
vm_fault_t dax_insert_pfn_mkwrite(struct vm_fault *vmf, pfn_t pfn,
				  unsigned int order)
{
	struct address_space *mapping = vmf->vma->vm_file->f_mapping;
	XA_STATE_ORDER(xas, &mapping->i_pages, vmf->pgoff, order);
	void *entry;
	vm_fault_t ret;

	xas_lock_irq(&xas);
	entry = get_unlocked_entry(&xas, order);
	/* Did we race with someone splitting entry or so? */
	if (!entry || dax_is_conflict(entry) ||
	    (order == 0 && !dax_is_pte_entry(entry))) {
		put_unlocked_entry(&xas, entry, WAKE_NEXT);
		xas_unlock_irq(&xas);
		trace_dax_insert_pfn_mkwrite_no_entry(mapping->host, vmf,
						      VM_FAULT_NOPAGE);
		return VM_FAULT_NOPAGE;
	}
	xas_set_mark(&xas, PAGECACHE_TAG_DIRTY);
	dax_lock_entry(&xas, entry);
	xas_unlock_irq(&xas);
	if (order == 0)
		ret = vmf_insert_mixed_mkwrite(vmf->vma, vmf->address, pfn);
#ifdef CONFIG_FS_DAX_PMD
	else if (order == PMD_ORDER)
		ret = vmf_insert_pfn_pmd(vmf, pfn, FAULT_FLAG_WRITE);
#endif
	else if (order == PUD_ORDER)
		ret = vmf_insert_pfn_pud(vmf, pfn, FAULT_FLAG_WRITE);
	else
		ret = VM_FAULT_FALLBACK;
	dax_unlock_entry(&xas, entry);
	trace_dax_insert_pfn_mkwrite(mapping->host, vmf, ret);
	return ret;
}
