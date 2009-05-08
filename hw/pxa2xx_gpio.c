/*
 * Intel XScale PXA255/270 GPIO controller emulation.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GPL.
 */

#include "hw.h"
#include "pxa.h"

#define PXA2XX_GPIO_BANKS	4

struct pxa2xx_gpio_info_s {
    qemu_irq *pic;
    int lines;
    CPUState *cpu_env;
    qemu_irq *in;

    /* XXX: GNU C vectors are more suitable */
    uint32_t ilevel[PXA2XX_GPIO_BANKS];
    uint32_t olevel[PXA2XX_GPIO_BANKS];
    uint32_t dir[PXA2XX_GPIO_BANKS];
    uint32_t rising[PXA2XX_GPIO_BANKS];
    uint32_t falling[PXA2XX_GPIO_BANKS];
    uint32_t status[PXA2XX_GPIO_BANKS];
    uint32_t gpsr[PXA2XX_GPIO_BANKS];
    uint32_t gafr[PXA2XX_GPIO_BANKS * 2];

    uint32_t prev_level[PXA2XX_GPIO_BANKS];
    qemu_irq handler[PXA2XX_GPIO_BANKS * 32];
    qemu_irq read_notify;
};

static struct {
    enum {
        GPIO_NONE,
        GPLR,
        GPSR,
        GPCR,
        GPDR,
        GRER,
        GFER,
        GEDR,
        GAFR_L,
        GAFR_U,
    } reg;
    int bank;
} pxa2xx_gpio_regs[0x200] = {
    [0 ... 0x1ff] = { GPIO_NONE, 0 },
#define PXA2XX_REG(reg, a0, a1, a2, a3)	\
    [a0] = { reg, 0 }, [a1] = { reg, 1 }, [a2] = { reg, 2 }, [a3] = { reg, 3 },

    PXA2XX_REG(GPLR, 0x000, 0x004, 0x008, 0x100)
    PXA2XX_REG(GPSR, 0x018, 0x01c, 0x020, 0x118)
    PXA2XX_REG(GPCR, 0x024, 0x028, 0x02c, 0x124)
    PXA2XX_REG(GPDR, 0x00c, 0x010, 0x014, 0x10c)
    PXA2XX_REG(GRER, 0x030, 0x034, 0x038, 0x130)
    PXA2XX_REG(GFER, 0x03c, 0x040, 0x044, 0x13c)
    PXA2XX_REG(GEDR, 0x048, 0x04c, 0x050, 0x148)
    PXA2XX_REG(GAFR_L, 0x054, 0x05c, 0x064, 0x06c)
    PXA2XX_REG(GAFR_U, 0x058, 0x060, 0x068, 0x070)
};

static void pxa2xx_gpio_irq_update(struct pxa2xx_gpio_info_s *s)
{
    if (s->status[0] & (1 << 0))
        qemu_irq_raise(s->pic[PXA2XX_PIC_GPIO_0]);
    else
        qemu_irq_lower(s->pic[PXA2XX_PIC_GPIO_0]);

    if (s->status[0] & (1 << 1))
        qemu_irq_raise(s->pic[PXA2XX_PIC_GPIO_1]);
    else
        qemu_irq_lower(s->pic[PXA2XX_PIC_GPIO_1]);

    if ((s->status[0] & ~3) | s->status[1] | s->status[2] | s->status[3])
        qemu_irq_raise(s->pic[PXA2XX_PIC_GPIO_X]);
    else
        qemu_irq_lower(s->pic[PXA2XX_PIC_GPIO_X]);
}

/* Bitmap of pins used as standby and sleep wake-up sources.  */
static const int pxa2xx_gpio_wake[PXA2XX_GPIO_BANKS] = {
    0x8003fe1b, 0x002001fc, 0xec080000, 0x0012007f,
};

static void pxa2xx_gpio_set(void *opaque, int line, int level)
{
    struct pxa2xx_gpio_info_s *s = (struct pxa2xx_gpio_info_s *) opaque;
    int bank;
    uint32_t mask;

    if (line >= s->lines) {
        printf("%s: No GPIO pin %i\n", __FUNCTION__, line);
        return;
    }

    bank = line >> 5;
    mask = 1 << (line & 31);

    if (level) {
        s->status[bank] |= s->rising[bank] & mask &
                ~s->ilevel[bank] & ~s->dir[bank];
        s->ilevel[bank] |= mask;
    } else {
        s->status[bank] |= s->falling[bank] & mask &
                s->ilevel[bank] & ~s->dir[bank];
        s->ilevel[bank] &= ~mask;
    }

    if (s->status[bank] & mask)
        pxa2xx_gpio_irq_update(s);

    /* Wake-up GPIOs */
    if (s->cpu_env->halted && (mask & ~s->dir[bank] & pxa2xx_gpio_wake[bank]))
        cpu_interrupt(s->cpu_env, CPU_INTERRUPT_EXITTB);
}

static void pxa2xx_gpio_handler_update(struct pxa2xx_gpio_info_s *s) {
    uint32_t level, diff;
    int i, bit, line;
    for (i = 0; i < PXA2XX_GPIO_BANKS; i ++) {
        level = s->olevel[i] & s->dir[i];

        for (diff = s->prev_level[i] ^ level; diff; diff ^= 1 << bit) {
            bit = ffs(diff) - 1;
            line = bit + 32 * i;
            qemu_set_irq(s->handler[line], (level >> bit) & 1);
        }

        s->prev_level[i] = level;
    }
}

static uint32_t pxa2xx_gpio_read(void *opaque, target_phys_addr_t offset)
{
    struct pxa2xx_gpio_info_s *s = (struct pxa2xx_gpio_info_s *) opaque;
    uint32_t ret;
    int bank;
    if (offset >= 0x200)
        return 0;

    bank = pxa2xx_gpio_regs[offset].bank;
    switch (pxa2xx_gpio_regs[offset].reg) {
    case GPDR:		/* GPIO Pin-Direction registers */
        return s->dir[bank];

    case GPSR:		/* GPIO Pin-Output Set registers */
        printf("%s: Read from a write-only register " REG_FMT "\n",
                        __FUNCTION__, offset);
        return s->gpsr[bank];	/* Return last written value.  */

    case GPCR:		/* GPIO Pin-Output Clear registers */
        printf("%s: Read from a write-only register " REG_FMT "\n",
                        __FUNCTION__, offset);
        return 31337;		/* Specified as unpredictable in the docs.  */

    case GRER:		/* GPIO Rising-Edge Detect Enable registers */
        return s->rising[bank];

    case GFER:		/* GPIO Falling-Edge Detect Enable registers */
        return s->falling[bank];

    case GAFR_L:	/* GPIO Alternate Function registers */
        return s->gafr[bank * 2];

    case GAFR_U:	/* GPIO Alternate Function registers */
        return s->gafr[bank * 2 + 1];

    case GPLR:		/* GPIO Pin-Level registers */
        ret = (s->olevel[bank] & s->dir[bank]) |
                (s->ilevel[bank] & ~s->dir[bank]);
        qemu_irq_raise(s->read_notify);
        return ret;

    case GEDR:		/* GPIO Edge Detect Status registers */
        return s->status[bank];

    default:
        hw_error("%s: Bad offset " REG_FMT "\n", __FUNCTION__, offset);
    }

    return 0;
}

static void pxa2xx_gpio_write(void *opaque,
                target_phys_addr_t offset, uint32_t value)
{
    struct pxa2xx_gpio_info_s *s = (struct pxa2xx_gpio_info_s *) opaque;
    int bank;
    if (offset >= 0x200)
        return;

    bank = pxa2xx_gpio_regs[offset].bank;
    switch (pxa2xx_gpio_regs[offset].reg) {
    case GPDR:		/* GPIO Pin-Direction registers */
        s->dir[bank] = value;
        pxa2xx_gpio_handler_update(s);
        break;

    case GPSR:		/* GPIO Pin-Output Set registers */
        s->olevel[bank] |= value;
        pxa2xx_gpio_handler_update(s);
        s->gpsr[bank] = value;
        break;

    case GPCR:		/* GPIO Pin-Output Clear registers */
        s->olevel[bank] &= ~value;
        pxa2xx_gpio_handler_update(s);
        break;

    case GRER:		/* GPIO Rising-Edge Detect Enable registers */
        s->rising[bank] = value;
        break;

    case GFER:		/* GPIO Falling-Edge Detect Enable registers */
        s->falling[bank] = value;
        break;

    case GAFR_L:	/* GPIO Alternate Function registers */
        s->gafr[bank * 2] = value;
        break;

    case GAFR_U:	/* GPIO Alternate Function registers */
        s->gafr[bank * 2 + 1] = value;
        break;

    case GEDR:		/* GPIO Edge Detect Status registers */
        s->status[bank] &= ~value;
        pxa2xx_gpio_irq_update(s);
        break;

    default:
        hw_error("%s: Bad offset " REG_FMT "\n", __FUNCTION__, offset);
    }
}

static CPUReadMemoryFunc *pxa2xx_gpio_readfn[] = {
    pxa2xx_gpio_read,
    pxa2xx_gpio_read,
    pxa2xx_gpio_read
};

static CPUWriteMemoryFunc *pxa2xx_gpio_writefn[] = {
    pxa2xx_gpio_write,
    pxa2xx_gpio_write,
    pxa2xx_gpio_write
};

static void pxa2xx_gpio_save(QEMUFile *f, void *opaque)
{
    struct pxa2xx_gpio_info_s *s = (struct pxa2xx_gpio_info_s *) opaque;
    int i;

    qemu_put_be32(f, s->lines);

    for (i = 0; i < PXA2XX_GPIO_BANKS; i ++) {
        qemu_put_be32s(f, &s->ilevel[i]);
        qemu_put_be32s(f, &s->olevel[i]);
        qemu_put_be32s(f, &s->dir[i]);
        qemu_put_be32s(f, &s->rising[i]);
        qemu_put_be32s(f, &s->falling[i]);
        qemu_put_be32s(f, &s->status[i]);
        qemu_put_be32s(f, &s->gafr[i * 2 + 0]);
        qemu_put_be32s(f, &s->gafr[i * 2 + 1]);

        qemu_put_be32s(f, &s->prev_level[i]);
    }
}

static int pxa2xx_gpio_load(QEMUFile *f, void *opaque, int version_id)
{
    struct pxa2xx_gpio_info_s *s = (struct pxa2xx_gpio_info_s *) opaque;
    int i;

    if (qemu_get_be32(f) != s->lines)
        return -EINVAL;

    for (i = 0; i < PXA2XX_GPIO_BANKS; i ++) {
        qemu_get_be32s(f, &s->ilevel[i]);
        qemu_get_be32s(f, &s->olevel[i]);
        qemu_get_be32s(f, &s->dir[i]);
        qemu_get_be32s(f, &s->rising[i]);
        qemu_get_be32s(f, &s->falling[i]);
        qemu_get_be32s(f, &s->status[i]);
        qemu_get_be32s(f, &s->gafr[i * 2 + 0]);
        qemu_get_be32s(f, &s->gafr[i * 2 + 1]);

        qemu_get_be32s(f, &s->prev_level[i]);
    }

    return 0;
}

struct pxa2xx_gpio_info_s *pxa2xx_gpio_init(target_phys_addr_t base,
                CPUState *env, qemu_irq *pic, int lines)
{
    int iomemtype;
    struct pxa2xx_gpio_info_s *s;

    s = (struct pxa2xx_gpio_info_s *)
            qemu_mallocz(sizeof(struct pxa2xx_gpio_info_s));
    memset(s, 0, sizeof(struct pxa2xx_gpio_info_s));
    s->pic = pic;
    s->lines = lines;
    s->cpu_env = env;
    s->in = qemu_allocate_irqs(pxa2xx_gpio_set, s, lines);

    iomemtype = cpu_register_io_memory(0, pxa2xx_gpio_readfn,
                    pxa2xx_gpio_writefn, s);
    cpu_register_physical_memory(base, 0x00001000, iomemtype);

    register_savevm("pxa2xx_gpio", 0, 0,
                    pxa2xx_gpio_save, pxa2xx_gpio_load, s);

    return s;
}

qemu_irq *pxa2xx_gpio_in_get(struct pxa2xx_gpio_info_s *s)
{
    return s->in;
}

void pxa2xx_gpio_out_set(struct pxa2xx_gpio_info_s *s,
                int line, qemu_irq handler)
{
    if (line >= s->lines) {
        printf("%s: No GPIO pin %i\n", __FUNCTION__, line);
        return;
    }

    s->handler[line] = handler;
}

/*
 * Registers a callback to notify on GPLR reads.  This normally
 * shouldn't be needed but it is used for the hack on Spitz machines.
 */
void pxa2xx_gpio_read_notifier(struct pxa2xx_gpio_info_s *s, qemu_irq handler)
{
    s->read_notify = handler;
}
