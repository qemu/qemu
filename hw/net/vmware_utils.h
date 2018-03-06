/*
 * QEMU VMWARE paravirtual devices - auxiliary code
 *
 * Copyright (c) 2012 Ravello Systems LTD (http://ravellosystems.com)
 *
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 * Dmitry Fleytman <dmitry@daynix.com>
 * Yan Vugenfirer <yan@daynix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef VMWARE_UTILS_H
#define VMWARE_UTILS_H

#include "qemu/range.h"
#include "vmxnet_debug.h"

/*
 * Shared memory access functions with byte swap support
 * Each function contains printout for reverse-engineering needs
 *
 */
static inline void
vmw_shmem_read(PCIDevice *d, hwaddr addr, void *buf, int len)
{
    VMW_SHPRN("SHMEM r: %" PRIx64 ", len: %d to %p", addr, len, buf);
    pci_dma_read(d, addr, buf, len);
}

static inline void
vmw_shmem_write(PCIDevice *d, hwaddr addr, void *buf, int len)
{
    VMW_SHPRN("SHMEM w: %" PRIx64 ", len: %d to %p", addr, len, buf);
    pci_dma_write(d, addr, buf, len);
}

static inline void
vmw_shmem_rw(PCIDevice *d, hwaddr addr, void *buf, int len, int is_write)
{
    VMW_SHPRN("SHMEM r/w: %" PRIx64 ", len: %d (to %p), is write: %d",
              addr, len, buf, is_write);

    if (is_write)
        pci_dma_write(d, addr, buf, len);
    else
        pci_dma_read(d, addr, buf, len);
}

static inline void
vmw_shmem_set(PCIDevice *d, hwaddr addr, uint8_t val, int len)
{
    int i;
    VMW_SHPRN("SHMEM set: %" PRIx64 ", len: %d (value 0x%X)", addr, len, val);

    for (i = 0; i < len; i++) {
        pci_dma_write(d, addr + i, &val, 1);
    }
}

static inline uint32_t
vmw_shmem_ld8(PCIDevice *d, hwaddr addr)
{
    uint8_t res;
    pci_dma_read(d, addr, &res, 1);
    VMW_SHPRN("SHMEM load8: %" PRIx64 " (value 0x%X)", addr, res);
    return res;
}

static inline void
vmw_shmem_st8(PCIDevice *d, hwaddr addr, uint8_t value)
{
    VMW_SHPRN("SHMEM store8: %" PRIx64 " (value 0x%X)", addr, value);
    pci_dma_write(d, addr, &value, 1);
}

static inline uint32_t
vmw_shmem_ld16(PCIDevice *d, hwaddr addr)
{
    uint16_t res;
    pci_dma_read(d, addr, &res, 2);
    res = le16_to_cpu(res);
    VMW_SHPRN("SHMEM load16: %" PRIx64 " (value 0x%X)", addr, res);
    return res;
}

static inline void
vmw_shmem_st16(PCIDevice *d, hwaddr addr, uint16_t value)
{
    VMW_SHPRN("SHMEM store16: %" PRIx64 " (value 0x%X)", addr, value);
    value = cpu_to_le16(value);
    pci_dma_write(d, addr, &value, 2);
}

static inline uint32_t
vmw_shmem_ld32(PCIDevice *d, hwaddr addr)
{
    uint32_t res;
    pci_dma_read(d, addr, &res, 4);
    res = le32_to_cpu(res);
    VMW_SHPRN("SHMEM load32: %" PRIx64 " (value 0x%X)", addr, res);
    return res;
}

static inline void
vmw_shmem_st32(PCIDevice *d, hwaddr addr, uint32_t value)
{
    VMW_SHPRN("SHMEM store32: %" PRIx64 " (value 0x%X)", addr, value);
    value = cpu_to_le32(value);
    pci_dma_write(d, addr, &value, 4);
}

static inline uint64_t
vmw_shmem_ld64(PCIDevice *d, hwaddr addr)
{
    uint64_t res;
    pci_dma_read(d, addr, &res, 8);
    res = le64_to_cpu(res);
    VMW_SHPRN("SHMEM load64: %" PRIx64 " (value %" PRIx64 ")", addr, res);
    return res;
}

static inline void
vmw_shmem_st64(PCIDevice *d, hwaddr addr, uint64_t value)
{
    VMW_SHPRN("SHMEM store64: %" PRIx64 " (value %" PRIx64 ")", addr, value);
    value = cpu_to_le64(value);
    pci_dma_write(d, addr, &value, 8);
}

/* Macros for simplification of operations on array-style registers */

/*
 * Whether <addr> lies inside of array-style register defined by <base>,
 * number of elements (<cnt>) and element size (<regsize>)
 *
*/
#define VMW_IS_MULTIREG_ADDR(addr, base, cnt, regsize)                 \
    range_covers_byte(base, cnt * regsize, addr)

/*
 * Returns index of given register (<addr>) in array-style register defined by
 * <base> and element size (<regsize>)
 *
*/
#define VMW_MULTIREG_IDX_BY_ADDR(addr, base, regsize)                  \
    (((addr) - (base)) / (regsize))

#endif
