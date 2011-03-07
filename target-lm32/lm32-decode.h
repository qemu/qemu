/*
 *  LatticeMico32 instruction decoding macros.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
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

/* Convenient binary macros */
#define HEX__(n) 0x##n##LU
#define B8__(x) (((x&0x0000000FLU) ? 1 : 0) \
                  + ((x&0x000000F0LU) ? 2 : 0) \
                  + ((x&0x00000F00LU) ? 4 : 0) \
                  + ((x&0x0000F000LU) ? 8 : 0) \
                  + ((x&0x000F0000LU) ? 16 : 0) \
                  + ((x&0x00F00000LU) ? 32 : 0) \
                  + ((x&0x0F000000LU) ? 64 : 0) \
                  + ((x&0xF0000000LU) ? 128 : 0))
#define B8(d) ((unsigned char)B8__(HEX__(d)))

/* Decode logic, value and mask.  */
#define DEC_ADD     {B8(00001101), B8(00011111)}
#define DEC_AND     {B8(00001000), B8(00011111)}
#define DEC_ANDHI   {B8(00011000), B8(00111111)}
#define DEC_B       {B8(00110000), B8(00111111)}
#define DEC_BI      {B8(00111000), B8(00111111)}
#define DEC_BE      {B8(00010001), B8(00111111)}
#define DEC_BG      {B8(00010010), B8(00111111)}
#define DEC_BGE     {B8(00010011), B8(00111111)}
#define DEC_BGEU    {B8(00010100), B8(00111111)}
#define DEC_BGU     {B8(00010101), B8(00111111)}
#define DEC_BNE     {B8(00010111), B8(00111111)}
#define DEC_CALL    {B8(00110110), B8(00111111)}
#define DEC_CALLI   {B8(00111110), B8(00111111)}
#define DEC_CMPE    {B8(00011001), B8(00011111)}
#define DEC_CMPG    {B8(00011010), B8(00011111)}
#define DEC_CMPGE   {B8(00011011), B8(00011111)}
#define DEC_CMPGEU  {B8(00011100), B8(00011111)}
#define DEC_CMPGU   {B8(00011101), B8(00011111)}
#define DEC_CMPNE   {B8(00011111), B8(00011111)}
#define DEC_DIVU    {B8(00100011), B8(00111111)}
#define DEC_LB      {B8(00000100), B8(00111111)}
#define DEC_LBU     {B8(00010000), B8(00111111)}
#define DEC_LH      {B8(00000111), B8(00111111)}
#define DEC_LHU     {B8(00001011), B8(00111111)}
#define DEC_LW      {B8(00001010), B8(00111111)}
#define DEC_MODU    {B8(00110001), B8(00111111)}
#define DEC_MUL     {B8(00000010), B8(00011111)}
#define DEC_NOR     {B8(00000001), B8(00011111)}
#define DEC_OR      {B8(00001110), B8(00011111)}
#define DEC_ORHI    {B8(00011110), B8(00111111)}
#define DEC_RAISE   {B8(00101011), B8(00111111)}
#define DEC_RCSR    {B8(00100100), B8(00111111)}
#define DEC_SB      {B8(00001100), B8(00111111)}
#define DEC_SEXTB   {B8(00101100), B8(00111111)}
#define DEC_SEXTH   {B8(00110111), B8(00111111)}
#define DEC_SH      {B8(00000011), B8(00111111)}
#define DEC_SL      {B8(00001111), B8(00011111)}
#define DEC_SR      {B8(00000101), B8(00011111)}
#define DEC_SRU     {B8(00000000), B8(00011111)}
#define DEC_SUB     {B8(00110010), B8(00111111)}
#define DEC_SW      {B8(00010110), B8(00111111)}
#define DEC_USER    {B8(00110011), B8(00111111)}
#define DEC_WCSR    {B8(00110100), B8(00111111)}
#define DEC_XNOR    {B8(00001001), B8(00011111)}
#define DEC_XOR     {B8(00000110), B8(00011111)}

