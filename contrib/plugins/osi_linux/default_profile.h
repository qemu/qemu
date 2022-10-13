#pragma once

#include "kernel_profile.h"

target_ptr_t default_get_current_task_struct();
target_ptr_t default_get_task_struct_next(target_ptr_t ts);
target_ptr_t default_get_group_leader(target_ptr_t ts);
target_ptr_t default_get_file_fds(target_ptr_t files);
bool can_read_current();
void on_first_syscall(target_ulong pc, target_ulong callno);

const KernelProfile DEFAULT_PROFILE = {
    .get_current_task_struct = &default_get_current_task_struct,
    .get_task_struct_next = &default_get_task_struct_next,
    .get_group_leader = &default_get_group_leader,
    .get_files_fds = &default_get_file_fds
};

/* vim:set tabstop=4 softtabstop=4 expandtab: */
