#ifndef GDBSTUB_H
#define GDBSTUB_H

#define DEFAULT_GDBSTUB_PORT "1234"

typedef void (*gdb_syscall_complete_cb)(CPUState *env,
                                        target_ulong ret, target_ulong err);

void gdb_do_syscall(gdb_syscall_complete_cb cb, const char *fmt, ...);
int use_gdb_syscalls(void);
void gdb_set_stop_cpu(CPUState *env);
#ifdef CONFIG_USER_ONLY
int gdb_queuesig (void);
int gdb_handlesig (CPUState *, int);
void gdb_exit(CPUState *, int);
void gdb_signalled(CPUState *, int);
int gdbserver_start(int);
void gdbserver_fork(CPUState *);
#else
int gdbserver_start(const char *port);
#endif
/* Get or set a register.  Returns the size of the register.  */
typedef int (*gdb_reg_cb)(CPUState *env, uint8_t *buf, int reg);
void gdb_register_coprocessor(CPUState *env,
                              gdb_reg_cb get_reg, gdb_reg_cb set_reg,
                              int num_regs, const char *xml, int g_pos);

#endif
