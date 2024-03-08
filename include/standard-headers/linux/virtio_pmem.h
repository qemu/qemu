/* SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause */
/*
 * Definitions for virtio-pmem devices.
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * Author(s): Pankaj Gupta <pagupta@redhat.com>
 */

#ifndef _LINUX_VIRTIO_PMEM_H
#define _LINUX_VIRTIO_PMEM_H

#include "standard-headers/linux/types.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_config.h"

/* Feature bits */
/* guest physical address range will be indicated as shared memory region 0 */
#define VIRTIO_PMEM_F_SHMEM_REGION 0

/* shmid of the shared memory region corresponding to the pmem */
#define VIRTIO_PMEM_SHMEM_REGION_ID 0

struct virtio_pmem_config {
	uint64_t start;
	uint64_t size;
};

#define VIRTIO_PMEM_REQ_TYPE_FLUSH      0

struct virtio_pmem_resp {
	/* Host return status corresponding to flush request */
	uint32_t ret;
};

struct virtio_pmem_req {
	/* command type */
	uint32_t type;
};

#endif
