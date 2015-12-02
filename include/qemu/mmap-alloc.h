#ifndef QEMU_MMAP_ALLOC
#define QEMU_MMAP_ALLOC

#include "qemu-common.h"

size_t qemu_fd_getpagesize(int fd);

void *qemu_ram_mmap(int fd, size_t size, size_t align, bool shared);

void qemu_ram_munmap(void *ptr, size_t size);

#endif
