/*
 * Syborg serial port
 *
 * Copyright (c) 2008 CodeSourcery
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

#include "sysbus.h"
#include "qemu-char.h"
#include "syborg.h"

//#define DEBUG_SYBORG_SERIAL

#ifdef DEBUG_SYBORG_SERIAL
#define DPRINTF(fmt, ...) \
do { printf("syborg_serial: " fmt , ##args); } while (0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "syborg_serial: error: " fmt , ## __VA_ARGS__); \
    exit(1);} while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "syborg_serial: error: " fmt , ## __VA_ARGS__);} while (0)
#endif

enum {
    SERIAL_ID           = 0,
    SERIAL_DATA         = 1,
    SERIAL_FIFO_COUNT   = 2,
    SERIAL_INT_ENABLE   = 3,
    SERIAL_DMA_TX_ADDR  = 4,
    SERIAL_DMA_TX_COUNT = 5, /* triggers dma */
    SERIAL_DMA_RX_ADDR  = 6,
    SERIAL_DMA_RX_COUNT = 7, /* triggers dma */
    SERIAL_FIFO_SIZE    = 8
};

#define SERIAL_INT_FIFO   (1u << 0)
#define SERIAL_INT_DMA_TX (1u << 1)
#define SERIAL_INT_DMA_RX (1u << 2)

typedef struct {
    SysBusDevice busdev;
    uint32_t int_enable;
    uint32_t fifo_size;
    uint32_t *read_fifo;
    int read_pos;
    int read_count;
    CharDriverState *chr;
    qemu_irq irq;
    uint32_t dma_tx_ptr;
    uint32_t dma_rx_ptr;
    uint32_t dma_rx_size;
} SyborgSerialState;

static void syborg_serial_update(SyborgSerialState *s)
{
    int level;
    level = 0;
    if ((s->int_enable & SERIAL_INT_FIFO) && s->read_count)
        level = 1;
    if (s->int_enable & SERIAL_INT_DMA_TX)
        level = 1;
    if ((s->int_enable & SERIAL_INT_DMA_RX) && s->dma_rx_size == 0)
        level = 1;

    qemu_set_irq(s->irq, level);
}

static uint32_t fifo_pop(SyborgSerialState *s)
{
    const uint32_t c = s->read_fifo[s->read_pos];
    s->read_count--;
    s->read_pos++;
    if (s->read_pos == s->fifo_size)
        s->read_pos = 0;

    DPRINTF("FIFO pop %x (%d)\n", c, s->read_count);
    return c;
}

static void fifo_push(SyborgSerialState *s, uint32_t new_value)
{
    int slot;

    DPRINTF("FIFO push %x (%d)\n", new_value, s->read_count);
    slot = s->read_pos + s->read_count;
    if (slot >= s->fifo_size)
          slot -= s->fifo_size;
    s->read_fifo[slot] = new_value;
    s->read_count++;
}

static void do_dma_tx(SyborgSerialState *s, uint32_t count)
{
    unsigned char ch;

    if (count == 0)
        return;

    if (s->chr != NULL) {
        /* optimize later. Now, 1 byte per iteration */
        while (count--) {
            cpu_physical_memory_read(s->dma_tx_ptr, &ch, 1);
            qemu_chr_write(s->chr, &ch, 1);
            s->dma_tx_ptr++;
        }
    } else {
        s->dma_tx_ptr += count;
    }
    /* QEMU char backends do not have a nonblocking mode, so we transmit all
       the data immediately and the interrupt status will be unchanged.  */
}

/* Initiate RX DMA, and transfer data from the FIFO.  */
static void dma_rx_start(SyborgSerialState *s, uint32_t len)
{
    uint32_t dest;
    unsigned char ch;

    dest = s->dma_rx_ptr;
    if (s->read_count < len) {
        s->dma_rx_size = len - s->read_count;
        len = s->read_count;
    } else {
        s->dma_rx_size = 0;
    }

    while (len--) {
        ch = fifo_pop(s);
        cpu_physical_memory_write(dest, &ch, 1);
        dest++;
    }
    s->dma_rx_ptr = dest;
    syborg_serial_update(s);
}

static uint32_t syborg_serial_read(void *opaque, target_phys_addr_t offset)
{
    SyborgSerialState *s = (SyborgSerialState *)opaque;
    uint32_t c;

    offset &= 0xfff;
    DPRINTF("read 0x%x\n", (int)offset);
    switch(offset >> 2) {
    case SERIAL_ID:
        return SYBORG_ID_SERIAL;
    case SERIAL_DATA:
        if (s->read_count > 0)
            c = fifo_pop(s);
        else
            c = -1;
        syborg_serial_update(s);
        return c;
    case SERIAL_FIFO_COUNT:
        return s->read_count;
    case SERIAL_INT_ENABLE:
        return s->int_enable;
    case SERIAL_DMA_TX_ADDR:
        return s->dma_tx_ptr;
    case SERIAL_DMA_TX_COUNT:
        return 0;
    case SERIAL_DMA_RX_ADDR:
        return s->dma_rx_ptr;
    case SERIAL_DMA_RX_COUNT:
        return s->dma_rx_size;
    case SERIAL_FIFO_SIZE:
        return s->fifo_size;

    default:
        cpu_abort(cpu_single_env, "syborg_serial_read: Bad offset %x\n",
                  (int)offset);
        return 0;
    }
}

static void syborg_serial_write(void *opaque, target_phys_addr_t offset,
                                uint32_t value)
{
    SyborgSerialState *s = (SyborgSerialState *)opaque;
    unsigned char ch;

    offset &= 0xfff;
    DPRINTF("Write 0x%x=0x%x\n", (int)offset, value);
    switch (offset >> 2) {
    case SERIAL_DATA:
        ch = value;
        if (s->chr)
            qemu_chr_write(s->chr, &ch, 1);
        break;
    case SERIAL_INT_ENABLE:
        s->int_enable = value;
        syborg_serial_update(s);
        break;
    case SERIAL_DMA_TX_ADDR:
        s->dma_tx_ptr = value;
        break;
    case SERIAL_DMA_TX_COUNT:
        do_dma_tx(s, value);
        break;
    case SERIAL_DMA_RX_ADDR:
        /* For safety, writes to this register cancel any pending DMA.  */
        s->dma_rx_size = 0;
        s->dma_rx_ptr = value;
        break;
    case SERIAL_DMA_RX_COUNT:
        dma_rx_start(s, value);
        break;
    default:
        cpu_abort(cpu_single_env, "syborg_serial_write: Bad offset %x\n",
                  (int)offset);
        break;
    }
}

static int syborg_serial_can_receive(void *opaque)
{
    SyborgSerialState *s = (SyborgSerialState *)opaque;

    if (s->dma_rx_size)
        return s->dma_rx_size;
    return s->fifo_size - s->read_count;
}

static void syborg_serial_receive(void *opaque, const uint8_t *buf, int size)
{
    SyborgSerialState *s = (SyborgSerialState *)opaque;

    if (s->dma_rx_size) {
        /* Place it in the DMA buffer.  */
        cpu_physical_memory_write(s->dma_rx_ptr, buf, size);
        s->dma_rx_size -= size;
        s->dma_rx_ptr += size;
    } else {
        while (size--)
            fifo_push(s, *buf);
    }

    syborg_serial_update(s);
}

static void syborg_serial_event(void *opaque, int event)
{
    /* TODO: Report BREAK events?  */
}

static CPUReadMemoryFunc * const syborg_serial_readfn[] = {
     syborg_serial_read,
     syborg_serial_read,
     syborg_serial_read
};

static CPUWriteMemoryFunc * const syborg_serial_writefn[] = {
     syborg_serial_write,
     syborg_serial_write,
     syborg_serial_write
};

static const VMStateDescription vmstate_syborg_serial = {
    .name = "syborg_serial",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32_EQUAL(fifo_size, SyborgSerialState),
        VMSTATE_UINT32(int_enable, SyborgSerialState),
        VMSTATE_INT32(read_pos, SyborgSerialState),
        VMSTATE_INT32(read_count, SyborgSerialState),
        VMSTATE_UINT32(dma_tx_ptr, SyborgSerialState),
        VMSTATE_UINT32(dma_rx_ptr, SyborgSerialState),
        VMSTATE_UINT32(dma_rx_size, SyborgSerialState),
        VMSTATE_VARRAY_UINT32(read_fifo, SyborgSerialState, fifo_size, 1,
                              vmstate_info_uint32, uint32),
        VMSTATE_END_OF_LIST()
    }
};

static int syborg_serial_init(SysBusDevice *dev)
{
    SyborgSerialState *s = FROM_SYSBUS(SyborgSerialState, dev);
    int iomemtype;

    sysbus_init_irq(dev, &s->irq);
    iomemtype = cpu_register_io_memory(syborg_serial_readfn,
                                       syborg_serial_writefn, s,
                                       DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, 0x1000, iomemtype);
    s->chr = qdev_init_chardev(&dev->qdev);
    if (s->chr) {
        qemu_chr_add_handlers(s->chr, syborg_serial_can_receive,
                              syborg_serial_receive, syborg_serial_event, s);
    }
    if (s->fifo_size <= 0) {
        fprintf(stderr, "syborg_serial: fifo too small\n");
        s->fifo_size = 16;
    }
    s->read_fifo = g_malloc0(s->fifo_size * sizeof(s->read_fifo[0]));

    return 0;
}

static SysBusDeviceInfo syborg_serial_info = {
    .init = syborg_serial_init,
    .qdev.name  = "syborg,serial",
    .qdev.size  = sizeof(SyborgSerialState),
    .qdev.vmsd  = &vmstate_syborg_serial,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("fifo-size", SyborgSerialState, fifo_size, 16),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void syborg_serial_register_devices(void)
{
    sysbus_register_withprop(&syborg_serial_info);
}

device_init(syborg_serial_register_devices)
