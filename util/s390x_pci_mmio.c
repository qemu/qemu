/*
 * s390x PCI MMIO definitions
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Farhan Ali <alifm@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <sys/syscall.h>
#include "qemu/s390x_pci_mmio.h"
#include "elf.h"

union register_pair {
    unsigned __int128 pair;
    struct {
        uint64_t even;
        uint64_t odd;
    };
};

static bool is_mio_supported;

static __attribute__((constructor)) void check_is_mio_supported(void)
{
    is_mio_supported = !!(qemu_getauxval(AT_HWCAP) & HWCAP_S390_PCI_MIO);
}

static uint64_t s390x_pcilgi(const void *ioaddr, size_t len)
{
    union register_pair ioaddr_len = { .even = (uint64_t)ioaddr,
                                       .odd = len };
    uint64_t val;
    int cc;

    asm volatile(
        /* pcilgi */
        ".insn   rre,0xb9d60000,%[val],%[ioaddr_len]\n"
        "ipm     %[cc]\n"
        "srl     %[cc],28\n"
        : [cc] "=d"(cc), [val] "=d"(val),
        [ioaddr_len] "+d"(ioaddr_len.pair) :: "cc");

    if (cc) {
        val = -1ULL;
    }

    return val;
}

static void s390x_pcistgi(void *ioaddr, uint64_t val, size_t len)
{
    union register_pair ioaddr_len = {.even = (uint64_t)ioaddr, .odd = len};

    asm volatile (
        /* pcistgi */
        ".insn   rre,0xb9d40000,%[val],%[ioaddr_len]\n"
        : [ioaddr_len] "+d" (ioaddr_len.pair)
        : [val] "d" (val)
        : "cc", "memory");
}

uint8_t s390x_pci_mmio_read_8(const void *ioaddr)
{
    uint8_t val = 0;

    if (is_mio_supported) {
        val = s390x_pcilgi(ioaddr, sizeof(val));
    } else {
        syscall(__NR_s390_pci_mmio_read, ioaddr, &val, sizeof(val));
    }
    return val;
}

uint16_t s390x_pci_mmio_read_16(const void *ioaddr)
{
    uint16_t val = 0;

    if (is_mio_supported) {
        val = s390x_pcilgi(ioaddr, sizeof(val));
    } else {
        syscall(__NR_s390_pci_mmio_read, ioaddr, &val, sizeof(val));
    }
    return val;
}

uint32_t s390x_pci_mmio_read_32(const void *ioaddr)
{
    uint32_t val = 0;

    if (is_mio_supported) {
        val = s390x_pcilgi(ioaddr, sizeof(val));
    } else {
        syscall(__NR_s390_pci_mmio_read, ioaddr, &val, sizeof(val));
    }
    return val;
}

uint64_t s390x_pci_mmio_read_64(const void *ioaddr)
{
    uint64_t val = 0;

    if (is_mio_supported) {
        val = s390x_pcilgi(ioaddr, sizeof(val));
    } else {
        syscall(__NR_s390_pci_mmio_read, ioaddr, &val, sizeof(val));
    }
    return val;
}

void s390x_pci_mmio_write_8(void *ioaddr, uint8_t val)
{
    if (is_mio_supported) {
        s390x_pcistgi(ioaddr, val, sizeof(val));
    } else {
        syscall(__NR_s390_pci_mmio_write, ioaddr, &val, sizeof(val));
    }
}

void s390x_pci_mmio_write_16(void *ioaddr, uint16_t val)
{
    if (is_mio_supported) {
        s390x_pcistgi(ioaddr, val, sizeof(val));
    } else {
        syscall(__NR_s390_pci_mmio_write, ioaddr, &val, sizeof(val));
    }
}

void s390x_pci_mmio_write_32(void *ioaddr, uint32_t val)
{
    if (is_mio_supported) {
        s390x_pcistgi(ioaddr, val, sizeof(val));
    } else {
        syscall(__NR_s390_pci_mmio_write, ioaddr, &val, sizeof(val));
    }
}

void s390x_pci_mmio_write_64(void *ioaddr, uint64_t val)
{
    if (is_mio_supported) {
        s390x_pcistgi(ioaddr, val, sizeof(val));
    } else {
        syscall(__NR_s390_pci_mmio_write, ioaddr, &val, sizeof(val));
    }
}
