// SPDX-License-Identifier: GPL-2.0

#include <linux/objpool.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/atomic.h>
#include <linux/prefetch.h>

/*
 * objpool: ring-array based lockless MPMC/FIFO queues
 *
 * Copyright: wuqiang.matt@bytedance.com
 */

/* compute the suitable num of objects to be managed by slot */
static inline unsigned int __objpool_num_of_objs(unsigned int size)
{
	return rounddown_pow_of_two((size - sizeof(struct objpool_slot)) /
			(sizeof(uint32_t) + sizeof(void *)));
}

#define SLOT_AGES(s) ((uint32_t *)((char *)(s) + sizeof(struct objpool_slot)))
#define SLOT_ENTS(s) ((void **)((char *)(s) + sizeof(struct objpool_slot) + \
			sizeof(uint32_t) * (s)->size))
#define SLOT_OBJS(s) ((void *)((char *)(s) + sizeof(struct objpool_slot) + \
			(sizeof(uint32_t) + sizeof(void *)) * (s)->size))

/* allocate and initialize percpu slots */
static inline int
__objpool_init_percpu_slots(struct objpool_head *head, unsigned int nobjs,
			void *context, objpool_init_obj_cb objinit)
{
	unsigned int i, j, n, size, objsz, nents = head->capacity;

	/* aligned object size by sizeof(void *) */
	objsz = ALIGN(head->obj_size, sizeof(void *));
	/* shall we allocate objects along with objpool_slot */
	if (objsz)
		head->flags |= OBJPOOL_HAVE_OBJECTS;

	for (i = 0; i < head->nr_cpus; i++) {
		struct objpool_slot *os;

		/* compute how many objects to be managed by this slot */
		n = nobjs / head->nr_cpus;
		if (i < (nobjs % head->nr_cpus))
			n++;
		size = sizeof(struct objpool_slot) + sizeof(void *) * nents +
		       sizeof(uint32_t) * nents + objsz * n;

		/* decide memory area for cpu-slot allocation */
		if (!i && !(head->gfp & GFP_ATOMIC) && size > PAGE_SIZE / 2)
			head->flags |= OBJPOOL_FROM_VMALLOC;

		/* allocate percpu slot & objects from local memory */
		if (head->flags & OBJPOOL_FROM_VMALLOC)
			os = __vmalloc_node(size, sizeof(void *), head->gfp,
				cpu_to_node(i), __builtin_return_address(0));
		else
			os = kmalloc_node(size, head->gfp, cpu_to_node(i));
		if (!os)
			return -ENOMEM;

		/* initialize percpu slot for the i-th cpu */
		memset(os, 0, size);
		os->size = head->capacity;
		os->mask = os->size - 1;
		head->cpu_slots[i] = os;
		head->slot_sizes[i] = size;

		/*
		 * start from 2nd round to avoid conflict of 1st item.
		 * we assume that the head item is ready for retrieval
		 * iff head is equal to ages[head & mask]. but ages is
		 * initialized as 0, so in view of the caller of pop(),
		 * the 1st item (0th) is always ready, but fact could
		 * be: push() is stalled before the final update, thus
		 * the item being inserted will be lost forever.
		 */
		os->head = os->tail = head->capacity;

		if (!objsz)
			continue;

		for (j = 0; j < n; j++) {
			uint32_t *ages = SLOT_AGES(os);
			void **ents = SLOT_ENTS(os);
			void *obj = SLOT_OBJS(os) + j * objsz;
			uint32_t ie = os->tail & os->mask;

			/* perform object initialization */
			if (objinit) {
				int rc = objinit(context, obj);
				if (rc)
					return rc;
			}

			/* add obj into the ring array */
			ents[ie] = obj;
			ages[ie] = os->tail;
			os->tail++;
			head->nr_objs++;
		}
	}

	return 0;
}

/* cleanup all percpu slots of the object pool */
static inline void __objpool_fini_percpu_slots(struct objpool_head *head)
{
	unsigned int i;

	if (!head->cpu_slots)
		return;

	for (i = 0; i < head->nr_cpus; i++) {
		if (!head->cpu_slots[i])
			continue;
		if (head->flags & OBJPOOL_FROM_VMALLOC)
			vfree(head->cpu_slots[i]);
		else
			kfree(head->cpu_slots[i]);
	}
	kfree(head->cpu_slots);
	head->cpu_slots = NULL;
	head->slot_sizes = NULL;
}

/**
 * objpool_init: initialize object pool and pre-allocate objects
 *
 * args:
 * @head:    the object pool to be initialized, declared by caller
 * @nr_objs: total objects to be pre-allocated by this object pool
 * @max_objs: max entries (object pool capacity), use nr_objs if 0
 * @object_size: size of an object, no objects pre-allocated if 0
 * @gfp:     flags for memory allocation (via kmalloc or vmalloc)
 * @context: user context for object initialization callback
 * @objinit: object initialization callback for extra setting-up
 * @release: cleanup callback for private objects/pool/context
 *
 * return:
 *         0 for success, otherwise error code
 *
 * All pre-allocated objects are to be zeroed. Caller could do extra
 * initialization in objinit callback. The objinit callback will be
 * called once and only once after the slot allocation. Then objpool
 * won't touch any content of the objects since then. It's caller's
 * duty to perform reinitialization after object allocation (pop) or
 * clearance before object reclamation (push) if required.
 */
int objpool_init(struct objpool_head *head, unsigned int nr_objs,
		unsigned int max_objs, unsigned int object_size,
		gfp_t gfp, void *context, objpool_init_obj_cb objinit,
		objpool_release_cb release)
{
	unsigned int nents, ncpus = num_possible_cpus();
	int rc;

	/* calculate percpu slot size (rounded to pow of 2) */
	if (max_objs < nr_objs)
		max_objs = nr_objs;
	nents = max_objs / ncpus;
	if (nents < __objpool_num_of_objs(L1_CACHE_BYTES))
		nents = __objpool_num_of_objs(L1_CACHE_BYTES);
	nents = roundup_pow_of_two(nents);
	while (nents * ncpus < nr_objs)
		nents = nents << 1;

	memset(head, 0, sizeof(struct objpool_head));
	head->nr_cpus = ncpus;
	head->obj_size = object_size;
	head->capacity = nents;
	head->gfp = gfp & ~__GFP_ZERO;
	head->context = context;
	head->release = release;

	/* allocate array for percpu slots */
	head->cpu_slots = kzalloc(head->nr_cpus * sizeof(void *) +
			       head->nr_cpus * sizeof(uint32_t), head->gfp);
	if (!head->cpu_slots)
		return -ENOMEM;
	head->slot_sizes = (uint32_t *)&head->cpu_slots[head->nr_cpus];

	/* initialize per-cpu slots */
	rc = __objpool_init_percpu_slots(head, nr_objs, context, objinit);
	if (rc)
		__objpool_fini_percpu_slots(head);

	return rc;
}
EXPORT_SYMBOL_GPL(objpool_init);

/* adding object to slot tail, the given slot must NOT be full */
static inline int __objpool_add_slot(void *obj, struct objpool_slot *os)
{
	uint32_t *ages = SLOT_AGES(os);
	void **ents = SLOT_ENTS(os);
	uint32_t tail = atomic_inc_return((atomic_t *)&os->tail) - 1;

	WRITE_ONCE(ents[tail & os->mask], obj);

	/* order matters: obj must be updated before tail updating */
	smp_store_release(&ages[tail & os->mask], tail);
	return 0;
}

/* adding object to slot, abort if the slot was already full */
static inline int __objpool_try_add_slot(void *obj, struct objpool_slot *os)
{
	uint32_t *ages = SLOT_AGES(os);
	void **ents = SLOT_ENTS(os);
	uint32_t head, tail;

	do {
		/* perform memory loading for both head and tail */
		head = READ_ONCE(os->head);
		tail = READ_ONCE(os->tail);
		/* just abort if slot is full */
		if (tail >= head + os->size)
			return -ENOENT;
		/* try to extend tail by 1 using CAS to avoid races */
		if (try_cmpxchg_acquire(&os->tail, &tail, tail + 1))
			break;
	} while (1);

	/* the tail-th of slot is reserved for the given obj */
	WRITE_ONCE(ents[tail & os->mask], obj);
	/* update epoch id to make this object available for pop() */
	smp_store_release(&ages[tail & os->mask], tail);
	return 0;
}

/**
 * objpool_populate: add objects from user provided pool in batch
 *
 * args:
 * @head:  object pool
 * @pool: user buffer for pre-allocated objects
 * @size: size of user buffer
 * @object_size: size of object & element
 * @context: user context for objinit callback
 * @objinit: object initialization callback
 *
 * return: 0 or error code
 */
int objpool_populate(struct objpool_head *head, void *pool,
		unsigned int size, unsigned int object_size,
		void *context, objpool_init_obj_cb objinit)
{
	unsigned int n = head->nr_objs, used = 0, i;

	if (head->pool || !pool || size < object_size)
		return -EINVAL;
	if (head->obj_size && head->obj_size != object_size)
		return -EINVAL;
	if (head->context && context && head->context != context)
		return -EINVAL;
	if (head->nr_objs >= head->nr_cpus * head->capacity)
		return -ENOENT;

	WARN_ON_ONCE(((unsigned long)pool) & (sizeof(void *) - 1));
	WARN_ON_ONCE(((uint32_t)object_size) & (sizeof(void *) - 1));

	/* align object size by sizeof(void *) */
	head->obj_size = object_size;
	object_size = ALIGN(object_size, sizeof(void *));
	if (object_size == 0)
		return -EINVAL;

	while (used + object_size <= size) {
		void *obj = pool + used;

		/* perform object initialization */
		if (objinit) {
			int rc = objinit(context, obj);
			if (rc)
				return rc;
		}

		/* insert obj to its corresponding objpool slot */
		i = (n + used * head->nr_cpus/size) % head->nr_cpus;
		if (!__objpool_try_add_slot(obj, head->cpu_slots[i]))
			head->nr_objs++;

		used += object_size;
	}

	if (!used)
		return -ENOENT;

	head->context = context;
	head->pool = pool;
	head->pool_size = size;

	return 0;
}
EXPORT_SYMBOL_GPL(objpool_populate);

/**
 * objpool_add: add pre-allocated object to objpool during pool
 * initialization
 *
 * args:
 * @obj:  object pointer to be added to objpool
 * @head: object pool to be inserted into
 *
 * return:
 *     0 or error code
 *
 * objpool_add_node doesn't handle race conditions, can only be
 * called during objpool initialization
 */
int objpool_add(void *obj, struct objpool_head *head)
{
	unsigned int i, cpu;

	if (!obj)
		return -EINVAL;
	if (head->nr_objs >= head->nr_cpus * head->capacity)
		return -ENOENT;

	cpu = head->nr_objs % head->nr_cpus;
	for (i = 0; i < head->nr_cpus; i++) {
		if (!__objpool_try_add_slot(obj, head->cpu_slots[cpu])) {
			head->nr_objs++;
			return 0;
		}

		if (++cpu >= head->nr_cpus)
			cpu = 0;
	}

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(objpool_add);

/**
 * objpool_push: reclaim the object and return back to objects pool
 *
 * args:
 * @obj:  object pointer to be pushed to object pool
 * @head: object pool
 *
 * return:
 *     0 or error code: it fails only when objects pool are full
 *
 * objpool_push is non-blockable, and can be nested
 */
int objpool_push(void *obj, struct objpool_head *head)
{
	unsigned int cpu = raw_smp_processor_id() % head->nr_cpus;

	do {
		if (head->nr_objs > head->capacity) {
			if (!__objpool_try_add_slot(obj, head->cpu_slots[cpu]))
				return 0;
		} else {
			if (!__objpool_add_slot(obj, head->cpu_slots[cpu]))
				return 0;
		}
		if (++cpu >= head->nr_cpus)
			cpu = 0;
	} while (1);

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(objpool_push);

/* try to retrieve object from slot */
static inline void *__objpool_try_get_slot(struct objpool_slot *os)
{
	uint32_t *ages = SLOT_AGES(os);
	void **ents = SLOT_ENTS(os);
	/* do memory load of head to local head */
	uint32_t head = smp_load_acquire(&os->head);

	/* loop if slot isn't empty */
	while (head != READ_ONCE(os->tail)) {
		uint32_t id = head & os->mask, prev = head;

		/* do prefetching of object ents */
		prefetch(&ents[id]);

		/*
		 * check whether this item was ready for retrieval ? There's
		 * possibility * in theory * we might retrieve wrong object,
		 * in case ages[id] overflows when current task is sleeping,
		 * but it will take very very long to overflow an uint32_t
		 */
		if (smp_load_acquire(&ages[id]) == head) {
			/* node must have been udpated by push() */
			void *node = READ_ONCE(ents[id]);
			/* commit and move forward head of the slot */
			if (try_cmpxchg_release(&os->head, &head, head + 1))
				return node;
		}

		/* re-load head from memory continue trying */
		head = READ_ONCE(os->head);
		/*
		 * head stays unchanged, so it's very likely current pop()
		 * just preempted/interrupted an ongoing push() operation
		 */
		if (head == prev)
			break;
	}

	return NULL;
}

/**
 * objpool_pop: allocate an object from objects pool
 *
 * args:
 * @head:  object pool used to allocate an object
 *
 * return:
 *   object: NULL if failed (object pool is empty)
 *
 * objpool_pop can be nested, so can be used in any context.
 */
void *objpool_pop(struct objpool_head *head)
{
	unsigned int i, cpu;
	void *obj = NULL;

	cpu = raw_smp_processor_id() % head->nr_cpus;
	for (i = 0; i < head->nr_cpus; i++) {
		struct objpool_slot *slot = head->cpu_slots[cpu];
		obj = __objpool_try_get_slot(slot);
		if (obj)
			break;
		if (++cpu >= head->nr_cpus)
			cpu = 0;
	}

	return obj;
}
EXPORT_SYMBOL_GPL(objpool_pop);

/**
 * objpool_fini: cleanup the whole object pool (releasing all objects)
 *
 * args:
 * @head: object pool to be released
 *
 */
void objpool_fini(struct objpool_head *head)
{
	uint32_t i, flags;

	if (!head->cpu_slots)
		return;

	if (!head->release) {
		__objpool_fini_percpu_slots(head);
		return;
	}

	/* cleanup all objects remained in objpool */
	for (i = 0; i < head->nr_cpus; i++) {
		void *obj;
		do {
			flags = OBJPOOL_FLAG_NODE;
			obj = __objpool_try_get_slot(head->cpu_slots[i]);
			if (!obj)
				break;
			if (!objpool_is_inpool(obj, head) &&
			    !objpool_is_inslot(obj, head)) {
				flags |= OBJPOOL_FLAG_USER;
			}
			head->release(head->context, obj, flags);
		} while (obj);
	}

	/* release percpu slots */
	__objpool_fini_percpu_slots(head);

	/* cleanup user private pool and related context */
	flags = OBJPOOL_FLAG_POOL;
	if (head->pool)
		flags |= OBJPOOL_FLAG_USER;
	head->release(head->context, head->pool, flags);
}
EXPORT_SYMBOL_GPL(objpool_fini);
