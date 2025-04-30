/*
 * API for host PCI MMIO accesses (e.g. Linux VFIO BARs)
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Farhan Ali <alifm@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HOST_PCI_MMIO_H
#define HOST_PCI_MMIO_H

#include "qemu/bswap.h"
#include "qemu/s390x_pci_mmio.h"

static inline uint8_t host_pci_ldub_p(const void *ioaddr)
{
    uint8_t ret = 0;
#ifdef __s390x__
    ret = s390x_pci_mmio_read_8(ioaddr);
#else
    ret = ldub_p(ioaddr);
#endif

    return ret;
}

static inline uint16_t host_pci_lduw_le_p(const void *ioaddr)
{
    uint16_t ret = 0;
#ifdef __s390x__
    ret = le16_to_cpu(s390x_pci_mmio_read_16(ioaddr));
#else
    ret = lduw_le_p(ioaddr);
#endif

    return ret;
}

static inline uint32_t host_pci_ldl_le_p(const void *ioaddr)
{
    uint32_t ret = 0;
#ifdef __s390x__
    ret = le32_to_cpu(s390x_pci_mmio_read_32(ioaddr));
#else
    ret = ldl_le_p(ioaddr);
#endif

    return ret;
}

static inline uint64_t host_pci_ldq_le_p(const void *ioaddr)
{
    uint64_t ret = 0;
#ifdef __s390x__
    ret = le64_to_cpu(s390x_pci_mmio_read_64(ioaddr));
#else
    ret = ldq_le_p(ioaddr);
#endif

    return ret;
}

static inline void host_pci_stb_p(void *ioaddr, uint8_t val)
{
#ifdef __s390x__
    s390x_pci_mmio_write_8(ioaddr, val);
#else
    stb_p(ioaddr, val);
#endif
}

static inline void host_pci_stw_le_p(void *ioaddr, uint16_t val)
{
#ifdef __s390x__
    s390x_pci_mmio_write_16(ioaddr, cpu_to_le16(val));
#else
    stw_le_p(ioaddr, val);
#endif
}

static inline void host_pci_stl_le_p(void *ioaddr, uint32_t val)
{
#ifdef __s390x__
    s390x_pci_mmio_write_32(ioaddr, cpu_to_le32(val));
#else
    stl_le_p(ioaddr, val);
#endif
}

static inline void host_pci_stq_le_p(void *ioaddr, uint64_t val)
{
#ifdef __s390x__
    s390x_pci_mmio_write_64(ioaddr, cpu_to_le64(val));
#else
    stq_le_p(ioaddr, val);
#endif
}

static inline uint64_t host_pci_ldn_le_p(const void *ioaddr, int sz)
{
    switch (sz) {
    case 1:
        return host_pci_ldub_p(ioaddr);
    case 2:
        return host_pci_lduw_le_p(ioaddr);
    case 4:
        return host_pci_ldl_le_p(ioaddr);
    case 8:
        return host_pci_ldq_le_p(ioaddr);
    default:
        g_assert_not_reached();
    }
}

static inline void host_pci_stn_le_p(void *ioaddr, int sz, uint64_t v)
{
    switch (sz) {
    case 1:
        host_pci_stb_p(ioaddr, v);
        break;
    case 2:
        host_pci_stw_le_p(ioaddr, v);
        break;
    case 4:
        host_pci_stl_le_p(ioaddr, v);
        break;
    case 8:
        host_pci_stq_le_p(ioaddr, v);
        break;
    default:
        g_assert_not_reached();
    }
}

#endif
