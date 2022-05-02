/*
 * ARM Compatible Semihosting Console Support.
 *
 * Copyright (c) 2019 Linaro Ltd
 *
 * Currently ARM and RISC-V are unique in having support for
 * semihosting support in linux-user. So for now we implement the
 * common console API but just for arm and risc-v linux-user.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "semihosting/console.h"
#include "qemu.h"
#include "user-internals.h"
#include <termios.h>

/*
 * For linux-user we can safely block. However as we want to return as
 * soon as a character is read we need to tweak the termio to disable
 * line buffering. We restore the old mode afterwards in case the
 * program is expecting more normal behaviour. This is slow but
 * nothing using semihosting console reading is expecting to be fast.
 */
int qemu_semihosting_console_read(CPUState *cs, void *buf, int len)
{
    int ret;
    struct termios old_tio, new_tio;

    /* Disable line-buffering and echo */
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= (~ICANON & ~ECHO);
    new_tio.c_cc[VMIN] = 1;
    new_tio.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

    ret = fread(buf, 1, len, stdin);

    /* restore config */
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);

    return ret;
}

int qemu_semihosting_console_write(void *buf, int len)
{
    return fwrite(buf, 1, len, stderr);
}
