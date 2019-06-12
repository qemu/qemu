/*
 * QEMU HP-PARISC PCI support functions.
 *
 */

#include "qemu/osdep.h"
#include "hppa_sys.h"
#include "qemu/log.h"
#include "sysemu/sysemu.h"
#include "trace.h"


/* Fallback for unassigned PCI I/O operations.  Avoids MCHK.  */

static uint64_t ignore_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void ignore_write(void *opaque, hwaddr addr, uint64_t v, unsigned size)
{
}

const MemoryRegionOps hppa_pci_ignore_ops = {
    .read = ignore_read,
    .write = ignore_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};


/* PCI config space reads/writes, to byte-word addressable memory.  */
static uint64_t bw_conf1_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    PCIBus *b = opaque;
    return pci_data_read(b, addr, size);
}

static void bw_conf1_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    PCIBus *b = opaque;
    pci_data_write(b, addr, val, size);
}

const MemoryRegionOps hppa_pci_conf1_ops = {
    .read = bw_conf1_read,
    .write = bw_conf1_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

/* PCI/EISA Interrupt Acknowledge Cycle.  */

static uint64_t iack_read(void *opaque, hwaddr addr, unsigned size)
{
    return pic_read_irq(isa_pic);
}

static void special_write(void *opaque, hwaddr addr,
                          uint64_t val, unsigned size)
{
    trace_hppa_pci_iack_write();
}

const MemoryRegionOps hppa_pci_iack_ops = {
    .read = iack_read,
    .write = special_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};
