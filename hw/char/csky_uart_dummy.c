/*
 * Csky UART emulation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "sysemu/char.h"
#include "qemu/log.h"
#include "trace.h"

typedef struct csky_uart_dummy_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t dll;   /* Divisor Latch Low */
    uint32_t dlh;   /* Divisor Latch High */
    uint32_t ier;   /* Interrupt Enable Register */
    uint32_t iir;   /* Interrupt Identity Register */
    uint32_t fcr;   /* FIFO control register */
    uint32_t lcr;   /* line control register */
    uint32_t mcr;   /* modem control register */
    uint32_t lsr;   /* line status register */
    uint32_t msr;   /* modem status register */
    uint32_t usr;   /* uart status register */
    uint32_t rx_fifo[16];
    int rx_pos;
    int rx_count;
    int rx_trigger;
    CharBackend chr;
    qemu_irq irq;
} csky_uart_dummy_state;

#define TYPE_CSKY_UART  "csky_uart_dummy"
#define CSKY_UART(obj)  OBJECT_CHECK(csky_uart_dummy_state, (obj), TYPE_CSKY_UART)

/* lsr:line status register */
#define lsr_TEMT 0x40
#define lsr_THRE 0x20   /* no new data has been written
                           to the THR or TX FIFO */
#define lsr_OE   0x2    /* overruun error */
#define lsr_DR   0x1    /* at least one character in the RBR or
                           the receiver FIFO */


/* flags: USR user status register */
#define usr_REF  0x10   /* Receive FIFO Full */
#define usr_RFNE 0x8    /* Receive FIFO not empty */
#define usr_TFE  0x4    /* transmit FIFO empty */
#define usr_TFNF 0x2    /* transmit FIFO not full */

/* interrupt type */
#define INT_NONE 0x1   /* no interrupt */
#define INT_TX 0x2     /* Transmitter holding register empty */
#define INT_RX 0x4     /* Receiver data available */

static void csky_uart_dummy_update(csky_uart_dummy_state *s)
{
    uint32_t flags = 0;

    flags = (s->iir & 0xf) == INT_TX && (s->ier & 0x2) != 0;
    flags |= (s->iir & 0xf) == INT_RX && (s->ier & 0x1) != 0;
    qemu_set_irq(s->irq, flags != 0);
}

static uint64_t csky_uart_dummy_read(void *opaque, hwaddr offset, unsigned size)
{
    csky_uart_dummy_state *s = (csky_uart_dummy_state *)opaque;
    uint32_t c;
    uint64_t ret = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_uart_dummy_read: 0x%x must word align read\n",
                      (int)offset);
    }

    switch ((offset & 0xfff) >> 2) {
    case 0x0: /* RBR,DLL */
        if (s->lcr & 0x80) {
            ret = s->dll;
        } else if (s->fcr & 0x1) {
            s->usr &= ~usr_REF;   /* receive fifo not full */
            c = s->rx_fifo[s->rx_pos];
            if (s->rx_count > 0) {
                s->rx_count--;
                if (++s->rx_pos == 16) {
                    s->rx_pos = 0;
                }
            }
            if (s->rx_count == 0) {
                s->lsr &= ~lsr_DR;
                s->usr &= ~usr_RFNE;    /* receive fifo empty */
            }
            if (s->rx_count == s->rx_trigger - 1) {
                s->iir = (s->iir & ~0xf) | INT_NONE;
            }
            csky_uart_dummy_update(s);
            qemu_chr_fe_accept_input(&s->chr);
            ret =  c;
        } else {
            s->usr &= ~usr_REF;
            s->usr &= ~usr_RFNE;
            s->lsr &= ~lsr_DR;
            s->iir = (s->iir & ~0xf) | INT_NONE;
            csky_uart_dummy_update(s);
            qemu_chr_fe_accept_input(&s->chr);
            ret =  s->rx_fifo[0];
        }
        break;
    case 0x1: /* DLH, IER */
        if (s->lcr & 0x80) {
            ret = s->dlh;
        } else {
            ret = s->ier;
        }
        break;
    case 0x2: /* IIR */
        if ((s->iir & 0xf) == INT_RX) {
            s->iir = (s->iir & ~0xf) | INT_NONE;
            csky_uart_dummy_update(s);
            ret = (s->iir & ~0xf) | INT_RX;
        } else if ((s->iir & 0xf) == INT_TX) {
            s->iir = (s->iir & ~0xf) | INT_NONE;
            csky_uart_dummy_update(s);
            ret = (s->iir & ~0xf) | INT_TX;
        } else {
            ret = s->iir;
        }
        break;
    case 0x3: /* LCR */
        ret = s->lcr;
        break;
    case 0x4: /* MCR */
        ret = s->mcr;
        break;
    case 0x5: /* LSR */
        ret = s->lsr;
        break;
    case 0x6: /* MSR */
        ret = s->msr;
        break;
    case 0x1f: /* USR */
        ret = s->usr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_uart_dummy_read: Bad offset %x\n", (int)offset);
    }

    return ret;
}

static void csky_uart_dummy_fcr_update(csky_uart_dummy_state *s)
{
    /* update rx_trigger */
    if (s->fcr & 0x1) {
        /* fifo enabled */
        switch ((s->fcr >> 6) & 0x3) {
        case 0:
            s->rx_trigger = 1;
            break;
        case 1:
            s->rx_trigger = 4;
            break;
        case 2:
            s->rx_trigger = 8;
            break;
        case 3:
            s->rx_trigger = 14;
            break;
        default:
            s->rx_trigger = 1;
            break;
        }
    } else {
        s->rx_trigger = 1;
    }

    /* reset rx_fifo */
    if (s->fcr & 0x2) {
        s->rx_pos = 0;
        s->rx_count = 0;
    }
}

static void csky_uart_dummy_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    csky_uart_dummy_state *s = (csky_uart_dummy_state *)opaque;
    unsigned char ch;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_uart_dummy_write: 0x%x must word align read\n",
                      (int)offset);
    }

    switch (offset >> 2) {
    case 0x0: /*dll, thr */
        if (s->lcr & 0x80) {
            s->dll = value;
        } else {
            ch = value;
            qemu_chr_fe_write_all(&s->chr, &ch, 1);
            s->lsr |= (lsr_THRE | lsr_TEMT);
            if ((s->iir & 0xf) != INT_RX) {
                s->iir = (s->iir & ~0xf) | INT_TX;
            }
            csky_uart_dummy_update(s);
        }
        break;
    case 0x1: /* DLH, IER */
        if (s->lcr & 0x80) {
            s->dlh = value;
        } else {
            s->ier = value;
            s->iir = (s->iir & ~0xf) | INT_TX;
            csky_uart_dummy_update(s);
        }
        break;
    case 0x2: /* FCR */
        if ((s->fcr & 0x1) ^ (value & 0x1)) {
            /* change fifo enable bit, reset rx_fifo */
            s->rx_pos = 0;
            s->rx_count = 0;
        }
        s->fcr = value;
        csky_uart_dummy_fcr_update(s);
        break;
    case 0x3: /* LCR */
        s->lcr = value;
        break;
    case 0x4: /* MCR */
        s->mcr = value;
        break;
    case 0x5: /* LSR read only*/
        return;
    case 0x6: /* MSR read only*/
        return;
    case 0x1f: /* USR read only*/
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_uart_dummy_write: Bad offset %x\n", (int)offset);
    }
}

static int csky_uart_dummy_can_receive(void *opaque)
{
    /* always can receive data */
    csky_uart_dummy_state *s = (csky_uart_dummy_state *)opaque;

    if (s->fcr & 0x1) { /* fifo enabled */
        return s->rx_count < 16;
    } else {
        return s->rx_count < 1;
    }
}


static void csky_uart_dummy_receive(void *opaque, const uint8_t *buf, int size)
{
    csky_uart_dummy_state *s = (csky_uart_dummy_state *)opaque;
    int slot;

    if (size < 1) {
        return;
    }

    if (s->usr & usr_REF) {
        s->lsr |= lsr_OE;  /* overrun error */
    }

    if (!(s->fcr & 0x1)) { /* none fifo mode */
        s->rx_fifo[0] = *buf;
        s->usr |= usr_REF;
        s->usr |= usr_RFNE;
        s->iir = (s->iir & ~0xf) | INT_RX;
        s->lsr |= lsr_DR;
        csky_uart_dummy_update(s);
        return;
    }

    /* fifo mode */
    slot = s->rx_pos + s->rx_count;
    if (slot >= 16) {
        slot -= 16;
    }
    s->rx_fifo[slot] = *buf;
    s->rx_count++;
    s->lsr |= lsr_DR;
    s->usr |= usr_RFNE;     /* receive fifo not empty */
    if (s->rx_count == 16) {
        s->usr |= usr_REF;    /* receive fifo full */
    }
    s->iir = (s->iir & ~0xf) | INT_RX;
    csky_uart_dummy_update(s);
    return;
}

static void csky_uart_dummy_event(void *opaque, int event)
{
}

static const MemoryRegionOps csky_uart_dummy_ops = {
    .read = csky_uart_dummy_read,
    .write = csky_uart_dummy_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_csky_uart_dummy = {
    .name = TYPE_CSKY_UART,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(dll, csky_uart_dummy_state),
        VMSTATE_UINT32(dlh, csky_uart_dummy_state),
        VMSTATE_UINT32(ier, csky_uart_dummy_state),
        VMSTATE_UINT32(iir, csky_uart_dummy_state),
        VMSTATE_UINT32(fcr, csky_uart_dummy_state),
        VMSTATE_UINT32(lcr, csky_uart_dummy_state),
        VMSTATE_UINT32(mcr, csky_uart_dummy_state),
        VMSTATE_UINT32(lsr, csky_uart_dummy_state),
        VMSTATE_UINT32(msr, csky_uart_dummy_state),
        VMSTATE_UINT32(usr, csky_uart_dummy_state),
        VMSTATE_UINT32_ARRAY(rx_fifo, csky_uart_dummy_state, 16),
        VMSTATE_INT32(rx_pos, csky_uart_dummy_state),
        VMSTATE_INT32(rx_count, csky_uart_dummy_state),
        VMSTATE_INT32(rx_trigger, csky_uart_dummy_state),
        VMSTATE_END_OF_LIST()
    }
};

static Property csky_uart_dummy_properties[] = {
    DEFINE_PROP_CHR("chardev", csky_uart_dummy_state, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void csky_uart_dummy_init(Object *obj)
{
    csky_uart_dummy_state *s = CSKY_UART(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &csky_uart_dummy_ops, s,
                          TYPE_CSKY_UART, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->rx_trigger = 1;
    s->dlh = 0x4;
    s->iir = 0x1;
    s->lsr = 0x60;
    s->usr = 0x6;
}

static void csky_uart_dummy_realize(DeviceState *dev, Error **errp)
{
    csky_uart_dummy_state *s = CSKY_UART(dev);

    qemu_chr_fe_set_handlers(&s->chr, csky_uart_dummy_can_receive,
                             csky_uart_dummy_receive,
                             csky_uart_dummy_event, s, NULL, true);
}

static void csky_uart_dummy_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = csky_uart_dummy_realize;
    dc->vmsd = &vmstate_csky_uart_dummy;
    dc->props = csky_uart_dummy_properties;
}

static const TypeInfo csky_uart_dummy_info = {
    .name          = TYPE_CSKY_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_uart_dummy_state),
    .instance_init = csky_uart_dummy_init,
    .class_init    = csky_uart_dummy_class_init,
};


static void csky_uart_dummy_register_types(void)
{
    type_register_static(&csky_uart_dummy_info);
}

type_init(csky_uart_dummy_register_types)

