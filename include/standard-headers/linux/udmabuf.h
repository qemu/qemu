/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_UDMABUF_H
#define _LINUX_UDMABUF_H

#include "standard-headers/linux/types.h"

#define UDMABUF_FLAGS_CLOEXEC	0x01

struct udmabuf_create {
	uint32_t memfd;
	uint32_t flags;
	uint64_t offset;
	uint64_t size;
};

struct udmabuf_create_item {
	uint32_t memfd;
	uint32_t __pad;
	uint64_t offset;
	uint64_t size;
};

struct udmabuf_create_list {
	uint32_t flags;
	uint32_t count;
	struct udmabuf_create_item list[];
};

#define UDMABUF_CREATE       _IOW('u', 0x42, struct udmabuf_create)
#define UDMABUF_CREATE_LIST  _IOW('u', 0x43, struct udmabuf_create_list)

#endif /* _LINUX_UDMABUF_H */
