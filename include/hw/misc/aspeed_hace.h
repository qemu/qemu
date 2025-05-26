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

#define ASPEED_HACE_MAX_SG  256 /* max number of entries */

struct AspeedHACEState {
    SysBusDevice parent;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t *regs;
    uint32_t total_req_len;

    MemoryRegion *dram_mr;
    AddressSpace dram_as;

    QCryptoHash *hash_ctx;
};


struct AspeedHACEClass {
    SysBusDeviceClass parent_class;

    const MemoryRegionOps *reg_ops;
    uint32_t src_mask;
    uint32_t dest_mask;
    uint32_t key_mask;
    uint32_t hash_mask;
    uint64_t nr_regs;
    bool raise_crypt_interrupt_workaround;
    uint32_t src_hi_mask;
    uint32_t dest_hi_mask;
    uint32_t key_hi_mask;
    bool has_dma64;
};

#endif /* ASPEED_HACE_H */
