#ifndef QEMU_MMAP_ALLOC_H
#define QEMU_MMAP_ALLOC_H


size_t qemu_fd_getpagesize(int fd);

size_t qemu_mempath_getpagesize(const char *mem_path);

/**
 * qemu_ram_mmap: mmap anonymous memory, the specified file or device.
 *
 * mmap() abstraction to map guest RAM, simplifying flag handling, taking
 * care of alignment requirements and installing guard pages.
 *
 * Parameters:
 *  @fd: the file or the device to mmap
 *  @size: the number of bytes to be mmaped
 *  @align: if not zero, specify the alignment of the starting mapping address;
 *          otherwise, the alignment in use will be determined by QEMU.
 *  @qemu_map_flags: QEMU_MAP_* flags
 *  @map_offset: map starts at offset of map_offset from the start of fd
 *
 * Internally, MAP_PRIVATE, MAP_ANONYMOUS and MAP_SHARED_VALIDATE are set
 * implicitly based on other parameters.
 *
 * Return:
 *  On success, return a pointer to the mapped area.
 *  On failure, return MAP_FAILED.
 */
void *qemu_ram_mmap(int fd,
                    size_t size,
                    size_t align,
                    uint32_t qemu_map_flags,
                    off_t map_offset);

void qemu_ram_munmap(int fd, void *ptr, size_t size);

/*
 * Abstraction of PROT_ and MAP_ flags as passed to mmap(), for example,
 * consumed by qemu_ram_mmap().
 */

/* Map PROT_READ instead of PROT_READ | PROT_WRITE. */
#define QEMU_MAP_READONLY   (1 << 0)

/* Use MAP_SHARED instead of MAP_PRIVATE. */
#define QEMU_MAP_SHARED     (1 << 1)

/*
 * Use MAP_SYNC | MAP_SHARED_VALIDATE if supported. Ignored without
 * QEMU_MAP_SHARED. If mapping fails, warn and fallback to !QEMU_MAP_SYNC.
 */
#define QEMU_MAP_SYNC       (1 << 2)

/*
 * Use MAP_NORESERVE to skip reservation of swap space (or huge pages if
 * applicable). Bail out if not supported/effective.
 */
#define QEMU_MAP_NORESERVE  (1 << 3)

#endif
