/*
 * Helper routines to provide target memory access for semihosting
 * syscalls in system emulation mode.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GPL
 */

#include "qemu/osdep.h"
#include "exec/exec-all.h"
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

ssize_t softmmu_strlen_user(CPUArchState *env, target_ulong addr)
{
    int mmu_idx = cpu_mmu_index(env, false);
    size_t len = 0;

    while (1) {
        size_t left_in_page;
        int flags;
        void *h;

        /* Find the number of bytes remaining in the page. */
        left_in_page = TARGET_PAGE_SIZE - (addr & ~TARGET_PAGE_MASK);

        flags = probe_access_flags(env, addr, MMU_DATA_LOAD,
                                   mmu_idx, true, &h, 0);
        if (flags & TLB_INVALID_MASK) {
            return -1;
        }
        if (flags & TLB_MMIO) {
            do {
                uint8_t c;
                if (cpu_memory_rw_debug(env_cpu(env), addr, &c, 1, 0)) {
                    return -1;
                }
                if (c == 0) {
                    return len;
                }
                addr++;
                len++;
                if (len > INT32_MAX) {
                    return -1;
                }
            } while (--left_in_page != 0);
        } else {
            char *p = memchr(h, 0, left_in_page);
            if (p) {
                len += p - (char *)h;
                return len <= INT32_MAX ? (ssize_t)len : -1;
            }
            addr += left_in_page;
            len += left_in_page;
            if (len > INT32_MAX) {
                return -1;
            }
        }
    }
}

char *softmmu_lock_user_string(CPUArchState *env, target_ulong addr)
{
    ssize_t len = softmmu_strlen_user(env, addr);
    if (len < 0) {
        return NULL;
    }
    return softmmu_lock_user(env, addr, len + 1, true);
}

void softmmu_unlock_user(CPUArchState *env, void *p,
                         target_ulong addr, target_ulong len)
{
    if (len) {
        cpu_memory_rw_debug(env_cpu(env), addr, p, len, 1);
    }
    free(p);
}
