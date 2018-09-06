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

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 0x0002U
#endif

#ifndef MFD_HUGETLB
#define MFD_HUGETLB 0x0004U
#endif

#ifndef MFD_HUGE_SHIFT
#define MFD_HUGE_SHIFT 26
#endif

int qemu_memfd_create(const char *name, size_t size, bool hugetlb,
                      uint64_t hugetlbsize, unsigned int seals, Error **errp);
bool qemu_memfd_alloc_check(void);
void *qemu_memfd_alloc(const char *name, size_t size, unsigned int seals,
                       int *fd, Error **errp);
void qemu_memfd_free(void *ptr, size_t size, int fd);
bool qemu_memfd_check(unsigned int flags);

#endif /* QEMU_MEMFD_H */
