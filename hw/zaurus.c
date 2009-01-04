/*
 * Copyright (c) 2006-2008 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "hw.h"
#include "pxa.h"
#include "sharpsl.h"

#undef REG_FMT
#if TARGET_PHYS_ADDR_BITS == 32
#define REG_FMT			"0x%02x"
#else
#define REG_FMT			"0x%02lx"
#endif

/* SCOOP devices */

struct scoop_info_s {
    qemu_irq handler[16];
    qemu_irq *in;
    uint16_t status;
    uint16_t power;
    uint32_t gpio_level;
    uint32_t gpio_dir;
    uint32_t prev_level;

    uint16_t mcr;
    uint16_t cdr;
    uint16_t ccr;
    uint16_t irr;
    uint16_t imr;
    uint16_t isr;
};

#define SCOOP_MCR	0x00
#define SCOOP_CDR	0x04
#define SCOOP_CSR	0x08
#define SCOOP_CPR	0x0c
#define SCOOP_CCR	0x10
#define SCOOP_IRR_IRM	0x14
#define SCOOP_IMR	0x18
#define SCOOP_ISR	0x1c
#define SCOOP_GPCR	0x20
#define SCOOP_GPWR	0x24
#define SCOOP_GPRR	0x28

static inline void scoop_gpio_handler_update(struct scoop_info_s *s) {
    uint32_t level, diff;
    int bit;
    level = s->gpio_level & s->gpio_dir;

    for (diff = s->prev_level ^ level; diff; diff ^= 1 << bit) {
        bit = ffs(diff) - 1;
        qemu_set_irq(s->handler[bit], (level >> bit) & 1);
    }

    s->prev_level = level;
}

static uint32_t scoop_readb(void *opaque, target_phys_addr_t addr)
{
    struct scoop_info_s *s = (struct scoop_info_s *) opaque;

    switch (addr) {
    case SCOOP_MCR:
        return s->mcr;
    case SCOOP_CDR:
        return s->cdr;
    case SCOOP_CSR:
        return s->status;
    case SCOOP_CPR:
        return s->power;
    case SCOOP_CCR:
        return s->ccr;
    case SCOOP_IRR_IRM:
        return s->irr;
    case SCOOP_IMR:
        return s->imr;
    case SCOOP_ISR:
        return s->isr;
    case SCOOP_GPCR:
        return s->gpio_dir;
    case SCOOP_GPWR:
    case SCOOP_GPRR:
        return s->gpio_level;
    default:
        zaurus_printf("Bad register offset " REG_FMT "\n", addr);
    }

    return 0;
}

static void scoop_writeb(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    struct scoop_info_s *s = (struct scoop_info_s *) opaque;
    value &= 0xffff;

    switch (addr) {
    case SCOOP_MCR:
        s->mcr = value;
        break;
    case SCOOP_CDR:
        s->cdr = value;
        break;
    case SCOOP_CPR:
        s->power = value;
        if (value & 0x80)
            s->power |= 0x8040;
        break;
    case SCOOP_CCR:
        s->ccr = value;
        break;
    case SCOOP_IRR_IRM:
        s->irr = value;
        break;
    case SCOOP_IMR:
        s->imr = value;
        break;
    case SCOOP_ISR:
        s->isr = value;
        break;
    case SCOOP_GPCR:
        s->gpio_dir = value;
        scoop_gpio_handler_update(s);
        break;
    case SCOOP_GPWR:
    case SCOOP_GPRR:	/* GPRR is probably R/O in real HW */
        s->gpio_level = value & s->gpio_dir;
        scoop_gpio_handler_update(s);
        break;
    default:
        zaurus_printf("Bad register offset " REG_FMT "\n", addr);
    }
}

static CPUReadMemoryFunc *scoop_readfn[] = {
    scoop_readb,
    scoop_readb,
    scoop_readb,
};
static CPUWriteMemoryFunc *scoop_writefn[] = {
    scoop_writeb,
    scoop_writeb,
    scoop_writeb,
};

void scoop_gpio_set(void *opaque, int line, int level)
{
    struct scoop_info_s *s = (struct scoop_info_s *) s;

    if (level)
        s->gpio_level |= (1 << line);
    else
        s->gpio_level &= ~(1 << line);
}

qemu_irq *scoop_gpio_in_get(struct scoop_info_s *s)
{
    return s->in;
}

void scoop_gpio_out_set(struct scoop_info_s *s, int line,
                qemu_irq handler) {
    if (line >= 16) {
        fprintf(stderr, "No GPIO pin %i\n", line);
        exit(-1);
    }

    s->handler[line] = handler;
}

static void scoop_save(QEMUFile *f, void *opaque)
{
    struct scoop_info_s *s = (struct scoop_info_s *) opaque;
    qemu_put_be16s(f, &s->status);
    qemu_put_be16s(f, &s->power);
    qemu_put_be32s(f, &s->gpio_level);
    qemu_put_be32s(f, &s->gpio_dir);
    qemu_put_be32s(f, &s->prev_level);
    qemu_put_be16s(f, &s->mcr);
    qemu_put_be16s(f, &s->cdr);
    qemu_put_be16s(f, &s->ccr);
    qemu_put_be16s(f, &s->irr);
    qemu_put_be16s(f, &s->imr);
    qemu_put_be16s(f, &s->isr);
}

static int scoop_load(QEMUFile *f, void *opaque, int version_id)
{
    uint16_t dummy;
    struct scoop_info_s *s = (struct scoop_info_s *) opaque;
    qemu_get_be16s(f, &s->status);
    qemu_get_be16s(f, &s->power);
    qemu_get_be32s(f, &s->gpio_level);
    qemu_get_be32s(f, &s->gpio_dir);
    qemu_get_be32s(f, &s->prev_level);
    qemu_get_be16s(f, &s->mcr);
    qemu_get_be16s(f, &s->cdr);
    qemu_get_be16s(f, &s->ccr);
    qemu_get_be16s(f, &s->irr);
    qemu_get_be16s(f, &s->imr);
    qemu_get_be16s(f, &s->isr);
    if (version_id < 1)
	    qemu_get_be16s(f, &dummy);

    return 0;
}

struct scoop_info_s *scoop_init(struct pxa2xx_state_s *cpu,
		int instance,
		target_phys_addr_t target_base) {
    int iomemtype;
    struct scoop_info_s *s;

    s = (struct scoop_info_s *)
            qemu_mallocz(sizeof(struct scoop_info_s));
    memset(s, 0, sizeof(struct scoop_info_s));

    s->status = 0x02;
    s->in = qemu_allocate_irqs(scoop_gpio_set, s, 16);
    iomemtype = cpu_register_io_memory(0, scoop_readfn,
                    scoop_writefn, s);
    cpu_register_physical_memory(target_base, 0x1000, iomemtype);
    register_savevm("scoop", instance, 1, scoop_save, scoop_load, s);

    return s;
}

/* Write the bootloader parameters memory area.  */

#define MAGIC_CHG(a, b, c, d)	((d << 24) | (c << 16) | (b << 8) | a)

static struct __attribute__ ((__packed__)) sl_param_info {
    uint32_t comadj_keyword;
    int32_t comadj;

    uint32_t uuid_keyword;
    char uuid[16];

    uint32_t touch_keyword;
    int32_t touch_xp;
    int32_t touch_yp;
    int32_t touch_xd;
    int32_t touch_yd;

    uint32_t adadj_keyword;
    int32_t adadj;

    uint32_t phad_keyword;
    int32_t phadadj;
} zaurus_bootparam = {
    .comadj_keyword	= MAGIC_CHG('C', 'M', 'A', 'D'),
    .comadj		= 125,
    .uuid_keyword	= MAGIC_CHG('U', 'U', 'I', 'D'),
    .uuid		= { -1 },
    .touch_keyword	= MAGIC_CHG('T', 'U', 'C', 'H'),
    .touch_xp		= -1,
    .adadj_keyword	= MAGIC_CHG('B', 'V', 'A', 'D'),
    .adadj		= -1,
    .phad_keyword	= MAGIC_CHG('P', 'H', 'A', 'D'),
    .phadadj		= 0x01,
};

void sl_bootparam_write(uint32_t ptr)
{
    memcpy(phys_ram_base + ptr, &zaurus_bootparam,
                    sizeof(struct sl_param_info));
}
