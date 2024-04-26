/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Declaration of guest_base.
 *  Copyright (c) 2003 Fabrice Bellard
 */

#ifndef USER_GUEST_BASE_H
#define USER_GUEST_BASE_H

#ifndef CONFIG_USER_ONLY
#error Cannot include this header from system emulation
#endif

extern uintptr_t guest_base;

extern bool have_guest_base;

#endif
