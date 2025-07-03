/*
 * Helper routines to provide target memory access for semihosting
 * syscalls in system emulation mode.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GPL
 */

#ifndef SEMIHOSTING_UACCESS_H
#define SEMIHOSTING_UACCESS_H

#ifdef CONFIG_USER_ONLY
#error Cannot include semihosting/uaccess.h from user emulation
#endif

#include "exec/cpu-common.h"
#include "exec/tswap.h"
#include "exec/page-protection.h"
#include "exec/vaddr.h"

/**
 * get_user_u64:
 *
 * Returns: 0 on success, -1 on error.
 */
#define get_user_u64(val, addr)                                         \
    ({ uint64_t val_ = 0;                                               \
       int ret_ = cpu_memory_rw_debug(env_cpu(env), (addr),             \
                                      &val_, sizeof(val_), 0);          \
       (val) = tswap64(val_); ret_; })

/**
 * get_user_u32:
 *
 * Returns: 0 on success, -1 on error.
 */
#define get_user_u32(val, addr)                                         \
    ({ uint32_t val_ = 0;                                               \
       int ret_ = cpu_memory_rw_debug(env_cpu(env), (addr),             \
                                      &val_, sizeof(val_), 0);          \
       (val) = tswap32(val_); ret_; })

/**
 * get_user_u8:
 *
 * Returns: 0 on success, -1 on error.
 */
#define get_user_u8(val, addr)                                          \
    ({ uint8_t val_ = 0;                                                \
       int ret_ = cpu_memory_rw_debug(env_cpu(env), (addr),             \
                                      &val_, sizeof(val_), 0);          \
       (val) = val_; ret_; })

/**
 * get_user_ual:
 *
 * Returns: 0 on success, -1 on error.
 */
#define get_user_ual(arg, p) get_user_u32(arg, p)

/**
 * put_user_u64:
 *
 * Returns: 0 on success, -1 on error.
 */
#define put_user_u64(val, addr)                                         \
    ({ uint64_t val_ = tswap64(val);                                    \
       cpu_memory_rw_debug(env_cpu(env), (addr), &val_, sizeof(val_), 1); })

/**
 * put_user_u32:
 *
 * Returns: 0 on success, -1 on error.
 */
#define put_user_u32(val, addr)                                         \
    ({ uint32_t val_ = tswap32(val);                                    \
       cpu_memory_rw_debug(env_cpu(env), (addr), &val_, sizeof(val_), 1); })

/**
 * put_user_ual:
 *
 * Returns: 0 on success, -1 on error.
 */
#define put_user_ual(arg, p) put_user_u32(arg, p)

/**
 * uaccess_lock_user:
 *
 * The returned pointer should be freed using uaccess_unlock_user().
 */
void *uaccess_lock_user(CPUArchState *env, vaddr addr,
                        size_t len, bool copy);
/**
 * lock_user:
 *
 * The returned pointer should be freed using unlock_user().
 */
#define lock_user(type, p, len, copy) uaccess_lock_user(env, p, len, copy)

/**
 * uaccess_lock_user_string:
 *
 * The returned string should be freed using uaccess_unlock_user().
 */
char *uaccess_lock_user_string(CPUArchState *env, vaddr addr);
/**
 * uaccess_lock_user_string:
 *
 * The returned string should be freed using unlock_user().
 */
#define lock_user_string(p) uaccess_lock_user_string(env, p)

void uaccess_unlock_user(CPUArchState *env, void *p,
                         vaddr addr, size_t len);
#define unlock_user(s, args, len) uaccess_unlock_user(env, s, args, len)

ssize_t uaccess_strlen_user(CPUArchState *env, vaddr addr);
#define target_strlen(p) uaccess_strlen_user(env, p)

#endif /* SEMIHOSTING_SOFTMMU_UACCESS_H */
