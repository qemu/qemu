/*
 * Ingenic SFC (SPI Flash Controller) emulation
 *
 * Copyright (c) 2024 OpenSensor Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This implements the Ingenic SFC V2 controller found in T41 SoC.
 * The SFC provides access to SPI NOR flash via memory-mapped registers.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/block/flash.h"
#include "system/block-backend.h"
#include "exec/cpu-common.h"
#include "qom/object.h"

/* SFC Register offsets */
#define SFC_GLB0            0x0000
#define SFC_DEV_CONF        0x0004
#define SFC_DEV_STA_EXP     0x0008
#define SFC_DEV0_STA_RT     0x000c
#define SFC_DEV_STA_MSK     0x0010
#define SFC_TRAN_CONF0(n)   (0x0014 + (n * 4))
#define SFC_TRAN_LEN        0x002c
#define SFC_DEV_ADDR(n)     (0x0030 + (n * 4))
#define SFC_DEV_ADDR_PLUS(n) (0x0048 + (n * 4))
#define SFC_MEM_ADDR        0x0060
#define SFC_TRIG            0x0064
#define SFC_SR              0x0068
#define SFC_SCR             0x006c
#define SFC_INTC            0x0070
#define SFC_FSM             0x0074
#define SFC_CGE             0x0078
#define SFC_CMD_IDX         0x007c
#define SFC_COL_ADDR        0x0080
#define SFC_ROW_ADDR        0x0084
#define SFC_STA_ADDR0       0x0088
#define SFC_STA_ADDR1       0x008c
#define SFC_DES_ADDR        0x0090
#define SFC_GLB1            0x0094
#define SFC_DEV1_STA_RT     0x0098
#define SFC_TRAN_CONF1(n)   (0x009c + (n * 4))
#define SFC_CDT             0x0800  /* CDT table 0x800 ~ 0xbff */
#define SFC_RM_DR           0x1000  /* Read mode data register */

/* SFC_GLB0 bits */
#define GLB0_DES_EN         (1 << 15)
#define GLB0_CDT_EN         (1 << 14)
#define GLB0_TRAN_DIR       (1 << 13)
#define GLB0_OP_MODE        (1 << 6)

/* SFC_TRIG bits */
#define TRIG_FLUSH          (1 << 2)
#define TRIG_STOP           (1 << 1)
#define TRIG_START          (1 << 0)

/* SFC_SR bits */
#define SFC_WORKING         (1 << 7)
#define SFC_BUSY            (0x3 << 5)
#define SFC_END             (1 << 4)
#define SFC_TREQ            (1 << 3)
#define SFC_RREQ            (1 << 2)
#define SFC_OVER            (1 << 1)
#define SFC_UNDER           (1 << 0)

/* SFC_SCR bits (clear) */
#define CLR_END             (1 << 4)
#define CLR_TREQ            (1 << 3)
#define CLR_RREQ            (1 << 2)
#define CLR_OVER            (1 << 1)
#define CLR_UNDER           (1 << 0)

/* Standard SPI Flash commands */
#define CMD_READ_ID         0x9f
#define CMD_READ_STATUS     0x05
#define CMD_READ_DATA       0x03
#define CMD_FAST_READ       0x0b
#define CMD_PAGE_PROGRAM    0x02
#define CMD_SECTOR_ERASE    0x20
#define CMD_BLOCK_ERASE     0xd8
#define CMD_CHIP_ERASE      0xc7
#define CMD_WRITE_ENABLE    0x06
#define CMD_WRITE_DISABLE   0x04

#define TYPE_INGENIC_SFC "ingenic-sfc"
OBJECT_DECLARE_SIMPLE_TYPE(IngenicSFCState, INGENIC_SFC)

#define SFC_REG_SIZE        0x2000
#define SFC_CDT_SIZE        0x400   /* CDT table size */
#define SFC_FLASH_SIZE      (16 * 1024 * 1024)  /* 16MB default */
#define SFC_FIFO_SIZE       64      /* FIFO size in bytes */

struct IngenicSFCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    /* Registers */
    uint32_t glb0;
    uint32_t dev_conf;
    uint32_t dev_sta_exp;
    uint32_t dev_sta_rt;
    uint32_t dev_sta_msk;
    uint32_t tran_conf0[6];
    uint32_t tran_conf1[6];
    uint32_t tran_len;
    uint32_t dev_addr[6];
    uint32_t dev_addr_plus[6];
    uint32_t mem_addr;
    uint32_t trig;
    uint32_t sr;
    uint32_t scr;
    uint32_t intc;
    uint32_t fsm;
    uint32_t cge;
    uint32_t cmd_idx;
    uint32_t col_addr;
    uint32_t row_addr;
    uint32_t sta_addr0;
    uint32_t sta_addr1;
    uint32_t des_addr;
    uint32_t glb1;
    uint32_t dev1_sta_rt;

    /* CDT (Command Descriptor Table) */
    uint8_t cdt[SFC_CDT_SIZE];

    /* Flash storage */
    BlockBackend *blk;
    uint8_t *flash_data;
    uint32_t flash_size;

    /* Flash state */
    uint8_t flash_status;
    bool write_enabled;

    /* FIFO for CPU mode transfers */
    uint8_t fifo[SFC_FIFO_SIZE];
    uint32_t fifo_pos;
    uint32_t fifo_len;
};

static void ingenic_sfc_update_irq(IngenicSFCState *s)
{
    /* INTC is a mask register - bit=0 means interrupt enabled
     * SR contains status bits, INTC masks them
     * Interrupt fires when any unmasked status bit is set
     */
    uint32_t pending = s->sr & ~s->intc & 0x1f;
    int level = pending ? 1 : 0;
    qemu_set_irq(s->irq, level);
}

static uint64_t ingenic_sfc_read(void *opaque, hwaddr offset, unsigned size)
{
    IngenicSFCState *s = INGENIC_SFC(opaque);
    uint64_t val = 0;

    /* Handle CDT table reads */
    if (offset >= SFC_CDT && offset < SFC_CDT + SFC_CDT_SIZE) {
        uint32_t cdt_off = offset - SFC_CDT;
        if (cdt_off + size <= SFC_CDT_SIZE) {
            memcpy(&val, &s->cdt[cdt_off], size);
        }
        return val;
    }

    /* Handle data register reads (for CPU mode FIFO) */
    if (offset >= SFC_RM_DR && offset < SFC_RM_DR + 0x100) {
        /* Read from FIFO - return 32-bit words */
        val = 0;
        if (s->fifo_pos < s->fifo_len) {
            uint32_t bytes_left = s->fifo_len - s->fifo_pos;
            uint32_t to_read = bytes_left < 4 ? bytes_left : 4;
            memcpy(&val, &s->fifo[s->fifo_pos], to_read);
            s->fifo_pos += 4;

            /* Check if FIFO is empty */
            if (s->fifo_pos >= s->fifo_len) {
                /* All data read - clear RREQ, keep END set */
                s->sr &= ~SFC_RREQ;
                ingenic_sfc_update_irq(s);
            }
        }
        return val;
    }

    switch (offset) {
    case SFC_GLB0:
        val = s->glb0;
        break;
    case SFC_DEV_CONF:
        val = s->dev_conf;
        break;
    case SFC_DEV_STA_EXP:
        val = s->dev_sta_exp;
        break;
    case SFC_DEV0_STA_RT:
        /* Return flash status - flash ready (not busy) */
        val = 0;
        break;
    case SFC_DEV_STA_MSK:
        val = s->dev_sta_msk;
        break;
    case SFC_TRAN_LEN:
        val = s->tran_len;
        break;
    case SFC_MEM_ADDR:
        val = s->mem_addr;
        break;
    case SFC_TRIG:
        val = s->trig;
        break;
    case SFC_SR:
        /* Return status - transfer complete, not busy */
        val = s->sr | SFC_END;
        val &= ~(SFC_WORKING | SFC_BUSY);
        break;
    case SFC_SCR:
        val = s->scr;
        break;
    case SFC_INTC:
        val = s->intc;
        break;
    case SFC_FSM:
        val = s->fsm;
        break;
    case SFC_CGE:
        val = s->cge;
        break;
    case SFC_CMD_IDX:
        val = s->cmd_idx;
        break;
    case SFC_COL_ADDR:
        val = s->col_addr;
        break;
    case SFC_ROW_ADDR:
        val = s->row_addr;
        break;
    case SFC_STA_ADDR0:
        val = s->sta_addr0;
        break;
    case SFC_STA_ADDR1:
        val = s->sta_addr1;
        break;
    case SFC_DES_ADDR:
        val = s->des_addr;
        break;
    case SFC_GLB1:
        val = s->glb1;
        break;
    case SFC_DEV1_STA_RT:
        val = s->dev1_sta_rt;
        break;
    default:
        /* Handle TRAN_CONF0/1 and DEV_ADDR arrays */
        if (offset >= 0x0014 && offset < 0x002c) {
            int idx = (offset - 0x0014) / 4;
            if (idx < 6) val = s->tran_conf0[idx];
        } else if (offset >= 0x0030 && offset < 0x0048) {
            int idx = (offset - 0x0030) / 4;
            if (idx < 6) val = s->dev_addr[idx];
        } else if (offset >= 0x0048 && offset < 0x0060) {
            int idx = (offset - 0x0048) / 4;
            if (idx < 6) val = s->dev_addr_plus[idx];
        } else if (offset >= 0x009c && offset < 0x00b4) {
            int idx = (offset - 0x009c) / 4;
            if (idx < 6) val = s->tran_conf1[idx];
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ingenic_sfc: read from unknown offset 0x%"HWADDR_PRIx"\n",
                          offset);
        }
        break;
    }

    return val;
}

/*
 * DMA descriptor structure (matches Linux driver's struct sfc_desc):
 *   uint32_t next_des_addr;  // offset 0
 *   uint32_t mem_addr;       // offset 4
 *   uint32_t tran_len;       // offset 8
 *   uint32_t link;           // offset 12
 */
#define DESC_NEXT_ADDR_OFF  0
#define DESC_MEM_ADDR_OFF   4
#define DESC_TRAN_LEN_OFF   8
#define DESC_LINK_OFF       12
#define DESC_SIZE           16

static void ingenic_sfc_do_transfer(IngenicSFCState *s)
{
    /* Execute a flash transfer based on current register settings.
     * The driver uses CDT (Command Descriptor Table) mode.
     * We need to:
     * 1. Look up the command from the CDT
     * 2. Execute the flash command
     * 3. Write results to memory (for DMA mode) or FIFO (for CPU mode)
     * 4. Set completion status and generate interrupt
     */
    uint32_t cmd_idx = s->cmd_idx & 0x3f;
    uint32_t dataen = (s->cmd_idx >> 31) & 1;
    uint32_t datadir = (s->cmd_idx >> 30) & 1;
    uint32_t mem_addr = s->mem_addr;
    uint32_t tran_len = s->tran_len;
    uint32_t cdt_offset = cmd_idx * 16;  /* Each CDT entry is 16 bytes */
    uint32_t cdt_xfer;
    uint8_t cmd;
    bool cpu_mode = !(s->glb0 & GLB0_OP_MODE);  /* 0 = CPU mode, 1 = DMA mode */

    /* Read the CDT entry to get the command */
    if (cdt_offset + 8 <= SFC_CDT_SIZE) {
        memcpy(&cdt_xfer, &s->cdt[cdt_offset + 4], 4);
        cmd = cdt_xfer & 0xff;
    } else {
        cmd = 0;
    }

    /* For DMA mode, read the descriptor to get the actual memory address */
    if (!cpu_mode && s->des_addr != 0) {
        uint32_t desc[4];
        /* Convert KSEG0/KSEG1 address to physical address */
        uint32_t phys_des_addr = s->des_addr & 0x1FFFFFFF;
        cpu_physical_memory_read(phys_des_addr, desc, DESC_SIZE);
        /* mem_addr in descriptor is also a KSEG address, convert to physical */
        mem_addr = desc[1] & 0x1FFFFFFF;
    }

    /* Reset FIFO for CPU mode */
    s->fifo_pos = 0;
    s->fifo_len = 0;

    /* Handle flash commands */
    if (dataen && !datadir && tran_len > 0) {
        /* Read operation */
        if (cmd == CMD_READ_ID || cmd == 0x9f) {
            /* Read JEDEC ID - return Winbond W25Q128 ID */
            /* Manufacturer: 0xEF (Winbond), Device: 0x4018 (W25Q128) */
            uint8_t id_data[3] = {0xEF, 0x40, 0x18};
            uint32_t id_len = tran_len < 3 ? tran_len : 3;
            if (cpu_mode) {
                memcpy(s->fifo, id_data, id_len);
                s->fifo_len = id_len;
                s->fifo_pos = 0;
                s->sr |= SFC_RREQ;
            } else if (mem_addr) {
                cpu_physical_memory_write(mem_addr, id_data, id_len);
            }
        } else if (cmd == CMD_READ_STATUS || cmd == 0x05) {
            /* Read status register - return ready (not busy) */
            uint8_t status = s->flash_status;
            if (cpu_mode) {
                s->fifo[0] = status;
                s->fifo_len = 1;
                s->fifo_pos = 0;
                s->sr |= SFC_RREQ;
            } else if (mem_addr) {
                cpu_physical_memory_write(mem_addr, &status, 1);
            }
        } else if (cmd == CMD_READ_DATA || cmd == 0x03 ||
                   cmd == CMD_FAST_READ || cmd == 0x0b) {
            /* Read data from flash - support large DMA transfers */
            uint32_t flash_addr = s->row_addr;

            if (cpu_mode) {
                /* CPU mode - limited to FIFO size */
                uint32_t read_len = tran_len < SFC_FIFO_SIZE ? tran_len : SFC_FIFO_SIZE;
                if (flash_addr + read_len <= s->flash_size && s->flash_data) {
                    memcpy(s->fifo, &s->flash_data[flash_addr], read_len);
                } else {
                    memset(s->fifo, 0xff, read_len);
                }
                s->fifo_len = read_len;
                s->fifo_pos = 0;
                s->sr |= SFC_RREQ;
            } else if (mem_addr) {
                /* DMA mode - transfer full length directly to memory */
                if (flash_addr + tran_len <= s->flash_size && s->flash_data) {
                    cpu_physical_memory_write(mem_addr, &s->flash_data[flash_addr],
                                              tran_len);
                } else if (flash_addr < s->flash_size && s->flash_data) {
                    /* Partial read - read what we can, fill rest with 0xff */
                    uint32_t avail = s->flash_size - flash_addr;
                    cpu_physical_memory_write(mem_addr, &s->flash_data[flash_addr],
                                              avail);
                    uint8_t *fill = g_malloc(tran_len - avail);
                    memset(fill, 0xff, tran_len - avail);
                    cpu_physical_memory_write(mem_addr + avail, fill,
                                              tran_len - avail);
                    g_free(fill);
                } else {
                    /* Address out of range - return 0xff */
                    uint8_t *fill = g_malloc(tran_len);
                    memset(fill, 0xff, tran_len);
                    cpu_physical_memory_write(mem_addr, fill, tran_len);
                    g_free(fill);
                }
            }
        } else {
            /* Unknown read command - return 0xff */
            if (cpu_mode) {
                uint32_t read_len = tran_len < SFC_FIFO_SIZE ? tran_len : SFC_FIFO_SIZE;
                memset(s->fifo, 0xff, read_len);
                s->fifo_len = read_len;
                s->fifo_pos = 0;
                s->sr |= SFC_RREQ;
            } else if (mem_addr) {
                uint8_t *fill = g_malloc(tran_len);
                memset(fill, 0xff, tran_len);
                cpu_physical_memory_write(mem_addr, fill, tran_len);
                g_free(fill);
            }
        }
    } else if (dataen && datadir && tran_len > 0) {
        /* Write operation - read data from system memory */
        if (cmd == CMD_PAGE_PROGRAM || cmd == 0x02) {
            uint32_t flash_addr = s->row_addr;
            if (s->write_enabled && flash_addr + tran_len <= s->flash_size &&
                s->flash_data && !cpu_mode && mem_addr) {
                cpu_physical_memory_read(mem_addr, &s->flash_data[flash_addr],
                                         tran_len);
            }
        }
    } else {
        /* Command-only operations */
        if (cmd == CMD_WRITE_ENABLE || cmd == 0x06) {
            s->write_enabled = true;
            s->flash_status |= 0x02;  /* WEL bit */
        } else if (cmd == CMD_WRITE_DISABLE || cmd == 0x04) {
            s->write_enabled = false;
            s->flash_status &= ~0x02;
        }
    }

    /* Mark transfer complete */
    s->sr |= SFC_END;
    s->sr &= ~(SFC_WORKING | SFC_BUSY);
    s->trig &= ~TRIG_START;

    /* Generate interrupt if enabled */
    ingenic_sfc_update_irq(s);
}

static void ingenic_sfc_write(void *opaque, hwaddr offset,
                               uint64_t val, unsigned size)
{
    IngenicSFCState *s = INGENIC_SFC(opaque);

    /* Handle CDT table writes */
    if (offset >= SFC_CDT && offset < SFC_CDT + SFC_CDT_SIZE) {
        uint32_t cdt_off = offset - SFC_CDT;
        if (cdt_off + size <= SFC_CDT_SIZE) {
            memcpy(&s->cdt[cdt_off], &val, size);
        }
        return;
    }

    switch (offset) {
    case SFC_GLB0:
        s->glb0 = val;
        break;
    case SFC_DEV_CONF:
        s->dev_conf = val;
        break;
    case SFC_DEV_STA_EXP:
        s->dev_sta_exp = val;
        break;
    case SFC_DEV_STA_MSK:
        s->dev_sta_msk = val;
        break;
    case SFC_TRAN_LEN:
        s->tran_len = val;
        break;
    case SFC_MEM_ADDR:
        s->mem_addr = val;
        break;
    case SFC_TRIG:
        if (val & TRIG_STOP) {
            s->sr &= ~(SFC_WORKING | SFC_BUSY);
            s->trig &= ~TRIG_START;
        }
        if (val & TRIG_FLUSH) {
            /* Flush FIFO - nothing to do in emulation */
        }
        if (val & TRIG_START) {
            s->trig |= TRIG_START;
            s->sr |= SFC_WORKING;
            ingenic_sfc_do_transfer(s);
        }
        break;
    case SFC_SCR:
        /* Clear status bits */
        if (val & CLR_END) s->sr &= ~SFC_END;
        if (val & CLR_TREQ) s->sr &= ~SFC_TREQ;
        if (val & CLR_RREQ) s->sr &= ~SFC_RREQ;
        if (val & CLR_OVER) s->sr &= ~SFC_OVER;
        if (val & CLR_UNDER) s->sr &= ~SFC_UNDER;
        ingenic_sfc_update_irq(s);
        break;
    case SFC_INTC:
        s->intc = val;
        ingenic_sfc_update_irq(s);
        break;
    case SFC_CGE:
        s->cge = val;
        break;
    case SFC_CMD_IDX:
        s->cmd_idx = val;
        break;
    case SFC_COL_ADDR:
        s->col_addr = val;
        break;
    case SFC_ROW_ADDR:
        s->row_addr = val;
        break;
    case SFC_STA_ADDR0:
        s->sta_addr0 = val;
        break;
    case SFC_STA_ADDR1:
        s->sta_addr1 = val;
        break;
    case SFC_DES_ADDR:
        s->des_addr = val;
        break;
    case SFC_GLB1:
        s->glb1 = val;
        break;
    default:
        /* Handle TRAN_CONF0/1 and DEV_ADDR arrays */
        if (offset >= 0x0014 && offset < 0x002c) {
            int idx = (offset - 0x0014) / 4;
            if (idx < 6) s->tran_conf0[idx] = val;
        } else if (offset >= 0x0030 && offset < 0x0048) {
            int idx = (offset - 0x0030) / 4;
            if (idx < 6) s->dev_addr[idx] = val;
        } else if (offset >= 0x0048 && offset < 0x0060) {
            int idx = (offset - 0x0048) / 4;
            if (idx < 6) s->dev_addr_plus[idx] = val;
        } else if (offset >= 0x009c && offset < 0x00b4) {
            int idx = (offset - 0x009c) / 4;
            if (idx < 6) s->tran_conf1[idx] = val;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ingenic_sfc: write to unknown offset 0x%"HWADDR_PRIx"\n",
                          offset);
        }
        break;
    }
}

static const MemoryRegionOps ingenic_sfc_ops = {
    .read = ingenic_sfc_read,
    .write = ingenic_sfc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void ingenic_sfc_reset(DeviceState *dev)
{
    IngenicSFCState *s = INGENIC_SFC(dev);

    s->glb0 = 0;
    s->dev_conf = 0;
    s->dev_sta_exp = 0;
    s->dev_sta_rt = 0;
    s->dev_sta_msk = 0;
    s->tran_len = 0;
    s->mem_addr = 0;
    s->trig = 0;
    s->sr = 0;  /* Start with no status bits set */
    s->scr = 0;
    s->intc = 0x1f;  /* Start with all interrupts masked */
    s->fsm = 0;
    s->cge = 0;
    s->cmd_idx = 0;
    s->col_addr = 0;
    s->row_addr = 0;
    s->sta_addr0 = 0;
    s->sta_addr1 = 0;
    s->des_addr = 0;
    s->glb1 = 0;
    s->dev1_sta_rt = 0;

    memset(s->tran_conf0, 0, sizeof(s->tran_conf0));
    memset(s->tran_conf1, 0, sizeof(s->tran_conf1));
    memset(s->dev_addr, 0, sizeof(s->dev_addr));
    memset(s->dev_addr_plus, 0, sizeof(s->dev_addr_plus));
    memset(s->cdt, 0, sizeof(s->cdt));

    s->flash_status = 0;
    s->write_enabled = false;

    /* Reset FIFO */
    memset(s->fifo, 0, sizeof(s->fifo));
    s->fifo_pos = 0;
    s->fifo_len = 0;
}

static void ingenic_sfc_realize(DeviceState *dev, Error **errp)
{
    IngenicSFCState *s = INGENIC_SFC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &ingenic_sfc_ops, s,
                          "ingenic-sfc", SFC_REG_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    /* Allocate flash storage */
    s->flash_size = SFC_FLASH_SIZE;
    s->flash_data = g_malloc0(s->flash_size);

    /* Initialize flash to 0xFF (erased state) */
    memset(s->flash_data, 0xFF, s->flash_size);

    /* If a block backend is provided, load the flash image */
    if (s->blk) {
        int ret = blk_pread(s->blk, 0, s->flash_size, s->flash_data, 0);
        if (ret < 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ingenic_sfc: failed to read flash image\n");
        }
    }
}

static const Property ingenic_sfc_properties[] = {
    DEFINE_PROP_DRIVE("drive", IngenicSFCState, blk),
};

static void ingenic_sfc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ingenic_sfc_realize;
    device_class_set_legacy_reset(dc, ingenic_sfc_reset);
    device_class_set_props(dc, ingenic_sfc_properties);
}

static const TypeInfo ingenic_sfc_info = {
    .name          = TYPE_INGENIC_SFC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IngenicSFCState),
    .class_init    = ingenic_sfc_class_init,
};

static void ingenic_sfc_register_types(void)
{
    type_register_static(&ingenic_sfc_info);
}

type_init(ingenic_sfc_register_types)

