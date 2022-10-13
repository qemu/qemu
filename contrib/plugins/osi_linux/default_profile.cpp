#include "osi_linux.h"
#include "default_profile.h"
#include "osi_types.h"

/**
 * @brief Retrieves the task_struct address using per cpu information.
 */
target_ptr_t default_get_current_task_struct(void)
{
    struct_get_ret_t err;
    target_ptr_t current_task_addr;
    target_ptr_t ts;
    current_task_addr = ki.task.current_task_addr;
    err = struct_get(&ts, current_task_addr, ki.task.per_cpu_offset_0_addr);
    //assert(err == struct_get_ret_t::SUCCESS && "failed to get current task struct");
    if (err != struct_get_ret_t::SUCCESS) {
      // Callers need to check if we return NULL!
      return 0;
    }

    fixupendian(ts);
    return ts;
}

/**
 * @brief Retrieves the address of the following task_struct in the process list.
 */
target_ptr_t default_get_task_struct_next(target_ptr_t task_struct)
{
    struct_get_ret_t err;
    target_ptr_t tasks;
    err = struct_get(&tasks, task_struct, ki.task.tasks_offset);
    fixupendian(tasks);
    assert(err == struct_get_ret_t::SUCCESS && "failed to get next task");
    return tasks-ki.task.tasks_offset;
}

/**
 * @brief Retrieves the thread group leader address from task_struct.
 */
target_ptr_t default_get_group_leader(target_ptr_t ts)
{
    struct_get_ret_t err;
    target_ptr_t group_leader;
    err = struct_get(&group_leader, ts, ki.task.group_leader_offset);
    fixupendian(group_leader);
    assert(err == struct_get_ret_t::SUCCESS && "failed to get group leader for task");
    return group_leader;
}

/**
 * @brief Retrieves the array of file structs from the files struct.
 * The n-th element of the array corresponds to the n-th open fd.
 */
target_ptr_t default_get_file_fds(target_ptr_t files)
{
    struct_get_ret_t err;
    target_ptr_t files_fds;
    err = struct_get(&files_fds, files, {ki.fs.fdt_offset, ki.fs.fd_offset});
    if (err != struct_get_ret_t::SUCCESS) {
        printf("Failed to retrieve file structs (error code: %d)", err);
        return (target_ptr_t)NULL;
    }
    fixupendian(files_fds);
    return files_fds;
}

/* vim:set tabstop=4 softtabstop=4 expandtab: */
