/*
 * SPDX-License-Identifer: GPL-2.0-or-later
 *
 * Goldfish TTY
 *
 * (c) 2020 Laurent Vivier <laurent@vivier.eu>
 *
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties-system.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "chardev/char-fe.h"
#include "qemu/log.h"
#include "trace.h"
#include "exec/address-spaces.h"
#include "hw/char/goldfish_tty.h"

#define GOLDFISH_TTY_VERSION 1

/* registers */

enum {
    REG_PUT_CHAR      = 0x00,
    REG_BYTES_READY   = 0x04,
    REG_CMD           = 0x08,
    REG_DATA_PTR      = 0x10,
    REG_DATA_LEN      = 0x14,
    REG_DATA_PTR_HIGH = 0x18,
    REG_VERSION       = 0x20,
};

/* commands */

enum {
    CMD_INT_DISABLE   = 0x00,
    CMD_INT_ENABLE    = 0x01,
    CMD_WRITE_BUFFER  = 0x02,
    CMD_READ_BUFFER   = 0x03,
};

static uint64_t goldfish_tty_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    GoldfishTTYState *s = opaque;
    uint64_t value = 0;

    switch (addr) {
    case REG_BYTES_READY:
        value = fifo8_num_used(&s->rx_fifo);
        break;
    case REG_VERSION:
        value = GOLDFISH_TTY_VERSION;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register read 0x%02"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }

    trace_goldfish_tty_read(s, addr, size, value);

    return value;
}

static void goldfish_tty_cmd(GoldfishTTYState *s, uint32_t cmd)
{
    uint32_t to_copy;
    uint8_t *buf;
    uint8_t data_out[GOLFISH_TTY_BUFFER_SIZE];
    int len;
    uint64_t ptr;

    switch (cmd) {
    case CMD_INT_DISABLE:
        if (s->int_enabled) {
            if (!fifo8_is_empty(&s->rx_fifo)) {
                qemu_set_irq(s->irq, 0);
            }
            s->int_enabled = false;
        }
        break;
    case CMD_INT_ENABLE:
        if (!s->int_enabled) {
            if (!fifo8_is_empty(&s->rx_fifo)) {
                qemu_set_irq(s->irq, 1);
            }
            s->int_enabled = true;
        }
        break;
    case CMD_WRITE_BUFFER:
        len = s->data_len;
        ptr = s->data_ptr;
        while (len) {
            to_copy = MIN(GOLFISH_TTY_BUFFER_SIZE, len);

            address_space_rw(&address_space_memory, ptr,
                             MEMTXATTRS_UNSPECIFIED, data_out, to_copy, 0);
            qemu_chr_fe_write_all(&s->chr, data_out, to_copy);

            len -= to_copy;
            ptr += to_copy;
        }
        break;
    case CMD_READ_BUFFER:
        len = s->data_len;
        ptr = s->data_ptr;
        while (len && !fifo8_is_empty(&s->rx_fifo)) {
            buf = (uint8_t *)fifo8_pop_buf(&s->rx_fifo, len, &to_copy);
            address_space_rw(&address_space_memory, ptr,
                            MEMTXATTRS_UNSPECIFIED, buf, to_copy, 1);

            len -= to_copy;
            ptr += to_copy;
        }
        if (s->int_enabled && fifo8_is_empty(&s->rx_fifo)) {
            qemu_set_irq(s->irq, 0);
        }
        break;
    }
}

static void goldfish_tty_write(void *opaque, hwaddr addr,
                               uint64_t value, unsigned size)
{
    GoldfishTTYState *s = opaque;
    unsigned char c;

    trace_goldfish_tty_write(s, addr, size, value);

    switch (addr) {
    case REG_PUT_CHAR:
        c = value;
        qemu_chr_fe_write_all(&s->chr, &c, sizeof(c));
        break;
    case REG_CMD:
        goldfish_tty_cmd(s, value);
        break;
    case REG_DATA_PTR:
        s->data_ptr = value;
        break;
    case REG_DATA_PTR_HIGH:
        s->data_ptr = deposit64(s->data_ptr, 32, 32, value);
        break;
    case REG_DATA_LEN:
        s->data_len = value;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register write 0x%02"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps goldfish_tty_ops = {
    .read = goldfish_tty_read,
    .write = goldfish_tty_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.max_access_size = 4,
    .impl.max_access_size = 4,
    .impl.min_access_size = 4,
};

static int goldfish_tty_can_receive(void *opaque)
{
    GoldfishTTYState *s = opaque;
    int available = fifo8_num_free(&s->rx_fifo);

    trace_goldfish_tty_can_receive(s, available);

    return available;
}

static void goldfish_tty_receive(void *opaque, const uint8_t *buffer, int size)
{
    GoldfishTTYState *s = opaque;

    trace_goldfish_tty_receive(s, size);

    g_assert(size <= fifo8_num_free(&s->rx_fifo));

    fifo8_push_all(&s->rx_fifo, buffer, size);

    if (s->int_enabled && !fifo8_is_empty(&s->rx_fifo)) {
        qemu_set_irq(s->irq, 1);
    }
}

static void goldfish_tty_reset(DeviceState *dev)
{
    GoldfishTTYState *s = GOLDFISH_TTY(dev);

    trace_goldfish_tty_reset(s);

    fifo8_reset(&s->rx_fifo);
    s->int_enabled = false;
    s->data_ptr = 0;
    s->data_len = 0;
}

static void goldfish_tty_realize(DeviceState *dev, Error **errp)
{
    GoldfishTTYState *s = GOLDFISH_TTY(dev);

    trace_goldfish_tty_realize(s);

    fifo8_create(&s->rx_fifo, GOLFISH_TTY_BUFFER_SIZE);
    memory_region_init_io(&s->iomem, OBJECT(s), &goldfish_tty_ops, s,
                          "goldfish_tty", 0x24);

    if (qemu_chr_fe_backend_connected(&s->chr)) {
        qemu_chr_fe_set_handlers(&s->chr, goldfish_tty_can_receive,
                                 goldfish_tty_receive, NULL, NULL,
                                 s, NULL, true);
    }
}

static void goldfish_tty_unrealize(DeviceState *dev)
{
    GoldfishTTYState *s = GOLDFISH_TTY(dev);

    trace_goldfish_tty_unrealize(s);

    fifo8_destroy(&s->rx_fifo);
}

static const VMStateDescription vmstate_goldfish_tty = {
    .name = "goldfish_tty",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(data_len, GoldfishTTYState),
        VMSTATE_UINT64(data_ptr, GoldfishTTYState),
        VMSTATE_BOOL(int_enabled, GoldfishTTYState),
        VMSTATE_FIFO8(rx_fifo, GoldfishTTYState),
        VMSTATE_END_OF_LIST()
    }
};

static Property goldfish_tty_properties[] = {
    DEFINE_PROP_CHR("chardev", GoldfishTTYState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void goldfish_tty_instance_init(Object *obj)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    GoldfishTTYState *s = GOLDFISH_TTY(obj);

    trace_goldfish_tty_instance_init(s);

    sysbus_init_mmio(dev, &s->iomem);
    sysbus_init_irq(dev, &s->irq);
}

static void goldfish_tty_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_props(dc, goldfish_tty_properties);
    dc->reset = goldfish_tty_reset;
    dc->realize = goldfish_tty_realize;
    dc->unrealize = goldfish_tty_unrealize;
    dc->vmsd = &vmstate_goldfish_tty;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo goldfish_tty_info = {
    .name = TYPE_GOLDFISH_TTY,
    .parent = TYPE_SYS_BUS_DEVICE,
    .class_init = goldfish_tty_class_init,
    .instance_init = goldfish_tty_instance_init,
    .instance_size = sizeof(GoldfishTTYState),
};

static void goldfish_tty_register_types(void)
{
    type_register_static(&goldfish_tty_info);
}

type_init(goldfish_tty_register_types)
