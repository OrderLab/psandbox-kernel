//
// The PSandbox project
//
// Created by yigonghu on 2/18/21.
//
// Copyright (c) 2021, Johns Hopkins University - Order Lab
//
//      All rights reserved.
//      Licensed under the Apache License, Version 2.0 (the "License");


#include <linux/psandbox.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/time.h>


#define MAX_TIME 100*5

struct psandbox_info {
  struct task_struct * current_task;
  struct list_head	psandbox;	/* psandbox list */
  int loop_count;
  int flag;
};

enum enum_event_type
{
  EVENT_ENTERLOOP,EVENT_EXITLOOP,EVENT_BEGINTASK,EVENT_ENDTASK
};


struct psandbox_info  psandboxs[10];
int i = 0;

/*This function will create a psandbox and bind to the current thread*/
SYSCALL_DEFINE1(psandbox_create, char *, name) {
  struct psandbox_info psandbox;
  psandbox.current_task = current;
  psandbox.loop_count = 0;
  if(i>10) {
    printk(KERN_INFO "kernel panic %d\n",current->pid);
  }
  psandboxs[i] = psandbox;
  i++;
  printk(KERN_INFO "psandbox syscall called psandbox_create id =%d, i =%d\n",current->pid,i);
  return i-1;
}

SYSCALL_DEFINE1(psandbox_release, int, psandbox_id) {
  int j = 0;
  for(j = psandbox_id+1;j<i;j++){
      psandboxs[j-1]= psandboxs[j];
  }
  i--;
  printk(KERN_INFO "psandbox syscall called psandbox_release id =%d, i = %d\n",current->pid, i);
  return 0;
}

SYSCALL_DEFINE1(psandbox_wakeup, int, tid) {
  if (!tid) {
  printk(KERN_INFO "psandbox syscall called psandbox_wakeup pid\n");
  return;
  }
  struct task_struct *task = find_get_task_by_vpid(tid);
//  struct kernel_siginfo *info;
//  info->si_signo = SIGCONT;
//  info->si_errno = 0;
//  info->si_code = SI_USER;
//  info->si_pid = task_tgid_vnr(task);
//  info->si_uid = 0;
//
//  do_send_sig_info(SIGCONT,info,task,PIDTYPE_TGID);
int success = wake_up_state(task,TASK_INTERRUPTIBLE);
  printk(KERN_INFO "psandbox syscall called psandbox_wakeup pid =%d; success =%d\n",task->pid,success);
  return 0;
}