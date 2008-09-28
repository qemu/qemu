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
#include "hw.h"
#include "console.h"
#include "pc.h"
#include "pci.h"
#include "vga_int.h"
#include "pixel_ops.h"
#include "qemu-timer.h"

//#define DEBUG_VGA
//#define DEBUG_VGA_MEM
//#define DEBUG_VGA_REG

//#define DEBUG_BOCHS_VBE

/* force some bits to zero */
const uint8_t sr_mask[8] = {
    (uint8_t)~0xfc,
    (uint8_t)~0xc2,
    (uint8_t)~0xf0,
    (uint8_t)~0xc0,
    (uint8_t)~0xf1,
    (uint8_t)~0xff,
    (uint8_t)~0xff,
    (uint8_t)~0x00,
};

const uint8_t gr_mask[16] = {
    (uint8_t)~0xf0, /* 0x00 */
    (uint8_t)~0xf0, /* 0x01 */
    (uint8_t)~0xf0, /* 0x02 */
    (uint8_t)~0xe0, /* 0x03 */
    (uint8_t)~0xfc, /* 0x04 */
    (uint8_t)~0x84, /* 0x05 */
    (uint8_t)~0xf0, /* 0x06 */
    (uint8_t)~0xf0, /* 0x07 */
    (uint8_t)~0x00, /* 0x08 */
    (uint8_t)~0xff, /* 0x09 */
    (uint8_t)~0xff, /* 0x0a */
    (uint8_t)~0xff, /* 0x0b */
    (uint8_t)~0xff, /* 0x0c */
    (uint8_t)~0xff, /* 0x0d */
    (uint8_t)~0xff, /* 0x0e */
    (uint8_t)~0xff, /* 0x0f */
};

#define cbswap_32(__x) \
((uint32_t)( \
		(((uint32_t)(__x) & (uint32_t)0x000000ffUL) << 24) | \
		(((uint32_t)(__x) & (uint32_t)0x0000ff00UL) <<  8) | \
		(((uint32_t)(__x) & (uint32_t)0x00ff0000UL) >>  8) | \
		(((uint32_t)(__x) & (uint32_t)0xff000000UL) >> 24) ))

#ifdef WORDS_BIGENDIAN
#define PAT(x) cbswap_32(x)
#else
#define PAT(x) (x)
#endif

#ifdef WORDS_BIGENDIAN
#define BIG 1
#else
#define BIG 0
#endif

#ifdef WORDS_BIGENDIAN
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

#ifdef WORDS_BIGENDIAN
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

static void vga_screen_dump(void *opaque, const char *filename);

static void vga_dumb_update_retrace_info(VGAState *s)
{
    (void) s;
}

static void vga_precise_update_retrace_info(VGAState *s)
{
    int htotal_chars;
    int hretr_start_char;
    int hretr_skew_chars;
    int hretr_end_char;

    int vtotal_lines;
    int vretr_start_line;
    int vretr_end_line;

    int div2, sldiv2, dots;
    int clocking_mode;
    int clock_sel;
    const int hz[] = {25175000, 28322000, 25175000, 25175000};
    int64_t chars_per_sec;
    struct vga_precise_retrace *r = &s->retrace_info.precise;

    htotal_chars = s->cr[0x00] + 5;
    hretr_start_char = s->cr[0x04];
    hretr_skew_chars = (s->cr[0x05] >> 5) & 3;
    hretr_end_char = s->cr[0x05] & 0x1f;

    vtotal_lines = (s->cr[0x06]
                    | (((s->cr[0x07] & 1) | ((s->cr[0x07] >> 4) & 2)) << 8)) + 2
        ;
    vretr_start_line = s->cr[0x10]
        | ((((s->cr[0x07] >> 2) & 1) | ((s->cr[0x07] >> 6) & 2)) << 8)
        ;
    vretr_end_line = s->cr[0x11] & 0xf;


    div2 = (s->cr[0x17] >> 2) & 1;
    sldiv2 = (s->cr[0x17] >> 3) & 1;

    clocking_mode = (s->sr[0x01] >> 3) & 1;
    clock_sel = (s->msr >> 2) & 3;
    dots = (s->msr & 1) ? 8 : 9;

    chars_per_sec = hz[clock_sel] / dots;

    htotal_chars <<= clocking_mode;

    r->total_chars = vtotal_lines * htotal_chars;
    r->total_chars = (vretr_start_line + vretr_end_line + 1) * htotal_chars;
    if (r->freq) {
        r->ticks_per_char = ticks_per_sec / (r->total_chars * r->freq);
    } else {
        r->ticks_per_char = ticks_per_sec / chars_per_sec;
    }

    r->vstart = vretr_start_line;
    r->vend = r->vstart + vretr_end_line + 1;

    r->hstart = hretr_start_char + hretr_skew_chars;
    r->hend = r->hstart + hretr_end_char + 1;
    r->htotal = htotal_chars;

#if 0
    printf("hz=%f\n",
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
        "ticks/char = %lld\n"
        "\n",
        (double) ticks_per_sec / (r->ticks_per_char * r->total_chars),
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
        hz[clock_sel],
        dots,
        r->ticks_per_char
        );
#endif
}

static uint8_t vga_precise_retrace(VGAState *s)
{
    struct vga_precise_retrace *r = &s->retrace_info.precise;
    uint8_t val = s->st01 & ~(ST01_V_RETRACE | ST01_DISP_ENABLE);

    if (r->total_chars) {
        int cur_line, cur_line_char, cur_char;
        int64_t cur_tick;

        cur_tick = qemu_get_clock(vm_clock);

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

static uint8_t vga_dumb_retrace(VGAState *s)
{
    return s->st01 ^ (ST01_V_RETRACE | ST01_DISP_ENABLE);
}

static uint32_t vga_ioport_read(void *opaque, uint32_t addr)
{
    VGAState *s = opaque;
    int val, index;

    /* check port range access depending on color/monochrome mode */
    if ((addr >= 0x3b0 && addr <= 0x3bf && (s->msr & MSR_COLOR_EMULATION)) ||
        (addr >= 0x3d0 && addr <= 0x3df && !(s->msr & MSR_COLOR_EMULATION))) {
        val = 0xff;
    } else {
        switch(addr) {
        case 0x3c0:
            if (s->ar_flip_flop == 0) {
                val = s->ar_index;
            } else {
                val = 0;
            }
            break;
        case 0x3c1:
            index = s->ar_index & 0x1f;
            if (index < 21)
                val = s->ar[index];
            else
                val = 0;
            break;
        case 0x3c2:
            val = s->st00;
            break;
        case 0x3c4:
            val = s->sr_index;
            break;
        case 0x3c5:
            val = s->sr[s->sr_index];
#ifdef DEBUG_VGA_REG
            printf("vga: read SR%x = 0x%02x\n", s->sr_index, val);
#endif
            break;
        case 0x3c7:
            val = s->dac_state;
            break;
	case 0x3c8:
	    val = s->dac_write_index;
	    break;
        case 0x3c9:
            val = s->palette[s->dac_read_index * 3 + s->dac_sub_index];
            if (++s->dac_sub_index == 3) {
                s->dac_sub_index = 0;
                s->dac_read_index++;
            }
            break;
        case 0x3ca:
            val = s->fcr;
            break;
        case 0x3cc:
            val = s->msr;
            break;
        case 0x3ce:
            val = s->gr_index;
            break;
        case 0x3cf:
            val = s->gr[s->gr_index];
#ifdef DEBUG_VGA_REG
            printf("vga: read GR%x = 0x%02x\n", s->gr_index, val);
#endif
            break;
        case 0x3b4:
        case 0x3d4:
            val = s->cr_index;
            break;
        case 0x3b5:
        case 0x3d5:
            val = s->cr[s->cr_index];
#ifdef DEBUG_VGA_REG
            printf("vga: read CR%x = 0x%02x\n", s->cr_index, val);
#endif
            break;
        case 0x3ba:
        case 0x3da:
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

static void vga_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    VGAState *s = opaque;
    int index;

    /* check port range access depending on color/monochrome mode */
    if ((addr >= 0x3b0 && addr <= 0x3bf && (s->msr & MSR_COLOR_EMULATION)) ||
        (addr >= 0x3d0 && addr <= 0x3df && !(s->msr & MSR_COLOR_EMULATION)))
        return;

#ifdef DEBUG_VGA
    printf("VGA: write addr=0x%04x data=0x%02x\n", addr, val);
#endif

    switch(addr) {
    case 0x3c0:
        if (s->ar_flip_flop == 0) {
            val &= 0x3f;
            s->ar_index = val;
        } else {
            index = s->ar_index & 0x1f;
            switch(index) {
            case 0x00 ... 0x0f:
                s->ar[index] = val & 0x3f;
                break;
            case 0x10:
                s->ar[index] = val & ~0x10;
                break;
            case 0x11:
                s->ar[index] = val;
                break;
            case 0x12:
                s->ar[index] = val & ~0xc0;
                break;
            case 0x13:
                s->ar[index] = val & ~0xf0;
                break;
            case 0x14:
                s->ar[index] = val & ~0xf0;
                break;
            default:
                break;
            }
        }
        s->ar_flip_flop ^= 1;
        break;
    case 0x3c2:
        s->msr = val & ~0x10;
        s->update_retrace_info(s);
        break;
    case 0x3c4:
        s->sr_index = val & 7;
        break;
    case 0x3c5:
#ifdef DEBUG_VGA_REG
        printf("vga: write SR%x = 0x%02x\n", s->sr_index, val);
#endif
        s->sr[s->sr_index] = val & sr_mask[s->sr_index];
        if (s->sr_index == 1) s->update_retrace_info(s);
        break;
    case 0x3c7:
        s->dac_read_index = val;
        s->dac_sub_index = 0;
        s->dac_state = 3;
        break;
    case 0x3c8:
        s->dac_write_index = val;
        s->dac_sub_index = 0;
        s->dac_state = 0;
        break;
    case 0x3c9:
        s->dac_cache[s->dac_sub_index] = val;
        if (++s->dac_sub_index == 3) {
            memcpy(&s->palette[s->dac_write_index * 3], s->dac_cache, 3);
            s->dac_sub_index = 0;
            s->dac_write_index++;
        }
        break;
    case 0x3ce:
        s->gr_index = val & 0x0f;
        break;
    case 0x3cf:
#ifdef DEBUG_VGA_REG
        printf("vga: write GR%x = 0x%02x\n", s->gr_index, val);
#endif
        s->gr[s->gr_index] = val & gr_mask[s->gr_index];
        break;
    case 0x3b4:
    case 0x3d4:
        s->cr_index = val;
        break;
    case 0x3b5:
    case 0x3d5:
#ifdef DEBUG_VGA_REG
        printf("vga: write CR%x = 0x%02x\n", s->cr_index, val);
#endif
        /* handle CR0-7 protection */
        if ((s->cr[0x11] & 0x80) && s->cr_index <= 7) {
            /* can always write bit 4 of CR7 */
            if (s->cr_index == 7)
                s->cr[7] = (s->cr[7] & ~0x10) | (val & 0x10);
            return;
        }
        switch(s->cr_index) {
        case 0x01: /* horizontal display end */
        case 0x07:
        case 0x09:
        case 0x0c:
        case 0x0d:
        case 0x12: /* vertical display end */
            s->cr[s->cr_index] = val;
            break;
        default:
            s->cr[s->cr_index] = val;
            break;
        }

        switch(s->cr_index) {
        case 0x00:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x11:
        case 0x17:
            s->update_retrace_info(s);
            break;
        }
        break;
    case 0x3ba:
    case 0x3da:
        s->fcr = val & 0x10;
        break;
    }
}

#ifdef CONFIG_BOCHS_VBE
static uint32_t vbe_ioport_read_index(void *opaque, uint32_t addr)
{
    VGAState *s = opaque;
    uint32_t val;
    val = s->vbe_index;
    return val;
}

static uint32_t vbe_ioport_read_data(void *opaque, uint32_t addr)
{
    VGAState *s = opaque;
    uint32_t val;

    if (s->vbe_index <= VBE_DISPI_INDEX_NB) {
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
    } else {
        val = 0;
    }
#ifdef DEBUG_BOCHS_VBE
    printf("VBE: read index=0x%x val=0x%x\n", s->vbe_index, val);
#endif
    return val;
}

static void vbe_ioport_write_index(void *opaque, uint32_t addr, uint32_t val)
{
    VGAState *s = opaque;
    s->vbe_index = val;
}

static void vbe_ioport_write_data(void *opaque, uint32_t addr, uint32_t val)
{
    VGAState *s = opaque;

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
                s->gr[0x06] = (s->gr[0x06] & ~0x0c) | 0x05; /* graphic mode + memory map 1 */
                s->cr[0x17] |= 3; /* no CGA modes */
                s->cr[0x13] = s->vbe_line_offset >> 3;
                /* width */
                s->cr[0x01] = (s->vbe_regs[VBE_DISPI_INDEX_XRES] >> 3) - 1;
                /* height (only meaningful if < 1024) */
                h = s->vbe_regs[VBE_DISPI_INDEX_YRES] - 1;
                s->cr[0x12] = h;
                s->cr[0x07] = (s->cr[0x07] & ~0x42) |
                    ((h >> 7) & 0x02) | ((h >> 3) & 0x40);
                /* line compare to 1023 */
                s->cr[0x18] = 0xff;
                s->cr[0x07] |= 0x10;
                s->cr[0x09] |= 0x40;

                if (s->vbe_regs[VBE_DISPI_INDEX_BPP] == 4) {
                    shift_control = 0;
                    s->sr[0x01] &= ~8; /* no double line */
                } else {
                    shift_control = 2;
                    s->sr[4] |= 0x08; /* set chain 4 mode */
                    s->sr[2] |= 0x0f; /* activate all planes */
                }
                s->gr[0x05] = (s->gr[0x05] & ~0x60) | (shift_control << 5);
                s->cr[0x09] &= ~0x9f; /* no double scan */
            } else {
                /* XXX: the bios should do that */
                s->bank_offset = 0;
            }
            s->dac_8bit = (val & VBE_DISPI_8BIT_DAC) > 0;
            s->vbe_regs[s->vbe_index] = val;
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
#endif

/* called for accesses between 0xa0000 and 0xc0000 */
uint32_t vga_mem_readb(void *opaque, target_phys_addr_t addr)
{
    VGAState *s = opaque;
    int memory_map_mode, plane;
    uint32_t ret;

    /* convert to VGA memory offset */
    memory_map_mode = (s->gr[6] >> 2) & 3;
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

    if (s->sr[4] & 0x08) {
        /* chain 4 mode : simplest access */
        ret = s->vram_ptr[addr];
    } else if (s->gr[5] & 0x10) {
        /* odd/even mode (aka text mode mapping) */
        plane = (s->gr[4] & 2) | (addr & 1);
        ret = s->vram_ptr[((addr & ~1) << 1) | plane];
    } else {
        /* standard VGA latched access */
        s->latch = ((uint32_t *)s->vram_ptr)[addr];

        if (!(s->gr[5] & 0x08)) {
            /* read mode 0 */
            plane = s->gr[4];
            ret = GET_PLANE(s->latch, plane);
        } else {
            /* read mode 1 */
            ret = (s->latch ^ mask16[s->gr[2]]) & mask16[s->gr[7]];
            ret |= ret >> 16;
            ret |= ret >> 8;
            ret = (~ret) & 0xff;
        }
    }
    return ret;
}

static uint32_t vga_mem_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
#ifdef TARGET_WORDS_BIGENDIAN
    v = vga_mem_readb(opaque, addr) << 8;
    v |= vga_mem_readb(opaque, addr + 1);
#else
    v = vga_mem_readb(opaque, addr);
    v |= vga_mem_readb(opaque, addr + 1) << 8;
#endif
    return v;
}

static uint32_t vga_mem_readl(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
#ifdef TARGET_WORDS_BIGENDIAN
    v = vga_mem_readb(opaque, addr) << 24;
    v |= vga_mem_readb(opaque, addr + 1) << 16;
    v |= vga_mem_readb(opaque, addr + 2) << 8;
    v |= vga_mem_readb(opaque, addr + 3);
#else
    v = vga_mem_readb(opaque, addr);
    v |= vga_mem_readb(opaque, addr + 1) << 8;
    v |= vga_mem_readb(opaque, addr + 2) << 16;
    v |= vga_mem_readb(opaque, addr + 3) << 24;
#endif
    return v;
}

/* called for accesses between 0xa0000 and 0xc0000 */
void vga_mem_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    VGAState *s = opaque;
    int memory_map_mode, plane, write_mode, b, func_select, mask;
    uint32_t write_mask, bit_mask, set_mask;

#ifdef DEBUG_VGA_MEM
    printf("vga: [0x%x] = 0x%02x\n", addr, val);
#endif
    /* convert to VGA memory offset */
    memory_map_mode = (s->gr[6] >> 2) & 3;
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

    if (s->sr[4] & 0x08) {
        /* chain 4 mode : simplest access */
        plane = addr & 3;
        mask = (1 << plane);
        if (s->sr[2] & mask) {
            s->vram_ptr[addr] = val;
#ifdef DEBUG_VGA_MEM
            printf("vga: chain4: [0x%x]\n", addr);
#endif
            s->plane_updated |= mask; /* only used to detect font change */
            cpu_physical_memory_set_dirty(s->vram_offset + addr);
        }
    } else if (s->gr[5] & 0x10) {
        /* odd/even mode (aka text mode mapping) */
        plane = (s->gr[4] & 2) | (addr & 1);
        mask = (1 << plane);
        if (s->sr[2] & mask) {
            addr = ((addr & ~1) << 1) | plane;
            s->vram_ptr[addr] = val;
#ifdef DEBUG_VGA_MEM
            printf("vga: odd/even: [0x%x]\n", addr);
#endif
            s->plane_updated |= mask; /* only used to detect font change */
            cpu_physical_memory_set_dirty(s->vram_offset + addr);
        }
    } else {
        /* standard VGA latched access */
        write_mode = s->gr[5] & 3;
        switch(write_mode) {
        default:
        case 0:
            /* rotate */
            b = s->gr[3] & 7;
            val = ((val >> b) | (val << (8 - b))) & 0xff;
            val |= val << 8;
            val |= val << 16;

            /* apply set/reset mask */
            set_mask = mask16[s->gr[1]];
            val = (val & ~set_mask) | (mask16[s->gr[0]] & set_mask);
            bit_mask = s->gr[8];
            break;
        case 1:
            val = s->latch;
            goto do_write;
        case 2:
            val = mask16[val & 0x0f];
            bit_mask = s->gr[8];
            break;
        case 3:
            /* rotate */
            b = s->gr[3] & 7;
            val = (val >> b) | (val << (8 - b));

            bit_mask = s->gr[8] & val;
            val = mask16[s->gr[0]];
            break;
        }

        /* apply logical operation */
        func_select = s->gr[3] >> 3;
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
        mask = s->sr[2];
        s->plane_updated |= mask; /* only used to detect font change */
        write_mask = mask16[mask];
        ((uint32_t *)s->vram_ptr)[addr] =
            (((uint32_t *)s->vram_ptr)[addr] & ~write_mask) |
            (val & write_mask);
#ifdef DEBUG_VGA_MEM
            printf("vga: latch: [0x%x] mask=0x%08x val=0x%08x\n",
                   addr * 4, write_mask, val);
#endif
            cpu_physical_memory_set_dirty(s->vram_offset + (addr << 2));
    }
}

static void vga_mem_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
#ifdef TARGET_WORDS_BIGENDIAN
    vga_mem_writeb(opaque, addr, (val >> 8) & 0xff);
    vga_mem_writeb(opaque, addr + 1, val & 0xff);
#else
    vga_mem_writeb(opaque, addr, val & 0xff);
    vga_mem_writeb(opaque, addr + 1, (val >> 8) & 0xff);
#endif
}

static void vga_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
#ifdef TARGET_WORDS_BIGENDIAN
    vga_mem_writeb(opaque, addr, (val >> 24) & 0xff);
    vga_mem_writeb(opaque, addr + 1, (val >> 16) & 0xff);
    vga_mem_writeb(opaque, addr + 2, (val >> 8) & 0xff);
    vga_mem_writeb(opaque, addr + 3, val & 0xff);
#else
    vga_mem_writeb(opaque, addr, val & 0xff);
    vga_mem_writeb(opaque, addr + 1, (val >> 8) & 0xff);
    vga_mem_writeb(opaque, addr + 2, (val >> 16) & 0xff);
    vga_mem_writeb(opaque, addr + 3, (val >> 24) & 0xff);
#endif
}

typedef void vga_draw_glyph8_func(uint8_t *d, int linesize,
                             const uint8_t *font_ptr, int h,
                             uint32_t fgcol, uint32_t bgcol);
typedef void vga_draw_glyph9_func(uint8_t *d, int linesize,
                                  const uint8_t *font_ptr, int h,
                                  uint32_t fgcol, uint32_t bgcol, int dup9);
typedef void vga_draw_line_func(VGAState *s1, uint8_t *d,
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
static int update_palette16(VGAState *s)
{
    int full_update, i;
    uint32_t v, col, *palette;

    full_update = 0;
    palette = s->last_palette;
    for(i = 0; i < 16; i++) {
        v = s->ar[i];
        if (s->ar[0x10] & 0x80)
            v = ((s->ar[0x14] & 0xf) << 4) | (v & 0xf);
        else
            v = ((s->ar[0x14] & 0xc) << 4) | (v & 0x3f);
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
static int update_palette256(VGAState *s)
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

static void vga_get_offsets(VGAState *s,
                            uint32_t *pline_offset,
                            uint32_t *pstart_addr,
                            uint32_t *pline_compare)
{
    uint32_t start_addr, line_offset, line_compare;
#ifdef CONFIG_BOCHS_VBE
    if (s->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED) {
        line_offset = s->vbe_line_offset;
        start_addr = s->vbe_start_addr;
        line_compare = 65535;
    } else
#endif
    {
        /* compute line_offset in bytes */
        line_offset = s->cr[0x13];
        line_offset <<= 3;

        /* starting address */
        start_addr = s->cr[0x0d] | (s->cr[0x0c] << 8);

        /* line compare */
        line_compare = s->cr[0x18] |
            ((s->cr[0x07] & 0x10) << 4) |
            ((s->cr[0x09] & 0x40) << 3);
    }
    *pline_offset = line_offset;
    *pstart_addr = start_addr;
    *pline_compare = line_compare;
}

/* update start_addr and line_offset. Return TRUE if modified */
static int update_basic_params(VGAState *s)
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

static inline int get_depth_index(DisplayState *s)
{
    switch(s->depth) {
    default:
    case 8:
        return 0;
    case 15:
        if (s->bgr)
            return 5;
        else
            return 1;
    case 16:
        if (s->bgr)
            return 6;
        else
            return 2;
    case 32:
        if (s->bgr)
            return 4;
        else
            return 3;
    }
}

static vga_draw_glyph8_func *vga_draw_glyph8_table[NB_DEPTHS] = {
    vga_draw_glyph8_8,
    vga_draw_glyph8_16,
    vga_draw_glyph8_16,
    vga_draw_glyph8_32,
    vga_draw_glyph8_32,
    vga_draw_glyph8_16,
    vga_draw_glyph8_16,
};

static vga_draw_glyph8_func *vga_draw_glyph16_table[NB_DEPTHS] = {
    vga_draw_glyph16_8,
    vga_draw_glyph16_16,
    vga_draw_glyph16_16,
    vga_draw_glyph16_32,
    vga_draw_glyph16_32,
    vga_draw_glyph16_16,
    vga_draw_glyph16_16,
};

static vga_draw_glyph9_func *vga_draw_glyph9_table[NB_DEPTHS] = {
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

/*
 * Text mode update
 * Missing:
 * - double scan
 * - double width
 * - underline
 * - flashing
 */
static void vga_draw_text(VGAState *s, int full_update)
{
    int cx, cy, cheight, cw, ch, cattr, height, width, ch_attr;
    int cx_min, cx_max, linesize, x_incr;
    uint32_t offset, fgcol, bgcol, v, cursor_offset;
    uint8_t *d1, *d, *src, *s1, *dest, *cursor_ptr;
    const uint8_t *font_ptr, *font_base[2];
    int dup9, line_offset, depth_index;
    uint32_t *palette;
    uint32_t *ch_attr_ptr;
    vga_draw_glyph8_func *vga_draw_glyph8;
    vga_draw_glyph9_func *vga_draw_glyph9;

    full_update |= update_palette16(s);
    palette = s->last_palette;

    /* compute font data address (in plane 2) */
    v = s->sr[3];
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
    if (s->plane_updated & (1 << 2)) {
        /* if the plane 2 was modified since the last display, it
           indicates the font may have been modified */
        s->plane_updated = 0;
        full_update = 1;
    }
    full_update |= update_basic_params(s);

    line_offset = s->line_offset;
    s1 = s->vram_ptr + (s->start_addr * 4);

    /* total width & height */
    cheight = (s->cr[9] & 0x1f) + 1;
    cw = 8;
    if (!(s->sr[1] & 0x01))
        cw = 9;
    if (s->sr[1] & 0x08)
        cw = 16; /* NOTE: no 18 pixel wide */
    x_incr = cw * ((s->ds->depth + 7) >> 3);
    width = (s->cr[0x01] + 1);
    if (s->cr[0x06] == 100) {
        /* ugly hack for CGA 160x100x16 - explain me the logic */
        height = 100;
    } else {
        height = s->cr[0x12] |
            ((s->cr[0x07] & 0x02) << 7) |
            ((s->cr[0x07] & 0x40) << 3);
        height = (height + 1) / cheight;
    }
    if ((height * width) > CH_ATTR_SIZE) {
        /* better than nothing: exit if transient size is too big */
        return;
    }

    if (width != s->last_width || height != s->last_height ||
        cw != s->last_cw || cheight != s->last_ch) {
        s->last_scr_width = width * cw;
        s->last_scr_height = height * cheight;
        qemu_console_resize(s->console, s->last_scr_width, s->last_scr_height);
        s->last_width = width;
        s->last_height = height;
        s->last_ch = cheight;
        s->last_cw = cw;
        full_update = 1;
    }
    cursor_offset = ((s->cr[0x0e] << 8) | s->cr[0x0f]) - s->start_addr;
    if (cursor_offset != s->cursor_offset ||
        s->cr[0xa] != s->cursor_start ||
        s->cr[0xb] != s->cursor_end) {
      /* if the cursor position changed, we update the old and new
         chars */
        if (s->cursor_offset < CH_ATTR_SIZE)
            s->last_ch_attr[s->cursor_offset] = -1;
        if (cursor_offset < CH_ATTR_SIZE)
            s->last_ch_attr[cursor_offset] = -1;
        s->cursor_offset = cursor_offset;
        s->cursor_start = s->cr[0xa];
        s->cursor_end = s->cr[0xb];
    }
    cursor_ptr = s->vram_ptr + (s->start_addr + cursor_offset) * 4;

    depth_index = get_depth_index(s->ds);
    if (cw == 16)
        vga_draw_glyph8 = vga_draw_glyph16_table[depth_index];
    else
        vga_draw_glyph8 = vga_draw_glyph8_table[depth_index];
    vga_draw_glyph9 = vga_draw_glyph9_table[depth_index];

    dest = s->ds->data;
    linesize = s->ds->linesize;
    ch_attr_ptr = s->last_ch_attr;
    for(cy = 0; cy < height; cy++) {
        d1 = dest;
        src = s1;
        cx_min = width;
        cx_max = -1;
        for(cx = 0; cx < width; cx++) {
            ch_attr = *(uint16_t *)src;
            if (full_update || ch_attr != *ch_attr_ptr) {
                if (cx < cx_min)
                    cx_min = cx;
                if (cx > cx_max)
                    cx_max = cx;
                *ch_attr_ptr = ch_attr;
#ifdef WORDS_BIGENDIAN
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
                    if (ch >= 0xb0 && ch <= 0xdf && (s->ar[0x10] & 0x04))
                        dup9 = 1;
                    vga_draw_glyph9(d1, linesize,
                                    font_ptr, cheight, fgcol, bgcol, dup9);
                }
                if (src == cursor_ptr &&
                    !(s->cr[0x0a] & 0x20)) {
                    int line_start, line_last, h;
                    /* draw the cursor */
                    line_start = s->cr[0x0a] & 0x1f;
                    line_last = s->cr[0x0b] & 0x1f;
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
            dpy_update(s->ds, cx_min * cw, cy * cheight,
                       (cx_max - cx_min + 1) * cw, cheight);
        }
        dest += linesize * cheight;
        s1 += line_offset;
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

static vga_draw_line_func *vga_draw_line_table[NB_DEPTHS * VGA_DRAW_LINE_NB] = {
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

typedef unsigned int rgb_to_pixel_dup_func(unsigned int r, unsigned int g, unsigned b);

static rgb_to_pixel_dup_func *rgb_to_pixel_dup_table[NB_DEPTHS] = {
    rgb_to_pixel8_dup,
    rgb_to_pixel15_dup,
    rgb_to_pixel16_dup,
    rgb_to_pixel32_dup,
    rgb_to_pixel32bgr_dup,
    rgb_to_pixel15bgr_dup,
    rgb_to_pixel16bgr_dup,
};

static int vga_get_bpp(VGAState *s)
{
    int ret;
#ifdef CONFIG_BOCHS_VBE
    if (s->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED) {
        ret = s->vbe_regs[VBE_DISPI_INDEX_BPP];
    } else
#endif
    {
        ret = 0;
    }
    return ret;
}

static void vga_get_resolution(VGAState *s, int *pwidth, int *pheight)
{
    int width, height;

#ifdef CONFIG_BOCHS_VBE
    if (s->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED) {
        width = s->vbe_regs[VBE_DISPI_INDEX_XRES];
        height = s->vbe_regs[VBE_DISPI_INDEX_YRES];
    } else
#endif
    {
        width = (s->cr[0x01] + 1) * 8;
        height = s->cr[0x12] |
            ((s->cr[0x07] & 0x02) << 7) |
            ((s->cr[0x07] & 0x40) << 3);
        height = (height + 1);
    }
    *pwidth = width;
    *pheight = height;
}

void vga_invalidate_scanlines(VGAState *s, int y1, int y2)
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

/*
 * graphic modes
 */
static void vga_draw_graphic(VGAState *s, int full_update)
{
    int y1, y, update, page_min, page_max, linesize, y_start, double_scan, mask;
    int width, height, shift_control, line_offset, page0, page1, bwidth, bits;
    int disp_width, multi_scan, multi_run;
    uint8_t *d;
    uint32_t v, addr1, addr;
    vga_draw_line_func *vga_draw_line;

    full_update |= update_basic_params(s);

    s->get_resolution(s, &width, &height);
    disp_width = width;

    shift_control = (s->gr[0x05] >> 5) & 3;
    double_scan = (s->cr[0x09] >> 7);
    if (shift_control != 1) {
        multi_scan = (((s->cr[0x09] & 0x1f) + 1) << double_scan) - 1;
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
        full_update |= update_palette16(s);
        if (s->sr[0x01] & 8) {
            v = VGA_DRAW_LINE4D2;
            disp_width <<= 1;
        } else {
            v = VGA_DRAW_LINE4;
        }
        bits = 4;
    } else if (shift_control == 1) {
        full_update |= update_palette16(s);
        if (s->sr[0x01] & 8) {
            v = VGA_DRAW_LINE2D2;
            disp_width <<= 1;
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
    vga_draw_line = vga_draw_line_table[v * NB_DEPTHS + get_depth_index(s->ds)];

    if (disp_width != s->last_width ||
        height != s->last_height) {
        qemu_console_resize(s->console, disp_width, height);
        s->last_scr_width = disp_width;
        s->last_scr_height = height;
        s->last_width = disp_width;
        s->last_height = height;
        full_update = 1;
    }
    if (s->cursor_invalidate)
        s->cursor_invalidate(s);

    line_offset = s->line_offset;
#if 0
    printf("w=%d h=%d v=%d line_offset=%d cr[0x09]=0x%02x cr[0x17]=0x%02x linecmp=%d sr[0x01]=0x%02x\n",
           width, height, v, line_offset, s->cr[9], s->cr[0x17], s->line_compare, s->sr[0x01]);
#endif
    addr1 = (s->start_addr * 4);
    bwidth = (width * bits + 7) / 8;
    y_start = -1;
    page_min = 0x7fffffff;
    page_max = -1;
    d = s->ds->data;
    linesize = s->ds->linesize;
    y1 = 0;
    for(y = 0; y < height; y++) {
        addr = addr1;
        if (!(s->cr[0x17] & 1)) {
            int shift;
            /* CGA compatibility handling */
            shift = 14 + ((s->cr[0x17] >> 6) & 1);
            addr = (addr & ~(1 << shift)) | ((y1 & 1) << shift);
        }
        if (!(s->cr[0x17] & 2)) {
            addr = (addr & ~0x8000) | ((y1 & 2) << 14);
        }
        page0 = s->vram_offset + (addr & TARGET_PAGE_MASK);
        page1 = s->vram_offset + ((addr + bwidth - 1) & TARGET_PAGE_MASK);
        update = full_update |
            cpu_physical_memory_get_dirty(page0, VGA_DIRTY_FLAG) |
            cpu_physical_memory_get_dirty(page1, VGA_DIRTY_FLAG);
        if ((page1 - page0) > TARGET_PAGE_SIZE) {
            /* if wide line, can use another page */
            update |= cpu_physical_memory_get_dirty(page0 + TARGET_PAGE_SIZE,
                                                    VGA_DIRTY_FLAG);
        }
        /* explicit invalidation for the hardware cursor */
        update |= (s->invalidated_y_table[y >> 5] >> (y & 0x1f)) & 1;
        if (update) {
            if (y_start < 0)
                y_start = y;
            if (page0 < page_min)
                page_min = page0;
            if (page1 > page_max)
                page_max = page1;
            vga_draw_line(s, d, s->vram_ptr + addr, width);
            if (s->cursor_draw_line)
                s->cursor_draw_line(s, d, y);
        } else {
            if (y_start >= 0) {
                /* flush to display */
                dpy_update(s->ds, 0, y_start,
                           disp_width, y - y_start);
                y_start = -1;
            }
        }
        if (!multi_run) {
            mask = (s->cr[0x17] & 3) ^ 3;
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
        dpy_update(s->ds, 0, y_start,
                   disp_width, y - y_start);
    }
    /* reset modified pages */
    if (page_max != -1) {
        cpu_physical_memory_reset_dirty(page_min, page_max + TARGET_PAGE_SIZE,
                                        VGA_DIRTY_FLAG);
    }
    memset(s->invalidated_y_table, 0, ((height + 31) >> 5) * 4);
}

static void vga_draw_blank(VGAState *s, int full_update)
{
    int i, w, val;
    uint8_t *d;

    if (!full_update)
        return;
    if (s->last_scr_width <= 0 || s->last_scr_height <= 0)
        return;
    if (s->ds->depth == 8)
        val = s->rgb_to_pixel(0, 0, 0);
    else
        val = 0;
    w = s->last_scr_width * ((s->ds->depth + 7) >> 3);
    d = s->ds->data;
    for(i = 0; i < s->last_scr_height; i++) {
        memset(d, val, w);
        d += s->ds->linesize;
    }
    dpy_update(s->ds, 0, 0,
               s->last_scr_width, s->last_scr_height);
}

#define GMODE_TEXT     0
#define GMODE_GRAPH    1
#define GMODE_BLANK 2

static void vga_update_display(void *opaque)
{
    VGAState *s = (VGAState *)opaque;
    int full_update, graphic_mode;

    if (s->ds->depth == 0) {
        /* nothing to do */
    } else {
        s->rgb_to_pixel =
            rgb_to_pixel_dup_table[get_depth_index(s->ds)];

        full_update = 0;
        if (!(s->ar_index & 0x20)) {
            graphic_mode = GMODE_BLANK;
        } else {
            graphic_mode = s->gr[6] & 1;
        }
        if (graphic_mode != s->graphic_mode) {
            s->graphic_mode = graphic_mode;
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
    VGAState *s = (VGAState *)opaque;

    s->last_width = -1;
    s->last_height = -1;
}

static void vga_reset(VGAState *s)
{
    memset(s, 0, sizeof(VGAState));
    s->graphic_mode = -1; /* force full update */
}

#define TEXTMODE_X(x)	((x) % width)
#define TEXTMODE_Y(x)	((x) / width)
#define VMEM2CHTYPE(v)	((v & 0xff0007ff) | \
        ((v & 0x00000800) << 10) | ((v & 0x00007000) >> 1))
/* relay text rendering to the display driver
 * instead of doing a full vga_update_display() */
static void vga_update_text(void *opaque, console_ch_t *chardata)
{
    VGAState *s = (VGAState *) opaque;
    int graphic_mode, i, cursor_offset, cursor_visible;
    int cw, cheight, width, height, size, c_min, c_max;
    uint32_t *src;
    console_ch_t *dst, val;
    char msg_buffer[80];
    int full_update = 0;

    if (!(s->ar_index & 0x20)) {
        graphic_mode = GMODE_BLANK;
    } else {
        graphic_mode = s->gr[6] & 1;
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
        cheight = (s->cr[9] & 0x1f) + 1;
        cw = 8;
        if (!(s->sr[1] & 0x01))
            cw = 9;
        if (s->sr[1] & 0x08)
            cw = 16; /* NOTE: no 18 pixel wide */
        width = (s->cr[0x01] + 1);
        if (s->cr[0x06] == 100) {
            /* ugly hack for CGA 160x100x16 - explain me the logic */
            height = 100;
        } else {
            height = s->cr[0x12] | 
                ((s->cr[0x07] & 0x02) << 7) | 
                ((s->cr[0x07] & 0x40) << 3);
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
            qemu_console_resize(s->console, width, height);
            s->last_width = width;
            s->last_height = height;
            s->last_ch = cheight;
            s->last_cw = cw;
            full_update = 1;
        }

        /* Update "hardware" cursor */
        cursor_offset = ((s->cr[0x0e] << 8) | s->cr[0x0f]) - s->start_addr;
        if (cursor_offset != s->cursor_offset ||
            s->cr[0xa] != s->cursor_start ||
            s->cr[0xb] != s->cursor_end || full_update) {
            cursor_visible = !(s->cr[0xa] & 0x20);
            if (cursor_visible && cursor_offset < size && cursor_offset >= 0)
                dpy_cursor(s->ds,
                           TEXTMODE_X(cursor_offset),
                           TEXTMODE_Y(cursor_offset));
            else
                dpy_cursor(s->ds, -1, -1);
            s->cursor_offset = cursor_offset;
            s->cursor_start = s->cr[0xa];
            s->cursor_end = s->cr[0xb];
        }

        src = (uint32_t *) s->vram_ptr + s->start_addr;
        dst = chardata;

        if (full_update) {
            for (i = 0; i < size; src ++, dst ++, i ++)
                console_write_ch(dst, VMEM2CHTYPE(*src));

            dpy_update(s->ds, 0, 0, width, height);
        } else {
            c_max = 0;

            for (i = 0; i < size; src ++, dst ++, i ++) {
                console_write_ch(&val, VMEM2CHTYPE(*src));
                if (*dst != val) {
                    *dst = val;
                    c_max = i;
                    break;
                }
            }
            c_min = i;
            for (; i < size; src ++, dst ++, i ++) {
                console_write_ch(&val, VMEM2CHTYPE(*src));
                if (*dst != val) {
                    *dst = val;
                    c_max = i;
                }
            }

            if (c_min <= c_max) {
                i = TEXTMODE_Y(c_min);
                dpy_update(s->ds, 0, i, width, TEXTMODE_Y(c_max) - i + 1);
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
    dpy_cursor(s->ds, -1, -1);
    qemu_console_resize(s->console, s->last_width, height);

    for (dst = chardata, i = 0; i < s->last_width * height; i ++)
        console_write_ch(dst ++, ' ');

    size = strlen(msg_buffer);
    width = (s->last_width - size) / 2;
    dst = chardata + s->last_width + width;
    for (i = 0; i < size; i ++)
        console_write_ch(dst ++, 0x00200100 | msg_buffer[i]);

    dpy_update(s->ds, 0, 0, s->last_width, height);
}

static CPUReadMemoryFunc *vga_mem_read[3] = {
    vga_mem_readb,
    vga_mem_readw,
    vga_mem_readl,
};

static CPUWriteMemoryFunc *vga_mem_write[3] = {
    vga_mem_writeb,
    vga_mem_writew,
    vga_mem_writel,
};

static void vga_save(QEMUFile *f, void *opaque)
{
    VGAState *s = opaque;
    int i;

    if (s->pci_dev)
        pci_device_save(s->pci_dev, f);

    qemu_put_be32s(f, &s->latch);
    qemu_put_8s(f, &s->sr_index);
    qemu_put_buffer(f, s->sr, 8);
    qemu_put_8s(f, &s->gr_index);
    qemu_put_buffer(f, s->gr, 16);
    qemu_put_8s(f, &s->ar_index);
    qemu_put_buffer(f, s->ar, 21);
    qemu_put_be32(f, s->ar_flip_flop);
    qemu_put_8s(f, &s->cr_index);
    qemu_put_buffer(f, s->cr, 256);
    qemu_put_8s(f, &s->msr);
    qemu_put_8s(f, &s->fcr);
    qemu_put_byte(f, s->st00);
    qemu_put_8s(f, &s->st01);

    qemu_put_8s(f, &s->dac_state);
    qemu_put_8s(f, &s->dac_sub_index);
    qemu_put_8s(f, &s->dac_read_index);
    qemu_put_8s(f, &s->dac_write_index);
    qemu_put_buffer(f, s->dac_cache, 3);
    qemu_put_buffer(f, s->palette, 768);

    qemu_put_be32(f, s->bank_offset);
#ifdef CONFIG_BOCHS_VBE
    qemu_put_byte(f, 1);
    qemu_put_be16s(f, &s->vbe_index);
    for(i = 0; i < VBE_DISPI_INDEX_NB; i++)
        qemu_put_be16s(f, &s->vbe_regs[i]);
    qemu_put_be32s(f, &s->vbe_start_addr);
    qemu_put_be32s(f, &s->vbe_line_offset);
    qemu_put_be32s(f, &s->vbe_bank_mask);
#else
    qemu_put_byte(f, 0);
#endif
}

static int vga_load(QEMUFile *f, void *opaque, int version_id)
{
    VGAState *s = opaque;
    int is_vbe, i, ret;

    if (version_id > 2)
        return -EINVAL;

    if (s->pci_dev && version_id >= 2) {
        ret = pci_device_load(s->pci_dev, f);
        if (ret < 0)
            return ret;
    }

    qemu_get_be32s(f, &s->latch);
    qemu_get_8s(f, &s->sr_index);
    qemu_get_buffer(f, s->sr, 8);
    qemu_get_8s(f, &s->gr_index);
    qemu_get_buffer(f, s->gr, 16);
    qemu_get_8s(f, &s->ar_index);
    qemu_get_buffer(f, s->ar, 21);
    s->ar_flip_flop=qemu_get_be32(f);
    qemu_get_8s(f, &s->cr_index);
    qemu_get_buffer(f, s->cr, 256);
    qemu_get_8s(f, &s->msr);
    qemu_get_8s(f, &s->fcr);
    qemu_get_8s(f, &s->st00);
    qemu_get_8s(f, &s->st01);

    qemu_get_8s(f, &s->dac_state);
    qemu_get_8s(f, &s->dac_sub_index);
    qemu_get_8s(f, &s->dac_read_index);
    qemu_get_8s(f, &s->dac_write_index);
    qemu_get_buffer(f, s->dac_cache, 3);
    qemu_get_buffer(f, s->palette, 768);

    s->bank_offset=qemu_get_be32(f);
    is_vbe = qemu_get_byte(f);
#ifdef CONFIG_BOCHS_VBE
    if (!is_vbe)
        return -EINVAL;
    qemu_get_be16s(f, &s->vbe_index);
    for(i = 0; i < VBE_DISPI_INDEX_NB; i++)
        qemu_get_be16s(f, &s->vbe_regs[i]);
    qemu_get_be32s(f, &s->vbe_start_addr);
    qemu_get_be32s(f, &s->vbe_line_offset);
    qemu_get_be32s(f, &s->vbe_bank_mask);
#else
    if (is_vbe)
        return -EINVAL;
#endif

    /* force refresh */
    s->graphic_mode = -1;
    return 0;
}

typedef struct PCIVGAState {
    PCIDevice dev;
    VGAState vga_state;
} PCIVGAState;

static void vga_map(PCIDevice *pci_dev, int region_num,
                    uint32_t addr, uint32_t size, int type)
{
    PCIVGAState *d = (PCIVGAState *)pci_dev;
    VGAState *s = &d->vga_state;
    if (region_num == PCI_ROM_SLOT) {
        cpu_register_physical_memory(addr, s->bios_size, s->bios_offset);
    } else {
        cpu_register_physical_memory(addr, s->vram_size, s->vram_offset);
    }
}

void vga_common_init(VGAState *s, DisplayState *ds, uint8_t *vga_ram_base,
                     unsigned long vga_ram_offset, int vga_ram_size)
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

    vga_reset(s);

    s->vram_ptr = vga_ram_base;
    s->vram_offset = vga_ram_offset;
    s->vram_size = vga_ram_size;
    s->ds = ds;
    s->get_bpp = vga_get_bpp;
    s->get_offsets = vga_get_offsets;
    s->get_resolution = vga_get_resolution;
    s->update = vga_update_display;
    s->invalidate = vga_invalidate_display;
    s->screen_dump = vga_screen_dump;
    s->text_update = vga_update_text;
    switch (vga_retrace_method) {
    case VGA_RETRACE_DUMB:
        s->retrace = vga_dumb_retrace;
        s->update_retrace_info = vga_dumb_update_retrace_info;
        break;

    case VGA_RETRACE_PRECISE:
        s->retrace = vga_precise_retrace;
        s->update_retrace_info = vga_precise_update_retrace_info;
        memset(&s->retrace_info, 0, sizeof (s->retrace_info));
        break;
    }
}

/* used by both ISA and PCI */
void vga_init(VGAState *s)
{
    int vga_io_memory;

    register_savevm("vga", 0, 2, vga_save, vga_load, s);

    register_ioport_write(0x3c0, 16, 1, vga_ioport_write, s);

    register_ioport_write(0x3b4, 2, 1, vga_ioport_write, s);
    register_ioport_write(0x3d4, 2, 1, vga_ioport_write, s);
    register_ioport_write(0x3ba, 1, 1, vga_ioport_write, s);
    register_ioport_write(0x3da, 1, 1, vga_ioport_write, s);

    register_ioport_read(0x3c0, 16, 1, vga_ioport_read, s);

    register_ioport_read(0x3b4, 2, 1, vga_ioport_read, s);
    register_ioport_read(0x3d4, 2, 1, vga_ioport_read, s);
    register_ioport_read(0x3ba, 1, 1, vga_ioport_read, s);
    register_ioport_read(0x3da, 1, 1, vga_ioport_read, s);
    s->bank_offset = 0;

#ifdef CONFIG_BOCHS_VBE
    s->vbe_regs[VBE_DISPI_INDEX_ID] = VBE_DISPI_ID0;
    s->vbe_bank_mask = ((s->vram_size >> 16) - 1);
#if defined (TARGET_I386)
    register_ioport_read(0x1ce, 1, 2, vbe_ioport_read_index, s);
    register_ioport_read(0x1cf, 1, 2, vbe_ioport_read_data, s);

    register_ioport_write(0x1ce, 1, 2, vbe_ioport_write_index, s);
    register_ioport_write(0x1cf, 1, 2, vbe_ioport_write_data, s);

    /* old Bochs IO ports */
    register_ioport_read(0xff80, 1, 2, vbe_ioport_read_index, s);
    register_ioport_read(0xff81, 1, 2, vbe_ioport_read_data, s);

    register_ioport_write(0xff80, 1, 2, vbe_ioport_write_index, s);
    register_ioport_write(0xff81, 1, 2, vbe_ioport_write_data, s);
#else
    register_ioport_read(0x1ce, 1, 2, vbe_ioport_read_index, s);
    register_ioport_read(0x1d0, 1, 2, vbe_ioport_read_data, s);

    register_ioport_write(0x1ce, 1, 2, vbe_ioport_write_index, s);
    register_ioport_write(0x1d0, 1, 2, vbe_ioport_write_data, s);
#endif
#endif /* CONFIG_BOCHS_VBE */

    vga_io_memory = cpu_register_io_memory(0, vga_mem_read, vga_mem_write, s);
    cpu_register_physical_memory(isa_mem_base + 0x000a0000, 0x20000,
                                 vga_io_memory);
}

/* Memory mapped interface */
static uint32_t vga_mm_readb (void *opaque, target_phys_addr_t addr)
{
    VGAState *s = opaque;

    return vga_ioport_read(s, (addr - s->base_ctrl) >> s->it_shift) & 0xff;
}

static void vga_mm_writeb (void *opaque,
                           target_phys_addr_t addr, uint32_t value)
{
    VGAState *s = opaque;

    vga_ioport_write(s, (addr - s->base_ctrl) >> s->it_shift, value & 0xff);
}

static uint32_t vga_mm_readw (void *opaque, target_phys_addr_t addr)
{
    VGAState *s = opaque;

    return vga_ioport_read(s, (addr - s->base_ctrl) >> s->it_shift) & 0xffff;
}

static void vga_mm_writew (void *opaque,
                           target_phys_addr_t addr, uint32_t value)
{
    VGAState *s = opaque;

    vga_ioport_write(s, (addr - s->base_ctrl) >> s->it_shift, value & 0xffff);
}

static uint32_t vga_mm_readl (void *opaque, target_phys_addr_t addr)
{
    VGAState *s = opaque;

    return vga_ioport_read(s, (addr - s->base_ctrl) >> s->it_shift);
}

static void vga_mm_writel (void *opaque,
                           target_phys_addr_t addr, uint32_t value)
{
    VGAState *s = opaque;

    vga_ioport_write(s, (addr - s->base_ctrl) >> s->it_shift, value);
}

static CPUReadMemoryFunc *vga_mm_read_ctrl[] = {
    &vga_mm_readb,
    &vga_mm_readw,
    &vga_mm_readl,
};

static CPUWriteMemoryFunc *vga_mm_write_ctrl[] = {
    &vga_mm_writeb,
    &vga_mm_writew,
    &vga_mm_writel,
};

static void vga_mm_init(VGAState *s, target_phys_addr_t vram_base,
                        target_phys_addr_t ctrl_base, int it_shift)
{
    int s_ioport_ctrl, vga_io_memory;

    s->base_ctrl = ctrl_base;
    s->it_shift = it_shift;
    s_ioport_ctrl = cpu_register_io_memory(0, vga_mm_read_ctrl, vga_mm_write_ctrl, s);
    vga_io_memory = cpu_register_io_memory(0, vga_mem_read, vga_mem_write, s);

    register_savevm("vga", 0, 2, vga_save, vga_load, s);

    cpu_register_physical_memory(ctrl_base, 0x100000, s_ioport_ctrl);
    s->bank_offset = 0;
    cpu_register_physical_memory(vram_base + 0x000a0000, 0x20000, vga_io_memory);
}

int isa_vga_init(DisplayState *ds, uint8_t *vga_ram_base,
                 unsigned long vga_ram_offset, int vga_ram_size)
{
    VGAState *s;

    s = qemu_mallocz(sizeof(VGAState));
    if (!s)
        return -1;

    vga_common_init(s, ds, vga_ram_base, vga_ram_offset, vga_ram_size);
    vga_init(s);

    s->console = graphic_console_init(s->ds, s->update, s->invalidate,
                                      s->screen_dump, s->text_update, s);

#ifdef CONFIG_BOCHS_VBE
    /* XXX: use optimized standard vga accesses */
    cpu_register_physical_memory(VBE_DISPI_LFB_PHYSICAL_ADDRESS,
                                 vga_ram_size, vga_ram_offset);
#endif
    return 0;
}

int isa_vga_mm_init(DisplayState *ds, uint8_t *vga_ram_base,
                    unsigned long vga_ram_offset, int vga_ram_size,
                    target_phys_addr_t vram_base, target_phys_addr_t ctrl_base,
                    int it_shift)
{
    VGAState *s;

    s = qemu_mallocz(sizeof(VGAState));
    if (!s)
        return -1;

    vga_common_init(s, ds, vga_ram_base, vga_ram_offset, vga_ram_size);
    vga_mm_init(s, vram_base, ctrl_base, it_shift);

    s->console = graphic_console_init(s->ds, s->update, s->invalidate,
                                      s->screen_dump, s->text_update, s);

#ifdef CONFIG_BOCHS_VBE
    /* XXX: use optimized standard vga accesses */
    cpu_register_physical_memory(VBE_DISPI_LFB_PHYSICAL_ADDRESS,
                                 vga_ram_size, vga_ram_offset);
#endif
    return 0;
}

int pci_vga_init(PCIBus *bus, DisplayState *ds, uint8_t *vga_ram_base,
                 unsigned long vga_ram_offset, int vga_ram_size,
                 unsigned long vga_bios_offset, int vga_bios_size)
{
    PCIVGAState *d;
    VGAState *s;
    uint8_t *pci_conf;

    d = (PCIVGAState *)pci_register_device(bus, "VGA",
                                           sizeof(PCIVGAState),
                                           -1, NULL, NULL);
    if (!d)
        return -1;
    s = &d->vga_state;

    vga_common_init(s, ds, vga_ram_base, vga_ram_offset, vga_ram_size);
    vga_init(s);

    s->console = graphic_console_init(s->ds, s->update, s->invalidate,
                                      s->screen_dump, s->text_update, s);

    s->pci_dev = &d->dev;

    pci_conf = d->dev.config;
    pci_conf[0x00] = 0x34; // dummy VGA (same as Bochs ID)
    pci_conf[0x01] = 0x12;
    pci_conf[0x02] = 0x11;
    pci_conf[0x03] = 0x11;
    pci_conf[0x0a] = 0x00; // VGA controller
    pci_conf[0x0b] = 0x03;
    pci_conf[0x0e] = 0x00; // header_type

    /* XXX: vga_ram_size must be a power of two */
    pci_register_io_region(&d->dev, 0, vga_ram_size,
                           PCI_ADDRESS_SPACE_MEM_PREFETCH, vga_map);
    if (vga_bios_size != 0) {
        unsigned int bios_total_size;
        s->bios_offset = vga_bios_offset;
        s->bios_size = vga_bios_size;
        /* must be a power of two */
        bios_total_size = 1;
        while (bios_total_size < vga_bios_size)
            bios_total_size <<= 1;
        pci_register_io_region(&d->dev, PCI_ROM_SLOT, bios_total_size,
                               PCI_ADDRESS_SPACE_MEM_PREFETCH, vga_map);
    }
    return 0;
}

/********************************************************/
/* vga screen dump */

static int vga_save_w, vga_save_h;

static void vga_save_dpy_update(DisplayState *s,
                                int x, int y, int w, int h)
{
}

static void vga_save_dpy_resize(DisplayState *s, int w, int h)
{
    s->linesize = w * 4;
    s->data = qemu_malloc(h * s->linesize);
    vga_save_w = w;
    vga_save_h = h;
}

static void vga_save_dpy_refresh(DisplayState *s)
{
}

int ppm_save(const char *filename, uint8_t *data,
             int w, int h, int linesize)
{
    FILE *f;
    uint8_t *d, *d1;
    unsigned int v;
    int y, x;

    f = fopen(filename, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n%d\n",
            w, h, 255);
    d1 = data;
    for(y = 0; y < h; y++) {
        d = d1;
        for(x = 0; x < w; x++) {
            v = *(uint32_t *)d;
            fputc((v >> 16) & 0xff, f);
            fputc((v >> 8) & 0xff, f);
            fputc((v) & 0xff, f);
            d += 4;
        }
        d1 += linesize;
    }
    fclose(f);
    return 0;
}

/* save the vga display in a PPM image even if no display is
   available */
static void vga_screen_dump(void *opaque, const char *filename)
{
    VGAState *s = (VGAState *)opaque;
    DisplayState *saved_ds, ds1, *ds = &ds1;

    /* XXX: this is a little hackish */
    vga_invalidate_display(s);
    saved_ds = s->ds;

    memset(ds, 0, sizeof(DisplayState));
    ds->dpy_update = vga_save_dpy_update;
    ds->dpy_resize = vga_save_dpy_resize;
    ds->dpy_refresh = vga_save_dpy_refresh;
    ds->depth = 32;

    s->ds = ds;
    s->graphic_mode = -1;
    vga_update_display(s);

    if (ds->data) {
        ppm_save(filename, ds->data, vga_save_w, vga_save_h,
                 s->ds->linesize);
        qemu_free(ds->data);
    }
    s->ds = saved_ds;
}
