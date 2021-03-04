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
	TRY_QUEUE,
	ENTER_QUEUE,
	EXIT_QUEUE,
	SLEEP_BEGIN,
	SLEEP_END,
	UPDATE_QUEUE_CONDITION,
	MUTEX_REQUIRE,
	MUTEX_GET,
	MUTEX_RELEASE
};

enum enum_key_type {
	INTEGER, FLOAT, LONGLONG, MUTEX
};

enum enum_psandbox_state {
	BOX_ACTIVE, BOX_FREEZE, BOX_START, BOX_AWAKE
};

enum enum_condition {
	COND_LARGE, COND_SMALL, COND_LARGE_OR_EQUAL, COND_SMALL_OR_EQUAL
};

enum enum_queue_state {
	QUEUE_NULL,QUEUE_ENTER,QUEUE_SLEEP,QUEUE_AWAKE
};

typedef struct activity {
	struct timespec64 execution_start;
	struct timespec64 delayed_time;
	struct timespec64 delaying_start;
	enum enum_queue_state queue_state;
} Activity;

typedef struct psandbox_info {
	int bid;
	struct task_struct *current_task;
	enum enum_psandbox_state state;
	int delay_ratio;
	Activity *activity;
} PSandbox;

typedef struct condition {
	int value;
	enum enum_condition compare;
} Condition;

typedef struct sandboxEvent {
	enum enum_event_type event_type;
	void* key;
	enum enum_key_type key_type;
} PsandboxEvent;

#endif //LINUX_5_4_PSANDBOX_H
