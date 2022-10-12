#pragma once

#include <stdbool.h>

// BEGIN_PYPANDA_NEEDS_THIS -- do not delete this comment bc pypanda
// api autogen needs it.  And don't put any compiler directives
// between this and END_PYPANDA_NEEDS_THIS except includes of other
// files in this directory that contain subsections like this one.

// returns minimal handles for processes in an array
GArray *get_process_handles(CPUState *cpu);

// returns the current thread
OsiThread *get_current_thread(CPUState *cpu);

// returns information about the modules loaded by the guest OS kernel
GArray *get_modules(CPUState *cpu);

// returns information about the memory mappings of libraries loaded by a guest OS process
GArray *get_mappings(CPUState *cpu, OsiProc *p);

// returns operating system introspection info for each process in an array
GArray *get_processes(CPUState *cpu);

// gets the currently running process
OsiProc *get_current_process(CPUState *cpu);

OsiModule* get_one_module(GArray *osimodules, unsigned int idx);

OsiProc* get_one_proc(GArray *osiprocs, unsigned int idx);

void cleanup_garray(GArray *g);

// returns true if execution is currently within a dynamically-linked function, else false.
bool in_shared_object(CPUState *cpu, OsiProc *p);

// END_PYPANDA_NEEDS_THIS -- do not delete this comment!

// gets the currently running process handle
OsiProcHandle *get_current_process_handle(CPUState *cpu);

// gets the process pointed to by the handle
OsiProc *get_process(CPUState *cpu, const OsiProcHandle *h);

// functions retrieving partial process information via an OsiProcHandle
target_pid_t get_process_pid(CPUState *cpu, const OsiProcHandle *h);
target_pid_t get_process_ppid(CPUState *cpu, const OsiProcHandle *h);

void notify_task_change(CPUState *cpu);

// END_PYPANDA_NEEDS_THIS -- do not delete this comment!
