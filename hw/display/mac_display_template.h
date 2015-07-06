/*
 * QEMU Macintosh 128k draw line template
 *
 * Copyright (c) 2015 Pavel Dovgalyuk
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#if DEPTH == 8
# define BPP 1
# define PIXEL_TYPE uint8_t
#elif DEPTH == 15 || DEPTH == 16
# define BPP 2
# define PIXEL_TYPE uint16_t
#elif DEPTH == 32
# define BPP 4
# define PIXEL_TYPE uint32_t
#else
# error unsupport depth
#endif

/*
 * 1-bit colour
 */
static void glue(draw_line_, DEPTH)(uint8_t *d, const uint8_t *s, int width)
{
    uint8_t v, r, g, b, mask;

    do {
        v = ldub_p((void *) s);
        for (mask = 0x80 ; mask ; mask >>= 1) {
            r = (v & mask) ? 0xff : 0;
            g = (v & mask) ? 0xff : 0;
            b = (v & mask) ? 0xff : 0;
            ((PIXEL_TYPE *) d)[0] = glue(rgb_to_pixel, DEPTH)(r, g, b);
            d += BPP;
        }
        ++s;
        width -= 8;
    } while (width > 0);
}

#undef DEPTH
#undef BPP
#undef PIXEL_TYPE
