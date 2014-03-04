/*
 * mxs_uart.c
 *
 * Copyright: Michel Pollet <buserror@gmail.com>
 *
 * QEMU Licence
 */

/*
 * Work in progress ! Right now there's just enough so that linux driver
 * will instantiate after a probe, there is no functional code.
 */
#include "hw/sysbus.h"
#include "hw/arm/mxs.h"

#define D(w) w

enum {
    UART_CTRL = 0x0,
    UART_CTRL1 = 0x1,
    UART_CTRL2 = 0x2,
    UART_LINECTRL = 0x3,
    UART_LINECTRL2 = 0x4,
    UART_INTR = 0x5,
    UART_APP_DATA = 0x6,
    UART_APP_STAT = 0x7,
    UART_APP_DEBUG = 0x8,
    UART_APP_VERSION = 0x9,
    UART_APP_AUTOBAUD = 0xa,

    UART_MAX,
};
typedef struct mxs_uart_state {
    SysBusDevice busdev;
    MemoryRegion iomem;

    uint32_t r[UART_MAX];

    struct {
        uint16_t b[16];
        int w, r;
    } fifo[2];
    qemu_irq irq;
    CharDriverState *chr;
} mxs_uart_state;

static uint64_t mxs_uart_read(
        void *opaque, hwaddr offset, unsigned size)
{
    mxs_uart_state *s = (mxs_uart_state *) opaque;
    uint32_t res = 0;

    D(printf("%s %04x (%d) = ", __func__, (int)offset, size);)
    switch (offset >> 4) {
        case 0 ... UART_MAX:
            res = s->r[offset >> 4];
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: bad offset 0x%x\n", __func__, (int) offset);
            break;
    }
    D(printf("%08x\n", res);)

    return res;
}

static void mxs_uart_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
    mxs_uart_state *s = (mxs_uart_state *) opaque;
    uint32_t oldvalue = 0;

    D(printf("%s %04x %08x(%d)\n", __func__, (int)offset, (int)value, size);)
    switch (offset >> 4) {
        case 0 ... UART_MAX:
            mxs_write(&s->r[offset >> 4], offset, value, size);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: bad offset 0x%x\n", __func__, (int) offset);
            break;
    }
    switch (offset >> 4) {
        case UART_CTRL:
            if ((oldvalue ^ s->r[UART_CTRL]) == 0x80000000
                    && !(oldvalue & 0x80000000)) {
                printf("%s reseting, anding clockgate\n", __func__);
                s->r[UART_CTRL] |= 0x40000000;
            }
            break;
    }
}

static void mxs_uart_set_irq(void *opaque, int irq, int level)
{
//    mxs_uart_state *s = (mxs_uart_state *)opaque;
    printf("%s %3d = %d\n", __func__, irq, level);
}

static const MemoryRegionOps mxs_uart_ops = {
    .read = mxs_uart_read,
    .write = mxs_uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static int mxs_uart_init(SysBusDevice *dev)
{
    mxs_uart_state *s = OBJECT_CHECK(mxs_uart_state, dev, "mxs_uart");
    DeviceState *qdev = DEVICE(dev);

    qdev_init_gpio_in(qdev, mxs_uart_set_irq, 32 * 3);
    sysbus_init_irq(dev, &s->irq);
    memory_region_init_io(&s->iomem, OBJECT(s), &mxs_uart_ops, s, "mxs_uart", 0x2000);
    sysbus_init_mmio(dev, &s->iomem);

    s->r[UART_CTRL] = 0xc0030000;
    s->r[UART_CTRL2] = 0x00220180;
    s->r[UART_APP_STAT] = 0x89f00000;
    s->r[UART_APP_VERSION] = 0x03000000;
    return 0;
}


static void mxs_uart_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = mxs_uart_init;
}

static TypeInfo uart_info = {
    .name          = "mxs_uart",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mxs_uart_state),
    .class_init    = mxs_uart_class_init,
};

static void mxs_uart_register(void)
{
    type_register_static(&uart_info);
}

type_init(mxs_uart_register)

