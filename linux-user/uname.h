#ifndef UNAME_H
#define UNAME_H

#include <sys/utsname.h>
#include <linux/utsname.h>

const char *cpu_to_uname_machine(CPUArchState *cpu_env);
int sys_uname(struct new_utsname *buf);

#endif /* UNAME_H */
