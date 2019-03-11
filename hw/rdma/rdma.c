/*
 * RDMA device interface
 *
 * Copyright (C) 2018 Oracle
 * Copyright (C) 2018 Red Hat Inc
 *
 * Authors:
 *     Yuval Shaia <yuval.shaia@oracle.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/rdma/rdma.h"
#include "qemu/module.h"

static const TypeInfo rdma_hmp_info = {
    .name = INTERFACE_RDMA_PROVIDER,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(RdmaProviderClass),
};

static void rdma_register_types(void)
{
    type_register_static(&rdma_hmp_info);
}

type_init(rdma_register_types)
