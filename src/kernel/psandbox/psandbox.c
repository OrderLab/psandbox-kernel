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


SYSCALL_DEFINE1(wakeup_psandbox, int, bid)
{
	struct task_struct *task = find_get_task_by_vpid(bid);
	PSandbox *psandbox;

	if (!task || !task->psandbox || !task->psandbox->activity) {
		printk(KERN_INFO "can't find sandbox based on the id %d\n",bid);
		return -1;
	}
//	printk(KERN_INFO
//	       "psandbox syscall called psandbox_wakeup pid =%d \n",
//	       task->pid);
	psandbox = task->psandbox;
	psandbox->state = BOX_AWAKE;
	wake_up_process(task);

	return 0;
}

SYSCALL_DEFINE2(penalize_psandbox, int, bid, int, penalty_ns)
{
	ktime_t penalty = penalty_ns;

	if(penalty_ns > 0) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_hrtimeout(&penalty, HRTIMER_MODE_REL);
	} else {
		struct task_struct *task = find_get_task_by_vpid(bid);

		if (!task || !task->psandbox) {
			printk(KERN_INFO "can't find sandbox based on the id\n");
			return -1;
		}
//		printk(KERN_INFO "call penalize psandbox %d\n",bid);
		if (task_is_stopped(task))
			return 0;

		smp_store_mb(task->state, (TASK_UNINTERRUPTIBLE));
		schedule();
		return 1;
	}

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
