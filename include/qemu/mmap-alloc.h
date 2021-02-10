#ifndef QEMU_MMAP_ALLOC_H
#define QEMU_MMAP_ALLOC_H


size_t qemu_fd_getpagesize(int fd);

size_t qemu_mempath_getpagesize(const char *mem_path);

/**
 * qemu_ram_mmap: mmap the specified file or device.
 *
 * Parameters:
 *  @fd: the file or the device to mmap
 *  @size: the number of bytes to be mmaped
 *  @align: if not zero, specify the alignment of the starting mapping address;
 *          otherwise, the alignment in use will be determined by QEMU.
 *  @readonly: true for a read-only mapping, false for read/write.
 *  @shared: map has RAM_SHARED flag.
 *  @is_pmem: map has RAM_PMEM flag.
 *  @map_offset: map starts at offset of map_offset from the start of fd
 *
 * Return:
 *  On success, return a pointer to the mapped area.
 *  On failure, return MAP_FAILED.
 */
void *qemu_ram_mmap(int fd,
                    size_t size,
                    size_t align,
                    bool readonly,
                    bool shared,
                    bool is_pmem,
                    off_t map_offset);

void qemu_ram_munmap(int fd, void *ptr, size_t size);

#endif
