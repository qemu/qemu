#pragma once
#include "osi_types.h"

struct KernelProfile
{
    target_ptr_t (*get_current_task_struct)();
    target_ptr_t (*get_task_struct_next)(target_ptr_t ts);
    target_ptr_t (*get_group_leader)(target_ptr_t ts);
    target_ptr_t (*get_files_fds)(target_ptr_t files);
};

/* vim:set tabstop=4 softtabstop=4 expandtab: */
