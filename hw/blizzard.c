/*
 * Epson S1D13744/S1D13745 (Blizzard/Hailstorm/Tornado) LCD/TV controller.
 *
 * Copyright (C) 2008 Nokia Corporation
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
#include "console.h"
#include "devices.h"
#include "vga_int.h"
#include "pixel_ops.h"

typedef void (*blizzard_fn_t)(uint8_t *, const uint8_t *, unsigned int);

struct blizzard_s {
    uint8_t reg;
    uint32_t addr;
    int swallow;

    int pll;
    int pll_range;
    int pll_ctrl;
    uint8_t pll_mode;
    uint8_t clksel;
    int memenable;
    int memrefresh;
    uint8_t timing[3];
    int priority;

    uint8_t lcd_config;
    int x;
    int y;
    int skipx;
    int skipy;
    uint8_t hndp;
    uint8_t vndp;
    uint8_t hsync;
    uint8_t vsync;
    uint8_t pclk;
    uint8_t u;
    uint8_t v;
    uint8_t yrc[2];
    int ix[2];
    int iy[2];
    int ox[2];
    int oy[2];

    int enable;
    int blank;
    int bpp;
    int invalidate;
    int mx[2];
    int my[2];
    uint8_t mode;
    uint8_t effect;
    uint8_t iformat;
    uint8_t source;
    DisplayState *state;
    blizzard_fn_t *line_fn_tab[2];
    void *fb;

    uint8_t hssi_config[3];
    uint8_t tv_config;
    uint8_t tv_timing[4];
    uint8_t vbi;
    uint8_t tv_x;
    uint8_t tv_y;
    uint8_t tv_test;
    uint8_t tv_filter_config;
    uint8_t tv_filter_idx;
    uint8_t tv_filter_coeff[0x20];
    uint8_t border_r;
    uint8_t border_g;
    uint8_t border_b;
    uint8_t gamma_config;
    uint8_t gamma_idx;
    uint8_t gamma_lut[0x100];
    uint8_t matrix_ena;
    uint8_t matrix_coeff[0x12];
    uint8_t matrix_r;
    uint8_t matrix_g;
    uint8_t matrix_b;
    uint8_t pm;
    uint8_t status;
    uint8_t rgbgpio_dir;
    uint8_t rgbgpio;
    uint8_t gpio_dir;
    uint8_t gpio;
    uint8_t gpio_edge[2];
    uint8_t gpio_irq;
    uint8_t gpio_pdown;

    struct {
        int x;
        int y;
        int dx;
        int dy;
        int len;
        int buflen;
        void *buf;
        void *data;
        uint16_t *ptr;
        int angle;
        int pitch;
        blizzard_fn_t line_fn;
    } data;
};

/* Bytes(!) per pixel */
static const int blizzard_iformat_bpp[0x10] = {
    0,
    2,	/* RGB 5:6:5*/
    3,	/* RGB 6:6:6 mode 1 */
    3,	/* RGB 8:8:8 mode 1 */
    0, 0,
    4,	/* RGB 6:6:6 mode 2 */
    4,	/* RGB 8:8:8 mode 2 */
    0,	/* YUV 4:2:2 */
    0,	/* YUV 4:2:0 */
    0, 0, 0, 0, 0, 0,
};

static inline void blizzard_rgb2yuv(int r, int g, int b,
                int *y, int *u, int *v)
{
    *y = 0x10 + ((0x838 * r + 0x1022 * g + 0x322 * b) >> 13);
    *u = 0x80 + ((0xe0e * b - 0x04c1 * r - 0x94e * g) >> 13);
    *v = 0x80 + ((0xe0e * r - 0x0bc7 * g - 0x247 * b) >> 13);
}

static void blizzard_window(struct blizzard_s *s)
{
    uint8_t *src, *dst;
    int bypp[2];
    int bypl[3];
    int y;
    blizzard_fn_t fn = s->data.line_fn;

    if (!fn)
        return;
    if (s->mx[0] > s->data.x)
        s->mx[0] = s->data.x;
    if (s->my[0] > s->data.y)
        s->my[0] = s->data.y;
    if (s->mx[1] < s->data.x + s->data.dx)
        s->mx[1] = s->data.x + s->data.dx;
    if (s->my[1] < s->data.y + s->data.dy)
        s->my[1] = s->data.y + s->data.dy;

    bypp[0] = s->bpp;
    bypp[1] = (ds_get_bits_per_pixel(s->state) + 7) >> 3;
    bypl[0] = bypp[0] * s->data.pitch;
    bypl[1] = bypp[1] * s->x;
    bypl[2] = bypp[0] * s->data.dx;

    src = s->data.data;
    dst = s->fb + bypl[1] * s->data.y + bypp[1] * s->data.x;
    for (y = s->data.dy; y > 0; y --, src += bypl[0], dst += bypl[1])
        fn(dst, src, bypl[2]);
}

static int blizzard_transfer_setup(struct blizzard_s *s)
{
    if (s->source > 3 || !s->bpp ||
                    s->ix[1] < s->ix[0] || s->iy[1] < s->iy[0])
        return 0;

    s->data.angle = s->effect & 3;
    s->data.line_fn = s->line_fn_tab[!!s->data.angle][s->iformat];
    s->data.x = s->ix[0];
    s->data.y = s->iy[0];
    s->data.dx = s->ix[1] - s->ix[0] + 1;
    s->data.dy = s->iy[1] - s->iy[0] + 1;
    s->data.len = s->bpp * s->data.dx * s->data.dy;
    s->data.pitch = s->data.dx;
    if (s->data.len > s->data.buflen) {
        s->data.buf = qemu_realloc(s->data.buf, s->data.len);
        s->data.buflen = s->data.len;
    }
    s->data.ptr = s->data.buf;
    s->data.data = s->data.buf;
    s->data.len /= 2;
    return 1;
}

static void blizzard_reset(struct blizzard_s *s)
{
    s->reg = 0;
    s->swallow = 0;

    s->pll = 9;
    s->pll_range = 1;
    s->pll_ctrl = 0x14;
    s->pll_mode = 0x32;
    s->clksel = 0x00;
    s->memenable = 0;
    s->memrefresh = 0x25c;
    s->timing[0] = 0x3f;
    s->timing[1] = 0x13;
    s->timing[2] = 0x21;
    s->priority = 0;

    s->lcd_config = 0x74;
    s->x = 8;
    s->y = 1;
    s->skipx = 0;
    s->skipy = 0;
    s->hndp = 3;
    s->vndp = 2;
    s->hsync = 1;
    s->vsync = 1;
    s->pclk = 0x80;

    s->ix[0] = 0;
    s->ix[1] = 0;
    s->iy[0] = 0;
    s->iy[1] = 0;
    s->ox[0] = 0;
    s->ox[1] = 0;
    s->oy[0] = 0;
    s->oy[1] = 0;

    s->yrc[0] = 0x00;
    s->yrc[1] = 0x30;
    s->u = 0;
    s->v = 0;

    s->iformat = 3;
    s->source = 0;
    s->bpp = blizzard_iformat_bpp[s->iformat];

    s->hssi_config[0] = 0x00;
    s->hssi_config[1] = 0x00;
    s->hssi_config[2] = 0x01;
    s->tv_config = 0x00;
    s->tv_timing[0] = 0x00;
    s->tv_timing[1] = 0x00;
    s->tv_timing[2] = 0x00;
    s->tv_timing[3] = 0x00;
    s->vbi = 0x10;
    s->tv_x = 0x14;
    s->tv_y = 0x03;
    s->tv_test = 0x00;
    s->tv_filter_config = 0x80;
    s->tv_filter_idx = 0x00;
    s->border_r = 0x10;
    s->border_g = 0x80;
    s->border_b = 0x80;
    s->gamma_config = 0x00;
    s->gamma_idx = 0x00;
    s->matrix_ena = 0x00;
    memset(&s->matrix_coeff, 0, sizeof(s->matrix_coeff));
    s->matrix_r = 0x00;
    s->matrix_g = 0x00;
    s->matrix_b = 0x00;
    s->pm = 0x02;
    s->status = 0x00;
    s->rgbgpio_dir = 0x00;
    s->gpio_dir = 0x00;
    s->gpio_edge[0] = 0x00;
    s->gpio_edge[1] = 0x00;
    s->gpio_irq = 0x00;
    s->gpio_pdown = 0xff;
}

static inline void blizzard_invalidate_display(void *opaque) {
    struct blizzard_s *s = (struct blizzard_s *) opaque;

    s->invalidate = 1;
}

static uint16_t blizzard_reg_read(void *opaque, uint8_t reg)
{
    struct blizzard_s *s = (struct blizzard_s *) opaque;

    switch (reg) {
    case 0x00:	/* Revision Code */
        return 0xa5;

    case 0x02:	/* Configuration Readback */
        return 0x83;	/* Macrovision OK, CNF[2:0] = 3 */

    case 0x04:	/* PLL M-Divider */
        return (s->pll - 1) | (1 << 7);
    case 0x06:	/* PLL Lock Range Control */
        return s->pll_range;
    case 0x08:	/* PLL Lock Synthesis Control 0 */
        return s->pll_ctrl & 0xff;
    case 0x0a:	/* PLL Lock Synthesis Control 1 */
        return s->pll_ctrl >> 8;
    case 0x0c:	/* PLL Mode Control 0 */
        return s->pll_mode;

    case 0x0e:	/* Clock-Source Select */
        return s->clksel;

    case 0x10:	/* Memory Controller Activate */
    case 0x14:	/* Memory Controller Bank 0 Status Flag */
        return s->memenable;

    case 0x18:	/* Auto-Refresh Interval Setting 0 */
        return s->memrefresh & 0xff;
    case 0x1a:	/* Auto-Refresh Interval Setting 1 */
        return s->memrefresh >> 8;

    case 0x1c:	/* Power-On Sequence Timing Control */
        return s->timing[0];
    case 0x1e:	/* Timing Control 0 */
        return s->timing[1];
    case 0x20:	/* Timing Control 1 */
        return s->timing[2];

    case 0x24:	/* Arbitration Priority Control */
        return s->priority;

    case 0x28:	/* LCD Panel Configuration */
        return s->lcd_config;

    case 0x2a:	/* LCD Horizontal Display Width */
        return s->x >> 3;
    case 0x2c:	/* LCD Horizontal Non-display Period */
        return s->hndp;
    case 0x2e:	/* LCD Vertical Display Height 0 */
        return s->y & 0xff;
    case 0x30:	/* LCD Vertical Display Height 1 */
        return s->y >> 8;
    case 0x32:	/* LCD Vertical Non-display Period */
        return s->vndp;
    case 0x34:	/* LCD HS Pulse-width */
        return s->hsync;
    case 0x36:	/* LCd HS Pulse Start Position */
        return s->skipx >> 3;
    case 0x38:	/* LCD VS Pulse-width */
        return s->vsync;
    case 0x3a:	/* LCD VS Pulse Start Position */
        return s->skipy;

    case 0x3c:	/* PCLK Polarity */
        return s->pclk;

    case 0x3e:	/* High-speed Serial Interface Tx Configuration Port 0 */
        return s->hssi_config[0];
    case 0x40:	/* High-speed Serial Interface Tx Configuration Port 1 */
        return s->hssi_config[1];
    case 0x42:	/* High-speed Serial Interface Tx Mode */
        return s->hssi_config[2];
    case 0x44:	/* TV Display Configuration */
        return s->tv_config;
    case 0x46 ... 0x4c:	/* TV Vertical Blanking Interval Data bits */
        return s->tv_timing[(reg - 0x46) >> 1];
    case 0x4e:	/* VBI: Closed Caption / XDS Control / Status */
        return s->vbi;
    case 0x50:	/* TV Horizontal Start Position */
        return s->tv_x;
    case 0x52:	/* TV Vertical Start Position */
        return s->tv_y;
    case 0x54:	/* TV Test Pattern Setting */
        return s->tv_test;
    case 0x56:	/* TV Filter Setting */
        return s->tv_filter_config;
    case 0x58:	/* TV Filter Coefficient Index */
        return s->tv_filter_idx;
    case 0x5a:	/* TV Filter Coefficient Data */
        if (s->tv_filter_idx < 0x20)
            return s->tv_filter_coeff[s->tv_filter_idx ++];
        return 0;

    case 0x60:	/* Input YUV/RGB Translate Mode 0 */
        return s->yrc[0];
    case 0x62:	/* Input YUV/RGB Translate Mode 1 */
        return s->yrc[1];
    case 0x64:	/* U Data Fix */
        return s->u;
    case 0x66:	/* V Data Fix */
        return s->v;

    case 0x68:	/* Display Mode */
        return s->mode;

    case 0x6a:	/* Special Effects */
        return s->effect;

    case 0x6c:	/* Input Window X Start Position 0 */
        return s->ix[0] & 0xff;
    case 0x6e:	/* Input Window X Start Position 1 */
        return s->ix[0] >> 3;
    case 0x70:	/* Input Window Y Start Position 0 */
        return s->ix[0] & 0xff;
    case 0x72:	/* Input Window Y Start Position 1 */
        return s->ix[0] >> 3;
    case 0x74:	/* Input Window X End Position 0 */
        return s->ix[1] & 0xff;
    case 0x76:	/* Input Window X End Position 1 */
        return s->ix[1] >> 3;
    case 0x78:	/* Input Window Y End Position 0 */
        return s->ix[1] & 0xff;
    case 0x7a:	/* Input Window Y End Position 1 */
        return s->ix[1] >> 3;
    case 0x7c:	/* Output Window X Start Position 0 */
        return s->ox[0] & 0xff;
    case 0x7e:	/* Output Window X Start Position 1 */
        return s->ox[0] >> 3;
    case 0x80:	/* Output Window Y Start Position 0 */
        return s->oy[0] & 0xff;
    case 0x82:	/* Output Window Y Start Position 1 */
        return s->oy[0] >> 3;
    case 0x84:	/* Output Window X End Position 0 */
        return s->ox[1] & 0xff;
    case 0x86:	/* Output Window X End Position 1 */
        return s->ox[1] >> 3;
    case 0x88:	/* Output Window Y End Position 0 */
        return s->oy[1] & 0xff;
    case 0x8a:	/* Output Window Y End Position 1 */
        return s->oy[1] >> 3;

    case 0x8c:	/* Input Data Format */
        return s->iformat;
    case 0x8e:	/* Data Source Select */
        return s->source;
    case 0x90:	/* Display Memory Data Port */
        return 0;

    case 0xa8:	/* Border Color 0 */
        return s->border_r;
    case 0xaa:	/* Border Color 1 */
        return s->border_g;
    case 0xac:	/* Border Color 2 */
        return s->border_b;

    case 0xb4:	/* Gamma Correction Enable */
        return s->gamma_config;
    case 0xb6:	/* Gamma Correction Table Index */
        return s->gamma_idx;
    case 0xb8:	/* Gamma Correction Table Data */
        return s->gamma_lut[s->gamma_idx ++];

    case 0xba:	/* 3x3 Matrix Enable */
        return s->matrix_ena;
    case 0xbc ... 0xde:	/* Coefficient Registers */
        return s->matrix_coeff[(reg - 0xbc) >> 1];
    case 0xe0:	/* 3x3 Matrix Red Offset */
        return s->matrix_r;
    case 0xe2:	/* 3x3 Matrix Green Offset */
        return s->matrix_g;
    case 0xe4:	/* 3x3 Matrix Blue Offset */
        return s->matrix_b;

    case 0xe6:	/* Power-save */
        return s->pm;
    case 0xe8:	/* Non-display Period Control / Status */
        return s->status | (1 << 5);
    case 0xea:	/* RGB Interface Control */
        return s->rgbgpio_dir;
    case 0xec:	/* RGB Interface Status */
        return s->rgbgpio;
    case 0xee:	/* General-purpose IO Pins Configuration */
        return s->gpio_dir;
    case 0xf0:	/* General-purpose IO Pins Status / Control */
        return s->gpio;
    case 0xf2:	/* GPIO Positive Edge Interrupt Trigger */
        return s->gpio_edge[0];
    case 0xf4:	/* GPIO Negative Edge Interrupt Trigger */
        return s->gpio_edge[1];
    case 0xf6:	/* GPIO Interrupt Status */
        return s->gpio_irq;
    case 0xf8:	/* GPIO Pull-down Control */
        return s->gpio_pdown;

    default:
        fprintf(stderr, "%s: unknown register %02x\n", __FUNCTION__, reg);
        return 0;
    }
}

static void blizzard_reg_write(void *opaque, uint8_t reg, uint16_t value)
{
    struct blizzard_s *s = (struct blizzard_s *) opaque;

    switch (reg) {
    case 0x04:	/* PLL M-Divider */
        s->pll = (value & 0x3f) + 1;
        break;
    case 0x06:	/* PLL Lock Range Control */
        s->pll_range = value & 3;
        break;
    case 0x08:	/* PLL Lock Synthesis Control 0 */
        s->pll_ctrl &= 0xf00;
        s->pll_ctrl |= (value << 0) & 0x0ff;
        break;
    case 0x0a:	/* PLL Lock Synthesis Control 1 */
        s->pll_ctrl &= 0x0ff;
        s->pll_ctrl |= (value << 8) & 0xf00;
        break;
    case 0x0c:	/* PLL Mode Control 0 */
        s->pll_mode = value & 0x77;
        if ((value & 3) == 0 || (value & 3) == 3)
            fprintf(stderr, "%s: wrong PLL Control bits (%i)\n",
                    __FUNCTION__, value & 3);
        break;

    case 0x0e:	/* Clock-Source Select */
        s->clksel = value & 0xff;
        break;

    case 0x10:	/* Memory Controller Activate */
        s->memenable = value & 1;
        break;
    case 0x14:	/* Memory Controller Bank 0 Status Flag */
        break;

    case 0x18:	/* Auto-Refresh Interval Setting 0 */
        s->memrefresh &= 0xf00;
        s->memrefresh |= (value << 0) & 0x0ff;
        break;
    case 0x1a:	/* Auto-Refresh Interval Setting 1 */
        s->memrefresh &= 0x0ff;
        s->memrefresh |= (value << 8) & 0xf00;
        break;

    case 0x1c:	/* Power-On Sequence Timing Control */
        s->timing[0] = value & 0x7f;
        break;
    case 0x1e:	/* Timing Control 0 */
        s->timing[1] = value & 0x17;
        break;
    case 0x20:	/* Timing Control 1 */
        s->timing[2] = value & 0x35;
        break;

    case 0x24:	/* Arbitration Priority Control */
        s->priority = value & 1;
        break;

    case 0x28:	/* LCD Panel Configuration */
        s->lcd_config = value & 0xff;
        if (value & (1 << 7))
            fprintf(stderr, "%s: data swap not supported!\n", __FUNCTION__);
        break;

    case 0x2a:	/* LCD Horizontal Display Width */
        s->x = value << 3;
        break;
    case 0x2c:	/* LCD Horizontal Non-display Period */
        s->hndp = value & 0xff;
        break;
    case 0x2e:	/* LCD Vertical Display Height 0 */
        s->y &= 0x300;
        s->y |= (value << 0) & 0x0ff;
        break;
    case 0x30:	/* LCD Vertical Display Height 1 */
        s->y &= 0x0ff;
        s->y |= (value << 8) & 0x300;
        break;
    case 0x32:	/* LCD Vertical Non-display Period */
        s->vndp = value & 0xff;
        break;
    case 0x34:	/* LCD HS Pulse-width */
        s->hsync = value & 0xff;
        break;
    case 0x36:	/* LCD HS Pulse Start Position */
        s->skipx = value & 0xff;
        break;
    case 0x38:	/* LCD VS Pulse-width */
        s->vsync = value & 0xbf;
        break;
    case 0x3a:	/* LCD VS Pulse Start Position */
        s->skipy = value & 0xff;
        break;

    case 0x3c:	/* PCLK Polarity */
        s->pclk = value & 0x82;
        /* Affects calculation of s->hndp, s->hsync and s->skipx.  */
        break;

    case 0x3e:	/* High-speed Serial Interface Tx Configuration Port 0 */
        s->hssi_config[0] = value;
        break;
    case 0x40:	/* High-speed Serial Interface Tx Configuration Port 1 */
        s->hssi_config[1] = value;
        if (((value >> 4) & 3) == 3)
            fprintf(stderr, "%s: Illegal active-data-links value\n",
                            __FUNCTION__);
        break;
    case 0x42:	/* High-speed Serial Interface Tx Mode */
        s->hssi_config[2] = value & 0xbd;
        break;

    case 0x44:	/* TV Display Configuration */
        s->tv_config = value & 0xfe;
        break;
    case 0x46 ... 0x4c:	/* TV Vertical Blanking Interval Data bits 0 */
        s->tv_timing[(reg - 0x46) >> 1] = value;
        break;
    case 0x4e:	/* VBI: Closed Caption / XDS Control / Status */
        s->vbi = value;
        break;
    case 0x50:	/* TV Horizontal Start Position */
        s->tv_x = value;
        break;
    case 0x52:	/* TV Vertical Start Position */
        s->tv_y = value & 0x7f;
        break;
    case 0x54:	/* TV Test Pattern Setting */
        s->tv_test = value;
        break;
    case 0x56:	/* TV Filter Setting */
        s->tv_filter_config = value & 0xbf;
        break;
    case 0x58:	/* TV Filter Coefficient Index */
        s->tv_filter_idx = value & 0x1f;
        break;
    case 0x5a:	/* TV Filter Coefficient Data */
        if (s->tv_filter_idx < 0x20)
            s->tv_filter_coeff[s->tv_filter_idx ++] = value;
        break;

    case 0x60:	/* Input YUV/RGB Translate Mode 0 */
        s->yrc[0] = value & 0xb0;
        break;
    case 0x62:	/* Input YUV/RGB Translate Mode 1 */
        s->yrc[1] = value & 0x30;
        break;
    case 0x64:	/* U Data Fix */
        s->u = value & 0xff;
        break;
    case 0x66:	/* V Data Fix */
        s->v = value & 0xff;
        break;

    case 0x68:	/* Display Mode */
        if ((s->mode ^ value) & 3)
            s->invalidate = 1;
        s->mode = value & 0xb7;
        s->enable = value & 1;
        s->blank = (value >> 1) & 1;
        if (value & (1 << 4))
            fprintf(stderr, "%s: Macrovision enable attempt!\n", __FUNCTION__);
        break;

    case 0x6a:	/* Special Effects */
        s->effect = value & 0xfb;
        break;

    case 0x6c:	/* Input Window X Start Position 0 */
        s->ix[0] &= 0x300;
        s->ix[0] |= (value << 0) & 0x0ff;
        break;
    case 0x6e:	/* Input Window X Start Position 1 */
        s->ix[0] &= 0x0ff;
        s->ix[0] |= (value << 8) & 0x300;
        break;
    case 0x70:	/* Input Window Y Start Position 0 */
        s->iy[0] &= 0x300;
        s->iy[0] |= (value << 0) & 0x0ff;
        break;
    case 0x72:	/* Input Window Y Start Position 1 */
        s->iy[0] &= 0x0ff;
        s->iy[0] |= (value << 8) & 0x300;
        break;
    case 0x74:	/* Input Window X End Position 0 */
        s->ix[1] &= 0x300;
        s->ix[1] |= (value << 0) & 0x0ff;
        break;
    case 0x76:	/* Input Window X End Position 1 */
        s->ix[1] &= 0x0ff;
        s->ix[1] |= (value << 8) & 0x300;
        break;
    case 0x78:	/* Input Window Y End Position 0 */
        s->iy[1] &= 0x300;
        s->iy[1] |= (value << 0) & 0x0ff;
        break;
    case 0x7a:	/* Input Window Y End Position 1 */
        s->iy[1] &= 0x0ff;
        s->iy[1] |= (value << 8) & 0x300;
        break;
    case 0x7c:	/* Output Window X Start Position 0 */
        s->ox[0] &= 0x300;
        s->ox[0] |= (value << 0) & 0x0ff;
        break;
    case 0x7e:	/* Output Window X Start Position 1 */
        s->ox[0] &= 0x0ff;
        s->ox[0] |= (value << 8) & 0x300;
        break;
    case 0x80:	/* Output Window Y Start Position 0 */
        s->oy[0] &= 0x300;
        s->oy[0] |= (value << 0) & 0x0ff;
        break;
    case 0x82:	/* Output Window Y Start Position 1 */
        s->oy[0] &= 0x0ff;
        s->oy[0] |= (value << 8) & 0x300;
        break;
    case 0x84:	/* Output Window X End Position 0 */
        s->ox[1] &= 0x300;
        s->ox[1] |= (value << 0) & 0x0ff;
        break;
    case 0x86:	/* Output Window X End Position 1 */
        s->ox[1] &= 0x0ff;
        s->ox[1] |= (value << 8) & 0x300;
        break;
    case 0x88:	/* Output Window Y End Position 0 */
        s->oy[1] &= 0x300;
        s->oy[1] |= (value << 0) & 0x0ff;
        break;
    case 0x8a:	/* Output Window Y End Position 1 */
        s->oy[1] &= 0x0ff;
        s->oy[1] |= (value << 8) & 0x300;
        break;

    case 0x8c:	/* Input Data Format */
        s->iformat = value & 0xf;
        s->bpp = blizzard_iformat_bpp[s->iformat];
        if (!s->bpp)
            fprintf(stderr, "%s: Illegal or unsupported input format %x\n",
                            __FUNCTION__, s->iformat);
        break;
    case 0x8e:	/* Data Source Select */
        s->source = value & 7;
        /* Currently all windows will be "destructive overlays".  */
        if ((!(s->effect & (1 << 3)) && (s->ix[0] != s->ox[0] ||
                                        s->iy[0] != s->oy[0] ||
                                        s->ix[1] != s->ox[1] ||
                                        s->iy[1] != s->oy[1])) ||
                        !((s->ix[1] - s->ix[0]) & (s->iy[1] - s->iy[0]) &
                          (s->ox[1] - s->ox[0]) & (s->oy[1] - s->oy[0]) & 1))
            fprintf(stderr, "%s: Illegal input/output window positions\n",
                            __FUNCTION__);

        blizzard_transfer_setup(s);
        break;

    case 0x90:	/* Display Memory Data Port */
        if (!s->data.len && !blizzard_transfer_setup(s))
            break;

        *s->data.ptr ++ = value;
        if (-- s->data.len == 0)
            blizzard_window(s);
        break;

    case 0xa8:	/* Border Color 0 */
        s->border_r = value;
        break;
    case 0xaa:	/* Border Color 1 */
        s->border_g = value;
        break;
    case 0xac:	/* Border Color 2 */
        s->border_b = value;
        break;

    case 0xb4:	/* Gamma Correction Enable */
        s->gamma_config = value & 0x87;
        break;
    case 0xb6:	/* Gamma Correction Table Index */
        s->gamma_idx = value;
        break;
    case 0xb8:	/* Gamma Correction Table Data */
        s->gamma_lut[s->gamma_idx ++] = value;
        break;

    case 0xba:	/* 3x3 Matrix Enable */
        s->matrix_ena = value & 1;
        break;
    case 0xbc ... 0xde:	/* Coefficient Registers */
        s->matrix_coeff[(reg - 0xbc) >> 1] = value & ((reg & 2) ? 0x80 : 0xff);
        break;
    case 0xe0:	/* 3x3 Matrix Red Offset */
        s->matrix_r = value;
        break;
    case 0xe2:	/* 3x3 Matrix Green Offset */
        s->matrix_g = value;
        break;
    case 0xe4:	/* 3x3 Matrix Blue Offset */
        s->matrix_b = value;
        break;

    case 0xe6:	/* Power-save */
        s->pm = value & 0x83;
        if (value & s->mode & 1)
            fprintf(stderr, "%s: The display must be disabled before entering "
                            "Standby Mode\n", __FUNCTION__);
        break;
    case 0xe8:	/* Non-display Period Control / Status */
        s->status = value & 0x1b;
        break;
    case 0xea:	/* RGB Interface Control */
        s->rgbgpio_dir = value & 0x8f;
        break;
    case 0xec:	/* RGB Interface Status */
        s->rgbgpio = value & 0xcf;
        break;
    case 0xee:	/* General-purpose IO Pins Configuration */
        s->gpio_dir = value;
        break;
    case 0xf0:	/* General-purpose IO Pins Status / Control */
        s->gpio = value;
        break;
    case 0xf2:	/* GPIO Positive Edge Interrupt Trigger */
        s->gpio_edge[0] = value;
        break;
    case 0xf4:	/* GPIO Negative Edge Interrupt Trigger */
        s->gpio_edge[1] = value;
        break;
    case 0xf6:	/* GPIO Interrupt Status */
        s->gpio_irq &= value;
        break;
    case 0xf8:	/* GPIO Pull-down Control */
        s->gpio_pdown = value;
        break;

    default:
        fprintf(stderr, "%s: unknown register %02x\n", __FUNCTION__, reg);
        break;
    }
}

uint16_t s1d13745_read(void *opaque, int dc)
{
    struct blizzard_s *s = (struct blizzard_s *) opaque;
    uint16_t value = blizzard_reg_read(s, s->reg);

    if (s->swallow -- > 0)
        return 0;
    if (dc)
        s->reg ++;

    return value;
}

void s1d13745_write(void *opaque, int dc, uint16_t value)
{
    struct blizzard_s *s = (struct blizzard_s *) opaque;

    if (s->swallow -- > 0)
        return;
    if (dc) {
        blizzard_reg_write(s, s->reg, value);

        if (s->reg != 0x90 && s->reg != 0x5a && s->reg != 0xb8)
            s->reg += 2;
    } else
        s->reg = value & 0xff;
}

void s1d13745_write_block(void *opaque, int dc,
                void *buf, size_t len, int pitch)
{
    struct blizzard_s *s = (struct blizzard_s *) opaque;

    while (len > 0) {
        if (s->reg == 0x90 && dc &&
                        (s->data.len || blizzard_transfer_setup(s)) &&
                        len >= (s->data.len << 1)) {
            len -= s->data.len << 1;
            s->data.len = 0;
            s->data.data = buf;
            if (pitch)
                s->data.pitch = pitch;
            blizzard_window(s);
            s->data.data = s->data.buf;
            continue;
        }

        s1d13745_write(opaque, dc, *(uint16_t *) buf);
        len -= 2;
        buf += 2;
    }

    return;
}

static void blizzard_update_display(void *opaque)
{
    struct blizzard_s *s = (struct blizzard_s *) opaque;
    int y, bypp, bypl, bwidth;
    uint8_t *src, *dst;

    if (!s->enable)
        return;

    if (s->x != ds_get_width(s->state) || s->y != ds_get_height(s->state)) {
        s->invalidate = 1;
        qemu_console_resize(s->state, s->x, s->y);
    }

    if (s->invalidate) {
        s->invalidate = 0;

        if (s->blank) {
            bypp = (ds_get_bits_per_pixel(s->state) + 7) >> 3;
            memset(ds_get_data(s->state), 0, bypp * s->x * s->y);
            return;
        }

        s->mx[0] = 0;
        s->mx[1] = s->x;
        s->my[0] = 0;
        s->my[1] = s->y;
    }

    if (s->mx[1] <= s->mx[0])
        return;

    bypp = (ds_get_bits_per_pixel(s->state) + 7) >> 3;
    bypl = bypp * s->x;
    bwidth = bypp * (s->mx[1] - s->mx[0]);
    y = s->my[0];
    src = s->fb + bypl * y + bypp * s->mx[0];
    dst = ds_get_data(s->state) + bypl * y + bypp * s->mx[0];
    for (; y < s->my[1]; y ++, src += bypl, dst += bypl)
        memcpy(dst, src, bwidth);

    dpy_update(s->state, s->mx[0], s->my[0],
                    s->mx[1] - s->mx[0], y - s->my[0]);

    s->mx[0] = s->x;
    s->mx[1] = 0;
    s->my[0] = s->y;
    s->my[1] = 0;
}

static void blizzard_screen_dump(void *opaque, const char *filename) {
    struct blizzard_s *s = (struct blizzard_s *) opaque;

    blizzard_update_display(opaque);
    if (s && ds_get_data(s->state))
        ppm_save(filename, ds_get_data(s->state), s->x, s->y, ds_get_linesize(s->state));
}

#define DEPTH 8
#include "blizzard_template.h"
#define DEPTH 15
#include "blizzard_template.h"
#define DEPTH 16
#include "blizzard_template.h"
#define DEPTH 24
#include "blizzard_template.h"
#define DEPTH 32
#include "blizzard_template.h"

void *s1d13745_init(qemu_irq gpio_int)
{
    struct blizzard_s *s = (struct blizzard_s *) qemu_mallocz(sizeof(*s));

    s->fb = qemu_malloc(0x180000);

    switch (ds_get_bits_per_pixel(s->state)) {
    case 0:
        s->line_fn_tab[0] = s->line_fn_tab[1] =
                qemu_mallocz(sizeof(blizzard_fn_t) * 0x10);
        break;
    case 8:
        s->line_fn_tab[0] = blizzard_draw_fn_8;
        s->line_fn_tab[1] = blizzard_draw_fn_r_8;
        break;
    case 15:
        s->line_fn_tab[0] = blizzard_draw_fn_15;
        s->line_fn_tab[1] = blizzard_draw_fn_r_15;
        break;
    case 16:
        s->line_fn_tab[0] = blizzard_draw_fn_16;
        s->line_fn_tab[1] = blizzard_draw_fn_r_16;
        break;
    case 24:
        s->line_fn_tab[0] = blizzard_draw_fn_24;
        s->line_fn_tab[1] = blizzard_draw_fn_r_24;
        break;
    case 32:
        s->line_fn_tab[0] = blizzard_draw_fn_32;
        s->line_fn_tab[1] = blizzard_draw_fn_r_32;
        break;
    default:
        fprintf(stderr, "%s: Bad color depth\n", __FUNCTION__);
        exit(1);
    }

    blizzard_reset(s);

    s->state = graphic_console_init(blizzard_update_display,
                                 blizzard_invalidate_display,
                                 blizzard_screen_dump, NULL, s);

    return s;
}
