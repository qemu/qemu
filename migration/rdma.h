/*
 * RDMA protocol and interfaces
 *
 * Copyright IBM, Corp. 2010-2013
 * Copyright Red Hat, Inc. 2015-2016
 *
 * Authors:
 *  Michael R. Hines <mrhines@us.ibm.com>
 *  Jiuxing Liu <jl@us.ibm.com>
 *  Daniel P. Berrange <berrange@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/sockets.h"

#ifndef QEMU_MIGRATION_RDMA_H
#define QEMU_MIGRATION_RDMA_H

#include "system/memory.h"

void rdma_start_outgoing_migration(void *opaque, InetSocketAddress *host_port,
                                   Error **errp);

void rdma_start_incoming_migration(InetSocketAddress *host_port, Error **errp);

/*
 * Constants used by rdma return codes
 */
#define RAM_CONTROL_SETUP     0
#define RAM_CONTROL_ROUND     1
#define RAM_CONTROL_FINISH    3

#define RAM_SAVE_CONTROL_NOT_SUPP -1000
#define RAM_SAVE_CONTROL_DELAYED  -2000

#ifdef CONFIG_RDMA
int rdma_registration_handle(QEMUFile *f);
int rdma_registration_start(QEMUFile *f, uint64_t flags);
int rdma_registration_stop(QEMUFile *f, uint64_t flags);
int rdma_block_notification_handle(QEMUFile *f, const char *name);
int rdma_control_save_page(QEMUFile *f, ram_addr_t block_offset,
                           ram_addr_t offset, size_t size);
#else
static inline
int rdma_registration_handle(QEMUFile *f) { return 0; }
static inline
int rdma_registration_start(QEMUFile *f, uint64_t flags) { return 0; }
static inline
int rdma_registration_stop(QEMUFile *f, uint64_t flags) { return 0; }
static inline
int rdma_block_notification_handle(QEMUFile *f, const char *name) { return 0; }
static inline
int rdma_control_save_page(QEMUFile *f, ram_addr_t block_offset,
                           ram_addr_t offset, size_t size)
{
    return RAM_SAVE_CONTROL_NOT_SUPP;
}
#endif
#endif
