#ifndef GDBSTUB_H
#define GDBSTUB_H

#define DEFAULT_GDBSTUB_PORT "1234"

/* GDB breakpoint/watchpoint types */
#define GDB_BREAKPOINT_SW        0
#define GDB_BREAKPOINT_HW        1
#define GDB_WATCHPOINT_WRITE     2
#define GDB_WATCHPOINT_READ      3
#define GDB_WATCHPOINT_ACCESS    4

#ifdef NEED_CPU_H
typedef void (*gdb_syscall_complete_cb)(CPUState *env,
                                        target_ulong ret, target_ulong err);

void gdb_do_syscall(gdb_syscall_complete_cb cb, const char *fmt, ...);
int use_gdb_syscalls(void);
void gdb_set_stop_cpu(CPUState *env);
void gdb_exit(CPUState *, int);
#ifdef CONFIG_USER_ONLY
int gdb_queuesig (void);
int gdb_handlesig (CPUState *, int);
void gdb_signalled(CPUState *, int);
void gdbserver_fork(CPUState *);
#endif
/* Get or set a register.  Returns the size of the register.  */
typedef int (*gdb_reg_cb)(CPUState *env, uint8_t *buf, int reg);
void gdb_register_coprocessor(CPUState *env,
                              gdb_reg_cb get_reg, gdb_reg_cb set_reg,
                              int num_regs, const char *xml, int g_pos);

#endif

#ifdef CONFIG_USER_ONLY
int gdbserver_start(int);
#else
int gdbserver_start(const char *port);
#endif

#endif
