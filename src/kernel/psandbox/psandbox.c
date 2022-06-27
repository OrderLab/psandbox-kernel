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
#define BASE_RATE 1
#define LONG_SECTION 1000
#define STEP 20


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
	psandbox->is_futex =0;
	psandbox->bad_activities = 0;
	psandbox->creator_psandbox = current;
	psandbox->is_lazy = 0;
	// psandbox->is_accept = 0;
	psandbox->unbind_flags = UNBIND_NONE;
	psandbox->requeued = 0;
	psandbox->task_key = 0;
	psandbox->count = 0;
	psandbox->rule.type = type;
	psandbox->rule.isolation_level = isolation_level;
	psandbox->rule.is_retro = false;
	psandbox->priority = priority;
	psandbox->is_nice = 0;
	psandbox->step = 1;
	psandbox->should_penalize_in_queue = 0;
	psandbox->in_queue_penalty_time = 0;
	psandbox->in_queue_victim = NULL;
	psandbox->event_key = 0;
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

	pr_info("psandbox syscall called psandbox_create id =%ld by thread %d\n",
	       psandbox->bid,current->pid);
	return psandbox->bid;
}

SYSCALL_DEFINE1(release_psandbox, int, pid)
{
	PSandbox *psandbox = get_psandbox(pid);
	struct task_struct *task = psandbox->current_task;
	if (!psandbox) {
		printk(KERN_INFO "can't find sandbox based on the id\n");
		return -1;
	}
	if (!task) {
		printk(KERN_INFO "psandbox has already been released\n");

		return 0;
	}

	clean_psandbox(psandbox);
	task->psandbox = NULL;
	task->is_psandbox = 0;
	return 0;
}

SYSCALL_DEFINE0(activate_psandbox)
{
	PSandbox *psandbox = current->psandbox;



	ktime_t penalty_ns;
	PSandbox *pos = NULL;
	if (!psandbox) {
		pr_info("there is no psandbox\n");
		return 0;
	}

	psandbox->state = BOX_ACTIVE;
	ktime_get_real_ts64(&psandbox->activity->execution_start);


	if (psandbox->rule.is_retro) {
		DemandNode demand[100];
		int slowdown, current_demand, i = 0, unsatisfied,fair=0, capacity = 0, total = 0;
		if (psandbox->average_defer_time == 0) {
			return 0;
		} else {
			slowdown = psandbox->average_execution_time/(psandbox->average_execution_time-psandbox->average_defer_time);
			current_demand = psandbox->average_execution_time-psandbox->average_defer_time;
		}

		write_lock(&psandbox_lock);
		list_for_each_entry(pos,&psandbox_list,list) {
			int load = pos->average_execution_time -
				   pos->average_defer_time;
			demand[i].demand = load;
			demand[i].psandbox = pos;
			demand[i].is_satisfied = 0;
			i++;
			capacity += load / 2;
		}
		total = capacity;
		unsatisfied = i;
		do {
			int assigned, unused = 0,j;
			if ( unsatisfied == 0) {
//				pr_info("there is no live_psandbox\n");
				break;
			} else {
				assigned = total/unsatisfied;
			}

			for (j=0; j < i; j++) {
				if (demand[j].is_satisfied) {
					continue;
				}

				if (assigned > demand[j].demand) {
					unused = assigned - demand[j].demand;
					unsatisfied--;
					demand[j].is_satisfied = true;
					if (demand[j].psandbox == psandbox) {
						fair += demand[j].demand;
					}
				} else {
					if (demand[j].psandbox == psandbox) {
						fair += assigned;
					}
				}
			}
			total = unused;
//			pr_info("The total is %d")
		} while (total > 0);

		list_for_each_entry(pos,&psandbox_list,list) {
			if (pos->average_defer_time == 0 || pos == psandbox ) {
				continue;
			} else {
				slowdown = pos->average_execution_time/(pos->average_execution_time-pos->average_defer_time);
			}
			if (slowdown > 10 && fair < current_demand) {
				penalty_ns = current_demand - fair;
				schedule_hrtimeout(&penalty_ns,HRTIMER_MODE_REL);
				break;
			}
		}
		write_unlock(&psandbox_lock);

	}
	return 0;
}

SYSCALL_DEFINE0(freeze_psandbox)
{
	PSandbox *psandbox = current->psandbox;
	if (!psandbox) {
		pr_info("there is no psandbox\n");
		return 0;
	}
	do_freeze_psandbox(psandbox);


	return 0;
}

int do_prepare(PSandbox *psandbox, unsigned int key) {
	/* PSandbox *psandbox; */
	PSandboxNode *cur;
	StatisticNode *stat_cur = NULL;
	struct hlist_node *tmp;

	if (!psandbox) {
		return -1;
	}
	/* psandbox = current->psandbox; */
	psandbox->count++;

	if(psandbox->is_lazy == 1)
		return 0;

	// in switch
	int is_duplicate = false;
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
	hash_add(competitors_map,&node->node, key);
	write_unlock(&competitors_lock);


	read_lock(&stat_map_lock);
	hash_for_each_possible_safe(stat_map, stat_cur,tmp, node, key) {
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
		stat_node->step = 1;
		spin_lock_init(&stat_node->stat_lock);
		write_lock(&stat_map_lock);
		hash_add(stat_map,&stat_node->node, key);
		write_unlock(&stat_map_lock);
	}

	return 0;
}


int do_enter(PSandbox *psandbox, unsigned int key) {
	PSandboxNode *cur;
	StatisticNode *stat_cur = NULL;
	struct hlist_node *tmp;

	if (!psandbox) {
		return -1;
	}
	psandbox->count++;

	if(psandbox->is_lazy == 1)
		return 0;

	// enter start
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
			} else {
				kfree(cur);
			}
			break;
		}
	}
	write_unlock(&competitors_lock);

	if(defer_tm.tv_sec == -1) {
		printk (KERN_INFO "can't find the key for delaying start for psandbox %ld\n", psandbox->bid);
	}
	psandbox->activity->c_resource_numbers++;
	current_tm = psandbox->activity->defer_time;
	ktime_t dt = timespec64_to_ns(&current_tm);
	psandbox->activity->defer_time = timespec64_add(defer_tm, current_tm);
}


int do_unhold(PSandbox *psandbox, unsigned int key, unsigned int event_type) {
	PSandboxNode *cur;
	StatisticNode *stat_cur = NULL;
	struct hlist_node *tmp;

	if (!psandbox) {
		return -1;
	}
	psandbox->count++;

	if(psandbox->is_lazy == 1)
		return 0;

	// unhold start
	int is_lazy = 0; // not doing lazy

	struct timespec64 current_tm, defer_tm, executing_tm;
	ktime_t penalty_ns = 0, old_defer = 0, old_execution = 1,
		current_defer, current_execution;
	PSandbox *victim = NULL;
	psandbox->unhold++;
	psandbox->activity->activity_state = ACTIVITY_EXIT;
	psandbox->activity->c_resource_numbers--;
		  //pr_info("call UNHOLD for psandbox %d\n",current->psandbox->bid);
		  // calculating the defering time

	/* printk(KERN_INFO "0. psandbox=%d unhold, avg defer time %lu", psandbox->bid); */

	int count = 0;
	read_lock(&competitors_lock);
	hash_for_each_possible_safe (competitors_map, cur, tmp, node, key) {
		int is_noisy = false;
		if (psandbox->action_level != LOW_PRIORITY)
			continue;

		defer_tm.tv_sec = -1;
		if (cur->psandbox->bid != psandbox->bid) {
			count++;
			/* printk(KERN_INFO "1. psandbox=%d looping through competitors_map %d, avg_defer_tm=%lu, threshold=%lu", psandbox->bid, count, */
			/* 	cur->psandbox->average_defer_time * 1000,  9 * cur->psandbox->average_execution_time * cur->psandbox->rule.isolation_level); */

			switch (cur->psandbox->rule.type) {
			case RELATIVE:
				if (cur->psandbox->average_defer_time * 1000 < 9 * cur->psandbox->average_execution_time * cur->psandbox->rule.isolation_level) {
					/* printk(KERN_INFO "2. psandbox=%d skip! looping through competitors_map %d", psandbox->bid, count); */
					continue;
				}
//						pr_info("after call continues %ld, defer time %llu, execution time %llu\n", cur->psandbox->bid,cur->psandbox->average_defer_time, cur->psandbox->average_execution_time);
				break;
			case SCALABLE:
				if (cur->psandbox->average_defer_time * 1000  < 9 * cur->psandbox->average_execution_time * cur->psandbox->rule.isolation_level * live_psandbox) {
					continue;
				}
//						pr_info("after call continues %ld, defer time %llu, execution time %llu\n", cur->psandbox->bid,cur->psandbox->average_defer_time, cur->psandbox->average_execution_time);
				break;
			default: break;
			}

			/* printk(KERN_INFO "psandbox=%d 3 calculating defer time! looping through competitors_map %d", psandbox->bid, count); */
			ktime_get_real_ts64(&current_tm);
			defer_tm = timespec64_sub(current_tm,cur->delaying_start);
			defer_tm = timespec64_sub(defer_tm,cur->psandbox->activity->defer_time);

			if (defer_tm.tv_sec == -1) {
			//	pr_info("2. can't find the key for delaying start for psandbox %ld\n", psandbox->bid);
			}
			executing_tm = timespec64_sub(timespec64_sub(current_tm, cur->psandbox->activity->execution_start), defer_tm);
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
					penalty_ns = current_defer;
					victim = cur->psandbox;
				}
			}
		}
	}
	if (count == 0) {
		psandbox->competitor++;
	}
	read_unlock(&competitors_lock);

	if (event_type == UNHOLD) {
		if (is_lazy) {
			if (penalty_ns > 10000 && victim) {
				psandbox->activity->victim_id = victim->current_task->pid;
				psandbox->activity->key = key;
				psandbox->activity->penalty_ns = penalty_ns;
//				pr_info("call do update %d, victim id %d, key %lu \n",psandbox->bid,psandbox->activity->victim_id,psandbox->activity->key);
				return penalty_ns;
			} else {
//				pr_info("the penalty is %lu\n",penalty_ns);
				return 0;
			}
		}

		if (penalty_ns > 10000 && victim) {
			if (penalty_ns > victim->average_execution_time * LONG_SECTION) {
				penalty_ns = calculate_starting_penalty_ns(victim,penalty_ns,psandbox,4);
				do_penalty(victim,penalty_ns, key,true);
			} else { // FIXME victim could be NULL pointer?
				penalty_ns = calculate_starting_penalty_ns(victim,penalty_ns,psandbox,5);
				do_penalty(victim,penalty_ns, key,false);
			}

		} else if (penalty_ns != 0){
			pr_info("3. skip event: sleep psandbox %d, thread %d, defer time %llu\n", current->psandbox->bid, current->pid, penalty_ns);
		}
	}

	else if (event_type == UNHOLD_IN_QUEUE_PENALTY) {
		if (penalty_ns > 10000 && victim) {
			if (penalty_ns > victim->average_execution_time * LONG_SECTION) {
				penalty_ns = calculate_starting_penalty_ns(victim,penalty_ns,psandbox,4);
			} else {
				penalty_ns = calculate_starting_penalty_ns(victim,penalty_ns,psandbox,5);
			}
			psandbox->should_penalize_in_queue++;
			psandbox->in_queue_penalty_time += penalty_ns; // or we call it requeue time to wake up (curr tm + penalty_ns)
		}
		//XXX assuming only one user queue unhold event
		//psandbox->in_queue_victim = victim;
		if (victim) {
			victim->should_penalize_in_queue = 0;
			victim->in_queue_penalty_time = 0;
		}
	}

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
//		pr_info("can't find sandbox for the update event %d\n",current->psandbox);
		return -1;
	}
	psandbox = current->psandbox;
	psandbox->count++;
	if(psandbox->is_lazy == 1)
		return 0;


	switch (event_type) {
	case PREPARE: {
		int is_duplicate = false;
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
		hash_add(competitors_map,&node->node, key);
		write_unlock(&competitors_lock);


		read_lock(&stat_map_lock);
		hash_for_each_possible_safe(stat_map, stat_cur,tmp, node, key) {
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
			stat_node->step = 1;
			spin_lock_init(&stat_node->stat_lock);
			write_lock(&stat_map_lock);
			hash_add(stat_map,&stat_node->node, key);
			write_unlock(&stat_map_lock);
		}
		break;
	}
	case ENTER: {
		struct timespec64 current_tm, defer_tm;
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
				} else {
					kfree(cur);
				}
				break;
			}
		}
		write_unlock(&competitors_lock);

		if(defer_tm.tv_sec == -1) {
			pr_info ("can't find the key for delaying start for psandbox %ld\n", psandbox->bid);
		}
		psandbox->activity->c_resource_numbers++;
		current_tm = psandbox->activity->defer_time;
		ktime_t dt = timespec64_to_ns(&current_tm);
		psandbox->activity->defer_time = timespec64_add(defer_tm, current_tm);
		/* printk(KERN_INFO "--ENTER-- psandbox %d: defer time=%lu + %lu = %lu\n", */
			/* psandbox->bid, dt, timespec64_to_ns(&defer_tm), timespec64_to_ns(&psandbox->activity->defer_time)); */
		break;
	}
	case UNHOLD:
	case UNHOLD_IN_QUEUE_PENALTY: {
		struct timespec64 current_tm, defer_tm, executing_tm;
		ktime_t penalty_ns = 0, old_defer = 0, old_execution = 1,
			current_defer, current_execution;
		PSandbox *victim = NULL;
		int count = 0;
		psandbox->unhold++;
		psandbox->activity->activity_state = ACTIVITY_EXIT;
		psandbox->activity->c_resource_numbers--;
		int count = 0;

		read_lock(&competitors_lock);
		hash_for_each_possible_safe (competitors_map, cur, tmp, node, key) {
			int is_noisy = false;
			if (psandbox->action_level != LOW_PRIORITY)
				continue;

			defer_tm.tv_sec = -1;
			if (cur->psandbox->bid != psandbox->bid) {
				count++;
				/* printk(KERN_INFO "1. psandbox=%d looping through competitors_map %d, avg_defer_tm=%lu, threshold=%lu", psandbox->bid, count, */
				/* 	cur->psandbox->average_defer_time * 1000,  9 * cur->psandbox->average_execution_time * cur->psandbox->rule.isolation_level); */

				switch (cur->psandbox->rule.type) {
				case RELATIVE:
					if (cur->psandbox->average_defer_time * 1000 < 9 * cur->psandbox->average_execution_time * cur->psandbox->rule.isolation_level) {
						/* printk(KERN_INFO "2. psandbox=%d skip! looping through competitors_map %d", psandbox->bid, count); */
						continue;
					}
//						pr_info("after call continues %ld, defer time %llu, execution time %llu\n", cur->psandbox->bid,cur->psandbox->average_defer_time, cur->psandbox->average_execution_time);
					break;
				case SCALABLE:
					if (cur->psandbox->average_defer_time * 1000  < 9 * cur->psandbox->average_execution_time * cur->psandbox->rule.isolation_level * live_psandbox) {
						continue;
					}
//						pr_info("after call continues %ld, defer time %llu, execution time %llu\n", cur->psandbox->bid,cur->psandbox->average_defer_time, cur->psandbox->average_execution_time);
					break;
				default: break;
				}

				/* printk(KERN_INFO "psandbox=%d 3 calculating defer time! looping through competitors_map %d", psandbox->bid, count); */
				ktime_get_real_ts64(&current_tm);
				defer_tm = timespec64_sub(current_tm,cur->delaying_start);
				defer_tm = timespec64_sub(defer_tm,cur->psandbox->activity->defer_time);

				if (defer_tm.tv_sec == -1) {
				//	pr_info("2. can't find the key for delaying start for psandbox %ld\n", psandbox->bid);
				}
				executing_tm = timespec64_sub(timespec64_sub(current_tm, cur->psandbox->activity->execution_start), defer_tm);
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
//					 pr_info ("the defer time is %ld for psandbox %ld\n",timespec64_to_ns(&defer_tm),cur->psandbox->bid);
					// Find the psandbox that is interferenced most
					if (current_defer * old_execution > old_defer * current_execution) {
						old_defer = current_defer;
						old_execution = current_execution;
						penalty_ns = current_defer;
						victim = cur->psandbox;
					}
				}
			}
		}
		if (count == 0) {
			psandbox->competitor++;
		}
		read_unlock(&competitors_lock);

		if (event_type == UNHOLD) {
			if (is_lazy) {
				if (penalty_ns > 10000 && victim) {
					psandbox->activity->victim_id = victim->current_task->pid;
					psandbox->activity->key = key;
					psandbox->activity->penalty_ns = penalty_ns;
	//				pr_info("call do update %d, victim id %d, key %lu \n",psandbox->bid,psandbox->activity->victim_id,psandbox->activity->key);
					return penalty_ns;
				} else {
	//				pr_info("the penalty is %lu\n",penalty_ns);
					return 0;
				}
			}

			if (penalty_ns > 10000 && victim) {
				if (penalty_ns > victim->average_execution_time * LONG_SECTION) {
					penalty_ns = calculate_starting_penalty_ns(victim,penalty_ns,psandbox,4);
					do_penalty(victim,penalty_ns, key,true);
				} else { // FIXME victim could be NULL pointer?
					penalty_ns = calculate_starting_penalty_ns(victim,penalty_ns,psandbox,5);
					do_penalty(victim,penalty_ns, key,false);
				}

			} else if (penalty_ns != 0){
				pr_info("3. skip event: sleep psandbox %d, thread %d, defer time %llu\n", current->psandbox->bid, current->pid, penalty_ns);
			}
		}	else if (event_type == UNHOLD_IN_QUEUE_PENALTY) {
			/* printk(KERN_INFO "UNHOLD IN QUEUEU PENALTY: before penalize psandbox %d for %llu\n", psandbox->bid, penalty_ns); */
			if (penalty_ns > 10000 && victim) {
				if (penalty_ns > victim->average_execution_time * LONG_SECTION) {
					penalty_ns = calculate_starting_penalty_ns(victim,penalty_ns,psandbox,4);
					/* do_penalty(victim,penalty_ns, key,true); */
				} else {
					penalty_ns = calculate_starting_penalty_ns(victim,penalty_ns,psandbox,5);
					/* do_penalty(victim,penalty_ns, key,false); */
				}
				/* printk(KERN_INFO "penalize psandbox %d for %llu\b", psandbox->bid, penalty_ns); */
				psandbox->should_penalize_in_queue++;
				psandbox->in_queue_penalty_time += penalty_ns; // or we call it requeue time to wake up (curr tm + penalty_ns)
			}
			//XXX assuming only one user queue unhold event
			//psandbox->in_queue_victim = victim;
			if (victim) {
				victim->should_penalize_in_queue = 0;
				victim->in_queue_penalty_time = 0;
			}
		}
		break;
	}
	default:break;
	}
	return 0;
}

ktime_t calculate_starting_penalty_ns(PSandbox *victim,ktime_t penalty_ns,PSandbox *noisy,int type){
	switch(type) {
		case NORMAL:
			return penalty_ns;
		case AVERAGE:
			return (int_sqrt64(penalty_ns*noisy->average_execution_time) - victim->average_execution_time);
		case TAIL:
			return 100 * victim->average_execution_time;
		case GOOD:
			return noisy->average_execution_time * noisy->rule.isolation_level/100;
		case LONG: {
			ktime_t average_p =  int_sqrt64(penalty_ns*noisy->average_execution_time) - victim->average_execution_time;
			ktime_t good_p = noisy->average_execution_time * noisy->rule.isolation_level/100;
			return average_p > good_p? good_p: average_p;
		}
		case SHORT: {
			ktime_t tail_p =  100 * victim->average_execution_time;
			ktime_t good_p = noisy->average_execution_time * noisy->rule.isolation_level/100;
			return tail_p > good_p? good_p: tail_p;
		}
		default:
			break;
	}
	return 0;
}

void do_penalty(PSandbox *victim, ktime_t penalty_ns, unsigned int key, int is_long) {
	StatisticNode *stat_node = NULL;
	StatisticNode *stat_cur = NULL;
	struct hlist_node *tmp;
	ktime_t old_execution;
	ktime_t old_slack = victim->average_defer_time;
	ktime_t new_slack = victim->average_execution_time;

	read_lock(&stat_map_lock);
	hash_for_each_possible_safe (stat_map, stat_cur, tmp, node, key) {
		if (stat_cur->psandbox == current->psandbox) {
			stat_node = stat_cur;
			break;
		}
	}
	read_unlock(&stat_map_lock);
	if(!stat_node) {
		pr_info("Can't find the psandbox %ld in the stat map with key %u\n",victim->bid,key);
		return;
	}

	if (is_long) {
		penalty_ns *= current->psandbox->step;
//		pr_info("1.penalty time %u ms, step %d\n",penalty_ns/1000000, current->psandbox->step);
	} else {
		if (stat_node->bad_action ) {
			penalty_ns *= stat_node->bad_action / BASE_RATE;		
//			pr_info("2.penalty time %u ms, score %d\n",penalty_ns/1000000, stat_node->bad_action);
		}
	}

	wake_up_process(victim->current_task);
	__set_current_state(TASK_INTERRUPTIBLE);

	if (penalty_ns > 10000000000) {
		penalty_ns = 10000000000;
//		pr_info("2.event: sleep psandbox %lu for psandbox %lu in the key %u, thread %d, defer time 10 s\n", current->psandbox->bid, victim->bid, key, current->pid);
		current->psandbox->total_penalty_time += 10000000000;
		schedule_hrtimeout(&penalty_ns,HRTIMER_MODE_REL);
	} else {
//		pr_info("2.event: sleep psandbox %lu for psandbox %lu in the key %u, thread %d, defer time %lu ms\n", current->psandbox->bid, victim->bid, key, current->pid, penalty_ns/1000000);
		current->psandbox->total_penalty_time += penalty_ns;
		schedule_hrtimeout(&penalty_ns,HRTIMER_MODE_REL);
	}
	old_execution = new_slack;
	new_slack *= victim->average_defer_time;
	old_slack *= victim->average_execution_time;
	switch (victim->rule.type) {
	case RELATIVE:
		if (victim->average_defer_time * 100 > victim->average_execution_time * victim->rule.isolation_level) {
			ktime_t gap,detla;
			spin_lock(&stat_node->stat_lock);
			stat_node->bad_action++;
			spin_unlock(&stat_node->stat_lock);
			if (is_long) {
				gap = victim->average_defer_time * 100 / victim->average_execution_time - victim->rule.isolation_level;
				detla = (new_slack - old_slack) * 100 / (victim->average_defer_time * old_execution * stat_node->step);
				if(detla <= 0)
					detla = 1;
				current->psandbox->step = (STEP*gap)/(detla*100);
				if (current->psandbox->step <= 0)
					current->psandbox->step = 1;
				//				pr_info("the ratio is %d\n",current->psandbox->step);
			}
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
		if (victim->average_defer_time * 100 > victim->average_defer_time * victim->rule.isolation_level * live_psandbox) {
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
//		pr_info("there is no psandbox in current thread\n");
		return -1;
	}
	return current->psandbox->bid;
}

SYSCALL_DEFINE1(get_psandbox, size_t, addr)
{
	PSandbox *psandbox = NULL;
	psandbox = get_unbind_psandbox(addr);

	if (!psandbox) {
		// pr_info("can't find psandbox for addr %llu\n", addr);
		return -1;
	}
	return psandbox->bid;
}

SYSCALL_DEFINE2(annotate_resource, u32 __user *, uaddr, int, action_type) {
	WhiteList *white_mutex;
	white_mutex = (WhiteList *)kzalloc(sizeof(WhiteList),GFP_KERNEL);
	white_mutex->addr = uaddr;
	INIT_LIST_HEAD(&white_mutex->list);
	list_add(&white_mutex->list,&white_list);
	return 0;
}

SYSCALL_DEFINE2(unbind_psandbox, size_t, addr, int, flags)
{
	PSandbox *psandbox = current->psandbox;
	pid_t pid = psandbox->current_task->pid;

	if (!psandbox) {
		pr_info("can't find psandbox to unbind\n");
		return -1;
	}

	// XXX We use old unbind flags because UNHOLD is the last state event
	// to update when finishing and events should start with PREPARE.
	if (psandbox->unbind_flags & UNBIND_HANDLE_ACCEPT &&
		psandbox->event_key) {
		do_unhold(psandbox, psandbox->event_key, UNHOLD_IN_QUEUE_PENALTY);
		/* printk(KERN_INFO "UNBIND PSANDBOX %d: event_key %lu\n", psandbox->bid, psandbox->event_key); */
	}

	// XXX Here we set the new flags
	psandbox->unbind_flags = flags;
	psandbox->is_lazy = flags & UNBIND_LAZY;
	current->is_psandbox = 0;
	psandbox->task_key = addr;
	ktime_get_real_ts64(&psandbox->activity->last_unbind_start);
	do_freeze_psandbox(psandbox);

	/* if (flags & UNBIND_HANDLE_ACCEPT) */
		/* printk(KERN_INFO "psandbox %d: avg defer=%lu, avg exec=%lu, finished=%d", */
			/* psandbox->bid, psandbox->average_defer_time, psandbox->average_execution_time, psandbox->finished_activities); */

	if (!psandbox->is_lazy) {
		do_unbind(0);
		//printk(KERN_INFO "task %d: !!! do unbind psandbox %d for addr %llu, is_psandbox %d\n", current->pid, pid, addr, current->is_psandbox);
	}

	return psandbox->bid;
}

SYSCALL_DEFINE1(bind_psandbox, size_t, addr)
{
	PSandbox *psandbox = NULL;
	PSandboxNode *cur;
	struct hlist_node *tmp;
	int success;
	struct timespec64 current_tm, unbind_tm;


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
		pr_info("can't find psandbox to bind %lu\n",addr);
		return -1;
	}

	current->psandbox = psandbox;
	current->is_psandbox = 1;
	psandbox->current_task = current;
	psandbox->is_lazy = 0;
	// XXX Do NOT clear unbind flags here
	/* psandbox->unbind_flags = UNBIND_NONE; */
	psandbox->state = BOX_ACTIVE;
	ktime_get_real_ts64(&psandbox->activity->execution_start);

	ktime_get_real_ts64(&current_tm);
	unbind_tm =
		timespec64_sub(current_tm, psandbox->activity->last_unbind_start);
	// current_tm = psandbox->activity->unbind_time;
	// psandbox->activity->unbind_time = timespec64_add(unbind_tm, current_tm);
	psandbox->activity->unbind_time = unbind_tm;
	// pr_info("bind the psandbox %d (find by addr %d) to thread %d\n",psandbox->bid,addr,current->pid);

	// ktime_get_real_ts64(&current_tm);
	// ktime_t tm = timespec64_to_ns(&psandbox->activity->unbind_time);
	// pr_info("unbind time nsec %llu \n", timespec64_to_ns(&psandbox->activity->unbind_time));
	// pr_info("current sec %llu \n", current_tm.tv_sec);
	// pr_info("last time start sec %llu \n", psandbox->activity->last_unbind_start.tv_sec);
	// pr_info("task %d: !!! bind psandbox for addr %llu\n", current->pid, addr);
	// pr_info("task %d: +++ do bind psandbox %d for addr %llu\n", current->pid, psandbox->bid, addr);


	return psandbox->bid;
}

int do_unbind(size_t addr){
	PSandboxNode *a = NULL;
	int i;
	PSandbox *psandbox = current->psandbox;

	if (!current->psandbox) {
//		pr_info("can't find psandbox to do unbind\n");
		return 0;
	}

	if(current->psandbox->task_key == addr) {
		current->psandbox->is_lazy = 0;
		current->psandbox->unbind_flags = UNBIND_NONE;
		current->psandbox->state = BOX_ACTIVE;
		ktime_get_real_ts64(&current->psandbox->activity->execution_start);
		return current->pid;
	}

	for (i = 0; i < PREALLOCATION_SIZE; ++i) {
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
	// pr_info("!!!------- INSIDE do unbind for addr %d\n", addr);
	hash_add(transfers_map,&a->node,psandbox->task_key);
	write_unlock(&transfers_lock);

	return 0;
}

void do_freeze_psandbox(PSandbox *psandbox){
	struct timespec64 current_tm, total_time, last_unbind_start;
//	ktime_t average_defer;
//	struct list_head temp;
	ktime_t defer_tm;

	psandbox->state = BOX_FREEZE;
	if (!(psandbox->unbind_flags & UNBIND_ACT_UNFINISHED))
		psandbox->finished_activities++;
	ktime_get_real_ts64(&current_tm);
	total_time = timespec64_sub(current_tm,psandbox->activity->execution_start);
	psandbox->activity->execution_time = timespec64_sub(total_time,psandbox->activity->defer_time);
	psandbox->total_execution_time += timespec64_to_ns(&psandbox->activity->execution_time);

	// psandbox->last_unbind_time = timespec64_to_ns(&psandbox->activity->unbind_time);
	last_unbind_start = psandbox->activity->last_unbind_start;

	//adjust actual execution time
	if (psandbox->total_execution_time > psandbox->activity->adjust_ns) {
		psandbox->total_execution_time -= psandbox->activity->adjust_ns;
		if (psandbox->finished_activities)
			psandbox->average_execution_time = psandbox->total_execution_time/psandbox->finished_activities;
	} else {
		psandbox->total_execution_time = 0;
		psandbox->average_execution_time = 0;
	}

	defer_tm = timespec64_to_ns(&psandbox->activity->defer_time);
	psandbox->total_defer_time += defer_tm;
	if (!(psandbox->unbind_flags & UNBIND_ACT_UNFINISHED)) {
		if (psandbox->finished_activities) {
			psandbox->average_defer_time = psandbox->total_defer_time/psandbox->finished_activities;
			psandbox->average_execution_time = psandbox->total_execution_time/psandbox->finished_activities;
		}
	}

        // XXX when the activity is unfinished, bad_activities may increase for multiple times
	if (timespec64_to_ns(&psandbox->activity->execution_time) * psandbox->rule.isolation_level  < timespec64_to_ns(&psandbox->activity->defer_time) * 100 ) {
		psandbox->bad_activities++;
	}

	if (psandbox->activity->victim_id && psandbox->activity->key) {
		struct task_struct *task = find_get_task_by_vpid(psandbox->activity->victim_id);
		if(task) {
			PSandbox *victim = task->psandbox;
			if(victim)
				do_penalty(victim,psandbox->activity->penalty_ns,psandbox->activity->key,false);
		}
	}

	memset(psandbox->activity, 0, sizeof(Activity));
	psandbox->activity->last_unbind_start = last_unbind_start;
}

void clean_psandbox(PSandbox *psandbox) {
	unsigned bkt;
	struct hlist_node *tmp;
	PSandboxNode *cur;
	StatisticNode *stat_cur;
	struct delaying_start *pos,*temp;
	if (psandbox->finished_activities > 0) {
		pr_info( "psandbox syscall called psandbox_release id =%ld by the thread %d, total penalty time %llu ms, total defer time %llu ms, total execution time %llu ms, the ratio is %ld\n",
			 psandbox->bid, current->pid,psandbox->total_penalty_time/1000000,psandbox->total_defer_time/1000000, psandbox->total_execution_time/1000000, psandbox->bad_activities*100/psandbox->finished_activities);
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

PSandbox *get_unbind_psandbox(size_t addr) {
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
	return psandbox;
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
