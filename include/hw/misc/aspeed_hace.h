/*
 * ASPEED Hash and Crypto Engine
 *
 * Copyright (c) 2024 Seagate Technology LLC and/or its Affiliates
 * Copyright (C) 2021 IBM Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ASPEED_HACE_H
#define ASPEED_HACE_H

#include "hw/sysbus.h"
#include "crypto/hash.h"

#define TYPE_ASPEED_HACE "aspeed.hace"
#define TYPE_ASPEED_AST2400_HACE TYPE_ASPEED_HACE "-ast2400"
#define TYPE_ASPEED_AST2500_HACE TYPE_ASPEED_HACE "-ast2500"
#define TYPE_ASPEED_AST2600_HACE TYPE_ASPEED_HACE "-ast2600"
#define TYPE_ASPEED_AST1030_HACE TYPE_ASPEED_HACE "-ast1030"
#define TYPE_ASPEED_AST2700_HACE TYPE_ASPEED_HACE "-ast2700"

OBJECT_DECLARE_TYPE(AspeedHACEState, AspeedHACEClass, ASPEED_HACE)

#define ASPEED_HACE_NR_REGS (0x64 >> 2)
#define ASPEED_HACE_MAX_SG  256 /* max number of entries */

struct AspeedHACEState {
    SysBusDevice parent;

    MemoryRegion iomem;
    qemu_irq irq;

    struct iovec iov_cache[ASPEED_HACE_MAX_SG];
    uint32_t regs[ASPEED_HACE_NR_REGS];
    uint32_t total_req_len;
    uint32_t iov_count;

    MemoryRegion *dram_mr;
    AddressSpace dram_as;

    QCryptoHash *hash_ctx;
};


struct AspeedHACEClass {
    SysBusDeviceClass parent_class;

    uint32_t src_mask;
    uint32_t dest_mask;
    uint32_t key_mask;
    uint32_t hash_mask;
    bool raise_crypt_interrupt_workaround;
};

#endif /* ASPEED_HACE_H */
