/*
 * Nokia N-series internet tablets.
 *
 * Copyright (C) 2007 Nokia Corporation
 * Written by Andrzej Zaborowski <andrew@openedhand.com>
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

#include "qemu-common.h"
#include "sysemu.h"
#include "omap.h"
#include "arm-misc.h"
#include "irq.h"
#include "console.h"
#include "boards.h"
#include "i2c.h"
#include "devices.h"
#include "flash.h"
#include "hw.h"
#include "bt.h"

/* Nokia N8x0 support */
struct n800_s {
    struct omap_mpu_state_s *cpu;

    struct rfbi_chip_s blizzard;
    struct {
        void *opaque;
        uint32_t (*txrx)(void *opaque, uint32_t value, int len);
        uWireSlave *chip;
    } ts;
    i2c_bus *i2c;

    int keymap[0x80];
    i2c_slave *kbd;

    TUSBState *usb;
    void *retu;
    void *tahvo;
    void *nand;
};

/* GPIO pins */
#define N8X0_TUSB_ENABLE_GPIO		0
#define N800_MMC2_WP_GPIO		8
#define N800_UNKNOWN_GPIO0		9	/* out */
#define N810_MMC2_VIOSD_GPIO		9
#define N810_HEADSET_AMP_GPIO		10
#define N800_CAM_TURN_GPIO		12
#define N810_GPS_RESET_GPIO		12
#define N800_BLIZZARD_POWERDOWN_GPIO	15
#define N800_MMC1_WP_GPIO		23
#define N810_MMC2_VSD_GPIO		23
#define N8X0_ONENAND_GPIO		26
#define N810_BLIZZARD_RESET_GPIO	30
#define N800_UNKNOWN_GPIO2		53	/* out */
#define N8X0_TUSB_INT_GPIO		58
#define N8X0_BT_WKUP_GPIO		61
#define N8X0_STI_GPIO			62
#define N8X0_CBUS_SEL_GPIO		64
#define N8X0_CBUS_DAT_GPIO		65
#define N8X0_CBUS_CLK_GPIO		66
#define N8X0_WLAN_IRQ_GPIO		87
#define N8X0_BT_RESET_GPIO		92
#define N8X0_TEA5761_CS_GPIO		93
#define N800_UNKNOWN_GPIO		94
#define N810_TSC_RESET_GPIO		94
#define N800_CAM_ACT_GPIO		95
#define N810_GPS_WAKEUP_GPIO		95
#define N8X0_MMC_CS_GPIO		96
#define N8X0_WLAN_PWR_GPIO		97
#define N8X0_BT_HOST_WKUP_GPIO		98
#define N810_SPEAKER_AMP_GPIO		101
#define N810_KB_LOCK_GPIO		102
#define N800_TSC_TS_GPIO		103
#define N810_TSC_TS_GPIO		106
#define N8X0_HEADPHONE_GPIO		107
#define N8X0_RETU_GPIO			108
#define N800_TSC_KP_IRQ_GPIO		109
#define N810_KEYBOARD_GPIO		109
#define N800_BAT_COVER_GPIO		110
#define N810_SLIDE_GPIO			110
#define N8X0_TAHVO_GPIO			111
#define N800_UNKNOWN_GPIO4		112	/* out */
#define N810_SLEEPX_LED_GPIO		112
#define N800_TSC_RESET_GPIO		118	/* ? */
#define N810_AIC33_RESET_GPIO		118
#define N800_TSC_UNKNOWN_GPIO		119	/* out */
#define N8X0_TMP105_GPIO		125

/* Config */
#define BT_UART				0
#define XLDR_LL_UART			1

/* Addresses on the I2C bus 0 */
#define N810_TLV320AIC33_ADDR		0x18	/* Audio CODEC */
#define N8X0_TCM825x_ADDR		0x29	/* Camera */
#define N810_LP5521_ADDR		0x32	/* LEDs */
#define N810_TSL2563_ADDR		0x3d	/* Light sensor */
#define N810_LM8323_ADDR		0x45	/* Keyboard */
/* Addresses on the I2C bus 1 */
#define N8X0_TMP105_ADDR		0x48	/* Temperature sensor */
#define N8X0_MENELAUS_ADDR		0x72	/* Power management */

/* Chipselects on GPMC NOR interface */
#define N8X0_ONENAND_CS			0
#define N8X0_USB_ASYNC_CS		1
#define N8X0_USB_SYNC_CS		4

#define N8X0_BD_ADDR			0x00, 0x1a, 0x89, 0x9e, 0x3e, 0x81

static void n800_mmc_cs_cb(void *opaque, int line, int level)
{
    /* TODO: this seems to actually be connected to the menelaus, to
     * which also both MMC slots connect.  */
    omap_mmc_enable((struct omap_mmc_s *) opaque, !level);

    printf("%s: MMC slot %i active\n", __FUNCTION__, level + 1);
}

static void n8x0_gpio_setup(struct n800_s *s)
{
    qemu_irq *mmc_cs = qemu_allocate_irqs(n800_mmc_cs_cb, s->cpu->mmc, 1);
    omap2_gpio_out_set(s->cpu->gpif, N8X0_MMC_CS_GPIO, mmc_cs[0]);

    qemu_irq_lower(omap2_gpio_in_get(s->cpu->gpif, N800_BAT_COVER_GPIO)[0]);
}

#define MAEMO_CAL_HEADER(...)				\
    'C',  'o',  'n',  'F',  0x02, 0x00, 0x04, 0x00,	\
    __VA_ARGS__,					\
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

static const uint8_t n8x0_cal_wlan_mac[] = {
    MAEMO_CAL_HEADER('w', 'l', 'a', 'n', '-', 'm', 'a', 'c')
    0x1c, 0x00, 0x00, 0x00, 0x47, 0xd6, 0x69, 0xb3,
    0x30, 0x08, 0xa0, 0x83, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00,
    0x89, 0x00, 0x00, 0x00, 0x9e, 0x00, 0x00, 0x00,
    0x5d, 0x00, 0x00, 0x00, 0xc1, 0x00, 0x00, 0x00,
};

static const uint8_t n8x0_cal_bt_id[] = {
    MAEMO_CAL_HEADER('b', 't', '-', 'i', 'd', 0, 0, 0)
    0x0a, 0x00, 0x00, 0x00, 0xa3, 0x4b, 0xf6, 0x96,
    0xa8, 0xeb, 0xb2, 0x41, 0x00, 0x00, 0x00, 0x00,
    N8X0_BD_ADDR,
};

static void n8x0_nand_setup(struct n800_s *s)
{
    char *otp_region;

    /* Either ec40xx or ec48xx are OK for the ID */
    omap_gpmc_attach(s->cpu->gpmc, N8X0_ONENAND_CS, 0, onenand_base_update,
                    onenand_base_unmap,
                    (s->nand = onenand_init(0xec4800, 1,
                                            omap2_gpio_in_get(s->cpu->gpif,
                                                    N8X0_ONENAND_GPIO)[0])));
    otp_region = onenand_raw_otp(s->nand);

    memcpy(otp_region + 0x000, n8x0_cal_wlan_mac, sizeof(n8x0_cal_wlan_mac));
    memcpy(otp_region + 0x800, n8x0_cal_bt_id, sizeof(n8x0_cal_bt_id));
    /* XXX: in theory should also update the OOB for both pages */
}

static void n8x0_i2c_setup(struct n800_s *s)
{
    qemu_irq tmp_irq = omap2_gpio_in_get(s->cpu->gpif, N8X0_TMP105_GPIO)[0];

    /* Attach the CPU on one end of our I2C bus.  */
    s->i2c = omap_i2c_bus(s->cpu->i2c[0]);

    /* Attach a menelaus PM chip */
    i2c_set_slave_address(
                    twl92230_init(s->i2c,
                            s->cpu->irq[0][OMAP_INT_24XX_SYS_NIRQ]),
                    N8X0_MENELAUS_ADDR);

    /* Attach a TMP105 PM chip (A0 wired to ground) */
    i2c_set_slave_address(tmp105_init(s->i2c, tmp_irq), N8X0_TMP105_ADDR);
}

/* Touchscreen and keypad controller */
static MouseTransformInfo n800_pointercal = {
    .x = 800,
    .y = 480,
    .a = { 14560, -68, -3455208, -39, -9621, 35152972, 65536 },
};

static MouseTransformInfo n810_pointercal = {
    .x = 800,
    .y = 480,
    .a = { 15041, 148, -4731056, 171, -10238, 35933380, 65536 },
};

#define RETU_KEYCODE	61	/* F3 */

static void n800_key_event(void *opaque, int keycode)
{
    struct n800_s *s = (struct n800_s *) opaque;
    int code = s->keymap[keycode & 0x7f];

    if (code == -1) {
        if ((keycode & 0x7f) == RETU_KEYCODE)
            retu_key_event(s->retu, !(keycode & 0x80));
        return;
    }

    tsc210x_key_event(s->ts.chip, code, !(keycode & 0x80));
}

static const int n800_keys[16] = {
    -1,
    72,	/* Up */
    63,	/* Home (F5) */
    -1,
    75,	/* Left */
    28,	/* Enter */
    77,	/* Right */
    -1,
     1,	/* Cycle (ESC) */
    80,	/* Down */
    62,	/* Menu (F4) */
    -1,
    66,	/* Zoom- (F8) */
    64,	/* FullScreen (F6) */
    65,	/* Zoom+ (F7) */
    -1,
};

static void n800_tsc_kbd_setup(struct n800_s *s)
{
    int i;

    /* XXX: are the three pins inverted inside the chip between the
     * tsc and the cpu (N4111)?  */
    qemu_irq penirq = 0;	/* NC */
    qemu_irq kbirq = omap2_gpio_in_get(s->cpu->gpif, N800_TSC_KP_IRQ_GPIO)[0];
    qemu_irq dav = omap2_gpio_in_get(s->cpu->gpif, N800_TSC_TS_GPIO)[0];

    s->ts.chip = tsc2301_init(penirq, kbirq, dav);
    s->ts.opaque = s->ts.chip->opaque;
    s->ts.txrx = tsc210x_txrx;

    for (i = 0; i < 0x80; i ++)
        s->keymap[i] = -1;
    for (i = 0; i < 0x10; i ++)
        if (n800_keys[i] >= 0)
            s->keymap[n800_keys[i]] = i;

    qemu_add_kbd_event_handler(n800_key_event, s);

    tsc210x_set_transform(s->ts.chip, &n800_pointercal);
}

static void n810_tsc_setup(struct n800_s *s)
{
    qemu_irq pintdav = omap2_gpio_in_get(s->cpu->gpif, N810_TSC_TS_GPIO)[0];

    s->ts.opaque = tsc2005_init(pintdav);
    s->ts.txrx = tsc2005_txrx;

    tsc2005_set_transform(s->ts.opaque, &n810_pointercal);
}

/* N810 Keyboard controller */
static void n810_key_event(void *opaque, int keycode)
{
    struct n800_s *s = (struct n800_s *) opaque;
    int code = s->keymap[keycode & 0x7f];

    if (code == -1) {
        if ((keycode & 0x7f) == RETU_KEYCODE)
            retu_key_event(s->retu, !(keycode & 0x80));
        return;
    }

    lm832x_key_event(s->kbd, code, !(keycode & 0x80));
}

#define M	0

static int n810_keys[0x80] = {
    [0x01] = 16,	/* Q */
    [0x02] = 37,	/* K */
    [0x03] = 24,	/* O */
    [0x04] = 25,	/* P */
    [0x05] = 14,	/* Backspace */
    [0x06] = 30,	/* A */
    [0x07] = 31,	/* S */
    [0x08] = 32,	/* D */
    [0x09] = 33,	/* F */
    [0x0a] = 34,	/* G */
    [0x0b] = 35,	/* H */
    [0x0c] = 36,	/* J */

    [0x11] = 17,	/* W */
    [0x12] = 62,	/* Menu (F4) */
    [0x13] = 38,	/* L */
    [0x14] = 40,	/* ' (Apostrophe) */
    [0x16] = 44,	/* Z */
    [0x17] = 45,	/* X */
    [0x18] = 46,	/* C */
    [0x19] = 47,	/* V */
    [0x1a] = 48,	/* B */
    [0x1b] = 49,	/* N */
    [0x1c] = 42,	/* Shift (Left shift) */
    [0x1f] = 65,	/* Zoom+ (F7) */

    [0x21] = 18,	/* E */
    [0x22] = 39,	/* ; (Semicolon) */
    [0x23] = 12,	/* - (Minus) */
    [0x24] = 13,	/* = (Equal) */
    [0x2b] = 56,	/* Fn (Left Alt) */
    [0x2c] = 50,	/* M */
    [0x2f] = 66,	/* Zoom- (F8) */

    [0x31] = 19,	/* R */
    [0x32] = 29 | M,	/* Right Ctrl */
    [0x34] = 57,	/* Space */
    [0x35] = 51,	/* , (Comma) */
    [0x37] = 72 | M,	/* Up */
    [0x3c] = 82 | M,	/* Compose (Insert) */
    [0x3f] = 64,	/* FullScreen (F6) */

    [0x41] = 20,	/* T */
    [0x44] = 52,	/* . (Dot) */
    [0x46] = 77 | M,	/* Right */
    [0x4f] = 63,	/* Home (F5) */
    [0x51] = 21,	/* Y */
    [0x53] = 80 | M,	/* Down */
    [0x55] = 28,	/* Enter */
    [0x5f] =  1,	/* Cycle (ESC) */

    [0x61] = 22,	/* U */
    [0x64] = 75 | M,	/* Left */

    [0x71] = 23,	/* I */
#if 0
    [0x75] = 28 | M,	/* KP Enter (KP Enter) */
#else
    [0x75] = 15,	/* KP Enter (Tab) */
#endif
};

#undef M

static void n810_kbd_setup(struct n800_s *s)
{
    qemu_irq kbd_irq = omap2_gpio_in_get(s->cpu->gpif, N810_KEYBOARD_GPIO)[0];
    int i;

    for (i = 0; i < 0x80; i ++)
        s->keymap[i] = -1;
    for (i = 0; i < 0x80; i ++)
        if (n810_keys[i] > 0)
            s->keymap[n810_keys[i]] = i;

    qemu_add_kbd_event_handler(n810_key_event, s);

    /* Attach the LM8322 keyboard to the I2C bus,
     * should happen in n8x0_i2c_setup and s->kbd be initialised here.  */
    s->kbd = lm8323_init(s->i2c, kbd_irq);
    i2c_set_slave_address(s->kbd, N810_LM8323_ADDR);
}

/* LCD MIPI DBI-C controller (URAL) */
struct mipid_s {
    int resp[4];
    int param[4];
    int p;
    int pm;
    int cmd;

    int sleep;
    int booster;
    int te;
    int selfcheck;
    int partial;
    int normal;
    int vscr;
    int invert;
    int onoff;
    int gamma;
    uint32_t id;
};

static void mipid_reset(struct mipid_s *s)
{
    if (!s->sleep)
        fprintf(stderr, "%s: Display off\n", __FUNCTION__);

    s->pm = 0;
    s->cmd = 0;

    s->sleep = 1;
    s->booster = 0;
    s->selfcheck =
            (1 << 7) |	/* Register loading OK.  */
            (1 << 5) |	/* The chip is attached.  */
            (1 << 4);	/* Display glass still in one piece.  */
    s->te = 0;
    s->partial = 0;
    s->normal = 1;
    s->vscr = 0;
    s->invert = 0;
    s->onoff = 1;
    s->gamma = 0;
}

static uint32_t mipid_txrx(void *opaque, uint32_t cmd, int len)
{
    struct mipid_s *s = (struct mipid_s *) opaque;
    uint8_t ret;

    if (len > 9)
        hw_error("%s: FIXME: bad SPI word width %i\n", __FUNCTION__, len);

    if (s->p >= ARRAY_SIZE(s->resp))
        ret = 0;
    else
        ret = s->resp[s->p ++];
    if (s->pm --> 0)
        s->param[s->pm] = cmd;
    else
        s->cmd = cmd;

    switch (s->cmd) {
    case 0x00:	/* NOP */
        break;

    case 0x01:	/* SWRESET */
        mipid_reset(s);
        break;

    case 0x02:	/* BSTROFF */
        s->booster = 0;
        break;
    case 0x03:	/* BSTRON */
        s->booster = 1;
        break;

    case 0x04:	/* RDDID */
        s->p = 0;
        s->resp[0] = (s->id >> 16) & 0xff;
        s->resp[1] = (s->id >>  8) & 0xff;
        s->resp[2] = (s->id >>  0) & 0xff;
        break;

    case 0x06:	/* RD_RED */
    case 0x07:	/* RD_GREEN */
        /* XXX the bootloader sometimes issues RD_BLUE meaning RDDID so
         * for the bootloader one needs to change this.  */
    case 0x08:	/* RD_BLUE */
        s->p = 0;
        /* TODO: return first pixel components */
        s->resp[0] = 0x01;
        break;

    case 0x09:	/* RDDST */
        s->p = 0;
        s->resp[0] = s->booster << 7;
        s->resp[1] = (5 << 4) | (s->partial << 2) |
                (s->sleep << 1) | s->normal;
        s->resp[2] = (s->vscr << 7) | (s->invert << 5) |
                (s->onoff << 2) | (s->te << 1) | (s->gamma >> 2);
        s->resp[3] = s->gamma << 6;
        break;

    case 0x0a:	/* RDDPM */
        s->p = 0;
        s->resp[0] = (s->onoff << 2) | (s->normal << 3) | (s->sleep << 4) |
                (s->partial << 5) | (s->sleep << 6) | (s->booster << 7);
        break;
    case 0x0b:	/* RDDMADCTR */
        s->p = 0;
        s->resp[0] = 0;
        break;
    case 0x0c:	/* RDDCOLMOD */
        s->p = 0;
        s->resp[0] = 5;	/* 65K colours */
        break;
    case 0x0d:	/* RDDIM */
        s->p = 0;
        s->resp[0] = (s->invert << 5) | (s->vscr << 7) | s->gamma;
        break;
    case 0x0e:	/* RDDSM */
        s->p = 0;
        s->resp[0] = s->te << 7;
        break;
    case 0x0f:	/* RDDSDR */
        s->p = 0;
        s->resp[0] = s->selfcheck;
        break;

    case 0x10:	/* SLPIN */
        s->sleep = 1;
        break;
    case 0x11:	/* SLPOUT */
        s->sleep = 0;
        s->selfcheck ^= 1 << 6;	/* POFF self-diagnosis Ok */
        break;

    case 0x12:	/* PTLON */
        s->partial = 1;
        s->normal = 0;
        s->vscr = 0;
        break;
    case 0x13:	/* NORON */
        s->partial = 0;
        s->normal = 1;
        s->vscr = 0;
        break;

    case 0x20:	/* INVOFF */
        s->invert = 0;
        break;
    case 0x21:	/* INVON */
        s->invert = 1;
        break;

    case 0x22:	/* APOFF */
    case 0x23:	/* APON */
        goto bad_cmd;

    case 0x25:	/* WRCNTR */
        if (s->pm < 0)
            s->pm = 1;
        goto bad_cmd;

    case 0x26:	/* GAMSET */
        if (!s->pm)
            s->gamma = ffs(s->param[0] & 0xf) - 1;
        else if (s->pm < 0)
            s->pm = 1;
        break;

    case 0x28:	/* DISPOFF */
        s->onoff = 0;
        fprintf(stderr, "%s: Display off\n", __FUNCTION__);
        break;
    case 0x29:	/* DISPON */
        s->onoff = 1;
        fprintf(stderr, "%s: Display on\n", __FUNCTION__);
        break;

    case 0x2a:	/* CASET */
    case 0x2b:	/* RASET */
    case 0x2c:	/* RAMWR */
    case 0x2d:	/* RGBSET */
    case 0x2e:	/* RAMRD */
    case 0x30:	/* PTLAR */
    case 0x33:	/* SCRLAR */
        goto bad_cmd;

    case 0x34:	/* TEOFF */
        s->te = 0;
        break;
    case 0x35:	/* TEON */
        if (!s->pm)
            s->te = 1;
        else if (s->pm < 0)
            s->pm = 1;
        break;

    case 0x36:	/* MADCTR */
        goto bad_cmd;

    case 0x37:	/* VSCSAD */
        s->partial = 0;
        s->normal = 0;
        s->vscr = 1;
        break;

    case 0x38:	/* IDMOFF */
    case 0x39:	/* IDMON */
    case 0x3a:	/* COLMOD */
        goto bad_cmd;

    case 0xb0:	/* CLKINT / DISCTL */
    case 0xb1:	/* CLKEXT */
        if (s->pm < 0)
            s->pm = 2;
        break;

    case 0xb4:	/* FRMSEL */
        break;

    case 0xb5:	/* FRM8SEL */
    case 0xb6:	/* TMPRNG / INIESC */
    case 0xb7:	/* TMPHIS / NOP2 */
    case 0xb8:	/* TMPREAD / MADCTL */
    case 0xba:	/* DISTCTR */
    case 0xbb:	/* EPVOL */
        goto bad_cmd;

    case 0xbd:	/* Unknown */
        s->p = 0;
        s->resp[0] = 0;
        s->resp[1] = 1;
        break;

    case 0xc2:	/* IFMOD */
        if (s->pm < 0)
            s->pm = 2;
        break;

    case 0xc6:	/* PWRCTL */
    case 0xc7:	/* PPWRCTL */
    case 0xd0:	/* EPWROUT */
    case 0xd1:	/* EPWRIN */
    case 0xd4:	/* RDEV */
    case 0xd5:	/* RDRR */
        goto bad_cmd;

    case 0xda:	/* RDID1 */
        s->p = 0;
        s->resp[0] = (s->id >> 16) & 0xff;
        break;
    case 0xdb:	/* RDID2 */
        s->p = 0;
        s->resp[0] = (s->id >>  8) & 0xff;
        break;
    case 0xdc:	/* RDID3 */
        s->p = 0;
        s->resp[0] = (s->id >>  0) & 0xff;
        break;

    default:
    bad_cmd:
        fprintf(stderr, "%s: unknown command %02x\n", __FUNCTION__, s->cmd);
        break;
    }

    return ret;
}

static void *mipid_init(void)
{
    struct mipid_s *s = (struct mipid_s *) qemu_mallocz(sizeof(*s));

    s->id = 0x838f03;
    mipid_reset(s);

    return s;
}

static void n8x0_spi_setup(struct n800_s *s)
{
    void *tsc = s->ts.opaque;
    void *mipid = mipid_init();

    omap_mcspi_attach(s->cpu->mcspi[0], s->ts.txrx, tsc, 0);
    omap_mcspi_attach(s->cpu->mcspi[0], mipid_txrx, mipid, 1);
}

/* This task is normally performed by the bootloader.  If we're loading
 * a kernel directly, we need to enable the Blizzard ourselves.  */
static void n800_dss_init(struct rfbi_chip_s *chip)
{
    uint8_t *fb_blank;

    chip->write(chip->opaque, 0, 0x2a);		/* LCD Width register */
    chip->write(chip->opaque, 1, 0x64);
    chip->write(chip->opaque, 0, 0x2c);		/* LCD HNDP register */
    chip->write(chip->opaque, 1, 0x1e);
    chip->write(chip->opaque, 0, 0x2e);		/* LCD Height 0 register */
    chip->write(chip->opaque, 1, 0xe0);
    chip->write(chip->opaque, 0, 0x30);		/* LCD Height 1 register */
    chip->write(chip->opaque, 1, 0x01);
    chip->write(chip->opaque, 0, 0x32);		/* LCD VNDP register */
    chip->write(chip->opaque, 1, 0x06);
    chip->write(chip->opaque, 0, 0x68);		/* Display Mode register */
    chip->write(chip->opaque, 1, 1);		/* Enable bit */

    chip->write(chip->opaque, 0, 0x6c);	
    chip->write(chip->opaque, 1, 0x00);		/* Input X Start Position */
    chip->write(chip->opaque, 1, 0x00);		/* Input X Start Position */
    chip->write(chip->opaque, 1, 0x00);		/* Input Y Start Position */
    chip->write(chip->opaque, 1, 0x00);		/* Input Y Start Position */
    chip->write(chip->opaque, 1, 0x1f);		/* Input X End Position */
    chip->write(chip->opaque, 1, 0x03);		/* Input X End Position */
    chip->write(chip->opaque, 1, 0xdf);		/* Input Y End Position */
    chip->write(chip->opaque, 1, 0x01);		/* Input Y End Position */
    chip->write(chip->opaque, 1, 0x00);		/* Output X Start Position */
    chip->write(chip->opaque, 1, 0x00);		/* Output X Start Position */
    chip->write(chip->opaque, 1, 0x00);		/* Output Y Start Position */
    chip->write(chip->opaque, 1, 0x00);		/* Output Y Start Position */
    chip->write(chip->opaque, 1, 0x1f);		/* Output X End Position */
    chip->write(chip->opaque, 1, 0x03);		/* Output X End Position */
    chip->write(chip->opaque, 1, 0xdf);		/* Output Y End Position */
    chip->write(chip->opaque, 1, 0x01);		/* Output Y End Position */
    chip->write(chip->opaque, 1, 0x01);		/* Input Data Format */
    chip->write(chip->opaque, 1, 0x01);		/* Data Source Select */

    fb_blank = memset(qemu_malloc(800 * 480 * 2), 0xff, 800 * 480 * 2);
    /* Display Memory Data Port */
    chip->block(chip->opaque, 1, fb_blank, 800 * 480 * 2, 800);
    free(fb_blank);
}

static void n8x0_dss_setup(struct n800_s *s)
{
    s->blizzard.opaque = s1d13745_init(0);
    s->blizzard.block = s1d13745_write_block;
    s->blizzard.write = s1d13745_write;
    s->blizzard.read = s1d13745_read;

    omap_rfbi_attach(s->cpu->dss, 0, &s->blizzard);
}

static void n8x0_cbus_setup(struct n800_s *s)
{
    qemu_irq dat_out = omap2_gpio_in_get(s->cpu->gpif, N8X0_CBUS_DAT_GPIO)[0];
    qemu_irq retu_irq = omap2_gpio_in_get(s->cpu->gpif, N8X0_RETU_GPIO)[0];
    qemu_irq tahvo_irq = omap2_gpio_in_get(s->cpu->gpif, N8X0_TAHVO_GPIO)[0];

    CBus *cbus = cbus_init(dat_out);

    omap2_gpio_out_set(s->cpu->gpif, N8X0_CBUS_CLK_GPIO, cbus->clk);
    omap2_gpio_out_set(s->cpu->gpif, N8X0_CBUS_DAT_GPIO, cbus->dat);
    omap2_gpio_out_set(s->cpu->gpif, N8X0_CBUS_SEL_GPIO, cbus->sel);

    cbus_attach(cbus, s->retu = retu_init(retu_irq, 1));
    cbus_attach(cbus, s->tahvo = tahvo_init(tahvo_irq, 1));
}

static void n8x0_uart_setup(struct n800_s *s)
{
    CharDriverState *radio = uart_hci_init(
                    omap2_gpio_in_get(s->cpu->gpif,
                            N8X0_BT_HOST_WKUP_GPIO)[0]);

    omap2_gpio_out_set(s->cpu->gpif, N8X0_BT_RESET_GPIO,
                    csrhci_pins_get(radio)[csrhci_pin_reset]);
    omap2_gpio_out_set(s->cpu->gpif, N8X0_BT_WKUP_GPIO,
                    csrhci_pins_get(radio)[csrhci_pin_wakeup]);

    omap_uart_attach(s->cpu->uart[BT_UART], radio);
}

static void n8x0_usb_power_cb(void *opaque, int line, int level)
{
    struct n800_s *s = opaque;

    tusb6010_power(s->usb, level);
}

static void n8x0_usb_setup(struct n800_s *s)
{
    qemu_irq tusb_irq = omap2_gpio_in_get(s->cpu->gpif, N8X0_TUSB_INT_GPIO)[0];
    qemu_irq tusb_pwr = qemu_allocate_irqs(n8x0_usb_power_cb, s, 1)[0];
    TUSBState *tusb = tusb6010_init(tusb_irq);

    /* Using the NOR interface */
    omap_gpmc_attach(s->cpu->gpmc, N8X0_USB_ASYNC_CS,
                    tusb6010_async_io(tusb), 0, 0, tusb);
    omap_gpmc_attach(s->cpu->gpmc, N8X0_USB_SYNC_CS,
                    tusb6010_sync_io(tusb), 0, 0, tusb);

    s->usb = tusb;
    omap2_gpio_out_set(s->cpu->gpif, N8X0_TUSB_ENABLE_GPIO, tusb_pwr);
}

/* Setup done before the main bootloader starts by some early setup code
 * - used when we want to run the main bootloader in emulation.  This
 * isn't documented.  */
static uint32_t n800_pinout[104] = {
    0x080f00d8, 0x00d40808, 0x03080808, 0x080800d0,
    0x00dc0808, 0x0b0f0f00, 0x080800b4, 0x00c00808,
    0x08080808, 0x180800c4, 0x00b80000, 0x08080808,
    0x080800bc, 0x00cc0808, 0x08081818, 0x18180128,
    0x01241800, 0x18181818, 0x000000f0, 0x01300000,
    0x00001b0b, 0x1b0f0138, 0x00e0181b, 0x1b031b0b,
    0x180f0078, 0x00740018, 0x0f0f0f1a, 0x00000080,
    0x007c0000, 0x00000000, 0x00000088, 0x00840000,
    0x00000000, 0x00000094, 0x00980300, 0x0f180003,
    0x0000008c, 0x00900f0f, 0x0f0f1b00, 0x0f00009c,
    0x01140000, 0x1b1b0f18, 0x0818013c, 0x01400008,
    0x00001818, 0x000b0110, 0x010c1800, 0x0b030b0f,
    0x181800f4, 0x00f81818, 0x00000018, 0x000000fc,
    0x00401808, 0x00000000, 0x0f1b0030, 0x003c0008,
    0x00000000, 0x00000038, 0x00340000, 0x00000000,
    0x1a080070, 0x00641a1a, 0x08080808, 0x08080060,
    0x005c0808, 0x08080808, 0x08080058, 0x00540808,
    0x08080808, 0x0808006c, 0x00680808, 0x08080808,
    0x000000a8, 0x00b00000, 0x08080808, 0x000000a0,
    0x00a40000, 0x00000000, 0x08ff0050, 0x004c0808,
    0xffffffff, 0xffff0048, 0x0044ffff, 0xffffffff,
    0x000000ac, 0x01040800, 0x08080b0f, 0x18180100,
    0x01081818, 0x0b0b1808, 0x1a0300e4, 0x012c0b1a,
    0x02020018, 0x0b000134, 0x011c0800, 0x0b1b1b00,
    0x0f0000c8, 0x00ec181b, 0x000f0f02, 0x00180118,
    0x01200000, 0x0f0b1b1b, 0x0f0200e8, 0x0000020b,
};

static void n800_setup_nolo_tags(void *sram_base)
{
    int i;
    uint32_t *p = sram_base + 0x8000;
    uint32_t *v = sram_base + 0xa000;

    memset(p, 0, 0x3000);

    strcpy((void *) (p + 0), "QEMU N800");

    strcpy((void *) (p + 8), "F5");

    stl_raw(p + 10, 0x04f70000);
    strcpy((void *) (p + 9), "RX-34");

    /* RAM size in MB? */
    stl_raw(p + 12, 0x80);

    /* Pointer to the list of tags */
    stl_raw(p + 13, OMAP2_SRAM_BASE + 0x9000);

    /* The NOLO tags start here */
    p = sram_base + 0x9000;
#define ADD_TAG(tag, len)				\
    stw_raw((uint16_t *) p + 0, tag);			\
    stw_raw((uint16_t *) p + 1, len); p ++;		\
    stl_raw(p ++, OMAP2_SRAM_BASE | (((void *) v - sram_base) & 0xffff));

    /* OMAP STI console? Pin out settings? */
    ADD_TAG(0x6e01, 414);
    for (i = 0; i < ARRAY_SIZE(n800_pinout); i ++)
        stl_raw(v ++, n800_pinout[i]);

    /* Kernel memsize? */
    ADD_TAG(0x6e05, 1);
    stl_raw(v ++, 2);

    /* NOLO serial console */
    ADD_TAG(0x6e02, 4);
    stl_raw(v ++, XLDR_LL_UART);	/* UART number (1 - 3) */

#if 0
    /* CBUS settings (Retu/AVilma) */
    ADD_TAG(0x6e03, 6);
    stw_raw((uint16_t *) v + 0, 65);	/* CBUS GPIO0 */
    stw_raw((uint16_t *) v + 1, 66);	/* CBUS GPIO1 */
    stw_raw((uint16_t *) v + 2, 64);	/* CBUS GPIO2 */
    v += 2;
#endif

    /* Nokia ASIC BB5 (Retu/Tahvo) */
    ADD_TAG(0x6e0a, 4);
    stw_raw((uint16_t *) v + 0, 111);	/* "Retu" interrupt GPIO */
    stw_raw((uint16_t *) v + 1, 108);	/* "Tahvo" interrupt GPIO */
    v ++;

    /* LCD console? */
    ADD_TAG(0x6e04, 4);
    stw_raw((uint16_t *) v + 0, 30);	/* ??? */
    stw_raw((uint16_t *) v + 1, 24);	/* ??? */
    v ++;

#if 0
    /* LCD settings */
    ADD_TAG(0x6e06, 2);
    stw_raw((uint16_t *) (v ++), 15);	/* ??? */
#endif

    /* I^2C (Menelaus) */
    ADD_TAG(0x6e07, 4);
    stl_raw(v ++, 0x00720000);		/* ??? */

    /* Unknown */
    ADD_TAG(0x6e0b, 6);
    stw_raw((uint16_t *) v + 0, 94);	/* ??? */
    stw_raw((uint16_t *) v + 1, 23);	/* ??? */
    stw_raw((uint16_t *) v + 2, 0);	/* ??? */
    v += 2;

    /* OMAP gpio switch info */
    ADD_TAG(0x6e0c, 80);
    strcpy((void *) v, "bat_cover");	v += 3;
    stw_raw((uint16_t *) v + 0, 110);	/* GPIO num ??? */
    stw_raw((uint16_t *) v + 1, 1);	/* GPIO num ??? */
    v += 2;
    strcpy((void *) v, "cam_act");	v += 3;
    stw_raw((uint16_t *) v + 0, 95);	/* GPIO num ??? */
    stw_raw((uint16_t *) v + 1, 32);	/* GPIO num ??? */
    v += 2;
    strcpy((void *) v, "cam_turn");	v += 3;
    stw_raw((uint16_t *) v + 0, 12);	/* GPIO num ??? */
    stw_raw((uint16_t *) v + 1, 33);	/* GPIO num ??? */
    v += 2;
    strcpy((void *) v, "headphone");	v += 3;
    stw_raw((uint16_t *) v + 0, 107);	/* GPIO num ??? */
    stw_raw((uint16_t *) v + 1, 17);	/* GPIO num ??? */
    v += 2;

    /* Bluetooth */
    ADD_TAG(0x6e0e, 12);
    stl_raw(v ++, 0x5c623d01);		/* ??? */
    stl_raw(v ++, 0x00000201);		/* ??? */
    stl_raw(v ++, 0x00000000);		/* ??? */

    /* CX3110x WLAN settings */
    ADD_TAG(0x6e0f, 8);
    stl_raw(v ++, 0x00610025);		/* ??? */
    stl_raw(v ++, 0xffff0057);		/* ??? */

    /* MMC host settings */
    ADD_TAG(0x6e10, 12);
    stl_raw(v ++, 0xffff000f);		/* ??? */
    stl_raw(v ++, 0xffffffff);		/* ??? */
    stl_raw(v ++, 0x00000060);		/* ??? */

    /* OneNAND chip select */
    ADD_TAG(0x6e11, 10);
    stl_raw(v ++, 0x00000401);		/* ??? */
    stl_raw(v ++, 0x0002003a);		/* ??? */
    stl_raw(v ++, 0x00000002);		/* ??? */

    /* TEA5761 sensor settings */
    ADD_TAG(0x6e12, 2);
    stl_raw(v ++, 93);			/* GPIO num ??? */

#if 0
    /* Unknown tag */
    ADD_TAG(6e09, 0);

    /* Kernel UART / console */
    ADD_TAG(6e12, 0);
#endif

    /* End of the list */
    stl_raw(p ++, 0x00000000);
    stl_raw(p ++, 0x00000000);
}

/* This task is normally performed by the bootloader.  If we're loading
 * a kernel directly, we need to set up GPMC mappings ourselves.  */
static void n800_gpmc_init(struct n800_s *s)
{
    uint32_t config7 =
            (0xf << 8) |	/* MASKADDRESS */
            (1 << 6) |		/* CSVALID */
            (4 << 0);		/* BASEADDRESS */

    cpu_physical_memory_write(0x6800a078,		/* GPMC_CONFIG7_0 */
                    (void *) &config7, sizeof(config7));
}

/* Setup sequence done by the bootloader */
static void n8x0_boot_init(void *opaque)
{
    struct n800_s *s = (struct n800_s *) opaque;
    uint32_t buf;

    /* PRCM setup */
#define omap_writel(addr, val)	\
    buf = (val);			\
    cpu_physical_memory_write(addr, (void *) &buf, sizeof(buf))

    omap_writel(0x48008060, 0x41);		/* PRCM_CLKSRC_CTRL */
    omap_writel(0x48008070, 1);			/* PRCM_CLKOUT_CTRL */
    omap_writel(0x48008078, 0);			/* PRCM_CLKEMUL_CTRL */
    omap_writel(0x48008090, 0);			/* PRCM_VOLTSETUP */
    omap_writel(0x48008094, 0);			/* PRCM_CLKSSETUP */
    omap_writel(0x48008098, 0);			/* PRCM_POLCTRL */
    omap_writel(0x48008140, 2);			/* CM_CLKSEL_MPU */
    omap_writel(0x48008148, 0);			/* CM_CLKSTCTRL_MPU */
    omap_writel(0x48008158, 1);			/* RM_RSTST_MPU */
    omap_writel(0x480081c8, 0x15);		/* PM_WKDEP_MPU */
    omap_writel(0x480081d4, 0x1d4);		/* PM_EVGENCTRL_MPU */
    omap_writel(0x480081d8, 0);			/* PM_EVEGENONTIM_MPU */
    omap_writel(0x480081dc, 0);			/* PM_EVEGENOFFTIM_MPU */
    omap_writel(0x480081e0, 0xc);		/* PM_PWSTCTRL_MPU */
    omap_writel(0x48008200, 0x047e7ff7);	/* CM_FCLKEN1_CORE */
    omap_writel(0x48008204, 0x00000004);	/* CM_FCLKEN2_CORE */
    omap_writel(0x48008210, 0x047e7ff1);	/* CM_ICLKEN1_CORE */
    omap_writel(0x48008214, 0x00000004);	/* CM_ICLKEN2_CORE */
    omap_writel(0x4800821c, 0x00000000);	/* CM_ICLKEN4_CORE */
    omap_writel(0x48008230, 0);			/* CM_AUTOIDLE1_CORE */
    omap_writel(0x48008234, 0);			/* CM_AUTOIDLE2_CORE */
    omap_writel(0x48008238, 7);			/* CM_AUTOIDLE3_CORE */
    omap_writel(0x4800823c, 0);			/* CM_AUTOIDLE4_CORE */
    omap_writel(0x48008240, 0x04360626);	/* CM_CLKSEL1_CORE */
    omap_writel(0x48008244, 0x00000014);	/* CM_CLKSEL2_CORE */
    omap_writel(0x48008248, 0);			/* CM_CLKSTCTRL_CORE */
    omap_writel(0x48008300, 0x00000000);	/* CM_FCLKEN_GFX */
    omap_writel(0x48008310, 0x00000000);	/* CM_ICLKEN_GFX */
    omap_writel(0x48008340, 0x00000001);	/* CM_CLKSEL_GFX */
    omap_writel(0x48008400, 0x00000004);	/* CM_FCLKEN_WKUP */
    omap_writel(0x48008410, 0x00000004);	/* CM_ICLKEN_WKUP */
    omap_writel(0x48008440, 0x00000000);	/* CM_CLKSEL_WKUP */
    omap_writel(0x48008500, 0x000000cf);	/* CM_CLKEN_PLL */
    omap_writel(0x48008530, 0x0000000c);	/* CM_AUTOIDLE_PLL */
    omap_writel(0x48008540,			/* CM_CLKSEL1_PLL */
                    (0x78 << 12) | (6 << 8));
    omap_writel(0x48008544, 2);			/* CM_CLKSEL2_PLL */

    /* GPMC setup */
    n800_gpmc_init(s);

    /* Video setup */
    n800_dss_init(&s->blizzard);

    /* CPU setup */
    s->cpu->env->regs[15] = s->cpu->env->boot_info->loader_start;
    s->cpu->env->GE = 0x5;

    /* If the machine has a slided keyboard, open it */
    if (s->kbd)
        qemu_irq_raise(omap2_gpio_in_get(s->cpu->gpif, N810_SLIDE_GPIO)[0]);
}

#define OMAP_TAG_NOKIA_BT	0x4e01
#define OMAP_TAG_WLAN_CX3110X	0x4e02
#define OMAP_TAG_CBUS		0x4e03
#define OMAP_TAG_EM_ASIC_BB5	0x4e04

static struct omap_gpiosw_info_s {
    const char *name;
    int line;
    int type;
} n800_gpiosw_info[] = {
    {
        "bat_cover", N800_BAT_COVER_GPIO,
        OMAP_GPIOSW_TYPE_COVER | OMAP_GPIOSW_INVERTED,
    }, {
        "cam_act", N800_CAM_ACT_GPIO,
        OMAP_GPIOSW_TYPE_ACTIVITY,
    }, {
        "cam_turn", N800_CAM_TURN_GPIO,
        OMAP_GPIOSW_TYPE_ACTIVITY | OMAP_GPIOSW_INVERTED,
    }, {
        "headphone", N8X0_HEADPHONE_GPIO,
        OMAP_GPIOSW_TYPE_CONNECTION | OMAP_GPIOSW_INVERTED,
    },
    { 0 }
}, n810_gpiosw_info[] = {
    {
        "gps_reset", N810_GPS_RESET_GPIO,
        OMAP_GPIOSW_TYPE_ACTIVITY | OMAP_GPIOSW_OUTPUT,
    }, {
        "gps_wakeup", N810_GPS_WAKEUP_GPIO,
        OMAP_GPIOSW_TYPE_ACTIVITY | OMAP_GPIOSW_OUTPUT,
    }, {
        "headphone", N8X0_HEADPHONE_GPIO,
        OMAP_GPIOSW_TYPE_CONNECTION | OMAP_GPIOSW_INVERTED,
    }, {
        "kb_lock", N810_KB_LOCK_GPIO,
        OMAP_GPIOSW_TYPE_COVER | OMAP_GPIOSW_INVERTED,
    }, {
        "sleepx_led", N810_SLEEPX_LED_GPIO,
        OMAP_GPIOSW_TYPE_ACTIVITY | OMAP_GPIOSW_INVERTED | OMAP_GPIOSW_OUTPUT,
    }, {
        "slide", N810_SLIDE_GPIO,
        OMAP_GPIOSW_TYPE_COVER | OMAP_GPIOSW_INVERTED,
    },
    { 0 }
};

static struct omap_partition_info_s {
    uint32_t offset;
    uint32_t size;
    int mask;
    const char *name;
} n800_part_info[] = {
    { 0x00000000, 0x00020000, 0x3, "bootloader" },
    { 0x00020000, 0x00060000, 0x0, "config" },
    { 0x00080000, 0x00200000, 0x0, "kernel" },
    { 0x00280000, 0x00200000, 0x3, "initfs" },
    { 0x00480000, 0x0fb80000, 0x3, "rootfs" },

    { 0, 0, 0, 0 }
}, n810_part_info[] = {
    { 0x00000000, 0x00020000, 0x3, "bootloader" },
    { 0x00020000, 0x00060000, 0x0, "config" },
    { 0x00080000, 0x00220000, 0x0, "kernel" },
    { 0x002a0000, 0x00400000, 0x0, "initfs" },
    { 0x006a0000, 0x0f960000, 0x0, "rootfs" },

    { 0, 0, 0, 0 }
};

static bdaddr_t n8x0_bd_addr = {{ N8X0_BD_ADDR }};

static int n8x0_atag_setup(void *p, int model)
{
    uint8_t *b;
    uint16_t *w;
    uint32_t *l;
    struct omap_gpiosw_info_s *gpiosw;
    struct omap_partition_info_s *partition;
    const char *tag;

    w = p;

    stw_raw(w ++, OMAP_TAG_UART);		/* u16 tag */
    stw_raw(w ++, 4);				/* u16 len */
    stw_raw(w ++, (1 << 2) | (1 << 1) | (1 << 0)); /* uint enabled_uarts */
    w ++;

#if 0
    stw_raw(w ++, OMAP_TAG_SERIAL_CONSOLE);	/* u16 tag */
    stw_raw(w ++, 4);				/* u16 len */
    stw_raw(w ++, XLDR_LL_UART + 1);		/* u8 console_uart */
    stw_raw(w ++, 115200);			/* u32 console_speed */
#endif

    stw_raw(w ++, OMAP_TAG_LCD);		/* u16 tag */
    stw_raw(w ++, 36);				/* u16 len */
    strcpy((void *) w, "QEMU LCD panel");	/* char panel_name[16] */
    w += 8;
    strcpy((void *) w, "blizzard");		/* char ctrl_name[16] */
    w += 8;
    stw_raw(w ++, N810_BLIZZARD_RESET_GPIO);	/* TODO: n800 s16 nreset_gpio */
    stw_raw(w ++, 24);				/* u8 data_lines */

    stw_raw(w ++, OMAP_TAG_CBUS);		/* u16 tag */
    stw_raw(w ++, 8);				/* u16 len */
    stw_raw(w ++, N8X0_CBUS_CLK_GPIO);		/* s16 clk_gpio */
    stw_raw(w ++, N8X0_CBUS_DAT_GPIO);		/* s16 dat_gpio */
    stw_raw(w ++, N8X0_CBUS_SEL_GPIO);		/* s16 sel_gpio */
    w ++;

    stw_raw(w ++, OMAP_TAG_EM_ASIC_BB5);	/* u16 tag */
    stw_raw(w ++, 4);				/* u16 len */
    stw_raw(w ++, N8X0_RETU_GPIO);		/* s16 retu_irq_gpio */
    stw_raw(w ++, N8X0_TAHVO_GPIO);		/* s16 tahvo_irq_gpio */

    gpiosw = (model == 810) ? n810_gpiosw_info : n800_gpiosw_info;
    for (; gpiosw->name; gpiosw ++) {
        stw_raw(w ++, OMAP_TAG_GPIO_SWITCH);	/* u16 tag */
        stw_raw(w ++, 20);			/* u16 len */
        strcpy((void *) w, gpiosw->name);	/* char name[12] */
        w += 6;
        stw_raw(w ++, gpiosw->line);		/* u16 gpio */
        stw_raw(w ++, gpiosw->type);
        stw_raw(w ++, 0);
        stw_raw(w ++, 0);
    }

    stw_raw(w ++, OMAP_TAG_NOKIA_BT);		/* u16 tag */
    stw_raw(w ++, 12);				/* u16 len */
    b = (void *) w;
    stb_raw(b ++, 0x01);			/* u8 chip_type	(CSR) */
    stb_raw(b ++, N8X0_BT_WKUP_GPIO);		/* u8 bt_wakeup_gpio */
    stb_raw(b ++, N8X0_BT_HOST_WKUP_GPIO);	/* u8 host_wakeup_gpio */
    stb_raw(b ++, N8X0_BT_RESET_GPIO);		/* u8 reset_gpio */
    stb_raw(b ++, BT_UART + 1);			/* u8 bt_uart */
    memcpy(b, &n8x0_bd_addr, 6);		/* u8 bd_addr[6] */
    b += 6;
    stb_raw(b ++, 0x02);			/* u8 bt_sysclk (38.4) */
    w = (void *) b;

    stw_raw(w ++, OMAP_TAG_WLAN_CX3110X);	/* u16 tag */
    stw_raw(w ++, 8);				/* u16 len */
    stw_raw(w ++, 0x25);			/* u8 chip_type */
    stw_raw(w ++, N8X0_WLAN_PWR_GPIO);		/* s16 power_gpio */
    stw_raw(w ++, N8X0_WLAN_IRQ_GPIO);		/* s16 irq_gpio */
    stw_raw(w ++, -1);				/* s16 spi_cs_gpio */

    stw_raw(w ++, OMAP_TAG_MMC);		/* u16 tag */
    stw_raw(w ++, 16);				/* u16 len */
    if (model == 810) {
        stw_raw(w ++, 0x23f);			/* unsigned flags */
        stw_raw(w ++, -1);			/* s16 power_pin */
        stw_raw(w ++, -1);			/* s16 switch_pin */
        stw_raw(w ++, -1);			/* s16 wp_pin */
        stw_raw(w ++, 0x240);			/* unsigned flags */
        stw_raw(w ++, 0xc000);			/* s16 power_pin */
        stw_raw(w ++, 0x0248);			/* s16 switch_pin */
        stw_raw(w ++, 0xc000);			/* s16 wp_pin */
    } else {
        stw_raw(w ++, 0xf);			/* unsigned flags */
        stw_raw(w ++, -1);			/* s16 power_pin */
        stw_raw(w ++, -1);			/* s16 switch_pin */
        stw_raw(w ++, -1);			/* s16 wp_pin */
        stw_raw(w ++, 0);			/* unsigned flags */
        stw_raw(w ++, 0);			/* s16 power_pin */
        stw_raw(w ++, 0);			/* s16 switch_pin */
        stw_raw(w ++, 0);			/* s16 wp_pin */
    }

    stw_raw(w ++, OMAP_TAG_TEA5761);		/* u16 tag */
    stw_raw(w ++, 4);				/* u16 len */
    stw_raw(w ++, N8X0_TEA5761_CS_GPIO);	/* u16 enable_gpio */
    w ++;

    partition = (model == 810) ? n810_part_info : n800_part_info;
    for (; partition->name; partition ++) {
        stw_raw(w ++, OMAP_TAG_PARTITION);	/* u16 tag */
        stw_raw(w ++, 28);			/* u16 len */
        strcpy((void *) w, partition->name);	/* char name[16] */
        l = (void *) (w + 8);
        stl_raw(l ++, partition->size);		/* unsigned int size */
        stl_raw(l ++, partition->offset);	/* unsigned int offset */
        stl_raw(l ++, partition->mask);		/* unsigned int mask_flags */
        w = (void *) l;
    }

    stw_raw(w ++, OMAP_TAG_BOOT_REASON);	/* u16 tag */
    stw_raw(w ++, 12);				/* u16 len */
#if 0
    strcpy((void *) w, "por");			/* char reason_str[12] */
    strcpy((void *) w, "charger");		/* char reason_str[12] */
    strcpy((void *) w, "32wd_to");		/* char reason_str[12] */
    strcpy((void *) w, "sw_rst");		/* char reason_str[12] */
    strcpy((void *) w, "mbus");			/* char reason_str[12] */
    strcpy((void *) w, "unknown");		/* char reason_str[12] */
    strcpy((void *) w, "swdg_to");		/* char reason_str[12] */
    strcpy((void *) w, "sec_vio");		/* char reason_str[12] */
    strcpy((void *) w, "pwr_key");		/* char reason_str[12] */
    strcpy((void *) w, "rtc_alarm");		/* char reason_str[12] */
#else
    strcpy((void *) w, "pwr_key");		/* char reason_str[12] */
#endif
    w += 6;

    tag = (model == 810) ? "RX-44" : "RX-34";
    stw_raw(w ++, OMAP_TAG_VERSION_STR);	/* u16 tag */
    stw_raw(w ++, 24);				/* u16 len */
    strcpy((void *) w, "product");		/* char component[12] */
    w += 6;
    strcpy((void *) w, tag);			/* char version[12] */
    w += 6;

    stw_raw(w ++, OMAP_TAG_VERSION_STR);	/* u16 tag */
    stw_raw(w ++, 24);				/* u16 len */
    strcpy((void *) w, "hw-build");		/* char component[12] */
    w += 6;
    strcpy((void *) w, "QEMU " QEMU_VERSION);	/* char version[12] */
    w += 6;

    tag = (model == 810) ? "1.1.10-qemu" : "1.1.6-qemu";
    stw_raw(w ++, OMAP_TAG_VERSION_STR);	/* u16 tag */
    stw_raw(w ++, 24);				/* u16 len */
    strcpy((void *) w, "nolo");			/* char component[12] */
    w += 6;
    strcpy((void *) w, tag);			/* char version[12] */
    w += 6;

    return (void *) w - p;
}

static int n800_atag_setup(struct arm_boot_info *info, void *p)
{
    return n8x0_atag_setup(p, 800);
}

static int n810_atag_setup(struct arm_boot_info *info, void *p)
{
    return n8x0_atag_setup(p, 810);
}

static void n8x0_init(ram_addr_t ram_size, const char *boot_device,
                const char *kernel_filename,
                const char *kernel_cmdline, const char *initrd_filename,
                const char *cpu_model, struct arm_boot_info *binfo, int model)
{
    struct n800_s *s = (struct n800_s *) qemu_mallocz(sizeof(*s));
    int sdram_size = binfo->ram_size;
    DisplayState *ds;

    s->cpu = omap2420_mpu_init(sdram_size, cpu_model);

    /* Setup peripherals
     *
     * Believed external peripherals layout in the N810:
     * (spi bus 1)
     *   tsc2005
     *   lcd_mipid
     * (spi bus 2)
     *   Conexant cx3110x (WLAN)
     *   optional: pc2400m (WiMAX)
     * (i2c bus 0)
     *   TLV320AIC33 (audio codec)
     *   TCM825x (camera by Toshiba)
     *   lp5521 (clever LEDs)
     *   tsl2563 (light sensor, hwmon, model 7, rev. 0)
     *   lm8323 (keypad, manf 00, rev 04)
     * (i2c bus 1)
     *   tmp105 (temperature sensor, hwmon)
     *   menelaus (pm)
     * (somewhere on i2c - maybe N800-only)
     *   tea5761 (FM tuner)
     * (serial 0)
     *   GPS
     * (some serial port)
     *   csr41814 (Bluetooth)
     */
    n8x0_gpio_setup(s);
    n8x0_nand_setup(s);
    n8x0_i2c_setup(s);
    if (model == 800)
        n800_tsc_kbd_setup(s);
    else if (model == 810) {
        n810_tsc_setup(s);
        n810_kbd_setup(s);
    }
    n8x0_spi_setup(s);
    n8x0_dss_setup(s);
    n8x0_cbus_setup(s);
    n8x0_uart_setup(s);
    if (usb_enabled)
        n8x0_usb_setup(s);

    /* Setup initial (reset) machine state */

    /* Start at the OneNAND bootloader.  */
    s->cpu->env->regs[15] = 0;

    if (kernel_filename) {
        /* Or at the linux loader.  */
        binfo->kernel_filename = kernel_filename;
        binfo->kernel_cmdline = kernel_cmdline;
        binfo->initrd_filename = initrd_filename;
        arm_load_kernel(s->cpu->env, binfo);

        qemu_register_reset(n8x0_boot_init, s);
        n8x0_boot_init(s);
    }

    if (option_rom[0] && (boot_device[0] == 'n' || !kernel_filename)) {
        int rom_size;
        uint8_t nolo_tags[0x10000];
        /* No, wait, better start at the ROM.  */
        s->cpu->env->regs[15] = OMAP2_Q2_BASE + 0x400000;

        /* This is intended for loading the `secondary.bin' program from
         * Nokia images (the NOLO bootloader).  The entry point seems
         * to be at OMAP2_Q2_BASE + 0x400000.
         *
         * The `2nd.bin' files contain some kind of earlier boot code and
         * for them the entry point needs to be set to OMAP2_SRAM_BASE.
         *
         * The code above is for loading the `zImage' file from Nokia
         * images.  */
        rom_size = load_image_targphys(option_rom[0],
                                       OMAP2_Q2_BASE + 0x400000,
                                       sdram_size - 0x400000);
        printf("%i bytes of image loaded\n", rom_size);

        n800_setup_nolo_tags(nolo_tags);
        cpu_physical_memory_write(OMAP2_SRAM_BASE, nolo_tags, 0x10000);
    }
    /* FIXME: We shouldn't really be doing this here.  The LCD controller
       will set the size once configured, so this just sets an initial
       size until the guest activates the display.  */
    ds = get_displaystate();
    ds->surface = qemu_resize_displaysurface(ds, 800, 480);
    dpy_resize(ds);
}

static struct arm_boot_info n800_binfo = {
    .loader_start = OMAP2_Q2_BASE,
    /* Actually two chips of 0x4000000 bytes each */
    .ram_size = 0x08000000,
    .board_id = 0x4f7,
    .atag_board = n800_atag_setup,
};

static struct arm_boot_info n810_binfo = {
    .loader_start = OMAP2_Q2_BASE,
    /* Actually two chips of 0x4000000 bytes each */
    .ram_size = 0x08000000,
    /* 0x60c and 0x6bf (WiMAX Edition) have been assigned but are not
     * used by some older versions of the bootloader and 5555 is used
     * instead (including versions that shipped with many devices).  */
    .board_id = 0x60c,
    .atag_board = n810_atag_setup,
};

static void n800_init(ram_addr_t ram_size,
                const char *boot_device,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    return n8x0_init(ram_size, boot_device,
                    kernel_filename, kernel_cmdline, initrd_filename,
                    cpu_model, &n800_binfo, 800);
}

static void n810_init(ram_addr_t ram_size,
                const char *boot_device,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    return n8x0_init(ram_size, boot_device,
                    kernel_filename, kernel_cmdline, initrd_filename,
                    cpu_model, &n810_binfo, 810);
}

QEMUMachine n800_machine = {
    .name = "n800",
    .desc = "Nokia N800 tablet aka. RX-34 (OMAP2420)",
    .init = n800_init,
};

QEMUMachine n810_machine = {
    .name = "n810",
    .desc = "Nokia N810 tablet aka. RX-44 (OMAP2420)",
    .init = n810_init,
};
