/*
 * QEMU EDID generator.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/display/edid.h"

static const struct edid_mode {
    uint32_t xres;
    uint32_t yres;
    uint32_t byte;
    uint32_t xtra3;
    uint32_t bit;
    uint32_t dta;
} modes[] = {
    /* dea/dta extension timings (all @ 50 Hz) */
    { .xres = 5120,   .yres = 2160,   .dta = 125 },
    { .xres = 4096,   .yres = 2160,   .dta = 101 },
    { .xres = 3840,   .yres = 2160,   .dta =  96 },
    { .xres = 2560,   .yres = 1080,   .dta =  89 },
    { .xres = 2048,   .yres = 1152 },
    { .xres = 1920,   .yres = 1080,   .dta =  31 },

    /* additional standard timings 3 (all @ 60Hz) */
    { .xres = 1920,   .yres = 1200,   .xtra3 = 10,   .bit = 0 },
    { .xres = 1600,   .yres = 1200,   .xtra3 =  9,   .bit = 2 },
    { .xres = 1680,   .yres = 1050,   .xtra3 =  9,   .bit = 5 },
    { .xres = 1440,   .yres =  900,   .xtra3 =  8,   .bit = 5 },
    { .xres = 1280,   .yres = 1024,   .xtra3 =  7,   .bit = 1 },
    { .xres = 1280,   .yres =  960,   .xtra3 =  7,   .bit = 3 },
    { .xres = 1280,   .yres =  768,   .xtra3 =  7,   .bit = 6 },

    { .xres = 1920,   .yres = 1440,   .xtra3 = 11,   .bit = 5 },
    { .xres = 1856,   .yres = 1392,   .xtra3 = 10,   .bit = 3 },
    { .xres = 1792,   .yres = 1344,   .xtra3 = 10,   .bit = 5 },
    { .xres = 1440,   .yres = 1050,   .xtra3 =  8,   .bit = 1 },
    { .xres = 1360,   .yres =  768,   .xtra3 =  8,   .bit = 7 },

    /* established timings (all @ 60Hz) */
    { .xres = 1024,   .yres =  768,   .byte  = 36,   .bit = 3 },
    { .xres =  800,   .yres =  600,   .byte  = 35,   .bit = 0 },
    { .xres =  640,   .yres =  480,   .byte  = 35,   .bit = 5 },
};

typedef struct Timings {
    uint32_t xfront;
    uint32_t xsync;
    uint32_t xblank;

    uint32_t yfront;
    uint32_t ysync;
    uint32_t yblank;

    uint64_t clock;
} Timings;

static void generate_timings(Timings *timings, uint32_t refresh_rate,
                             uint32_t xres, uint32_t yres)
{
    /* pull some realistic looking timings out of thin air */
    timings->xfront = xres * 25 / 100;
    timings->xsync  = xres *  3 / 100;
    timings->xblank = xres * 35 / 100;

    timings->yfront = yres *  5 / 1000;
    timings->ysync  = yres *  5 / 1000;
    timings->yblank = yres * 35 / 1000;

    timings->clock  = ((uint64_t)refresh_rate *
                       (xres + timings->xblank) *
                       (yres + timings->yblank)) / 10000000;
}

static void edid_ext_dta(uint8_t *dta)
{
    dta[0] = 0x02;
    dta[1] = 0x03;
    dta[2] = 0x05;
    dta[3] = 0x00;

    /* video data block */
    dta[4] = 0x40;
}

static void edid_ext_dta_mode(uint8_t *dta, uint8_t nr)
{
    dta[dta[2]] = nr;
    dta[2]++;
    dta[4]++;
}

static int edid_std_mode(uint8_t *mode, uint32_t xres, uint32_t yres)
{
    uint32_t aspect;

    if (xres == 0 || yres == 0) {
        mode[0] = 0x01;
        mode[1] = 0x01;
        return 0;

    } else if (xres * 10 == yres * 16) {
        aspect = 0;
    } else if (xres * 3 == yres * 4) {
        aspect = 1;
    } else if (xres * 4 == yres * 5) {
        aspect = 2;
    } else if (xres * 9 == yres * 16) {
        aspect = 3;
    } else {
        return -1;
    }

    if ((xres / 8) - 31 > 255) {
        return -1;
    }

    mode[0] = (xres / 8) - 31;
    mode[1] = ((aspect << 6) | (60 - 60));
    return 0;
}

static void edid_fill_modes(uint8_t *edid, uint8_t *xtra3, uint8_t *dta,
                            uint32_t maxx, uint32_t maxy)
{
    const struct edid_mode *mode;
    int std = 38;
    int rc, i;

    for (i = 0; i < ARRAY_SIZE(modes); i++) {
        mode = modes + i;

        if ((maxx && mode->xres > maxx) ||
            (maxy && mode->yres > maxy)) {
            continue;
        }

        if (mode->byte) {
            edid[mode->byte] |= (1 << mode->bit);
        } else if (std < 54) {
            rc = edid_std_mode(edid + std, mode->xres, mode->yres);
            if (rc == 0) {
                std += 2;
            }
        } else if (mode->xtra3 && xtra3) {
            xtra3[mode->xtra3] |= (1 << mode->bit);
        }

        if (dta && mode->dta) {
            edid_ext_dta_mode(dta, mode->dta);
        }
    }

    while (std < 54) {
        edid_std_mode(edid + std, 0, 0);
        std += 2;
    }
}

static void edid_checksum(uint8_t *edid, size_t len)
{
    uint32_t sum = 0;
    int i;

    for (i = 0; i < len; i++) {
        sum += edid[i];
    }
    sum &= 0xff;
    if (sum) {
        edid[len] = 0x100 - sum;
    }
}

static uint8_t *edid_desc_next(uint8_t *edid, uint8_t *dta, uint8_t *desc)
{
    if (desc == NULL) {
        return NULL;
    }
    if (desc + 18 + 18 < edid + 127) {
        return desc + 18;
    }
    if (dta) {
        if (desc < edid + 127) {
            return dta + dta[2];
        }
        if (desc + 18 + 18 < dta + 127) {
            return desc + 18;
        }
    }
    return NULL;
}

static void edid_desc_type(uint8_t *desc, uint8_t type)
{
    desc[0] = 0;
    desc[1] = 0;
    desc[2] = 0;
    desc[3] = type;
    desc[4] = 0;
}

static void edid_desc_text(uint8_t *desc, uint8_t type,
                           const char *text)
{
    size_t len;

    edid_desc_type(desc, type);
    memset(desc + 5, ' ', 13);

    len = strlen(text);
    if (len > 12) {
        len = 12;
    }
    memcpy(desc + 5, text, len);
    desc[5 + len] = '\n';
}

static void edid_desc_ranges(uint8_t *desc)
{
    edid_desc_type(desc, 0xfd);

    /* vertical (50 -> 125 Hz) */
    desc[5] =  50;
    desc[6] = 125;

    /* horizontal (30 -> 160 kHz) */
    desc[7] =  30;
    desc[8] = 160;

    /* max dot clock (2550 MHz) */
    desc[9] = 2550 / 10;

    /* no extended timing information */
    desc[10] = 0x01;

    /* padding */
    desc[11] = '\n';
    memset(desc + 12, ' ', 6);
}

/* additional standard timings 3 */
static void edid_desc_xtra3_std(uint8_t *desc)
{
    edid_desc_type(desc, 0xf7);
    desc[5] = 10;
}

static void edid_desc_dummy(uint8_t *desc)
{
    edid_desc_type(desc, 0x10);
}

static void edid_desc_timing(uint8_t *desc, uint32_t refresh_rate,
                             uint32_t xres, uint32_t yres,
                             uint32_t xmm, uint32_t ymm)
{
    Timings timings;
    generate_timings(&timings, refresh_rate, xres, yres);
    stl_le_p(desc, timings.clock);

    desc[2] = xres   & 0xff;
    desc[3] = timings.xblank & 0xff;
    desc[4] = (((xres   & 0xf00) >> 4) |
               ((timings.xblank & 0xf00) >> 8));

    desc[5] = yres   & 0xff;
    desc[6] = timings.yblank & 0xff;
    desc[7] = (((yres   & 0xf00) >> 4) |
               ((timings.yblank & 0xf00) >> 8));

    desc[8] = timings.xfront & 0xff;
    desc[9] = timings.xsync  & 0xff;

    desc[10] = (((timings.yfront & 0x00f) << 4) |
                ((timings.ysync  & 0x00f) << 0));
    desc[11] = (((timings.xfront & 0x300) >> 2) |
                ((timings.xsync  & 0x300) >> 4) |
                ((timings.yfront & 0x030) >> 2) |
                ((timings.ysync  & 0x030) >> 4));

    desc[12] = xmm & 0xff;
    desc[13] = ymm & 0xff;
    desc[14] = (((xmm & 0xf00) >> 4) |
                ((ymm & 0xf00) >> 8));

    desc[17] = 0x18;
}

static uint32_t edid_to_10bit(float value)
{
    return (uint32_t)(value * 1024 + 0.5);
}

static void edid_colorspace(uint8_t *edid,
                            float rx, float ry,
                            float gx, float gy,
                            float bx, float by,
                            float wx, float wy)
{
    uint32_t red_x   = edid_to_10bit(rx);
    uint32_t red_y   = edid_to_10bit(ry);
    uint32_t green_x = edid_to_10bit(gx);
    uint32_t green_y = edid_to_10bit(gy);
    uint32_t blue_x  = edid_to_10bit(bx);
    uint32_t blue_y  = edid_to_10bit(by);
    uint32_t white_x = edid_to_10bit(wx);
    uint32_t white_y = edid_to_10bit(wy);

    edid[25] = (((red_x   & 0x03) << 6) |
                ((red_y   & 0x03) << 4) |
                ((green_x & 0x03) << 2) |
                ((green_y & 0x03) << 0));
    edid[26] = (((blue_x  & 0x03) << 6) |
                ((blue_y  & 0x03) << 4) |
                ((white_x & 0x03) << 2) |
                ((white_y & 0x03) << 0));
    edid[27] = red_x   >> 2;
    edid[28] = red_y   >> 2;
    edid[29] = green_x >> 2;
    edid[30] = green_y >> 2;
    edid[31] = blue_x  >> 2;
    edid[32] = blue_y  >> 2;
    edid[33] = white_x >> 2;
    edid[34] = white_y >> 2;
}

static uint32_t qemu_edid_dpi_from_mm(uint32_t mm, uint32_t res)
{
    return res * 254 / 10 / mm;
}

uint32_t qemu_edid_dpi_to_mm(uint32_t dpi, uint32_t res)
{
    return res * 254 / 10 / dpi;
}

static void init_displayid(uint8_t *did)
{
    did[0] = 0x70; /* display id extension */
    did[1] = 0x13; /* version 1.3 */
    did[2] = 4;    /* length */
    did[3] = 0x03; /* product type (0x03 == standalone display device) */
    edid_checksum(did + 1, did[2] + 4);
}

static void qemu_displayid_generate(uint8_t *did, uint32_t refresh_rate,
                                    uint32_t xres, uint32_t yres,
                                    uint32_t xmm, uint32_t ymm)
{
    Timings timings;
    generate_timings(&timings, refresh_rate, xres, yres);

    did[0] = 0x70; /* display id extension */
    did[1] = 0x13; /* version 1.3 */
    did[2] = 23;   /* length */
    did[3] = 0x03; /* product type (0x03 == standalone display device) */

    did[5] = 0x03; /* Detailed Timings Data Block */
    did[6] = 0x00; /* revision */
    did[7] = 0x14; /* block length */

    did[8]  = timings.clock  & 0xff;
    did[9]  = (timings.clock & 0xff00) >> 8;
    did[10] = (timings.clock & 0xff0000) >> 16;

    did[11] = 0x88; /* leave aspect ratio undefined */

    stw_le_p(did + 12, 0xffff & (xres - 1));
    stw_le_p(did + 14, 0xffff & (timings.xblank - 1));
    stw_le_p(did + 16, 0xffff & (timings.xfront - 1));
    stw_le_p(did + 18, 0xffff & (timings.xsync - 1));

    stw_le_p(did + 20, 0xffff & (yres - 1));
    stw_le_p(did + 22, 0xffff & (timings.yblank - 1));
    stw_le_p(did + 24, 0xffff & (timings.yfront - 1));
    stw_le_p(did + 26, 0xffff & (timings.ysync - 1));

    edid_checksum(did + 1, did[2] + 4);
}

void qemu_edid_generate(uint8_t *edid, size_t size,
                        qemu_edid_info *info)
{
    uint8_t *desc = edid + 54;
    uint8_t *xtra3 = NULL;
    uint8_t *dta = NULL;
    uint8_t *did = NULL;
    uint32_t width_mm, height_mm;
    uint32_t refresh_rate = info->refresh_rate ? info->refresh_rate : 75000;
    uint32_t dpi = 100; /* if no width_mm/height_mm */
    uint32_t large_screen = 0;

    /* =============== set defaults  =============== */

    if (!info->vendor || strlen(info->vendor) != 3) {
        info->vendor = "RHT";
    }
    if (!info->name) {
        info->name = "QEMU Monitor";
    }
    if (!info->prefx) {
        info->prefx = 1024;
    }
    if (!info->prefy) {
        info->prefy = 768;
    }
    if (info->prefx >= 4096 || info->prefy >= 4096) {
        large_screen = 1;
    }
    if (info->width_mm && info->height_mm) {
        width_mm = info->width_mm;
        height_mm = info->height_mm;
        dpi = qemu_edid_dpi_from_mm(width_mm, info->prefx);
    } else {
        width_mm = qemu_edid_dpi_to_mm(dpi, info->prefx);
        height_mm = qemu_edid_dpi_to_mm(dpi, info->prefy);
    }

    /* =============== extensions  =============== */

    if (size >= 256) {
        dta = edid + 128;
        edid[126]++;
        edid_ext_dta(dta);
    }

    if (size >= 384 && large_screen) {
        did = edid + 256;
        edid[126]++;
        init_displayid(did);
    }

    /* =============== header information =============== */

    /* fixed */
    edid[0] = 0x00;
    edid[1] = 0xff;
    edid[2] = 0xff;
    edid[3] = 0xff;
    edid[4] = 0xff;
    edid[5] = 0xff;
    edid[6] = 0xff;
    edid[7] = 0x00;

    /* manufacturer id, product code, serial number */
    uint16_t vendor_id = ((((info->vendor[0] - '@') & 0x1f) << 10) |
                          (((info->vendor[1] - '@') & 0x1f) <<  5) |
                          (((info->vendor[2] - '@') & 0x1f) <<  0));
    uint16_t model_nr = 0x1234;
    uint32_t serial_nr = info->serial ? atoi(info->serial) : 0;
    stw_be_p(edid +  8, vendor_id);
    stw_le_p(edid + 10, model_nr);
    stl_le_p(edid + 12, serial_nr);

    /* manufacture week and year */
    edid[16] = 42;
    edid[17] = 2014 - 1990;

    /* edid version */
    edid[18] = 1;
    edid[19] = 4;


    /* =============== basic display parameters =============== */

    /* video input: digital, 8bpc, displayport */
    edid[20] = 0xa5;

    /* screen size: undefined */
    edid[21] = width_mm / 10;
    edid[22] = height_mm / 10;

    /* display gamma: 2.2 */
    edid[23] = 220 - 100;

    /* supported features bitmap: std sRGB, preferred timing */
    edid[24] = 0x06;


    /* =============== chromaticity coordinates =============== */

    /* standard sRGB colorspace */
    edid_colorspace(edid,
                    0.6400, 0.3300,   /* red   */
                    0.3000, 0.6000,   /* green */
                    0.1500, 0.0600,   /* blue  */
                    0.3127, 0.3290);  /* white point  */

    /* =============== established timing bitmap =============== */
    /* =============== standard timing information =============== */

    /* both filled by edid_fill_modes() */


    /* =============== descriptor blocks =============== */

    if (!large_screen) {
        /* The DTD section has only 12 bits to store the resolution */
        edid_desc_timing(desc, refresh_rate, info->prefx, info->prefy,
                         width_mm, height_mm);
        desc = edid_desc_next(edid, dta, desc);
    }

    xtra3 = desc;
    edid_desc_xtra3_std(xtra3);
    desc = edid_desc_next(edid, dta, desc);
    edid_fill_modes(edid, xtra3, dta, info->maxx, info->maxy);
    /*
     * dta video data block is finished at thus point,
     * so dta descriptor offsets don't move any more.
     */

    edid_desc_ranges(desc);
    desc = edid_desc_next(edid, dta, desc);

    if (desc && info->name) {
        edid_desc_text(desc, 0xfc, info->name);
        desc = edid_desc_next(edid, dta, desc);
    }

    if (desc && info->serial) {
        edid_desc_text(desc, 0xff, info->serial);
        desc = edid_desc_next(edid, dta, desc);
    }

    while (desc) {
        edid_desc_dummy(desc);
        desc = edid_desc_next(edid, dta, desc);
    }

    /* =============== display id extensions =============== */

    if (did && large_screen) {
        qemu_displayid_generate(did, refresh_rate, info->prefx, info->prefy,
                                width_mm, height_mm);
    }

    /* =============== finish up =============== */

    edid_checksum(edid, 127);
    if (dta) {
        edid_checksum(dta, 127);
    }
    if (did) {
        edid_checksum(did, 127);
    }
}

size_t qemu_edid_size(uint8_t *edid)
{
    uint32_t exts;

    if (edid[0] != 0x00 ||
        edid[1] != 0xff) {
        /* doesn't look like a valid edid block */
        return 0;
    }

    exts = edid[126];
    return 128 * (exts + 1);
}
