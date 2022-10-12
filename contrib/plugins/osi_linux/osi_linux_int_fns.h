#pragma once

// Here we define functions osi_linux provides in addition to
// the standard osi API.

#define INVALID_FILE_POS (-1)

// BEGIN_PYPANDA_NEEDS_THIS -- do not delete this comment bc pypanda
// api autogen needs it.  And don't put any compiler directives
// between this and END_PYPANDA_NEEDS_THIS except includes of other
// files in this directory that contain subsections like this one.

// returns fd for a filename or a NULL if failed
char *osi_linux_fd_to_filename(CPUState *env, OsiProc *p, int fd);

// returns pos in a file 
unsigned long long osi_linux_fd_to_pos(CPUState *env, OsiProc *p, int fd);

target_ptr_t ext_get_file_struct_ptr(CPUState *env, target_ptr_t task_struct, int fd);
target_ptr_t ext_get_file_dentry(CPUState *env, target_ptr_t file_struct);
// END_PYPANDA_NEEDS_THIS -- do not delete this comment!

/* vim:set tabstop=4 softtabstop=4 expandtab: */
