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

#include "hw/pci/pci.h"
#include "hw/nvram/eeprom93xx.h"
#include "hw/scsi/esp.h"
#include "trace.h"
#include "qemu/log.h"

#define TYPE_AM53C974_DEVICE "am53c974"

#define PCI_ESP(obj) \
    OBJECT_CHECK(PCIESPState, (obj), TYPE_AM53C974_DEVICE)

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

#define SBAC_STATUS 0x1000

typedef struct PCIESPState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    MemoryRegion io;
    uint32_t dma_regs[8];
    uint32_t sbac;
    ESPState esp;
} PCIESPState;

static void esp_pci_handle_idle(PCIESPState *pci, uint32_t val)
{
    trace_esp_pci_dma_idle(val);
    esp_dma_enable(&pci->esp, 0, 0);
}

static void esp_pci_handle_blast(PCIESPState *pci, uint32_t val)
{
    trace_esp_pci_dma_blast(val);
    qemu_log_mask(LOG_UNIMP, "am53c974: cmd BLAST not implemented\n");
}

static void esp_pci_handle_abort(PCIESPState *pci, uint32_t val)
{
    trace_esp_pci_dma_abort(val);
    if (pci->esp.current_req) {
        scsi_req_cancel(pci->esp.current_req);
    }
}

static void esp_pci_handle_start(PCIESPState *pci, uint32_t val)
{
    trace_esp_pci_dma_start(val);

    pci->dma_regs[DMA_WBC] = pci->dma_regs[DMA_STC];
    pci->dma_regs[DMA_WAC] = pci->dma_regs[DMA_SPA];
    pci->dma_regs[DMA_WMAC] = pci->dma_regs[DMA_SMDLA];

    pci->dma_regs[DMA_STAT] &= ~(DMA_STAT_BCMBLT | DMA_STAT_SCSIINT
                               | DMA_STAT_DONE | DMA_STAT_ABORT
                               | DMA_STAT_ERROR | DMA_STAT_PWDN);

    esp_dma_enable(&pci->esp, 0, 1);
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
        if (!(pci->sbac & SBAC_STATUS)) {
            /* clear some bits on write */
            uint32_t mask = DMA_STAT_ERROR | DMA_STAT_ABORT | DMA_STAT_DONE;
            pci->dma_regs[DMA_STAT] &= ~(val & mask);
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
        if (pci->esp.rregs[ESP_RSTAT] & STAT_INT) {
            val |= DMA_STAT_SCSIINT;
        }
        if (pci->sbac & SBAC_STATUS) {
            pci->dma_regs[DMA_STAT] &= ~(DMA_STAT_ERROR | DMA_STAT_ABORT |
                                         DMA_STAT_DONE);
        }
    }

    trace_esp_pci_dma_read(saddr, val);
    return val;
}

static void esp_pci_io_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned int size)
{
    PCIESPState *pci = opaque;

    if (size < 4 || addr & 3) {
        /* need to upgrade request: we only support 4-bytes accesses */
        uint32_t current = 0, mask;
        int shift;

        if (addr < 0x40) {
            current = pci->esp.wregs[addr >> 2];
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

    if (addr < 0x40) {
        /* SCSI core reg */
        esp_reg_write(&pci->esp, addr >> 2, val);
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
    uint32_t ret;

    if (addr < 0x40) {
        /* SCSI core reg */
        ret = esp_reg_read(&pci->esp, addr >> 2);
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

    addr = pci->dma_regs[DMA_SPA];
    if (pci->dma_regs[DMA_WBC] < len) {
        len = pci->dma_regs[DMA_WBC];
    }

    pci_dma_rw(PCI_DEVICE(pci), addr, buf, len, dir);

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
    esp_hard_reset(&pci->esp);
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
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIESPState),
        VMSTATE_BUFFER_UNSAFE(dma_regs, PCIESPState, 0, 8 * sizeof(uint32_t)),
        VMSTATE_STRUCT(esp, PCIESPState, 0, vmstate_esp, ESPState),
        VMSTATE_END_OF_LIST()
    }
};

static void esp_pci_command_complete(SCSIRequest *req, uint32_t status,
                                     size_t resid)
{
    ESPState *s = req->hba_private;
    PCIESPState *pci = container_of(s, PCIESPState, esp);

    esp_command_complete(req, status, resid);
    pci->dma_regs[DMA_WBC] = 0;
    pci->dma_regs[DMA_STAT] |= DMA_STAT_DONE;
}

static const struct SCSIBusInfo esp_pci_scsi_info = {
    .tcq = false,
    .max_target = ESP_MAX_DEVS,
    .max_lun = 7,

    .transfer_data = esp_transfer_data,
    .complete = esp_pci_command_complete,
    .cancel = esp_request_cancelled,
};

static int esp_pci_scsi_init(PCIDevice *dev)
{
    PCIESPState *pci = PCI_ESP(dev);
    DeviceState *d = DEVICE(dev);
    ESPState *s = &pci->esp;
    uint8_t *pci_conf;
    Error *err = NULL;

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
    s->irq = pci_allocate_irq(dev);

    scsi_bus_new(&s->bus, sizeof(s->bus), d, &esp_pci_scsi_info, NULL);
    if (!d->hotplugged) {
        scsi_bus_legacy_handle_cmdline(&s->bus, &err);
        if (err != NULL) {
            error_free(err);
            return -1;
        }
    }
    return 0;
}

static void esp_pci_scsi_uninit(PCIDevice *d)
{
    PCIESPState *pci = PCI_ESP(d);

    qemu_free_irq(pci->esp.irq);
}

static void esp_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = esp_pci_scsi_init;
    k->exit = esp_pci_scsi_uninit;
    k->vendor_id = PCI_VENDOR_ID_AMD;
    k->device_id = PCI_DEVICE_ID_AMD_SCSI;
    k->revision = 0x10;
    k->class_id = PCI_CLASS_STORAGE_SCSI;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->desc = "AMD Am53c974 PCscsi-PCI SCSI adapter";
    dc->reset = esp_pci_hard_reset;
    dc->vmsd = &vmstate_esp_pci_scsi;
}

static const TypeInfo esp_pci_info = {
    .name = TYPE_AM53C974_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIESPState),
    .class_init = esp_pci_class_init,
};

typedef struct {
    PCIESPState pci;
    eeprom_t *eeprom;
} DC390State;

#define TYPE_DC390_DEVICE "dc390"
#define DC390(obj) \
    OBJECT_CHECK(DC390State, obj, TYPE_DC390_DEVICE)

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

static int dc390_scsi_init(PCIDevice *dev)
{
    DC390State *pci = DC390(dev);
    uint8_t *contents;
    uint16_t chksum = 0;
    int i, ret;

    /* init base class */
    ret = esp_pci_scsi_init(dev);
    if (ret < 0) {
        return ret;
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

    return 0;
}

static void dc390_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = dc390_scsi_init;
    k->config_read = dc390_read_config;
    k->config_write = dc390_write_config;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->desc = "Tekram DC-390 SCSI adapter";
}

static const TypeInfo dc390_info = {
    .name = "dc390",
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
