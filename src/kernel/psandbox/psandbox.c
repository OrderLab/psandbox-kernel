//
// The PSandbox project
//
// Created by yigonghu on 2/18/21.
//
// Copyright (c) 2021, Johns Hopkins University - Order Lab
//
//      All rights reserved.
//      Licensed under the Apache License, Version 2.0 (the "License");

#include <linux/psandbox/psandbox.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/time.h>
#include "linux/psandbox/hashmap.h"
#include "linux/psandbox/linked_list.h"
#include <linux/mutex.h>


HashMap keys_map;

typedef struct object {
	int avg_delay;
	int max_delay;
	int count;
} Object;

/* This function will create a psandbox and bind to the current thread */
SYSCALL_DEFINE1(create_psandbox, int, rule)
{
	PSandbox *psandbox;
	psandbox = (PSandbox *)kmalloc(sizeof(PSandbox), GFP_KERNEL);
	if (!psandbox) {
		return -1;
	}
	psandbox->current_task = current;
	current->psandbox = psandbox;
	psandbox->activity = (Activity *)kzalloc(sizeof(Activity), GFP_KERNEL);
	psandbox->state = BOX_START;
	psandbox->delay_ratio = rule;
//	if(keys_map.table_size == 0) {
//		if (0 != hashmap_create(8, &keys_map)) {
//			return -1;
//		}
//	}

	printk(KERN_INFO "psandbox syscall called psandbox_create id =%d\n",
	       current->pid);
	return psandbox->current_task->pid;
}

SYSCALL_DEFINE1(release_psandbox, int, bid)
{
	struct task_struct *task = find_get_task_by_vpid(bid);
	if (!task) {
		printk(KERN_INFO "can't find sandbox based on the id\n");
		return -1;
	}
	if (!task->psandbox) {
		printk(KERN_INFO "there is no psandbox\n");
		return 0;
	}

	kfree(task->psandbox->activity);
	kfree(task->psandbox);
	printk(KERN_INFO "psandbox syscall called psandbox_release id =%d\n",
	       current->pid);
	return 0;
}

//TODO: use a hashmap in futex.c to store uaddr
SYSCALL_DEFINE4(update_psandbox, int, bid, enum enum_event_type, action, int,
		arg ,u32 __user *, uaddr)
{
	struct task_struct *task = find_get_task_by_vpid(bid);
	PSandbox *psandbox;
	int success = 0;

	if (!task || !task->psandbox) {
		printk(KERN_INFO "can't find sandbox based on the id\n");
		return -1;
	}
	psandbox = task->psandbox;

	if (psandbox->state == BOX_FREEZE)
		return 0;

	switch (action) {
	case TRY_QUEUE: {

			if (task_is_stopped(task))
				return 0;
			smp_store_mb(task->state, (TASK_INTERRUPTIBLE));
			schedule();
			return 1;
	}
	case ENTER_QUEUE: {
//		Object *o = hashmap_get(keys_map,uaddr);
//		if(o) {
//			o->avg_delay = 0;
//			o->max_delay = 0;
//			hashmap_put(&keys_map, uaddr, o);
//		}
		break;
	}
	default:
		break;
	}
	return success;
}

//SYSCALL_DEFINE2(schedule_psandbox, u32 __user *, key, LinkedList __user *, competitors) {
//	Object* o = hashmap_get(keys_map,key);
//	struct linkedlist_element_s* node;
//	ktime_t penalty_ns = 1000000;
//	for (node = competitors->head; node != NULL; node = node->next) {
//		PSandbox* competitor_sandbox = (PSandbox *)(node->data);
//		long defer_tm = list_size(competitors) * o->avg_delay / 2 + timespec64_to_ktime(competitor_sandbox->activity->defer_time);
//		if (defer_tm > competitor_sandbox->delay_ratio * list_size(competitors)) {
//			wake_up_process(competitor_sandbox->current_task);
//			set_current_state(TASK_INTERRUPTIBLE);
//			schedule_hrtimeout(penalty_ns, HRTIMER_MODE_REL);
//			break;
//		}
//	}
//
//}

SYSCALL_DEFINE1(wakeup_psandbox, int, bid)
{
	struct task_struct *task = find_get_task_by_vpid(bid);
	PSandbox *psandbox;

	if (!task || !task->psandbox || !task->psandbox->activity) {
		printk(KERN_INFO "can't find sandbox based on the id %d\n",bid);
		return -1;
	}
	printk(KERN_INFO
	       "psandbox syscall called psandbox_wakeup pid =%d \n",
	       task->pid);
	psandbox = task->psandbox;
	psandbox->state = BOX_AWAKE;
	wake_up_process(task);

	return 0;
}

SYSCALL_DEFINE1(penalize_psandbox, int, penalty_ns)
{
	ktime_t penalty = penalty_ns;
	printk(KERN_INFO
	       "psandbox syscall called penalize_psandbox pid =%d \n",
	       current->pid);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_hrtimeout(&penalty, HRTIMER_MODE_REL);
	return 0;
}

SYSCALL_DEFINE0(get_psandbox)
{
	if (!current->psandbox) {
		printk(KERN_INFO "there is no psandbox in current thread\n");
		return -1;
	}
	return current->pid;
}

SYSCALL_DEFINE0(destroy_psandbox)
{
	return 0;
}