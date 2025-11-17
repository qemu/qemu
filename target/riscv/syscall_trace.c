/*
 * Helper for tracing linux-syscall.
 */

#include "syscall_trace.h"

void handle_payload_in(CPUState *cs, trace_event_t *evt, FILE *f)
{
    switch (evt->ax[7]) {
    case __NR_execve:
        do_execve(cs, evt, f);
        break;
    default:
        break;
    }
}

oid handle_payload_out(CPUState *cs, trace_event_t *evt, FILE *f)
{
    switch (evt->ax[7]) {
    case __NR_openat:
        do_openat(cs, evt, f);
        break;
    case __NR_uname:
        do_uname(cs, evt, f);
        break;
    case __NR_faccessat:
        do_faccessat(cs, evt, f);
        break;
    case __NR_read:
        do_read_event(cs, evt, f);
        break;
    case __NR_write:
        do_write_event(cs, evt, f);
        break;
    case __NR_rt_sigaction:
        do_rt_sigaction(cs, evt, f);
        break;
    case __NR_rt_sigprocmask:
        do_rt_sigprocmask(cs, evt, f);
        break;
    case __NR_unlinkat:
        handle_path(1, cs, evt, f);
        break;
    case __NR_fstatat:
        handle_path(1, cs, evt, f);
        do_fstatat_out(cs, evt, f);
        break;
    case __NR_getcwd:
    case __NR_chdir:
        handle_path(0, cs, evt, f);
        break;
    default:
        break;
    }
}