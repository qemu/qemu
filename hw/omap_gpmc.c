/*
 * TI OMAP general purpose memory controller emulation.
 *
 * Copyright (C) 2007-2009 Nokia Corporation
 * Original code written by Andrzej Zaborowski <andrew@openedhand.com>
 * Enhancements for OMAP3 and NAND support written by Juha Riihimäki
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) any later version of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "hw.h"
#include "flash.h"
#include "omap.h"
#include "memory.h"
#include "exec-memory.h"

/* General-Purpose Memory Controller */
struct omap_gpmc_s {
    qemu_irq irq;
    MemoryRegion iomem;

    uint8_t sysconfig;
    uint16_t irqst;
    uint16_t irqen;
    uint16_t timeout;
    uint16_t config;
    uint32_t prefconfig[2];
    int prefcontrol;
    int preffifo;
    int prefcount;
    struct omap_gpmc_cs_file_s {
        uint32_t config[7];
        MemoryRegion *iomem;
        MemoryRegion container;
    } cs_file[8];
    int ecc_cs;
    int ecc_ptr;
    uint32_t ecc_cfg;
    ECCState ecc[9];
};

static void omap_gpmc_int_update(struct omap_gpmc_s *s)
{
    qemu_set_irq(s->irq, s->irqen & s->irqst);
}

static void omap_gpmc_cs_map(struct omap_gpmc_s *s, int cs)
{
    struct omap_gpmc_cs_file_s *f = &s->cs_file[cs];
    uint32_t mask = (f->config[6] >> 8) & 0xf;
    uint32_t base = f->config[6] & 0x3f;
    uint32_t size;

    if (!f->iomem) {
        return;
    }

    if (!(f->config[6] & (1 << 6))) {
        /* Do nothing unless CSVALID */
        return;
    }

    /* TODO: check for overlapping regions and report access errors */
    if ((mask != 0x8 && mask != 0xc && mask != 0xe && mask != 0xf) ||
        (base & 0x0f & ~mask)) {
        fprintf(stderr, "%s: wrong cs address mapping/decoding!\n",
                        __FUNCTION__);
        return;
    }

    base <<= 24;
    size = (0x0fffffff & ~(mask << 24)) + 1;
    /* TODO: rather than setting the size of the mapping (which should be
     * constant), the mask should cause wrapping of the address space, so
     * that the same memory becomes accessible at every <i>size</i> bytes
     * starting from <i>base</i>.  */
    memory_region_init(&f->container, "omap-gpmc-file", size);
    memory_region_add_subregion(&f->container, 0, f->iomem);
    memory_region_add_subregion(get_system_memory(), base,
                                &f->container);
}

static void omap_gpmc_cs_unmap(struct omap_gpmc_s *s, int cs)
{
    struct omap_gpmc_cs_file_s *f = &s->cs_file[cs];
    if (!(f->config[6] & (1 << 6))) {
        /* Do nothing unless CSVALID */
        return;
    }
    if (!f->iomem) {
        return;
    }

    memory_region_del_subregion(get_system_memory(), &f->container);
    memory_region_del_subregion(&f->container, f->iomem);
    memory_region_destroy(&f->container);
}

void omap_gpmc_reset(struct omap_gpmc_s *s)
{
    int i;

    s->sysconfig = 0;
    s->irqst = 0;
    s->irqen = 0;
    omap_gpmc_int_update(s);
    s->timeout = 0;
    s->config = 0xa00;
    s->prefconfig[0] = 0x00004000;
    s->prefconfig[1] = 0x00000000;
    s->prefcontrol = 0;
    s->preffifo = 0;
    s->prefcount = 0;
    for (i = 0; i < 8; i ++) {
        omap_gpmc_cs_unmap(s, i);
        s->cs_file[i].config[1] = 0x101001;
        s->cs_file[i].config[2] = 0x020201;
        s->cs_file[i].config[3] = 0x10031003;
        s->cs_file[i].config[4] = 0x10f1111;
        s->cs_file[i].config[5] = 0;
        s->cs_file[i].config[6] = 0xf00 | (i ? 0 : 1 << 6);

        s->cs_file[i].config[6] = 0xf00;
        /* In theory we could probe attached devices for some CFG1
         * bits here, but we just retain them across resets as they
         * were set initially by omap_gpmc_attach().
         */
        if (i == 0) {
            s->cs_file[i].config[0] &= 0x00433e00;
            s->cs_file[i].config[6] |= 1 << 6; /* CSVALID */
            omap_gpmc_cs_map(s, i);
        } else {
            s->cs_file[i].config[0] &= 0x00403c00;
        }
    }
    s->ecc_cs = 0;
    s->ecc_ptr = 0;
    s->ecc_cfg = 0x3fcff000;
    for (i = 0; i < 9; i ++)
        ecc_reset(&s->ecc[i]);
}

static uint64_t omap_gpmc_read(void *opaque, target_phys_addr_t addr,
                               unsigned size)
{
    struct omap_gpmc_s *s = (struct omap_gpmc_s *) opaque;
    int cs;
    struct omap_gpmc_cs_file_s *f;

    if (size != 4) {
        return omap_badwidth_read32(opaque, addr);
    }

    switch (addr) {
    case 0x000:	/* GPMC_REVISION */
        return 0x20;

    case 0x010:	/* GPMC_SYSCONFIG */
        return s->sysconfig;

    case 0x014:	/* GPMC_SYSSTATUS */
        return 1;						/* RESETDONE */

    case 0x018:	/* GPMC_IRQSTATUS */
        return s->irqst;

    case 0x01c:	/* GPMC_IRQENABLE */
        return s->irqen;

    case 0x040:	/* GPMC_TIMEOUT_CONTROL */
        return s->timeout;

    case 0x044:	/* GPMC_ERR_ADDRESS */
    case 0x048:	/* GPMC_ERR_TYPE */
        return 0;

    case 0x050:	/* GPMC_CONFIG */
        return s->config;

    case 0x054:	/* GPMC_STATUS */
        return 0x001;

    case 0x060 ... 0x1d4:
        cs = (addr - 0x060) / 0x30;
        addr -= cs * 0x30;
        f = s->cs_file + cs;
        switch (addr) {
            case 0x60:	/* GPMC_CONFIG1 */
                return f->config[0];
            case 0x64:	/* GPMC_CONFIG2 */
                return f->config[1];
            case 0x68:	/* GPMC_CONFIG3 */
                return f->config[2];
            case 0x6c:	/* GPMC_CONFIG4 */
                return f->config[3];
            case 0x70:	/* GPMC_CONFIG5 */
                return f->config[4];
            case 0x74:	/* GPMC_CONFIG6 */
                return f->config[5];
            case 0x78:	/* GPMC_CONFIG7 */
                return f->config[6];
            case 0x84:	/* GPMC_NAND_DATA */
                return 0;
        }
        break;

    case 0x1e0:	/* GPMC_PREFETCH_CONFIG1 */
        return s->prefconfig[0];
    case 0x1e4:	/* GPMC_PREFETCH_CONFIG2 */
        return s->prefconfig[1];
    case 0x1ec:	/* GPMC_PREFETCH_CONTROL */
        return s->prefcontrol;
    case 0x1f0:	/* GPMC_PREFETCH_STATUS */
        return (s->preffifo << 24) |
                ((s->preffifo >
                  ((s->prefconfig[0] >> 8) & 0x7f) ? 1 : 0) << 16) |
                s->prefcount;

    case 0x1f4:	/* GPMC_ECC_CONFIG */
        return s->ecc_cs;
    case 0x1f8:	/* GPMC_ECC_CONTROL */
        return s->ecc_ptr;
    case 0x1fc:	/* GPMC_ECC_SIZE_CONFIG */
        return s->ecc_cfg;
    case 0x200 ... 0x220:	/* GPMC_ECC_RESULT */
        cs = (addr & 0x1f) >> 2;
        /* TODO: check correctness */
        return
                ((s->ecc[cs].cp    &  0x07) <<  0) |
                ((s->ecc[cs].cp    &  0x38) << 13) |
                ((s->ecc[cs].lp[0] & 0x1ff) <<  3) |
                ((s->ecc[cs].lp[1] & 0x1ff) << 19);

    case 0x230:	/* GPMC_TESTMODE_CTRL */
        return 0;
    case 0x234:	/* GPMC_PSA_LSB */
    case 0x238:	/* GPMC_PSA_MSB */
        return 0x00000000;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_gpmc_write(void *opaque, target_phys_addr_t addr,
                            uint64_t value, unsigned size)
{
    struct omap_gpmc_s *s = (struct omap_gpmc_s *) opaque;
    int cs;
    struct omap_gpmc_cs_file_s *f;

    if (size != 4) {
        return omap_badwidth_write32(opaque, addr, value);
    }

    switch (addr) {
    case 0x000:	/* GPMC_REVISION */
    case 0x014:	/* GPMC_SYSSTATUS */
    case 0x054:	/* GPMC_STATUS */
    case 0x1f0:	/* GPMC_PREFETCH_STATUS */
    case 0x200 ... 0x220:	/* GPMC_ECC_RESULT */
    case 0x234:	/* GPMC_PSA_LSB */
    case 0x238:	/* GPMC_PSA_MSB */
        OMAP_RO_REG(addr);
        break;

    case 0x010:	/* GPMC_SYSCONFIG */
        if ((value >> 3) == 0x3)
            fprintf(stderr, "%s: bad SDRAM idle mode %"PRIi64"\n",
                            __FUNCTION__, value >> 3);
        if (value & 2)
            omap_gpmc_reset(s);
        s->sysconfig = value & 0x19;
        break;

    case 0x018:	/* GPMC_IRQSTATUS */
        s->irqen = ~value;
        omap_gpmc_int_update(s);
        break;

    case 0x01c:	/* GPMC_IRQENABLE */
        s->irqen = value & 0xf03;
        omap_gpmc_int_update(s);
        break;

    case 0x040:	/* GPMC_TIMEOUT_CONTROL */
        s->timeout = value & 0x1ff1;
        break;

    case 0x044:	/* GPMC_ERR_ADDRESS */
    case 0x048:	/* GPMC_ERR_TYPE */
        break;

    case 0x050:	/* GPMC_CONFIG */
        s->config = value & 0xf13;
        break;

    case 0x060 ... 0x1d4:
        cs = (addr - 0x060) / 0x30;
        addr -= cs * 0x30;
        f = s->cs_file + cs;
        switch (addr) {
            case 0x60:	/* GPMC_CONFIG1 */
                f->config[0] = value & 0xffef3e13;
                break;
            case 0x64:	/* GPMC_CONFIG2 */
                f->config[1] = value & 0x001f1f8f;
                break;
            case 0x68:	/* GPMC_CONFIG3 */
                f->config[2] = value & 0x001f1f8f;
                break;
            case 0x6c:	/* GPMC_CONFIG4 */
                f->config[3] = value & 0x1f8f1f8f;
                break;
            case 0x70:	/* GPMC_CONFIG5 */
                f->config[4] = value & 0x0f1f1f1f;
                break;
            case 0x74:	/* GPMC_CONFIG6 */
                f->config[5] = value & 0x00000fcf;
                break;
            case 0x78:	/* GPMC_CONFIG7 */
                if ((f->config[6] ^ value) & 0xf7f) {
                    omap_gpmc_cs_unmap(s, cs);
                    f->config[6] = value & 0x00000f7f;
                    omap_gpmc_cs_map(s, cs);
                }
                break;
            case 0x7c:	/* GPMC_NAND_COMMAND */
            case 0x80:	/* GPMC_NAND_ADDRESS */
            case 0x84:	/* GPMC_NAND_DATA */
                break;

            default:
                goto bad_reg;
        }
        break;

    case 0x1e0:	/* GPMC_PREFETCH_CONFIG1 */
        s->prefconfig[0] = value & 0x7f8f7fbf;
        /* TODO: update interrupts, fifos, dmas */
        break;

    case 0x1e4:	/* GPMC_PREFETCH_CONFIG2 */
        s->prefconfig[1] = value & 0x3fff;
        break;

    case 0x1ec:	/* GPMC_PREFETCH_CONTROL */
        s->prefcontrol = value & 1;
        if (s->prefcontrol) {
            if (s->prefconfig[0] & 1)
                s->preffifo = 0x40;
            else
                s->preffifo = 0x00;
        }
        /* TODO: start */
        break;

    case 0x1f4:	/* GPMC_ECC_CONFIG */
        s->ecc_cs = 0x8f;
        break;
    case 0x1f8:	/* GPMC_ECC_CONTROL */
        if (value & (1 << 8))
            for (cs = 0; cs < 9; cs ++)
                ecc_reset(&s->ecc[cs]);
        s->ecc_ptr = value & 0xf;
        if (s->ecc_ptr == 0 || s->ecc_ptr > 9) {
            s->ecc_ptr = 0;
            s->ecc_cs &= ~1;
        }
        break;
    case 0x1fc:	/* GPMC_ECC_SIZE_CONFIG */
        s->ecc_cfg = value & 0x3fcff1ff;
        break;
    case 0x230:	/* GPMC_TESTMODE_CTRL */
        if (value & 7)
            fprintf(stderr, "%s: test mode enable attempt\n", __FUNCTION__);
        break;

    default:
    bad_reg:
        OMAP_BAD_REG(addr);
        return;
    }
}

static const MemoryRegionOps omap_gpmc_ops = {
    .read = omap_gpmc_read,
    .write = omap_gpmc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

struct omap_gpmc_s *omap_gpmc_init(target_phys_addr_t base, qemu_irq irq)
{
    struct omap_gpmc_s *s = (struct omap_gpmc_s *)
            g_malloc0(sizeof(struct omap_gpmc_s));

    memory_region_init_io(&s->iomem, &omap_gpmc_ops, s, "omap-gpmc", 0x1000);
    memory_region_add_subregion(get_system_memory(), base, &s->iomem);

    omap_gpmc_reset(s);

    return s;
}

void omap_gpmc_attach(struct omap_gpmc_s *s, int cs, MemoryRegion *iomem)
{
    struct omap_gpmc_cs_file_s *f;
    assert(iomem);

    if (cs < 0 || cs >= 8) {
        fprintf(stderr, "%s: bad chip-select %i\n", __FUNCTION__, cs);
        exit(-1);
    }
    f = &s->cs_file[cs];

    omap_gpmc_cs_unmap(s, cs);
    f->iomem = iomem;
    omap_gpmc_cs_map(s, cs);
}
