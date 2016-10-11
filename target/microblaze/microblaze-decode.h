/*
 *  MicroBlaze insn decoding macros.
 *
 *  Copyright (c) 2009 Edgar E. Iglesias <edgar.iglesias@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/* Convenient binary macros.  */
#define HEX__(n) 0x##n##LU
#define B8__(x) ((x&0x0000000FLU)?1:0) \
                 + ((x&0x000000F0LU)?2:0) \
                 + ((x&0x00000F00LU)?4:0) \
                 + ((x&0x0000F000LU)?8:0) \
                 + ((x&0x000F0000LU)?16:0) \
                 + ((x&0x00F00000LU)?32:0) \
                 + ((x&0x0F000000LU)?64:0) \
                 + ((x&0xF0000000LU)?128:0)
#define B8(d) ((unsigned char)B8__(HEX__(d)))

/* Decode logic, value and mask.  */
#define DEC_ADD     {B8(00000000), B8(00110001)}
#define DEC_SUB     {B8(00000001), B8(00110001)}
#define DEC_AND     {B8(00100001), B8(00110101)}
#define DEC_XOR     {B8(00100010), B8(00110111)}
#define DEC_OR      {B8(00100000), B8(00110111)}
#define DEC_BIT     {B8(00100100), B8(00111111)}
#define DEC_MSR     {B8(00100101), B8(00111111)}

#define DEC_BARREL  {B8(00010001), B8(00110111)}
#define DEC_MUL     {B8(00010000), B8(00110111)}
#define DEC_DIV     {B8(00010010), B8(00110111)}
#define DEC_FPU     {B8(00010110), B8(00111111)}

#define DEC_LD      {B8(00110000), B8(00110100)}
#define DEC_ST      {B8(00110100), B8(00110100)}
#define DEC_IMM     {B8(00101100), B8(00111111)}

#define DEC_BR      {B8(00100110), B8(00110111)}
#define DEC_BCC     {B8(00100111), B8(00110111)}
#define DEC_RTS     {B8(00101101), B8(00111111)}

#define DEC_STREAM  {B8(00010011), B8(00110111)}

