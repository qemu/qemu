#pragma once

#include "kernel_profile.h"

/**
 * @brief Page size used by the kernel. Used to calculate THREADINFO_MASK.
 */
#define PAGE_SIZE 4096

/**
 * @brief Mask to apply on ESP to get the thread_info address.
 *
 * The value should be either ~8191 or ~4095, depending on the
 * size of the stack used by the kernel.
 *
 * @see Understanding the Linux Kernel 3rd ed., pp85.
 * @todo Check if this value can be read from kernelinfo.conf.
 */
#define THREADINFO_MASK (~(PAGE_SIZE + PAGE_SIZE - 1))

target_ptr_t kernel24x_get_current_task_struct(CPUState *cpu);
target_ptr_t kernel24x_get_task_struct_next(CPUState *cpu, target_ptr_t ts);
target_ptr_t kernel24x_get_group_leader(CPUState *cpu, target_ptr_t ts);
target_ptr_t kernel24x_get_files_fds(CPUState *cpu, target_ptr_t files);

const KernelProfile KERNEL24X_PROFILE = {
    .get_current_task_struct = &kernel24x_get_current_task_struct,
    .get_task_struct_next = &kernel24x_get_task_struct_next,
    .get_group_leader = &kernel24x_get_group_leader,
    .get_files_fds = &kernel24x_get_files_fds
};

/* vim:set tabstop=4 softtabstop=4 expandtab: */
