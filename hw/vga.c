/*
 * QEMU VGA Emulator. An S3 86c968 is emulated
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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <malloc.h>
#include <termios.h>
#include <sys/poll.h>
#include <errno.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include "cpu-i386.h"
#include "exec.h"

#include "vl.h"

#define NO_THUNK_TYPE_SIZE
#include "thunk.h"

//#define DEBUG_VGA
//#define DEBUG_VGA_MEM

#define MSR_COLOR_EMULATION 0x01
#define MSR_PAGE_SELECT     0x20

#define ST01_V_RETRACE      0x08
#define ST01_DISP_ENABLE    0x01

typedef struct VGAState {
    uint8_t *vram_ptr;
    unsigned long vram_offset;
    unsigned int vram_size;
    uint32_t latch;
    uint8_t sr_index;
    uint8_t sr[8];
    uint8_t gr_index;
    uint8_t gr[16];
    uint8_t ar_index;
    uint8_t ar[21];
    int ar_flip_flop;
    uint8_t cr_index;
    uint8_t cr[256]; /* CRT registers */
    uint8_t msr; /* Misc Output Register */
    uint8_t fcr; /* Feature Control Register */
    uint8_t st00; /* status 0 */
    uint8_t st01; /* status 1 */
    uint8_t dac_state;
    uint8_t dac_sub_index;
    uint8_t dac_read_index;
    uint8_t dac_write_index;
    uint8_t dac_cache[3]; /* used when writing */
    uint8_t palette[768];

    /* display refresh support */
    DisplayState *ds;
    uint32_t font_offsets[2];
    int graphic_mode;
    uint8_t shift_control;
    uint8_t double_scan;
    uint32_t line_offset;
    uint32_t line_compare;
    uint32_t start_addr;
    uint8_t last_cw, last_ch;
    uint32_t last_width, last_height;
    uint8_t cursor_start, cursor_end;
    uint32_t cursor_offset;
    unsigned int (*rgb_to_pixel)(unsigned int r, unsigned int g, unsigned b);
    /* tell for each page if it has been updated since the last time */
    uint8_t vram_updated[VGA_RAM_SIZE / 4096];
    uint32_t last_palette[256];
#define CH_ATTR_SIZE (160 * 100)
    uint32_t last_ch_attr[CH_ATTR_SIZE]; /* XXX: make it dynamic */
} VGAState;

/* force some bits to zero */
static const uint8_t sr_mask[8] = {
    (uint8_t)~0xfc,
    (uint8_t)~0xc2,
    (uint8_t)~0xf0,
    (uint8_t)~0xc0,
    (uint8_t)~0xf1,
    (uint8_t)~0xff,
    (uint8_t)~0xff,
    (uint8_t)~0x00,
};

static const uint8_t gr_mask[16] = {
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

#ifdef WORD_BIGENDIAN
#define PAT(x) cbswap_32(x)
#else
#define PAT(x) (x)
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

#ifdef WORD_BIGENDIAN
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

VGAState vga_state;
int vga_io_memory;

static uint32_t vga_ioport_read(CPUX86State *env, uint32_t addr)
{
    VGAState *s = &vga_state;
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
            break;
        case 0x3c7:
            val = s->dac_state;
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
            break;
        case 0x3b4:
        case 0x3d4:
            val = s->cr_index;
            break;
        case 0x3b5:
        case 0x3d5:
            val = s->cr[s->cr_index];
            break;
        case 0x3ba:
        case 0x3da:
            /* just toggle to fool polling */
            s->st01 ^= ST01_V_RETRACE | ST01_DISP_ENABLE;
            val = s->st01;
            s->ar_flip_flop = 0;
            break;
        default:
            val = 0x00;
            break;
        }
    }
#ifdef DEBUG_VGA
    printf("VGA: read addr=0x%04x data=0x%02x\n", addr, val);
#endif
    return val;
}

static void vga_ioport_write(CPUX86State *env, uint32_t addr, uint32_t val)
{
    VGAState *s = &vga_state;
    int index, v;

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
        break;
    case 0x3c4:
        s->sr_index = val & 7;
        break;
    case 0x3c5:
        s->sr[s->sr_index] = val & sr_mask[s->sr_index];
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
        s->gr[s->gr_index] = val & gr_mask[s->gr_index];
        break;
    case 0x3b4:
    case 0x3d4:
        s->cr_index = val;
        break;
    case 0x3b5:
    case 0x3d5:
        /* handle CR0-7 protection */
        if ((s->cr[11] & 0x80) && s->cr_index <= 7) {
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
        case 0x12: /* veritcal display end */
            s->cr[s->cr_index] = val;
            break;

            /* S3 registers */
        case 0x2d:
        case 0x2e:
        case 0x2f:
        case 0x30:
            /* chip ID, cannot write */
            break;
        case 0x31:
            /* update start address */
            s->cr[s->cr_index] = val;
            v = (val >> 4) & 3;
            s->cr[0x69] = (s->cr[69] & ~0x03) | v;
            break;
        case 0x51:
            /* update start address */
            s->cr[s->cr_index] = val;
            v = val & 3;
            s->cr[0x69] = (s->cr[69] & ~0x0c) | (v << 2);
            break;
        default:
            s->cr[s->cr_index] = val;
            break;
        }
        break;
    case 0x3ba:
    case 0x3da:
        s->fcr = val & 0x10;
        break;
    }
}

/* called for accesses between 0xa0000 and 0xc0000 */
static uint32_t vga_mem_readb(uint32_t addr)
{
    VGAState *s = &vga_state;
    int memory_map_mode, plane;
    uint32_t ret;
    
    /* convert to VGA memory offset */
    memory_map_mode = (s->gr[6] >> 2) & 3;
    switch(memory_map_mode) {
    case 0:
        addr -= 0xa0000;
        break;
    case 1:
        addr -= 0xa0000;
        if (addr >= 0x10000)
            return 0xff;
        break;
    case 2:
        addr -= 0xb0000;
        if (addr >= 0x8000)
            return 0xff;
        break;
    default:
    case 3:
        addr -= 0xb8000;
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
#ifdef WORD_BIGENDIAN
            ret = (s->latch >> (24 - (plane * 8))) & 0xff;
#else
            ret = (s->latch >> (plane * 8)) & 0xff;
#endif
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

static uint32_t vga_mem_readw(uint32_t addr)
{
    uint32_t v;
    v = vga_mem_readb(addr);
    v |= vga_mem_readb(addr + 1) << 8;
    return v;
}

static uint32_t vga_mem_readl(uint32_t addr)
{
    uint32_t v;
    v = vga_mem_readb(addr);
    v |= vga_mem_readb(addr + 1) << 8;
    v |= vga_mem_readb(addr + 2) << 16;
    v |= vga_mem_readb(addr + 3) << 24;
    return v;
}


/* called for accesses between 0xa0000 and 0xc0000 */
void vga_mem_writeb(uint32_t addr, uint32_t val)
{
    VGAState *s = &vga_state;
    int memory_map_mode, plane, write_mode, b, func_select;
    uint32_t write_mask, bit_mask, set_mask;

#ifdef DEBUG_VGA_MEM
    printf("vga: [0x%x] = 0x%02x\n", addr, val);
#endif
    /* convert to VGA memory offset */
    memory_map_mode = (s->gr[6] >> 2) & 3;
    switch(memory_map_mode) {
    case 0:
        addr -= 0xa0000;
        break;
    case 1:
        addr -= 0xa0000;
        if (addr >= 0x10000)
            return;
        break;
    case 2:
        addr -= 0xb0000;
        if (addr >= 0x8000)
            return;
        break;
    default:
    case 3:
        addr -= 0xb8000;
        break;
    }
    
    if (s->sr[4] & 0x08) {
        /* chain 4 mode : simplest access */
        plane = addr & 3;
        if (s->sr[2] & (1 << plane)) {
            s->vram_ptr[addr] = val;
#ifdef DEBUG_VGA_MEM
            printf("vga: chain4: [0x%x]\n", addr);
#endif
            s->vram_updated[addr >> 12] = 1;
        }
    } else if (s->gr[5] & 0x10) {
        /* odd/even mode (aka text mode mapping) */
        plane = (s->gr[4] & 2) | (addr & 1);
        if (s->sr[2] & (1 << plane)) {
            addr = ((addr & ~1) << 1) | plane;
            s->vram_ptr[addr] = val;
#ifdef DEBUG_VGA_MEM
            printf("vga: odd/even: [0x%x]\n", addr);
#endif
            s->vram_updated[addr >> 12] = 1;
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
            val = ((val >> b) | (val << (8 - b)));

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
        write_mask = mask16[s->sr[2]];
        ((uint32_t *)s->vram_ptr)[addr] = 
            (((uint32_t *)s->vram_ptr)[addr] & ~write_mask) | 
            (val & write_mask);
#ifdef DEBUG_VGA_MEM
            printf("vga: latch: [0x%x] mask=0x%08x val=0x%08x\n", 
                   addr * 4, write_mask, val);
#endif
        s->vram_updated[addr >> 10] = 1;
    }
}

void vga_mem_writew(uint32_t addr, uint32_t val)
{
    vga_mem_writeb(addr, val & 0xff);
    vga_mem_writeb(addr + 1, (val >> 8) & 0xff);
}

void vga_mem_writel(uint32_t addr, uint32_t val)
{
    vga_mem_writeb(addr, val & 0xff);
    vga_mem_writeb(addr + 1, (val >> 8) & 0xff);
    vga_mem_writeb(addr + 2, (val >> 16) & 0xff);
    vga_mem_writeb(addr + 3, (val >> 24) & 0xff);
}

#ifdef WORD_BIGENDIAN
#define BIG 1
#else
#define BIG 0
#endif

#ifdef WORDS_BIGENDIAN
#define GET_PLANE(data, p) (((data) >> (24 - (p) * 8)) & 0xff)
#else
#define GET_PLANE(data, p) (((data) >> ((p) * 8)) & 0xff)
#endif

typedef void vga_draw_glyph8_func(uint8_t *d, int linesize,
                             const uint8_t *font_ptr, int h,
                             uint32_t fgcol, uint32_t bgcol);
typedef void vga_draw_glyph9_func(uint8_t *d, int linesize,
                                  const uint8_t *font_ptr, int h, 
                                  uint32_t fgcol, uint32_t bgcol, int dup9);
typedef void vga_draw_line_func(VGAState *s1, uint8_t *d, 
                                const uint8_t *s, int width);

static inline unsigned int rgb_to_pixel8(unsigned int r, unsigned int g, unsigned b)
{
    /* XXX: TODO */
    return 0;
}

static inline unsigned int rgb_to_pixel15(unsigned int r, unsigned int g, unsigned b)
{
    return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
}

static inline unsigned int rgb_to_pixel16(unsigned int r, unsigned int g, unsigned b)
{
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static inline unsigned int rgb_to_pixel32(unsigned int r, unsigned int g, unsigned b)
{
    return (r << 16) | (g << 8) | b;
}

#define DEPTH 8
#include "vga_template.h"

#define DEPTH 15
#include "vga_template.h"

#define DEPTH 16
#include "vga_template.h"

#define DEPTH 32
#include "vga_template.h"

static inline int c6_to_8(int v)
{
    int b;
    v &= 0x3f;
    b = v & 1;
    return (v << 2) | (b << 1) | b;
}

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

static unsigned int rgb_to_pixel16_dup(unsigned int r, unsigned int g, unsigned b)
{
    unsigned int col;
    col = rgb_to_pixel16(r, g, b);
    col |= col << 16;
    return col;
}

static unsigned int rgb_to_pixel32_dup(unsigned int r, unsigned int g, unsigned b)
{
    unsigned int col;
    col = rgb_to_pixel32(r, g, b);
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
        col = s->rgb_to_pixel(c6_to_8(s->palette[v]), 
                              c6_to_8(s->palette[v + 1]), 
                              c6_to_8(s->palette[v + 2]));
        if (col != palette[i]) {
            full_update = 1;
            palette[i] = col;
        }
        v += 3;
    }
    return full_update;
}

/* update start_addr and line_offset. Return TRUE if modified */
static int update_basic_params(VGAState *s)
{
    int full_update;
    uint32_t start_addr, line_offset, line_compare, v;
    
    full_update = 0;
    /* compute line_offset in bytes */
    v = (s->cr[0x51] >> 4) & 3; /* S3 extension */
    if (v == 0)
        v = (s->cr[0x43] >> 2) & 1; /* S3 extension */
    line_offset = s->cr[0x13] | (v << 8);
    line_offset <<= 3;

    /* starting address */
    start_addr = s->cr[0x0d] | (s->cr[0x0c] << 8);
    start_addr |= (s->cr[0x69] & 0x1f) << 16; /* S3 extension */

    /* line compare */
    line_compare = s->cr[0x18] | 
        ((s->cr[0x07] & 0x10) << 4) |
        ((s->cr[0x09] & 0x40) << 3);

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

static inline int get_depth_index(int depth)
{
    switch(depth) {
    default:
    case 8:
        return 0;
    case 15:
        return 1;
    case 16:
        return 2;
    case 32:
        return 3;
    }
}

static vga_draw_glyph8_func *vga_draw_glyph8_table[4] = {
    vga_draw_glyph8_8,
    vga_draw_glyph8_16,
    vga_draw_glyph8_16,
    vga_draw_glyph8_32,
};

static vga_draw_glyph8_func *vga_draw_glyph16_table[4] = {
    vga_draw_glyph16_8,
    vga_draw_glyph16_16,
    vga_draw_glyph16_16,
    vga_draw_glyph16_32,
};

static vga_draw_glyph9_func *vga_draw_glyph9_table[4] = {
    vga_draw_glyph9_8,
    vga_draw_glyph9_16,
    vga_draw_glyph9_16,
    vga_draw_glyph9_32,
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
    offset = (((v >> 5) & 1) | ((v >> 1) & 6)) * 8192 * 4 + 2;
    if (offset != s->font_offsets[0]) {
        s->font_offsets[0] = offset;
        full_update = 1;
    }
    font_base[0] = s->vram_ptr + offset;

    offset = (((v >> 4) & 1) | ((v << 1) & 6)) * 8192 * 4 + 2;
    font_base[1] = s->vram_ptr + offset;
    if (offset != s->font_offsets[1]) {
        s->font_offsets[1] = offset;
        full_update = 1;
    }

    full_update |= update_basic_params(s);

    line_offset = s->line_offset;
    s1 = s->vram_ptr + (s->start_addr * 4);

    /* total width & height */
    cheight = (s->cr[9] & 0x1f) + 1;
    cw = 8;
    if (s->sr[1] & 0x01)
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
    if (width != s->last_width || height != s->last_height ||
        cw != s->last_cw || cw != s->last_cw) {
        dpy_resize(s->ds, width * cw, height * cheight);
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
    
    depth_index = get_depth_index(s->ds->depth);
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
    VGA_DRAW_LINE32,
    VGA_DRAW_LINE_NB,
};

static vga_draw_line_func *vga_draw_line_table[4 * VGA_DRAW_LINE_NB] = {
    vga_draw_line2_8,
    vga_draw_line2_16,
    vga_draw_line2_16,
    vga_draw_line2_32,

    vga_draw_line2d2_8,
    vga_draw_line2d2_16,
    vga_draw_line2d2_16,
    vga_draw_line2d2_32,

    vga_draw_line4_8,
    vga_draw_line4_16,
    vga_draw_line4_16,
    vga_draw_line4_32,

    vga_draw_line4d2_8,
    vga_draw_line4d2_16,
    vga_draw_line4d2_16,
    vga_draw_line4d2_32,

    vga_draw_line8d2_8,
    vga_draw_line8d2_16,
    vga_draw_line8d2_16,
    vga_draw_line8d2_32,

    vga_draw_line8_8,
    vga_draw_line8_16,
    vga_draw_line8_16,
    vga_draw_line8_32,

    vga_draw_line15_8,
    vga_draw_line15_15,
    vga_draw_line15_16,
    vga_draw_line15_32,

    vga_draw_line16_8,
    vga_draw_line16_15,
    vga_draw_line16_16,
    vga_draw_line16_32,

    vga_draw_line32_8,
    vga_draw_line32_15,
    vga_draw_line32_16,
    vga_draw_line32_32,
};

/* 
 * graphic modes
 * Missing:
 * - double scan
 * - double width 
 */
static void vga_draw_graphic(VGAState *s, int full_update)
{
    int y1, y, update, page_min, page_max, linesize, y_start, double_scan, mask;
    int width, height, shift_control, line_offset, page0, page1, bwidth;
    int disp_width;
    uint8_t *d;
    uint32_t v, addr1, addr;
    vga_draw_line_func *vga_draw_line;
    
    full_update |= update_basic_params(s);

    width = (s->cr[0x01] + 1) * 8;
    height = s->cr[0x12] | 
        ((s->cr[0x07] & 0x02) << 7) | 
        ((s->cr[0x07] & 0x40) << 3);
    height = (height + 1);
    disp_width = width;
    
    shift_control = (s->gr[0x05] >> 5) & 3;
    double_scan = (s->cr[0x09] & 0x80);
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
    } else if (shift_control == 1) {
        full_update |= update_palette16(s);
        if (s->sr[0x01] & 8) {
            v = VGA_DRAW_LINE2D2;
            disp_width <<= 1;
        } else {
            v = VGA_DRAW_LINE2;
        }
    } else {
        full_update |= update_palette256(s);
        v = VGA_DRAW_LINE8D2;
        double_scan = 1; /* XXX: explain me why it is always activated */
    }
    vga_draw_line = vga_draw_line_table[v * 4 + get_depth_index(s->ds->depth)];

    if (disp_width != s->last_width ||
        height != s->last_height) {
        dpy_resize(s->ds, disp_width, height);
        s->last_width = disp_width;
        s->last_height = height;
        full_update = 1;
    }

    line_offset = s->line_offset;
#if 0
    printf("w=%d h=%d v=%d line_offset=%d double_scan=0x%02x cr[0x17]=0x%02x linecmp=%d sr[0x01]=%02x\n",
           width, height, v, line_offset, s->cr[9], s->cr[0x17], s->line_compare, s->sr[0x01]);
#endif
    addr1 = (s->start_addr * 4);
    bwidth = width * 4;
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
        page0 = addr >> 12;
        page1 = (addr + bwidth - 1) >> 12;
        update = full_update | s->vram_updated[page0] | s->vram_updated[page1];
        if ((page1 - page0) > 1) {
            /* if wide line, can use another page */
            update |= s->vram_updated[page0 + 1];
        }
        if (update) {
            if (y_start < 0)
                y_start = y;
            if (page0 < page_min)
                page_min = page0;
            if (page1 > page_max)
                page_max = page1;
            vga_draw_line(s, d, s->vram_ptr + addr, width);
        } else {
            if (y_start >= 0) {
                /* flush to display */
                dpy_update(s->ds, 0, y_start, 
                           disp_width, y - y_start);
                y_start = -1;
            }
        }
        if (!double_scan || (y & 1) != 0) {
            if (y1 == s->line_compare) {
                addr1 = 0;
            } else {
                mask = (s->cr[0x17] & 3) ^ 3;
                if ((y1 & mask) == mask)
                    addr1 += line_offset; 
            }
            y1++;
        }
        d += linesize;
    }
    if (y_start >= 0) {
        /* flush to display */
        dpy_update(s->ds, 0, y_start, 
                   disp_width, y - y_start);
    }
    /* reset modified pages */
    if (page_max != -1) {
        memset(s->vram_updated + page_min, 0, page_max - page_min + 1);
    }
}

/* draw text terminal (very limited, just for simple boot debug
   messages) */
static int last_cursor_pos;

void vga_draw_dumb(VGAState *s)
{
    int c, i, cursor_pos, eol;

    cursor_pos = s->cr[0x0f] | (s->cr[0x0e] << 8);
    eol = 0;
    for(i = last_cursor_pos; i < cursor_pos; i++) {
        /* XXX: should use vga RAM */
        c = phys_ram_base[0xb8000 + (i) * 2];
        if (c >= ' ') {
            putchar(c);
            eol = 0;
        } else {
            if (!eol)
                putchar('\n');
            eol = 1;
        }
    }
    fflush(stdout);
    last_cursor_pos = cursor_pos;
}

void vga_update_display(void)
{
    VGAState *s = &vga_state;
    int full_update, graphic_mode;

    if (s->ds->depth == 0) {
        vga_draw_dumb(s);
    } else {
        full_update = 0;
        graphic_mode = s->gr[6] & 1;
        if (graphic_mode != s->graphic_mode) {
            s->graphic_mode = graphic_mode;
            full_update = 1;
        }
        if (graphic_mode)
            vga_draw_graphic(s, full_update);
        else
            vga_draw_text(s, full_update);
    }
}

void vga_reset(VGAState *s)
{
    memset(s, 0, sizeof(VGAState));
    /* chip ID for 8c968 */
    s->cr[0x2d] = 0x88;
    s->cr[0x2e] = 0xb0;
    s->cr[0x2f] = 0x01; /* XXX: check revision code */
    s->cr[0x30] = 0xe1;
    s->graphic_mode = -1; /* force full update */
}

CPUReadMemoryFunc *vga_mem_read[3] = {
    vga_mem_readb,
    vga_mem_readw,
    vga_mem_readl,
};

CPUWriteMemoryFunc *vga_mem_write[3] = {
    vga_mem_writeb,
    vga_mem_writew,
    vga_mem_writel,
};

int vga_init(DisplayState *ds, uint8_t *vga_ram_base, 
             unsigned long vga_ram_offset, int vga_ram_size)
{
    VGAState *s = &vga_state;
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

    switch(ds->depth) {
    case 8:
        s->rgb_to_pixel = rgb_to_pixel8_dup;
        break;
    case 15:
        s->rgb_to_pixel = rgb_to_pixel15_dup;
        break;
    default:
    case 16:
        s->rgb_to_pixel = rgb_to_pixel16_dup;
        break;
    case 32:
        s->rgb_to_pixel = rgb_to_pixel32_dup;
        break;
    }

    s->vram_ptr = vga_ram_base;
    s->vram_offset = vga_ram_offset;
    s->vram_size = vga_ram_size;
    s->ds = ds;

    register_ioport_write(0x3c0, 16, vga_ioport_write, 1);

    register_ioport_write(0x3b4, 2, vga_ioport_write, 1);
    register_ioport_write(0x3d4, 2, vga_ioport_write, 1);
    register_ioport_write(0x3ba, 1, vga_ioport_write, 1);
    register_ioport_write(0x3da, 1, vga_ioport_write, 1);

    register_ioport_read(0x3c0, 16, vga_ioport_read, 1);

    register_ioport_read(0x3b4, 2, vga_ioport_read, 1);
    register_ioport_read(0x3d4, 2, vga_ioport_read, 1);
    register_ioport_read(0x3ba, 1, vga_ioport_read, 1);
    register_ioport_read(0x3da, 1, vga_ioport_read, 1);

    vga_io_memory = cpu_register_io_memory(0, vga_mem_read, vga_mem_write);
    cpu_register_physical_memory(0xa0000, 0x20000, vga_io_memory);
    return 0;
}
