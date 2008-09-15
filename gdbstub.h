#ifndef GDBSTUB_H
#define GDBSTUB_H

#define DEFAULT_GDBSTUB_PORT "1234"

typedef void (*gdb_syscall_complete_cb)(CPUState *env,
                                        target_ulong ret, target_ulong err);

void gdb_do_syscall(gdb_syscall_complete_cb cb, const char *fmt, ...);
int use_gdb_syscalls(void);
#ifdef CONFIG_USER_ONLY
int gdb_handlesig (CPUState *, int);
void gdb_exit(CPUState *, int);
int gdbserver_start(int);
#else
int gdbserver_start(const char *port);
#endif

#endif
