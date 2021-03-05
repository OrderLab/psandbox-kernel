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

int size = 0;
const unsigned initial_size = 8;
HashMap competed_sandbox_set;
HashMap key_condition_map;

/*This function will create a psandbox and bind to the current thread*/
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
	size++;
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
	size--;
	kfree(task->psandbox->activity);
	kfree(task->psandbox);
	printk(KERN_INFO "psandbox syscall called psandbox_release id =%d\n",
	       current->pid);
	return 0;
}

int push_or_create_competitors(LinkedList* competitors, struct sandboxEvent *event, PSandbox* new_competitor) {
	int success = 0;
	if (NULL == competitors) {
		competitors = (LinkedList *)kmalloc(sizeof(LinkedList), GFP_KERNEL);
		if (!competitors) {
			printk(KERN_INFO "Error: fail to create linked list\n");
			return NULL;
		}

		success = list_push_front(competitors,new_competitor);
		hashmap_put(&competed_sandbox_set,event->key,competitors);
	} else {
		if(!list_find(competitors,new_competitor)) {
			success = list_push_front(competitors,new_competitor);
		}
	}

	return success;
}

int wakeup_competitor(LinkedList *competitors, PSandbox* sandbox) {
	struct linkedlist_element_s* node;
	for ( node = competitors->head; node != NULL; node = node->next) {
		PSandbox* competitor_sandbox = (PSandbox *)(node->data);

		// Don't wakeup itself
		if (sandbox == node->data || competitor_sandbox->activity->queue_state != QUEUE_SLEEP)
			continue;


		struct timespec64 current_time;
		ktime_get_real_ts64(&current_time);
		ktime_t current_tm = timespec64_to_ktime(current_time);
		ktime_t delaying_start_tm = timespec64_to_ktime(competitor_sandbox->activity->delaying_start);
		ktime_t execution_start_tm = timespec64_to_ktime(competitor_sandbox->activity->execution_start);
		ktime_t executing_time = current_tm - execution_start_tm;
		ktime_t delayed_time = current_tm - delaying_start_tm;

		if(delayed_time > (executing_time - delayed_time) * sandbox->delay_ratio * size) {
			struct task_struct *task = competitor_sandbox->current_task;

			task->psandbox->state = BOX_AWAKE;
			int success = wake_up_process(task);
			printk(KERN_INFO
			       "psandbox syscall called psandbox_wakeup pid =%d; success =%d\n",
			       task->pid, success);
			break;
		}
	}
	return 0;
}

SYSCALL_DEFINE2(update_psandbox, PsandboxEvent __user *, event, int, bid)
{
	struct task_struct *task = find_get_task_by_vpid(bid);
	PSandbox *psandbox;
	int event_type = event->event_type;
	LinkedList *competitors;
	int delayed_competitors = 0;
//	printk(KERN_INFO "call update_psandbox %d\n",bid);
	if (!task || !task->psandbox) {
		printk(KERN_INFO "can't find sandbox based on the id\n");
		return -1;
	}
	psandbox = task->psandbox;
	if (competed_sandbox_set.table_size == 0) {
		if (0 != hashmap_create(initial_size, &competed_sandbox_set))
			return -1;
	}

	if (psandbox->state == BOX_FREEZE)
		return 0;

	switch (event_type) {
	case UPDATE_QUEUE_CONDITION:
		printk(KERN_INFO "CALL INTO UPDATE_QUEUE_CONDITION\n");
		competitors = hashmap_get(&competed_sandbox_set, event->key);
		if(!competitors) {
			printk(KERN_INFO "Error: fail to find the competitor sandbox\n");
			return -1;
		}
		printk(KERN_INFO "get competitor %d\n",competitors);
		Condition* cond = hashmap_get(&key_condition_map, event->key);
		if(!cond) {
			printk(KERN_INFO "Error: fail to find condition\n");
			return -1;
		}
		printk(KERN_INFO "get cond %d\n",cond);
		switch (cond->compare) {
		case COND_LARGE:
			if (*(int*)event->key <= cond->value) {
				wakeup_competitor(competitors,psandbox);
			}
			break;
		case COND_SMALL:
			if (*(int*)event->key >= *(int *)cond->value) {
				wakeup_competitor(competitors,psandbox);
			}
			break;
		case COND_LARGE_OR_EQUAL:
			if (*(int*)event->key < *(int *)cond->value) {
				wakeup_competitor(competitors,psandbox);
			}
			break;
		case COND_SMALL_OR_EQUAL:
			if (*(int*)event->key > *(int *)cond->value) {
				wakeup_competitor(competitors,psandbox);
			}
			break;
		}
		break;
	case SLEEP_BEGIN:
		psandbox->activity->queue_state=QUEUE_SLEEP;
		break;
	case SLEEP_END:
		psandbox->activity->queue_state=QUEUE_AWAKE;
		break;
	case TRY_QUEUE:
		competitors = hashmap_get(&competed_sandbox_set, event->key);

		if(push_or_create_competitors(competitors,event,psandbox)) {
			printk(KERN_INFO "Error: fail to create competitor list\n");
			return -1;
		}

		ktime_get_real_ts64(&psandbox->activity->delaying_start);
		break;
	case ENTER_QUEUE:
		psandbox->activity->queue_state = QUEUE_ENTER;

		struct  timespec64 current_time;
		ktime_get_real_ts64(&current_time);
		ktime_t cur = timespec64_to_ktime(current_time);
		ktime_t del = timespec64_to_ktime(psandbox->activity->delaying_start);
		ktime_t delayed = timespec64_to_ktime(psandbox->activity->delayed_time);
		delayed += cur-del;
		psandbox->activity->delayed_time = ktime_to_timespec64(delayed);
		break;
	case EXIT_QUEUE:
		competitors = hashmap_get(&competed_sandbox_set, event->key);
		if(!competitors) {
			printk(KERN_INFO "Error: fail to create competitor list\n");
			return -1;
		}

		if(list_remove(competitors,psandbox)) {
			printk(KERN_INFO "Error: fail to remove competitor sandbox\n");
			return -1;
		}

		if(competitors->size == 0) {
			if(hashmap_remove(&competed_sandbox_set, event->key) || hashmap_remove(&key_condition_map,event->key)) {
				printk(KERN_INFO "Error: fail to remove empty list\n");
				return -1;
			}

		}
		psandbox->activity->queue_state=QUEUE_NULL;
		break;
	case MUTEX_REQUIRE:
		competitors = hashmap_get (&competed_sandbox_set, event->key);
		if (push_or_create_competitors (competitors,event,psandbox)) {
			printk(KERN_INFO "Error: fail to create competitor list\n");
			return -1;
		}
		ktime_get_real_ts64(&psandbox->activity->delaying_start );
		break;
	case MUTEX_GET: {
		struct  timespec64 current_time;
		ktime_get_real_ts64(&current_time);
		ktime_t cur = timespec64_to_ktime(current_time);
		ktime_t del = timespec64_to_ktime(psandbox->activity->delaying_start);
		ktime_t delayed = timespec64_to_ktime(psandbox->activity->delayed_time);
		delayed += cur-del;
		psandbox->activity->delayed_time = ktime_to_timespec64(delayed);
		break;
	}
	case MUTEX_RELEASE:
		delayed_competitors = 0;
		competitors = hashmap_get(&competed_sandbox_set, event->key);
		if(!competitors) {
			printk(KERN_INFO "Error: fail to create competitor list\n");
			return -1;
		}

		if(list_remove(competitors,psandbox)) {
			printk(KERN_INFO "Error: fail to remove competitor sandbox\n");
			return -1;
		}

		if(competitors->size == 0) {
			if(hashmap_remove(&competed_sandbox_set, event->key)) {
				printk(KERN_INFO "Error: fail to remove empty list\n");
				return -1;
			}

		}
		int tid;
		ktime_t	delayed_in_ns = 0;
		struct linkedlist_element_s* node;
		for (node = competitors->head; node != NULL; node = node->next) {
			PSandbox* competitor_sandbox = (PSandbox *)(node->data);

			if (psandbox == node->data)
				continue;

			struct  timespec64 current_time;
			ktime_get_real_ts64(&current_time);
			ktime_t current_tm = timespec64_to_ktime(current_time);
			ktime_t delaying_start_tm = timespec64_to_ktime(psandbox->activity->delaying_start);
			ktime_t execution_start_tm = timespec64_to_ktime(psandbox->activity->execution_start);
			ktime_t executing_time = current_tm - execution_start_tm;
			ktime_t delayed_time = current_tm - delaying_start_tm;

			int num = size;
			if (delayed_time/executing_time >  psandbox->delay_ratio * num) {
				delayed_in_ns += delayed_time;
				tid = competitor_sandbox->current_task->pid;
				delayed_competitors++;
			}
		}

		if (delayed_competitors) {
//			printk(KERN_INFO "give penalty to sandbox %d, delayed_in_ns %lu\n", current->pid, delayed_in_ns);
			schedule_hrtimeout(&delayed_in_ns, HRTIMER_MODE_ABS);
//			printk(KERN_INFO "psandbox penalty end pid =%d\n", current->pid);
		}
		break;
	default:break;
	}
	return  0;
}

SYSCALL_DEFINE2(update_condition_psandbox, int __user *, key, Condition __user *, cond)
{
	Condition *condition;
	if (key_condition_map.table_size == 0) {
		if (0 != hashmap_create(initial_size, &key_condition_map)) {
			return -1;
		}
	}
	condition = hashmap_get(&key_condition_map, key);
	if(!condition) {
		condition  = kmalloc(sizeof(Condition),GFP_KERNEL);
		condition->compare = cond->compare;
		condition->value = cond->value;
		if (hashmap_put(&key_condition_map, key, condition)) {
			printk(KERN_INFO "Error: fail to add condition list\n");
			return -1;
		}
	} else {
		condition->compare = cond->compare;
		condition->value = cond->value;
	}
	return 0;
}

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
	ktime_get_real_ts64(&(psandbox->activity->execution_start));
	psandbox->activity->delayed_time.tv_nsec = 0;
	psandbox->activity->delayed_time.tv_sec = 0;
	psandbox->activity->delaying_start.tv_nsec = 0;
	psandbox->activity->delaying_start.tv_sec = 0;
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
	psandbox->activity->delayed_time.tv_nsec = 0;
	psandbox->activity->delayed_time.tv_sec = 0;
	psandbox->activity->delaying_start.tv_nsec = 0;
	psandbox->activity->delaying_start.tv_sec = 0;
	psandbox->activity->execution_start.tv_nsec = 0;
	psandbox->activity->execution_start.tv_sec = 0;
	psandbox->activity->queue_state = QUEUE_NULL;
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