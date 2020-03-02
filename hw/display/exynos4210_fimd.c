/*
 * Samsung exynos4210 Display Controller (FIMD)
 *
 * Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd.
 * All rights reserved.
 * Based on LCD controller for Samsung S5PC1xx-based board emulation
 * by Kirill Batuzov <batuzovk@ispras.ru>
 *
 * Contributed by Mitsyanko Igor <i.mitsyanko@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"
#include "qemu/bswap.h"
#include "qemu/module.h"

/* Debug messages configuration */
#define EXYNOS4210_FIMD_DEBUG              0
#define EXYNOS4210_FIMD_MODE_TRACE         0

#if EXYNOS4210_FIMD_DEBUG == 0
    #define DPRINT_L1(fmt, args...)       do { } while (0)
    #define DPRINT_L2(fmt, args...)       do { } while (0)
    #define DPRINT_ERROR(fmt, args...)    do { } while (0)
#elif EXYNOS4210_FIMD_DEBUG == 1
    #define DPRINT_L1(fmt, args...) \
        do {fprintf(stderr, "QEMU FIMD: "fmt, ## args); } while (0)
    #define DPRINT_L2(fmt, args...)       do { } while (0)
    #define DPRINT_ERROR(fmt, args...)  \
        do {fprintf(stderr, "QEMU FIMD ERROR: "fmt, ## args); } while (0)
#else
    #define DPRINT_L1(fmt, args...) \
        do {fprintf(stderr, "QEMU FIMD: "fmt, ## args); } while (0)
    #define DPRINT_L2(fmt, args...) \
        do {fprintf(stderr, "QEMU FIMD: "fmt, ## args); } while (0)
    #define DPRINT_ERROR(fmt, args...)  \
        do {fprintf(stderr, "QEMU FIMD ERROR: "fmt, ## args); } while (0)
#endif

#if EXYNOS4210_FIMD_MODE_TRACE == 0
    #define DPRINT_TRACE(fmt, args...)        do { } while (0)
#else
    #define DPRINT_TRACE(fmt, args...)        \
        do {fprintf(stderr, "QEMU FIMD: "fmt, ## args); } while (0)
#endif

#define NUM_OF_WINDOWS              5
#define FIMD_REGS_SIZE              0x4114

/* Video main control registers */
#define FIMD_VIDCON0                0x0000
#define FIMD_VIDCON1                0x0004
#define FIMD_VIDCON2                0x0008
#define FIMD_VIDCON3                0x000C
#define FIMD_VIDCON0_ENVID_F        (1 << 0)
#define FIMD_VIDCON0_ENVID          (1 << 1)
#define FIMD_VIDCON0_ENVID_MASK     ((1 << 0) | (1 << 1))
#define FIMD_VIDCON1_ROMASK         0x07FFE000

/* Video time control registers */
#define FIMD_VIDTCON_START          0x10
#define FIMD_VIDTCON_END            0x1C
#define FIMD_VIDTCON2_SIZE_MASK     0x07FF
#define FIMD_VIDTCON2_HOR_SHIFT     0
#define FIMD_VIDTCON2_VER_SHIFT     11

/* Window control registers */
#define FIMD_WINCON_START           0x0020
#define FIMD_WINCON_END             0x0030
#define FIMD_WINCON_ROMASK          0x82200000
#define FIMD_WINCON_ENWIN           (1 << 0)
#define FIMD_WINCON_BLD_PIX         (1 << 6)
#define FIMD_WINCON_ALPHA_MUL       (1 << 7)
#define FIMD_WINCON_ALPHA_SEL       (1 << 1)
#define FIMD_WINCON_SWAP            0x078000
#define FIMD_WINCON_SWAP_SHIFT      15
#define FIMD_WINCON_SWAP_WORD       0x1
#define FIMD_WINCON_SWAP_HWORD      0x2
#define FIMD_WINCON_SWAP_BYTE       0x4
#define FIMD_WINCON_SWAP_BITS       0x8
#define FIMD_WINCON_BUFSTAT_L       (1 << 21)
#define FIMD_WINCON_BUFSTAT_H       (1 << 31)
#define FIMD_WINCON_BUFSTATUS       ((1 << 21) | (1 << 31))
#define FIMD_WINCON_BUF0_STAT       ((0 << 21) | (0 << 31))
#define FIMD_WINCON_BUF1_STAT       ((1 << 21) | (0 << 31))
#define FIMD_WINCON_BUF2_STAT       ((0 << 21) | (1U << 31))
#define FIMD_WINCON_BUFSELECT       ((1 << 20) | (1 << 30))
#define FIMD_WINCON_BUF0_SEL        ((0 << 20) | (0 << 30))
#define FIMD_WINCON_BUF1_SEL        ((1 << 20) | (0 << 30))
#define FIMD_WINCON_BUF2_SEL        ((0 << 20) | (1 << 30))
#define FIMD_WINCON_BUFMODE         (1 << 14)
#define IS_PALETTIZED_MODE(w)       (w->wincon & 0xC)
#define PAL_MODE_WITH_ALPHA(x)       ((x) == 7)
#define WIN_BPP_MODE(w)             ((w->wincon >> 2) & 0xF)
#define WIN_BPP_MODE_WITH_ALPHA(w)     \
    (WIN_BPP_MODE(w) == 0xD || WIN_BPP_MODE(w) == 0xE)

/* Shadow control register */
#define FIMD_SHADOWCON              0x0034
#define FIMD_WINDOW_PROTECTED(s, w) ((s) & (1 << (10 + (w))))
/* Channel mapping control register */
#define FIMD_WINCHMAP               0x003C

/* Window position control registers */
#define FIMD_VIDOSD_START           0x0040
#define FIMD_VIDOSD_END             0x0088
#define FIMD_VIDOSD_COORD_MASK      0x07FF
#define FIMD_VIDOSD_HOR_SHIFT       11
#define FIMD_VIDOSD_VER_SHIFT       0
#define FIMD_VIDOSD_ALPHA_AEN0      0xFFF000
#define FIMD_VIDOSD_AEN0_SHIFT      12
#define FIMD_VIDOSD_ALPHA_AEN1      0x000FFF

/* Frame buffer address registers */
#define FIMD_VIDWADD0_START         0x00A0
#define FIMD_VIDWADD0_END           0x00C4
#define FIMD_VIDWADD0_END           0x00C4
#define FIMD_VIDWADD1_START         0x00D0
#define FIMD_VIDWADD1_END           0x00F4
#define FIMD_VIDWADD2_START         0x0100
#define FIMD_VIDWADD2_END           0x0110
#define FIMD_VIDWADD2_PAGEWIDTH     0x1FFF
#define FIMD_VIDWADD2_OFFSIZE       0x1FFF
#define FIMD_VIDWADD2_OFFSIZE_SHIFT 13
#define FIMD_VIDW0ADD0_B2           0x20A0
#define FIMD_VIDW4ADD0_B2           0x20C0

/* Video interrupt control registers */
#define FIMD_VIDINTCON0             0x130
#define FIMD_VIDINTCON1             0x134

/* Window color key registers */
#define FIMD_WKEYCON_START          0x140
#define FIMD_WKEYCON_END            0x15C
#define FIMD_WKEYCON0_COMPKEY       0x00FFFFFF
#define FIMD_WKEYCON0_CTL_SHIFT     24
#define FIMD_WKEYCON0_DIRCON        (1 << 24)
#define FIMD_WKEYCON0_KEYEN         (1 << 25)
#define FIMD_WKEYCON0_KEYBLEN       (1 << 26)
/* Window color key alpha control register */
#define FIMD_WKEYALPHA_START        0x160
#define FIMD_WKEYALPHA_END          0x16C

/* Dithering control register */
#define FIMD_DITHMODE               0x170

/* Window alpha control registers */
#define FIMD_VIDALPHA_ALPHA_LOWER   0x000F0F0F
#define FIMD_VIDALPHA_ALPHA_UPPER   0x00F0F0F0
#define FIMD_VIDWALPHA_START        0x21C
#define FIMD_VIDWALPHA_END          0x240

/* Window color map registers */
#define FIMD_WINMAP_START           0x180
#define FIMD_WINMAP_END             0x190
#define FIMD_WINMAP_EN              (1 << 24)
#define FIMD_WINMAP_COLOR_MASK      0x00FFFFFF

/* Window palette control registers */
#define FIMD_WPALCON_HIGH           0x019C
#define FIMD_WPALCON_LOW            0x01A0
#define FIMD_WPALCON_UPDATEEN       (1 << 9)
#define FIMD_WPAL_W0PAL_L           0x07
#define FIMD_WPAL_W0PAL_L_SHT        0
#define FIMD_WPAL_W1PAL_L           0x07
#define FIMD_WPAL_W1PAL_L_SHT       3
#define FIMD_WPAL_W2PAL_L           0x01
#define FIMD_WPAL_W2PAL_L_SHT       6
#define FIMD_WPAL_W2PAL_H           0x06
#define FIMD_WPAL_W2PAL_H_SHT       8
#define FIMD_WPAL_W3PAL_L           0x01
#define FIMD_WPAL_W3PAL_L_SHT       7
#define FIMD_WPAL_W3PAL_H           0x06
#define FIMD_WPAL_W3PAL_H_SHT       12
#define FIMD_WPAL_W4PAL_L           0x01
#define FIMD_WPAL_W4PAL_L_SHT       8
#define FIMD_WPAL_W4PAL_H           0x06
#define FIMD_WPAL_W4PAL_H_SHT       16

/* Trigger control registers */
#define FIMD_TRIGCON                0x01A4
#define FIMD_TRIGCON_ROMASK         0x00000004

/* LCD I80 Interface Control */
#define FIMD_I80IFCON_START         0x01B0
#define FIMD_I80IFCON_END           0x01BC
/* Color gain control register */
#define FIMD_COLORGAINCON           0x01C0
/* LCD i80 Interface Command Control */
#define FIMD_LDI_CMDCON0            0x01D0
#define FIMD_LDI_CMDCON1            0x01D4
/* I80 System Interface Manual Command Control */
#define FIMD_SIFCCON0               0x01E0
#define FIMD_SIFCCON2               0x01E8

/* Hue Control Registers */
#define FIMD_HUECOEFCR_START        0x01EC
#define FIMD_HUECOEFCR_END          0x01F4
#define FIMD_HUECOEFCB_START        0x01FC
#define FIMD_HUECOEFCB_END          0x0208
#define FIMD_HUEOFFSET              0x020C

/* Video interrupt control registers */
#define FIMD_VIDINT_INTFIFOPEND     (1 << 0)
#define FIMD_VIDINT_INTFRMPEND      (1 << 1)
#define FIMD_VIDINT_INTI80PEND      (1 << 2)
#define FIMD_VIDINT_INTEN           (1 << 0)
#define FIMD_VIDINT_INTFIFOEN       (1 << 1)
#define FIMD_VIDINT_INTFRMEN        (1 << 12)
#define FIMD_VIDINT_I80IFDONE       (1 << 17)

/* Window blend equation control registers */
#define FIMD_BLENDEQ_START          0x0244
#define FIMD_BLENDEQ_END            0x0250
#define FIMD_BLENDCON               0x0260
#define FIMD_ALPHA_8BIT             (1 << 0)
#define FIMD_BLENDEQ_COEF_MASK      0xF

/* Window RTQOS Control Registers */
#define FIMD_WRTQOSCON_START        0x0264
#define FIMD_WRTQOSCON_END          0x0274

/* LCD I80 Interface Command */
#define FIMD_I80IFCMD_START         0x0280
#define FIMD_I80IFCMD_END           0x02AC

/* Shadow windows control registers */
#define FIMD_SHD_ADD0_START         0x40A0
#define FIMD_SHD_ADD0_END           0x40C0
#define FIMD_SHD_ADD1_START         0x40D0
#define FIMD_SHD_ADD1_END           0x40F0
#define FIMD_SHD_ADD2_START         0x4100
#define FIMD_SHD_ADD2_END           0x4110

/* Palette memory */
#define FIMD_PAL_MEM_START          0x2400
#define FIMD_PAL_MEM_END            0x37FC
/* Palette memory aliases for windows 0 and 1 */
#define FIMD_PALMEM_AL_START        0x0400
#define FIMD_PALMEM_AL_END          0x0BFC

typedef struct {
    uint8_t r, g, b;
    /* D[31..24]dummy, D[23..16]rAlpha, D[15..8]gAlpha, D[7..0]bAlpha */
    uint32_t a;
} rgba;
#define RGBA_SIZE  7

typedef void pixel_to_rgb_func(uint32_t pixel, rgba *p);
typedef struct Exynos4210fimdWindow Exynos4210fimdWindow;

struct Exynos4210fimdWindow {
    uint32_t wincon;        /* Window control register */
    uint32_t buf_start[3];  /* Start address for video frame buffer */
    uint32_t buf_end[3];    /* End address for video frame buffer */
    uint32_t keycon[2];     /* Window color key registers */
    uint32_t keyalpha;      /* Color key alpha control register */
    uint32_t winmap;        /* Window color map register */
    uint32_t blendeq;       /* Window blending equation control register */
    uint32_t rtqoscon;      /* Window RTQOS Control Registers */
    uint32_t palette[256];  /* Palette RAM */
    uint32_t shadow_buf_start;      /* Start address of shadow frame buffer */
    uint32_t shadow_buf_end;        /* End address of shadow frame buffer */
    uint32_t shadow_buf_size;       /* Virtual shadow screen width */

    pixel_to_rgb_func *pixel_to_rgb;
    void (*draw_line)(Exynos4210fimdWindow *w, uint8_t *src, uint8_t *dst,
            bool blend);
    uint32_t (*get_alpha)(Exynos4210fimdWindow *w, uint32_t pix_a);
    uint16_t lefttop_x, lefttop_y;   /* VIDOSD0 register */
    uint16_t rightbot_x, rightbot_y; /* VIDOSD1 register */
    uint32_t osdsize;                /* VIDOSD2&3 register */
    uint32_t alpha_val[2];           /* VIDOSD2&3, VIDWALPHA registers */
    uint16_t virtpage_width;         /* VIDWADD2 register */
    uint16_t virtpage_offsize;       /* VIDWADD2 register */
    MemoryRegionSection mem_section; /* RAM fragment containing framebuffer */
    uint8_t *host_fb_addr;           /* Host pointer to window's framebuffer */
    hwaddr fb_len;       /* Framebuffer length */
};

#define TYPE_EXYNOS4210_FIMD "exynos4210.fimd"
#define EXYNOS4210_FIMD(obj) \
    OBJECT_CHECK(Exynos4210fimdState, (obj), TYPE_EXYNOS4210_FIMD)

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    QemuConsole *console;
    qemu_irq irq[3];

    uint32_t vidcon[4];     /* Video main control registers 0-3 */
    uint32_t vidtcon[4];    /* Video time control registers 0-3 */
    uint32_t shadowcon;     /* Window shadow control register */
    uint32_t winchmap;      /* Channel mapping control register */
    uint32_t vidintcon[2];  /* Video interrupt control registers */
    uint32_t dithmode;      /* Dithering control register */
    uint32_t wpalcon[2];    /* Window palette control registers */
    uint32_t trigcon;       /* Trigger control register */
    uint32_t i80ifcon[4];   /* I80 interface control registers */
    uint32_t colorgaincon;  /* Color gain control register */
    uint32_t ldi_cmdcon[2]; /* LCD I80 interface command control */
    uint32_t sifccon[3];    /* I80 System Interface Manual Command Control */
    uint32_t huecoef_cr[4]; /* Hue control registers */
    uint32_t huecoef_cb[4]; /* Hue control registers */
    uint32_t hueoffset;     /* Hue offset control register */
    uint32_t blendcon;      /* Blending control register */
    uint32_t i80ifcmd[12];  /* LCD I80 Interface Command */

    Exynos4210fimdWindow window[5];    /* Window-specific registers */
    uint8_t *ifb;           /* Internal frame buffer */
    bool invalidate;        /* Image needs to be redrawn */
    bool enabled;           /* Display controller is enabled */
} Exynos4210fimdState;

/* Perform byte/halfword/word swap of data according to WINCON */
static inline void fimd_swap_data(unsigned int swap_ctl, uint64_t *data)
{
    int i;
    uint64_t res;
    uint64_t x = *data;

    if (swap_ctl & FIMD_WINCON_SWAP_BITS) {
        res = 0;
        for (i = 0; i < 64; i++) {
            if (x & (1ULL << (63 - i))) {
                res |= (1ULL << i);
            }
        }
        x = res;
    }

    if (swap_ctl & FIMD_WINCON_SWAP_BYTE) {
        x = bswap64(x);
    }

    if (swap_ctl & FIMD_WINCON_SWAP_HWORD) {
        x = ((x & 0x000000000000FFFFULL) << 48) |
            ((x & 0x00000000FFFF0000ULL) << 16) |
            ((x & 0x0000FFFF00000000ULL) >> 16) |
            ((x & 0xFFFF000000000000ULL) >> 48);
    }

    if (swap_ctl & FIMD_WINCON_SWAP_WORD) {
        x = ((x & 0x00000000FFFFFFFFULL) << 32) |
            ((x & 0xFFFFFFFF00000000ULL) >> 32);
    }

    *data = x;
}

/* Conversion routines of Pixel data from frame buffer area to internal RGBA
 * pixel representation.
 * Every color component internally represented as 8-bit value. If original
 * data has less than 8 bit for component, data is extended to 8 bit. For
 * example, if blue component has only two possible values 0 and 1 it will be
 * extended to 0 and 0xFF */

/* One bit for alpha representation */
#define DEF_PIXEL_TO_RGB_A1(N, R, G, B) \
static void N(uint32_t pixel, rgba *p) \
{ \
    p->b = ((pixel & ((1 << (B)) - 1)) << (8 - (B))) | \
           ((pixel >> (2 * (B) - 8)) & ((1 << (8 - (B))) - 1)); \
    pixel >>= (B); \
    p->g = (pixel & ((1 << (G)) - 1)) << (8 - (G)) | \
           ((pixel >> (2 * (G) - 8)) & ((1 << (8 - (G))) - 1)); \
    pixel >>= (G); \
    p->r = (pixel & ((1 << (R)) - 1)) << (8 - (R)) | \
           ((pixel >> (2 * (R) - 8)) & ((1 << (8 - (R))) - 1)); \
    pixel >>= (R); \
    p->a = (pixel & 0x1); \
}

DEF_PIXEL_TO_RGB_A1(pixel_a444_to_rgb, 4, 4, 4)
DEF_PIXEL_TO_RGB_A1(pixel_a555_to_rgb, 5, 5, 5)
DEF_PIXEL_TO_RGB_A1(pixel_a666_to_rgb, 6, 6, 6)
DEF_PIXEL_TO_RGB_A1(pixel_a665_to_rgb, 6, 6, 5)
DEF_PIXEL_TO_RGB_A1(pixel_a888_to_rgb, 8, 8, 8)
DEF_PIXEL_TO_RGB_A1(pixel_a887_to_rgb, 8, 8, 7)

/* Alpha component is always zero */
#define DEF_PIXEL_TO_RGB_A0(N, R, G, B) \
static void N(uint32_t pixel, rgba *p) \
{ \
    p->b = ((pixel & ((1 << (B)) - 1)) << (8 - (B))) | \
           ((pixel >> (2 * (B) - 8)) & ((1 << (8 - (B))) - 1)); \
    pixel >>= (B); \
    p->g = (pixel & ((1 << (G)) - 1)) << (8 - (G)) | \
           ((pixel >> (2 * (G) - 8)) & ((1 << (8 - (G))) - 1)); \
    pixel >>= (G); \
    p->r = (pixel & ((1 << (R)) - 1)) << (8 - (R)) | \
           ((pixel >> (2 * (R) - 8)) & ((1 << (8 - (R))) - 1)); \
    p->a = 0x0; \
}

DEF_PIXEL_TO_RGB_A0(pixel_565_to_rgb,  5, 6, 5)
DEF_PIXEL_TO_RGB_A0(pixel_555_to_rgb,  5, 5, 5)
DEF_PIXEL_TO_RGB_A0(pixel_666_to_rgb,  6, 6, 6)
DEF_PIXEL_TO_RGB_A0(pixel_888_to_rgb,  8, 8, 8)

/* Alpha component has some meaningful value */
#define DEF_PIXEL_TO_RGB_A(N, R, G, B, A) \
static void N(uint32_t pixel, rgba *p) \
{ \
    p->b = ((pixel & ((1 << (B)) - 1)) << (8 - (B))) | \
           ((pixel >> (2 * (B) - 8)) & ((1 << (8 - (B))) - 1)); \
    pixel >>= (B); \
    p->g = (pixel & ((1 << (G)) - 1)) << (8 - (G)) | \
           ((pixel >> (2 * (G) - 8)) & ((1 << (8 - (G))) - 1)); \
    pixel >>= (G); \
    p->r = (pixel & ((1 << (R)) - 1)) << (8 - (R)) | \
           ((pixel >> (2 * (R) - 8)) & ((1 << (8 - (R))) - 1)); \
    pixel >>= (R); \
    p->a = (pixel & ((1 << (A)) - 1)) << (8 - (A)) | \
           ((pixel >> (2 * (A) - 8)) & ((1 << (8 - (A))) - 1)); \
    p->a = p->a | (p->a << 8) | (p->a << 16); \
}

DEF_PIXEL_TO_RGB_A(pixel_4444_to_rgb, 4, 4, 4, 4)
DEF_PIXEL_TO_RGB_A(pixel_8888_to_rgb, 8, 8, 8, 8)

/* Lookup table to extent 2-bit color component to 8 bit */
static const uint8_t pixel_lutable_2b[4] = {
     0x0, 0x55, 0xAA, 0xFF
};
/* Lookup table to extent 3-bit color component to 8 bit */
static const uint8_t pixel_lutable_3b[8] = {
     0x0, 0x24, 0x49, 0x6D, 0x92, 0xB6, 0xDB, 0xFF
};
/* Special case for a232 bpp mode */
static void pixel_a232_to_rgb(uint32_t pixel, rgba *p)
{
    p->b = pixel_lutable_2b[(pixel & 0x3)];
    pixel >>= 2;
    p->g = pixel_lutable_3b[(pixel & 0x7)];
    pixel >>= 3;
    p->r = pixel_lutable_2b[(pixel & 0x3)];
    pixel >>= 2;
    p->a = (pixel & 0x1);
}

/* Special case for (5+1, 5+1, 5+1) mode. Data bit 15 is common LSB
 * for all three color components */
static void pixel_1555_to_rgb(uint32_t pixel, rgba *p)
{
    uint8_t comm = (pixel >> 15) & 1;
    p->b = ((((pixel & 0x1F) << 1) | comm) << 2) | ((pixel >> 3) & 0x3);
    pixel >>= 5;
    p->g = ((((pixel & 0x1F) << 1) | comm) << 2) | ((pixel >> 3) & 0x3);
    pixel >>= 5;
    p->r = ((((pixel & 0x1F) << 1) | comm) << 2) | ((pixel >> 3) & 0x3);
    p->a = 0x0;
}

/* Put/get pixel to/from internal LCD Controller framebuffer */

static int put_pixel_ifb(const rgba p, uint8_t *d)
{
    *(uint8_t *)d++ = p.r;
    *(uint8_t *)d++ = p.g;
    *(uint8_t *)d++ = p.b;
    *(uint32_t *)d = p.a;
    return RGBA_SIZE;
}

static int get_pixel_ifb(const uint8_t *s, rgba *p)
{
    p->r = *(uint8_t *)s++;
    p->g = *(uint8_t *)s++;
    p->b = *(uint8_t *)s++;
    p->a = (*(uint32_t *)s) & 0x00FFFFFF;
    return RGBA_SIZE;
}

static pixel_to_rgb_func *palette_data_format[8] = {
    [0] = pixel_565_to_rgb,
    [1] = pixel_a555_to_rgb,
    [2] = pixel_666_to_rgb,
    [3] = pixel_a665_to_rgb,
    [4] = pixel_a666_to_rgb,
    [5] = pixel_888_to_rgb,
    [6] = pixel_a888_to_rgb,
    [7] = pixel_8888_to_rgb
};

/* Returns Index in palette data formats table for given window number WINDOW */
static uint32_t
exynos4210_fimd_palette_format(Exynos4210fimdState *s, int window)
{
    uint32_t ret;

    switch (window) {
    case 0:
        ret = (s->wpalcon[1] >> FIMD_WPAL_W0PAL_L_SHT) & FIMD_WPAL_W0PAL_L;
        if (ret != 7) {
            ret = 6 - ret;
        }
        break;
    case 1:
        ret = (s->wpalcon[1] >> FIMD_WPAL_W1PAL_L_SHT) & FIMD_WPAL_W1PAL_L;
        if (ret != 7) {
            ret = 6 - ret;
        }
        break;
    case 2:
        ret = ((s->wpalcon[0] >> FIMD_WPAL_W2PAL_H_SHT) & FIMD_WPAL_W2PAL_H) |
            ((s->wpalcon[1] >> FIMD_WPAL_W2PAL_L_SHT) & FIMD_WPAL_W2PAL_L);
        break;
    case 3:
        ret = ((s->wpalcon[0] >> FIMD_WPAL_W3PAL_H_SHT) & FIMD_WPAL_W3PAL_H) |
            ((s->wpalcon[1] >> FIMD_WPAL_W3PAL_L_SHT) & FIMD_WPAL_W3PAL_L);
        break;
    case 4:
        ret = ((s->wpalcon[0] >> FIMD_WPAL_W4PAL_H_SHT) & FIMD_WPAL_W4PAL_H) |
            ((s->wpalcon[1] >> FIMD_WPAL_W4PAL_L_SHT) & FIMD_WPAL_W4PAL_L);
        break;
    default:
        hw_error("exynos4210.fimd: incorrect window number %d\n", window);
        ret = 0;
        break;
    }
    return ret;
}

#define FIMD_1_MINUS_COLOR(x)    \
            ((0xFF - ((x) & 0xFF)) | (0xFF00 - ((x) & 0xFF00)) | \
                                  (0xFF0000 - ((x) & 0xFF0000)))
#define EXTEND_LOWER_HALFBYTE(x) (((x) & 0xF0F0F) | (((x) << 4) & 0xF0F0F0))
#define EXTEND_UPPER_HALFBYTE(x) (((x) & 0xF0F0F0) | (((x) >> 4) & 0xF0F0F))

/* Multiply three lower bytes of two 32-bit words with each other.
 * Each byte with values 0-255 is considered as a number with possible values
 * in a range [0 - 1] */
static inline uint32_t fimd_mult_each_byte(uint32_t a, uint32_t b)
{
    uint32_t tmp;
    uint32_t ret;

    ret = ((tmp = (((a & 0xFF) * (b & 0xFF)) / 0xFF)) > 0xFF) ? 0xFF : tmp;
    ret |= ((tmp = ((((a >> 8) & 0xFF) * ((b >> 8) & 0xFF)) / 0xFF)) > 0xFF) ?
            0xFF00 : tmp << 8;
    ret |= ((tmp = ((((a >> 16) & 0xFF) * ((b >> 16) & 0xFF)) / 0xFF)) > 0xFF) ?
            0xFF0000 : tmp << 16;
    return ret;
}

/* For each corresponding bytes of two 32-bit words: (a*b + c*d)
 * Byte values 0-255 are mapped to a range [0 .. 1] */
static inline uint32_t
fimd_mult_and_sum_each_byte(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t tmp;
    uint32_t ret;

    ret = ((tmp = (((a & 0xFF) * (b & 0xFF) + (c & 0xFF) * (d & 0xFF)) / 0xFF))
            > 0xFF) ? 0xFF : tmp;
    ret |= ((tmp = ((((a >> 8) & 0xFF) * ((b >> 8) & 0xFF) + ((c >> 8) & 0xFF) *
            ((d >> 8) & 0xFF)) / 0xFF)) > 0xFF) ? 0xFF00 : tmp << 8;
    ret |= ((tmp = ((((a >> 16) & 0xFF) * ((b >> 16) & 0xFF) +
            ((c >> 16) & 0xFF) * ((d >> 16) & 0xFF)) / 0xFF)) > 0xFF) ?
                    0xFF0000 : tmp << 16;
    return ret;
}

/* These routines cover all possible sources of window's transparent factor
 * used in blending equation. Choice of routine is affected by WPALCON
 * registers, BLENDCON register and window's WINCON register */

static uint32_t fimd_get_alpha_pix(Exynos4210fimdWindow *w, uint32_t pix_a)
{
    return pix_a;
}

static uint32_t
fimd_get_alpha_pix_extlow(Exynos4210fimdWindow *w, uint32_t pix_a)
{
    return EXTEND_LOWER_HALFBYTE(pix_a);
}

static uint32_t
fimd_get_alpha_pix_exthigh(Exynos4210fimdWindow *w, uint32_t pix_a)
{
    return EXTEND_UPPER_HALFBYTE(pix_a);
}

static uint32_t fimd_get_alpha_mult(Exynos4210fimdWindow *w, uint32_t pix_a)
{
    return fimd_mult_each_byte(pix_a, w->alpha_val[0]);
}

static uint32_t fimd_get_alpha_mult_ext(Exynos4210fimdWindow *w, uint32_t pix_a)
{
    return fimd_mult_each_byte(EXTEND_LOWER_HALFBYTE(pix_a),
            EXTEND_UPPER_HALFBYTE(w->alpha_val[0]));
}

static uint32_t fimd_get_alpha_aen(Exynos4210fimdWindow *w, uint32_t pix_a)
{
    return w->alpha_val[pix_a];
}

static uint32_t fimd_get_alpha_aen_ext(Exynos4210fimdWindow *w, uint32_t pix_a)
{
    return EXTEND_UPPER_HALFBYTE(w->alpha_val[pix_a]);
}

static uint32_t fimd_get_alpha_sel(Exynos4210fimdWindow *w, uint32_t pix_a)
{
    return w->alpha_val[(w->wincon & FIMD_WINCON_ALPHA_SEL) ? 1 : 0];
}

static uint32_t fimd_get_alpha_sel_ext(Exynos4210fimdWindow *w, uint32_t pix_a)
{
    return EXTEND_UPPER_HALFBYTE(w->alpha_val[(w->wincon &
            FIMD_WINCON_ALPHA_SEL) ? 1 : 0]);
}

/* Updates currently active alpha value get function for specified window */
static void fimd_update_get_alpha(Exynos4210fimdState *s, int win)
{
    Exynos4210fimdWindow *w = &s->window[win];
    const bool alpha_is_8bit = s->blendcon & FIMD_ALPHA_8BIT;

    if (w->wincon & FIMD_WINCON_BLD_PIX) {
        if ((w->wincon & FIMD_WINCON_ALPHA_SEL) && WIN_BPP_MODE_WITH_ALPHA(w)) {
            /* In this case, alpha component contains meaningful value */
            if (w->wincon & FIMD_WINCON_ALPHA_MUL) {
                w->get_alpha = alpha_is_8bit ?
                        fimd_get_alpha_mult : fimd_get_alpha_mult_ext;
            } else {
                w->get_alpha = alpha_is_8bit ?
                        fimd_get_alpha_pix : fimd_get_alpha_pix_extlow;
            }
        } else {
            if (IS_PALETTIZED_MODE(w) &&
                  PAL_MODE_WITH_ALPHA(exynos4210_fimd_palette_format(s, win))) {
                /* Alpha component has 8-bit numeric value */
                w->get_alpha = alpha_is_8bit ?
                        fimd_get_alpha_pix : fimd_get_alpha_pix_exthigh;
            } else {
                /* Alpha has only two possible values (AEN) */
                w->get_alpha = alpha_is_8bit ?
                        fimd_get_alpha_aen : fimd_get_alpha_aen_ext;
            }
        }
    } else {
        w->get_alpha = alpha_is_8bit ? fimd_get_alpha_sel :
                fimd_get_alpha_sel_ext;
    }
}

/* Blends current window's (w) pixel (foreground pixel *ret) with background
 * window (w_blend) pixel p_bg according to formula:
 * NEW_COLOR = a_coef x FG_PIXEL_COLOR + b_coef x BG_PIXEL_COLOR
 * NEW_ALPHA = p_coef x FG_ALPHA + q_coef x BG_ALPHA
 */
static void
exynos4210_fimd_blend_pixel(Exynos4210fimdWindow *w, rgba p_bg, rgba *ret)
{
    rgba p_fg = *ret;
    uint32_t bg_color = ((p_bg.r & 0xFF) << 16) | ((p_bg.g & 0xFF) << 8) |
            (p_bg.b & 0xFF);
    uint32_t fg_color = ((p_fg.r & 0xFF) << 16) | ((p_fg.g & 0xFF) << 8) |
            (p_fg.b & 0xFF);
    uint32_t alpha_fg = p_fg.a;
    int i;
    /* It is possible that blending equation parameters a and b do not
     * depend on window BLENEQ register. Account for this with first_coef */
    enum { A_COEF = 0, B_COEF = 1, P_COEF = 2, Q_COEF = 3, COEF_NUM = 4};
    uint32_t first_coef = A_COEF;
    uint32_t blend_param[COEF_NUM];

    if (w->keycon[0] & FIMD_WKEYCON0_KEYEN) {
        uint32_t colorkey = (w->keycon[1] &
              ~(w->keycon[0] & FIMD_WKEYCON0_COMPKEY)) & FIMD_WKEYCON0_COMPKEY;

        if ((w->keycon[0] & FIMD_WKEYCON0_DIRCON) &&
            (bg_color & ~(w->keycon[0] & FIMD_WKEYCON0_COMPKEY)) == colorkey) {
            /* Foreground pixel is displayed */
            if (w->keycon[0] & FIMD_WKEYCON0_KEYBLEN) {
                alpha_fg = w->keyalpha;
                blend_param[A_COEF] = alpha_fg;
                blend_param[B_COEF] = FIMD_1_MINUS_COLOR(alpha_fg);
            } else {
                alpha_fg = 0;
                blend_param[A_COEF] = 0xFFFFFF;
                blend_param[B_COEF] = 0x0;
            }
            first_coef = P_COEF;
        } else if ((w->keycon[0] & FIMD_WKEYCON0_DIRCON) == 0 &&
            (fg_color & ~(w->keycon[0] & FIMD_WKEYCON0_COMPKEY)) == colorkey) {
            /* Background pixel is displayed */
            if (w->keycon[0] & FIMD_WKEYCON0_KEYBLEN) {
                alpha_fg = w->keyalpha;
                blend_param[A_COEF] = alpha_fg;
                blend_param[B_COEF] = FIMD_1_MINUS_COLOR(alpha_fg);
            } else {
                alpha_fg = 0;
                blend_param[A_COEF] = 0x0;
                blend_param[B_COEF] = 0xFFFFFF;
            }
            first_coef = P_COEF;
        }
    }

    for (i = first_coef; i < COEF_NUM; i++) {
        switch ((w->blendeq >> i * 6) & FIMD_BLENDEQ_COEF_MASK) {
        case 0:
            blend_param[i] = 0;
            break;
        case 1:
            blend_param[i] = 0xFFFFFF;
            break;
        case 2:
            blend_param[i] = alpha_fg;
            break;
        case 3:
            blend_param[i] = FIMD_1_MINUS_COLOR(alpha_fg);
            break;
        case 4:
            blend_param[i] = p_bg.a;
            break;
        case 5:
            blend_param[i] = FIMD_1_MINUS_COLOR(p_bg.a);
            break;
        case 6:
            blend_param[i] = w->alpha_val[0];
            break;
        case 10:
            blend_param[i] = fg_color;
            break;
        case 11:
            blend_param[i] = FIMD_1_MINUS_COLOR(fg_color);
            break;
        case 12:
            blend_param[i] = bg_color;
            break;
        case 13:
            blend_param[i] = FIMD_1_MINUS_COLOR(bg_color);
            break;
        default:
            hw_error("exynos4210.fimd: blend equation coef illegal value\n");
            break;
        }
    }

    fg_color = fimd_mult_and_sum_each_byte(bg_color, blend_param[B_COEF],
            fg_color, blend_param[A_COEF]);
    ret->b = fg_color & 0xFF;
    fg_color >>= 8;
    ret->g = fg_color & 0xFF;
    fg_color >>= 8;
    ret->r = fg_color & 0xFF;
    ret->a = fimd_mult_and_sum_each_byte(alpha_fg, blend_param[P_COEF],
            p_bg.a, blend_param[Q_COEF]);
}

/* These routines read data from video frame buffer in system RAM, convert
 * this data to display controller internal representation, if necessary,
 * perform pixel blending with data, currently presented in internal buffer.
 * Result is stored in display controller internal frame buffer. */

/* Draw line with index in palette table in RAM frame buffer data */
#define DEF_DRAW_LINE_PALETTE(N) \
static void glue(draw_line_palette_, N)(Exynos4210fimdWindow *w, uint8_t *src, \
               uint8_t *dst, bool blend) \
{ \
    int width = w->rightbot_x - w->lefttop_x + 1; \
    uint8_t *ifb = dst; \
    uint8_t swap = (w->wincon & FIMD_WINCON_SWAP) >> FIMD_WINCON_SWAP_SHIFT; \
    uint64_t data; \
    rgba p, p_old; \
    int i; \
    do { \
        memcpy(&data, src, sizeof(data)); \
        src += 8; \
        fimd_swap_data(swap, &data); \
        for (i = (64 / (N) - 1); i >= 0; i--) { \
            w->pixel_to_rgb(w->palette[(data >> ((N) * i)) & \
                                   ((1ULL << (N)) - 1)], &p); \
            p.a = w->get_alpha(w, p.a); \
            if (blend) { \
                ifb +=  get_pixel_ifb(ifb, &p_old); \
                exynos4210_fimd_blend_pixel(w, p_old, &p); \
            } \
            dst += put_pixel_ifb(p, dst); \
        } \
        width -= (64 / (N)); \
    } while (width > 0); \
}

/* Draw line with direct color value in RAM frame buffer data */
#define DEF_DRAW_LINE_NOPALETTE(N) \
static void glue(draw_line_, N)(Exynos4210fimdWindow *w, uint8_t *src, \
                    uint8_t *dst, bool blend) \
{ \
    int width = w->rightbot_x - w->lefttop_x + 1; \
    uint8_t *ifb = dst; \
    uint8_t swap = (w->wincon & FIMD_WINCON_SWAP) >> FIMD_WINCON_SWAP_SHIFT; \
    uint64_t data; \
    rgba p, p_old; \
    int i; \
    do { \
        memcpy(&data, src, sizeof(data)); \
        src += 8; \
        fimd_swap_data(swap, &data); \
        for (i = (64 / (N) - 1); i >= 0; i--) { \
            w->pixel_to_rgb((data >> ((N) * i)) & ((1ULL << (N)) - 1), &p); \
            p.a = w->get_alpha(w, p.a); \
            if (blend) { \
                ifb += get_pixel_ifb(ifb, &p_old); \
                exynos4210_fimd_blend_pixel(w, p_old, &p); \
            } \
            dst += put_pixel_ifb(p, dst); \
        } \
        width -= (64 / (N)); \
    } while (width > 0); \
}

DEF_DRAW_LINE_PALETTE(1)
DEF_DRAW_LINE_PALETTE(2)
DEF_DRAW_LINE_PALETTE(4)
DEF_DRAW_LINE_PALETTE(8)
DEF_DRAW_LINE_NOPALETTE(8)  /* 8bpp mode has palette and non-palette versions */
DEF_DRAW_LINE_NOPALETTE(16)
DEF_DRAW_LINE_NOPALETTE(32)

/* Special draw line routine for window color map case */
static void draw_line_mapcolor(Exynos4210fimdWindow *w, uint8_t *src,
                       uint8_t *dst, bool blend)
{
    rgba p, p_old;
    uint8_t *ifb = dst;
    int width = w->rightbot_x - w->lefttop_x + 1;
    uint32_t map_color = w->winmap & FIMD_WINMAP_COLOR_MASK;

    do {
        pixel_888_to_rgb(map_color, &p);
        p.a = w->get_alpha(w, p.a);
        if (blend) {
            ifb += get_pixel_ifb(ifb, &p_old);
            exynos4210_fimd_blend_pixel(w, p_old, &p);
        }
        dst += put_pixel_ifb(p, dst);
    } while (--width);
}

/* Write RGB to QEMU's GraphicConsole framebuffer */

static int put_to_qemufb_pixel8(const rgba p, uint8_t *d)
{
    uint32_t pixel = rgb_to_pixel8(p.r, p.g, p.b);
    *(uint8_t *)d = pixel;
    return 1;
}

static int put_to_qemufb_pixel15(const rgba p, uint8_t *d)
{
    uint32_t pixel = rgb_to_pixel15(p.r, p.g, p.b);
    *(uint16_t *)d = pixel;
    return 2;
}

static int put_to_qemufb_pixel16(const rgba p, uint8_t *d)
{
    uint32_t pixel = rgb_to_pixel16(p.r, p.g, p.b);
    *(uint16_t *)d = pixel;
    return 2;
}

static int put_to_qemufb_pixel24(const rgba p, uint8_t *d)
{
    uint32_t pixel = rgb_to_pixel24(p.r, p.g, p.b);
    *(uint8_t *)d++ = (pixel >>  0) & 0xFF;
    *(uint8_t *)d++ = (pixel >>  8) & 0xFF;
    *(uint8_t *)d++ = (pixel >> 16) & 0xFF;
    return 3;
}

static int put_to_qemufb_pixel32(const rgba p, uint8_t *d)
{
    uint32_t pixel = rgb_to_pixel24(p.r, p.g, p.b);
    *(uint32_t *)d = pixel;
    return 4;
}

/* Routine to copy pixel from internal buffer to QEMU buffer */
static int (*put_pixel_toqemu)(const rgba p, uint8_t *pixel);
static inline void fimd_update_putpix_qemu(int bpp)
{
    switch (bpp) {
    case 8:
        put_pixel_toqemu = put_to_qemufb_pixel8;
        break;
    case 15:
        put_pixel_toqemu = put_to_qemufb_pixel15;
        break;
    case 16:
        put_pixel_toqemu = put_to_qemufb_pixel16;
        break;
    case 24:
        put_pixel_toqemu = put_to_qemufb_pixel24;
        break;
    case 32:
        put_pixel_toqemu = put_to_qemufb_pixel32;
        break;
    default:
        hw_error("exynos4210.fimd: unsupported BPP (%d)", bpp);
        break;
    }
}

/* Routine to copy a line from internal frame buffer to QEMU display */
static void fimd_copy_line_toqemu(int width, uint8_t *src, uint8_t *dst)
{
    rgba p;

    do {
        src += get_pixel_ifb(src, &p);
        dst += put_pixel_toqemu(p, dst);
    } while (--width);
}

/* Parse BPPMODE_F = WINCON1[5:2] bits */
static void exynos4210_fimd_update_win_bppmode(Exynos4210fimdState *s, int win)
{
    Exynos4210fimdWindow *w = &s->window[win];

    if (w->winmap & FIMD_WINMAP_EN) {
        w->draw_line = draw_line_mapcolor;
        return;
    }

    switch (WIN_BPP_MODE(w)) {
    case 0:
        w->draw_line = draw_line_palette_1;
        w->pixel_to_rgb =
                palette_data_format[exynos4210_fimd_palette_format(s, win)];
        break;
    case 1:
        w->draw_line = draw_line_palette_2;
        w->pixel_to_rgb =
                palette_data_format[exynos4210_fimd_palette_format(s, win)];
        break;
    case 2:
        w->draw_line = draw_line_palette_4;
        w->pixel_to_rgb =
                palette_data_format[exynos4210_fimd_palette_format(s, win)];
        break;
    case 3:
        w->draw_line = draw_line_palette_8;
        w->pixel_to_rgb =
                palette_data_format[exynos4210_fimd_palette_format(s, win)];
        break;
    case 4:
        w->draw_line = draw_line_8;
        w->pixel_to_rgb = pixel_a232_to_rgb;
        break;
    case 5:
        w->draw_line = draw_line_16;
        w->pixel_to_rgb = pixel_565_to_rgb;
        break;
    case 6:
        w->draw_line = draw_line_16;
        w->pixel_to_rgb = pixel_a555_to_rgb;
        break;
    case 7:
        w->draw_line = draw_line_16;
        w->pixel_to_rgb = pixel_1555_to_rgb;
        break;
    case 8:
        w->draw_line = draw_line_32;
        w->pixel_to_rgb = pixel_666_to_rgb;
        break;
    case 9:
        w->draw_line = draw_line_32;
        w->pixel_to_rgb = pixel_a665_to_rgb;
        break;
    case 10:
        w->draw_line = draw_line_32;
        w->pixel_to_rgb = pixel_a666_to_rgb;
        break;
    case 11:
        w->draw_line = draw_line_32;
        w->pixel_to_rgb = pixel_888_to_rgb;
        break;
    case 12:
        w->draw_line = draw_line_32;
        w->pixel_to_rgb = pixel_a887_to_rgb;
        break;
    case 13:
        w->draw_line = draw_line_32;
        if ((w->wincon & FIMD_WINCON_BLD_PIX) && (w->wincon &
                FIMD_WINCON_ALPHA_SEL)) {
            w->pixel_to_rgb = pixel_8888_to_rgb;
        } else {
            w->pixel_to_rgb = pixel_a888_to_rgb;
        }
        break;
    case 14:
        w->draw_line = draw_line_16;
        if ((w->wincon & FIMD_WINCON_BLD_PIX) && (w->wincon &
                FIMD_WINCON_ALPHA_SEL)) {
            w->pixel_to_rgb = pixel_4444_to_rgb;
        } else {
            w->pixel_to_rgb = pixel_a444_to_rgb;
        }
        break;
    case 15:
        w->draw_line = draw_line_16;
        w->pixel_to_rgb = pixel_555_to_rgb;
        break;
    }
}

#if EXYNOS4210_FIMD_MODE_TRACE > 0
static const char *exynos4210_fimd_get_bppmode(int mode_code)
{
    switch (mode_code) {
    case 0:
        return "1 bpp";
    case 1:
        return "2 bpp";
    case 2:
        return "4 bpp";
    case 3:
        return "8 bpp (palettized)";
    case 4:
        return "8 bpp (non-palettized, A: 1-R:2-G:3-B:2)";
    case 5:
        return "16 bpp (non-palettized, R:5-G:6-B:5)";
    case 6:
        return "16 bpp (non-palettized, A:1-R:5-G:5-B:5)";
    case 7:
        return "16 bpp (non-palettized, I :1-R:5-G:5-B:5)";
    case 8:
        return "Unpacked 18 bpp (non-palettized, R:6-G:6-B:6)";
    case 9:
        return "Unpacked 18bpp (non-palettized,A:1-R:6-G:6-B:5)";
    case 10:
        return "Unpacked 19bpp (non-palettized,A:1-R:6-G:6-B:6)";
    case 11:
        return "Unpacked 24 bpp (non-palettized R:8-G:8-B:8)";
    case 12:
        return "Unpacked 24 bpp (non-palettized A:1-R:8-G:8-B:7)";
    case 13:
        return "Unpacked 25 bpp (non-palettized A:1-R:8-G:8-B:8)";
    case 14:
        return "Unpacked 13 bpp (non-palettized A:1-R:4-G:4-B:4)";
    case 15:
        return "Unpacked 15 bpp (non-palettized R:5-G:5-B:5)";
    default:
        return "Non-existing bpp mode";
    }
}

static inline void exynos4210_fimd_trace_bppmode(Exynos4210fimdState *s,
                int win_num, uint32_t val)
{
    Exynos4210fimdWindow *w = &s->window[win_num];

    if (w->winmap & FIMD_WINMAP_EN) {
        printf("QEMU FIMD: Window %d is mapped with MAPCOLOR=0x%x\n",
                win_num, w->winmap & 0xFFFFFF);
        return;
    }

    if ((val != 0xFFFFFFFF) && ((w->wincon >> 2) & 0xF) == ((val >> 2) & 0xF)) {
        return;
    }
    printf("QEMU FIMD: Window %d BPP mode set to %s\n", win_num,
        exynos4210_fimd_get_bppmode((val >> 2) & 0xF));
}
#else
static inline void exynos4210_fimd_trace_bppmode(Exynos4210fimdState *s,
        int win_num, uint32_t val)
{

}
#endif

static inline int fimd_get_buffer_id(Exynos4210fimdWindow *w)
{
    switch (w->wincon & FIMD_WINCON_BUFSTATUS) {
    case FIMD_WINCON_BUF0_STAT:
        return 0;
    case FIMD_WINCON_BUF1_STAT:
        return 1;
    case FIMD_WINCON_BUF2_STAT:
        return 2;
    default:
        DPRINT_ERROR("Non-existent buffer index\n");
        return 0;
    }
}

static void exynos4210_fimd_invalidate(void *opaque)
{
    Exynos4210fimdState *s = (Exynos4210fimdState *)opaque;
    s->invalidate = true;
}

/* Updates specified window's MemorySection based on values of WINCON,
 * VIDOSDA, VIDOSDB, VIDWADDx and SHADOWCON registers */
static void fimd_update_memory_section(Exynos4210fimdState *s, unsigned win)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);
    Exynos4210fimdWindow *w = &s->window[win];
    hwaddr fb_start_addr, fb_mapped_len;

    if (!s->enabled || !(w->wincon & FIMD_WINCON_ENWIN) ||
            FIMD_WINDOW_PROTECTED(s->shadowcon, win)) {
        return;
    }

    if (w->host_fb_addr) {
        cpu_physical_memory_unmap(w->host_fb_addr, w->fb_len, 0, 0);
        w->host_fb_addr = NULL;
        w->fb_len = 0;
    }

    fb_start_addr = w->buf_start[fimd_get_buffer_id(w)];
    /* Total number of bytes of virtual screen used by current window */
    w->fb_len = fb_mapped_len = (w->virtpage_width + w->virtpage_offsize) *
            (w->rightbot_y - w->lefttop_y + 1);

    /* TODO: add .exit and unref the region there.  Not needed yet since sysbus
     * does not support hot-unplug.
     */
    if (w->mem_section.mr) {
        memory_region_set_log(w->mem_section.mr, false, DIRTY_MEMORY_VGA);
        memory_region_unref(w->mem_section.mr);
    }

    w->mem_section = memory_region_find(sysbus_address_space(sbd),
                                        fb_start_addr, w->fb_len);
    assert(w->mem_section.mr);
    assert(w->mem_section.offset_within_address_space == fb_start_addr);
    DPRINT_TRACE("Window %u framebuffer changed: address=0x%08x, len=0x%x\n",
            win, fb_start_addr, w->fb_len);

    if (int128_get64(w->mem_section.size) != w->fb_len ||
            !memory_region_is_ram(w->mem_section.mr)) {
        DPRINT_ERROR("Failed to find window %u framebuffer region\n", win);
        goto error_return;
    }

    w->host_fb_addr = cpu_physical_memory_map(fb_start_addr, &fb_mapped_len,
                                              false);
    if (!w->host_fb_addr) {
        DPRINT_ERROR("Failed to map window %u framebuffer\n", win);
        goto error_return;
    }

    if (fb_mapped_len != w->fb_len) {
        DPRINT_ERROR("Window %u mapped framebuffer length is less then "
                "expected\n", win);
        cpu_physical_memory_unmap(w->host_fb_addr, fb_mapped_len, 0, 0);
        goto error_return;
    }
    memory_region_set_log(w->mem_section.mr, true, DIRTY_MEMORY_VGA);
    exynos4210_fimd_invalidate(s);
    return;

error_return:
    memory_region_unref(w->mem_section.mr);
    w->mem_section.mr = NULL;
    w->mem_section.size = int128_zero();
    w->host_fb_addr = NULL;
    w->fb_len = 0;
}

static void exynos4210_fimd_enable(Exynos4210fimdState *s, bool enabled)
{
    if (enabled && !s->enabled) {
        unsigned w;
        s->enabled = true;
        for (w = 0; w < NUM_OF_WINDOWS; w++) {
            fimd_update_memory_section(s, w);
        }
    }
    s->enabled = enabled;
    DPRINT_TRACE("display controller %s\n", enabled ? "enabled" : "disabled");
}

static inline uint32_t unpack_upper_4(uint32_t x)
{
    return ((x & 0xF00) << 12) | ((x & 0xF0) << 8) | ((x & 0xF) << 4);
}

static inline uint32_t pack_upper_4(uint32_t x)
{
    return (((x & 0xF00000) >> 12) | ((x & 0xF000) >> 8) |
            ((x & 0xF0) >> 4)) & 0xFFF;
}

static void exynos4210_fimd_update_irq(Exynos4210fimdState *s)
{
    if (!(s->vidintcon[0] & FIMD_VIDINT_INTEN)) {
        qemu_irq_lower(s->irq[0]);
        qemu_irq_lower(s->irq[1]);
        qemu_irq_lower(s->irq[2]);
        return;
    }
    if ((s->vidintcon[0] & FIMD_VIDINT_INTFIFOEN) &&
            (s->vidintcon[1] & FIMD_VIDINT_INTFIFOPEND)) {
        qemu_irq_raise(s->irq[0]);
    } else {
        qemu_irq_lower(s->irq[0]);
    }
    if ((s->vidintcon[0] & FIMD_VIDINT_INTFRMEN) &&
            (s->vidintcon[1] & FIMD_VIDINT_INTFRMPEND)) {
        qemu_irq_raise(s->irq[1]);
    } else {
        qemu_irq_lower(s->irq[1]);
    }
    if ((s->vidintcon[0] & FIMD_VIDINT_I80IFDONE) &&
            (s->vidintcon[1] & FIMD_VIDINT_INTI80PEND)) {
        qemu_irq_raise(s->irq[2]);
    } else {
        qemu_irq_lower(s->irq[2]);
    }
}

static void exynos4210_update_resolution(Exynos4210fimdState *s)
{
    DisplaySurface *surface = qemu_console_surface(s->console);

    /* LCD resolution is stored in VIDEO TIME CONTROL REGISTER 2 */
    uint32_t width = ((s->vidtcon[2] >> FIMD_VIDTCON2_HOR_SHIFT) &
            FIMD_VIDTCON2_SIZE_MASK) + 1;
    uint32_t height = ((s->vidtcon[2] >> FIMD_VIDTCON2_VER_SHIFT) &
            FIMD_VIDTCON2_SIZE_MASK) + 1;

    if (s->ifb == NULL || surface_width(surface) != width ||
            surface_height(surface) != height) {
        DPRINT_L1("Resolution changed from %ux%u to %ux%u\n",
           surface_width(surface), surface_height(surface), width, height);
        qemu_console_resize(s->console, width, height);
        s->ifb = g_realloc(s->ifb, width * height * RGBA_SIZE + 1);
        memset(s->ifb, 0, width * height * RGBA_SIZE + 1);
        exynos4210_fimd_invalidate(s);
    }
}

static void exynos4210_fimd_update(void *opaque)
{
    Exynos4210fimdState *s = (Exynos4210fimdState *)opaque;
    DisplaySurface *surface;
    Exynos4210fimdWindow *w;
    DirtyBitmapSnapshot *snap;
    int i, line;
    hwaddr fb_line_addr, inc_size;
    int scrn_height;
    int first_line = -1, last_line = -1, scrn_width;
    bool blend = false;
    uint8_t *host_fb_addr;
    bool is_dirty = false;
    const int global_width = (s->vidtcon[2] & FIMD_VIDTCON2_SIZE_MASK) + 1;

    if (!s || !s->console || !s->enabled ||
        surface_bits_per_pixel(qemu_console_surface(s->console)) == 0) {
        return;
    }
    exynos4210_update_resolution(s);
    surface = qemu_console_surface(s->console);

    for (i = 0; i < NUM_OF_WINDOWS; i++) {
        w = &s->window[i];
        if ((w->wincon & FIMD_WINCON_ENWIN) && w->host_fb_addr) {
            scrn_height = w->rightbot_y - w->lefttop_y + 1;
            scrn_width = w->virtpage_width;
            /* Total width of virtual screen page in bytes */
            inc_size = scrn_width + w->virtpage_offsize;
            host_fb_addr = w->host_fb_addr;
            fb_line_addr = w->mem_section.offset_within_region;
            snap = memory_region_snapshot_and_clear_dirty(w->mem_section.mr,
                    fb_line_addr, inc_size * scrn_height, DIRTY_MEMORY_VGA);

            for (line = 0; line < scrn_height; line++) {
                is_dirty = memory_region_snapshot_get_dirty(w->mem_section.mr,
                            snap, fb_line_addr, scrn_width);

                if (s->invalidate || is_dirty) {
                    if (first_line == -1) {
                        first_line = line;
                    }
                    last_line = line;
                    w->draw_line(w, host_fb_addr, s->ifb +
                        w->lefttop_x * RGBA_SIZE + (w->lefttop_y + line) *
                        global_width * RGBA_SIZE, blend);
                }
                host_fb_addr += inc_size;
                fb_line_addr += inc_size;
            }
            g_free(snap);
            blend = true;
        }
    }

    /* Copy resulting image to QEMU_CONSOLE. */
    if (first_line >= 0) {
        uint8_t *d;
        int bpp;

        bpp = surface_bits_per_pixel(surface);
        fimd_update_putpix_qemu(bpp);
        bpp = (bpp + 1) >> 3;
        d = surface_data(surface);
        for (line = first_line; line <= last_line; line++) {
            fimd_copy_line_toqemu(global_width, s->ifb + global_width * line *
                    RGBA_SIZE, d + global_width * line * bpp);
        }
        dpy_gfx_update_full(s->console);
    }
    s->invalidate = false;
    s->vidintcon[1] |= FIMD_VIDINT_INTFRMPEND;
    if ((s->vidcon[0] & FIMD_VIDCON0_ENVID_F) == 0) {
        exynos4210_fimd_enable(s, false);
    }
    exynos4210_fimd_update_irq(s);
}

static void exynos4210_fimd_reset(DeviceState *d)
{
    Exynos4210fimdState *s = EXYNOS4210_FIMD(d);
    unsigned w;

    DPRINT_TRACE("Display controller reset\n");
    /* Set all display controller registers to 0 */
    memset(&s->vidcon, 0, (uint8_t *)&s->window - (uint8_t *)&s->vidcon);
    for (w = 0; w < NUM_OF_WINDOWS; w++) {
        memset(&s->window[w], 0, sizeof(Exynos4210fimdWindow));
        s->window[w].blendeq = 0xC2;
        exynos4210_fimd_update_win_bppmode(s, w);
        exynos4210_fimd_trace_bppmode(s, w, 0xFFFFFFFF);
        fimd_update_get_alpha(s, w);
    }

    g_free(s->ifb);
    s->ifb = NULL;

    exynos4210_fimd_invalidate(s);
    exynos4210_fimd_enable(s, false);
    /* Some registers have non-zero initial values */
    s->winchmap = 0x7D517D51;
    s->colorgaincon = 0x10040100;
    s->huecoef_cr[0] = s->huecoef_cr[3] = 0x01000100;
    s->huecoef_cb[0] = s->huecoef_cb[3] = 0x01000100;
    s->hueoffset = 0x01800080;
}

static void exynos4210_fimd_write(void *opaque, hwaddr offset,
                              uint64_t val, unsigned size)
{
    Exynos4210fimdState *s = (Exynos4210fimdState *)opaque;
    unsigned w, i;
    uint32_t old_value;

    DPRINT_L2("write offset 0x%08x, value=%llu(0x%08llx)\n", offset,
            (long long unsigned int)val, (long long unsigned int)val);

    switch (offset) {
    case FIMD_VIDCON0:
        if ((val & FIMD_VIDCON0_ENVID_MASK) == FIMD_VIDCON0_ENVID_MASK) {
            exynos4210_fimd_enable(s, true);
        } else {
            if ((val & FIMD_VIDCON0_ENVID) == 0) {
                exynos4210_fimd_enable(s, false);
            }
        }
        s->vidcon[0] = val;
        break;
    case FIMD_VIDCON1:
        /* Leave read-only bits as is */
        val = (val & (~FIMD_VIDCON1_ROMASK)) |
                (s->vidcon[1] & FIMD_VIDCON1_ROMASK);
        s->vidcon[1] = val;
        break;
    case FIMD_VIDCON2 ... FIMD_VIDCON3:
        s->vidcon[(offset) >> 2] = val;
        break;
    case FIMD_VIDTCON_START ... FIMD_VIDTCON_END:
        s->vidtcon[(offset - FIMD_VIDTCON_START) >> 2] = val;
        break;
    case FIMD_WINCON_START ... FIMD_WINCON_END:
        w = (offset - FIMD_WINCON_START) >> 2;
        /* Window's current buffer ID */
        i = fimd_get_buffer_id(&s->window[w]);
        old_value = s->window[w].wincon;
        val = (val & ~FIMD_WINCON_ROMASK) |
                (s->window[w].wincon & FIMD_WINCON_ROMASK);
        if (w == 0) {
            /* Window 0 wincon ALPHA_MUL bit must always be 0 */
            val &= ~FIMD_WINCON_ALPHA_MUL;
        }
        exynos4210_fimd_trace_bppmode(s, w, val);
        switch (val & FIMD_WINCON_BUFSELECT) {
        case FIMD_WINCON_BUF0_SEL:
            val &= ~FIMD_WINCON_BUFSTATUS;
            break;
        case FIMD_WINCON_BUF1_SEL:
            val = (val & ~FIMD_WINCON_BUFSTAT_H) | FIMD_WINCON_BUFSTAT_L;
            break;
        case FIMD_WINCON_BUF2_SEL:
            if (val & FIMD_WINCON_BUFMODE) {
                val = (val & ~FIMD_WINCON_BUFSTAT_L) | FIMD_WINCON_BUFSTAT_H;
            }
            break;
        default:
            break;
        }
        s->window[w].wincon = val;
        exynos4210_fimd_update_win_bppmode(s, w);
        fimd_update_get_alpha(s, w);
        if ((i != fimd_get_buffer_id(&s->window[w])) ||
                (!(old_value & FIMD_WINCON_ENWIN) && (s->window[w].wincon &
                        FIMD_WINCON_ENWIN))) {
            fimd_update_memory_section(s, w);
        }
        break;
    case FIMD_SHADOWCON:
        old_value = s->shadowcon;
        s->shadowcon = val;
        for (w = 0; w < NUM_OF_WINDOWS; w++) {
            if (FIMD_WINDOW_PROTECTED(old_value, w) &&
                    !FIMD_WINDOW_PROTECTED(s->shadowcon, w)) {
                fimd_update_memory_section(s, w);
            }
        }
        break;
    case FIMD_WINCHMAP:
        s->winchmap = val;
        break;
    case FIMD_VIDOSD_START ... FIMD_VIDOSD_END:
        w = (offset - FIMD_VIDOSD_START) >> 4;
        i = ((offset - FIMD_VIDOSD_START) & 0xF) >> 2;
        switch (i) {
        case 0:
            old_value = s->window[w].lefttop_y;
            s->window[w].lefttop_x = (val >> FIMD_VIDOSD_HOR_SHIFT) &
                                      FIMD_VIDOSD_COORD_MASK;
            s->window[w].lefttop_y = (val >> FIMD_VIDOSD_VER_SHIFT) &
                                      FIMD_VIDOSD_COORD_MASK;
            if (s->window[w].lefttop_y != old_value) {
                fimd_update_memory_section(s, w);
            }
            break;
        case 1:
            old_value = s->window[w].rightbot_y;
            s->window[w].rightbot_x = (val >> FIMD_VIDOSD_HOR_SHIFT) &
                                       FIMD_VIDOSD_COORD_MASK;
            s->window[w].rightbot_y = (val >> FIMD_VIDOSD_VER_SHIFT) &
                                       FIMD_VIDOSD_COORD_MASK;
            if (s->window[w].rightbot_y != old_value) {
                fimd_update_memory_section(s, w);
            }
            break;
        case 2:
            if (w == 0) {
                s->window[w].osdsize = val;
            } else {
                s->window[w].alpha_val[0] =
                    unpack_upper_4((val & FIMD_VIDOSD_ALPHA_AEN0) >>
                    FIMD_VIDOSD_AEN0_SHIFT) |
                    (s->window[w].alpha_val[0] & FIMD_VIDALPHA_ALPHA_LOWER);
                s->window[w].alpha_val[1] =
                    unpack_upper_4(val & FIMD_VIDOSD_ALPHA_AEN1) |
                    (s->window[w].alpha_val[1] & FIMD_VIDALPHA_ALPHA_LOWER);
            }
            break;
        case 3:
            if (w != 1 && w != 2) {
                DPRINT_ERROR("Bad write offset 0x%08x\n", offset);
                return;
            }
            s->window[w].osdsize = val;
            break;
        }
        break;
    case FIMD_VIDWADD0_START ... FIMD_VIDWADD0_END:
        w = (offset - FIMD_VIDWADD0_START) >> 3;
        i = ((offset - FIMD_VIDWADD0_START) >> 2) & 1;
        if (i == fimd_get_buffer_id(&s->window[w]) &&
                s->window[w].buf_start[i] != val) {
            s->window[w].buf_start[i] = val;
            fimd_update_memory_section(s, w);
            break;
        }
        s->window[w].buf_start[i] = val;
        break;
    case FIMD_VIDWADD1_START ... FIMD_VIDWADD1_END:
        w = (offset - FIMD_VIDWADD1_START) >> 3;
        i = ((offset - FIMD_VIDWADD1_START) >> 2) & 1;
        s->window[w].buf_end[i] = val;
        break;
    case FIMD_VIDWADD2_START ... FIMD_VIDWADD2_END:
        w = (offset - FIMD_VIDWADD2_START) >> 2;
        if (((val & FIMD_VIDWADD2_PAGEWIDTH) != s->window[w].virtpage_width) ||
            (((val >> FIMD_VIDWADD2_OFFSIZE_SHIFT) & FIMD_VIDWADD2_OFFSIZE) !=
                        s->window[w].virtpage_offsize)) {
            s->window[w].virtpage_width = val & FIMD_VIDWADD2_PAGEWIDTH;
            s->window[w].virtpage_offsize =
                (val >> FIMD_VIDWADD2_OFFSIZE_SHIFT) & FIMD_VIDWADD2_OFFSIZE;
            fimd_update_memory_section(s, w);
        }
        break;
    case FIMD_VIDINTCON0:
        s->vidintcon[0] = val;
        break;
    case FIMD_VIDINTCON1:
        s->vidintcon[1] &= ~(val & 7);
        exynos4210_fimd_update_irq(s);
        break;
    case FIMD_WKEYCON_START ... FIMD_WKEYCON_END:
        w = ((offset - FIMD_WKEYCON_START) >> 3) + 1;
        i = ((offset - FIMD_WKEYCON_START) >> 2) & 1;
        s->window[w].keycon[i] = val;
        break;
    case FIMD_WKEYALPHA_START ... FIMD_WKEYALPHA_END:
        w = ((offset - FIMD_WKEYALPHA_START) >> 2) + 1;
        s->window[w].keyalpha = val;
        break;
    case FIMD_DITHMODE:
        s->dithmode = val;
        break;
    case FIMD_WINMAP_START ... FIMD_WINMAP_END:
        w = (offset - FIMD_WINMAP_START) >> 2;
        old_value = s->window[w].winmap;
        s->window[w].winmap = val;
        if ((val & FIMD_WINMAP_EN) ^ (old_value & FIMD_WINMAP_EN)) {
            exynos4210_fimd_invalidate(s);
            exynos4210_fimd_update_win_bppmode(s, w);
            exynos4210_fimd_trace_bppmode(s, w, 0xFFFFFFFF);
            exynos4210_fimd_update(s);
        }
        break;
    case FIMD_WPALCON_HIGH ... FIMD_WPALCON_LOW:
        i = (offset - FIMD_WPALCON_HIGH) >> 2;
        s->wpalcon[i] = val;
        if (s->wpalcon[1] & FIMD_WPALCON_UPDATEEN) {
            for (w = 0; w < NUM_OF_WINDOWS; w++) {
                exynos4210_fimd_update_win_bppmode(s, w);
                fimd_update_get_alpha(s, w);
            }
        }
        break;
    case FIMD_TRIGCON:
        val = (val & ~FIMD_TRIGCON_ROMASK) | (s->trigcon & FIMD_TRIGCON_ROMASK);
        s->trigcon = val;
        break;
    case FIMD_I80IFCON_START ... FIMD_I80IFCON_END:
        s->i80ifcon[(offset - FIMD_I80IFCON_START) >> 2] = val;
        break;
    case FIMD_COLORGAINCON:
        s->colorgaincon = val;
        break;
    case FIMD_LDI_CMDCON0 ... FIMD_LDI_CMDCON1:
        s->ldi_cmdcon[(offset - FIMD_LDI_CMDCON0) >> 2] = val;
        break;
    case FIMD_SIFCCON0 ... FIMD_SIFCCON2:
        i = (offset - FIMD_SIFCCON0) >> 2;
        if (i != 2) {
            s->sifccon[i] = val;
        }
        break;
    case FIMD_HUECOEFCR_START ... FIMD_HUECOEFCR_END:
        i = (offset - FIMD_HUECOEFCR_START) >> 2;
        s->huecoef_cr[i] = val;
        break;
    case FIMD_HUECOEFCB_START ... FIMD_HUECOEFCB_END:
        i = (offset - FIMD_HUECOEFCB_START) >> 2;
        s->huecoef_cb[i] = val;
        break;
    case FIMD_HUEOFFSET:
        s->hueoffset = val;
        break;
    case FIMD_VIDWALPHA_START ... FIMD_VIDWALPHA_END:
        w = ((offset - FIMD_VIDWALPHA_START) >> 3);
        i = ((offset - FIMD_VIDWALPHA_START) >> 2) & 1;
        if (w == 0) {
            s->window[w].alpha_val[i] = val;
        } else {
            s->window[w].alpha_val[i] = (val & FIMD_VIDALPHA_ALPHA_LOWER) |
                (s->window[w].alpha_val[i] & FIMD_VIDALPHA_ALPHA_UPPER);
        }
        break;
    case FIMD_BLENDEQ_START ... FIMD_BLENDEQ_END:
        s->window[(offset - FIMD_BLENDEQ_START) >> 2].blendeq = val;
        break;
    case FIMD_BLENDCON:
        old_value = s->blendcon;
        s->blendcon = val;
        if ((s->blendcon & FIMD_ALPHA_8BIT) != (old_value & FIMD_ALPHA_8BIT)) {
            for (w = 0; w < NUM_OF_WINDOWS; w++) {
                fimd_update_get_alpha(s, w);
            }
        }
        break;
    case FIMD_WRTQOSCON_START ... FIMD_WRTQOSCON_END:
        s->window[(offset - FIMD_WRTQOSCON_START) >> 2].rtqoscon = val;
        break;
    case FIMD_I80IFCMD_START ... FIMD_I80IFCMD_END:
        s->i80ifcmd[(offset - FIMD_I80IFCMD_START) >> 2] = val;
        break;
    case FIMD_VIDW0ADD0_B2 ... FIMD_VIDW4ADD0_B2:
        if (offset & 0x0004) {
            DPRINT_ERROR("bad write offset 0x%08x\n", offset);
            break;
        }
        w = (offset - FIMD_VIDW0ADD0_B2) >> 3;
        if (fimd_get_buffer_id(&s->window[w]) == 2 &&
                s->window[w].buf_start[2] != val) {
            s->window[w].buf_start[2] = val;
            fimd_update_memory_section(s, w);
            break;
        }
        s->window[w].buf_start[2] = val;
        break;
    case FIMD_SHD_ADD0_START ... FIMD_SHD_ADD0_END:
        if (offset & 0x0004) {
            DPRINT_ERROR("bad write offset 0x%08x\n", offset);
            break;
        }
        s->window[(offset - FIMD_SHD_ADD0_START) >> 3].shadow_buf_start = val;
        break;
    case FIMD_SHD_ADD1_START ... FIMD_SHD_ADD1_END:
        if (offset & 0x0004) {
            DPRINT_ERROR("bad write offset 0x%08x\n", offset);
            break;
        }
        s->window[(offset - FIMD_SHD_ADD1_START) >> 3].shadow_buf_end = val;
        break;
    case FIMD_SHD_ADD2_START ... FIMD_SHD_ADD2_END:
        s->window[(offset - FIMD_SHD_ADD2_START) >> 2].shadow_buf_size = val;
        break;
    case FIMD_PAL_MEM_START ... FIMD_PAL_MEM_END:
        w = (offset - FIMD_PAL_MEM_START) >> 10;
        i = ((offset - FIMD_PAL_MEM_START) >> 2) & 0xFF;
        s->window[w].palette[i] = val;
        break;
    case FIMD_PALMEM_AL_START ... FIMD_PALMEM_AL_END:
        /* Palette memory aliases for windows 0 and 1 */
        w = (offset - FIMD_PALMEM_AL_START) >> 10;
        i = ((offset - FIMD_PALMEM_AL_START) >> 2) & 0xFF;
        s->window[w].palette[i] = val;
        break;
    default:
        DPRINT_ERROR("bad write offset 0x%08x\n", offset);
        break;
    }
}

static uint64_t exynos4210_fimd_read(void *opaque, hwaddr offset,
                                  unsigned size)
{
    Exynos4210fimdState *s = (Exynos4210fimdState *)opaque;
    int w, i;
    uint32_t ret = 0;

    DPRINT_L2("read offset 0x%08x\n", offset);

    switch (offset) {
    case FIMD_VIDCON0 ... FIMD_VIDCON3:
        return s->vidcon[(offset - FIMD_VIDCON0) >> 2];
    case FIMD_VIDTCON_START ... FIMD_VIDTCON_END:
        return s->vidtcon[(offset - FIMD_VIDTCON_START) >> 2];
    case FIMD_WINCON_START ... FIMD_WINCON_END:
        return s->window[(offset - FIMD_WINCON_START) >> 2].wincon;
    case FIMD_SHADOWCON:
        return s->shadowcon;
    case FIMD_WINCHMAP:
        return s->winchmap;
    case FIMD_VIDOSD_START ... FIMD_VIDOSD_END:
        w = (offset - FIMD_VIDOSD_START) >> 4;
        i = ((offset - FIMD_VIDOSD_START) & 0xF) >> 2;
        switch (i) {
        case 0:
            ret = ((s->window[w].lefttop_x & FIMD_VIDOSD_COORD_MASK) <<
            FIMD_VIDOSD_HOR_SHIFT) |
            (s->window[w].lefttop_y & FIMD_VIDOSD_COORD_MASK);
            break;
        case 1:
            ret = ((s->window[w].rightbot_x & FIMD_VIDOSD_COORD_MASK) <<
                FIMD_VIDOSD_HOR_SHIFT) |
                (s->window[w].rightbot_y & FIMD_VIDOSD_COORD_MASK);
            break;
        case 2:
            if (w == 0) {
                ret = s->window[w].osdsize;
            } else {
                ret = (pack_upper_4(s->window[w].alpha_val[0]) <<
                    FIMD_VIDOSD_AEN0_SHIFT) |
                    pack_upper_4(s->window[w].alpha_val[1]);
            }
            break;
        case 3:
            if (w != 1 && w != 2) {
                DPRINT_ERROR("bad read offset 0x%08x\n", offset);
                return 0xBAADBAAD;
            }
            ret = s->window[w].osdsize;
            break;
        }
        return ret;
    case FIMD_VIDWADD0_START ... FIMD_VIDWADD0_END:
        w = (offset - FIMD_VIDWADD0_START) >> 3;
        i = ((offset - FIMD_VIDWADD0_START) >> 2) & 1;
        return s->window[w].buf_start[i];
    case FIMD_VIDWADD1_START ... FIMD_VIDWADD1_END:
        w = (offset - FIMD_VIDWADD1_START) >> 3;
        i = ((offset - FIMD_VIDWADD1_START) >> 2) & 1;
        return s->window[w].buf_end[i];
    case FIMD_VIDWADD2_START ... FIMD_VIDWADD2_END:
        w = (offset - FIMD_VIDWADD2_START) >> 2;
        return s->window[w].virtpage_width | (s->window[w].virtpage_offsize <<
            FIMD_VIDWADD2_OFFSIZE_SHIFT);
    case FIMD_VIDINTCON0 ... FIMD_VIDINTCON1:
        return s->vidintcon[(offset - FIMD_VIDINTCON0) >> 2];
    case FIMD_WKEYCON_START ... FIMD_WKEYCON_END:
        w = ((offset - FIMD_WKEYCON_START) >> 3) + 1;
        i = ((offset - FIMD_WKEYCON_START) >> 2) & 1;
        return s->window[w].keycon[i];
    case FIMD_WKEYALPHA_START ... FIMD_WKEYALPHA_END:
        w = ((offset - FIMD_WKEYALPHA_START) >> 2) + 1;
        return s->window[w].keyalpha;
    case FIMD_DITHMODE:
        return s->dithmode;
    case FIMD_WINMAP_START ... FIMD_WINMAP_END:
        return s->window[(offset - FIMD_WINMAP_START) >> 2].winmap;
    case FIMD_WPALCON_HIGH ... FIMD_WPALCON_LOW:
        return s->wpalcon[(offset - FIMD_WPALCON_HIGH) >> 2];
    case FIMD_TRIGCON:
        return s->trigcon;
    case FIMD_I80IFCON_START ... FIMD_I80IFCON_END:
        return s->i80ifcon[(offset - FIMD_I80IFCON_START) >> 2];
    case FIMD_COLORGAINCON:
        return s->colorgaincon;
    case FIMD_LDI_CMDCON0 ... FIMD_LDI_CMDCON1:
        return s->ldi_cmdcon[(offset - FIMD_LDI_CMDCON0) >> 2];
    case FIMD_SIFCCON0 ... FIMD_SIFCCON2:
        i = (offset - FIMD_SIFCCON0) >> 2;
        return s->sifccon[i];
    case FIMD_HUECOEFCR_START ... FIMD_HUECOEFCR_END:
        i = (offset - FIMD_HUECOEFCR_START) >> 2;
        return s->huecoef_cr[i];
    case FIMD_HUECOEFCB_START ... FIMD_HUECOEFCB_END:
        i = (offset - FIMD_HUECOEFCB_START) >> 2;
        return s->huecoef_cb[i];
    case FIMD_HUEOFFSET:
        return s->hueoffset;
    case FIMD_VIDWALPHA_START ... FIMD_VIDWALPHA_END:
        w = ((offset - FIMD_VIDWALPHA_START) >> 3);
        i = ((offset - FIMD_VIDWALPHA_START) >> 2) & 1;
        return s->window[w].alpha_val[i] &
                (w == 0 ? 0xFFFFFF : FIMD_VIDALPHA_ALPHA_LOWER);
    case FIMD_BLENDEQ_START ... FIMD_BLENDEQ_END:
        return s->window[(offset - FIMD_BLENDEQ_START) >> 2].blendeq;
    case FIMD_BLENDCON:
        return s->blendcon;
    case FIMD_WRTQOSCON_START ... FIMD_WRTQOSCON_END:
        return s->window[(offset - FIMD_WRTQOSCON_START) >> 2].rtqoscon;
    case FIMD_I80IFCMD_START ... FIMD_I80IFCMD_END:
        return s->i80ifcmd[(offset - FIMD_I80IFCMD_START) >> 2];
    case FIMD_VIDW0ADD0_B2 ... FIMD_VIDW4ADD0_B2:
        if (offset & 0x0004) {
            break;
        }
        return s->window[(offset - FIMD_VIDW0ADD0_B2) >> 3].buf_start[2];
    case FIMD_SHD_ADD0_START ... FIMD_SHD_ADD0_END:
        if (offset & 0x0004) {
            break;
        }
        return s->window[(offset - FIMD_SHD_ADD0_START) >> 3].shadow_buf_start;
    case FIMD_SHD_ADD1_START ... FIMD_SHD_ADD1_END:
        if (offset & 0x0004) {
            break;
        }
        return s->window[(offset - FIMD_SHD_ADD1_START) >> 3].shadow_buf_end;
    case FIMD_SHD_ADD2_START ... FIMD_SHD_ADD2_END:
        return s->window[(offset - FIMD_SHD_ADD2_START) >> 2].shadow_buf_size;
    case FIMD_PAL_MEM_START ... FIMD_PAL_MEM_END:
        w = (offset - FIMD_PAL_MEM_START) >> 10;
        i = ((offset - FIMD_PAL_MEM_START) >> 2) & 0xFF;
        return s->window[w].palette[i];
    case FIMD_PALMEM_AL_START ... FIMD_PALMEM_AL_END:
        /* Palette aliases for win 0,1 */
        w = (offset - FIMD_PALMEM_AL_START) >> 10;
        i = ((offset - FIMD_PALMEM_AL_START) >> 2) & 0xFF;
        return s->window[w].palette[i];
    }

    DPRINT_ERROR("bad read offset 0x%08x\n", offset);
    return 0xBAADBAAD;
}

static const MemoryRegionOps exynos4210_fimd_mmio_ops = {
    .read = exynos4210_fimd_read,
    .write = exynos4210_fimd_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int exynos4210_fimd_load(void *opaque, int version_id)
{
    Exynos4210fimdState *s = (Exynos4210fimdState *)opaque;
    int w;

    if (version_id != 1) {
        return -EINVAL;
    }

    for (w = 0; w < NUM_OF_WINDOWS; w++) {
        exynos4210_fimd_update_win_bppmode(s, w);
        fimd_update_get_alpha(s, w);
        fimd_update_memory_section(s, w);
    }

    /* Redraw the whole screen */
    exynos4210_update_resolution(s);
    exynos4210_fimd_invalidate(s);
    exynos4210_fimd_enable(s, (s->vidcon[0] & FIMD_VIDCON0_ENVID_MASK) ==
            FIMD_VIDCON0_ENVID_MASK);
    return 0;
}

static const VMStateDescription exynos4210_fimd_window_vmstate = {
    .name = "exynos4210.fimd_window",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(wincon, Exynos4210fimdWindow),
        VMSTATE_UINT32_ARRAY(buf_start, Exynos4210fimdWindow, 3),
        VMSTATE_UINT32_ARRAY(buf_end, Exynos4210fimdWindow, 3),
        VMSTATE_UINT32_ARRAY(keycon, Exynos4210fimdWindow, 2),
        VMSTATE_UINT32(keyalpha, Exynos4210fimdWindow),
        VMSTATE_UINT32(winmap, Exynos4210fimdWindow),
        VMSTATE_UINT32(blendeq, Exynos4210fimdWindow),
        VMSTATE_UINT32(rtqoscon, Exynos4210fimdWindow),
        VMSTATE_UINT32_ARRAY(palette, Exynos4210fimdWindow, 256),
        VMSTATE_UINT32(shadow_buf_start, Exynos4210fimdWindow),
        VMSTATE_UINT32(shadow_buf_end, Exynos4210fimdWindow),
        VMSTATE_UINT32(shadow_buf_size, Exynos4210fimdWindow),
        VMSTATE_UINT16(lefttop_x, Exynos4210fimdWindow),
        VMSTATE_UINT16(lefttop_y, Exynos4210fimdWindow),
        VMSTATE_UINT16(rightbot_x, Exynos4210fimdWindow),
        VMSTATE_UINT16(rightbot_y, Exynos4210fimdWindow),
        VMSTATE_UINT32(osdsize, Exynos4210fimdWindow),
        VMSTATE_UINT32_ARRAY(alpha_val, Exynos4210fimdWindow, 2),
        VMSTATE_UINT16(virtpage_width, Exynos4210fimdWindow),
        VMSTATE_UINT16(virtpage_offsize, Exynos4210fimdWindow),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription exynos4210_fimd_vmstate = {
    .name = "exynos4210.fimd",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = exynos4210_fimd_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(vidcon, Exynos4210fimdState, 4),
        VMSTATE_UINT32_ARRAY(vidtcon, Exynos4210fimdState, 4),
        VMSTATE_UINT32(shadowcon, Exynos4210fimdState),
        VMSTATE_UINT32(winchmap, Exynos4210fimdState),
        VMSTATE_UINT32_ARRAY(vidintcon, Exynos4210fimdState, 2),
        VMSTATE_UINT32(dithmode, Exynos4210fimdState),
        VMSTATE_UINT32_ARRAY(wpalcon, Exynos4210fimdState, 2),
        VMSTATE_UINT32(trigcon, Exynos4210fimdState),
        VMSTATE_UINT32_ARRAY(i80ifcon, Exynos4210fimdState, 4),
        VMSTATE_UINT32(colorgaincon, Exynos4210fimdState),
        VMSTATE_UINT32_ARRAY(ldi_cmdcon, Exynos4210fimdState, 2),
        VMSTATE_UINT32_ARRAY(sifccon, Exynos4210fimdState, 3),
        VMSTATE_UINT32_ARRAY(huecoef_cr, Exynos4210fimdState, 4),
        VMSTATE_UINT32_ARRAY(huecoef_cb, Exynos4210fimdState, 4),
        VMSTATE_UINT32(hueoffset, Exynos4210fimdState),
        VMSTATE_UINT32_ARRAY(i80ifcmd, Exynos4210fimdState, 12),
        VMSTATE_UINT32(blendcon, Exynos4210fimdState),
        VMSTATE_STRUCT_ARRAY(window, Exynos4210fimdState, 5, 1,
                exynos4210_fimd_window_vmstate, Exynos4210fimdWindow),
        VMSTATE_END_OF_LIST()
    }
};

static const GraphicHwOps exynos4210_fimd_ops = {
    .invalidate  = exynos4210_fimd_invalidate,
    .gfx_update  = exynos4210_fimd_update,
};

static void exynos4210_fimd_init(Object *obj)
{
    Exynos4210fimdState *s = EXYNOS4210_FIMD(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    s->ifb = NULL;

    sysbus_init_irq(dev, &s->irq[0]);
    sysbus_init_irq(dev, &s->irq[1]);
    sysbus_init_irq(dev, &s->irq[2]);

    memory_region_init_io(&s->iomem, obj, &exynos4210_fimd_mmio_ops, s,
            "exynos4210.fimd", FIMD_REGS_SIZE);
    sysbus_init_mmio(dev, &s->iomem);
}

static void exynos4210_fimd_realize(DeviceState *dev, Error **errp)
{
    Exynos4210fimdState *s = EXYNOS4210_FIMD(dev);

    s->console = graphic_console_init(dev, 0, &exynos4210_fimd_ops, s);
}

static void exynos4210_fimd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &exynos4210_fimd_vmstate;
    dc->reset = exynos4210_fimd_reset;
    dc->realize = exynos4210_fimd_realize;
}

static const TypeInfo exynos4210_fimd_info = {
    .name = TYPE_EXYNOS4210_FIMD,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Exynos4210fimdState),
    .instance_init = exynos4210_fimd_init,
    .class_init = exynos4210_fimd_class_init,
};

static void exynos4210_fimd_register_types(void)
{
    type_register_static(&exynos4210_fimd_info);
}

type_init(exynos4210_fimd_register_types)
