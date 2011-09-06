/*
 * QEMU IDE Emulation: microdrive (CF / PCMCIA)
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Openedhand Ltd.
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
#include <hw/hw.h>
#include <hw/pc.h>
#include <hw/pcmcia.h>
#include "block.h"
#include "dma.h"

#include <hw/ide/internal.h>

/***********************************************************/
/* CF-ATA Microdrive */

#define METADATA_SIZE	0x20

/* DSCM-1XXXX Microdrive hard disk with CF+ II / PCMCIA interface.  */
typedef struct {
    IDEBus bus;
    PCMCIACardState card;
    uint32_t attr_base;
    uint32_t io_base;

    /* Card state */
    uint8_t opt;
    uint8_t stat;
    uint8_t pins;

    uint8_t ctrl;
    uint16_t io;
    uint8_t cycle;
} MicroDriveState;

/* Register bitfields */
enum md_opt {
    OPT_MODE_MMAP	= 0,
    OPT_MODE_IOMAP16	= 1,
    OPT_MODE_IOMAP1	= 2,
    OPT_MODE_IOMAP2	= 3,
    OPT_MODE		= 0x3f,
    OPT_LEVIREQ		= 0x40,
    OPT_SRESET		= 0x80,
};
enum md_cstat {
    STAT_INT		= 0x02,
    STAT_PWRDWN		= 0x04,
    STAT_XE		= 0x10,
    STAT_IOIS8		= 0x20,
    STAT_SIGCHG		= 0x40,
    STAT_CHANGED	= 0x80,
};
enum md_pins {
    PINS_MRDY		= 0x02,
    PINS_CRDY		= 0x20,
};
enum md_ctrl {
    CTRL_IEN		= 0x02,
    CTRL_SRST		= 0x04,
};

static inline void md_interrupt_update(MicroDriveState *s)
{
    if (!s->card.slot)
        return;

    qemu_set_irq(s->card.slot->irq,
                    !(s->stat & STAT_INT) &&	/* Inverted */
                    !(s->ctrl & (CTRL_IEN | CTRL_SRST)) &&
                    !(s->opt & OPT_SRESET));
}

static void md_set_irq(void *opaque, int irq, int level)
{
    MicroDriveState *s = opaque;
    if (level)
        s->stat |= STAT_INT;
    else
        s->stat &= ~STAT_INT;

    md_interrupt_update(s);
}

static void md_reset(MicroDriveState *s)
{
    s->opt = OPT_MODE_MMAP;
    s->stat = 0;
    s->pins = 0;
    s->cycle = 0;
    s->ctrl = 0;
    ide_bus_reset(&s->bus);
}

static uint8_t md_attr_read(void *opaque, uint32_t at)
{
    MicroDriveState *s = opaque;
    if (at < s->attr_base) {
        if (at < s->card.cis_len)
            return s->card.cis[at];
        else
            return 0x00;
    }

    at -= s->attr_base;

    switch (at) {
    case 0x00:	/* Configuration Option Register */
        return s->opt;
    case 0x02:	/* Card Configuration Status Register */
        if (s->ctrl & CTRL_IEN)
            return s->stat & ~STAT_INT;
        else
            return s->stat;
    case 0x04:	/* Pin Replacement Register */
        return (s->pins & PINS_CRDY) | 0x0c;
    case 0x06:	/* Socket and Copy Register */
        return 0x00;
#ifdef VERBOSE
    default:
        printf("%s: Bad attribute space register %02x\n", __FUNCTION__, at);
#endif
    }

    return 0;
}

static void md_attr_write(void *opaque, uint32_t at, uint8_t value)
{
    MicroDriveState *s = opaque;
    at -= s->attr_base;

    switch (at) {
    case 0x00:	/* Configuration Option Register */
        s->opt = value & 0xcf;
        if (value & OPT_SRESET)
            md_reset(s);
        md_interrupt_update(s);
        break;
    case 0x02:	/* Card Configuration Status Register */
        if ((s->stat ^ value) & STAT_PWRDWN)
            s->pins |= PINS_CRDY;
        s->stat &= 0x82;
        s->stat |= value & 0x74;
        md_interrupt_update(s);
        /* Word 170 in Identify Device must be equal to STAT_XE */
        break;
    case 0x04:	/* Pin Replacement Register */
        s->pins &= PINS_CRDY;
        s->pins |= value & PINS_MRDY;
        break;
    case 0x06:	/* Socket and Copy Register */
        break;
    default:
        printf("%s: Bad attribute space register %02x\n", __FUNCTION__, at);
    }
}

static uint16_t md_common_read(void *opaque, uint32_t at)
{
    MicroDriveState *s = opaque;
    IDEState *ifs;
    uint16_t ret;
    at -= s->io_base;

    switch (s->opt & OPT_MODE) {
    case OPT_MODE_MMAP:
        if ((at & ~0x3ff) == 0x400)
            at = 0;
        break;
    case OPT_MODE_IOMAP16:
        at &= 0xf;
        break;
    case OPT_MODE_IOMAP1:
        if ((at & ~0xf) == 0x3f0)
            at -= 0x3e8;
        else if ((at & ~0xf) == 0x1f0)
            at -= 0x1f0;
        break;
    case OPT_MODE_IOMAP2:
        if ((at & ~0xf) == 0x370)
            at -= 0x368;
        else if ((at & ~0xf) == 0x170)
            at -= 0x170;
    }

    switch (at) {
    case 0x0:	/* Even RD Data */
    case 0x8:
        return ide_data_readw(&s->bus, 0);

        /* TODO: 8-bit accesses */
        if (s->cycle)
            ret = s->io >> 8;
        else {
            s->io = ide_data_readw(&s->bus, 0);
            ret = s->io & 0xff;
        }
        s->cycle = !s->cycle;
        return ret;
    case 0x9:	/* Odd RD Data */
        return s->io >> 8;
    case 0xd:	/* Error */
        return ide_ioport_read(&s->bus, 0x1);
    case 0xe:	/* Alternate Status */
        ifs = idebus_active_if(&s->bus);
        if (ifs->bs)
            return ifs->status;
        else
            return 0;
    case 0xf:	/* Device Address */
        ifs = idebus_active_if(&s->bus);
        return 0xc2 | ((~ifs->select << 2) & 0x3c);
    default:
        return ide_ioport_read(&s->bus, at);
    }

    return 0;
}

static void md_common_write(void *opaque, uint32_t at, uint16_t value)
{
    MicroDriveState *s = opaque;
    at -= s->io_base;

    switch (s->opt & OPT_MODE) {
    case OPT_MODE_MMAP:
        if ((at & ~0x3ff) == 0x400)
            at = 0;
        break;
    case OPT_MODE_IOMAP16:
        at &= 0xf;
        break;
    case OPT_MODE_IOMAP1:
        if ((at & ~0xf) == 0x3f0)
            at -= 0x3e8;
        else if ((at & ~0xf) == 0x1f0)
            at -= 0x1f0;
        break;
    case OPT_MODE_IOMAP2:
        if ((at & ~0xf) == 0x370)
            at -= 0x368;
        else if ((at & ~0xf) == 0x170)
            at -= 0x170;
    }

    switch (at) {
    case 0x0:	/* Even WR Data */
    case 0x8:
        ide_data_writew(&s->bus, 0, value);
        break;

        /* TODO: 8-bit accesses */
        if (s->cycle)
            ide_data_writew(&s->bus, 0, s->io | (value << 8));
        else
            s->io = value & 0xff;
        s->cycle = !s->cycle;
        break;
    case 0x9:
        s->io = value & 0xff;
        s->cycle = !s->cycle;
        break;
    case 0xd:	/* Features */
        ide_ioport_write(&s->bus, 0x1, value);
        break;
    case 0xe:	/* Device Control */
        s->ctrl = value;
        if (value & CTRL_SRST)
            md_reset(s);
        md_interrupt_update(s);
        break;
    default:
        if (s->stat & STAT_PWRDWN) {
            s->pins |= PINS_CRDY;
            s->stat &= ~STAT_PWRDWN;
        }
        ide_ioport_write(&s->bus, at, value);
    }
}

static const VMStateDescription vmstate_microdrive = {
    .name = "microdrive",
    .version_id = 3,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .fields      = (VMStateField []) {
        VMSTATE_UINT8(opt, MicroDriveState),
        VMSTATE_UINT8(stat, MicroDriveState),
        VMSTATE_UINT8(pins, MicroDriveState),
        VMSTATE_UINT8(ctrl, MicroDriveState),
        VMSTATE_UINT16(io, MicroDriveState),
        VMSTATE_UINT8(cycle, MicroDriveState),
        VMSTATE_IDE_BUS(bus, MicroDriveState),
        VMSTATE_IDE_DRIVES(bus.ifs, MicroDriveState),
        VMSTATE_END_OF_LIST()
    }
};

static const uint8_t dscm1xxxx_cis[0x14a] = {
    [0x000] = CISTPL_DEVICE,	/* 5V Device Information */
    [0x002] = 0x03,		/* Tuple length = 4 bytes */
    [0x004] = 0xdb,		/* ID: DTYPE_FUNCSPEC, non WP, DSPEED_150NS */
    [0x006] = 0x01,		/* Size = 2K bytes */
    [0x008] = CISTPL_ENDMARK,

    [0x00a] = CISTPL_DEVICE_OC,	/* Additional Device Information */
    [0x00c] = 0x04,		/* Tuple length = 4 byest */
    [0x00e] = 0x03,		/* Conditions: Ext = 0, Vcc 3.3V, MWAIT = 1 */
    [0x010] = 0xdb,		/* ID: DTYPE_FUNCSPEC, non WP, DSPEED_150NS */
    [0x012] = 0x01,		/* Size = 2K bytes */
    [0x014] = CISTPL_ENDMARK,

    [0x016] = CISTPL_JEDEC_C,	/* JEDEC ID */
    [0x018] = 0x02,		/* Tuple length = 2 bytes */
    [0x01a] = 0xdf,		/* PC Card ATA with no Vpp required */
    [0x01c] = 0x01,

    [0x01e] = CISTPL_MANFID,	/* Manufacture ID */
    [0x020] = 0x04,		/* Tuple length = 4 bytes */
    [0x022] = 0xa4,		/* TPLMID_MANF = 00a4 (IBM) */
    [0x024] = 0x00,
    [0x026] = 0x00,		/* PLMID_CARD = 0000 */
    [0x028] = 0x00,

    [0x02a] = CISTPL_VERS_1,	/* Level 1 Version */
    [0x02c] = 0x12,		/* Tuple length = 23 bytes */
    [0x02e] = 0x04,		/* Major Version = JEIDA 4.2 / PCMCIA 2.1 */
    [0x030] = 0x01,		/* Minor Version = 1 */
    [0x032] = 'I',
    [0x034] = 'B',
    [0x036] = 'M',
    [0x038] = 0x00,
    [0x03a] = 'm',
    [0x03c] = 'i',
    [0x03e] = 'c',
    [0x040] = 'r',
    [0x042] = 'o',
    [0x044] = 'd',
    [0x046] = 'r',
    [0x048] = 'i',
    [0x04a] = 'v',
    [0x04c] = 'e',
    [0x04e] = 0x00,
    [0x050] = CISTPL_ENDMARK,

    [0x052] = CISTPL_FUNCID,	/* Function ID */
    [0x054] = 0x02,		/* Tuple length = 2 bytes */
    [0x056] = 0x04,		/* TPLFID_FUNCTION = Fixed Disk */
    [0x058] = 0x01,		/* TPLFID_SYSINIT: POST = 1, ROM = 0 */

    [0x05a] = CISTPL_FUNCE,	/* Function Extension */
    [0x05c] = 0x02,		/* Tuple length = 2 bytes */
    [0x05e] = 0x01,		/* TPLFE_TYPE = Disk Device Interface */
    [0x060] = 0x01,		/* TPLFE_DATA = PC Card ATA Interface */

    [0x062] = CISTPL_FUNCE,	/* Function Extension */
    [0x064] = 0x03,		/* Tuple length = 3 bytes */
    [0x066] = 0x02,		/* TPLFE_TYPE = Basic PC Card ATA Interface */
    [0x068] = 0x08,		/* TPLFE_DATA: Rotating, Unique, Single */
    [0x06a] = 0x0f,		/* TPLFE_DATA: Sleep, Standby, Idle, Auto */

    [0x06c] = CISTPL_CONFIG,	/* Configuration */
    [0x06e] = 0x05,		/* Tuple length = 5 bytes */
    [0x070] = 0x01,		/* TPCC_RASZ = 2 bytes, TPCC_RMSZ = 1 byte */
    [0x072] = 0x07,		/* TPCC_LAST = 7 */
    [0x074] = 0x00,		/* TPCC_RADR = 0200 */
    [0x076] = 0x02,
    [0x078] = 0x0f,		/* TPCC_RMSK = 200, 202, 204, 206 */

    [0x07a] = CISTPL_CFTABLE_ENTRY,	/* 16-bit PC Card Configuration */
    [0x07c] = 0x0b,		/* Tuple length = 11 bytes */
    [0x07e] = 0xc0,		/* TPCE_INDX = Memory Mode, Default, Iface */
    [0x080] = 0xc0,		/* TPCE_IF = Memory, no BVDs, no WP, READY */
    [0x082] = 0xa1,		/* TPCE_FS = Vcc only, no I/O, Memory, Misc */
    [0x084] = 0x27,		/* NomV = 1, MinV = 1, MaxV = 1, Peakl = 1 */
    [0x086] = 0x55,		/* NomV: 5.0 V */
    [0x088] = 0x4d,		/* MinV: 4.5 V */
    [0x08a] = 0x5d,		/* MaxV: 5.5 V */
    [0x08c] = 0x4e,		/* Peakl: 450 mA */
    [0x08e] = 0x08,		/* TPCE_MS = 1 window, 1 byte, Host address */
    [0x090] = 0x00,		/* Window descriptor: Window length = 0 */
    [0x092] = 0x20,		/* TPCE_MI: support power down mode, RW */

    [0x094] = CISTPL_CFTABLE_ENTRY,	/* 16-bit PC Card Configuration */
    [0x096] = 0x06,		/* Tuple length = 6 bytes */
    [0x098] = 0x00,		/* TPCE_INDX = Memory Mode, no Default */
    [0x09a] = 0x01,		/* TPCE_FS = Vcc only, no I/O, no Memory */
    [0x09c] = 0x21,		/* NomV = 1, MinV = 0, MaxV = 0, Peakl = 1 */
    [0x09e] = 0xb5,		/* NomV: 3.3 V */
    [0x0a0] = 0x1e,
    [0x0a2] = 0x3e,		/* Peakl: 350 mA */

    [0x0a4] = CISTPL_CFTABLE_ENTRY,	/* 16-bit PC Card Configuration */
    [0x0a6] = 0x0d,		/* Tuple length = 13 bytes */
    [0x0a8] = 0xc1,		/* TPCE_INDX = I/O and Memory Mode, Default */
    [0x0aa] = 0x41,		/* TPCE_IF = I/O and Memory, no BVD, no WP */
    [0x0ac] = 0x99,		/* TPCE_FS = Vcc only, I/O, Interrupt, Misc */
    [0x0ae] = 0x27,		/* NomV = 1, MinV = 1, MaxV = 1, Peakl = 1 */
    [0x0b0] = 0x55,		/* NomV: 5.0 V */
    [0x0b2] = 0x4d,		/* MinV: 4.5 V */
    [0x0b4] = 0x5d,		/* MaxV: 5.5 V */
    [0x0b6] = 0x4e,		/* Peakl: 450 mA */
    [0x0b8] = 0x64,		/* TPCE_IO = 16-byte boundary, 16/8 accesses */
    [0x0ba] = 0xf0,		/* TPCE_IR =  MASK, Level, Pulse, Share */
    [0x0bc] = 0xff,		/* IRQ0..IRQ7 supported */
    [0x0be] = 0xff,		/* IRQ8..IRQ15 supported */
    [0x0c0] = 0x20,		/* TPCE_MI = support power down mode */

    [0x0c2] = CISTPL_CFTABLE_ENTRY,	/* 16-bit PC Card Configuration */
    [0x0c4] = 0x06,		/* Tuple length = 6 bytes */
    [0x0c6] = 0x01,		/* TPCE_INDX = I/O and Memory Mode */
    [0x0c8] = 0x01,		/* TPCE_FS = Vcc only, no I/O, no Memory */
    [0x0ca] = 0x21,		/* NomV = 1, MinV = 0, MaxV = 0, Peakl = 1 */
    [0x0cc] = 0xb5,		/* NomV: 3.3 V */
    [0x0ce] = 0x1e,
    [0x0d0] = 0x3e,		/* Peakl: 350 mA */

    [0x0d2] = CISTPL_CFTABLE_ENTRY,	/* 16-bit PC Card Configuration */
    [0x0d4] = 0x12,		/* Tuple length = 18 bytes */
    [0x0d6] = 0xc2,		/* TPCE_INDX = I/O Primary Mode */
    [0x0d8] = 0x41,		/* TPCE_IF = I/O and Memory, no BVD, no WP */
    [0x0da] = 0x99,		/* TPCE_FS = Vcc only, I/O, Interrupt, Misc */
    [0x0dc] = 0x27,		/* NomV = 1, MinV = 1, MaxV = 1, Peakl = 1 */
    [0x0de] = 0x55,		/* NomV: 5.0 V */
    [0x0e0] = 0x4d,		/* MinV: 4.5 V */
    [0x0e2] = 0x5d,		/* MaxV: 5.5 V */
    [0x0e4] = 0x4e,		/* Peakl: 450 mA */
    [0x0e6] = 0xea,		/* TPCE_IO = 1K boundary, 16/8 access, Range */
    [0x0e8] = 0x61,		/* Range: 2 fields, 2 bytes addr, 1 byte len */
    [0x0ea] = 0xf0,		/* Field 1 address = 0x01f0 */
    [0x0ec] = 0x01,
    [0x0ee] = 0x07,		/* Address block length = 8 */
    [0x0f0] = 0xf6,		/* Field 2 address = 0x03f6 */
    [0x0f2] = 0x03,
    [0x0f4] = 0x01,		/* Address block length = 2 */
    [0x0f6] = 0xee,		/* TPCE_IR = IRQ E, Level, Pulse, Share */
    [0x0f8] = 0x20,		/* TPCE_MI = support power down mode */

    [0x0fa] = CISTPL_CFTABLE_ENTRY,	/* 16-bit PC Card Configuration */
    [0x0fc] = 0x06,		/* Tuple length = 6 bytes */
    [0x0fe] = 0x02,		/* TPCE_INDX = I/O Primary Mode, no Default */
    [0x100] = 0x01,		/* TPCE_FS = Vcc only, no I/O, no Memory */
    [0x102] = 0x21,		/* NomV = 1, MinV = 0, MaxV = 0, Peakl = 1 */
    [0x104] = 0xb5,		/* NomV: 3.3 V */
    [0x106] = 0x1e,
    [0x108] = 0x3e,		/* Peakl: 350 mA */

    [0x10a] = CISTPL_CFTABLE_ENTRY,	/* 16-bit PC Card Configuration */
    [0x10c] = 0x12,		/* Tuple length = 18 bytes */
    [0x10e] = 0xc3,		/* TPCE_INDX = I/O Secondary Mode, Default */
    [0x110] = 0x41,		/* TPCE_IF = I/O and Memory, no BVD, no WP */
    [0x112] = 0x99,		/* TPCE_FS = Vcc only, I/O, Interrupt, Misc */
    [0x114] = 0x27,		/* NomV = 1, MinV = 1, MaxV = 1, Peakl = 1 */
    [0x116] = 0x55,		/* NomV: 5.0 V */
    [0x118] = 0x4d,		/* MinV: 4.5 V */
    [0x11a] = 0x5d,		/* MaxV: 5.5 V */
    [0x11c] = 0x4e,		/* Peakl: 450 mA */
    [0x11e] = 0xea,		/* TPCE_IO = 1K boundary, 16/8 access, Range */
    [0x120] = 0x61,		/* Range: 2 fields, 2 byte addr, 1 byte len */
    [0x122] = 0x70,		/* Field 1 address = 0x0170 */
    [0x124] = 0x01,
    [0x126] = 0x07,		/* Address block length = 8 */
    [0x128] = 0x76,		/* Field 2 address = 0x0376 */
    [0x12a] = 0x03,
    [0x12c] = 0x01,		/* Address block length = 2 */
    [0x12e] = 0xee,		/* TPCE_IR = IRQ E, Level, Pulse, Share */
    [0x130] = 0x20,		/* TPCE_MI = support power down mode */

    [0x132] = CISTPL_CFTABLE_ENTRY,	/* 16-bit PC Card Configuration */
    [0x134] = 0x06,		/* Tuple length = 6 bytes */
    [0x136] = 0x03,		/* TPCE_INDX = I/O Secondary Mode */
    [0x138] = 0x01,		/* TPCE_FS = Vcc only, no I/O, no Memory */
    [0x13a] = 0x21,		/* NomV = 1, MinV = 0, MaxV = 0, Peakl = 1 */
    [0x13c] = 0xb5,		/* NomV: 3.3 V */
    [0x13e] = 0x1e,
    [0x140] = 0x3e,		/* Peakl: 350 mA */

    [0x142] = CISTPL_NO_LINK,	/* No Link */
    [0x144] = 0x00,		/* Tuple length = 0 bytes */

    [0x146] = CISTPL_END,	/* Tuple End */
};

static int dscm1xxxx_attach(void *opaque)
{
    MicroDriveState *md = opaque;
    md->card.attr_read = md_attr_read;
    md->card.attr_write = md_attr_write;
    md->card.common_read = md_common_read;
    md->card.common_write = md_common_write;
    md->card.io_read = md_common_read;
    md->card.io_write = md_common_write;

    md->attr_base = md->card.cis[0x74] | (md->card.cis[0x76] << 8);
    md->io_base = 0x0;

    md_reset(md);
    md_interrupt_update(md);

    md->card.slot->card_string = "DSCM-1xxxx Hitachi Microdrive";
    return 0;
}

static int dscm1xxxx_detach(void *opaque)
{
    MicroDriveState *md = opaque;
    md_reset(md);
    return 0;
}

PCMCIACardState *dscm1xxxx_init(DriveInfo *bdrv)
{
    MicroDriveState *md = (MicroDriveState *) g_malloc0(sizeof(MicroDriveState));
    md->card.state = md;
    md->card.attach = dscm1xxxx_attach;
    md->card.detach = dscm1xxxx_detach;
    md->card.cis = dscm1xxxx_cis;
    md->card.cis_len = sizeof(dscm1xxxx_cis);

    ide_init2_with_non_qdev_drives(&md->bus, bdrv, NULL,
                                   qemu_allocate_irqs(md_set_irq, md, 1)[0]);
    md->bus.ifs[0].drive_kind = IDE_CFATA;
    md->bus.ifs[0].mdata_size = METADATA_SIZE;
    md->bus.ifs[0].mdata_storage = (uint8_t *) g_malloc0(METADATA_SIZE);

    vmstate_register(NULL, -1, &vmstate_microdrive, md);

    return &md->card;
}
