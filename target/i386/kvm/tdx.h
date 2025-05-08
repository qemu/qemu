/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef QEMU_I386_TDX_H
#define QEMU_I386_TDX_H

#ifndef CONFIG_USER_ONLY
#include CONFIG_DEVICES /* CONFIG_TDX */
#endif

#include "confidential-guest.h"

#define TYPE_TDX_GUEST "tdx-guest"
#define TDX_GUEST(obj)  OBJECT_CHECK(TdxGuest, (obj), TYPE_TDX_GUEST)

typedef struct TdxGuestClass {
    X86ConfidentialGuestClass parent_class;
} TdxGuestClass;

typedef struct TdxGuest {
    X86ConfidentialGuest parent_obj;

    uint64_t attributes;    /* TD attributes */
} TdxGuest;

#ifdef CONFIG_TDX
bool is_tdx_vm(void);
#else
#define is_tdx_vm() 0
#endif /* CONFIG_TDX */

#endif /* QEMU_I386_TDX_H */
