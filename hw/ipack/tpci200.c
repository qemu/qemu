/*
 * QEMU TEWS TPCI200 IndustryPack carrier emulation
 *
 * Copyright (C) 2012 Igalia, S.L.
 * Author: Alberto Garcia <berto@igalia.com>
 *
 * This code is licensed under the GNU GPL v2 or (at your option) any
 * later version.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/ipack/ipack.h"
#include "hw/irq.h"
#include "hw/pci/pci_device.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/module.h"
#include "qom/object.h"

/* #define DEBUG_TPCI */

#ifdef DEBUG_TPCI
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, "TPCI200: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

#define N_MODULES 4

#define IP_ID_SPACE  2
#define IP_INT_SPACE 3
#define IP_IO_SPACE_ADDR_MASK  0x7F
#define IP_ID_SPACE_ADDR_MASK  0x3F
#define IP_INT_SPACE_ADDR_MASK 0x3F

#define STATUS_INT(IP, INTNO) BIT((IP) * 2 + (INTNO))
#define STATUS_TIME(IP)       BIT((IP) + 12)
#define STATUS_ERR_ANY        0xF00

#define CTRL_CLKRATE          BIT(0)
#define CTRL_RECOVER          BIT(1)
#define CTRL_TIME_INT         BIT(2)
#define CTRL_ERR_INT          BIT(3)
#define CTRL_INT_EDGE(INTNO)  BIT(4 + (INTNO))
#define CTRL_INT(INTNO)       BIT(6 + (INTNO))

#define REG_REV_ID    0x00
#define REG_IP_A_CTRL 0x02
#define REG_IP_B_CTRL 0x04
#define REG_IP_C_CTRL 0x06
#define REG_IP_D_CTRL 0x08
#define REG_RESET     0x0A
#define REG_STATUS    0x0C
#define IP_N_FROM_REG(REG) ((REG) / 2 - 1)

struct TPCI200State {
    PCIDevice dev;
    IPackBus bus;
    MemoryRegion mmio;
    MemoryRegion io;
    MemoryRegion las0;
    MemoryRegion las1;
    MemoryRegion las2;
    MemoryRegion las3;
    bool big_endian[3];
    uint8_t ctrl[N_MODULES];
    uint16_t status;
    uint8_t int_set;
};

#define TYPE_TPCI200 "tpci200"

OBJECT_DECLARE_SIMPLE_TYPE(TPCI200State, TPCI200)

static const uint8_t local_config_regs[] = {
    0x00, 0xFF, 0xFF, 0x0F, 0x00, 0xFC, 0xFF, 0x0F, 0x00, 0x00, 0x00,
    0x0E, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x08, 0x01, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0xA0, 0x60, 0x41, 0xD4,
    0xA2, 0x20, 0x41, 0x14, 0xA2, 0x20, 0x41, 0x14, 0xA2, 0x20, 0x01,
    0x14, 0x00, 0x00, 0x00, 0x00, 0x81, 0x00, 0x00, 0x08, 0x01, 0x02,
    0x00, 0x04, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x80, 0x02, 0x41,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x7A, 0x00, 0x52, 0x92, 0x24, 0x02
};

static void adjust_addr(bool big_endian, hwaddr *addr, unsigned size)
{
    /* During 8 bit access in big endian mode,
       odd and even addresses are swapped */
    if (big_endian && size == 1) {
        *addr ^= 1;
    }
}

static uint64_t adjust_value(bool big_endian, uint64_t *val, unsigned size)
{
    /* Local spaces only support 8/16 bit access,
     * so there's no need to care for sizes > 2 */
    if (big_endian && size == 2) {
        *val = bswap16(*val);
    }
    return *val;
}

static void tpci200_set_irq(void *opaque, int intno, int level)
{
    IPackDevice *ip = opaque;
    IPackBus *bus = IPACK_BUS(qdev_get_parent_bus(DEVICE(ip)));
    PCIDevice *pcidev = PCI_DEVICE(BUS(bus)->parent);
    TPCI200State *dev = TPCI200(pcidev);
    unsigned ip_n = ip->slot;
    uint16_t prev_status = dev->status;

    assert(ip->slot >= 0 && ip->slot < N_MODULES);

    /* The requested interrupt must be enabled in the IP CONTROL
     * register */
    if (!(dev->ctrl[ip_n] & CTRL_INT(intno))) {
        return;
    }

    /* Update the interrupt status in the IP STATUS register */
    if (level) {
        dev->status |=  STATUS_INT(ip_n, intno);
    } else {
        dev->status &= ~STATUS_INT(ip_n, intno);
    }

    /* Return if there are no changes */
    if (dev->status == prev_status) {
        return;
    }

    DPRINTF("IP %u INT%u#: %u\n", ip_n, intno, level);

    /* Check if the interrupt is edge sensitive */
    if (dev->ctrl[ip_n] & CTRL_INT_EDGE(intno)) {
        if (level) {
            pci_set_irq(&dev->dev, !dev->int_set);
            pci_set_irq(&dev->dev,  dev->int_set);
        }
    } else {
        unsigned i, j;
        uint16_t level_status = dev->status;

        /* Check if there are any level sensitive interrupts set by
           removing the ones that are edge sensitive from the status
           register */
        for (i = 0; i < N_MODULES; i++) {
            for (j = 0; j < 2; j++) {
                if (dev->ctrl[i] & CTRL_INT_EDGE(j)) {
                    level_status &= ~STATUS_INT(i, j);
                }
            }
        }

        if (level_status && !dev->int_set) {
            pci_irq_assert(&dev->dev);
            dev->int_set = 1;
        } else if (!level_status && dev->int_set) {
            pci_irq_deassert(&dev->dev);
            dev->int_set = 0;
        }
    }
}

static uint64_t tpci200_read_cfg(void *opaque, hwaddr addr, unsigned size)
{
    TPCI200State *s = opaque;
    uint8_t ret = 0;
    if (addr < ARRAY_SIZE(local_config_regs)) {
        ret = local_config_regs[addr];
    }
    /* Endianness is stored in the first bit of these registers */
    if ((addr == 0x2b && s->big_endian[0]) ||
        (addr == 0x2f && s->big_endian[1]) ||
        (addr == 0x33 && s->big_endian[2])) {
        ret |= 1;
    }
    DPRINTF("Read from LCR 0x%x: 0x%x\n", (unsigned) addr, (unsigned) ret);
    return ret;
}

static void tpci200_write_cfg(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    TPCI200State *s = opaque;
    /* Endianness is stored in the first bit of these registers */
    if (addr == 0x2b || addr == 0x2f || addr == 0x33) {
        unsigned las = (addr - 0x2b) / 4;
        s->big_endian[las] = val & 1;
        DPRINTF("LAS%u big endian mode: %u\n", las, (unsigned) val & 1);
    } else {
        DPRINTF("Write to LCR 0x%x: 0x%x\n", (unsigned) addr, (unsigned) val);
    }
}

static uint64_t tpci200_read_las0(void *opaque, hwaddr addr, unsigned size)
{
    TPCI200State *s = opaque;
    uint64_t ret = 0;

    switch (addr) {

    case REG_REV_ID:
        DPRINTF("Read REVISION ID\n"); /* Current value is 0x00 */
        break;

    case REG_IP_A_CTRL:
    case REG_IP_B_CTRL:
    case REG_IP_C_CTRL:
    case REG_IP_D_CTRL:
        {
            unsigned ip_n = IP_N_FROM_REG(addr);
            ret = s->ctrl[ip_n];
            DPRINTF("Read IP %c CONTROL: 0x%x\n", 'A' + ip_n, (unsigned) ret);
        }
        break;

    case REG_RESET:
        DPRINTF("Read RESET\n"); /* Not implemented */
        break;

    case REG_STATUS:
        ret = s->status;
        DPRINTF("Read STATUS: 0x%x\n", (unsigned) ret);
        break;

    /* Reserved */
    default:
        DPRINTF("Unsupported read from LAS0 0x%x\n", (unsigned) addr);
        break;
    }

    return adjust_value(s->big_endian[0], &ret, size);
}

static void tpci200_write_las0(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    TPCI200State *s = opaque;

    adjust_value(s->big_endian[0], &val, size);

    switch (addr) {

    case REG_REV_ID:
        DPRINTF("Write Revision ID: 0x%x\n", (unsigned) val); /* No effect */
        break;

    case REG_IP_A_CTRL:
    case REG_IP_B_CTRL:
    case REG_IP_C_CTRL:
    case REG_IP_D_CTRL:
        {
            unsigned ip_n = IP_N_FROM_REG(addr);
            s->ctrl[ip_n] = val;
            DPRINTF("Write IP %c CONTROL: 0x%x\n", 'A' + ip_n, (unsigned) val);
        }
        break;

    case REG_RESET:
        DPRINTF("Write RESET: 0x%x\n", (unsigned) val); /* Not implemented */
        break;

    case REG_STATUS:
        {
            unsigned i;

            for (i = 0; i < N_MODULES; i++) {
                IPackDevice *ip = ipack_device_find(&s->bus, i);

                if (ip != NULL) {
                    if (val & STATUS_INT(i, 0)) {
                        DPRINTF("Clear IP %c INT0# status\n", 'A' + i);
                        qemu_irq_lower(ip->irq[0]);
                    }
                    if (val & STATUS_INT(i, 1)) {
                        DPRINTF("Clear IP %c INT1# status\n", 'A' + i);
                        qemu_irq_lower(ip->irq[1]);
                    }
                }

                if (val & STATUS_TIME(i)) {
                    DPRINTF("Clear IP %c timeout\n", 'A' + i);
                    s->status &= ~STATUS_TIME(i);
                }
            }

            if (val & STATUS_ERR_ANY) {
                DPRINTF("Unexpected write to STATUS register: 0x%x\n",
                        (unsigned) val);
            }
        }
        break;

    /* Reserved */
    default:
        DPRINTF("Unsupported write to LAS0 0x%x: 0x%x\n",
                (unsigned) addr, (unsigned) val);
        break;
    }
}

static uint64_t tpci200_read_las1(void *opaque, hwaddr addr, unsigned size)
{
    TPCI200State *s = opaque;
    IPackDevice *ip;
    uint64_t ret = 0;
    unsigned ip_n, space;
    uint8_t offset;

    adjust_addr(s->big_endian[1], &addr, size);

    /*
     * The address is divided into the IP module number (0-4), the IP
     * address space (I/O, ID, INT) and the offset within that space.
     */
    ip_n = addr >> 8;
    space = (addr >> 6) & 3;
    ip = ipack_device_find(&s->bus, ip_n);

    if (ip == NULL) {
        DPRINTF("Read LAS1: IP module %u not installed\n", ip_n);
    } else {
        IPackDeviceClass *k = IPACK_DEVICE_GET_CLASS(ip);
        switch (space) {

        case IP_ID_SPACE:
            offset = addr & IP_ID_SPACE_ADDR_MASK;
            if (k->id_read) {
                ret = k->id_read(ip, offset);
            }
            break;

        case IP_INT_SPACE:
            offset = addr & IP_INT_SPACE_ADDR_MASK;

            /* Read address 0 to ACK IP INT0# and address 2 to ACK IP INT1# */
            if (offset == 0 || offset == 2) {
                unsigned intno = offset / 2;
                bool int_set = s->status & STATUS_INT(ip_n, intno);
                bool int_edge_sensitive = s->ctrl[ip_n] & CTRL_INT_EDGE(intno);
                if (int_set && !int_edge_sensitive) {
                    qemu_irq_lower(ip->irq[intno]);
                }
            }

            if (k->int_read) {
                ret = k->int_read(ip, offset);
            }
            break;

        default:
            offset = addr & IP_IO_SPACE_ADDR_MASK;
            if (k->io_read) {
                ret = k->io_read(ip, offset);
            }
            break;
        }
    }

    return adjust_value(s->big_endian[1], &ret, size);
}

static void tpci200_write_las1(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    TPCI200State *s = opaque;
    IPackDevice *ip;
    unsigned ip_n, space;
    uint8_t offset;

    adjust_addr(s->big_endian[1], &addr, size);
    adjust_value(s->big_endian[1], &val, size);

    /*
     * The address is divided into the IP module number, the IP
     * address space (I/O, ID, INT) and the offset within that space.
     */
    ip_n = addr >> 8;
    space = (addr >> 6) & 3;
    ip = ipack_device_find(&s->bus, ip_n);

    if (ip == NULL) {
        DPRINTF("Write LAS1: IP module %u not installed\n", ip_n);
    } else {
        IPackDeviceClass *k = IPACK_DEVICE_GET_CLASS(ip);
        switch (space) {

        case IP_ID_SPACE:
            offset = addr & IP_ID_SPACE_ADDR_MASK;
            if (k->id_write) {
                k->id_write(ip, offset, val);
            }
            break;

        case IP_INT_SPACE:
            offset = addr & IP_INT_SPACE_ADDR_MASK;
            if (k->int_write) {
                k->int_write(ip, offset, val);
            }
            break;

        default:
            offset = addr & IP_IO_SPACE_ADDR_MASK;
            if (k->io_write) {
                k->io_write(ip, offset, val);
            }
            break;
        }
    }
}

static uint64_t tpci200_read_las2(void *opaque, hwaddr addr, unsigned size)
{
    TPCI200State *s = opaque;
    IPackDevice *ip;
    uint64_t ret = 0;
    unsigned ip_n;
    uint32_t offset;

    adjust_addr(s->big_endian[2], &addr, size);

    /*
     * The address is divided into the IP module number and the offset
     * within the IP module MEM space.
     */
    ip_n = addr >> 23;
    offset = addr & 0x7fffff;
    ip = ipack_device_find(&s->bus, ip_n);

    if (ip == NULL) {
        DPRINTF("Read LAS2: IP module %u not installed\n", ip_n);
    } else {
        IPackDeviceClass *k = IPACK_DEVICE_GET_CLASS(ip);
        if (k->mem_read16) {
            ret = k->mem_read16(ip, offset);
        }
    }

    return adjust_value(s->big_endian[2], &ret, size);
}

static void tpci200_write_las2(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    TPCI200State *s = opaque;
    IPackDevice *ip;
    unsigned ip_n;
    uint32_t offset;

    adjust_addr(s->big_endian[2], &addr, size);
    adjust_value(s->big_endian[2], &val, size);

    /*
     * The address is divided into the IP module number and the offset
     * within the IP module MEM space.
     */
    ip_n = addr >> 23;
    offset = addr & 0x7fffff;
    ip = ipack_device_find(&s->bus, ip_n);

    if (ip == NULL) {
        DPRINTF("Write LAS2: IP module %u not installed\n", ip_n);
    } else {
        IPackDeviceClass *k = IPACK_DEVICE_GET_CLASS(ip);
        if (k->mem_write16) {
            k->mem_write16(ip, offset, val);
        }
    }
}

static uint64_t tpci200_read_las3(void *opaque, hwaddr addr, unsigned size)
{
    TPCI200State *s = opaque;
    IPackDevice *ip;
    uint64_t ret = 0;
    /*
     * The address is divided into the IP module number and the offset
     * within the IP module MEM space.
     */
    unsigned ip_n = addr >> 22;
    uint32_t offset = addr & 0x3fffff;

    ip = ipack_device_find(&s->bus, ip_n);

    if (ip == NULL) {
        DPRINTF("Read LAS3: IP module %u not installed\n", ip_n);
    } else {
        IPackDeviceClass *k = IPACK_DEVICE_GET_CLASS(ip);
        if (k->mem_read8) {
            ret = k->mem_read8(ip, offset);
        }
    }

    return ret;
}

static void tpci200_write_las3(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    TPCI200State *s = opaque;
    IPackDevice *ip;
    /*
     * The address is divided into the IP module number and the offset
     * within the IP module MEM space.
     */
    unsigned ip_n = addr >> 22;
    uint32_t offset = addr & 0x3fffff;

    ip = ipack_device_find(&s->bus, ip_n);

    if (ip == NULL) {
        DPRINTF("Write LAS3: IP module %u not installed\n", ip_n);
    } else {
        IPackDeviceClass *k = IPACK_DEVICE_GET_CLASS(ip);
        if (k->mem_write8) {
            k->mem_write8(ip, offset, val);
        }
    }
}

static const MemoryRegionOps tpci200_cfg_ops = {
    .read = tpci200_read_cfg,
    .write = tpci200_write_cfg,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid =  {
        .min_access_size = 1,
        .max_access_size = 4
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1
    }
};

static const MemoryRegionOps tpci200_las0_ops = {
    .read = tpci200_read_las0,
    .write = tpci200_write_las0,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid =  {
        .min_access_size = 2,
        .max_access_size = 2
    }
};

static const MemoryRegionOps tpci200_las1_ops = {
    .read = tpci200_read_las1,
    .write = tpci200_write_las1,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid =  {
        .min_access_size = 1,
        .max_access_size = 2
    }
};

static const MemoryRegionOps tpci200_las2_ops = {
    .read = tpci200_read_las2,
    .write = tpci200_write_las2,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid =  {
        .min_access_size = 1,
        .max_access_size = 2
    }
};

static const MemoryRegionOps tpci200_las3_ops = {
    .read = tpci200_read_las3,
    .write = tpci200_write_las3,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid =  {
        .min_access_size = 1,
        .max_access_size = 1
    }
};

static void tpci200_realize(PCIDevice *pci_dev, Error **errp)
{
    TPCI200State *s = TPCI200(pci_dev);
    uint8_t *c = s->dev.config;

    pci_set_word(c + PCI_COMMAND, 0x0003);
    pci_set_word(c + PCI_STATUS,  0x0280);

    pci_set_byte(c + PCI_INTERRUPT_PIN, 0x01); /* Interrupt pin A */

    pci_set_byte(c + PCI_CAPABILITY_LIST, 0x40);
    pci_set_long(c + 0x40, 0x48014801);
    pci_set_long(c + 0x48, 0x00024C06);
    pci_set_long(c + 0x4C, 0x00000003);

    memory_region_init_io(&s->mmio, OBJECT(s), &tpci200_cfg_ops,
                          s, "tpci200_mmio", 128);
    memory_region_init_io(&s->io, OBJECT(s),   &tpci200_cfg_ops,
                          s, "tpci200_io",   128);
    memory_region_init_io(&s->las0, OBJECT(s), &tpci200_las0_ops,
                          s, "tpci200_las0", 256);
    memory_region_init_io(&s->las1, OBJECT(s), &tpci200_las1_ops,
                          s, "tpci200_las1", 1024);
    memory_region_init_io(&s->las2, OBJECT(s), &tpci200_las2_ops,
                          s, "tpci200_las2", 32 * MiB);
    memory_region_init_io(&s->las3, OBJECT(s), &tpci200_las3_ops,
                          s, "tpci200_las3", 16 * MiB);
    pci_register_bar(&s->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
    pci_register_bar(&s->dev, 1, PCI_BASE_ADDRESS_SPACE_IO,     &s->io);
    pci_register_bar(&s->dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->las0);
    pci_register_bar(&s->dev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->las1);
    pci_register_bar(&s->dev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->las2);
    pci_register_bar(&s->dev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->las3);

    ipack_bus_init(&s->bus, sizeof(s->bus), DEVICE(pci_dev),
                   N_MODULES, tpci200_set_irq);
}

static const VMStateDescription vmstate_tpci200 = {
    .name = "tpci200",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, TPCI200State),
        VMSTATE_BOOL_ARRAY(big_endian, TPCI200State, 3),
        VMSTATE_UINT8_ARRAY(ctrl, TPCI200State, N_MODULES),
        VMSTATE_UINT16(status, TPCI200State),
        VMSTATE_UINT8(int_set, TPCI200State),
        VMSTATE_END_OF_LIST()
    }
};

static void tpci200_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = tpci200_realize;
    k->vendor_id = PCI_VENDOR_ID_TEWS;
    k->device_id = PCI_DEVICE_ID_TEWS_TPCI200;
    k->class_id = PCI_CLASS_BRIDGE_OTHER;
    k->subsystem_vendor_id = PCI_VENDOR_ID_TEWS;
    k->subsystem_id = 0x300A;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->desc = "TEWS TPCI200 IndustryPack carrier";
    dc->vmsd = &vmstate_tpci200;
}

static const TypeInfo tpci200_info = {
    .name          = TYPE_TPCI200,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(TPCI200State),
    .class_init    = tpci200_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void tpci200_register_types(void)
{
    type_register_static(&tpci200_info);
}

type_init(tpci200_register_types)
