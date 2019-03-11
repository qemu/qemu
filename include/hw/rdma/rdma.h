/*
 * RDMA device interface
 *
 * Copyright (C) 2019 Oracle
 * Copyright (C) 2019 Red Hat Inc
 *
 * Authors:
 *     Yuval Shaia <yuval.shaia@oracle.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef RDMA_H
#define RDMA_H

#include "qom/object.h"

#define INTERFACE_RDMA_PROVIDER "rdma"

#define INTERFACE_RDMA_PROVIDER_CLASS(klass) \
    OBJECT_CLASS_CHECK(RdmaProviderClass, (klass), \
                       INTERFACE_RDMA_PROVIDER)
#define RDMA_PROVIDER_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RdmaProviderClass, (obj), \
                     INTERFACE_RDMA_PROVIDER)
#define RDMA_PROVIDER(obj) \
    INTERFACE_CHECK(RdmaProvider, (obj), \
                    INTERFACE_RDMA_PROVIDER)

typedef struct RdmaProvider RdmaProvider;

typedef struct RdmaProviderClass {
    InterfaceClass parent;

    void (*print_statistics)(Monitor *mon, RdmaProvider *obj);
} RdmaProviderClass;

#endif
