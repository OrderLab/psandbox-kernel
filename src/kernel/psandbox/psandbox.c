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
	case WAKEUP_QUEUE: {
		time_t current_tm, delaying_start_tm, execution_start_tm,
			executing_time, delayed_time;
		struct timespec64 current_time;
		int competitors_num = arg;
		ktime_get_real_ts64(&current_time);
		current_tm = timespec64_to_ktime(current_time);
		delaying_start_tm =
			timespec64_to_ktime(psandbox->activity->delaying_start);
		execution_start_tm = timespec64_to_ktime(
			psandbox->activity->execution_start);
		executing_time = current_tm - execution_start_tm;
		delayed_time = current_tm - delaying_start_tm;

		if (delayed_time > (executing_time - delayed_time) *
					   psandbox->delay_ratio * competitors_num) {
			task->psandbox->state = BOX_AWAKE;
			success = wake_up_process(task);
			printk(KERN_INFO
			       "psandbox syscall called psandbox_wakeup pid =%d; success =%d\n",
			       task->pid, success);
			break;
		}
		break;
	}
	case TRY_QUEUE: {
		int competitors_num = arg;
		if (current->pid == bid) {
			struct timespec64 current_time;
			ktime_t current_tm, delaying_start_tm,
				execution_start_tm, executing_tm, defer_tm;

			ktime_get_real_ts64(&current_time);
			current_tm = timespec64_to_ktime(current_time);
			delaying_start_tm = timespec64_to_ktime(
				psandbox->activity->delaying_start);
			execution_start_tm = timespec64_to_ktime(
				psandbox->activity->execution_start);
			executing_tm = current_tm - execution_start_tm;
			defer_tm = current_tm - delaying_start_tm;
//			psandbox->activity->try_number++;
			//TODO: separate the detection part from the tracing part
			if (defer_tm > (executing_tm - defer_tm) *
					       psandbox->delay_ratio * competitors_num) {
				return 1;
			}
			success = 0;
		} else {
			if (task_is_stopped(task))
				return 0;
			smp_store_mb(task->state, (TASK_INTERRUPTIBLE));
			schedule();
			return 1;
		}
		break;
	}
	case START_QUEUE:
		ktime_get_real_ts64(&psandbox->activity->delaying_start);
		break;
	case ENTER_QUEUE: {
		ktime_t current_tm, delaying_start_tm, defer_tm;
		struct timespec64 current_time;
		psandbox->activity->queue_state = QUEUE_ENTER;
		ktime_get_real_ts64(&current_time);
		current_tm = timespec64_to_ktime(current_time);
		delaying_start_tm =
			timespec64_to_ktime(psandbox->activity->delaying_start);
		defer_tm = timespec64_to_ktime(psandbox->activity->defer_time);
		defer_tm += current_tm - delaying_start_tm;
		psandbox->activity->defer_time = ktime_to_timespec64(defer_tm);
//		Object *o = hashmap_get(keys_map,uaddr);
//		if(o) {
//			o->avg_delay = 0;
//			o->max_delay = 0;
//			hashmap_put(&keys_map, uaddr, o);
//		}
		break;
	}
	case EXIT_QUEUE: {
		int flag = arg;
//		ktime_t defer_tm,current_tm ;
//		struct timespec64 current_time;
//		ktime_get_real_ts64(&current_time);
//		current_tm = timespec64_to_ktime(current_time);
//		defer_tm = psandbox->activity->delaying_start - current_tm;
//		Object *o = hashmap_get(keys_map,uaddr);
//		if ( o->max_delay < defer_tm ) {
//			o->max_delay = defer_tm;
//		}
//		o->avg_delay += defer_tm/o->count;

		if (flag == 1) {
			wake_up_process(task);
		}
		break;
	}
	case MUTEX_REQUIRE:
		ktime_get_real_ts64(&psandbox->activity->delaying_start);
		break;
	case MUTEX_GET: {
		struct timespec64 current_time;
		ktime_t current_tm, delaying_start_tm, defer_tm;
		ktime_get_real_ts64(&current_time);
		current_tm = timespec64_to_ktime(current_time);
		delaying_start_tm =
			timespec64_to_ktime(psandbox->activity->delaying_start);
		defer_tm = timespec64_to_ktime(psandbox->activity->defer_time);
		defer_tm += current_tm - delaying_start_tm;
		psandbox->activity->defer_time = ktime_to_timespec64(defer_tm);
//		Object *o = hashmap_get(keys_map,uaddr);
//		if(o) {
//			o->avg_delay = 0;
//			o->max_delay = 0;
//			hashmap_put(&keys_map, uaddr, o);
//		}

		break;
	}
	case MUTEX_RELEASE: {
		int delayed_competitors = 0;
		ktime_t penalty_ns = 0;
		struct timespec64 current_time;
		int competitors_num = arg;
		ktime_t current_tm, delaying_start_tm, execution_start_tm,
			executing_tm, defer_tm;

		ktime_get_real_ts64(&current_time);
		current_tm = timespec64_to_ktime(current_time);
		delaying_start_tm =
			timespec64_to_ktime(psandbox->activity->delaying_start);
		execution_start_tm = timespec64_to_ktime(
			psandbox->activity->execution_start);
		executing_tm = current_tm - execution_start_tm;
		defer_tm = current_tm - delaying_start_tm;

		if (defer_tm >
		    (executing_tm - defer_tm) * psandbox->delay_ratio * competitors_num) {
			penalty_ns += defer_tm;
			delayed_competitors++;
		}
//		Object *o = hashmap_get(keys_map,uaddr);
//		if ( o->max_delay < defer_tm ) {
//			o->max_delay = defer_tm;
//		}
//		o->avg_delay += defer_tm/o->count;

		if (delayed_competitors) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_hrtimeout(&penalty_ns, HRTIMER_MODE_REL);
		}
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

SYSCALL_DEFINE1(active_psandbox, int, bid)
{
	struct task_struct *task = find_get_task_by_vpid(bid);
	PSandbox *psandbox;
	if (!task || !task->psandbox || !task->psandbox->activity) {
		printk(KERN_INFO "can't find sandbox based on the id\n");
		return -1;
	}
	psandbox = task->psandbox;
	psandbox->state = BOX_ACTIVE;
	ktime_get_real_ts64(&psandbox->activity->execution_start);
	psandbox->activity->defer_time.tv_nsec = 0;
	psandbox->activity->defer_time.tv_sec = 0;
	psandbox->activity->delaying_start.tv_nsec = 0;
	psandbox->activity->delaying_start.tv_sec = 0;
//	psandbox->activity->try_number = 0;
	return 0;
}

SYSCALL_DEFINE1(freeze_psandbox, int, bid)
{
	struct task_struct *task = find_get_task_by_vpid(bid);
	PSandbox *psandbox;
	if (!task || !task->psandbox) {
		printk(KERN_INFO "can't find sandbox based on the id\n");
		return -1;
	}
	psandbox = task->psandbox;
	psandbox->state = BOX_FREEZE;
	psandbox->activity->defer_time.tv_nsec = 0;
	psandbox->activity->defer_time.tv_sec = 0;
	psandbox->activity->delaying_start.tv_nsec = 0;
	psandbox->activity->delaying_start.tv_sec = 0;
	psandbox->activity->execution_start.tv_nsec = 0;
	psandbox->activity->execution_start.tv_sec = 0;
	psandbox->activity->queue_state = QUEUE_NULL;
//	psandbox->activity->try_number = 0;
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