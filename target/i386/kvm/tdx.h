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

    QemuMutex lock;

    bool initialized;
    uint64_t attributes;    /* TD attributes */
    uint64_t xfam;
    char *mrconfigid;       /* base64 encoded sha348 digest */
    char *mrowner;          /* base64 encoded sha348 digest */
    char *mrownerconfig;    /* base64 encoded sha348 digest */
} TdxGuest;

#ifdef CONFIG_TDX
bool is_tdx_vm(void);
#else
#define is_tdx_vm() 0
#endif /* CONFIG_TDX */

int tdx_pre_create_vcpu(CPUState *cpu, Error **errp);

#endif /* QEMU_I386_TDX_H */
