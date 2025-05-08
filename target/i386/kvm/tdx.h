/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef QEMU_I386_TDX_H
#define QEMU_I386_TDX_H

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

#endif /* QEMU_I386_TDX_H */
