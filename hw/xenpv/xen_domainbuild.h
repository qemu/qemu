#ifndef QEMU_HW_XEN_DOMAINBUILD_H
#define QEMU_HW_XEN_DOMAINBUILD_H

#include "hw/xen/xen_common.h"

int xenstore_domain_init1(const char *kernel, const char *ramdisk,
                          const char *cmdline);
int xenstore_domain_init2(int xenstore_port, int xenstore_mfn,
                          int console_port, int console_mfn);
int xen_domain_build_pv(const char *kernel, const char *ramdisk,
                        const char *cmdline);

#endif /* QEMU_HW_XEN_DOMAINBUILD_H */
