/* hw/s3c24xx_gpio.c
 *
 * Samsung S3C24XX GPIO emulation (mostly for E-INT)
 *
 * Copyright 2006, 2007 Daniel Silverstone and Vincent Sanders
 *
 * This file is under the terms of the GNU General Public
 * License Version 2
 */

#include "hw.h"
#include "sysbus.h"
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
typedef struct s3c24xx_gpio_state_s {
    SysBusDevice busdev;
    MemoryRegion mmio;

    uint32_t gpio_reg[S3C_GPIO_MAX];

    qemu_irq *eirqs; /* gpio external interrupts */

    qemu_irq irqs[6]; /* cpu irqs to cascade */
} S3C24xxGpioState;

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
s3c24xx_gpio_write_f(void *opaque, target_phys_addr_t addr_, uint64_t value,
                     unsigned size)
{
    S3C24xxGpioState *s = opaque;
    uint32_t addr = (addr_ >> 2) & 0x3f;

    assert(addr < S3C_GPIO_MAX);

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
s3c24xx_gpio_read_f(void *opaque, target_phys_addr_t addr_, unsigned size)
{
    S3C24xxGpioState *s = opaque;
    uint32_t addr = (addr_ >> 2);
    uint32_t ret;

    assert(addr < S3C_GPIO_MAX);

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
        .min_access_size = 4,
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

static int s3c24xx_gpio_init_(SysBusDevice *dev)
{
    S3C24xxGpioState *s = FROM_SYSBUS(S3C24xxGpioState, dev);

    //~ qdev_init_gpio_in(&dev->qdev, mv88w8618_pic_set_irq, 32);
    //~ sysbus_init_irq(dev, &s->parent_irq);
    memory_region_init_io(&s->mmio, &s3c24xx_gpio_ops, s,
                          "s3c24xx-gpio", S3C_GPIO_MAX * 4);
    sysbus_init_mmio(dev, &s->mmio);

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

struct s3c24xx_gpio_state_s *
s3c24xx_gpio_init(S3CState *soc, target_phys_addr_t base_addr, uint32_t cpu_id)
{
    /* Samsung S3C24XX GPIO
     *
     * The primary operation here is the ID register and IRQs
     */
    struct s3c24xx_gpio_state_s *s;
    int i;

    s = g_malloc0(sizeof(S3C24xxGpioState));
    if (!s) {
        return NULL;
    }

#if 0
    memory_region_init_io(&s->mmio, &s3c24xx_gpio_ops, s,
                          "s3c24xx-gpio", S3C_GPIO_MAX * 4);
    cpu_register_physical_memory(base_addr, S3C_GPIO_MAX * 4, tag);
#endif

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
s3c24xx_get_eirq(struct s3c24xx_gpio_state_s *s, unsigned einum)
{
    assert(einum < 24);
    return s->eirqs[einum];
}

static const VMStateDescription s3c24xx_gpio_vmstate = {
    .name = "s3c24xx_gpio",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(gpio_reg, S3C24xxGpioState, S3C_GPIO_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static SysBusDeviceInfo s3c24xx_gpio_info = {
    .init = s3c24xx_gpio_init_,
    .qdev.name  = "s3c24xx_gpio",
    .qdev.size  = sizeof(S3C24xxGpioState),
    .qdev.vmsd = &s3c24xx_gpio_vmstate,
    .qdev.props = (Property[]) {
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void s3c24xx_register(void)
{
    sysbus_register_withprop(&s3c24xx_gpio_info);
}

device_init(s3c24xx_register)
