/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DAX_H
#define _LINUX_DAX_H

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/radix-tree.h>

typedef unsigned long dax_entry_t;

struct dax_device;
struct gendisk;
struct iomap_ops;
struct iomap_iter;
struct iomap;

enum dax_access_mode {
	DAX_ACCESS,
	DAX_RECOVERY_WRITE,
};

struct dax_operations {
	/*
	 * direct_access: translate a device-relative
	 * logical-page-offset into an absolute physical pfn. Return the
	 * number of pages available for DAX at that pfn.
	 */
	long (*direct_access)(struct dax_device *, pgoff_t, long,
			enum dax_access_mode, void **, pfn_t *);
	/*
	 * Validate whether this device is usable as an fsdax backing
	 * device.
	 */
	bool (*dax_supported)(struct dax_device *, struct block_device *, int,
			sector_t, sector_t);
	/* zero_page_range: required operation. Zero page range   */
	int (*zero_page_range)(struct dax_device *, pgoff_t, size_t);
	/*
	 * recovery_write: recover a poisoned range by DAX device driver
	 * capable of clearing poison.
	 */
	size_t (*recovery_write)(struct dax_device *dax_dev, pgoff_t pgoff,
			void *addr, size_t bytes, struct iov_iter *iter);
};

struct dax_holder_operations {
	/*
	 * notify_failure - notify memory failure into inner holder device
	 * @dax_dev: the dax device which contains the holder
	 * @offset: offset on this dax device where memory failure occurs
	 * @len: length of this memory failure event
	 * @flags: action flags for memory failure handler
	 */
	int (*notify_failure)(struct dax_device *dax_dev, u64 offset,
			u64 len, int mf_flags);
};

#if IS_ENABLED(CONFIG_DAX)
struct dax_device *alloc_dax(void *private, const struct dax_operations *ops);
void *dax_holder(struct dax_device *dax_dev);
void put_dax(struct dax_device *dax_dev);
void kill_dax(struct dax_device *dax_dev);
void dax_write_cache(struct dax_device *dax_dev, bool wc);
bool dax_write_cache_enabled(struct dax_device *dax_dev);
bool dax_synchronous(struct dax_device *dax_dev);
void set_dax_synchronous(struct dax_device *dax_dev);
size_t dax_recovery_write(struct dax_device *dax_dev, pgoff_t pgoff,
		void *addr, size_t bytes, struct iov_iter *i);
/*
 * Check if given mapping is supported by the file / underlying device.
 */
static inline bool daxdev_mapping_supported(struct vm_area_struct *vma,
					     struct dax_device *dax_dev)
{
	if (!(vma->vm_flags & VM_SYNC))
		return true;
	if (!IS_DAX(file_inode(vma->vm_file)))
		return false;
	return dax_synchronous(dax_dev);
}
#else
static inline void *dax_holder(struct dax_device *dax_dev)
{
	return NULL;
}
static inline struct dax_device *alloc_dax(void *private,
		const struct dax_operations *ops)
{
	/*
	 * Callers should check IS_ENABLED(CONFIG_DAX) to know if this
	 * NULL is an error or expected.
	 */
	return NULL;
}
static inline void put_dax(struct dax_device *dax_dev)
{
}
static inline void kill_dax(struct dax_device *dax_dev)
{
}
static inline void dax_write_cache(struct dax_device *dax_dev, bool wc)
{
}
static inline bool dax_write_cache_enabled(struct dax_device *dax_dev)
{
	return false;
}
static inline bool dax_synchronous(struct dax_device *dax_dev)
{
	return true;
}
static inline void set_dax_synchronous(struct dax_device *dax_dev)
{
}
static inline bool daxdev_mapping_supported(struct vm_area_struct *vma,
				struct dax_device *dax_dev)
{
	return !(vma->vm_flags & VM_SYNC);
}
static inline size_t dax_recovery_write(struct dax_device *dax_dev,
		pgoff_t pgoff, void *addr, size_t bytes, struct iov_iter *i)
{
	return 0;
}
#endif

void set_dax_nocache(struct dax_device *dax_dev);
void set_dax_nomc(struct dax_device *dax_dev);

struct writeback_control;
#if defined(CONFIG_BLOCK) && defined(CONFIG_FS_DAX)
int dax_add_host(struct dax_device *dax_dev, struct gendisk *disk);
void dax_remove_host(struct gendisk *disk);
struct dax_device *fs_dax_get_by_bdev(struct block_device *bdev, u64 *start_off,
		void *holder, const struct dax_holder_operations *ops);
void fs_put_dax(struct dax_device *dax_dev, void *holder);
#else
static inline int dax_add_host(struct dax_device *dax_dev, struct gendisk *disk)
{
	return 0;
}
static inline void dax_remove_host(struct gendisk *disk)
{
}
static inline struct dax_device *fs_dax_get_by_bdev(struct block_device *bdev,
		u64 *start_off, void *holder,
		const struct dax_holder_operations *ops)
{
	return NULL;
}
static inline void fs_put_dax(struct dax_device *dax_dev, void *holder)
{
}
#endif /* CONFIG_BLOCK && CONFIG_FS_DAX */

#if IS_ENABLED(CONFIG_FS_DAX)
int dax_writeback_mapping_range(struct address_space *mapping,
		struct dax_device *dax_dev, struct writeback_control *wbc);

#else
static inline int dax_writeback_mapping_range(struct address_space *mapping,
		struct dax_device *dax_dev, struct writeback_control *wbc)
{
	return -EOPNOTSUPP;
}

#endif

int dax_zero_range(struct inode *inode, loff_t pos, loff_t len, bool *did_zero,
		const struct iomap_ops *ops);
int dax_truncate_page(struct inode *inode, loff_t pos, bool *did_zero,
		const struct iomap_ops *ops);

#if IS_ENABLED(CONFIG_DAX)
int dax_read_lock(void);
void dax_read_unlock(int id);
dax_entry_t dax_lock_page(struct page *page);
void dax_unlock_page(struct page *page, dax_entry_t cookie);
void run_dax(struct dax_device *dax_dev);
dax_entry_t dax_lock_mapping_entry(struct address_space *mapping,
		unsigned long index, struct page **page);
void dax_unlock_mapping_entry(struct address_space *mapping,
		unsigned long index, dax_entry_t cookie);
struct page *dax_zap_mappings(struct address_space *mapping);
struct page *dax_zap_mappings_range(struct address_space *mapping, loff_t start,
				    loff_t end);
#else
static inline struct page *dax_zap_mappings(struct address_space *mapping)
{
	return NULL;
}

static inline struct page *dax_zap_mappings_range(struct address_space *mapping,
						  pgoff_t start,
						  pgoff_t nr_pages)
{
	return NULL;
}

static inline dax_entry_t dax_lock_page(struct page *page)
{
	if (IS_DAX(page->mapping->host))
		return ~0UL;
	return 0;
}

static inline void dax_unlock_page(struct page *page, dax_entry_t cookie)
{
}

static inline int dax_read_lock(void)
{
	return 0;
}

static inline void dax_read_unlock(int id)
{
}

static inline dax_entry_t dax_lock_mapping_entry(struct address_space *mapping,
		unsigned long index, struct page **page)
{
	return 0;
}

static inline void dax_unlock_mapping_entry(struct address_space *mapping,
		unsigned long index, dax_entry_t cookie)
{
}
#endif

/*
 * Document all the code locations that want know when a dax page is
 * unreferenced.
 */
static inline bool dax_page_idle(struct page *page)
{
	return page_ref_count(page) == 0;
}

static inline bool dax_folio_idle(struct folio *folio)
{
	return dax_page_idle(folio_page(folio, 0));
}

bool dax_alive(struct dax_device *dax_dev);
void *dax_get_private(struct dax_device *dax_dev);
long dax_direct_access(struct dax_device *dax_dev, pgoff_t pgoff, long nr_pages,
		enum dax_access_mode mode, void **kaddr, pfn_t *pfn);
size_t dax_copy_from_iter(struct dax_device *dax_dev, pgoff_t pgoff, void *addr,
		size_t bytes, struct iov_iter *i);
size_t dax_copy_to_iter(struct dax_device *dax_dev, pgoff_t pgoff, void *addr,
		size_t bytes, struct iov_iter *i);
int dax_zero_page_range(struct dax_device *dax_dev, pgoff_t pgoff,
			size_t nr_pages);
int dax_holder_notify_failure(struct dax_device *dax_dev, u64 off, u64 len,
		int mf_flags);
void dax_flush(struct dax_device *dax_dev, void *addr, size_t size);

ssize_t dax_iomap_rw(struct kiocb *iocb, struct iov_iter *iter,
		const struct iomap_ops *ops);
vm_fault_t dax_iomap_fault(struct vm_fault *vmf, enum page_entry_size pe_size,
		    pfn_t *pfnp, int *errp, const struct iomap_ops *ops);
vm_fault_t dax_finish_sync_fault(struct vm_fault *vmf,
		enum page_entry_size pe_size, pfn_t pfn);

static inline bool is_dax_err(void *entry)
{
	return xa_is_internal(entry);
}

static inline vm_fault_t dax_err_to_vmfault(void *entry)
{
	return (vm_fault_t __force)(xa_to_internal(entry));
}

static inline void *vmfault_to_dax_err(vm_fault_t error)
{
	return xa_mk_internal((unsigned long __force)error);
}

void *dax_grab_mapping_entry(struct xa_state *xas,
			     struct address_space *mapping, unsigned int order);
void dax_unlock_entry(struct xa_state *xas, void *entry);
int dax_delete_mapping_entry(struct address_space *mapping, pgoff_t index);
int dax_invalidate_mapping_entry_sync(struct address_space *mapping,
				      pgoff_t index);
void dax_break_layouts(struct address_space *mapping, pgoff_t index,
		       pgoff_t end);
int dax_dedupe_file_range_compare(struct inode *src, loff_t srcoff,
				  struct inode *dest, loff_t destoff,
				  loff_t len, bool *is_same,
				  const struct iomap_ops *ops);
int dax_remap_file_range_prep(struct file *file_in, loff_t pos_in,
			      struct file *file_out, loff_t pos_out,
			      loff_t *len, unsigned int remap_flags,
			      const struct iomap_ops *ops);
static inline bool dax_mapping(struct address_space *mapping)
{
	return mapping->host && IS_DAX(mapping->host);
}

/*
 * DAX pagecache entries use XArray value entries so they can't be
 * mistaken for pages.  We use one bit for locking, two bits for the
 * entry size (PMD, PUD) and two more to tell us if the entry is a zero
 * page or an empty entry that is just used for locking.  In total 5
 * special bits which limits the max pfn that can be stored as:
 * (1UL << 57 - PAGE_SHIFT). 63 - DAX_SHIFT - 1 (for xa_mk_value()).
 *
 * If the P{M,U}D bits are not set the entry has size PAGE_SIZE, and if
 * the ZERO_PAGE and EMPTY bits aren't set the entry is a normal DAX
 * entry with a filesystem block allocation.
 */
#define DAX_SHIFT	(6)
#define DAX_MASK	((1UL << DAX_SHIFT) - 1)
#define DAX_LOCKED	(1UL << 0)
#define DAX_PMD		(1UL << 1)
#define DAX_PUD		(1UL << 2)
#define DAX_ZERO_PAGE	(1UL << 3)
#define DAX_EMPTY	(1UL << 4)
#define DAX_ZAP		(1UL << 5)

/*
 * These flags are not conveyed in Xarray value entries, they are just
 * modifiers to dax_insert_entry().
 */
#define DAX_DIRTY (1UL << (DAX_SHIFT + 0))
#define DAX_COW   (1UL << (DAX_SHIFT + 1))

vm_fault_t dax_insert_entry(struct xa_state *xas, struct vm_fault *vmf,
			    void **pentry, pfn_t pfn, unsigned long flags);
vm_fault_t dax_insert_pfn_mkwrite(struct vm_fault *vmf, pfn_t pfn,
				  unsigned int order);
int dax_writeback_one(struct xa_state *xas, struct dax_device *dax_dev,
		      struct address_space *mapping, void *entry);

#ifdef CONFIG_MMU
/* The 'colour' (ie low bits) within a PMD of a page offset.  */
#define PG_PMD_COLOUR ((PMD_SIZE >> PAGE_SHIFT) - 1)
#define PG_PMD_NR (PMD_SIZE >> PAGE_SHIFT)

/* The order of a PMD entry */
#define PMD_ORDER (PMD_SHIFT - PAGE_SHIFT)

/* The 'colour' (ie low bits) within a PUD of a page offset.  */
#define PG_PUD_COLOUR ((PUD_SIZE >> PAGE_SHIFT) - 1)
#define PG_PUD_NR (PUD_SIZE >> PAGE_SHIFT)

/* The order of a PUD entry */
#define PUD_ORDER (PUD_SHIFT - PAGE_SHIFT)

static inline unsigned int pe_order(enum page_entry_size pe_size)
{
	if (pe_size == PE_SIZE_PTE)
		return PAGE_SHIFT - PAGE_SHIFT;
	if (pe_size == PE_SIZE_PMD)
		return PMD_SHIFT - PAGE_SHIFT;
	if (pe_size == PE_SIZE_PUD)
		return PUD_SHIFT - PAGE_SHIFT;
	return ~0;
}

#ifdef CONFIG_DEV_DAX_HMEM_DEVICES
void hmem_register_device(int target_nid, struct resource *r);
#else
static inline void hmem_register_device(int target_nid, struct resource *r)
{
}
#endif
#endif /* CONFIG_MMU */

#endif
