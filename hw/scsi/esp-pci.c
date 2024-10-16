/*
 * QEMU ESP/NCR53C9x emulation
 *
 * Copyright (c) 2005-2006 Fabrice Bellard
 * Copyright (c) 2012 Herve Poussineau
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/pci/pci_device.h"
#include "hw/irq.h"
#include "hw/nvram/eeprom93xx.h"
#include "hw/scsi/esp.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"

#define TYPE_AM53C974_DEVICE "am53c974"

typedef struct PCIESPState PCIESPState;
DECLARE_INSTANCE_CHECKER(PCIESPState, PCI_ESP,
                         TYPE_AM53C974_DEVICE)

#define DMA_CMD   0x0
#define DMA_STC   0x1
#define DMA_SPA   0x2
#define DMA_WBC   0x3
#define DMA_WAC   0x4
#define DMA_STAT  0x5
#define DMA_SMDLA 0x6
#define DMA_WMAC  0x7

#define DMA_CMD_MASK   0x03
#define DMA_CMD_DIAG   0x04
#define DMA_CMD_MDL    0x10
#define DMA_CMD_INTE_P 0x20
#define DMA_CMD_INTE_D 0x40
#define DMA_CMD_DIR    0x80

#define DMA_STAT_PWDN    0x01
#define DMA_STAT_ERROR   0x02
#define DMA_STAT_ABORT   0x04
#define DMA_STAT_DONE    0x08
#define DMA_STAT_SCSIINT 0x10
#define DMA_STAT_BCMBLT  0x20

#define SBAC_STATUS (1 << 24)

struct PCIESPState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    MemoryRegion io;
    uint32_t dma_regs[8];
    uint32_t sbac;
    ESPState esp;
};

static void esp_pci_update_irq(PCIESPState *pci)
{
    int scsi_level = !!(pci->dma_regs[DMA_STAT] & DMA_STAT_SCSIINT);
    int dma_level = (pci->dma_regs[DMA_CMD] & DMA_CMD_INTE_D) ?
                    !!(pci->dma_regs[DMA_STAT] & DMA_STAT_DONE) : 0;
    int level = scsi_level || dma_level;

    pci_set_irq(PCI_DEVICE(pci), level);
}

static void esp_irq_handler(void *opaque, int irq_num, int level)
{
    PCIESPState *pci = PCI_ESP(opaque);

    if (level) {
        pci->dma_regs[DMA_STAT] |= DMA_STAT_SCSIINT;

        /*
         * If raising the ESP IRQ to indicate end of DMA transfer, set
         * DMA_STAT_DONE at the same time. In theory this should be done in
         * esp_pci_dma_memory_rw(), however there is a delay between setting
         * DMA_STAT_DONE and the ESP IRQ arriving which is visible to the
         * guest that can cause confusion e.g. Linux
         */
        if ((pci->dma_regs[DMA_CMD] & DMA_CMD_MASK) == 0x3 &&
            pci->dma_regs[DMA_WBC] == 0) {
                pci->dma_regs[DMA_STAT] |= DMA_STAT_DONE;
        }
    } else {
        pci->dma_regs[DMA_STAT] &= ~DMA_STAT_SCSIINT;
    }

    esp_pci_update_irq(pci);
}

static void esp_pci_handle_idle(PCIESPState *pci, uint32_t val)
{
    ESPState *s = &pci->esp;

    trace_esp_pci_dma_idle(val);
    esp_dma_enable(s, 0, 0);
}

static void esp_pci_handle_blast(PCIESPState *pci, uint32_t val)
{
    trace_esp_pci_dma_blast(val);
    qemu_log_mask(LOG_UNIMP, "am53c974: cmd BLAST not implemented\n");
    pci->dma_regs[DMA_STAT] |= DMA_STAT_BCMBLT;
}

static void esp_pci_handle_abort(PCIESPState *pci, uint32_t val)
{
    ESPState *s = &pci->esp;

    trace_esp_pci_dma_abort(val);
    if (s->current_req) {
        scsi_req_cancel(s->current_req);
    }
}

static void esp_pci_handle_start(PCIESPState *pci, uint32_t val)
{
    ESPState *s = &pci->esp;

    trace_esp_pci_dma_start(val);

    pci->dma_regs[DMA_WBC] = pci->dma_regs[DMA_STC];
    pci->dma_regs[DMA_WAC] = pci->dma_regs[DMA_SPA];
    pci->dma_regs[DMA_WMAC] = pci->dma_regs[DMA_SMDLA];

    pci->dma_regs[DMA_STAT] &= ~(DMA_STAT_BCMBLT | DMA_STAT_SCSIINT
                               | DMA_STAT_DONE | DMA_STAT_ABORT
                               | DMA_STAT_ERROR | DMA_STAT_PWDN);

    esp_dma_enable(s, 0, 1);
}

static void esp_pci_dma_write(PCIESPState *pci, uint32_t saddr, uint32_t val)
{
    trace_esp_pci_dma_write(saddr, pci->dma_regs[saddr], val);
    switch (saddr) {
    case DMA_CMD:
        pci->dma_regs[saddr] = val;
        switch (val & DMA_CMD_MASK) {
        case 0x0: /* IDLE */
            esp_pci_handle_idle(pci, val);
            break;
        case 0x1: /* BLAST */
            esp_pci_handle_blast(pci, val);
            break;
        case 0x2: /* ABORT */
            esp_pci_handle_abort(pci, val);
            break;
        case 0x3: /* START */
            esp_pci_handle_start(pci, val);
            break;
        default: /* can't happen */
            abort();
        }
        break;
    case DMA_STC:
    case DMA_SPA:
    case DMA_SMDLA:
        pci->dma_regs[saddr] = val;
        break;
    case DMA_STAT:
        if (pci->sbac & SBAC_STATUS) {
            /* clear some bits on write */
            uint32_t mask = DMA_STAT_ERROR | DMA_STAT_ABORT | DMA_STAT_DONE;
            pci->dma_regs[DMA_STAT] &= ~(val & mask);
            esp_pci_update_irq(pci);
        }
        break;
    default:
        trace_esp_pci_error_invalid_write_dma(val, saddr);
        return;
    }
}

static uint32_t esp_pci_dma_read(PCIESPState *pci, uint32_t saddr)
{
    uint32_t val;

    val = pci->dma_regs[saddr];
    if (saddr == DMA_STAT) {
        if (!(pci->sbac & SBAC_STATUS)) {
            pci->dma_regs[DMA_STAT] &= ~(DMA_STAT_ERROR | DMA_STAT_ABORT |
                                         DMA_STAT_DONE);
            esp_pci_update_irq(pci);
        }
    }

    trace_esp_pci_dma_read(saddr, val);
    return val;
}

static void esp_pci_io_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned int size)
{
    PCIESPState *pci = opaque;
    ESPState *s = &pci->esp;

    if (size < 4 || addr & 3) {
        /* need to upgrade request: we only support 4-bytes accesses */
        uint32_t current = 0, mask;
        int shift;

        if (addr < 0x40) {
            current = s->wregs[addr >> 2];
        } else if (addr < 0x60) {
            current = pci->dma_regs[(addr - 0x40) >> 2];
        } else if (addr < 0x74) {
            current = pci->sbac;
        }

        shift = (4 - size) * 8;
        mask = (~(uint32_t)0 << shift) >> shift;

        shift = ((4 - (addr & 3)) & 3) * 8;
        val <<= shift;
        val |= current & ~(mask << shift);
        addr &= ~3;
        size = 4;
    }
    g_assert(size >= 4);

    if (addr < 0x40) {
        /* SCSI core reg */
        esp_reg_write(s, addr >> 2, val);
    } else if (addr < 0x60) {
        /* PCI DMA CCB */
        esp_pci_dma_write(pci, (addr - 0x40) >> 2, val);
    } else if (addr == 0x70) {
        /* DMA SCSI Bus and control */
        trace_esp_pci_sbac_write(pci->sbac, val);
        pci->sbac = val;
    } else {
        trace_esp_pci_error_invalid_write((int)addr);
    }
}

static uint64_t esp_pci_io_read(void *opaque, hwaddr addr,
                                unsigned int size)
{
    PCIESPState *pci = opaque;
    ESPState *s = &pci->esp;
    uint32_t ret;

    if (addr < 0x40) {
        /* SCSI core reg */
        ret = esp_reg_read(s, addr >> 2);
    } else if (addr < 0x60) {
        /* PCI DMA CCB */
        ret = esp_pci_dma_read(pci, (addr - 0x40) >> 2);
    } else if (addr == 0x70) {
        /* DMA SCSI Bus and control */
        trace_esp_pci_sbac_read(pci->sbac);
        ret = pci->sbac;
    } else {
        /* Invalid region */
        trace_esp_pci_error_invalid_read((int)addr);
        ret = 0;
    }

    /* give only requested data */
    ret >>= (addr & 3) * 8;
    ret &= ~(~(uint64_t)0 << (8 * size));

    return ret;
}

static void esp_pci_dma_memory_rw(PCIESPState *pci, uint8_t *buf, int len,
                                  DMADirection dir)
{
    dma_addr_t addr;
    DMADirection expected_dir;

    if (pci->dma_regs[DMA_CMD] & DMA_CMD_DIR) {
        expected_dir = DMA_DIRECTION_FROM_DEVICE;
    } else {
        expected_dir = DMA_DIRECTION_TO_DEVICE;
    }

    if (dir != expected_dir) {
        trace_esp_pci_error_invalid_dma_direction();
        return;
    }

    if (pci->dma_regs[DMA_STAT] & DMA_CMD_MDL) {
        qemu_log_mask(LOG_UNIMP, "am53c974: MDL transfer not implemented\n");
    }

    addr = pci->dma_regs[DMA_WAC];
    if (pci->dma_regs[DMA_WBC] < len) {
        len = pci->dma_regs[DMA_WBC];
    }

    pci_dma_rw(PCI_DEVICE(pci), addr, buf, len, dir, MEMTXATTRS_UNSPECIFIED);

    /* update status registers */
    pci->dma_regs[DMA_WBC] -= len;
    pci->dma_regs[DMA_WAC] += len;
}

static void esp_pci_dma_memory_read(void *opaque, uint8_t *buf, int len)
{
    PCIESPState *pci = opaque;
    esp_pci_dma_memory_rw(pci, buf, len, DMA_DIRECTION_TO_DEVICE);
}

static void esp_pci_dma_memory_write(void *opaque, uint8_t *buf, int len)
{
    PCIESPState *pci = opaque;
    esp_pci_dma_memory_rw(pci, buf, len, DMA_DIRECTION_FROM_DEVICE);
}

static const MemoryRegionOps esp_pci_io_ops = {
    .read = esp_pci_io_read,
    .write = esp_pci_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void esp_pci_hard_reset(DeviceState *dev)
{
    PCIESPState *pci = PCI_ESP(dev);
    ESPState *s = &pci->esp;

    esp_hard_reset(s);
    pci->dma_regs[DMA_CMD] &= ~(DMA_CMD_DIR | DMA_CMD_INTE_D | DMA_CMD_INTE_P
                              | DMA_CMD_MDL | DMA_CMD_DIAG | DMA_CMD_MASK);
    pci->dma_regs[DMA_WBC] &= ~0xffff;
    pci->dma_regs[DMA_WAC] = 0xffffffff;
    pci->dma_regs[DMA_STAT] &= ~(DMA_STAT_BCMBLT | DMA_STAT_SCSIINT
                               | DMA_STAT_DONE | DMA_STAT_ABORT
                               | DMA_STAT_ERROR);
    pci->dma_regs[DMA_WMAC] = 0xfffffffd;
}

static const VMStateDescription vmstate_esp_pci_scsi = {
    .name = "pciespscsi",
    .version_id = 2,
    .minimum_version_id = 1,
    .pre_save = esp_pre_save,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIESPState),
        VMSTATE_BUFFER_UNSAFE(dma_regs, PCIESPState, 0, 8 * sizeof(uint32_t)),
        VMSTATE_UINT8_V(esp.mig_version_id, PCIESPState, 2),
        VMSTATE_STRUCT(esp, PCIESPState, 0, vmstate_esp, ESPState),
        VMSTATE_END_OF_LIST()
    }
};

static const struct SCSIBusInfo esp_pci_scsi_info = {
    .tcq = false,
    .max_target = ESP_MAX_DEVS,
    .max_lun = 7,

    .transfer_data = esp_transfer_data,
    .complete = esp_command_complete,
    .cancel = esp_request_cancelled,
};

static void esp_pci_scsi_realize(PCIDevice *dev, Error **errp)
{
    PCIESPState *pci = PCI_ESP(dev);
    DeviceState *d = DEVICE(dev);
    ESPState *s = &pci->esp;
    uint8_t *pci_conf;

    if (!qdev_realize(DEVICE(s), NULL, errp)) {
        return;
    }

    pci_conf = dev->config;

    /* Interrupt pin A */
    pci_conf[PCI_INTERRUPT_PIN] = 0x01;

    s->dma_memory_read = esp_pci_dma_memory_read;
    s->dma_memory_write = esp_pci_dma_memory_write;
    s->dma_opaque = pci;
    s->chip_id = TCHI_AM53C974;
    memory_region_init_io(&pci->io, OBJECT(pci), &esp_pci_io_ops, pci,
                          "esp-io", 0x80);

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &pci->io);
    s->irq = qemu_allocate_irq(esp_irq_handler, pci, 0);

    scsi_bus_init(&s->bus, sizeof(s->bus), d, &esp_pci_scsi_info);
}

static void esp_pci_scsi_exit(PCIDevice *d)
{
    PCIESPState *pci = PCI_ESP(d);
    ESPState *s = &pci->esp;

    qemu_free_irq(s->irq);
}

static void esp_pci_init(Object *obj)
{
    PCIESPState *pci = PCI_ESP(obj);

    object_initialize_child(obj, "esp", &pci->esp, TYPE_ESP);
}

static void esp_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = esp_pci_scsi_realize;
    k->exit = esp_pci_scsi_exit;
    k->vendor_id = PCI_VENDOR_ID_AMD;
    k->device_id = PCI_DEVICE_ID_AMD_SCSI;
    k->revision = 0x10;
    k->class_id = PCI_CLASS_STORAGE_SCSI;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->desc = "AMD Am53c974 PCscsi-PCI SCSI adapter";
    device_class_set_legacy_reset(dc, esp_pci_hard_reset);
    dc->vmsd = &vmstate_esp_pci_scsi;
}

static const TypeInfo esp_pci_info = {
    .name = TYPE_AM53C974_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_init = esp_pci_init,
    .instance_size = sizeof(PCIESPState),
    .class_init = esp_pci_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

struct DC390State {
    PCIESPState pci;
    eeprom_t *eeprom;
};
typedef struct DC390State DC390State;

#define TYPE_DC390_DEVICE "dc390"
DECLARE_INSTANCE_CHECKER(DC390State, DC390,
                         TYPE_DC390_DEVICE)

#define EE_ADAPT_SCSI_ID 64
#define EE_MODE2         65
#define EE_DELAY         66
#define EE_TAG_CMD_NUM   67
#define EE_ADAPT_OPTIONS 68
#define EE_BOOT_SCSI_ID  69
#define EE_BOOT_SCSI_LUN 70
#define EE_CHKSUM1       126
#define EE_CHKSUM2       127

#define EE_ADAPT_OPTION_F6_F8_AT_BOOT   0x01
#define EE_ADAPT_OPTION_BOOT_FROM_CDROM 0x02
#define EE_ADAPT_OPTION_INT13           0x04
#define EE_ADAPT_OPTION_SCAM_SUPPORT    0x08


static uint32_t dc390_read_config(PCIDevice *dev, uint32_t addr, int l)
{
    DC390State *pci = DC390(dev);
    uint32_t val;

    val = pci_default_read_config(dev, addr, l);

    if (addr == 0x00 && l == 1) {
        /* First byte of address space is AND-ed with EEPROM DO line */
        if (!eeprom93xx_read(pci->eeprom)) {
            val &= ~0xff;
        }
    }

    return val;
}

static void dc390_write_config(PCIDevice *dev,
                               uint32_t addr, uint32_t val, int l)
{
    DC390State *pci = DC390(dev);
    if (addr == 0x80) {
        /* EEPROM write */
        int eesk = val & 0x80 ? 1 : 0;
        int eedi = val & 0x40 ? 1 : 0;
        eeprom93xx_write(pci->eeprom, 1, eesk, eedi);
    } else if (addr == 0xc0) {
        /* EEPROM CS low */
        eeprom93xx_write(pci->eeprom, 0, 0, 0);
    } else {
        pci_default_write_config(dev, addr, val, l);
    }
}

static void dc390_scsi_realize(PCIDevice *dev, Error **errp)
{
    DC390State *pci = DC390(dev);
    Error *err = NULL;
    uint8_t *contents;
    uint16_t chksum = 0;
    int i;

    /* init base class */
    esp_pci_scsi_realize(dev, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    /* EEPROM */
    pci->eeprom = eeprom93xx_new(DEVICE(dev), 64);

    /* set default eeprom values */
    contents = (uint8_t *)eeprom93xx_data(pci->eeprom);

    for (i = 0; i < 16; i++) {
        contents[i * 2] = 0x57;
        contents[i * 2 + 1] = 0x00;
    }
    contents[EE_ADAPT_SCSI_ID] = 7;
    contents[EE_MODE2] = 0x0f;
    contents[EE_TAG_CMD_NUM] = 0x04;
    contents[EE_ADAPT_OPTIONS] = EE_ADAPT_OPTION_F6_F8_AT_BOOT
                               | EE_ADAPT_OPTION_BOOT_FROM_CDROM
                               | EE_ADAPT_OPTION_INT13;

    /* update eeprom checksum */
    for (i = 0; i < EE_CHKSUM1; i += 2) {
        chksum += contents[i] + (((uint16_t)contents[i + 1]) << 8);
    }
    chksum = 0x1234 - chksum;
    contents[EE_CHKSUM1] = chksum & 0xff;
    contents[EE_CHKSUM2] = chksum >> 8;
}

static void dc390_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = dc390_scsi_realize;
    k->config_read = dc390_read_config;
    k->config_write = dc390_write_config;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->desc = "Tekram DC-390 SCSI adapter";
}

static const TypeInfo dc390_info = {
    .name = TYPE_DC390_DEVICE,
    .parent = TYPE_AM53C974_DEVICE,
    .instance_size = sizeof(DC390State),
    .class_init = dc390_class_init,
};

static void esp_pci_register_types(void)
{
    type_register_static(&esp_pci_info);
    type_register_static(&dc390_info);
}

type_init(esp_pci_register_types)
