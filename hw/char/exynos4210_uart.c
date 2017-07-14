/*
 *  Exynos4210 UART Emulation
 *
 *  Copyright (C) 2011 Samsung Electronics Co Ltd.
 *    Maksim Kozlov, <m.kozlov@samsung.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "sysemu/sysemu.h"
#include "chardev/char-fe.h"
#include "chardev/char-serial.h"

#include "hw/arm/exynos4210.h"

#undef DEBUG_UART
#undef DEBUG_UART_EXTEND
#undef DEBUG_IRQ
#undef DEBUG_Rx_DATA
#undef DEBUG_Tx_DATA

#define DEBUG_UART            0
#define DEBUG_UART_EXTEND     0
#define DEBUG_IRQ             0
#define DEBUG_Rx_DATA         0
#define DEBUG_Tx_DATA         0

#if DEBUG_UART
#define  PRINT_DEBUG(fmt, args...)  \
        do { \
            fprintf(stderr, "  [%s:%d]   "fmt, __func__, __LINE__, ##args); \
        } while (0)

#if DEBUG_UART_EXTEND
#define  PRINT_DEBUG_EXTEND(fmt, args...) \
        do { \
            fprintf(stderr, "  [%s:%d]   "fmt, __func__, __LINE__, ##args); \
        } while (0)
#else
#define  PRINT_DEBUG_EXTEND(fmt, args...) \
        do {} while (0)
#endif /* EXTEND */

#else
#define  PRINT_DEBUG(fmt, args...)  \
        do {} while (0)
#define  PRINT_DEBUG_EXTEND(fmt, args...) \
        do {} while (0)
#endif

#define  PRINT_ERROR(fmt, args...) \
        do { \
            fprintf(stderr, "  [%s:%d]   "fmt, __func__, __LINE__, ##args); \
        } while (0)

/*
 *  Offsets for UART registers relative to SFR base address
 *  for UARTn
 *
 */
#define ULCON      0x0000 /* Line Control             */
#define UCON       0x0004 /* Control                  */
#define UFCON      0x0008 /* FIFO Control             */
#define UMCON      0x000C /* Modem Control            */
#define UTRSTAT    0x0010 /* Tx/Rx Status             */
#define UERSTAT    0x0014 /* UART Error Status        */
#define UFSTAT     0x0018 /* FIFO Status              */
#define UMSTAT     0x001C /* Modem Status             */
#define UTXH       0x0020 /* Transmit Buffer          */
#define URXH       0x0024 /* Receive Buffer           */
#define UBRDIV     0x0028 /* Baud Rate Divisor        */
#define UFRACVAL   0x002C /* Divisor Fractional Value */
#define UINTP      0x0030 /* Interrupt Pending        */
#define UINTSP     0x0034 /* Interrupt Source Pending */
#define UINTM      0x0038 /* Interrupt Mask           */

/*
 * for indexing register in the uint32_t array
 *
 * 'reg' - register offset (see offsets definitions above)
 *
 */
#define I_(reg) (reg / sizeof(uint32_t))

typedef struct Exynos4210UartReg {
    const char         *name; /* the only reason is the debug output */
    hwaddr  offset;
    uint32_t            reset_value;
} Exynos4210UartReg;

static const Exynos4210UartReg exynos4210_uart_regs[] = {
    {"ULCON",    ULCON,    0x00000000},
    {"UCON",     UCON,     0x00003000},
    {"UFCON",    UFCON,    0x00000000},
    {"UMCON",    UMCON,    0x00000000},
    {"UTRSTAT",  UTRSTAT,  0x00000006}, /* RO */
    {"UERSTAT",  UERSTAT,  0x00000000}, /* RO */
    {"UFSTAT",   UFSTAT,   0x00000000}, /* RO */
    {"UMSTAT",   UMSTAT,   0x00000000}, /* RO */
    {"UTXH",     UTXH,     0x5c5c5c5c}, /* WO, undefined reset value*/
    {"URXH",     URXH,     0x00000000}, /* RO */
    {"UBRDIV",   UBRDIV,   0x00000000},
    {"UFRACVAL", UFRACVAL, 0x00000000},
    {"UINTP",    UINTP,    0x00000000},
    {"UINTSP",   UINTSP,   0x00000000},
    {"UINTM",    UINTM,    0x00000000},
};

#define EXYNOS4210_UART_REGS_MEM_SIZE    0x3C

/* UART FIFO Control */
#define UFCON_FIFO_ENABLE                    0x1
#define UFCON_Rx_FIFO_RESET                  0x2
#define UFCON_Tx_FIFO_RESET                  0x4
#define UFCON_Tx_FIFO_TRIGGER_LEVEL_SHIFT    8
#define UFCON_Tx_FIFO_TRIGGER_LEVEL (7 << UFCON_Tx_FIFO_TRIGGER_LEVEL_SHIFT)
#define UFCON_Rx_FIFO_TRIGGER_LEVEL_SHIFT    4
#define UFCON_Rx_FIFO_TRIGGER_LEVEL (7 << UFCON_Rx_FIFO_TRIGGER_LEVEL_SHIFT)

/* Uart FIFO Status */
#define UFSTAT_Rx_FIFO_COUNT        0xff
#define UFSTAT_Rx_FIFO_FULL         0x100
#define UFSTAT_Rx_FIFO_ERROR        0x200
#define UFSTAT_Tx_FIFO_COUNT_SHIFT  16
#define UFSTAT_Tx_FIFO_COUNT        (0xff << UFSTAT_Tx_FIFO_COUNT_SHIFT)
#define UFSTAT_Tx_FIFO_FULL_SHIFT   24
#define UFSTAT_Tx_FIFO_FULL         (1 << UFSTAT_Tx_FIFO_FULL_SHIFT)

/* UART Interrupt Source Pending */
#define UINTSP_RXD      0x1 /* Receive interrupt  */
#define UINTSP_ERROR    0x2 /* Error interrupt    */
#define UINTSP_TXD      0x4 /* Transmit interrupt */
#define UINTSP_MODEM    0x8 /* Modem interrupt    */

/* UART Line Control */
#define ULCON_IR_MODE_SHIFT   6
#define ULCON_PARITY_SHIFT    3
#define ULCON_STOP_BIT_SHIFT  1

/* UART Tx/Rx Status */
#define UTRSTAT_TRANSMITTER_EMPTY       0x4
#define UTRSTAT_Tx_BUFFER_EMPTY         0x2
#define UTRSTAT_Rx_BUFFER_DATA_READY    0x1

/* UART Error Status */
#define UERSTAT_OVERRUN  0x1
#define UERSTAT_PARITY   0x2
#define UERSTAT_FRAME    0x4
#define UERSTAT_BREAK    0x8

typedef struct {
    uint8_t    *data;
    uint32_t    sp, rp; /* store and retrieve pointers */
    uint32_t    size;
} Exynos4210UartFIFO;

#define TYPE_EXYNOS4210_UART "exynos4210.uart"
#define EXYNOS4210_UART(obj) \
    OBJECT_CHECK(Exynos4210UartState, (obj), TYPE_EXYNOS4210_UART)

typedef struct Exynos4210UartState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t             reg[EXYNOS4210_UART_REGS_MEM_SIZE / sizeof(uint32_t)];
    Exynos4210UartFIFO   rx;
    Exynos4210UartFIFO   tx;

    CharBackend       chr;
    qemu_irq          irq;

    uint32_t channel;

} Exynos4210UartState;


#if DEBUG_UART
/* Used only for debugging inside PRINT_DEBUG_... macros */
static const char *exynos4210_uart_regname(hwaddr  offset)
{

    int i;

    for (i = 0; i < ARRAY_SIZE(exynos4210_uart_regs); i++) {
        if (offset == exynos4210_uart_regs[i].offset) {
            return exynos4210_uart_regs[i].name;
        }
    }

    return NULL;
}
#endif


static void fifo_store(Exynos4210UartFIFO *q, uint8_t ch)
{
    q->data[q->sp] = ch;
    q->sp = (q->sp + 1) % q->size;
}

static uint8_t fifo_retrieve(Exynos4210UartFIFO *q)
{
    uint8_t ret = q->data[q->rp];
    q->rp = (q->rp + 1) % q->size;
    return  ret;
}

static int fifo_elements_number(const Exynos4210UartFIFO *q)
{
    if (q->sp < q->rp) {
        return q->size - q->rp + q->sp;
    }

    return q->sp - q->rp;
}

static int fifo_empty_elements_number(const Exynos4210UartFIFO *q)
{
    return q->size - fifo_elements_number(q);
}

static void fifo_reset(Exynos4210UartFIFO *q)
{
    g_free(q->data);
    q->data = NULL;

    q->data = (uint8_t *)g_malloc0(q->size);

    q->sp = 0;
    q->rp = 0;
}

static uint32_t exynos4210_uart_Tx_FIFO_trigger_level(const Exynos4210UartState *s)
{
    uint32_t level = 0;
    uint32_t reg;

    reg = (s->reg[I_(UFCON)] & UFCON_Tx_FIFO_TRIGGER_LEVEL) >>
            UFCON_Tx_FIFO_TRIGGER_LEVEL_SHIFT;

    switch (s->channel) {
    case 0:
        level = reg * 32;
        break;
    case 1:
    case 4:
        level = reg * 8;
        break;
    case 2:
    case 3:
        level = reg * 2;
        break;
    default:
        level = 0;
        PRINT_ERROR("Wrong UART channel number: %d\n", s->channel);
    }

    return level;
}

static void exynos4210_uart_update_irq(Exynos4210UartState *s)
{
    /*
     * The Tx interrupt is always requested if the number of data in the
     * transmit FIFO is smaller than the trigger level.
     */
    if (s->reg[I_(UFCON)] & UFCON_FIFO_ENABLE) {

        uint32_t count = (s->reg[I_(UFSTAT)] & UFSTAT_Tx_FIFO_COUNT) >>
                UFSTAT_Tx_FIFO_COUNT_SHIFT;

        if (count <= exynos4210_uart_Tx_FIFO_trigger_level(s)) {
            s->reg[I_(UINTSP)] |= UINTSP_TXD;
        }
    }

    s->reg[I_(UINTP)] = s->reg[I_(UINTSP)] & ~s->reg[I_(UINTM)];

    if (s->reg[I_(UINTP)]) {
        qemu_irq_raise(s->irq);

#if DEBUG_IRQ
        fprintf(stderr, "UART%d: IRQ has been raised: %08x\n",
                s->channel, s->reg[I_(UINTP)]);
#endif

    } else {
        qemu_irq_lower(s->irq);
    }
}

static void exynos4210_uart_update_parameters(Exynos4210UartState *s)
{
    int speed, parity, data_bits, stop_bits;
    QEMUSerialSetParams ssp;
    uint64_t uclk_rate;

    if (s->reg[I_(UBRDIV)] == 0) {
        return;
    }

    if (s->reg[I_(ULCON)] & 0x20) {
        if (s->reg[I_(ULCON)] & 0x28) {
            parity = 'E';
        } else {
            parity = 'O';
        }
    } else {
        parity = 'N';
    }

    if (s->reg[I_(ULCON)] & 0x4) {
        stop_bits = 2;
    } else {
        stop_bits = 1;
    }

    data_bits = (s->reg[I_(ULCON)] & 0x3) + 5;

    uclk_rate = 24000000;

    speed = uclk_rate / ((16 * (s->reg[I_(UBRDIV)]) & 0xffff) +
            (s->reg[I_(UFRACVAL)] & 0x7) + 16);

    ssp.speed     = speed;
    ssp.parity    = parity;
    ssp.data_bits = data_bits;
    ssp.stop_bits = stop_bits;

    qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_SET_PARAMS, &ssp);

    PRINT_DEBUG("UART%d: speed: %d, parity: %c, data: %d, stop: %d\n",
                s->channel, speed, parity, data_bits, stop_bits);
}

static void exynos4210_uart_write(void *opaque, hwaddr offset,
                               uint64_t val, unsigned size)
{
    Exynos4210UartState *s = (Exynos4210UartState *)opaque;
    uint8_t ch;

    PRINT_DEBUG_EXTEND("UART%d: <0x%04x> %s <- 0x%08llx\n", s->channel,
        offset, exynos4210_uart_regname(offset), (long long unsigned int)val);

    switch (offset) {
    case ULCON:
    case UBRDIV:
    case UFRACVAL:
        s->reg[I_(offset)] = val;
        exynos4210_uart_update_parameters(s);
        break;
    case UFCON:
        s->reg[I_(UFCON)] = val;
        if (val & UFCON_Rx_FIFO_RESET) {
            fifo_reset(&s->rx);
            s->reg[I_(UFCON)] &= ~UFCON_Rx_FIFO_RESET;
            PRINT_DEBUG("UART%d: Rx FIFO Reset\n", s->channel);
        }
        if (val & UFCON_Tx_FIFO_RESET) {
            fifo_reset(&s->tx);
            s->reg[I_(UFCON)] &= ~UFCON_Tx_FIFO_RESET;
            PRINT_DEBUG("UART%d: Tx FIFO Reset\n", s->channel);
        }
        break;

    case UTXH:
        if (qemu_chr_fe_backend_connected(&s->chr)) {
            s->reg[I_(UTRSTAT)] &= ~(UTRSTAT_TRANSMITTER_EMPTY |
                    UTRSTAT_Tx_BUFFER_EMPTY);
            ch = (uint8_t)val;
            /* XXX this blocks entire thread. Rewrite to use
             * qemu_chr_fe_write and background I/O callbacks */
            qemu_chr_fe_write_all(&s->chr, &ch, 1);
#if DEBUG_Tx_DATA
            fprintf(stderr, "%c", ch);
#endif
            s->reg[I_(UTRSTAT)] |= UTRSTAT_TRANSMITTER_EMPTY |
                    UTRSTAT_Tx_BUFFER_EMPTY;
            s->reg[I_(UINTSP)]  |= UINTSP_TXD;
            exynos4210_uart_update_irq(s);
        }
        break;

    case UINTP:
        s->reg[I_(UINTP)] &= ~val;
        s->reg[I_(UINTSP)] &= ~val;
        PRINT_DEBUG("UART%d: UINTP [%04x] have been cleared: %08x\n",
                    s->channel, offset, s->reg[I_(UINTP)]);
        exynos4210_uart_update_irq(s);
        break;
    case UTRSTAT:
    case UERSTAT:
    case UFSTAT:
    case UMSTAT:
    case URXH:
        PRINT_DEBUG("UART%d: Trying to write into RO register: %s [%04x]\n",
                    s->channel, exynos4210_uart_regname(offset), offset);
        break;
    case UINTSP:
        s->reg[I_(UINTSP)]  &= ~val;
        break;
    case UINTM:
        s->reg[I_(UINTM)] = val;
        exynos4210_uart_update_irq(s);
        break;
    case UCON:
    case UMCON:
    default:
        s->reg[I_(offset)] = val;
        break;
    }
}
static uint64_t exynos4210_uart_read(void *opaque, hwaddr offset,
                                  unsigned size)
{
    Exynos4210UartState *s = (Exynos4210UartState *)opaque;
    uint32_t res;

    switch (offset) {
    case UERSTAT: /* Read Only */
        res = s->reg[I_(UERSTAT)];
        s->reg[I_(UERSTAT)] = 0;
        return res;
    case UFSTAT: /* Read Only */
        s->reg[I_(UFSTAT)] = fifo_elements_number(&s->rx) & 0xff;
        if (fifo_empty_elements_number(&s->rx) == 0) {
            s->reg[I_(UFSTAT)] |= UFSTAT_Rx_FIFO_FULL;
            s->reg[I_(UFSTAT)] &= ~0xff;
        }
        return s->reg[I_(UFSTAT)];
    case URXH:
        if (s->reg[I_(UFCON)] & UFCON_FIFO_ENABLE) {
            if (fifo_elements_number(&s->rx)) {
                res = fifo_retrieve(&s->rx);
#if DEBUG_Rx_DATA
                fprintf(stderr, "%c", res);
#endif
                if (!fifo_elements_number(&s->rx)) {
                    s->reg[I_(UTRSTAT)] &= ~UTRSTAT_Rx_BUFFER_DATA_READY;
                } else {
                    s->reg[I_(UTRSTAT)] |= UTRSTAT_Rx_BUFFER_DATA_READY;
                }
            } else {
                s->reg[I_(UINTSP)] |= UINTSP_ERROR;
                exynos4210_uart_update_irq(s);
                res = 0;
            }
        } else {
            s->reg[I_(UTRSTAT)] &= ~UTRSTAT_Rx_BUFFER_DATA_READY;
            res = s->reg[I_(URXH)];
        }
        return res;
    case UTXH:
        PRINT_DEBUG("UART%d: Trying to read from WO register: %s [%04x]\n",
                    s->channel, exynos4210_uart_regname(offset), offset);
        break;
    default:
        return s->reg[I_(offset)];
    }

    return 0;
}

static const MemoryRegionOps exynos4210_uart_ops = {
    .read = exynos4210_uart_read,
    .write = exynos4210_uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .max_access_size = 4,
        .unaligned = false
    },
};

static int exynos4210_uart_can_receive(void *opaque)
{
    Exynos4210UartState *s = (Exynos4210UartState *)opaque;

    return fifo_empty_elements_number(&s->rx);
}


static void exynos4210_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    Exynos4210UartState *s = (Exynos4210UartState *)opaque;
    int i;

    if (s->reg[I_(UFCON)] & UFCON_FIFO_ENABLE) {
        if (fifo_empty_elements_number(&s->rx) < size) {
            for (i = 0; i < fifo_empty_elements_number(&s->rx); i++) {
                fifo_store(&s->rx, buf[i]);
            }
            s->reg[I_(UINTSP)] |= UINTSP_ERROR;
            s->reg[I_(UTRSTAT)] |= UTRSTAT_Rx_BUFFER_DATA_READY;
        } else {
            for (i = 0; i < size; i++) {
                fifo_store(&s->rx, buf[i]);
            }
            s->reg[I_(UTRSTAT)] |= UTRSTAT_Rx_BUFFER_DATA_READY;
        }
        /* XXX: Around here we maybe should check Rx trigger level */
        s->reg[I_(UINTSP)] |= UINTSP_RXD;
    } else {
        s->reg[I_(URXH)] = buf[0];
        s->reg[I_(UINTSP)] |= UINTSP_RXD;
        s->reg[I_(UTRSTAT)] |= UTRSTAT_Rx_BUFFER_DATA_READY;
    }

    exynos4210_uart_update_irq(s);
}


static void exynos4210_uart_event(void *opaque, int event)
{
    Exynos4210UartState *s = (Exynos4210UartState *)opaque;

    if (event == CHR_EVENT_BREAK) {
        /* When the RxDn is held in logic 0, then a null byte is pushed into the
         * fifo */
        fifo_store(&s->rx, '\0');
        s->reg[I_(UERSTAT)] |= UERSTAT_BREAK;
        exynos4210_uart_update_irq(s);
    }
}


static void exynos4210_uart_reset(DeviceState *dev)
{
    Exynos4210UartState *s = EXYNOS4210_UART(dev);
    int i;

    for (i = 0; i < ARRAY_SIZE(exynos4210_uart_regs); i++) {
        s->reg[I_(exynos4210_uart_regs[i].offset)] =
                exynos4210_uart_regs[i].reset_value;
    }

    fifo_reset(&s->rx);
    fifo_reset(&s->tx);

    PRINT_DEBUG("UART%d: Rx FIFO size: %d\n", s->channel, s->rx.size);
}

static const VMStateDescription vmstate_exynos4210_uart_fifo = {
    .name = "exynos4210.uart.fifo",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(sp, Exynos4210UartFIFO),
        VMSTATE_UINT32(rp, Exynos4210UartFIFO),
        VMSTATE_VBUFFER_UINT32(data, Exynos4210UartFIFO, 1, NULL, size),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_exynos4210_uart = {
    .name = "exynos4210.uart",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(rx, Exynos4210UartState, 1,
                       vmstate_exynos4210_uart_fifo, Exynos4210UartFIFO),
        VMSTATE_UINT32_ARRAY(reg, Exynos4210UartState,
                             EXYNOS4210_UART_REGS_MEM_SIZE / sizeof(uint32_t)),
        VMSTATE_END_OF_LIST()
    }
};

DeviceState *exynos4210_uart_create(hwaddr addr,
                                    int fifo_size,
                                    int channel,
                                    Chardev *chr,
                                    qemu_irq irq)
{
    DeviceState  *dev;
    SysBusDevice *bus;

    const char chr_name[] = "serial";
    char label[ARRAY_SIZE(chr_name) + 1];

    dev = qdev_create(NULL, TYPE_EXYNOS4210_UART);

    if (!chr) {
        if (channel >= MAX_SERIAL_PORTS) {
            error_report("Only %d serial ports are supported by QEMU",
                         MAX_SERIAL_PORTS);
            exit(1);
        }
        chr = serial_hds[channel];
        if (!chr) {
            snprintf(label, ARRAY_SIZE(label), "%s%d", chr_name, channel);
            chr = qemu_chr_new(label, "null");
            if (!(chr)) {
                error_report("Can't assign serial port to UART%d", channel);
                exit(1);
            }
        }
    }

    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_prop_set_uint32(dev, "channel", channel);
    qdev_prop_set_uint32(dev, "rx-size", fifo_size);
    qdev_prop_set_uint32(dev, "tx-size", fifo_size);

    bus = SYS_BUS_DEVICE(dev);
    qdev_init_nofail(dev);
    if (addr != (hwaddr)-1) {
        sysbus_mmio_map(bus, 0, addr);
    }
    sysbus_connect_irq(bus, 0, irq);

    return dev;
}

static void exynos4210_uart_init(Object *obj)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    Exynos4210UartState *s = EXYNOS4210_UART(dev);

    /* memory mapping */
    memory_region_init_io(&s->iomem, obj, &exynos4210_uart_ops, s,
                          "exynos4210.uart", EXYNOS4210_UART_REGS_MEM_SIZE);
    sysbus_init_mmio(dev, &s->iomem);

    sysbus_init_irq(dev, &s->irq);
}

static void exynos4210_uart_realize(DeviceState *dev, Error **errp)
{
    Exynos4210UartState *s = EXYNOS4210_UART(dev);

    qemu_chr_fe_set_handlers(&s->chr, exynos4210_uart_can_receive,
                             exynos4210_uart_receive, exynos4210_uart_event,
                             NULL, s, NULL, true);
}

static Property exynos4210_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", Exynos4210UartState, chr),
    DEFINE_PROP_UINT32("channel", Exynos4210UartState, channel, 0),
    DEFINE_PROP_UINT32("rx-size", Exynos4210UartState, rx.size, 16),
    DEFINE_PROP_UINT32("tx-size", Exynos4210UartState, tx.size, 16),
    DEFINE_PROP_END_OF_LIST(),
};

static void exynos4210_uart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = exynos4210_uart_realize;
    dc->reset = exynos4210_uart_reset;
    dc->props = exynos4210_uart_properties;
    dc->vmsd = &vmstate_exynos4210_uart;
}

static const TypeInfo exynos4210_uart_info = {
    .name          = TYPE_EXYNOS4210_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Exynos4210UartState),
    .instance_init = exynos4210_uart_init,
    .class_init    = exynos4210_uart_class_init,
};

static void exynos4210_uart_register(void)
{
    type_register_static(&exynos4210_uart_info);
}

type_init(exynos4210_uart_register)
