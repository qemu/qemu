/*
 * ARM Semihosting Console Support
 *
 * Copyright (c) 2019 Linaro Ltd
 *
 * Currently ARM is unique in having support for semihosting support
 * in linux-user. So for now we implement the common console API but
 * just for arm linux-user.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/semihosting/console.h"
#include "qemu.h"

int qemu_semihosting_console_out(CPUArchState *env, target_ulong addr, int len)
{
    void *s = lock_user_string(addr);
    len = write(STDERR_FILENO, s, len ? len : strlen(s));
    unlock_user(s, addr, 0);
    return len;
}
