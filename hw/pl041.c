/*
 * Arm PrimeCell PL041 Advanced Audio Codec Interface
 *
 * Copyright (c) 2011
 * Written by Mathieu Sonet - www.elasticsheep.com
 *
 * This code is licensed under the GPL.
 *
 * *****************************************************************
 *
 * This driver emulates the ARM AACI interface
 * connected to a LM4549 codec.
 *
 * Limitations:
 * - Supports only a playback on one channel (Versatile/Vexpress)
 * - Supports only one TX FIFO in compact-mode or non-compact mode.
 * - Supports playback of 12, 16, 18 and 20 bits samples.
 * - Record is not supported.
 * - The PL041 is hardwired to a LM4549 codec.
 *
 */

#include "sysbus.h"

#include "pl041.h"
#include "lm4549.h"

#if 0
#define PL041_DEBUG_LEVEL 1
#endif

#if defined(PL041_DEBUG_LEVEL) && (PL041_DEBUG_LEVEL >= 1)
#define DBG_L1(fmt, ...) \
do { printf("pl041: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DBG_L1(fmt, ...) \
do { } while (0)
#endif

#if defined(PL041_DEBUG_LEVEL) && (PL041_DEBUG_LEVEL >= 2)
#define DBG_L2(fmt, ...) \
do { printf("pl041: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DBG_L2(fmt, ...) \
do { } while (0)
#endif


#define MAX_FIFO_DEPTH      (1024)
#define DEFAULT_FIFO_DEPTH  (8)

#define SLOT1_RW    (1 << 19)

/* This FIFO only stores 20-bit samples on 32-bit words.
   So its level is independent of the selected mode */
typedef struct {
    uint32_t level;
    uint32_t data[MAX_FIFO_DEPTH];
} pl041_fifo;

typedef struct {
    pl041_fifo tx_fifo;
    uint8_t tx_enabled;
    uint8_t tx_compact_mode;
    uint8_t tx_sample_size;

    pl041_fifo rx_fifo;
    uint8_t rx_enabled;
    uint8_t rx_compact_mode;
    uint8_t rx_sample_size;
} pl041_channel;

typedef struct {
    SysBusDevice busdev;
    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t fifo_depth; /* FIFO depth in non-compact mode */

    pl041_regfile regs;
    pl041_channel fifo1;
    lm4549_state codec;
} pl041_state;


static const unsigned char pl041_default_id[8] = {
    0x41, 0x10, 0x04, 0x00, 0x0d, 0xf0, 0x05, 0xb1
};

#if defined(PL041_DEBUG_LEVEL)
#define REGISTER(name, offset) #name,
static const char *pl041_regs_name[] = {
    #include "pl041.hx"
};
#undef REGISTER
#endif


#if defined(PL041_DEBUG_LEVEL)
static const char *get_reg_name(target_phys_addr_t offset)
{
    if (offset <= PL041_dr1_7) {
        return pl041_regs_name[offset >> 2];
    }

    return "unknown";
}
#endif

static uint8_t pl041_compute_periphid3(pl041_state *s)
{
    uint8_t id3 = 1; /* One channel */

    /* Add the fifo depth information */
    switch (s->fifo_depth) {
    case 8:
        id3 |= 0 << 3;
        break;
    case 32:
        id3 |= 1 << 3;
        break;
    case 64:
        id3 |= 2 << 3;
        break;
    case 128:
        id3 |= 3 << 3;
        break;
    case 256:
        id3 |= 4 << 3;
        break;
    case 512:
        id3 |= 5 << 3;
        break;
    case 1024:
        id3 |= 6 << 3;
        break;
    case 2048:
        id3 |= 7 << 3;
        break;
    }

    return id3;
}

static void pl041_reset(pl041_state *s)
{
    DBG_L1("pl041_reset\n");

    memset(&s->regs, 0x00, sizeof(pl041_regfile));

    s->regs.slfr = SL1TXEMPTY | SL2TXEMPTY | SL12TXEMPTY;
    s->regs.sr1 = TXFE | RXFE | TXHE;
    s->regs.isr1 = 0;

    memset(&s->fifo1, 0x00, sizeof(s->fifo1));
}


static void pl041_fifo1_write(pl041_state *s, uint32_t value)
{
    pl041_channel *channel = &s->fifo1;
    pl041_fifo *fifo = &s->fifo1.tx_fifo;

    /* Push the value in the FIFO */
    if (channel->tx_compact_mode == 0) {
        /* Non-compact mode */

        if (fifo->level < s->fifo_depth) {
            /* Pad the value with 0 to obtain a 20-bit sample */
            switch (channel->tx_sample_size) {
            case 12:
                value = (value << 8) & 0xFFFFF;
                break;
            case 16:
                value = (value << 4) & 0xFFFFF;
                break;
            case 18:
                value = (value << 2) & 0xFFFFF;
                break;
            case 20:
            default:
                break;
            }

            /* Store the sample in the FIFO */
            fifo->data[fifo->level++] = value;
        }
#if defined(PL041_DEBUG_LEVEL)
        else {
            DBG_L1("fifo1 write: overrun\n");
        }
#endif
    } else {
        /* Compact mode */

        if ((fifo->level + 2) < s->fifo_depth) {
            uint32_t i = 0;
            uint32_t sample = 0;

            for (i = 0; i < 2; i++) {
                sample = value & 0xFFFF;
                value = value >> 16;

                /* Pad each sample with 0 to obtain a 20-bit sample */
                switch (channel->tx_sample_size) {
                case 12:
                    sample = sample << 8;
                    break;
                case 16:
                default:
                    sample = sample << 4;
                    break;
                }

                /* Store the sample in the FIFO */
                fifo->data[fifo->level++] = sample;
            }
        }
#if defined(PL041_DEBUG_LEVEL)
        else {
            DBG_L1("fifo1 write: overrun\n");
        }
#endif
    }

    /* Update the status register */
    if (fifo->level > 0) {
        s->regs.sr1 &= ~(TXUNDERRUN | TXFE);
    }

    if (fifo->level >= (s->fifo_depth / 2)) {
        s->regs.sr1 &= ~TXHE;
    }

    if (fifo->level >= s->fifo_depth) {
        s->regs.sr1 |= TXFF;
    }

    DBG_L2("fifo1_push sr1 = 0x%08x\n", s->regs.sr1);
}

static void pl041_fifo1_transmit(pl041_state *s)
{
    pl041_channel *channel = &s->fifo1;
    pl041_fifo *fifo = &s->fifo1.tx_fifo;
    uint32_t slots = s->regs.txcr1 & TXSLOT_MASK;
    uint32_t written_samples;

    /* Check if FIFO1 transmit is enabled */
    if ((channel->tx_enabled) && (slots & (TXSLOT3 | TXSLOT4))) {
        if (fifo->level >= (s->fifo_depth / 2)) {
            int i;

            DBG_L1("Transfer FIFO level = %i\n", fifo->level);

            /* Try to transfer the whole FIFO */
            for (i = 0; i < (fifo->level / 2); i++) {
                uint32_t left = fifo->data[i * 2];
                uint32_t right = fifo->data[i * 2 + 1];

                 /* Transmit two 20-bit samples to the codec */
                if (lm4549_write_samples(&s->codec, left, right) == 0) {
                    DBG_L1("Codec buffer full\n");
                    break;
                }
            }

            written_samples = i * 2;
            if (written_samples > 0) {
                /* Update the FIFO level */
                fifo->level -= written_samples;

                /* Move back the pending samples to the start of the FIFO */
                for (i = 0; i < fifo->level; i++) {
                    fifo->data[i] = fifo->data[written_samples + i];
                }

                /* Update the status register */
                s->regs.sr1 &= ~TXFF;

                if (fifo->level <= (s->fifo_depth / 2)) {
                    s->regs.sr1 |= TXHE;
                }

                if (fifo->level == 0) {
                    s->regs.sr1 |= TXFE | TXUNDERRUN;
                    DBG_L1("Empty FIFO\n");
                }
            }
        }
    }
}

static void pl041_isr1_update(pl041_state *s)
{
    /* Update ISR1 */
    if (s->regs.sr1 & TXUNDERRUN) {
        s->regs.isr1 |= URINTR;
    } else {
        s->regs.isr1 &= ~URINTR;
    }

    if (s->regs.sr1 & TXHE) {
        s->regs.isr1 |= TXINTR;
    } else {
        s->regs.isr1 &= ~TXINTR;
    }

    if (!(s->regs.sr1 & TXBUSY) && (s->regs.sr1 & TXFE)) {
        s->regs.isr1 |= TXCINTR;
    } else {
        s->regs.isr1 &= ~TXCINTR;
    }

    /* Update the irq state */
    qemu_set_irq(s->irq, ((s->regs.isr1 & s->regs.ie1) > 0) ? 1 : 0);
    DBG_L2("Set interrupt sr1 = 0x%08x isr1 = 0x%08x masked = 0x%08x\n",
           s->regs.sr1, s->regs.isr1, s->regs.isr1 & s->regs.ie1);
}

static void pl041_request_data(void *opaque)
{
    pl041_state *s = (pl041_state *)opaque;

    /* Trigger pending transfers */
    pl041_fifo1_transmit(s);
    pl041_isr1_update(s);
}

static uint64_t pl041_read(void *opaque, target_phys_addr_t offset,
                                unsigned size)
{
    pl041_state *s = (pl041_state *)opaque;
    int value;

    if ((offset >= PL041_periphid0) && (offset <= PL041_pcellid3)) {
        if (offset == PL041_periphid3) {
            value = pl041_compute_periphid3(s);
        } else {
            value = pl041_default_id[(offset - PL041_periphid0) >> 2];
        }

        DBG_L1("pl041_read [0x%08x] => 0x%08x\n", offset, value);
        return value;
    } else if (offset <= PL041_dr4_7) {
        value = *((uint32_t *)&s->regs + (offset >> 2));
    } else {
        DBG_L1("pl041_read: Reserved offset %x\n", (int)offset);
        return 0;
    }

    switch (offset) {
    case PL041_allints:
        value = s->regs.isr1 & 0x7F;
        break;
    }

    DBG_L1("pl041_read [0x%08x] %s => 0x%08x\n", offset,
           get_reg_name(offset), value);

    return value;
}

static void pl041_write(void *opaque, target_phys_addr_t offset,
                             uint64_t value, unsigned size)
{
    pl041_state *s = (pl041_state *)opaque;
    uint16_t control, data;
    uint32_t result;

    DBG_L1("pl041_write [0x%08x] %s <= 0x%08x\n", offset,
           get_reg_name(offset), (unsigned int)value);

    /* Write the register */
    if (offset <= PL041_dr4_7) {
        *((uint32_t *)&s->regs + (offset >> 2)) = value;
    } else {
        DBG_L1("pl041_write: Reserved offset %x\n", (int)offset);
        return;
    }

    /* Execute the actions */
    switch (offset) {
    case PL041_txcr1:
    {
        pl041_channel *channel = &s->fifo1;

        uint32_t txen = s->regs.txcr1 & TXEN;
        uint32_t tsize = (s->regs.txcr1 & TSIZE_MASK) >> TSIZE_MASK_BIT;
        uint32_t compact_mode = (s->regs.txcr1 & TXCOMPACT) ? 1 : 0;
#if defined(PL041_DEBUG_LEVEL)
        uint32_t slots = (s->regs.txcr1 & TXSLOT_MASK) >> TXSLOT_MASK_BIT;
        uint32_t txfen = (s->regs.txcr1 & TXFEN) > 0 ? 1 : 0;
#endif

        DBG_L1("=> txen = %i slots = 0x%01x tsize = %i compact = %i "
               "txfen = %i\n", txen, slots,  tsize, compact_mode, txfen);

        channel->tx_enabled = txen;
        channel->tx_compact_mode = compact_mode;

        switch (tsize) {
        case 0:
            channel->tx_sample_size = 16;
            break;
        case 1:
            channel->tx_sample_size = 18;
            break;
        case 2:
            channel->tx_sample_size = 20;
            break;
        case 3:
            channel->tx_sample_size = 12;
            break;
        }

        DBG_L1("TX enabled = %i\n", channel->tx_enabled);
        DBG_L1("TX compact mode = %i\n", channel->tx_compact_mode);
        DBG_L1("TX sample width = %i\n", channel->tx_sample_size);

        /* Check if compact mode is allowed with selected tsize */
        if (channel->tx_compact_mode == 1) {
            if ((channel->tx_sample_size == 18) ||
                (channel->tx_sample_size == 20)) {
                channel->tx_compact_mode = 0;
                DBG_L1("Compact mode not allowed with 18/20-bit sample size\n");
            }
        }

        break;
    }
    case PL041_sl1tx:
        s->regs.slfr &= ~SL1TXEMPTY;

        control = (s->regs.sl1tx >> 12) & 0x7F;
        data = (s->regs.sl2tx >> 4) & 0xFFFF;

        if ((s->regs.sl1tx & SLOT1_RW) == 0) {
            /* Write operation */
            lm4549_write(&s->codec, control, data);
        } else {
            /* Read operation */
            result = lm4549_read(&s->codec, control);

            /* Store the returned value */
            s->regs.sl1rx = s->regs.sl1tx & ~SLOT1_RW;
            s->regs.sl2rx = result << 4;

            s->regs.slfr &= ~(SL1RXBUSY | SL2RXBUSY);
            s->regs.slfr |= SL1RXVALID | SL2RXVALID;
        }
        break;

    case PL041_sl2tx:
        s->regs.sl2tx = value;
        s->regs.slfr &= ~SL2TXEMPTY;
        break;

    case PL041_intclr:
        DBG_L1("=> Clear interrupt intclr = 0x%08x isr1 = 0x%08x\n",
               s->regs.intclr, s->regs.isr1);

        if (s->regs.intclr & TXUEC1) {
            s->regs.sr1 &= ~TXUNDERRUN;
        }
        break;

    case PL041_maincr:
    {
#if defined(PL041_DEBUG_LEVEL)
        char debug[] = " AACIFE  SL1RXEN  SL1TXEN";
        if (!(value & AACIFE)) {
            debug[0] = '!';
        }
        if (!(value & SL1RXEN)) {
            debug[8] = '!';
        }
        if (!(value & SL1TXEN)) {
            debug[17] = '!';
        }
        DBG_L1("%s\n", debug);
#endif

        if ((s->regs.maincr & AACIFE) == 0) {
            pl041_reset(s);
        }
        break;
    }

    case PL041_dr1_0:
    case PL041_dr1_1:
    case PL041_dr1_2:
    case PL041_dr1_3:
        pl041_fifo1_write(s, value);
        break;
    }

    /* Transmit the FIFO content */
    pl041_fifo1_transmit(s);

    /* Update the ISR1 register */
    pl041_isr1_update(s);
}

static void pl041_device_reset(DeviceState *d)
{
    pl041_state *s = DO_UPCAST(pl041_state, busdev.qdev, d);

    pl041_reset(s);
}

static const MemoryRegionOps pl041_ops = {
    .read = pl041_read,
    .write = pl041_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int pl041_init(SysBusDevice *dev)
{
    pl041_state *s = FROM_SYSBUS(pl041_state, dev);

    DBG_L1("pl041_init 0x%08x\n", (uint32_t)s);

    /* Check the device properties */
    switch (s->fifo_depth) {
    case 8:
    case 32:
    case 64:
    case 128:
    case 256:
    case 512:
    case 1024:
    case 2048:
        break;
    case 16:
    default:
        /* NC FIFO depth of 16 is not allowed because its id bits in
           AACIPERIPHID3 overlap with the id for the default NC FIFO depth */
        fprintf(stderr, "pl041: unsupported non-compact fifo depth [%i]\n",
                s->fifo_depth);
        return -1;
    }

    /* Connect the device to the sysbus */
    memory_region_init_io(&s->iomem, &pl041_ops, s, "pl041", 0x1000);
    sysbus_init_mmio(dev, &s->iomem);
    sysbus_init_irq(dev, &s->irq);

    /* Init the codec */
    lm4549_init(&s->codec, &pl041_request_data, (void *)s);

    return 0;
}

static const VMStateDescription vmstate_pl041_regfile = {
    .name = "pl041_regfile",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
#define REGISTER(name, offset) VMSTATE_UINT32(name, pl041_regfile),
        #include "pl041.hx"
#undef REGISTER
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pl041_fifo = {
    .name = "pl041_fifo",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(level, pl041_fifo),
        VMSTATE_UINT32_ARRAY(data, pl041_fifo, MAX_FIFO_DEPTH),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pl041_channel = {
    .name = "pl041_channel",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_STRUCT(tx_fifo, pl041_channel, 0,
                       vmstate_pl041_fifo, pl041_fifo),
        VMSTATE_UINT8(tx_enabled, pl041_channel),
        VMSTATE_UINT8(tx_compact_mode, pl041_channel),
        VMSTATE_UINT8(tx_sample_size, pl041_channel),
        VMSTATE_STRUCT(rx_fifo, pl041_channel, 0,
                       vmstate_pl041_fifo, pl041_fifo),
        VMSTATE_UINT8(rx_enabled, pl041_channel),
        VMSTATE_UINT8(rx_compact_mode, pl041_channel),
        VMSTATE_UINT8(rx_sample_size, pl041_channel),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pl041 = {
    .name = "pl041",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(fifo_depth, pl041_state),
        VMSTATE_STRUCT(regs, pl041_state, 0,
                       vmstate_pl041_regfile, pl041_regfile),
        VMSTATE_STRUCT(fifo1, pl041_state, 0,
                       vmstate_pl041_channel, pl041_channel),
        VMSTATE_STRUCT(codec, pl041_state, 0,
                       vmstate_lm4549_state, lm4549_state),
        VMSTATE_END_OF_LIST()
    }
};

static Property pl041_device_properties[] = {
    /* Non-compact FIFO depth property */
    DEFINE_PROP_UINT32("nc_fifo_depth", pl041_state, fifo_depth, DEFAULT_FIFO_DEPTH),
    DEFINE_PROP_END_OF_LIST(),
};

static void pl041_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = pl041_init;
    dc->no_user = 1;
    dc->reset = pl041_device_reset;
    dc->vmsd = &vmstate_pl041;
    dc->props = pl041_device_properties;
}

static TypeInfo pl041_device_info = {
    .name          = "pl041",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(pl041_state),
    .class_init    = pl041_device_class_init,
};

static void pl041_register_types(void)
{
    type_register_static(&pl041_device_info);
}

type_init(pl041_register_types)
