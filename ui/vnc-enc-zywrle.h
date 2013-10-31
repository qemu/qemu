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

#ifndef VNC_ENCODING_ZYWRLE_H
#define VNC_ENCODING_ZYWRLE_H

/* Tables for Coefficients filtering. */
#ifndef ZYWRLE_QUANTIZE
/* Type A:lower bit omitting of EZW style. */
static const unsigned int zywrle_param[3][3]={
	{0x0000F000, 0x00000000, 0x00000000},
	{0x0000C000, 0x00F0F0F0, 0x00000000},
	{0x0000C000, 0x00C0C0C0, 0x00F0F0F0},
/*	{0x0000FF00, 0x00000000, 0x00000000},
	{0x0000FF00, 0x00FFFFFF, 0x00000000},
	{0x0000FF00, 0x00FFFFFF, 0x00FFFFFF}, */
};
#else
/* Type B:Non liner quantization filter. */
static const int8_t zywrle_conv[4][256]={
{	/* bi=5, bo=5 r=0.0:PSNR=24.849 */
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
},
{	/* bi=5, bo=5 r=2.0:PSNR=74.031 */
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 32,
	32, 32, 32, 32, 32, 32, 32, 32,
	32, 32, 32, 32, 32, 32, 32, 32,
	48, 48, 48, 48, 48, 48, 48, 48,
	48, 48, 48, 56, 56, 56, 56, 56,
	56, 56, 56, 56, 64, 64, 64, 64,
	64, 64, 64, 64, 72, 72, 72, 72,
	72, 72, 72, 72, 80, 80, 80, 80,
	80, 80, 88, 88, 88, 88, 88, 88,
	88, 88, 88, 88, 88, 88, 96, 96,
	96, 96, 96, 104, 104, 104, 104, 104,
	104, 104, 104, 104, 104, 112, 112, 112,
	112, 112, 112, 112, 112, 112, 120, 120,
	120, 120, 120, 120, 120, 120, 120, 120,
	0, -120, -120, -120, -120, -120, -120, -120,
	-120, -120, -120, -112, -112, -112, -112, -112,
	-112, -112, -112, -112, -104, -104, -104, -104,
	-104, -104, -104, -104, -104, -104, -96, -96,
	-96, -96, -96, -88, -88, -88, -88, -88,
	-88, -88, -88, -88, -88, -88, -88, -80,
	-80, -80, -80, -80, -80, -72, -72, -72,
	-72, -72, -72, -72, -72, -64, -64, -64,
	-64, -64, -64, -64, -64, -56, -56, -56,
	-56, -56, -56, -56, -56, -56, -48, -48,
	-48, -48, -48, -48, -48, -48, -48, -48,
	-48, -32, -32, -32, -32, -32, -32, -32,
	-32, -32, -32, -32, -32, -32, -32, -32,
	-32, -32, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
},
{	/* bi=5, bo=4 r=2.0:PSNR=64.441 */
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	48, 48, 48, 48, 48, 48, 48, 48,
	48, 48, 48, 48, 48, 48, 48, 48,
	48, 48, 48, 48, 48, 48, 48, 48,
	64, 64, 64, 64, 64, 64, 64, 64,
	64, 64, 64, 64, 64, 64, 64, 64,
	80, 80, 80, 80, 80, 80, 80, 80,
	80, 80, 80, 80, 80, 88, 88, 88,
	88, 88, 88, 88, 88, 88, 88, 88,
	104, 104, 104, 104, 104, 104, 104, 104,
	104, 104, 104, 112, 112, 112, 112, 112,
	112, 112, 112, 112, 120, 120, 120, 120,
	120, 120, 120, 120, 120, 120, 120, 120,
	0, -120, -120, -120, -120, -120, -120, -120,
	-120, -120, -120, -120, -120, -112, -112, -112,
	-112, -112, -112, -112, -112, -112, -104, -104,
	-104, -104, -104, -104, -104, -104, -104, -104,
	-104, -88, -88, -88, -88, -88, -88, -88,
	-88, -88, -88, -88, -80, -80, -80, -80,
	-80, -80, -80, -80, -80, -80, -80, -80,
	-80, -64, -64, -64, -64, -64, -64, -64,
	-64, -64, -64, -64, -64, -64, -64, -64,
	-64, -48, -48, -48, -48, -48, -48, -48,
	-48, -48, -48, -48, -48, -48, -48, -48,
	-48, -48, -48, -48, -48, -48, -48, -48,
	-48, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
},
{	/* bi=5, bo=2 r=2.0:PSNR=43.175 */
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	88, 88, 88, 88, 88, 88, 88, 88,
	88, 88, 88, 88, 88, 88, 88, 88,
	88, 88, 88, 88, 88, 88, 88, 88,
	88, 88, 88, 88, 88, 88, 88, 88,
	88, 88, 88, 88, 88, 88, 88, 88,
	88, 88, 88, 88, 88, 88, 88, 88,
	88, 88, 88, 88, 88, 88, 88, 88,
	88, 88, 88, 88, 88, 88, 88, 88,
	0, -88, -88, -88, -88, -88, -88, -88,
	-88, -88, -88, -88, -88, -88, -88, -88,
	-88, -88, -88, -88, -88, -88, -88, -88,
	-88, -88, -88, -88, -88, -88, -88, -88,
	-88, -88, -88, -88, -88, -88, -88, -88,
	-88, -88, -88, -88, -88, -88, -88, -88,
	-88, -88, -88, -88, -88, -88, -88, -88,
	-88, -88, -88, -88, -88, -88, -88, -88,
	-88, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
}
};

static const int8_t *zywrle_param[3][3][3]={
	{{zywrle_conv[0], zywrle_conv[2], zywrle_conv[0]},
         {zywrle_conv[0], zywrle_conv[0], zywrle_conv[0]},
         {zywrle_conv[0], zywrle_conv[0], zywrle_conv[0]}},
	{{zywrle_conv[0], zywrle_conv[3], zywrle_conv[0]},
         {zywrle_conv[1], zywrle_conv[1], zywrle_conv[1]},
         {zywrle_conv[0], zywrle_conv[0], zywrle_conv[0]}},
	{{zywrle_conv[0], zywrle_conv[3], zywrle_conv[0]},
         {zywrle_conv[2], zywrle_conv[2], zywrle_conv[2]},
         {zywrle_conv[1], zywrle_conv[1], zywrle_conv[1]}},
};
#endif

/*   Load/Save pixel stuffs. */
#define ZYWRLE_YMASK15  0xFFFFFFF8
#define ZYWRLE_UVMASK15 0xFFFFFFF8
#define ZYWRLE_LOAD_PIXEL15(src, r, g, b)                               \
    do {                                                                \
	r = (((uint8_t*)src)[S_1]<< 1)& 0xF8;                           \
	g = (((uint8_t*)src)[S_1]<< 6) | (((uint8_t*)src)[S_0]>> 2);    \
        g &= 0xF8;                                                      \
	b =  (((uint8_t*)src)[S_0]<< 3)& 0xF8;                          \
    } while (0)

#define ZYWRLE_SAVE_PIXEL15(dst, r, g, b)                               \
    do {                                                                \
	r &= 0xF8;                                                      \
	g &= 0xF8;                                                      \
	b &= 0xF8;                                                      \
	((uint8_t*)dst)[S_1] = (uint8_t)((r >> 1)|(g >> 6));            \
	((uint8_t*)dst)[S_0] = (uint8_t)(((b >> 3)|(g << 2))& 0xFF);    \
    } while (0)

#define ZYWRLE_YMASK16  0xFFFFFFFC
#define ZYWRLE_UVMASK16 0xFFFFFFF8
#define ZYWRLE_LOAD_PIXEL16(src, r, g, b)                               \
    do {                                                                \
	r = ((uint8_t*)src)[S_1] & 0xF8;                                \
	g = (((uint8_t*)src)[S_1]<< 5) | (((uint8_t*)src)[S_0] >> 3);   \
        g &= 0xFC;                                                      \
	b = (((uint8_t*)src)[S_0]<< 3) & 0xF8;                          \
    } while (0)

#define ZYWRLE_SAVE_PIXEL16(dst, r, g,b)                                \
    do {                                                                \
	r &= 0xF8;                                                      \
	g &= 0xFC;                                                      \
	b &= 0xF8;                                                      \
	((uint8_t*)dst)[S_1] = (uint8_t)(r | (g >> 5));                 \
	((uint8_t*)dst)[S_0] = (uint8_t)(((b >> 3)|(g << 3)) & 0xFF);   \
    } while (0)

#define ZYWRLE_YMASK32  0xFFFFFFFF
#define ZYWRLE_UVMASK32 0xFFFFFFFF
#define ZYWRLE_LOAD_PIXEL32(src, r, g, b)     \
    do {                                      \
	r = ((uint8_t*)src)[L_2];             \
	g = ((uint8_t*)src)[L_1];             \
	b = ((uint8_t*)src)[L_0];             \
    } while (0)
#define ZYWRLE_SAVE_PIXEL32(dst, r, g, b)             \
    do {                                              \
	((uint8_t*)dst)[L_2] = (uint8_t)r;            \
	((uint8_t*)dst)[L_1] = (uint8_t)g;            \
	((uint8_t*)dst)[L_0] = (uint8_t)b;            \
    } while (0)

static inline void harr(int8_t *px0, int8_t *px1)
{
    /* Piecewise-Linear Harr(PLHarr) */
    int x0 = (int)*px0, x1 = (int)*px1;
    int orgx0 = x0, orgx1 = x1;

    if ((x0 ^ x1) & 0x80) {
        /* differ sign */
        x1 += x0;
        if (((x1 ^ orgx1) & 0x80) == 0) {
            /* |x1| > |x0| */
            x0 -= x1;	/* H = -B */
        }
    } else {
        /* same sign */
        x0 -= x1;
        if (((x0 ^ orgx0) & 0x80) == 0) {
            /* |x0| > |x1| */
            x1 += x0;	/* L = A */
        }
    }
    *px0 = (int8_t)x1;
    *px1 = (int8_t)x0;
}

/*
 1D-Wavelet transform.

 In coefficients array, the famous 'pyramid' decomposition is well used.

 1D Model:
   |L0L0L0L0|L0L0L0L0|H0H0H0H0|H0H0H0H0| : level 0
   |L1L1L1L1|H1H1H1H1|H0H0H0H0|H0H0H0H0| : level 1

 But this method needs line buffer because H/L is different position from X0/X1.
 So, I used 'interleave' decomposition instead of it.

 1D Model:
   |L0H0L0H0|L0H0L0H0|L0H0L0H0|L0H0L0H0| : level 0
   |L1H0H1H0|L1H0H1H0|L1H0H1H0|L1H0H1H0| : level 1

 In this method, H/L and X0/X1 is always same position.
 This leads us to more speed and less memory.
 Of cause, the result of both method is quite same
 because it's only difference that coefficient position.
*/
static inline void wavelet_level(int *data, int size, int l, int skip_pixel)
{
    int s, ofs;
    int8_t *px0;
    int8_t *end;

    px0 = (int8_t*)data;
    s = (8 << l) * skip_pixel;
    end = px0 + (size >> (l + 1)) * s;
    s -= 2;
    ofs = (4 << l) * skip_pixel;

    while (px0 < end) {
        harr(px0, px0 + ofs);
        px0++;
        harr(px0, px0 + ofs);
        px0++;
        harr(px0, px0 + ofs);
        px0 += s;
    }
}

#ifndef ZYWRLE_QUANTIZE
/* Type A:lower bit omitting of EZW style. */
static inline void filter_wavelet_square(int *buf, int width, int height,
                                         int level, int l)
{
    int r, s;
    int x, y;
    int *h;
    const unsigned int *m;

    m = &(zywrle_param[level - 1][l]);
    s = 2 << l;

    for (r = 1; r < 4; r++) {
        h = buf;
        if (r & 0x01) {
            h += s >> 1;
        }
        if (r & 0x02) {
            h += (s >> 1) * width;
        }
        for (y = 0; y < height / s; y++) {
            for (x = 0; x < width / s; x++) {
                /*
                  these are same following code.
                  h[x] = h[x] / (~m[x]+1) * (~m[x]+1);
                  ( round h[x] with m[x] bit )
                  '&' operator isn't 'round' but is 'floor'.
                  So, we must offset when h[x] is negative.
                */
                if (((int8_t*)h)[0] & 0x80) {
                    ((int8_t*)h)[0] += ~((int8_t*)m)[0];
                }
                if (((int8_t*)h)[1] & 0x80) {
                    ((int8_t*)h)[1] += ~((int8_t*)m)[1];
                }
                if (((int8_t*)h)[2] & 0x80) {
                    ((int8_t*)h)[2] += ~((int8_t*)m)[2];
                }
                *h &= *m;
                h += s;
            }
            h += (s-1)*width;
        }
    }
}
#else
/*
 Type B:Non liner quantization filter.

 Coefficients have Gaussian curve and smaller value which is
 large part of coefficients isn't more important than larger value.
 So, I use filter of Non liner quantize/dequantize table.
 In general, Non liner quantize formula is explained as following.

    y=f(x)   = sign(x)*round( ((abs(x)/(2^7))^ r   )* 2^(bo-1) )*2^(8-bo)
    x=f-1(y) = sign(y)*round( ((abs(y)/(2^7))^(1/r))* 2^(bi-1) )*2^(8-bi)
 ( r:power coefficient  bi:effective MSB in input  bo:effective MSB in output )

   r < 1.0 : Smaller value is more important than larger value.
   r > 1.0 : Larger value is more important than smaller value.
   r = 1.0 : Liner quantization which is same with EZW style.

 r = 0.75 is famous non liner quantization used in MP3 audio codec.
 In contrast to audio data, larger value is important in wavelet coefficients.
 So, I select r = 2.0 table( quantize is x^2, dequantize sqrt(x) ).

 As compared with EZW style liner quantization, this filter tended to be
 more sharp edge and be more compression rate but be more blocking noise and be
 less quality. Especially, the surface of graphic objects has distinguishable
 noise in middle quality mode.

 We need only quantized-dequantized(filtered) value rather than quantized value
 itself because all values are packed or palette-lized in later ZRLE section.
 This lead us not to need to modify client decoder when we change
 the filtering procedure in future.
 Client only decodes coefficients given by encoder.
*/
static inline void filter_wavelet_square(int *buf, int width, int height,
                                         int level, int l)
{
    int r, s;
    int x, y;
    int *h;
    const int8_t **m;

    m = zywrle_param[level - 1][l];
    s = 2 << l;

    for (r = 1; r < 4; r++) {
        h = buf;
        if (r & 0x01) {
            h += s >> 1;
        }
        if (r & 0x02) {
            h += (s >> 1) * width;
        }
        for (y = 0; y < height / s; y++) {
            for (x = 0; x < width / s; x++) {
                ((int8_t*)h)[0] = m[0][((uint8_t*)h)[0]];
                ((int8_t*)h)[1] = m[1][((uint8_t*)h)[1]];
                ((int8_t*)h)[2] = m[2][((uint8_t*)h)[2]];
                h += s;
            }
            h += (s - 1) * width;
        }
    }
}
#endif

static inline void wavelet(int *buf, int width, int height, int level)
{
	int l, s;
	int *top;
	int *end;

	for (l = 0; l < level; l++) {
		top = buf;
		end = buf + height * width;
		s = width << l;
		while (top < end) {
			wavelet_level(top, width, l, 1);
			top += s;
		}
		top = buf;
		end = buf + width;
		s = 1<<l;
		while (top < end) {
			wavelet_level(top, height, l, width);
			top += s;
		}
		filter_wavelet_square(buf, width, height, level, l);
	}
}


/* Load/Save coefficients stuffs.
 Coefficients manages as 24 bits little-endian pixel. */
#define ZYWRLE_LOAD_COEFF(src, r, g, b)         \
    do {                                        \
	r = ((int8_t*)src)[2];                  \
	g = ((int8_t*)src)[1];                  \
	b = ((int8_t*)src)[0];                  \
    } while (0)

#define ZYWRLE_SAVE_COEFF(dst, r, g, b)       \
    do {                                      \
	((int8_t*)dst)[2] = (int8_t)r;        \
	((int8_t*)dst)[1] = (int8_t)g;        \
	((int8_t*)dst)[0] = (int8_t)b;        \
    } while (0)

/*
  RGB <=> YUV conversion stuffs.
  YUV coversion is explained as following formula in strict meaning:
  Y =  0.299R + 0.587G + 0.114B (   0<=Y<=255)
  U = -0.169R - 0.331G + 0.500B (-128<=U<=127)
  V =  0.500R - 0.419G - 0.081B (-128<=V<=127)

  I use simple conversion RCT(reversible color transform) which is described
  in JPEG-2000 specification.
  Y = (R + 2G + B)/4 (   0<=Y<=255)
  U = B-G (-256<=U<=255)
  V = R-G (-256<=V<=255)
*/

/* RCT is N-bit RGB to N-bit Y and N+1-bit UV.
   For make Same N-bit, UV is lossy.
   More exact PLHarr, we reduce to odd range(-127<=x<=127). */
#define ZYWRLE_RGBYUV_(r, g, b, y, u, v, ymask, uvmask)          \
    do {                                                         \
	y = (r + (g << 1) + b) >> 2;                             \
	u =  b - g;                                              \
	v =  r - g;                                              \
	y -= 128;                                                \
	u >>= 1;                                                 \
	v >>= 1;                                                 \
	y &= ymask;                                              \
	u &= uvmask;                                             \
	v &= uvmask;                                             \
	if (y == -128) {                                         \
            y += (0xFFFFFFFF - ymask + 1);                       \
        }                                                        \
	if (u == -128) {                                         \
            u += (0xFFFFFFFF - uvmask + 1);                      \
        }                                                        \
	if (v == -128) {                                         \
            v += (0xFFFFFFFF - uvmask + 1);                      \
        }                                                        \
    } while (0)


/*
 coefficient packing/unpacking stuffs.
 Wavelet transform makes 4 sub coefficient image from 1 original image.

 model with pyramid decomposition:
   +------+------+
   |      |      |
   |  L   |  Hx  |
   |      |      |
   +------+------+
   |      |      |
   |  H   |  Hxy |
   |      |      |
   +------+------+

 So, we must transfer each sub images individually in strict meaning.
 But at least ZRLE meaning, following one decompositon image is same as
 avobe individual sub image. I use this format.
 (Strictly saying, transfer order is reverse(Hxy->Hy->Hx->L)
  for simplified procedure for any wavelet level.)

   +------+------+
   |      L      |
   +------+------+
   |      Hx     |
   +------+------+
   |      Hy     |
   +------+------+
   |      Hxy    |
   +------+------+
*/
#define ZYWRLE_INC_PTR(data)                         \
    do {                                             \
        data++;                                      \
        if( data - p >= (w + uw) ) {                 \
            data += scanline-(w + uw);               \
            p = data;                                \
        }                                            \
    } while (0)

#define ZYWRLE_TRANSFER_COEFF(buf, data, t, w, h, scanline, level, TRANS) \
    do {                                                                \
        ph = buf;                                                       \
        s = 2 << level;                                                 \
        if (t & 0x01) {                                                 \
            ph += s >> 1;                                               \
        }                                                               \
        if (t & 0x02) {                                                 \
            ph += (s >> 1) * w;                                         \
        }                                                               \
        end = ph + h * w;                                               \
        while (ph < end) {                                              \
            line = ph + w;                                              \
            while (ph < line) {                                         \
                TRANS                                                   \
                    ZYWRLE_INC_PTR(data);                               \
                ph += s;                                                \
            }                                                           \
            ph += (s - 1) * w;                                          \
        }                                                               \
    } while (0)

#define ZYWRLE_PACK_COEFF(buf, data, t, width, height, scanline, level)	\
    ZYWRLE_TRANSFER_COEFF(buf, data, t, width, height, scanline, level, \
                          ZYWRLE_LOAD_COEFF(ph, r, g, b);               \
                          ZYWRLE_SAVE_PIXEL(data, r, g, b);)

#define ZYWRLE_UNPACK_COEFF(buf, data, t, width, height, scanline, level) \
    ZYWRLE_TRANSFER_COEFF(buf, data, t, width, height, scanline, level, \
                          ZYWRLE_LOAD_PIXEL(data, r, g, b);             \
                          ZYWRLE_SAVE_COEFF(ph, r, g, b);)

#define ZYWRLE_SAVE_UNALIGN(data, TRANS)                     \
    do {                                                     \
        top = buf + w * h;                                   \
        end = buf + (w + uw) * (h + uh);                     \
        while (top < end) {                                  \
            TRANS                                            \
                ZYWRLE_INC_PTR(data);                        \
                top++;                                       \
        }                                                    \
    } while (0)

#define ZYWRLE_LOAD_UNALIGN(data,TRANS)                                 \
    do {                                                                \
        top = buf + w * h;                                              \
        if (uw) {                                                       \
            p = data + w;                                               \
            end = (int*)(p + h * scanline);                             \
            while (p < (ZRLE_PIXEL*)end) {                              \
                line = (int*)(p + uw);                                  \
                while (p < (ZRLE_PIXEL*)line) {                         \
                    TRANS                                               \
                        p++;                                            \
                    top++;                                              \
                }                                                       \
                p += scanline - uw;                                     \
            }                                                           \
        }                                                               \
        if (uh) {                                                       \
            p = data + h * scanline;                                    \
            end = (int*)(p + uh * scanline);                            \
            while (p < (ZRLE_PIXEL*)end) {                              \
                line = (int*)(p + w);                                   \
                while (p < (ZRLE_PIXEL*)line) {                         \
                    TRANS                                               \
                        p++;                                            \
                    top++;                                              \
                }                                                       \
                p += scanline - w;                                      \
            }                                                           \
        }                                                               \
        if (uw && uh) {                                                 \
            p= data + w + h * scanline;                                 \
            end = (int*)(p + uh * scanline);                            \
            while (p < (ZRLE_PIXEL*)end) {                              \
                line = (int*)(p + uw);                                  \
                while (p < (ZRLE_PIXEL*)line) {                         \
                    TRANS                                               \
                        p++;                                            \
                    top++;                                              \
                }                                                       \
                p += scanline-uw;                                       \
            }                                                           \
        }                                                               \
    } while (0)

static inline void zywrle_calc_size(int *w, int *h, int level)
{
    *w &= ~((1 << level) - 1);
    *h &= ~((1 << level) - 1);
}

#endif
