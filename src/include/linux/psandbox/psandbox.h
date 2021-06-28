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
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/list.h>

enum enum_event_type {
	START_QUEUE,
	TRY_QUEUE,
	ENTER_QUEUE,
	EXIT_QUEUE,
	SLEEP_BEGIN,
	SLEEP_END,
	WAKEUP_QUEUE,
	MUTEX_REQUIRE,
	MUTEX_GET,
	MUTEX_RELEASE
};

enum enum_psandbox_state {
	BOX_ACTIVE, BOX_FREEZE, BOX_START, BOX_AWAKE, BOX_PREEMPTED
};

enum enum_condition {
	COND_LARGE, COND_SMALL, COND_LARGE_OR_EQUAL, COND_SMALL_OR_EQUAL
};

enum enum_queue_state {
	QUEUE_NULL,QUEUE_ENTER,QUEUE_SLEEP,QUEUE_AWAKE
};

typedef struct activity {
	struct timespec64 execution_start;
	struct timespec64 defer_time;
	struct timespec64 delaying_start;
	int try_number;
	enum enum_queue_state queue_state;
} Activity;

typedef struct psandbox_info {
	int bid;
	struct task_struct *current_task;
	enum enum_psandbox_state state;
	int delay_ratio;
	Activity *activity;
	int white_mutex;
} PSandbox;

#endif //LINUX_5_4_PSANDBOX_H
