/*
 * imx23_pinctrl.c
 *
 * Copyright: Michel Pollet <buserror@gmail.com>
 *
 * QEMU Licence
 */

/*
 * Implements the pinctrl and GPIO block for the imx23
 * It handles GPIO output, and GPIO input from qemu translated
 * into pin values and interrupts, if appropriate.
 */
#include "hw/sysbus.h"
#include "hw/arm/mxs.h"

#define D(w)

enum {
    PINCTRL_BANK_COUNT = 3,

    PINCTRL_CTRL = 0,
    PINCTRL_BANK_MUXSEL = 0x10,
    PINCTRL_BANK_BASE = 0x40,

    /* these are not << 4 register numbers, these are << 8 register numbers */
    PINCTRL_BANK_PULL = 0x4,
    PINCTRL_BANK_OUT = 0x5,
    PINCTRL_BANK_DIN = 0x6,
    PINCTRL_BANK_DOE = 0x7,
    PINCTRL_BANK_PIN2IRQ = 0x8,
    PINCTRL_BANK_IRQEN = 0x9,
    PINCTRL_BANK_IRQLEVEL = 0xa,
    PINCTRL_BANK_IRQPOL = 0xb,
    PINCTRL_BANK_IRQSTAT = 0xc,

    PINCTRL_BANK_INTERNAL_STATE = 0xd,
    PINCTRL_MAX = 0xe0,
};

#define PINCTRL_BANK_REG(_bank, _reg) ((_reg << 8) | (_bank << 4))

enum {
    MUX_GPIO = 0x3,
};


typedef struct imx23_pinctrl_state {
    SysBusDevice busdev;
    MemoryRegion iomem;

    uint32_t r[PINCTRL_MAX];
    qemu_irq irq_in[3];
    qemu_irq irq_out[PINCTRL_BANK_COUNT * 32];

    uint32_t state[PINCTRL_BANK_COUNT];
} imx23_pinctrl_state;

static uint64_t imx23_pinctrl_read(
        void *opaque, hwaddr offset, unsigned size)
{
    imx23_pinctrl_state *s = (imx23_pinctrl_state *) opaque;
    uint32_t res = 0;

    switch (offset >> 4) {
        case 0 ... PINCTRL_MAX:
            res = s->r[offset >> 4];
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: bad offset 0x%x\n", __func__, (int) offset);
            break;
    }

    return res;
}

static uint8_t imx23_pinctrl_getmux(
        imx23_pinctrl_state *s, int pin)
{
    int base = pin / 16, offset = pin % 16;
    return (s->r[PINCTRL_BANK_MUXSEL + base] >> (offset * 2)) & 0x3;
}

/*
 * usage imx23_pinctrl_getbit(s, PINCTRL_BANK_IRQEN, 48)...
 */
static uint8_t imx23_pinctrl_getbit(
        imx23_pinctrl_state *s, uint16_t reg, int pin)
{
    int bank = pin / 32, offset = pin % 32;
    uint32_t * latch = &s->r[PINCTRL_BANK_REG(bank, reg) >> 4];
//    printf("%s bank %d offset %d reg %d : %04x=%08x\n", __func__, bank, offset, reg,
//            PINCTRL_BANK_REG(bank, reg),
//            *latch);
    return (*latch >> offset) & 0x1;
}

static void imx23_pinctrl_setbit(
        imx23_pinctrl_state *s, uint16_t reg, int pin, int value)
{
    int bank = pin / 32, offset = pin % 32;
    uint32_t * latch = &s->r[PINCTRL_BANK_REG(bank, reg) >> 4];
    *latch = (*latch & ~(1 << offset)) | (!!value << offset);
}

static void imx23_pinctrl_write_bank(
        imx23_pinctrl_state *s, int bank,
        int reg, uint32_t value,
        uint32_t mask)
{
    int set, pin;
    switch (reg) {
        /*
         * Linux has a way of using the DOE&PULL register to toggle the pin
         */
        case PINCTRL_BANK_PULL:
        case PINCTRL_BANK_DOE:
        /*
         * Writing to the Data OUT register just triggers the
         * output qemu IRQ for any further peripherals
         */
        case PINCTRL_BANK_OUT: {
            while ((set = ffs(mask)) > 0) {
                set--;
                mask &= ~(1 << set);
                pin = (bank * 32) + set;
                /*
                 * For a reason that is not clear, the pullup bit appears
                 * inverted (!) ignoring for now, assume hardware pullup
                 */
                // imx23_pinctrl_getbit(s, PINCTRL_BANK_PULL, pin)
                int val =
                        imx23_pinctrl_getbit(s, PINCTRL_BANK_DOE, pin) ?
                                imx23_pinctrl_getbit(s, PINCTRL_BANK_OUT, pin) :
                                1;
                D(printf("%s set %2d to OUT:%d DOE:%d (PULL:%d) = %d\n", __func__,
                        pin,
                        imx23_pinctrl_getbit(s, PINCTRL_BANK_OUT, pin),
                        imx23_pinctrl_getbit(s, PINCTRL_BANK_DOE, pin),
                        imx23_pinctrl_getbit(s, PINCTRL_BANK_PULL, pin),
                        val);)

                if (imx23_pinctrl_getbit(s, PINCTRL_BANK_INTERNAL_STATE, pin) != val) {
                    qemu_set_irq(s->irq_out[pin], val);
                    imx23_pinctrl_setbit(s, PINCTRL_BANK_INTERNAL_STATE, pin, val);
                }
            }
        }   break;
        /*
         * This happends when we receive a qemu IRQ on the input ones,
         * the register gets updated by the code executed until now,
         * and all we need to do here is to trigger the imx233 IRQ
         * if appropriate.
         * Doing a write to these registers from guest code will acts as
         * a software interrupt, not entirely sure this is appropriate
         */
        case PINCTRL_BANK_DIN: {
            while ((set = ffs(mask)) > 0) {
                set--;
                mask &= ~(1 << set);
                pin = (bank * 32) + set;
                D(printf("%s input %2d set to %d\n", __func__,
                        pin, !!(value & (1 << set)));)
                // its it a GPIO ?
                if (imx23_pinctrl_getmux(s, pin) != MUX_GPIO) {
                    break;
                }
                // if the new value matches the polarity bit, it's the
                // edge the guy wanted.
                int valid_irq = ((value >> set) & 1) ==
                        imx23_pinctrl_getbit(s, PINCTRL_BANK_IRQPOL, pin);
                if (valid_irq) {
                    if (imx23_pinctrl_getbit(s, PINCTRL_BANK_PIN2IRQ, pin)) {
                        imx23_pinctrl_setbit(s, PINCTRL_BANK_IRQSTAT, pin, 1);
                    }
                    // is the interrupt enabled?
                    if (imx23_pinctrl_getbit(s, PINCTRL_BANK_IRQEN, pin)) {
                        qemu_irq_raise(s->irq_in[bank]);
                    }
                }
            }
        }   break;
    }
}

static void imx23_pinctrl_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
    imx23_pinctrl_state *s = (imx23_pinctrl_state *) opaque;
    uint32_t oldvalue = 0;

//    printf("%s offset %04x value %08x\n", __func__, (int)offset, (int)value);
    switch (offset >> 4) {
        case 0 ... PINCTRL_MAX:
            oldvalue = mxs_write(&s->r[offset >> 4], offset, value, size);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: bad offset 0x%x\n", __func__, (int) offset);
            break;
    }
    switch (offset >> 4) {
        case PINCTRL_CTRL:
            if ((oldvalue ^ s->r[PINCTRL_CTRL]) == 0x80000000
                    && !(oldvalue & 0x80000000)) {
             //   printf("%s reseting, anding clockgate\n", __func__);
                s->r[PINCTRL_CTRL] |= 0x40000000;
            }
            break;
        case PINCTRL_BANK_BASE ... PINCTRL_MAX: {
            int bank = (offset >> 4) & 0xf;
            int reg = (offset >> 8);
            uint32_t mask = oldvalue ^ s->r[offset >> 4];
         //   printf("%s b %d r %x input %08x mask %08x value %08x\n",
         //           __func__, bank, reg, (int)value, mask, s->r[offset >> 4]);
            imx23_pinctrl_write_bank(s, bank, reg,
                    s->r[offset >> 4], mask);
        }   break;
    }
}

/*
 * This will interfaces qemu other components back to the guest input pins
 */
static void imx23_pinctrl_set_irq(void *opaque, int irq, int level)
{
    // simulate a write to the data IN address
    int bank = irq / 32;
    hwaddr offset =
            PINCTRL_BANK_REG(bank, PINCTRL_BANK_DIN);
    uint64_t value = (1 << (irq % 32));
    D(printf("%s %d = %d\n", __func__, irq, level);)
    imx23_pinctrl_write(opaque,
            offset + (level ? 0x4 : 0x8),
            value, 4);
}

static const MemoryRegionOps imx23_pinctrl_ops = {
    .read = imx23_pinctrl_read,
    .write = imx23_pinctrl_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int imx23_pinctrl_init(SysBusDevice *dev)
{
    imx23_pinctrl_state *s = OBJECT_CHECK(imx23_pinctrl_state, dev, "imx23_pinctrl");
    int i;
    DeviceState *qdev = DEVICE(dev);

    // NEEDED for qdev_find_recursive to work
    qdev->id = "imx23_pinctrl";
    qdev_init_gpio_in(qdev, imx23_pinctrl_set_irq, 32 * PINCTRL_BANK_COUNT);
    qdev_init_gpio_out(qdev, s->irq_out, ARRAY_SIZE(s->irq_out));
    memory_region_init_io(&s->iomem, OBJECT(s), &imx23_pinctrl_ops, s,
            "imx23_pinctrl", 0x2000);
    sysbus_init_mmio(dev, &s->iomem);

    for (i = 0; i < PINCTRL_BANK_COUNT; i++) {
        sysbus_init_irq(dev, &s->irq_in[i]);
        s->r[PINCTRL_BANK_REG(i, PINCTRL_BANK_DIN) >> 4] = 0;
        s->r[PINCTRL_BANK_REG(i, PINCTRL_BANK_PULL) >> 4] = 0xffffffff;
    }
    /* set default mux values */
    for (i = 0; i < 8; i++)
        s->r[(0x100 >> 4) + i] = 0x33333333;

    s->r[PINCTRL_CTRL] = 0xcf000000;

    return 0;
}


static void imx23_pinctrl_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = imx23_pinctrl_init;
}

static TypeInfo pinctrl_info = {
    .name          = "imx23_pinctrl",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(imx23_pinctrl_state),
    .class_init    = imx23_pinctrl_class_init,
};

static void imx23_pinctrl_register(void)
{
    type_register_static(&pinctrl_info);
}

type_init(imx23_pinctrl_register)
