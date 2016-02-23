#ifndef QEMU_MEMFD_H
#define QEMU_MEMFD_H


#ifndef F_LINUX_SPECIFIC_BASE
#define F_LINUX_SPECIFIC_BASE 1024
#endif

#ifndef F_ADD_SEALS
#define F_ADD_SEALS (F_LINUX_SPECIFIC_BASE + 9)
#define F_GET_SEALS (F_LINUX_SPECIFIC_BASE + 10)

#define F_SEAL_SEAL     0x0001  /* prevent further seals from being set */
#define F_SEAL_SHRINK   0x0002  /* prevent file from shrinking */
#define F_SEAL_GROW     0x0004  /* prevent file from growing */
#define F_SEAL_WRITE    0x0008  /* prevent writes */
#endif

void *qemu_memfd_alloc(const char *name, size_t size, unsigned int seals,
                       int *fd);
void qemu_memfd_free(void *ptr, size_t size, int fd);
bool qemu_memfd_check(void);

#endif /* QEMU_MEMFD_H */
