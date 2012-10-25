/*
 * Samsung Exynos4210 SD/MMC host controller model
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 * Mitsyanko Igor <i.mitsyanko@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "sdhci.h"

#define EXYNOS4_SDHC_CAPABILITIES    0x05E80080
#define EXYNOS4_SDHC_MAX_BUFSZ       512

#define EXYNOS4_SDHC_DEBUG           0

#if EXYNOS4_SDHC_DEBUG == 0
    #define DPRINT_L1(fmt, args...)       do { } while (0)
    #define DPRINT_L2(fmt, args...)       do { } while (0)
    #define ERRPRINT(fmt, args...)        do { } while (0)
#elif EXYNOS4_SDHC_DEBUG == 1
    #define DPRINT_L1(fmt, args...)       \
        do {fprintf(stderr, "QEMU SDHC: "fmt, ## args); } while (0)
    #define DPRINT_L2(fmt, args...)       do { } while (0)
    #define ERRPRINT(fmt, args...)        \
        do {fprintf(stderr, "QEMU SDHC ERROR: "fmt, ## args); } while (0)
#else
    #define DPRINT_L1(fmt, args...)       \
        do {fprintf(stderr, "QEMU SDHC: "fmt, ## args); } while (0)
    #define DPRINT_L2(fmt, args...)       \
        do {fprintf(stderr, "QEMU SDHC: "fmt, ## args); } while (0)
    #define ERRPRINT(fmt, args...)        \
        do {fprintf(stderr, "QEMU SDHC ERROR: "fmt, ## args); } while (0)
#endif


#define TYPE_EXYNOS4_SDHC            "exynos4210.sdhci"
#define EXYNOS4_SDHCI(obj)           \
     OBJECT_CHECK(Exynos4SDHCIState, (obj), TYPE_EXYNOS4_SDHC)

/* ADMA Error Status Register */
#define EXYNOS4_SDHC_FINAL_BLOCK     (1 << 10)
#define EXYNOS4_SDHC_CONTINUE_REQ    (1 << 9)
#define EXYNOS4_SDHC_IRQ_STAT        (1 << 8)
/* Control register 2 */
#define EXYNOS4_SDHC_CONTROL2        0x80
#define EXYNOS4_SDHC_HWINITFIN       (1 << 0)
#define EXYNOS4_SDHC_DISBUFRD        (1 << 6)
#define EXYNOS4_SDHC_SDOPSIGPC       (1 << 12)
#define EXYNOS4_SDHC_SDINPSIGPC      (1 << 3)
/* Control register 3 */
#define EXYNOS4_SDHC_CONTROL3        0x84
/* Control register 4 */
#define EXYNOS4_SDHC_CONTROL4        0x8C
/* Clock control register */
#define EXYNOS4_SDHC_SDCLK_STBL      (1 << 3)

#define EXYNOS4_SDHC_CMD_USES_DAT(cmd)  \
    (((cmd) & SDHC_CMD_DATA_PRESENT) || \
    ((cmd) & SDHC_CMD_RESPONSE) == SDHC_CMD_RSP_WITH_BUSY)

typedef struct Exynos4SDHCIState {
    SDHCIState sdhci;

    uint32_t admaerr;
    uint32_t control2;
    uint32_t control3;
    bool stopped_adma;
} Exynos4SDHCIState;

static uint8_t sdhci_slotint(SDHCIState *s)
{
    return (s->norintsts & s->norintsigen) || (s->errintsts & s->errintsigen) ||
         ((s->norintsts & SDHC_NIS_INSERT) && (s->wakcon & SDHC_WKUP_ON_INS)) ||
         ((s->norintsts & SDHC_NIS_REMOVE) && (s->wakcon & SDHC_WKUP_ON_RMV));
}

static inline void exynos4210_sdhci_update_irq(SDHCIState *s)
{
    qemu_set_irq(s->irq, sdhci_slotint(s));
}

static void exynos4210_sdhci_reset(DeviceState *d)
{
    Exynos4SDHCIState *s = EXYNOS4_SDHCI(d);

    SDHCI_GET_CLASS(d)->reset(SDHCI(d));
    s->stopped_adma = false;
    s->admaerr = 0;
    s->control2 = 0;
    s->control3 = 0x7F5F3F1F;
}

static void exynos4210_sdhci_start_adma(SDHCIState *sdhci)
{
    Exynos4SDHCIState *s = EXYNOS4_SDHCI(sdhci);
    unsigned int length, n, begin;
    hwaddr entry_addr;
    uint32_t addr;
    uint8_t attributes;
    const uint16_t block_size = sdhci->blksize & 0x0fff;
    s->admaerr &=
            ~(EXYNOS4_SDHC_FINAL_BLOCK | SDHC_ADMAERR_LENGTH_MISMATCH);

    while (1) {
        addr = length = attributes = 0;
        entry_addr = (hwaddr)(sdhci->admasysaddr & 0xFFFFFFFFull);

        /* fetch next entry from descriptor table */
        cpu_physical_memory_read(entry_addr + 4, (uint8_t *)(&addr), 4);
        cpu_physical_memory_read(entry_addr + 2, (uint8_t *)(&length), 2);
        cpu_physical_memory_read(entry_addr, (uint8_t *)(&attributes), 1);
        DPRINT_L1("ADMA loop: addr=0x%08x, len=%d, attr=%x\n",
                addr, length, attributes);

        if ((attributes & SDHC_ADMA_ATTR_VALID) == 0) {
            /* Indicate that error occurred in ST_FDS state */
            s->admaerr &= ~SDHC_ADMAERR_STATE_MASK;
            s->admaerr |= SDHC_ADMAERR_STATE_ST_FDS;
            DPRINT_L1("ADMA not valid at addr=0x%lx\n", sdhci->admasysaddr);

            if (sdhci->errintstsen & SDHC_EISEN_ADMAERR) {
                sdhci->errintsts |= SDHC_EIS_ADMAERR;
                sdhci->norintsts |= SDHC_NIS_ERR;
            }

            exynos4210_sdhci_update_irq(sdhci);
            break;
        }

        if (length == 0) {
            length = 65536;
        }

        addr &= 0xfffffffc;  /* minimum unit of addr is 4 byte */

        switch (attributes & SDHC_ADMA_ATTR_ACT_MASK) {
        case SDHC_ADMA_ATTR_ACT_TRAN:  /* data transfer */
            if (sdhci->trnmod & SDHC_TRNS_READ) {
                while (length) {
                    if (sdhci->data_count == 0) {
                        for (n = 0; n < block_size; n++) {
                            sdhci->fifo_buffer[n] = sd_read_data(sdhci->card);
                        }
                    }
                    begin = sdhci->data_count;
                    if ((length + begin) < block_size) {
                        sdhci->data_count = length + begin;
                        length = 0;
                     } else {
                        sdhci->data_count = block_size;
                        length -= block_size - begin;
                    }
                    cpu_physical_memory_write(addr, &sdhci->fifo_buffer[begin],
                            sdhci->data_count - begin);
                    addr += sdhci->data_count - begin;
                    if (sdhci->data_count == block_size) {
                        sdhci->data_count = 0;
                        if (sdhci->trnmod & SDHC_TRNS_BLK_CNT_EN) {
                            sdhci->blkcnt--;
                            if (sdhci->blkcnt == 0) {
                                break;
                            }
                        }
                    }
                }
            } else {
                while (length) {
                    begin = sdhci->data_count;
                    if ((length + begin) < block_size) {
                        sdhci->data_count = length + begin;
                        length = 0;
                     } else {
                        sdhci->data_count = block_size;
                        length -= block_size - begin;
                    }
                    cpu_physical_memory_read(addr,
                            &sdhci->fifo_buffer[begin], sdhci->data_count);
                    addr += sdhci->data_count - begin;
                    if (sdhci->data_count == block_size) {
                        for (n = 0; n < block_size; n++) {
                            sd_write_data(sdhci->card, sdhci->fifo_buffer[n]);
                        }
                        sdhci->data_count = 0;
                        if (sdhci->trnmod & SDHC_TRNS_BLK_CNT_EN) {
                            sdhci->blkcnt--;
                            if (sdhci->blkcnt == 0) {
                                break;
                            }
                        }
                    }
                }
            }
            sdhci->admasysaddr += 8;
            break;
        case SDHC_ADMA_ATTR_ACT_LINK:   /* link to next descriptor table */
            sdhci->admasysaddr = addr;
            DPRINT_L1("ADMA link: admasysaddr=0x%lx\n", sdhci->admasysaddr);
            break;
        default:
            sdhci->admasysaddr += 8;
            break;
        }

        /* ADMA transfer terminates if blkcnt == 0 or by END attribute */
        if (((sdhci->trnmod & SDHC_TRNS_BLK_CNT_EN) && (sdhci->blkcnt == 0)) ||
                (attributes & SDHC_ADMA_ATTR_END)) {
            DPRINT_L2("ADMA transfer completed\n");
            if (length || ((attributes & SDHC_ADMA_ATTR_END) &&
               (sdhci->trnmod & SDHC_TRNS_BLK_CNT_EN) && sdhci->blkcnt != 0) ||
               ((sdhci->trnmod & SDHC_TRNS_BLK_CNT_EN) && sdhci->blkcnt == 0 &&
               (attributes & SDHC_ADMA_ATTR_END) == 0)) {
                ERRPRINT("ADMA length mismatch\n");
                s->admaerr |= SDHC_ADMAERR_LENGTH_MISMATCH |
                        SDHC_ADMAERR_STATE_ST_TFR;
                if (sdhci->errintstsen & SDHC_EISEN_ADMAERR) {
                    sdhci->errintsts |= SDHC_EIS_ADMAERR;
                    sdhci->norintsts |= SDHC_NIS_ERR;
                }

                exynos4210_sdhci_update_irq(sdhci);
            }

            s->admaerr |= EXYNOS4_SDHC_FINAL_BLOCK;
            SDHCI_GET_CLASS(sdhci)->end_data_transfer(sdhci);
            break;
        }

        if (attributes & SDHC_ADMA_ATTR_INT) {
            DPRINT_L1("ADMA interrupt: addr=0x%lx\n", sdhci->admasysaddr);
            s->admaerr |= EXYNOS4_SDHC_IRQ_STAT;
            s->stopped_adma = true;
            if (sdhci->norintstsen & SDHC_NISEN_DMA) {
                sdhci->norintsts |= SDHC_NIS_DMA;
            }
            exynos4210_sdhci_update_irq(sdhci);
            break;
        }
    }
}

static bool exynos4210_sdhci_can_issue_command(SDHCIState *sdhci)
{
    Exynos4SDHCIState *s = EXYNOS4_SDHCI(sdhci);

    /* Check that power is supplied and clock is enabled.
     * If SDOPSIGPC and SDINPSIGPC bits in CONTROL2 register are not set, power
     * is supplied regardless of the PWRCON register state */
    if (!SDHC_CLOCK_IS_ON(sdhci->clkcon) || (!(sdhci->pwrcon & SDHC_POWER_ON) &&
        (s->control2 & (EXYNOS4_SDHC_SDOPSIGPC | EXYNOS4_SDHC_SDINPSIGPC)))) {
        return false;
    }

    /* Controller cannot issue a command which uses data line (unless its an
     * ABORT command) if data line is currently busy */
    if (((sdhci->prnsts & SDHC_DATA_INHIBIT) || sdhci->stopped_state) &&
        (EXYNOS4_SDHC_CMD_USES_DAT(sdhci->cmdreg) &&
        SDHC_COMMAND_TYPE(sdhci->cmdreg) != SDHC_CMD_ABORT)) {
        return false;
    }

    return true;
}

static uint64_t
exynos4210_sdhci_readfn(void *opaque, hwaddr offset, unsigned size)
{
    Exynos4SDHCIState *s = (Exynos4SDHCIState *)opaque;
    uint32_t ret;

    switch (offset & ~0x3) {
    case SDHC_BDATA:
        /* Buffer data port read can be disabled by CONTROL2 register */
        if (s->control2 & EXYNOS4_SDHC_DISBUFRD) {
            ret = 0;
        } else {
            ret = SDHCI_GET_CLASS(s)->mem_read(SDHCI(s), offset, size);
        }
        break;
    case SDHC_ADMAERR:
        ret = (s->admaerr >> 8 * (offset - SDHC_ADMAERR)) &
                ((1 << 8 * size) - 1);
        break;
    case EXYNOS4_SDHC_CONTROL2:
        ret = (s->control2 >> 8 * (offset - EXYNOS4_SDHC_CONTROL2)) &
                ((1 << 8 * size) - 1);
        break;
    case EXYNOS4_SDHC_CONTROL3:
        ret = (s->control3 >> 8 * (offset - EXYNOS4_SDHC_CONTROL3)) &
                ((1 << 8 * size) - 1);
        break;
    case EXYNOS4_SDHC_CONTROL4:
        ret = 0;
        break;
    default:
        ret = SDHCI_GET_CLASS(s)->mem_read(SDHCI(s), offset, size);
        break;
    }

    DPRINT_L2("read %ub: addr[0x%04x] -> %u(0x%x)\n", size, offset, ret, ret);
    return ret;
}

static void exynos4210_sdhci_writefn(void *opaque, hwaddr offset,
        uint64_t val, unsigned size)
{
    Exynos4SDHCIState *s = (Exynos4SDHCIState *)opaque;
    SDHCIState *sdhci = SDHCI(s);
    unsigned shift;

    DPRINT_L2("write %ub: addr[0x%04x] <- %u(0x%x)\n", size, (uint32_t)offset,
            (uint32_t)val, (uint32_t)val);

    switch (offset) {
    case SDHC_CLKCON:
        if ((val & SDHC_CLOCK_SDCLK_EN) &&
                (sdhci->prnsts & SDHC_CARD_PRESENT)) {
            val |= EXYNOS4_SDHC_SDCLK_STBL;
        } else {
            val &= ~EXYNOS4_SDHC_SDCLK_STBL;
        }
        /* Break out to superclass write to handle the rest of this register */
        break;
    case EXYNOS4_SDHC_CONTROL2 ... EXYNOS4_SDHC_CONTROL2 + 3:
        shift = (offset - EXYNOS4_SDHC_CONTROL2) * 8;
        s->control2 = (s->control2 & ~(((1 << 8 * size) - 1) << shift)) |
                (val << shift);
        return;
    case EXYNOS4_SDHC_CONTROL3 ... EXYNOS4_SDHC_CONTROL3 + 3:
        shift = (offset - EXYNOS4_SDHC_CONTROL2) * 8;
        s->control3 = (s->control3 & ~(((1 << 8 * size) - 1) << shift)) |
                (val << shift);
        return;
    case SDHC_ADMAERR ... SDHC_ADMAERR + 3:
        if (size == 4 || (size == 2 && offset == SDHC_ADMAERR) ||
                (size == 1 && offset == (SDHC_ADMAERR + 1))) {
            uint32_t mask = 0;

            if (size == 2) {
                mask = 0xFFFF0000;
            } else if (size == 1) {
                mask = 0xFFFF00FF;
                val <<= 8;
            }

            s->admaerr = (s->admaerr & (mask | EXYNOS4_SDHC_FINAL_BLOCK |
               EXYNOS4_SDHC_IRQ_STAT)) | (val & ~(EXYNOS4_SDHC_FINAL_BLOCK |
               EXYNOS4_SDHC_IRQ_STAT | EXYNOS4_SDHC_CONTINUE_REQ));
            s->admaerr &= ~(val & EXYNOS4_SDHC_IRQ_STAT);
            if ((s->stopped_adma) && (val & EXYNOS4_SDHC_CONTINUE_REQ) &&
                (SDHC_DMA_TYPE(sdhci->hostctl) == SDHC_CTRL_ADMA2_32)) {
                s->stopped_adma = false;
                SDHCI_GET_CLASS(sdhci)->do_adma(sdhci);
            }
        } else {
            uint32_t mask = (1 << (size * 8)) - 1;
            shift = 8 * (offset & 0x3);
            val <<= shift;
            mask = ~(mask << shift);
            s->admaerr = (s->admaerr & mask) | val;
        }
        return;
    }

    SDHCI_GET_CLASS(s)->mem_write(sdhci, offset, val, size);
}

static const MemoryRegionOps exynos4210_sdhci_mmio_ops = {
    .read = exynos4210_sdhci_readfn,
    .write = exynos4210_sdhci_writefn,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const VMStateDescription exynos4210_sdhci_vmstate = {
    .name = "exynos4210.sdhci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(sdhci, Exynos4SDHCIState, 1, sdhci_vmstate, SDHCIState),
        VMSTATE_UINT32(admaerr, Exynos4SDHCIState),
        VMSTATE_UINT32(control2, Exynos4SDHCIState),
        VMSTATE_UINT32(control3, Exynos4SDHCIState),
        VMSTATE_BOOL(stopped_adma, Exynos4SDHCIState),
        VMSTATE_END_OF_LIST()
    }
};

static int exynos4210_sdhci_realize(SysBusDevice *busdev)
{
    SDHCIState *sdhci = SDHCI(busdev);

    qdev_prop_set_uint32(DEVICE(busdev), "capareg", EXYNOS4_SDHC_CAPABILITIES);
    sdhci->buf_maxsz = EXYNOS4_SDHC_MAX_BUFSZ;
    sdhci->fifo_buffer = g_malloc0(sdhci->buf_maxsz);
    sysbus_init_irq(busdev, &sdhci->irq);
    memory_region_init_io(&sdhci->iomem, &exynos4210_sdhci_mmio_ops,
            EXYNOS4_SDHCI(sdhci), "exynos4210.sdhci", SDHC_REGISTERS_MAP_SIZE);
    sysbus_init_mmio(busdev, &sdhci->iomem);
    return 0;
}

static void exynos4210_sdhci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *sbdc = SYS_BUS_DEVICE_CLASS(klass);
    SDHCIClass *k = SDHCI_CLASS(klass);

    dc->vmsd = &exynos4210_sdhci_vmstate;
    dc->reset = exynos4210_sdhci_reset;
    sbdc->init = exynos4210_sdhci_realize;

    k->can_issue_command = exynos4210_sdhci_can_issue_command;
    k->do_adma = exynos4210_sdhci_start_adma;
}

static const TypeInfo exynos4210_sdhci_type_info = {
    .name = TYPE_EXYNOS4_SDHC,
    .parent = TYPE_SDHCI,
    .instance_size = sizeof(Exynos4SDHCIState),
    .class_init = exynos4210_sdhci_class_init,
};

static void exynos4210_sdhci_register_types(void)
{
    type_register_static(&exynos4210_sdhci_type_info);
}

type_init(exynos4210_sdhci_register_types)
