/*
 * Semihosting support
 *
 * Copyright (c) 2015 Imagination Technologies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SEMIHOST_H
#define SEMIHOST_H

typedef enum SemihostingTarget {
    SEMIHOSTING_TARGET_AUTO = 0,
    SEMIHOSTING_TARGET_NATIVE,
    SEMIHOSTING_TARGET_GDB
} SemihostingTarget;

/**
 * semihosting_enabled:
 * @is_user: true if guest code is in usermode (i.e. not privileged)
 *
 * Return true if guest code is allowed to make semihosting calls.
 */
bool semihosting_enabled(bool is_user);

SemihostingTarget semihosting_get_target(void);
const char *semihosting_get_arg(int i);
int semihosting_get_argc(void);
const char *semihosting_get_cmdline(void);
void semihosting_arg_fallback(const char *file, const char *cmd);

/* for vl.c hooks */
void qemu_semihosting_enable(void);
int qemu_semihosting_config_options(const char *optstr);
void qemu_semihosting_chardev_init(void);
void qemu_semihosting_console_init(Chardev *);
void qemu_semihosting_guestfd_init(void);

#endif /* SEMIHOST_H */
