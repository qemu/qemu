/*
 * QEMU VNC display driver: tight encoding
 *
 * From libvncserver/rfb/rfbproto.h
 * Copyright (C) 2005 Rohit Kumar, Johannes E. Schindelin
 * Copyright (C) 2000-2002 Constantin Kaplinsky.  All Rights Reserved.
 * Copyright (C) 2000 Tridia Corporation.  All Rights Reserved.
 * Copyright (C) 1999 AT&T Laboratories Cambridge.  All Rights Reserved.
 *
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

#ifndef VNC_ENC_TIGHT_H
#define VNC_ENC_TIGHT_H

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * Tight Encoding.
 *
 *-- The first byte of each Tight-encoded rectangle is a "compression control
 *   byte". Its format is as follows (bit 0 is the least significant one):
 *
 *   bit 0:    if 1, then compression stream 0 should be reset;
 *   bit 1:    if 1, then compression stream 1 should be reset;
 *   bit 2:    if 1, then compression stream 2 should be reset;
 *   bit 3:    if 1, then compression stream 3 should be reset;
 *   bits 7-4: if 1000 (0x08), then the compression type is "fill",
 *             if 1001 (0x09), then the compression type is "jpeg",
 *             if 1010 (0x0A), then the compression type is "png",
 *             if 0xxx, then the compression type is "basic",
 *             values greater than 1010 are not valid.
 *
 * If the compression type is "basic", then bits 6..4 of the
 * compression control byte (those xxx in 0xxx) specify the following:
 *
 *   bits 5-4:  decimal representation is the index of a particular zlib
 *              stream which should be used for decompressing the data;
 *   bit 6:     if 1, then a "filter id" byte is following this byte.
 *
 *-- The data that follows after the compression control byte described
 * above depends on the compression type ("fill", "jpeg", "png" or "basic").
 *
 *-- If the compression type is "fill", then the only pixel value follows, in
 * client pixel format (see NOTE 1). This value applies to all pixels of the
 * rectangle.
 *
 *-- If the compression type is "jpeg" or "png", the following data stream
 * looks like this:
 *
 *   1..3 bytes:  data size (N) in compact representation;
 *   N bytes:     JPEG or PNG image.
 *
 * Data size is compactly represented in one, two or three bytes, according
 * to the following scheme:
 *
 *  0xxxxxxx                    (for values 0..127)
 *  1xxxxxxx 0yyyyyyy           (for values 128..16383)
 *  1xxxxxxx 1yyyyyyy zzzzzzzz  (for values 16384..4194303)
 *
 * Here each character denotes one bit, xxxxxxx are the least significant 7
 * bits of the value (bits 0-6), yyyyyyy are bits 7-13, and zzzzzzzz are the
 * most significant 8 bits (bits 14-21). For example, decimal value 10000
 * should be represented as two bytes: binary 10010000 01001110, or
 * hexadecimal 90 4E.
 *
 *-- If the compression type is "basic" and bit 6 of the compression control
 * byte was set to 1, then the next (second) byte specifies "filter id" which
 * tells the decoder what filter type was used by the encoder to pre-process
 * pixel data before the compression. The "filter id" byte can be one of the
 * following:
 *
 *   0:  no filter ("copy" filter);
 *   1:  "palette" filter;
 *   2:  "gradient" filter.
 *
 *-- If bit 6 of the compression control byte is set to 0 (no "filter id"
 * byte), or if the filter id is 0, then raw pixel values in the client
 * format (see NOTE 1) will be compressed. See below details on the
 * compression.
 *
 *-- The "gradient" filter pre-processes pixel data with a simple algorithm
 * which converts each color component to a difference between a "predicted"
 * intensity and the actual intensity. Such a technique does not affect
 * uncompressed data size, but helps to compress photo-like images better.
 * Pseudo-code for converting intensities to differences is the following:
 *
 *   P[i,j] := V[i-1,j] + V[i,j-1] - V[i-1,j-1];
 *   if (P[i,j] < 0) then P[i,j] := 0;
 *   if (P[i,j] > MAX) then P[i,j] := MAX;
 *   D[i,j] := V[i,j] - P[i,j];
 *
 * Here V[i,j] is the intensity of a color component for a pixel at
 * coordinates (i,j). MAX is the maximum value of intensity for a color
 * component.
 *
 *-- The "palette" filter converts true-color pixel data to indexed colors
 * and a palette which can consist of 2..256 colors. If the number of colors
 * is 2, then each pixel is encoded in 1 bit, otherwise 8 bits is used to
 * encode one pixel. 1-bit encoding is performed such way that the most
 * significant bits correspond to the leftmost pixels, and each raw of pixels
 * is aligned to the byte boundary. When "palette" filter is used, the
 * palette is sent before the pixel data. The palette begins with an unsigned
 * byte which value is the number of colors in the palette minus 1 (i.e. 1
 * means 2 colors, 255 means 256 colors in the palette). Then follows the
 * palette itself which consist of pixel values in client pixel format (see
 * NOTE 1).
 *
 *-- The pixel data is compressed using the zlib library. But if the data
 * size after applying the filter but before the compression is less then 12,
 * then the data is sent as is, uncompressed. Four separate zlib streams
 * (0..3) can be used and the decoder should read the actual stream id from
 * the compression control byte (see NOTE 2).
 *
 * If the compression is not used, then the pixel data is sent as is,
 * otherwise the data stream looks like this:
 *
 *   1..3 bytes:  data size (N) in compact representation;
 *   N bytes:     zlib-compressed data.
 *
 * Data size is compactly represented in one, two or three bytes, just like
 * in the "jpeg" compression method (see above).
 *
 *-- NOTE 1. If the color depth is 24, and all three color components are
 * 8-bit wide, then one pixel in Tight encoding is always represented by
 * three bytes, where the first byte is red component, the second byte is
 * green component, and the third byte is blue component of the pixel color
 * value. This applies to colors in palettes as well.
 *
 *-- NOTE 2. The decoder must reset compression streams' states before
 * decoding the rectangle, if some of bits 0,1,2,3 in the compression control
 * byte are set to 1. Note that the decoder must reset zlib streams even if
 * the compression type is "fill", "jpeg" or "png".
 *
 *-- NOTE 3. The "gradient" filter and "jpeg" compression may be used only
 * when bits-per-pixel value is either 16 or 32, not 8.
 *
 *-- NOTE 4. The width of any Tight-encoded rectangle cannot exceed 2048
 * pixels. If a rectangle is wider, it must be split into several rectangles
 * and each one should be encoded separately.
 *
 */

#define VNC_TIGHT_EXPLICIT_FILTER       0x04
#define VNC_TIGHT_FILL                  0x08
#define VNC_TIGHT_JPEG                  0x09
#define VNC_TIGHT_PNG                   0x0A
#define VNC_TIGHT_MAX_SUBENCODING       0x0A

/* Filters to improve compression efficiency */
#define VNC_TIGHT_FILTER_COPY             0x00
#define VNC_TIGHT_FILTER_PALETTE          0x01
#define VNC_TIGHT_FILTER_GRADIENT         0x02

/* Note: The following constant should not be changed. */
#define VNC_TIGHT_MIN_TO_COMPRESS 12

/* The parameters below may be adjusted. */
#define VNC_TIGHT_MIN_SPLIT_RECT_SIZE     4096
#define VNC_TIGHT_MIN_SOLID_SUBRECT_SIZE  2048
#define VNC_TIGHT_MAX_SPLIT_TILE_SIZE       16

#define VNC_TIGHT_JPEG_MIN_RECT_SIZE      4096
#define VNC_TIGHT_DETECT_SUBROW_WIDTH        7
#define VNC_TIGHT_DETECT_MIN_WIDTH           8
#define VNC_TIGHT_DETECT_MIN_HEIGHT          8

#endif /* VNC_ENC_TIGHT_H */
