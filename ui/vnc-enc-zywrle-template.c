
/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE 'ZYWRLE' VNC CODEC SOURCE CODE.         *
 *                                                                  *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS LIBRARY SOURCE IS     *
 * GOVERNED BY A FOLLOWING BSD-STYLE SOURCE LICENSE.                *
 * PLEASE READ THESE TERMS BEFORE DISTRIBUTING.                     *
 *                                                                  *
 * THE 'ZYWRLE' VNC CODEC SOURCE CODE IS (C) COPYRIGHT 2006         *
 * BY Hitachi Systems & Services, Ltd.                              *
 * (Noriaki Yamazaki, Research & Development Center)               *
 *                                                                  *
 *                                                                  *
 ********************************************************************
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

- Neither the name of the Hitachi Systems & Services, Ltd. nor
the names of its contributors may be used to endorse or promote
products derived from this software without specific prior written
permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ********************************************************************/

/* Change Log:
     V0.02 : 2008/02/04 : Fix mis encode/decode when width != scanline
	                     (Thanks Johannes Schindelin, author of LibVNC
						  Server/Client)
     V0.01 : 2007/02/06 : Initial release
*/

/*
[References]
 PLHarr:
   Senecal, J. G., P. Lindstrom, M. A. Duchaineau, and K. I. Joy,
   "An Improved N-Bit to N-Bit Reversible Haar-Like Transform,"
   Pacific Graphics 2004, October 2004, pp. 371-380.
 EZW:
   Shapiro, JM: Embedded Image Coding Using Zerotrees of Wavelet Coefficients,
   IEEE Trans. Signal. Process., Vol.41, pp.3445-3462 (1993).
*/


/* Template Macro stuffs. */
#undef ZYWRLE_ANALYZE
#undef ZYWRLE_SYNTHESIZE

#define ZYWRLE_SUFFIX     ZRLE_CONCAT2(ZRLE_BPP,ZRLE_ENDIAN_SUFFIX)

#define ZYWRLE_ANALYZE    ZRLE_CONCAT2(zywrle_analyze_,   ZYWRLE_SUFFIX)
#define ZYWRLE_SYNTHESIZE ZRLE_CONCAT2(zywrle_synthesize_,ZYWRLE_SUFFIX)

#define ZYWRLE_RGBYUV     ZRLE_CONCAT2(zywrle_rgbyuv_,    ZYWRLE_SUFFIX)
#define ZYWRLE_YUVRGB     ZRLE_CONCAT2(zywrle_yuvrgb_,    ZYWRLE_SUFFIX)
#define ZYWRLE_YMASK      ZRLE_CONCAT2(ZYWRLE_YMASK,      ZRLE_BPP)
#define ZYWRLE_UVMASK     ZRLE_CONCAT2(ZYWRLE_UVMASK,     ZRLE_BPP)
#define ZYWRLE_LOAD_PIXEL ZRLE_CONCAT2(ZYWRLE_LOAD_PIXEL, ZRLE_BPP)
#define ZYWRLE_SAVE_PIXEL ZRLE_CONCAT2(ZYWRLE_SAVE_PIXEL, ZRLE_BPP)

/* Packing/Unpacking pixel stuffs.
   Endian conversion stuffs. */
#undef S_0
#undef S_1
#undef L_0
#undef L_1
#undef L_2

#if ZYWRLE_ENDIAN == ENDIAN_BIG
#  define S_0	1
#  define S_1	0
#  define L_0	3
#  define L_1	2
#  define L_2	1
#else
#  define S_0	0
#  define S_1	1
#  define L_0	0
#  define L_1	1
#  define L_2	2
#endif

#define ZYWRLE_QUANTIZE
#include "vnc-enc-zywrle.h"

#ifndef ZRLE_COMPACT_PIXEL
static inline void ZYWRLE_RGBYUV(int *buf, ZRLE_PIXEL *data,
                                 int width, int height, int scanline)
{
    int r, g, b;
    int y, u, v;
    int *line;
    int *end;

    end = buf + height * width;
    while (buf < end) {
        line = buf + width;
        while (buf < line) {
            ZYWRLE_LOAD_PIXEL(data, r, g, b);
            ZYWRLE_RGBYUV_(r, g, b, y, u, v, ZYWRLE_YMASK, ZYWRLE_UVMASK);
            ZYWRLE_SAVE_COEFF(buf, v, y, u);
            buf++;
            data++;
        }
        data += scanline - width;
    }
}

static ZRLE_PIXEL *ZYWRLE_ANALYZE(ZRLE_PIXEL *dst, ZRLE_PIXEL *src,
                                  int w, int h, int scanline, int level,
                                  int *buf) {
    int l;
    int uw = w;
    int uh = h;
    int *top;
    int *end;
    int *line;
    ZRLE_PIXEL *p;
    int r, g, b;
    int s;
    int *ph;

    zywrle_calc_size(&w, &h, level);

    if (w == 0 || h == 0) {
        return NULL;
    }
    uw -= w;
    uh -= h;

    p = dst;
    ZYWRLE_LOAD_UNALIGN(src,*(ZRLE_PIXEL*)top = *p;);
    ZYWRLE_RGBYUV(buf, src, w, h, scanline);
    wavelet(buf, w, h, level);
    for (l = 0; l < level; l++) {
        ZYWRLE_PACK_COEFF(buf, dst, 3, w, h, scanline, l);
        ZYWRLE_PACK_COEFF(buf, dst, 2, w, h, scanline, l);
        ZYWRLE_PACK_COEFF(buf, dst, 1, w, h, scanline, l);
        if (l == level - 1) {
            ZYWRLE_PACK_COEFF(buf, dst, 0, w, h, scanline, l);
        }
    }
    ZYWRLE_SAVE_UNALIGN(dst,*dst = *(ZRLE_PIXEL*)top;);
    return dst;
}
#endif  /* ZRLE_COMPACT_PIXEL */

#undef ZYWRLE_RGBYUV
#undef ZYWRLE_YUVRGB
#undef ZYWRLE_LOAD_PIXEL
#undef ZYWRLE_SAVE_PIXEL
