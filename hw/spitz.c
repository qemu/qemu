/*
 * PXA270-based Clamshell PDA platforms.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "hw.h"
#include "pxa.h"
#include "arm-misc.h"
#include "sysemu.h"
#include "pcmcia.h"
#include "i2c.h"
#include "ssi.h"
#include "flash.h"
#include "qemu-timer.h"
#include "devices.h"
#include "sharpsl.h"
#include "console.h"
#include "block.h"
#include "audio/audio.h"
#include "boards.h"
#include "blockdev.h"
#include "sysbus.h"
#include "exec-memory.h"

#undef REG_FMT
#define REG_FMT			"0x%02lx"

/* Spitz Flash */
#define FLASH_BASE		0x0c000000
#define FLASH_ECCLPLB		0x00	/* Line parity 7 - 0 bit */
#define FLASH_ECCLPUB		0x04	/* Line parity 15 - 8 bit */
#define FLASH_ECCCP		0x08	/* Column parity 5 - 0 bit */
#define FLASH_ECCCNTR		0x0c	/* ECC byte counter */
#define FLASH_ECCCLRR		0x10	/* Clear ECC */
#define FLASH_FLASHIO		0x14	/* Flash I/O */
#define FLASH_FLASHCTL		0x18	/* Flash Control */

#define FLASHCTL_CE0		(1 << 0)
#define FLASHCTL_CLE		(1 << 1)
#define FLASHCTL_ALE		(1 << 2)
#define FLASHCTL_WP		(1 << 3)
#define FLASHCTL_CE1		(1 << 4)
#define FLASHCTL_RYBY		(1 << 5)
#define FLASHCTL_NCE		(FLASHCTL_CE0 | FLASHCTL_CE1)

typedef struct {
    SysBusDevice busdev;
    MemoryRegion iomem;
    DeviceState *nand;
    uint8_t ctl;
    uint8_t manf_id;
    uint8_t chip_id;
    ECCState ecc;
} SLNANDState;

static uint64_t sl_read(void *opaque, hwaddr addr, unsigned size)
{
    SLNANDState *s = (SLNANDState *) opaque;
    int ryby;

    switch (addr) {
#define BSHR(byte, from, to)	((s->ecc.lp[byte] >> (from - to)) & (1 << to))
    case FLASH_ECCLPLB:
        return BSHR(0, 4, 0) | BSHR(0, 5, 2) | BSHR(0, 6, 4) | BSHR(0, 7, 6) |
                BSHR(1, 4, 1) | BSHR(1, 5, 3) | BSHR(1, 6, 5) | BSHR(1, 7, 7);

#define BSHL(byte, from, to)	((s->ecc.lp[byte] << (to - from)) & (1 << to))
    case FLASH_ECCLPUB:
        return BSHL(0, 0, 0) | BSHL(0, 1, 2) | BSHL(0, 2, 4) | BSHL(0, 3, 6) |
                BSHL(1, 0, 1) | BSHL(1, 1, 3) | BSHL(1, 2, 5) | BSHL(1, 3, 7);

    case FLASH_ECCCP:
        return s->ecc.cp;

    case FLASH_ECCCNTR:
        return s->ecc.count & 0xff;

    case FLASH_FLASHCTL:
        nand_getpins(s->nand, &ryby);
        if (ryby)
            return s->ctl | FLASHCTL_RYBY;
        else
            return s->ctl;

    case FLASH_FLASHIO:
        if (size == 4) {
            return ecc_digest(&s->ecc, nand_getio(s->nand)) |
                (ecc_digest(&s->ecc, nand_getio(s->nand)) << 16);
        }
        return ecc_digest(&s->ecc, nand_getio(s->nand));

    default:
        zaurus_printf("Bad register offset " REG_FMT "\n", (unsigned long)addr);
    }
    return 0;
}

static void sl_write(void *opaque, hwaddr addr,
                     uint64_t value, unsigned size)
{
    SLNANDState *s = (SLNANDState *) opaque;

    switch (addr) {
    case FLASH_ECCCLRR:
        /* Value is ignored.  */
        ecc_reset(&s->ecc);
        break;

    case FLASH_FLASHCTL:
        s->ctl = value & 0xff & ~FLASHCTL_RYBY;
        nand_setpins(s->nand,
                        s->ctl & FLASHCTL_CLE,
                        s->ctl & FLASHCTL_ALE,
                        s->ctl & FLASHCTL_NCE,
                        s->ctl & FLASHCTL_WP,
                        0);
        break;

    case FLASH_FLASHIO:
        nand_setio(s->nand, ecc_digest(&s->ecc, value & 0xff));
        break;

    default:
        zaurus_printf("Bad register offset " REG_FMT "\n", (unsigned long)addr);
    }
}

enum {
    FLASH_128M,
    FLASH_1024M,
};

static const MemoryRegionOps sl_ops = {
    .read = sl_read,
    .write = sl_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void sl_flash_register(PXA2xxState *cpu, int size)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "sl-nand");

    qdev_prop_set_uint8(dev, "manf_id", NAND_MFR_SAMSUNG);
    if (size == FLASH_128M)
        qdev_prop_set_uint8(dev, "chip_id", 0x73);
    else if (size == FLASH_1024M)
        qdev_prop_set_uint8(dev, "chip_id", 0xf1);

    qdev_init_nofail(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, FLASH_BASE);
}

static int sl_nand_init(SysBusDevice *dev) {
    SLNANDState *s;
    DriveInfo *nand;

    s = FROM_SYSBUS(SLNANDState, dev);

    s->ctl = 0;
    nand = drive_get(IF_MTD, 0, 0);
    s->nand = nand_init(nand ? nand->bdrv : NULL, s->manf_id, s->chip_id);

    memory_region_init_io(&s->iomem, &sl_ops, s, "sl", 0x40);
    sysbus_init_mmio(dev, &s->iomem);

    return 0;
}

/* Spitz Keyboard */

#define SPITZ_KEY_STROBE_NUM	11
#define SPITZ_KEY_SENSE_NUM	7

static const int spitz_gpio_key_sense[SPITZ_KEY_SENSE_NUM] = {
    12, 17, 91, 34, 36, 38, 39
};

static const int spitz_gpio_key_strobe[SPITZ_KEY_STROBE_NUM] = {
    88, 23, 24, 25, 26, 27, 52, 103, 107, 108, 114
};

/* Eighth additional row maps the special keys */
static int spitz_keymap[SPITZ_KEY_SENSE_NUM + 1][SPITZ_KEY_STROBE_NUM] = {
    { 0x1d, 0x02, 0x04, 0x06, 0x07, 0x08, 0x0a, 0x0b, 0x0e, 0x3f, 0x40 },
    {  -1 , 0x03, 0x05, 0x13, 0x15, 0x09, 0x17, 0x18, 0x19, 0x41, 0x42 },
    { 0x0f, 0x10, 0x12, 0x14, 0x22, 0x16, 0x24, 0x25,  -1 ,  -1 ,  -1  },
    { 0x3c, 0x11, 0x1f, 0x21, 0x2f, 0x23, 0x32, 0x26,  -1 , 0x36,  -1  },
    { 0x3b, 0x1e, 0x20, 0x2e, 0x30, 0x31, 0x34,  -1 , 0x1c, 0x2a,  -1  },
    { 0x44, 0x2c, 0x2d, 0x0c, 0x39, 0x33,  -1 , 0x48,  -1 ,  -1 , 0x38 },
    { 0x37, 0x3d,  -1 , 0x45, 0x57, 0x58, 0x4b, 0x50, 0x4d,  -1 ,  -1  },
    { 0x52, 0x43, 0x01, 0x47, 0x49,  -1 ,  -1 ,  -1 ,  -1 ,  -1 ,  -1  },
};

#define SPITZ_GPIO_AK_INT	13	/* Remote control */
#define SPITZ_GPIO_SYNC		16	/* Sync button */
#define SPITZ_GPIO_ON_KEY	95	/* Power button */
#define SPITZ_GPIO_SWA		97	/* Lid */
#define SPITZ_GPIO_SWB		96	/* Tablet mode */

/* The special buttons are mapped to unused keys */
static const int spitz_gpiomap[5] = {
    SPITZ_GPIO_AK_INT, SPITZ_GPIO_SYNC, SPITZ_GPIO_ON_KEY,
    SPITZ_GPIO_SWA, SPITZ_GPIO_SWB,
};

typedef struct {
    SysBusDevice busdev;
    qemu_irq sense[SPITZ_KEY_SENSE_NUM];
    qemu_irq gpiomap[5];
    int keymap[0x80];
    uint16_t keyrow[SPITZ_KEY_SENSE_NUM];
    uint16_t strobe_state;
    uint16_t sense_state;

    uint16_t pre_map[0x100];
    uint16_t modifiers;
    uint16_t imodifiers;
    uint8_t fifo[16];
    int fifopos, fifolen;
    QEMUTimer *kbdtimer;
} SpitzKeyboardState;

static void spitz_keyboard_sense_update(SpitzKeyboardState *s)
{
    int i;
    uint16_t strobe, sense = 0;
    for (i = 0; i < SPITZ_KEY_SENSE_NUM; i ++) {
        strobe = s->keyrow[i] & s->strobe_state;
        if (strobe) {
            sense |= 1 << i;
            if (!(s->sense_state & (1 << i)))
                qemu_irq_raise(s->sense[i]);
        } else if (s->sense_state & (1 << i))
            qemu_irq_lower(s->sense[i]);
    }

    s->sense_state = sense;
}

static void spitz_keyboard_strobe(void *opaque, int line, int level)
{
    SpitzKeyboardState *s = (SpitzKeyboardState *) opaque;

    if (level)
        s->strobe_state |= 1 << line;
    else
        s->strobe_state &= ~(1 << line);
    spitz_keyboard_sense_update(s);
}

static void spitz_keyboard_keydown(SpitzKeyboardState *s, int keycode)
{
    int spitz_keycode = s->keymap[keycode & 0x7f];
    if (spitz_keycode == -1)
        return;

    /* Handle the additional keys */
    if ((spitz_keycode >> 4) == SPITZ_KEY_SENSE_NUM) {
        qemu_set_irq(s->gpiomap[spitz_keycode & 0xf], (keycode < 0x80));
        return;
    }

    if (keycode & 0x80)
        s->keyrow[spitz_keycode >> 4] &= ~(1 << (spitz_keycode & 0xf));
    else
        s->keyrow[spitz_keycode >> 4] |= 1 << (spitz_keycode & 0xf);

    spitz_keyboard_sense_update(s);
}

#define SHIFT	(1 << 7)
#define CTRL	(1 << 8)
#define FN	(1 << 9)

#define QUEUE_KEY(c)	s->fifo[(s->fifopos + s->fifolen ++) & 0xf] = c

static void spitz_keyboard_handler(void *opaque, int keycode)
{
    SpitzKeyboardState *s = opaque;
    uint16_t code;
    int mapcode;
    switch (keycode) {
    case 0x2a:	/* Left Shift */
        s->modifiers |= 1;
        break;
    case 0xaa:
        s->modifiers &= ~1;
        break;
    case 0x36:	/* Right Shift */
        s->modifiers |= 2;
        break;
    case 0xb6:
        s->modifiers &= ~2;
        break;
    case 0x1d:	/* Control */
        s->modifiers |= 4;
        break;
    case 0x9d:
        s->modifiers &= ~4;
        break;
    case 0x38:	/* Alt */
        s->modifiers |= 8;
        break;
    case 0xb8:
        s->modifiers &= ~8;
        break;
    }

    code = s->pre_map[mapcode = ((s->modifiers & 3) ?
            (keycode | SHIFT) :
            (keycode & ~SHIFT))];

    if (code != mapcode) {
#if 0
        if ((code & SHIFT) && !(s->modifiers & 1))
            QUEUE_KEY(0x2a | (keycode & 0x80));
        if ((code & CTRL ) && !(s->modifiers & 4))
            QUEUE_KEY(0x1d | (keycode & 0x80));
        if ((code & FN   ) && !(s->modifiers & 8))
            QUEUE_KEY(0x38 | (keycode & 0x80));
        if ((code & FN   ) && (s->modifiers & 1))
            QUEUE_KEY(0x2a | (~keycode & 0x80));
        if ((code & FN   ) && (s->modifiers & 2))
            QUEUE_KEY(0x36 | (~keycode & 0x80));
#else
        if (keycode & 0x80) {
            if ((s->imodifiers & 1   ) && !(s->modifiers & 1))
                QUEUE_KEY(0x2a | 0x80);
            if ((s->imodifiers & 4   ) && !(s->modifiers & 4))
                QUEUE_KEY(0x1d | 0x80);
            if ((s->imodifiers & 8   ) && !(s->modifiers & 8))
                QUEUE_KEY(0x38 | 0x80);
            if ((s->imodifiers & 0x10) && (s->modifiers & 1))
                QUEUE_KEY(0x2a);
            if ((s->imodifiers & 0x20) && (s->modifiers & 2))
                QUEUE_KEY(0x36);
            s->imodifiers = 0;
        } else {
            if ((code & SHIFT) && !((s->modifiers | s->imodifiers) & 1)) {
                QUEUE_KEY(0x2a);
                s->imodifiers |= 1;
            }
            if ((code & CTRL ) && !((s->modifiers | s->imodifiers) & 4)) {
                QUEUE_KEY(0x1d);
                s->imodifiers |= 4;
            }
            if ((code & FN   ) && !((s->modifiers | s->imodifiers) & 8)) {
                QUEUE_KEY(0x38);
                s->imodifiers |= 8;
            }
            if ((code & FN   ) && (s->modifiers & 1) &&
                            !(s->imodifiers & 0x10)) {
                QUEUE_KEY(0x2a | 0x80);
                s->imodifiers |= 0x10;
            }
            if ((code & FN   ) && (s->modifiers & 2) &&
                            !(s->imodifiers & 0x20)) {
                QUEUE_KEY(0x36 | 0x80);
                s->imodifiers |= 0x20;
            }
        }
#endif
    }

    QUEUE_KEY((code & 0x7f) | (keycode & 0x80));
}

static void spitz_keyboard_tick(void *opaque)
{
    SpitzKeyboardState *s = (SpitzKeyboardState *) opaque;

    if (s->fifolen) {
        spitz_keyboard_keydown(s, s->fifo[s->fifopos ++]);
        s->fifolen --;
        if (s->fifopos >= 16)
            s->fifopos = 0;
    }

    qemu_mod_timer(s->kbdtimer, qemu_get_clock_ns(vm_clock) +
                   get_ticks_per_sec() / 32);
}

static void spitz_keyboard_pre_map(SpitzKeyboardState *s)
{
    int i;
    for (i = 0; i < 0x100; i ++)
        s->pre_map[i] = i;
    s->pre_map[0x02 | SHIFT	] = 0x02 | SHIFT;	/* exclam */
    s->pre_map[0x28 | SHIFT	] = 0x03 | SHIFT;	/* quotedbl */
    s->pre_map[0x04 | SHIFT	] = 0x04 | SHIFT;	/* numbersign */
    s->pre_map[0x05 | SHIFT	] = 0x05 | SHIFT;	/* dollar */
    s->pre_map[0x06 | SHIFT	] = 0x06 | SHIFT;	/* percent */
    s->pre_map[0x08 | SHIFT	] = 0x07 | SHIFT;	/* ampersand */
    s->pre_map[0x28		] = 0x08 | SHIFT;	/* apostrophe */
    s->pre_map[0x0a | SHIFT	] = 0x09 | SHIFT;	/* parenleft */
    s->pre_map[0x0b | SHIFT	] = 0x0a | SHIFT;	/* parenright */
    s->pre_map[0x29 | SHIFT	] = 0x0b | SHIFT;	/* asciitilde */
    s->pre_map[0x03 | SHIFT	] = 0x0c | SHIFT;	/* at */
    s->pre_map[0xd3		] = 0x0e | FN;		/* Delete */
    s->pre_map[0x3a		] = 0x0f | FN;		/* Caps_Lock */
    s->pre_map[0x07 | SHIFT	] = 0x11 | FN;		/* asciicircum */
    s->pre_map[0x0d		] = 0x12 | FN;		/* equal */
    s->pre_map[0x0d | SHIFT	] = 0x13 | FN;		/* plus */
    s->pre_map[0x1a		] = 0x14 | FN;		/* bracketleft */
    s->pre_map[0x1b		] = 0x15 | FN;		/* bracketright */
    s->pre_map[0x1a | SHIFT	] = 0x16 | FN;		/* braceleft */
    s->pre_map[0x1b | SHIFT	] = 0x17 | FN;		/* braceright */
    s->pre_map[0x27		] = 0x22 | FN;		/* semicolon */
    s->pre_map[0x27 | SHIFT	] = 0x23 | FN;		/* colon */
    s->pre_map[0x09 | SHIFT	] = 0x24 | FN;		/* asterisk */
    s->pre_map[0x2b		] = 0x25 | FN;		/* backslash */
    s->pre_map[0x2b | SHIFT	] = 0x26 | FN;		/* bar */
    s->pre_map[0x0c | SHIFT	] = 0x30 | FN;		/* underscore */
    s->pre_map[0x33 | SHIFT	] = 0x33 | FN;		/* less */
    s->pre_map[0x35		] = 0x33 | SHIFT;	/* slash */
    s->pre_map[0x34 | SHIFT	] = 0x34 | FN;		/* greater */
    s->pre_map[0x35 | SHIFT	] = 0x34 | SHIFT;	/* question */
    s->pre_map[0x49		] = 0x48 | FN;		/* Page_Up */
    s->pre_map[0x51		] = 0x50 | FN;		/* Page_Down */

    s->modifiers = 0;
    s->imodifiers = 0;
    s->fifopos = 0;
    s->fifolen = 0;
}

#undef SHIFT
#undef CTRL
#undef FN

static int spitz_keyboard_post_load(void *opaque, int version_id)
{
    SpitzKeyboardState *s = (SpitzKeyboardState *) opaque;

    /* Release all pressed keys */
    memset(s->keyrow, 0, sizeof(s->keyrow));
    spitz_keyboard_sense_update(s);
    s->modifiers = 0;
    s->imodifiers = 0;
    s->fifopos = 0;
    s->fifolen = 0;

    return 0;
}

static void spitz_keyboard_register(PXA2xxState *cpu)
{
    int i;
    DeviceState *dev;
    SpitzKeyboardState *s;

    dev = sysbus_create_simple("spitz-keyboard", -1, NULL);
    s = FROM_SYSBUS(SpitzKeyboardState, sysbus_from_qdev(dev));

    for (i = 0; i < SPITZ_KEY_SENSE_NUM; i ++)
        qdev_connect_gpio_out(dev, i, qdev_get_gpio_in(cpu->gpio, spitz_gpio_key_sense[i]));

    for (i = 0; i < 5; i ++)
        s->gpiomap[i] = qdev_get_gpio_in(cpu->gpio, spitz_gpiomap[i]);

    if (!graphic_rotate)
        s->gpiomap[4] = qemu_irq_invert(s->gpiomap[4]);

    for (i = 0; i < 5; i++)
        qemu_set_irq(s->gpiomap[i], 0);

    for (i = 0; i < SPITZ_KEY_STROBE_NUM; i ++)
        qdev_connect_gpio_out(cpu->gpio, spitz_gpio_key_strobe[i],
                qdev_get_gpio_in(dev, i));

    qemu_mod_timer(s->kbdtimer, qemu_get_clock_ns(vm_clock));

    qemu_add_kbd_event_handler(spitz_keyboard_handler, s);
}

static int spitz_keyboard_init(SysBusDevice *dev)
{
    SpitzKeyboardState *s;
    int i, j;

    s = FROM_SYSBUS(SpitzKeyboardState, dev);

    for (i = 0; i < 0x80; i ++)
        s->keymap[i] = -1;
    for (i = 0; i < SPITZ_KEY_SENSE_NUM + 1; i ++)
        for (j = 0; j < SPITZ_KEY_STROBE_NUM; j ++)
            if (spitz_keymap[i][j] != -1)
                s->keymap[spitz_keymap[i][j]] = (i << 4) | j;

    spitz_keyboard_pre_map(s);

    s->kbdtimer = qemu_new_timer_ns(vm_clock, spitz_keyboard_tick, s);
    qdev_init_gpio_in(&dev->qdev, spitz_keyboard_strobe, SPITZ_KEY_STROBE_NUM);
    qdev_init_gpio_out(&dev->qdev, s->sense, SPITZ_KEY_SENSE_NUM);

    return 0;
}

/* LCD backlight controller */

#define LCDTG_RESCTL	0x00
#define LCDTG_PHACTRL	0x01
#define LCDTG_DUTYCTRL	0x02
#define LCDTG_POWERREG0	0x03
#define LCDTG_POWERREG1	0x04
#define LCDTG_GPOR3	0x05
#define LCDTG_PICTRL	0x06
#define LCDTG_POLCTRL	0x07

typedef struct {
    SSISlave ssidev;
    uint32_t bl_intensity;
    uint32_t bl_power;
} SpitzLCDTG;

static void spitz_bl_update(SpitzLCDTG *s)
{
    if (s->bl_power && s->bl_intensity)
        zaurus_printf("LCD Backlight now at %i/63\n", s->bl_intensity);
    else
        zaurus_printf("LCD Backlight now off\n");
}

/* FIXME: Implement GPIO properly and remove this hack.  */
static SpitzLCDTG *spitz_lcdtg;

static inline void spitz_bl_bit5(void *opaque, int line, int level)
{
    SpitzLCDTG *s = spitz_lcdtg;
    int prev = s->bl_intensity;

    if (level)
        s->bl_intensity &= ~0x20;
    else
        s->bl_intensity |= 0x20;

    if (s->bl_power && prev != s->bl_intensity)
        spitz_bl_update(s);
}

static inline void spitz_bl_power(void *opaque, int line, int level)
{
    SpitzLCDTG *s = spitz_lcdtg;
    s->bl_power = !!level;
    spitz_bl_update(s);
}

static uint32_t spitz_lcdtg_transfer(SSISlave *dev, uint32_t value)
{
    SpitzLCDTG *s = FROM_SSI_SLAVE(SpitzLCDTG, dev);
    int addr;
    addr = value >> 5;
    value &= 0x1f;

    switch (addr) {
    case LCDTG_RESCTL:
        if (value)
            zaurus_printf("LCD in QVGA mode\n");
        else
            zaurus_printf("LCD in VGA mode\n");
        break;

    case LCDTG_DUTYCTRL:
        s->bl_intensity &= ~0x1f;
        s->bl_intensity |= value;
        if (s->bl_power)
            spitz_bl_update(s);
        break;

    case LCDTG_POWERREG0:
        /* Set common voltage to M62332FP */
        break;
    }
    return 0;
}

static int spitz_lcdtg_init(SSISlave *dev)
{
    SpitzLCDTG *s = FROM_SSI_SLAVE(SpitzLCDTG, dev);

    spitz_lcdtg = s;
    s->bl_power = 0;
    s->bl_intensity = 0x20;

    return 0;
}

/* SSP devices */

#define CORGI_SSP_PORT		2

#define SPITZ_GPIO_LCDCON_CS	53
#define SPITZ_GPIO_ADS7846_CS	14
#define SPITZ_GPIO_MAX1111_CS	20
#define SPITZ_GPIO_TP_INT	11

static DeviceState *max1111;

/* "Demux" the signal based on current chipselect */
typedef struct {
    SSISlave ssidev;
    SSIBus *bus[3];
    uint32_t enable[3];
} CorgiSSPState;

static uint32_t corgi_ssp_transfer(SSISlave *dev, uint32_t value)
{
    CorgiSSPState *s = FROM_SSI_SLAVE(CorgiSSPState, dev);
    int i;

    for (i = 0; i < 3; i++) {
        if (s->enable[i]) {
            return ssi_transfer(s->bus[i], value);
        }
    }
    return 0;
}

static void corgi_ssp_gpio_cs(void *opaque, int line, int level)
{
    CorgiSSPState *s = (CorgiSSPState *)opaque;
    assert(line >= 0 && line < 3);
    s->enable[line] = !level;
}

#define MAX1111_BATT_VOLT	1
#define MAX1111_BATT_TEMP	2
#define MAX1111_ACIN_VOLT	3

#define SPITZ_BATTERY_TEMP	0xe0	/* About 2.9V */
#define SPITZ_BATTERY_VOLT	0xd0	/* About 4.0V */
#define SPITZ_CHARGEON_ACIN	0x80	/* About 5.0V */

static void spitz_adc_temp_on(void *opaque, int line, int level)
{
    if (!max1111)
        return;

    if (level)
        max111x_set_input(max1111, MAX1111_BATT_TEMP, SPITZ_BATTERY_TEMP);
    else
        max111x_set_input(max1111, MAX1111_BATT_TEMP, 0);
}

static int corgi_ssp_init(SSISlave *dev)
{
    CorgiSSPState *s = FROM_SSI_SLAVE(CorgiSSPState, dev);

    qdev_init_gpio_in(&dev->qdev, corgi_ssp_gpio_cs, 3);
    s->bus[0] = ssi_create_bus(&dev->qdev, "ssi0");
    s->bus[1] = ssi_create_bus(&dev->qdev, "ssi1");
    s->bus[2] = ssi_create_bus(&dev->qdev, "ssi2");

    return 0;
}

static void spitz_ssp_attach(PXA2xxState *cpu)
{
    DeviceState *mux;
    DeviceState *dev;
    void *bus;

    mux = ssi_create_slave(cpu->ssp[CORGI_SSP_PORT - 1], "corgi-ssp");

    bus = qdev_get_child_bus(mux, "ssi0");
    ssi_create_slave(bus, "spitz-lcdtg");

    bus = qdev_get_child_bus(mux, "ssi1");
    dev = ssi_create_slave(bus, "ads7846");
    qdev_connect_gpio_out(dev, 0,
                          qdev_get_gpio_in(cpu->gpio, SPITZ_GPIO_TP_INT));

    bus = qdev_get_child_bus(mux, "ssi2");
    max1111 = ssi_create_slave(bus, "max1111");
    max111x_set_input(max1111, MAX1111_BATT_VOLT, SPITZ_BATTERY_VOLT);
    max111x_set_input(max1111, MAX1111_BATT_TEMP, 0);
    max111x_set_input(max1111, MAX1111_ACIN_VOLT, SPITZ_CHARGEON_ACIN);

    qdev_connect_gpio_out(cpu->gpio, SPITZ_GPIO_LCDCON_CS,
                        qdev_get_gpio_in(mux, 0));
    qdev_connect_gpio_out(cpu->gpio, SPITZ_GPIO_ADS7846_CS,
                        qdev_get_gpio_in(mux, 1));
    qdev_connect_gpio_out(cpu->gpio, SPITZ_GPIO_MAX1111_CS,
                        qdev_get_gpio_in(mux, 2));
}

/* CF Microdrive */

static void spitz_microdrive_attach(PXA2xxState *cpu, int slot)
{
    PCMCIACardState *md;
    DriveInfo *dinfo;

    dinfo = drive_get(IF_IDE, 0, 0);
    if (!dinfo || dinfo->media_cd)
        return;
    md = dscm1xxxx_init(dinfo);
    pxa2xx_pcmcia_attach(cpu->pcmcia[slot], md);
}

/* Wm8750 and Max7310 on I2C */

#define AKITA_MAX_ADDR	0x18
#define SPITZ_WM_ADDRL	0x1b
#define SPITZ_WM_ADDRH	0x1a

#define SPITZ_GPIO_WM	5

static void spitz_wm8750_addr(void *opaque, int line, int level)
{
    I2CSlave *wm = (I2CSlave *) opaque;
    if (level)
        i2c_set_slave_address(wm, SPITZ_WM_ADDRH);
    else
        i2c_set_slave_address(wm, SPITZ_WM_ADDRL);
}

static void spitz_i2c_setup(PXA2xxState *cpu)
{
    /* Attach the CPU on one end of our I2C bus.  */
    i2c_bus *bus = pxa2xx_i2c_bus(cpu->i2c[0]);

    DeviceState *wm;

    /* Attach a WM8750 to the bus */
    wm = i2c_create_slave(bus, "wm8750", 0);

    spitz_wm8750_addr(wm, 0, 0);
    qdev_connect_gpio_out(cpu->gpio, SPITZ_GPIO_WM,
                    qemu_allocate_irqs(spitz_wm8750_addr, wm, 1)[0]);
    /* .. and to the sound interface.  */
    cpu->i2s->opaque = wm;
    cpu->i2s->codec_out = wm8750_dac_dat;
    cpu->i2s->codec_in = wm8750_adc_dat;
    wm8750_data_req_set(wm, cpu->i2s->data_req, cpu->i2s);
}

static void spitz_akita_i2c_setup(PXA2xxState *cpu)
{
    /* Attach a Max7310 to Akita I2C bus.  */
    i2c_create_slave(pxa2xx_i2c_bus(cpu->i2c[0]), "max7310",
                     AKITA_MAX_ADDR);
}

/* Other peripherals */

static void spitz_out_switch(void *opaque, int line, int level)
{
    switch (line) {
    case 0:
        zaurus_printf("Charging %s.\n", level ? "off" : "on");
        break;
    case 1:
        zaurus_printf("Discharging %s.\n", level ? "on" : "off");
        break;
    case 2:
        zaurus_printf("Green LED %s.\n", level ? "on" : "off");
        break;
    case 3:
        zaurus_printf("Orange LED %s.\n", level ? "on" : "off");
        break;
    case 4:
        spitz_bl_bit5(opaque, line, level);
        break;
    case 5:
        spitz_bl_power(opaque, line, level);
        break;
    case 6:
        spitz_adc_temp_on(opaque, line, level);
        break;
    }
}

#define SPITZ_SCP_LED_GREEN		1
#define SPITZ_SCP_JK_B			2
#define SPITZ_SCP_CHRG_ON		3
#define SPITZ_SCP_MUTE_L		4
#define SPITZ_SCP_MUTE_R		5
#define SPITZ_SCP_CF_POWER		6
#define SPITZ_SCP_LED_ORANGE		7
#define SPITZ_SCP_JK_A			8
#define SPITZ_SCP_ADC_TEMP_ON		9
#define SPITZ_SCP2_IR_ON		1
#define SPITZ_SCP2_AKIN_PULLUP		2
#define SPITZ_SCP2_BACKLIGHT_CONT	7
#define SPITZ_SCP2_BACKLIGHT_ON		8
#define SPITZ_SCP2_MIC_BIAS		9

static void spitz_scoop_gpio_setup(PXA2xxState *cpu,
                DeviceState *scp0, DeviceState *scp1)
{
    qemu_irq *outsignals = qemu_allocate_irqs(spitz_out_switch, cpu, 8);

    qdev_connect_gpio_out(scp0, SPITZ_SCP_CHRG_ON, outsignals[0]);
    qdev_connect_gpio_out(scp0, SPITZ_SCP_JK_B, outsignals[1]);
    qdev_connect_gpio_out(scp0, SPITZ_SCP_LED_GREEN, outsignals[2]);
    qdev_connect_gpio_out(scp0, SPITZ_SCP_LED_ORANGE, outsignals[3]);

    if (scp1) {
        qdev_connect_gpio_out(scp1, SPITZ_SCP2_BACKLIGHT_CONT, outsignals[4]);
        qdev_connect_gpio_out(scp1, SPITZ_SCP2_BACKLIGHT_ON, outsignals[5]);
    }

    qdev_connect_gpio_out(scp0, SPITZ_SCP_ADC_TEMP_ON, outsignals[6]);
}

#define SPITZ_GPIO_HSYNC		22
#define SPITZ_GPIO_SD_DETECT		9
#define SPITZ_GPIO_SD_WP		81
#define SPITZ_GPIO_ON_RESET		89
#define SPITZ_GPIO_BAT_COVER		90
#define SPITZ_GPIO_CF1_IRQ		105
#define SPITZ_GPIO_CF1_CD		94
#define SPITZ_GPIO_CF2_IRQ		106
#define SPITZ_GPIO_CF2_CD		93

static int spitz_hsync;

static void spitz_lcd_hsync_handler(void *opaque, int line, int level)
{
    PXA2xxState *cpu = (PXA2xxState *) opaque;
    qemu_set_irq(qdev_get_gpio_in(cpu->gpio, SPITZ_GPIO_HSYNC), spitz_hsync);
    spitz_hsync ^= 1;
}

static void spitz_gpio_setup(PXA2xxState *cpu, int slots)
{
    qemu_irq lcd_hsync;
    /*
     * Bad hack: We toggle the LCD hsync GPIO on every GPIO status
     * read to satisfy broken guests that poll-wait for hsync.
     * Simulating a real hsync event would be less practical and
     * wouldn't guarantee that a guest ever exits the loop.
     */
    spitz_hsync = 0;
    lcd_hsync = qemu_allocate_irqs(spitz_lcd_hsync_handler, cpu, 1)[0];
    pxa2xx_gpio_read_notifier(cpu->gpio, lcd_hsync);
    pxa2xx_lcd_vsync_notifier(cpu->lcd, lcd_hsync);

    /* MMC/SD host */
    pxa2xx_mmci_handlers(cpu->mmc,
                    qdev_get_gpio_in(cpu->gpio, SPITZ_GPIO_SD_WP),
                    qdev_get_gpio_in(cpu->gpio, SPITZ_GPIO_SD_DETECT));

    /* Battery lock always closed */
    qemu_irq_raise(qdev_get_gpio_in(cpu->gpio, SPITZ_GPIO_BAT_COVER));

    /* Handle reset */
    qdev_connect_gpio_out(cpu->gpio, SPITZ_GPIO_ON_RESET, cpu->reset);

    /* PCMCIA signals: card's IRQ and Card-Detect */
    if (slots >= 1)
        pxa2xx_pcmcia_set_irq_cb(cpu->pcmcia[0],
                        qdev_get_gpio_in(cpu->gpio, SPITZ_GPIO_CF1_IRQ),
                        qdev_get_gpio_in(cpu->gpio, SPITZ_GPIO_CF1_CD));
    if (slots >= 2)
        pxa2xx_pcmcia_set_irq_cb(cpu->pcmcia[1],
                        qdev_get_gpio_in(cpu->gpio, SPITZ_GPIO_CF2_IRQ),
                        qdev_get_gpio_in(cpu->gpio, SPITZ_GPIO_CF2_CD));
}

/* Board init.  */
enum spitz_model_e { spitz, akita, borzoi, terrier };

#define SPITZ_RAM	0x04000000
#define SPITZ_ROM	0x00800000

static struct arm_boot_info spitz_binfo = {
    .loader_start = PXA2XX_SDRAM_BASE,
    .ram_size = 0x04000000,
};

static void spitz_common_init(QEMUMachineInitArgs *args,
                              enum spitz_model_e model, int arm_id)
{
    PXA2xxState *mpu;
    DeviceState *scp0, *scp1 = NULL;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *rom = g_new(MemoryRegion, 1);
    const char *cpu_model = args->cpu_model;

    if (!cpu_model)
        cpu_model = (model == terrier) ? "pxa270-c5" : "pxa270-c0";

    /* Setup CPU & memory */
    mpu = pxa270_init(address_space_mem, spitz_binfo.ram_size, cpu_model);

    sl_flash_register(mpu, (model == spitz) ? FLASH_128M : FLASH_1024M);

    memory_region_init_ram(rom, "spitz.rom", SPITZ_ROM);
    vmstate_register_ram_global(rom);
    memory_region_set_readonly(rom, true);
    memory_region_add_subregion(address_space_mem, 0, rom);

    /* Setup peripherals */
    spitz_keyboard_register(mpu);

    spitz_ssp_attach(mpu);

    scp0 = sysbus_create_simple("scoop", 0x10800000, NULL);
    if (model != akita) {
        scp1 = sysbus_create_simple("scoop", 0x08800040, NULL);
    }

    spitz_scoop_gpio_setup(mpu, scp0, scp1);

    spitz_gpio_setup(mpu, (model == akita) ? 1 : 2);

    spitz_i2c_setup(mpu);

    if (model == akita)
        spitz_akita_i2c_setup(mpu);

    if (model == terrier)
        /* A 6.0 GB microdrive is permanently sitting in CF slot 1.  */
        spitz_microdrive_attach(mpu, 1);
    else if (model != akita)
        /* A 4.0 GB microdrive is permanently sitting in CF slot 0.  */
        spitz_microdrive_attach(mpu, 0);

    spitz_binfo.kernel_filename = args->kernel_filename;
    spitz_binfo.kernel_cmdline = args->kernel_cmdline;
    spitz_binfo.initrd_filename = args->initrd_filename;
    spitz_binfo.board_id = arm_id;
    arm_load_kernel(mpu->cpu, &spitz_binfo);
    sl_bootparam_write(SL_PXA_PARAM_BASE);
}

static void spitz_init(QEMUMachineInitArgs *args)
{
    spitz_common_init(args, spitz, 0x2c9);
}

static void borzoi_init(QEMUMachineInitArgs *args)
{
    spitz_common_init(args, borzoi, 0x33f);
}

static void akita_init(QEMUMachineInitArgs *args)
{
    spitz_common_init(args, akita, 0x2e8);
}

static void terrier_init(QEMUMachineInitArgs *args)
{
    spitz_common_init(args, terrier, 0x33f);
}

static QEMUMachine akitapda_machine = {
    .name = "akita",
    .desc = "Akita PDA (PXA270)",
    .init = akita_init,
};

static QEMUMachine spitzpda_machine = {
    .name = "spitz",
    .desc = "Spitz PDA (PXA270)",
    .init = spitz_init,
};

static QEMUMachine borzoipda_machine = {
    .name = "borzoi",
    .desc = "Borzoi PDA (PXA270)",
    .init = borzoi_init,
};

static QEMUMachine terrierpda_machine = {
    .name = "terrier",
    .desc = "Terrier PDA (PXA270)",
    .init = terrier_init,
};

static void spitz_machine_init(void)
{
    qemu_register_machine(&akitapda_machine);
    qemu_register_machine(&spitzpda_machine);
    qemu_register_machine(&borzoipda_machine);
    qemu_register_machine(&terrierpda_machine);
}

machine_init(spitz_machine_init);

static bool is_version_0(void *opaque, int version_id)
{
    return version_id == 0;
}

static VMStateDescription vmstate_sl_nand_info = {
    .name = "sl-nand",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .fields = (VMStateField []) {
        VMSTATE_UINT8(ctl, SLNANDState),
        VMSTATE_STRUCT(ecc, SLNANDState, 0, vmstate_ecc_state, ECCState),
        VMSTATE_END_OF_LIST(),
    },
};

static Property sl_nand_properties[] = {
    DEFINE_PROP_UINT8("manf_id", SLNANDState, manf_id, NAND_MFR_SAMSUNG),
    DEFINE_PROP_UINT8("chip_id", SLNANDState, chip_id, 0xf1),
    DEFINE_PROP_END_OF_LIST(),
};

static void sl_nand_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = sl_nand_init;
    dc->vmsd = &vmstate_sl_nand_info;
    dc->props = sl_nand_properties;
}

static TypeInfo sl_nand_info = {
    .name          = "sl-nand",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SLNANDState),
    .class_init    = sl_nand_class_init,
};

static VMStateDescription vmstate_spitz_kbd = {
    .name = "spitz-keyboard",
    .version_id = 1,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .post_load = spitz_keyboard_post_load,
    .fields = (VMStateField []) {
        VMSTATE_UINT16(sense_state, SpitzKeyboardState),
        VMSTATE_UINT16(strobe_state, SpitzKeyboardState),
        VMSTATE_UNUSED_TEST(is_version_0, 5),
        VMSTATE_END_OF_LIST(),
    },
};

static Property spitz_keyboard_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void spitz_keyboard_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = spitz_keyboard_init;
    dc->vmsd = &vmstate_spitz_kbd;
    dc->props = spitz_keyboard_properties;
}

static TypeInfo spitz_keyboard_info = {
    .name          = "spitz-keyboard",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SpitzKeyboardState),
    .class_init    = spitz_keyboard_class_init,
};

static const VMStateDescription vmstate_corgi_ssp_regs = {
    .name = "corgi-ssp",
    .version_id = 2,
    .minimum_version_id = 2,
    .minimum_version_id_old = 2,
    .fields = (VMStateField []) {
        VMSTATE_SSI_SLAVE(ssidev, CorgiSSPState),
        VMSTATE_UINT32_ARRAY(enable, CorgiSSPState, 3),
        VMSTATE_END_OF_LIST(),
    }
};

static void corgi_ssp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SSISlaveClass *k = SSI_SLAVE_CLASS(klass);

    k->init = corgi_ssp_init;
    k->transfer = corgi_ssp_transfer;
    dc->vmsd = &vmstate_corgi_ssp_regs;
}

static TypeInfo corgi_ssp_info = {
    .name          = "corgi-ssp",
    .parent        = TYPE_SSI_SLAVE,
    .instance_size = sizeof(CorgiSSPState),
    .class_init    = corgi_ssp_class_init,
};

static const VMStateDescription vmstate_spitz_lcdtg_regs = {
    .name = "spitz-lcdtg",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField []) {
        VMSTATE_SSI_SLAVE(ssidev, SpitzLCDTG),
        VMSTATE_UINT32(bl_intensity, SpitzLCDTG),
        VMSTATE_UINT32(bl_power, SpitzLCDTG),
        VMSTATE_END_OF_LIST(),
    }
};

static void spitz_lcdtg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SSISlaveClass *k = SSI_SLAVE_CLASS(klass);

    k->init = spitz_lcdtg_init;
    k->transfer = spitz_lcdtg_transfer;
    dc->vmsd = &vmstate_spitz_lcdtg_regs;
}

static TypeInfo spitz_lcdtg_info = {
    .name          = "spitz-lcdtg",
    .parent        = TYPE_SSI_SLAVE,
    .instance_size = sizeof(SpitzLCDTG),
    .class_init    = spitz_lcdtg_class_init,
};

static void spitz_register_types(void)
{
    type_register_static(&corgi_ssp_info);
    type_register_static(&spitz_lcdtg_info);
    type_register_static(&spitz_keyboard_info);
    type_register_static(&sl_nand_info);
}

type_init(spitz_register_types)
