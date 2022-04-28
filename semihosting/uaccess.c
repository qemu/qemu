/*
 * Helper routines to provide target memory access for semihosting
 * syscalls in system emulation mode.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GPL
 */

#include "qemu/osdep.h"
#include "semihosting/softmmu-uaccess.h"

void *softmmu_lock_user(CPUArchState *env, target_ulong addr,
                        target_ulong len, bool copy)
{
    void *p = malloc(len);
    if (p && copy) {
        if (cpu_memory_rw_debug(env_cpu(env), addr, p, len, 0)) {
            free(p);
            p = NULL;
        }
    }
    return p;
}

char *softmmu_lock_user_string(CPUArchState *env, target_ulong addr)
{
    /* TODO: Make this something that isn't fixed size.  */
    char *s = malloc(1024);
    size_t len = 0;

    if (!s) {
        return NULL;
    }
    do {
        if (cpu_memory_rw_debug(env_cpu(env), addr++, s + len, 1, 0)) {
            free(s);
            return NULL;
        }
    } while (s[len++]);
    return s;
}

void softmmu_unlock_user(CPUArchState *env, void *p,
                         target_ulong addr, target_ulong len)
{
    if (len) {
        cpu_memory_rw_debug(env_cpu(env), addr, p, len, 1);
    }
    free(p);
}
