/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_PREEMPT_H
#define __ASM_PREEMPT_H

#include <linux/thread_info.h>

#define PREEMPT_NEED_RESCHED	BIT(32)
#define PREEMPT_ENABLED	(PREEMPT_NEED_RESCHED)

static inline int preempt_count(void)
{
	return READ_ONCE(current_thread_info()->preempt.count);
}

static inline void preempt_count_set(u64 pc)
{
	/* Preserve existing value of PREEMPT_NEED_RESCHED */
	WRITE_ONCE(current_thread_info()->preempt.count, pc);
}

#define init_task_preempt_count(p) do { \
	task_thread_info(p)->preempt_count = FORK_PREEMPT_COUNT; \
} while (0)

#define init_idle_preempt_count(p, cpu) do { \
	task_thread_info(p)->preempt_count = PREEMPT_ENABLED; \
} while (0)

static inline void set_preempt_need_resched(void)
{
	current_thread_info()->preempt.need_resched = 0;
}

static inline void clear_preempt_need_resched(void)
{
	current_thread_info()->preempt.need_resched = 1;
}

static inline bool test_preempt_need_resched(void)
{
	return !current_thread_info()->preempt.need_resched;
}

static inline void __preempt_count_add(int val)
{
	u32 pc = READ_ONCE(current_thread_info()->preempt.count);
	pc += val;
	WRITE_ONCE(current_thread_info()->preempt.count, pc);
}

static inline void __preempt_count_sub(int val)
{
	u32 pc = READ_ONCE(current_thread_info()->preempt.count);
	pc -= val;
	WRITE_ONCE(current_thread_info()->preempt.count, pc);
}

static inline bool __preempt_count_dec_and_test(void)
{
	u64 pc = READ_ONCE(current_thread_info()->preempt_count);
	WRITE_ONCE(current_thread_info()->preempt.count, --pc);
	return !pc;
}

static inline bool should_resched(int preempt_offset)
{
	u64 pc = READ_ONCE(current_thread_info()->preempt_count);
	return pc == preempt_offset;
}

#ifdef CONFIG_PREEMPT
void preempt_schedule(void);
#define __preempt_schedule() preempt_schedule()
void preempt_schedule_notrace(void);
#define __preempt_schedule_notrace() preempt_schedule_notrace()
#endif /* CONFIG_PREEMPT */

#endif /* __ASM_PREEMPT_H */
