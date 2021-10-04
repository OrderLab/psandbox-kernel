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

//#define ENABLE_DEBUG
struct list_head white_list;
struct list_head psandbox_list;
long int psandbox_id = 1;
long int live_psandbox = 0;

__cacheline_aligned DEFINE_RWLOCK(transfers_lock);
__cacheline_aligned DEFINE_RWLOCK(competitors_lock);
__cacheline_aligned DEFINE_RWLOCK(holders_lock);
__cacheline_aligned DEFINE_RWLOCK(psandbox_lock);

#define COMPENSATION_TICKET_NUMBER	1000L
#define PROBING_NUMBER 100




DECLARE_HASHTABLE(competitors_map, 10);
DECLARE_HASHTABLE(holders_map, 10);
DECLARE_HASHTABLE(transfers_map, 10);


/* This function will create a psandbox and bind to the current thread */
SYSCALL_DEFINE0(create_psandbox)
{
	PSandbox *psandbox;


	psandbox = (PSandbox *)kmalloc(sizeof(PSandbox), GFP_KERNEL);
	if (!psandbox) {
		return -1;
	}

	if(current->psandbox && current->psandbox->is_lazy) {
		do_unbind(0);
	}

	psandbox->bid = psandbox_id;
	psandbox->current_task = current;
	psandbox->activity = (Activity *)kzalloc(sizeof(Activity), GFP_KERNEL);
	psandbox->state = BOX_START;
	psandbox->white_list = &white_list;
	current->psandbox = psandbox;
	current->is_psandbox = 1;
	current->is_creator = 1;
	psandbox->finished_activities = 0;
	psandbox->action_level = LOW_PRIORITY;
	psandbox->compensation_ticket = 0;
	psandbox->is_white = 0;
	psandbox->delay_ratio = 1;
	psandbox->is_futex = 0;
	psandbox->tail_requirement = 90;
	psandbox->bad_activities = 0;
	psandbox->creator_psandbox = current;
	psandbox->is_lazy = 0;
	psandbox->task_key = 0;

	INIT_LIST_HEAD(&psandbox->activity->delay_list);

	write_lock(&psandbox_lock);
	list_add(&psandbox->list,&psandbox_list);
	write_unlock(&psandbox_lock);
	psandbox_id++;
	live_psandbox++;

	printk(KERN_INFO "psandbox syscall called psandbox_create id =%ld by thread %d\n",
	       psandbox->bid,current->pid);
	return current->pid;
}

SYSCALL_DEFINE1(release_psandbox, int, pid)
{
	struct task_struct *task = find_get_task_by_vpid(pid);
	if (!task) {
		printk(KERN_INFO "can't find sandbox based on the id\n");
		return -1;
	}
	if (!task->psandbox) {
		printk(KERN_INFO "there is no psandbox in task %d\n", task->pid);
		return 0;
	}
	clean_psandbox(task->psandbox);
	task->psandbox = NULL;
	task->is_psandbox=0;


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
	if (!psandbox) {
		printk(KERN_INFO "there is no psandbox\n");
		return 0;
	}
	do_freeze_psandbox(psandbox);


	return 0;
}

SYSCALL_DEFINE1(update_event, BoxEvent __user *, event) {
	int event_type = event->event_type, key = event->key;
	PSandbox *psandbox;
	PSandboxNode *cur;
	struct hlist_node *tmp;

	if (!current->psandbox || !event) {
//		printk(KERN_INFO "can't find sandbox for the update event %d\n",current->psandbox);
		return -1;
	}
	psandbox = current->psandbox;
	if(psandbox->is_lazy == 1)
		return 0;

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
		read_lock(&competitors_lock);
		hash_for_each_possible_safe(competitors_map, cur,tmp, node,key) {
			if (cur->psandbox == psandbox) {
				is_duplicate = true;
				break;
			}
		}
		read_unlock(&competitors_lock);

		if (!is_duplicate) {
			PSandboxNode* node = NULL;
			int i;

			for (i = 0; i< COMPETITORS_SIZE ; ++i) {
				if (psandbox->competitors[i].psandbox == NULL) {
					node = psandbox->competitors + i;
					break;
				}
			}
			if (!node)	{
				pr_info("call create for competitor\n");
				node = (PSandboxNode *)kzalloc(sizeof(PSandboxNode),GFP_KERNEL);
			}
			node->psandbox = psandbox;

			write_lock(&competitors_lock);
			hash_add(competitors_map,&node->node,key);
			write_unlock(&competitors_lock);
		}
		break;
	}
	case ENTER: {
		struct timespec64 current_tm, defer_tm;
		struct delaying_start *pos;
		psandbox->activity->activity_state = ACTIVITY_ENTER;
		//Free the competitors map
		write_lock(&competitors_lock);
		hash_for_each_possible_safe (competitors_map, cur, tmp, node, key) {
			if (cur->psandbox == psandbox) {
				hash_del(&cur->node);
				if (&psandbox->competitors[0] <= cur && cur < &psandbox->competitors[0] + COMPETITORS_SIZE) {
					cur->psandbox = NULL;
				}  else {
					kfree(cur);
				}
				break;
			}
		}
		write_unlock(&competitors_lock);

		ktime_get_real_ts64(&current_tm);

		list_for_each_entry(pos,&psandbox->activity->delay_list,list) {
			if (pos->key == key) {
				defer_tm = timespec64_sub(current_tm,pos->delaying_start);
				break;
			}
		}
		if(defer_tm.tv_sec == 0 && defer_tm.tv_nsec == 0) {
			printk (KERN_INFO "can't find the key for delaying start for psandbox %ld\n", psandbox->bid);
		}
		current_tm = psandbox->activity->defer_time;
		psandbox->activity->defer_time = timespec64_add(defer_tm, current_tm);
		break;
	}
	case HOLD: {
		int is_duplicate = false;
		// Add the holder map
		read_lock(&holders_lock);
		hash_for_each_possible_safe(holders_map, cur,tmp, node,key) {
			if (cur->psandbox == psandbox) {
				is_duplicate = true;
				break;
			}
		}
		read_unlock(&holders_lock);

		if (!is_duplicate) {
			PSandboxNode* node = NULL;
			int i;
			for (i = 0; i<PREALLOCATION_SIZE ; ++i) {
				if (psandbox->holders[i].psandbox == NULL) {
					node = psandbox->holders + i;
					break;
				}
			}
			if (!node)	{
//				pr_info("call create for holder\n");
				node = (PSandboxNode *)kzalloc(sizeof(PSandboxNode),GFP_KERNEL);
			}
			node->psandbox = psandbox;
			write_lock(&holders_lock);
			hash_add(holders_map,&node->node,key);
			write_unlock(&holders_lock);
		}
		break;
	}
	case UNHOLD: {
		int is_action = false;
		struct timespec64 current_tm, defer_tm, executing_tm,delaying_tm;
		ktime_t penalty_ns = 0;
		struct task_struct *good_task = NULL;
		struct task_struct *bad_task;
		PSandboxNode *current_psandbox = NULL;
		psandbox->activity->activity_state = ACTIVITY_EXIT;

		write_lock(&holders_lock);
		hash_for_each_possible_safe (holders_map, cur, tmp, node, key) {
			if (cur->psandbox == psandbox) {
				current_psandbox = cur;
				hash_del(&cur->node);
				break;
			}


		}
		write_unlock(&holders_lock);

		if(current_psandbox) {
			if (&psandbox->holders[0] <= cur && cur < &psandbox->holders[0] + HOLDER_SIZE) {
				cur->psandbox = NULL;
			}  else {
				kfree(cur);
			}
			return 1;
		}

		read_lock(&competitors_lock);

		// calculating the defering time
		hash_for_each_possible_safe (competitors_map, cur, tmp, node,
					     key) {
			if (psandbox->action_level != LOW_PRIORITY)
				break;

			if (cur->psandbox->bid != psandbox->bid) {
				struct delaying_start *pos;
				ktime_get_real_ts64(&current_tm);


				list_for_each_entry(pos,&cur->psandbox->activity->delay_list,list) {
					if (pos->key == key) {
						delaying_tm = pos->delaying_start;
						defer_tm = timespec64_sub(current_tm,pos->delaying_start);
						break;
					}
				}
				executing_tm = timespec64_sub(
					timespec64_sub(timespec64_sub(current_tm,psandbox->activity->execution_start),defer_tm),
					psandbox->activity->unbind_time);


				if(defer_tm.tv_sec == 0 && defer_tm.tv_nsec == 0) {
					printk (KERN_INFO "cann't find the key for delaying start for psandbox %ld\n", psandbox->bid);
				}
				executing_tm = timespec64_sub(timespec64_sub(current_tm,cur->psandbox->activity->execution_start),defer_tm);

//				printk (KERN_INFO "the executing time is %lu, the defer time is %lu for psandbox %d, current psandbox %d\n",
//				       timespec64_to_ns(&executing_tm),timespec64_to_ns(&defer_tm), cur->bid, psandbox->bid);
//
//				printk (KERN_INFO "the delaying start time is %lu\n",timespec64_to_ns(&cur->activity->delaying_start));
//				printk (KERN_INFO "the state is %d\n",cur->current_task->state);
				if (timespec64_to_ns(&defer_tm) > timespec64_to_ns(&executing_tm) *
				cur->psandbox->delay_ratio * live_psandbox) {
					penalty_ns += timespec64_to_ns(&defer_tm);
					good_task = find_get_task_by_vpid(cur->psandbox->current_task->pid);
				}

				if (cur->psandbox->action_level == HIGHEST_PRIORITY)
					is_action = true;
			}
		}
		read_unlock(&competitors_lock);
		if (is_action) {
			int penalty_us = 1000000;

			hash_for_each_possible_safe (competitors_map, cur, tmp, node,
						     key) {
				if (cur->psandbox->action_level == LOW_PRIORITY &&
				cur->psandbox->bid != psandbox->bid ) {
					unsigned long timeout;
					bad_task = find_get_task_by_vpid(cur->psandbox->current_task->pid);
					if (!bad_task || !bad_task->psandbox) {
						printk(KERN_INFO "can't find sandbox based on the id\n");
						continue;
					}
					timeout = usecs_to_jiffies(penalty_us);
					psandbox_schedule_timeout(timeout,bad_task);
				}

				if (cur->psandbox->action_level == HIGHEST_PRIORITY &&
					cur->psandbox->bid != psandbox->bid ) {
					good_task = find_get_task_by_vpid(cur->psandbox->current_task->pid);
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
			if (good_task) {
				wake_up_process(good_task);
			}

			if (penalty_ns > 1000000) {
				penalty_ns = 1000000;
				schedule_hrtimeout(&penalty_ns, HRTIMER_MODE_REL);
			} else {
				schedule_hrtimeout(&penalty_ns, HRTIMER_MODE_REL);
			}

		}
		break;
	}

	default:break;
	}
	return 0;
}

SYSCALL_DEFINE3(compensate_psandbox,int, noisy_pid,int, victim_pid, int, penalty_us)
{
	struct task_struct *task = find_get_task_by_vpid(noisy_pid);
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

	task = find_get_task_by_vpid(victim_pid);

	if (!task || !task->psandbox || !task->psandbox->activity) {
		printk(KERN_INFO "can't find sandbox based on the id %d\n",
		       victim_pid);
		return -1;
	}
//	printk(KERN_INFO
//	       "psandbox syscall called psandbox_wakeup pid =%d \n",
//	       task->pid);
	task->psandbox->state = BOX_AWAKE;
	wake_up_process(task);
	return 1;
}

SYSCALL_DEFINE1(wakeup_psandbox, int, pid)
{
	struct task_struct *task = find_get_task_by_vpid(pid);
//	PSandbox *psandbox;

	if (!task || !task->psandbox || !task->psandbox->activity) {
		printk(KERN_INFO "can't find sandbox based on the id %d\n",pid);
		return -1;
	}
//	printk(KERN_INFO
//	       "psandbox syscall called psandbox_wakeup pid =%d \n",
//	       task->pid);
	task->psandbox->state = BOX_AWAKE;
	wake_up_process(task);
	return 0;
}

SYSCALL_DEFINE2(penalize_psandbox, int, pid, int, penalty_us)
{
	ktime_t penalty = penalty_us * 1000;

	if(pid == current->pid) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_hrtimeout(&penalty, HRTIMER_MODE_REL);
	} else {
		struct task_struct *task = find_get_task_by_vpid(pid);
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

SYSCALL_DEFINE0(get_current_psandbox)
{
	if (!current->psandbox || current->psandbox->is_lazy) {
//		printk(KERN_INFO "there is no psandbox in current thread\n");
		return -1;
	}
	return current->pid;
}

SYSCALL_DEFINE1(get_psandbox, int, addr)
{
	PSandbox *psandbox = NULL;
	PSandboxNode *cur;
	struct hlist_node *tmp;
	read_lock(&transfers_lock);
	hash_for_each_possible_safe (transfers_map, cur, tmp, node, addr) {
		if (cur->psandbox->task_key == addr) {
			psandbox = cur->psandbox;
			break;
		}
	}
	read_unlock(&transfers_lock);
	if (!psandbox) {
		printk(KERN_INFO "can't find psandbox for addr %d\n", addr);
		return -1;
	}

	return psandbox->current_task->pid;
}

SYSCALL_DEFINE1(start_manager, u32 __user *, uaddr) {
	WhiteList *white_mutex;
	white_mutex = (WhiteList *)kzalloc(sizeof(WhiteList),GFP_KERNEL);
	white_mutex->addr = uaddr;
	INIT_LIST_HEAD(&white_mutex->list);
	list_add(&white_mutex->list,&white_list);
	return 0;
}

SYSCALL_DEFINE1(unbind_psandbox, u64, addr)
{
	PSandbox *psandbox = current->psandbox;

	if (!psandbox) {
		printk(KERN_INFO "can't find psandbox to unbind\n");
		return -1;
	}
	psandbox->is_lazy = 1;
	current->is_psandbox = 0;
	psandbox->task_key =  addr;
	ktime_get_real_ts64(&psandbox->activity->last_unbind_start);
//	do_freeze_psandbox(psandbox);
//	printk(KERN_INFO "lazy unbind psandbox %d to addr %d\n", psandbox->bid,addr);
	return psandbox->current_task->pid;
}

SYSCALL_DEFINE1(bind_psandbox, int, addr)
{
	PSandbox *psandbox = NULL;
	PSandboxNode *cur;
	struct hlist_node *tmp;
	int success;
//	struct timespec64 current_tm, unbind_tm;

	success = do_unbind(addr);
	if( success != 0 )
		return success;

	write_lock(&transfers_lock);
	hash_for_each_possible_safe (transfers_map, cur, tmp, node, addr) {
		if (cur->psandbox->task_key == addr) {
			psandbox = cur->psandbox;
			hash_del(&cur->node);
			if (&psandbox->transfers[0] <= cur && cur < &psandbox->transfers[0] + PREALLOCATION_SIZE) {
				cur->psandbox = NULL;
			}  else {
				kfree(cur);
			}
		}
	}

	write_unlock(&transfers_lock);

	if (!psandbox) {
		printk(KERN_INFO "can't find psandbox to bind %d\n",addr);
		return -1;
	}

	current->psandbox = psandbox;
	current->is_psandbox = 1;
	psandbox->current_task = current;
	psandbox->is_lazy = 0;
	psandbox->state = BOX_ACTIVE;
	ktime_get_real_ts64(&psandbox->activity->execution_start);
//	ktime_get_real_ts64(&current_tm);
//	unbind_tm =
//		timespec64_sub(current_tm, psandbox->activity->last_unbind_start);
//	current_tm = psandbox->activity->unbind_time;
//	psandbox->activity->unbind_time = timespec64_add(unbind_tm, current_tm);
//	printk(KERN_INFO "bind the psandbox %d (find by addr %d) to thread %d\n",psandbox->bid,addr,current->pid);

	return psandbox->current_task->pid;
}

int do_unbind(int addr){
	PSandboxNode *a = NULL;
	int i;
	PSandbox *psandbox = current->psandbox;

	if (!current->psandbox) {
//		printk(KERN_INFO "can't find psandbox to do unbind\n");
		return 0;
	}

	if(current->psandbox->task_key == addr) {
		current->psandbox->is_lazy = 0;
		current->psandbox->state = BOX_ACTIVE;
		ktime_get_real_ts64(&current->psandbox->activity->execution_start);
		return current->pid;
	}

	for (i = 0; i<PREALLOCATION_SIZE ; ++i) {
		if (psandbox->transfers[i].psandbox == NULL) {
			a = psandbox->transfers + i;
			break;
		}
	}

	if (!a)	{
		a = (PSandboxNode *)kzalloc(sizeof(PSandboxNode),GFP_KERNEL);
	}

	psandbox->current_task = NULL;
	current->psandbox = NULL;
	current->is_psandbox = 0;
	a->psandbox = psandbox;
	write_lock(&transfers_lock);
	hash_add(transfers_map,&a->node,psandbox->task_key);
	write_unlock(&transfers_lock);

//	printk(KERN_INFO "do unbind the psandbox %d to addr %d\n",psandbox->bid,addr);
	return 0;
}

void do_freeze_psandbox(PSandbox *psandbox){
//	struct timespec64 current_tm, total_time;
//	struct list_head temp;

	if (psandbox->compensation_ticket > 1) {
		psandbox->compensation_ticket--;
	} else {
		psandbox->action_level = LOW_PRIORITY;
	}

	psandbox->state = BOX_FREEZE;
	psandbox->finished_activities++;
	//	ktime_get_real_ts64(&current_tm);
	//	total_time = timespec64_sub(current_tm,psandbox->activity->execution_start);
	//	//	printk(KERN_INFO "the defer time is %lu ns, psandbox %d\n", timespec64_to_ktime(psandbox->activity->defer_time), psandbox->bid);
	//	psandbox->activity->execution_time = timespec64_sub(total_time,psandbox->activity->defer_time);
	//	if (live_psandbox)  {
	//		if(timespec64_to_ns(&psandbox->activity->execution_time) * psandbox->delay_ratio * live_psandbox
	//		<  timespec64_to_ns(&psandbox->activity->defer_time)) {
	//			psandbox->bad_activities++;
	//			if (psandbox->action_level == LOW_PRIORITY)
	//				psandbox->action_level = MID_PRIORITY;
	//			if (psandbox->action_level != HIGHEST_PRIORITY && psandbox->finished_activities > PROBING_NUMBER && psandbox->bad_activities * 100
	//			> (psandbox->tail_requirement * psandbox->finished_activities)) {
	//				psandbox->action_level = HIGHEST_PRIORITY;
	//				//				printk(KERN_INFO "give a ticket for %lu, bad activity %d, finish activity %d\n", psandbox->bid, psandbox->bad_activities, psandbox->finished_activities);
	//				psandbox->compensation_ticket = COMPENSATION_TICKET_NUMBER;
	//			}
	//		}
	//	}
	//	temp = psandbox->activity->delay_list;
	//	memset(psandbox->activity, 0, sizeof(Activity));
	//	psandbox->activity->delay_list = temp;
}

void clean_psandbox(PSandbox *psandbox) {
	unsigned bkt;
	struct hlist_node *tmp;
	PSandboxNode *cur;
	struct delaying_start *pos,*temp;
	printk(KERN_INFO "psandbox syscall called psandbox_release id =%ld by the thread %d\n",
	       psandbox->bid, current->pid);

	write_lock(&competitors_lock);
	hash_for_each_safe(competitors_map, bkt, tmp, cur, node) {
		if (cur->psandbox == psandbox) {
			hash_del(&cur->node);
			if (&psandbox->competitors[0] <= cur && cur < &psandbox->competitors[0] + COMPETITORS_SIZE) {
				cur->psandbox = NULL;
			}  else {
				kfree(cur);
			}
		}
	}
	write_unlock(&competitors_lock);
	write_lock(&holders_lock);
	hash_for_each_safe(holders_map, bkt, tmp, cur, node) {
		if (cur->psandbox == psandbox) {
			hash_del(&cur->node);
			if (&psandbox->holders[0] <= cur && cur < &psandbox->holders[0] + HOLDER_SIZE) {
				cur->psandbox = NULL;
			}  else {
				kfree(cur);
			}
		}
	}
	write_unlock(&holders_lock);
	write_lock(&transfers_lock);
	hash_for_each_safe(transfers_map, bkt, tmp, cur, node) {
		if (cur->psandbox == psandbox) {
			hash_del(&cur->node);
			if (&psandbox->transfers[0] <= cur && cur < &psandbox->transfers[0] + PREALLOCATION_SIZE) {
				cur->psandbox = NULL;
			}  else {
				kfree(cur);
			}
		}
	}
	write_unlock(&transfers_lock);
	live_psandbox--;

	list_for_each_entry_safe(pos,temp,&psandbox->activity->delay_list,list) {
		kfree(pos);
	}

	write_lock(&psandbox_lock);
	list_del(&psandbox->list);
	write_unlock(&psandbox_lock);
	kfree(psandbox->activity);
	kfree(psandbox);
}

void clean_unbind_psandbox(struct task_struct *task) {
	unsigned bkt;
	struct hlist_node *tmp;
	PSandboxNode *cur;
	write_lock(&transfers_lock);
	hash_for_each_safe(transfers_map, bkt, tmp, cur, node) {
		if (cur->psandbox->creator_psandbox->pid == task->pid) {
			pr_info("the addr is %d, the lazy is %d for psandbox %d\n",cur->psandbox->task_key, cur->psandbox->is_lazy, cur->psandbox->bid);
			hash_del(&cur->node);
			write_unlock(&transfers_lock);
			clean_psandbox(cur->psandbox);
			write_lock(&transfers_lock);
		}
	}
	write_unlock(&transfers_lock);
}

PSandbox *get_psandbox(int bid) {
	PSandbox *pos = NULL;

	read_lock(&psandbox_lock);
	list_for_each_entry(pos,&psandbox_list,list) {
		if (pos->bid == bid) {
			read_unlock(&psandbox_lock);
			return pos;
		}

	}

	read_unlock(&psandbox_lock);
	return pos;
}

static int psandbox_init(void)
{
	INIT_LIST_HEAD(&white_list);
	INIT_LIST_HEAD(&psandbox_list);
	hash_init(competitors_map);
	hash_init(holders_map);
	hash_init(transfers_map);
	return 0;
}
core_initcall(psandbox_init);