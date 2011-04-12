/*
 * Copyright (C) 2010-2011 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef TARGET_SIGNAL_H
#define TARGET_SIGNAL_H

/* this struct defines a stack used during syscall handling */
typedef struct target_sigaltstack {
    abi_ulong ss_sp;
    abi_ulong ss_flags;
    abi_ulong ss_size;
} target_stack_t;

/*
 * sigaltstack controls
 */
#define TARGET_SS_ONSTACK               1
#define TARGET_SS_DISABLE               2

#define get_sp_from_cpustate(cpustate)  (cpustate->regs[29])

#endif /* TARGET_SIGNAL_H */
