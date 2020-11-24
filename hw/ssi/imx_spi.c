/*
 * IMX SPI Controller
 *
 * Copyright (c) 2016 Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/ssi/imx_spi.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include <math.h>

#ifndef DEBUG_IMX_SPI
#define DEBUG_IMX_SPI 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_IMX_SPI) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_IMX_SPI, \
                                             __func__, ##args); \
        } \
    } while (0)

static const char *imx_spi_reg_name(uint32_t reg)
{
    static char unknown[20];

    switch (reg) {
    case ECSPI_RXDATA:
        return  "ECSPI_RXDATA";
    case ECSPI_TXDATA:
        return  "ECSPI_TXDATA";
    case ECSPI_CONREG:
        return  "ECSPI_CONREG";
    case ECSPI_CONFIGREG:
        return  "ECSPI_CONFIGREG";
    case ECSPI_INTREG:
        return  "ECSPI_INTREG";
    case ECSPI_DMAREG:
        return  "ECSPI_DMAREG";
    case ECSPI_STATREG:
        return  "ECSPI_STATREG";
    case ECSPI_PERIODREG:
        return  "ECSPI_PERIODREG";
    case ECSPI_TESTREG:
        return  "ECSPI_TESTREG";
    case ECSPI_MSGDATA:
        return  "ECSPI_MSGDATA";
    default:
        sprintf(unknown, "%u ?", reg);
        return unknown;
    }
}

static const VMStateDescription vmstate_imx_spi = {
    .name = TYPE_IMX_SPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_FIFO32(tx_fifo, IMXSPIState),
        VMSTATE_FIFO32(rx_fifo, IMXSPIState),
        VMSTATE_INT16(burst_length, IMXSPIState),
        VMSTATE_UINT32_ARRAY(regs, IMXSPIState, ECSPI_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static void imx_spi_txfifo_reset(IMXSPIState *s)
{
    fifo32_reset(&s->tx_fifo);
    s->regs[ECSPI_STATREG] |= ECSPI_STATREG_TE;
    s->regs[ECSPI_STATREG] &= ~ECSPI_STATREG_TF;
}

static void imx_spi_rxfifo_reset(IMXSPIState *s)
{
    fifo32_reset(&s->rx_fifo);
    s->regs[ECSPI_STATREG] &= ~ECSPI_STATREG_RR;
    s->regs[ECSPI_STATREG] &= ~ECSPI_STATREG_RF;
    s->regs[ECSPI_STATREG] &= ~ECSPI_STATREG_RO;
}

static void imx_spi_update_irq(IMXSPIState *s)
{
    int level;

    if (fifo32_is_empty(&s->rx_fifo)) {
        s->regs[ECSPI_STATREG] &= ~ECSPI_STATREG_RR;
    } else {
        s->regs[ECSPI_STATREG] |= ECSPI_STATREG_RR;
    }

    if (fifo32_is_full(&s->rx_fifo)) {
        s->regs[ECSPI_STATREG] |= ECSPI_STATREG_RF;
    } else {
        s->regs[ECSPI_STATREG] &= ~ECSPI_STATREG_RF;
    }

    if (fifo32_is_empty(&s->tx_fifo)) {
        s->regs[ECSPI_STATREG] |= ECSPI_STATREG_TE;
    } else {
        s->regs[ECSPI_STATREG] &= ~ECSPI_STATREG_TE;
    }

    if (fifo32_is_full(&s->tx_fifo)) {
        s->regs[ECSPI_STATREG] |= ECSPI_STATREG_TF;
    } else {
        s->regs[ECSPI_STATREG] &= ~ECSPI_STATREG_TF;
    }

    level = s->regs[ECSPI_STATREG] & s->regs[ECSPI_INTREG] ? 1 : 0;

    qemu_set_irq(s->irq, level);

    DPRINTF("IRQ level is %d\n", level);
}

static uint8_t imx_spi_selected_channel(IMXSPIState *s)
{
    return EXTRACT(s->regs[ECSPI_CONREG], ECSPI_CONREG_CHANNEL_SELECT);
}

static uint32_t imx_spi_burst_length(IMXSPIState *s)
{
    return EXTRACT(s->regs[ECSPI_CONREG], ECSPI_CONREG_BURST_LENGTH) + 1;
}

static bool imx_spi_is_enabled(IMXSPIState *s)
{
    return s->regs[ECSPI_CONREG] & ECSPI_CONREG_EN;
}

static bool imx_spi_channel_is_master(IMXSPIState *s)
{
    uint8_t mode = EXTRACT(s->regs[ECSPI_CONREG], ECSPI_CONREG_CHANNEL_MODE);

    return (mode & (1 << imx_spi_selected_channel(s))) ? true : false;
}

static bool imx_spi_is_multiple_master_burst(IMXSPIState *s)
{
    uint8_t wave = EXTRACT(s->regs[ECSPI_CONFIGREG], ECSPI_CONFIGREG_SS_CTL);

    return imx_spi_channel_is_master(s) &&
           !(s->regs[ECSPI_CONREG] & ECSPI_CONREG_SMC) &&
           ((wave & (1 << imx_spi_selected_channel(s))) ? true : false);
}

static void imx_spi_select(IMXSPIState *s)
{
    // ToDo: IRQ call should be based on the configuration
    //       as either active high or low.
    qemu_set_irq(s->cs_lines[imx_spi_selected_channel(s)], 0);
}

static void imx_spi_deselect(IMXSPIState *s)
{
    // ToDo: IRQ call should be based on the configuration
    //       as either active high or low.
    qemu_set_irq(s->cs_lines[imx_spi_selected_channel(s)], 1);
}

static void imx_spi_update_fifo_count(IMXSPIState *s)
{
    s->regs[ECSPI_TESTREG] = ((fifo32_num_used(&s->rx_fifo) << ECSPI_TESTREG_RXCNT_SHIFT) & ECSPI_TESTREG_RXCNT_MASK) +
        (fifo32_num_used(&s->tx_fifo) & ECSPI_TESTREG_TXCNT_MASK);
}

static void imx_spi_flush_txfifo(IMXSPIState *s)
{ 
    uint32_t tx;
    uint32_t rx;

    DPRINTF("Begin: TX Fifo Size = %d, RX Fifo Size = %d\n",
            fifo32_num_used(&s->tx_fifo), fifo32_num_used(&s->rx_fifo));

    while (!fifo32_is_empty(&s->tx_fifo)) {
        int tx_burst = 0;

        if (s->burst_length <= 0) {
            s->burst_length = imx_spi_burst_length(s);

            DPRINTF("Burst length = %d\n", s->burst_length);

            //Add sanity check to avoid hanging in this loop
            if (s->burst_length <= 0) {
                assert(0);
                break;
            }

            //Assert the cs line
            imx_spi_select(s);

            if (imx_spi_is_multiple_master_burst(s)) {
                s->regs[ECSPI_CONREG] |= ECSPI_CONREG_XCH;
            }
        }

        tx = fifo32_pop(&s->tx_fifo);

        DPRINTF("data tx:0x%08x\n", tx);

        tx_burst = MIN(s->burst_length, 32);

        rx = 0;

        // This change is done to allow sending multiple bytes if the burst length is more than a byte upto 4 bytes at a time.
        if (tx_burst > 0) {
            uint32_t transfer_value = tx & ((uint32_t) (pow(2, tx_burst) - 1));

            DPRINTF("writing 0x%02x\n", transfer_value);

            /* Write bits equal to burst length upto a maximum of 32 bits at a time */
            transfer_value = ssi_transfer(s->bus, transfer_value);

            DPRINTF("0x%02x read\n", transfer_value);

            rx = transfer_value;

            /* Remove tx_burst bits from the actual burst */
            s->burst_length -= tx_burst;
        }

        DPRINTF("data rx:0x%08x\n", rx);

        if (fifo32_is_full(&s->rx_fifo)) {
            s->regs[ECSPI_STATREG] |= ECSPI_STATREG_RO;
        } else {
            fifo32_push(&s->rx_fifo, rx);
        }

        if (s->burst_length <= 0) {
            // Deassert the cs line
            imx_spi_deselect(s);

            if (!imx_spi_is_multiple_master_burst(s)) {
                s->regs[ECSPI_STATREG] |= ECSPI_STATREG_TC;
                break;
            }
        }
    }

    if (fifo32_is_empty(&s->tx_fifo)) {
        s->regs[ECSPI_STATREG] |= ECSPI_STATREG_TC;
        s->regs[ECSPI_CONREG] &= ~ECSPI_CONREG_XCH;
    }

    /* TODO: We should also use TDR and RDR bits */

    DPRINTF("End: TX Fifo Size = %d, RX Fifo Size = %d\n",
            fifo32_num_used(&s->tx_fifo), fifo32_num_used(&s->rx_fifo));

    // 1. Update FIFO content count
    // 2. Move update of the irq at appropriate locations
    imx_spi_update_fifo_count(s);
    imx_spi_update_irq(s);
}

static void imx_spi_reset(DeviceState *dev)
{
    IMXSPIState *s = IMX_SPI(dev);

    DPRINTF("\n");

    memset(s->regs, 0, sizeof(s->regs));

    s->regs[ECSPI_STATREG] = 0x00000003;

    imx_spi_rxfifo_reset(s);
    imx_spi_txfifo_reset(s);

    imx_spi_update_irq(s);

    s->burst_length = 0;
}

static uint64_t imx_spi_read(void *opaque, hwaddr offset, unsigned size)
{
    uint32_t value = 0;
    IMXSPIState *s = opaque;
    uint32_t index = offset >> 2;

    if (index >=  ECSPI_MAX) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_SPI, __func__, offset);
        return 0;
    }

    switch (index) {
    case ECSPI_RXDATA:
        if (!imx_spi_is_enabled(s)) {
            value = 0;
        } else if (fifo32_is_empty(&s->rx_fifo)) {
            /* value is undefined */
            value = 0xdeadbeef;
        } else {
            /* read from the RX FIFO */
            value = fifo32_pop(&s->rx_fifo);
        }

        // 1. Update FIFO content count
        // 2. Move update of the irq at appropriate locations
        imx_spi_update_fifo_count(s);
        imx_spi_update_irq(s);

        break;
    case ECSPI_TXDATA:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Trying to read from TX FIFO\n",
                      TYPE_IMX_SPI, __func__);

        /* Reading from TXDATA gives 0 */

        break;
    case ECSPI_MSGDATA:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Trying to read from MSG FIFO\n",
                      TYPE_IMX_SPI, __func__);

        /* Reading from MSGDATA gives 0 */

        break;
    default:
        value = s->regs[index];
        break;
    }

    DPRINTF("reg[%s] => 0x%" PRIx32 "\n", imx_spi_reg_name(index), value);

    // Move update of the irq at appropriate locations

    return (uint64_t)value;
}

static void imx_spi_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    IMXSPIState *s = opaque;
    uint32_t index = offset >> 2;
    uint32_t change_mask;

    if (index >=  ECSPI_MAX) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_SPI, __func__, offset);
        return;
    }

    DPRINTF("reg[%s] <= 0x%" PRIx32 "\n", imx_spi_reg_name(index),
            (uint32_t)value);

    change_mask = s->regs[index] ^ value;

    switch (index) {
    case ECSPI_RXDATA:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Trying to write to RX FIFO\n",
                      TYPE_IMX_SPI, __func__);
        break;
    case ECSPI_TXDATA:
        if (!imx_spi_is_enabled(s)) {
            /* Ignore writes if device is disabled */
            break;
        } else if (fifo32_is_full(&s->tx_fifo)) {
            /* Ignore writes if queue is full */
            break;
        }

        fifo32_push(&s->tx_fifo, (uint32_t)value);

        // 1. Update FIFO content count
        // 2. Move update of the irq at appropriate locations
        imx_spi_update_fifo_count(s);
        if (fifo32_is_full(&s->tx_fifo)) {
            imx_spi_update_irq(s);
        }

        if (imx_spi_channel_is_master(s) &&
            (s->regs[ECSPI_CONREG] & ECSPI_CONREG_SMC)) {
            /*
             * Start emitting if current channel is master and SMC bit is
             * set.
             */
            imx_spi_flush_txfifo(s);
        }

        break;
    case ECSPI_STATREG:
        /* the RO and TC bits are write-one-to-clear */
        value &= ECSPI_STATREG_RO | ECSPI_STATREG_TC;
        s->regs[ECSPI_STATREG] &= ~value;

        break;
    case ECSPI_CONREG:
        s->regs[ECSPI_CONREG] = value;

        if (!imx_spi_is_enabled(s)) {
            /* device is disabled, so this is a reset */
            imx_spi_reset(DEVICE(s));
            return;
        }

        if (imx_spi_channel_is_master(s)) {
            /* We are in master mode */

            // Toggle CS only when transmitting inside imx_spi_flush_txfifo()

            if ((value & change_mask & ECSPI_CONREG_SMC) &&
                !fifo32_is_empty(&s->tx_fifo)) {
                /* SMC bit is set and TX FIFO has some slots filled in */
                imx_spi_flush_txfifo(s);
            } else if ((value & change_mask & ECSPI_CONREG_XCH) &&
                !(value & ECSPI_CONREG_SMC)) {
                /* This is a request to start emitting */
                imx_spi_flush_txfifo(s);
            }
        }

        break;
    case ECSPI_MSGDATA:
        /* it is not clear from the spec what MSGDATA is for */
        /* Anyway it is not used by Linux driver */
        /* So for now we just ignore it */
        qemu_log_mask(LOG_UNIMP,
                      "[%s]%s: Trying to write to MSGDATA, ignoring\n",
                      TYPE_IMX_SPI, __func__);
        break;
    default:
        s->regs[index] = value;

        break;
    }

    // Move update of the irq at appropriate locations
}

static const struct MemoryRegionOps imx_spi_ops = {
    .read = imx_spi_read,
    .write = imx_spi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        /*
         * Our device would not work correctly if the guest was doing
         * unaligned access. This might not be a limitation on the real
         * device but in practice there is no reason for a guest to access
         * this device unaligned.
         */
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void imx_spi_realize(DeviceState *dev, Error **errp)
{
    IMXSPIState *s = IMX_SPI(dev);
    int i;

    s->bus = ssi_create_bus(dev, "spi");

    memory_region_init_io(&s->iomem, OBJECT(dev), &imx_spi_ops, s,
                          TYPE_IMX_SPI, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    for (i = 0; i < 4; ++i) {
        sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->cs_lines[i]);
    }

    s->burst_length = 0;

    fifo32_create(&s->tx_fifo, ECSPI_FIFO_SIZE);
    fifo32_create(&s->rx_fifo, ECSPI_FIFO_SIZE);
}

static void imx_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = imx_spi_realize;
    dc->vmsd = &vmstate_imx_spi;
    dc->reset = imx_spi_reset;
    dc->desc = "i.MX SPI Controller";
}

static const TypeInfo imx_spi_info = {
    .name          = TYPE_IMX_SPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXSPIState),
    .class_init    = imx_spi_class_init,
};

static void imx_spi_register_types(void)
{
    type_register_static(&imx_spi_info);
}

type_init(imx_spi_register_types)
