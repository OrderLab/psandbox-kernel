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
ktime_t average_defer_time = 0 ;

__cacheline_aligned DEFINE_RWLOCK(transfers_lock);
__cacheline_aligned DEFINE_RWLOCK(competitors_lock);
__cacheline_aligned DEFINE_RWLOCK(holders_lock);
__cacheline_aligned DEFINE_RWLOCK(psandbox_lock);
__cacheline_aligned DEFINE_RWLOCK(stat_map_lock);
#define COMPENSATION_TICKET_NUMBER	1000L
#define BASE_RATE 5


static DEFINE_SPINLOCK(stat_lock);
DECLARE_HASHTABLE(competitors_map, 10);
DECLARE_HASHTABLE(holders_map, 10);
DECLARE_HASHTABLE(transfers_map, 10);
DECLARE_HASHTABLE(stat_map,10);

/* This function will create a psandbox and bind to the current thread */
SYSCALL_DEFINE3(create_psandbox, int, type, int, isolation_level, int, priority)
{
	PSandbox *psandbox;
	unsigned long flags;

	psandbox = (PSandbox *)kzalloc(sizeof(PSandbox), GFP_KERNEL);
	if (!psandbox) {
		return -1;
	}

	if(current->psandbox && current->psandbox->is_lazy) {
		do_unbind(0);
	}

	psandbox->current_task = current;
	psandbox->activity = (Activity *)kzalloc(sizeof(Activity), GFP_KERNEL);
	psandbox->state = BOX_START;
	psandbox->white_list = &white_list;
	current->psandbox = psandbox;
	current->is_psandbox = 1;
	current->is_creator = 1;
	psandbox->finished_activities = 0;
	psandbox->action_level = LOW_PRIORITY;
	psandbox->is_white = 0;
	psandbox->delay_ratio = 1;
	psandbox->is_futex = 0;
	psandbox->tail_requirement = 90;
	psandbox->bad_activities = 0;
	psandbox->creator_psandbox = current;
	psandbox->is_lazy = 0;
	psandbox->task_key = 0;
	psandbox->count = 0;
	psandbox->rule.type = type;
	psandbox->rule.isolation_level = isolation_level;
	psandbox->priority = priority;
        spin_lock_init(&psandbox->lock);
	INIT_LIST_HEAD(&psandbox->delay_list);

	write_lock(&psandbox_lock);
	list_add(&psandbox->list,&psandbox_list);
	write_unlock(&psandbox_lock);
	spin_lock_irqsave(&stat_lock, flags);
	psandbox_id++;
	spin_unlock_irqrestore(&stat_lock, flags);
	psandbox->bid = psandbox_id;
	
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

SYSCALL_DEFINE0(activate_psandbox)
{
	PSandbox *psandbox = current->psandbox;
	if (!psandbox) {
		printk(KERN_INFO "there is no psandbox\n");
		return 0;
	}
	psandbox->state = BOX_ACTIVE;
	ktime_get_real_ts64(&psandbox->activity->execution_start);
//	if(psandbox->bid == 2)
//		pr_info("call do activate %d\n",psandbox->bid);
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

SYSCALL_DEFINE2(update_event, BoxEvent __user *, event, int, is_lazy) {
	BoxEvent boxevent;
	unsigned int event_type, key;
	PSandbox *psandbox;
	PSandboxNode *cur;
	StatisticNode *stat_cur = NULL;
	struct hlist_node *tmp;

	if (copy_from_user(&boxevent, event, sizeof(*event))) {
		pr_info("cannot read boxevent %p\n", event);
		return -EINVAL;
	}
	event_type = boxevent.event_type;
	key = boxevent.key;
	if (!current->psandbox || !event) {
//		printk(KERN_INFO "can't find sandbox for the update event %d\n",current->psandbox);
		return -1;
	}
	psandbox = current->psandbox;
	psandbox->count++;
	if(psandbox->is_lazy == 1)
		return 0;


	switch (event_type) {
	case PREPARE:{
		int is_duplicate = false, is_first = true;
		struct delaying_start *pos;
		PSandboxNode* node = NULL;
		int i;

		psandbox->activity->activity_state = ACTIVITY_WAITING;

		//Add event to the competitor map, update the defer time
		for (i = 0; i< COMPETITORS_SIZE ; ++i) {
			if (psandbox->competitors[i].psandbox == NULL) {
				node = psandbox->competitors + i;
				break;
			}
		}
		if (!node) {
			pr_info("create for competitor\n");
			node = (PSandboxNode *)kzalloc(sizeof(PSandboxNode),GFP_KERNEL);
		}
		node->psandbox = psandbox;
		ktime_get_real_ts64(&node->delaying_start);
		write_lock(&competitors_lock);
		hash_add(competitors_map,&node->node,key);
		write_unlock(&competitors_lock);


		read_lock(&stat_map_lock);
		hash_for_each_possible_safe(stat_map, stat_cur,tmp, node,key) {
			if (stat_cur->psandbox == psandbox) {
				is_duplicate = true;
				break;
			}
		}
		read_unlock(&stat_map_lock);
		if (!is_duplicate) {
			StatisticNode *stat_node;
			stat_node = (StatisticNode *)kzalloc(sizeof(StatisticNode),GFP_KERNEL);
			stat_node->bad_action = 0;
			stat_node->psandbox = psandbox;
			spin_lock_init(&stat_node->stat_lock);
			write_lock(&stat_map_lock);
			hash_add(stat_map,&stat_node->node,key);
			write_unlock(&stat_map_lock);
		}
		break;
	}
	case ENTER: {
		struct timespec64 current_tm, defer_tm;
		struct delaying_start *pos;
		psandbox->activity->activity_state = ACTIVITY_ENTER;
		// Free the competitors map
		defer_tm.tv_sec = -1;

		ktime_get_real_ts64(&current_tm);
		write_lock(&competitors_lock);
		hash_for_each_possible_safe (competitors_map, cur, tmp, node, key) {
			if (cur->psandbox == psandbox) {
				defer_tm = timespec64_sub(current_tm,cur->delaying_start);
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

		if(defer_tm.tv_sec == -1) {
			printk (KERN_INFO "can't find the key for delaying start for psandbox %ld\n", psandbox->bid);
		}

		current_tm = psandbox->activity->defer_time;
		psandbox->activity->defer_time = timespec64_add(defer_tm, current_tm);
		break;
	}
	case UNHOLD: {
		struct timespec64 current_tm, defer_tm, executing_tm;
		ktime_t penalty_ns = 0, old_defer = 0, old_execution = 1,
			current_defer, current_execution;
		PSandbox *victim = NULL;
		psandbox->unhold++;
		psandbox->activity->activity_state = ACTIVITY_EXIT;
		// calculating the defering time
		int count = 0;
		read_lock(&competitors_lock);
		hash_for_each_possible_safe (competitors_map, cur, tmp, node, key) {
			int is_noisy = false;

			if (psandbox->action_level != LOW_PRIORITY)
				break;

			defer_tm.tv_sec = -1;
			if (cur->psandbox->bid != psandbox->bid) {
				count++;
				switch (cur->psandbox->rule.type) {
					case RELATIVE:
						if (cur->psandbox->average_defer_time * 100  < cur->psandbox->average_execution_time * cur->psandbox->rule.isolation_level ) {
							continue;
						}
						pr_info("after call continues %ld, defer time %llu, execution time %llu\n", cur->psandbox->bid,cur->psandbox->average_defer_time, cur->psandbox->average_execution_time);
						break;
					case SCALABLE:
						if (cur->psandbox->average_defer_time * 100  < cur->psandbox->average_execution_time * cur->psandbox->rule.isolation_level * live_psandbox) {
							continue;
						}
//						pr_info("after call continues %ld, defer time %llu, execution time %llu\n", cur->psandbox->bid,cur->psandbox->average_defer_time, cur->psandbox->average_execution_time);
						break;
					default: break;
				}
				ktime_get_real_ts64(&current_tm);
				defer_tm = timespec64_sub(current_tm,cur->delaying_start);

				if (defer_tm.tv_sec == -1) {
					pr_info("2. can't find the key for delaying start for psandbox %ld\n", psandbox->bid);
				}
				executing_tm = timespec64_sub(timespec64_sub(current_tm, cur->psandbox->activity ->execution_start), defer_tm);
//				pr_info ("current time %lu, executing start %lu ns, the executing time is %lu ns, the defer time is %lu ns for psandbox %d, current psandbox %d\n",timespec64_to_ns(&current_tm),timespec64_to_ns(&cur->psandbox->activity->execution_start),timespec64_to_ns(&executing_tm),timespec64_to_ns(&defer_tm), cur->psandbox->bid, psandbox->bid);
				current_defer = timespec64_to_ns(&defer_tm);
				current_execution = timespec64_to_ns(&executing_tm);
				switch (cur->psandbox->rule.type) {
					case RELATIVE:
						if (current_defer * 100 > current_execution * cur->psandbox->rule.isolation_level) {
							is_noisy = true;
						}
						break;
					case SCALABLE:
						if (current_defer * 100 >current_execution * cur->psandbox->rule.isolation_level * live_psandbox) {
							is_noisy = true;
						}
						break;
					default: break;
				}

				if (is_noisy) {
//					 printk (KERN_INFO "the defer time is %ld for psandbox %ld\n",timespec64_to_ns(&defer_tm),cur->psandbox->bid);
					// Find the psandbox that is interferenced most
					if (current_defer * old_execution > old_defer * current_execution) {
						old_defer = current_defer;
						old_execution = current_execution;
						penalty_ns = current_defer ;
						victim = cur->psandbox;
					}
				}
			}
		}
		if (count == 0) {
			psandbox->competitor++;
		}
		read_unlock(&competitors_lock);

		if (is_lazy) {
			if (penalty_ns > 10000 && victim) {
				psandbox->activity->victim_id = victim->current_task->pid;
				psandbox->activity->key = key;
				psandbox->activity->penalty_ns = penalty_ns;
				pr_info("call do update %d, victim id %d, key %lu \n",psandbox->bid,psandbox->activity->victim_id,psandbox->activity->key);
				return penalty_ns;
			} else {
				pr_info("the penalty is %lu\n",penalty_ns);
				return 0;
			}

		}

		if (penalty_ns > 10000 && victim) {
			do_penalty(victim,penalty_ns, key);
		}
		break;
	}
	default:break;
	}

	return 0;
}
// TODO: add delay penalty
void do_penalty(PSandbox *victim, ktime_t penalty_ns, unsigned int key) {
	StatisticNode *stat_node = NULL;
	StatisticNode *stat_cur = NULL;
	struct hlist_node *tmp;
	ktime_t old_slack = victim->total_defer_time;
	ktime_t new_slack = victim->total_execution_time;
	read_lock(&stat_map_lock);
	hash_for_each_possible_safe (stat_map, stat_cur, tmp, node, key) {
		if (stat_cur->psandbox == victim) {
			stat_node = stat_cur;
			break;
		}
	}
	read_unlock(&stat_map_lock);

	if(!stat_node) {
		pr_info("Can't find the psandbox %ld in the stat map with key %u\n",victim->bid,key);
		return;
	}

	if (stat_node->bad_action && stat_node->bad_action > BASE_RATE)
		penalty_ns *= stat_node->bad_action / BASE_RATE;

	victim->state = BOX_AWAKE;
	wake_up_process(victim->current_task);
	__set_current_state(TASK_INTERRUPTIBLE);

	if (penalty_ns > 10000000000) {
		penalty_ns = 10000000000;
		pr_info("event: sleep psandbox %d, thread %d, defer time %u, score %d\n", current->psandbox->bid, current->pid, penalty_ns,stat_node->bad_action);
		schedule_hrtimeout(&penalty_ns,HRTIMER_MODE_REL);
	} else {
//		penalty_ns=2000000000;
		pr_info("event: sleep psandbox %d, thread %d, defer time %u, score %d\n", current->psandbox->bid, current->pid, penalty_ns,stat_node->bad_action);
		schedule_hrtimeout(&penalty_ns,HRTIMER_MODE_REL);
	}

	new_slack *= victim->total_defer_time;
	old_slack *= victim->total_execution_time;
	switch (victim->rule.type) {
	case RELATIVE:
		if (victim->total_defer_time * 100 > victim->total_execution_time * victim->rule.isolation_level) {
			spin_lock(&stat_node->stat_lock);
			stat_node->bad_action++;
			spin_unlock(&stat_node->stat_lock);
		} else if (new_slack < old_slack)  {
			spin_lock(&stat_node->stat_lock);
			stat_node->bad_action++;
			spin_unlock(&stat_node->stat_lock);
		} else if (stat_node->bad_action > 1) {
			spin_lock(&stat_node->stat_lock);
			stat_node->bad_action--;
			spin_unlock(&stat_node->stat_lock);
		}
		break;
		case SCALABLE:
			if (victim->total_defer_time * 100 > victim->total_execution_time * victim->rule.isolation_level * live_psandbox) {
				spin_lock(&stat_node->stat_lock);
				stat_node->bad_action++;
				spin_unlock(&stat_node->stat_lock);
			} else if (new_slack < old_slack) {
				spin_lock(&stat_node->stat_lock);
				stat_node->bad_action++;
				spin_unlock(&stat_node->stat_lock);
			} else if (stat_node->bad_action > 1) {
				spin_lock(&stat_node->stat_lock);
				stat_node->bad_action--;
				spin_unlock(&stat_node->stat_lock);
			}
			break;
			default:
				break;
	}
}

SYSCALL_DEFINE0(get_current_psandbox)
{
	if (!current->psandbox || current->psandbox->is_lazy) {
//		printk(KERN_INFO "there is no psandbox in current thread\n");
		return -1;
	}
	return current->psandbox->bid;
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

SYSCALL_DEFINE2(annotate_resource, u32 __user *, uaddr, int, action_type) {
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
	do_freeze_psandbox(psandbox);
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

SYSCALL_DEFINE2(penalize_psandbox, long int, penalty_us,unsigned int, key)
{
	ktime_t penalty = penalty_us * 1000;
	struct task_struct *task;
	if(!current->psandbox)
		return 0;

	task = find_get_task_by_vpid(current->psandbox->activity->victim_id);
	if(task) {

		PSandbox *victim = task->psandbox;
		if(victim)
			do_penalty(victim,penalty,key);

	}


	return 0;
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
	struct timespec64 current_tm, total_time;
//	ktime_t average_defer;
//	struct list_head temp;
	ktime_t defer_tm;
	unsigned long flags;

	psandbox->state = BOX_FREEZE;
	psandbox->finished_activities++;
	ktime_get_real_ts64(&current_tm);
	total_time = timespec64_sub(current_tm,psandbox->activity->execution_start);
	psandbox->activity->execution_time = timespec64_sub(total_time,psandbox->activity->defer_time);
	psandbox->total_execution_time += timespec64_to_ns(&psandbox->activity->execution_time);

	defer_tm = timespec64_to_ns(&psandbox->activity->defer_time);
	psandbox->total_defer_time += defer_tm;
//	average_defer = psandbox->total_defer_time/psandbox->finished_activities;
	spin_lock_irqsave(&stat_lock, flags);
//	average_defer_time = average_defer_time * live_psandbox + (average_defer - psandbox->average_defer_time);
	spin_unlock_irqrestore(&stat_lock, flags);
	psandbox->average_defer_time = psandbox->total_defer_time/psandbox->finished_activities;
	psandbox->average_execution_time = psandbox->total_execution_time/psandbox->finished_activities;

//	if (timespec64_to_ns(&psandbox->activity->execution_time) * psandbox->delay_ratio * live_psandbox < defer_tm) {
//		psandbox->bad_activities++;
//	}
//	if(psandbox->bid == 2)
//		pr_info("call do freeze %d, victim id %d, key %lu \n",psandbox->bid,psandbox->activity->victim_id,psandbox->activity->key);
	if (psandbox->activity->victim_id && psandbox->activity->key) {
		struct task_struct *task = find_get_task_by_vpid(psandbox->activity->victim_id);
		if(task) {
			PSandbox *victim = task->psandbox;
			if(victim)
				do_penalty(victim,psandbox->activity->penalty_ns,psandbox->activity->key);
		}
	}

	memset(psandbox->activity, 0, sizeof(Activity));
}

void clean_psandbox(PSandbox *psandbox) {
	unsigned bkt;
	struct hlist_node *tmp;
	PSandboxNode *cur;
	StatisticNode *stat_cur;
	struct delaying_start *pos,*temp;
	if (psandbox->finished_activities > 0) {
		pr_info( "psandbox syscall called psandbox_release id =%ld by the thread %d, total defer time %llu ns, total execution time %llu ns \n",
		       psandbox->bid, current->pid,psandbox->total_defer_time, psandbox->total_execution_time);
	} else {
		pr_info("psandbox syscall called psandbox_release id =%ld by the thread %d\n",
		       psandbox->bid, current->pid);
	}


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
//	write_lock(&holders_lock);
//	hash_for_each_safe(holders_map, bkt, tmp, cur, node) {
//		if (cur->psandbox == psandbox) {
//			hash_del(&cur->node);
//			if (&psandbox->holders[0] <= cur && cur < &psandbox->holders[0] + HOLDER_SIZE) {
//				cur->psandbox = NULL;
//			}  else {
//				kfree(cur);
//			}
//		}
//	}
//	write_unlock(&holders_lock);
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
	write_lock(&stat_map_lock);
	hash_for_each_safe(stat_map, bkt, tmp, stat_cur, node) {
		if (stat_cur->psandbox == psandbox) {
			hash_del(&stat_cur->node);
			kfree(stat_cur);
			break;
		}
	}
	write_unlock(&stat_map_lock);
	live_psandbox--;

	list_for_each_entry_safe(pos,temp,&psandbox->delay_list,list) {
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
//			pr_info("the addr is %d, the lazy is %d for psandbox %d\n",cur->psandbox->task_key, cur->psandbox->is_lazy, cur->psandbox->bid);
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
	hash_init(stat_map);
	return 0;
}
core_initcall(psandbox_init);
