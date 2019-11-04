/*
 * Semihosting support
 *
 * Copyright (c) 2015 Imagination Technologies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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

#ifdef CONFIG_USER_ONLY
static inline bool semihosting_enabled(void)
{
    return true;
}

static inline SemihostingTarget semihosting_get_target(void)
{
    return SEMIHOSTING_TARGET_AUTO;
}

static inline const char *semihosting_get_arg(int i)
{
    return NULL;
}

static inline int semihosting_get_argc(void)
{
    return 0;
}

static inline const char *semihosting_get_cmdline(void)
{
    return NULL;
}

static inline Chardev *semihosting_get_chardev(void)
{
    return NULL;
}
static inline void qemu_semihosting_console_init(void)
{
}
#else /* !CONFIG_USER_ONLY */
bool semihosting_enabled(void);
SemihostingTarget semihosting_get_target(void);
const char *semihosting_get_arg(int i);
int semihosting_get_argc(void);
const char *semihosting_get_cmdline(void);
void semihosting_arg_fallback(const char *file, const char *cmd);
Chardev *semihosting_get_chardev(void);
/* for vl.c hooks */
void qemu_semihosting_enable(void);
int qemu_semihosting_config_options(const char *opt);
void qemu_semihosting_connect_chardevs(void);
void qemu_semihosting_console_init(void);
#endif /* CONFIG_USER_ONLY */

#endif /* SEMIHOST_H */
