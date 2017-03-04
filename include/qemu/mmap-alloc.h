#ifndef QEMU_MMAP_ALLOC_H
#define QEMU_MMAP_ALLOC_H

#include "qemu-common.h"

size_t qemu_fd_getpagesize(int fd);

size_t qemu_mempath_getpagesize(const char *mem_path);

void *qemu_ram_mmap(int fd, size_t size, size_t align, bool shared);

void qemu_ram_munmap(void *ptr, size_t size);

#endif
