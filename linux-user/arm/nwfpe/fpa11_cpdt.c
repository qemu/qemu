/*
    NetWinder Floating Point Emulator
    (c) Rebel.com, 1998-1999
    (c) Philip Blundell, 1998

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
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "fpa11.h"
#include "softfloat.h"
#include "fpopcode.h"
//#include "fpmodule.h"
//#include "fpmodule.inl"

//#include <asm/uaccess.h>

static inline
void loadSingle(const unsigned int Fn, target_ulong addr)
{
   FPA11 *fpa11 = GET_FPA11();
   fpa11->fType[Fn] = typeSingle;
   /* FIXME - handle failure of get_user() */
   get_user_u32(fpa11->fpreg[Fn].fSingle, addr);
}

static inline
void loadDouble(const unsigned int Fn, target_ulong addr)
{
   FPA11 *fpa11 = GET_FPA11();
   unsigned int *p;
   p = (unsigned int*)&fpa11->fpreg[Fn].fDouble;
   fpa11->fType[Fn] = typeDouble;
#ifdef WORDS_BIGENDIAN
   /* FIXME - handle failure of get_user() */
   get_user_u32(p[0], addr); /* sign & exponent */
   get_user_u32(p[1], addr + 4);
#else
   /* FIXME - handle failure of get_user() */
   get_user_u32(p[0], addr + 4);
   get_user_u32(p[1], addr); /* sign & exponent */
#endif
}

static inline
void loadExtended(const unsigned int Fn, target_ulong addr)
{
   FPA11 *fpa11 = GET_FPA11();
   unsigned int *p;
   p = (unsigned int*)&fpa11->fpreg[Fn].fExtended;
   fpa11->fType[Fn] = typeExtended;
   /* FIXME - handle failure of get_user() */
   get_user_u32(p[0], addr);  /* sign & exponent */
   get_user_u32(p[1], addr + 8);  /* ls bits */
   get_user_u32(p[2], addr + 4);  /* ms bits */
}

static inline
void loadMultiple(const unsigned int Fn, target_ulong addr)
{
   FPA11 *fpa11 = GET_FPA11();
   register unsigned int *p;
   unsigned long x;

   p = (unsigned int*)&(fpa11->fpreg[Fn]);
   /* FIXME - handle failure of get_user() */
   get_user_u32(x, addr);
   fpa11->fType[Fn] = (x >> 14) & 0x00000003;

   switch (fpa11->fType[Fn])
   {
      case typeSingle:
      case typeDouble:
      {
         /* FIXME - handle failure of get_user() */
         get_user_u32(p[0], addr + 8);  /* Single */
         get_user_u32(p[1], addr + 4);  /* double msw */
         p[2] = 0;        /* empty */
      }
      break;

      case typeExtended:
      {
         /* FIXME - handle failure of get_user() */
         get_user_u32(p[1], addr + 8);
         get_user_u32(p[2], addr + 4);  /* msw */
         p[0] = (x & 0x80003fff);
      }
      break;
   }
}

static inline
void storeSingle(const unsigned int Fn, target_ulong addr)
{
   FPA11 *fpa11 = GET_FPA11();
   float32 val;
   register unsigned int *p = (unsigned int*)&val;

   switch (fpa11->fType[Fn])
   {
      case typeDouble:
         val = float64_to_float32(fpa11->fpreg[Fn].fDouble, &fpa11->fp_status);
      break;

      case typeExtended:
         val = floatx80_to_float32(fpa11->fpreg[Fn].fExtended, &fpa11->fp_status);
      break;

      default: val = fpa11->fpreg[Fn].fSingle;
   }

   /* FIXME - handle put_user() failures */
   put_user_u32(p[0], addr);
}

static inline
void storeDouble(const unsigned int Fn, target_ulong addr)
{
   FPA11 *fpa11 = GET_FPA11();
   float64 val;
   register unsigned int *p = (unsigned int*)&val;

   switch (fpa11->fType[Fn])
   {
      case typeSingle:
         val = float32_to_float64(fpa11->fpreg[Fn].fSingle, &fpa11->fp_status);
      break;

      case typeExtended:
         val = floatx80_to_float64(fpa11->fpreg[Fn].fExtended, &fpa11->fp_status);
      break;

      default: val = fpa11->fpreg[Fn].fDouble;
   }
   /* FIXME - handle put_user() failures */
#ifdef WORDS_BIGENDIAN
   put_user_u32(p[0], addr);	/* msw */
   put_user_u32(p[1], addr + 4);	/* lsw */
#else
   put_user_u32(p[1], addr);	/* msw */
   put_user_u32(p[0], addr + 4);	/* lsw */
#endif
}

static inline
void storeExtended(const unsigned int Fn, target_ulong addr)
{
   FPA11 *fpa11 = GET_FPA11();
   floatx80 val;
   register unsigned int *p = (unsigned int*)&val;

   switch (fpa11->fType[Fn])
   {
      case typeSingle:
         val = float32_to_floatx80(fpa11->fpreg[Fn].fSingle, &fpa11->fp_status);
      break;

      case typeDouble:
         val = float64_to_floatx80(fpa11->fpreg[Fn].fDouble, &fpa11->fp_status);
      break;

      default: val = fpa11->fpreg[Fn].fExtended;
   }

   /* FIXME - handle put_user() failures */
   put_user_u32(p[0], addr); /* sign & exp */
   put_user_u32(p[1], addr + 8);
   put_user_u32(p[2], addr + 4); /* msw */
}

static inline
void storeMultiple(const unsigned int Fn, target_ulong addr)
{
   FPA11 *fpa11 = GET_FPA11();
   register unsigned int nType, *p;

   p = (unsigned int*)&(fpa11->fpreg[Fn]);
   nType = fpa11->fType[Fn];

   switch (nType)
   {
      case typeSingle:
      case typeDouble:
      {
         put_user_u32(p[0], addr + 8); /* single */
	 put_user_u32(p[1], addr + 4); /* double msw */
	 put_user_u32(nType << 14, addr);
      }
      break;

      case typeExtended:
      {
         put_user_u32(p[2], addr + 4); /* msw */
	 put_user_u32(p[1], addr + 8);
	 put_user_u32((p[0] & 0x80003fff) | (nType << 14), addr);
      }
      break;
   }
}

static unsigned int PerformLDF(const unsigned int opcode)
{
    target_ulong pBase, pAddress, pFinal;
    unsigned int nRc = 1,
     write_back = WRITE_BACK(opcode);

   //printk("PerformLDF(0x%08x), Fd = 0x%08x\n",opcode,getFd(opcode));

   pBase = readRegister(getRn(opcode));
   if (REG_PC == getRn(opcode))
   {
     pBase += 8;
     write_back = 0;
   }

   pFinal = pBase;
   if (BIT_UP_SET(opcode))
     pFinal += getOffset(opcode) * 4;
   else
     pFinal -= getOffset(opcode) * 4;

   if (PREINDEXED(opcode)) pAddress = pFinal; else pAddress = pBase;

   switch (opcode & MASK_TRANSFER_LENGTH)
   {
      case TRANSFER_SINGLE  : loadSingle(getFd(opcode),pAddress);   break;
      case TRANSFER_DOUBLE  : loadDouble(getFd(opcode),pAddress);   break;
      case TRANSFER_EXTENDED: loadExtended(getFd(opcode),pAddress); break;
      default: nRc = 0;
   }

   if (write_back) writeRegister(getRn(opcode),(unsigned int)pFinal);
   return nRc;
}

static unsigned int PerformSTF(const unsigned int opcode)
{
   target_ulong pBase, pAddress, pFinal;
   unsigned int nRc = 1,
     write_back = WRITE_BACK(opcode);

   //printk("PerformSTF(0x%08x), Fd = 0x%08x\n",opcode,getFd(opcode));
   SetRoundingMode(ROUND_TO_NEAREST);

   pBase = readRegister(getRn(opcode));
   if (REG_PC == getRn(opcode))
   {
     pBase += 8;
     write_back = 0;
   }

   pFinal = pBase;
   if (BIT_UP_SET(opcode))
     pFinal += getOffset(opcode) * 4;
   else
     pFinal -= getOffset(opcode) * 4;

   if (PREINDEXED(opcode)) pAddress = pFinal; else pAddress = pBase;

   switch (opcode & MASK_TRANSFER_LENGTH)
   {
      case TRANSFER_SINGLE  : storeSingle(getFd(opcode),pAddress);   break;
      case TRANSFER_DOUBLE  : storeDouble(getFd(opcode),pAddress);   break;
      case TRANSFER_EXTENDED: storeExtended(getFd(opcode),pAddress); break;
      default: nRc = 0;
   }

   if (write_back) writeRegister(getRn(opcode),(unsigned int)pFinal);
   return nRc;
}

static unsigned int PerformLFM(const unsigned int opcode)
{
   unsigned int i, Fd,
     write_back = WRITE_BACK(opcode);
   target_ulong pBase, pAddress, pFinal;

   pBase = readRegister(getRn(opcode));
   if (REG_PC == getRn(opcode))
   {
     pBase += 8;
     write_back = 0;
   }

   pFinal = pBase;
   if (BIT_UP_SET(opcode))
     pFinal += getOffset(opcode) * 4;
   else
     pFinal -= getOffset(opcode) * 4;

   if (PREINDEXED(opcode)) pAddress = pFinal; else pAddress = pBase;

   Fd = getFd(opcode);
   for (i=getRegisterCount(opcode);i>0;i--)
   {
     loadMultiple(Fd,pAddress);
     pAddress += 12; Fd++;
     if (Fd == 8) Fd = 0;
   }

   if (write_back) writeRegister(getRn(opcode),(unsigned int)pFinal);
   return 1;
}

static unsigned int PerformSFM(const unsigned int opcode)
{
   unsigned int i, Fd,
     write_back = WRITE_BACK(opcode);
   target_ulong pBase, pAddress, pFinal;

   pBase = readRegister(getRn(opcode));
   if (REG_PC == getRn(opcode))
   {
     pBase += 8;
     write_back = 0;
   }

   pFinal = pBase;
   if (BIT_UP_SET(opcode))
     pFinal += getOffset(opcode) * 4;
   else
     pFinal -= getOffset(opcode) * 4;

   if (PREINDEXED(opcode)) pAddress = pFinal; else pAddress = pBase;

   Fd = getFd(opcode);
   for (i=getRegisterCount(opcode);i>0;i--)
   {
     storeMultiple(Fd,pAddress);
     pAddress += 12; Fd++;
     if (Fd == 8) Fd = 0;
   }

   if (write_back) writeRegister(getRn(opcode),(unsigned int)pFinal);
   return 1;
}

#if 1
unsigned int EmulateCPDT(const unsigned int opcode)
{
  unsigned int nRc = 0;

  //printk("EmulateCPDT(0x%08x)\n",opcode);

  if (LDF_OP(opcode))
  {
    nRc = PerformLDF(opcode);
  }
  else if (LFM_OP(opcode))
  {
    nRc = PerformLFM(opcode);
  }
  else if (STF_OP(opcode))
  {
    nRc = PerformSTF(opcode);
  }
  else if (SFM_OP(opcode))
  {
    nRc = PerformSFM(opcode);
  }
  else
  {
    nRc = 0;
  }

  return nRc;
}
#endif
