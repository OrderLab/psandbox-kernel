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
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/hashtable.h>

struct list_head white_mutexs;
int total_psandbox = 0;
struct competitors_node {
	PSandbox *psandbox;
	struct hlist_node node;
};
DECLARE_HASHTABLE(competitors_map, 10);

/* This function will create a psandbox and bind to the current thread */
SYSCALL_DEFINE0(create_psandbox)
{
	PSandbox *psandbox;
	psandbox = (PSandbox *)kmalloc(sizeof(PSandbox), GFP_KERNEL);
	if (!psandbox) {
		return -1;
	}
	psandbox->bid = current->pid;
	psandbox->current_task = current;
	psandbox->activity = (Activity *)kzalloc(sizeof(Activity), GFP_KERNEL);
	psandbox->state = BOX_START;
	psandbox->white_list = &white_mutexs;
	current->psandbox = psandbox;
	psandbox->finished_activities = 0;
	psandbox->action_level = LOW_PRIORITY;
	psandbox->compensation_ticket = 0;
	psandbox->is_white = 0;
	psandbox->delay_ratio = 1;
	total_psandbox++;
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
	total_psandbox--;
	kfree(task->psandbox->activity);
	kfree(task->psandbox);
	printk(KERN_INFO "psandbox syscall called psandbox_release id =%d\n",
	       current->pid);

	return 0;
}

SYSCALL_DEFINE0(active_psandbox)
{
	PSandbox *psandbox = current->psandbox;
	if (!psandbox) {
		printk(KERN_INFO "there is no psandbox\n");
		return 0;
	}
	psandbox->state = BOX_ACTIVE;
	ktime_get_real_ts64(&psandbox->activity->execution_start);
}

SYSCALL_DEFINE0(freeze_psandbox)
{
	PSandbox *psandbox = current->psandbox;
	struct timespec64 current_tm, total_time;
	if (!psandbox) {
		printk(KERN_INFO "there is no psandbox\n");
		return 0;
	}
	if (psandbox->compensation_ticket > 1) {
		psandbox->compensation_ticket--;
	} else {
		psandbox->action_level = LOW_PRIORITY;
	}

	psandbox->state = BOX_FREEZE;
	psandbox->finished_activities++;
	ktime_get_real_ts64(&current_tm);
	total_time = timespec64_sub(current_tm,psandbox->activity->execution_start);
//	printk(KERN_INFO "the defer time is %lu ns, psandbox %d\n", timespec64_to_ktime(psandbox->activity->defer_time), psandbox->bid);
	total_time = timespec64_sub(total_time,psandbox->activity->defer_time);
	memset(psandbox->activity, 0, sizeof(Activity));
	return 0;
}

SYSCALL_DEFINE2(update_event, BoxEvent __user *, event, int, bid) {
	struct task_struct *task = find_get_task_by_vpid(bid);
	int event_type = event->event_type, key = event->key;
	PSandbox *psandbox = current->psandbox;
	unsigned long timeout;
	if (!task || !task->psandbox || !event) {
		printk(KERN_INFO "can't find sandbox based on the id\n");
		return -1;
	}

	switch (event_type) {
	case PREPARE:{
		struct competitors_node a, *cur;
		unsigned bkt;
		ktime_get_real_ts64(&psandbox->activity->delaying_start);
		psandbox->activity->activity_state = ACTIVITY_WAITING;
		a.psandbox = psandbox;
		hash_add(competitors_map,&a.node,key);
		hash_for_each(competitors_map, bkt, cur, node) {
				printk(KERN_INFO "myhashtable: element: id = %d\n",
					cur->psandbox->bid);
			}
			printk(KERN_INFO "END\n");
//		competitors = g_hash_table_lookup(competitors_map, GINT_TO_POINTER(key));
//		competitors = g_list_append(competitors, p_sandbox);
//		g_hash_table_insert(competitors_map, GINT_TO_POINTER(key), competitors);
////      printf("PREPARE the size is %d, the key is %d, %d\n",g_list_length(competitors), (*(int *)event->key),p_sandbox->bid);
//		if (p_sandbox->action_level == HIGHEST_PRIORITY) {
//			interfered_psandboxs = g_hash_table_lookup(interfered_competitors, GINT_TO_POINTER(key));
//			interfered_psandboxs = g_list_append(interfered_psandboxs, p_sandbox);
//			g_hash_table_insert(interfered_competitors, GINT_TO_POINTER(key), interfered_psandboxs);
//		}


//		pthread_mutex_unlock(&stats_lock);
		break;
	}
//	case ENTER:{
//		GList *iterator;
//
////      printf("ENTER get the mutex psandbox %d,the key is %d\n",p_sandbox->bid,(*(int *)event->key));
//		pthread_mutex_lock(&stats_lock);
//		competitors = g_hash_table_lookup(competitors_map, GINT_TO_POINTER(key));
//		competitors = g_list_remove(competitors, p_sandbox);
//		g_hash_table_insert(competitors_map, GINT_TO_POINTER(key), competitors);
//		p_sandbox->activity->activity_state = QUEUE_ENTER;
//
//		holders = g_hash_table_lookup(holders_map, GINT_TO_POINTER(key));
//		holders = g_list_append(holders, p_sandbox);
//		g_hash_table_insert(holders_map, GINT_TO_POINTER(key), holders);
//
////      printf("ENTER the size is %d,the key is %d, %d\n",g_list_length(competitors),(*(int *)event->key),p_sandbox->bid);
//		if (p_sandbox->action_level == HIGHEST_PRIORITY) {
//			interfered_psandboxs = g_hash_table_lookup(interfered_competitors, GINT_TO_POINTER(key));
//			interfered_psandboxs = g_list_remove(interfered_psandboxs, p_sandbox);
//			g_hash_table_insert(interfered_competitors, GINT_TO_POINTER(key), interfered_psandboxs);
//
//			for (iterator = competitors; iterator; iterator = iterator->next) {
//				PSandbox *psandbox = (PSandbox *) iterator->data;
////          printf("find psandbox %d, activity state %d, promoting %d, state %d\n",psandbox->bid,psandbox->activity->activity_state,psandbox->action_level,psandbox->state);
//				if (psandbox == NULL || p_sandbox == psandbox || psandbox->activity->activity_state != QUEUE_WAITING
//					|| psandbox->action_level != LOW_PRIORITY || psandbox->state != BOX_PREEMPTED)
//					continue;
//
//				psandbox->state = BOX_ACTIVE;
//				syscall(SYS_WAKEUP_PSANDBOX, psandbox->bid);
//			}
//		}
//		updateDefertime(p_sandbox);
//		pthread_mutex_unlock(&stats_lock);
//		break;
//	}
//	case EXIT:
//	{
//		pthread_mutex_lock(&stats_lock);
//		GList *iterator = NULL;
//		p_sandbox->activity->activity_state = QUEUE_EXIT;
//
//		competitors = g_hash_table_lookup(competitors_map, GINT_TO_POINTER(key));
//		interfered_psandboxs = g_hash_table_lookup(interfered_competitors, GINT_TO_POINTER(key));
//
//		holders = g_hash_table_lookup(holders_map, GINT_TO_POINTER(key));
//		holders = g_list_remove(holders, p_sandbox);
//		g_hash_table_insert(holders_map, GINT_TO_POINTER(key), holders);
//
//		if (p_sandbox->action_level == LOW_PRIORITY && competitors && interfered_psandboxs) {
//			for (iterator = competitors; iterator; iterator = iterator->next) {
//				PSandbox *competitor = (PSandbox *) iterator->data;
//				if (competitor == NULL || p_sandbox == competitor || competitor->activity->activity_state != QUEUE_WAITING
//					|| competitor->action_level != LOW_PRIORITY)
//					continue;
//
//				competitor->state = BOX_PREEMPTED;
//				syscall(SYS_PENALIZE_PSANDBOX, competitor->bid, 1000);
//			}
//
//			interfered_psandboxs = g_hash_table_lookup(interfered_competitors, GINT_TO_POINTER(key));
//
//			for (iterator = interfered_psandboxs; iterator; iterator = iterator->next) {
//				PSandbox *victim = (PSandbox *) iterator->data;
//
//				if (victim == NULL || victim->activity->activity_state != QUEUE_WAITING)
//					continue;
//
//				interfered_psandboxs = g_list_remove(interfered_psandboxs, victim);
//				g_hash_table_insert(interfered_competitors, GINT_TO_POINTER(key), interfered_psandboxs);
////        printf("psandbox %d wakeup %d\n",p_sandbox->bid, victim->bid);
//				syscall(SYS_WAKEUP_PSANDBOX, victim->bid);
//			}
//		}
//
//		if (is_interfered(p_sandbox, event, holders)) {
//			PSandbox* noisy_neighbor;
//			noisy_neighbor = find_noisyNeighbor(p_sandbox, holders);
//
//			if (noisy_neighbor) {
//				//Give penalty to the noisy neighbor
//				printf("1.thread %lu sleep thread %lu\n", p_sandbox->bid, noisy_neighbor->bid);
//				penalize_competitor(noisy_neighbor,p_sandbox,event);
//			}
//		}
//		pthread_mutex_unlock(&stats_lock);
//		break;
//	}
	default:break;
	}
}

SYSCALL_DEFINE3(compensate_psandbox,int, noisy_bid,int, victim_bid, int, penalty_us){
	struct task_struct *task = find_get_task_by_vpid(noisy_bid);
	unsigned long timeout;
	if (!task || !task->psandbox) {
		printk(KERN_INFO "can't find sandbox based on the id\n");
		return -1;
	}
	if (penalty_us == 0) {
		smp_store_mb(task->psandbox->state, BOX_PREEMPTED);
		smp_store_mb(task->state, (TASK_UNINTERRUPTIBLE));
		if (task_is_stopped(task))
			return -1;
		schedule();
	} else {
		smp_store_mb(task->state, (TASK_INTERRUPTIBLE));
		if (task_is_stopped(task))
			return -1;

		timeout = usecs_to_jiffies(penalty_us);
		psandbox_schedule_timeout(timeout,task);
	}

	task = find_get_task_by_vpid(victim_bid);

	if (!task || !task->psandbox || !task->psandbox->activity) {
		printk(KERN_INFO "can't find sandbox based on the id %d\n",victim_bid);
		return -1;
	}
//	printk(KERN_INFO
//	       "psandbox syscall called psandbox_wakeup pid =%d \n",
//	       task->pid);
	task->psandbox->state = BOX_AWAKE;
	wake_up_process(task);
	return 1;
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
	task->psandbox->state = BOX_AWAKE;
	wake_up_process(task);
	return 0;
}

SYSCALL_DEFINE2(penalize_psandbox, int, bid, int, penalty_us)
{
	ktime_t penalty = penalty_us * 1000;

	if(bid == current->pid) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_hrtimeout(&penalty, HRTIMER_MODE_REL);
	} else {
		struct task_struct *task = find_get_task_by_vpid(bid);
		unsigned long timeout;
		if (!task || !task->psandbox) {
			printk(KERN_INFO "can't find sandbox based on the id\n");
			return -1;
		}
		if (penalty_us == 0) {
			smp_store_mb(task->psandbox->state, BOX_PREEMPTED);
			smp_store_mb(task->state, (TASK_UNINTERRUPTIBLE));
			if (task_is_stopped(task))
				return -1;
			schedule();
		} else {
			smp_store_mb(task->state, (TASK_INTERRUPTIBLE));
			if (task_is_stopped(task))
				return -1;

			timeout = usecs_to_jiffies(penalty_us);
			psandbox_schedule_timeout(timeout,task);
		}
		return 1;
	}

	return 1;
}

SYSCALL_DEFINE0(get_psandbox)
{
	if (!current->psandbox) {
//		printk(KERN_INFO "there is no psandbox in current thread\n");
		return -1;
	}
	return current->pid;
}

SYSCALL_DEFINE1(start_manager, u32 __user *, uaddr) {
	WhiteList *white_mutex,*whiteAddr;
	white_mutex = (WhiteList *)kzalloc(sizeof(WhiteList),GFP_KERNEL);
	white_mutex->addr = uaddr;
	INIT_LIST_HEAD(&white_mutex->list);
	list_add(&white_mutex->list,&white_mutexs);
}

static int psandbox_init(void)
{
	INIT_LIST_HEAD(&white_mutexs);
	hash_init(competitors_map);
	return 0;
}
core_initcall(psandbox_init);