/*
 * QEMU Cirrus VGA Emulator.
 * 
 * Copyright (c) 2004 Fabrice Bellard
 * Copyright (c) 2004 Suzu
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
#include "vl.h"
#include "vga_int.h"

//#define DEBUG_CIRRUS

/***************************************
 *
 *  definitions
 *
 ***************************************/

#define qemu_MIN(a,b) ((a) < (b) ? (a) : (b))

// ID
#define CIRRUS_ID_CLGD5422  (0x23<<2)
#define CIRRUS_ID_CLGD5426  (0x24<<2)
#define CIRRUS_ID_CLGD5424  (0x25<<2)
#define CIRRUS_ID_CLGD5428  (0x26<<2)
#define CIRRUS_ID_CLGD5430  (0x28<<2)
#define CIRRUS_ID_CLGD5434  (0x2A<<2)
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

// PCI 0x00: vendor, 0x02: device
#define PCI_VENDOR_CIRRUS             0x1013
#define PCI_DEVICE_CLGD5430           0x00a0	// CLGD5430 or CLGD5440
#define PCI_DEVICE_CLGD5434           0x00a8
#define PCI_DEVICE_CLGD5436           0x00ac
#define PCI_DEVICE_CLGD5446           0x00b8
#define PCI_DEVICE_CLGD5462           0x00d0
#define PCI_DEVICE_CLGD5465           0x00d6
// PCI 0x04: command(word), 0x06(word): status
#define PCI_COMMAND_IOACCESS                0x0001
#define PCI_COMMAND_MEMACCESS               0x0002
#define PCI_COMMAND_BUSMASTER               0x0004
#define PCI_COMMAND_SPECIALCYCLE            0x0008
#define PCI_COMMAND_MEMWRITEINVALID         0x0010
#define PCI_COMMAND_PALETTESNOOPING         0x0020
#define PCI_COMMAND_PARITYDETECTION         0x0040
#define PCI_COMMAND_ADDRESSDATASTEPPING     0x0080
#define PCI_COMMAND_SERR                    0x0100
#define PCI_COMMAND_BACKTOBACKTRANS         0x0200
// PCI 0x08, 0xff000000 (0x09-0x0b:class,0x08:rev)
#define PCI_CLASS_BASE_DISPLAY        0x03
// PCI 0x08, 0x00ff0000
#define PCI_CLASS_SUB_VGA             0x00
// PCI 0x0c, 0x00ff0000 (0x0c:cacheline,0x0d:latency,0x0e:headertype,0x0f:Built-in self test)
#define PCI_CLASS_HEADERTYPE_00h  0x00
// 0x10-0x3f (headertype 00h)
// PCI 0x10,0x14,0x18,0x1c,0x20,0x24: base address mapping registers
//   0x10: MEMBASE, 0x14: IOBASE(hard-coded in XFree86 3.x)
#define PCI_MAP_MEM                 0x0
#define PCI_MAP_IO                  0x1
#define PCI_MAP_MEM_ADDR_MASK       (~0xf)
#define PCI_MAP_IO_ADDR_MASK        (~0x3)
#define PCI_MAP_MEMFLAGS_32BIT      0x0
#define PCI_MAP_MEMFLAGS_32BIT_1M   0x1
#define PCI_MAP_MEMFLAGS_64BIT      0x4
#define PCI_MAP_MEMFLAGS_CACHEABLE  0x8
// PCI 0x28: cardbus CIS pointer
// PCI 0x2c: subsystem vendor id, 0x2e: subsystem id
// PCI 0x30: expansion ROM base address
#define PCI_ROMBIOS_ENABLED         0x1
// PCI 0x34: 0xffffff00=reserved, 0x000000ff=capabilities pointer
// PCI 0x38: reserved
// PCI 0x3c: 0x3c=int-line, 0x3d=int-pin, 0x3e=min-gnt, 0x3f=maax-lat

#define CIRRUS_PNPMMIO_SIZE         0x800


/* I/O and memory hook */
#define CIRRUS_HOOK_NOT_HANDLED 0
#define CIRRUS_HOOK_HANDLED 1

typedef void (*cirrus_bitblt_rop_t) (uint8_t * dst, const uint8_t * src,
				     int dstpitch, int srcpitch,
				     int bltwidth, int bltheight);

typedef void (*cirrus_bitblt_handler_t) (void *opaque);

typedef struct CirrusVGAState {
    /* XXX: we use the anonymous struct/union gcc 3.x extension */
    __extension__ struct VGAState;

    int cirrus_linear_io_addr;
    int cirrus_mmio_io_addr;
    uint32_t cirrus_addr_mask;
    uint8_t cirrus_shadow_gr0;
    uint8_t cirrus_shadow_gr1;
    uint8_t cirrus_hidden_dac_lockindex;
    uint8_t cirrus_hidden_dac_data;
    uint32_t cirrus_bank_base[2];
    uint32_t cirrus_bank_limit[2];
    uint8_t cirrus_hidden_palette[48];
    uint32_t cirrus_hw_cursor_x;
    uint32_t cirrus_hw_cursor_y;
    int cirrus_blt_pixelwidth;
    int cirrus_blt_width;
    int cirrus_blt_height;
    int cirrus_blt_dstpitch;
    int cirrus_blt_srcpitch;
    uint32_t cirrus_blt_dstaddr;
    uint32_t cirrus_blt_srcaddr;
    uint8_t cirrus_blt_mode;
    cirrus_bitblt_rop_t cirrus_rop;
#define CIRRUS_BLTBUFSIZE 256
    uint8_t cirrus_bltbuf[CIRRUS_BLTBUFSIZE];
    uint8_t *cirrus_srcptr;
    uint8_t *cirrus_srcptr_end;
    uint32_t cirrus_srccounter;
    uint8_t *cirrus_dstptr;
    uint8_t *cirrus_dstptr_end;
    uint32_t cirrus_dstcounter;
    cirrus_bitblt_handler_t cirrus_blt_handler;
    int cirrus_blt_horz_counter;
} CirrusVGAState;

typedef struct PCICirrusVGAState {
    PCIDevice dev;
    CirrusVGAState cirrus_vga;
} PCICirrusVGAState;

/***************************************
 *
 *  prototypes.
 *
 ***************************************/


static void cirrus_bitblt_reset(CirrusVGAState * s);

/***************************************
 *
 *  raster operations
 *
 ***************************************/

#define IMPLEMENT_FORWARD_BITBLT(name,opline) \
  static void \
  cirrus_bitblt_rop_fwd_##name( \
    uint8_t *dst,const uint8_t *src, \
    int dstpitch,int srcpitch, \
    int bltwidth,int bltheight) \
  { \
    int x,y; \
    dstpitch -= bltwidth; \
    srcpitch -= bltwidth; \
    for (y = 0; y < bltheight; y++) { \
      for (x = 0; x < bltwidth; x++) { \
        opline; \
        dst++; \
        src++; \
        } \
      dst += dstpitch; \
      src += srcpitch; \
      } \
    }

#define IMPLEMENT_BACKWARD_BITBLT(name,opline) \
  static void \
  cirrus_bitblt_rop_bkwd_##name( \
    uint8_t *dst,const uint8_t *src, \
    int dstpitch,int srcpitch, \
    int bltwidth,int bltheight) \
  { \
    int x,y; \
    dstpitch += bltwidth; \
    srcpitch += bltwidth; \
    for (y = 0; y < bltheight; y++) { \
      for (x = 0; x < bltwidth; x++) { \
        opline; \
        dst--; \
        src--; \
      } \
      dst += dstpitch; \
      src += srcpitch; \
    } \
  }

IMPLEMENT_FORWARD_BITBLT(0, *dst = 0)
    IMPLEMENT_FORWARD_BITBLT(src_and_dst, *dst = (*src) & (*dst))
    IMPLEMENT_FORWARD_BITBLT(nop, (void) 0)
    IMPLEMENT_FORWARD_BITBLT(src_and_notdst, *dst = (*src) & (~(*dst)))
    IMPLEMENT_FORWARD_BITBLT(notdst, *dst = ~(*dst))
    IMPLEMENT_FORWARD_BITBLT(src, *dst = *src)
    IMPLEMENT_FORWARD_BITBLT(1, *dst = 0xff)
    IMPLEMENT_FORWARD_BITBLT(notsrc_and_dst, *dst = (~(*src)) & (*dst))
    IMPLEMENT_FORWARD_BITBLT(src_xor_dst, *dst = (*src) ^ (*dst))
    IMPLEMENT_FORWARD_BITBLT(src_or_dst, *dst = (*src) | (*dst))
    IMPLEMENT_FORWARD_BITBLT(notsrc_or_notdst, *dst = (~(*src)) | (~(*dst)))
    IMPLEMENT_FORWARD_BITBLT(src_notxor_dst, *dst = ~((*src) ^ (*dst)))
    IMPLEMENT_FORWARD_BITBLT(src_or_notdst, *dst = (*src) | (~(*dst)))
    IMPLEMENT_FORWARD_BITBLT(notsrc, *dst = (~(*src)))
    IMPLEMENT_FORWARD_BITBLT(notsrc_or_dst, *dst = (~(*src)) | (*dst))
    IMPLEMENT_FORWARD_BITBLT(notsrc_and_notdst, *dst = (~(*src)) & (~(*dst)))

    IMPLEMENT_BACKWARD_BITBLT(0, *dst = 0)
    IMPLEMENT_BACKWARD_BITBLT(src_and_dst, *dst = (*src) & (*dst))
    IMPLEMENT_BACKWARD_BITBLT(nop, (void) 0)
    IMPLEMENT_BACKWARD_BITBLT(src_and_notdst, *dst = (*src) & (~(*dst)))
    IMPLEMENT_BACKWARD_BITBLT(notdst, *dst = ~(*dst))
    IMPLEMENT_BACKWARD_BITBLT(src, *dst = *src)
    IMPLEMENT_BACKWARD_BITBLT(1, *dst = 0xff)
    IMPLEMENT_BACKWARD_BITBLT(notsrc_and_dst, *dst = (~(*src)) & (*dst))
    IMPLEMENT_BACKWARD_BITBLT(src_xor_dst, *dst = (*src) ^ (*dst))
    IMPLEMENT_BACKWARD_BITBLT(src_or_dst, *dst = (*src) | (*dst))
    IMPLEMENT_BACKWARD_BITBLT(notsrc_or_notdst, *dst = (~(*src)) | (~(*dst)))
    IMPLEMENT_BACKWARD_BITBLT(src_notxor_dst, *dst = ~((*src) ^ (*dst)))
    IMPLEMENT_BACKWARD_BITBLT(src_or_notdst, *dst = (*src) | (~(*dst)))
    IMPLEMENT_BACKWARD_BITBLT(notsrc, *dst = (~(*src)))
    IMPLEMENT_BACKWARD_BITBLT(notsrc_or_dst, *dst = (~(*src)) | (*dst))
    IMPLEMENT_BACKWARD_BITBLT(notsrc_and_notdst, *dst = (~(*src)) & (~(*dst)))

static cirrus_bitblt_rop_t cirrus_get_fwd_rop_handler(uint8_t rop)
{
    cirrus_bitblt_rop_t rop_handler = cirrus_bitblt_rop_fwd_nop;

    switch (rop) {
    case CIRRUS_ROP_0:
	rop_handler = cirrus_bitblt_rop_fwd_0;
	break;
    case CIRRUS_ROP_SRC_AND_DST:
	rop_handler = cirrus_bitblt_rop_fwd_src_and_dst;
	break;
    case CIRRUS_ROP_NOP:
	rop_handler = cirrus_bitblt_rop_fwd_nop;
	break;
    case CIRRUS_ROP_SRC_AND_NOTDST:
	rop_handler = cirrus_bitblt_rop_fwd_src_and_notdst;
	break;
    case CIRRUS_ROP_NOTDST:
	rop_handler = cirrus_bitblt_rop_fwd_notdst;
	break;
    case CIRRUS_ROP_SRC:
	rop_handler = cirrus_bitblt_rop_fwd_src;
	break;
    case CIRRUS_ROP_1:
	rop_handler = cirrus_bitblt_rop_fwd_1;
	break;
    case CIRRUS_ROP_NOTSRC_AND_DST:
	rop_handler = cirrus_bitblt_rop_fwd_notsrc_and_dst;
	break;
    case CIRRUS_ROP_SRC_XOR_DST:
	rop_handler = cirrus_bitblt_rop_fwd_src_xor_dst;
	break;
    case CIRRUS_ROP_SRC_OR_DST:
	rop_handler = cirrus_bitblt_rop_fwd_src_or_dst;
	break;
    case CIRRUS_ROP_NOTSRC_OR_NOTDST:
	rop_handler = cirrus_bitblt_rop_fwd_notsrc_or_notdst;
	break;
    case CIRRUS_ROP_SRC_NOTXOR_DST:
	rop_handler = cirrus_bitblt_rop_fwd_src_notxor_dst;
	break;
    case CIRRUS_ROP_SRC_OR_NOTDST:
	rop_handler = cirrus_bitblt_rop_fwd_src_or_notdst;
	break;
    case CIRRUS_ROP_NOTSRC:
	rop_handler = cirrus_bitblt_rop_fwd_notsrc;
	break;
    case CIRRUS_ROP_NOTSRC_OR_DST:
	rop_handler = cirrus_bitblt_rop_fwd_notsrc_or_dst;
	break;
    case CIRRUS_ROP_NOTSRC_AND_NOTDST:
	rop_handler = cirrus_bitblt_rop_fwd_notsrc_and_notdst;
	break;
    default:
#ifdef DEBUG_CIRRUS
	printf("unknown ROP %02x\n", rop);
#endif
	break;
    }

    return rop_handler;
}

static cirrus_bitblt_rop_t cirrus_get_bkwd_rop_handler(uint8_t rop)
{
    cirrus_bitblt_rop_t rop_handler = cirrus_bitblt_rop_bkwd_nop;

    switch (rop) {
    case CIRRUS_ROP_0:
	rop_handler = cirrus_bitblt_rop_bkwd_0;
	break;
    case CIRRUS_ROP_SRC_AND_DST:
	rop_handler = cirrus_bitblt_rop_bkwd_src_and_dst;
	break;
    case CIRRUS_ROP_NOP:
	rop_handler = cirrus_bitblt_rop_bkwd_nop;
	break;
    case CIRRUS_ROP_SRC_AND_NOTDST:
	rop_handler = cirrus_bitblt_rop_bkwd_src_and_notdst;
	break;
    case CIRRUS_ROP_NOTDST:
	rop_handler = cirrus_bitblt_rop_bkwd_notdst;
	break;
    case CIRRUS_ROP_SRC:
	rop_handler = cirrus_bitblt_rop_bkwd_src;
	break;
    case CIRRUS_ROP_1:
	rop_handler = cirrus_bitblt_rop_bkwd_1;
	break;
    case CIRRUS_ROP_NOTSRC_AND_DST:
	rop_handler = cirrus_bitblt_rop_bkwd_notsrc_and_dst;
	break;
    case CIRRUS_ROP_SRC_XOR_DST:
	rop_handler = cirrus_bitblt_rop_bkwd_src_xor_dst;
	break;
    case CIRRUS_ROP_SRC_OR_DST:
	rop_handler = cirrus_bitblt_rop_bkwd_src_or_dst;
	break;
    case CIRRUS_ROP_NOTSRC_OR_NOTDST:
	rop_handler = cirrus_bitblt_rop_bkwd_notsrc_or_notdst;
	break;
    case CIRRUS_ROP_SRC_NOTXOR_DST:
	rop_handler = cirrus_bitblt_rop_bkwd_src_notxor_dst;
	break;
    case CIRRUS_ROP_SRC_OR_NOTDST:
	rop_handler = cirrus_bitblt_rop_bkwd_src_or_notdst;
	break;
    case CIRRUS_ROP_NOTSRC:
	rop_handler = cirrus_bitblt_rop_bkwd_notsrc;
	break;
    case CIRRUS_ROP_NOTSRC_OR_DST:
	rop_handler = cirrus_bitblt_rop_bkwd_notsrc_or_dst;
	break;
    case CIRRUS_ROP_NOTSRC_AND_NOTDST:
	rop_handler = cirrus_bitblt_rop_bkwd_notsrc_and_notdst;
	break;
    default:
#ifdef DEBUG_CIRRUS
	printf("unknown ROP %02x\n", rop);
#endif
	break;
    }

    return rop_handler;
}

/***************************************
 *
 *  color expansion
 *
 ***************************************/

static void
cirrus_colorexpand_8(CirrusVGAState * s, uint8_t * dst,
		     const uint8_t * src, int count)
{
    int x;
    uint8_t colors[2];
    unsigned bits;
    unsigned bitmask;
    int srcskipleft = 0;

    colors[0] = s->gr[0x00];
    colors[1] = s->gr[0x01];

    bitmask = 0x80 >> srcskipleft;
    bits = *src++;
    for (x = 0; x < count; x++) {
	if ((bitmask & 0xff) == 0) {
	    bitmask = 0x80;
	    bits = *src++;
	}
	*dst++ = colors[!!(bits & bitmask)];
	bitmask >>= 1;
    }
}

static void
cirrus_colorexpand_16(CirrusVGAState * s, uint8_t * dst,
		      const uint8_t * src, int count)
{
    int x;
    uint8_t colors[2][2];
    unsigned bits;
    unsigned bitmask;
    unsigned index;
    int srcskipleft = 0;

    colors[0][0] = s->gr[0x00];
    colors[0][1] = s->gr[0x10];
    colors[1][0] = s->gr[0x01];
    colors[1][1] = s->gr[0x11];

    bitmask = 0x80 >> srcskipleft;
    bits = *src++;
    for (x = 0; x < count; x++) {
	if ((bitmask & 0xff) == 0) {
	    bitmask = 0x80;
	    bits = *src++;
	}
	index = !!(bits & bitmask);
	*dst++ = colors[index][0];
	*dst++ = colors[index][1];
	bitmask >>= 1;
    }
}

static void
cirrus_colorexpand_24(CirrusVGAState * s, uint8_t * dst,
		      const uint8_t * src, int count)
{
    int x;
    uint8_t colors[2][3];
    unsigned bits;
    unsigned bitmask;
    unsigned index;
    int srcskipleft = 0;

    colors[0][0] = s->gr[0x00];
    colors[0][1] = s->gr[0x10];
    colors[0][2] = s->gr[0x12];
    colors[1][0] = s->gr[0x01];
    colors[1][1] = s->gr[0x11];
    colors[1][2] = s->gr[0x13];

    bitmask = 0x80 << srcskipleft;
    bits = *src++;
    for (x = 0; x < count; x++) {
	if ((bitmask & 0xff) == 0) {
	    bitmask = 0x80;
	    bits = *src++;
	}
	index = !!(bits & bitmask);
	*dst++ = colors[index][0];
	*dst++ = colors[index][1];
	*dst++ = colors[index][2];
	bitmask >>= 1;
    }
}

static void
cirrus_colorexpand_32(CirrusVGAState * s, uint8_t * dst,
		      const uint8_t * src, int count)
{
    int x;
    uint8_t colors[2][4];
    unsigned bits;
    unsigned bitmask;
    unsigned index;
    int srcskipleft = 0;

    colors[0][0] = s->gr[0x00];
    colors[0][1] = s->gr[0x10];
    colors[0][2] = s->gr[0x12];
    colors[0][3] = s->gr[0x14];
    colors[1][0] = s->gr[0x01];
    colors[1][1] = s->gr[0x11];
    colors[1][2] = s->gr[0x13];
    colors[1][3] = s->gr[0x15];

    bitmask = 0x80 << srcskipleft;
    bits = *src++;
    for (x = 0; x < count; x++) {
	if ((bitmask & 0xff) == 0) {
	    bitmask = 0x80;
	    bits = *src++;
	}
	index = !!(bits & bitmask);
	*dst++ = colors[index][0];
	*dst++ = colors[index][1];
	*dst++ = colors[index][2];
	*dst++ = colors[index][3];
	bitmask >>= 1;
    }
}

static void
cirrus_colorexpand(CirrusVGAState * s, uint8_t * dst, const uint8_t * src,
		   int count)
{
    switch (s->cirrus_blt_pixelwidth) {
    case 1:
	cirrus_colorexpand_8(s, dst, src, count);
	break;
    case 2:
	cirrus_colorexpand_16(s, dst, src, count);
	break;
    case 3:
	cirrus_colorexpand_24(s, dst, src, count);
	break;
    case 4:
	cirrus_colorexpand_32(s, dst, src, count);
	break;
    default:
#ifdef DEBUG_CIRRUS
	printf("cirrus: COLOREXPAND pixelwidth %d - unimplemented\n",
	       s->cirrus_blt_pixelwidth);
#endif
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
	off_cur_end = off_cur + bytesperline;
	off_cur &= TARGET_PAGE_MASK;
	while (off_cur < off_cur_end) {
	    cpu_physical_memory_set_dirty(s->vram_offset + off_cur);
	    off_cur += TARGET_PAGE_SIZE;
	}
	off_begin += off_pitch;
    }
}



static int cirrus_bitblt_common_patterncopy(CirrusVGAState * s,
					    const uint8_t * src)
{
    uint8_t work_colorexp[256];
    uint8_t *dst;
    uint8_t *dstc;
    int x, y;
    int tilewidth, tileheight;
    int patternbytes = s->cirrus_blt_pixelwidth * 8;

    if (s->cirrus_blt_mode & CIRRUS_BLTMODE_COLOREXPAND) {
	cirrus_colorexpand(s, work_colorexp, src, 8 * 8);
	src = work_colorexp;
	s->cirrus_blt_mode &= ~CIRRUS_BLTMODE_COLOREXPAND;
    }
    if (s->cirrus_blt_mode & ~CIRRUS_BLTMODE_PATTERNCOPY) {
#ifdef DEBUG_CIRRUS
	printf("cirrus: blt mode %02x (pattercopy) - unimplemented\n",
	       s->cirrus_blt_mode);
#endif
	return 0;
    }

    dst = s->vram_ptr + s->cirrus_blt_dstaddr;
    for (y = 0; y < s->cirrus_blt_height; y += 8) {
	dstc = dst;
	tileheight = qemu_MIN(8, s->cirrus_blt_height - y);
	for (x = 0; x < s->cirrus_blt_width; x += patternbytes) {
	    tilewidth = qemu_MIN(patternbytes, s->cirrus_blt_width - x);
	    (*s->cirrus_rop) (dstc, src,
			      s->cirrus_blt_dstpitch, patternbytes,
			      tilewidth, tileheight);
	    dstc += patternbytes;
	}
	dst += s->cirrus_blt_dstpitch * 8;
    }
    cirrus_invalidate_region(s, s->cirrus_blt_dstaddr,
			     s->cirrus_blt_dstpitch, s->cirrus_blt_width,
			     s->cirrus_blt_height);
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
					    s->vram_ptr +
					    s->cirrus_blt_srcaddr);
}

static int cirrus_bitblt_videotovideo_copy(CirrusVGAState * s)
{
    if ((s->cirrus_blt_mode & CIRRUS_BLTMODE_COLOREXPAND) != 0) {
#ifdef DEBUG_CIRRUS
	printf("cirrus: CIRRUS_BLTMODE_COLOREXPAND - unimplemented\n");
#endif
	return 0;
    }
    if ((s->cirrus_blt_mode & (~CIRRUS_BLTMODE_BACKWARDS)) != 0) {
#ifdef DEBUG_CIRRUS
	printf("cirrus: blt mode %02x - unimplemented\n",
	       s->cirrus_blt_mode);
#endif
	return 0;
    }

    (*s->cirrus_rop) (s->vram_ptr + s->cirrus_blt_dstaddr,
		      s->vram_ptr + s->cirrus_blt_srcaddr,
		      s->cirrus_blt_dstpitch, s->cirrus_blt_srcpitch,
		      s->cirrus_blt_width, s->cirrus_blt_height);
    cirrus_invalidate_region(s, s->cirrus_blt_dstaddr,
			     s->cirrus_blt_dstpitch, s->cirrus_blt_width,
			     s->cirrus_blt_height);
    return 1;
}

/***************************************
 *
 *  bitblt (cpu-to-video)
 *
 ***************************************/

static void cirrus_bitblt_cputovideo_patterncopy(void *opaque)
{
    CirrusVGAState *s = (CirrusVGAState *) opaque;
    int data_count;

    data_count = s->cirrus_srcptr - &s->cirrus_bltbuf[0];

    if (data_count > 0) {
	if (data_count != s->cirrus_srccounter) {
#ifdef DEBUG_CIRRUS
	    printf("cirrus: internal error\n");
#endif
	} else {
	    cirrus_bitblt_common_patterncopy(s, &s->cirrus_bltbuf[0]);
	}
	cirrus_bitblt_reset(s);
    }
}

static void cirrus_bitblt_cputovideo_copy(void *opaque)
{
    CirrusVGAState *s = (CirrusVGAState *) opaque;
    int data_count;
    int data_avail;
    uint8_t work_colorexp[256];
    uint8_t *src_ptr = NULL;
    int src_avail = 0;
    int src_processing;
    int src_linepad = 0;

    if (s->cirrus_blt_height <= 0) {
	s->cirrus_srcptr = s->cirrus_srcptr_end;
	return;
    }

    s->cirrus_srcptr = &s->cirrus_bltbuf[0];
    while (1) {
	/* get BLT source. */
	if (src_avail <= 0) {
	    data_count = s->cirrus_srcptr_end - s->cirrus_srcptr;
	    if (data_count <= 0)
		break;

	    if (s->cirrus_blt_mode & CIRRUS_BLTMODE_COLOREXPAND) {
		if (s->cirrus_blt_mode & ~CIRRUS_BLTMODE_COLOREXPAND) {
#ifdef DEBUG_CIRRUS
		    printf("cirrus: unsupported\n");
#endif
		    cirrus_bitblt_reset(s);
		    return;
		}
		data_avail = qemu_MIN(data_count, 256 / 32);
		cirrus_colorexpand(s, work_colorexp, s->cirrus_srcptr,
				   data_avail * 8);
		src_ptr = &work_colorexp[0];
		src_avail = data_avail * 8 * s->cirrus_blt_pixelwidth;
		s->cirrus_srcptr += data_avail;
		src_linepad =
		    ((s->cirrus_blt_width + 7) / 8) * 8 -
		    s->cirrus_blt_width;
		src_linepad *= s->cirrus_blt_pixelwidth;
	    } else {
		if (s->cirrus_blt_mode != 0) {
#ifdef DEBUG_CIRRUS
		    printf("cirrus: unsupported\n");
#endif
		    cirrus_bitblt_reset(s);
		    return;
		}
		src_ptr = s->cirrus_srcptr;
		src_avail =
		    data_count / s->cirrus_blt_pixelwidth *
		    s->cirrus_blt_pixelwidth;
		s->cirrus_srcptr += src_avail;
	    }
	    if (src_avail <= 0)
		break;
	}

	/* 1-line BLT */
	src_processing =
	    s->cirrus_blt_srcpitch - s->cirrus_blt_horz_counter;
	src_processing = qemu_MIN(src_avail, src_processing);
	(*s->cirrus_rop) (s->vram_ptr + s->cirrus_blt_dstaddr,
			  src_ptr, 0, 0, src_processing, 1);
	cirrus_invalidate_region(s, s->cirrus_blt_dstaddr, 0,
				 src_processing, 1);

	s->cirrus_blt_dstaddr += src_processing;
	src_ptr += src_processing;
	src_avail -= src_processing;
	s->cirrus_blt_horz_counter += src_processing;
	if (s->cirrus_blt_horz_counter >= s->cirrus_blt_srcpitch) {
	    src_ptr += src_linepad;
	    src_avail -= src_linepad;
	    s->cirrus_blt_dstaddr +=
		s->cirrus_blt_dstpitch - s->cirrus_blt_srcpitch;
	    s->cirrus_blt_horz_counter = 0;
	    s->cirrus_blt_height--;
	    if (s->cirrus_blt_height <= 0) {
		s->cirrus_srcptr = s->cirrus_srcptr_end;
		return;
	    }
	}
    }
}

static void cirrus_bitblt_cputovideo_next(CirrusVGAState * s)
{
    int copy_count;
    int avail_count;

    s->cirrus_blt_handler(s);

    if (s->cirrus_srccounter > 0) {
	s->cirrus_srccounter -= s->cirrus_srcptr - &s->cirrus_bltbuf[0];
	copy_count = s->cirrus_srcptr_end - s->cirrus_srcptr;
	memmove(&s->cirrus_bltbuf[0], s->cirrus_srcptr, copy_count);
	avail_count = qemu_MIN(CIRRUS_BLTBUFSIZE, s->cirrus_srccounter);
	s->cirrus_srcptr = &s->cirrus_bltbuf[0];
	s->cirrus_srcptr_end = s->cirrus_srcptr + avail_count;
	if (s->cirrus_srccounter <= 0) {
	    cirrus_bitblt_reset(s);
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
    s->gr[0x31] &=
	~(CIRRUS_BLT_START | CIRRUS_BLT_BUSY | CIRRUS_BLT_FIFOUSED);
    s->cirrus_srcptr = &s->cirrus_bltbuf[0];
    s->cirrus_srcptr_end = &s->cirrus_bltbuf[0];
    s->cirrus_srccounter = 0;
    s->cirrus_dstptr = &s->cirrus_bltbuf[0];
    s->cirrus_dstptr_end = &s->cirrus_bltbuf[0];
    s->cirrus_dstcounter = 0;
    s->cirrus_blt_handler = NULL;
}

static int cirrus_bitblt_cputovideo(CirrusVGAState * s)
{
    s->cirrus_blt_mode &= ~CIRRUS_BLTMODE_MEMSYSSRC;
    s->cirrus_srcptr = &s->cirrus_bltbuf[0];
    s->cirrus_srcptr_end = &s->cirrus_bltbuf[0];

    if (s->cirrus_blt_mode & CIRRUS_BLTMODE_PATTERNCOPY) {
	if (s->cirrus_blt_mode & CIRRUS_BLTMODE_COLOREXPAND) {
	    s->cirrus_srccounter = 8;
	} else {
	    s->cirrus_srccounter = 8 * 8 * s->cirrus_blt_pixelwidth;
	}
	s->cirrus_blt_srcpitch = 0;
	s->cirrus_blt_handler = cirrus_bitblt_cputovideo_patterncopy;
    } else {
	if (s->cirrus_blt_mode & CIRRUS_BLTMODE_COLOREXPAND) {
	    s->cirrus_srccounter =
		((s->cirrus_blt_width + 7) / 8) * s->cirrus_blt_height;
	    s->cirrus_blt_srcpitch =
		s->cirrus_blt_width * s->cirrus_blt_pixelwidth;
	} else {
	    s->cirrus_srccounter =
		s->cirrus_blt_width * s->cirrus_blt_height;
	    s->cirrus_blt_srcpitch = s->cirrus_blt_width;
	}
	/* 4-byte alignment */
	s->cirrus_srccounter = (s->cirrus_srccounter + 3) & (~3);

	s->cirrus_blt_handler = cirrus_bitblt_cputovideo_copy;
	s->cirrus_blt_horz_counter = 0;
    }

    cirrus_bitblt_cputovideo_next(s);
    return 1;
}

static int cirrus_bitblt_videotocpu(CirrusVGAState * s)
{
    /* XXX */
#ifdef DEBUG_CIRRUS
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

    s->cirrus_blt_width = (s->gr[0x20] | (s->gr[0x21] << 8)) + 1;
    s->cirrus_blt_height = (s->gr[0x22] | (s->gr[0x23] << 8)) + 1;
    s->cirrus_blt_dstpitch = (s->gr[0x24] | (s->gr[0x25] << 8));
    s->cirrus_blt_srcpitch = (s->gr[0x26] | (s->gr[0x27] << 8));
    s->cirrus_blt_dstaddr =
	(s->gr[0x28] | (s->gr[0x29] << 8) | (s->gr[0x2a] << 16));
    s->cirrus_blt_srcaddr =
	(s->gr[0x2c] | (s->gr[0x2d] << 8) | (s->gr[0x2e] << 16));
    s->cirrus_blt_mode = s->gr[0x30];
    blt_rop = s->gr[0x32];

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
#ifdef DEBUG_CIRRUS
	printf("cirrus: bitblt - pixel width is unknown\n");
#endif
	goto bitblt_ignore;
    }
    s->cirrus_blt_mode &= ~CIRRUS_BLTMODE_PIXELWIDTHMASK;

    if ((s->
	 cirrus_blt_mode & (CIRRUS_BLTMODE_MEMSYSSRC |
			    CIRRUS_BLTMODE_MEMSYSDEST))
	== (CIRRUS_BLTMODE_MEMSYSSRC | CIRRUS_BLTMODE_MEMSYSDEST)) {
#ifdef DEBUG_CIRRUS
	printf("cirrus: bitblt - memory-to-memory copy is requested\n");
#endif
	goto bitblt_ignore;
    }

    if (s->cirrus_blt_mode & CIRRUS_BLTMODE_BACKWARDS) {
	s->cirrus_blt_dstpitch = -s->cirrus_blt_dstpitch;
	s->cirrus_blt_srcpitch = -s->cirrus_blt_srcpitch;
	s->cirrus_rop = cirrus_get_bkwd_rop_handler(blt_rop);
    } else {
	s->cirrus_rop = cirrus_get_fwd_rop_handler(blt_rop);
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

    return;
  bitblt_ignore:;
    cirrus_bitblt_reset(s);
}

static void cirrus_write_bitblt(CirrusVGAState * s, unsigned reg_value)
{
    unsigned old_value;

    old_value = s->gr[0x31];
    s->gr[0x31] = reg_value;

    if (((old_value & CIRRUS_BLT_RESET) != 0) &&
	((reg_value & CIRRUS_BLT_RESET) == 0)) {
	cirrus_bitblt_reset(s);
    } else if (((old_value & CIRRUS_BLT_START) == 0) &&
	       ((reg_value & CIRRUS_BLT_START) != 0)) {
	s->gr[0x31] |= CIRRUS_BLT_BUSY;
	cirrus_bitblt_start(s);
    }
}


/***************************************
 *
 *  basic parameters
 *
 ***************************************/

static void cirrus_get_offsets(VGAState *s1, 
                                   uint32_t *pline_offset,
                                   uint32_t *pstart_addr)
{
    CirrusVGAState * s = (CirrusVGAState *)s1;
    uint32_t start_addr;
    uint32_t line_offset;

    line_offset = s->cr[0x13]
	| ((s->cr[0x1b] & 0x10) << 4);
    line_offset <<= 3;
    *pline_offset = line_offset;

    start_addr = (s->cr[0x0c] << 8)
	| s->cr[0x0d]
	| ((s->cr[0x1b] & 0x01) << 16)
	| ((s->cr[0x1b] & 0x0c) << 15)
	| ((s->cr[0x1d] & 0x80) << 12);
    *pstart_addr = start_addr;
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

static int cirrus_get_bpp(VGAState *s1)
{
    CirrusVGAState * s = (CirrusVGAState *)s1;
    uint32_t ret = 8;

    if ((s->sr[0x07] & 0x01) != 0) {
	/* Cirrus SVGA */
	switch (s->sr[0x07] & CIRRUS_SR7_BPP_MASK) {
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
	    printf("cirrus: unknown bpp - sr7=%x\n", s->sr[0x7]);
#endif
	    ret = 8;
	    break;
	}
    } else {
	/* VGA */
	ret = 8;
    }

    return ret;
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

    if ((s->gr[0x0b] & 0x01) != 0)	/* dual bank */
	offset = s->gr[0x09 + bank_index];
    else			/* single bank */
	offset = s->gr[0x09];

    if ((s->gr[0x0b] & 0x20) != 0)
	offset <<= 14;
    else
	offset <<= 12;

    if (s->vram_size <= offset)
	limit = 0;
    else
	limit = s->vram_size - offset;

    if (((s->gr[0x0b] & 0x01) == 0) && (bank_index != 0)) {
	if (limit > 0x8000) {
	    offset += 0x8000;
	    limit -= 0x8000;
	} else {
	    limit = 0;
	}
    }

    if (limit > 0) {
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

static int
cirrus_hook_read_sr(CirrusVGAState * s, unsigned reg_index, int *reg_value)
{
    switch (reg_index) {
    case 0x00:			// Standard VGA
    case 0x01:			// Standard VGA
    case 0x02:			// Standard VGA
    case 0x03:			// Standard VGA
    case 0x04:			// Standard VGA
	return CIRRUS_HOOK_NOT_HANDLED;
    case 0x06:			// Unlock Cirrus extensions
	*reg_value = s->sr[reg_index];
	break;
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
    case 0x10:
    case 0x30:
    case 0x50:
    case 0x70:			// Graphics Cursor X
    case 0x90:
    case 0xb0:
    case 0xd0:
    case 0xf0:			// Graphics Cursor X
    case 0x11:
    case 0x31:
    case 0x51:
    case 0x71:			// Graphics Cursor Y
    case 0x91:
    case 0xb1:
    case 0xd1:
    case 0xf1:			// Graphics Cursor Y
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
	printf("cirrus: handled inport sr_index %02x\n", reg_index);
#endif
	*reg_value = s->sr[reg_index];
	break;
    default:
#ifdef DEBUG_CIRRUS
	printf("cirrus: inport sr_index %02x\n", reg_index);
#endif
	*reg_value = 0xff;
	break;
    }

    return CIRRUS_HOOK_HANDLED;
}

static int
cirrus_hook_write_sr(CirrusVGAState * s, unsigned reg_index, int reg_value)
{
    switch (reg_index) {
    case 0x00:			// Standard VGA
    case 0x01:			// Standard VGA
    case 0x02:			// Standard VGA
    case 0x03:			// Standard VGA
    case 0x04:			// Standard VGA
	return CIRRUS_HOOK_NOT_HANDLED;
    case 0x06:			// Unlock Cirrus extensions
	reg_value &= 0x17;
	if (reg_value == 0x12) {
	    s->sr[reg_index] = 0x12;
	} else {
	    s->sr[reg_index] = 0x0f;
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
	s->sr[0x10] = reg_value;
	s->cirrus_hw_cursor_x = ((reg_index << 3) & 0x700) | reg_value;
	break;
    case 0x11:
    case 0x31:
    case 0x51:
    case 0x71:			// Graphics Cursor Y
    case 0x91:
    case 0xb1:
    case 0xd1:
    case 0xf1:			// Graphics Cursor Y
	s->sr[0x11] = reg_value;
	s->cirrus_hw_cursor_y = ((reg_index << 3) & 0x700) | reg_value;
	break;
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
    case 0x19:			// Signature Generator Result
    case 0x1a:			// Signature Generator Result
    case 0x1b:			// VCLK 0 Denominator & Post
    case 0x1c:			// VCLK 1 Denominator & Post
    case 0x1d:			// VCLK 2 Denominator & Post
    case 0x1e:			// VCLK 3 Denominator & Post
    case 0x1f:			// BIOS Write Enable and MCLK select
	s->sr[reg_index] = reg_value;
#ifdef DEBUG_CIRRUS
	printf("cirrus: handled outport sr_index %02x, sr_value %02x\n",
	       reg_index, reg_value);
#endif
	break;
    default:
#ifdef DEBUG_CIRRUS
	printf("cirrus: outport sr_index %02x, sr_value %02x\n", reg_index,
	       reg_value);
#endif
	break;
    }

    return CIRRUS_HOOK_HANDLED;
}

/***************************************
 *
 *  I/O access at 0x3c6
 *
 ***************************************/

static void cirrus_read_hidden_dac(CirrusVGAState * s, int *reg_value)
{
    *reg_value = 0xff;
    if (s->cirrus_hidden_dac_lockindex < 5) {
	if (s->cirrus_hidden_dac_lockindex == 4) {
	    *reg_value = s->cirrus_hidden_dac_data;
	}
	s->cirrus_hidden_dac_lockindex++;
    }
}

static void cirrus_write_hidden_dac(CirrusVGAState * s, int reg_value)
{
    if (s->cirrus_hidden_dac_lockindex == 4) {
	s->cirrus_hidden_dac_data = reg_value;
#ifdef DEBUG_CIRRUS
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

static int cirrus_hook_read_palette(CirrusVGAState * s, int *reg_value)
{
    if (!(s->sr[0x12] & CIRRUS_CURSOR_HIDDENPEL))
	return CIRRUS_HOOK_NOT_HANDLED;
    if (s->dac_read_index < 0x10) {
	*reg_value =
	    s->cirrus_hidden_palette[s->dac_read_index * 3 +
				     s->dac_sub_index];
    } else {
	*reg_value = 0xff;	/* XXX */
    }
    if (++s->dac_sub_index == 3) {
	s->dac_sub_index = 0;
	s->dac_read_index++;
    }
    return CIRRUS_HOOK_HANDLED;
}

static int cirrus_hook_write_palette(CirrusVGAState * s, int reg_value)
{
    if (!(s->sr[0x12] & CIRRUS_CURSOR_HIDDENPEL))
	return CIRRUS_HOOK_NOT_HANDLED;
    s->dac_cache[s->dac_sub_index] = reg_value;
    if (++s->dac_sub_index == 3) {
	if (s->dac_read_index < 0x10) {
	    memcpy(&s->cirrus_hidden_palette[s->dac_write_index * 3],
		   s->dac_cache, 3);
	    /* XXX update cursor */
	}
	s->dac_sub_index = 0;
	s->dac_write_index++;
    }
    return CIRRUS_HOOK_HANDLED;
}

/***************************************
 *
 *  I/O access between 0x3ce-0x3cf
 *
 ***************************************/

static int
cirrus_hook_read_gr(CirrusVGAState * s, unsigned reg_index, int *reg_value)
{
    switch (reg_index) {
    case 0x02:			// Standard VGA
    case 0x03:			// Standard VGA
    case 0x04:			// Standard VGA
    case 0x06:			// Standard VGA
    case 0x07:			// Standard VGA
    case 0x08:			// Standard VGA
	return CIRRUS_HOOK_NOT_HANDLED;
    case 0x05:			// Standard VGA, Cirrus extended mode
    default:
	break;
    }

    if (reg_index < 0x3a) {
	*reg_value = s->gr[reg_index];
    } else {
#ifdef DEBUG_CIRRUS
	printf("cirrus: inport gr_index %02x\n", reg_index);
#endif
	*reg_value = 0xff;
    }

    return CIRRUS_HOOK_HANDLED;
}

static int
cirrus_hook_write_gr(CirrusVGAState * s, unsigned reg_index, int reg_value)
{
    switch (reg_index) {
    case 0x00:			// Standard VGA, BGCOLOR 0x000000ff
	s->gr[0x00] = reg_value;
	return CIRRUS_HOOK_NOT_HANDLED;
    case 0x01:			// Standard VGA, FGCOLOR 0x000000ff
	s->gr[0x01] = reg_value;
	return CIRRUS_HOOK_NOT_HANDLED;
    case 0x02:			// Standard VGA
    case 0x03:			// Standard VGA
    case 0x04:			// Standard VGA
    case 0x06:			// Standard VGA
    case 0x07:			// Standard VGA
    case 0x08:			// Standard VGA
	return CIRRUS_HOOK_NOT_HANDLED;
    case 0x05:			// Standard VGA, Cirrus extended mode
	s->gr[reg_index] = reg_value & 0x7f;
	break;
    case 0x09:			// bank offset #0
    case 0x0A:			// bank offset #1
    case 0x0B:
	s->gr[reg_index] = reg_value;
	cirrus_update_bank_ptr(s, 0);
	cirrus_update_bank_ptr(s, 1);
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
    case 0x30:			// BLT MODE
    case 0x32:			// RASTER OP
    case 0x34:			// BLT TRANSPARENT COLOR 0x00ff
    case 0x35:			// BLT TRANSPARENT COLOR 0xff00
    case 0x38:			// BLT TRANSPARENT COLOR MASK 0x00ff
    case 0x39:			// BLT TRANSPARENT COLOR MASK 0xff00
	s->gr[reg_index] = reg_value;
	break;
    case 0x21:			// BLT WIDTH 0x001f00
    case 0x23:			// BLT HEIGHT 0x001f00
    case 0x25:			// BLT DEST PITCH 0x001f00
    case 0x27:			// BLT SRC PITCH 0x001f00
	s->gr[reg_index] = reg_value & 0x1f;
	break;
    case 0x2a:			// BLT DEST ADDR 0x3f0000
    case 0x2e:			// BLT SRC ADDR 0x3f0000
	s->gr[reg_index] = reg_value & 0x3f;
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

    return CIRRUS_HOOK_HANDLED;
}

/***************************************
 *
 *  I/O access between 0x3d4-0x3d5
 *
 ***************************************/

static int
cirrus_hook_read_cr(CirrusVGAState * s, unsigned reg_index, int *reg_value)
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
	return CIRRUS_HOOK_NOT_HANDLED;
    case 0x19:			// Interlace End
    case 0x1a:			// Miscellaneous Control
    case 0x1b:			// Extended Display Control
    case 0x1c:			// Sync Adjust and Genlock
    case 0x1d:			// Overlay Extended Control
    case 0x22:			// Graphics Data Latches Readback (R)
    case 0x24:			// Attribute Controller Toggle Readback (R)
    case 0x25:			// Part Status
    case 0x27:			// Part ID (R)
	*reg_value = s->cr[reg_index];
	break;
    case 0x26:			// Attribute Controller Index Readback (R)
	*reg_value = s->ar_index & 0x3f;
	break;
    default:
#ifdef DEBUG_CIRRUS
	printf("cirrus: inport cr_index %02x\n", reg_index);
	*reg_value = 0xff;
#endif
	break;
    }

    return CIRRUS_HOOK_HANDLED;
}

static int
cirrus_hook_write_cr(CirrusVGAState * s, unsigned reg_index, int reg_value)
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
	return CIRRUS_HOOK_NOT_HANDLED;
    case 0x19:			// Interlace End
    case 0x1a:			// Miscellaneous Control
    case 0x1b:			// Extended Display Control
    case 0x1c:			// Sync Adjust and Genlock
	s->cr[reg_index] = reg_value;
#ifdef DEBUG_CIRRUS
	printf("cirrus: handled outport cr_index %02x, cr_value %02x\n",
	       reg_index, reg_value);
#endif
	break;
    case 0x22:			// Graphics Data Latches Readback (R)
    case 0x24:			// Attribute Controller Toggle Readback (R)
    case 0x26:			// Attribute Controller Index Readback (R)
    case 0x27:			// Part ID (R)
	break;
    case 0x1d:			// Overlay Extended Control
    case 0x25:			// Part Status
    default:
#ifdef DEBUG_CIRRUS
	printf("cirrus: outport cr_index %02x, cr_value %02x\n", reg_index,
	       reg_value);
#endif
	break;
    }

    return CIRRUS_HOOK_HANDLED;
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
	cirrus_hook_read_gr(s, 0x00, &value);
	break;
    case (CIRRUS_MMIO_BLTBGCOLOR + 1):
	cirrus_hook_read_gr(s, 0x10, &value);
	break;
    case (CIRRUS_MMIO_BLTBGCOLOR + 2):
	cirrus_hook_read_gr(s, 0x12, &value);
	break;
    case (CIRRUS_MMIO_BLTBGCOLOR + 3):
	cirrus_hook_read_gr(s, 0x14, &value);
	break;
    case (CIRRUS_MMIO_BLTFGCOLOR + 0):
	cirrus_hook_read_gr(s, 0x01, &value);
	break;
    case (CIRRUS_MMIO_BLTFGCOLOR + 1):
	cirrus_hook_read_gr(s, 0x11, &value);
	break;
    case (CIRRUS_MMIO_BLTFGCOLOR + 2):
	cirrus_hook_read_gr(s, 0x13, &value);
	break;
    case (CIRRUS_MMIO_BLTFGCOLOR + 3):
	cirrus_hook_read_gr(s, 0x15, &value);
	break;
    case (CIRRUS_MMIO_BLTWIDTH + 0):
	cirrus_hook_read_gr(s, 0x20, &value);
	break;
    case (CIRRUS_MMIO_BLTWIDTH + 1):
	cirrus_hook_read_gr(s, 0x21, &value);
	break;
    case (CIRRUS_MMIO_BLTHEIGHT + 0):
	cirrus_hook_read_gr(s, 0x22, &value);
	break;
    case (CIRRUS_MMIO_BLTHEIGHT + 1):
	cirrus_hook_read_gr(s, 0x23, &value);
	break;
    case (CIRRUS_MMIO_BLTDESTPITCH + 0):
	cirrus_hook_read_gr(s, 0x24, &value);
	break;
    case (CIRRUS_MMIO_BLTDESTPITCH + 1):
	cirrus_hook_read_gr(s, 0x25, &value);
	break;
    case (CIRRUS_MMIO_BLTSRCPITCH + 0):
	cirrus_hook_read_gr(s, 0x26, &value);
	break;
    case (CIRRUS_MMIO_BLTSRCPITCH + 1):
	cirrus_hook_read_gr(s, 0x27, &value);
	break;
    case (CIRRUS_MMIO_BLTDESTADDR + 0):
	cirrus_hook_read_gr(s, 0x28, &value);
	break;
    case (CIRRUS_MMIO_BLTDESTADDR + 1):
	cirrus_hook_read_gr(s, 0x29, &value);
	break;
    case (CIRRUS_MMIO_BLTDESTADDR + 2):
	cirrus_hook_read_gr(s, 0x2a, &value);
	break;
    case (CIRRUS_MMIO_BLTSRCADDR + 0):
	cirrus_hook_read_gr(s, 0x2c, &value);
	break;
    case (CIRRUS_MMIO_BLTSRCADDR + 1):
	cirrus_hook_read_gr(s, 0x2d, &value);
	break;
    case (CIRRUS_MMIO_BLTSRCADDR + 2):
	cirrus_hook_read_gr(s, 0x2e, &value);
	break;
    case CIRRUS_MMIO_BLTWRITEMASK:
	cirrus_hook_read_gr(s, 0x2f, &value);
	break;
    case CIRRUS_MMIO_BLTMODE:
	cirrus_hook_read_gr(s, 0x30, &value);
	break;
    case CIRRUS_MMIO_BLTROP:
	cirrus_hook_read_gr(s, 0x32, &value);
	break;
    case (CIRRUS_MMIO_BLTTRANSPARENTCOLOR + 0):
	cirrus_hook_read_gr(s, 0x34, &value);
	break;
    case (CIRRUS_MMIO_BLTTRANSPARENTCOLOR + 1):
	cirrus_hook_read_gr(s, 0x35, &value);
	break;
    case (CIRRUS_MMIO_BLTTRANSPARENTCOLORMASK + 0):
	cirrus_hook_read_gr(s, 0x38, &value);
	break;
    case (CIRRUS_MMIO_BLTTRANSPARENTCOLORMASK + 1):
	cirrus_hook_read_gr(s, 0x39, &value);
	break;
    case CIRRUS_MMIO_BLTSTATUS:
	cirrus_hook_read_gr(s, 0x31, &value);
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
	cirrus_hook_write_gr(s, 0x00, value);
	break;
    case (CIRRUS_MMIO_BLTBGCOLOR + 1):
	cirrus_hook_write_gr(s, 0x10, value);
	break;
    case (CIRRUS_MMIO_BLTBGCOLOR + 2):
	cirrus_hook_write_gr(s, 0x12, value);
	break;
    case (CIRRUS_MMIO_BLTBGCOLOR + 3):
	cirrus_hook_write_gr(s, 0x14, value);
	break;
    case (CIRRUS_MMIO_BLTFGCOLOR + 0):
	cirrus_hook_write_gr(s, 0x01, value);
	break;
    case (CIRRUS_MMIO_BLTFGCOLOR + 1):
	cirrus_hook_write_gr(s, 0x11, value);
	break;
    case (CIRRUS_MMIO_BLTFGCOLOR + 2):
	cirrus_hook_write_gr(s, 0x13, value);
	break;
    case (CIRRUS_MMIO_BLTFGCOLOR + 3):
	cirrus_hook_write_gr(s, 0x15, value);
	break;
    case (CIRRUS_MMIO_BLTWIDTH + 0):
	cirrus_hook_write_gr(s, 0x20, value);
	break;
    case (CIRRUS_MMIO_BLTWIDTH + 1):
	cirrus_hook_write_gr(s, 0x21, value);
	break;
    case (CIRRUS_MMIO_BLTHEIGHT + 0):
	cirrus_hook_write_gr(s, 0x22, value);
	break;
    case (CIRRUS_MMIO_BLTHEIGHT + 1):
	cirrus_hook_write_gr(s, 0x23, value);
	break;
    case (CIRRUS_MMIO_BLTDESTPITCH + 0):
	cirrus_hook_write_gr(s, 0x24, value);
	break;
    case (CIRRUS_MMIO_BLTDESTPITCH + 1):
	cirrus_hook_write_gr(s, 0x25, value);
	break;
    case (CIRRUS_MMIO_BLTSRCPITCH + 0):
	cirrus_hook_write_gr(s, 0x26, value);
	break;
    case (CIRRUS_MMIO_BLTSRCPITCH + 1):
	cirrus_hook_write_gr(s, 0x27, value);
	break;
    case (CIRRUS_MMIO_BLTDESTADDR + 0):
	cirrus_hook_write_gr(s, 0x28, value);
	break;
    case (CIRRUS_MMIO_BLTDESTADDR + 1):
	cirrus_hook_write_gr(s, 0x29, value);
	break;
    case (CIRRUS_MMIO_BLTDESTADDR + 2):
	cirrus_hook_write_gr(s, 0x2a, value);
	break;
    case (CIRRUS_MMIO_BLTDESTADDR + 3):
	/* ignored */
	break;
    case (CIRRUS_MMIO_BLTSRCADDR + 0):
	cirrus_hook_write_gr(s, 0x2c, value);
	break;
    case (CIRRUS_MMIO_BLTSRCADDR + 1):
	cirrus_hook_write_gr(s, 0x2d, value);
	break;
    case (CIRRUS_MMIO_BLTSRCADDR + 2):
	cirrus_hook_write_gr(s, 0x2e, value);
	break;
    case CIRRUS_MMIO_BLTWRITEMASK:
	cirrus_hook_write_gr(s, 0x2f, value);
	break;
    case CIRRUS_MMIO_BLTMODE:
	cirrus_hook_write_gr(s, 0x30, value);
	break;
    case CIRRUS_MMIO_BLTROP:
	cirrus_hook_write_gr(s, 0x32, value);
	break;
    case (CIRRUS_MMIO_BLTTRANSPARENTCOLOR + 0):
	cirrus_hook_write_gr(s, 0x34, value);
	break;
    case (CIRRUS_MMIO_BLTTRANSPARENTCOLOR + 1):
	cirrus_hook_write_gr(s, 0x35, value);
	break;
    case (CIRRUS_MMIO_BLTTRANSPARENTCOLORMASK + 0):
	cirrus_hook_write_gr(s, 0x38, value);
	break;
    case (CIRRUS_MMIO_BLTTRANSPARENTCOLORMASK + 1):
	cirrus_hook_write_gr(s, 0x39, value);
	break;
    case CIRRUS_MMIO_BLTSTATUS:
	cirrus_hook_write_gr(s, 0x31, value);
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

    dst = s->vram_ptr + offset;
    for (x = 0; x < 8; x++) {
	if (val & 0x80) {
	    *dst++ = s->gr[0x01];
	} else if (mode == 5) {
	    *dst++ = s->gr[0x00];
	}
	val <<= 1;
    }
    cpu_physical_memory_set_dirty(s->vram_offset + offset);
    cpu_physical_memory_set_dirty(s->vram_offset + offset + 7);
}

static void cirrus_mem_writeb_mode4and5_16bpp(CirrusVGAState * s,
					      unsigned mode,
					      unsigned offset,
					      uint32_t mem_value)
{
    int x;
    unsigned val = mem_value;
    uint8_t *dst;

    dst = s->vram_ptr + offset;
    for (x = 0; x < 8; x++) {
	if (val & 0x80) {
	    *dst++ = s->gr[0x01];
	    *dst++ = s->gr[0x11];
	} else if (mode == 5) {
	    *dst++ = s->gr[0x00];
	    *dst++ = s->gr[0x10];
	}
	val <<= 1;
    }
    cpu_physical_memory_set_dirty(s->vram_offset + offset);
    cpu_physical_memory_set_dirty(s->vram_offset + offset + 15);
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

    if ((s->sr[0x07] & 0x01) == 0) {
	return vga_mem_readb(s, addr);
    }

    if (addr < 0x10000) {
	/* XXX handle bitblt */
	/* video memory */
	bank_index = addr >> 15;
	bank_offset = addr & 0x7fff;
	if (bank_offset < s->cirrus_bank_limit[bank_index]) {
	    bank_offset += s->cirrus_bank_base[bank_index];
	    if ((s->gr[0x0B] & 0x14) == 0x14) {
		bank_offset <<= 4;
	    } else if (s->gr[0x0B] & 0x02) {
		bank_offset <<= 3;
	    }
	    bank_offset &= s->cirrus_addr_mask;
	    val = *(s->vram_ptr + bank_offset);
	} else
	    val = 0xff;
    } else if (addr >= 0x18000 && addr < 0x18100) {
	/* memory-mapped I/O */
	val = 0xff;
	if ((s->sr[0x17] & 0x44) == 0x04) {
	    val = cirrus_mmio_blt_read(s, addr & 0xff);
	}
    } else {
	val = 0xff;
#ifdef DEBUG_CIRRUS
	printf("cirrus: mem_readb %06x\n", addr);
#endif
    }
    return val;
}

static uint32_t cirrus_vga_mem_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
#ifdef TARGET_WORDS_BIGENDIAN
    v = cirrus_vga_mem_readb(opaque, addr) << 8;
    v |= cirrus_vga_mem_readb(opaque, addr + 1);
#else
    v = cirrus_vga_mem_readb(opaque, addr);
    v |= cirrus_vga_mem_readb(opaque, addr + 1) << 8;
#endif
    return v;
}

static uint32_t cirrus_vga_mem_readl(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
#ifdef TARGET_WORDS_BIGENDIAN
    v = cirrus_vga_mem_readb(opaque, addr) << 24;
    v |= cirrus_vga_mem_readb(opaque, addr + 1) << 16;
    v |= cirrus_vga_mem_readb(opaque, addr + 2) << 8;
    v |= cirrus_vga_mem_readb(opaque, addr + 3);
#else
    v = cirrus_vga_mem_readb(opaque, addr);
    v |= cirrus_vga_mem_readb(opaque, addr + 1) << 8;
    v |= cirrus_vga_mem_readb(opaque, addr + 2) << 16;
    v |= cirrus_vga_mem_readb(opaque, addr + 3) << 24;
#endif
    return v;
}

static void cirrus_vga_mem_writeb(void *opaque, target_phys_addr_t addr, 
                                  uint32_t mem_value)
{
    CirrusVGAState *s = opaque;
    unsigned bank_index;
    unsigned bank_offset;
    unsigned mode;

    if ((s->sr[0x07] & 0x01) == 0) {
	vga_mem_writeb(s, addr, mem_value);
        return;
    }

    if (addr < 0x10000) {
	if (s->cirrus_srcptr != s->cirrus_srcptr_end) {
	    /* bitblt */
	    *s->cirrus_srcptr++ = (uint8_t) mem_value;
	    if (s->cirrus_srcptr == s->cirrus_srcptr_end) {
		cirrus_bitblt_cputovideo_next(s);
	    }
	} else {
	    /* video memory */
	    bank_index = addr >> 15;
	    bank_offset = addr & 0x7fff;
	    if (bank_offset < s->cirrus_bank_limit[bank_index]) {
		bank_offset += s->cirrus_bank_base[bank_index];
		if ((s->gr[0x0B] & 0x14) == 0x14) {
		    bank_offset <<= 4;
		} else if (s->gr[0x0B] & 0x02) {
		    bank_offset <<= 3;
		}
		bank_offset &= s->cirrus_addr_mask;
		mode = s->gr[0x05] & 0x7;
		if (mode < 4 || mode > 5 || ((s->gr[0x0B] & 0x4) == 0)) {
		    *(s->vram_ptr + bank_offset) = mem_value;
		    cpu_physical_memory_set_dirty(s->vram_offset +
						  bank_offset);
		} else {
		    if ((s->gr[0x0B] & 0x14) != 0x14) {
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
	if ((s->sr[0x17] & 0x44) == 0x04) {
	    cirrus_mmio_blt_write(s, addr & 0xff, mem_value);
	}
    } else {
#ifdef DEBUG_CIRRUS
	printf("cirrus: mem_writeb %06x value %02x\n", addr, mem_value);
#endif
    }
}

static void cirrus_vga_mem_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
#ifdef TARGET_WORDS_BIGENDIAN
    cirrus_vga_mem_writeb(opaque, addr, (val >> 8) & 0xff);
    cirrus_vga_mem_writeb(opaque, addr + 1, val & 0xff);
#else
    cirrus_vga_mem_writeb(opaque, addr, val & 0xff);
    cirrus_vga_mem_writeb(opaque, addr + 1, (val >> 8) & 0xff);
#endif
}

static void cirrus_vga_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
#ifdef TARGET_WORDS_BIGENDIAN
    cirrus_vga_mem_writeb(opaque, addr, (val >> 24) & 0xff);
    cirrus_vga_mem_writeb(opaque, addr + 1, (val >> 16) & 0xff);
    cirrus_vga_mem_writeb(opaque, addr + 2, (val >> 8) & 0xff);
    cirrus_vga_mem_writeb(opaque, addr + 3, val & 0xff);
#else
    cirrus_vga_mem_writeb(opaque, addr, val & 0xff);
    cirrus_vga_mem_writeb(opaque, addr + 1, (val >> 8) & 0xff);
    cirrus_vga_mem_writeb(opaque, addr + 2, (val >> 16) & 0xff);
    cirrus_vga_mem_writeb(opaque, addr + 3, (val >> 24) & 0xff);
#endif
}

static CPUReadMemoryFunc *cirrus_vga_mem_read[3] = {
    cirrus_vga_mem_readb,
    cirrus_vga_mem_readw,
    cirrus_vga_mem_readl,
};

static CPUWriteMemoryFunc *cirrus_vga_mem_write[3] = {
    cirrus_vga_mem_writeb,
    cirrus_vga_mem_writew,
    cirrus_vga_mem_writel,
};

/***************************************
 *
 *  LFB memory access
 *
 ***************************************/

static uint32_t cirrus_linear_readb(void *opaque, target_phys_addr_t addr)
{
    CirrusVGAState *s = (CirrusVGAState *) opaque;
    uint32_t ret;

    /* XXX: s->vram_size must be a power of two */
    addr &= s->cirrus_addr_mask;

    if (((s->sr[0x17] & 0x44) == 0x44) && ((addr & 0x1fff00) == 0x1fff00)) {
	/* memory-mapped I/O */
	ret = cirrus_mmio_blt_read(s, addr & 0xff);
    } else if (0) {
	/* XXX handle bitblt */
	ret = 0xff;
    } else {
	/* video memory */
	if ((s->gr[0x0B] & 0x14) == 0x14) {
	    addr <<= 4;
	} else if (s->gr[0x0B] & 0x02) {
	    addr <<= 3;
	}
	addr &= s->cirrus_addr_mask;
	ret = *(s->vram_ptr + addr);
    }

    return ret;
}

static uint32_t cirrus_linear_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
#ifdef TARGET_WORDS_BIGENDIAN
    v = cirrus_linear_readb(opaque, addr) << 8;
    v |= cirrus_linear_readb(opaque, addr + 1);
#else
    v = cirrus_linear_readb(opaque, addr);
    v |= cirrus_linear_readb(opaque, addr + 1) << 8;
#endif
    return v;
}

static uint32_t cirrus_linear_readl(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
#ifdef TARGET_WORDS_BIGENDIAN
    v = cirrus_linear_readb(opaque, addr) << 24;
    v |= cirrus_linear_readb(opaque, addr + 1) << 16;
    v |= cirrus_linear_readb(opaque, addr + 2) << 8;
    v |= cirrus_linear_readb(opaque, addr + 3);
#else
    v = cirrus_linear_readb(opaque, addr);
    v |= cirrus_linear_readb(opaque, addr + 1) << 8;
    v |= cirrus_linear_readb(opaque, addr + 2) << 16;
    v |= cirrus_linear_readb(opaque, addr + 3) << 24;
#endif
    return v;
}

static void cirrus_linear_writeb(void *opaque, target_phys_addr_t addr,
				 uint32_t val)
{
    CirrusVGAState *s = (CirrusVGAState *) opaque;
    unsigned mode;

    addr &= s->cirrus_addr_mask;

    if (((s->sr[0x17] & 0x44) == 0x44) && ((addr & 0x1fff00) == 0x1fff00)) {
	/* memory-mapped I/O */
	cirrus_mmio_blt_write(s, addr & 0xff, val);
    } else if (s->cirrus_srcptr != s->cirrus_srcptr_end) {
	/* bitblt */
	*s->cirrus_srcptr++ = (uint8_t) val;
	if (s->cirrus_srcptr == s->cirrus_srcptr_end) {
	    cirrus_bitblt_cputovideo_next(s);
	}
    } else {
	/* video memory */
	if ((s->gr[0x0B] & 0x14) == 0x14) {
	    addr <<= 4;
	} else if (s->gr[0x0B] & 0x02) {
	    addr <<= 3;
	}
	addr &= s->cirrus_addr_mask;

	mode = s->gr[0x05] & 0x7;
	if (mode < 4 || mode > 5 || ((s->gr[0x0B] & 0x4) == 0)) {
	    *(s->vram_ptr + addr) = (uint8_t) val;
	    cpu_physical_memory_set_dirty(s->vram_offset + addr);
	} else {
	    if ((s->gr[0x0B] & 0x14) != 0x14) {
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
#ifdef TARGET_WORDS_BIGENDIAN
    cirrus_linear_writeb(opaque, addr, (val >> 8) & 0xff);
    cirrus_linear_writeb(opaque, addr + 1, val & 0xff);
#else
    cirrus_linear_writeb(opaque, addr, val & 0xff);
    cirrus_linear_writeb(opaque, addr + 1, (val >> 8) & 0xff);
#endif
}

static void cirrus_linear_writel(void *opaque, target_phys_addr_t addr,
				 uint32_t val)
{
#ifdef TARGET_WORDS_BIGENDIAN
    cirrus_linear_writeb(opaque, addr, (val >> 24) & 0xff);
    cirrus_linear_writeb(opaque, addr + 1, (val >> 16) & 0xff);
    cirrus_linear_writeb(opaque, addr + 2, (val >> 8) & 0xff);
    cirrus_linear_writeb(opaque, addr + 3, val & 0xff);
#else
    cirrus_linear_writeb(opaque, addr, val & 0xff);
    cirrus_linear_writeb(opaque, addr + 1, (val >> 8) & 0xff);
    cirrus_linear_writeb(opaque, addr + 2, (val >> 16) & 0xff);
    cirrus_linear_writeb(opaque, addr + 3, (val >> 24) & 0xff);
#endif
}


static CPUReadMemoryFunc *cirrus_linear_read[3] = {
    cirrus_linear_readb,
    cirrus_linear_readw,
    cirrus_linear_readl,
};

static CPUWriteMemoryFunc *cirrus_linear_write[3] = {
    cirrus_linear_writeb,
    cirrus_linear_writew,
    cirrus_linear_writel,
};

/* I/O ports */

static uint32_t vga_ioport_read(void *opaque, uint32_t addr)
{
    CirrusVGAState *s = opaque;
    int val, index;

    /* check port range access depending on color/monochrome mode */
    if ((addr >= 0x3b0 && addr <= 0x3bf && (s->msr & MSR_COLOR_EMULATION))
	|| (addr >= 0x3d0 && addr <= 0x3df
	    && !(s->msr & MSR_COLOR_EMULATION))) {
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
	    if (cirrus_hook_read_sr(s, s->sr_index, &val))
		break;
	    val = s->sr[s->sr_index];
#ifdef DEBUG_VGA_REG
	    printf("vga: read SR%x = 0x%02x\n", s->sr_index, val);
#endif
	    break;
	case 0x3c6:
	    cirrus_read_hidden_dac(s, &val);
	    break;
	case 0x3c7:
	    val = s->dac_state;
	    break;
	case 0x3c9:
	    if (cirrus_hook_read_palette(s, &val))
		break;
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
	    if (cirrus_hook_read_gr(s, s->gr_index, &val))
		break;
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
	    if (cirrus_hook_read_cr(s, s->cr_index, &val))
		break;
	    val = s->cr[s->cr_index];
#ifdef DEBUG_VGA_REG
	    printf("vga: read CR%x = 0x%02x\n", s->cr_index, val);
#endif
#ifdef DEBUG_S3
	    if (s->cr_index >= 0x20)
		printf("S3: CR read index=0x%x val=0x%x\n",
		       s->cr_index, val);
#endif
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
#if defined(DEBUG_VGA)
    printf("VGA: read addr=0x%04x data=0x%02x\n", addr, val);
#endif
    return val;
}

static void vga_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    CirrusVGAState *s = opaque;
    int index;

    /* check port range access depending on color/monochrome mode */
    if ((addr >= 0x3b0 && addr <= 0x3bf && (s->msr & MSR_COLOR_EMULATION))
	|| (addr >= 0x3d0 && addr <= 0x3df
	    && !(s->msr & MSR_COLOR_EMULATION)))
	return;

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
	break;
    case 0x3c4:
	s->sr_index = val;
	break;
    case 0x3c5:
	if (cirrus_hook_write_sr(s, s->sr_index, val))
	    break;
#ifdef DEBUG_VGA_REG
	printf("vga: write SR%x = 0x%02x\n", s->sr_index, val);
#endif
	s->sr[s->sr_index] = val & sr_mask[s->sr_index];
	break;
    case 0x3c6:
	cirrus_write_hidden_dac(s, val);
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
	if (cirrus_hook_write_palette(s, val))
	    break;
	s->dac_cache[s->dac_sub_index] = val;
	if (++s->dac_sub_index == 3) {
	    memcpy(&s->palette[s->dac_write_index * 3], s->dac_cache, 3);
	    s->dac_sub_index = 0;
	    s->dac_write_index++;
	}
	break;
    case 0x3ce:
	s->gr_index = val;
	break;
    case 0x3cf:
	if (cirrus_hook_write_gr(s, s->gr_index, val))
	    break;
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
	if (cirrus_hook_write_cr(s, s->cr_index, val))
	    break;
#ifdef DEBUG_VGA_REG
	printf("vga: write CR%x = 0x%02x\n", s->cr_index, val);
#endif
	/* handle CR0-7 protection */
	if ((s->cr[11] & 0x80) && s->cr_index <= 7) {
	    /* can always write bit 4 of CR7 */
	    if (s->cr_index == 7)
		s->cr[7] = (s->cr[7] & ~0x10) | (val & 0x10);
	    return;
	}
	switch (s->cr_index) {
	case 0x01:		/* horizontal display end */
	case 0x07:
	case 0x09:
	case 0x0c:
	case 0x0d:
	case 0x12:		/* veritcal display end */
	    s->cr[s->cr_index] = val;
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

/***************************************
 *
 *  memory-mapped I/O access
 *
 ***************************************/

static uint32_t cirrus_mmio_readb(void *opaque, target_phys_addr_t addr)
{
    CirrusVGAState *s = (CirrusVGAState *) opaque;

    addr &= CIRRUS_PNPMMIO_SIZE - 1;

    if (addr >= 0x100) {
        return cirrus_mmio_blt_read(s, addr - 0x100);
    } else {
        return vga_ioport_read(s, addr + 0x3c0);
    }
}

static uint32_t cirrus_mmio_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
#ifdef TARGET_WORDS_BIGENDIAN
    v = cirrus_mmio_readb(opaque, addr) << 8;
    v |= cirrus_mmio_readb(opaque, addr + 1);
#else
    v = cirrus_mmio_readb(opaque, addr);
    v |= cirrus_mmio_readb(opaque, addr + 1) << 8;
#endif
    return v;
}

static uint32_t cirrus_mmio_readl(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
#ifdef TARGET_WORDS_BIGENDIAN
    v = cirrus_mmio_readb(opaque, addr) << 24;
    v |= cirrus_mmio_readb(opaque, addr + 1) << 16;
    v |= cirrus_mmio_readb(opaque, addr + 2) << 8;
    v |= cirrus_mmio_readb(opaque, addr + 3);
#else
    v = cirrus_mmio_readb(opaque, addr);
    v |= cirrus_mmio_readb(opaque, addr + 1) << 8;
    v |= cirrus_mmio_readb(opaque, addr + 2) << 16;
    v |= cirrus_mmio_readb(opaque, addr + 3) << 24;
#endif
    return v;
}

static void cirrus_mmio_writeb(void *opaque, target_phys_addr_t addr,
			       uint32_t val)
{
    CirrusVGAState *s = (CirrusVGAState *) opaque;

    addr &= CIRRUS_PNPMMIO_SIZE - 1;

    if (addr >= 0x100) {
	cirrus_mmio_blt_write(s, addr - 0x100, val);
    } else {
        vga_ioport_write(s, addr + 0x3c0, val);
    }
}

static void cirrus_mmio_writew(void *opaque, target_phys_addr_t addr,
			       uint32_t val)
{
#ifdef TARGET_WORDS_BIGENDIAN
    cirrus_mmio_writeb(opaque, addr, (val >> 8) & 0xff);
    cirrus_mmio_writeb(opaque, addr + 1, val & 0xff);
#else
    cirrus_mmio_writeb(opaque, addr, val & 0xff);
    cirrus_mmio_writeb(opaque, addr + 1, (val >> 8) & 0xff);
#endif
}

static void cirrus_mmio_writel(void *opaque, target_phys_addr_t addr,
			       uint32_t val)
{
#ifdef TARGET_WORDS_BIGENDIAN
    cirrus_mmio_writeb(opaque, addr, (val >> 24) & 0xff);
    cirrus_mmio_writeb(opaque, addr + 1, (val >> 16) & 0xff);
    cirrus_mmio_writeb(opaque, addr + 2, (val >> 8) & 0xff);
    cirrus_mmio_writeb(opaque, addr + 3, val & 0xff);
#else
    cirrus_mmio_writeb(opaque, addr, val & 0xff);
    cirrus_mmio_writeb(opaque, addr + 1, (val >> 8) & 0xff);
    cirrus_mmio_writeb(opaque, addr + 2, (val >> 16) & 0xff);
    cirrus_mmio_writeb(opaque, addr + 3, (val >> 24) & 0xff);
#endif
}


static CPUReadMemoryFunc *cirrus_mmio_read[3] = {
    cirrus_mmio_readb,
    cirrus_mmio_readw,
    cirrus_mmio_readl,
};

static CPUWriteMemoryFunc *cirrus_mmio_write[3] = {
    cirrus_mmio_writeb,
    cirrus_mmio_writew,
    cirrus_mmio_writel,
};

/***************************************
 *
 *  initialize
 *
 ***************************************/

static void cirrus_init_common(CirrusVGAState * s)
{
    int vga_io_memory;

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

    vga_io_memory = cpu_register_io_memory(0, cirrus_vga_mem_read, 
                                           cirrus_vga_mem_write, s);
    cpu_register_physical_memory(isa_mem_base + 0x000a0000, 0x20000, 
                                 vga_io_memory);

    s->sr[0x06] = 0x0f;
    s->sr[0x0F] = CIRRUS_MEMSIZE_2M;
    s->sr[0x1F] = 0x22;		// MemClock

    s->cr[0x27] = CIRRUS_ID_CLGD5430;

    s->cirrus_hidden_dac_lockindex = 5;
    s->cirrus_hidden_dac_data = 0;

    /* I/O handler for LFB */
    s->cirrus_linear_io_addr =
	cpu_register_io_memory(0, cirrus_linear_read, cirrus_linear_write,
			       s);
    /* I/O handler for memory-mapped I/O */
    s->cirrus_mmio_io_addr =
	cpu_register_io_memory(0, cirrus_mmio_read, cirrus_mmio_write, s);

    /* XXX: s->vram_size must be a power of two */
    s->cirrus_addr_mask = s->vram_size - 1;

    s->get_bpp = cirrus_get_bpp;
    s->get_offsets = cirrus_get_offsets;
}

/***************************************
 *
 *  ISA bus support
 *
 ***************************************/

void isa_cirrus_vga_init(DisplayState *ds, uint8_t *vga_ram_base, 
                         unsigned long vga_ram_offset, int vga_ram_size)
{
    CirrusVGAState *s;

    s = qemu_mallocz(sizeof(CirrusVGAState));
    
    vga_common_init((VGAState *)s, 
                    ds, vga_ram_base, vga_ram_offset, vga_ram_size);
    cirrus_init_common(s);
    s->sr[0x17] = CIRRUS_BUSTYPE_ISA;
    /* XXX ISA-LFB support */
}

/***************************************
 *
 *  PCI bus support
 *
 ***************************************/

static void cirrus_pci_lfb_map(PCIDevice *d, int region_num,
			       uint32_t addr, uint32_t size, int type)
{
    CirrusVGAState *s = &((PCICirrusVGAState *)d)->cirrus_vga;

    cpu_register_physical_memory(addr, s->vram_size,
				 s->cirrus_linear_io_addr);
}

static void cirrus_pci_mmio_map(PCIDevice *d, int region_num,
				uint32_t addr, uint32_t size, int type)
{
    CirrusVGAState *s = &((PCICirrusVGAState *)d)->cirrus_vga;

    cpu_register_physical_memory(addr, CIRRUS_PNPMMIO_SIZE,
				 s->cirrus_mmio_io_addr);
}

void pci_cirrus_vga_init(DisplayState *ds, uint8_t *vga_ram_base, 
                         unsigned long vga_ram_offset, int vga_ram_size)
{
    PCICirrusVGAState *d;
    uint8_t *pci_conf;
    CirrusVGAState *s;

    /* setup PCI configuration registers */
    d = (PCICirrusVGAState *)pci_register_device("Cirrus VGA", 
                                                 sizeof(PCICirrusVGAState), 
                                                 0, -1, NULL, NULL);
    pci_conf = d->dev.config;
    pci_conf[0x00] = (uint8_t) (PCI_VENDOR_CIRRUS & 0xff);
    pci_conf[0x01] = (uint8_t) (PCI_VENDOR_CIRRUS >> 8);
    pci_conf[0x02] = (uint8_t) (PCI_DEVICE_CLGD5430 & 0xff);
    pci_conf[0x03] = (uint8_t) (PCI_DEVICE_CLGD5430 >> 8);
    pci_conf[0x04] = PCI_COMMAND_IOACCESS | PCI_COMMAND_MEMACCESS;
    pci_conf[0x0a] = PCI_CLASS_SUB_VGA;
    pci_conf[0x0b] = PCI_CLASS_BASE_DISPLAY;
    pci_conf[0x0e] = PCI_CLASS_HEADERTYPE_00h;

    /* setup VGA */
    s = &d->cirrus_vga;
    vga_common_init((VGAState *)s, 
                    ds, vga_ram_base, vga_ram_offset, vga_ram_size);
    cirrus_init_common(s);
    s->sr[0x17] = CIRRUS_BUSTYPE_PCI;

    /* setup memory space */
    /* memory #0 LFB */
    /* memory #1 memory-mapped I/O */
    /* XXX: s->vram_size must be a power of two */
    pci_register_io_region((PCIDevice *)d, 0, s->vram_size,
			   PCI_ADDRESS_SPACE_MEM, cirrus_pci_lfb_map);
    pci_register_io_region((PCIDevice *)d, 1, CIRRUS_PNPMMIO_SIZE,
			   PCI_ADDRESS_SPACE_MEM, cirrus_pci_mmio_map);
    /* XXX: ROM BIOS */
}
