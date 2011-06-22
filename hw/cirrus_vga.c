/*
 * QEMU Cirrus CLGD 54xx VGA Emulator.
 *
 * Copyright (c) 2004 Fabrice Bellard
 * Copyright (c) 2004 Makoto Suzuki (suzu)
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
/*
 * Reference: Finn Thogersons' VGADOC4b
 *   available at http://home.worldonline.dk/~finth/
 */
#include "hw.h"
#include "pc.h"
#include "pci.h"
#include "console.h"
#include "vga_int.h"
#include "loader.h"

/*
 * TODO:
 *    - destination write mask support not complete (bits 5..7)
 *    - optimize linear mappings
 *    - optimize bitblt functions
 */

//#define DEBUG_CIRRUS
//#define DEBUG_BITBLT

/***************************************
 *
 *  definitions
 *
 ***************************************/

// ID
#define CIRRUS_ID_CLGD5422  (0x23<<2)
#define CIRRUS_ID_CLGD5426  (0x24<<2)
#define CIRRUS_ID_CLGD5424  (0x25<<2)
#define CIRRUS_ID_CLGD5428  (0x26<<2)
#define CIRRUS_ID_CLGD5430  (0x28<<2)
#define CIRRUS_ID_CLGD5434  (0x2A<<2)
#define CIRRUS_ID_CLGD5436  (0x2B<<2)
#define CIRRUS_ID_CLGD5446  (0x2E<<2)

// sequencer 0x07
#define CIRRUS_SR7_BPP_VGA            0x00
#define CIRRUS_SR7_BPP_SVGA           0x01
#define CIRRUS_SR7_BPP_MASK           0x0e
#define CIRRUS_SR7_BPP_8              0x00
#define CIRRUS_SR7_BPP_16_DOUBLEVCLK  0x02
#define CIRRUS_SR7_BPP_24             0x04
#define CIRRUS_SR7_BPP_16             0x06
#define CIRRUS_SR7_BPP_32             0x08
#define CIRRUS_SR7_ISAADDR_MASK       0xe0

// sequencer 0x0f
#define CIRRUS_MEMSIZE_512k        0x08
#define CIRRUS_MEMSIZE_1M          0x10
#define CIRRUS_MEMSIZE_2M          0x18
#define CIRRUS_MEMFLAGS_BANKSWITCH 0x80	// bank switching is enabled.

// sequencer 0x12
#define CIRRUS_CURSOR_SHOW         0x01
#define CIRRUS_CURSOR_HIDDENPEL    0x02
#define CIRRUS_CURSOR_LARGE        0x04	// 64x64 if set, 32x32 if clear

// sequencer 0x17
#define CIRRUS_BUSTYPE_VLBFAST   0x10
#define CIRRUS_BUSTYPE_PCI       0x20
#define CIRRUS_BUSTYPE_VLBSLOW   0x30
#define CIRRUS_BUSTYPE_ISA       0x38
#define CIRRUS_MMIO_ENABLE       0x04
#define CIRRUS_MMIO_USE_PCIADDR  0x40	// 0xb8000 if cleared.
#define CIRRUS_MEMSIZEEXT_DOUBLE 0x80

// control 0x0b
#define CIRRUS_BANKING_DUAL             0x01
#define CIRRUS_BANKING_GRANULARITY_16K  0x20	// set:16k, clear:4k

// control 0x30
#define CIRRUS_BLTMODE_BACKWARDS        0x01
#define CIRRUS_BLTMODE_MEMSYSDEST       0x02
#define CIRRUS_BLTMODE_MEMSYSSRC        0x04
#define CIRRUS_BLTMODE_TRANSPARENTCOMP  0x08
#define CIRRUS_BLTMODE_PATTERNCOPY      0x40
#define CIRRUS_BLTMODE_COLOREXPAND      0x80
#define CIRRUS_BLTMODE_PIXELWIDTHMASK   0x30
#define CIRRUS_BLTMODE_PIXELWIDTH8      0x00
#define CIRRUS_BLTMODE_PIXELWIDTH16     0x10
#define CIRRUS_BLTMODE_PIXELWIDTH24     0x20
#define CIRRUS_BLTMODE_PIXELWIDTH32     0x30

// control 0x31
#define CIRRUS_BLT_BUSY                 0x01
#define CIRRUS_BLT_START                0x02
#define CIRRUS_BLT_RESET                0x04
#define CIRRUS_BLT_FIFOUSED             0x10
#define CIRRUS_BLT_AUTOSTART            0x80

// control 0x32
#define CIRRUS_ROP_0                    0x00
#define CIRRUS_ROP_SRC_AND_DST          0x05
#define CIRRUS_ROP_NOP                  0x06
#define CIRRUS_ROP_SRC_AND_NOTDST       0x09
#define CIRRUS_ROP_NOTDST               0x0b
#define CIRRUS_ROP_SRC                  0x0d
#define CIRRUS_ROP_1                    0x0e
#define CIRRUS_ROP_NOTSRC_AND_DST       0x50
#define CIRRUS_ROP_SRC_XOR_DST          0x59
#define CIRRUS_ROP_SRC_OR_DST           0x6d
#define CIRRUS_ROP_NOTSRC_OR_NOTDST     0x90
#define CIRRUS_ROP_SRC_NOTXOR_DST       0x95
#define CIRRUS_ROP_SRC_OR_NOTDST        0xad
#define CIRRUS_ROP_NOTSRC               0xd0
#define CIRRUS_ROP_NOTSRC_OR_DST        0xd6
#define CIRRUS_ROP_NOTSRC_AND_NOTDST    0xda

#define CIRRUS_ROP_NOP_INDEX 2
#define CIRRUS_ROP_SRC_INDEX 5

// control 0x33
#define CIRRUS_BLTMODEEXT_SOLIDFILL        0x04
#define CIRRUS_BLTMODEEXT_COLOREXPINV      0x02
#define CIRRUS_BLTMODEEXT_DWORDGRANULARITY 0x01

// memory-mapped IO
#define CIRRUS_MMIO_BLTBGCOLOR        0x00	// dword
#define CIRRUS_MMIO_BLTFGCOLOR        0x04	// dword
#define CIRRUS_MMIO_BLTWIDTH          0x08	// word
#define CIRRUS_MMIO_BLTHEIGHT         0x0a	// word
#define CIRRUS_MMIO_BLTDESTPITCH      0x0c	// word
#define CIRRUS_MMIO_BLTSRCPITCH       0x0e	// word
#define CIRRUS_MMIO_BLTDESTADDR       0x10	// dword
#define CIRRUS_MMIO_BLTSRCADDR        0x14	// dword
#define CIRRUS_MMIO_BLTWRITEMASK      0x17	// byte
#define CIRRUS_MMIO_BLTMODE           0x18	// byte
#define CIRRUS_MMIO_BLTROP            0x1a	// byte
#define CIRRUS_MMIO_BLTMODEEXT        0x1b	// byte
#define CIRRUS_MMIO_BLTTRANSPARENTCOLOR 0x1c	// word?
#define CIRRUS_MMIO_BLTTRANSPARENTCOLORMASK 0x20	// word?
#define CIRRUS_MMIO_LINEARDRAW_START_X 0x24	// word
#define CIRRUS_MMIO_LINEARDRAW_START_Y 0x26	// word
#define CIRRUS_MMIO_LINEARDRAW_END_X  0x28	// word
#define CIRRUS_MMIO_LINEARDRAW_END_Y  0x2a	// word
#define CIRRUS_MMIO_LINEARDRAW_LINESTYLE_INC 0x2c	// byte
#define CIRRUS_MMIO_LINEARDRAW_LINESTYLE_ROLLOVER 0x2d	// byte
#define CIRRUS_MMIO_LINEARDRAW_LINESTYLE_MASK 0x2e	// byte
#define CIRRUS_MMIO_LINEARDRAW_LINESTYLE_ACCUM 0x2f	// byte
#define CIRRUS_MMIO_BRESENHAM_K1      0x30	// word
#define CIRRUS_MMIO_BRESENHAM_K3      0x32	// word
#define CIRRUS_MMIO_BRESENHAM_ERROR   0x34	// word
#define CIRRUS_MMIO_BRESENHAM_DELTA_MAJOR 0x36	// word
#define CIRRUS_MMIO_BRESENHAM_DIRECTION 0x38	// byte
#define CIRRUS_MMIO_LINEDRAW_MODE     0x39	// byte
#define CIRRUS_MMIO_BLTSTATUS         0x40	// byte

#define CIRRUS_PNPMMIO_SIZE         0x1000

#define ABS(a) ((signed)(a) > 0 ? a : -a)

#define BLTUNSAFE(s) \
    ( \
        ( /* check dst is within bounds */ \
            (s)->cirrus_blt_height * ABS((s)->cirrus_blt_dstpitch) \
                + ((s)->cirrus_blt_dstaddr & (s)->cirrus_addr_mask) > \
                    (s)->vga.vram_size \
        ) || \
        ( /* check src is within bounds */ \
            (s)->cirrus_blt_height * ABS((s)->cirrus_blt_srcpitch) \
                + ((s)->cirrus_blt_srcaddr & (s)->cirrus_addr_mask) > \
                    (s)->vga.vram_size \
        ) \
    )

struct CirrusVGAState;
typedef void (*cirrus_bitblt_rop_t) (struct CirrusVGAState *s,
                                     uint8_t * dst, const uint8_t * src,
				     int dstpitch, int srcpitch,
				     int bltwidth, int bltheight);
typedef void (*cirrus_fill_t)(struct CirrusVGAState *s,
                              uint8_t *dst, int dst_pitch, int width, int height);

typedef struct CirrusVGAState {
    VGACommonState vga;

    int cirrus_linear_io_addr;
    int cirrus_linear_bitblt_io_addr;
    int cirrus_mmio_io_addr;
    uint32_t cirrus_addr_mask;
    uint32_t linear_mmio_mask;
    uint8_t cirrus_shadow_gr0;
    uint8_t cirrus_shadow_gr1;
    uint8_t cirrus_hidden_dac_lockindex;
    uint8_t cirrus_hidden_dac_data;
    uint32_t cirrus_bank_base[2];
    uint32_t cirrus_bank_limit[2];
    uint8_t cirrus_hidden_palette[48];
    uint32_t hw_cursor_x;
    uint32_t hw_cursor_y;
    int cirrus_blt_pixelwidth;
    int cirrus_blt_width;
    int cirrus_blt_height;
    int cirrus_blt_dstpitch;
    int cirrus_blt_srcpitch;
    uint32_t cirrus_blt_fgcol;
    uint32_t cirrus_blt_bgcol;
    uint32_t cirrus_blt_dstaddr;
    uint32_t cirrus_blt_srcaddr;
    uint8_t cirrus_blt_mode;
    uint8_t cirrus_blt_modeext;
    cirrus_bitblt_rop_t cirrus_rop;
#define CIRRUS_BLTBUFSIZE (2048 * 4) /* one line width */
    uint8_t cirrus_bltbuf[CIRRUS_BLTBUFSIZE];
    uint8_t *cirrus_srcptr;
    uint8_t *cirrus_srcptr_end;
    uint32_t cirrus_srccounter;
    /* hwcursor display state */
    int last_hw_cursor_size;
    int last_hw_cursor_x;
    int last_hw_cursor_y;
    int last_hw_cursor_y_start;
    int last_hw_cursor_y_end;
    int real_vram_size; /* XXX: suppress that */
    int device_id;
    int bustype;
} CirrusVGAState;

typedef struct PCICirrusVGAState {
    PCIDevice dev;
    CirrusVGAState cirrus_vga;
} PCICirrusVGAState;

static uint8_t rop_to_index[256];

/***************************************
 *
 *  prototypes.
 *
 ***************************************/


static void cirrus_bitblt_reset(CirrusVGAState *s);
static void cirrus_update_memory_access(CirrusVGAState *s);

/***************************************
 *
 *  raster operations
 *
 ***************************************/

static void cirrus_bitblt_rop_nop(CirrusVGAState *s,
                                  uint8_t *dst,const uint8_t *src,
                                  int dstpitch,int srcpitch,
                                  int bltwidth,int bltheight)
{
}

static void cirrus_bitblt_fill_nop(CirrusVGAState *s,
                                   uint8_t *dst,
                                   int dstpitch, int bltwidth,int bltheight)
{
}

#define ROP_NAME 0
#define ROP_FN(d, s) 0
#include "cirrus_vga_rop.h"

#define ROP_NAME src_and_dst
#define ROP_FN(d, s) (s) & (d)
#include "cirrus_vga_rop.h"

#define ROP_NAME src_and_notdst
#define ROP_FN(d, s) (s) & (~(d))
#include "cirrus_vga_rop.h"

#define ROP_NAME notdst
#define ROP_FN(d, s) ~(d)
#include "cirrus_vga_rop.h"

#define ROP_NAME src
#define ROP_FN(d, s) s
#include "cirrus_vga_rop.h"

#define ROP_NAME 1
#define ROP_FN(d, s) ~0
#include "cirrus_vga_rop.h"

#define ROP_NAME notsrc_and_dst
#define ROP_FN(d, s) (~(s)) & (d)
#include "cirrus_vga_rop.h"

#define ROP_NAME src_xor_dst
#define ROP_FN(d, s) (s) ^ (d)
#include "cirrus_vga_rop.h"

#define ROP_NAME src_or_dst
#define ROP_FN(d, s) (s) | (d)
#include "cirrus_vga_rop.h"

#define ROP_NAME notsrc_or_notdst
#define ROP_FN(d, s) (~(s)) | (~(d))
#include "cirrus_vga_rop.h"

#define ROP_NAME src_notxor_dst
#define ROP_FN(d, s) ~((s) ^ (d))
#include "cirrus_vga_rop.h"

#define ROP_NAME src_or_notdst
#define ROP_FN(d, s) (s) | (~(d))
#include "cirrus_vga_rop.h"

#define ROP_NAME notsrc
#define ROP_FN(d, s) (~(s))
#include "cirrus_vga_rop.h"

#define ROP_NAME notsrc_or_dst
#define ROP_FN(d, s) (~(s)) | (d)
#include "cirrus_vga_rop.h"

#define ROP_NAME notsrc_and_notdst
#define ROP_FN(d, s) (~(s)) & (~(d))
#include "cirrus_vga_rop.h"

static const cirrus_bitblt_rop_t cirrus_fwd_rop[16] = {
    cirrus_bitblt_rop_fwd_0,
    cirrus_bitblt_rop_fwd_src_and_dst,
    cirrus_bitblt_rop_nop,
    cirrus_bitblt_rop_fwd_src_and_notdst,
    cirrus_bitblt_rop_fwd_notdst,
    cirrus_bitblt_rop_fwd_src,
    cirrus_bitblt_rop_fwd_1,
    cirrus_bitblt_rop_fwd_notsrc_and_dst,
    cirrus_bitblt_rop_fwd_src_xor_dst,
    cirrus_bitblt_rop_fwd_src_or_dst,
    cirrus_bitblt_rop_fwd_notsrc_or_notdst,
    cirrus_bitblt_rop_fwd_src_notxor_dst,
    cirrus_bitblt_rop_fwd_src_or_notdst,
    cirrus_bitblt_rop_fwd_notsrc,
    cirrus_bitblt_rop_fwd_notsrc_or_dst,
    cirrus_bitblt_rop_fwd_notsrc_and_notdst,
};

static const cirrus_bitblt_rop_t cirrus_bkwd_rop[16] = {
    cirrus_bitblt_rop_bkwd_0,
    cirrus_bitblt_rop_bkwd_src_and_dst,
    cirrus_bitblt_rop_nop,
    cirrus_bitblt_rop_bkwd_src_and_notdst,
    cirrus_bitblt_rop_bkwd_notdst,
    cirrus_bitblt_rop_bkwd_src,
    cirrus_bitblt_rop_bkwd_1,
    cirrus_bitblt_rop_bkwd_notsrc_and_dst,
    cirrus_bitblt_rop_bkwd_src_xor_dst,
    cirrus_bitblt_rop_bkwd_src_or_dst,
    cirrus_bitblt_rop_bkwd_notsrc_or_notdst,
    cirrus_bitblt_rop_bkwd_src_notxor_dst,
    cirrus_bitblt_rop_bkwd_src_or_notdst,
    cirrus_bitblt_rop_bkwd_notsrc,
    cirrus_bitblt_rop_bkwd_notsrc_or_dst,
    cirrus_bitblt_rop_bkwd_notsrc_and_notdst,
};

#define TRANSP_ROP(name) {\
    name ## _8,\
    name ## _16,\
        }
#define TRANSP_NOP(func) {\
    func,\
    func,\
        }

static const cirrus_bitblt_rop_t cirrus_fwd_transp_rop[16][2] = {
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_0),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src_and_dst),
    TRANSP_NOP(cirrus_bitblt_rop_nop),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src_and_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_1),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_notsrc_and_dst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src_xor_dst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src_or_dst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_notsrc_or_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src_notxor_dst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src_or_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_notsrc),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_notsrc_or_dst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_notsrc_and_notdst),
};

static const cirrus_bitblt_rop_t cirrus_bkwd_transp_rop[16][2] = {
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_0),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src_and_dst),
    TRANSP_NOP(cirrus_bitblt_rop_nop),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src_and_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_1),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_notsrc_and_dst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src_xor_dst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src_or_dst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_notsrc_or_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src_notxor_dst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src_or_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_notsrc),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_notsrc_or_dst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_notsrc_and_notdst),
};

#define ROP2(name) {\
    name ## _8,\
    name ## _16,\
    name ## _24,\
    name ## _32,\
        }

#define ROP_NOP2(func) {\
    func,\
    func,\
    func,\
    func,\
        }

static const cirrus_bitblt_rop_t cirrus_patternfill[16][4] = {
    ROP2(cirrus_patternfill_0),
    ROP2(cirrus_patternfill_src_and_dst),
    ROP_NOP2(cirrus_bitblt_rop_nop),
    ROP2(cirrus_patternfill_src_and_notdst),
    ROP2(cirrus_patternfill_notdst),
    ROP2(cirrus_patternfill_src),
    ROP2(cirrus_patternfill_1),
    ROP2(cirrus_patternfill_notsrc_and_dst),
    ROP2(cirrus_patternfill_src_xor_dst),
    ROP2(cirrus_patternfill_src_or_dst),
    ROP2(cirrus_patternfill_notsrc_or_notdst),
    ROP2(cirrus_patternfill_src_notxor_dst),
    ROP2(cirrus_patternfill_src_or_notdst),
    ROP2(cirrus_patternfill_notsrc),
    ROP2(cirrus_patternfill_notsrc_or_dst),
    ROP2(cirrus_patternfill_notsrc_and_notdst),
};

static const cirrus_bitblt_rop_t cirrus_colorexpand_transp[16][4] = {
    ROP2(cirrus_colorexpand_transp_0),
    ROP2(cirrus_colorexpand_transp_src_and_dst),
    ROP_NOP2(cirrus_bitblt_rop_nop),
    ROP2(cirrus_colorexpand_transp_src_and_notdst),
    ROP2(cirrus_colorexpand_transp_notdst),
    ROP2(cirrus_colorexpand_transp_src),
    ROP2(cirrus_colorexpand_transp_1),
    ROP2(cirrus_colorexpand_transp_notsrc_and_dst),
    ROP2(cirrus_colorexpand_transp_src_xor_dst),
    ROP2(cirrus_colorexpand_transp_src_or_dst),
    ROP2(cirrus_colorexpand_transp_notsrc_or_notdst),
    ROP2(cirrus_colorexpand_transp_src_notxor_dst),
    ROP2(cirrus_colorexpand_transp_src_or_notdst),
    ROP2(cirrus_colorexpand_transp_notsrc),
    ROP2(cirrus_colorexpand_transp_notsrc_or_dst),
    ROP2(cirrus_colorexpand_transp_notsrc_and_notdst),
};

static const cirrus_bitblt_rop_t cirrus_colorexpand[16][4] = {
    ROP2(cirrus_colorexpand_0),
    ROP2(cirrus_colorexpand_src_and_dst),
    ROP_NOP2(cirrus_bitblt_rop_nop),
    ROP2(cirrus_colorexpand_src_and_notdst),
    ROP2(cirrus_colorexpand_notdst),
    ROP2(cirrus_colorexpand_src),
    ROP2(cirrus_colorexpand_1),
    ROP2(cirrus_colorexpand_notsrc_and_dst),
    ROP2(cirrus_colorexpand_src_xor_dst),
    ROP2(cirrus_colorexpand_src_or_dst),
    ROP2(cirrus_colorexpand_notsrc_or_notdst),
    ROP2(cirrus_colorexpand_src_notxor_dst),
    ROP2(cirrus_colorexpand_src_or_notdst),
    ROP2(cirrus_colorexpand_notsrc),
    ROP2(cirrus_colorexpand_notsrc_or_dst),
    ROP2(cirrus_colorexpand_notsrc_and_notdst),
};

static const cirrus_bitblt_rop_t cirrus_colorexpand_pattern_transp[16][4] = {
    ROP2(cirrus_colorexpand_pattern_transp_0),
    ROP2(cirrus_colorexpand_pattern_transp_src_and_dst),
    ROP_NOP2(cirrus_bitblt_rop_nop),
    ROP2(cirrus_colorexpand_pattern_transp_src_and_notdst),
    ROP2(cirrus_colorexpand_pattern_transp_notdst),
    ROP2(cirrus_colorexpand_pattern_transp_src),
    ROP2(cirrus_colorexpand_pattern_transp_1),
    ROP2(cirrus_colorexpand_pattern_transp_notsrc_and_dst),
    ROP2(cirrus_colorexpand_pattern_transp_src_xor_dst),
    ROP2(cirrus_colorexpand_pattern_transp_src_or_dst),
    ROP2(cirrus_colorexpand_pattern_transp_notsrc_or_notdst),
    ROP2(cirrus_colorexpand_pattern_transp_src_notxor_dst),
    ROP2(cirrus_colorexpand_pattern_transp_src_or_notdst),
    ROP2(cirrus_colorexpand_pattern_transp_notsrc),
    ROP2(cirrus_colorexpand_pattern_transp_notsrc_or_dst),
    ROP2(cirrus_colorexpand_pattern_transp_notsrc_and_notdst),
};

static const cirrus_bitblt_rop_t cirrus_colorexpand_pattern[16][4] = {
    ROP2(cirrus_colorexpand_pattern_0),
    ROP2(cirrus_colorexpand_pattern_src_and_dst),
    ROP_NOP2(cirrus_bitblt_rop_nop),
    ROP2(cirrus_colorexpand_pattern_src_and_notdst),
    ROP2(cirrus_colorexpand_pattern_notdst),
    ROP2(cirrus_colorexpand_pattern_src),
    ROP2(cirrus_colorexpand_pattern_1),
    ROP2(cirrus_colorexpand_pattern_notsrc_and_dst),
    ROP2(cirrus_colorexpand_pattern_src_xor_dst),
    ROP2(cirrus_colorexpand_pattern_src_or_dst),
    ROP2(cirrus_colorexpand_pattern_notsrc_or_notdst),
    ROP2(cirrus_colorexpand_pattern_src_notxor_dst),
    ROP2(cirrus_colorexpand_pattern_src_or_notdst),
    ROP2(cirrus_colorexpand_pattern_notsrc),
    ROP2(cirrus_colorexpand_pattern_notsrc_or_dst),
    ROP2(cirrus_colorexpand_pattern_notsrc_and_notdst),
};

static const cirrus_fill_t cirrus_fill[16][4] = {
    ROP2(cirrus_fill_0),
    ROP2(cirrus_fill_src_and_dst),
    ROP_NOP2(cirrus_bitblt_fill_nop),
    ROP2(cirrus_fill_src_and_notdst),
    ROP2(cirrus_fill_notdst),
    ROP2(cirrus_fill_src),
    ROP2(cirrus_fill_1),
    ROP2(cirrus_fill_notsrc_and_dst),
    ROP2(cirrus_fill_src_xor_dst),
    ROP2(cirrus_fill_src_or_dst),
    ROP2(cirrus_fill_notsrc_or_notdst),
    ROP2(cirrus_fill_src_notxor_dst),
    ROP2(cirrus_fill_src_or_notdst),
    ROP2(cirrus_fill_notsrc),
    ROP2(cirrus_fill_notsrc_or_dst),
    ROP2(cirrus_fill_notsrc_and_notdst),
};

static inline void cirrus_bitblt_fgcol(CirrusVGAState *s)
{
    unsigned int color;
    switch (s->cirrus_blt_pixelwidth) {
    case 1:
        s->cirrus_blt_fgcol = s->cirrus_shadow_gr1;
        break;
    case 2:
        color = s->cirrus_shadow_gr1 | (s->vga.gr[0x11] << 8);
        s->cirrus_blt_fgcol = le16_to_cpu(color);
        break;
    case 3:
        s->cirrus_blt_fgcol = s->cirrus_shadow_gr1 |
            (s->vga.gr[0x11] << 8) | (s->vga.gr[0x13] << 16);
        break;
    default:
    case 4:
        color = s->cirrus_shadow_gr1 | (s->vga.gr[0x11] << 8) |
            (s->vga.gr[0x13] << 16) | (s->vga.gr[0x15] << 24);
        s->cirrus_blt_fgcol = le32_to_cpu(color);
        break;
    }
}

static inline void cirrus_bitblt_bgcol(CirrusVGAState *s)
{
    unsigned int color;
    switch (s->cirrus_blt_pixelwidth) {
    case 1:
        s->cirrus_blt_bgcol = s->cirrus_shadow_gr0;
        break;
    case 2:
        color = s->cirrus_shadow_gr0 | (s->vga.gr[0x10] << 8);
        s->cirrus_blt_bgcol = le16_to_cpu(color);
        break;
    case 3:
        s->cirrus_blt_bgcol = s->cirrus_shadow_gr0 |
            (s->vga.gr[0x10] << 8) | (s->vga.gr[0x12] << 16);
        break;
    default:
    case 4:
        color = s->cirrus_shadow_gr0 | (s->vga.gr[0x10] << 8) |
            (s->vga.gr[0x12] << 16) | (s->vga.gr[0x14] << 24);
        s->cirrus_blt_bgcol = le32_to_cpu(color);
        break;
    }
}

static void cirrus_invalidate_region(CirrusVGAState * s, int off_begin,
				     int off_pitch, int bytesperline,
				     int lines)
{
    int y;
    int off_cur;
    int off_cur_end;

    for (y = 0; y < lines; y++) {
	off_cur = off_begin;
	off_cur_end = (off_cur + bytesperline) & s->cirrus_addr_mask;
	off_cur &= TARGET_PAGE_MASK;
	while (off_cur < off_cur_end) {
	    cpu_physical_memory_set_dirty(s->vga.vram_offset + off_cur);
	    off_cur += TARGET_PAGE_SIZE;
	}
	off_begin += off_pitch;
    }
}

static int cirrus_bitblt_common_patterncopy(CirrusVGAState * s,
					    const uint8_t * src)
{
    uint8_t *dst;

    dst = s->vga.vram_ptr + (s->cirrus_blt_dstaddr & s->cirrus_addr_mask);

    if (BLTUNSAFE(s))
        return 0;

    (*s->cirrus_rop) (s, dst, src,
                      s->cirrus_blt_dstpitch, 0,
                      s->cirrus_blt_width, s->cirrus_blt_height);
    cirrus_invalidate_region(s, s->cirrus_blt_dstaddr,
                             s->cirrus_blt_dstpitch, s->cirrus_blt_width,
                             s->cirrus_blt_height);
    return 1;
}

/* fill */

static int cirrus_bitblt_solidfill(CirrusVGAState *s, int blt_rop)
{
    cirrus_fill_t rop_func;

    if (BLTUNSAFE(s))
        return 0;
    rop_func = cirrus_fill[rop_to_index[blt_rop]][s->cirrus_blt_pixelwidth - 1];
    rop_func(s, s->vga.vram_ptr + (s->cirrus_blt_dstaddr & s->cirrus_addr_mask),
             s->cirrus_blt_dstpitch,
             s->cirrus_blt_width, s->cirrus_blt_height);
    cirrus_invalidate_region(s, s->cirrus_blt_dstaddr,
			     s->cirrus_blt_dstpitch, s->cirrus_blt_width,
			     s->cirrus_blt_height);
    cirrus_bitblt_reset(s);
    return 1;
}

/***************************************
 *
 *  bitblt (video-to-video)
 *
 ***************************************/

static int cirrus_bitblt_videotovideo_patterncopy(CirrusVGAState * s)
{
    return cirrus_bitblt_common_patterncopy(s,
					    s->vga.vram_ptr + ((s->cirrus_blt_srcaddr & ~7) &
                                            s->cirrus_addr_mask));
}

static void cirrus_do_copy(CirrusVGAState *s, int dst, int src, int w, int h)
{
    int sx = 0, sy = 0;
    int dx = 0, dy = 0;
    int depth = 0;
    int notify = 0;

    /* make sure to only copy if it's a plain copy ROP */
    if (*s->cirrus_rop == cirrus_bitblt_rop_fwd_src ||
        *s->cirrus_rop == cirrus_bitblt_rop_bkwd_src) {

        int width, height;

        depth = s->vga.get_bpp(&s->vga) / 8;
        s->vga.get_resolution(&s->vga, &width, &height);

        /* extra x, y */
        sx = (src % ABS(s->cirrus_blt_srcpitch)) / depth;
        sy = (src / ABS(s->cirrus_blt_srcpitch));
        dx = (dst % ABS(s->cirrus_blt_dstpitch)) / depth;
        dy = (dst / ABS(s->cirrus_blt_dstpitch));

        /* normalize width */
        w /= depth;

        /* if we're doing a backward copy, we have to adjust
           our x/y to be the upper left corner (instead of the lower
           right corner) */
        if (s->cirrus_blt_dstpitch < 0) {
            sx -= (s->cirrus_blt_width / depth) - 1;
            dx -= (s->cirrus_blt_width / depth) - 1;
            sy -= s->cirrus_blt_height - 1;
            dy -= s->cirrus_blt_height - 1;
        }

        /* are we in the visible portion of memory? */
        if (sx >= 0 && sy >= 0 && dx >= 0 && dy >= 0 &&
            (sx + w) <= width && (sy + h) <= height &&
            (dx + w) <= width && (dy + h) <= height) {
            notify = 1;
        }
    }

    /* we have to flush all pending changes so that the copy
       is generated at the appropriate moment in time */
    if (notify)
	vga_hw_update();

    (*s->cirrus_rop) (s, s->vga.vram_ptr +
		      (s->cirrus_blt_dstaddr & s->cirrus_addr_mask),
		      s->vga.vram_ptr +
		      (s->cirrus_blt_srcaddr & s->cirrus_addr_mask),
		      s->cirrus_blt_dstpitch, s->cirrus_blt_srcpitch,
		      s->cirrus_blt_width, s->cirrus_blt_height);

    if (notify)
	qemu_console_copy(s->vga.ds,
			  sx, sy, dx, dy,
			  s->cirrus_blt_width / depth,
			  s->cirrus_blt_height);

    /* we don't have to notify the display that this portion has
       changed since qemu_console_copy implies this */

    cirrus_invalidate_region(s, s->cirrus_blt_dstaddr,
				s->cirrus_blt_dstpitch, s->cirrus_blt_width,
				s->cirrus_blt_height);
}

static int cirrus_bitblt_videotovideo_copy(CirrusVGAState * s)
{
    if (BLTUNSAFE(s))
        return 0;

    cirrus_do_copy(s, s->cirrus_blt_dstaddr - s->vga.start_addr,
            s->cirrus_blt_srcaddr - s->vga.start_addr,
            s->cirrus_blt_width, s->cirrus_blt_height);

    return 1;
}

/***************************************
 *
 *  bitblt (cpu-to-video)
 *
 ***************************************/

static void cirrus_bitblt_cputovideo_next(CirrusVGAState * s)
{
    int copy_count;
    uint8_t *end_ptr;

    if (s->cirrus_srccounter > 0) {
        if (s->cirrus_blt_mode & CIRRUS_BLTMODE_PATTERNCOPY) {
            cirrus_bitblt_common_patterncopy(s, s->cirrus_bltbuf);
        the_end:
            s->cirrus_srccounter = 0;
            cirrus_bitblt_reset(s);
        } else {
            /* at least one scan line */
            do {
                (*s->cirrus_rop)(s, s->vga.vram_ptr +
                                 (s->cirrus_blt_dstaddr & s->cirrus_addr_mask),
                                  s->cirrus_bltbuf, 0, 0, s->cirrus_blt_width, 1);
                cirrus_invalidate_region(s, s->cirrus_blt_dstaddr, 0,
                                         s->cirrus_blt_width, 1);
                s->cirrus_blt_dstaddr += s->cirrus_blt_dstpitch;
                s->cirrus_srccounter -= s->cirrus_blt_srcpitch;
                if (s->cirrus_srccounter <= 0)
                    goto the_end;
                /* more bytes than needed can be transfered because of
                   word alignment, so we keep them for the next line */
                /* XXX: keep alignment to speed up transfer */
                end_ptr = s->cirrus_bltbuf + s->cirrus_blt_srcpitch;
                copy_count = s->cirrus_srcptr_end - end_ptr;
                memmove(s->cirrus_bltbuf, end_ptr, copy_count);
                s->cirrus_srcptr = s->cirrus_bltbuf + copy_count;
                s->cirrus_srcptr_end = s->cirrus_bltbuf + s->cirrus_blt_srcpitch;
            } while (s->cirrus_srcptr >= s->cirrus_srcptr_end);
        }
    }
}

/***************************************
 *
 *  bitblt wrapper
 *
 ***************************************/

static void cirrus_bitblt_reset(CirrusVGAState * s)
{
    int need_update;

    s->vga.gr[0x31] &=
	~(CIRRUS_BLT_START | CIRRUS_BLT_BUSY | CIRRUS_BLT_FIFOUSED);
    need_update = s->cirrus_srcptr != &s->cirrus_bltbuf[0]
        || s->cirrus_srcptr_end != &s->cirrus_bltbuf[0];
    s->cirrus_srcptr = &s->cirrus_bltbuf[0];
    s->cirrus_srcptr_end = &s->cirrus_bltbuf[0];
    s->cirrus_srccounter = 0;
    if (!need_update)
        return;
    cirrus_update_memory_access(s);
}

static int cirrus_bitblt_cputovideo(CirrusVGAState * s)
{
    int w;

    s->cirrus_blt_mode &= ~CIRRUS_BLTMODE_MEMSYSSRC;
    s->cirrus_srcptr = &s->cirrus_bltbuf[0];
    s->cirrus_srcptr_end = &s->cirrus_bltbuf[0];

    if (s->cirrus_blt_mode & CIRRUS_BLTMODE_PATTERNCOPY) {
	if (s->cirrus_blt_mode & CIRRUS_BLTMODE_COLOREXPAND) {
	    s->cirrus_blt_srcpitch = 8;
	} else {
            /* XXX: check for 24 bpp */
	    s->cirrus_blt_srcpitch = 8 * 8 * s->cirrus_blt_pixelwidth;
	}
	s->cirrus_srccounter = s->cirrus_blt_srcpitch;
    } else {
	if (s->cirrus_blt_mode & CIRRUS_BLTMODE_COLOREXPAND) {
            w = s->cirrus_blt_width / s->cirrus_blt_pixelwidth;
            if (s->cirrus_blt_modeext & CIRRUS_BLTMODEEXT_DWORDGRANULARITY)
                s->cirrus_blt_srcpitch = ((w + 31) >> 5);
            else
                s->cirrus_blt_srcpitch = ((w + 7) >> 3);
	} else {
            /* always align input size to 32 bits */
	    s->cirrus_blt_srcpitch = (s->cirrus_blt_width + 3) & ~3;
	}
        s->cirrus_srccounter = s->cirrus_blt_srcpitch * s->cirrus_blt_height;
    }
    s->cirrus_srcptr = s->cirrus_bltbuf;
    s->cirrus_srcptr_end = s->cirrus_bltbuf + s->cirrus_blt_srcpitch;
    cirrus_update_memory_access(s);
    return 1;
}

static int cirrus_bitblt_videotocpu(CirrusVGAState * s)
{
    /* XXX */
#ifdef DEBUG_BITBLT
    printf("cirrus: bitblt (video to cpu) is not implemented yet\n");
#endif
    return 0;
}

static int cirrus_bitblt_videotovideo(CirrusVGAState * s)
{
    int ret;

    if (s->cirrus_blt_mode & CIRRUS_BLTMODE_PATTERNCOPY) {
	ret = cirrus_bitblt_videotovideo_patterncopy(s);
    } else {
	ret = cirrus_bitblt_videotovideo_copy(s);
    }
    if (ret)
	cirrus_bitblt_reset(s);
    return ret;
}

static void cirrus_bitblt_start(CirrusVGAState * s)
{
    uint8_t blt_rop;

    s->vga.gr[0x31] |= CIRRUS_BLT_BUSY;

    s->cirrus_blt_width = (s->vga.gr[0x20] | (s->vga.gr[0x21] << 8)) + 1;
    s->cirrus_blt_height = (s->vga.gr[0x22] | (s->vga.gr[0x23] << 8)) + 1;
    s->cirrus_blt_dstpitch = (s->vga.gr[0x24] | (s->vga.gr[0x25] << 8));
    s->cirrus_blt_srcpitch = (s->vga.gr[0x26] | (s->vga.gr[0x27] << 8));
    s->cirrus_blt_dstaddr =
	(s->vga.gr[0x28] | (s->vga.gr[0x29] << 8) | (s->vga.gr[0x2a] << 16));
    s->cirrus_blt_srcaddr =
	(s->vga.gr[0x2c] | (s->vga.gr[0x2d] << 8) | (s->vga.gr[0x2e] << 16));
    s->cirrus_blt_mode = s->vga.gr[0x30];
    s->cirrus_blt_modeext = s->vga.gr[0x33];
    blt_rop = s->vga.gr[0x32];

#ifdef DEBUG_BITBLT
    printf("rop=0x%02x mode=0x%02x modeext=0x%02x w=%d h=%d dpitch=%d spitch=%d daddr=0x%08x saddr=0x%08x writemask=0x%02x\n",
           blt_rop,
           s->cirrus_blt_mode,
           s->cirrus_blt_modeext,
           s->cirrus_blt_width,
           s->cirrus_blt_height,
           s->cirrus_blt_dstpitch,
           s->cirrus_blt_srcpitch,
           s->cirrus_blt_dstaddr,
           s->cirrus_blt_srcaddr,
           s->vga.gr[0x2f]);
#endif

    switch (s->cirrus_blt_mode & CIRRUS_BLTMODE_PIXELWIDTHMASK) {
    case CIRRUS_BLTMODE_PIXELWIDTH8:
	s->cirrus_blt_pixelwidth = 1;
	break;
    case CIRRUS_BLTMODE_PIXELWIDTH16:
	s->cirrus_blt_pixelwidth = 2;
	break;
    case CIRRUS_BLTMODE_PIXELWIDTH24:
	s->cirrus_blt_pixelwidth = 3;
	break;
    case CIRRUS_BLTMODE_PIXELWIDTH32:
	s->cirrus_blt_pixelwidth = 4;
	break;
    default:
#ifdef DEBUG_BITBLT
	printf("cirrus: bitblt - pixel width is unknown\n");
#endif
	goto bitblt_ignore;
    }
    s->cirrus_blt_mode &= ~CIRRUS_BLTMODE_PIXELWIDTHMASK;

    if ((s->
	 cirrus_blt_mode & (CIRRUS_BLTMODE_MEMSYSSRC |
			    CIRRUS_BLTMODE_MEMSYSDEST))
	== (CIRRUS_BLTMODE_MEMSYSSRC | CIRRUS_BLTMODE_MEMSYSDEST)) {
#ifdef DEBUG_BITBLT
	printf("cirrus: bitblt - memory-to-memory copy is requested\n");
#endif
	goto bitblt_ignore;
    }

    if ((s->cirrus_blt_modeext & CIRRUS_BLTMODEEXT_SOLIDFILL) &&
        (s->cirrus_blt_mode & (CIRRUS_BLTMODE_MEMSYSDEST |
                               CIRRUS_BLTMODE_TRANSPARENTCOMP |
                               CIRRUS_BLTMODE_PATTERNCOPY |
                               CIRRUS_BLTMODE_COLOREXPAND)) ==
         (CIRRUS_BLTMODE_PATTERNCOPY | CIRRUS_BLTMODE_COLOREXPAND)) {
        cirrus_bitblt_fgcol(s);
        cirrus_bitblt_solidfill(s, blt_rop);
    } else {
        if ((s->cirrus_blt_mode & (CIRRUS_BLTMODE_COLOREXPAND |
                                   CIRRUS_BLTMODE_PATTERNCOPY)) ==
            CIRRUS_BLTMODE_COLOREXPAND) {

            if (s->cirrus_blt_mode & CIRRUS_BLTMODE_TRANSPARENTCOMP) {
                if (s->cirrus_blt_modeext & CIRRUS_BLTMODEEXT_COLOREXPINV)
                    cirrus_bitblt_bgcol(s);
                else
                    cirrus_bitblt_fgcol(s);
                s->cirrus_rop = cirrus_colorexpand_transp[rop_to_index[blt_rop]][s->cirrus_blt_pixelwidth - 1];
            } else {
                cirrus_bitblt_fgcol(s);
                cirrus_bitblt_bgcol(s);
                s->cirrus_rop = cirrus_colorexpand[rop_to_index[blt_rop]][s->cirrus_blt_pixelwidth - 1];
            }
        } else if (s->cirrus_blt_mode & CIRRUS_BLTMODE_PATTERNCOPY) {
            if (s->cirrus_blt_mode & CIRRUS_BLTMODE_COLOREXPAND) {
                if (s->cirrus_blt_mode & CIRRUS_BLTMODE_TRANSPARENTCOMP) {
                    if (s->cirrus_blt_modeext & CIRRUS_BLTMODEEXT_COLOREXPINV)
                        cirrus_bitblt_bgcol(s);
                    else
                        cirrus_bitblt_fgcol(s);
                    s->cirrus_rop = cirrus_colorexpand_pattern_transp[rop_to_index[blt_rop]][s->cirrus_blt_pixelwidth - 1];
                } else {
                    cirrus_bitblt_fgcol(s);
                    cirrus_bitblt_bgcol(s);
                    s->cirrus_rop = cirrus_colorexpand_pattern[rop_to_index[blt_rop]][s->cirrus_blt_pixelwidth - 1];
                }
            } else {
                s->cirrus_rop = cirrus_patternfill[rop_to_index[blt_rop]][s->cirrus_blt_pixelwidth - 1];
            }
        } else {
	    if (s->cirrus_blt_mode & CIRRUS_BLTMODE_TRANSPARENTCOMP) {
		if (s->cirrus_blt_pixelwidth > 2) {
		    printf("src transparent without colorexpand must be 8bpp or 16bpp\n");
		    goto bitblt_ignore;
		}
		if (s->cirrus_blt_mode & CIRRUS_BLTMODE_BACKWARDS) {
		    s->cirrus_blt_dstpitch = -s->cirrus_blt_dstpitch;
		    s->cirrus_blt_srcpitch = -s->cirrus_blt_srcpitch;
		    s->cirrus_rop = cirrus_bkwd_transp_rop[rop_to_index[blt_rop]][s->cirrus_blt_pixelwidth - 1];
		} else {
		    s->cirrus_rop = cirrus_fwd_transp_rop[rop_to_index[blt_rop]][s->cirrus_blt_pixelwidth - 1];
		}
	    } else {
		if (s->cirrus_blt_mode & CIRRUS_BLTMODE_BACKWARDS) {
		    s->cirrus_blt_dstpitch = -s->cirrus_blt_dstpitch;
		    s->cirrus_blt_srcpitch = -s->cirrus_blt_srcpitch;
		    s->cirrus_rop = cirrus_bkwd_rop[rop_to_index[blt_rop]];
		} else {
		    s->cirrus_rop = cirrus_fwd_rop[rop_to_index[blt_rop]];
		}
	    }
	}
        // setup bitblt engine.
        if (s->cirrus_blt_mode & CIRRUS_BLTMODE_MEMSYSSRC) {
            if (!cirrus_bitblt_cputovideo(s))
                goto bitblt_ignore;
        } else if (s->cirrus_blt_mode & CIRRUS_BLTMODE_MEMSYSDEST) {
            if (!cirrus_bitblt_videotocpu(s))
                goto bitblt_ignore;
        } else {
            if (!cirrus_bitblt_videotovideo(s))
                goto bitblt_ignore;
        }
    }
    return;
  bitblt_ignore:;
    cirrus_bitblt_reset(s);
}

static void cirrus_write_bitblt(CirrusVGAState * s, unsigned reg_value)
{
    unsigned old_value;

    old_value = s->vga.gr[0x31];
    s->vga.gr[0x31] = reg_value;

    if (((old_value & CIRRUS_BLT_RESET) != 0) &&
	((reg_value & CIRRUS_BLT_RESET) == 0)) {
	cirrus_bitblt_reset(s);
    } else if (((old_value & CIRRUS_BLT_START) == 0) &&
	       ((reg_value & CIRRUS_BLT_START) != 0)) {
	cirrus_bitblt_start(s);
    }
}


/***************************************
 *
 *  basic parameters
 *
 ***************************************/

static void cirrus_get_offsets(VGACommonState *s1,
                               uint32_t *pline_offset,
                               uint32_t *pstart_addr,
                               uint32_t *pline_compare)
{
    CirrusVGAState * s = container_of(s1, CirrusVGAState, vga);
    uint32_t start_addr, line_offset, line_compare;

    line_offset = s->vga.cr[0x13]
	| ((s->vga.cr[0x1b] & 0x10) << 4);
    line_offset <<= 3;
    *pline_offset = line_offset;

    start_addr = (s->vga.cr[0x0c] << 8)
	| s->vga.cr[0x0d]
	| ((s->vga.cr[0x1b] & 0x01) << 16)
	| ((s->vga.cr[0x1b] & 0x0c) << 15)
	| ((s->vga.cr[0x1d] & 0x80) << 12);
    *pstart_addr = start_addr;

    line_compare = s->vga.cr[0x18] |
        ((s->vga.cr[0x07] & 0x10) << 4) |
        ((s->vga.cr[0x09] & 0x40) << 3);
    *pline_compare = line_compare;
}

static uint32_t cirrus_get_bpp16_depth(CirrusVGAState * s)
{
    uint32_t ret = 16;

    switch (s->cirrus_hidden_dac_data & 0xf) {
    case 0:
	ret = 15;
	break;			/* Sierra HiColor */
    case 1:
	ret = 16;
	break;			/* XGA HiColor */
    default:
#ifdef DEBUG_CIRRUS
	printf("cirrus: invalid DAC value %x in 16bpp\n",
	       (s->cirrus_hidden_dac_data & 0xf));
#endif
	ret = 15;		/* XXX */
	break;
    }
    return ret;
}

static int cirrus_get_bpp(VGACommonState *s1)
{
    CirrusVGAState * s = container_of(s1, CirrusVGAState, vga);
    uint32_t ret = 8;

    if ((s->vga.sr[0x07] & 0x01) != 0) {
	/* Cirrus SVGA */
	switch (s->vga.sr[0x07] & CIRRUS_SR7_BPP_MASK) {
	case CIRRUS_SR7_BPP_8:
	    ret = 8;
	    break;
	case CIRRUS_SR7_BPP_16_DOUBLEVCLK:
	    ret = cirrus_get_bpp16_depth(s);
	    break;
	case CIRRUS_SR7_BPP_24:
	    ret = 24;
	    break;
	case CIRRUS_SR7_BPP_16:
	    ret = cirrus_get_bpp16_depth(s);
	    break;
	case CIRRUS_SR7_BPP_32:
	    ret = 32;
	    break;
	default:
#ifdef DEBUG_CIRRUS
	    printf("cirrus: unknown bpp - sr7=%x\n", s->vga.sr[0x7]);
#endif
	    ret = 8;
	    break;
	}
    } else {
	/* VGA */
	ret = 0;
    }

    return ret;
}

static void cirrus_get_resolution(VGACommonState *s, int *pwidth, int *pheight)
{
    int width, height;

    width = (s->cr[0x01] + 1) * 8;
    height = s->cr[0x12] |
        ((s->cr[0x07] & 0x02) << 7) |
        ((s->cr[0x07] & 0x40) << 3);
    height = (height + 1);
    /* interlace support */
    if (s->cr[0x1a] & 0x01)
        height = height * 2;
    *pwidth = width;
    *pheight = height;
}

/***************************************
 *
 * bank memory
 *
 ***************************************/

static void cirrus_update_bank_ptr(CirrusVGAState * s, unsigned bank_index)
{
    unsigned offset;
    unsigned limit;

    if ((s->vga.gr[0x0b] & 0x01) != 0)	/* dual bank */
	offset = s->vga.gr[0x09 + bank_index];
    else			/* single bank */
	offset = s->vga.gr[0x09];

    if ((s->vga.gr[0x0b] & 0x20) != 0)
	offset <<= 14;
    else
	offset <<= 12;

    if (s->real_vram_size <= offset)
	limit = 0;
    else
	limit = s->real_vram_size - offset;

    if (((s->vga.gr[0x0b] & 0x01) == 0) && (bank_index != 0)) {
	if (limit > 0x8000) {
	    offset += 0x8000;
	    limit -= 0x8000;
	} else {
	    limit = 0;
	}
    }

    if (limit > 0) {
        /* Thinking about changing bank base? First, drop the dirty bitmap information
         * on the current location, otherwise we lose this pointer forever */
        if (s->vga.lfb_vram_mapped) {
            target_phys_addr_t base_addr = isa_mem_base + 0xa0000 + bank_index * 0x8000;
            cpu_physical_sync_dirty_bitmap(base_addr, base_addr + 0x8000);
        }
	s->cirrus_bank_base[bank_index] = offset;
	s->cirrus_bank_limit[bank_index] = limit;
    } else {
	s->cirrus_bank_base[bank_index] = 0;
	s->cirrus_bank_limit[bank_index] = 0;
    }
}

/***************************************
 *
 *  I/O access between 0x3c4-0x3c5
 *
 ***************************************/

static int cirrus_vga_read_sr(CirrusVGAState * s)
{
    switch (s->vga.sr_index) {
    case 0x00:			// Standard VGA
    case 0x01:			// Standard VGA
    case 0x02:			// Standard VGA
    case 0x03:			// Standard VGA
    case 0x04:			// Standard VGA
	return s->vga.sr[s->vga.sr_index];
    case 0x06:			// Unlock Cirrus extensions
	return s->vga.sr[s->vga.sr_index];
    case 0x10:
    case 0x30:
    case 0x50:
    case 0x70:			// Graphics Cursor X
    case 0x90:
    case 0xb0:
    case 0xd0:
    case 0xf0:			// Graphics Cursor X
	return s->vga.sr[0x10];
    case 0x11:
    case 0x31:
    case 0x51:
    case 0x71:			// Graphics Cursor Y
    case 0x91:
    case 0xb1:
    case 0xd1:
    case 0xf1:			// Graphics Cursor Y
	return s->vga.sr[0x11];
    case 0x05:			// ???
    case 0x07:			// Extended Sequencer Mode
    case 0x08:			// EEPROM Control
    case 0x09:			// Scratch Register 0
    case 0x0a:			// Scratch Register 1
    case 0x0b:			// VCLK 0
    case 0x0c:			// VCLK 1
    case 0x0d:			// VCLK 2
    case 0x0e:			// VCLK 3
    case 0x0f:			// DRAM Control
    case 0x12:			// Graphics Cursor Attribute
    case 0x13:			// Graphics Cursor Pattern Address
    case 0x14:			// Scratch Register 2
    case 0x15:			// Scratch Register 3
    case 0x16:			// Performance Tuning Register
    case 0x17:			// Configuration Readback and Extended Control
    case 0x18:			// Signature Generator Control
    case 0x19:			// Signal Generator Result
    case 0x1a:			// Signal Generator Result
    case 0x1b:			// VCLK 0 Denominator & Post
    case 0x1c:			// VCLK 1 Denominator & Post
    case 0x1d:			// VCLK 2 Denominator & Post
    case 0x1e:			// VCLK 3 Denominator & Post
    case 0x1f:			// BIOS Write Enable and MCLK select
#ifdef DEBUG_CIRRUS
	printf("cirrus: handled inport sr_index %02x\n", s->vga.sr_index);
#endif
	return s->vga.sr[s->vga.sr_index];
    default:
#ifdef DEBUG_CIRRUS
	printf("cirrus: inport sr_index %02x\n", s->vga.sr_index);
#endif
	return 0xff;
	break;
    }
}

static void cirrus_vga_write_sr(CirrusVGAState * s, uint32_t val)
{
    switch (s->vga.sr_index) {
    case 0x00:			// Standard VGA
    case 0x01:			// Standard VGA
    case 0x02:			// Standard VGA
    case 0x03:			// Standard VGA
    case 0x04:			// Standard VGA
	s->vga.sr[s->vga.sr_index] = val & sr_mask[s->vga.sr_index];
	if (s->vga.sr_index == 1)
            s->vga.update_retrace_info(&s->vga);
        break;
    case 0x06:			// Unlock Cirrus extensions
	val &= 0x17;
	if (val == 0x12) {
	    s->vga.sr[s->vga.sr_index] = 0x12;
	} else {
	    s->vga.sr[s->vga.sr_index] = 0x0f;
	}
	break;
    case 0x10:
    case 0x30:
    case 0x50:
    case 0x70:			// Graphics Cursor X
    case 0x90:
    case 0xb0:
    case 0xd0:
    case 0xf0:			// Graphics Cursor X
	s->vga.sr[0x10] = val;
	s->hw_cursor_x = (val << 3) | (s->vga.sr_index >> 5);
	break;
    case 0x11:
    case 0x31:
    case 0x51:
    case 0x71:			// Graphics Cursor Y
    case 0x91:
    case 0xb1:
    case 0xd1:
    case 0xf1:			// Graphics Cursor Y
	s->vga.sr[0x11] = val;
	s->hw_cursor_y = (val << 3) | (s->vga.sr_index >> 5);
	break;
    case 0x07:			// Extended Sequencer Mode
    cirrus_update_memory_access(s);
    case 0x08:			// EEPROM Control
    case 0x09:			// Scratch Register 0
    case 0x0a:			// Scratch Register 1
    case 0x0b:			// VCLK 0
    case 0x0c:			// VCLK 1
    case 0x0d:			// VCLK 2
    case 0x0e:			// VCLK 3
    case 0x0f:			// DRAM Control
    case 0x12:			// Graphics Cursor Attribute
    case 0x13:			// Graphics Cursor Pattern Address
    case 0x14:			// Scratch Register 2
    case 0x15:			// Scratch Register 3
    case 0x16:			// Performance Tuning Register
    case 0x18:			// Signature Generator Control
    case 0x19:			// Signature Generator Result
    case 0x1a:			// Signature Generator Result
    case 0x1b:			// VCLK 0 Denominator & Post
    case 0x1c:			// VCLK 1 Denominator & Post
    case 0x1d:			// VCLK 2 Denominator & Post
    case 0x1e:			// VCLK 3 Denominator & Post
    case 0x1f:			// BIOS Write Enable and MCLK select
	s->vga.sr[s->vga.sr_index] = val;
#ifdef DEBUG_CIRRUS
	printf("cirrus: handled outport sr_index %02x, sr_value %02x\n",
	       s->vga.sr_index, val);
#endif
	break;
    case 0x17:			// Configuration Readback and Extended Control
	s->vga.sr[s->vga.sr_index] = (s->vga.sr[s->vga.sr_index] & 0x38)
                                   | (val & 0xc7);
        cirrus_update_memory_access(s);
        break;
    default:
#ifdef DEBUG_CIRRUS
	printf("cirrus: outport sr_index %02x, sr_value %02x\n",
               s->vga.sr_index, val);
#endif
	break;
    }
}

/***************************************
 *
 *  I/O access at 0x3c6
 *
 ***************************************/

static int cirrus_read_hidden_dac(CirrusVGAState * s)
{
    if (++s->cirrus_hidden_dac_lockindex == 5) {
        s->cirrus_hidden_dac_lockindex = 0;
        return s->cirrus_hidden_dac_data;
    }
    return 0xff;
}

static void cirrus_write_hidden_dac(CirrusVGAState * s, int reg_value)
{
    if (s->cirrus_hidden_dac_lockindex == 4) {
	s->cirrus_hidden_dac_data = reg_value;
#if defined(DEBUG_CIRRUS)
	printf("cirrus: outport hidden DAC, value %02x\n", reg_value);
#endif
    }
    s->cirrus_hidden_dac_lockindex = 0;
}

/***************************************
 *
 *  I/O access at 0x3c9
 *
 ***************************************/

static int cirrus_vga_read_palette(CirrusVGAState * s)
{
    int val;

    if ((s->vga.sr[0x12] & CIRRUS_CURSOR_HIDDENPEL)) {
        val = s->cirrus_hidden_palette[(s->vga.dac_read_index & 0x0f) * 3 +
                                       s->vga.dac_sub_index];
    } else {
        val = s->vga.palette[s->vga.dac_read_index * 3 + s->vga.dac_sub_index];
    }
    if (++s->vga.dac_sub_index == 3) {
	s->vga.dac_sub_index = 0;
	s->vga.dac_read_index++;
    }
    return val;
}

static void cirrus_vga_write_palette(CirrusVGAState * s, int reg_value)
{
    s->vga.dac_cache[s->vga.dac_sub_index] = reg_value;
    if (++s->vga.dac_sub_index == 3) {
        if ((s->vga.sr[0x12] & CIRRUS_CURSOR_HIDDENPEL)) {
            memcpy(&s->cirrus_hidden_palette[(s->vga.dac_write_index & 0x0f) * 3],
                   s->vga.dac_cache, 3);
        } else {
            memcpy(&s->vga.palette[s->vga.dac_write_index * 3], s->vga.dac_cache, 3);
        }
        /* XXX update cursor */
	s->vga.dac_sub_index = 0;
	s->vga.dac_write_index++;
    }
}

/***************************************
 *
 *  I/O access between 0x3ce-0x3cf
 *
 ***************************************/

static int cirrus_vga_read_gr(CirrusVGAState * s, unsigned reg_index)
{
    switch (reg_index) {
    case 0x00: // Standard VGA, BGCOLOR 0x000000ff
        return s->cirrus_shadow_gr0;
    case 0x01: // Standard VGA, FGCOLOR 0x000000ff
        return s->cirrus_shadow_gr1;
    case 0x02:			// Standard VGA
    case 0x03:			// Standard VGA
    case 0x04:			// Standard VGA
    case 0x06:			// Standard VGA
    case 0x07:			// Standard VGA
    case 0x08:			// Standard VGA
        return s->vga.gr[s->vga.gr_index];
    case 0x05:			// Standard VGA, Cirrus extended mode
    default:
	break;
    }

    if (reg_index < 0x3a) {
	return s->vga.gr[reg_index];
    } else {
#ifdef DEBUG_CIRRUS
	printf("cirrus: inport gr_index %02x\n", reg_index);
#endif
	return 0xff;
    }
}

static void
cirrus_vga_write_gr(CirrusVGAState * s, unsigned reg_index, int reg_value)
{
#if defined(DEBUG_BITBLT) && 0
    printf("gr%02x: %02x\n", reg_index, reg_value);
#endif
    switch (reg_index) {
    case 0x00:			// Standard VGA, BGCOLOR 0x000000ff
	s->vga.gr[reg_index] = reg_value & gr_mask[reg_index];
	s->cirrus_shadow_gr0 = reg_value;
	break;
    case 0x01:			// Standard VGA, FGCOLOR 0x000000ff
	s->vga.gr[reg_index] = reg_value & gr_mask[reg_index];
	s->cirrus_shadow_gr1 = reg_value;
	break;
    case 0x02:			// Standard VGA
    case 0x03:			// Standard VGA
    case 0x04:			// Standard VGA
    case 0x06:			// Standard VGA
    case 0x07:			// Standard VGA
    case 0x08:			// Standard VGA
	s->vga.gr[reg_index] = reg_value & gr_mask[reg_index];
        break;
    case 0x05:			// Standard VGA, Cirrus extended mode
	s->vga.gr[reg_index] = reg_value & 0x7f;
        cirrus_update_memory_access(s);
	break;
    case 0x09:			// bank offset #0
    case 0x0A:			// bank offset #1
	s->vga.gr[reg_index] = reg_value;
	cirrus_update_bank_ptr(s, 0);
	cirrus_update_bank_ptr(s, 1);
        cirrus_update_memory_access(s);
        break;
    case 0x0B:
	s->vga.gr[reg_index] = reg_value;
	cirrus_update_bank_ptr(s, 0);
	cirrus_update_bank_ptr(s, 1);
        cirrus_update_memory_access(s);
	break;
    case 0x10:			// BGCOLOR 0x0000ff00
    case 0x11:			// FGCOLOR 0x0000ff00
    case 0x12:			// BGCOLOR 0x00ff0000
    case 0x13:			// FGCOLOR 0x00ff0000
    case 0x14:			// BGCOLOR 0xff000000
    case 0x15:			// FGCOLOR 0xff000000
    case 0x20:			// BLT WIDTH 0x0000ff
    case 0x22:			// BLT HEIGHT 0x0000ff
    case 0x24:			// BLT DEST PITCH 0x0000ff
    case 0x26:			// BLT SRC PITCH 0x0000ff
    case 0x28:			// BLT DEST ADDR 0x0000ff
    case 0x29:			// BLT DEST ADDR 0x00ff00
    case 0x2c:			// BLT SRC ADDR 0x0000ff
    case 0x2d:			// BLT SRC ADDR 0x00ff00
    case 0x2f:                  // BLT WRITEMASK
    case 0x30:			// BLT MODE
    case 0x32:			// RASTER OP
    case 0x33:			// BLT MODEEXT
    case 0x34:			// BLT TRANSPARENT COLOR 0x00ff
    case 0x35:			// BLT TRANSPARENT COLOR 0xff00
    case 0x38:			// BLT TRANSPARENT COLOR MASK 0x00ff
    case 0x39:			// BLT TRANSPARENT COLOR MASK 0xff00
	s->vga.gr[reg_index] = reg_value;
	break;
    case 0x21:			// BLT WIDTH 0x001f00
    case 0x23:			// BLT HEIGHT 0x001f00
    case 0x25:			// BLT DEST PITCH 0x001f00
    case 0x27:			// BLT SRC PITCH 0x001f00
	s->vga.gr[reg_index] = reg_value & 0x1f;
	break;
    case 0x2a:			// BLT DEST ADDR 0x3f0000
	s->vga.gr[reg_index] = reg_value & 0x3f;
        /* if auto start mode, starts bit blt now */
        if (s->vga.gr[0x31] & CIRRUS_BLT_AUTOSTART) {
            cirrus_bitblt_start(s);
        }
	break;
    case 0x2e:			// BLT SRC ADDR 0x3f0000
	s->vga.gr[reg_index] = reg_value & 0x3f;
	break;
    case 0x31:			// BLT STATUS/START
	cirrus_write_bitblt(s, reg_value);
	break;
    default:
#ifdef DEBUG_CIRRUS
	printf("cirrus: outport gr_index %02x, gr_value %02x\n", reg_index,
	       reg_value);
#endif
	break;
    }
}

/***************************************
 *
 *  I/O access between 0x3d4-0x3d5
 *
 ***************************************/

static int cirrus_vga_read_cr(CirrusVGAState * s, unsigned reg_index)
{
    switch (reg_index) {
    case 0x00:			// Standard VGA
    case 0x01:			// Standard VGA
    case 0x02:			// Standard VGA
    case 0x03:			// Standard VGA
    case 0x04:			// Standard VGA
    case 0x05:			// Standard VGA
    case 0x06:			// Standard VGA
    case 0x07:			// Standard VGA
    case 0x08:			// Standard VGA
    case 0x09:			// Standard VGA
    case 0x0a:			// Standard VGA
    case 0x0b:			// Standard VGA
    case 0x0c:			// Standard VGA
    case 0x0d:			// Standard VGA
    case 0x0e:			// Standard VGA
    case 0x0f:			// Standard VGA
    case 0x10:			// Standard VGA
    case 0x11:			// Standard VGA
    case 0x12:			// Standard VGA
    case 0x13:			// Standard VGA
    case 0x14:			// Standard VGA
    case 0x15:			// Standard VGA
    case 0x16:			// Standard VGA
    case 0x17:			// Standard VGA
    case 0x18:			// Standard VGA
	return s->vga.cr[s->vga.cr_index];
    case 0x24:			// Attribute Controller Toggle Readback (R)
        return (s->vga.ar_flip_flop << 7);
    case 0x19:			// Interlace End
    case 0x1a:			// Miscellaneous Control
    case 0x1b:			// Extended Display Control
    case 0x1c:			// Sync Adjust and Genlock
    case 0x1d:			// Overlay Extended Control
    case 0x22:			// Graphics Data Latches Readback (R)
    case 0x25:			// Part Status
    case 0x27:			// Part ID (R)
	return s->vga.cr[s->vga.cr_index];
    case 0x26:			// Attribute Controller Index Readback (R)
	return s->vga.ar_index & 0x3f;
	break;
    default:
#ifdef DEBUG_CIRRUS
	printf("cirrus: inport cr_index %02x\n", reg_index);
#endif
	return 0xff;
    }
}

static void cirrus_vga_write_cr(CirrusVGAState * s, int reg_value)
{
    switch (s->vga.cr_index) {
    case 0x00:			// Standard VGA
    case 0x01:			// Standard VGA
    case 0x02:			// Standard VGA
    case 0x03:			// Standard VGA
    case 0x04:			// Standard VGA
    case 0x05:			// Standard VGA
    case 0x06:			// Standard VGA
    case 0x07:			// Standard VGA
    case 0x08:			// Standard VGA
    case 0x09:			// Standard VGA
    case 0x0a:			// Standard VGA
    case 0x0b:			// Standard VGA
    case 0x0c:			// Standard VGA
    case 0x0d:			// Standard VGA
    case 0x0e:			// Standard VGA
    case 0x0f:			// Standard VGA
    case 0x10:			// Standard VGA
    case 0x11:			// Standard VGA
    case 0x12:			// Standard VGA
    case 0x13:			// Standard VGA
    case 0x14:			// Standard VGA
    case 0x15:			// Standard VGA
    case 0x16:			// Standard VGA
    case 0x17:			// Standard VGA
    case 0x18:			// Standard VGA
	/* handle CR0-7 protection */
	if ((s->vga.cr[0x11] & 0x80) && s->vga.cr_index <= 7) {
	    /* can always write bit 4 of CR7 */
	    if (s->vga.cr_index == 7)
		s->vga.cr[7] = (s->vga.cr[7] & ~0x10) | (reg_value & 0x10);
	    return;
	}
	s->vga.cr[s->vga.cr_index] = reg_value;
	switch(s->vga.cr_index) {
	case 0x00:
	case 0x04:
	case 0x05:
	case 0x06:
	case 0x07:
	case 0x11:
	case 0x17:
	    s->vga.update_retrace_info(&s->vga);
	    break;
	}
        break;
    case 0x19:			// Interlace End
    case 0x1a:			// Miscellaneous Control
    case 0x1b:			// Extended Display Control
    case 0x1c:			// Sync Adjust and Genlock
    case 0x1d:			// Overlay Extended Control
	s->vga.cr[s->vga.cr_index] = reg_value;
#ifdef DEBUG_CIRRUS
	printf("cirrus: handled outport cr_index %02x, cr_value %02x\n",
	       s->vga.cr_index, reg_value);
#endif
	break;
    case 0x22:			// Graphics Data Latches Readback (R)
    case 0x24:			// Attribute Controller Toggle Readback (R)
    case 0x26:			// Attribute Controller Index Readback (R)
    case 0x27:			// Part ID (R)
	break;
    case 0x25:			// Part Status
    default:
#ifdef DEBUG_CIRRUS
	printf("cirrus: outport cr_index %02x, cr_value %02x\n",
               s->vga.cr_index, reg_value);
#endif
	break;
    }
}

/***************************************
 *
 *  memory-mapped I/O (bitblt)
 *
 ***************************************/

static uint8_t cirrus_mmio_blt_read(CirrusVGAState * s, unsigned address)
{
    int value = 0xff;

    switch (address) {
    case (CIRRUS_MMIO_BLTBGCOLOR + 0):
	value = cirrus_vga_read_gr(s, 0x00);
	break;
    case (CIRRUS_MMIO_BLTBGCOLOR + 1):
	value = cirrus_vga_read_gr(s, 0x10);
	break;
    case (CIRRUS_MMIO_BLTBGCOLOR + 2):
	value = cirrus_vga_read_gr(s, 0x12);
	break;
    case (CIRRUS_MMIO_BLTBGCOLOR + 3):
	value = cirrus_vga_read_gr(s, 0x14);
	break;
    case (CIRRUS_MMIO_BLTFGCOLOR + 0):
	value = cirrus_vga_read_gr(s, 0x01);
	break;
    case (CIRRUS_MMIO_BLTFGCOLOR + 1):
	value = cirrus_vga_read_gr(s, 0x11);
	break;
    case (CIRRUS_MMIO_BLTFGCOLOR + 2):
	value = cirrus_vga_read_gr(s, 0x13);
	break;
    case (CIRRUS_MMIO_BLTFGCOLOR + 3):
	value = cirrus_vga_read_gr(s, 0x15);
	break;
    case (CIRRUS_MMIO_BLTWIDTH + 0):
	value = cirrus_vga_read_gr(s, 0x20);
	break;
    case (CIRRUS_MMIO_BLTWIDTH + 1):
	value = cirrus_vga_read_gr(s, 0x21);
	break;
    case (CIRRUS_MMIO_BLTHEIGHT + 0):
	value = cirrus_vga_read_gr(s, 0x22);
	break;
    case (CIRRUS_MMIO_BLTHEIGHT + 1):
	value = cirrus_vga_read_gr(s, 0x23);
	break;
    case (CIRRUS_MMIO_BLTDESTPITCH + 0):
	value = cirrus_vga_read_gr(s, 0x24);
	break;
    case (CIRRUS_MMIO_BLTDESTPITCH + 1):
	value = cirrus_vga_read_gr(s, 0x25);
	break;
    case (CIRRUS_MMIO_BLTSRCPITCH + 0):
	value = cirrus_vga_read_gr(s, 0x26);
	break;
    case (CIRRUS_MMIO_BLTSRCPITCH + 1):
	value = cirrus_vga_read_gr(s, 0x27);
	break;
    case (CIRRUS_MMIO_BLTDESTADDR + 0):
	value = cirrus_vga_read_gr(s, 0x28);
	break;
    case (CIRRUS_MMIO_BLTDESTADDR + 1):
	value = cirrus_vga_read_gr(s, 0x29);
	break;
    case (CIRRUS_MMIO_BLTDESTADDR + 2):
	value = cirrus_vga_read_gr(s, 0x2a);
	break;
    case (CIRRUS_MMIO_BLTSRCADDR + 0):
	value = cirrus_vga_read_gr(s, 0x2c);
	break;
    case (CIRRUS_MMIO_BLTSRCADDR + 1):
	value = cirrus_vga_read_gr(s, 0x2d);
	break;
    case (CIRRUS_MMIO_BLTSRCADDR + 2):
	value = cirrus_vga_read_gr(s, 0x2e);
	break;
    case CIRRUS_MMIO_BLTWRITEMASK:
	value = cirrus_vga_read_gr(s, 0x2f);
	break;
    case CIRRUS_MMIO_BLTMODE:
	value = cirrus_vga_read_gr(s, 0x30);
	break;
    case CIRRUS_MMIO_BLTROP:
	value = cirrus_vga_read_gr(s, 0x32);
	break;
    case CIRRUS_MMIO_BLTMODEEXT:
	value = cirrus_vga_read_gr(s, 0x33);
	break;
    case (CIRRUS_MMIO_BLTTRANSPARENTCOLOR + 0):
	value = cirrus_vga_read_gr(s, 0x34);
	break;
    case (CIRRUS_MMIO_BLTTRANSPARENTCOLOR + 1):
	value = cirrus_vga_read_gr(s, 0x35);
	break;
    case (CIRRUS_MMIO_BLTTRANSPARENTCOLORMASK + 0):
	value = cirrus_vga_read_gr(s, 0x38);
	break;
    case (CIRRUS_MMIO_BLTTRANSPARENTCOLORMASK + 1):
	value = cirrus_vga_read_gr(s, 0x39);
	break;
    case CIRRUS_MMIO_BLTSTATUS:
	value = cirrus_vga_read_gr(s, 0x31);
	break;
    default:
#ifdef DEBUG_CIRRUS
	printf("cirrus: mmio read - address 0x%04x\n", address);
#endif
	break;
    }

    return (uint8_t) value;
}

static void cirrus_mmio_blt_write(CirrusVGAState * s, unsigned address,
				  uint8_t value)
{
    switch (address) {
    case (CIRRUS_MMIO_BLTBGCOLOR + 0):
	cirrus_vga_write_gr(s, 0x00, value);
	break;
    case (CIRRUS_MMIO_BLTBGCOLOR + 1):
	cirrus_vga_write_gr(s, 0x10, value);
	break;
    case (CIRRUS_MMIO_BLTBGCOLOR + 2):
	cirrus_vga_write_gr(s, 0x12, value);
	break;
    case (CIRRUS_MMIO_BLTBGCOLOR + 3):
	cirrus_vga_write_gr(s, 0x14, value);
	break;
    case (CIRRUS_MMIO_BLTFGCOLOR + 0):
	cirrus_vga_write_gr(s, 0x01, value);
	break;
    case (CIRRUS_MMIO_BLTFGCOLOR + 1):
	cirrus_vga_write_gr(s, 0x11, value);
	break;
    case (CIRRUS_MMIO_BLTFGCOLOR + 2):
	cirrus_vga_write_gr(s, 0x13, value);
	break;
    case (CIRRUS_MMIO_BLTFGCOLOR + 3):
	cirrus_vga_write_gr(s, 0x15, value);
	break;
    case (CIRRUS_MMIO_BLTWIDTH + 0):
	cirrus_vga_write_gr(s, 0x20, value);
	break;
    case (CIRRUS_MMIO_BLTWIDTH + 1):
	cirrus_vga_write_gr(s, 0x21, value);
	break;
    case (CIRRUS_MMIO_BLTHEIGHT + 0):
	cirrus_vga_write_gr(s, 0x22, value);
	break;
    case (CIRRUS_MMIO_BLTHEIGHT + 1):
	cirrus_vga_write_gr(s, 0x23, value);
	break;
    case (CIRRUS_MMIO_BLTDESTPITCH + 0):
	cirrus_vga_write_gr(s, 0x24, value);
	break;
    case (CIRRUS_MMIO_BLTDESTPITCH + 1):
	cirrus_vga_write_gr(s, 0x25, value);
	break;
    case (CIRRUS_MMIO_BLTSRCPITCH + 0):
	cirrus_vga_write_gr(s, 0x26, value);
	break;
    case (CIRRUS_MMIO_BLTSRCPITCH + 1):
	cirrus_vga_write_gr(s, 0x27, value);
	break;
    case (CIRRUS_MMIO_BLTDESTADDR + 0):
	cirrus_vga_write_gr(s, 0x28, value);
	break;
    case (CIRRUS_MMIO_BLTDESTADDR + 1):
	cirrus_vga_write_gr(s, 0x29, value);
	break;
    case (CIRRUS_MMIO_BLTDESTADDR + 2):
	cirrus_vga_write_gr(s, 0x2a, value);
	break;
    case (CIRRUS_MMIO_BLTDESTADDR + 3):
	/* ignored */
	break;
    case (CIRRUS_MMIO_BLTSRCADDR + 0):
	cirrus_vga_write_gr(s, 0x2c, value);
	break;
    case (CIRRUS_MMIO_BLTSRCADDR + 1):
	cirrus_vga_write_gr(s, 0x2d, value);
	break;
    case (CIRRUS_MMIO_BLTSRCADDR + 2):
	cirrus_vga_write_gr(s, 0x2e, value);
	break;
    case CIRRUS_MMIO_BLTWRITEMASK:
	cirrus_vga_write_gr(s, 0x2f, value);
	break;
    case CIRRUS_MMIO_BLTMODE:
	cirrus_vga_write_gr(s, 0x30, value);
	break;
    case CIRRUS_MMIO_BLTROP:
	cirrus_vga_write_gr(s, 0x32, value);
	break;
    case CIRRUS_MMIO_BLTMODEEXT:
	cirrus_vga_write_gr(s, 0x33, value);
	break;
    case (CIRRUS_MMIO_BLTTRANSPARENTCOLOR + 0):
	cirrus_vga_write_gr(s, 0x34, value);
	break;
    case (CIRRUS_MMIO_BLTTRANSPARENTCOLOR + 1):
	cirrus_vga_write_gr(s, 0x35, value);
	break;
    case (CIRRUS_MMIO_BLTTRANSPARENTCOLORMASK + 0):
	cirrus_vga_write_gr(s, 0x38, value);
	break;
    case (CIRRUS_MMIO_BLTTRANSPARENTCOLORMASK + 1):
	cirrus_vga_write_gr(s, 0x39, value);
	break;
    case CIRRUS_MMIO_BLTSTATUS:
	cirrus_vga_write_gr(s, 0x31, value);
	break;
    default:
#ifdef DEBUG_CIRRUS
	printf("cirrus: mmio write - addr 0x%04x val 0x%02x (ignored)\n",
	       address, value);
#endif
	break;
    }
}

/***************************************
 *
 *  write mode 4/5
 *
 * assume TARGET_PAGE_SIZE >= 16
 *
 ***************************************/

static void cirrus_mem_writeb_mode4and5_8bpp(CirrusVGAState * s,
					     unsigned mode,
					     unsigned offset,
					     uint32_t mem_value)
{
    int x;
    unsigned val = mem_value;
    uint8_t *dst;

    dst = s->vga.vram_ptr + (offset &= s->cirrus_addr_mask);
    for (x = 0; x < 8; x++) {
	if (val & 0x80) {
	    *dst = s->cirrus_shadow_gr1;
	} else if (mode == 5) {
	    *dst = s->cirrus_shadow_gr0;
	}
	val <<= 1;
	dst++;
    }
    cpu_physical_memory_set_dirty(s->vga.vram_offset + offset);
    cpu_physical_memory_set_dirty(s->vga.vram_offset + offset + 7);
}

static void cirrus_mem_writeb_mode4and5_16bpp(CirrusVGAState * s,
					      unsigned mode,
					      unsigned offset,
					      uint32_t mem_value)
{
    int x;
    unsigned val = mem_value;
    uint8_t *dst;

    dst = s->vga.vram_ptr + (offset &= s->cirrus_addr_mask);
    for (x = 0; x < 8; x++) {
	if (val & 0x80) {
	    *dst = s->cirrus_shadow_gr1;
	    *(dst + 1) = s->vga.gr[0x11];
	} else if (mode == 5) {
	    *dst = s->cirrus_shadow_gr0;
	    *(dst + 1) = s->vga.gr[0x10];
	}
	val <<= 1;
	dst += 2;
    }
    cpu_physical_memory_set_dirty(s->vga.vram_offset + offset);
    cpu_physical_memory_set_dirty(s->vga.vram_offset + offset + 15);
}

/***************************************
 *
 *  memory access between 0xa0000-0xbffff
 *
 ***************************************/

static uint32_t cirrus_vga_mem_readb(void *opaque, target_phys_addr_t addr)
{
    CirrusVGAState *s = opaque;
    unsigned bank_index;
    unsigned bank_offset;
    uint32_t val;

    if ((s->vga.sr[0x07] & 0x01) == 0) {
	return vga_mem_readb(s, addr);
    }

    addr &= 0x1ffff;

    if (addr < 0x10000) {
	/* XXX handle bitblt */
	/* video memory */
	bank_index = addr >> 15;
	bank_offset = addr & 0x7fff;
	if (bank_offset < s->cirrus_bank_limit[bank_index]) {
	    bank_offset += s->cirrus_bank_base[bank_index];
	    if ((s->vga.gr[0x0B] & 0x14) == 0x14) {
		bank_offset <<= 4;
	    } else if (s->vga.gr[0x0B] & 0x02) {
		bank_offset <<= 3;
	    }
	    bank_offset &= s->cirrus_addr_mask;
	    val = *(s->vga.vram_ptr + bank_offset);
	} else
	    val = 0xff;
    } else if (addr >= 0x18000 && addr < 0x18100) {
	/* memory-mapped I/O */
	val = 0xff;
	if ((s->vga.sr[0x17] & 0x44) == 0x04) {
	    val = cirrus_mmio_blt_read(s, addr & 0xff);
	}
    } else {
	val = 0xff;
#ifdef DEBUG_CIRRUS
	printf("cirrus: mem_readb " TARGET_FMT_plx "\n", addr);
#endif
    }
    return val;
}

static uint32_t cirrus_vga_mem_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;

    v = cirrus_vga_mem_readb(opaque, addr);
    v |= cirrus_vga_mem_readb(opaque, addr + 1) << 8;
    return v;
}

static uint32_t cirrus_vga_mem_readl(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;

    v = cirrus_vga_mem_readb(opaque, addr);
    v |= cirrus_vga_mem_readb(opaque, addr + 1) << 8;
    v |= cirrus_vga_mem_readb(opaque, addr + 2) << 16;
    v |= cirrus_vga_mem_readb(opaque, addr + 3) << 24;
    return v;
}

static void cirrus_vga_mem_writeb(void *opaque, target_phys_addr_t addr,
                                  uint32_t mem_value)
{
    CirrusVGAState *s = opaque;
    unsigned bank_index;
    unsigned bank_offset;
    unsigned mode;

    if ((s->vga.sr[0x07] & 0x01) == 0) {
	vga_mem_writeb(s, addr, mem_value);
        return;
    }

    addr &= 0x1ffff;

    if (addr < 0x10000) {
	if (s->cirrus_srcptr != s->cirrus_srcptr_end) {
	    /* bitblt */
	    *s->cirrus_srcptr++ = (uint8_t) mem_value;
	    if (s->cirrus_srcptr >= s->cirrus_srcptr_end) {
		cirrus_bitblt_cputovideo_next(s);
	    }
	} else {
	    /* video memory */
	    bank_index = addr >> 15;
	    bank_offset = addr & 0x7fff;
	    if (bank_offset < s->cirrus_bank_limit[bank_index]) {
		bank_offset += s->cirrus_bank_base[bank_index];
		if ((s->vga.gr[0x0B] & 0x14) == 0x14) {
		    bank_offset <<= 4;
		} else if (s->vga.gr[0x0B] & 0x02) {
		    bank_offset <<= 3;
		}
		bank_offset &= s->cirrus_addr_mask;
		mode = s->vga.gr[0x05] & 0x7;
		if (mode < 4 || mode > 5 || ((s->vga.gr[0x0B] & 0x4) == 0)) {
		    *(s->vga.vram_ptr + bank_offset) = mem_value;
		    cpu_physical_memory_set_dirty(s->vga.vram_offset +
						  bank_offset);
		} else {
		    if ((s->vga.gr[0x0B] & 0x14) != 0x14) {
			cirrus_mem_writeb_mode4and5_8bpp(s, mode,
							 bank_offset,
							 mem_value);
		    } else {
			cirrus_mem_writeb_mode4and5_16bpp(s, mode,
							  bank_offset,
							  mem_value);
		    }
		}
	    }
	}
    } else if (addr >= 0x18000 && addr < 0x18100) {
	/* memory-mapped I/O */
	if ((s->vga.sr[0x17] & 0x44) == 0x04) {
	    cirrus_mmio_blt_write(s, addr & 0xff, mem_value);
	}
    } else {
#ifdef DEBUG_CIRRUS
        printf("cirrus: mem_writeb " TARGET_FMT_plx " value %02x\n", addr,
               mem_value);
#endif
    }
}

static void cirrus_vga_mem_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    cirrus_vga_mem_writeb(opaque, addr, val & 0xff);
    cirrus_vga_mem_writeb(opaque, addr + 1, (val >> 8) & 0xff);
}

static void cirrus_vga_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    cirrus_vga_mem_writeb(opaque, addr, val & 0xff);
    cirrus_vga_mem_writeb(opaque, addr + 1, (val >> 8) & 0xff);
    cirrus_vga_mem_writeb(opaque, addr + 2, (val >> 16) & 0xff);
    cirrus_vga_mem_writeb(opaque, addr + 3, (val >> 24) & 0xff);
}

static CPUReadMemoryFunc * const cirrus_vga_mem_read[3] = {
    cirrus_vga_mem_readb,
    cirrus_vga_mem_readw,
    cirrus_vga_mem_readl,
};

static CPUWriteMemoryFunc * const cirrus_vga_mem_write[3] = {
    cirrus_vga_mem_writeb,
    cirrus_vga_mem_writew,
    cirrus_vga_mem_writel,
};

/***************************************
 *
 *  hardware cursor
 *
 ***************************************/

static inline void invalidate_cursor1(CirrusVGAState *s)
{
    if (s->last_hw_cursor_size) {
        vga_invalidate_scanlines(&s->vga,
                                 s->last_hw_cursor_y + s->last_hw_cursor_y_start,
                                 s->last_hw_cursor_y + s->last_hw_cursor_y_end);
    }
}

static inline void cirrus_cursor_compute_yrange(CirrusVGAState *s)
{
    const uint8_t *src;
    uint32_t content;
    int y, y_min, y_max;

    src = s->vga.vram_ptr + s->real_vram_size - 16 * 1024;
    if (s->vga.sr[0x12] & CIRRUS_CURSOR_LARGE) {
        src += (s->vga.sr[0x13] & 0x3c) * 256;
        y_min = 64;
        y_max = -1;
        for(y = 0; y < 64; y++) {
            content = ((uint32_t *)src)[0] |
                ((uint32_t *)src)[1] |
                ((uint32_t *)src)[2] |
                ((uint32_t *)src)[3];
            if (content) {
                if (y < y_min)
                    y_min = y;
                if (y > y_max)
                    y_max = y;
            }
            src += 16;
        }
    } else {
        src += (s->vga.sr[0x13] & 0x3f) * 256;
        y_min = 32;
        y_max = -1;
        for(y = 0; y < 32; y++) {
            content = ((uint32_t *)src)[0] |
                ((uint32_t *)(src + 128))[0];
            if (content) {
                if (y < y_min)
                    y_min = y;
                if (y > y_max)
                    y_max = y;
            }
            src += 4;
        }
    }
    if (y_min > y_max) {
        s->last_hw_cursor_y_start = 0;
        s->last_hw_cursor_y_end = 0;
    } else {
        s->last_hw_cursor_y_start = y_min;
        s->last_hw_cursor_y_end = y_max + 1;
    }
}

/* NOTE: we do not currently handle the cursor bitmap change, so we
   update the cursor only if it moves. */
static void cirrus_cursor_invalidate(VGACommonState *s1)
{
    CirrusVGAState *s = container_of(s1, CirrusVGAState, vga);
    int size;

    if (!(s->vga.sr[0x12] & CIRRUS_CURSOR_SHOW)) {
        size = 0;
    } else {
        if (s->vga.sr[0x12] & CIRRUS_CURSOR_LARGE)
            size = 64;
        else
            size = 32;
    }
    /* invalidate last cursor and new cursor if any change */
    if (s->last_hw_cursor_size != size ||
        s->last_hw_cursor_x != s->hw_cursor_x ||
        s->last_hw_cursor_y != s->hw_cursor_y) {

        invalidate_cursor1(s);

        s->last_hw_cursor_size = size;
        s->last_hw_cursor_x = s->hw_cursor_x;
        s->last_hw_cursor_y = s->hw_cursor_y;
        /* compute the real cursor min and max y */
        cirrus_cursor_compute_yrange(s);
        invalidate_cursor1(s);
    }
}

static void cirrus_cursor_draw_line(VGACommonState *s1, uint8_t *d1, int scr_y)
{
    CirrusVGAState *s = container_of(s1, CirrusVGAState, vga);
    int w, h, bpp, x1, x2, poffset;
    unsigned int color0, color1;
    const uint8_t *palette, *src;
    uint32_t content;

    if (!(s->vga.sr[0x12] & CIRRUS_CURSOR_SHOW))
        return;
    /* fast test to see if the cursor intersects with the scan line */
    if (s->vga.sr[0x12] & CIRRUS_CURSOR_LARGE) {
        h = 64;
    } else {
        h = 32;
    }
    if (scr_y < s->hw_cursor_y ||
        scr_y >= (s->hw_cursor_y + h))
        return;

    src = s->vga.vram_ptr + s->real_vram_size - 16 * 1024;
    if (s->vga.sr[0x12] & CIRRUS_CURSOR_LARGE) {
        src += (s->vga.sr[0x13] & 0x3c) * 256;
        src += (scr_y - s->hw_cursor_y) * 16;
        poffset = 8;
        content = ((uint32_t *)src)[0] |
            ((uint32_t *)src)[1] |
            ((uint32_t *)src)[2] |
            ((uint32_t *)src)[3];
    } else {
        src += (s->vga.sr[0x13] & 0x3f) * 256;
        src += (scr_y - s->hw_cursor_y) * 4;
        poffset = 128;
        content = ((uint32_t *)src)[0] |
            ((uint32_t *)(src + 128))[0];
    }
    /* if nothing to draw, no need to continue */
    if (!content)
        return;
    w = h;

    x1 = s->hw_cursor_x;
    if (x1 >= s->vga.last_scr_width)
        return;
    x2 = s->hw_cursor_x + w;
    if (x2 > s->vga.last_scr_width)
        x2 = s->vga.last_scr_width;
    w = x2 - x1;
    palette = s->cirrus_hidden_palette;
    color0 = s->vga.rgb_to_pixel(c6_to_8(palette[0x0 * 3]),
                                 c6_to_8(palette[0x0 * 3 + 1]),
                                 c6_to_8(palette[0x0 * 3 + 2]));
    color1 = s->vga.rgb_to_pixel(c6_to_8(palette[0xf * 3]),
                                 c6_to_8(palette[0xf * 3 + 1]),
                                 c6_to_8(palette[0xf * 3 + 2]));
    bpp = ((ds_get_bits_per_pixel(s->vga.ds) + 7) >> 3);
    d1 += x1 * bpp;
    switch(ds_get_bits_per_pixel(s->vga.ds)) {
    default:
        break;
    case 8:
        vga_draw_cursor_line_8(d1, src, poffset, w, color0, color1, 0xff);
        break;
    case 15:
        vga_draw_cursor_line_16(d1, src, poffset, w, color0, color1, 0x7fff);
        break;
    case 16:
        vga_draw_cursor_line_16(d1, src, poffset, w, color0, color1, 0xffff);
        break;
    case 32:
        vga_draw_cursor_line_32(d1, src, poffset, w, color0, color1, 0xffffff);
        break;
    }
}

/***************************************
 *
 *  LFB memory access
 *
 ***************************************/

static uint32_t cirrus_linear_readb(void *opaque, target_phys_addr_t addr)
{
    CirrusVGAState *s = opaque;
    uint32_t ret;

    addr &= s->cirrus_addr_mask;

    if (((s->vga.sr[0x17] & 0x44) == 0x44) &&
        ((addr & s->linear_mmio_mask) == s->linear_mmio_mask)) {
	/* memory-mapped I/O */
	ret = cirrus_mmio_blt_read(s, addr & 0xff);
    } else if (0) {
	/* XXX handle bitblt */
	ret = 0xff;
    } else {
	/* video memory */
	if ((s->vga.gr[0x0B] & 0x14) == 0x14) {
	    addr <<= 4;
	} else if (s->vga.gr[0x0B] & 0x02) {
	    addr <<= 3;
	}
	addr &= s->cirrus_addr_mask;
	ret = *(s->vga.vram_ptr + addr);
    }

    return ret;
}

static uint32_t cirrus_linear_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;

    v = cirrus_linear_readb(opaque, addr);
    v |= cirrus_linear_readb(opaque, addr + 1) << 8;
    return v;
}

static uint32_t cirrus_linear_readl(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;

    v = cirrus_linear_readb(opaque, addr);
    v |= cirrus_linear_readb(opaque, addr + 1) << 8;
    v |= cirrus_linear_readb(opaque, addr + 2) << 16;
    v |= cirrus_linear_readb(opaque, addr + 3) << 24;
    return v;
}

static void cirrus_linear_writeb(void *opaque, target_phys_addr_t addr,
				 uint32_t val)
{
    CirrusVGAState *s = opaque;
    unsigned mode;

    addr &= s->cirrus_addr_mask;

    if (((s->vga.sr[0x17] & 0x44) == 0x44) &&
        ((addr & s->linear_mmio_mask) ==  s->linear_mmio_mask)) {
	/* memory-mapped I/O */
	cirrus_mmio_blt_write(s, addr & 0xff, val);
    } else if (s->cirrus_srcptr != s->cirrus_srcptr_end) {
	/* bitblt */
	*s->cirrus_srcptr++ = (uint8_t) val;
	if (s->cirrus_srcptr >= s->cirrus_srcptr_end) {
	    cirrus_bitblt_cputovideo_next(s);
	}
    } else {
	/* video memory */
	if ((s->vga.gr[0x0B] & 0x14) == 0x14) {
	    addr <<= 4;
	} else if (s->vga.gr[0x0B] & 0x02) {
	    addr <<= 3;
	}
	addr &= s->cirrus_addr_mask;

	mode = s->vga.gr[0x05] & 0x7;
	if (mode < 4 || mode > 5 || ((s->vga.gr[0x0B] & 0x4) == 0)) {
	    *(s->vga.vram_ptr + addr) = (uint8_t) val;
	    cpu_physical_memory_set_dirty(s->vga.vram_offset + addr);
	} else {
	    if ((s->vga.gr[0x0B] & 0x14) != 0x14) {
		cirrus_mem_writeb_mode4and5_8bpp(s, mode, addr, val);
	    } else {
		cirrus_mem_writeb_mode4and5_16bpp(s, mode, addr, val);
	    }
	}
    }
}

static void cirrus_linear_writew(void *opaque, target_phys_addr_t addr,
				 uint32_t val)
{
    cirrus_linear_writeb(opaque, addr, val & 0xff);
    cirrus_linear_writeb(opaque, addr + 1, (val >> 8) & 0xff);
}

static void cirrus_linear_writel(void *opaque, target_phys_addr_t addr,
				 uint32_t val)
{
    cirrus_linear_writeb(opaque, addr, val & 0xff);
    cirrus_linear_writeb(opaque, addr + 1, (val >> 8) & 0xff);
    cirrus_linear_writeb(opaque, addr + 2, (val >> 16) & 0xff);
    cirrus_linear_writeb(opaque, addr + 3, (val >> 24) & 0xff);
}


static CPUReadMemoryFunc * const cirrus_linear_read[3] = {
    cirrus_linear_readb,
    cirrus_linear_readw,
    cirrus_linear_readl,
};

static CPUWriteMemoryFunc * const cirrus_linear_write[3] = {
    cirrus_linear_writeb,
    cirrus_linear_writew,
    cirrus_linear_writel,
};

/***************************************
 *
 *  system to screen memory access
 *
 ***************************************/


static uint32_t cirrus_linear_bitblt_readb(void *opaque, target_phys_addr_t addr)
{
    uint32_t ret;

    /* XXX handle bitblt */
    ret = 0xff;
    return ret;
}

static uint32_t cirrus_linear_bitblt_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;

    v = cirrus_linear_bitblt_readb(opaque, addr);
    v |= cirrus_linear_bitblt_readb(opaque, addr + 1) << 8;
    return v;
}

static uint32_t cirrus_linear_bitblt_readl(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;

    v = cirrus_linear_bitblt_readb(opaque, addr);
    v |= cirrus_linear_bitblt_readb(opaque, addr + 1) << 8;
    v |= cirrus_linear_bitblt_readb(opaque, addr + 2) << 16;
    v |= cirrus_linear_bitblt_readb(opaque, addr + 3) << 24;
    return v;
}

static void cirrus_linear_bitblt_writeb(void *opaque, target_phys_addr_t addr,
				 uint32_t val)
{
    CirrusVGAState *s = opaque;

    if (s->cirrus_srcptr != s->cirrus_srcptr_end) {
	/* bitblt */
	*s->cirrus_srcptr++ = (uint8_t) val;
	if (s->cirrus_srcptr >= s->cirrus_srcptr_end) {
	    cirrus_bitblt_cputovideo_next(s);
	}
    }
}

static void cirrus_linear_bitblt_writew(void *opaque, target_phys_addr_t addr,
				 uint32_t val)
{
    cirrus_linear_bitblt_writeb(opaque, addr, val & 0xff);
    cirrus_linear_bitblt_writeb(opaque, addr + 1, (val >> 8) & 0xff);
}

static void cirrus_linear_bitblt_writel(void *opaque, target_phys_addr_t addr,
				 uint32_t val)
{
    cirrus_linear_bitblt_writeb(opaque, addr, val & 0xff);
    cirrus_linear_bitblt_writeb(opaque, addr + 1, (val >> 8) & 0xff);
    cirrus_linear_bitblt_writeb(opaque, addr + 2, (val >> 16) & 0xff);
    cirrus_linear_bitblt_writeb(opaque, addr + 3, (val >> 24) & 0xff);
}


static CPUReadMemoryFunc * const cirrus_linear_bitblt_read[3] = {
    cirrus_linear_bitblt_readb,
    cirrus_linear_bitblt_readw,
    cirrus_linear_bitblt_readl,
};

static CPUWriteMemoryFunc * const cirrus_linear_bitblt_write[3] = {
    cirrus_linear_bitblt_writeb,
    cirrus_linear_bitblt_writew,
    cirrus_linear_bitblt_writel,
};

static void map_linear_vram(CirrusVGAState *s)
{
    if (!s->vga.map_addr && s->vga.lfb_addr && s->vga.lfb_end) {
        s->vga.map_addr = s->vga.lfb_addr;
        s->vga.map_end = s->vga.lfb_end;
        cpu_register_physical_memory_log(s->vga.map_addr,
					 s->vga.map_end - s->vga.map_addr,
					 s->vga.vram_offset, 0, true);
    }

    if (!s->vga.map_addr)
        return;

    s->vga.lfb_vram_mapped = 0;

    if (!(s->cirrus_srcptr != s->cirrus_srcptr_end)
        && !((s->vga.sr[0x07] & 0x01) == 0)
        && !((s->vga.gr[0x0B] & 0x14) == 0x14)
        && !(s->vga.gr[0x0B] & 0x02)) {

        cpu_register_physical_memory_log(isa_mem_base + 0xa0000, 0x8000,
					 (s->vga.vram_offset +
					  s->cirrus_bank_base[0]) |
					 IO_MEM_RAM, 0, true);
        cpu_register_physical_memory_log(isa_mem_base + 0xa8000, 0x8000,
					 (s->vga.vram_offset +
					  s->cirrus_bank_base[1]) |
					 IO_MEM_RAM, 0, true);

        s->vga.lfb_vram_mapped = 1;
    }
    else {
        cpu_register_physical_memory(isa_mem_base + 0xa0000, 0x20000,
                                     s->vga.vga_io_memory);
    }

    vga_dirty_log_start(&s->vga);
}

static void unmap_linear_vram(CirrusVGAState *s)
{
    if (s->vga.map_addr && s->vga.lfb_addr && s->vga.lfb_end) {
        s->vga.map_addr = s->vga.map_end = 0;
         cpu_register_physical_memory(s->vga.lfb_addr, s->vga.vram_size,
                                      s->cirrus_linear_io_addr);
    }
    cpu_register_physical_memory(isa_mem_base + 0xa0000, 0x20000,
                                 s->vga.vga_io_memory);
}

/* Compute the memory access functions */
static void cirrus_update_memory_access(CirrusVGAState *s)
{
    unsigned mode;

    if ((s->vga.sr[0x17] & 0x44) == 0x44) {
        goto generic_io;
    } else if (s->cirrus_srcptr != s->cirrus_srcptr_end) {
        goto generic_io;
    } else {
	if ((s->vga.gr[0x0B] & 0x14) == 0x14) {
            goto generic_io;
	} else if (s->vga.gr[0x0B] & 0x02) {
            goto generic_io;
        }

	mode = s->vga.gr[0x05] & 0x7;
	if (mode < 4 || mode > 5 || ((s->vga.gr[0x0B] & 0x4) == 0)) {
            map_linear_vram(s);
        } else {
        generic_io:
            unmap_linear_vram(s);
        }
    }
}


/* I/O ports */

static uint32_t cirrus_vga_ioport_read(void *opaque, uint32_t addr)
{
    CirrusVGAState *c = opaque;
    VGACommonState *s = &c->vga;
    int val, index;

    if (vga_ioport_invalid(s, addr)) {
	val = 0xff;
    } else {
	switch (addr) {
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
	    val = cirrus_vga_read_sr(c);
            break;
#ifdef DEBUG_VGA_REG
	    printf("vga: read SR%x = 0x%02x\n", s->sr_index, val);
#endif
	    break;
	case 0x3c6:
	    val = cirrus_read_hidden_dac(c);
	    break;
	case 0x3c7:
	    val = s->dac_state;
	    break;
	case 0x3c8:
	    val = s->dac_write_index;
	    c->cirrus_hidden_dac_lockindex = 0;
	    break;
        case 0x3c9:
            val = cirrus_vga_read_palette(c);
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
	    val = cirrus_vga_read_gr(c, s->gr_index);
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
            val = cirrus_vga_read_cr(c, s->cr_index);
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

static void cirrus_vga_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    CirrusVGAState *c = opaque;
    VGACommonState *s = &c->vga;
    int index;

    /* check port range access depending on color/monochrome mode */
    if (vga_ioport_invalid(s, addr)) {
	return;
    }
#ifdef DEBUG_VGA
    printf("VGA: write addr=0x%04x data=0x%02x\n", addr, val);
#endif

    switch (addr) {
    case 0x3c0:
	if (s->ar_flip_flop == 0) {
	    val &= 0x3f;
	    s->ar_index = val;
	} else {
	    index = s->ar_index & 0x1f;
	    switch (index) {
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
	s->sr_index = val;
	break;
    case 0x3c5:
#ifdef DEBUG_VGA_REG
	printf("vga: write SR%x = 0x%02x\n", s->sr_index, val);
#endif
	cirrus_vga_write_sr(c, val);
        break;
	break;
    case 0x3c6:
	cirrus_write_hidden_dac(c, val);
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
        cirrus_vga_write_palette(c, val);
        break;
    case 0x3ce:
	s->gr_index = val;
	break;
    case 0x3cf:
#ifdef DEBUG_VGA_REG
	printf("vga: write GR%x = 0x%02x\n", s->gr_index, val);
#endif
	cirrus_vga_write_gr(c, s->gr_index, val);
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
	cirrus_vga_write_cr(c, val);
	break;
    case 0x3ba:
    case 0x3da:
	s->fcr = val & 0x10;
	break;
    }
}

/***************************************
 *
 *  memory-mapped I/O access
 *
 ***************************************/

static uint32_t cirrus_mmio_readb(void *opaque, target_phys_addr_t addr)
{
    CirrusVGAState *s = opaque;

    addr &= CIRRUS_PNPMMIO_SIZE - 1;

    if (addr >= 0x100) {
        return cirrus_mmio_blt_read(s, addr - 0x100);
    } else {
        return cirrus_vga_ioport_read(s, addr + 0x3c0);
    }
}

static uint32_t cirrus_mmio_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;

    v = cirrus_mmio_readb(opaque, addr);
    v |= cirrus_mmio_readb(opaque, addr + 1) << 8;
    return v;
}

static uint32_t cirrus_mmio_readl(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;

    v = cirrus_mmio_readb(opaque, addr);
    v |= cirrus_mmio_readb(opaque, addr + 1) << 8;
    v |= cirrus_mmio_readb(opaque, addr + 2) << 16;
    v |= cirrus_mmio_readb(opaque, addr + 3) << 24;
    return v;
}

static void cirrus_mmio_writeb(void *opaque, target_phys_addr_t addr,
			       uint32_t val)
{
    CirrusVGAState *s = opaque;

    addr &= CIRRUS_PNPMMIO_SIZE - 1;

    if (addr >= 0x100) {
	cirrus_mmio_blt_write(s, addr - 0x100, val);
    } else {
        cirrus_vga_ioport_write(s, addr + 0x3c0, val);
    }
}

static void cirrus_mmio_writew(void *opaque, target_phys_addr_t addr,
			       uint32_t val)
{
    cirrus_mmio_writeb(opaque, addr, val & 0xff);
    cirrus_mmio_writeb(opaque, addr + 1, (val >> 8) & 0xff);
}

static void cirrus_mmio_writel(void *opaque, target_phys_addr_t addr,
			       uint32_t val)
{
    cirrus_mmio_writeb(opaque, addr, val & 0xff);
    cirrus_mmio_writeb(opaque, addr + 1, (val >> 8) & 0xff);
    cirrus_mmio_writeb(opaque, addr + 2, (val >> 16) & 0xff);
    cirrus_mmio_writeb(opaque, addr + 3, (val >> 24) & 0xff);
}


static CPUReadMemoryFunc * const cirrus_mmio_read[3] = {
    cirrus_mmio_readb,
    cirrus_mmio_readw,
    cirrus_mmio_readl,
};

static CPUWriteMemoryFunc * const cirrus_mmio_write[3] = {
    cirrus_mmio_writeb,
    cirrus_mmio_writew,
    cirrus_mmio_writel,
};

/* load/save state */

static int cirrus_post_load(void *opaque, int version_id)
{
    CirrusVGAState *s = opaque;

    s->vga.gr[0x00] = s->cirrus_shadow_gr0 & 0x0f;
    s->vga.gr[0x01] = s->cirrus_shadow_gr1 & 0x0f;

    cirrus_update_memory_access(s);
    /* force refresh */
    s->vga.graphic_mode = -1;
    cirrus_update_bank_ptr(s, 0);
    cirrus_update_bank_ptr(s, 1);
    return 0;
}

static const VMStateDescription vmstate_cirrus_vga = {
    .name = "cirrus_vga",
    .version_id = 2,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .post_load = cirrus_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_UINT32(vga.latch, CirrusVGAState),
        VMSTATE_UINT8(vga.sr_index, CirrusVGAState),
        VMSTATE_BUFFER(vga.sr, CirrusVGAState),
        VMSTATE_UINT8(vga.gr_index, CirrusVGAState),
        VMSTATE_UINT8(cirrus_shadow_gr0, CirrusVGAState),
        VMSTATE_UINT8(cirrus_shadow_gr1, CirrusVGAState),
        VMSTATE_BUFFER_START_MIDDLE(vga.gr, CirrusVGAState, 2),
        VMSTATE_UINT8(vga.ar_index, CirrusVGAState),
        VMSTATE_BUFFER(vga.ar, CirrusVGAState),
        VMSTATE_INT32(vga.ar_flip_flop, CirrusVGAState),
        VMSTATE_UINT8(vga.cr_index, CirrusVGAState),
        VMSTATE_BUFFER(vga.cr, CirrusVGAState),
        VMSTATE_UINT8(vga.msr, CirrusVGAState),
        VMSTATE_UINT8(vga.fcr, CirrusVGAState),
        VMSTATE_UINT8(vga.st00, CirrusVGAState),
        VMSTATE_UINT8(vga.st01, CirrusVGAState),
        VMSTATE_UINT8(vga.dac_state, CirrusVGAState),
        VMSTATE_UINT8(vga.dac_sub_index, CirrusVGAState),
        VMSTATE_UINT8(vga.dac_read_index, CirrusVGAState),
        VMSTATE_UINT8(vga.dac_write_index, CirrusVGAState),
        VMSTATE_BUFFER(vga.dac_cache, CirrusVGAState),
        VMSTATE_BUFFER(vga.palette, CirrusVGAState),
        VMSTATE_INT32(vga.bank_offset, CirrusVGAState),
        VMSTATE_UINT8(cirrus_hidden_dac_lockindex, CirrusVGAState),
        VMSTATE_UINT8(cirrus_hidden_dac_data, CirrusVGAState),
        VMSTATE_UINT32(hw_cursor_x, CirrusVGAState),
        VMSTATE_UINT32(hw_cursor_y, CirrusVGAState),
        /* XXX: we do not save the bitblt state - we assume we do not save
           the state when the blitter is active */
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pci_cirrus_vga = {
    .name = "cirrus_vga",
    .version_id = 2,
    .minimum_version_id = 2,
    .minimum_version_id_old = 2,
    .fields      = (VMStateField []) {
        VMSTATE_PCI_DEVICE(dev, PCICirrusVGAState),
        VMSTATE_STRUCT(cirrus_vga, PCICirrusVGAState, 0,
                       vmstate_cirrus_vga, CirrusVGAState),
        VMSTATE_END_OF_LIST()
    }
};

/***************************************
 *
 *  initialize
 *
 ***************************************/

static void cirrus_reset(void *opaque)
{
    CirrusVGAState *s = opaque;

    vga_common_reset(&s->vga);
    unmap_linear_vram(s);
    s->vga.sr[0x06] = 0x0f;
    if (s->device_id == CIRRUS_ID_CLGD5446) {
        /* 4MB 64 bit memory config, always PCI */
        s->vga.sr[0x1F] = 0x2d;		// MemClock
        s->vga.gr[0x18] = 0x0f;             // fastest memory configuration
        s->vga.sr[0x0f] = 0x98;
        s->vga.sr[0x17] = 0x20;
        s->vga.sr[0x15] = 0x04; /* memory size, 3=2MB, 4=4MB */
    } else {
        s->vga.sr[0x1F] = 0x22;		// MemClock
        s->vga.sr[0x0F] = CIRRUS_MEMSIZE_2M;
        s->vga.sr[0x17] = s->bustype;
        s->vga.sr[0x15] = 0x03; /* memory size, 3=2MB, 4=4MB */
    }
    s->vga.cr[0x27] = s->device_id;

    /* Win2K seems to assume that the pattern buffer is at 0xff
       initially ! */
    memset(s->vga.vram_ptr, 0xff, s->real_vram_size);

    s->cirrus_hidden_dac_lockindex = 5;
    s->cirrus_hidden_dac_data = 0;
}

static void cirrus_init_common(CirrusVGAState * s, int device_id, int is_pci)
{
    int i;
    static int inited;

    if (!inited) {
        inited = 1;
        for(i = 0;i < 256; i++)
            rop_to_index[i] = CIRRUS_ROP_NOP_INDEX; /* nop rop */
        rop_to_index[CIRRUS_ROP_0] = 0;
        rop_to_index[CIRRUS_ROP_SRC_AND_DST] = 1;
        rop_to_index[CIRRUS_ROP_NOP] = 2;
        rop_to_index[CIRRUS_ROP_SRC_AND_NOTDST] = 3;
        rop_to_index[CIRRUS_ROP_NOTDST] = 4;
        rop_to_index[CIRRUS_ROP_SRC] = 5;
        rop_to_index[CIRRUS_ROP_1] = 6;
        rop_to_index[CIRRUS_ROP_NOTSRC_AND_DST] = 7;
        rop_to_index[CIRRUS_ROP_SRC_XOR_DST] = 8;
        rop_to_index[CIRRUS_ROP_SRC_OR_DST] = 9;
        rop_to_index[CIRRUS_ROP_NOTSRC_OR_NOTDST] = 10;
        rop_to_index[CIRRUS_ROP_SRC_NOTXOR_DST] = 11;
        rop_to_index[CIRRUS_ROP_SRC_OR_NOTDST] = 12;
        rop_to_index[CIRRUS_ROP_NOTSRC] = 13;
        rop_to_index[CIRRUS_ROP_NOTSRC_OR_DST] = 14;
        rop_to_index[CIRRUS_ROP_NOTSRC_AND_NOTDST] = 15;
        s->device_id = device_id;
        if (is_pci)
            s->bustype = CIRRUS_BUSTYPE_PCI;
        else
            s->bustype = CIRRUS_BUSTYPE_ISA;
    }

    register_ioport_write(0x3c0, 16, 1, cirrus_vga_ioport_write, s);

    register_ioport_write(0x3b4, 2, 1, cirrus_vga_ioport_write, s);
    register_ioport_write(0x3d4, 2, 1, cirrus_vga_ioport_write, s);
    register_ioport_write(0x3ba, 1, 1, cirrus_vga_ioport_write, s);
    register_ioport_write(0x3da, 1, 1, cirrus_vga_ioport_write, s);

    register_ioport_read(0x3c0, 16, 1, cirrus_vga_ioport_read, s);

    register_ioport_read(0x3b4, 2, 1, cirrus_vga_ioport_read, s);
    register_ioport_read(0x3d4, 2, 1, cirrus_vga_ioport_read, s);
    register_ioport_read(0x3ba, 1, 1, cirrus_vga_ioport_read, s);
    register_ioport_read(0x3da, 1, 1, cirrus_vga_ioport_read, s);

    s->vga.vga_io_memory = cpu_register_io_memory(cirrus_vga_mem_read,
                                                  cirrus_vga_mem_write, s,
                                                  DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(isa_mem_base + 0x000a0000, 0x20000,
                                 s->vga.vga_io_memory);
    qemu_register_coalesced_mmio(isa_mem_base + 0x000a0000, 0x20000);

    /* I/O handler for LFB */
    s->cirrus_linear_io_addr =
        cpu_register_io_memory(cirrus_linear_read, cirrus_linear_write, s,
                               DEVICE_LITTLE_ENDIAN);

    /* I/O handler for LFB */
    s->cirrus_linear_bitblt_io_addr =
        cpu_register_io_memory(cirrus_linear_bitblt_read,
                               cirrus_linear_bitblt_write, s,
                               DEVICE_LITTLE_ENDIAN);

    /* I/O handler for memory-mapped I/O */
    s->cirrus_mmio_io_addr =
        cpu_register_io_memory(cirrus_mmio_read, cirrus_mmio_write, s,
                               DEVICE_LITTLE_ENDIAN);

    s->real_vram_size =
        (s->device_id == CIRRUS_ID_CLGD5446) ? 4096 * 1024 : 2048 * 1024;

    /* XXX: s->vga.vram_size must be a power of two */
    s->cirrus_addr_mask = s->real_vram_size - 1;
    s->linear_mmio_mask = s->real_vram_size - 256;

    s->vga.get_bpp = cirrus_get_bpp;
    s->vga.get_offsets = cirrus_get_offsets;
    s->vga.get_resolution = cirrus_get_resolution;
    s->vga.cursor_invalidate = cirrus_cursor_invalidate;
    s->vga.cursor_draw_line = cirrus_cursor_draw_line;

    qemu_register_reset(cirrus_reset, s);
}

/***************************************
 *
 *  ISA bus support
 *
 ***************************************/

void isa_cirrus_vga_init(void)
{
    CirrusVGAState *s;

    s = qemu_mallocz(sizeof(CirrusVGAState));

    vga_common_init(&s->vga, VGA_RAM_SIZE);
    cirrus_init_common(s, CIRRUS_ID_CLGD5430, 0);
    s->vga.ds = graphic_console_init(s->vga.update, s->vga.invalidate,
                                     s->vga.screen_dump, s->vga.text_update,
                                     &s->vga);
    vmstate_register(NULL, 0, &vmstate_cirrus_vga, s);
    rom_add_vga(VGABIOS_CIRRUS_FILENAME);
    /* XXX ISA-LFB support */
}

/***************************************
 *
 *  PCI bus support
 *
 ***************************************/

static void cirrus_pci_lfb_map(PCIDevice *d, int region_num,
			       pcibus_t addr, pcibus_t size, int type)
{
    CirrusVGAState *s = &DO_UPCAST(PCICirrusVGAState, dev, d)->cirrus_vga;

    /* XXX: add byte swapping apertures */
    cpu_register_physical_memory(addr, s->vga.vram_size,
				 s->cirrus_linear_io_addr);
    cpu_register_physical_memory(addr + 0x1000000, 0x400000,
				 s->cirrus_linear_bitblt_io_addr);

    s->vga.map_addr = s->vga.map_end = 0;
    s->vga.lfb_addr = addr & TARGET_PAGE_MASK;
    s->vga.lfb_end = ((addr + VGA_RAM_SIZE) + TARGET_PAGE_SIZE - 1) & TARGET_PAGE_MASK;
    /* account for overflow */
    if (s->vga.lfb_end < addr + VGA_RAM_SIZE)
        s->vga.lfb_end = addr + VGA_RAM_SIZE;

    vga_dirty_log_start(&s->vga);
}

static void pci_cirrus_write_config(PCIDevice *d,
                                    uint32_t address, uint32_t val, int len)
{
    PCICirrusVGAState *pvs = DO_UPCAST(PCICirrusVGAState, dev, d);
    CirrusVGAState *s = &pvs->cirrus_vga;

    pci_default_write_config(d, address, val, len);
    if (s->vga.map_addr && d->io_regions[0].addr == PCI_BAR_UNMAPPED) {
        s->vga.map_addr = 0;
        s->vga.lfb_addr = 0;
        s->vga.lfb_end = 0;
    }
    cirrus_update_memory_access(s);
}

static int pci_cirrus_vga_initfn(PCIDevice *dev)
{
     PCICirrusVGAState *d = DO_UPCAST(PCICirrusVGAState, dev, dev);
     CirrusVGAState *s = &d->cirrus_vga;
     uint8_t *pci_conf = d->dev.config;
     int device_id = CIRRUS_ID_CLGD5446;

     /* setup VGA */
     vga_common_init(&s->vga, VGA_RAM_SIZE);
     cirrus_init_common(s, device_id, 1);
     s->vga.ds = graphic_console_init(s->vga.update, s->vga.invalidate,
                                      s->vga.screen_dump, s->vga.text_update,
                                      &s->vga);

     /* setup PCI */
     pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_CIRRUS);
     pci_config_set_device_id(pci_conf, device_id);
     pci_config_set_class(pci_conf, PCI_CLASS_DISPLAY_VGA);

     /* setup memory space */
     /* memory #0 LFB */
     /* memory #1 memory-mapped I/O */
     /* XXX: s->vga.vram_size must be a power of two */
     pci_register_bar(&d->dev, 0, 0x2000000,
                      PCI_BASE_ADDRESS_MEM_PREFETCH, cirrus_pci_lfb_map);
     if (device_id == CIRRUS_ID_CLGD5446) {
         pci_register_bar_simple(&d->dev, 1, CIRRUS_PNPMMIO_SIZE, 0,
                                 s->cirrus_mmio_io_addr);
     }
     return 0;
}

void pci_cirrus_vga_init(PCIBus *bus)
{
    pci_create_simple(bus, -1, "cirrus-vga");
}

static PCIDeviceInfo cirrus_vga_info = {
    .qdev.name    = "cirrus-vga",
    .qdev.desc    = "Cirrus CLGD 54xx VGA",
    .qdev.size    = sizeof(PCICirrusVGAState),
    .qdev.vmsd    = &vmstate_pci_cirrus_vga,
    .no_hotplug   = 1,
    .init         = pci_cirrus_vga_initfn,
    .romfile      = VGABIOS_CIRRUS_FILENAME,
    .config_write = pci_cirrus_write_config,
};

static void cirrus_vga_register(void)
{
    pci_qdev_register(&cirrus_vga_info);
}
device_init(cirrus_vga_register);
