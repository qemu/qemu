/*
 * Helper routines to provide target memory access for semihosting
 * syscalls in system emulation mode.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GPL
 */

#ifndef SEMIHOSTING_SOFTMMU_UACCESS_H
#define SEMIHOSTING_SOFTMMU_UACCESS_H

#include "cpu.h"

#define get_user_u64(val, addr)                                         \
    ({ uint64_t val_ = 0;                                               \
       int ret_ = cpu_memory_rw_debug(env_cpu(env), (addr),             \
                                      &val_, sizeof(val_), 0);          \
       (val) = tswap64(val_); ret_; })

#define get_user_u32(val, addr)                                         \
    ({ uint32_t val_ = 0;                                               \
       int ret_ = cpu_memory_rw_debug(env_cpu(env), (addr),             \
                                      &val_, sizeof(val_), 0);          \
       (val) = tswap32(val_); ret_; })

#define get_user_u8(val, addr)                                          \
    ({ uint8_t val_ = 0;                                                \
       int ret_ = cpu_memory_rw_debug(env_cpu(env), (addr),             \
                                      &val_, sizeof(val_), 0);          \
       (val) = val_; ret_; })

#define get_user_ual(arg, p) get_user_u32(arg, p)

#define put_user_u64(val, addr)                                         \
    ({ uint64_t val_ = tswap64(val);                                    \
       cpu_memory_rw_debug(env_cpu(env), (addr), &val_, sizeof(val_), 1); })

#define put_user_u32(val, addr)                                         \
    ({ uint32_t val_ = tswap32(val);                                    \
       cpu_memory_rw_debug(env_cpu(env), (addr), &val_, sizeof(val_), 1); })

#define put_user_ual(arg, p) put_user_u32(arg, p)

void *softmmu_lock_user(CPUArchState *env, target_ulong addr,
                        target_ulong len, bool copy);
#define lock_user(type, p, len, copy) softmmu_lock_user(env, p, len, copy)

char *softmmu_lock_user_string(CPUArchState *env, target_ulong addr);
#define lock_user_string(p) softmmu_lock_user_string(env, p)

void softmmu_unlock_user(CPUArchState *env, void *p,
                         target_ulong addr, target_ulong len);
#define unlock_user(s, args, len) softmmu_unlock_user(env, s, args, len)

ssize_t softmmu_strlen_user(CPUArchState *env, target_ulong addr);
#define target_strlen(p) softmmu_strlen_user(env, p)

#endif /* SEMIHOSTING_SOFTMMU_UACCESS_H */
