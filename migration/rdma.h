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

#ifndef QEMU_MIGRATION_RDMA_H
#define QEMU_MIGRATION_RDMA_H

void rdma_start_outgoing_migration(void *opaque, const char *host_port,
                                   Error **errp);

void rdma_start_incoming_migration(const char *host_port, Error **errp);


#ifdef CONFIG_RDMA
int qemu_rdma_registration_handle(QEMUFile *f);
int qemu_rdma_registration_start(QEMUFile *f, uint64_t flags);
int qemu_rdma_registration_stop(QEMUFile *f, uint64_t flags);
#else
static inline
int qemu_rdma_registration_handle(QEMUFile *f) { return 0; }
static inline
int qemu_rdma_registration_start(QEMUFile *f, uint64_t flags) { return 0; }
static inline
int qemu_rdma_registration_stop(QEMUFile *f, uint64_t flags) { return 0; }
#endif
#endif
