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
spinlock_t competitors_lock;
spinlock_t holders_lock;
int pid = 0;
#define COMPENSATION_TICKET_NUMBER	1000L
#define PROBING_NUMBER 100

DECLARE_HASHTABLE(competitors_map, 10);
DECLARE_HASHTABLE(holders_map, 10);

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
	psandbox->is_futex = 0;
	psandbox->tail_requirement = 90;
	psandbox->bad_activities = 0;
	INIT_LIST_HEAD(&psandbox->activity->delay_list);
	total_psandbox++;
	if(total_psandbox == 5) {
		pid = psandbox->bid;
	}
	printk(KERN_INFO "psandbox syscall called psandbox_create id =%d\n",
	       current->pid);
	return psandbox->current_task->pid;
}

SYSCALL_DEFINE1(release_psandbox, int, bid)
{
	PSandbox *cur;
	struct delaying_start *pos,*temp;
	unsigned bkt;
	struct hlist_node *tmp;
	struct task_struct *task = find_get_task_by_vpid(bid);
	if (!task) {
		printk(KERN_INFO "can't find sandbox based on the id\n");
		return -1;
	}
	if (!task->psandbox) {
		printk(KERN_INFO "there is no psandbox\n");
		return 0;
	}
	hash_for_each_safe(competitors_map, bkt, tmp, cur, node) {
			if (cur == task->psandbox) {
				hash_del(&cur->node);
			}
	}
	total_psandbox--;

	list_for_each_entry_safe(pos,temp,&task->psandbox->activity->delay_list,list) {
		kfree(pos);
	}
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

	return 0;
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
	psandbox->activity->execution_time = timespec64_sub(total_time,psandbox->activity->defer_time);
	if (total_psandbox)  {
		if(timespec64_to_ns(&psandbox->activity->execution_time) * psandbox->delay_ratio * total_psandbox
		        <  timespec64_to_ns(&psandbox->activity->defer_time)) {
			psandbox->bad_activities++;
			if (psandbox->action_level == LOW_PRIORITY)
				psandbox->action_level = MID_PRIORITY;
			if (psandbox->action_level != HIGHEST_PRIORITY && psandbox->finished_activities > PROBING_NUMBER && psandbox->bad_activities * 100
				> (psandbox->tail_requirement * psandbox->finished_activities)) {
				psandbox->action_level = HIGHEST_PRIORITY;
				printk(KERN_INFO "give a ticket for %lu, bad activity %d, finish activity %d\n", psandbox->bid, psandbox->bad_activities, psandbox->finished_activities);
				psandbox->compensation_ticket = COMPENSATION_TICKET_NUMBER;
			}
		}
	}
	struct list_head temp = psandbox->activity->delay_list;
	memset(psandbox->activity, 0, sizeof(Activity));
	psandbox->activity->delay_list = temp;
	return 0;
}

SYSCALL_DEFINE2(update_event, BoxEvent __user *, event, int, bid) {
	struct task_struct *task = find_get_task_by_vpid(bid);
	int event_type = event->event_type, key = event->key;
	PSandbox *psandbox = task->psandbox;
	PSandbox *cur;
	struct hlist_node *tmp;

	if (!task || !task->psandbox || !event) {
		printk(KERN_INFO "can't find sandbox based on the id\n");
		return -1;
	}



	switch (event_type) {
	case PREPARE:{
		int is_duplicate = false, is_first = true;
		struct delaying_start *pos;

		// Update the defering time
		list_for_each_entry(pos,&psandbox->activity->delay_list,list) {
			if (pos->key == key) {
				is_first = false;
				ktime_get_real_ts64(&pos->delaying_start);
			}
		}

		if (is_first) {
			struct delaying_start *delaying_start;
			delaying_start = (struct delaying_start *)kzalloc(sizeof(struct delaying_start),GFP_KERNEL);
			ktime_get_real_ts64(&delaying_start->delaying_start);
			delaying_start->key = key;
			list_add(&delaying_start->list,&psandbox->activity->delay_list);
		}

		psandbox->activity->activity_state = ACTIVITY_WAITING;

		//Add to the holder map
		spin_lock(&competitors_lock);
		if (competitors_map[hash_min(key, HASH_BITS(competitors_map))].first == NULL) {
			spin_unlock(&competitors_lock);
			spin_lock(&competitors_lock);
			hash_add(competitors_map,&psandbox->node,key);
			spin_unlock(&competitors_lock);
		}
		spin_unlock(&competitors_lock);
		spin_lock(&competitors_lock);
		hash_for_each_possible_safe(competitors_map, cur,tmp, node,key) {
			if (cur == psandbox) {
				is_duplicate = true;
				break;
			}
		}
		spin_unlock(&competitors_lock);

		if (!is_duplicate) {
			spin_lock(&competitors_lock);
			hash_add(competitors_map,&psandbox->node,key);
			spin_unlock(&competitors_lock);
		}
		break;
	}
	case ENTER: {
		struct timespec64 current_tm, defer_tm;
		psandbox->activity->activity_state = ACTIVITY_ENTER;
		//Free the competitors map
		spin_lock(&competitors_lock);
		hash_for_each_possible_safe (competitors_map, cur, tmp, node, key) {
			if (cur == psandbox) {
				hash_del(&cur->node);
				break;
			}
		}
		spin_unlock(&competitors_lock);

		ktime_get_real_ts64(&current_tm);
		struct delaying_start *pos;
		list_for_each_entry(pos,&psandbox->activity->delay_list,list) {
			if (pos->key == key) {
				defer_tm = timespec64_sub(current_tm,pos->delaying_start);
				break;
			}
		}
		if(defer_tm.tv_sec == 0 && defer_tm.tv_nsec == 0) {
			printk (KERN_INFO "can't find the key for delaying start for psandbox %d\n", psandbox->bid);
		}
		current_tm = psandbox->activity->defer_time;
		psandbox->activity->defer_time = timespec64_add(defer_tm, current_tm);
		break;
	}
	case HOLD: {
		int is_duplicate = false;
		// Add the holder map

		spin_lock(&holders_lock);
		if (holders_map[hash_min(key, HASH_BITS(holders_map))].first == NULL) {
			hash_add(holders_map,&psandbox->node,key);
		}

		hash_for_each_possible_safe(holders_map, cur,tmp, node,key) {
			if (cur == psandbox) {
				is_duplicate = true;
				break;
			}
		}
		spin_unlock(&holders_lock);
		if (!is_duplicate) {
			spin_lock(&holders_lock);
			hash_add(holders_map,&psandbox->node,key);
			spin_unlock(&holders_lock);
		}

	}
	case UNHOLD: {
		int is_action = false, is_holder = false;
		struct timespec64 current_tm, defer_tm, executing_tm,delaying_tm;
		ktime_t penalty_ns = 0;
		struct task_struct *good_task;
		psandbox->activity->activity_state = ACTIVITY_EXIT;
		spin_lock(&holders_lock);
		hash_for_each_possible_safe (holders_map, cur, tmp, node, key) {
			if (cur == psandbox) {
				hash_del(&cur->node);
//				printk (KERN_INFO "the psandbox is %d\n", cur->bid);
				is_holder = true;
				break;
			}
		}
		spin_unlock(&holders_lock);
		if(!is_holder)
			return 1;
		spin_lock(&competitors_lock);

		// calculating the defering time
		hash_for_each_possible_safe (competitors_map, cur, tmp, node,
					     key) {
			if (psandbox->action_level != LOW_PRIORITY)
				break;

			if (cur->bid != psandbox->bid) {
				ktime_get_real_ts64(&current_tm);
				struct delaying_start *pos;
				list_for_each_entry(pos,&cur->activity->delay_list,list) {
					if (pos->key == key) {
						delaying_tm = pos->delaying_start;
						defer_tm = timespec64_sub(current_tm,pos->delaying_start);
						break;
					}
				}
				if(defer_tm.tv_sec == 0 && defer_tm.tv_nsec == 0) {
					printk (KERN_INFO "cann't find the key for delaying start for psandbox %d\n", psandbox->bid);
				}
				executing_tm = timespec64_sub(timespec64_sub(current_tm,cur->activity->execution_start),defer_tm);

//				printk (KERN_INFO "the executing time is %lu, the defer time is %lu for psandbox %d, current psandbox %d\n",
//				       timespec64_to_ns(&executing_tm),timespec64_to_ns(&defer_tm), cur->bid, psandbox->bid);
//
//				printk (KERN_INFO "the delaying start time is %lu\n",timespec64_to_ns(&cur->activity->delaying_start));
//				printk (KERN_INFO "the state is %d\n",cur->current_task->state);
				if (timespec64_to_ns(&defer_tm) > timespec64_to_ns(&executing_tm) *
					cur->delay_ratio * total_psandbox) {
					penalty_ns += timespec64_to_ns(&defer_tm);
					good_task = find_get_task_by_vpid(cur->bid);
				}

				if (cur->action_level == HIGHEST_PRIORITY)
					is_action = true;
			}
		}
		spin_unlock(&competitors_lock);
		if (is_action) {
			int penalty_us = 1000000;

			hash_for_each_possible_safe (competitors_map, cur, tmp, node,
						     key) {
				if (cur->action_level == LOW_PRIORITY &&
					cur->bid != psandbox->bid ) {
					struct task_struct *bad_task = find_get_task_by_vpid(cur->bid);
					unsigned long timeout;
					if (!bad_task || !bad_task->psandbox) {
						printk(KERN_INFO "can't find sandbox based on the id\n");
						continue;
					}
					timeout = usecs_to_jiffies(penalty_us);
					psandbox_schedule_timeout(timeout,bad_task);
				}

				if (cur->action_level == HIGHEST_PRIORITY &&
					cur->bid != psandbox->bid ) {
					struct task_struct *good_task = find_get_task_by_vpid(cur->bid);
					if (!good_task || !good_task->psandbox) {
						printk(KERN_INFO "can't find sandbox based on the id\n");
						continue;
					}
					good_task->psandbox->state = BOX_AWAKE;
					wake_up_process(good_task);
				}
			}
		}

		if (penalty_ns) {

			__set_current_state(TASK_INTERRUPTIBLE);
//			if (penalty_ns > 1000000000) {
//				printk (KERN_INFO "penalize psandbox %d for %lu ns\n",psandbox->bid, penalty_ns);
//			}
			if (good_task)
				wake_up_process(good_task);
			if (penalty_ns > 1000000) {
				penalty_ns = 1000000;
				schedule_hrtimeout(&penalty_ns, HRTIMER_MODE_REL);
			} else {
				schedule_hrtimeout(&penalty_ns, HRTIMER_MODE_REL);
			}

		}

	}

	default:break;
	}
	return 0;
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
//	PSandbox *psandbox;

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
	WhiteList *white_mutex;
	white_mutex = (WhiteList *)kzalloc(sizeof(WhiteList),GFP_KERNEL);
	white_mutex->addr = uaddr;
	INIT_LIST_HEAD(&white_mutex->list);
	list_add(&white_mutex->list,&white_mutexs);
	return 0;
}

static int psandbox_init(void)
{
	INIT_LIST_HEAD(&white_mutexs);
	hash_init(competitors_map);
	hash_init(holders_map);
	spin_lock_init(&competitors_lock);
	spin_lock_init(&holders_lock);
	return 0;
}
core_initcall(psandbox_init);