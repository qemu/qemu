/*
    NetWinder Floating Point Emulator
    (c) Rebel.COM, 1998,1999

    Direct questions, comments to Scott Bambrough <scottb@netwinder.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include "fpa11.h"
#include "fpu/softfloat.h"
#include "fpopcode.h"
#include "fpsr.h"
//#include "fpmodule.h"
//#include "fpmodule.inl"

const floatx80 floatx80Constant[] = {
  { 0x0000000000000000ULL, 0x0000},	/* extended 0.0 */
  { 0x8000000000000000ULL, 0x3fff},	/* extended 1.0 */
  { 0x8000000000000000ULL, 0x4000},	/* extended 2.0 */
  { 0xc000000000000000ULL, 0x4000},	/* extended 3.0 */
  { 0x8000000000000000ULL, 0x4001},	/* extended 4.0 */
  { 0xa000000000000000ULL, 0x4001},	/* extended 5.0 */
  { 0x8000000000000000ULL, 0x3ffe},	/* extended 0.5 */
  { 0xa000000000000000ULL, 0x4002}	/* extended 10.0 */
};

const float64 float64Constant[] = {
  const_float64(0x0000000000000000ULL),		/* double 0.0 */
  const_float64(0x3ff0000000000000ULL),		/* double 1.0 */
  const_float64(0x4000000000000000ULL),		/* double 2.0 */
  const_float64(0x4008000000000000ULL),		/* double 3.0 */
  const_float64(0x4010000000000000ULL),		/* double 4.0 */
  const_float64(0x4014000000000000ULL),		/* double 5.0 */
  const_float64(0x3fe0000000000000ULL),		/* double 0.5 */
  const_float64(0x4024000000000000ULL)			/* double 10.0 */
};

const float32 float32Constant[] = {
  const_float32(0x00000000),				/* single 0.0 */
  const_float32(0x3f800000),				/* single 1.0 */
  const_float32(0x40000000),				/* single 2.0 */
  const_float32(0x40400000),				/* single 3.0 */
  const_float32(0x40800000),				/* single 4.0 */
  const_float32(0x40a00000),				/* single 5.0 */
  const_float32(0x3f000000),				/* single 0.5 */
  const_float32(0x41200000)				/* single 10.0 */
};

unsigned int getRegisterCount(const unsigned int opcode)
{
  unsigned int nRc;

  switch (opcode & MASK_REGISTER_COUNT)
  {
    case 0x00000000: nRc = 4; break;
    case 0x00008000: nRc = 1; break;
    case 0x00400000: nRc = 2; break;
    case 0x00408000: nRc = 3; break;
    default: nRc = 0;
  }

  return(nRc);
}

unsigned int getDestinationSize(const unsigned int opcode)
{
  unsigned int nRc;

  switch (opcode & MASK_DESTINATION_SIZE)
  {
    case 0x00000000: nRc = typeSingle; break;
    case 0x00000080: nRc = typeDouble; break;
    case 0x00080000: nRc = typeExtended; break;
    default: nRc = typeNone;
  }

  return(nRc);
}

/* condition code lookup table
 index into the table is test code: EQ, NE, ... LT, GT, AL, NV
 bit position in short is condition code: NZCV */
static const unsigned short aCC[16] = {
    0xF0F0, // EQ == Z set
    0x0F0F, // NE
    0xCCCC, // CS == C set
    0x3333, // CC
    0xFF00, // MI == N set
    0x00FF, // PL
    0xAAAA, // VS == V set
    0x5555, // VC
    0x0C0C, // HI == C set && Z clear
    0xF3F3, // LS == C clear || Z set
    0xAA55, // GE == (N==V)
    0x55AA, // LT == (N!=V)
    0x0A05, // GT == (!Z && (N==V))
    0xF5FA, // LE == (Z || (N!=V))
    0xFFFF, // AL always
    0 // NV
};
