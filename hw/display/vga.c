/*
 * QEMU VGA Emulator.
 *
 * Copyright (c) 2003 Fabrice Bellard
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
#include "hw/hw.h"
#include "vga.h"
#include "ui/console.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "vga_int.h"
#include "ui/pixel_ops.h"
#include "qemu/timer.h"
#include "hw/xen/xen.h"
#include "trace.h"

//#define DEBUG_VGA
//#define DEBUG_VGA_MEM
//#define DEBUG_VGA_REG

//#define DEBUG_BOCHS_VBE

/* 16 state changes per vertical frame @60 Hz */
#define VGA_TEXT_CURSOR_PERIOD_MS       (1000 * 2 * 16 / 60)

/*
 * Video Graphics Array (VGA)
 *
 * Chipset docs for original IBM VGA:
 * http://www.mcamafia.de/pdf/ibm_vgaxga_trm2.pdf
 *
 * FreeVGA site:
 * http://www.osdever.net/FreeVGA/home.htm
 *
 * Standard VGA features and Bochs VBE extensions are implemented.
 */

/* force some bits to zero */
const uint8_t sr_mask[8] = {
    0x03,
    0x3d,
    0x0f,
    0x3f,
    0x0e,
    0x00,
    0x00,
    0xff,
};

const uint8_t gr_mask[16] = {
    0x0f, /* 0x00 */
    0x0f, /* 0x01 */
    0x0f, /* 0x02 */
    0x1f, /* 0x03 */
    0x03, /* 0x04 */
    0x7b, /* 0x05 */
    0x0f, /* 0x06 */
    0x0f, /* 0x07 */
    0xff, /* 0x08 */
    0x00, /* 0x09 */
    0x00, /* 0x0a */
    0x00, /* 0x0b */
    0x00, /* 0x0c */
    0x00, /* 0x0d */
    0x00, /* 0x0e */
    0x00, /* 0x0f */
};

#define cbswap_32(__x) \
((uint32_t)( \
		(((uint32_t)(__x) & (uint32_t)0x000000ffUL) << 24) | \
		(((uint32_t)(__x) & (uint32_t)0x0000ff00UL) <<  8) | \
		(((uint32_t)(__x) & (uint32_t)0x00ff0000UL) >>  8) | \
		(((uint32_t)(__x) & (uint32_t)0xff000000UL) >> 24) ))

#ifdef HOST_WORDS_BIGENDIAN
#define PAT(x) cbswap_32(x)
#else
#define PAT(x) (x)
#endif

#ifdef HOST_WORDS_BIGENDIAN
#define BIG 1
#else
#define BIG 0
#endif

#ifdef HOST_WORDS_BIGENDIAN
#define GET_PLANE(data, p) (((data) >> (24 - (p) * 8)) & 0xff)
#else
#define GET_PLANE(data, p) (((data) >> ((p) * 8)) & 0xff)
#endif

static const uint32_t mask16[16] = {
    PAT(0x00000000),
    PAT(0x000000ff),
    PAT(0x0000ff00),
    PAT(0x0000ffff),
    PAT(0x00ff0000),
    PAT(0x00ff00ff),
    PAT(0x00ffff00),
    PAT(0x00ffffff),
    PAT(0xff000000),
    PAT(0xff0000ff),
    PAT(0xff00ff00),
    PAT(0xff00ffff),
    PAT(0xffff0000),
    PAT(0xffff00ff),
    PAT(0xffffff00),
    PAT(0xffffffff),
};

#undef PAT

#ifdef HOST_WORDS_BIGENDIAN
#define PAT(x) (x)
#else
#define PAT(x) cbswap_32(x)
#endif

static const uint32_t dmask16[16] = {
    PAT(0x00000000),
    PAT(0x000000ff),
    PAT(0x0000ff00),
    PAT(0x0000ffff),
    PAT(0x00ff0000),
    PAT(0x00ff00ff),
    PAT(0x00ffff00),
    PAT(0x00ffffff),
    PAT(0xff000000),
    PAT(0xff0000ff),
    PAT(0xff00ff00),
    PAT(0xff00ffff),
    PAT(0xffff0000),
    PAT(0xffff00ff),
    PAT(0xffffff00),
    PAT(0xffffffff),
};

static const uint32_t dmask4[4] = {
    PAT(0x00000000),
    PAT(0x0000ffff),
    PAT(0xffff0000),
    PAT(0xffffffff),
};

static uint32_t expand4[256];
static uint16_t expand2[256];
static uint8_t expand4to8[16];

static void vga_update_memory_access(VGACommonState *s)
{
    hwaddr base, offset, size;

    if (s->legacy_address_space == NULL) {
        return;
    }

    if (s->has_chain4_alias) {
        memory_region_del_subregion(s->legacy_address_space, &s->chain4_alias);
        object_unparent(OBJECT(&s->chain4_alias));
        s->has_chain4_alias = false;
        s->plane_updated = 0xf;
    }
    if ((s->sr[VGA_SEQ_PLANE_WRITE] & VGA_SR02_ALL_PLANES) ==
        VGA_SR02_ALL_PLANES && s->sr[VGA_SEQ_MEMORY_MODE] & VGA_SR04_CHN_4M) {
        offset = 0;
        switch ((s->gr[VGA_GFX_MISC] >> 2) & 3) {
        case 0:
            base = 0xa0000;
            size = 0x20000;
            break;
        case 1:
            base = 0xa0000;
            size = 0x10000;
            offset = s->bank_offset;
            break;
        case 2:
            base = 0xb0000;
            size = 0x8000;
            break;
        case 3:
        default:
            base = 0xb8000;
            size = 0x8000;
            break;
        }
        base += isa_mem_base;
        memory_region_init_alias(&s->chain4_alias, memory_region_owner(&s->vram),
                                 "vga.chain4", &s->vram, offset, size);
        memory_region_add_subregion_overlap(s->legacy_address_space, base,
                                            &s->chain4_alias, 2);
        s->has_chain4_alias = true;
    }
}

static void vga_dumb_update_retrace_info(VGACommonState *s)
{
    (void) s;
}

static void vga_precise_update_retrace_info(VGACommonState *s)
{
    int htotal_chars;
    int hretr_start_char;
    int hretr_skew_chars;
    int hretr_end_char;

    int vtotal_lines;
    int vretr_start_line;
    int vretr_end_line;

    int dots;
#if 0
    int div2, sldiv2;
#endif
    int clocking_mode;
    int clock_sel;
    const int clk_hz[] = {25175000, 28322000, 25175000, 25175000};
    int64_t chars_per_sec;
    struct vga_precise_retrace *r = &s->retrace_info.precise;

    htotal_chars = s->cr[VGA_CRTC_H_TOTAL] + 5;
    hretr_start_char = s->cr[VGA_CRTC_H_SYNC_START];
    hretr_skew_chars = (s->cr[VGA_CRTC_H_SYNC_END] >> 5) & 3;
    hretr_end_char = s->cr[VGA_CRTC_H_SYNC_END] & 0x1f;

    vtotal_lines = (s->cr[VGA_CRTC_V_TOTAL] |
                    (((s->cr[VGA_CRTC_OVERFLOW] & 1) |
                      ((s->cr[VGA_CRTC_OVERFLOW] >> 4) & 2)) << 8)) + 2;
    vretr_start_line = s->cr[VGA_CRTC_V_SYNC_START] |
        ((((s->cr[VGA_CRTC_OVERFLOW] >> 2) & 1) |
          ((s->cr[VGA_CRTC_OVERFLOW] >> 6) & 2)) << 8);
    vretr_end_line = s->cr[VGA_CRTC_V_SYNC_END] & 0xf;

    clocking_mode = (s->sr[VGA_SEQ_CLOCK_MODE] >> 3) & 1;
    clock_sel = (s->msr >> 2) & 3;
    dots = (s->msr & 1) ? 8 : 9;

    chars_per_sec = clk_hz[clock_sel] / dots;

    htotal_chars <<= clocking_mode;

    r->total_chars = vtotal_lines * htotal_chars;
    if (r->freq) {
        r->ticks_per_char = get_ticks_per_sec() / (r->total_chars * r->freq);
    } else {
        r->ticks_per_char = get_ticks_per_sec() / chars_per_sec;
    }

    r->vstart = vretr_start_line;
    r->vend = r->vstart + vretr_end_line + 1;

    r->hstart = hretr_start_char + hretr_skew_chars;
    r->hend = r->hstart + hretr_end_char + 1;
    r->htotal = htotal_chars;

#if 0
    div2 = (s->cr[VGA_CRTC_MODE] >> 2) & 1;
    sldiv2 = (s->cr[VGA_CRTC_MODE] >> 3) & 1;
    printf (
        "hz=%f\n"
        "htotal = %d\n"
        "hretr_start = %d\n"
        "hretr_skew = %d\n"
        "hretr_end = %d\n"
        "vtotal = %d\n"
        "vretr_start = %d\n"
        "vretr_end = %d\n"
        "div2 = %d sldiv2 = %d\n"
        "clocking_mode = %d\n"
        "clock_sel = %d %d\n"
        "dots = %d\n"
        "ticks/char = %" PRId64 "\n"
        "\n",
        (double) get_ticks_per_sec() / (r->ticks_per_char * r->total_chars),
        htotal_chars,
        hretr_start_char,
        hretr_skew_chars,
        hretr_end_char,
        vtotal_lines,
        vretr_start_line,
        vretr_end_line,
        div2, sldiv2,
        clocking_mode,
        clock_sel,
        clk_hz[clock_sel],
        dots,
        r->ticks_per_char
        );
#endif
}

static uint8_t vga_precise_retrace(VGACommonState *s)
{
    struct vga_precise_retrace *r = &s->retrace_info.precise;
    uint8_t val = s->st01 & ~(ST01_V_RETRACE | ST01_DISP_ENABLE);

    if (r->total_chars) {
        int cur_line, cur_line_char, cur_char;
        int64_t cur_tick;

        cur_tick = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

        cur_char = (cur_tick / r->ticks_per_char) % r->total_chars;
        cur_line = cur_char / r->htotal;

        if (cur_line >= r->vstart && cur_line <= r->vend) {
            val |= ST01_V_RETRACE | ST01_DISP_ENABLE;
        } else {
            cur_line_char = cur_char % r->htotal;
            if (cur_line_char >= r->hstart && cur_line_char <= r->hend) {
                val |= ST01_DISP_ENABLE;
            }
        }

        return val;
    } else {
        return s->st01 ^ (ST01_V_RETRACE | ST01_DISP_ENABLE);
    }
}

static uint8_t vga_dumb_retrace(VGACommonState *s)
{
    return s->st01 ^ (ST01_V_RETRACE | ST01_DISP_ENABLE);
}

int vga_ioport_invalid(VGACommonState *s, uint32_t addr)
{
    if (s->msr & VGA_MIS_COLOR) {
        /* Color */
        return (addr >= 0x3b0 && addr <= 0x3bf);
    } else {
        /* Monochrome */
        return (addr >= 0x3d0 && addr <= 0x3df);
    }
}

uint32_t vga_ioport_read(void *opaque, uint32_t addr)
{
    VGACommonState *s = opaque;
    int val, index;

    if (vga_ioport_invalid(s, addr)) {
        val = 0xff;
    } else {
        switch(addr) {
        case VGA_ATT_W:
            if (s->ar_flip_flop == 0) {
                val = s->ar_index;
            } else {
                val = 0;
            }
            break;
        case VGA_ATT_R:
            index = s->ar_index & 0x1f;
            if (index < VGA_ATT_C) {
                val = s->ar[index];
            } else {
                val = 0;
            }
            break;
        case VGA_MIS_W:
            val = s->st00;
            break;
        case VGA_SEQ_I:
            val = s->sr_index;
            break;
        case VGA_SEQ_D:
            val = s->sr[s->sr_index];
#ifdef DEBUG_VGA_REG
            printf("vga: read SR%x = 0x%02x\n", s->sr_index, val);
#endif
            break;
        case VGA_PEL_IR:
            val = s->dac_state;
            break;
        case VGA_PEL_IW:
            val = s->dac_write_index;
            break;
        case VGA_PEL_D:
            val = s->palette[s->dac_read_index * 3 + s->dac_sub_index];
            if (++s->dac_sub_index == 3) {
                s->dac_sub_index = 0;
                s->dac_read_index++;
            }
            break;
        case VGA_FTC_R:
            val = s->fcr;
            break;
        case VGA_MIS_R:
            val = s->msr;
            break;
        case VGA_GFX_I:
            val = s->gr_index;
            break;
        case VGA_GFX_D:
            val = s->gr[s->gr_index];
#ifdef DEBUG_VGA_REG
            printf("vga: read GR%x = 0x%02x\n", s->gr_index, val);
#endif
            break;
        case VGA_CRT_IM:
        case VGA_CRT_IC:
            val = s->cr_index;
            break;
        case VGA_CRT_DM:
        case VGA_CRT_DC:
            val = s->cr[s->cr_index];
#ifdef DEBUG_VGA_REG
            printf("vga: read CR%x = 0x%02x\n", s->cr_index, val);
#endif
            break;
        case VGA_IS1_RM:
        case VGA_IS1_RC:
            /* just toggle to fool polling */
            val = s->st01 = s->retrace(s);
            s->ar_flip_flop = 0;
            break;
        default:
            val = 0x00;
            break;
        }
    }
#if defined(DEBUG_VGA)
    printf("VGA: read addr=0x%04x data=0x%02x\n", addr, val);
#endif
    return val;
}

void vga_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    VGACommonState *s = opaque;
    int index;

    /* check port range access depending on color/monochrome mode */
    if (vga_ioport_invalid(s, addr)) {
        return;
    }
#ifdef DEBUG_VGA
    printf("VGA: write addr=0x%04x data=0x%02x\n", addr, val);
#endif

    switch(addr) {
    case VGA_ATT_W:
        if (s->ar_flip_flop == 0) {
            val &= 0x3f;
            s->ar_index = val;
        } else {
            index = s->ar_index & 0x1f;
            switch(index) {
            case VGA_ATC_PALETTE0 ... VGA_ATC_PALETTEF:
                s->ar[index] = val & 0x3f;
                break;
            case VGA_ATC_MODE:
                s->ar[index] = val & ~0x10;
                break;
            case VGA_ATC_OVERSCAN:
                s->ar[index] = val;
                break;
            case VGA_ATC_PLANE_ENABLE:
                s->ar[index] = val & ~0xc0;
                break;
            case VGA_ATC_PEL:
                s->ar[index] = val & ~0xf0;
                break;
            case VGA_ATC_COLOR_PAGE:
                s->ar[index] = val & ~0xf0;
                break;
            default:
                break;
            }
        }
        s->ar_flip_flop ^= 1;
        break;
    case VGA_MIS_W:
        s->msr = val & ~0x10;
        s->update_retrace_info(s);
        break;
    case VGA_SEQ_I:
        s->sr_index = val & 7;
        break;
    case VGA_SEQ_D:
#ifdef DEBUG_VGA_REG
        printf("vga: write SR%x = 0x%02x\n", s->sr_index, val);
#endif
        s->sr[s->sr_index] = val & sr_mask[s->sr_index];
        if (s->sr_index == VGA_SEQ_CLOCK_MODE) {
            s->update_retrace_info(s);
        }
        vga_update_memory_access(s);
        break;
    case VGA_PEL_IR:
        s->dac_read_index = val;
        s->dac_sub_index = 0;
        s->dac_state = 3;
        break;
    case VGA_PEL_IW:
        s->dac_write_index = val;
        s->dac_sub_index = 0;
        s->dac_state = 0;
        break;
    case VGA_PEL_D:
        s->dac_cache[s->dac_sub_index] = val;
        if (++s->dac_sub_index == 3) {
            memcpy(&s->palette[s->dac_write_index * 3], s->dac_cache, 3);
            s->dac_sub_index = 0;
            s->dac_write_index++;
        }
        break;
    case VGA_GFX_I:
        s->gr_index = val & 0x0f;
        break;
    case VGA_GFX_D:
#ifdef DEBUG_VGA_REG
        printf("vga: write GR%x = 0x%02x\n", s->gr_index, val);
#endif
        s->gr[s->gr_index] = val & gr_mask[s->gr_index];
        vga_update_memory_access(s);
        break;
    case VGA_CRT_IM:
    case VGA_CRT_IC:
        s->cr_index = val;
        break;
    case VGA_CRT_DM:
    case VGA_CRT_DC:
#ifdef DEBUG_VGA_REG
        printf("vga: write CR%x = 0x%02x\n", s->cr_index, val);
#endif
        /* handle CR0-7 protection */
        if ((s->cr[VGA_CRTC_V_SYNC_END] & VGA_CR11_LOCK_CR0_CR7) &&
            s->cr_index <= VGA_CRTC_OVERFLOW) {
            /* can always write bit 4 of CR7 */
            if (s->cr_index == VGA_CRTC_OVERFLOW) {
                s->cr[VGA_CRTC_OVERFLOW] = (s->cr[VGA_CRTC_OVERFLOW] & ~0x10) |
                    (val & 0x10);
            }
            return;
        }
        s->cr[s->cr_index] = val;

        switch(s->cr_index) {
        case VGA_CRTC_H_TOTAL:
        case VGA_CRTC_H_SYNC_START:
        case VGA_CRTC_H_SYNC_END:
        case VGA_CRTC_V_TOTAL:
        case VGA_CRTC_OVERFLOW:
        case VGA_CRTC_V_SYNC_END:
        case VGA_CRTC_MODE:
            s->update_retrace_info(s);
            break;
        }
        break;
    case VGA_IS1_RM:
    case VGA_IS1_RC:
        s->fcr = val & 0x10;
        break;
    }
}

static uint32_t vbe_ioport_read_index(void *opaque, uint32_t addr)
{
    VGACommonState *s = opaque;
    uint32_t val;
    val = s->vbe_index;
    return val;
}

uint32_t vbe_ioport_read_data(void *opaque, uint32_t addr)
{
    VGACommonState *s = opaque;
    uint32_t val;

    if (s->vbe_index < VBE_DISPI_INDEX_NB) {
        if (s->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_GETCAPS) {
            switch(s->vbe_index) {
                /* XXX: do not hardcode ? */
            case VBE_DISPI_INDEX_XRES:
                val = VBE_DISPI_MAX_XRES;
                break;
            case VBE_DISPI_INDEX_YRES:
                val = VBE_DISPI_MAX_YRES;
                break;
            case VBE_DISPI_INDEX_BPP:
                val = VBE_DISPI_MAX_BPP;
                break;
            default:
                val = s->vbe_regs[s->vbe_index];
                break;
            }
        } else {
            val = s->vbe_regs[s->vbe_index];
        }
    } else if (s->vbe_index == VBE_DISPI_INDEX_VIDEO_MEMORY_64K) {
        val = s->vram_size / (64 * 1024);
    } else {
        val = 0;
    }
#ifdef DEBUG_BOCHS_VBE
    printf("VBE: read index=0x%x val=0x%x\n", s->vbe_index, val);
#endif
    return val;
}

void vbe_ioport_write_index(void *opaque, uint32_t addr, uint32_t val)
{
    VGACommonState *s = opaque;
    s->vbe_index = val;
}

void vbe_ioport_write_data(void *opaque, uint32_t addr, uint32_t val)
{
    VGACommonState *s = opaque;

    if (s->vbe_index <= VBE_DISPI_INDEX_NB) {
#ifdef DEBUG_BOCHS_VBE
        printf("VBE: write index=0x%x val=0x%x\n", s->vbe_index, val);
#endif
        switch(s->vbe_index) {
        case VBE_DISPI_INDEX_ID:
            if (val == VBE_DISPI_ID0 ||
                val == VBE_DISPI_ID1 ||
                val == VBE_DISPI_ID2 ||
                val == VBE_DISPI_ID3 ||
                val == VBE_DISPI_ID4) {
                s->vbe_regs[s->vbe_index] = val;
            }
            break;
        case VBE_DISPI_INDEX_XRES:
            if ((val <= VBE_DISPI_MAX_XRES) && ((val & 7) == 0)) {
                s->vbe_regs[s->vbe_index] = val;
            }
            break;
        case VBE_DISPI_INDEX_YRES:
            if (val <= VBE_DISPI_MAX_YRES) {
                s->vbe_regs[s->vbe_index] = val;
            }
            break;
        case VBE_DISPI_INDEX_BPP:
            if (val == 0)
                val = 8;
            if (val == 4 || val == 8 || val == 15 ||
                val == 16 || val == 24 || val == 32) {
                s->vbe_regs[s->vbe_index] = val;
            }
            break;
        case VBE_DISPI_INDEX_BANK:
            if (s->vbe_regs[VBE_DISPI_INDEX_BPP] == 4) {
              val &= (s->vbe_bank_mask >> 2);
            } else {
              val &= s->vbe_bank_mask;
            }
            s->vbe_regs[s->vbe_index] = val;
            s->bank_offset = (val << 16);
            vga_update_memory_access(s);
            break;
        case VBE_DISPI_INDEX_ENABLE:
            if ((val & VBE_DISPI_ENABLED) &&
                !(s->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED)) {
                int h, shift_control;

                s->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] =
                    s->vbe_regs[VBE_DISPI_INDEX_XRES];
                s->vbe_regs[VBE_DISPI_INDEX_VIRT_HEIGHT] =
                    s->vbe_regs[VBE_DISPI_INDEX_YRES];
                s->vbe_regs[VBE_DISPI_INDEX_X_OFFSET] = 0;
                s->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET] = 0;

                if (s->vbe_regs[VBE_DISPI_INDEX_BPP] == 4)
                    s->vbe_line_offset = s->vbe_regs[VBE_DISPI_INDEX_XRES] >> 1;
                else
                    s->vbe_line_offset = s->vbe_regs[VBE_DISPI_INDEX_XRES] *
                        ((s->vbe_regs[VBE_DISPI_INDEX_BPP] + 7) >> 3);
                s->vbe_start_addr = 0;

                /* clear the screen (should be done in BIOS) */
                if (!(val & VBE_DISPI_NOCLEARMEM)) {
                    memset(s->vram_ptr, 0,
                           s->vbe_regs[VBE_DISPI_INDEX_YRES] * s->vbe_line_offset);
                }

                /* we initialize the VGA graphic mode (should be done
                   in BIOS) */
                /* graphic mode + memory map 1 */
                s->gr[VGA_GFX_MISC] = (s->gr[VGA_GFX_MISC] & ~0x0c) | 0x04 |
                    VGA_GR06_GRAPHICS_MODE;
                s->cr[VGA_CRTC_MODE] |= 3; /* no CGA modes */
                s->cr[VGA_CRTC_OFFSET] = s->vbe_line_offset >> 3;
                /* width */
                s->cr[VGA_CRTC_H_DISP] =
                    (s->vbe_regs[VBE_DISPI_INDEX_XRES] >> 3) - 1;
                /* height (only meaningful if < 1024) */
                h = s->vbe_regs[VBE_DISPI_INDEX_YRES] - 1;
                s->cr[VGA_CRTC_V_DISP_END] = h;
                s->cr[VGA_CRTC_OVERFLOW] = (s->cr[VGA_CRTC_OVERFLOW] & ~0x42) |
                    ((h >> 7) & 0x02) | ((h >> 3) & 0x40);
                /* line compare to 1023 */
                s->cr[VGA_CRTC_LINE_COMPARE] = 0xff;
                s->cr[VGA_CRTC_OVERFLOW] |= 0x10;
                s->cr[VGA_CRTC_MAX_SCAN] |= 0x40;

                if (s->vbe_regs[VBE_DISPI_INDEX_BPP] == 4) {
                    shift_control = 0;
                    s->sr[VGA_SEQ_CLOCK_MODE] &= ~8; /* no double line */
                } else {
                    shift_control = 2;
                    /* set chain 4 mode */
                    s->sr[VGA_SEQ_MEMORY_MODE] |= VGA_SR04_CHN_4M;
                    /* activate all planes */
                    s->sr[VGA_SEQ_PLANE_WRITE] |= VGA_SR02_ALL_PLANES;
                }
                s->gr[VGA_GFX_MODE] = (s->gr[VGA_GFX_MODE] & ~0x60) |
                    (shift_control << 5);
                s->cr[VGA_CRTC_MAX_SCAN] &= ~0x9f; /* no double scan */
            } else {
                /* XXX: the bios should do that */
                s->bank_offset = 0;
            }
            s->dac_8bit = (val & VBE_DISPI_8BIT_DAC) > 0;
            s->vbe_regs[s->vbe_index] = val;
            vga_update_memory_access(s);
            break;
        case VBE_DISPI_INDEX_VIRT_WIDTH:
            {
                int w, h, line_offset;

                if (val < s->vbe_regs[VBE_DISPI_INDEX_XRES])
                    return;
                w = val;
                if (s->vbe_regs[VBE_DISPI_INDEX_BPP] == 4)
                    line_offset = w >> 1;
                else
                    line_offset = w * ((s->vbe_regs[VBE_DISPI_INDEX_BPP] + 7) >> 3);
                h = s->vram_size / line_offset;
                /* XXX: support weird bochs semantics ? */
                if (h < s->vbe_regs[VBE_DISPI_INDEX_YRES])
                    return;
                s->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] = w;
                s->vbe_regs[VBE_DISPI_INDEX_VIRT_HEIGHT] = h;
                s->vbe_line_offset = line_offset;
            }
            break;
        case VBE_DISPI_INDEX_X_OFFSET:
        case VBE_DISPI_INDEX_Y_OFFSET:
            {
                int x;
                s->vbe_regs[s->vbe_index] = val;
                s->vbe_start_addr = s->vbe_line_offset * s->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET];
                x = s->vbe_regs[VBE_DISPI_INDEX_X_OFFSET];
                if (s->vbe_regs[VBE_DISPI_INDEX_BPP] == 4)
                    s->vbe_start_addr += x >> 1;
                else
                    s->vbe_start_addr += x * ((s->vbe_regs[VBE_DISPI_INDEX_BPP] + 7) >> 3);
                s->vbe_start_addr >>= 2;
            }
            break;
        default:
            break;
        }
    }
}

/* called for accesses between 0xa0000 and 0xc0000 */
uint32_t vga_mem_readb(VGACommonState *s, hwaddr addr)
{
    int memory_map_mode, plane;
    uint32_t ret;

    /* convert to VGA memory offset */
    memory_map_mode = (s->gr[VGA_GFX_MISC] >> 2) & 3;
    addr &= 0x1ffff;
    switch(memory_map_mode) {
    case 0:
        break;
    case 1:
        if (addr >= 0x10000)
            return 0xff;
        addr += s->bank_offset;
        break;
    case 2:
        addr -= 0x10000;
        if (addr >= 0x8000)
            return 0xff;
        break;
    default:
    case 3:
        addr -= 0x18000;
        if (addr >= 0x8000)
            return 0xff;
        break;
    }

    if (s->sr[VGA_SEQ_MEMORY_MODE] & VGA_SR04_CHN_4M) {
        /* chain 4 mode : simplest access */
        ret = s->vram_ptr[addr];
    } else if (s->gr[VGA_GFX_MODE] & 0x10) {
        /* odd/even mode (aka text mode mapping) */
        plane = (s->gr[VGA_GFX_PLANE_READ] & 2) | (addr & 1);
        ret = s->vram_ptr[((addr & ~1) << 1) | plane];
    } else {
        /* standard VGA latched access */
        s->latch = ((uint32_t *)s->vram_ptr)[addr];

        if (!(s->gr[VGA_GFX_MODE] & 0x08)) {
            /* read mode 0 */
            plane = s->gr[VGA_GFX_PLANE_READ];
            ret = GET_PLANE(s->latch, plane);
        } else {
            /* read mode 1 */
            ret = (s->latch ^ mask16[s->gr[VGA_GFX_COMPARE_VALUE]]) &
                mask16[s->gr[VGA_GFX_COMPARE_MASK]];
            ret |= ret >> 16;
            ret |= ret >> 8;
            ret = (~ret) & 0xff;
        }
    }
    return ret;
}

/* called for accesses between 0xa0000 and 0xc0000 */
void vga_mem_writeb(VGACommonState *s, hwaddr addr, uint32_t val)
{
    int memory_map_mode, plane, write_mode, b, func_select, mask;
    uint32_t write_mask, bit_mask, set_mask;

#ifdef DEBUG_VGA_MEM
    printf("vga: [0x" TARGET_FMT_plx "] = 0x%02x\n", addr, val);
#endif
    /* convert to VGA memory offset */
    memory_map_mode = (s->gr[VGA_GFX_MISC] >> 2) & 3;
    addr &= 0x1ffff;
    switch(memory_map_mode) {
    case 0:
        break;
    case 1:
        if (addr >= 0x10000)
            return;
        addr += s->bank_offset;
        break;
    case 2:
        addr -= 0x10000;
        if (addr >= 0x8000)
            return;
        break;
    default:
    case 3:
        addr -= 0x18000;
        if (addr >= 0x8000)
            return;
        break;
    }

    if (s->sr[VGA_SEQ_MEMORY_MODE] & VGA_SR04_CHN_4M) {
        /* chain 4 mode : simplest access */
        plane = addr & 3;
        mask = (1 << plane);
        if (s->sr[VGA_SEQ_PLANE_WRITE] & mask) {
            s->vram_ptr[addr] = val;
#ifdef DEBUG_VGA_MEM
            printf("vga: chain4: [0x" TARGET_FMT_plx "]\n", addr);
#endif
            s->plane_updated |= mask; /* only used to detect font change */
            memory_region_set_dirty(&s->vram, addr, 1);
        }
    } else if (s->gr[VGA_GFX_MODE] & 0x10) {
        /* odd/even mode (aka text mode mapping) */
        plane = (s->gr[VGA_GFX_PLANE_READ] & 2) | (addr & 1);
        mask = (1 << plane);
        if (s->sr[VGA_SEQ_PLANE_WRITE] & mask) {
            addr = ((addr & ~1) << 1) | plane;
            s->vram_ptr[addr] = val;
#ifdef DEBUG_VGA_MEM
            printf("vga: odd/even: [0x" TARGET_FMT_plx "]\n", addr);
#endif
            s->plane_updated |= mask; /* only used to detect font change */
            memory_region_set_dirty(&s->vram, addr, 1);
        }
    } else {
        /* standard VGA latched access */
        write_mode = s->gr[VGA_GFX_MODE] & 3;
        switch(write_mode) {
        default:
        case 0:
            /* rotate */
            b = s->gr[VGA_GFX_DATA_ROTATE] & 7;
            val = ((val >> b) | (val << (8 - b))) & 0xff;
            val |= val << 8;
            val |= val << 16;

            /* apply set/reset mask */
            set_mask = mask16[s->gr[VGA_GFX_SR_ENABLE]];
            val = (val & ~set_mask) |
                (mask16[s->gr[VGA_GFX_SR_VALUE]] & set_mask);
            bit_mask = s->gr[VGA_GFX_BIT_MASK];
            break;
        case 1:
            val = s->latch;
            goto do_write;
        case 2:
            val = mask16[val & 0x0f];
            bit_mask = s->gr[VGA_GFX_BIT_MASK];
            break;
        case 3:
            /* rotate */
            b = s->gr[VGA_GFX_DATA_ROTATE] & 7;
            val = (val >> b) | (val << (8 - b));

            bit_mask = s->gr[VGA_GFX_BIT_MASK] & val;
            val = mask16[s->gr[VGA_GFX_SR_VALUE]];
            break;
        }

        /* apply logical operation */
        func_select = s->gr[VGA_GFX_DATA_ROTATE] >> 3;
        switch(func_select) {
        case 0:
        default:
            /* nothing to do */
            break;
        case 1:
            /* and */
            val &= s->latch;
            break;
        case 2:
            /* or */
            val |= s->latch;
            break;
        case 3:
            /* xor */
            val ^= s->latch;
            break;
        }

        /* apply bit mask */
        bit_mask |= bit_mask << 8;
        bit_mask |= bit_mask << 16;
        val = (val & bit_mask) | (s->latch & ~bit_mask);

    do_write:
        /* mask data according to sr[2] */
        mask = s->sr[VGA_SEQ_PLANE_WRITE];
        s->plane_updated |= mask; /* only used to detect font change */
        write_mask = mask16[mask];
        ((uint32_t *)s->vram_ptr)[addr] =
            (((uint32_t *)s->vram_ptr)[addr] & ~write_mask) |
            (val & write_mask);
#ifdef DEBUG_VGA_MEM
        printf("vga: latch: [0x" TARGET_FMT_plx "] mask=0x%08x val=0x%08x\n",
               addr * 4, write_mask, val);
#endif
        memory_region_set_dirty(&s->vram, addr << 2, sizeof(uint32_t));
    }
}

typedef void vga_draw_glyph8_func(uint8_t *d, int linesize,
                             const uint8_t *font_ptr, int h,
                             uint32_t fgcol, uint32_t bgcol);
typedef void vga_draw_glyph9_func(uint8_t *d, int linesize,
                                  const uint8_t *font_ptr, int h,
                                  uint32_t fgcol, uint32_t bgcol, int dup9);
typedef void vga_draw_line_func(VGACommonState *s1, uint8_t *d,
                                const uint8_t *s, int width);

#define DEPTH 8
#include "vga_template.h"

#define DEPTH 15
#include "vga_template.h"

#define BGR_FORMAT
#define DEPTH 15
#include "vga_template.h"

#define DEPTH 16
#include "vga_template.h"

#define BGR_FORMAT
#define DEPTH 16
#include "vga_template.h"

#define DEPTH 32
#include "vga_template.h"

#define BGR_FORMAT
#define DEPTH 32
#include "vga_template.h"

static unsigned int rgb_to_pixel8_dup(unsigned int r, unsigned int g, unsigned b)
{
    unsigned int col;
    col = rgb_to_pixel8(r, g, b);
    col |= col << 8;
    col |= col << 16;
    return col;
}

static unsigned int rgb_to_pixel15_dup(unsigned int r, unsigned int g, unsigned b)
{
    unsigned int col;
    col = rgb_to_pixel15(r, g, b);
    col |= col << 16;
    return col;
}

static unsigned int rgb_to_pixel15bgr_dup(unsigned int r, unsigned int g,
                                          unsigned int b)
{
    unsigned int col;
    col = rgb_to_pixel15bgr(r, g, b);
    col |= col << 16;
    return col;
}

static unsigned int rgb_to_pixel16_dup(unsigned int r, unsigned int g, unsigned b)
{
    unsigned int col;
    col = rgb_to_pixel16(r, g, b);
    col |= col << 16;
    return col;
}

static unsigned int rgb_to_pixel16bgr_dup(unsigned int r, unsigned int g,
                                          unsigned int b)
{
    unsigned int col;
    col = rgb_to_pixel16bgr(r, g, b);
    col |= col << 16;
    return col;
}

static unsigned int rgb_to_pixel32_dup(unsigned int r, unsigned int g, unsigned b)
{
    unsigned int col;
    col = rgb_to_pixel32(r, g, b);
    return col;
}

static unsigned int rgb_to_pixel32bgr_dup(unsigned int r, unsigned int g, unsigned b)
{
    unsigned int col;
    col = rgb_to_pixel32bgr(r, g, b);
    return col;
}

/* return true if the palette was modified */
static int update_palette16(VGACommonState *s)
{
    int full_update, i;
    uint32_t v, col, *palette;

    full_update = 0;
    palette = s->last_palette;
    for(i = 0; i < 16; i++) {
        v = s->ar[i];
        if (s->ar[VGA_ATC_MODE] & 0x80) {
            v = ((s->ar[VGA_ATC_COLOR_PAGE] & 0xf) << 4) | (v & 0xf);
        } else {
            v = ((s->ar[VGA_ATC_COLOR_PAGE] & 0xc) << 4) | (v & 0x3f);
        }
        v = v * 3;
        col = s->rgb_to_pixel(c6_to_8(s->palette[v]),
                              c6_to_8(s->palette[v + 1]),
                              c6_to_8(s->palette[v + 2]));
        if (col != palette[i]) {
            full_update = 1;
            palette[i] = col;
        }
    }
    return full_update;
}

/* return true if the palette was modified */
static int update_palette256(VGACommonState *s)
{
    int full_update, i;
    uint32_t v, col, *palette;

    full_update = 0;
    palette = s->last_palette;
    v = 0;
    for(i = 0; i < 256; i++) {
        if (s->dac_8bit) {
          col = s->rgb_to_pixel(s->palette[v],
                                s->palette[v + 1],
                                s->palette[v + 2]);
        } else {
          col = s->rgb_to_pixel(c6_to_8(s->palette[v]),
                                c6_to_8(s->palette[v + 1]),
                                c6_to_8(s->palette[v + 2]));
        }
        if (col != palette[i]) {
            full_update = 1;
            palette[i] = col;
        }
        v += 3;
    }
    return full_update;
}

static void vga_get_offsets(VGACommonState *s,
                            uint32_t *pline_offset,
                            uint32_t *pstart_addr,
                            uint32_t *pline_compare)
{
    uint32_t start_addr, line_offset, line_compare;

    if (s->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED) {
        line_offset = s->vbe_line_offset;
        start_addr = s->vbe_start_addr;
        line_compare = 65535;
    } else {
        /* compute line_offset in bytes */
        line_offset = s->cr[VGA_CRTC_OFFSET];
        line_offset <<= 3;

        /* starting address */
        start_addr = s->cr[VGA_CRTC_START_LO] |
            (s->cr[VGA_CRTC_START_HI] << 8);

        /* line compare */
        line_compare = s->cr[VGA_CRTC_LINE_COMPARE] |
            ((s->cr[VGA_CRTC_OVERFLOW] & 0x10) << 4) |
            ((s->cr[VGA_CRTC_MAX_SCAN] & 0x40) << 3);
    }
    *pline_offset = line_offset;
    *pstart_addr = start_addr;
    *pline_compare = line_compare;
}

/* update start_addr and line_offset. Return TRUE if modified */
static int update_basic_params(VGACommonState *s)
{
    int full_update;
    uint32_t start_addr, line_offset, line_compare;

    full_update = 0;

    s->get_offsets(s, &line_offset, &start_addr, &line_compare);

    if (line_offset != s->line_offset ||
        start_addr != s->start_addr ||
        line_compare != s->line_compare) {
        s->line_offset = line_offset;
        s->start_addr = start_addr;
        s->line_compare = line_compare;
        full_update = 1;
    }
    return full_update;
}

#define NB_DEPTHS 7

static inline int get_depth_index(DisplaySurface *s)
{
    switch (surface_bits_per_pixel(s)) {
    default:
    case 8:
        return 0;
    case 15:
        return 1;
    case 16:
        return 2;
    case 32:
        if (is_surface_bgr(s)) {
            return 4;
        } else {
            return 3;
        }
    }
}

static vga_draw_glyph8_func * const vga_draw_glyph8_table[NB_DEPTHS] = {
    vga_draw_glyph8_8,
    vga_draw_glyph8_16,
    vga_draw_glyph8_16,
    vga_draw_glyph8_32,
    vga_draw_glyph8_32,
    vga_draw_glyph8_16,
    vga_draw_glyph8_16,
};

static vga_draw_glyph8_func * const vga_draw_glyph16_table[NB_DEPTHS] = {
    vga_draw_glyph16_8,
    vga_draw_glyph16_16,
    vga_draw_glyph16_16,
    vga_draw_glyph16_32,
    vga_draw_glyph16_32,
    vga_draw_glyph16_16,
    vga_draw_glyph16_16,
};

static vga_draw_glyph9_func * const vga_draw_glyph9_table[NB_DEPTHS] = {
    vga_draw_glyph9_8,
    vga_draw_glyph9_16,
    vga_draw_glyph9_16,
    vga_draw_glyph9_32,
    vga_draw_glyph9_32,
    vga_draw_glyph9_16,
    vga_draw_glyph9_16,
};

static const uint8_t cursor_glyph[32 * 4] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static void vga_get_text_resolution(VGACommonState *s, int *pwidth, int *pheight,
                                    int *pcwidth, int *pcheight)
{
    int width, cwidth, height, cheight;

    /* total width & height */
    cheight = (s->cr[VGA_CRTC_MAX_SCAN] & 0x1f) + 1;
    cwidth = 8;
    if (!(s->sr[VGA_SEQ_CLOCK_MODE] & VGA_SR01_CHAR_CLK_8DOTS)) {
        cwidth = 9;
    }
    if (s->sr[VGA_SEQ_CLOCK_MODE] & 0x08) {
        cwidth = 16; /* NOTE: no 18 pixel wide */
    }
    width = (s->cr[VGA_CRTC_H_DISP] + 1);
    if (s->cr[VGA_CRTC_V_TOTAL] == 100) {
        /* ugly hack for CGA 160x100x16 - explain me the logic */
        height = 100;
    } else {
        height = s->cr[VGA_CRTC_V_DISP_END] |
            ((s->cr[VGA_CRTC_OVERFLOW] & 0x02) << 7) |
            ((s->cr[VGA_CRTC_OVERFLOW] & 0x40) << 3);
        height = (height + 1) / cheight;
    }

    *pwidth = width;
    *pheight = height;
    *pcwidth = cwidth;
    *pcheight = cheight;
}

typedef unsigned int rgb_to_pixel_dup_func(unsigned int r, unsigned int g, unsigned b);

static rgb_to_pixel_dup_func * const rgb_to_pixel_dup_table[NB_DEPTHS] = {
    rgb_to_pixel8_dup,
    rgb_to_pixel15_dup,
    rgb_to_pixel16_dup,
    rgb_to_pixel32_dup,
    rgb_to_pixel32bgr_dup,
    rgb_to_pixel15bgr_dup,
    rgb_to_pixel16bgr_dup,
};

/*
 * Text mode update
 * Missing:
 * - double scan
 * - double width
 * - underline
 * - flashing
 */
static void vga_draw_text(VGACommonState *s, int full_update)
{
    DisplaySurface *surface = qemu_console_surface(s->con);
    int cx, cy, cheight, cw, ch, cattr, height, width, ch_attr;
    int cx_min, cx_max, linesize, x_incr, line, line1;
    uint32_t offset, fgcol, bgcol, v, cursor_offset;
    uint8_t *d1, *d, *src, *dest, *cursor_ptr;
    const uint8_t *font_ptr, *font_base[2];
    int dup9, line_offset, depth_index;
    uint32_t *palette;
    uint32_t *ch_attr_ptr;
    vga_draw_glyph8_func *vga_draw_glyph8;
    vga_draw_glyph9_func *vga_draw_glyph9;
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);

    /* compute font data address (in plane 2) */
    v = s->sr[VGA_SEQ_CHARACTER_MAP];
    offset = (((v >> 4) & 1) | ((v << 1) & 6)) * 8192 * 4 + 2;
    if (offset != s->font_offsets[0]) {
        s->font_offsets[0] = offset;
        full_update = 1;
    }
    font_base[0] = s->vram_ptr + offset;

    offset = (((v >> 5) & 1) | ((v >> 1) & 6)) * 8192 * 4 + 2;
    font_base[1] = s->vram_ptr + offset;
    if (offset != s->font_offsets[1]) {
        s->font_offsets[1] = offset;
        full_update = 1;
    }
    if (s->plane_updated & (1 << 2) || s->has_chain4_alias) {
        /* if the plane 2 was modified since the last display, it
           indicates the font may have been modified */
        s->plane_updated = 0;
        full_update = 1;
    }
    full_update |= update_basic_params(s);

    line_offset = s->line_offset;

    vga_get_text_resolution(s, &width, &height, &cw, &cheight);
    if ((height * width) <= 1) {
        /* better than nothing: exit if transient size is too small */
        return;
    }
    if ((height * width) > CH_ATTR_SIZE) {
        /* better than nothing: exit if transient size is too big */
        return;
    }

    if (width != s->last_width || height != s->last_height ||
        cw != s->last_cw || cheight != s->last_ch || s->last_depth) {
        s->last_scr_width = width * cw;
        s->last_scr_height = height * cheight;
        qemu_console_resize(s->con, s->last_scr_width, s->last_scr_height);
        surface = qemu_console_surface(s->con);
        dpy_text_resize(s->con, width, height);
        s->last_depth = 0;
        s->last_width = width;
        s->last_height = height;
        s->last_ch = cheight;
        s->last_cw = cw;
        full_update = 1;
    }
    s->rgb_to_pixel =
        rgb_to_pixel_dup_table[get_depth_index(surface)];
    full_update |= update_palette16(s);
    palette = s->last_palette;
    x_incr = cw * surface_bytes_per_pixel(surface);

    if (full_update) {
        s->full_update_text = 1;
    }
    if (s->full_update_gfx) {
        s->full_update_gfx = 0;
        full_update |= 1;
    }

    cursor_offset = ((s->cr[VGA_CRTC_CURSOR_HI] << 8) |
                     s->cr[VGA_CRTC_CURSOR_LO]) - s->start_addr;
    if (cursor_offset != s->cursor_offset ||
        s->cr[VGA_CRTC_CURSOR_START] != s->cursor_start ||
        s->cr[VGA_CRTC_CURSOR_END] != s->cursor_end) {
      /* if the cursor position changed, we update the old and new
         chars */
        if (s->cursor_offset < CH_ATTR_SIZE)
            s->last_ch_attr[s->cursor_offset] = -1;
        if (cursor_offset < CH_ATTR_SIZE)
            s->last_ch_attr[cursor_offset] = -1;
        s->cursor_offset = cursor_offset;
        s->cursor_start = s->cr[VGA_CRTC_CURSOR_START];
        s->cursor_end = s->cr[VGA_CRTC_CURSOR_END];
    }
    cursor_ptr = s->vram_ptr + (s->start_addr + cursor_offset) * 4;
    if (now >= s->cursor_blink_time) {
        s->cursor_blink_time = now + VGA_TEXT_CURSOR_PERIOD_MS / 2;
        s->cursor_visible_phase = !s->cursor_visible_phase;
    }

    depth_index = get_depth_index(surface);
    if (cw == 16)
        vga_draw_glyph8 = vga_draw_glyph16_table[depth_index];
    else
        vga_draw_glyph8 = vga_draw_glyph8_table[depth_index];
    vga_draw_glyph9 = vga_draw_glyph9_table[depth_index];

    dest = surface_data(surface);
    linesize = surface_stride(surface);
    ch_attr_ptr = s->last_ch_attr;
    line = 0;
    offset = s->start_addr * 4;
    for(cy = 0; cy < height; cy++) {
        d1 = dest;
        src = s->vram_ptr + offset;
        cx_min = width;
        cx_max = -1;
        for(cx = 0; cx < width; cx++) {
            ch_attr = *(uint16_t *)src;
            if (full_update || ch_attr != *ch_attr_ptr || src == cursor_ptr) {
                if (cx < cx_min)
                    cx_min = cx;
                if (cx > cx_max)
                    cx_max = cx;
                *ch_attr_ptr = ch_attr;
#ifdef HOST_WORDS_BIGENDIAN
                ch = ch_attr >> 8;
                cattr = ch_attr & 0xff;
#else
                ch = ch_attr & 0xff;
                cattr = ch_attr >> 8;
#endif
                font_ptr = font_base[(cattr >> 3) & 1];
                font_ptr += 32 * 4 * ch;
                bgcol = palette[cattr >> 4];
                fgcol = palette[cattr & 0x0f];
                if (cw != 9) {
                    vga_draw_glyph8(d1, linesize,
                                    font_ptr, cheight, fgcol, bgcol);
                } else {
                    dup9 = 0;
                    if (ch >= 0xb0 && ch <= 0xdf &&
                        (s->ar[VGA_ATC_MODE] & 0x04)) {
                        dup9 = 1;
                    }
                    vga_draw_glyph9(d1, linesize,
                                    font_ptr, cheight, fgcol, bgcol, dup9);
                }
                if (src == cursor_ptr &&
                    !(s->cr[VGA_CRTC_CURSOR_START] & 0x20) &&
                    s->cursor_visible_phase) {
                    int line_start, line_last, h;
                    /* draw the cursor */
                    line_start = s->cr[VGA_CRTC_CURSOR_START] & 0x1f;
                    line_last = s->cr[VGA_CRTC_CURSOR_END] & 0x1f;
                    /* XXX: check that */
                    if (line_last > cheight - 1)
                        line_last = cheight - 1;
                    if (line_last >= line_start && line_start < cheight) {
                        h = line_last - line_start + 1;
                        d = d1 + linesize * line_start;
                        if (cw != 9) {
                            vga_draw_glyph8(d, linesize,
                                            cursor_glyph, h, fgcol, bgcol);
                        } else {
                            vga_draw_glyph9(d, linesize,
                                            cursor_glyph, h, fgcol, bgcol, 1);
                        }
                    }
                }
            }
            d1 += x_incr;
            src += 4;
            ch_attr_ptr++;
        }
        if (cx_max != -1) {
            dpy_gfx_update(s->con, cx_min * cw, cy * cheight,
                           (cx_max - cx_min + 1) * cw, cheight);
        }
        dest += linesize * cheight;
        line1 = line + cheight;
        offset += line_offset;
        if (line < s->line_compare && line1 >= s->line_compare) {
            offset = 0;
        }
        line = line1;
    }
}

enum {
    VGA_DRAW_LINE2,
    VGA_DRAW_LINE2D2,
    VGA_DRAW_LINE4,
    VGA_DRAW_LINE4D2,
    VGA_DRAW_LINE8D2,
    VGA_DRAW_LINE8,
    VGA_DRAW_LINE15,
    VGA_DRAW_LINE16,
    VGA_DRAW_LINE24,
    VGA_DRAW_LINE32,
    VGA_DRAW_LINE_NB,
};

static vga_draw_line_func * const vga_draw_line_table[NB_DEPTHS * VGA_DRAW_LINE_NB] = {
    vga_draw_line2_8,
    vga_draw_line2_16,
    vga_draw_line2_16,
    vga_draw_line2_32,
    vga_draw_line2_32,
    vga_draw_line2_16,
    vga_draw_line2_16,

    vga_draw_line2d2_8,
    vga_draw_line2d2_16,
    vga_draw_line2d2_16,
    vga_draw_line2d2_32,
    vga_draw_line2d2_32,
    vga_draw_line2d2_16,
    vga_draw_line2d2_16,

    vga_draw_line4_8,
    vga_draw_line4_16,
    vga_draw_line4_16,
    vga_draw_line4_32,
    vga_draw_line4_32,
    vga_draw_line4_16,
    vga_draw_line4_16,

    vga_draw_line4d2_8,
    vga_draw_line4d2_16,
    vga_draw_line4d2_16,
    vga_draw_line4d2_32,
    vga_draw_line4d2_32,
    vga_draw_line4d2_16,
    vga_draw_line4d2_16,

    vga_draw_line8d2_8,
    vga_draw_line8d2_16,
    vga_draw_line8d2_16,
    vga_draw_line8d2_32,
    vga_draw_line8d2_32,
    vga_draw_line8d2_16,
    vga_draw_line8d2_16,

    vga_draw_line8_8,
    vga_draw_line8_16,
    vga_draw_line8_16,
    vga_draw_line8_32,
    vga_draw_line8_32,
    vga_draw_line8_16,
    vga_draw_line8_16,

    vga_draw_line15_8,
    vga_draw_line15_15,
    vga_draw_line15_16,
    vga_draw_line15_32,
    vga_draw_line15_32bgr,
    vga_draw_line15_15bgr,
    vga_draw_line15_16bgr,

    vga_draw_line16_8,
    vga_draw_line16_15,
    vga_draw_line16_16,
    vga_draw_line16_32,
    vga_draw_line16_32bgr,
    vga_draw_line16_15bgr,
    vga_draw_line16_16bgr,

    vga_draw_line24_8,
    vga_draw_line24_15,
    vga_draw_line24_16,
    vga_draw_line24_32,
    vga_draw_line24_32bgr,
    vga_draw_line24_15bgr,
    vga_draw_line24_16bgr,

    vga_draw_line32_8,
    vga_draw_line32_15,
    vga_draw_line32_16,
    vga_draw_line32_32,
    vga_draw_line32_32bgr,
    vga_draw_line32_15bgr,
    vga_draw_line32_16bgr,
};

static int vga_get_bpp(VGACommonState *s)
{
    int ret;

    if (s->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED) {
        ret = s->vbe_regs[VBE_DISPI_INDEX_BPP];
    } else {
        ret = 0;
    }
    return ret;
}

static void vga_get_resolution(VGACommonState *s, int *pwidth, int *pheight)
{
    int width, height;

    if (s->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED) {
        width = s->vbe_regs[VBE_DISPI_INDEX_XRES];
        height = s->vbe_regs[VBE_DISPI_INDEX_YRES];
    } else {
        width = (s->cr[VGA_CRTC_H_DISP] + 1) * 8;
        height = s->cr[VGA_CRTC_V_DISP_END] |
            ((s->cr[VGA_CRTC_OVERFLOW] & 0x02) << 7) |
            ((s->cr[VGA_CRTC_OVERFLOW] & 0x40) << 3);
        height = (height + 1);
    }
    *pwidth = width;
    *pheight = height;
}

void vga_invalidate_scanlines(VGACommonState *s, int y1, int y2)
{
    int y;
    if (y1 >= VGA_MAX_HEIGHT)
        return;
    if (y2 >= VGA_MAX_HEIGHT)
        y2 = VGA_MAX_HEIGHT;
    for(y = y1; y < y2; y++) {
        s->invalidated_y_table[y >> 5] |= 1 << (y & 0x1f);
    }
}

void vga_sync_dirty_bitmap(VGACommonState *s)
{
    memory_region_sync_dirty_bitmap(&s->vram);
}

void vga_dirty_log_start(VGACommonState *s)
{
    memory_region_set_log(&s->vram, true, DIRTY_MEMORY_VGA);
}

void vga_dirty_log_stop(VGACommonState *s)
{
    memory_region_set_log(&s->vram, false, DIRTY_MEMORY_VGA);
}

/*
 * graphic modes
 */
static void vga_draw_graphic(VGACommonState *s, int full_update)
{
    DisplaySurface *surface = qemu_console_surface(s->con);
    int y1, y, update, linesize, y_start, double_scan, mask, depth;
    int width, height, shift_control, line_offset, bwidth, bits;
    ram_addr_t page0, page1, page_min, page_max;
    int disp_width, multi_scan, multi_run;
    uint8_t *d;
    uint32_t v, addr1, addr;
    vga_draw_line_func *vga_draw_line;
#if defined(HOST_WORDS_BIGENDIAN) == defined(TARGET_WORDS_BIGENDIAN)
    static const bool byteswap = false;
#else
    static const bool byteswap = true;
#endif

    full_update |= update_basic_params(s);

    if (!full_update)
        vga_sync_dirty_bitmap(s);

    s->get_resolution(s, &width, &height);
    disp_width = width;

    shift_control = (s->gr[VGA_GFX_MODE] >> 5) & 3;
    double_scan = (s->cr[VGA_CRTC_MAX_SCAN] >> 7);
    if (shift_control != 1) {
        multi_scan = (((s->cr[VGA_CRTC_MAX_SCAN] & 0x1f) + 1) << double_scan)
            - 1;
    } else {
        /* in CGA modes, multi_scan is ignored */
        /* XXX: is it correct ? */
        multi_scan = double_scan;
    }
    multi_run = multi_scan;
    if (shift_control != s->shift_control ||
        double_scan != s->double_scan) {
        full_update = 1;
        s->shift_control = shift_control;
        s->double_scan = double_scan;
    }

    if (shift_control == 0) {
        if (s->sr[VGA_SEQ_CLOCK_MODE] & 8) {
            disp_width <<= 1;
        }
    } else if (shift_control == 1) {
        if (s->sr[VGA_SEQ_CLOCK_MODE] & 8) {
            disp_width <<= 1;
        }
    }

    depth = s->get_bpp(s);
    if (s->line_offset != s->last_line_offset ||
        disp_width != s->last_width ||
        height != s->last_height ||
        s->last_depth != depth) {
        if (depth == 32 || (depth == 16 && !byteswap)) {
            pixman_format_code_t format =
                qemu_default_pixman_format(depth, !byteswap);
            surface = qemu_create_displaysurface_from(disp_width,
                    height, format, s->line_offset,
                    s->vram_ptr + (s->start_addr * 4));
            dpy_gfx_replace_surface(s->con, surface);
        } else {
            qemu_console_resize(s->con, disp_width, height);
            surface = qemu_console_surface(s->con);
        }
        s->last_scr_width = disp_width;
        s->last_scr_height = height;
        s->last_width = disp_width;
        s->last_height = height;
        s->last_line_offset = s->line_offset;
        s->last_depth = depth;
        full_update = 1;
    } else if (is_buffer_shared(surface) &&
               (full_update || surface_data(surface) != s->vram_ptr
                + (s->start_addr * 4))) {
        pixman_format_code_t format =
            qemu_default_pixman_format(depth, !byteswap);
        surface = qemu_create_displaysurface_from(disp_width,
                height, format, s->line_offset,
                s->vram_ptr + (s->start_addr * 4));
        dpy_gfx_replace_surface(s->con, surface);
    }

    s->rgb_to_pixel =
        rgb_to_pixel_dup_table[get_depth_index(surface)];

    if (shift_control == 0) {
        full_update |= update_palette16(s);
        if (s->sr[VGA_SEQ_CLOCK_MODE] & 8) {
            v = VGA_DRAW_LINE4D2;
        } else {
            v = VGA_DRAW_LINE4;
        }
        bits = 4;
    } else if (shift_control == 1) {
        full_update |= update_palette16(s);
        if (s->sr[VGA_SEQ_CLOCK_MODE] & 8) {
            v = VGA_DRAW_LINE2D2;
        } else {
            v = VGA_DRAW_LINE2;
        }
        bits = 4;
    } else {
        switch(s->get_bpp(s)) {
        default:
        case 0:
            full_update |= update_palette256(s);
            v = VGA_DRAW_LINE8D2;
            bits = 4;
            break;
        case 8:
            full_update |= update_palette256(s);
            v = VGA_DRAW_LINE8;
            bits = 8;
            break;
        case 15:
            v = VGA_DRAW_LINE15;
            bits = 16;
            break;
        case 16:
            v = VGA_DRAW_LINE16;
            bits = 16;
            break;
        case 24:
            v = VGA_DRAW_LINE24;
            bits = 24;
            break;
        case 32:
            v = VGA_DRAW_LINE32;
            bits = 32;
            break;
        }
    }
    vga_draw_line = vga_draw_line_table[v * NB_DEPTHS +
                                        get_depth_index(surface)];

    if (!is_buffer_shared(surface) && s->cursor_invalidate) {
        s->cursor_invalidate(s);
    }

    line_offset = s->line_offset;
#if 0
    printf("w=%d h=%d v=%d line_offset=%d cr[0x09]=0x%02x cr[0x17]=0x%02x linecmp=%d sr[0x01]=0x%02x\n",
           width, height, v, line_offset, s->cr[9], s->cr[VGA_CRTC_MODE],
           s->line_compare, s->sr[VGA_SEQ_CLOCK_MODE]);
#endif
    addr1 = (s->start_addr * 4);
    bwidth = (width * bits + 7) / 8;
    y_start = -1;
    page_min = -1;
    page_max = 0;
    d = surface_data(surface);
    linesize = surface_stride(surface);
    y1 = 0;
    for(y = 0; y < height; y++) {
        addr = addr1;
        if (!(s->cr[VGA_CRTC_MODE] & 1)) {
            int shift;
            /* CGA compatibility handling */
            shift = 14 + ((s->cr[VGA_CRTC_MODE] >> 6) & 1);
            addr = (addr & ~(1 << shift)) | ((y1 & 1) << shift);
        }
        if (!(s->cr[VGA_CRTC_MODE] & 2)) {
            addr = (addr & ~0x8000) | ((y1 & 2) << 14);
        }
        update = full_update;
        page0 = addr;
        page1 = addr + bwidth - 1;
        update |= memory_region_get_dirty(&s->vram, page0, page1 - page0,
                                          DIRTY_MEMORY_VGA);
        /* explicit invalidation for the hardware cursor */
        update |= (s->invalidated_y_table[y >> 5] >> (y & 0x1f)) & 1;
        if (update) {
            if (y_start < 0)
                y_start = y;
            if (page0 < page_min)
                page_min = page0;
            if (page1 > page_max)
                page_max = page1;
            if (!(is_buffer_shared(surface))) {
                vga_draw_line(s, d, s->vram_ptr + addr, width);
                if (s->cursor_draw_line)
                    s->cursor_draw_line(s, d, y);
            }
        } else {
            if (y_start >= 0) {
                /* flush to display */
                dpy_gfx_update(s->con, 0, y_start,
                               disp_width, y - y_start);
                y_start = -1;
            }
        }
        if (!multi_run) {
            mask = (s->cr[VGA_CRTC_MODE] & 3) ^ 3;
            if ((y1 & mask) == mask)
                addr1 += line_offset;
            y1++;
            multi_run = multi_scan;
        } else {
            multi_run--;
        }
        /* line compare acts on the displayed lines */
        if (y == s->line_compare)
            addr1 = 0;
        d += linesize;
    }
    if (y_start >= 0) {
        /* flush to display */
        dpy_gfx_update(s->con, 0, y_start,
                       disp_width, y - y_start);
    }
    /* reset modified pages */
    if (page_max >= page_min) {
        memory_region_reset_dirty(&s->vram,
                                  page_min,
                                  page_max - page_min,
                                  DIRTY_MEMORY_VGA);
    }
    memset(s->invalidated_y_table, 0, ((height + 31) >> 5) * 4);
}

static void vga_draw_blank(VGACommonState *s, int full_update)
{
    DisplaySurface *surface = qemu_console_surface(s->con);
    int i, w, val;
    uint8_t *d;

    if (!full_update)
        return;
    if (s->last_scr_width <= 0 || s->last_scr_height <= 0)
        return;

    s->rgb_to_pixel =
        rgb_to_pixel_dup_table[get_depth_index(surface)];
    if (surface_bits_per_pixel(surface) == 8) {
        val = s->rgb_to_pixel(0, 0, 0);
    } else {
        val = 0;
    }
    w = s->last_scr_width * surface_bytes_per_pixel(surface);
    d = surface_data(surface);
    for(i = 0; i < s->last_scr_height; i++) {
        memset(d, val, w);
        d += surface_stride(surface);
    }
    dpy_gfx_update(s->con, 0, 0,
                   s->last_scr_width, s->last_scr_height);
}

#define GMODE_TEXT     0
#define GMODE_GRAPH    1
#define GMODE_BLANK 2

static void vga_update_display(void *opaque)
{
    VGACommonState *s = opaque;
    DisplaySurface *surface = qemu_console_surface(s->con);
    int full_update, graphic_mode;

    qemu_flush_coalesced_mmio_buffer();

    if (surface_bits_per_pixel(surface) == 0) {
        /* nothing to do */
    } else {
        full_update = 0;
        if (!(s->ar_index & 0x20)) {
            graphic_mode = GMODE_BLANK;
        } else {
            graphic_mode = s->gr[VGA_GFX_MISC] & VGA_GR06_GRAPHICS_MODE;
        }
        if (graphic_mode != s->graphic_mode) {
            s->graphic_mode = graphic_mode;
            s->cursor_blink_time = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
            full_update = 1;
        }
        switch(graphic_mode) {
        case GMODE_TEXT:
            vga_draw_text(s, full_update);
            break;
        case GMODE_GRAPH:
            vga_draw_graphic(s, full_update);
            break;
        case GMODE_BLANK:
        default:
            vga_draw_blank(s, full_update);
            break;
        }
    }
}

/* force a full display refresh */
static void vga_invalidate_display(void *opaque)
{
    VGACommonState *s = opaque;

    s->last_width = -1;
    s->last_height = -1;
}

void vga_common_reset(VGACommonState *s)
{
    s->sr_index = 0;
    memset(s->sr, '\0', sizeof(s->sr));
    s->gr_index = 0;
    memset(s->gr, '\0', sizeof(s->gr));
    s->ar_index = 0;
    memset(s->ar, '\0', sizeof(s->ar));
    s->ar_flip_flop = 0;
    s->cr_index = 0;
    memset(s->cr, '\0', sizeof(s->cr));
    s->msr = 0;
    s->fcr = 0;
    s->st00 = 0;
    s->st01 = 0;
    s->dac_state = 0;
    s->dac_sub_index = 0;
    s->dac_read_index = 0;
    s->dac_write_index = 0;
    memset(s->dac_cache, '\0', sizeof(s->dac_cache));
    s->dac_8bit = 0;
    memset(s->palette, '\0', sizeof(s->palette));
    s->bank_offset = 0;
    s->vbe_index = 0;
    memset(s->vbe_regs, '\0', sizeof(s->vbe_regs));
    s->vbe_regs[VBE_DISPI_INDEX_ID] = VBE_DISPI_ID5;
    s->vbe_start_addr = 0;
    s->vbe_line_offset = 0;
    s->vbe_bank_mask = (s->vram_size >> 16) - 1;
    memset(s->font_offsets, '\0', sizeof(s->font_offsets));
    s->graphic_mode = -1; /* force full update */
    s->shift_control = 0;
    s->double_scan = 0;
    s->line_offset = 0;
    s->line_compare = 0;
    s->start_addr = 0;
    s->plane_updated = 0;
    s->last_cw = 0;
    s->last_ch = 0;
    s->last_width = 0;
    s->last_height = 0;
    s->last_scr_width = 0;
    s->last_scr_height = 0;
    s->cursor_start = 0;
    s->cursor_end = 0;
    s->cursor_offset = 0;
    memset(s->invalidated_y_table, '\0', sizeof(s->invalidated_y_table));
    memset(s->last_palette, '\0', sizeof(s->last_palette));
    memset(s->last_ch_attr, '\0', sizeof(s->last_ch_attr));
    switch (vga_retrace_method) {
    case VGA_RETRACE_DUMB:
        break;
    case VGA_RETRACE_PRECISE:
        memset(&s->retrace_info, 0, sizeof (s->retrace_info));
        break;
    }
    vga_update_memory_access(s);
}

static void vga_reset(void *opaque)
{
    VGACommonState *s =  opaque;
    vga_common_reset(s);
}

#define TEXTMODE_X(x)	((x) % width)
#define TEXTMODE_Y(x)	((x) / width)
#define VMEM2CHTYPE(v)	((v & 0xff0007ff) | \
        ((v & 0x00000800) << 10) | ((v & 0x00007000) >> 1))
/* relay text rendering to the display driver
 * instead of doing a full vga_update_display() */
static void vga_update_text(void *opaque, console_ch_t *chardata)
{
    VGACommonState *s =  opaque;
    int graphic_mode, i, cursor_offset, cursor_visible;
    int cw, cheight, width, height, size, c_min, c_max;
    uint32_t *src;
    console_ch_t *dst, val;
    char msg_buffer[80];
    int full_update = 0;

    qemu_flush_coalesced_mmio_buffer();

    if (!(s->ar_index & 0x20)) {
        graphic_mode = GMODE_BLANK;
    } else {
        graphic_mode = s->gr[VGA_GFX_MISC] & VGA_GR06_GRAPHICS_MODE;
    }
    if (graphic_mode != s->graphic_mode) {
        s->graphic_mode = graphic_mode;
        full_update = 1;
    }
    if (s->last_width == -1) {
        s->last_width = 0;
        full_update = 1;
    }

    switch (graphic_mode) {
    case GMODE_TEXT:
        /* TODO: update palette */
        full_update |= update_basic_params(s);

        /* total width & height */
        cheight = (s->cr[VGA_CRTC_MAX_SCAN] & 0x1f) + 1;
        cw = 8;
        if (!(s->sr[VGA_SEQ_CLOCK_MODE] & VGA_SR01_CHAR_CLK_8DOTS)) {
            cw = 9;
        }
        if (s->sr[VGA_SEQ_CLOCK_MODE] & 0x08) {
            cw = 16; /* NOTE: no 18 pixel wide */
        }
        width = (s->cr[VGA_CRTC_H_DISP] + 1);
        if (s->cr[VGA_CRTC_V_TOTAL] == 100) {
            /* ugly hack for CGA 160x100x16 - explain me the logic */
            height = 100;
        } else {
            height = s->cr[VGA_CRTC_V_DISP_END] |
                ((s->cr[VGA_CRTC_OVERFLOW] & 0x02) << 7) |
                ((s->cr[VGA_CRTC_OVERFLOW] & 0x40) << 3);
            height = (height + 1) / cheight;
        }

        size = (height * width);
        if (size > CH_ATTR_SIZE) {
            if (!full_update)
                return;

            snprintf(msg_buffer, sizeof(msg_buffer), "%i x %i Text mode",
                     width, height);
            break;
        }

        if (width != s->last_width || height != s->last_height ||
            cw != s->last_cw || cheight != s->last_ch) {
            s->last_scr_width = width * cw;
            s->last_scr_height = height * cheight;
            qemu_console_resize(s->con, s->last_scr_width, s->last_scr_height);
            dpy_text_resize(s->con, width, height);
            s->last_depth = 0;
            s->last_width = width;
            s->last_height = height;
            s->last_ch = cheight;
            s->last_cw = cw;
            full_update = 1;
        }

        if (full_update) {
            s->full_update_gfx = 1;
        }
        if (s->full_update_text) {
            s->full_update_text = 0;
            full_update |= 1;
        }

        /* Update "hardware" cursor */
        cursor_offset = ((s->cr[VGA_CRTC_CURSOR_HI] << 8) |
                         s->cr[VGA_CRTC_CURSOR_LO]) - s->start_addr;
        if (cursor_offset != s->cursor_offset ||
            s->cr[VGA_CRTC_CURSOR_START] != s->cursor_start ||
            s->cr[VGA_CRTC_CURSOR_END] != s->cursor_end || full_update) {
            cursor_visible = !(s->cr[VGA_CRTC_CURSOR_START] & 0x20);
            if (cursor_visible && cursor_offset < size && cursor_offset >= 0)
                dpy_text_cursor(s->con,
                                TEXTMODE_X(cursor_offset),
                                TEXTMODE_Y(cursor_offset));
            else
                dpy_text_cursor(s->con, -1, -1);
            s->cursor_offset = cursor_offset;
            s->cursor_start = s->cr[VGA_CRTC_CURSOR_START];
            s->cursor_end = s->cr[VGA_CRTC_CURSOR_END];
        }

        src = (uint32_t *) s->vram_ptr + s->start_addr;
        dst = chardata;

        if (full_update) {
            for (i = 0; i < size; src ++, dst ++, i ++)
                console_write_ch(dst, VMEM2CHTYPE(le32_to_cpu(*src)));

            dpy_text_update(s->con, 0, 0, width, height);
        } else {
            c_max = 0;

            for (i = 0; i < size; src ++, dst ++, i ++) {
                console_write_ch(&val, VMEM2CHTYPE(le32_to_cpu(*src)));
                if (*dst != val) {
                    *dst = val;
                    c_max = i;
                    break;
                }
            }
            c_min = i;
            for (; i < size; src ++, dst ++, i ++) {
                console_write_ch(&val, VMEM2CHTYPE(le32_to_cpu(*src)));
                if (*dst != val) {
                    *dst = val;
                    c_max = i;
                }
            }

            if (c_min <= c_max) {
                i = TEXTMODE_Y(c_min);
                dpy_text_update(s->con, 0, i, width, TEXTMODE_Y(c_max) - i + 1);
            }
        }

        return;
    case GMODE_GRAPH:
        if (!full_update)
            return;

        s->get_resolution(s, &width, &height);
        snprintf(msg_buffer, sizeof(msg_buffer), "%i x %i Graphic mode",
                 width, height);
        break;
    case GMODE_BLANK:
    default:
        if (!full_update)
            return;

        snprintf(msg_buffer, sizeof(msg_buffer), "VGA Blank mode");
        break;
    }

    /* Display a message */
    s->last_width = 60;
    s->last_height = height = 3;
    dpy_text_cursor(s->con, -1, -1);
    dpy_text_resize(s->con, s->last_width, height);

    for (dst = chardata, i = 0; i < s->last_width * height; i ++)
        console_write_ch(dst ++, ' ');

    size = strlen(msg_buffer);
    width = (s->last_width - size) / 2;
    dst = chardata + s->last_width + width;
    for (i = 0; i < size; i ++)
        console_write_ch(dst ++, 0x00200100 | msg_buffer[i]);

    dpy_text_update(s->con, 0, 0, s->last_width, height);
}

static uint64_t vga_mem_read(void *opaque, hwaddr addr,
                             unsigned size)
{
    VGACommonState *s = opaque;

    return vga_mem_readb(s, addr);
}

static void vga_mem_write(void *opaque, hwaddr addr,
                          uint64_t data, unsigned size)
{
    VGACommonState *s = opaque;

    return vga_mem_writeb(s, addr, data);
}

const MemoryRegionOps vga_mem_ops = {
    .read = vga_mem_read,
    .write = vga_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static int vga_common_post_load(void *opaque, int version_id)
{
    VGACommonState *s = opaque;

    /* force refresh */
    s->graphic_mode = -1;
    return 0;
}

const VMStateDescription vmstate_vga_common = {
    .name = "vga",
    .version_id = 2,
    .minimum_version_id = 2,
    .post_load = vga_common_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(latch, VGACommonState),
        VMSTATE_UINT8(sr_index, VGACommonState),
        VMSTATE_PARTIAL_BUFFER(sr, VGACommonState, 8),
        VMSTATE_UINT8(gr_index, VGACommonState),
        VMSTATE_PARTIAL_BUFFER(gr, VGACommonState, 16),
        VMSTATE_UINT8(ar_index, VGACommonState),
        VMSTATE_BUFFER(ar, VGACommonState),
        VMSTATE_INT32(ar_flip_flop, VGACommonState),
        VMSTATE_UINT8(cr_index, VGACommonState),
        VMSTATE_BUFFER(cr, VGACommonState),
        VMSTATE_UINT8(msr, VGACommonState),
        VMSTATE_UINT8(fcr, VGACommonState),
        VMSTATE_UINT8(st00, VGACommonState),
        VMSTATE_UINT8(st01, VGACommonState),

        VMSTATE_UINT8(dac_state, VGACommonState),
        VMSTATE_UINT8(dac_sub_index, VGACommonState),
        VMSTATE_UINT8(dac_read_index, VGACommonState),
        VMSTATE_UINT8(dac_write_index, VGACommonState),
        VMSTATE_BUFFER(dac_cache, VGACommonState),
        VMSTATE_BUFFER(palette, VGACommonState),

        VMSTATE_INT32(bank_offset, VGACommonState),
        VMSTATE_UINT8_EQUAL(is_vbe_vmstate, VGACommonState),
        VMSTATE_UINT16(vbe_index, VGACommonState),
        VMSTATE_UINT16_ARRAY(vbe_regs, VGACommonState, VBE_DISPI_INDEX_NB),
        VMSTATE_UINT32(vbe_start_addr, VGACommonState),
        VMSTATE_UINT32(vbe_line_offset, VGACommonState),
        VMSTATE_UINT32(vbe_bank_mask, VGACommonState),
        VMSTATE_END_OF_LIST()
    }
};

static const GraphicHwOps vga_ops = {
    .invalidate  = vga_invalidate_display,
    .gfx_update  = vga_update_display,
    .text_update = vga_update_text,
};

void vga_common_init(VGACommonState *s, Object *obj, bool global_vmstate)
{
    int i, j, v, b;

    for(i = 0;i < 256; i++) {
        v = 0;
        for(j = 0; j < 8; j++) {
            v |= ((i >> j) & 1) << (j * 4);
        }
        expand4[i] = v;

        v = 0;
        for(j = 0; j < 4; j++) {
            v |= ((i >> (2 * j)) & 3) << (j * 4);
        }
        expand2[i] = v;
    }
    for(i = 0; i < 16; i++) {
        v = 0;
        for(j = 0; j < 4; j++) {
            b = ((i >> j) & 1);
            v |= b << (2 * j);
            v |= b << (2 * j + 1);
        }
        expand4to8[i] = v;
    }

    /* valid range: 1 MB -> 256 MB */
    s->vram_size = 1024 * 1024;
    while (s->vram_size < (s->vram_size_mb << 20) &&
           s->vram_size < (256 << 20)) {
        s->vram_size <<= 1;
    }
    s->vram_size_mb = s->vram_size >> 20;

    s->is_vbe_vmstate = 1;
    memory_region_init_ram(&s->vram, obj, "vga.vram", s->vram_size);
    vmstate_register_ram(&s->vram, global_vmstate ? NULL : DEVICE(obj));
    xen_register_framebuffer(&s->vram);
    s->vram_ptr = memory_region_get_ram_ptr(&s->vram);
    s->get_bpp = vga_get_bpp;
    s->get_offsets = vga_get_offsets;
    s->get_resolution = vga_get_resolution;
    s->hw_ops = &vga_ops;
    switch (vga_retrace_method) {
    case VGA_RETRACE_DUMB:
        s->retrace = vga_dumb_retrace;
        s->update_retrace_info = vga_dumb_update_retrace_info;
        break;

    case VGA_RETRACE_PRECISE:
        s->retrace = vga_precise_retrace;
        s->update_retrace_info = vga_precise_update_retrace_info;
        break;
    }
    vga_dirty_log_start(s);
}

static const MemoryRegionPortio vga_portio_list[] = {
    { 0x04,  2, 1, .read = vga_ioport_read, .write = vga_ioport_write }, /* 3b4 */
    { 0x0a,  1, 1, .read = vga_ioport_read, .write = vga_ioport_write }, /* 3ba */
    { 0x10, 16, 1, .read = vga_ioport_read, .write = vga_ioport_write }, /* 3c0 */
    { 0x24,  2, 1, .read = vga_ioport_read, .write = vga_ioport_write }, /* 3d4 */
    { 0x2a,  1, 1, .read = vga_ioport_read, .write = vga_ioport_write }, /* 3da */
    PORTIO_END_OF_LIST(),
};

static const MemoryRegionPortio vbe_portio_list[] = {
    { 0, 1, 2, .read = vbe_ioport_read_index, .write = vbe_ioport_write_index },
# ifdef TARGET_I386
    { 1, 1, 2, .read = vbe_ioport_read_data, .write = vbe_ioport_write_data },
# endif
    { 2, 1, 2, .read = vbe_ioport_read_data, .write = vbe_ioport_write_data },
    PORTIO_END_OF_LIST(),
};

/* Used by both ISA and PCI */
MemoryRegion *vga_init_io(VGACommonState *s, Object *obj,
                          const MemoryRegionPortio **vga_ports,
                          const MemoryRegionPortio **vbe_ports)
{
    MemoryRegion *vga_mem;

    *vga_ports = vga_portio_list;
    *vbe_ports = vbe_portio_list;

    vga_mem = g_malloc(sizeof(*vga_mem));
    memory_region_init_io(vga_mem, obj, &vga_mem_ops, s,
                          "vga-lowmem", 0x20000);
    memory_region_set_flush_coalesced(vga_mem);

    return vga_mem;
}

void vga_init(VGACommonState *s, Object *obj, MemoryRegion *address_space,
              MemoryRegion *address_space_io, bool init_vga_ports)
{
    MemoryRegion *vga_io_memory;
    const MemoryRegionPortio *vga_ports, *vbe_ports;

    qemu_register_reset(vga_reset, s);

    s->bank_offset = 0;

    s->legacy_address_space = address_space;

    vga_io_memory = vga_init_io(s, obj, &vga_ports, &vbe_ports);
    memory_region_add_subregion_overlap(address_space,
                                        isa_mem_base + 0x000a0000,
                                        vga_io_memory,
                                        1);
    memory_region_set_coalescing(vga_io_memory);
    if (init_vga_ports) {
        portio_list_init(&s->vga_port_list, obj, vga_ports, s, "vga");
        portio_list_set_flush_coalesced(&s->vga_port_list);
        portio_list_add(&s->vga_port_list, address_space_io, 0x3b0);
    }
    if (vbe_ports) {
        portio_list_init(&s->vbe_port_list, obj, vbe_ports, s, "vbe");
        portio_list_add(&s->vbe_port_list, address_space_io, 0x1ce);
    }
}

void vga_init_vbe(VGACommonState *s, Object *obj, MemoryRegion *system_memory)
{
    /* With pc-0.12 and below we map both the PCI BAR and the fixed VBE region,
     * so use an alias to avoid double-mapping the same region.
     */
    memory_region_init_alias(&s->vram_vbe, obj, "vram.vbe",
                             &s->vram, 0, memory_region_size(&s->vram));
    /* XXX: use optimized standard vga accesses */
    memory_region_add_subregion(system_memory,
                                VBE_DISPI_LFB_PHYSICAL_ADDRESS,
                                &s->vram_vbe);
    s->vbe_mapped = 1;
}
