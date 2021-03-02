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

#define MAX_TIME 100 * 5

struct psandbox_info psandboxs[10];
int i = 0;

/*This function will create a psandbox and bind to the current thread*/
SYSCALL_DEFINE1(psandbox_create, char *, name)
{
	PSandbox psandbox;
	psandbox.current_task = current;
	current->psandbox = &psandbox;
	psandbox.event = START;
	if (i > 10) {
		printk(KERN_INFO "kernel panic %d\n", current->pid);
	}
	psandboxs[i] = psandbox;
	i++;
	printk(KERN_INFO
	       "psandbox syscall called psandbox_create id =%d, i =%d\n",
	       current->pid, i);
	return i - 1;
}

SYSCALL_DEFINE1(psandbox_release, int, psandbox_id)
{
	int j = 0;
	for (j = psandbox_id + 1; j < i; j++) {
		psandboxs[j - 1] = psandboxs[j];
	}
	i--;
	printk(KERN_INFO
	       "psandbox syscall called psandbox_release id =%d, i = %d\n",
	       current->pid, i);
	return 0;
}

SYSCALL_DEFINE1(psandbox_wakeup, int, tid)
{
	int j;
	if (!tid) {
		printk(KERN_INFO
		       "psandbox syscall called psandbox_wakeup pid\n");
		return;
	}
	struct task_struct *task = find_get_task_by_vpid(tid);
	for (j = 0; j < i; j++) {
		printk(KERN_INFO
		       "the id is %d, the sandbox pid is %d, the pid is %d\n",
		       j, psandboxs[j].current_task, task);
		if (psandboxs[j].current_task == task) {
			printk(KERN_INFO "change to awake pid %d\n", task->pid);
			task->psandbox->event = AWAKE;
			break;
		}
	}
	int success = wake_up_process(task);
	printk(KERN_INFO
	       "psandbox syscall called psandbox_wakeup pid =%d; success =%d\n",
	       task->pid, success);
	return 0;
}