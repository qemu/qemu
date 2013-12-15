/* hw/s3c24xx_gpio.c
 *
 * Samsung S3C24XX GPIO emulation (mostly for E-INT)
 *
 * Copyright 2006, 2007 Daniel Silverstone and Vincent Sanders
 *
 * Copyright 2010, 2013 Stefan Weil
 *
 * This file is under the terms of the GNU General Public License Version 2.
 */

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "s3c24xx.h"

#define S3C_GPIO_GPECON         0x40
#define S3C_GPIO_GPEDAT         0x44
#define S3C_GPIO_GPEUP          0x48

#define S3C_GPIO_EINT_MASK      0xA4
#define S3C_GPIO_EINT_PEND      0xA8
#define S3C_GPIO_GSTATUS0       0xAC
#define S3C_GPIO_GSTATUS1       0xB0
#define S3C_GPIO_GSTATUS2       0xB4
#define S3C_GPIO_GSTATUS3       0xB8
#define S3C_GPIO_GSTATUS4       0xBC

#define GPRN(r) (r>>2)
#define GPR(P) s->gpio_reg[P>>2]

#define S3C_GPIO_MAX            0x43

/* GPIO controller state */

#define TYPE_S3C24XX_GPIO "s3c24xx_gpio"
#define S3C24XX_GPIO(obj) \
    OBJECT_CHECK(S3C24xxGpioState, (obj), TYPE_S3C24XX_GPIO)

struct S3C24xxGpioState {
    SysBusDevice busdev;
    MemoryRegion mmio;

    uint32_t gpio_reg[S3C_GPIO_MAX];

    qemu_irq *eirqs; /* gpio external interrupts */

    qemu_irq irqs[6]; /* cpu irqs to cascade */
};

static void
s3c24xx_gpio_propogate_eint(S3C24xxGpioState *s)
{
    uint32_t ints, i;

    ints = GPR(S3C_GPIO_EINT_PEND) & ~GPR(S3C_GPIO_EINT_MASK);

    /* EINT0 - EINT3 are INT0 - INT3 */
    for (i=0; i < 4; ++i) {
        qemu_set_irq(s->irqs[i], (ints & (1<<i))?1:0);
    }

    /* EINT4 - EINT7 are INT4 */
    qemu_set_irq(s->irqs[4], (ints & 0xf0)?1:0);

    /* EINT8 - EINT23 are INT5 */
    qemu_set_irq(s->irqs[5], (ints & 0x00ffff00)?1:0);
}

static uint32_t
gpio_con_to_mask(uint32_t con)
{
    uint32_t mask = 0x0;
    int bit;

    for (bit = 0; bit < 16; bit++) {
        if (((con >> (bit * 2)) & 0x3) == 0x01) {
            mask |= 1 << bit;
        }
    }

    return mask;
}

static void
s3c24xx_gpio_write_f(void *opaque, hwaddr addr_, uint64_t value,
                     unsigned size)
{
    S3C24xxGpioState *s = opaque;
    uint32_t addr = (addr_ >> 2);

    assert(addr < S3C_GPIO_MAX);

    assert(!(addr > 0x3f));
    addr &= 0x3f;

    if (addr == (S3C_GPIO_EINT_MASK>>2)) {
        value &= ~0xf; /* cannot mask EINT0-EINT3 */
    }

    if (addr == (S3C_GPIO_EINT_PEND>>2)) {
        s->gpio_reg[addr] &= ~value;
    } else {
        if (addr < (0x80/4) && (addr_ & 0xf) == 0x04) {
            uint32_t mask = gpio_con_to_mask(s->gpio_reg[addr - 1]);

            value &= mask;

            s->gpio_reg[addr] &= ~mask;
            s->gpio_reg[addr] |= value;
        } else {
            s->gpio_reg[addr] = value;
        }
    }

    if ((addr == (S3C_GPIO_EINT_MASK)>>2) ||
        (addr == (S3C_GPIO_EINT_PEND)>>2)) {
        /* A write to the EINT regs leads us to determine the interrupts to
         * propagate
         */
        s3c24xx_gpio_propogate_eint(s);
    }
}

static uint64_t
s3c24xx_gpio_read_f(void *opaque, hwaddr addr_, unsigned size)
{
    S3C24xxGpioState *s = opaque;
    uint32_t addr = (addr_ >> 2);
    uint32_t ret;

    assert(addr < S3C_GPIO_MAX);

    assert(!(addr > 0x3f));
    addr &= 0x3f;

    ret = s->gpio_reg[addr];

    if (addr == GPRN(S3C_GPIO_GPEDAT)) {
        /* IIC pins are special function pins on GPE14 and GPE15. If GPE is is
         * in input mode make the IIC lines appear to be pulled high. This is
         * neccissary because OS i2c drivers use this to ensure the I2C bus is
         * clear.
         */
        if ((GPR(S3C_GPIO_GPECON) & (3<<28)) == 0) {
            ret |= 1 << 14;
        }

        if ((GPR(S3C_GPIO_GPECON) & (3<<30)) == 0) {
            ret |= 1 << 15;
        }
    }

    return ret;
}

static const MemoryRegionOps s3c24xx_gpio_ops = {
    .read = s3c24xx_gpio_read_f,
    .write = s3c24xx_gpio_write_f,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4
    }
};

static void
s3c24xx_gpio_irq_handler(void *opaque, int n, int level)
{
    S3C24xxGpioState *s = opaque;

    if (level) {
        GPR(S3C_GPIO_EINT_PEND) |= (1<<n);
    }

    s3c24xx_gpio_propogate_eint(s);
}

static int s3c24xx_gpio_init_(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    S3C24xxGpioState *s = S3C24XX_GPIO(dev);

    //~ qdev_init_gpio_in(&dev->qdev, mv88w8618_pic_set_irq, 32);
    //~ sysbus_init_irq(dev, &s->parent_irq);
    memory_region_init_io(&s->mmio, OBJECT(s), &s3c24xx_gpio_ops, s,
                          "s3c24xx-gpio", S3C_GPIO_MAX * 4);
    sysbus_init_mmio(sbd, &s->mmio);
#if 0
    TODO: i/o starting at base_addr, S3C_GPIO_MAX * 4 bytes.
#endif

    /* Set non zero default values. */
    GPR(0x00) = 0x7fffff;
    GPR(0x34) = 0xfefc;
    GPR(0x38) = 0xf000;
    GPR(0x68) = 0xf800;
    GPR(0x80) = 0x10330;
    GPR(S3C_GPIO_EINT_MASK) = 0xfffff0;
    //~ GPR(S3C_GPIO_GSTATUS1) = cpu_id;
    GPR(S3C_GPIO_GSTATUS2) = 1;
    GPR(S3C_GPIO_GSTATUS3) = 0;
    GPR(S3C_GPIO_GSTATUS4) = 0;

    return 0;
}

S3C24xxGpioState *
s3c24xx_gpio_init(S3CState *soc, hwaddr base_addr, uint32_t cpu_id)
{
    /* Samsung S3C24XX GPIO
     *
     * The primary operation here is the ID register and IRQs
     */
    int i;

    S3C24xxGpioState *s = g_new0(S3C24xxGpioState, 1);

    /* TODO: Diese Funktion ist veraltet und soll ersetzt werden, s.o. */

    /* Set non zero default values. */
    GPR(0x00) = 0x7fffff;
    GPR(0x34) = 0xfefc;
    GPR(0x38) = 0xf000;
    GPR(0x68) = 0xf800;
    GPR(0x80) = 0x10330;
    GPR(S3C_GPIO_EINT_MASK) = 0xfffff0;
    GPR(S3C_GPIO_GSTATUS1) = cpu_id;
    GPR(S3C_GPIO_GSTATUS2) = 1;
    GPR(S3C_GPIO_GSTATUS3) = 0;
    GPR(S3C_GPIO_GSTATUS4) = 0;

    /* obtain first level IRQs for cascade */
    for (i = 0; i <= 5; i++) {
        s->irqs[i] = s3c24xx_get_irq(soc->irq, i);
    }

    /* EINTs 0-23 -- Only 24, not 48 because EINTs are not level */
    s->eirqs = qemu_allocate_irqs(s3c24xx_gpio_irq_handler, s, 24);

    return s;
}

/* get the qemu interrupt from an eirq number */
qemu_irq
s3c24xx_get_eirq(S3C24xxGpioState *s, unsigned einum)
{
    assert(einum < 24);
    return s->eirqs[einum];
}

static const VMStateDescription s3c24xx_gpio_vmstate = {
    .name = TYPE_S3C24XX_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(gpio_reg, S3C24xxGpioState, S3C_GPIO_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static Property s3c24xx_gpio_properties[] = {
    DEFINE_PROP_END_OF_LIST()
};

static void s3c24xx_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    dc->props = s3c24xx_gpio_properties;
    dc->vmsd = &s3c24xx_gpio_vmstate;
    k->init = s3c24xx_gpio_init_;
}

static const TypeInfo s3c24xx_gpio_info = {
    .name = TYPE_S3C24XX_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S3C24xxGpioState),
    .class_init = s3c24xx_gpio_class_init
};

static void s3c24xx_register_types(void)
{
    type_register_static(&s3c24xx_gpio_info);
}

type_init(s3c24xx_register_types)
