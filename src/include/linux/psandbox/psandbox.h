//
// The PSandbox project
//
// Created by yigonghu on 2/18/21.
//
// Copyright (c) 2021, Johns Hopkins University - Order Lab
//
//      All rights reserved.
//      Licensed under the Apache License, Version 2.0 (the "License");

#ifndef LINUX_5_4_PSANDBOX_H
#define LINUX_5_4_PSANDBOX_H
#include <linux/time64.h>
#include <linux/list.h>
#include <linux/thread_info.h>
#include <linux/spinlock_types.h>
#include <linux/ktime.h>
struct task_struct;

#define PREALLOCATION_SIZE 10
#define HOLDER_SIZE 1000
#define COMPETITORS_SIZE 1000
#define SANDBOX_NUMBER 50

enum enum_event_type {
	PREPARE,
	ENTER,
	HOLD,
	UNHOLD,
	UNHOLD_IN_QUEUE_PENALTY,
};

enum enum_action_level { PSB_LOW_PRIORITY, PSB_MID_PRIORITY, PSB_HIGH_PRIORITY };

enum enum_isolation_type { ISOLATION_ABSOLUTE, ISOLATION_RELATIVE, ISOLATION_SCALABLE, ISOLATION_DEFAULT };

enum enum_penalty_type { PENALTY_NORMAL, PENALTY_AVERAGE, PENALTY_TAIL, PENALTY_GOOD, PENALTY_LONG, PENALTY_SHORT};
typedef struct sandboxEvent {
	enum enum_event_type event_type;
	u32 key;
} BoxEvent;

typedef struct isolationRule {
	enum enum_isolation_type type;
	int isolation_level; // ratio = isolation_level / 100
	int is_retro;
}IsolationRule;

enum enum_psandbox_state {
	BOX_ACTIVE, BOX_FREEZE, BOX_START, BOX_AWAKE, BOX_PREEMPTED
};

enum enum_activity_state {
	ACTIVITY_WAITING,ACTIVITY_ENTER,ACTIVITY_EXIT,ACTIVITY_PREEMPTED,ACTIVITY_PROMOTED
};

enum enum_unbind_flag {
	UNBIND_LAZY 		  = 0x1,
	UNBIND_ACT_UNFINISHED = 0x2,
	UNBIND_HANDLE_ACCEPT  = 0x4,
	// UNBIND_HANDLE_CONNECT = 0x8,
	UNBIND_NONE 		  = 0x0, //TODO change to UNBIND_DEFAULT
};


struct delaying_start {
	struct timespec64 delaying_start;
	u64 key;
	struct list_head list;
};

typedef struct activity {
	enum enum_activity_state activity_state;
	struct timespec64 execution_start;
	struct timespec64 defer_time;
	struct timespec64 execution_time;
	struct timespec64 last_unbind_start;
	struct timespec64 unbind_time;
	struct timespec64 last_queue_in;
	struct timespec64 expected_queue_out;
	struct timespec64 requeue_start;
	int try_number;
	int victim_id;
	ktime_t penalty_ns;
	ktime_t adjust_ns;
	int key;
	int c_resource_numbers;
} Activity;

typedef struct white_list {
	u32 *addr;
	struct list_head list;
}WhiteList;

typedef struct psandbox_info PSandbox;
typedef struct transfer_node {
	PSandbox *psandbox;
	struct timespec64 delaying_start;
	struct hlist_node node;
} PSandboxNode;

typedef struct statistics_node {
	PSandbox *psandbox;
	int bad_action;
	struct hlist_node node;
	int step;
	spinlock_t stat_lock;
} StatisticNode;

typedef struct demand_node{
	PSandbox *psandbox;
	int demand;
	int is_satisfied;
}DemandNode;

struct psandbox_info {
	long int bid;
	struct task_struct *current_task;
	struct task_struct *creator_psandbox;
	enum enum_psandbox_state state;
	Activity *activity;
	long int finished_activities;
	long int bad_activities;
	enum enum_action_level action_level;
	ktime_t total_execution_time;
	ktime_t total_penalty_time;
	ktime_t total_defer_time;
	ktime_t average_defer_time;
	ktime_t average_execution_time;
	// ktime_t last_unbind_time; //XXX remove
	ktime_t last_queue_time;
	IsolationRule rule; // the rule for isolation
	int priority;
	struct list_head *white_list;
	int is_white;
	struct hlist_node node;
	size_t task_key;
	struct list_head list;
	spinlock_t lock;
	PSandboxNode transfers[PREALLOCATION_SIZE];
	PSandboxNode holders[HOLDER_SIZE];
	PSandboxNode competitors[COMPETITORS_SIZE];
	struct list_head delay_list;
	int is_lazy;
	int step;
	// int is_accept;
	enum enum_unbind_flag unbind_flags;
	int requeued;
	u64 addr;
	int should_penalize_in_queue;
	ktime_t in_queue_penalty_time;
	PSandbox *in_queue_victim;
	unsigned int event_key;
	int is_nice;
	// Debug
	int is_futex;
	long int count;
	long unsigned int competitor;
	long int unhold;

	//Retro
	unsigned long long start_cycles;
	unsigned long long cycles;
	ktime_t total_spend_time;
	ktime_t optimal_time;
	struct timespec64 lock_start;
	ktime_t lock_waiting_time;
	ktime_t IO_total_time;
	int is_throttle;
	unsigned frequency;
	unsigned cpu_slowdown;
	unsigned cpu_load;
	unsigned lock_slowdown;
	unsigned lock_load;
};

extern long int live_psandbox;

ktime_t calculate_starting_penalty_ns(PSandbox *victim,ktime_t penalty_ns,PSandbox *noisy,int type);
void do_penalty(PSandbox *victim, ktime_t penalty_ns, unsigned int key, int is_long) ;
void do_freeze_psandbox(PSandbox *psandbox);
void clean_psandbox(PSandbox *psandbox);
void clean_unbind_psandbox(struct task_struct *task);
PSandbox *get_psandbox(int bid);
// PSandbox *get_psandbox_unbind(u64 addr);
PSandbox *get_unbind_psandbox(size_t addr);
int do_unbind(size_t addr);
int do_prepare(PSandbox *psandbox, unsigned int key);
int do_enter(PSandbox *psandbox, unsigned int key);
int do_unhold(PSandbox *psandbox, unsigned int key, unsigned int event_type);
#endif //LINUX_5_4_PSANDBOX_H
