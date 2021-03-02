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

enum enum_psandbox_event { START, AWAKE };

typedef struct psandbox_info {
	int bid;
	struct task_struct *current_task;
	enum enum_psandbox_event event;
} PSandbox;

#endif //LINUX_5_4_PSANDBOX_H
