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

struct task_struct;

#define HIGHEST_PRIORITY 2
#define MID_PRIORITY 1
#define LOW_PRIORITY 0

enum enum_event_type {
	PREPARE,
	ENTER,
	EXIT,
};

typedef struct sandboxEvent {
	enum enum_event_type event_type;
	u32 key;
} BoxEvent;

enum enum_psandbox_state {
	BOX_ACTIVE, BOX_FREEZE, BOX_START, BOX_AWAKE, BOX_PREEMPTED
};

enum enum_activity_state {
	ACTIVITY_WAITING,ACTIVITY_ENTER,ACTIVITY_EXIT,ACTIVITY_PREEMPTED,ACTIVITY_PROMOTED
};

typedef struct activity {
	enum enum_activity_state activity_state;
	struct timespec64 execution_start;
	struct timespec64 defer_time;
	struct timespec64 delaying_start;
	struct timespec64 execution_time;
	int try_number;
} Activity;

typedef struct white_list {
	u32 *addr;
	struct list_head list;
}WhiteList;

typedef struct psandbox_info {
	int bid;
	struct task_struct *current_task;
	enum enum_psandbox_state state;
	int delay_ratio;
	Activity *activity;
	int finished_activities;
	int action_level;
	int compensation_ticket;
	struct list_head *white_list;
	int is_white;
} PSandbox;

#endif //LINUX_5_4_PSANDBOX_H
