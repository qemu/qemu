/*
 *  CRIS insn decoding macros.
 *
 *  Copyright (c) 2007 AXIS Communications AB
 *  Written by Edgar E. Iglesias.
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
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

/* Quick imm.  */
#define DEC_BCCQ     {B8(00000000), B8(11110000)}
#define DEC_ADDOQ    {B8(00010000), B8(11110000)}
#define DEC_ADDQ     {B8(00100000), B8(11111100)}
#define DEC_MOVEQ    {B8(00100100), B8(11111100)}
#define DEC_SUBQ     {B8(00101000), B8(11111100)}
#define DEC_CMPQ     {B8(00101100), B8(11111100)}
#define DEC_ANDQ     {B8(00110000), B8(11111100)}
#define DEC_ORQ      {B8(00110100), B8(11111100)}
#define DEC_BTSTQ    {B8(00111000), B8(11111110)}
#define DEC_ASRQ     {B8(00111010), B8(11111110)}
#define DEC_LSLQ     {B8(00111100), B8(11111110)}
#define DEC_LSRQ     {B8(00111110), B8(11111110)}

/* Register.  */
#define DEC_MOVU_R   {B8(01000100), B8(11111110)}
#define DEC_MOVU_R   {B8(01000100), B8(11111110)}
#define DEC_MOVS_R   {B8(01000110), B8(11111110)}
#define DEC_MOVE_R   {B8(01100100), B8(11111100)}
#define DEC_MOVE_RP  {B8(01100011), B8(11111111)}
#define DEC_MOVE_PR  {B8(01100111), B8(11111111)}
#define DEC_DSTEP_R  {B8(01101111), B8(11111111)}
#define DEC_MOVE_RS  {B8(10110111), B8(11111111)}
#define DEC_MOVE_SR  {B8(11110111), B8(11111111)}
#define DEC_ADDU_R   {B8(01000000), B8(11111110)}
#define DEC_ADDS_R   {B8(01000010), B8(11111110)}
#define DEC_ADD_R    {B8(01100000), B8(11111100)}
#define DEC_ADDI_R   {B8(01010000), B8(11111100)}
#define DEC_MULS_R   {B8(11010000), B8(11111100)}
#define DEC_MULU_R   {B8(10010000), B8(11111100)}
#define DEC_ADDI_ACR {B8(01010100), B8(11111100)}
#define DEC_NEG_R    {B8(01011000), B8(11111100)}
#define DEC_BOUND_R  {B8(01011100), B8(11111100)}
#define DEC_SUBU_R   {B8(01001000), B8(11111110)}
#define DEC_SUBS_R   {B8(01001010), B8(11111110)}
#define DEC_SUB_R    {B8(01101000), B8(11111100)}
#define DEC_CMP_R    {B8(01101100), B8(11111100)}
#define DEC_AND_R    {B8(01110000), B8(11111100)}
#define DEC_ABS_R    {B8(01101011), B8(11111111)}
#define DEC_LZ_R     {B8(01110011), B8(11111111)}
#define DEC_MCP_R    {B8(01111111), B8(11111111)}
#define DEC_SWAP_R   {B8(01110111), B8(11111111)}
#define DEC_XOR_R    {B8(01111011), B8(11111111)}
#define DEC_LSL_R    {B8(01001100), B8(11111100)}
#define DEC_LSR_R    {B8(01111100), B8(11111100)}
#define DEC_ASR_R    {B8(01111000), B8(11111100)}
#define DEC_OR_R     {B8(01110100), B8(11111100)}
#define DEC_BTST_R   {B8(01001111), B8(11111111)}

/* Fixed.  */
#define DEC_SETF     {B8(01011011), B8(11111111)}
#define DEC_CLEARF   {B8(01011111), B8(11111111)}

/* Memory.  */
#define DEC_ADDU_M   {B8(10000000), B8(10111110)}
#define DEC_ADDS_M   {B8(10000010), B8(10111110)}
#define DEC_MOVU_M   {B8(10000100), B8(10111110)}
#define DEC_MOVS_M   {B8(10000110), B8(10111110)}
#define DEC_SUBU_M   {B8(10001000), B8(10111110)}
#define DEC_SUBS_M   {B8(10001010), B8(10111110)}
#define DEC_CMPU_M   {B8(10001100), B8(10111110)}
#define DEC_CMPS_M   {B8(10001110), B8(10111110)}
#define DEC_ADDO_M   {B8(10010100), B8(10111100)}
#define DEC_BOUND_M  {B8(10011100), B8(10111100)}
#define DEC_ADD_M    {B8(10100000), B8(10111100)}
#define DEC_MOVE_MR  {B8(10100100), B8(10111100)}
#define DEC_SUB_M    {B8(10101000), B8(10111100)}
#define DEC_CMP_M    {B8(10101100), B8(10111100)}
#define DEC_AND_M    {B8(10110000), B8(10111100)}
#define DEC_OR_M     {B8(10110100), B8(10111100)}
#define DEC_TEST_M   {B8(10111000), B8(10111100)}
#define DEC_MOVE_RM  {B8(10111100), B8(10111100)}

#define DEC_ADDC_R   {B8(01010111), B8(11111111)}
#define DEC_ADDC_MR  {B8(10011010), B8(10111111)}
#define DEC_LAPCQ    {B8(10010111), B8(11111111)}
#define DEC_LAPC_IM  {B8(11010111), B8(11111111)}

#define DEC_MOVE_MP  {B8(10100011), B8(10111111)}
#define DEC_MOVE_PM  {B8(10100111), B8(10111111)}

#define DEC_SCC_R    {B8(01010011), B8(11111111)}
#define DEC_RFE_ETC  {B8(10010011), B8(11111111)}
#define DEC_JUMP_P   {B8(10011111), B8(11111111)}
#define DEC_BCC_IM   {B8(11011111), B8(11111111)}
#define DEC_JAS_R    {B8(10011011), B8(11111111)}
#define DEC_JASC_R   {B8(10110011), B8(11111111)}
#define DEC_JAS_IM   {B8(11011011), B8(11111111)}
#define DEC_JASC_IM  {B8(11110011), B8(11111111)}
#define DEC_BAS_IM   {B8(11101011), B8(11111111)}
#define DEC_BASC_IM  {B8(11101111), B8(11111111)}
#define DEC_MOVEM_MR {B8(10111011), B8(10111111)}
#define DEC_MOVEM_RM {B8(10111111), B8(10111111)}

#define DEC_FTAG_FIDX_D_M {B8(10101011), B8(11111111)}
#define DEC_FTAG_FIDX_I_M {B8(11010011), B8(11111111)}
