/*
 * QEMU Cirrus CLGD 54xx VGA Emulator.
 *
 * Copyright (c) 2004 Fabrice Bellard
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

static inline void glue(rop_8_, ROP_NAME)(CirrusVGAState *s,
                                          uint32_t dstaddr, uint8_t src)
{
    uint8_t *dst = &s->vga.vram_ptr[dstaddr & s->cirrus_addr_mask];
    *dst = ROP_FN(*dst, src);
}

static inline void glue(rop_tr_8_, ROP_NAME)(CirrusVGAState *s,
                                             uint32_t dstaddr, uint8_t src,
                                             uint8_t transp)
{
    uint8_t *dst = &s->vga.vram_ptr[dstaddr & s->cirrus_addr_mask];
    uint8_t pixel = ROP_FN(*dst, src);
    if (pixel != transp) {
        *dst = pixel;
    }
}

static inline void glue(rop_16_, ROP_NAME)(CirrusVGAState *s,
                                           uint32_t dstaddr, uint16_t src)
{
    uint16_t *dst = (uint16_t *)
        (&s->vga.vram_ptr[dstaddr & s->cirrus_addr_mask & ~1]);
    *dst = ROP_FN(*dst, src);
}

static inline void glue(rop_tr_16_, ROP_NAME)(CirrusVGAState *s,
                                              uint32_t dstaddr, uint16_t src,
                                              uint16_t transp)
{
    uint16_t *dst = (uint16_t *)
        (&s->vga.vram_ptr[dstaddr & s->cirrus_addr_mask & ~1]);
    uint16_t pixel = ROP_FN(*dst, src);
    if (pixel != transp) {
        *dst = pixel;
    }
}

static inline void glue(rop_32_, ROP_NAME)(CirrusVGAState *s,
                                           uint32_t dstaddr, uint32_t src)
{
    uint32_t *dst = (uint32_t *)
        (&s->vga.vram_ptr[dstaddr & s->cirrus_addr_mask & ~3]);
    *dst = ROP_FN(*dst, src);
}

#define ROP_OP(st, d, s)           glue(rop_8_, ROP_NAME)(st, d, s)
#define ROP_OP_TR(st, d, s, t)     glue(rop_tr_8_, ROP_NAME)(st, d, s, t)
#define ROP_OP_16(st, d, s)        glue(rop_16_, ROP_NAME)(st, d, s)
#define ROP_OP_TR_16(st, d, s, t)  glue(rop_tr_16_, ROP_NAME)(st, d, s, t)
#define ROP_OP_32(st, d, s)        glue(rop_32_, ROP_NAME)(st, d, s)
#undef ROP_FN

static void
glue(cirrus_bitblt_rop_fwd_, ROP_NAME)(CirrusVGAState *s,
                                       uint32_t dstaddr,
                                       uint32_t srcaddr,
                                       int dstpitch, int srcpitch,
                                       int bltwidth, int bltheight)
{
    int x,y;
    dstpitch -= bltwidth;
    srcpitch -= bltwidth;

    if (bltheight > 1 && (dstpitch < 0 || srcpitch < 0)) {
        return;
    }

    for (y = 0; y < bltheight; y++) {
        for (x = 0; x < bltwidth; x++) {
            ROP_OP(s, dstaddr, cirrus_src(s, srcaddr));
            dstaddr++;
            srcaddr++;
        }
        dstaddr += dstpitch;
        srcaddr += srcpitch;
    }
}

static void
glue(cirrus_bitblt_rop_bkwd_, ROP_NAME)(CirrusVGAState *s,
                                        uint32_t dstaddr,
                                        uint32_t srcaddr,
                                        int dstpitch, int srcpitch,
                                        int bltwidth, int bltheight)
{
    int x,y;
    dstpitch += bltwidth;
    srcpitch += bltwidth;
    for (y = 0; y < bltheight; y++) {
        for (x = 0; x < bltwidth; x++) {
            ROP_OP(s, dstaddr, cirrus_src(s, srcaddr));
            dstaddr--;
            srcaddr--;
        }
        dstaddr += dstpitch;
        srcaddr += srcpitch;
    }
}

static void
glue(glue(cirrus_bitblt_rop_fwd_transp_, ROP_NAME),_8)(CirrusVGAState *s,
                                                       uint32_t dstaddr,
                                                       uint32_t srcaddr,
                                                       int dstpitch,
                                                       int srcpitch,
                                                       int bltwidth,
                                                       int bltheight)
{
    int x,y;
    uint8_t transp = s->vga.gr[0x34];
    dstpitch -= bltwidth;
    srcpitch -= bltwidth;

    if (bltheight > 1 && (dstpitch < 0 || srcpitch < 0)) {
        return;
    }

    for (y = 0; y < bltheight; y++) {
        for (x = 0; x < bltwidth; x++) {
            ROP_OP_TR(s, dstaddr, cirrus_src(s, srcaddr), transp);
            dstaddr++;
            srcaddr++;
        }
        dstaddr += dstpitch;
        srcaddr += srcpitch;
    }
}

static void
glue(glue(cirrus_bitblt_rop_bkwd_transp_, ROP_NAME),_8)(CirrusVGAState *s,
                                                        uint32_t dstaddr,
                                                        uint32_t srcaddr,
                                                        int dstpitch,
                                                        int srcpitch,
                                                        int bltwidth,
                                                        int bltheight)
{
    int x,y;
    uint8_t transp = s->vga.gr[0x34];
    dstpitch += bltwidth;
    srcpitch += bltwidth;
    for (y = 0; y < bltheight; y++) {
        for (x = 0; x < bltwidth; x++) {
            ROP_OP_TR(s, dstaddr, cirrus_src(s, srcaddr), transp);
            dstaddr--;
            srcaddr--;
        }
        dstaddr += dstpitch;
        srcaddr += srcpitch;
    }
}

static void
glue(glue(cirrus_bitblt_rop_fwd_transp_, ROP_NAME),_16)(CirrusVGAState *s,
                                                        uint32_t dstaddr,
                                                        uint32_t srcaddr,
                                                        int dstpitch,
                                                        int srcpitch,
                                                        int bltwidth,
                                                        int bltheight)
{
    int x,y;
    uint16_t transp = s->vga.gr[0x34] | (uint16_t)s->vga.gr[0x35] << 8;
    dstpitch -= bltwidth;
    srcpitch -= bltwidth;

    if (bltheight > 1 && (dstpitch < 0 || srcpitch < 0)) {
        return;
    }

    for (y = 0; y < bltheight; y++) {
        for (x = 0; x < bltwidth; x+=2) {
            ROP_OP_TR_16(s, dstaddr, cirrus_src16(s, srcaddr), transp);
            dstaddr += 2;
            srcaddr += 2;
        }
        dstaddr += dstpitch;
        srcaddr += srcpitch;
    }
}

static void
glue(glue(cirrus_bitblt_rop_bkwd_transp_, ROP_NAME),_16)(CirrusVGAState *s,
                                                         uint32_t dstaddr,
                                                         uint32_t srcaddr,
                                                         int dstpitch,
                                                         int srcpitch,
                                                         int bltwidth,
                                                         int bltheight)
{
    int x,y;
    uint16_t transp = s->vga.gr[0x34] | (uint16_t)s->vga.gr[0x35] << 8;
    dstpitch += bltwidth;
    srcpitch += bltwidth;
    for (y = 0; y < bltheight; y++) {
        for (x = 0; x < bltwidth; x+=2) {
            ROP_OP_TR_16(s, dstaddr - 1, cirrus_src16(s, srcaddr - 1), transp);
            dstaddr -= 2;
            srcaddr -= 2;
        }
        dstaddr += dstpitch;
        srcaddr += srcpitch;
    }
}

#define DEPTH 8
#include "cirrus_vga_rop2.h"

#define DEPTH 16
#include "cirrus_vga_rop2.h"

#define DEPTH 24
#include "cirrus_vga_rop2.h"

#define DEPTH 32
#include "cirrus_vga_rop2.h"

#undef ROP_NAME
#undef ROP_OP
#undef ROP_OP_16
#undef ROP_OP_32
