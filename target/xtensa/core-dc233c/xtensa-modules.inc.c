/* Xtensa configuration-specific ISA information.

   Customer ID=4869; Build=0x2cfec; Copyright (c) 2003-2010 Tensilica Inc.

   Permission is hereby granted, free of charge, to any person obtaining
   a copy of this software and associated documentation files (the
   "Software"), to deal in the Software without restriction, including
   without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to
   permit persons to whom the Software is furnished to do so, subject to
   the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.  */

#include "qemu/osdep.h"
#include "xtensa-isa.h"
#include "xtensa-isa-internal.h"


/* Sysregs.  */

static xtensa_sysreg_internal sysregs[] = {
  { "LBEG", 0, 0 },
  { "LEND", 1, 0 },
  { "LCOUNT", 2, 0 },
  { "ACCLO", 16, 0 },
  { "ACCHI", 17, 0 },
  { "M0", 32, 0 },
  { "M1", 33, 0 },
  { "M2", 34, 0 },
  { "M3", 35, 0 },
  { "PTEVADDR", 83, 0 },
  { "MMID", 89, 0 },
  { "DDR", 104, 0 },
  { "176", 176, 0 },
  { "208", 208, 0 },
  { "INTERRUPT", 226, 0 },
  { "INTCLEAR", 227, 0 },
  { "CCOUNT", 234, 0 },
  { "PRID", 235, 0 },
  { "ICOUNT", 236, 0 },
  { "CCOMPARE0", 240, 0 },
  { "CCOMPARE1", 241, 0 },
  { "CCOMPARE2", 242, 0 },
  { "VECBASE", 231, 0 },
  { "EPC1", 177, 0 },
  { "EPC2", 178, 0 },
  { "EPC3", 179, 0 },
  { "EPC4", 180, 0 },
  { "EPC5", 181, 0 },
  { "EPC6", 182, 0 },
  { "EPC7", 183, 0 },
  { "EXCSAVE1", 209, 0 },
  { "EXCSAVE2", 210, 0 },
  { "EXCSAVE3", 211, 0 },
  { "EXCSAVE4", 212, 0 },
  { "EXCSAVE5", 213, 0 },
  { "EXCSAVE6", 214, 0 },
  { "EXCSAVE7", 215, 0 },
  { "EPS2", 194, 0 },
  { "EPS3", 195, 0 },
  { "EPS4", 196, 0 },
  { "EPS5", 197, 0 },
  { "EPS6", 198, 0 },
  { "EPS7", 199, 0 },
  { "EXCCAUSE", 232, 0 },
  { "DEPC", 192, 0 },
  { "EXCVADDR", 238, 0 },
  { "WINDOWBASE", 72, 0 },
  { "WINDOWSTART", 73, 0 },
  { "SAR", 3, 0 },
  { "LITBASE", 5, 0 },
  { "PS", 230, 0 },
  { "MISC0", 244, 0 },
  { "MISC1", 245, 0 },
  { "INTENABLE", 228, 0 },
  { "DBREAKA0", 144, 0 },
  { "DBREAKC0", 160, 0 },
  { "DBREAKA1", 145, 0 },
  { "DBREAKC1", 161, 0 },
  { "IBREAKA0", 128, 0 },
  { "IBREAKA1", 129, 0 },
  { "IBREAKENABLE", 96, 0 },
  { "ICOUNTLEVEL", 237, 0 },
  { "DEBUGCAUSE", 233, 0 },
  { "RASID", 90, 0 },
  { "ITLBCFG", 91, 0 },
  { "DTLBCFG", 92, 0 },
  { "CPENABLE", 224, 0 },
  { "SCOMPARE1", 12, 0 },
  { "ATOMCTL", 99, 0 },
  { "THREADPTR", 231, 1 },
  { "EXPSTATE", 230, 1 }
};

#define NUM_SYSREGS 71
#define MAX_SPECIAL_REG 245
#define MAX_USER_REG 231


/* Processor states.  */

static xtensa_state_internal states[] = {
  { "LCOUNT", 32, 0 },
  { "PC", 32, 0 },
  { "ICOUNT", 32, 0 },
  { "DDR", 32, 0 },
  { "INTERRUPT", 22, 0 },
  { "CCOUNT", 32, 0 },
  { "XTSYNC", 1, 0 },
  { "VECBASE", 22, 0 },
  { "EPC1", 32, 0 },
  { "EPC2", 32, 0 },
  { "EPC3", 32, 0 },
  { "EPC4", 32, 0 },
  { "EPC5", 32, 0 },
  { "EPC6", 32, 0 },
  { "EPC7", 32, 0 },
  { "EXCSAVE1", 32, 0 },
  { "EXCSAVE2", 32, 0 },
  { "EXCSAVE3", 32, 0 },
  { "EXCSAVE4", 32, 0 },
  { "EXCSAVE5", 32, 0 },
  { "EXCSAVE6", 32, 0 },
  { "EXCSAVE7", 32, 0 },
  { "EPS2", 15, 0 },
  { "EPS3", 15, 0 },
  { "EPS4", 15, 0 },
  { "EPS5", 15, 0 },
  { "EPS6", 15, 0 },
  { "EPS7", 15, 0 },
  { "EXCCAUSE", 6, 0 },
  { "PSINTLEVEL", 4, 0 },
  { "PSUM", 1, 0 },
  { "PSWOE", 1, 0 },
  { "PSRING", 2, 0 },
  { "PSEXCM", 1, 0 },
  { "DEPC", 32, 0 },
  { "EXCVADDR", 32, 0 },
  { "WindowBase", 3, 0 },
  { "WindowStart", 8, 0 },
  { "PSCALLINC", 2, 0 },
  { "PSOWB", 4, 0 },
  { "LBEG", 32, 0 },
  { "LEND", 32, 0 },
  { "SAR", 6, 0 },
  { "THREADPTR", 32, 0 },
  { "LITBADDR", 20, 0 },
  { "LITBEN", 1, 0 },
  { "MISC0", 32, 0 },
  { "MISC1", 32, 0 },
  { "ACC", 40, 0 },
  { "InOCDMode", 1, 0 },
  { "INTENABLE", 22, 0 },
  { "DBREAKA0", 32, 0 },
  { "DBREAKC0", 8, 0 },
  { "DBREAKA1", 32, 0 },
  { "DBREAKC1", 8, 0 },
  { "IBREAKA0", 32, 0 },
  { "IBREAKA1", 32, 0 },
  { "IBREAKENABLE", 2, 0 },
  { "ICOUNTLEVEL", 4, 0 },
  { "DEBUGCAUSE", 6, 0 },
  { "DBNUM", 4, 0 },
  { "CCOMPARE0", 32, 0 },
  { "CCOMPARE1", 32, 0 },
  { "CCOMPARE2", 32, 0 },
  { "ASID3", 8, 0 },
  { "ASID2", 8, 0 },
  { "ASID1", 8, 0 },
  { "INSTPGSZID6", 1, 0 },
  { "INSTPGSZID5", 1, 0 },
  { "INSTPGSZID4", 2, 0 },
  { "DATAPGSZID6", 1, 0 },
  { "DATAPGSZID5", 1, 0 },
  { "DATAPGSZID4", 2, 0 },
  { "PTBASE", 10, 0 },
  { "CPENABLE", 8, 0 },
  { "SCOMPARE1", 32, 0 },
  { "ATOMCTL", 6, 0 },
  { "EXPSTATE", 32, XTENSA_STATE_IS_EXPORTED }
};

#define NUM_STATES 78

enum xtensa_state_id {
  STATE_LCOUNT,
  STATE_PC,
  STATE_ICOUNT,
  STATE_DDR,
  STATE_INTERRUPT,
  STATE_CCOUNT,
  STATE_XTSYNC,
  STATE_VECBASE,
  STATE_EPC1,
  STATE_EPC2,
  STATE_EPC3,
  STATE_EPC4,
  STATE_EPC5,
  STATE_EPC6,
  STATE_EPC7,
  STATE_EXCSAVE1,
  STATE_EXCSAVE2,
  STATE_EXCSAVE3,
  STATE_EXCSAVE4,
  STATE_EXCSAVE5,
  STATE_EXCSAVE6,
  STATE_EXCSAVE7,
  STATE_EPS2,
  STATE_EPS3,
  STATE_EPS4,
  STATE_EPS5,
  STATE_EPS6,
  STATE_EPS7,
  STATE_EXCCAUSE,
  STATE_PSINTLEVEL,
  STATE_PSUM,
  STATE_PSWOE,
  STATE_PSRING,
  STATE_PSEXCM,
  STATE_DEPC,
  STATE_EXCVADDR,
  STATE_WindowBase,
  STATE_WindowStart,
  STATE_PSCALLINC,
  STATE_PSOWB,
  STATE_LBEG,
  STATE_LEND,
  STATE_SAR,
  STATE_THREADPTR,
  STATE_LITBADDR,
  STATE_LITBEN,
  STATE_MISC0,
  STATE_MISC1,
  STATE_ACC,
  STATE_InOCDMode,
  STATE_INTENABLE,
  STATE_DBREAKA0,
  STATE_DBREAKC0,
  STATE_DBREAKA1,
  STATE_DBREAKC1,
  STATE_IBREAKA0,
  STATE_IBREAKA1,
  STATE_IBREAKENABLE,
  STATE_ICOUNTLEVEL,
  STATE_DEBUGCAUSE,
  STATE_DBNUM,
  STATE_CCOMPARE0,
  STATE_CCOMPARE1,
  STATE_CCOMPARE2,
  STATE_ASID3,
  STATE_ASID2,
  STATE_ASID1,
  STATE_INSTPGSZID6,
  STATE_INSTPGSZID5,
  STATE_INSTPGSZID4,
  STATE_DATAPGSZID6,
  STATE_DATAPGSZID5,
  STATE_DATAPGSZID4,
  STATE_PTBASE,
  STATE_CPENABLE,
  STATE_SCOMPARE1,
  STATE_ATOMCTL,
  STATE_EXPSTATE
};


/* Field definitions.  */

static unsigned
Field_t_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 24) >> 28);
  return tie_t;
}

static void
Field_t_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0) | (tie_t << 4);
}

static unsigned
Field_s_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  return tie_t;
}

static void
Field_s_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
}

static unsigned
Field_r_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_r_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_op2_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 8) >> 28);
  return tie_t;
}

static void
Field_op2_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf00000) | (tie_t << 20);
}

static unsigned
Field_op1_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 12) >> 28);
  return tie_t;
}

static void
Field_op1_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0000) | (tie_t << 16);
}

static unsigned
Field_op0_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 28) >> 28);
  return tie_t;
}

static void
Field_op0_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf) | (tie_t << 0);
}

static unsigned
Field_n_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 26) >> 30);
  return tie_t;
}

static void
Field_n_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 30) >> 30;
  insn[0] = (insn[0] & ~0x30) | (tie_t << 4);
}

static unsigned
Field_m_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 24) >> 30);
  return tie_t;
}

static void
Field_m_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 30) >> 30;
  insn[0] = (insn[0] & ~0xc0) | (tie_t << 6);
}

static unsigned
Field_sr_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  return tie_t;
}

static void
Field_sr_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
  tie_t = (val << 24) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_st_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  tie_t = (tie_t << 4) | ((insn[0] << 24) >> 28);
  return tie_t;
}

static void
Field_st_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0) | (tie_t << 4);
  tie_t = (val << 24) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
}

static unsigned
Field_thi3_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 3) | ((insn[0] << 24) >> 29);
  return tie_t;
}

static void
Field_thi3_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 29) >> 29;
  insn[0] = (insn[0] & ~0xe0) | (tie_t << 5);
}

static unsigned
Field_t3_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 24) >> 31);
  return tie_t;
}

static void
Field_t3_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x80) | (tie_t << 7);
}

static unsigned
Field_tlo_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 26) >> 30);
  return tie_t;
}

static void
Field_tlo_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 30) >> 30;
  insn[0] = (insn[0] & ~0x30) | (tie_t << 4);
}

static unsigned
Field_w_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 18) >> 30);
  return tie_t;
}

static void
Field_w_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 30) >> 30;
  insn[0] = (insn[0] & ~0x3000) | (tie_t << 12);
}

static unsigned
Field_r3_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 16) >> 31);
  return tie_t;
}

static void
Field_r3_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x8000) | (tie_t << 15);
}

static unsigned
Field_rhi_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 16) >> 30);
  return tie_t;
}

static void
Field_rhi_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 30) >> 30;
  insn[0] = (insn[0] & ~0xc000) | (tie_t << 14);
}

static unsigned
Field_s3to1_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 3) | ((insn[0] << 20) >> 29);
  return tie_t;
}

static void
Field_s3to1_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 29) >> 29;
  insn[0] = (insn[0] & ~0xe00) | (tie_t << 9);
}

static unsigned
Field_op0_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 28) >> 28);
  return tie_t;
}

static void
Field_op0_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf) | (tie_t << 0);
}

static unsigned
Field_t_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 24) >> 28);
  return tie_t;
}

static void
Field_t_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0) | (tie_t << 4);
}

static unsigned
Field_r_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_r_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_op0_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 28) >> 28);
  return tie_t;
}

static void
Field_op0_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf) | (tie_t << 0);
}

static unsigned
Field_z_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 25) >> 31);
  return tie_t;
}

static void
Field_z_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x40) | (tie_t << 6);
}

static unsigned
Field_i_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 24) >> 31);
  return tie_t;
}

static void
Field_i_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x80) | (tie_t << 7);
}

static unsigned
Field_s_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  return tie_t;
}

static void
Field_s_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
}

static unsigned
Field_t_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 24) >> 28);
  return tie_t;
}

static void
Field_t_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0) | (tie_t << 4);
}

static unsigned
Field_bbi4_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 19) >> 31);
  return tie_t;
}

static void
Field_bbi4_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x1000) | (tie_t << 12);
}

static unsigned
Field_bbi_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 19) >> 31);
  tie_t = (tie_t << 4) | ((insn[0] << 24) >> 28);
  return tie_t;
}

static void
Field_bbi_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0) | (tie_t << 4);
  tie_t = (val << 27) >> 31;
  insn[0] = (insn[0] & ~0x1000) | (tie_t << 12);
}

static unsigned
Field_imm12_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 12) | ((insn[0] << 8) >> 20);
  return tie_t;
}

static void
Field_imm12_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 20) >> 20;
  insn[0] = (insn[0] & ~0xfff000) | (tie_t << 12);
}

static unsigned
Field_imm8_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 8) | ((insn[0] << 8) >> 24);
  return tie_t;
}

static void
Field_imm8_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 24) >> 24;
  insn[0] = (insn[0] & ~0xff0000) | (tie_t << 16);
}

static unsigned
Field_s_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  return tie_t;
}

static void
Field_s_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
}

static unsigned
Field_imm12b_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  tie_t = (tie_t << 8) | ((insn[0] << 8) >> 24);
  return tie_t;
}

static void
Field_imm12b_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 24) >> 24;
  insn[0] = (insn[0] & ~0xff0000) | (tie_t << 16);
  tie_t = (val << 20) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
}

static unsigned
Field_imm16_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 16) | ((insn[0] << 8) >> 16);
  return tie_t;
}

static void
Field_imm16_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 16) >> 16;
  insn[0] = (insn[0] & ~0xffff00) | (tie_t << 8);
}

static unsigned
Field_offset_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 18) | ((insn[0] << 8) >> 14);
  return tie_t;
}

static void
Field_offset_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 14) >> 14;
  insn[0] = (insn[0] & ~0xffffc0) | (tie_t << 6);
}

static unsigned
Field_r_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_r_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_sa4_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 11) >> 31);
  return tie_t;
}

static void
Field_sa4_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x100000) | (tie_t << 20);
}

static unsigned
Field_sae4_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 15) >> 31);
  return tie_t;
}

static void
Field_sae4_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x10000) | (tie_t << 16);
}

static unsigned
Field_sae_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 15) >> 31);
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  return tie_t;
}

static void
Field_sae_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
  tie_t = (val << 27) >> 31;
  insn[0] = (insn[0] & ~0x10000) | (tie_t << 16);
}

static unsigned
Field_sal_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 11) >> 31);
  tie_t = (tie_t << 4) | ((insn[0] << 24) >> 28);
  return tie_t;
}

static void
Field_sal_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0) | (tie_t << 4);
  tie_t = (val << 27) >> 31;
  insn[0] = (insn[0] & ~0x100000) | (tie_t << 20);
}

static unsigned
Field_sargt_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 11) >> 31);
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  return tie_t;
}

static void
Field_sargt_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
  tie_t = (val << 27) >> 31;
  insn[0] = (insn[0] & ~0x100000) | (tie_t << 20);
}

static unsigned
Field_sas4_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 27) >> 31);
  return tie_t;
}

static void
Field_sas4_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x10) | (tie_t << 4);
}

static unsigned
Field_sas_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 27) >> 31);
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  return tie_t;
}

static void
Field_sas_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
  tie_t = (val << 27) >> 31;
  insn[0] = (insn[0] & ~0x10) | (tie_t << 4);
}

static unsigned
Field_sr_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  return tie_t;
}

static void
Field_sr_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
  tie_t = (val << 24) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_sr_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  return tie_t;
}

static void
Field_sr_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
  tie_t = (val << 24) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_st_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  tie_t = (tie_t << 4) | ((insn[0] << 24) >> 28);
  return tie_t;
}

static void
Field_st_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0) | (tie_t << 4);
  tie_t = (val << 24) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
}

static unsigned
Field_st_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 20) >> 28);
  tie_t = (tie_t << 4) | ((insn[0] << 24) >> 28);
  return tie_t;
}

static void
Field_st_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0) | (tie_t << 4);
  tie_t = (val << 24) >> 28;
  insn[0] = (insn[0] & ~0xf00) | (tie_t << 8);
}

static unsigned
Field_imm4_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_imm4_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_imm4_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_imm4_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_imm4_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_imm4_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_mn_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 24) >> 30);
  tie_t = (tie_t << 2) | ((insn[0] << 26) >> 30);
  return tie_t;
}

static void
Field_mn_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 30) >> 30;
  insn[0] = (insn[0] & ~0x30) | (tie_t << 4);
  tie_t = (val << 28) >> 30;
  insn[0] = (insn[0] & ~0xc0) | (tie_t << 6);
}

static unsigned
Field_i_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 24) >> 31);
  return tie_t;
}

static void
Field_i_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x80) | (tie_t << 7);
}

static unsigned
Field_imm6lo_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_imm6lo_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_imm6lo_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_imm6lo_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_imm6hi_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 26) >> 30);
  return tie_t;
}

static void
Field_imm6hi_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 30) >> 30;
  insn[0] = (insn[0] & ~0x30) | (tie_t << 4);
}

static unsigned
Field_imm6hi_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 26) >> 30);
  return tie_t;
}

static void
Field_imm6hi_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 30) >> 30;
  insn[0] = (insn[0] & ~0x30) | (tie_t << 4);
}

static unsigned
Field_imm7lo_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_imm7lo_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_imm7lo_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_imm7lo_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
}

static unsigned
Field_imm7hi_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 3) | ((insn[0] << 25) >> 29);
  return tie_t;
}

static void
Field_imm7hi_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 29) >> 29;
  insn[0] = (insn[0] & ~0x70) | (tie_t << 4);
}

static unsigned
Field_imm7hi_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 3) | ((insn[0] << 25) >> 29);
  return tie_t;
}

static void
Field_imm7hi_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 29) >> 29;
  insn[0] = (insn[0] & ~0x70) | (tie_t << 4);
}

static unsigned
Field_z_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 25) >> 31);
  return tie_t;
}

static void
Field_z_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x40) | (tie_t << 6);
}

static unsigned
Field_imm6_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 26) >> 30);
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_imm6_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
  tie_t = (val << 26) >> 30;
  insn[0] = (insn[0] & ~0x30) | (tie_t << 4);
}

static unsigned
Field_imm6_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 26) >> 30);
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_imm6_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
  tie_t = (val << 26) >> 30;
  insn[0] = (insn[0] & ~0x30) | (tie_t << 4);
}

static unsigned
Field_imm7_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 3) | ((insn[0] << 25) >> 29);
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_imm7_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
  tie_t = (val << 25) >> 29;
  insn[0] = (insn[0] & ~0x70) | (tie_t << 4);
}

static unsigned
Field_imm7_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 3) | ((insn[0] << 25) >> 29);
  tie_t = (tie_t << 4) | ((insn[0] << 16) >> 28);
  return tie_t;
}

static void
Field_imm7_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf000) | (tie_t << 12);
  tie_t = (val << 25) >> 29;
  insn[0] = (insn[0] & ~0x70) | (tie_t << 4);
}

static unsigned
Field_rbit2_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 17) >> 31);
  return tie_t;
}

static void
Field_rbit2_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x4000) | (tie_t << 14);
}

static unsigned
Field_tbit2_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 25) >> 31);
  return tie_t;
}

static void
Field_tbit2_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x40) | (tie_t << 6);
}

static unsigned
Field_y_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 25) >> 31);
  return tie_t;
}

static void
Field_y_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x40) | (tie_t << 6);
}

static unsigned
Field_x_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 17) >> 31);
  return tie_t;
}

static void
Field_x_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x4000) | (tie_t << 14);
}

static unsigned
Field_xt_wbr15_imm_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 15) | ((insn[0] << 8) >> 17);
  return tie_t;
}

static void
Field_xt_wbr15_imm_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 17) >> 17;
  insn[0] = (insn[0] & ~0xfffe00) | (tie_t << 9);
}

static unsigned
Field_xt_wbr18_imm_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 18) | ((insn[0] << 8) >> 14);
  return tie_t;
}

static void
Field_xt_wbr18_imm_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 14) >> 14;
  insn[0] = (insn[0] & ~0xffffc0) | (tie_t << 6);
}

static unsigned
Field_bitindex_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 23) >> 31);
  tie_t = (tie_t << 4) | ((insn[0] << 24) >> 28);
  return tie_t;
}

static void
Field_bitindex_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0) | (tie_t << 4);
  tie_t = (val << 27) >> 31;
  insn[0] = (insn[0] & ~0x100) | (tie_t << 8);
}

static unsigned
Field_bitindex_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 23) >> 31);
  tie_t = (tie_t << 4) | ((insn[0] << 24) >> 28);
  return tie_t;
}

static void
Field_bitindex_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0) | (tie_t << 4);
  tie_t = (val << 27) >> 31;
  insn[0] = (insn[0] & ~0x100) | (tie_t << 8);
}

static unsigned
Field_bitindex_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 23) >> 31);
  tie_t = (tie_t << 4) | ((insn[0] << 24) >> 28);
  return tie_t;
}

static void
Field_bitindex_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 28) >> 28;
  insn[0] = (insn[0] & ~0xf0) | (tie_t << 4);
  tie_t = (val << 27) >> 31;
  insn[0] = (insn[0] & ~0x100) | (tie_t << 8);
}

static unsigned
Field_s3to1_Slot_inst16a_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 3) | ((insn[0] << 20) >> 29);
  return tie_t;
}

static void
Field_s3to1_Slot_inst16a_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 29) >> 29;
  insn[0] = (insn[0] & ~0xe00) | (tie_t << 9);
}

static unsigned
Field_s3to1_Slot_inst16b_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 3) | ((insn[0] << 20) >> 29);
  return tie_t;
}

static void
Field_s3to1_Slot_inst16b_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 29) >> 29;
  insn[0] = (insn[0] & ~0xe00) | (tie_t << 9);
}

static void
Implicit_Field_set (xtensa_insnbuf insn ATTRIBUTE_UNUSED,
		    uint32 val ATTRIBUTE_UNUSED)
{
  /* Do nothing.  */
}

static unsigned
Implicit_Field_ar0_get (const xtensa_insnbuf insn ATTRIBUTE_UNUSED)
{
  return 0;
}

static unsigned
Implicit_Field_ar4_get (const xtensa_insnbuf insn ATTRIBUTE_UNUSED)
{
  return 4;
}

static unsigned
Implicit_Field_ar8_get (const xtensa_insnbuf insn ATTRIBUTE_UNUSED)
{
  return 8;
}

static unsigned
Implicit_Field_ar12_get (const xtensa_insnbuf insn ATTRIBUTE_UNUSED)
{
  return 12;
}

static unsigned
Implicit_Field_mr0_get (const xtensa_insnbuf insn ATTRIBUTE_UNUSED)
{
  return 0;
}

static unsigned
Implicit_Field_mr1_get (const xtensa_insnbuf insn ATTRIBUTE_UNUSED)
{
  return 1;
}

static unsigned
Implicit_Field_mr2_get (const xtensa_insnbuf insn ATTRIBUTE_UNUSED)
{
  return 2;
}

static unsigned
Implicit_Field_mr3_get (const xtensa_insnbuf insn ATTRIBUTE_UNUSED)
{
  return 3;
}

enum xtensa_field_id {
  FIELD_t,
  FIELD_bbi4,
  FIELD_bbi,
  FIELD_imm12,
  FIELD_imm8,
  FIELD_s,
  FIELD_imm12b,
  FIELD_imm16,
  FIELD_m,
  FIELD_n,
  FIELD_offset,
  FIELD_op0,
  FIELD_op1,
  FIELD_op2,
  FIELD_r,
  FIELD_sa4,
  FIELD_sae4,
  FIELD_sae,
  FIELD_sal,
  FIELD_sargt,
  FIELD_sas4,
  FIELD_sas,
  FIELD_sr,
  FIELD_st,
  FIELD_thi3,
  FIELD_imm4,
  FIELD_mn,
  FIELD_i,
  FIELD_imm6lo,
  FIELD_imm6hi,
  FIELD_imm7lo,
  FIELD_imm7hi,
  FIELD_z,
  FIELD_imm6,
  FIELD_imm7,
  FIELD_r3,
  FIELD_rbit2,
  FIELD_rhi,
  FIELD_t3,
  FIELD_tbit2,
  FIELD_tlo,
  FIELD_w,
  FIELD_y,
  FIELD_x,
  FIELD_xt_wbr15_imm,
  FIELD_xt_wbr18_imm,
  FIELD_bitindex,
  FIELD_s3to1,
  FIELD__ar0,
  FIELD__ar4,
  FIELD__ar8,
  FIELD__ar12,
  FIELD__mr0,
  FIELD__mr1,
  FIELD__mr2,
  FIELD__mr3
};


/* Functional units.  */

static xtensa_funcUnit_internal funcUnits[] = {

};


/* Register files.  */

enum xtensa_regfile_id {
  REGFILE_AR,
  REGFILE_MR
};

static xtensa_regfile_internal regfiles[] = {
  { "AR", "a", REGFILE_AR, 32, 32 },
  { "MR", "m", REGFILE_MR, 32, 4 }
};


/* Interfaces.  */

static xtensa_interface_internal interfaces[] = {
  { "IMPWIRE", 32, 0, 0, 'i' }
};

enum xtensa_interface_id {
  INTERFACE_IMPWIRE
};


/* Constant tables.  */

/* constant table ai4c */
static const unsigned CONST_TBL_ai4c_0[] = {
  0xffffffff,
  0x1,
  0x2,
  0x3,
  0x4,
  0x5,
  0x6,
  0x7,
  0x8,
  0x9,
  0xa,
  0xb,
  0xc,
  0xd,
  0xe,
  0xf,
  0
};

/* constant table b4c */
static const unsigned CONST_TBL_b4c_0[] = {
  0xffffffff,
  0x1,
  0x2,
  0x3,
  0x4,
  0x5,
  0x6,
  0x7,
  0x8,
  0xa,
  0xc,
  0x10,
  0x20,
  0x40,
  0x80,
  0x100,
  0
};

/* constant table b4cu */
static const unsigned CONST_TBL_b4cu_0[] = {
  0x8000,
  0x10000,
  0x2,
  0x3,
  0x4,
  0x5,
  0x6,
  0x7,
  0x8,
  0xa,
  0xc,
  0x10,
  0x20,
  0x40,
  0x80,
  0x100,
  0
};


/* Instruction operands.  */

static int
Operand_soffsetx4_decode (uint32 *valp)
{
  unsigned soffsetx4_0, offset_0;
  offset_0 = *valp & 0x3ffff;
  soffsetx4_0 = 0x4 + ((((int) offset_0 << 14) >> 14) << 2);
  *valp = soffsetx4_0;
  return 0;
}

static int
Operand_soffsetx4_encode (uint32 *valp)
{
  unsigned offset_0, soffsetx4_0;
  soffsetx4_0 = *valp;
  offset_0 = ((soffsetx4_0 - 0x4) >> 2) & 0x3ffff;
  *valp = offset_0;
  return 0;
}

static int
Operand_soffsetx4_ator (uint32 *valp, uint32 pc)
{
  *valp -= (pc & ~0x3);
  return 0;
}

static int
Operand_soffsetx4_rtoa (uint32 *valp, uint32 pc)
{
  *valp += (pc & ~0x3);
  return 0;
}

static int
Operand_uimm12x8_decode (uint32 *valp)
{
  unsigned uimm12x8_0, imm12_0;
  imm12_0 = *valp & 0xfff;
  uimm12x8_0 = imm12_0 << 3;
  *valp = uimm12x8_0;
  return 0;
}

static int
Operand_uimm12x8_encode (uint32 *valp)
{
  unsigned imm12_0, uimm12x8_0;
  uimm12x8_0 = *valp;
  imm12_0 = ((uimm12x8_0 >> 3) & 0xfff);
  *valp = imm12_0;
  return 0;
}

static int
Operand_simm4_decode (uint32 *valp)
{
  unsigned simm4_0, mn_0;
  mn_0 = *valp & 0xf;
  simm4_0 = ((int) mn_0 << 28) >> 28;
  *valp = simm4_0;
  return 0;
}

static int
Operand_simm4_encode (uint32 *valp)
{
  unsigned mn_0, simm4_0;
  simm4_0 = *valp;
  mn_0 = (simm4_0 & 0xf);
  *valp = mn_0;
  return 0;
}

static int
Operand_arr_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_arr_encode (uint32 *valp)
{
  return (*valp & ~0xf) != 0;
}

static int
Operand_ars_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_ars_encode (uint32 *valp)
{
  return (*valp & ~0xf) != 0;
}

static int
Operand_art_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_art_encode (uint32 *valp)
{
  return (*valp & ~0xf) != 0;
}

static int
Operand_ar0_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_ar0_encode (uint32 *valp)
{
  return (*valp & ~0x1f) != 0;
}

static int
Operand_ar4_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_ar4_encode (uint32 *valp)
{
  return (*valp & ~0x1f) != 0;
}

static int
Operand_ar8_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_ar8_encode (uint32 *valp)
{
  return (*valp & ~0x1f) != 0;
}

static int
Operand_ar12_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_ar12_encode (uint32 *valp)
{
  return (*valp & ~0x1f) != 0;
}

static int
Operand_ars_entry_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_ars_entry_encode (uint32 *valp)
{
  return (*valp & ~0x1f) != 0;
}

static int
Operand_immrx4_decode (uint32 *valp)
{
  unsigned immrx4_0, r_0;
  r_0 = *valp & 0xf;
  immrx4_0 = (((0xfffffff) << 4) | r_0) << 2;
  *valp = immrx4_0;
  return 0;
}

static int
Operand_immrx4_encode (uint32 *valp)
{
  unsigned r_0, immrx4_0;
  immrx4_0 = *valp;
  r_0 = ((immrx4_0 >> 2) & 0xf);
  *valp = r_0;
  return 0;
}

static int
Operand_lsi4x4_decode (uint32 *valp)
{
  unsigned lsi4x4_0, r_0;
  r_0 = *valp & 0xf;
  lsi4x4_0 = r_0 << 2;
  *valp = lsi4x4_0;
  return 0;
}

static int
Operand_lsi4x4_encode (uint32 *valp)
{
  unsigned r_0, lsi4x4_0;
  lsi4x4_0 = *valp;
  r_0 = ((lsi4x4_0 >> 2) & 0xf);
  *valp = r_0;
  return 0;
}

static int
Operand_simm7_decode (uint32 *valp)
{
  unsigned simm7_0, imm7_0;
  imm7_0 = *valp & 0x7f;
  simm7_0 = ((((-((((imm7_0 >> 6) & 1)) & (((imm7_0 >> 5) & 1)))) & 0x1ffffff)) << 7) | imm7_0;
  *valp = simm7_0;
  return 0;
}

static int
Operand_simm7_encode (uint32 *valp)
{
  unsigned imm7_0, simm7_0;
  simm7_0 = *valp;
  imm7_0 = (simm7_0 & 0x7f);
  *valp = imm7_0;
  return 0;
}

static int
Operand_uimm6_decode (uint32 *valp)
{
  unsigned uimm6_0, imm6_0;
  imm6_0 = *valp & 0x3f;
  uimm6_0 = 0x4 + (((0) << 6) | imm6_0);
  *valp = uimm6_0;
  return 0;
}

static int
Operand_uimm6_encode (uint32 *valp)
{
  unsigned imm6_0, uimm6_0;
  uimm6_0 = *valp;
  imm6_0 = (uimm6_0 - 0x4) & 0x3f;
  *valp = imm6_0;
  return 0;
}

static int
Operand_uimm6_ator (uint32 *valp, uint32 pc)
{
  *valp -= pc;
  return 0;
}

static int
Operand_uimm6_rtoa (uint32 *valp, uint32 pc)
{
  *valp += pc;
  return 0;
}

static int
Operand_ai4const_decode (uint32 *valp)
{
  unsigned ai4const_0, t_0;
  t_0 = *valp & 0xf;
  ai4const_0 = CONST_TBL_ai4c_0[t_0 & 0xf];
  *valp = ai4const_0;
  return 0;
}

static int
Operand_ai4const_encode (uint32 *valp)
{
  unsigned t_0, ai4const_0;
  ai4const_0 = *valp;
  switch (ai4const_0)
    {
    case 0xffffffff: t_0 = 0; break;
    case 0x1: t_0 = 0x1; break;
    case 0x2: t_0 = 0x2; break;
    case 0x3: t_0 = 0x3; break;
    case 0x4: t_0 = 0x4; break;
    case 0x5: t_0 = 0x5; break;
    case 0x6: t_0 = 0x6; break;
    case 0x7: t_0 = 0x7; break;
    case 0x8: t_0 = 0x8; break;
    case 0x9: t_0 = 0x9; break;
    case 0xa: t_0 = 0xa; break;
    case 0xb: t_0 = 0xb; break;
    case 0xc: t_0 = 0xc; break;
    case 0xd: t_0 = 0xd; break;
    case 0xe: t_0 = 0xe; break;
    default: t_0 = 0xf; break;
    }
  *valp = t_0;
  return 0;
}

static int
Operand_b4const_decode (uint32 *valp)
{
  unsigned b4const_0, r_0;
  r_0 = *valp & 0xf;
  b4const_0 = CONST_TBL_b4c_0[r_0 & 0xf];
  *valp = b4const_0;
  return 0;
}

static int
Operand_b4const_encode (uint32 *valp)
{
  unsigned r_0, b4const_0;
  b4const_0 = *valp;
  switch (b4const_0)
    {
    case 0xffffffff: r_0 = 0; break;
    case 0x1: r_0 = 0x1; break;
    case 0x2: r_0 = 0x2; break;
    case 0x3: r_0 = 0x3; break;
    case 0x4: r_0 = 0x4; break;
    case 0x5: r_0 = 0x5; break;
    case 0x6: r_0 = 0x6; break;
    case 0x7: r_0 = 0x7; break;
    case 0x8: r_0 = 0x8; break;
    case 0xa: r_0 = 0x9; break;
    case 0xc: r_0 = 0xa; break;
    case 0x10: r_0 = 0xb; break;
    case 0x20: r_0 = 0xc; break;
    case 0x40: r_0 = 0xd; break;
    case 0x80: r_0 = 0xe; break;
    default: r_0 = 0xf; break;
    }
  *valp = r_0;
  return 0;
}

static int
Operand_b4constu_decode (uint32 *valp)
{
  unsigned b4constu_0, r_0;
  r_0 = *valp & 0xf;
  b4constu_0 = CONST_TBL_b4cu_0[r_0 & 0xf];
  *valp = b4constu_0;
  return 0;
}

static int
Operand_b4constu_encode (uint32 *valp)
{
  unsigned r_0, b4constu_0;
  b4constu_0 = *valp;
  switch (b4constu_0)
    {
    case 0x8000: r_0 = 0; break;
    case 0x10000: r_0 = 0x1; break;
    case 0x2: r_0 = 0x2; break;
    case 0x3: r_0 = 0x3; break;
    case 0x4: r_0 = 0x4; break;
    case 0x5: r_0 = 0x5; break;
    case 0x6: r_0 = 0x6; break;
    case 0x7: r_0 = 0x7; break;
    case 0x8: r_0 = 0x8; break;
    case 0xa: r_0 = 0x9; break;
    case 0xc: r_0 = 0xa; break;
    case 0x10: r_0 = 0xb; break;
    case 0x20: r_0 = 0xc; break;
    case 0x40: r_0 = 0xd; break;
    case 0x80: r_0 = 0xe; break;
    default: r_0 = 0xf; break;
    }
  *valp = r_0;
  return 0;
}

static int
Operand_uimm8_decode (uint32 *valp)
{
  unsigned uimm8_0, imm8_0;
  imm8_0 = *valp & 0xff;
  uimm8_0 = imm8_0;
  *valp = uimm8_0;
  return 0;
}

static int
Operand_uimm8_encode (uint32 *valp)
{
  unsigned imm8_0, uimm8_0;
  uimm8_0 = *valp;
  imm8_0 = (uimm8_0 & 0xff);
  *valp = imm8_0;
  return 0;
}

static int
Operand_uimm8x2_decode (uint32 *valp)
{
  unsigned uimm8x2_0, imm8_0;
  imm8_0 = *valp & 0xff;
  uimm8x2_0 = imm8_0 << 1;
  *valp = uimm8x2_0;
  return 0;
}

static int
Operand_uimm8x2_encode (uint32 *valp)
{
  unsigned imm8_0, uimm8x2_0;
  uimm8x2_0 = *valp;
  imm8_0 = ((uimm8x2_0 >> 1) & 0xff);
  *valp = imm8_0;
  return 0;
}

static int
Operand_uimm8x4_decode (uint32 *valp)
{
  unsigned uimm8x4_0, imm8_0;
  imm8_0 = *valp & 0xff;
  uimm8x4_0 = imm8_0 << 2;
  *valp = uimm8x4_0;
  return 0;
}

static int
Operand_uimm8x4_encode (uint32 *valp)
{
  unsigned imm8_0, uimm8x4_0;
  uimm8x4_0 = *valp;
  imm8_0 = ((uimm8x4_0 >> 2) & 0xff);
  *valp = imm8_0;
  return 0;
}

static int
Operand_uimm4x16_decode (uint32 *valp)
{
  unsigned uimm4x16_0, op2_0;
  op2_0 = *valp & 0xf;
  uimm4x16_0 = op2_0 << 4;
  *valp = uimm4x16_0;
  return 0;
}

static int
Operand_uimm4x16_encode (uint32 *valp)
{
  unsigned op2_0, uimm4x16_0;
  uimm4x16_0 = *valp;
  op2_0 = ((uimm4x16_0 >> 4) & 0xf);
  *valp = op2_0;
  return 0;
}

static int
Operand_simm8_decode (uint32 *valp)
{
  unsigned simm8_0, imm8_0;
  imm8_0 = *valp & 0xff;
  simm8_0 = ((int) imm8_0 << 24) >> 24;
  *valp = simm8_0;
  return 0;
}

static int
Operand_simm8_encode (uint32 *valp)
{
  unsigned imm8_0, simm8_0;
  simm8_0 = *valp;
  imm8_0 = (simm8_0 & 0xff);
  *valp = imm8_0;
  return 0;
}

static int
Operand_simm8x256_decode (uint32 *valp)
{
  unsigned simm8x256_0, imm8_0;
  imm8_0 = *valp & 0xff;
  simm8x256_0 = (((int) imm8_0 << 24) >> 24) << 8;
  *valp = simm8x256_0;
  return 0;
}

static int
Operand_simm8x256_encode (uint32 *valp)
{
  unsigned imm8_0, simm8x256_0;
  simm8x256_0 = *valp;
  imm8_0 = ((simm8x256_0 >> 8) & 0xff);
  *valp = imm8_0;
  return 0;
}

static int
Operand_simm12b_decode (uint32 *valp)
{
  unsigned simm12b_0, imm12b_0;
  imm12b_0 = *valp & 0xfff;
  simm12b_0 = ((int) imm12b_0 << 20) >> 20;
  *valp = simm12b_0;
  return 0;
}

static int
Operand_simm12b_encode (uint32 *valp)
{
  unsigned imm12b_0, simm12b_0;
  simm12b_0 = *valp;
  imm12b_0 = (simm12b_0 & 0xfff);
  *valp = imm12b_0;
  return 0;
}

static int
Operand_msalp32_decode (uint32 *valp)
{
  unsigned msalp32_0, sal_0;
  sal_0 = *valp & 0x1f;
  msalp32_0 = 0x20 - sal_0;
  *valp = msalp32_0;
  return 0;
}

static int
Operand_msalp32_encode (uint32 *valp)
{
  unsigned sal_0, msalp32_0;
  msalp32_0 = *valp;
  sal_0 = (0x20 - msalp32_0) & 0x1f;
  *valp = sal_0;
  return 0;
}

static int
Operand_op2p1_decode (uint32 *valp)
{
  unsigned op2p1_0, op2_0;
  op2_0 = *valp & 0xf;
  op2p1_0 = op2_0 + 0x1;
  *valp = op2p1_0;
  return 0;
}

static int
Operand_op2p1_encode (uint32 *valp)
{
  unsigned op2_0, op2p1_0;
  op2p1_0 = *valp;
  op2_0 = (op2p1_0 - 0x1) & 0xf;
  *valp = op2_0;
  return 0;
}

static int
Operand_label8_decode (uint32 *valp)
{
  unsigned label8_0, imm8_0;
  imm8_0 = *valp & 0xff;
  label8_0 = 0x4 + (((int) imm8_0 << 24) >> 24);
  *valp = label8_0;
  return 0;
}

static int
Operand_label8_encode (uint32 *valp)
{
  unsigned imm8_0, label8_0;
  label8_0 = *valp;
  imm8_0 = (label8_0 - 0x4) & 0xff;
  *valp = imm8_0;
  return 0;
}

static int
Operand_label8_ator (uint32 *valp, uint32 pc)
{
  *valp -= pc;
  return 0;
}

static int
Operand_label8_rtoa (uint32 *valp, uint32 pc)
{
  *valp += pc;
  return 0;
}

static int
Operand_ulabel8_decode (uint32 *valp)
{
  unsigned ulabel8_0, imm8_0;
  imm8_0 = *valp & 0xff;
  ulabel8_0 = 0x4 + (((0) << 8) | imm8_0);
  *valp = ulabel8_0;
  return 0;
}

static int
Operand_ulabel8_encode (uint32 *valp)
{
  unsigned imm8_0, ulabel8_0;
  ulabel8_0 = *valp;
  imm8_0 = (ulabel8_0 - 0x4) & 0xff;
  *valp = imm8_0;
  return 0;
}

static int
Operand_ulabel8_ator (uint32 *valp, uint32 pc)
{
  *valp -= pc;
  return 0;
}

static int
Operand_ulabel8_rtoa (uint32 *valp, uint32 pc)
{
  *valp += pc;
  return 0;
}

static int
Operand_label12_decode (uint32 *valp)
{
  unsigned label12_0, imm12_0;
  imm12_0 = *valp & 0xfff;
  label12_0 = 0x4 + (((int) imm12_0 << 20) >> 20);
  *valp = label12_0;
  return 0;
}

static int
Operand_label12_encode (uint32 *valp)
{
  unsigned imm12_0, label12_0;
  label12_0 = *valp;
  imm12_0 = (label12_0 - 0x4) & 0xfff;
  *valp = imm12_0;
  return 0;
}

static int
Operand_label12_ator (uint32 *valp, uint32 pc)
{
  *valp -= pc;
  return 0;
}

static int
Operand_label12_rtoa (uint32 *valp, uint32 pc)
{
  *valp += pc;
  return 0;
}

static int
Operand_soffset_decode (uint32 *valp)
{
  unsigned soffset_0, offset_0;
  offset_0 = *valp & 0x3ffff;
  soffset_0 = 0x4 + (((int) offset_0 << 14) >> 14);
  *valp = soffset_0;
  return 0;
}

static int
Operand_soffset_encode (uint32 *valp)
{
  unsigned offset_0, soffset_0;
  soffset_0 = *valp;
  offset_0 = (soffset_0 - 0x4) & 0x3ffff;
  *valp = offset_0;
  return 0;
}

static int
Operand_soffset_ator (uint32 *valp, uint32 pc)
{
  *valp -= pc;
  return 0;
}

static int
Operand_soffset_rtoa (uint32 *valp, uint32 pc)
{
  *valp += pc;
  return 0;
}

static int
Operand_uimm16x4_decode (uint32 *valp)
{
  unsigned uimm16x4_0, imm16_0;
  imm16_0 = *valp & 0xffff;
  uimm16x4_0 = (((0xffff) << 16) | imm16_0) << 2;
  *valp = uimm16x4_0;
  return 0;
}

static int
Operand_uimm16x4_encode (uint32 *valp)
{
  unsigned imm16_0, uimm16x4_0;
  uimm16x4_0 = *valp;
  imm16_0 = (uimm16x4_0 >> 2) & 0xffff;
  *valp = imm16_0;
  return 0;
}

static int
Operand_uimm16x4_ator (uint32 *valp, uint32 pc)
{
  *valp -= ((pc + 3) & ~0x3);
  return 0;
}

static int
Operand_uimm16x4_rtoa (uint32 *valp, uint32 pc)
{
  *valp += ((pc + 3) & ~0x3);
  return 0;
}

static int
Operand_mx_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_mx_encode (uint32 *valp)
{
  return (*valp & ~0x3) != 0;
}

static int
Operand_my_decode (uint32 *valp)
{
  *valp += 2;
  return 0;
}

static int
Operand_my_encode (uint32 *valp)
{
  int error;
  error = ((*valp & ~0x3) != 0) || ((*valp & 0x2) == 0);
  *valp = *valp & 1;
  return error;
}

static int
Operand_mw_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_mw_encode (uint32 *valp)
{
  return (*valp & ~0x3) != 0;
}

static int
Operand_mr0_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_mr0_encode (uint32 *valp)
{
  return (*valp & ~0x3) != 0;
}

static int
Operand_mr1_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_mr1_encode (uint32 *valp)
{
  return (*valp & ~0x3) != 0;
}

static int
Operand_mr2_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_mr2_encode (uint32 *valp)
{
  return (*valp & ~0x3) != 0;
}

static int
Operand_mr3_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
Operand_mr3_encode (uint32 *valp)
{
  return (*valp & ~0x3) != 0;
}

static int
Operand_immt_decode (uint32 *valp)
{
  unsigned immt_0, t_0;
  t_0 = *valp & 0xf;
  immt_0 = t_0;
  *valp = immt_0;
  return 0;
}

static int
Operand_immt_encode (uint32 *valp)
{
  unsigned t_0, immt_0;
  immt_0 = *valp;
  t_0 = immt_0 & 0xf;
  *valp = t_0;
  return 0;
}

static int
Operand_imms_decode (uint32 *valp)
{
  unsigned imms_0, s_0;
  s_0 = *valp & 0xf;
  imms_0 = s_0;
  *valp = imms_0;
  return 0;
}

static int
Operand_imms_encode (uint32 *valp)
{
  unsigned s_0, imms_0;
  imms_0 = *valp;
  s_0 = imms_0 & 0xf;
  *valp = s_0;
  return 0;
}

static int
Operand_tp7_decode (uint32 *valp)
{
  unsigned tp7_0, t_0;
  t_0 = *valp & 0xf;
  tp7_0 = t_0 + 0x7;
  *valp = tp7_0;
  return 0;
}

static int
Operand_tp7_encode (uint32 *valp)
{
  unsigned t_0, tp7_0;
  tp7_0 = *valp;
  t_0 = (tp7_0 - 0x7) & 0xf;
  *valp = t_0;
  return 0;
}

static int
Operand_xt_wbr15_label_decode (uint32 *valp)
{
  unsigned xt_wbr15_label_0, xt_wbr15_imm_0;
  xt_wbr15_imm_0 = *valp & 0x7fff;
  xt_wbr15_label_0 = 0x4 + (((int) xt_wbr15_imm_0 << 17) >> 17);
  *valp = xt_wbr15_label_0;
  return 0;
}

static int
Operand_xt_wbr15_label_encode (uint32 *valp)
{
  unsigned xt_wbr15_imm_0, xt_wbr15_label_0;
  xt_wbr15_label_0 = *valp;
  xt_wbr15_imm_0 = (xt_wbr15_label_0 - 0x4) & 0x7fff;
  *valp = xt_wbr15_imm_0;
  return 0;
}

static int
Operand_xt_wbr15_label_ator (uint32 *valp, uint32 pc)
{
  *valp -= pc;
  return 0;
}

static int
Operand_xt_wbr15_label_rtoa (uint32 *valp, uint32 pc)
{
  *valp += pc;
  return 0;
}

static int
Operand_xt_wbr18_label_decode (uint32 *valp)
{
  unsigned xt_wbr18_label_0, xt_wbr18_imm_0;
  xt_wbr18_imm_0 = *valp & 0x3ffff;
  xt_wbr18_label_0 = 0x4 + (((int) xt_wbr18_imm_0 << 14) >> 14);
  *valp = xt_wbr18_label_0;
  return 0;
}

static int
Operand_xt_wbr18_label_encode (uint32 *valp)
{
  unsigned xt_wbr18_imm_0, xt_wbr18_label_0;
  xt_wbr18_label_0 = *valp;
  xt_wbr18_imm_0 = (xt_wbr18_label_0 - 0x4) & 0x3ffff;
  *valp = xt_wbr18_imm_0;
  return 0;
}

static int
Operand_xt_wbr18_label_ator (uint32 *valp, uint32 pc)
{
  *valp -= pc;
  return 0;
}

static int
Operand_xt_wbr18_label_rtoa (uint32 *valp, uint32 pc)
{
  *valp += pc;
  return 0;
}

static xtensa_operand_internal operands[] = {
  { "soffsetx4", FIELD_offset, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    Operand_soffsetx4_encode, Operand_soffsetx4_decode,
    Operand_soffsetx4_ator, Operand_soffsetx4_rtoa },
  { "uimm12x8", FIELD_imm12, -1, 0,
    0,
    Operand_uimm12x8_encode, Operand_uimm12x8_decode,
    0, 0 },
  { "simm4", FIELD_mn, -1, 0,
    0,
    Operand_simm4_encode, Operand_simm4_decode,
    0, 0 },
  { "arr", FIELD_r, REGFILE_AR, 1,
    XTENSA_OPERAND_IS_REGISTER,
    Operand_arr_encode, Operand_arr_decode,
    0, 0 },
  { "ars", FIELD_s, REGFILE_AR, 1,
    XTENSA_OPERAND_IS_REGISTER,
    Operand_ars_encode, Operand_ars_decode,
    0, 0 },
  { "*ars_invisible", FIELD_s, REGFILE_AR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    Operand_ars_encode, Operand_ars_decode,
    0, 0 },
  { "art", FIELD_t, REGFILE_AR, 1,
    XTENSA_OPERAND_IS_REGISTER,
    Operand_art_encode, Operand_art_decode,
    0, 0 },
  { "ar0", FIELD__ar0, REGFILE_AR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    Operand_ar0_encode, Operand_ar0_decode,
    0, 0 },
  { "ar4", FIELD__ar4, REGFILE_AR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    Operand_ar4_encode, Operand_ar4_decode,
    0, 0 },
  { "ar8", FIELD__ar8, REGFILE_AR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    Operand_ar8_encode, Operand_ar8_decode,
    0, 0 },
  { "ar12", FIELD__ar12, REGFILE_AR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    Operand_ar12_encode, Operand_ar12_decode,
    0, 0 },
  { "ars_entry", FIELD_s, REGFILE_AR, 1,
    XTENSA_OPERAND_IS_REGISTER,
    Operand_ars_entry_encode, Operand_ars_entry_decode,
    0, 0 },
  { "immrx4", FIELD_r, -1, 0,
    0,
    Operand_immrx4_encode, Operand_immrx4_decode,
    0, 0 },
  { "lsi4x4", FIELD_r, -1, 0,
    0,
    Operand_lsi4x4_encode, Operand_lsi4x4_decode,
    0, 0 },
  { "simm7", FIELD_imm7, -1, 0,
    0,
    Operand_simm7_encode, Operand_simm7_decode,
    0, 0 },
  { "uimm6", FIELD_imm6, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    Operand_uimm6_encode, Operand_uimm6_decode,
    Operand_uimm6_ator, Operand_uimm6_rtoa },
  { "ai4const", FIELD_t, -1, 0,
    0,
    Operand_ai4const_encode, Operand_ai4const_decode,
    0, 0 },
  { "b4const", FIELD_r, -1, 0,
    0,
    Operand_b4const_encode, Operand_b4const_decode,
    0, 0 },
  { "b4constu", FIELD_r, -1, 0,
    0,
    Operand_b4constu_encode, Operand_b4constu_decode,
    0, 0 },
  { "uimm8", FIELD_imm8, -1, 0,
    0,
    Operand_uimm8_encode, Operand_uimm8_decode,
    0, 0 },
  { "uimm8x2", FIELD_imm8, -1, 0,
    0,
    Operand_uimm8x2_encode, Operand_uimm8x2_decode,
    0, 0 },
  { "uimm8x4", FIELD_imm8, -1, 0,
    0,
    Operand_uimm8x4_encode, Operand_uimm8x4_decode,
    0, 0 },
  { "uimm4x16", FIELD_op2, -1, 0,
    0,
    Operand_uimm4x16_encode, Operand_uimm4x16_decode,
    0, 0 },
  { "simm8", FIELD_imm8, -1, 0,
    0,
    Operand_simm8_encode, Operand_simm8_decode,
    0, 0 },
  { "simm8x256", FIELD_imm8, -1, 0,
    0,
    Operand_simm8x256_encode, Operand_simm8x256_decode,
    0, 0 },
  { "simm12b", FIELD_imm12b, -1, 0,
    0,
    Operand_simm12b_encode, Operand_simm12b_decode,
    0, 0 },
  { "msalp32", FIELD_sal, -1, 0,
    0,
    Operand_msalp32_encode, Operand_msalp32_decode,
    0, 0 },
  { "op2p1", FIELD_op2, -1, 0,
    0,
    Operand_op2p1_encode, Operand_op2p1_decode,
    0, 0 },
  { "label8", FIELD_imm8, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    Operand_label8_encode, Operand_label8_decode,
    Operand_label8_ator, Operand_label8_rtoa },
  { "ulabel8", FIELD_imm8, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    Operand_ulabel8_encode, Operand_ulabel8_decode,
    Operand_ulabel8_ator, Operand_ulabel8_rtoa },
  { "label12", FIELD_imm12, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    Operand_label12_encode, Operand_label12_decode,
    Operand_label12_ator, Operand_label12_rtoa },
  { "soffset", FIELD_offset, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    Operand_soffset_encode, Operand_soffset_decode,
    Operand_soffset_ator, Operand_soffset_rtoa },
  { "uimm16x4", FIELD_imm16, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    Operand_uimm16x4_encode, Operand_uimm16x4_decode,
    Operand_uimm16x4_ator, Operand_uimm16x4_rtoa },
  { "mx", FIELD_x, REGFILE_MR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_UNKNOWN,
    Operand_mx_encode, Operand_mx_decode,
    0, 0 },
  { "my", FIELD_y, REGFILE_MR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_UNKNOWN,
    Operand_my_encode, Operand_my_decode,
    0, 0 },
  { "mw", FIELD_w, REGFILE_MR, 1,
    XTENSA_OPERAND_IS_REGISTER,
    Operand_mw_encode, Operand_mw_decode,
    0, 0 },
  { "mr0", FIELD__mr0, REGFILE_MR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    Operand_mr0_encode, Operand_mr0_decode,
    0, 0 },
  { "mr1", FIELD__mr1, REGFILE_MR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    Operand_mr1_encode, Operand_mr1_decode,
    0, 0 },
  { "mr2", FIELD__mr2, REGFILE_MR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    Operand_mr2_encode, Operand_mr2_decode,
    0, 0 },
  { "mr3", FIELD__mr3, REGFILE_MR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    Operand_mr3_encode, Operand_mr3_decode,
    0, 0 },
  { "immt", FIELD_t, -1, 0,
    0,
    Operand_immt_encode, Operand_immt_decode,
    0, 0 },
  { "imms", FIELD_s, -1, 0,
    0,
    Operand_imms_encode, Operand_imms_decode,
    0, 0 },
  { "tp7", FIELD_t, -1, 0,
    0,
    Operand_tp7_encode, Operand_tp7_decode,
    0, 0 },
  { "xt_wbr15_label", FIELD_xt_wbr15_imm, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    Operand_xt_wbr15_label_encode, Operand_xt_wbr15_label_decode,
    Operand_xt_wbr15_label_ator, Operand_xt_wbr15_label_rtoa },
  { "xt_wbr18_label", FIELD_xt_wbr18_imm, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    Operand_xt_wbr18_label_encode, Operand_xt_wbr18_label_decode,
    Operand_xt_wbr18_label_ator, Operand_xt_wbr18_label_rtoa },
  { "t", FIELD_t, -1, 0, 0, 0, 0, 0, 0 },
  { "bbi4", FIELD_bbi4, -1, 0, 0, 0, 0, 0, 0 },
  { "bbi", FIELD_bbi, -1, 0, 0, 0, 0, 0, 0 },
  { "imm12", FIELD_imm12, -1, 0, 0, 0, 0, 0, 0 },
  { "imm8", FIELD_imm8, -1, 0, 0, 0, 0, 0, 0 },
  { "s", FIELD_s, -1, 0, 0, 0, 0, 0, 0 },
  { "imm12b", FIELD_imm12b, -1, 0, 0, 0, 0, 0, 0 },
  { "imm16", FIELD_imm16, -1, 0, 0, 0, 0, 0, 0 },
  { "m", FIELD_m, -1, 0, 0, 0, 0, 0, 0 },
  { "n", FIELD_n, -1, 0, 0, 0, 0, 0, 0 },
  { "offset", FIELD_offset, -1, 0, 0, 0, 0, 0, 0 },
  { "op0", FIELD_op0, -1, 0, 0, 0, 0, 0, 0 },
  { "op1", FIELD_op1, -1, 0, 0, 0, 0, 0, 0 },
  { "op2", FIELD_op2, -1, 0, 0, 0, 0, 0, 0 },
  { "r", FIELD_r, -1, 0, 0, 0, 0, 0, 0 },
  { "sa4", FIELD_sa4, -1, 0, 0, 0, 0, 0, 0 },
  { "sae4", FIELD_sae4, -1, 0, 0, 0, 0, 0, 0 },
  { "sae", FIELD_sae, -1, 0, 0, 0, 0, 0, 0 },
  { "sal", FIELD_sal, -1, 0, 0, 0, 0, 0, 0 },
  { "sargt", FIELD_sargt, -1, 0, 0, 0, 0, 0, 0 },
  { "sas4", FIELD_sas4, -1, 0, 0, 0, 0, 0, 0 },
  { "sas", FIELD_sas, -1, 0, 0, 0, 0, 0, 0 },
  { "sr", FIELD_sr, -1, 0, 0, 0, 0, 0, 0 },
  { "st", FIELD_st, -1, 0, 0, 0, 0, 0, 0 },
  { "thi3", FIELD_thi3, -1, 0, 0, 0, 0, 0, 0 },
  { "imm4", FIELD_imm4, -1, 0, 0, 0, 0, 0, 0 },
  { "mn", FIELD_mn, -1, 0, 0, 0, 0, 0, 0 },
  { "i", FIELD_i, -1, 0, 0, 0, 0, 0, 0 },
  { "imm6lo", FIELD_imm6lo, -1, 0, 0, 0, 0, 0, 0 },
  { "imm6hi", FIELD_imm6hi, -1, 0, 0, 0, 0, 0, 0 },
  { "imm7lo", FIELD_imm7lo, -1, 0, 0, 0, 0, 0, 0 },
  { "imm7hi", FIELD_imm7hi, -1, 0, 0, 0, 0, 0, 0 },
  { "z", FIELD_z, -1, 0, 0, 0, 0, 0, 0 },
  { "imm6", FIELD_imm6, -1, 0, 0, 0, 0, 0, 0 },
  { "imm7", FIELD_imm7, -1, 0, 0, 0, 0, 0, 0 },
  { "r3", FIELD_r3, -1, 0, 0, 0, 0, 0, 0 },
  { "rbit2", FIELD_rbit2, -1, 0, 0, 0, 0, 0, 0 },
  { "rhi", FIELD_rhi, -1, 0, 0, 0, 0, 0, 0 },
  { "t3", FIELD_t3, -1, 0, 0, 0, 0, 0, 0 },
  { "tbit2", FIELD_tbit2, -1, 0, 0, 0, 0, 0, 0 },
  { "tlo", FIELD_tlo, -1, 0, 0, 0, 0, 0, 0 },
  { "w", FIELD_w, -1, 0, 0, 0, 0, 0, 0 },
  { "y", FIELD_y, -1, 0, 0, 0, 0, 0, 0 },
  { "x", FIELD_x, -1, 0, 0, 0, 0, 0, 0 },
  { "xt_wbr15_imm", FIELD_xt_wbr15_imm, -1, 0, 0, 0, 0, 0, 0 },
  { "xt_wbr18_imm", FIELD_xt_wbr18_imm, -1, 0, 0, 0, 0, 0, 0 },
  { "bitindex", FIELD_bitindex, -1, 0, 0, 0, 0, 0, 0 },
  { "s3to1", FIELD_s3to1, -1, 0, 0, 0, 0, 0, 0 }
};

enum xtensa_operand_id {
  OPERAND_soffsetx4,
  OPERAND_uimm12x8,
  OPERAND_simm4,
  OPERAND_arr,
  OPERAND_ars,
  OPERAND__ars_invisible,
  OPERAND_art,
  OPERAND_ar0,
  OPERAND_ar4,
  OPERAND_ar8,
  OPERAND_ar12,
  OPERAND_ars_entry,
  OPERAND_immrx4,
  OPERAND_lsi4x4,
  OPERAND_simm7,
  OPERAND_uimm6,
  OPERAND_ai4const,
  OPERAND_b4const,
  OPERAND_b4constu,
  OPERAND_uimm8,
  OPERAND_uimm8x2,
  OPERAND_uimm8x4,
  OPERAND_uimm4x16,
  OPERAND_simm8,
  OPERAND_simm8x256,
  OPERAND_simm12b,
  OPERAND_msalp32,
  OPERAND_op2p1,
  OPERAND_label8,
  OPERAND_ulabel8,
  OPERAND_label12,
  OPERAND_soffset,
  OPERAND_uimm16x4,
  OPERAND_mx,
  OPERAND_my,
  OPERAND_mw,
  OPERAND_mr0,
  OPERAND_mr1,
  OPERAND_mr2,
  OPERAND_mr3,
  OPERAND_immt,
  OPERAND_imms,
  OPERAND_tp7,
  OPERAND_xt_wbr15_label,
  OPERAND_xt_wbr18_label,
  OPERAND_t,
  OPERAND_bbi4,
  OPERAND_bbi,
  OPERAND_imm12,
  OPERAND_imm8,
  OPERAND_s,
  OPERAND_imm12b,
  OPERAND_imm16,
  OPERAND_m,
  OPERAND_n,
  OPERAND_offset,
  OPERAND_op0,
  OPERAND_op1,
  OPERAND_op2,
  OPERAND_r,
  OPERAND_sa4,
  OPERAND_sae4,
  OPERAND_sae,
  OPERAND_sal,
  OPERAND_sargt,
  OPERAND_sas4,
  OPERAND_sas,
  OPERAND_sr,
  OPERAND_st,
  OPERAND_thi3,
  OPERAND_imm4,
  OPERAND_mn,
  OPERAND_i,
  OPERAND_imm6lo,
  OPERAND_imm6hi,
  OPERAND_imm7lo,
  OPERAND_imm7hi,
  OPERAND_z,
  OPERAND_imm6,
  OPERAND_imm7,
  OPERAND_r3,
  OPERAND_rbit2,
  OPERAND_rhi,
  OPERAND_t3,
  OPERAND_tbit2,
  OPERAND_tlo,
  OPERAND_w,
  OPERAND_y,
  OPERAND_x,
  OPERAND_xt_wbr15_imm,
  OPERAND_xt_wbr18_imm,
  OPERAND_bitindex,
  OPERAND_s3to1
};


/* Iclass table.  */

static xtensa_arg_internal Iclass_xt_iclass_rfe_stateArgs[] = {
  { { STATE_PSRING }, 'i' },
  { { STATE_PSEXCM }, 'm' },
  { { STATE_EPC1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rfde_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DEPC }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_call12_args[] = {
  { { OPERAND_soffsetx4 }, 'i' },
  { { OPERAND_ar12 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_call12_stateArgs[] = {
  { { STATE_PSCALLINC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_call8_args[] = {
  { { OPERAND_soffsetx4 }, 'i' },
  { { OPERAND_ar8 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_call8_stateArgs[] = {
  { { STATE_PSCALLINC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_call4_args[] = {
  { { OPERAND_soffsetx4 }, 'i' },
  { { OPERAND_ar4 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_call4_stateArgs[] = {
  { { STATE_PSCALLINC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_callx12_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_ar12 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_callx12_stateArgs[] = {
  { { STATE_PSCALLINC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_callx8_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_ar8 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_callx8_stateArgs[] = {
  { { STATE_PSCALLINC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_callx4_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_ar4 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_callx4_stateArgs[] = {
  { { STATE_PSCALLINC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_entry_args[] = {
  { { OPERAND_ars_entry }, 's' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm12x8 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_entry_stateArgs[] = {
  { { STATE_PSCALLINC }, 'i' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSWOE }, 'i' },
  { { STATE_WindowBase }, 'm' },
  { { STATE_WindowStart }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_movsp_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_movsp_stateArgs[] = {
  { { STATE_WindowBase }, 'i' },
  { { STATE_WindowStart }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rotw_args[] = {
  { { OPERAND_simm4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rotw_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_WindowBase }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_retw_args[] = {
  { { OPERAND__ars_invisible }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_retw_stateArgs[] = {
  { { STATE_WindowBase }, 'm' },
  { { STATE_WindowStart }, 'm' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSWOE }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rfwou_stateArgs[] = {
  { { STATE_EPC1 }, 'i' },
  { { STATE_PSEXCM }, 'm' },
  { { STATE_PSRING }, 'i' },
  { { STATE_WindowBase }, 'm' },
  { { STATE_WindowStart }, 'm' },
  { { STATE_PSOWB }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_l32e_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_immrx4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_l32e_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_s32e_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_immrx4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_s32e_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_windowbase_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_windowbase_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_WindowBase }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_windowbase_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_windowbase_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_WindowBase }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_windowbase_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_windowbase_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_WindowBase }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_windowstart_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_windowstart_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_WindowStart }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_windowstart_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_windowstart_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_WindowStart }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_windowstart_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_windowstart_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_WindowStart }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_add_n_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_addi_n_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_ai4const }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_bz6_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_loadi4_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_lsi4x4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_mov_n_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_movi_n_args[] = {
  { { OPERAND_ars }, 'o' },
  { { OPERAND_simm7 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_retn_args[] = {
  { { OPERAND__ars_invisible }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_storei4_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_lsi4x4 }, 'i' }
};

static xtensa_arg_internal Iclass_rur_threadptr_args[] = {
  { { OPERAND_arr }, 'o' }
};

static xtensa_arg_internal Iclass_rur_threadptr_stateArgs[] = {
  { { STATE_THREADPTR }, 'i' }
};

static xtensa_arg_internal Iclass_wur_threadptr_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_wur_threadptr_stateArgs[] = {
  { { STATE_THREADPTR }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_addi_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_simm8 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_addmi_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_simm8x256 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_addsub_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_bit_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_bsi8_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_b4const }, 'i' },
  { { OPERAND_label8 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_bsi8b_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_bbi }, 'i' },
  { { OPERAND_label8 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_bsi8u_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_b4constu }, 'i' },
  { { OPERAND_label8 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_bst8_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_art }, 'i' },
  { { OPERAND_label8 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_bsz12_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_label12 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_call0_args[] = {
  { { OPERAND_soffsetx4 }, 'i' },
  { { OPERAND_ar0 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_callx0_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_ar0 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_exti_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_art }, 'i' },
  { { OPERAND_sae }, 'i' },
  { { OPERAND_op2p1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_jump_args[] = {
  { { OPERAND_soffset }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_jumpx_args[] = {
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_l16ui_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm8x2 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_l16si_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm8x2 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_l32i_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm8x4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_l32r_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_uimm16x4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_l32r_stateArgs[] = {
  { { STATE_LITBADDR }, 'i' },
  { { STATE_LITBEN }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_l8i_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm8 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_loop_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_ulabel8 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_loop_stateArgs[] = {
  { { STATE_LBEG }, 'o' },
  { { STATE_LEND }, 'o' },
  { { STATE_LCOUNT }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_loopz_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_ulabel8 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_loopz_stateArgs[] = {
  { { STATE_LBEG }, 'o' },
  { { STATE_LEND }, 'o' },
  { { STATE_LCOUNT }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_movi_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_simm12b }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_movz_args[] = {
  { { OPERAND_arr }, 'm' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_neg_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_return_args[] = {
  { { OPERAND__ars_invisible }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_s16i_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm8x2 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_s32i_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm8x4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_s8i_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm8 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_sar_args[] = {
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_sar_stateArgs[] = {
  { { STATE_SAR }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_sari_args[] = {
  { { OPERAND_sas }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_sari_stateArgs[] = {
  { { STATE_SAR }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_shifts_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_shifts_stateArgs[] = {
  { { STATE_SAR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_shiftst_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_shiftst_stateArgs[] = {
  { { STATE_SAR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_shiftt_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_shiftt_stateArgs[] = {
  { { STATE_SAR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_slli_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_msalp32 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_srai_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_art }, 'i' },
  { { OPERAND_sargt }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_srli_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_art }, 'i' },
  { { OPERAND_s }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_sync_stateArgs[] = {
  { { STATE_XTSYNC }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsil_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_s }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsil_stateArgs[] = {
  { { STATE_PSWOE }, 'i' },
  { { STATE_PSCALLINC }, 'i' },
  { { STATE_PSOWB }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_PSUM }, 'i' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSINTLEVEL }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_lend_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_lend_stateArgs[] = {
  { { STATE_LEND }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_lend_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_lend_stateArgs[] = {
  { { STATE_LEND }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_lend_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_lend_stateArgs[] = {
  { { STATE_LEND }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_lcount_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_lcount_stateArgs[] = {
  { { STATE_LCOUNT }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_lcount_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_lcount_stateArgs[] = {
  { { STATE_XTSYNC }, 'o' },
  { { STATE_LCOUNT }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_lcount_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_lcount_stateArgs[] = {
  { { STATE_XTSYNC }, 'o' },
  { { STATE_LCOUNT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_lbeg_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_lbeg_stateArgs[] = {
  { { STATE_LBEG }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_lbeg_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_lbeg_stateArgs[] = {
  { { STATE_LBEG }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_lbeg_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_lbeg_stateArgs[] = {
  { { STATE_LBEG }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_sar_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_sar_stateArgs[] = {
  { { STATE_SAR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_sar_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_sar_stateArgs[] = {
  { { STATE_SAR }, 'o' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_sar_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_sar_stateArgs[] = {
  { { STATE_SAR }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_litbase_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_litbase_stateArgs[] = {
  { { STATE_LITBADDR }, 'i' },
  { { STATE_LITBEN }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_litbase_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_litbase_stateArgs[] = {
  { { STATE_LITBADDR }, 'o' },
  { { STATE_LITBEN }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_litbase_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_litbase_stateArgs[] = {
  { { STATE_LITBADDR }, 'm' },
  { { STATE_LITBEN }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_176_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_176_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_176_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_176_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_208_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_208_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ps_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ps_stateArgs[] = {
  { { STATE_PSWOE }, 'i' },
  { { STATE_PSCALLINC }, 'i' },
  { { STATE_PSOWB }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_PSUM }, 'i' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSINTLEVEL }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ps_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ps_stateArgs[] = {
  { { STATE_PSWOE }, 'o' },
  { { STATE_PSCALLINC }, 'o' },
  { { STATE_PSOWB }, 'o' },
  { { STATE_PSRING }, 'm' },
  { { STATE_PSUM }, 'o' },
  { { STATE_PSEXCM }, 'm' },
  { { STATE_PSINTLEVEL }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ps_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ps_stateArgs[] = {
  { { STATE_PSWOE }, 'm' },
  { { STATE_PSCALLINC }, 'm' },
  { { STATE_PSOWB }, 'm' },
  { { STATE_PSRING }, 'm' },
  { { STATE_PSUM }, 'm' },
  { { STATE_PSEXCM }, 'm' },
  { { STATE_PSINTLEVEL }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc1_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc1_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC1 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc1_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC1 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave1_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave1_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE1 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave1_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE1 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc2_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC2 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc2_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC2 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc2_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC2 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave2_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE2 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave2_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE2 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave2_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE2 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc3_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc3_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC3 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc3_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc3_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC3 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc3_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc3_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC3 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave3_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave3_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE3 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave3_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave3_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE3 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave3_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave3_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE3 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc4_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc4_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc4_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc4_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC4 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc4_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc4_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC4 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave4_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave4_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave4_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave4_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE4 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave4_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave4_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE4 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc5_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc5_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC5 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc5_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc5_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC5 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc5_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc5_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC5 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave5_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave5_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE5 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave5_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave5_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE5 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave5_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave5_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE5 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc6_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc6_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc6_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc6_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc6_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc6_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave6_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave6_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave6_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave6_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave6_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave6_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc7_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_epc7_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC7 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc7_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_epc7_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC7 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc7_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_epc7_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPC7 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave7_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excsave7_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE7 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave7_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excsave7_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE7 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave7_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excsave7_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCSAVE7 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eps2_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eps2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS2 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eps2_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eps2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS2 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eps2_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eps2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS2 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eps3_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eps3_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS3 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eps3_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eps3_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS3 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eps3_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eps3_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS3 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eps4_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eps4_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eps4_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eps4_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS4 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eps4_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eps4_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS4 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eps5_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eps5_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS5 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eps5_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eps5_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS5 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eps5_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eps5_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS5 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eps6_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eps6_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eps6_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eps6_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS6 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eps6_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eps6_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS6 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eps7_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eps7_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS7 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eps7_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eps7_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS7 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eps7_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eps7_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EPS7 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excvaddr_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_excvaddr_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCVADDR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excvaddr_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_excvaddr_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCVADDR }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excvaddr_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_excvaddr_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCVADDR }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_depc_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_depc_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DEPC }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_depc_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_depc_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DEPC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_depc_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_depc_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DEPC }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_exccause_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_exccause_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCCAUSE }, 'i' },
  { { STATE_XTSYNC }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_exccause_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_exccause_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCCAUSE }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_exccause_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_exccause_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_EXCCAUSE }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_misc0_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_misc0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_MISC0 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_misc0_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_misc0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_MISC0 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_misc0_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_misc0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_MISC0 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_misc1_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_misc1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_MISC1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_misc1_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_misc1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_MISC1 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_misc1_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_misc1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_MISC1 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_prid_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_prid_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_vecbase_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_vecbase_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_VECBASE }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_vecbase_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_vecbase_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_VECBASE }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_vecbase_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_vecbase_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_VECBASE }, 'm' }
};

static xtensa_arg_internal Iclass_xt_mul16_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_mul32_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16_aa_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16_aa_stateArgs[] = {
  { { STATE_ACC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16_ad_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_my }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16_ad_stateArgs[] = {
  { { STATE_ACC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16_da_args[] = {
  { { OPERAND_mx }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16_da_stateArgs[] = {
  { { STATE_ACC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16_dd_args[] = {
  { { OPERAND_mx }, 'i' },
  { { OPERAND_my }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16_dd_stateArgs[] = {
  { { STATE_ACC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16a_aa_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16a_aa_stateArgs[] = {
  { { STATE_ACC }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16a_ad_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_my }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16a_ad_stateArgs[] = {
  { { STATE_ACC }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16a_da_args[] = {
  { { OPERAND_mx }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16a_da_stateArgs[] = {
  { { STATE_ACC }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16a_dd_args[] = {
  { { OPERAND_mx }, 'i' },
  { { OPERAND_my }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16a_dd_stateArgs[] = {
  { { STATE_ACC }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16al_da_args[] = {
  { { OPERAND_mw }, 'o' },
  { { OPERAND_ars }, 'm' },
  { { OPERAND_mx }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16al_da_stateArgs[] = {
  { { STATE_ACC }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16al_dd_args[] = {
  { { OPERAND_mw }, 'o' },
  { { OPERAND_ars }, 'm' },
  { { OPERAND_mx }, 'i' },
  { { OPERAND_my }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16al_dd_stateArgs[] = {
  { { STATE_ACC }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_mac16_l_args[] = {
  { { OPERAND_mw }, 'o' },
  { { OPERAND_ars }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_m0_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_mr0 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_m0_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_mr0 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_m0_args[] = {
  { { OPERAND_art }, 'm' },
  { { OPERAND_mr0 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_m1_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_mr1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_m1_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_mr1 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_m1_args[] = {
  { { OPERAND_art }, 'm' },
  { { OPERAND_mr1 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_m2_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_mr2 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_m2_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_mr2 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_m2_args[] = {
  { { OPERAND_art }, 'm' },
  { { OPERAND_mr2 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_m3_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_mr3 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_m3_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_mr3 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_m3_args[] = {
  { { OPERAND_art }, 'm' },
  { { OPERAND_mr3 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_acclo_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_acclo_stateArgs[] = {
  { { STATE_ACC }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_acclo_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_acclo_stateArgs[] = {
  { { STATE_ACC }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_acclo_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_acclo_stateArgs[] = {
  { { STATE_ACC }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_acchi_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_acchi_stateArgs[] = {
  { { STATE_ACC }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_acchi_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_acchi_stateArgs[] = {
  { { STATE_ACC }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_acchi_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_acchi_stateArgs[] = {
  { { STATE_ACC }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rfi_args[] = {
  { { OPERAND_s }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rfi_stateArgs[] = {
  { { STATE_PSWOE }, 'o' },
  { { STATE_PSCALLINC }, 'o' },
  { { STATE_PSOWB }, 'o' },
  { { STATE_PSRING }, 'm' },
  { { STATE_PSUM }, 'o' },
  { { STATE_PSEXCM }, 'm' },
  { { STATE_PSINTLEVEL }, 'o' },
  { { STATE_EPC1 }, 'i' },
  { { STATE_EPC2 }, 'i' },
  { { STATE_EPC3 }, 'i' },
  { { STATE_EPC4 }, 'i' },
  { { STATE_EPC5 }, 'i' },
  { { STATE_EPC6 }, 'i' },
  { { STATE_EPC7 }, 'i' },
  { { STATE_EPS2 }, 'i' },
  { { STATE_EPS3 }, 'i' },
  { { STATE_EPS4 }, 'i' },
  { { STATE_EPS5 }, 'i' },
  { { STATE_EPS6 }, 'i' },
  { { STATE_EPS7 }, 'i' },
  { { STATE_InOCDMode }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_wait_args[] = {
  { { OPERAND_s }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wait_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_PSINTLEVEL }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_interrupt_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_interrupt_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_INTERRUPT }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_intset_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_intset_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' },
  { { STATE_INTERRUPT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_intclear_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_intclear_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' },
  { { STATE_INTERRUPT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_intenable_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_intenable_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_INTENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_intenable_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_intenable_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_INTENABLE }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_intenable_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_intenable_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_INTENABLE }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_break_args[] = {
  { { OPERAND_imms }, 'i' },
  { { OPERAND_immt }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_break_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSINTLEVEL }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_break_n_args[] = {
  { { OPERAND_imms }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_break_n_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSINTLEVEL }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dbreaka0_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dbreaka0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKA0 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dbreaka0_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dbreaka0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKA0 }, 'o' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dbreaka0_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dbreaka0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKA0 }, 'm' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dbreakc0_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dbreakc0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKC0 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dbreakc0_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dbreakc0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKC0 }, 'o' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dbreakc0_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dbreakc0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKC0 }, 'm' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dbreaka1_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dbreaka1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKA1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dbreaka1_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dbreaka1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKA1 }, 'o' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dbreaka1_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dbreaka1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKA1 }, 'm' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dbreakc1_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dbreakc1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKC1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dbreakc1_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dbreakc1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKC1 }, 'o' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dbreakc1_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dbreakc1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DBREAKC1 }, 'm' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ibreaka0_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ibreaka0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_IBREAKA0 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ibreaka0_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ibreaka0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_IBREAKA0 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ibreaka0_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ibreaka0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_IBREAKA0 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ibreaka1_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ibreaka1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_IBREAKA1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ibreaka1_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ibreaka1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_IBREAKA1 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ibreaka1_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ibreaka1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_IBREAKA1 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ibreakenable_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ibreakenable_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_IBREAKENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ibreakenable_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ibreakenable_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_IBREAKENABLE }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ibreakenable_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ibreakenable_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_IBREAKENABLE }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_debugcause_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_debugcause_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DEBUGCAUSE }, 'i' },
  { { STATE_DBNUM }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_debugcause_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_debugcause_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DEBUGCAUSE }, 'o' },
  { { STATE_DBNUM }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_debugcause_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_debugcause_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DEBUGCAUSE }, 'm' },
  { { STATE_DBNUM }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_icount_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_icount_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ICOUNT }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_icount_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_icount_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' },
  { { STATE_ICOUNT }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_icount_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_icount_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' },
  { { STATE_ICOUNT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_icountlevel_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_icountlevel_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ICOUNTLEVEL }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_icountlevel_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_icountlevel_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ICOUNTLEVEL }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_icountlevel_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_icountlevel_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ICOUNTLEVEL }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ddr_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ddr_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DDR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ddr_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ddr_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' },
  { { STATE_DDR }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ddr_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ddr_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' },
  { { STATE_DDR }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rfdo_args[] = {
  { { OPERAND_imms }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rfdo_stateArgs[] = {
  { { STATE_InOCDMode }, 'm' },
  { { STATE_EPC6 }, 'i' },
  { { STATE_PSWOE }, 'o' },
  { { STATE_PSCALLINC }, 'o' },
  { { STATE_PSOWB }, 'o' },
  { { STATE_PSRING }, 'o' },
  { { STATE_PSUM }, 'o' },
  { { STATE_PSEXCM }, 'o' },
  { { STATE_PSINTLEVEL }, 'o' },
  { { STATE_EPS6 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rfdd_stateArgs[] = {
  { { STATE_InOCDMode }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_mmid_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_mmid_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ccount_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ccount_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOUNT }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ccount_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ccount_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' },
  { { STATE_CCOUNT }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ccount_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ccount_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' },
  { { STATE_CCOUNT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ccompare0_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ccompare0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOMPARE0 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ccompare0_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ccompare0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOMPARE0 }, 'o' },
  { { STATE_INTERRUPT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ccompare0_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ccompare0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOMPARE0 }, 'm' },
  { { STATE_INTERRUPT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ccompare1_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ccompare1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOMPARE1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ccompare1_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ccompare1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOMPARE1 }, 'o' },
  { { STATE_INTERRUPT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ccompare1_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ccompare1_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOMPARE1 }, 'm' },
  { { STATE_INTERRUPT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ccompare2_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ccompare2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOMPARE2 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ccompare2_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ccompare2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOMPARE2 }, 'o' },
  { { STATE_INTERRUPT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ccompare2_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ccompare2_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CCOMPARE2 }, 'm' },
  { { STATE_INTERRUPT }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_icache_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm8x4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_icache_lock_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm4x16 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_icache_lock_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_icache_inv_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm8x4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_icache_inv_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_licx_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_licx_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_sicx_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_sicx_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_dcache_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm8x4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_dcache_ind_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm4x16 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_dcache_ind_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_dcache_inv_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm8x4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_dcache_inv_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_dpf_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm8x4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_dcache_lock_args[] = {
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm4x16 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_dcache_lock_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_sdct_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_sdct_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_ldct_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_ldct_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ptevaddr_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_ptevaddr_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_PTBASE }, 'o' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ptevaddr_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_ptevaddr_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_PTBASE }, 'i' },
  { { STATE_EXCVADDR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ptevaddr_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_ptevaddr_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_PTBASE }, 'm' },
  { { STATE_EXCVADDR }, 'i' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_rasid_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_rasid_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ASID3 }, 'i' },
  { { STATE_ASID2 }, 'i' },
  { { STATE_ASID1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_rasid_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_rasid_stateArgs[] = {
  { { STATE_XTSYNC }, 'o' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ASID3 }, 'o' },
  { { STATE_ASID2 }, 'o' },
  { { STATE_ASID1 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_rasid_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_rasid_stateArgs[] = {
  { { STATE_XTSYNC }, 'o' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ASID3 }, 'm' },
  { { STATE_ASID2 }, 'm' },
  { { STATE_ASID1 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_itlbcfg_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_itlbcfg_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_INSTPGSZID6 }, 'i' },
  { { STATE_INSTPGSZID5 }, 'i' },
  { { STATE_INSTPGSZID4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_itlbcfg_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_itlbcfg_stateArgs[] = {
  { { STATE_XTSYNC }, 'o' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_INSTPGSZID6 }, 'o' },
  { { STATE_INSTPGSZID5 }, 'o' },
  { { STATE_INSTPGSZID4 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_itlbcfg_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_itlbcfg_stateArgs[] = {
  { { STATE_XTSYNC }, 'o' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_INSTPGSZID6 }, 'm' },
  { { STATE_INSTPGSZID5 }, 'm' },
  { { STATE_INSTPGSZID4 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dtlbcfg_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_dtlbcfg_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DATAPGSZID6 }, 'i' },
  { { STATE_DATAPGSZID5 }, 'i' },
  { { STATE_DATAPGSZID4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dtlbcfg_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_dtlbcfg_stateArgs[] = {
  { { STATE_XTSYNC }, 'o' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DATAPGSZID6 }, 'o' },
  { { STATE_DATAPGSZID5 }, 'o' },
  { { STATE_DATAPGSZID4 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dtlbcfg_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_dtlbcfg_stateArgs[] = {
  { { STATE_XTSYNC }, 'o' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_DATAPGSZID6 }, 'm' },
  { { STATE_DATAPGSZID5 }, 'm' },
  { { STATE_DATAPGSZID4 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_idtlb_args[] = {
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_idtlb_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rdtlb_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rdtlb_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wdtlb_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wdtlb_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_iitlb_args[] = {
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_iitlb_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_ritlb_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_ritlb_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_witlb_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_witlb_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_ldpte_stateArgs[] = {
  { { STATE_PTBASE }, 'i' },
  { { STATE_EXCVADDR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_hwwitlba_stateArgs[] = {
  { { STATE_EXCVADDR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_hwwdtlba_stateArgs[] = {
  { { STATE_EXCVADDR }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_cpenable_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_cpenable_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_cpenable_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_cpenable_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CPENABLE }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_cpenable_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_cpenable_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_CPENABLE }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_clamp_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_tp7 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_minmax_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_nsa_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_sx_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_tp7 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_l32ai_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm8x4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_s32ri_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm8x4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_s32c1i_args[] = {
  { { OPERAND_art }, 'm' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimm8x4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_s32c1i_stateArgs[] = {
  { { STATE_SCOMPARE1 }, 'i' },
  { { STATE_XTSYNC }, 'i' },
  { { STATE_SCOMPARE1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_scompare1_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_scompare1_stateArgs[] = {
  { { STATE_SCOMPARE1 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_scompare1_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_scompare1_stateArgs[] = {
  { { STATE_SCOMPARE1 }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_scompare1_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_scompare1_stateArgs[] = {
  { { STATE_SCOMPARE1 }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_atomctl_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_atomctl_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ATOMCTL }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_atomctl_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_atomctl_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ATOMCTL }, 'o' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_atomctl_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_atomctl_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ATOMCTL }, 'm' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_div_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rer_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wer_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_rur_expstate_args[] = {
  { { OPERAND_arr }, 'o' }
};

static xtensa_arg_internal Iclass_rur_expstate_stateArgs[] = {
  { { STATE_EXPSTATE }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_wur_expstate_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_wur_expstate_stateArgs[] = {
  { { STATE_EXPSTATE }, 'o' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_iclass_READ_IMPWIRE_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_iclass_READ_IMPWIRE_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_interface Iclass_iclass_READ_IMPWIRE_intfArgs[] = {
  INTERFACE_IMPWIRE
};

static xtensa_arg_internal Iclass_iclass_SETB_EXPSTATE_args[] = {
  { { OPERAND_bitindex }, 'i' }
};

static xtensa_arg_internal Iclass_iclass_SETB_EXPSTATE_stateArgs[] = {
  { { STATE_EXPSTATE }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_iclass_CLRB_EXPSTATE_args[] = {
  { { OPERAND_bitindex }, 'i' }
};

static xtensa_arg_internal Iclass_iclass_CLRB_EXPSTATE_stateArgs[] = {
  { { STATE_EXPSTATE }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_iclass_WRMSK_EXPSTATE_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_iclass_WRMSK_EXPSTATE_stateArgs[] = {
  { { STATE_EXPSTATE }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_iclass_internal iclasses[] = {
  { 0, 0 /* xt_iclass_excw */,
    0, 0, 0, 0 },
  { 0, 0 /* xt_iclass_rfe */,
    3, Iclass_xt_iclass_rfe_stateArgs, 0, 0 },
  { 0, 0 /* xt_iclass_rfde */,
    3, Iclass_xt_iclass_rfde_stateArgs, 0, 0 },
  { 0, 0 /* xt_iclass_syscall */,
    0, 0, 0, 0 },
  { 0, 0 /* xt_iclass_simcall */,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_call12_args,
    1, Iclass_xt_iclass_call12_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_call8_args,
    1, Iclass_xt_iclass_call8_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_call4_args,
    1, Iclass_xt_iclass_call4_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_callx12_args,
    1, Iclass_xt_iclass_callx12_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_callx8_args,
    1, Iclass_xt_iclass_callx8_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_callx4_args,
    1, Iclass_xt_iclass_callx4_stateArgs, 0, 0 },
  { 3, Iclass_xt_iclass_entry_args,
    5, Iclass_xt_iclass_entry_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_movsp_args,
    2, Iclass_xt_iclass_movsp_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rotw_args,
    3, Iclass_xt_iclass_rotw_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_retw_args,
    4, Iclass_xt_iclass_retw_stateArgs, 0, 0 },
  { 0, 0 /* xt_iclass_rfwou */,
    6, Iclass_xt_iclass_rfwou_stateArgs, 0, 0 },
  { 3, Iclass_xt_iclass_l32e_args,
    2, Iclass_xt_iclass_l32e_stateArgs, 0, 0 },
  { 3, Iclass_xt_iclass_s32e_args,
    2, Iclass_xt_iclass_s32e_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_windowbase_args,
    3, Iclass_xt_iclass_rsr_windowbase_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_windowbase_args,
    3, Iclass_xt_iclass_wsr_windowbase_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_windowbase_args,
    3, Iclass_xt_iclass_xsr_windowbase_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_windowstart_args,
    3, Iclass_xt_iclass_rsr_windowstart_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_windowstart_args,
    3, Iclass_xt_iclass_wsr_windowstart_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_windowstart_args,
    3, Iclass_xt_iclass_xsr_windowstart_stateArgs, 0, 0 },
  { 3, Iclass_xt_iclass_add_n_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_addi_n_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_bz6_args,
    0, 0, 0, 0 },
  { 0, 0 /* xt_iclass_ill_n */,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_loadi4_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_mov_n_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_movi_n_args,
    0, 0, 0, 0 },
  { 0, 0 /* xt_iclass_nopn */,
    0, 0, 0, 0 },
  { 1, Iclass_xt_iclass_retn_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_storei4_args,
    0, 0, 0, 0 },
  { 1, Iclass_rur_threadptr_args,
    1, Iclass_rur_threadptr_stateArgs, 0, 0 },
  { 1, Iclass_wur_threadptr_args,
    1, Iclass_wur_threadptr_stateArgs, 0, 0 },
  { 3, Iclass_xt_iclass_addi_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_addmi_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_addsub_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_bit_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_bsi8_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_bsi8b_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_bsi8u_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_bst8_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_bsz12_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_call0_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_callx0_args,
    0, 0, 0, 0 },
  { 4, Iclass_xt_iclass_exti_args,
    0, 0, 0, 0 },
  { 0, 0 /* xt_iclass_ill */,
    0, 0, 0, 0 },
  { 1, Iclass_xt_iclass_jump_args,
    0, 0, 0, 0 },
  { 1, Iclass_xt_iclass_jumpx_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_l16ui_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_l16si_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_l32i_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_l32r_args,
    2, Iclass_xt_iclass_l32r_stateArgs, 0, 0 },
  { 3, Iclass_xt_iclass_l8i_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_loop_args,
    3, Iclass_xt_iclass_loop_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_loopz_args,
    3, Iclass_xt_iclass_loopz_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_movi_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_movz_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_neg_args,
    0, 0, 0, 0 },
  { 0, 0 /* xt_iclass_nop */,
    0, 0, 0, 0 },
  { 1, Iclass_xt_iclass_return_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_s16i_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_s32i_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_s8i_args,
    0, 0, 0, 0 },
  { 1, Iclass_xt_iclass_sar_args,
    1, Iclass_xt_iclass_sar_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_sari_args,
    1, Iclass_xt_iclass_sari_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_shifts_args,
    1, Iclass_xt_iclass_shifts_stateArgs, 0, 0 },
  { 3, Iclass_xt_iclass_shiftst_args,
    1, Iclass_xt_iclass_shiftst_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_shiftt_args,
    1, Iclass_xt_iclass_shiftt_stateArgs, 0, 0 },
  { 3, Iclass_xt_iclass_slli_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_srai_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_srli_args,
    0, 0, 0, 0 },
  { 0, 0 /* xt_iclass_memw */,
    0, 0, 0, 0 },
  { 0, 0 /* xt_iclass_extw */,
    0, 0, 0, 0 },
  { 0, 0 /* xt_iclass_isync */,
    0, 0, 0, 0 },
  { 0, 0 /* xt_iclass_sync */,
    1, Iclass_xt_iclass_sync_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_rsil_args,
    7, Iclass_xt_iclass_rsil_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_lend_args,
    1, Iclass_xt_iclass_rsr_lend_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_lend_args,
    1, Iclass_xt_iclass_wsr_lend_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_lend_args,
    1, Iclass_xt_iclass_xsr_lend_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_lcount_args,
    1, Iclass_xt_iclass_rsr_lcount_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_lcount_args,
    2, Iclass_xt_iclass_wsr_lcount_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_lcount_args,
    2, Iclass_xt_iclass_xsr_lcount_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_lbeg_args,
    1, Iclass_xt_iclass_rsr_lbeg_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_lbeg_args,
    1, Iclass_xt_iclass_wsr_lbeg_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_lbeg_args,
    1, Iclass_xt_iclass_xsr_lbeg_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_sar_args,
    1, Iclass_xt_iclass_rsr_sar_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_sar_args,
    2, Iclass_xt_iclass_wsr_sar_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_sar_args,
    1, Iclass_xt_iclass_xsr_sar_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_litbase_args,
    2, Iclass_xt_iclass_rsr_litbase_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_litbase_args,
    2, Iclass_xt_iclass_wsr_litbase_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_litbase_args,
    2, Iclass_xt_iclass_xsr_litbase_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_176_args,
    2, Iclass_xt_iclass_rsr_176_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_176_args,
    2, Iclass_xt_iclass_wsr_176_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_208_args,
    2, Iclass_xt_iclass_rsr_208_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ps_args,
    7, Iclass_xt_iclass_rsr_ps_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ps_args,
    7, Iclass_xt_iclass_wsr_ps_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ps_args,
    7, Iclass_xt_iclass_xsr_ps_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_epc1_args,
    3, Iclass_xt_iclass_rsr_epc1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_epc1_args,
    3, Iclass_xt_iclass_wsr_epc1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_epc1_args,
    3, Iclass_xt_iclass_xsr_epc1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_excsave1_args,
    3, Iclass_xt_iclass_rsr_excsave1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_excsave1_args,
    3, Iclass_xt_iclass_wsr_excsave1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_excsave1_args,
    3, Iclass_xt_iclass_xsr_excsave1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_epc2_args,
    3, Iclass_xt_iclass_rsr_epc2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_epc2_args,
    3, Iclass_xt_iclass_wsr_epc2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_epc2_args,
    3, Iclass_xt_iclass_xsr_epc2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_excsave2_args,
    3, Iclass_xt_iclass_rsr_excsave2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_excsave2_args,
    3, Iclass_xt_iclass_wsr_excsave2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_excsave2_args,
    3, Iclass_xt_iclass_xsr_excsave2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_epc3_args,
    3, Iclass_xt_iclass_rsr_epc3_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_epc3_args,
    3, Iclass_xt_iclass_wsr_epc3_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_epc3_args,
    3, Iclass_xt_iclass_xsr_epc3_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_excsave3_args,
    3, Iclass_xt_iclass_rsr_excsave3_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_excsave3_args,
    3, Iclass_xt_iclass_wsr_excsave3_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_excsave3_args,
    3, Iclass_xt_iclass_xsr_excsave3_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_epc4_args,
    3, Iclass_xt_iclass_rsr_epc4_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_epc4_args,
    3, Iclass_xt_iclass_wsr_epc4_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_epc4_args,
    3, Iclass_xt_iclass_xsr_epc4_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_excsave4_args,
    3, Iclass_xt_iclass_rsr_excsave4_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_excsave4_args,
    3, Iclass_xt_iclass_wsr_excsave4_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_excsave4_args,
    3, Iclass_xt_iclass_xsr_excsave4_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_epc5_args,
    3, Iclass_xt_iclass_rsr_epc5_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_epc5_args,
    3, Iclass_xt_iclass_wsr_epc5_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_epc5_args,
    3, Iclass_xt_iclass_xsr_epc5_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_excsave5_args,
    3, Iclass_xt_iclass_rsr_excsave5_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_excsave5_args,
    3, Iclass_xt_iclass_wsr_excsave5_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_excsave5_args,
    3, Iclass_xt_iclass_xsr_excsave5_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_epc6_args,
    3, Iclass_xt_iclass_rsr_epc6_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_epc6_args,
    3, Iclass_xt_iclass_wsr_epc6_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_epc6_args,
    3, Iclass_xt_iclass_xsr_epc6_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_excsave6_args,
    3, Iclass_xt_iclass_rsr_excsave6_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_excsave6_args,
    3, Iclass_xt_iclass_wsr_excsave6_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_excsave6_args,
    3, Iclass_xt_iclass_xsr_excsave6_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_epc7_args,
    3, Iclass_xt_iclass_rsr_epc7_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_epc7_args,
    3, Iclass_xt_iclass_wsr_epc7_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_epc7_args,
    3, Iclass_xt_iclass_xsr_epc7_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_excsave7_args,
    3, Iclass_xt_iclass_rsr_excsave7_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_excsave7_args,
    3, Iclass_xt_iclass_wsr_excsave7_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_excsave7_args,
    3, Iclass_xt_iclass_xsr_excsave7_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_eps2_args,
    3, Iclass_xt_iclass_rsr_eps2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_eps2_args,
    3, Iclass_xt_iclass_wsr_eps2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_eps2_args,
    3, Iclass_xt_iclass_xsr_eps2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_eps3_args,
    3, Iclass_xt_iclass_rsr_eps3_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_eps3_args,
    3, Iclass_xt_iclass_wsr_eps3_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_eps3_args,
    3, Iclass_xt_iclass_xsr_eps3_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_eps4_args,
    3, Iclass_xt_iclass_rsr_eps4_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_eps4_args,
    3, Iclass_xt_iclass_wsr_eps4_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_eps4_args,
    3, Iclass_xt_iclass_xsr_eps4_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_eps5_args,
    3, Iclass_xt_iclass_rsr_eps5_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_eps5_args,
    3, Iclass_xt_iclass_wsr_eps5_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_eps5_args,
    3, Iclass_xt_iclass_xsr_eps5_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_eps6_args,
    3, Iclass_xt_iclass_rsr_eps6_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_eps6_args,
    3, Iclass_xt_iclass_wsr_eps6_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_eps6_args,
    3, Iclass_xt_iclass_xsr_eps6_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_eps7_args,
    3, Iclass_xt_iclass_rsr_eps7_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_eps7_args,
    3, Iclass_xt_iclass_wsr_eps7_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_eps7_args,
    3, Iclass_xt_iclass_xsr_eps7_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_excvaddr_args,
    3, Iclass_xt_iclass_rsr_excvaddr_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_excvaddr_args,
    3, Iclass_xt_iclass_wsr_excvaddr_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_excvaddr_args,
    3, Iclass_xt_iclass_xsr_excvaddr_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_depc_args,
    3, Iclass_xt_iclass_rsr_depc_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_depc_args,
    3, Iclass_xt_iclass_wsr_depc_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_depc_args,
    3, Iclass_xt_iclass_xsr_depc_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_exccause_args,
    4, Iclass_xt_iclass_rsr_exccause_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_exccause_args,
    3, Iclass_xt_iclass_wsr_exccause_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_exccause_args,
    3, Iclass_xt_iclass_xsr_exccause_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_misc0_args,
    3, Iclass_xt_iclass_rsr_misc0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_misc0_args,
    3, Iclass_xt_iclass_wsr_misc0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_misc0_args,
    3, Iclass_xt_iclass_xsr_misc0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_misc1_args,
    3, Iclass_xt_iclass_rsr_misc1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_misc1_args,
    3, Iclass_xt_iclass_wsr_misc1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_misc1_args,
    3, Iclass_xt_iclass_xsr_misc1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_prid_args,
    2, Iclass_xt_iclass_rsr_prid_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_vecbase_args,
    3, Iclass_xt_iclass_rsr_vecbase_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_vecbase_args,
    3, Iclass_xt_iclass_wsr_vecbase_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_vecbase_args,
    3, Iclass_xt_iclass_xsr_vecbase_stateArgs, 0, 0 },
  { 3, Iclass_xt_mul16_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_mul32_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_mac16_aa_args,
    1, Iclass_xt_iclass_mac16_aa_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_mac16_ad_args,
    1, Iclass_xt_iclass_mac16_ad_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_mac16_da_args,
    1, Iclass_xt_iclass_mac16_da_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_mac16_dd_args,
    1, Iclass_xt_iclass_mac16_dd_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_mac16a_aa_args,
    1, Iclass_xt_iclass_mac16a_aa_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_mac16a_ad_args,
    1, Iclass_xt_iclass_mac16a_ad_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_mac16a_da_args,
    1, Iclass_xt_iclass_mac16a_da_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_mac16a_dd_args,
    1, Iclass_xt_iclass_mac16a_dd_stateArgs, 0, 0 },
  { 4, Iclass_xt_iclass_mac16al_da_args,
    1, Iclass_xt_iclass_mac16al_da_stateArgs, 0, 0 },
  { 4, Iclass_xt_iclass_mac16al_dd_args,
    1, Iclass_xt_iclass_mac16al_dd_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_mac16_l_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_rsr_m0_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_wsr_m0_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_xsr_m0_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_rsr_m1_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_wsr_m1_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_xsr_m1_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_rsr_m2_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_wsr_m2_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_xsr_m2_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_rsr_m3_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_wsr_m3_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_xsr_m3_args,
    0, 0, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_acclo_args,
    1, Iclass_xt_iclass_rsr_acclo_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_acclo_args,
    1, Iclass_xt_iclass_wsr_acclo_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_acclo_args,
    1, Iclass_xt_iclass_xsr_acclo_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_acchi_args,
    1, Iclass_xt_iclass_rsr_acchi_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_acchi_args,
    1, Iclass_xt_iclass_wsr_acchi_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_acchi_args,
    1, Iclass_xt_iclass_xsr_acchi_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rfi_args,
    21, Iclass_xt_iclass_rfi_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wait_args,
    3, Iclass_xt_iclass_wait_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_interrupt_args,
    3, Iclass_xt_iclass_rsr_interrupt_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_intset_args,
    4, Iclass_xt_iclass_wsr_intset_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_intclear_args,
    4, Iclass_xt_iclass_wsr_intclear_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_intenable_args,
    3, Iclass_xt_iclass_rsr_intenable_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_intenable_args,
    3, Iclass_xt_iclass_wsr_intenable_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_intenable_args,
    3, Iclass_xt_iclass_xsr_intenable_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_break_args,
    2, Iclass_xt_iclass_break_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_break_n_args,
    2, Iclass_xt_iclass_break_n_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_dbreaka0_args,
    3, Iclass_xt_iclass_rsr_dbreaka0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_dbreaka0_args,
    4, Iclass_xt_iclass_wsr_dbreaka0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_dbreaka0_args,
    4, Iclass_xt_iclass_xsr_dbreaka0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_dbreakc0_args,
    3, Iclass_xt_iclass_rsr_dbreakc0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_dbreakc0_args,
    4, Iclass_xt_iclass_wsr_dbreakc0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_dbreakc0_args,
    4, Iclass_xt_iclass_xsr_dbreakc0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_dbreaka1_args,
    3, Iclass_xt_iclass_rsr_dbreaka1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_dbreaka1_args,
    4, Iclass_xt_iclass_wsr_dbreaka1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_dbreaka1_args,
    4, Iclass_xt_iclass_xsr_dbreaka1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_dbreakc1_args,
    3, Iclass_xt_iclass_rsr_dbreakc1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_dbreakc1_args,
    4, Iclass_xt_iclass_wsr_dbreakc1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_dbreakc1_args,
    4, Iclass_xt_iclass_xsr_dbreakc1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ibreaka0_args,
    3, Iclass_xt_iclass_rsr_ibreaka0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ibreaka0_args,
    3, Iclass_xt_iclass_wsr_ibreaka0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ibreaka0_args,
    3, Iclass_xt_iclass_xsr_ibreaka0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ibreaka1_args,
    3, Iclass_xt_iclass_rsr_ibreaka1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ibreaka1_args,
    3, Iclass_xt_iclass_wsr_ibreaka1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ibreaka1_args,
    3, Iclass_xt_iclass_xsr_ibreaka1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ibreakenable_args,
    3, Iclass_xt_iclass_rsr_ibreakenable_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ibreakenable_args,
    3, Iclass_xt_iclass_wsr_ibreakenable_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ibreakenable_args,
    3, Iclass_xt_iclass_xsr_ibreakenable_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_debugcause_args,
    4, Iclass_xt_iclass_rsr_debugcause_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_debugcause_args,
    4, Iclass_xt_iclass_wsr_debugcause_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_debugcause_args,
    4, Iclass_xt_iclass_xsr_debugcause_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_icount_args,
    3, Iclass_xt_iclass_rsr_icount_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_icount_args,
    4, Iclass_xt_iclass_wsr_icount_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_icount_args,
    4, Iclass_xt_iclass_xsr_icount_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_icountlevel_args,
    3, Iclass_xt_iclass_rsr_icountlevel_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_icountlevel_args,
    3, Iclass_xt_iclass_wsr_icountlevel_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_icountlevel_args,
    3, Iclass_xt_iclass_xsr_icountlevel_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ddr_args,
    3, Iclass_xt_iclass_rsr_ddr_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ddr_args,
    4, Iclass_xt_iclass_wsr_ddr_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ddr_args,
    4, Iclass_xt_iclass_xsr_ddr_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rfdo_args,
    10, Iclass_xt_iclass_rfdo_stateArgs, 0, 0 },
  { 0, 0 /* xt_iclass_rfdd */,
    1, Iclass_xt_iclass_rfdd_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_mmid_args,
    3, Iclass_xt_iclass_wsr_mmid_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ccount_args,
    3, Iclass_xt_iclass_rsr_ccount_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ccount_args,
    4, Iclass_xt_iclass_wsr_ccount_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ccount_args,
    4, Iclass_xt_iclass_xsr_ccount_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ccompare0_args,
    3, Iclass_xt_iclass_rsr_ccompare0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ccompare0_args,
    4, Iclass_xt_iclass_wsr_ccompare0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ccompare0_args,
    4, Iclass_xt_iclass_xsr_ccompare0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ccompare1_args,
    3, Iclass_xt_iclass_rsr_ccompare1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ccompare1_args,
    4, Iclass_xt_iclass_wsr_ccompare1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ccompare1_args,
    4, Iclass_xt_iclass_xsr_ccompare1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ccompare2_args,
    3, Iclass_xt_iclass_rsr_ccompare2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ccompare2_args,
    4, Iclass_xt_iclass_wsr_ccompare2_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ccompare2_args,
    4, Iclass_xt_iclass_xsr_ccompare2_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_icache_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_icache_lock_args,
    2, Iclass_xt_iclass_icache_lock_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_icache_inv_args,
    2, Iclass_xt_iclass_icache_inv_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_licx_args,
    2, Iclass_xt_iclass_licx_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_sicx_args,
    2, Iclass_xt_iclass_sicx_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_dcache_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_dcache_ind_args,
    2, Iclass_xt_iclass_dcache_ind_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_dcache_inv_args,
    2, Iclass_xt_iclass_dcache_inv_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_dpf_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_dcache_lock_args,
    2, Iclass_xt_iclass_dcache_lock_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_sdct_args,
    2, Iclass_xt_iclass_sdct_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_ldct_args,
    2, Iclass_xt_iclass_ldct_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_ptevaddr_args,
    4, Iclass_xt_iclass_wsr_ptevaddr_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_ptevaddr_args,
    4, Iclass_xt_iclass_rsr_ptevaddr_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_ptevaddr_args,
    5, Iclass_xt_iclass_xsr_ptevaddr_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_rasid_args,
    5, Iclass_xt_iclass_rsr_rasid_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_rasid_args,
    6, Iclass_xt_iclass_wsr_rasid_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_rasid_args,
    6, Iclass_xt_iclass_xsr_rasid_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_itlbcfg_args,
    5, Iclass_xt_iclass_rsr_itlbcfg_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_itlbcfg_args,
    6, Iclass_xt_iclass_wsr_itlbcfg_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_itlbcfg_args,
    6, Iclass_xt_iclass_xsr_itlbcfg_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_dtlbcfg_args,
    5, Iclass_xt_iclass_rsr_dtlbcfg_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_dtlbcfg_args,
    6, Iclass_xt_iclass_wsr_dtlbcfg_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_dtlbcfg_args,
    6, Iclass_xt_iclass_xsr_dtlbcfg_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_idtlb_args,
    3, Iclass_xt_iclass_idtlb_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_rdtlb_args,
    2, Iclass_xt_iclass_rdtlb_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_wdtlb_args,
    3, Iclass_xt_iclass_wdtlb_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_iitlb_args,
    2, Iclass_xt_iclass_iitlb_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_ritlb_args,
    2, Iclass_xt_iclass_ritlb_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_witlb_args,
    2, Iclass_xt_iclass_witlb_stateArgs, 0, 0 },
  { 0, 0 /* xt_iclass_ldpte */,
    2, Iclass_xt_iclass_ldpte_stateArgs, 0, 0 },
  { 0, 0 /* xt_iclass_hwwitlba */,
    1, Iclass_xt_iclass_hwwitlba_stateArgs, 0, 0 },
  { 0, 0 /* xt_iclass_hwwdtlba */,
    1, Iclass_xt_iclass_hwwdtlba_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_cpenable_args,
    3, Iclass_xt_iclass_rsr_cpenable_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_cpenable_args,
    3, Iclass_xt_iclass_wsr_cpenable_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_cpenable_args,
    3, Iclass_xt_iclass_xsr_cpenable_stateArgs, 0, 0 },
  { 3, Iclass_xt_iclass_clamp_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_minmax_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_nsa_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_sx_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_l32ai_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_s32ri_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_s32c1i_args,
    3, Iclass_xt_iclass_s32c1i_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_scompare1_args,
    1, Iclass_xt_iclass_rsr_scompare1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_scompare1_args,
    1, Iclass_xt_iclass_wsr_scompare1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_scompare1_args,
    1, Iclass_xt_iclass_xsr_scompare1_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_atomctl_args,
    3, Iclass_xt_iclass_rsr_atomctl_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_atomctl_args,
    4, Iclass_xt_iclass_wsr_atomctl_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_atomctl_args,
    4, Iclass_xt_iclass_xsr_atomctl_stateArgs, 0, 0 },
  { 3, Iclass_xt_iclass_div_args,
    0, 0, 0, 0 },
  { 0, 0 /* xt_iclass_rer */,
    2, Iclass_xt_iclass_rer_stateArgs, 0, 0 },
  { 0, 0 /* xt_iclass_wer */,
    2, Iclass_xt_iclass_wer_stateArgs, 0, 0 },
  { 1, Iclass_rur_expstate_args,
    2, Iclass_rur_expstate_stateArgs, 0, 0 },
  { 1, Iclass_wur_expstate_args,
    2, Iclass_wur_expstate_stateArgs, 0, 0 },
  { 1, Iclass_iclass_READ_IMPWIRE_args,
    1, Iclass_iclass_READ_IMPWIRE_stateArgs, 1, Iclass_iclass_READ_IMPWIRE_intfArgs },
  { 1, Iclass_iclass_SETB_EXPSTATE_args,
    2, Iclass_iclass_SETB_EXPSTATE_stateArgs, 0, 0 },
  { 1, Iclass_iclass_CLRB_EXPSTATE_args,
    2, Iclass_iclass_CLRB_EXPSTATE_stateArgs, 0, 0 },
  { 2, Iclass_iclass_WRMSK_EXPSTATE_args,
    2, Iclass_iclass_WRMSK_EXPSTATE_stateArgs, 0, 0 }
};

enum xtensa_iclass_id {
  ICLASS_xt_iclass_excw,
  ICLASS_xt_iclass_rfe,
  ICLASS_xt_iclass_rfde,
  ICLASS_xt_iclass_syscall,
  ICLASS_xt_iclass_simcall,
  ICLASS_xt_iclass_call12,
  ICLASS_xt_iclass_call8,
  ICLASS_xt_iclass_call4,
  ICLASS_xt_iclass_callx12,
  ICLASS_xt_iclass_callx8,
  ICLASS_xt_iclass_callx4,
  ICLASS_xt_iclass_entry,
  ICLASS_xt_iclass_movsp,
  ICLASS_xt_iclass_rotw,
  ICLASS_xt_iclass_retw,
  ICLASS_xt_iclass_rfwou,
  ICLASS_xt_iclass_l32e,
  ICLASS_xt_iclass_s32e,
  ICLASS_xt_iclass_rsr_windowbase,
  ICLASS_xt_iclass_wsr_windowbase,
  ICLASS_xt_iclass_xsr_windowbase,
  ICLASS_xt_iclass_rsr_windowstart,
  ICLASS_xt_iclass_wsr_windowstart,
  ICLASS_xt_iclass_xsr_windowstart,
  ICLASS_xt_iclass_add_n,
  ICLASS_xt_iclass_addi_n,
  ICLASS_xt_iclass_bz6,
  ICLASS_xt_iclass_ill_n,
  ICLASS_xt_iclass_loadi4,
  ICLASS_xt_iclass_mov_n,
  ICLASS_xt_iclass_movi_n,
  ICLASS_xt_iclass_nopn,
  ICLASS_xt_iclass_retn,
  ICLASS_xt_iclass_storei4,
  ICLASS_rur_threadptr,
  ICLASS_wur_threadptr,
  ICLASS_xt_iclass_addi,
  ICLASS_xt_iclass_addmi,
  ICLASS_xt_iclass_addsub,
  ICLASS_xt_iclass_bit,
  ICLASS_xt_iclass_bsi8,
  ICLASS_xt_iclass_bsi8b,
  ICLASS_xt_iclass_bsi8u,
  ICLASS_xt_iclass_bst8,
  ICLASS_xt_iclass_bsz12,
  ICLASS_xt_iclass_call0,
  ICLASS_xt_iclass_callx0,
  ICLASS_xt_iclass_exti,
  ICLASS_xt_iclass_ill,
  ICLASS_xt_iclass_jump,
  ICLASS_xt_iclass_jumpx,
  ICLASS_xt_iclass_l16ui,
  ICLASS_xt_iclass_l16si,
  ICLASS_xt_iclass_l32i,
  ICLASS_xt_iclass_l32r,
  ICLASS_xt_iclass_l8i,
  ICLASS_xt_iclass_loop,
  ICLASS_xt_iclass_loopz,
  ICLASS_xt_iclass_movi,
  ICLASS_xt_iclass_movz,
  ICLASS_xt_iclass_neg,
  ICLASS_xt_iclass_nop,
  ICLASS_xt_iclass_return,
  ICLASS_xt_iclass_s16i,
  ICLASS_xt_iclass_s32i,
  ICLASS_xt_iclass_s8i,
  ICLASS_xt_iclass_sar,
  ICLASS_xt_iclass_sari,
  ICLASS_xt_iclass_shifts,
  ICLASS_xt_iclass_shiftst,
  ICLASS_xt_iclass_shiftt,
  ICLASS_xt_iclass_slli,
  ICLASS_xt_iclass_srai,
  ICLASS_xt_iclass_srli,
  ICLASS_xt_iclass_memw,
  ICLASS_xt_iclass_extw,
  ICLASS_xt_iclass_isync,
  ICLASS_xt_iclass_sync,
  ICLASS_xt_iclass_rsil,
  ICLASS_xt_iclass_rsr_lend,
  ICLASS_xt_iclass_wsr_lend,
  ICLASS_xt_iclass_xsr_lend,
  ICLASS_xt_iclass_rsr_lcount,
  ICLASS_xt_iclass_wsr_lcount,
  ICLASS_xt_iclass_xsr_lcount,
  ICLASS_xt_iclass_rsr_lbeg,
  ICLASS_xt_iclass_wsr_lbeg,
  ICLASS_xt_iclass_xsr_lbeg,
  ICLASS_xt_iclass_rsr_sar,
  ICLASS_xt_iclass_wsr_sar,
  ICLASS_xt_iclass_xsr_sar,
  ICLASS_xt_iclass_rsr_litbase,
  ICLASS_xt_iclass_wsr_litbase,
  ICLASS_xt_iclass_xsr_litbase,
  ICLASS_xt_iclass_rsr_176,
  ICLASS_xt_iclass_wsr_176,
  ICLASS_xt_iclass_rsr_208,
  ICLASS_xt_iclass_rsr_ps,
  ICLASS_xt_iclass_wsr_ps,
  ICLASS_xt_iclass_xsr_ps,
  ICLASS_xt_iclass_rsr_epc1,
  ICLASS_xt_iclass_wsr_epc1,
  ICLASS_xt_iclass_xsr_epc1,
  ICLASS_xt_iclass_rsr_excsave1,
  ICLASS_xt_iclass_wsr_excsave1,
  ICLASS_xt_iclass_xsr_excsave1,
  ICLASS_xt_iclass_rsr_epc2,
  ICLASS_xt_iclass_wsr_epc2,
  ICLASS_xt_iclass_xsr_epc2,
  ICLASS_xt_iclass_rsr_excsave2,
  ICLASS_xt_iclass_wsr_excsave2,
  ICLASS_xt_iclass_xsr_excsave2,
  ICLASS_xt_iclass_rsr_epc3,
  ICLASS_xt_iclass_wsr_epc3,
  ICLASS_xt_iclass_xsr_epc3,
  ICLASS_xt_iclass_rsr_excsave3,
  ICLASS_xt_iclass_wsr_excsave3,
  ICLASS_xt_iclass_xsr_excsave3,
  ICLASS_xt_iclass_rsr_epc4,
  ICLASS_xt_iclass_wsr_epc4,
  ICLASS_xt_iclass_xsr_epc4,
  ICLASS_xt_iclass_rsr_excsave4,
  ICLASS_xt_iclass_wsr_excsave4,
  ICLASS_xt_iclass_xsr_excsave4,
  ICLASS_xt_iclass_rsr_epc5,
  ICLASS_xt_iclass_wsr_epc5,
  ICLASS_xt_iclass_xsr_epc5,
  ICLASS_xt_iclass_rsr_excsave5,
  ICLASS_xt_iclass_wsr_excsave5,
  ICLASS_xt_iclass_xsr_excsave5,
  ICLASS_xt_iclass_rsr_epc6,
  ICLASS_xt_iclass_wsr_epc6,
  ICLASS_xt_iclass_xsr_epc6,
  ICLASS_xt_iclass_rsr_excsave6,
  ICLASS_xt_iclass_wsr_excsave6,
  ICLASS_xt_iclass_xsr_excsave6,
  ICLASS_xt_iclass_rsr_epc7,
  ICLASS_xt_iclass_wsr_epc7,
  ICLASS_xt_iclass_xsr_epc7,
  ICLASS_xt_iclass_rsr_excsave7,
  ICLASS_xt_iclass_wsr_excsave7,
  ICLASS_xt_iclass_xsr_excsave7,
  ICLASS_xt_iclass_rsr_eps2,
  ICLASS_xt_iclass_wsr_eps2,
  ICLASS_xt_iclass_xsr_eps2,
  ICLASS_xt_iclass_rsr_eps3,
  ICLASS_xt_iclass_wsr_eps3,
  ICLASS_xt_iclass_xsr_eps3,
  ICLASS_xt_iclass_rsr_eps4,
  ICLASS_xt_iclass_wsr_eps4,
  ICLASS_xt_iclass_xsr_eps4,
  ICLASS_xt_iclass_rsr_eps5,
  ICLASS_xt_iclass_wsr_eps5,
  ICLASS_xt_iclass_xsr_eps5,
  ICLASS_xt_iclass_rsr_eps6,
  ICLASS_xt_iclass_wsr_eps6,
  ICLASS_xt_iclass_xsr_eps6,
  ICLASS_xt_iclass_rsr_eps7,
  ICLASS_xt_iclass_wsr_eps7,
  ICLASS_xt_iclass_xsr_eps7,
  ICLASS_xt_iclass_rsr_excvaddr,
  ICLASS_xt_iclass_wsr_excvaddr,
  ICLASS_xt_iclass_xsr_excvaddr,
  ICLASS_xt_iclass_rsr_depc,
  ICLASS_xt_iclass_wsr_depc,
  ICLASS_xt_iclass_xsr_depc,
  ICLASS_xt_iclass_rsr_exccause,
  ICLASS_xt_iclass_wsr_exccause,
  ICLASS_xt_iclass_xsr_exccause,
  ICLASS_xt_iclass_rsr_misc0,
  ICLASS_xt_iclass_wsr_misc0,
  ICLASS_xt_iclass_xsr_misc0,
  ICLASS_xt_iclass_rsr_misc1,
  ICLASS_xt_iclass_wsr_misc1,
  ICLASS_xt_iclass_xsr_misc1,
  ICLASS_xt_iclass_rsr_prid,
  ICLASS_xt_iclass_rsr_vecbase,
  ICLASS_xt_iclass_wsr_vecbase,
  ICLASS_xt_iclass_xsr_vecbase,
  ICLASS_xt_mul16,
  ICLASS_xt_mul32,
  ICLASS_xt_iclass_mac16_aa,
  ICLASS_xt_iclass_mac16_ad,
  ICLASS_xt_iclass_mac16_da,
  ICLASS_xt_iclass_mac16_dd,
  ICLASS_xt_iclass_mac16a_aa,
  ICLASS_xt_iclass_mac16a_ad,
  ICLASS_xt_iclass_mac16a_da,
  ICLASS_xt_iclass_mac16a_dd,
  ICLASS_xt_iclass_mac16al_da,
  ICLASS_xt_iclass_mac16al_dd,
  ICLASS_xt_iclass_mac16_l,
  ICLASS_xt_iclass_rsr_m0,
  ICLASS_xt_iclass_wsr_m0,
  ICLASS_xt_iclass_xsr_m0,
  ICLASS_xt_iclass_rsr_m1,
  ICLASS_xt_iclass_wsr_m1,
  ICLASS_xt_iclass_xsr_m1,
  ICLASS_xt_iclass_rsr_m2,
  ICLASS_xt_iclass_wsr_m2,
  ICLASS_xt_iclass_xsr_m2,
  ICLASS_xt_iclass_rsr_m3,
  ICLASS_xt_iclass_wsr_m3,
  ICLASS_xt_iclass_xsr_m3,
  ICLASS_xt_iclass_rsr_acclo,
  ICLASS_xt_iclass_wsr_acclo,
  ICLASS_xt_iclass_xsr_acclo,
  ICLASS_xt_iclass_rsr_acchi,
  ICLASS_xt_iclass_wsr_acchi,
  ICLASS_xt_iclass_xsr_acchi,
  ICLASS_xt_iclass_rfi,
  ICLASS_xt_iclass_wait,
  ICLASS_xt_iclass_rsr_interrupt,
  ICLASS_xt_iclass_wsr_intset,
  ICLASS_xt_iclass_wsr_intclear,
  ICLASS_xt_iclass_rsr_intenable,
  ICLASS_xt_iclass_wsr_intenable,
  ICLASS_xt_iclass_xsr_intenable,
  ICLASS_xt_iclass_break,
  ICLASS_xt_iclass_break_n,
  ICLASS_xt_iclass_rsr_dbreaka0,
  ICLASS_xt_iclass_wsr_dbreaka0,
  ICLASS_xt_iclass_xsr_dbreaka0,
  ICLASS_xt_iclass_rsr_dbreakc0,
  ICLASS_xt_iclass_wsr_dbreakc0,
  ICLASS_xt_iclass_xsr_dbreakc0,
  ICLASS_xt_iclass_rsr_dbreaka1,
  ICLASS_xt_iclass_wsr_dbreaka1,
  ICLASS_xt_iclass_xsr_dbreaka1,
  ICLASS_xt_iclass_rsr_dbreakc1,
  ICLASS_xt_iclass_wsr_dbreakc1,
  ICLASS_xt_iclass_xsr_dbreakc1,
  ICLASS_xt_iclass_rsr_ibreaka0,
  ICLASS_xt_iclass_wsr_ibreaka0,
  ICLASS_xt_iclass_xsr_ibreaka0,
  ICLASS_xt_iclass_rsr_ibreaka1,
  ICLASS_xt_iclass_wsr_ibreaka1,
  ICLASS_xt_iclass_xsr_ibreaka1,
  ICLASS_xt_iclass_rsr_ibreakenable,
  ICLASS_xt_iclass_wsr_ibreakenable,
  ICLASS_xt_iclass_xsr_ibreakenable,
  ICLASS_xt_iclass_rsr_debugcause,
  ICLASS_xt_iclass_wsr_debugcause,
  ICLASS_xt_iclass_xsr_debugcause,
  ICLASS_xt_iclass_rsr_icount,
  ICLASS_xt_iclass_wsr_icount,
  ICLASS_xt_iclass_xsr_icount,
  ICLASS_xt_iclass_rsr_icountlevel,
  ICLASS_xt_iclass_wsr_icountlevel,
  ICLASS_xt_iclass_xsr_icountlevel,
  ICLASS_xt_iclass_rsr_ddr,
  ICLASS_xt_iclass_wsr_ddr,
  ICLASS_xt_iclass_xsr_ddr,
  ICLASS_xt_iclass_rfdo,
  ICLASS_xt_iclass_rfdd,
  ICLASS_xt_iclass_wsr_mmid,
  ICLASS_xt_iclass_rsr_ccount,
  ICLASS_xt_iclass_wsr_ccount,
  ICLASS_xt_iclass_xsr_ccount,
  ICLASS_xt_iclass_rsr_ccompare0,
  ICLASS_xt_iclass_wsr_ccompare0,
  ICLASS_xt_iclass_xsr_ccompare0,
  ICLASS_xt_iclass_rsr_ccompare1,
  ICLASS_xt_iclass_wsr_ccompare1,
  ICLASS_xt_iclass_xsr_ccompare1,
  ICLASS_xt_iclass_rsr_ccompare2,
  ICLASS_xt_iclass_wsr_ccompare2,
  ICLASS_xt_iclass_xsr_ccompare2,
  ICLASS_xt_iclass_icache,
  ICLASS_xt_iclass_icache_lock,
  ICLASS_xt_iclass_icache_inv,
  ICLASS_xt_iclass_licx,
  ICLASS_xt_iclass_sicx,
  ICLASS_xt_iclass_dcache,
  ICLASS_xt_iclass_dcache_ind,
  ICLASS_xt_iclass_dcache_inv,
  ICLASS_xt_iclass_dpf,
  ICLASS_xt_iclass_dcache_lock,
  ICLASS_xt_iclass_sdct,
  ICLASS_xt_iclass_ldct,
  ICLASS_xt_iclass_wsr_ptevaddr,
  ICLASS_xt_iclass_rsr_ptevaddr,
  ICLASS_xt_iclass_xsr_ptevaddr,
  ICLASS_xt_iclass_rsr_rasid,
  ICLASS_xt_iclass_wsr_rasid,
  ICLASS_xt_iclass_xsr_rasid,
  ICLASS_xt_iclass_rsr_itlbcfg,
  ICLASS_xt_iclass_wsr_itlbcfg,
  ICLASS_xt_iclass_xsr_itlbcfg,
  ICLASS_xt_iclass_rsr_dtlbcfg,
  ICLASS_xt_iclass_wsr_dtlbcfg,
  ICLASS_xt_iclass_xsr_dtlbcfg,
  ICLASS_xt_iclass_idtlb,
  ICLASS_xt_iclass_rdtlb,
  ICLASS_xt_iclass_wdtlb,
  ICLASS_xt_iclass_iitlb,
  ICLASS_xt_iclass_ritlb,
  ICLASS_xt_iclass_witlb,
  ICLASS_xt_iclass_ldpte,
  ICLASS_xt_iclass_hwwitlba,
  ICLASS_xt_iclass_hwwdtlba,
  ICLASS_xt_iclass_rsr_cpenable,
  ICLASS_xt_iclass_wsr_cpenable,
  ICLASS_xt_iclass_xsr_cpenable,
  ICLASS_xt_iclass_clamp,
  ICLASS_xt_iclass_minmax,
  ICLASS_xt_iclass_nsa,
  ICLASS_xt_iclass_sx,
  ICLASS_xt_iclass_l32ai,
  ICLASS_xt_iclass_s32ri,
  ICLASS_xt_iclass_s32c1i,
  ICLASS_xt_iclass_rsr_scompare1,
  ICLASS_xt_iclass_wsr_scompare1,
  ICLASS_xt_iclass_xsr_scompare1,
  ICLASS_xt_iclass_rsr_atomctl,
  ICLASS_xt_iclass_wsr_atomctl,
  ICLASS_xt_iclass_xsr_atomctl,
  ICLASS_xt_iclass_div,
  ICLASS_xt_iclass_rer,
  ICLASS_xt_iclass_wer,
  ICLASS_rur_expstate,
  ICLASS_wur_expstate,
  ICLASS_iclass_READ_IMPWIRE,
  ICLASS_iclass_SETB_EXPSTATE,
  ICLASS_iclass_CLRB_EXPSTATE,
  ICLASS_iclass_WRMSK_EXPSTATE
};


/*  Opcode encodings.  */

static void
Opcode_excw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2080;
}

static void
Opcode_rfe_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3000;
}

static void
Opcode_rfde_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3200;
}

static void
Opcode_syscall_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5000;
}

static void
Opcode_simcall_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5100;
}

static void
Opcode_call12_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x35;
}

static void
Opcode_call8_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x25;
}

static void
Opcode_call4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x15;
}

static void
Opcode_callx12_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf0;
}

static void
Opcode_callx8_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe0;
}

static void
Opcode_callx4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd0;
}

static void
Opcode_entry_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x36;
}

static void
Opcode_movsp_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1000;
}

static void
Opcode_rotw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x408000;
}

static void
Opcode_retw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x90;
}

static void
Opcode_retw_n_Slot_inst16b_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf01d;
}

static void
Opcode_rfwo_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3400;
}

static void
Opcode_rfwu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3500;
}

static void
Opcode_l32e_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x90000;
}

static void
Opcode_s32e_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x490000;
}

static void
Opcode_rsr_windowbase_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x34800;
}

static void
Opcode_wsr_windowbase_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x134800;
}

static void
Opcode_xsr_windowbase_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x614800;
}

static void
Opcode_rsr_windowstart_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x34900;
}

static void
Opcode_wsr_windowstart_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x134900;
}

static void
Opcode_xsr_windowstart_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x614900;
}

static void
Opcode_add_n_Slot_inst16a_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa;
}

static void
Opcode_addi_n_Slot_inst16a_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb;
}

static void
Opcode_beqz_n_Slot_inst16b_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8c;
}

static void
Opcode_bnez_n_Slot_inst16b_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xcc;
}

static void
Opcode_ill_n_Slot_inst16b_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf06d;
}

static void
Opcode_l32i_n_Slot_inst16a_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8;
}

static void
Opcode_mov_n_Slot_inst16b_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd;
}

static void
Opcode_movi_n_Slot_inst16b_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc;
}

static void
Opcode_nop_n_Slot_inst16b_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf03d;
}

static void
Opcode_ret_n_Slot_inst16b_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf00d;
}

static void
Opcode_s32i_n_Slot_inst16a_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9;
}

static void
Opcode_rur_threadptr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe30e70;
}

static void
Opcode_wur_threadptr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf3e700;
}

static void
Opcode_addi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc002;
}

static void
Opcode_addmi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd002;
}

static void
Opcode_add_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x800000;
}

static void
Opcode_sub_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc00000;
}

static void
Opcode_addx2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x900000;
}

static void
Opcode_addx4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa00000;
}

static void
Opcode_addx8_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb00000;
}

static void
Opcode_subx2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd00000;
}

static void
Opcode_subx4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe00000;
}

static void
Opcode_subx8_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf00000;
}

static void
Opcode_and_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x100000;
}

static void
Opcode_or_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x200000;
}

static void
Opcode_xor_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x300000;
}

static void
Opcode_beqi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x26;
}

static void
Opcode_bnei_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x66;
}

static void
Opcode_bgei_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe6;
}

static void
Opcode_blti_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa6;
}

static void
Opcode_bbci_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6007;
}

static void
Opcode_bbsi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe007;
}

static void
Opcode_bgeui_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf6;
}

static void
Opcode_bltui_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb6;
}

static void
Opcode_beq_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1007;
}

static void
Opcode_bne_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9007;
}

static void
Opcode_bge_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa007;
}

static void
Opcode_blt_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2007;
}

static void
Opcode_bgeu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb007;
}

static void
Opcode_bltu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3007;
}

static void
Opcode_bany_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8007;
}

static void
Opcode_bnone_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7;
}

static void
Opcode_ball_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4007;
}

static void
Opcode_bnall_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc007;
}

static void
Opcode_bbc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5007;
}

static void
Opcode_bbs_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd007;
}

static void
Opcode_beqz_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x16;
}

static void
Opcode_bnez_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x56;
}

static void
Opcode_bgez_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd6;
}

static void
Opcode_bltz_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x96;
}

static void
Opcode_call0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5;
}

static void
Opcode_callx0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc0;
}

static void
Opcode_extui_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x40000;
}

static void
Opcode_ill_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0;
}

static void
Opcode_j_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6;
}

static void
Opcode_jx_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa0;
}

static void
Opcode_l16ui_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1002;
}

static void
Opcode_l16si_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9002;
}

static void
Opcode_l32i_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2002;
}

static void
Opcode_l32r_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1;
}

static void
Opcode_l8ui_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2;
}

static void
Opcode_loop_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8076;
}

static void
Opcode_loopnez_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9076;
}

static void
Opcode_loopgtz_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa076;
}

static void
Opcode_movi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa002;
}

static void
Opcode_moveqz_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x830000;
}

static void
Opcode_movnez_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x930000;
}

static void
Opcode_movltz_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa30000;
}

static void
Opcode_movgez_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb30000;
}

static void
Opcode_neg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x600000;
}

static void
Opcode_abs_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x600100;
}

static void
Opcode_nop_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x20f0;
}

static void
Opcode_ret_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x80;
}

static void
Opcode_s16i_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5002;
}

static void
Opcode_s32i_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6002;
}

static void
Opcode_s8i_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4002;
}

static void
Opcode_ssr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x400000;
}

static void
Opcode_ssl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x401000;
}

static void
Opcode_ssa8l_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x402000;
}

static void
Opcode_ssa8b_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x403000;
}

static void
Opcode_ssai_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x404000;
}

static void
Opcode_sll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa10000;
}

static void
Opcode_src_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x810000;
}

static void
Opcode_srl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x910000;
}

static void
Opcode_sra_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb10000;
}

static void
Opcode_slli_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x10000;
}

static void
Opcode_srai_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x210000;
}

static void
Opcode_srli_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x410000;
}

static void
Opcode_memw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x20c0;
}

static void
Opcode_extw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x20d0;
}

static void
Opcode_isync_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2000;
}

static void
Opcode_rsync_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2010;
}

static void
Opcode_esync_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2020;
}

static void
Opcode_dsync_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2030;
}

static void
Opcode_rsil_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6000;
}

static void
Opcode_rsr_lend_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x30100;
}

static void
Opcode_wsr_lend_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x130100;
}

static void
Opcode_xsr_lend_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x610100;
}

static void
Opcode_rsr_lcount_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x30200;
}

static void
Opcode_wsr_lcount_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x130200;
}

static void
Opcode_xsr_lcount_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x610200;
}

static void
Opcode_rsr_lbeg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x30000;
}

static void
Opcode_wsr_lbeg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x130000;
}

static void
Opcode_xsr_lbeg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x610000;
}

static void
Opcode_rsr_sar_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x30300;
}

static void
Opcode_wsr_sar_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x130300;
}

static void
Opcode_xsr_sar_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x610300;
}

static void
Opcode_rsr_litbase_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x30500;
}

static void
Opcode_wsr_litbase_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x130500;
}

static void
Opcode_xsr_litbase_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x610500;
}

static void
Opcode_rsr_176_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3b000;
}

static void
Opcode_wsr_176_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13b000;
}

static void
Opcode_rsr_208_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3d000;
}

static void
Opcode_rsr_ps_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3e600;
}

static void
Opcode_wsr_ps_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13e600;
}

static void
Opcode_xsr_ps_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61e600;
}

static void
Opcode_rsr_epc1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3b100;
}

static void
Opcode_wsr_epc1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13b100;
}

static void
Opcode_xsr_epc1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61b100;
}

static void
Opcode_rsr_excsave1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3d100;
}

static void
Opcode_wsr_excsave1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13d100;
}

static void
Opcode_xsr_excsave1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61d100;
}

static void
Opcode_rsr_epc2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3b200;
}

static void
Opcode_wsr_epc2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13b200;
}

static void
Opcode_xsr_epc2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61b200;
}

static void
Opcode_rsr_excsave2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3d200;
}

static void
Opcode_wsr_excsave2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13d200;
}

static void
Opcode_xsr_excsave2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61d200;
}

static void
Opcode_rsr_epc3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3b300;
}

static void
Opcode_wsr_epc3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13b300;
}

static void
Opcode_xsr_epc3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61b300;
}

static void
Opcode_rsr_excsave3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3d300;
}

static void
Opcode_wsr_excsave3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13d300;
}

static void
Opcode_xsr_excsave3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61d300;
}

static void
Opcode_rsr_epc4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3b400;
}

static void
Opcode_wsr_epc4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13b400;
}

static void
Opcode_xsr_epc4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61b400;
}

static void
Opcode_rsr_excsave4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3d400;
}

static void
Opcode_wsr_excsave4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13d400;
}

static void
Opcode_xsr_excsave4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61d400;
}

static void
Opcode_rsr_epc5_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3b500;
}

static void
Opcode_wsr_epc5_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13b500;
}

static void
Opcode_xsr_epc5_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61b500;
}

static void
Opcode_rsr_excsave5_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3d500;
}

static void
Opcode_wsr_excsave5_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13d500;
}

static void
Opcode_xsr_excsave5_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61d500;
}

static void
Opcode_rsr_epc6_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3b600;
}

static void
Opcode_wsr_epc6_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13b600;
}

static void
Opcode_xsr_epc6_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61b600;
}

static void
Opcode_rsr_excsave6_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3d600;
}

static void
Opcode_wsr_excsave6_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13d600;
}

static void
Opcode_xsr_excsave6_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61d600;
}

static void
Opcode_rsr_epc7_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3b700;
}

static void
Opcode_wsr_epc7_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13b700;
}

static void
Opcode_xsr_epc7_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61b700;
}

static void
Opcode_rsr_excsave7_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3d700;
}

static void
Opcode_wsr_excsave7_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13d700;
}

static void
Opcode_xsr_excsave7_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61d700;
}

static void
Opcode_rsr_eps2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3c200;
}

static void
Opcode_wsr_eps2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13c200;
}

static void
Opcode_xsr_eps2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61c200;
}

static void
Opcode_rsr_eps3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3c300;
}

static void
Opcode_wsr_eps3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13c300;
}

static void
Opcode_xsr_eps3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61c300;
}

static void
Opcode_rsr_eps4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3c400;
}

static void
Opcode_wsr_eps4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13c400;
}

static void
Opcode_xsr_eps4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61c400;
}

static void
Opcode_rsr_eps5_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3c500;
}

static void
Opcode_wsr_eps5_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13c500;
}

static void
Opcode_xsr_eps5_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61c500;
}

static void
Opcode_rsr_eps6_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3c600;
}

static void
Opcode_wsr_eps6_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13c600;
}

static void
Opcode_xsr_eps6_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61c600;
}

static void
Opcode_rsr_eps7_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3c700;
}

static void
Opcode_wsr_eps7_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13c700;
}

static void
Opcode_xsr_eps7_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61c700;
}

static void
Opcode_rsr_excvaddr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3ee00;
}

static void
Opcode_wsr_excvaddr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13ee00;
}

static void
Opcode_xsr_excvaddr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61ee00;
}

static void
Opcode_rsr_depc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3c000;
}

static void
Opcode_wsr_depc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13c000;
}

static void
Opcode_xsr_depc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61c000;
}

static void
Opcode_rsr_exccause_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3e800;
}

static void
Opcode_wsr_exccause_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13e800;
}

static void
Opcode_xsr_exccause_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61e800;
}

static void
Opcode_rsr_misc0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3f400;
}

static void
Opcode_wsr_misc0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13f400;
}

static void
Opcode_xsr_misc0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61f400;
}

static void
Opcode_rsr_misc1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3f500;
}

static void
Opcode_wsr_misc1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13f500;
}

static void
Opcode_xsr_misc1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61f500;
}

static void
Opcode_rsr_prid_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3eb00;
}

static void
Opcode_rsr_vecbase_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3e700;
}

static void
Opcode_wsr_vecbase_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13e700;
}

static void
Opcode_xsr_vecbase_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61e700;
}

static void
Opcode_mul16u_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc10000;
}

static void
Opcode_mul16s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd10000;
}

static void
Opcode_mull_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x820000;
}

static void
Opcode_mul_aa_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x740004;
}

static void
Opcode_mul_aa_hl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x750004;
}

static void
Opcode_mul_aa_lh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x760004;
}

static void
Opcode_mul_aa_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x770004;
}

static void
Opcode_umul_aa_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x700004;
}

static void
Opcode_umul_aa_hl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x710004;
}

static void
Opcode_umul_aa_lh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x720004;
}

static void
Opcode_umul_aa_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x730004;
}

static void
Opcode_mul_ad_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x340004;
}

static void
Opcode_mul_ad_hl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x350004;
}

static void
Opcode_mul_ad_lh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x360004;
}

static void
Opcode_mul_ad_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x370004;
}

static void
Opcode_mul_da_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x640004;
}

static void
Opcode_mul_da_hl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x650004;
}

static void
Opcode_mul_da_lh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x660004;
}

static void
Opcode_mul_da_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x670004;
}

static void
Opcode_mul_dd_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x240004;
}

static void
Opcode_mul_dd_hl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x250004;
}

static void
Opcode_mul_dd_lh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x260004;
}

static void
Opcode_mul_dd_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x270004;
}

static void
Opcode_mula_aa_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x780004;
}

static void
Opcode_mula_aa_hl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x790004;
}

static void
Opcode_mula_aa_lh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7a0004;
}

static void
Opcode_mula_aa_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7b0004;
}

static void
Opcode_muls_aa_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7c0004;
}

static void
Opcode_muls_aa_hl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7d0004;
}

static void
Opcode_muls_aa_lh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7e0004;
}

static void
Opcode_muls_aa_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7f0004;
}

static void
Opcode_mula_ad_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x380004;
}

static void
Opcode_mula_ad_hl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x390004;
}

static void
Opcode_mula_ad_lh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3a0004;
}

static void
Opcode_mula_ad_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3b0004;
}

static void
Opcode_muls_ad_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3c0004;
}

static void
Opcode_muls_ad_hl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3d0004;
}

static void
Opcode_muls_ad_lh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3e0004;
}

static void
Opcode_muls_ad_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3f0004;
}

static void
Opcode_mula_da_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x680004;
}

static void
Opcode_mula_da_hl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x690004;
}

static void
Opcode_mula_da_lh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6a0004;
}

static void
Opcode_mula_da_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6b0004;
}

static void
Opcode_muls_da_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6c0004;
}

static void
Opcode_muls_da_hl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6d0004;
}

static void
Opcode_muls_da_lh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6e0004;
}

static void
Opcode_muls_da_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6f0004;
}

static void
Opcode_mula_dd_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x280004;
}

static void
Opcode_mula_dd_hl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x290004;
}

static void
Opcode_mula_dd_lh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2a0004;
}

static void
Opcode_mula_dd_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2b0004;
}

static void
Opcode_muls_dd_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2c0004;
}

static void
Opcode_muls_dd_hl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2d0004;
}

static void
Opcode_muls_dd_lh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2e0004;
}

static void
Opcode_muls_dd_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2f0004;
}

static void
Opcode_mula_da_ll_lddec_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x580004;
}

static void
Opcode_mula_da_ll_ldinc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x480004;
}

static void
Opcode_mula_da_hl_lddec_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x590004;
}

static void
Opcode_mula_da_hl_ldinc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x490004;
}

static void
Opcode_mula_da_lh_lddec_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5a0004;
}

static void
Opcode_mula_da_lh_ldinc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4a0004;
}

static void
Opcode_mula_da_hh_lddec_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5b0004;
}

static void
Opcode_mula_da_hh_ldinc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4b0004;
}

static void
Opcode_mula_dd_ll_lddec_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x180004;
}

static void
Opcode_mula_dd_ll_ldinc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x80004;
}

static void
Opcode_mula_dd_hl_lddec_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x190004;
}

static void
Opcode_mula_dd_hl_ldinc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x90004;
}

static void
Opcode_mula_dd_lh_lddec_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1a0004;
}

static void
Opcode_mula_dd_lh_ldinc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa0004;
}

static void
Opcode_mula_dd_hh_lddec_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1b0004;
}

static void
Opcode_mula_dd_hh_ldinc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb0004;
}

static void
Opcode_lddec_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x900004;
}

static void
Opcode_ldinc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x800004;
}

static void
Opcode_rsr_m0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x32000;
}

static void
Opcode_wsr_m0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x132000;
}

static void
Opcode_xsr_m0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x612000;
}

static void
Opcode_rsr_m1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x32100;
}

static void
Opcode_wsr_m1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x132100;
}

static void
Opcode_xsr_m1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x612100;
}

static void
Opcode_rsr_m2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x32200;
}

static void
Opcode_wsr_m2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x132200;
}

static void
Opcode_xsr_m2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x612200;
}

static void
Opcode_rsr_m3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x32300;
}

static void
Opcode_wsr_m3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x132300;
}

static void
Opcode_xsr_m3_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x612300;
}

static void
Opcode_rsr_acclo_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x31000;
}

static void
Opcode_wsr_acclo_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x131000;
}

static void
Opcode_xsr_acclo_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x611000;
}

static void
Opcode_rsr_acchi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x31100;
}

static void
Opcode_wsr_acchi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x131100;
}

static void
Opcode_xsr_acchi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x611100;
}

static void
Opcode_rfi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3010;
}

static void
Opcode_waiti_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7000;
}

static void
Opcode_rsr_interrupt_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3e200;
}

static void
Opcode_wsr_intset_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13e200;
}

static void
Opcode_wsr_intclear_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13e300;
}

static void
Opcode_rsr_intenable_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3e400;
}

static void
Opcode_wsr_intenable_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13e400;
}

static void
Opcode_xsr_intenable_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61e400;
}

static void
Opcode_break_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4000;
}

static void
Opcode_break_n_Slot_inst16b_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf02d;
}

static void
Opcode_rsr_dbreaka0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x39000;
}

static void
Opcode_wsr_dbreaka0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x139000;
}

static void
Opcode_xsr_dbreaka0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x619000;
}

static void
Opcode_rsr_dbreakc0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3a000;
}

static void
Opcode_wsr_dbreakc0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13a000;
}

static void
Opcode_xsr_dbreakc0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61a000;
}

static void
Opcode_rsr_dbreaka1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x39100;
}

static void
Opcode_wsr_dbreaka1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x139100;
}

static void
Opcode_xsr_dbreaka1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x619100;
}

static void
Opcode_rsr_dbreakc1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3a100;
}

static void
Opcode_wsr_dbreakc1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13a100;
}

static void
Opcode_xsr_dbreakc1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61a100;
}

static void
Opcode_rsr_ibreaka0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x38000;
}

static void
Opcode_wsr_ibreaka0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x138000;
}

static void
Opcode_xsr_ibreaka0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x618000;
}

static void
Opcode_rsr_ibreaka1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x38100;
}

static void
Opcode_wsr_ibreaka1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x138100;
}

static void
Opcode_xsr_ibreaka1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x618100;
}

static void
Opcode_rsr_ibreakenable_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x36000;
}

static void
Opcode_wsr_ibreakenable_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x136000;
}

static void
Opcode_xsr_ibreakenable_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x616000;
}

static void
Opcode_rsr_debugcause_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3e900;
}

static void
Opcode_wsr_debugcause_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13e900;
}

static void
Opcode_xsr_debugcause_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61e900;
}

static void
Opcode_rsr_icount_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3ec00;
}

static void
Opcode_wsr_icount_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13ec00;
}

static void
Opcode_xsr_icount_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61ec00;
}

static void
Opcode_rsr_icountlevel_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3ed00;
}

static void
Opcode_wsr_icountlevel_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13ed00;
}

static void
Opcode_xsr_icountlevel_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61ed00;
}

static void
Opcode_rsr_ddr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x36800;
}

static void
Opcode_wsr_ddr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x136800;
}

static void
Opcode_xsr_ddr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x616800;
}

static void
Opcode_rfdo_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf1e000;
}

static void
Opcode_rfdd_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf1e010;
}

static void
Opcode_wsr_mmid_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x135900;
}

static void
Opcode_rsr_ccount_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3ea00;
}

static void
Opcode_wsr_ccount_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13ea00;
}

static void
Opcode_xsr_ccount_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61ea00;
}

static void
Opcode_rsr_ccompare0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3f000;
}

static void
Opcode_wsr_ccompare0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13f000;
}

static void
Opcode_xsr_ccompare0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61f000;
}

static void
Opcode_rsr_ccompare1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3f100;
}

static void
Opcode_wsr_ccompare1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13f100;
}

static void
Opcode_xsr_ccompare1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61f100;
}

static void
Opcode_rsr_ccompare2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3f200;
}

static void
Opcode_wsr_ccompare2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13f200;
}

static void
Opcode_xsr_ccompare2_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61f200;
}

static void
Opcode_ipf_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x70c2;
}

static void
Opcode_ihi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x70e2;
}

static void
Opcode_ipfl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x70d2;
}

static void
Opcode_ihu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x270d2;
}

static void
Opcode_iiu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x370d2;
}

static void
Opcode_iii_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x70f2;
}

static void
Opcode_lict_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf10000;
}

static void
Opcode_licw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf12000;
}

static void
Opcode_sict_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf11000;
}

static void
Opcode_sicw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf13000;
}

static void
Opcode_dhwb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7042;
}

static void
Opcode_dhwbi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7052;
}

static void
Opcode_diwb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x47082;
}

static void
Opcode_diwbi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x57082;
}

static void
Opcode_dhi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7062;
}

static void
Opcode_dii_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7072;
}

static void
Opcode_dpfr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7002;
}

static void
Opcode_dpfw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7012;
}

static void
Opcode_dpfro_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7022;
}

static void
Opcode_dpfwo_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7032;
}

static void
Opcode_dpfl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7082;
}

static void
Opcode_dhu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x27082;
}

static void
Opcode_diu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x37082;
}

static void
Opcode_sdct_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf19000;
}

static void
Opcode_ldct_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf18000;
}

static void
Opcode_wsr_ptevaddr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x135300;
}

static void
Opcode_rsr_ptevaddr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x35300;
}

static void
Opcode_xsr_ptevaddr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x615300;
}

static void
Opcode_rsr_rasid_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x35a00;
}

static void
Opcode_wsr_rasid_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x135a00;
}

static void
Opcode_xsr_rasid_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x615a00;
}

static void
Opcode_rsr_itlbcfg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x35b00;
}

static void
Opcode_wsr_itlbcfg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x135b00;
}

static void
Opcode_xsr_itlbcfg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x615b00;
}

static void
Opcode_rsr_dtlbcfg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x35c00;
}

static void
Opcode_wsr_dtlbcfg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x135c00;
}

static void
Opcode_xsr_dtlbcfg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x615c00;
}

static void
Opcode_idtlb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x50c000;
}

static void
Opcode_pdtlb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x50d000;
}

static void
Opcode_rdtlb0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x50b000;
}

static void
Opcode_rdtlb1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x50f000;
}

static void
Opcode_wdtlb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x50e000;
}

static void
Opcode_iitlb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x504000;
}

static void
Opcode_pitlb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x505000;
}

static void
Opcode_ritlb0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x503000;
}

static void
Opcode_ritlb1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x507000;
}

static void
Opcode_witlb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x506000;
}

static void
Opcode_ldpte_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf1f000;
}

static void
Opcode_hwwitlba_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x501000;
}

static void
Opcode_hwwdtlba_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x509000;
}

static void
Opcode_rsr_cpenable_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3e000;
}

static void
Opcode_wsr_cpenable_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13e000;
}

static void
Opcode_xsr_cpenable_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x61e000;
}

static void
Opcode_clamps_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x330000;
}

static void
Opcode_min_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x430000;
}

static void
Opcode_max_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x530000;
}

static void
Opcode_minu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x630000;
}

static void
Opcode_maxu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x730000;
}

static void
Opcode_nsa_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x40e000;
}

static void
Opcode_nsau_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x40f000;
}

static void
Opcode_sext_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x230000;
}

static void
Opcode_l32ai_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb002;
}

static void
Opcode_s32ri_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf002;
}

static void
Opcode_s32c1i_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe002;
}

static void
Opcode_rsr_scompare1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x30c00;
}

static void
Opcode_wsr_scompare1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x130c00;
}

static void
Opcode_xsr_scompare1_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x610c00;
}

static void
Opcode_rsr_atomctl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x36300;
}

static void
Opcode_wsr_atomctl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x136300;
}

static void
Opcode_xsr_atomctl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x616300;
}

static void
Opcode_quou_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc20000;
}

static void
Opcode_quos_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd20000;
}

static void
Opcode_remu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe20000;
}

static void
Opcode_rems_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf20000;
}

static void
Opcode_rer_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x406000;
}

static void
Opcode_wer_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x407000;
}

static void
Opcode_rur_expstate_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe30e60;
}

static void
Opcode_wur_expstate_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf3e600;
}

static void
Opcode_read_impwire_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe0000;
}

static void
Opcode_setb_expstate_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe1000;
}

static void
Opcode_clrb_expstate_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe1200;
}

static void
Opcode_wrmsk_expstate_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe2000;
}

static xtensa_opcode_encode_fn Opcode_excw_encode_fns[] = {
  Opcode_excw_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rfe_encode_fns[] = {
  Opcode_rfe_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rfde_encode_fns[] = {
  Opcode_rfde_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_syscall_encode_fns[] = {
  Opcode_syscall_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_simcall_encode_fns[] = {
  Opcode_simcall_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_call12_encode_fns[] = {
  Opcode_call12_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_call8_encode_fns[] = {
  Opcode_call8_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_call4_encode_fns[] = {
  Opcode_call4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_callx12_encode_fns[] = {
  Opcode_callx12_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_callx8_encode_fns[] = {
  Opcode_callx8_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_callx4_encode_fns[] = {
  Opcode_callx4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_entry_encode_fns[] = {
  Opcode_entry_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movsp_encode_fns[] = {
  Opcode_movsp_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rotw_encode_fns[] = {
  Opcode_rotw_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_retw_encode_fns[] = {
  Opcode_retw_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_retw_n_encode_fns[] = {
  0, 0, Opcode_retw_n_Slot_inst16b_encode
};

static xtensa_opcode_encode_fn Opcode_rfwo_encode_fns[] = {
  Opcode_rfwo_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rfwu_encode_fns[] = {
  Opcode_rfwu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_l32e_encode_fns[] = {
  Opcode_l32e_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_s32e_encode_fns[] = {
  Opcode_s32e_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_windowbase_encode_fns[] = {
  Opcode_rsr_windowbase_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_windowbase_encode_fns[] = {
  Opcode_wsr_windowbase_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_windowbase_encode_fns[] = {
  Opcode_xsr_windowbase_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_windowstart_encode_fns[] = {
  Opcode_rsr_windowstart_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_windowstart_encode_fns[] = {
  Opcode_wsr_windowstart_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_windowstart_encode_fns[] = {
  Opcode_xsr_windowstart_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_add_n_encode_fns[] = {
  0, Opcode_add_n_Slot_inst16a_encode, 0
};

static xtensa_opcode_encode_fn Opcode_addi_n_encode_fns[] = {
  0, Opcode_addi_n_Slot_inst16a_encode, 0
};

static xtensa_opcode_encode_fn Opcode_beqz_n_encode_fns[] = {
  0, 0, Opcode_beqz_n_Slot_inst16b_encode
};

static xtensa_opcode_encode_fn Opcode_bnez_n_encode_fns[] = {
  0, 0, Opcode_bnez_n_Slot_inst16b_encode
};

static xtensa_opcode_encode_fn Opcode_ill_n_encode_fns[] = {
  0, 0, Opcode_ill_n_Slot_inst16b_encode
};

static xtensa_opcode_encode_fn Opcode_l32i_n_encode_fns[] = {
  0, Opcode_l32i_n_Slot_inst16a_encode, 0
};

static xtensa_opcode_encode_fn Opcode_mov_n_encode_fns[] = {
  0, 0, Opcode_mov_n_Slot_inst16b_encode
};

static xtensa_opcode_encode_fn Opcode_movi_n_encode_fns[] = {
  0, 0, Opcode_movi_n_Slot_inst16b_encode
};

static xtensa_opcode_encode_fn Opcode_nop_n_encode_fns[] = {
  0, 0, Opcode_nop_n_Slot_inst16b_encode
};

static xtensa_opcode_encode_fn Opcode_ret_n_encode_fns[] = {
  0, 0, Opcode_ret_n_Slot_inst16b_encode
};

static xtensa_opcode_encode_fn Opcode_s32i_n_encode_fns[] = {
  0, Opcode_s32i_n_Slot_inst16a_encode, 0
};

static xtensa_opcode_encode_fn Opcode_rur_threadptr_encode_fns[] = {
  Opcode_rur_threadptr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wur_threadptr_encode_fns[] = {
  Opcode_wur_threadptr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_addi_encode_fns[] = {
  Opcode_addi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_addmi_encode_fns[] = {
  Opcode_addmi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_add_encode_fns[] = {
  Opcode_add_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sub_encode_fns[] = {
  Opcode_sub_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_addx2_encode_fns[] = {
  Opcode_addx2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_addx4_encode_fns[] = {
  Opcode_addx4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_addx8_encode_fns[] = {
  Opcode_addx8_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_subx2_encode_fns[] = {
  Opcode_subx2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_subx4_encode_fns[] = {
  Opcode_subx4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_subx8_encode_fns[] = {
  Opcode_subx8_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_and_encode_fns[] = {
  Opcode_and_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_or_encode_fns[] = {
  Opcode_or_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xor_encode_fns[] = {
  Opcode_xor_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_beqi_encode_fns[] = {
  Opcode_beqi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bnei_encode_fns[] = {
  Opcode_bnei_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bgei_encode_fns[] = {
  Opcode_bgei_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_blti_encode_fns[] = {
  Opcode_blti_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bbci_encode_fns[] = {
  Opcode_bbci_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bbsi_encode_fns[] = {
  Opcode_bbsi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bgeui_encode_fns[] = {
  Opcode_bgeui_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bltui_encode_fns[] = {
  Opcode_bltui_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_beq_encode_fns[] = {
  Opcode_beq_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bne_encode_fns[] = {
  Opcode_bne_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bge_encode_fns[] = {
  Opcode_bge_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_blt_encode_fns[] = {
  Opcode_blt_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bgeu_encode_fns[] = {
  Opcode_bgeu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bltu_encode_fns[] = {
  Opcode_bltu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bany_encode_fns[] = {
  Opcode_bany_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bnone_encode_fns[] = {
  Opcode_bnone_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ball_encode_fns[] = {
  Opcode_ball_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bnall_encode_fns[] = {
  Opcode_bnall_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bbc_encode_fns[] = {
  Opcode_bbc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bbs_encode_fns[] = {
  Opcode_bbs_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_beqz_encode_fns[] = {
  Opcode_beqz_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bnez_encode_fns[] = {
  Opcode_bnez_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bgez_encode_fns[] = {
  Opcode_bgez_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bltz_encode_fns[] = {
  Opcode_bltz_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_call0_encode_fns[] = {
  Opcode_call0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_callx0_encode_fns[] = {
  Opcode_callx0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_extui_encode_fns[] = {
  Opcode_extui_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ill_encode_fns[] = {
  Opcode_ill_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_j_encode_fns[] = {
  Opcode_j_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_jx_encode_fns[] = {
  Opcode_jx_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_l16ui_encode_fns[] = {
  Opcode_l16ui_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_l16si_encode_fns[] = {
  Opcode_l16si_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_l32i_encode_fns[] = {
  Opcode_l32i_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_l32r_encode_fns[] = {
  Opcode_l32r_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_l8ui_encode_fns[] = {
  Opcode_l8ui_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_loop_encode_fns[] = {
  Opcode_loop_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_loopnez_encode_fns[] = {
  Opcode_loopnez_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_loopgtz_encode_fns[] = {
  Opcode_loopgtz_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movi_encode_fns[] = {
  Opcode_movi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_moveqz_encode_fns[] = {
  Opcode_moveqz_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movnez_encode_fns[] = {
  Opcode_movnez_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movltz_encode_fns[] = {
  Opcode_movltz_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movgez_encode_fns[] = {
  Opcode_movgez_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_neg_encode_fns[] = {
  Opcode_neg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_abs_encode_fns[] = {
  Opcode_abs_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_nop_encode_fns[] = {
  Opcode_nop_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ret_encode_fns[] = {
  Opcode_ret_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_s16i_encode_fns[] = {
  Opcode_s16i_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_s32i_encode_fns[] = {
  Opcode_s32i_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_s8i_encode_fns[] = {
  Opcode_s8i_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ssr_encode_fns[] = {
  Opcode_ssr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ssl_encode_fns[] = {
  Opcode_ssl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ssa8l_encode_fns[] = {
  Opcode_ssa8l_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ssa8b_encode_fns[] = {
  Opcode_ssa8b_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ssai_encode_fns[] = {
  Opcode_ssai_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sll_encode_fns[] = {
  Opcode_sll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_src_encode_fns[] = {
  Opcode_src_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_srl_encode_fns[] = {
  Opcode_srl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sra_encode_fns[] = {
  Opcode_sra_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_slli_encode_fns[] = {
  Opcode_slli_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_srai_encode_fns[] = {
  Opcode_srai_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_srli_encode_fns[] = {
  Opcode_srli_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_memw_encode_fns[] = {
  Opcode_memw_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_extw_encode_fns[] = {
  Opcode_extw_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_isync_encode_fns[] = {
  Opcode_isync_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsync_encode_fns[] = {
  Opcode_rsync_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_esync_encode_fns[] = {
  Opcode_esync_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dsync_encode_fns[] = {
  Opcode_dsync_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsil_encode_fns[] = {
  Opcode_rsil_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_lend_encode_fns[] = {
  Opcode_rsr_lend_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_lend_encode_fns[] = {
  Opcode_wsr_lend_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_lend_encode_fns[] = {
  Opcode_xsr_lend_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_lcount_encode_fns[] = {
  Opcode_rsr_lcount_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_lcount_encode_fns[] = {
  Opcode_wsr_lcount_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_lcount_encode_fns[] = {
  Opcode_xsr_lcount_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_lbeg_encode_fns[] = {
  Opcode_rsr_lbeg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_lbeg_encode_fns[] = {
  Opcode_wsr_lbeg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_lbeg_encode_fns[] = {
  Opcode_xsr_lbeg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_sar_encode_fns[] = {
  Opcode_rsr_sar_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_sar_encode_fns[] = {
  Opcode_wsr_sar_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_sar_encode_fns[] = {
  Opcode_xsr_sar_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_litbase_encode_fns[] = {
  Opcode_rsr_litbase_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_litbase_encode_fns[] = {
  Opcode_wsr_litbase_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_litbase_encode_fns[] = {
  Opcode_xsr_litbase_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_176_encode_fns[] = {
  Opcode_rsr_176_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_176_encode_fns[] = {
  Opcode_wsr_176_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_208_encode_fns[] = {
  Opcode_rsr_208_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ps_encode_fns[] = {
  Opcode_rsr_ps_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ps_encode_fns[] = {
  Opcode_wsr_ps_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ps_encode_fns[] = {
  Opcode_xsr_ps_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_epc1_encode_fns[] = {
  Opcode_rsr_epc1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_epc1_encode_fns[] = {
  Opcode_wsr_epc1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_epc1_encode_fns[] = {
  Opcode_xsr_epc1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_excsave1_encode_fns[] = {
  Opcode_rsr_excsave1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_excsave1_encode_fns[] = {
  Opcode_wsr_excsave1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_excsave1_encode_fns[] = {
  Opcode_xsr_excsave1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_epc2_encode_fns[] = {
  Opcode_rsr_epc2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_epc2_encode_fns[] = {
  Opcode_wsr_epc2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_epc2_encode_fns[] = {
  Opcode_xsr_epc2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_excsave2_encode_fns[] = {
  Opcode_rsr_excsave2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_excsave2_encode_fns[] = {
  Opcode_wsr_excsave2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_excsave2_encode_fns[] = {
  Opcode_xsr_excsave2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_epc3_encode_fns[] = {
  Opcode_rsr_epc3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_epc3_encode_fns[] = {
  Opcode_wsr_epc3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_epc3_encode_fns[] = {
  Opcode_xsr_epc3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_excsave3_encode_fns[] = {
  Opcode_rsr_excsave3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_excsave3_encode_fns[] = {
  Opcode_wsr_excsave3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_excsave3_encode_fns[] = {
  Opcode_xsr_excsave3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_epc4_encode_fns[] = {
  Opcode_rsr_epc4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_epc4_encode_fns[] = {
  Opcode_wsr_epc4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_epc4_encode_fns[] = {
  Opcode_xsr_epc4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_excsave4_encode_fns[] = {
  Opcode_rsr_excsave4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_excsave4_encode_fns[] = {
  Opcode_wsr_excsave4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_excsave4_encode_fns[] = {
  Opcode_xsr_excsave4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_epc5_encode_fns[] = {
  Opcode_rsr_epc5_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_epc5_encode_fns[] = {
  Opcode_wsr_epc5_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_epc5_encode_fns[] = {
  Opcode_xsr_epc5_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_excsave5_encode_fns[] = {
  Opcode_rsr_excsave5_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_excsave5_encode_fns[] = {
  Opcode_wsr_excsave5_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_excsave5_encode_fns[] = {
  Opcode_xsr_excsave5_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_epc6_encode_fns[] = {
  Opcode_rsr_epc6_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_epc6_encode_fns[] = {
  Opcode_wsr_epc6_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_epc6_encode_fns[] = {
  Opcode_xsr_epc6_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_excsave6_encode_fns[] = {
  Opcode_rsr_excsave6_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_excsave6_encode_fns[] = {
  Opcode_wsr_excsave6_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_excsave6_encode_fns[] = {
  Opcode_xsr_excsave6_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_epc7_encode_fns[] = {
  Opcode_rsr_epc7_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_epc7_encode_fns[] = {
  Opcode_wsr_epc7_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_epc7_encode_fns[] = {
  Opcode_xsr_epc7_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_excsave7_encode_fns[] = {
  Opcode_rsr_excsave7_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_excsave7_encode_fns[] = {
  Opcode_wsr_excsave7_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_excsave7_encode_fns[] = {
  Opcode_xsr_excsave7_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_eps2_encode_fns[] = {
  Opcode_rsr_eps2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_eps2_encode_fns[] = {
  Opcode_wsr_eps2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_eps2_encode_fns[] = {
  Opcode_xsr_eps2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_eps3_encode_fns[] = {
  Opcode_rsr_eps3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_eps3_encode_fns[] = {
  Opcode_wsr_eps3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_eps3_encode_fns[] = {
  Opcode_xsr_eps3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_eps4_encode_fns[] = {
  Opcode_rsr_eps4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_eps4_encode_fns[] = {
  Opcode_wsr_eps4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_eps4_encode_fns[] = {
  Opcode_xsr_eps4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_eps5_encode_fns[] = {
  Opcode_rsr_eps5_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_eps5_encode_fns[] = {
  Opcode_wsr_eps5_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_eps5_encode_fns[] = {
  Opcode_xsr_eps5_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_eps6_encode_fns[] = {
  Opcode_rsr_eps6_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_eps6_encode_fns[] = {
  Opcode_wsr_eps6_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_eps6_encode_fns[] = {
  Opcode_xsr_eps6_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_eps7_encode_fns[] = {
  Opcode_rsr_eps7_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_eps7_encode_fns[] = {
  Opcode_wsr_eps7_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_eps7_encode_fns[] = {
  Opcode_xsr_eps7_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_excvaddr_encode_fns[] = {
  Opcode_rsr_excvaddr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_excvaddr_encode_fns[] = {
  Opcode_wsr_excvaddr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_excvaddr_encode_fns[] = {
  Opcode_xsr_excvaddr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_depc_encode_fns[] = {
  Opcode_rsr_depc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_depc_encode_fns[] = {
  Opcode_wsr_depc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_depc_encode_fns[] = {
  Opcode_xsr_depc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_exccause_encode_fns[] = {
  Opcode_rsr_exccause_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_exccause_encode_fns[] = {
  Opcode_wsr_exccause_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_exccause_encode_fns[] = {
  Opcode_xsr_exccause_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_misc0_encode_fns[] = {
  Opcode_rsr_misc0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_misc0_encode_fns[] = {
  Opcode_wsr_misc0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_misc0_encode_fns[] = {
  Opcode_xsr_misc0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_misc1_encode_fns[] = {
  Opcode_rsr_misc1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_misc1_encode_fns[] = {
  Opcode_wsr_misc1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_misc1_encode_fns[] = {
  Opcode_xsr_misc1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_prid_encode_fns[] = {
  Opcode_rsr_prid_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_vecbase_encode_fns[] = {
  Opcode_rsr_vecbase_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_vecbase_encode_fns[] = {
  Opcode_wsr_vecbase_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_vecbase_encode_fns[] = {
  Opcode_xsr_vecbase_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul16u_encode_fns[] = {
  Opcode_mul16u_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul16s_encode_fns[] = {
  Opcode_mul16s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mull_encode_fns[] = {
  Opcode_mull_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_aa_ll_encode_fns[] = {
  Opcode_mul_aa_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_aa_hl_encode_fns[] = {
  Opcode_mul_aa_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_aa_lh_encode_fns[] = {
  Opcode_mul_aa_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_aa_hh_encode_fns[] = {
  Opcode_mul_aa_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_umul_aa_ll_encode_fns[] = {
  Opcode_umul_aa_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_umul_aa_hl_encode_fns[] = {
  Opcode_umul_aa_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_umul_aa_lh_encode_fns[] = {
  Opcode_umul_aa_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_umul_aa_hh_encode_fns[] = {
  Opcode_umul_aa_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_ad_ll_encode_fns[] = {
  Opcode_mul_ad_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_ad_hl_encode_fns[] = {
  Opcode_mul_ad_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_ad_lh_encode_fns[] = {
  Opcode_mul_ad_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_ad_hh_encode_fns[] = {
  Opcode_mul_ad_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_da_ll_encode_fns[] = {
  Opcode_mul_da_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_da_hl_encode_fns[] = {
  Opcode_mul_da_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_da_lh_encode_fns[] = {
  Opcode_mul_da_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_da_hh_encode_fns[] = {
  Opcode_mul_da_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_dd_ll_encode_fns[] = {
  Opcode_mul_dd_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_dd_hl_encode_fns[] = {
  Opcode_mul_dd_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_dd_lh_encode_fns[] = {
  Opcode_mul_dd_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_dd_hh_encode_fns[] = {
  Opcode_mul_dd_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_aa_ll_encode_fns[] = {
  Opcode_mula_aa_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_aa_hl_encode_fns[] = {
  Opcode_mula_aa_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_aa_lh_encode_fns[] = {
  Opcode_mula_aa_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_aa_hh_encode_fns[] = {
  Opcode_mula_aa_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_aa_ll_encode_fns[] = {
  Opcode_muls_aa_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_aa_hl_encode_fns[] = {
  Opcode_muls_aa_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_aa_lh_encode_fns[] = {
  Opcode_muls_aa_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_aa_hh_encode_fns[] = {
  Opcode_muls_aa_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_ad_ll_encode_fns[] = {
  Opcode_mula_ad_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_ad_hl_encode_fns[] = {
  Opcode_mula_ad_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_ad_lh_encode_fns[] = {
  Opcode_mula_ad_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_ad_hh_encode_fns[] = {
  Opcode_mula_ad_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_ad_ll_encode_fns[] = {
  Opcode_muls_ad_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_ad_hl_encode_fns[] = {
  Opcode_muls_ad_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_ad_lh_encode_fns[] = {
  Opcode_muls_ad_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_ad_hh_encode_fns[] = {
  Opcode_muls_ad_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_ll_encode_fns[] = {
  Opcode_mula_da_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_hl_encode_fns[] = {
  Opcode_mula_da_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_lh_encode_fns[] = {
  Opcode_mula_da_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_hh_encode_fns[] = {
  Opcode_mula_da_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_da_ll_encode_fns[] = {
  Opcode_muls_da_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_da_hl_encode_fns[] = {
  Opcode_muls_da_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_da_lh_encode_fns[] = {
  Opcode_muls_da_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_da_hh_encode_fns[] = {
  Opcode_muls_da_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_ll_encode_fns[] = {
  Opcode_mula_dd_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_hl_encode_fns[] = {
  Opcode_mula_dd_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_lh_encode_fns[] = {
  Opcode_mula_dd_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_hh_encode_fns[] = {
  Opcode_mula_dd_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_dd_ll_encode_fns[] = {
  Opcode_muls_dd_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_dd_hl_encode_fns[] = {
  Opcode_muls_dd_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_dd_lh_encode_fns[] = {
  Opcode_muls_dd_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_dd_hh_encode_fns[] = {
  Opcode_muls_dd_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_ll_lddec_encode_fns[] = {
  Opcode_mula_da_ll_lddec_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_ll_ldinc_encode_fns[] = {
  Opcode_mula_da_ll_ldinc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_hl_lddec_encode_fns[] = {
  Opcode_mula_da_hl_lddec_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_hl_ldinc_encode_fns[] = {
  Opcode_mula_da_hl_ldinc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_lh_lddec_encode_fns[] = {
  Opcode_mula_da_lh_lddec_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_lh_ldinc_encode_fns[] = {
  Opcode_mula_da_lh_ldinc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_hh_lddec_encode_fns[] = {
  Opcode_mula_da_hh_lddec_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_hh_ldinc_encode_fns[] = {
  Opcode_mula_da_hh_ldinc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_ll_lddec_encode_fns[] = {
  Opcode_mula_dd_ll_lddec_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_ll_ldinc_encode_fns[] = {
  Opcode_mula_dd_ll_ldinc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_hl_lddec_encode_fns[] = {
  Opcode_mula_dd_hl_lddec_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_hl_ldinc_encode_fns[] = {
  Opcode_mula_dd_hl_ldinc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_lh_lddec_encode_fns[] = {
  Opcode_mula_dd_lh_lddec_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_lh_ldinc_encode_fns[] = {
  Opcode_mula_dd_lh_ldinc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_hh_lddec_encode_fns[] = {
  Opcode_mula_dd_hh_lddec_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_hh_ldinc_encode_fns[] = {
  Opcode_mula_dd_hh_ldinc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_lddec_encode_fns[] = {
  Opcode_lddec_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ldinc_encode_fns[] = {
  Opcode_ldinc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_m0_encode_fns[] = {
  Opcode_rsr_m0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_m0_encode_fns[] = {
  Opcode_wsr_m0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_m0_encode_fns[] = {
  Opcode_xsr_m0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_m1_encode_fns[] = {
  Opcode_rsr_m1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_m1_encode_fns[] = {
  Opcode_wsr_m1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_m1_encode_fns[] = {
  Opcode_xsr_m1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_m2_encode_fns[] = {
  Opcode_rsr_m2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_m2_encode_fns[] = {
  Opcode_wsr_m2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_m2_encode_fns[] = {
  Opcode_xsr_m2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_m3_encode_fns[] = {
  Opcode_rsr_m3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_m3_encode_fns[] = {
  Opcode_wsr_m3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_m3_encode_fns[] = {
  Opcode_xsr_m3_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_acclo_encode_fns[] = {
  Opcode_rsr_acclo_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_acclo_encode_fns[] = {
  Opcode_wsr_acclo_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_acclo_encode_fns[] = {
  Opcode_xsr_acclo_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_acchi_encode_fns[] = {
  Opcode_rsr_acchi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_acchi_encode_fns[] = {
  Opcode_wsr_acchi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_acchi_encode_fns[] = {
  Opcode_xsr_acchi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rfi_encode_fns[] = {
  Opcode_rfi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_waiti_encode_fns[] = {
  Opcode_waiti_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_interrupt_encode_fns[] = {
  Opcode_rsr_interrupt_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_intset_encode_fns[] = {
  Opcode_wsr_intset_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_intclear_encode_fns[] = {
  Opcode_wsr_intclear_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_intenable_encode_fns[] = {
  Opcode_rsr_intenable_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_intenable_encode_fns[] = {
  Opcode_wsr_intenable_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_intenable_encode_fns[] = {
  Opcode_xsr_intenable_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_break_encode_fns[] = {
  Opcode_break_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_break_n_encode_fns[] = {
  0, 0, Opcode_break_n_Slot_inst16b_encode
};

static xtensa_opcode_encode_fn Opcode_rsr_dbreaka0_encode_fns[] = {
  Opcode_rsr_dbreaka0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_dbreaka0_encode_fns[] = {
  Opcode_wsr_dbreaka0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_dbreaka0_encode_fns[] = {
  Opcode_xsr_dbreaka0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_dbreakc0_encode_fns[] = {
  Opcode_rsr_dbreakc0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_dbreakc0_encode_fns[] = {
  Opcode_wsr_dbreakc0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_dbreakc0_encode_fns[] = {
  Opcode_xsr_dbreakc0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_dbreaka1_encode_fns[] = {
  Opcode_rsr_dbreaka1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_dbreaka1_encode_fns[] = {
  Opcode_wsr_dbreaka1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_dbreaka1_encode_fns[] = {
  Opcode_xsr_dbreaka1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_dbreakc1_encode_fns[] = {
  Opcode_rsr_dbreakc1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_dbreakc1_encode_fns[] = {
  Opcode_wsr_dbreakc1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_dbreakc1_encode_fns[] = {
  Opcode_xsr_dbreakc1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ibreaka0_encode_fns[] = {
  Opcode_rsr_ibreaka0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ibreaka0_encode_fns[] = {
  Opcode_wsr_ibreaka0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ibreaka0_encode_fns[] = {
  Opcode_xsr_ibreaka0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ibreaka1_encode_fns[] = {
  Opcode_rsr_ibreaka1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ibreaka1_encode_fns[] = {
  Opcode_wsr_ibreaka1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ibreaka1_encode_fns[] = {
  Opcode_xsr_ibreaka1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ibreakenable_encode_fns[] = {
  Opcode_rsr_ibreakenable_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ibreakenable_encode_fns[] = {
  Opcode_wsr_ibreakenable_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ibreakenable_encode_fns[] = {
  Opcode_xsr_ibreakenable_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_debugcause_encode_fns[] = {
  Opcode_rsr_debugcause_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_debugcause_encode_fns[] = {
  Opcode_wsr_debugcause_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_debugcause_encode_fns[] = {
  Opcode_xsr_debugcause_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_icount_encode_fns[] = {
  Opcode_rsr_icount_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_icount_encode_fns[] = {
  Opcode_wsr_icount_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_icount_encode_fns[] = {
  Opcode_xsr_icount_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_icountlevel_encode_fns[] = {
  Opcode_rsr_icountlevel_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_icountlevel_encode_fns[] = {
  Opcode_wsr_icountlevel_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_icountlevel_encode_fns[] = {
  Opcode_xsr_icountlevel_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ddr_encode_fns[] = {
  Opcode_rsr_ddr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ddr_encode_fns[] = {
  Opcode_wsr_ddr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ddr_encode_fns[] = {
  Opcode_xsr_ddr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rfdo_encode_fns[] = {
  Opcode_rfdo_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rfdd_encode_fns[] = {
  Opcode_rfdd_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_mmid_encode_fns[] = {
  Opcode_wsr_mmid_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ccount_encode_fns[] = {
  Opcode_rsr_ccount_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ccount_encode_fns[] = {
  Opcode_wsr_ccount_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ccount_encode_fns[] = {
  Opcode_xsr_ccount_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ccompare0_encode_fns[] = {
  Opcode_rsr_ccompare0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ccompare0_encode_fns[] = {
  Opcode_wsr_ccompare0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ccompare0_encode_fns[] = {
  Opcode_xsr_ccompare0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ccompare1_encode_fns[] = {
  Opcode_rsr_ccompare1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ccompare1_encode_fns[] = {
  Opcode_wsr_ccompare1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ccompare1_encode_fns[] = {
  Opcode_xsr_ccompare1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ccompare2_encode_fns[] = {
  Opcode_rsr_ccompare2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ccompare2_encode_fns[] = {
  Opcode_wsr_ccompare2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ccompare2_encode_fns[] = {
  Opcode_xsr_ccompare2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ipf_encode_fns[] = {
  Opcode_ipf_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ihi_encode_fns[] = {
  Opcode_ihi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ipfl_encode_fns[] = {
  Opcode_ipfl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ihu_encode_fns[] = {
  Opcode_ihu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_iiu_encode_fns[] = {
  Opcode_iiu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_iii_encode_fns[] = {
  Opcode_iii_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_lict_encode_fns[] = {
  Opcode_lict_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_licw_encode_fns[] = {
  Opcode_licw_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sict_encode_fns[] = {
  Opcode_sict_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sicw_encode_fns[] = {
  Opcode_sicw_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dhwb_encode_fns[] = {
  Opcode_dhwb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dhwbi_encode_fns[] = {
  Opcode_dhwbi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_diwb_encode_fns[] = {
  Opcode_diwb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_diwbi_encode_fns[] = {
  Opcode_diwbi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dhi_encode_fns[] = {
  Opcode_dhi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dii_encode_fns[] = {
  Opcode_dii_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dpfr_encode_fns[] = {
  Opcode_dpfr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dpfw_encode_fns[] = {
  Opcode_dpfw_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dpfro_encode_fns[] = {
  Opcode_dpfro_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dpfwo_encode_fns[] = {
  Opcode_dpfwo_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dpfl_encode_fns[] = {
  Opcode_dpfl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dhu_encode_fns[] = {
  Opcode_dhu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_diu_encode_fns[] = {
  Opcode_diu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sdct_encode_fns[] = {
  Opcode_sdct_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ldct_encode_fns[] = {
  Opcode_ldct_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_ptevaddr_encode_fns[] = {
  Opcode_wsr_ptevaddr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_ptevaddr_encode_fns[] = {
  Opcode_rsr_ptevaddr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_ptevaddr_encode_fns[] = {
  Opcode_xsr_ptevaddr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_rasid_encode_fns[] = {
  Opcode_rsr_rasid_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_rasid_encode_fns[] = {
  Opcode_wsr_rasid_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_rasid_encode_fns[] = {
  Opcode_xsr_rasid_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_itlbcfg_encode_fns[] = {
  Opcode_rsr_itlbcfg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_itlbcfg_encode_fns[] = {
  Opcode_wsr_itlbcfg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_itlbcfg_encode_fns[] = {
  Opcode_xsr_itlbcfg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_dtlbcfg_encode_fns[] = {
  Opcode_rsr_dtlbcfg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_dtlbcfg_encode_fns[] = {
  Opcode_wsr_dtlbcfg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_dtlbcfg_encode_fns[] = {
  Opcode_xsr_dtlbcfg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_idtlb_encode_fns[] = {
  Opcode_idtlb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_pdtlb_encode_fns[] = {
  Opcode_pdtlb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rdtlb0_encode_fns[] = {
  Opcode_rdtlb0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rdtlb1_encode_fns[] = {
  Opcode_rdtlb1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wdtlb_encode_fns[] = {
  Opcode_wdtlb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_iitlb_encode_fns[] = {
  Opcode_iitlb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_pitlb_encode_fns[] = {
  Opcode_pitlb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ritlb0_encode_fns[] = {
  Opcode_ritlb0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ritlb1_encode_fns[] = {
  Opcode_ritlb1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_witlb_encode_fns[] = {
  Opcode_witlb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ldpte_encode_fns[] = {
  Opcode_ldpte_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_hwwitlba_encode_fns[] = {
  Opcode_hwwitlba_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_hwwdtlba_encode_fns[] = {
  Opcode_hwwdtlba_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_cpenable_encode_fns[] = {
  Opcode_rsr_cpenable_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_cpenable_encode_fns[] = {
  Opcode_wsr_cpenable_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_cpenable_encode_fns[] = {
  Opcode_xsr_cpenable_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_clamps_encode_fns[] = {
  Opcode_clamps_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_min_encode_fns[] = {
  Opcode_min_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_max_encode_fns[] = {
  Opcode_max_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_minu_encode_fns[] = {
  Opcode_minu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_maxu_encode_fns[] = {
  Opcode_maxu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_nsa_encode_fns[] = {
  Opcode_nsa_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_nsau_encode_fns[] = {
  Opcode_nsau_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sext_encode_fns[] = {
  Opcode_sext_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_l32ai_encode_fns[] = {
  Opcode_l32ai_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_s32ri_encode_fns[] = {
  Opcode_s32ri_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_s32c1i_encode_fns[] = {
  Opcode_s32c1i_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_scompare1_encode_fns[] = {
  Opcode_rsr_scompare1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_scompare1_encode_fns[] = {
  Opcode_wsr_scompare1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_scompare1_encode_fns[] = {
  Opcode_xsr_scompare1_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_atomctl_encode_fns[] = {
  Opcode_rsr_atomctl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_atomctl_encode_fns[] = {
  Opcode_wsr_atomctl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_atomctl_encode_fns[] = {
  Opcode_xsr_atomctl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_quou_encode_fns[] = {
  Opcode_quou_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_quos_encode_fns[] = {
  Opcode_quos_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_remu_encode_fns[] = {
  Opcode_remu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rems_encode_fns[] = {
  Opcode_rems_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rer_encode_fns[] = {
  Opcode_rer_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wer_encode_fns[] = {
  Opcode_wer_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rur_expstate_encode_fns[] = {
  Opcode_rur_expstate_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wur_expstate_encode_fns[] = {
  Opcode_wur_expstate_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_read_impwire_encode_fns[] = {
  Opcode_read_impwire_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_setb_expstate_encode_fns[] = {
  Opcode_setb_expstate_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_clrb_expstate_encode_fns[] = {
  Opcode_clrb_expstate_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wrmsk_expstate_encode_fns[] = {
  Opcode_wrmsk_expstate_Slot_inst_encode, 0, 0
};


/* Opcode table.  */

static xtensa_opcode_internal opcodes[] = {
  { "excw", ICLASS_xt_iclass_excw,
    0,
    Opcode_excw_encode_fns, 0, 0 },
  { "rfe", ICLASS_xt_iclass_rfe,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_rfe_encode_fns, 0, 0 },
  { "rfde", ICLASS_xt_iclass_rfde,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_rfde_encode_fns, 0, 0 },
  { "syscall", ICLASS_xt_iclass_syscall,
    0,
    Opcode_syscall_encode_fns, 0, 0 },
  { "simcall", ICLASS_xt_iclass_simcall,
    0,
    Opcode_simcall_encode_fns, 0, 0 },
  { "call12", ICLASS_xt_iclass_call12,
    XTENSA_OPCODE_IS_CALL,
    Opcode_call12_encode_fns, 0, 0 },
  { "call8", ICLASS_xt_iclass_call8,
    XTENSA_OPCODE_IS_CALL,
    Opcode_call8_encode_fns, 0, 0 },
  { "call4", ICLASS_xt_iclass_call4,
    XTENSA_OPCODE_IS_CALL,
    Opcode_call4_encode_fns, 0, 0 },
  { "callx12", ICLASS_xt_iclass_callx12,
    XTENSA_OPCODE_IS_CALL,
    Opcode_callx12_encode_fns, 0, 0 },
  { "callx8", ICLASS_xt_iclass_callx8,
    XTENSA_OPCODE_IS_CALL,
    Opcode_callx8_encode_fns, 0, 0 },
  { "callx4", ICLASS_xt_iclass_callx4,
    XTENSA_OPCODE_IS_CALL,
    Opcode_callx4_encode_fns, 0, 0 },
  { "entry", ICLASS_xt_iclass_entry,
    0,
    Opcode_entry_encode_fns, 0, 0 },
  { "movsp", ICLASS_xt_iclass_movsp,
    0,
    Opcode_movsp_encode_fns, 0, 0 },
  { "rotw", ICLASS_xt_iclass_rotw,
    0,
    Opcode_rotw_encode_fns, 0, 0 },
  { "retw", ICLASS_xt_iclass_retw,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_retw_encode_fns, 0, 0 },
  { "retw.n", ICLASS_xt_iclass_retw,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_retw_n_encode_fns, 0, 0 },
  { "rfwo", ICLASS_xt_iclass_rfwou,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_rfwo_encode_fns, 0, 0 },
  { "rfwu", ICLASS_xt_iclass_rfwou,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_rfwu_encode_fns, 0, 0 },
  { "l32e", ICLASS_xt_iclass_l32e,
    0,
    Opcode_l32e_encode_fns, 0, 0 },
  { "s32e", ICLASS_xt_iclass_s32e,
    0,
    Opcode_s32e_encode_fns, 0, 0 },
  { "rsr.windowbase", ICLASS_xt_iclass_rsr_windowbase,
    0,
    Opcode_rsr_windowbase_encode_fns, 0, 0 },
  { "wsr.windowbase", ICLASS_xt_iclass_wsr_windowbase,
    0,
    Opcode_wsr_windowbase_encode_fns, 0, 0 },
  { "xsr.windowbase", ICLASS_xt_iclass_xsr_windowbase,
    0,
    Opcode_xsr_windowbase_encode_fns, 0, 0 },
  { "rsr.windowstart", ICLASS_xt_iclass_rsr_windowstart,
    0,
    Opcode_rsr_windowstart_encode_fns, 0, 0 },
  { "wsr.windowstart", ICLASS_xt_iclass_wsr_windowstart,
    0,
    Opcode_wsr_windowstart_encode_fns, 0, 0 },
  { "xsr.windowstart", ICLASS_xt_iclass_xsr_windowstart,
    0,
    Opcode_xsr_windowstart_encode_fns, 0, 0 },
  { "add.n", ICLASS_xt_iclass_add_n,
    0,
    Opcode_add_n_encode_fns, 0, 0 },
  { "addi.n", ICLASS_xt_iclass_addi_n,
    0,
    Opcode_addi_n_encode_fns, 0, 0 },
  { "beqz.n", ICLASS_xt_iclass_bz6,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_beqz_n_encode_fns, 0, 0 },
  { "bnez.n", ICLASS_xt_iclass_bz6,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bnez_n_encode_fns, 0, 0 },
  { "ill.n", ICLASS_xt_iclass_ill_n,
    0,
    Opcode_ill_n_encode_fns, 0, 0 },
  { "l32i.n", ICLASS_xt_iclass_loadi4,
    0,
    Opcode_l32i_n_encode_fns, 0, 0 },
  { "mov.n", ICLASS_xt_iclass_mov_n,
    0,
    Opcode_mov_n_encode_fns, 0, 0 },
  { "movi.n", ICLASS_xt_iclass_movi_n,
    0,
    Opcode_movi_n_encode_fns, 0, 0 },
  { "nop.n", ICLASS_xt_iclass_nopn,
    0,
    Opcode_nop_n_encode_fns, 0, 0 },
  { "ret.n", ICLASS_xt_iclass_retn,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_ret_n_encode_fns, 0, 0 },
  { "s32i.n", ICLASS_xt_iclass_storei4,
    0,
    Opcode_s32i_n_encode_fns, 0, 0 },
  { "rur.threadptr", ICLASS_rur_threadptr,
    0,
    Opcode_rur_threadptr_encode_fns, 0, 0 },
  { "wur.threadptr", ICLASS_wur_threadptr,
    0,
    Opcode_wur_threadptr_encode_fns, 0, 0 },
  { "addi", ICLASS_xt_iclass_addi,
    0,
    Opcode_addi_encode_fns, 0, 0 },
  { "addmi", ICLASS_xt_iclass_addmi,
    0,
    Opcode_addmi_encode_fns, 0, 0 },
  { "add", ICLASS_xt_iclass_addsub,
    0,
    Opcode_add_encode_fns, 0, 0 },
  { "sub", ICLASS_xt_iclass_addsub,
    0,
    Opcode_sub_encode_fns, 0, 0 },
  { "addx2", ICLASS_xt_iclass_addsub,
    0,
    Opcode_addx2_encode_fns, 0, 0 },
  { "addx4", ICLASS_xt_iclass_addsub,
    0,
    Opcode_addx4_encode_fns, 0, 0 },
  { "addx8", ICLASS_xt_iclass_addsub,
    0,
    Opcode_addx8_encode_fns, 0, 0 },
  { "subx2", ICLASS_xt_iclass_addsub,
    0,
    Opcode_subx2_encode_fns, 0, 0 },
  { "subx4", ICLASS_xt_iclass_addsub,
    0,
    Opcode_subx4_encode_fns, 0, 0 },
  { "subx8", ICLASS_xt_iclass_addsub,
    0,
    Opcode_subx8_encode_fns, 0, 0 },
  { "and", ICLASS_xt_iclass_bit,
    0,
    Opcode_and_encode_fns, 0, 0 },
  { "or", ICLASS_xt_iclass_bit,
    0,
    Opcode_or_encode_fns, 0, 0 },
  { "xor", ICLASS_xt_iclass_bit,
    0,
    Opcode_xor_encode_fns, 0, 0 },
  { "beqi", ICLASS_xt_iclass_bsi8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_beqi_encode_fns, 0, 0 },
  { "bnei", ICLASS_xt_iclass_bsi8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bnei_encode_fns, 0, 0 },
  { "bgei", ICLASS_xt_iclass_bsi8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bgei_encode_fns, 0, 0 },
  { "blti", ICLASS_xt_iclass_bsi8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_blti_encode_fns, 0, 0 },
  { "bbci", ICLASS_xt_iclass_bsi8b,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bbci_encode_fns, 0, 0 },
  { "bbsi", ICLASS_xt_iclass_bsi8b,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bbsi_encode_fns, 0, 0 },
  { "bgeui", ICLASS_xt_iclass_bsi8u,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bgeui_encode_fns, 0, 0 },
  { "bltui", ICLASS_xt_iclass_bsi8u,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bltui_encode_fns, 0, 0 },
  { "beq", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_beq_encode_fns, 0, 0 },
  { "bne", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bne_encode_fns, 0, 0 },
  { "bge", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bge_encode_fns, 0, 0 },
  { "blt", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_blt_encode_fns, 0, 0 },
  { "bgeu", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bgeu_encode_fns, 0, 0 },
  { "bltu", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bltu_encode_fns, 0, 0 },
  { "bany", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bany_encode_fns, 0, 0 },
  { "bnone", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bnone_encode_fns, 0, 0 },
  { "ball", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_ball_encode_fns, 0, 0 },
  { "bnall", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bnall_encode_fns, 0, 0 },
  { "bbc", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bbc_encode_fns, 0, 0 },
  { "bbs", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bbs_encode_fns, 0, 0 },
  { "beqz", ICLASS_xt_iclass_bsz12,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_beqz_encode_fns, 0, 0 },
  { "bnez", ICLASS_xt_iclass_bsz12,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bnez_encode_fns, 0, 0 },
  { "bgez", ICLASS_xt_iclass_bsz12,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bgez_encode_fns, 0, 0 },
  { "bltz", ICLASS_xt_iclass_bsz12,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bltz_encode_fns, 0, 0 },
  { "call0", ICLASS_xt_iclass_call0,
    XTENSA_OPCODE_IS_CALL,
    Opcode_call0_encode_fns, 0, 0 },
  { "callx0", ICLASS_xt_iclass_callx0,
    XTENSA_OPCODE_IS_CALL,
    Opcode_callx0_encode_fns, 0, 0 },
  { "extui", ICLASS_xt_iclass_exti,
    0,
    Opcode_extui_encode_fns, 0, 0 },
  { "ill", ICLASS_xt_iclass_ill,
    0,
    Opcode_ill_encode_fns, 0, 0 },
  { "j", ICLASS_xt_iclass_jump,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_j_encode_fns, 0, 0 },
  { "jx", ICLASS_xt_iclass_jumpx,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_jx_encode_fns, 0, 0 },
  { "l16ui", ICLASS_xt_iclass_l16ui,
    0,
    Opcode_l16ui_encode_fns, 0, 0 },
  { "l16si", ICLASS_xt_iclass_l16si,
    0,
    Opcode_l16si_encode_fns, 0, 0 },
  { "l32i", ICLASS_xt_iclass_l32i,
    0,
    Opcode_l32i_encode_fns, 0, 0 },
  { "l32r", ICLASS_xt_iclass_l32r,
    0,
    Opcode_l32r_encode_fns, 0, 0 },
  { "l8ui", ICLASS_xt_iclass_l8i,
    0,
    Opcode_l8ui_encode_fns, 0, 0 },
  { "loop", ICLASS_xt_iclass_loop,
    XTENSA_OPCODE_IS_LOOP,
    Opcode_loop_encode_fns, 0, 0 },
  { "loopnez", ICLASS_xt_iclass_loopz,
    XTENSA_OPCODE_IS_LOOP,
    Opcode_loopnez_encode_fns, 0, 0 },
  { "loopgtz", ICLASS_xt_iclass_loopz,
    XTENSA_OPCODE_IS_LOOP,
    Opcode_loopgtz_encode_fns, 0, 0 },
  { "movi", ICLASS_xt_iclass_movi,
    0,
    Opcode_movi_encode_fns, 0, 0 },
  { "moveqz", ICLASS_xt_iclass_movz,
    0,
    Opcode_moveqz_encode_fns, 0, 0 },
  { "movnez", ICLASS_xt_iclass_movz,
    0,
    Opcode_movnez_encode_fns, 0, 0 },
  { "movltz", ICLASS_xt_iclass_movz,
    0,
    Opcode_movltz_encode_fns, 0, 0 },
  { "movgez", ICLASS_xt_iclass_movz,
    0,
    Opcode_movgez_encode_fns, 0, 0 },
  { "neg", ICLASS_xt_iclass_neg,
    0,
    Opcode_neg_encode_fns, 0, 0 },
  { "abs", ICLASS_xt_iclass_neg,
    0,
    Opcode_abs_encode_fns, 0, 0 },
  { "nop", ICLASS_xt_iclass_nop,
    0,
    Opcode_nop_encode_fns, 0, 0 },
  { "ret", ICLASS_xt_iclass_return,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_ret_encode_fns, 0, 0 },
  { "s16i", ICLASS_xt_iclass_s16i,
    0,
    Opcode_s16i_encode_fns, 0, 0 },
  { "s32i", ICLASS_xt_iclass_s32i,
    0,
    Opcode_s32i_encode_fns, 0, 0 },
  { "s8i", ICLASS_xt_iclass_s8i,
    0,
    Opcode_s8i_encode_fns, 0, 0 },
  { "ssr", ICLASS_xt_iclass_sar,
    0,
    Opcode_ssr_encode_fns, 0, 0 },
  { "ssl", ICLASS_xt_iclass_sar,
    0,
    Opcode_ssl_encode_fns, 0, 0 },
  { "ssa8l", ICLASS_xt_iclass_sar,
    0,
    Opcode_ssa8l_encode_fns, 0, 0 },
  { "ssa8b", ICLASS_xt_iclass_sar,
    0,
    Opcode_ssa8b_encode_fns, 0, 0 },
  { "ssai", ICLASS_xt_iclass_sari,
    0,
    Opcode_ssai_encode_fns, 0, 0 },
  { "sll", ICLASS_xt_iclass_shifts,
    0,
    Opcode_sll_encode_fns, 0, 0 },
  { "src", ICLASS_xt_iclass_shiftst,
    0,
    Opcode_src_encode_fns, 0, 0 },
  { "srl", ICLASS_xt_iclass_shiftt,
    0,
    Opcode_srl_encode_fns, 0, 0 },
  { "sra", ICLASS_xt_iclass_shiftt,
    0,
    Opcode_sra_encode_fns, 0, 0 },
  { "slli", ICLASS_xt_iclass_slli,
    0,
    Opcode_slli_encode_fns, 0, 0 },
  { "srai", ICLASS_xt_iclass_srai,
    0,
    Opcode_srai_encode_fns, 0, 0 },
  { "srli", ICLASS_xt_iclass_srli,
    0,
    Opcode_srli_encode_fns, 0, 0 },
  { "memw", ICLASS_xt_iclass_memw,
    0,
    Opcode_memw_encode_fns, 0, 0 },
  { "extw", ICLASS_xt_iclass_extw,
    0,
    Opcode_extw_encode_fns, 0, 0 },
  { "isync", ICLASS_xt_iclass_isync,
    0,
    Opcode_isync_encode_fns, 0, 0 },
  { "rsync", ICLASS_xt_iclass_sync,
    0,
    Opcode_rsync_encode_fns, 0, 0 },
  { "esync", ICLASS_xt_iclass_sync,
    0,
    Opcode_esync_encode_fns, 0, 0 },
  { "dsync", ICLASS_xt_iclass_sync,
    0,
    Opcode_dsync_encode_fns, 0, 0 },
  { "rsil", ICLASS_xt_iclass_rsil,
    0,
    Opcode_rsil_encode_fns, 0, 0 },
  { "rsr.lend", ICLASS_xt_iclass_rsr_lend,
    0,
    Opcode_rsr_lend_encode_fns, 0, 0 },
  { "wsr.lend", ICLASS_xt_iclass_wsr_lend,
    0,
    Opcode_wsr_lend_encode_fns, 0, 0 },
  { "xsr.lend", ICLASS_xt_iclass_xsr_lend,
    0,
    Opcode_xsr_lend_encode_fns, 0, 0 },
  { "rsr.lcount", ICLASS_xt_iclass_rsr_lcount,
    0,
    Opcode_rsr_lcount_encode_fns, 0, 0 },
  { "wsr.lcount", ICLASS_xt_iclass_wsr_lcount,
    0,
    Opcode_wsr_lcount_encode_fns, 0, 0 },
  { "xsr.lcount", ICLASS_xt_iclass_xsr_lcount,
    0,
    Opcode_xsr_lcount_encode_fns, 0, 0 },
  { "rsr.lbeg", ICLASS_xt_iclass_rsr_lbeg,
    0,
    Opcode_rsr_lbeg_encode_fns, 0, 0 },
  { "wsr.lbeg", ICLASS_xt_iclass_wsr_lbeg,
    0,
    Opcode_wsr_lbeg_encode_fns, 0, 0 },
  { "xsr.lbeg", ICLASS_xt_iclass_xsr_lbeg,
    0,
    Opcode_xsr_lbeg_encode_fns, 0, 0 },
  { "rsr.sar", ICLASS_xt_iclass_rsr_sar,
    0,
    Opcode_rsr_sar_encode_fns, 0, 0 },
  { "wsr.sar", ICLASS_xt_iclass_wsr_sar,
    0,
    Opcode_wsr_sar_encode_fns, 0, 0 },
  { "xsr.sar", ICLASS_xt_iclass_xsr_sar,
    0,
    Opcode_xsr_sar_encode_fns, 0, 0 },
  { "rsr.litbase", ICLASS_xt_iclass_rsr_litbase,
    0,
    Opcode_rsr_litbase_encode_fns, 0, 0 },
  { "wsr.litbase", ICLASS_xt_iclass_wsr_litbase,
    0,
    Opcode_wsr_litbase_encode_fns, 0, 0 },
  { "xsr.litbase", ICLASS_xt_iclass_xsr_litbase,
    0,
    Opcode_xsr_litbase_encode_fns, 0, 0 },
  { "rsr.176", ICLASS_xt_iclass_rsr_176,
    0,
    Opcode_rsr_176_encode_fns, 0, 0 },
  { "wsr.176", ICLASS_xt_iclass_wsr_176,
    0,
    Opcode_wsr_176_encode_fns, 0, 0 },
  { "rsr.208", ICLASS_xt_iclass_rsr_208,
    0,
    Opcode_rsr_208_encode_fns, 0, 0 },
  { "rsr.ps", ICLASS_xt_iclass_rsr_ps,
    0,
    Opcode_rsr_ps_encode_fns, 0, 0 },
  { "wsr.ps", ICLASS_xt_iclass_wsr_ps,
    0,
    Opcode_wsr_ps_encode_fns, 0, 0 },
  { "xsr.ps", ICLASS_xt_iclass_xsr_ps,
    0,
    Opcode_xsr_ps_encode_fns, 0, 0 },
  { "rsr.epc1", ICLASS_xt_iclass_rsr_epc1,
    0,
    Opcode_rsr_epc1_encode_fns, 0, 0 },
  { "wsr.epc1", ICLASS_xt_iclass_wsr_epc1,
    0,
    Opcode_wsr_epc1_encode_fns, 0, 0 },
  { "xsr.epc1", ICLASS_xt_iclass_xsr_epc1,
    0,
    Opcode_xsr_epc1_encode_fns, 0, 0 },
  { "rsr.excsave1", ICLASS_xt_iclass_rsr_excsave1,
    0,
    Opcode_rsr_excsave1_encode_fns, 0, 0 },
  { "wsr.excsave1", ICLASS_xt_iclass_wsr_excsave1,
    0,
    Opcode_wsr_excsave1_encode_fns, 0, 0 },
  { "xsr.excsave1", ICLASS_xt_iclass_xsr_excsave1,
    0,
    Opcode_xsr_excsave1_encode_fns, 0, 0 },
  { "rsr.epc2", ICLASS_xt_iclass_rsr_epc2,
    0,
    Opcode_rsr_epc2_encode_fns, 0, 0 },
  { "wsr.epc2", ICLASS_xt_iclass_wsr_epc2,
    0,
    Opcode_wsr_epc2_encode_fns, 0, 0 },
  { "xsr.epc2", ICLASS_xt_iclass_xsr_epc2,
    0,
    Opcode_xsr_epc2_encode_fns, 0, 0 },
  { "rsr.excsave2", ICLASS_xt_iclass_rsr_excsave2,
    0,
    Opcode_rsr_excsave2_encode_fns, 0, 0 },
  { "wsr.excsave2", ICLASS_xt_iclass_wsr_excsave2,
    0,
    Opcode_wsr_excsave2_encode_fns, 0, 0 },
  { "xsr.excsave2", ICLASS_xt_iclass_xsr_excsave2,
    0,
    Opcode_xsr_excsave2_encode_fns, 0, 0 },
  { "rsr.epc3", ICLASS_xt_iclass_rsr_epc3,
    0,
    Opcode_rsr_epc3_encode_fns, 0, 0 },
  { "wsr.epc3", ICLASS_xt_iclass_wsr_epc3,
    0,
    Opcode_wsr_epc3_encode_fns, 0, 0 },
  { "xsr.epc3", ICLASS_xt_iclass_xsr_epc3,
    0,
    Opcode_xsr_epc3_encode_fns, 0, 0 },
  { "rsr.excsave3", ICLASS_xt_iclass_rsr_excsave3,
    0,
    Opcode_rsr_excsave3_encode_fns, 0, 0 },
  { "wsr.excsave3", ICLASS_xt_iclass_wsr_excsave3,
    0,
    Opcode_wsr_excsave3_encode_fns, 0, 0 },
  { "xsr.excsave3", ICLASS_xt_iclass_xsr_excsave3,
    0,
    Opcode_xsr_excsave3_encode_fns, 0, 0 },
  { "rsr.epc4", ICLASS_xt_iclass_rsr_epc4,
    0,
    Opcode_rsr_epc4_encode_fns, 0, 0 },
  { "wsr.epc4", ICLASS_xt_iclass_wsr_epc4,
    0,
    Opcode_wsr_epc4_encode_fns, 0, 0 },
  { "xsr.epc4", ICLASS_xt_iclass_xsr_epc4,
    0,
    Opcode_xsr_epc4_encode_fns, 0, 0 },
  { "rsr.excsave4", ICLASS_xt_iclass_rsr_excsave4,
    0,
    Opcode_rsr_excsave4_encode_fns, 0, 0 },
  { "wsr.excsave4", ICLASS_xt_iclass_wsr_excsave4,
    0,
    Opcode_wsr_excsave4_encode_fns, 0, 0 },
  { "xsr.excsave4", ICLASS_xt_iclass_xsr_excsave4,
    0,
    Opcode_xsr_excsave4_encode_fns, 0, 0 },
  { "rsr.epc5", ICLASS_xt_iclass_rsr_epc5,
    0,
    Opcode_rsr_epc5_encode_fns, 0, 0 },
  { "wsr.epc5", ICLASS_xt_iclass_wsr_epc5,
    0,
    Opcode_wsr_epc5_encode_fns, 0, 0 },
  { "xsr.epc5", ICLASS_xt_iclass_xsr_epc5,
    0,
    Opcode_xsr_epc5_encode_fns, 0, 0 },
  { "rsr.excsave5", ICLASS_xt_iclass_rsr_excsave5,
    0,
    Opcode_rsr_excsave5_encode_fns, 0, 0 },
  { "wsr.excsave5", ICLASS_xt_iclass_wsr_excsave5,
    0,
    Opcode_wsr_excsave5_encode_fns, 0, 0 },
  { "xsr.excsave5", ICLASS_xt_iclass_xsr_excsave5,
    0,
    Opcode_xsr_excsave5_encode_fns, 0, 0 },
  { "rsr.epc6", ICLASS_xt_iclass_rsr_epc6,
    0,
    Opcode_rsr_epc6_encode_fns, 0, 0 },
  { "wsr.epc6", ICLASS_xt_iclass_wsr_epc6,
    0,
    Opcode_wsr_epc6_encode_fns, 0, 0 },
  { "xsr.epc6", ICLASS_xt_iclass_xsr_epc6,
    0,
    Opcode_xsr_epc6_encode_fns, 0, 0 },
  { "rsr.excsave6", ICLASS_xt_iclass_rsr_excsave6,
    0,
    Opcode_rsr_excsave6_encode_fns, 0, 0 },
  { "wsr.excsave6", ICLASS_xt_iclass_wsr_excsave6,
    0,
    Opcode_wsr_excsave6_encode_fns, 0, 0 },
  { "xsr.excsave6", ICLASS_xt_iclass_xsr_excsave6,
    0,
    Opcode_xsr_excsave6_encode_fns, 0, 0 },
  { "rsr.epc7", ICLASS_xt_iclass_rsr_epc7,
    0,
    Opcode_rsr_epc7_encode_fns, 0, 0 },
  { "wsr.epc7", ICLASS_xt_iclass_wsr_epc7,
    0,
    Opcode_wsr_epc7_encode_fns, 0, 0 },
  { "xsr.epc7", ICLASS_xt_iclass_xsr_epc7,
    0,
    Opcode_xsr_epc7_encode_fns, 0, 0 },
  { "rsr.excsave7", ICLASS_xt_iclass_rsr_excsave7,
    0,
    Opcode_rsr_excsave7_encode_fns, 0, 0 },
  { "wsr.excsave7", ICLASS_xt_iclass_wsr_excsave7,
    0,
    Opcode_wsr_excsave7_encode_fns, 0, 0 },
  { "xsr.excsave7", ICLASS_xt_iclass_xsr_excsave7,
    0,
    Opcode_xsr_excsave7_encode_fns, 0, 0 },
  { "rsr.eps2", ICLASS_xt_iclass_rsr_eps2,
    0,
    Opcode_rsr_eps2_encode_fns, 0, 0 },
  { "wsr.eps2", ICLASS_xt_iclass_wsr_eps2,
    0,
    Opcode_wsr_eps2_encode_fns, 0, 0 },
  { "xsr.eps2", ICLASS_xt_iclass_xsr_eps2,
    0,
    Opcode_xsr_eps2_encode_fns, 0, 0 },
  { "rsr.eps3", ICLASS_xt_iclass_rsr_eps3,
    0,
    Opcode_rsr_eps3_encode_fns, 0, 0 },
  { "wsr.eps3", ICLASS_xt_iclass_wsr_eps3,
    0,
    Opcode_wsr_eps3_encode_fns, 0, 0 },
  { "xsr.eps3", ICLASS_xt_iclass_xsr_eps3,
    0,
    Opcode_xsr_eps3_encode_fns, 0, 0 },
  { "rsr.eps4", ICLASS_xt_iclass_rsr_eps4,
    0,
    Opcode_rsr_eps4_encode_fns, 0, 0 },
  { "wsr.eps4", ICLASS_xt_iclass_wsr_eps4,
    0,
    Opcode_wsr_eps4_encode_fns, 0, 0 },
  { "xsr.eps4", ICLASS_xt_iclass_xsr_eps4,
    0,
    Opcode_xsr_eps4_encode_fns, 0, 0 },
  { "rsr.eps5", ICLASS_xt_iclass_rsr_eps5,
    0,
    Opcode_rsr_eps5_encode_fns, 0, 0 },
  { "wsr.eps5", ICLASS_xt_iclass_wsr_eps5,
    0,
    Opcode_wsr_eps5_encode_fns, 0, 0 },
  { "xsr.eps5", ICLASS_xt_iclass_xsr_eps5,
    0,
    Opcode_xsr_eps5_encode_fns, 0, 0 },
  { "rsr.eps6", ICLASS_xt_iclass_rsr_eps6,
    0,
    Opcode_rsr_eps6_encode_fns, 0, 0 },
  { "wsr.eps6", ICLASS_xt_iclass_wsr_eps6,
    0,
    Opcode_wsr_eps6_encode_fns, 0, 0 },
  { "xsr.eps6", ICLASS_xt_iclass_xsr_eps6,
    0,
    Opcode_xsr_eps6_encode_fns, 0, 0 },
  { "rsr.eps7", ICLASS_xt_iclass_rsr_eps7,
    0,
    Opcode_rsr_eps7_encode_fns, 0, 0 },
  { "wsr.eps7", ICLASS_xt_iclass_wsr_eps7,
    0,
    Opcode_wsr_eps7_encode_fns, 0, 0 },
  { "xsr.eps7", ICLASS_xt_iclass_xsr_eps7,
    0,
    Opcode_xsr_eps7_encode_fns, 0, 0 },
  { "rsr.excvaddr", ICLASS_xt_iclass_rsr_excvaddr,
    0,
    Opcode_rsr_excvaddr_encode_fns, 0, 0 },
  { "wsr.excvaddr", ICLASS_xt_iclass_wsr_excvaddr,
    0,
    Opcode_wsr_excvaddr_encode_fns, 0, 0 },
  { "xsr.excvaddr", ICLASS_xt_iclass_xsr_excvaddr,
    0,
    Opcode_xsr_excvaddr_encode_fns, 0, 0 },
  { "rsr.depc", ICLASS_xt_iclass_rsr_depc,
    0,
    Opcode_rsr_depc_encode_fns, 0, 0 },
  { "wsr.depc", ICLASS_xt_iclass_wsr_depc,
    0,
    Opcode_wsr_depc_encode_fns, 0, 0 },
  { "xsr.depc", ICLASS_xt_iclass_xsr_depc,
    0,
    Opcode_xsr_depc_encode_fns, 0, 0 },
  { "rsr.exccause", ICLASS_xt_iclass_rsr_exccause,
    0,
    Opcode_rsr_exccause_encode_fns, 0, 0 },
  { "wsr.exccause", ICLASS_xt_iclass_wsr_exccause,
    0,
    Opcode_wsr_exccause_encode_fns, 0, 0 },
  { "xsr.exccause", ICLASS_xt_iclass_xsr_exccause,
    0,
    Opcode_xsr_exccause_encode_fns, 0, 0 },
  { "rsr.misc0", ICLASS_xt_iclass_rsr_misc0,
    0,
    Opcode_rsr_misc0_encode_fns, 0, 0 },
  { "wsr.misc0", ICLASS_xt_iclass_wsr_misc0,
    0,
    Opcode_wsr_misc0_encode_fns, 0, 0 },
  { "xsr.misc0", ICLASS_xt_iclass_xsr_misc0,
    0,
    Opcode_xsr_misc0_encode_fns, 0, 0 },
  { "rsr.misc1", ICLASS_xt_iclass_rsr_misc1,
    0,
    Opcode_rsr_misc1_encode_fns, 0, 0 },
  { "wsr.misc1", ICLASS_xt_iclass_wsr_misc1,
    0,
    Opcode_wsr_misc1_encode_fns, 0, 0 },
  { "xsr.misc1", ICLASS_xt_iclass_xsr_misc1,
    0,
    Opcode_xsr_misc1_encode_fns, 0, 0 },
  { "rsr.prid", ICLASS_xt_iclass_rsr_prid,
    0,
    Opcode_rsr_prid_encode_fns, 0, 0 },
  { "rsr.vecbase", ICLASS_xt_iclass_rsr_vecbase,
    0,
    Opcode_rsr_vecbase_encode_fns, 0, 0 },
  { "wsr.vecbase", ICLASS_xt_iclass_wsr_vecbase,
    0,
    Opcode_wsr_vecbase_encode_fns, 0, 0 },
  { "xsr.vecbase", ICLASS_xt_iclass_xsr_vecbase,
    0,
    Opcode_xsr_vecbase_encode_fns, 0, 0 },
  { "mul16u", ICLASS_xt_mul16,
    0,
    Opcode_mul16u_encode_fns, 0, 0 },
  { "mul16s", ICLASS_xt_mul16,
    0,
    Opcode_mul16s_encode_fns, 0, 0 },
  { "mull", ICLASS_xt_mul32,
    0,
    Opcode_mull_encode_fns, 0, 0 },
  { "mul.aa.ll", ICLASS_xt_iclass_mac16_aa,
    0,
    Opcode_mul_aa_ll_encode_fns, 0, 0 },
  { "mul.aa.hl", ICLASS_xt_iclass_mac16_aa,
    0,
    Opcode_mul_aa_hl_encode_fns, 0, 0 },
  { "mul.aa.lh", ICLASS_xt_iclass_mac16_aa,
    0,
    Opcode_mul_aa_lh_encode_fns, 0, 0 },
  { "mul.aa.hh", ICLASS_xt_iclass_mac16_aa,
    0,
    Opcode_mul_aa_hh_encode_fns, 0, 0 },
  { "umul.aa.ll", ICLASS_xt_iclass_mac16_aa,
    0,
    Opcode_umul_aa_ll_encode_fns, 0, 0 },
  { "umul.aa.hl", ICLASS_xt_iclass_mac16_aa,
    0,
    Opcode_umul_aa_hl_encode_fns, 0, 0 },
  { "umul.aa.lh", ICLASS_xt_iclass_mac16_aa,
    0,
    Opcode_umul_aa_lh_encode_fns, 0, 0 },
  { "umul.aa.hh", ICLASS_xt_iclass_mac16_aa,
    0,
    Opcode_umul_aa_hh_encode_fns, 0, 0 },
  { "mul.ad.ll", ICLASS_xt_iclass_mac16_ad,
    0,
    Opcode_mul_ad_ll_encode_fns, 0, 0 },
  { "mul.ad.hl", ICLASS_xt_iclass_mac16_ad,
    0,
    Opcode_mul_ad_hl_encode_fns, 0, 0 },
  { "mul.ad.lh", ICLASS_xt_iclass_mac16_ad,
    0,
    Opcode_mul_ad_lh_encode_fns, 0, 0 },
  { "mul.ad.hh", ICLASS_xt_iclass_mac16_ad,
    0,
    Opcode_mul_ad_hh_encode_fns, 0, 0 },
  { "mul.da.ll", ICLASS_xt_iclass_mac16_da,
    0,
    Opcode_mul_da_ll_encode_fns, 0, 0 },
  { "mul.da.hl", ICLASS_xt_iclass_mac16_da,
    0,
    Opcode_mul_da_hl_encode_fns, 0, 0 },
  { "mul.da.lh", ICLASS_xt_iclass_mac16_da,
    0,
    Opcode_mul_da_lh_encode_fns, 0, 0 },
  { "mul.da.hh", ICLASS_xt_iclass_mac16_da,
    0,
    Opcode_mul_da_hh_encode_fns, 0, 0 },
  { "mul.dd.ll", ICLASS_xt_iclass_mac16_dd,
    0,
    Opcode_mul_dd_ll_encode_fns, 0, 0 },
  { "mul.dd.hl", ICLASS_xt_iclass_mac16_dd,
    0,
    Opcode_mul_dd_hl_encode_fns, 0, 0 },
  { "mul.dd.lh", ICLASS_xt_iclass_mac16_dd,
    0,
    Opcode_mul_dd_lh_encode_fns, 0, 0 },
  { "mul.dd.hh", ICLASS_xt_iclass_mac16_dd,
    0,
    Opcode_mul_dd_hh_encode_fns, 0, 0 },
  { "mula.aa.ll", ICLASS_xt_iclass_mac16a_aa,
    0,
    Opcode_mula_aa_ll_encode_fns, 0, 0 },
  { "mula.aa.hl", ICLASS_xt_iclass_mac16a_aa,
    0,
    Opcode_mula_aa_hl_encode_fns, 0, 0 },
  { "mula.aa.lh", ICLASS_xt_iclass_mac16a_aa,
    0,
    Opcode_mula_aa_lh_encode_fns, 0, 0 },
  { "mula.aa.hh", ICLASS_xt_iclass_mac16a_aa,
    0,
    Opcode_mula_aa_hh_encode_fns, 0, 0 },
  { "muls.aa.ll", ICLASS_xt_iclass_mac16a_aa,
    0,
    Opcode_muls_aa_ll_encode_fns, 0, 0 },
  { "muls.aa.hl", ICLASS_xt_iclass_mac16a_aa,
    0,
    Opcode_muls_aa_hl_encode_fns, 0, 0 },
  { "muls.aa.lh", ICLASS_xt_iclass_mac16a_aa,
    0,
    Opcode_muls_aa_lh_encode_fns, 0, 0 },
  { "muls.aa.hh", ICLASS_xt_iclass_mac16a_aa,
    0,
    Opcode_muls_aa_hh_encode_fns, 0, 0 },
  { "mula.ad.ll", ICLASS_xt_iclass_mac16a_ad,
    0,
    Opcode_mula_ad_ll_encode_fns, 0, 0 },
  { "mula.ad.hl", ICLASS_xt_iclass_mac16a_ad,
    0,
    Opcode_mula_ad_hl_encode_fns, 0, 0 },
  { "mula.ad.lh", ICLASS_xt_iclass_mac16a_ad,
    0,
    Opcode_mula_ad_lh_encode_fns, 0, 0 },
  { "mula.ad.hh", ICLASS_xt_iclass_mac16a_ad,
    0,
    Opcode_mula_ad_hh_encode_fns, 0, 0 },
  { "muls.ad.ll", ICLASS_xt_iclass_mac16a_ad,
    0,
    Opcode_muls_ad_ll_encode_fns, 0, 0 },
  { "muls.ad.hl", ICLASS_xt_iclass_mac16a_ad,
    0,
    Opcode_muls_ad_hl_encode_fns, 0, 0 },
  { "muls.ad.lh", ICLASS_xt_iclass_mac16a_ad,
    0,
    Opcode_muls_ad_lh_encode_fns, 0, 0 },
  { "muls.ad.hh", ICLASS_xt_iclass_mac16a_ad,
    0,
    Opcode_muls_ad_hh_encode_fns, 0, 0 },
  { "mula.da.ll", ICLASS_xt_iclass_mac16a_da,
    0,
    Opcode_mula_da_ll_encode_fns, 0, 0 },
  { "mula.da.hl", ICLASS_xt_iclass_mac16a_da,
    0,
    Opcode_mula_da_hl_encode_fns, 0, 0 },
  { "mula.da.lh", ICLASS_xt_iclass_mac16a_da,
    0,
    Opcode_mula_da_lh_encode_fns, 0, 0 },
  { "mula.da.hh", ICLASS_xt_iclass_mac16a_da,
    0,
    Opcode_mula_da_hh_encode_fns, 0, 0 },
  { "muls.da.ll", ICLASS_xt_iclass_mac16a_da,
    0,
    Opcode_muls_da_ll_encode_fns, 0, 0 },
  { "muls.da.hl", ICLASS_xt_iclass_mac16a_da,
    0,
    Opcode_muls_da_hl_encode_fns, 0, 0 },
  { "muls.da.lh", ICLASS_xt_iclass_mac16a_da,
    0,
    Opcode_muls_da_lh_encode_fns, 0, 0 },
  { "muls.da.hh", ICLASS_xt_iclass_mac16a_da,
    0,
    Opcode_muls_da_hh_encode_fns, 0, 0 },
  { "mula.dd.ll", ICLASS_xt_iclass_mac16a_dd,
    0,
    Opcode_mula_dd_ll_encode_fns, 0, 0 },
  { "mula.dd.hl", ICLASS_xt_iclass_mac16a_dd,
    0,
    Opcode_mula_dd_hl_encode_fns, 0, 0 },
  { "mula.dd.lh", ICLASS_xt_iclass_mac16a_dd,
    0,
    Opcode_mula_dd_lh_encode_fns, 0, 0 },
  { "mula.dd.hh", ICLASS_xt_iclass_mac16a_dd,
    0,
    Opcode_mula_dd_hh_encode_fns, 0, 0 },
  { "muls.dd.ll", ICLASS_xt_iclass_mac16a_dd,
    0,
    Opcode_muls_dd_ll_encode_fns, 0, 0 },
  { "muls.dd.hl", ICLASS_xt_iclass_mac16a_dd,
    0,
    Opcode_muls_dd_hl_encode_fns, 0, 0 },
  { "muls.dd.lh", ICLASS_xt_iclass_mac16a_dd,
    0,
    Opcode_muls_dd_lh_encode_fns, 0, 0 },
  { "muls.dd.hh", ICLASS_xt_iclass_mac16a_dd,
    0,
    Opcode_muls_dd_hh_encode_fns, 0, 0 },
  { "mula.da.ll.lddec", ICLASS_xt_iclass_mac16al_da,
    0,
    Opcode_mula_da_ll_lddec_encode_fns, 0, 0 },
  { "mula.da.ll.ldinc", ICLASS_xt_iclass_mac16al_da,
    0,
    Opcode_mula_da_ll_ldinc_encode_fns, 0, 0 },
  { "mula.da.hl.lddec", ICLASS_xt_iclass_mac16al_da,
    0,
    Opcode_mula_da_hl_lddec_encode_fns, 0, 0 },
  { "mula.da.hl.ldinc", ICLASS_xt_iclass_mac16al_da,
    0,
    Opcode_mula_da_hl_ldinc_encode_fns, 0, 0 },
  { "mula.da.lh.lddec", ICLASS_xt_iclass_mac16al_da,
    0,
    Opcode_mula_da_lh_lddec_encode_fns, 0, 0 },
  { "mula.da.lh.ldinc", ICLASS_xt_iclass_mac16al_da,
    0,
    Opcode_mula_da_lh_ldinc_encode_fns, 0, 0 },
  { "mula.da.hh.lddec", ICLASS_xt_iclass_mac16al_da,
    0,
    Opcode_mula_da_hh_lddec_encode_fns, 0, 0 },
  { "mula.da.hh.ldinc", ICLASS_xt_iclass_mac16al_da,
    0,
    Opcode_mula_da_hh_ldinc_encode_fns, 0, 0 },
  { "mula.dd.ll.lddec", ICLASS_xt_iclass_mac16al_dd,
    0,
    Opcode_mula_dd_ll_lddec_encode_fns, 0, 0 },
  { "mula.dd.ll.ldinc", ICLASS_xt_iclass_mac16al_dd,
    0,
    Opcode_mula_dd_ll_ldinc_encode_fns, 0, 0 },
  { "mula.dd.hl.lddec", ICLASS_xt_iclass_mac16al_dd,
    0,
    Opcode_mula_dd_hl_lddec_encode_fns, 0, 0 },
  { "mula.dd.hl.ldinc", ICLASS_xt_iclass_mac16al_dd,
    0,
    Opcode_mula_dd_hl_ldinc_encode_fns, 0, 0 },
  { "mula.dd.lh.lddec", ICLASS_xt_iclass_mac16al_dd,
    0,
    Opcode_mula_dd_lh_lddec_encode_fns, 0, 0 },
  { "mula.dd.lh.ldinc", ICLASS_xt_iclass_mac16al_dd,
    0,
    Opcode_mula_dd_lh_ldinc_encode_fns, 0, 0 },
  { "mula.dd.hh.lddec", ICLASS_xt_iclass_mac16al_dd,
    0,
    Opcode_mula_dd_hh_lddec_encode_fns, 0, 0 },
  { "mula.dd.hh.ldinc", ICLASS_xt_iclass_mac16al_dd,
    0,
    Opcode_mula_dd_hh_ldinc_encode_fns, 0, 0 },
  { "lddec", ICLASS_xt_iclass_mac16_l,
    0,
    Opcode_lddec_encode_fns, 0, 0 },
  { "ldinc", ICLASS_xt_iclass_mac16_l,
    0,
    Opcode_ldinc_encode_fns, 0, 0 },
  { "rsr.m0", ICLASS_xt_iclass_rsr_m0,
    0,
    Opcode_rsr_m0_encode_fns, 0, 0 },
  { "wsr.m0", ICLASS_xt_iclass_wsr_m0,
    0,
    Opcode_wsr_m0_encode_fns, 0, 0 },
  { "xsr.m0", ICLASS_xt_iclass_xsr_m0,
    0,
    Opcode_xsr_m0_encode_fns, 0, 0 },
  { "rsr.m1", ICLASS_xt_iclass_rsr_m1,
    0,
    Opcode_rsr_m1_encode_fns, 0, 0 },
  { "wsr.m1", ICLASS_xt_iclass_wsr_m1,
    0,
    Opcode_wsr_m1_encode_fns, 0, 0 },
  { "xsr.m1", ICLASS_xt_iclass_xsr_m1,
    0,
    Opcode_xsr_m1_encode_fns, 0, 0 },
  { "rsr.m2", ICLASS_xt_iclass_rsr_m2,
    0,
    Opcode_rsr_m2_encode_fns, 0, 0 },
  { "wsr.m2", ICLASS_xt_iclass_wsr_m2,
    0,
    Opcode_wsr_m2_encode_fns, 0, 0 },
  { "xsr.m2", ICLASS_xt_iclass_xsr_m2,
    0,
    Opcode_xsr_m2_encode_fns, 0, 0 },
  { "rsr.m3", ICLASS_xt_iclass_rsr_m3,
    0,
    Opcode_rsr_m3_encode_fns, 0, 0 },
  { "wsr.m3", ICLASS_xt_iclass_wsr_m3,
    0,
    Opcode_wsr_m3_encode_fns, 0, 0 },
  { "xsr.m3", ICLASS_xt_iclass_xsr_m3,
    0,
    Opcode_xsr_m3_encode_fns, 0, 0 },
  { "rsr.acclo", ICLASS_xt_iclass_rsr_acclo,
    0,
    Opcode_rsr_acclo_encode_fns, 0, 0 },
  { "wsr.acclo", ICLASS_xt_iclass_wsr_acclo,
    0,
    Opcode_wsr_acclo_encode_fns, 0, 0 },
  { "xsr.acclo", ICLASS_xt_iclass_xsr_acclo,
    0,
    Opcode_xsr_acclo_encode_fns, 0, 0 },
  { "rsr.acchi", ICLASS_xt_iclass_rsr_acchi,
    0,
    Opcode_rsr_acchi_encode_fns, 0, 0 },
  { "wsr.acchi", ICLASS_xt_iclass_wsr_acchi,
    0,
    Opcode_wsr_acchi_encode_fns, 0, 0 },
  { "xsr.acchi", ICLASS_xt_iclass_xsr_acchi,
    0,
    Opcode_xsr_acchi_encode_fns, 0, 0 },
  { "rfi", ICLASS_xt_iclass_rfi,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_rfi_encode_fns, 0, 0 },
  { "waiti", ICLASS_xt_iclass_wait,
    0,
    Opcode_waiti_encode_fns, 0, 0 },
  { "rsr.interrupt", ICLASS_xt_iclass_rsr_interrupt,
    0,
    Opcode_rsr_interrupt_encode_fns, 0, 0 },
  { "wsr.intset", ICLASS_xt_iclass_wsr_intset,
    0,
    Opcode_wsr_intset_encode_fns, 0, 0 },
  { "wsr.intclear", ICLASS_xt_iclass_wsr_intclear,
    0,
    Opcode_wsr_intclear_encode_fns, 0, 0 },
  { "rsr.intenable", ICLASS_xt_iclass_rsr_intenable,
    0,
    Opcode_rsr_intenable_encode_fns, 0, 0 },
  { "wsr.intenable", ICLASS_xt_iclass_wsr_intenable,
    0,
    Opcode_wsr_intenable_encode_fns, 0, 0 },
  { "xsr.intenable", ICLASS_xt_iclass_xsr_intenable,
    0,
    Opcode_xsr_intenable_encode_fns, 0, 0 },
  { "break", ICLASS_xt_iclass_break,
    0,
    Opcode_break_encode_fns, 0, 0 },
  { "break.n", ICLASS_xt_iclass_break_n,
    0,
    Opcode_break_n_encode_fns, 0, 0 },
  { "rsr.dbreaka0", ICLASS_xt_iclass_rsr_dbreaka0,
    0,
    Opcode_rsr_dbreaka0_encode_fns, 0, 0 },
  { "wsr.dbreaka0", ICLASS_xt_iclass_wsr_dbreaka0,
    0,
    Opcode_wsr_dbreaka0_encode_fns, 0, 0 },
  { "xsr.dbreaka0", ICLASS_xt_iclass_xsr_dbreaka0,
    0,
    Opcode_xsr_dbreaka0_encode_fns, 0, 0 },
  { "rsr.dbreakc0", ICLASS_xt_iclass_rsr_dbreakc0,
    0,
    Opcode_rsr_dbreakc0_encode_fns, 0, 0 },
  { "wsr.dbreakc0", ICLASS_xt_iclass_wsr_dbreakc0,
    0,
    Opcode_wsr_dbreakc0_encode_fns, 0, 0 },
  { "xsr.dbreakc0", ICLASS_xt_iclass_xsr_dbreakc0,
    0,
    Opcode_xsr_dbreakc0_encode_fns, 0, 0 },
  { "rsr.dbreaka1", ICLASS_xt_iclass_rsr_dbreaka1,
    0,
    Opcode_rsr_dbreaka1_encode_fns, 0, 0 },
  { "wsr.dbreaka1", ICLASS_xt_iclass_wsr_dbreaka1,
    0,
    Opcode_wsr_dbreaka1_encode_fns, 0, 0 },
  { "xsr.dbreaka1", ICLASS_xt_iclass_xsr_dbreaka1,
    0,
    Opcode_xsr_dbreaka1_encode_fns, 0, 0 },
  { "rsr.dbreakc1", ICLASS_xt_iclass_rsr_dbreakc1,
    0,
    Opcode_rsr_dbreakc1_encode_fns, 0, 0 },
  { "wsr.dbreakc1", ICLASS_xt_iclass_wsr_dbreakc1,
    0,
    Opcode_wsr_dbreakc1_encode_fns, 0, 0 },
  { "xsr.dbreakc1", ICLASS_xt_iclass_xsr_dbreakc1,
    0,
    Opcode_xsr_dbreakc1_encode_fns, 0, 0 },
  { "rsr.ibreaka0", ICLASS_xt_iclass_rsr_ibreaka0,
    0,
    Opcode_rsr_ibreaka0_encode_fns, 0, 0 },
  { "wsr.ibreaka0", ICLASS_xt_iclass_wsr_ibreaka0,
    0,
    Opcode_wsr_ibreaka0_encode_fns, 0, 0 },
  { "xsr.ibreaka0", ICLASS_xt_iclass_xsr_ibreaka0,
    0,
    Opcode_xsr_ibreaka0_encode_fns, 0, 0 },
  { "rsr.ibreaka1", ICLASS_xt_iclass_rsr_ibreaka1,
    0,
    Opcode_rsr_ibreaka1_encode_fns, 0, 0 },
  { "wsr.ibreaka1", ICLASS_xt_iclass_wsr_ibreaka1,
    0,
    Opcode_wsr_ibreaka1_encode_fns, 0, 0 },
  { "xsr.ibreaka1", ICLASS_xt_iclass_xsr_ibreaka1,
    0,
    Opcode_xsr_ibreaka1_encode_fns, 0, 0 },
  { "rsr.ibreakenable", ICLASS_xt_iclass_rsr_ibreakenable,
    0,
    Opcode_rsr_ibreakenable_encode_fns, 0, 0 },
  { "wsr.ibreakenable", ICLASS_xt_iclass_wsr_ibreakenable,
    0,
    Opcode_wsr_ibreakenable_encode_fns, 0, 0 },
  { "xsr.ibreakenable", ICLASS_xt_iclass_xsr_ibreakenable,
    0,
    Opcode_xsr_ibreakenable_encode_fns, 0, 0 },
  { "rsr.debugcause", ICLASS_xt_iclass_rsr_debugcause,
    0,
    Opcode_rsr_debugcause_encode_fns, 0, 0 },
  { "wsr.debugcause", ICLASS_xt_iclass_wsr_debugcause,
    0,
    Opcode_wsr_debugcause_encode_fns, 0, 0 },
  { "xsr.debugcause", ICLASS_xt_iclass_xsr_debugcause,
    0,
    Opcode_xsr_debugcause_encode_fns, 0, 0 },
  { "rsr.icount", ICLASS_xt_iclass_rsr_icount,
    0,
    Opcode_rsr_icount_encode_fns, 0, 0 },
  { "wsr.icount", ICLASS_xt_iclass_wsr_icount,
    0,
    Opcode_wsr_icount_encode_fns, 0, 0 },
  { "xsr.icount", ICLASS_xt_iclass_xsr_icount,
    0,
    Opcode_xsr_icount_encode_fns, 0, 0 },
  { "rsr.icountlevel", ICLASS_xt_iclass_rsr_icountlevel,
    0,
    Opcode_rsr_icountlevel_encode_fns, 0, 0 },
  { "wsr.icountlevel", ICLASS_xt_iclass_wsr_icountlevel,
    0,
    Opcode_wsr_icountlevel_encode_fns, 0, 0 },
  { "xsr.icountlevel", ICLASS_xt_iclass_xsr_icountlevel,
    0,
    Opcode_xsr_icountlevel_encode_fns, 0, 0 },
  { "rsr.ddr", ICLASS_xt_iclass_rsr_ddr,
    0,
    Opcode_rsr_ddr_encode_fns, 0, 0 },
  { "wsr.ddr", ICLASS_xt_iclass_wsr_ddr,
    0,
    Opcode_wsr_ddr_encode_fns, 0, 0 },
  { "xsr.ddr", ICLASS_xt_iclass_xsr_ddr,
    0,
    Opcode_xsr_ddr_encode_fns, 0, 0 },
  { "rfdo", ICLASS_xt_iclass_rfdo,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_rfdo_encode_fns, 0, 0 },
  { "rfdd", ICLASS_xt_iclass_rfdd,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_rfdd_encode_fns, 0, 0 },
  { "wsr.mmid", ICLASS_xt_iclass_wsr_mmid,
    0,
    Opcode_wsr_mmid_encode_fns, 0, 0 },
  { "rsr.ccount", ICLASS_xt_iclass_rsr_ccount,
    0,
    Opcode_rsr_ccount_encode_fns, 0, 0 },
  { "wsr.ccount", ICLASS_xt_iclass_wsr_ccount,
    0,
    Opcode_wsr_ccount_encode_fns, 0, 0 },
  { "xsr.ccount", ICLASS_xt_iclass_xsr_ccount,
    0,
    Opcode_xsr_ccount_encode_fns, 0, 0 },
  { "rsr.ccompare0", ICLASS_xt_iclass_rsr_ccompare0,
    0,
    Opcode_rsr_ccompare0_encode_fns, 0, 0 },
  { "wsr.ccompare0", ICLASS_xt_iclass_wsr_ccompare0,
    0,
    Opcode_wsr_ccompare0_encode_fns, 0, 0 },
  { "xsr.ccompare0", ICLASS_xt_iclass_xsr_ccompare0,
    0,
    Opcode_xsr_ccompare0_encode_fns, 0, 0 },
  { "rsr.ccompare1", ICLASS_xt_iclass_rsr_ccompare1,
    0,
    Opcode_rsr_ccompare1_encode_fns, 0, 0 },
  { "wsr.ccompare1", ICLASS_xt_iclass_wsr_ccompare1,
    0,
    Opcode_wsr_ccompare1_encode_fns, 0, 0 },
  { "xsr.ccompare1", ICLASS_xt_iclass_xsr_ccompare1,
    0,
    Opcode_xsr_ccompare1_encode_fns, 0, 0 },
  { "rsr.ccompare2", ICLASS_xt_iclass_rsr_ccompare2,
    0,
    Opcode_rsr_ccompare2_encode_fns, 0, 0 },
  { "wsr.ccompare2", ICLASS_xt_iclass_wsr_ccompare2,
    0,
    Opcode_wsr_ccompare2_encode_fns, 0, 0 },
  { "xsr.ccompare2", ICLASS_xt_iclass_xsr_ccompare2,
    0,
    Opcode_xsr_ccompare2_encode_fns, 0, 0 },
  { "ipf", ICLASS_xt_iclass_icache,
    0,
    Opcode_ipf_encode_fns, 0, 0 },
  { "ihi", ICLASS_xt_iclass_icache,
    0,
    Opcode_ihi_encode_fns, 0, 0 },
  { "ipfl", ICLASS_xt_iclass_icache_lock,
    0,
    Opcode_ipfl_encode_fns, 0, 0 },
  { "ihu", ICLASS_xt_iclass_icache_lock,
    0,
    Opcode_ihu_encode_fns, 0, 0 },
  { "iiu", ICLASS_xt_iclass_icache_lock,
    0,
    Opcode_iiu_encode_fns, 0, 0 },
  { "iii", ICLASS_xt_iclass_icache_inv,
    0,
    Opcode_iii_encode_fns, 0, 0 },
  { "lict", ICLASS_xt_iclass_licx,
    0,
    Opcode_lict_encode_fns, 0, 0 },
  { "licw", ICLASS_xt_iclass_licx,
    0,
    Opcode_licw_encode_fns, 0, 0 },
  { "sict", ICLASS_xt_iclass_sicx,
    0,
    Opcode_sict_encode_fns, 0, 0 },
  { "sicw", ICLASS_xt_iclass_sicx,
    0,
    Opcode_sicw_encode_fns, 0, 0 },
  { "dhwb", ICLASS_xt_iclass_dcache,
    0,
    Opcode_dhwb_encode_fns, 0, 0 },
  { "dhwbi", ICLASS_xt_iclass_dcache,
    0,
    Opcode_dhwbi_encode_fns, 0, 0 },
  { "diwb", ICLASS_xt_iclass_dcache_ind,
    0,
    Opcode_diwb_encode_fns, 0, 0 },
  { "diwbi", ICLASS_xt_iclass_dcache_ind,
    0,
    Opcode_diwbi_encode_fns, 0, 0 },
  { "dhi", ICLASS_xt_iclass_dcache_inv,
    0,
    Opcode_dhi_encode_fns, 0, 0 },
  { "dii", ICLASS_xt_iclass_dcache_inv,
    0,
    Opcode_dii_encode_fns, 0, 0 },
  { "dpfr", ICLASS_xt_iclass_dpf,
    0,
    Opcode_dpfr_encode_fns, 0, 0 },
  { "dpfw", ICLASS_xt_iclass_dpf,
    0,
    Opcode_dpfw_encode_fns, 0, 0 },
  { "dpfro", ICLASS_xt_iclass_dpf,
    0,
    Opcode_dpfro_encode_fns, 0, 0 },
  { "dpfwo", ICLASS_xt_iclass_dpf,
    0,
    Opcode_dpfwo_encode_fns, 0, 0 },
  { "dpfl", ICLASS_xt_iclass_dcache_lock,
    0,
    Opcode_dpfl_encode_fns, 0, 0 },
  { "dhu", ICLASS_xt_iclass_dcache_lock,
    0,
    Opcode_dhu_encode_fns, 0, 0 },
  { "diu", ICLASS_xt_iclass_dcache_lock,
    0,
    Opcode_diu_encode_fns, 0, 0 },
  { "sdct", ICLASS_xt_iclass_sdct,
    0,
    Opcode_sdct_encode_fns, 0, 0 },
  { "ldct", ICLASS_xt_iclass_ldct,
    0,
    Opcode_ldct_encode_fns, 0, 0 },
  { "wsr.ptevaddr", ICLASS_xt_iclass_wsr_ptevaddr,
    0,
    Opcode_wsr_ptevaddr_encode_fns, 0, 0 },
  { "rsr.ptevaddr", ICLASS_xt_iclass_rsr_ptevaddr,
    0,
    Opcode_rsr_ptevaddr_encode_fns, 0, 0 },
  { "xsr.ptevaddr", ICLASS_xt_iclass_xsr_ptevaddr,
    0,
    Opcode_xsr_ptevaddr_encode_fns, 0, 0 },
  { "rsr.rasid", ICLASS_xt_iclass_rsr_rasid,
    0,
    Opcode_rsr_rasid_encode_fns, 0, 0 },
  { "wsr.rasid", ICLASS_xt_iclass_wsr_rasid,
    0,
    Opcode_wsr_rasid_encode_fns, 0, 0 },
  { "xsr.rasid", ICLASS_xt_iclass_xsr_rasid,
    0,
    Opcode_xsr_rasid_encode_fns, 0, 0 },
  { "rsr.itlbcfg", ICLASS_xt_iclass_rsr_itlbcfg,
    0,
    Opcode_rsr_itlbcfg_encode_fns, 0, 0 },
  { "wsr.itlbcfg", ICLASS_xt_iclass_wsr_itlbcfg,
    0,
    Opcode_wsr_itlbcfg_encode_fns, 0, 0 },
  { "xsr.itlbcfg", ICLASS_xt_iclass_xsr_itlbcfg,
    0,
    Opcode_xsr_itlbcfg_encode_fns, 0, 0 },
  { "rsr.dtlbcfg", ICLASS_xt_iclass_rsr_dtlbcfg,
    0,
    Opcode_rsr_dtlbcfg_encode_fns, 0, 0 },
  { "wsr.dtlbcfg", ICLASS_xt_iclass_wsr_dtlbcfg,
    0,
    Opcode_wsr_dtlbcfg_encode_fns, 0, 0 },
  { "xsr.dtlbcfg", ICLASS_xt_iclass_xsr_dtlbcfg,
    0,
    Opcode_xsr_dtlbcfg_encode_fns, 0, 0 },
  { "idtlb", ICLASS_xt_iclass_idtlb,
    0,
    Opcode_idtlb_encode_fns, 0, 0 },
  { "pdtlb", ICLASS_xt_iclass_rdtlb,
    0,
    Opcode_pdtlb_encode_fns, 0, 0 },
  { "rdtlb0", ICLASS_xt_iclass_rdtlb,
    0,
    Opcode_rdtlb0_encode_fns, 0, 0 },
  { "rdtlb1", ICLASS_xt_iclass_rdtlb,
    0,
    Opcode_rdtlb1_encode_fns, 0, 0 },
  { "wdtlb", ICLASS_xt_iclass_wdtlb,
    0,
    Opcode_wdtlb_encode_fns, 0, 0 },
  { "iitlb", ICLASS_xt_iclass_iitlb,
    0,
    Opcode_iitlb_encode_fns, 0, 0 },
  { "pitlb", ICLASS_xt_iclass_ritlb,
    0,
    Opcode_pitlb_encode_fns, 0, 0 },
  { "ritlb0", ICLASS_xt_iclass_ritlb,
    0,
    Opcode_ritlb0_encode_fns, 0, 0 },
  { "ritlb1", ICLASS_xt_iclass_ritlb,
    0,
    Opcode_ritlb1_encode_fns, 0, 0 },
  { "witlb", ICLASS_xt_iclass_witlb,
    0,
    Opcode_witlb_encode_fns, 0, 0 },
  { "ldpte", ICLASS_xt_iclass_ldpte,
    0,
    Opcode_ldpte_encode_fns, 0, 0 },
  { "hwwitlba", ICLASS_xt_iclass_hwwitlba,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_hwwitlba_encode_fns, 0, 0 },
  { "hwwdtlba", ICLASS_xt_iclass_hwwdtlba,
    0,
    Opcode_hwwdtlba_encode_fns, 0, 0 },
  { "rsr.cpenable", ICLASS_xt_iclass_rsr_cpenable,
    0,
    Opcode_rsr_cpenable_encode_fns, 0, 0 },
  { "wsr.cpenable", ICLASS_xt_iclass_wsr_cpenable,
    0,
    Opcode_wsr_cpenable_encode_fns, 0, 0 },
  { "xsr.cpenable", ICLASS_xt_iclass_xsr_cpenable,
    0,
    Opcode_xsr_cpenable_encode_fns, 0, 0 },
  { "clamps", ICLASS_xt_iclass_clamp,
    0,
    Opcode_clamps_encode_fns, 0, 0 },
  { "min", ICLASS_xt_iclass_minmax,
    0,
    Opcode_min_encode_fns, 0, 0 },
  { "max", ICLASS_xt_iclass_minmax,
    0,
    Opcode_max_encode_fns, 0, 0 },
  { "minu", ICLASS_xt_iclass_minmax,
    0,
    Opcode_minu_encode_fns, 0, 0 },
  { "maxu", ICLASS_xt_iclass_minmax,
    0,
    Opcode_maxu_encode_fns, 0, 0 },
  { "nsa", ICLASS_xt_iclass_nsa,
    0,
    Opcode_nsa_encode_fns, 0, 0 },
  { "nsau", ICLASS_xt_iclass_nsa,
    0,
    Opcode_nsau_encode_fns, 0, 0 },
  { "sext", ICLASS_xt_iclass_sx,
    0,
    Opcode_sext_encode_fns, 0, 0 },
  { "l32ai", ICLASS_xt_iclass_l32ai,
    0,
    Opcode_l32ai_encode_fns, 0, 0 },
  { "s32ri", ICLASS_xt_iclass_s32ri,
    0,
    Opcode_s32ri_encode_fns, 0, 0 },
  { "s32c1i", ICLASS_xt_iclass_s32c1i,
    0,
    Opcode_s32c1i_encode_fns, 0, 0 },
  { "rsr.scompare1", ICLASS_xt_iclass_rsr_scompare1,
    0,
    Opcode_rsr_scompare1_encode_fns, 0, 0 },
  { "wsr.scompare1", ICLASS_xt_iclass_wsr_scompare1,
    0,
    Opcode_wsr_scompare1_encode_fns, 0, 0 },
  { "xsr.scompare1", ICLASS_xt_iclass_xsr_scompare1,
    0,
    Opcode_xsr_scompare1_encode_fns, 0, 0 },
  { "rsr.atomctl", ICLASS_xt_iclass_rsr_atomctl,
    0,
    Opcode_rsr_atomctl_encode_fns, 0, 0 },
  { "wsr.atomctl", ICLASS_xt_iclass_wsr_atomctl,
    0,
    Opcode_wsr_atomctl_encode_fns, 0, 0 },
  { "xsr.atomctl", ICLASS_xt_iclass_xsr_atomctl,
    0,
    Opcode_xsr_atomctl_encode_fns, 0, 0 },
  { "quou", ICLASS_xt_iclass_div,
    0,
    Opcode_quou_encode_fns, 0, 0 },
  { "quos", ICLASS_xt_iclass_div,
    0,
    Opcode_quos_encode_fns, 0, 0 },
  { "remu", ICLASS_xt_iclass_div,
    0,
    Opcode_remu_encode_fns, 0, 0 },
  { "rems", ICLASS_xt_iclass_div,
    0,
    Opcode_rems_encode_fns, 0, 0 },
  { "rer", ICLASS_xt_iclass_rer,
    0,
    Opcode_rer_encode_fns, 0, 0 },
  { "wer", ICLASS_xt_iclass_wer,
    0,
    Opcode_wer_encode_fns, 0, 0 },
  { "rur.expstate", ICLASS_rur_expstate,
    0,
    Opcode_rur_expstate_encode_fns, 0, 0 },
  { "wur.expstate", ICLASS_wur_expstate,
    0,
    Opcode_wur_expstate_encode_fns, 0, 0 },
  { "read_impwire", ICLASS_iclass_READ_IMPWIRE,
    0,
    Opcode_read_impwire_encode_fns, 0, 0 },
  { "setb_expstate", ICLASS_iclass_SETB_EXPSTATE,
    0,
    Opcode_setb_expstate_encode_fns, 0, 0 },
  { "clrb_expstate", ICLASS_iclass_CLRB_EXPSTATE,
    0,
    Opcode_clrb_expstate_encode_fns, 0, 0 },
  { "wrmsk_expstate", ICLASS_iclass_WRMSK_EXPSTATE,
    0,
    Opcode_wrmsk_expstate_encode_fns, 0, 0 }
};

enum xtensa_opcode_id {
  OPCODE_EXCW,
  OPCODE_RFE,
  OPCODE_RFDE,
  OPCODE_SYSCALL,
  OPCODE_SIMCALL,
  OPCODE_CALL12,
  OPCODE_CALL8,
  OPCODE_CALL4,
  OPCODE_CALLX12,
  OPCODE_CALLX8,
  OPCODE_CALLX4,
  OPCODE_ENTRY,
  OPCODE_MOVSP,
  OPCODE_ROTW,
  OPCODE_RETW,
  OPCODE_RETW_N,
  OPCODE_RFWO,
  OPCODE_RFWU,
  OPCODE_L32E,
  OPCODE_S32E,
  OPCODE_RSR_WINDOWBASE,
  OPCODE_WSR_WINDOWBASE,
  OPCODE_XSR_WINDOWBASE,
  OPCODE_RSR_WINDOWSTART,
  OPCODE_WSR_WINDOWSTART,
  OPCODE_XSR_WINDOWSTART,
  OPCODE_ADD_N,
  OPCODE_ADDI_N,
  OPCODE_BEQZ_N,
  OPCODE_BNEZ_N,
  OPCODE_ILL_N,
  OPCODE_L32I_N,
  OPCODE_MOV_N,
  OPCODE_MOVI_N,
  OPCODE_NOP_N,
  OPCODE_RET_N,
  OPCODE_S32I_N,
  OPCODE_RUR_THREADPTR,
  OPCODE_WUR_THREADPTR,
  OPCODE_ADDI,
  OPCODE_ADDMI,
  OPCODE_ADD,
  OPCODE_SUB,
  OPCODE_ADDX2,
  OPCODE_ADDX4,
  OPCODE_ADDX8,
  OPCODE_SUBX2,
  OPCODE_SUBX4,
  OPCODE_SUBX8,
  OPCODE_AND,
  OPCODE_OR,
  OPCODE_XOR,
  OPCODE_BEQI,
  OPCODE_BNEI,
  OPCODE_BGEI,
  OPCODE_BLTI,
  OPCODE_BBCI,
  OPCODE_BBSI,
  OPCODE_BGEUI,
  OPCODE_BLTUI,
  OPCODE_BEQ,
  OPCODE_BNE,
  OPCODE_BGE,
  OPCODE_BLT,
  OPCODE_BGEU,
  OPCODE_BLTU,
  OPCODE_BANY,
  OPCODE_BNONE,
  OPCODE_BALL,
  OPCODE_BNALL,
  OPCODE_BBC,
  OPCODE_BBS,
  OPCODE_BEQZ,
  OPCODE_BNEZ,
  OPCODE_BGEZ,
  OPCODE_BLTZ,
  OPCODE_CALL0,
  OPCODE_CALLX0,
  OPCODE_EXTUI,
  OPCODE_ILL,
  OPCODE_J,
  OPCODE_JX,
  OPCODE_L16UI,
  OPCODE_L16SI,
  OPCODE_L32I,
  OPCODE_L32R,
  OPCODE_L8UI,
  OPCODE_LOOP,
  OPCODE_LOOPNEZ,
  OPCODE_LOOPGTZ,
  OPCODE_MOVI,
  OPCODE_MOVEQZ,
  OPCODE_MOVNEZ,
  OPCODE_MOVLTZ,
  OPCODE_MOVGEZ,
  OPCODE_NEG,
  OPCODE_ABS,
  OPCODE_NOP,
  OPCODE_RET,
  OPCODE_S16I,
  OPCODE_S32I,
  OPCODE_S8I,
  OPCODE_SSR,
  OPCODE_SSL,
  OPCODE_SSA8L,
  OPCODE_SSA8B,
  OPCODE_SSAI,
  OPCODE_SLL,
  OPCODE_SRC,
  OPCODE_SRL,
  OPCODE_SRA,
  OPCODE_SLLI,
  OPCODE_SRAI,
  OPCODE_SRLI,
  OPCODE_MEMW,
  OPCODE_EXTW,
  OPCODE_ISYNC,
  OPCODE_RSYNC,
  OPCODE_ESYNC,
  OPCODE_DSYNC,
  OPCODE_RSIL,
  OPCODE_RSR_LEND,
  OPCODE_WSR_LEND,
  OPCODE_XSR_LEND,
  OPCODE_RSR_LCOUNT,
  OPCODE_WSR_LCOUNT,
  OPCODE_XSR_LCOUNT,
  OPCODE_RSR_LBEG,
  OPCODE_WSR_LBEG,
  OPCODE_XSR_LBEG,
  OPCODE_RSR_SAR,
  OPCODE_WSR_SAR,
  OPCODE_XSR_SAR,
  OPCODE_RSR_LITBASE,
  OPCODE_WSR_LITBASE,
  OPCODE_XSR_LITBASE,
  OPCODE_RSR_176,
  OPCODE_WSR_176,
  OPCODE_RSR_208,
  OPCODE_RSR_PS,
  OPCODE_WSR_PS,
  OPCODE_XSR_PS,
  OPCODE_RSR_EPC1,
  OPCODE_WSR_EPC1,
  OPCODE_XSR_EPC1,
  OPCODE_RSR_EXCSAVE1,
  OPCODE_WSR_EXCSAVE1,
  OPCODE_XSR_EXCSAVE1,
  OPCODE_RSR_EPC2,
  OPCODE_WSR_EPC2,
  OPCODE_XSR_EPC2,
  OPCODE_RSR_EXCSAVE2,
  OPCODE_WSR_EXCSAVE2,
  OPCODE_XSR_EXCSAVE2,
  OPCODE_RSR_EPC3,
  OPCODE_WSR_EPC3,
  OPCODE_XSR_EPC3,
  OPCODE_RSR_EXCSAVE3,
  OPCODE_WSR_EXCSAVE3,
  OPCODE_XSR_EXCSAVE3,
  OPCODE_RSR_EPC4,
  OPCODE_WSR_EPC4,
  OPCODE_XSR_EPC4,
  OPCODE_RSR_EXCSAVE4,
  OPCODE_WSR_EXCSAVE4,
  OPCODE_XSR_EXCSAVE4,
  OPCODE_RSR_EPC5,
  OPCODE_WSR_EPC5,
  OPCODE_XSR_EPC5,
  OPCODE_RSR_EXCSAVE5,
  OPCODE_WSR_EXCSAVE5,
  OPCODE_XSR_EXCSAVE5,
  OPCODE_RSR_EPC6,
  OPCODE_WSR_EPC6,
  OPCODE_XSR_EPC6,
  OPCODE_RSR_EXCSAVE6,
  OPCODE_WSR_EXCSAVE6,
  OPCODE_XSR_EXCSAVE6,
  OPCODE_RSR_EPC7,
  OPCODE_WSR_EPC7,
  OPCODE_XSR_EPC7,
  OPCODE_RSR_EXCSAVE7,
  OPCODE_WSR_EXCSAVE7,
  OPCODE_XSR_EXCSAVE7,
  OPCODE_RSR_EPS2,
  OPCODE_WSR_EPS2,
  OPCODE_XSR_EPS2,
  OPCODE_RSR_EPS3,
  OPCODE_WSR_EPS3,
  OPCODE_XSR_EPS3,
  OPCODE_RSR_EPS4,
  OPCODE_WSR_EPS4,
  OPCODE_XSR_EPS4,
  OPCODE_RSR_EPS5,
  OPCODE_WSR_EPS5,
  OPCODE_XSR_EPS5,
  OPCODE_RSR_EPS6,
  OPCODE_WSR_EPS6,
  OPCODE_XSR_EPS6,
  OPCODE_RSR_EPS7,
  OPCODE_WSR_EPS7,
  OPCODE_XSR_EPS7,
  OPCODE_RSR_EXCVADDR,
  OPCODE_WSR_EXCVADDR,
  OPCODE_XSR_EXCVADDR,
  OPCODE_RSR_DEPC,
  OPCODE_WSR_DEPC,
  OPCODE_XSR_DEPC,
  OPCODE_RSR_EXCCAUSE,
  OPCODE_WSR_EXCCAUSE,
  OPCODE_XSR_EXCCAUSE,
  OPCODE_RSR_MISC0,
  OPCODE_WSR_MISC0,
  OPCODE_XSR_MISC0,
  OPCODE_RSR_MISC1,
  OPCODE_WSR_MISC1,
  OPCODE_XSR_MISC1,
  OPCODE_RSR_PRID,
  OPCODE_RSR_VECBASE,
  OPCODE_WSR_VECBASE,
  OPCODE_XSR_VECBASE,
  OPCODE_MUL16U,
  OPCODE_MUL16S,
  OPCODE_MULL,
  OPCODE_MUL_AA_LL,
  OPCODE_MUL_AA_HL,
  OPCODE_MUL_AA_LH,
  OPCODE_MUL_AA_HH,
  OPCODE_UMUL_AA_LL,
  OPCODE_UMUL_AA_HL,
  OPCODE_UMUL_AA_LH,
  OPCODE_UMUL_AA_HH,
  OPCODE_MUL_AD_LL,
  OPCODE_MUL_AD_HL,
  OPCODE_MUL_AD_LH,
  OPCODE_MUL_AD_HH,
  OPCODE_MUL_DA_LL,
  OPCODE_MUL_DA_HL,
  OPCODE_MUL_DA_LH,
  OPCODE_MUL_DA_HH,
  OPCODE_MUL_DD_LL,
  OPCODE_MUL_DD_HL,
  OPCODE_MUL_DD_LH,
  OPCODE_MUL_DD_HH,
  OPCODE_MULA_AA_LL,
  OPCODE_MULA_AA_HL,
  OPCODE_MULA_AA_LH,
  OPCODE_MULA_AA_HH,
  OPCODE_MULS_AA_LL,
  OPCODE_MULS_AA_HL,
  OPCODE_MULS_AA_LH,
  OPCODE_MULS_AA_HH,
  OPCODE_MULA_AD_LL,
  OPCODE_MULA_AD_HL,
  OPCODE_MULA_AD_LH,
  OPCODE_MULA_AD_HH,
  OPCODE_MULS_AD_LL,
  OPCODE_MULS_AD_HL,
  OPCODE_MULS_AD_LH,
  OPCODE_MULS_AD_HH,
  OPCODE_MULA_DA_LL,
  OPCODE_MULA_DA_HL,
  OPCODE_MULA_DA_LH,
  OPCODE_MULA_DA_HH,
  OPCODE_MULS_DA_LL,
  OPCODE_MULS_DA_HL,
  OPCODE_MULS_DA_LH,
  OPCODE_MULS_DA_HH,
  OPCODE_MULA_DD_LL,
  OPCODE_MULA_DD_HL,
  OPCODE_MULA_DD_LH,
  OPCODE_MULA_DD_HH,
  OPCODE_MULS_DD_LL,
  OPCODE_MULS_DD_HL,
  OPCODE_MULS_DD_LH,
  OPCODE_MULS_DD_HH,
  OPCODE_MULA_DA_LL_LDDEC,
  OPCODE_MULA_DA_LL_LDINC,
  OPCODE_MULA_DA_HL_LDDEC,
  OPCODE_MULA_DA_HL_LDINC,
  OPCODE_MULA_DA_LH_LDDEC,
  OPCODE_MULA_DA_LH_LDINC,
  OPCODE_MULA_DA_HH_LDDEC,
  OPCODE_MULA_DA_HH_LDINC,
  OPCODE_MULA_DD_LL_LDDEC,
  OPCODE_MULA_DD_LL_LDINC,
  OPCODE_MULA_DD_HL_LDDEC,
  OPCODE_MULA_DD_HL_LDINC,
  OPCODE_MULA_DD_LH_LDDEC,
  OPCODE_MULA_DD_LH_LDINC,
  OPCODE_MULA_DD_HH_LDDEC,
  OPCODE_MULA_DD_HH_LDINC,
  OPCODE_LDDEC,
  OPCODE_LDINC,
  OPCODE_RSR_M0,
  OPCODE_WSR_M0,
  OPCODE_XSR_M0,
  OPCODE_RSR_M1,
  OPCODE_WSR_M1,
  OPCODE_XSR_M1,
  OPCODE_RSR_M2,
  OPCODE_WSR_M2,
  OPCODE_XSR_M2,
  OPCODE_RSR_M3,
  OPCODE_WSR_M3,
  OPCODE_XSR_M3,
  OPCODE_RSR_ACCLO,
  OPCODE_WSR_ACCLO,
  OPCODE_XSR_ACCLO,
  OPCODE_RSR_ACCHI,
  OPCODE_WSR_ACCHI,
  OPCODE_XSR_ACCHI,
  OPCODE_RFI,
  OPCODE_WAITI,
  OPCODE_RSR_INTERRUPT,
  OPCODE_WSR_INTSET,
  OPCODE_WSR_INTCLEAR,
  OPCODE_RSR_INTENABLE,
  OPCODE_WSR_INTENABLE,
  OPCODE_XSR_INTENABLE,
  OPCODE_BREAK,
  OPCODE_BREAK_N,
  OPCODE_RSR_DBREAKA0,
  OPCODE_WSR_DBREAKA0,
  OPCODE_XSR_DBREAKA0,
  OPCODE_RSR_DBREAKC0,
  OPCODE_WSR_DBREAKC0,
  OPCODE_XSR_DBREAKC0,
  OPCODE_RSR_DBREAKA1,
  OPCODE_WSR_DBREAKA1,
  OPCODE_XSR_DBREAKA1,
  OPCODE_RSR_DBREAKC1,
  OPCODE_WSR_DBREAKC1,
  OPCODE_XSR_DBREAKC1,
  OPCODE_RSR_IBREAKA0,
  OPCODE_WSR_IBREAKA0,
  OPCODE_XSR_IBREAKA0,
  OPCODE_RSR_IBREAKA1,
  OPCODE_WSR_IBREAKA1,
  OPCODE_XSR_IBREAKA1,
  OPCODE_RSR_IBREAKENABLE,
  OPCODE_WSR_IBREAKENABLE,
  OPCODE_XSR_IBREAKENABLE,
  OPCODE_RSR_DEBUGCAUSE,
  OPCODE_WSR_DEBUGCAUSE,
  OPCODE_XSR_DEBUGCAUSE,
  OPCODE_RSR_ICOUNT,
  OPCODE_WSR_ICOUNT,
  OPCODE_XSR_ICOUNT,
  OPCODE_RSR_ICOUNTLEVEL,
  OPCODE_WSR_ICOUNTLEVEL,
  OPCODE_XSR_ICOUNTLEVEL,
  OPCODE_RSR_DDR,
  OPCODE_WSR_DDR,
  OPCODE_XSR_DDR,
  OPCODE_RFDO,
  OPCODE_RFDD,
  OPCODE_WSR_MMID,
  OPCODE_RSR_CCOUNT,
  OPCODE_WSR_CCOUNT,
  OPCODE_XSR_CCOUNT,
  OPCODE_RSR_CCOMPARE0,
  OPCODE_WSR_CCOMPARE0,
  OPCODE_XSR_CCOMPARE0,
  OPCODE_RSR_CCOMPARE1,
  OPCODE_WSR_CCOMPARE1,
  OPCODE_XSR_CCOMPARE1,
  OPCODE_RSR_CCOMPARE2,
  OPCODE_WSR_CCOMPARE2,
  OPCODE_XSR_CCOMPARE2,
  OPCODE_IPF,
  OPCODE_IHI,
  OPCODE_IPFL,
  OPCODE_IHU,
  OPCODE_IIU,
  OPCODE_III,
  OPCODE_LICT,
  OPCODE_LICW,
  OPCODE_SICT,
  OPCODE_SICW,
  OPCODE_DHWB,
  OPCODE_DHWBI,
  OPCODE_DIWB,
  OPCODE_DIWBI,
  OPCODE_DHI,
  OPCODE_DII,
  OPCODE_DPFR,
  OPCODE_DPFW,
  OPCODE_DPFRO,
  OPCODE_DPFWO,
  OPCODE_DPFL,
  OPCODE_DHU,
  OPCODE_DIU,
  OPCODE_SDCT,
  OPCODE_LDCT,
  OPCODE_WSR_PTEVADDR,
  OPCODE_RSR_PTEVADDR,
  OPCODE_XSR_PTEVADDR,
  OPCODE_RSR_RASID,
  OPCODE_WSR_RASID,
  OPCODE_XSR_RASID,
  OPCODE_RSR_ITLBCFG,
  OPCODE_WSR_ITLBCFG,
  OPCODE_XSR_ITLBCFG,
  OPCODE_RSR_DTLBCFG,
  OPCODE_WSR_DTLBCFG,
  OPCODE_XSR_DTLBCFG,
  OPCODE_IDTLB,
  OPCODE_PDTLB,
  OPCODE_RDTLB0,
  OPCODE_RDTLB1,
  OPCODE_WDTLB,
  OPCODE_IITLB,
  OPCODE_PITLB,
  OPCODE_RITLB0,
  OPCODE_RITLB1,
  OPCODE_WITLB,
  OPCODE_LDPTE,
  OPCODE_HWWITLBA,
  OPCODE_HWWDTLBA,
  OPCODE_RSR_CPENABLE,
  OPCODE_WSR_CPENABLE,
  OPCODE_XSR_CPENABLE,
  OPCODE_CLAMPS,
  OPCODE_MIN,
  OPCODE_MAX,
  OPCODE_MINU,
  OPCODE_MAXU,
  OPCODE_NSA,
  OPCODE_NSAU,
  OPCODE_SEXT,
  OPCODE_L32AI,
  OPCODE_S32RI,
  OPCODE_S32C1I,
  OPCODE_RSR_SCOMPARE1,
  OPCODE_WSR_SCOMPARE1,
  OPCODE_XSR_SCOMPARE1,
  OPCODE_RSR_ATOMCTL,
  OPCODE_WSR_ATOMCTL,
  OPCODE_XSR_ATOMCTL,
  OPCODE_QUOU,
  OPCODE_QUOS,
  OPCODE_REMU,
  OPCODE_REMS,
  OPCODE_RER,
  OPCODE_WER,
  OPCODE_RUR_EXPSTATE,
  OPCODE_WUR_EXPSTATE,
  OPCODE_READ_IMPWIRE,
  OPCODE_SETB_EXPSTATE,
  OPCODE_CLRB_EXPSTATE,
  OPCODE_WRMSK_EXPSTATE
};


/* Slot-specific opcode decode functions.  */

static int
Slot_inst_decode (const xtensa_insnbuf insn)
{
  switch (Field_op0_Slot_inst_get (insn))
    {
    case 0:
      switch (Field_op1_Slot_inst_get (insn))
	{
	case 0:
	  switch (Field_op2_Slot_inst_get (insn))
	    {
	    case 0:
	      switch (Field_r_Slot_inst_get (insn))
		{
		case 0:
		  switch (Field_m_Slot_inst_get (insn))
		    {
		    case 0:
		      if (Field_s_Slot_inst_get (insn) == 0 &&
			  Field_n_Slot_inst_get (insn) == 0)
			return OPCODE_ILL;
		      break;
		    case 2:
		      switch (Field_n_Slot_inst_get (insn))
			{
			case 0:
			  return OPCODE_RET;
			case 1:
			  return OPCODE_RETW;
			case 2:
			  return OPCODE_JX;
			}
		      break;
		    case 3:
		      switch (Field_n_Slot_inst_get (insn))
			{
			case 0:
			  return OPCODE_CALLX0;
			case 1:
			  return OPCODE_CALLX4;
			case 2:
			  return OPCODE_CALLX8;
			case 3:
			  return OPCODE_CALLX12;
			}
		      break;
		    }
		  break;
		case 1:
		  return OPCODE_MOVSP;
		case 2:
		  if (Field_s_Slot_inst_get (insn) == 0)
		    {
		      switch (Field_t_Slot_inst_get (insn))
			{
			case 0:
			  return OPCODE_ISYNC;
			case 1:
			  return OPCODE_RSYNC;
			case 2:
			  return OPCODE_ESYNC;
			case 3:
			  return OPCODE_DSYNC;
			case 8:
			  return OPCODE_EXCW;
			case 12:
			  return OPCODE_MEMW;
			case 13:
			  return OPCODE_EXTW;
			case 15:
			  return OPCODE_NOP;
			}
		    }
		  break;
		case 3:
		  switch (Field_t_Slot_inst_get (insn))
		    {
		    case 0:
		      switch (Field_s_Slot_inst_get (insn))
			{
			case 0:
			  return OPCODE_RFE;
			case 2:
			  return OPCODE_RFDE;
			case 4:
			  return OPCODE_RFWO;
			case 5:
			  return OPCODE_RFWU;
			}
		      break;
		    case 1:
		      return OPCODE_RFI;
		    }
		  break;
		case 4:
		  return OPCODE_BREAK;
		case 5:
		  switch (Field_s_Slot_inst_get (insn))
		    {
		    case 0:
		      if (Field_t_Slot_inst_get (insn) == 0)
			return OPCODE_SYSCALL;
		      break;
		    case 1:
		      if (Field_t_Slot_inst_get (insn) == 0)
			return OPCODE_SIMCALL;
		      break;
		    }
		  break;
		case 6:
		  return OPCODE_RSIL;
		case 7:
		  if (Field_t_Slot_inst_get (insn) == 0)
		    return OPCODE_WAITI;
		  break;
		}
	      break;
	    case 1:
	      return OPCODE_AND;
	    case 2:
	      return OPCODE_OR;
	    case 3:
	      return OPCODE_XOR;
	    case 4:
	      switch (Field_r_Slot_inst_get (insn))
		{
		case 0:
		  if (Field_t_Slot_inst_get (insn) == 0)
		    return OPCODE_SSR;
		  break;
		case 1:
		  if (Field_t_Slot_inst_get (insn) == 0)
		    return OPCODE_SSL;
		  break;
		case 2:
		  if (Field_t_Slot_inst_get (insn) == 0)
		    return OPCODE_SSA8L;
		  break;
		case 3:
		  if (Field_t_Slot_inst_get (insn) == 0)
		    return OPCODE_SSA8B;
		  break;
		case 4:
		  if (Field_thi3_Slot_inst_get (insn) == 0)
		    return OPCODE_SSAI;
		  break;
		case 6:
		  return OPCODE_RER;
		case 7:
		  return OPCODE_WER;
		case 8:
		  if (Field_s_Slot_inst_get (insn) == 0)
		    return OPCODE_ROTW;
		  break;
		case 14:
		  return OPCODE_NSA;
		case 15:
		  return OPCODE_NSAU;
		}
	      break;
	    case 5:
	      switch (Field_r_Slot_inst_get (insn))
		{
		case 1:
		  return OPCODE_HWWITLBA;
		case 3:
		  return OPCODE_RITLB0;
		case 4:
		  if (Field_t_Slot_inst_get (insn) == 0)
		    return OPCODE_IITLB;
		  break;
		case 5:
		  return OPCODE_PITLB;
		case 6:
		  return OPCODE_WITLB;
		case 7:
		  return OPCODE_RITLB1;
		case 9:
		  return OPCODE_HWWDTLBA;
		case 11:
		  return OPCODE_RDTLB0;
		case 12:
		  if (Field_t_Slot_inst_get (insn) == 0)
		    return OPCODE_IDTLB;
		  break;
		case 13:
		  return OPCODE_PDTLB;
		case 14:
		  return OPCODE_WDTLB;
		case 15:
		  return OPCODE_RDTLB1;
		}
	      break;
	    case 6:
	      switch (Field_s_Slot_inst_get (insn))
		{
		case 0:
		  return OPCODE_NEG;
		case 1:
		  return OPCODE_ABS;
		}
	      break;
	    case 8:
	      return OPCODE_ADD;
	    case 9:
	      return OPCODE_ADDX2;
	    case 10:
	      return OPCODE_ADDX4;
	    case 11:
	      return OPCODE_ADDX8;
	    case 12:
	      return OPCODE_SUB;
	    case 13:
	      return OPCODE_SUBX2;
	    case 14:
	      return OPCODE_SUBX4;
	    case 15:
	      return OPCODE_SUBX8;
	    }
	  break;
	case 1:
	  switch (Field_op2_Slot_inst_get (insn))
	    {
	    case 0:
	    case 1:
	      return OPCODE_SLLI;
	    case 2:
	    case 3:
	      return OPCODE_SRAI;
	    case 4:
	      return OPCODE_SRLI;
	    case 6:
	      switch (Field_sr_Slot_inst_get (insn))
		{
		case 0:
		  return OPCODE_XSR_LBEG;
		case 1:
		  return OPCODE_XSR_LEND;
		case 2:
		  return OPCODE_XSR_LCOUNT;
		case 3:
		  return OPCODE_XSR_SAR;
		case 5:
		  return OPCODE_XSR_LITBASE;
		case 12:
		  return OPCODE_XSR_SCOMPARE1;
		case 16:
		  return OPCODE_XSR_ACCLO;
		case 17:
		  return OPCODE_XSR_ACCHI;
		case 32:
		  return OPCODE_XSR_M0;
		case 33:
		  return OPCODE_XSR_M1;
		case 34:
		  return OPCODE_XSR_M2;
		case 35:
		  return OPCODE_XSR_M3;
		case 72:
		  return OPCODE_XSR_WINDOWBASE;
		case 73:
		  return OPCODE_XSR_WINDOWSTART;
		case 83:
		  return OPCODE_XSR_PTEVADDR;
		case 90:
		  return OPCODE_XSR_RASID;
		case 91:
		  return OPCODE_XSR_ITLBCFG;
		case 92:
		  return OPCODE_XSR_DTLBCFG;
		case 96:
		  return OPCODE_XSR_IBREAKENABLE;
		case 99:
		  return OPCODE_XSR_ATOMCTL;
		case 104:
		  return OPCODE_XSR_DDR;
		case 128:
		  return OPCODE_XSR_IBREAKA0;
		case 129:
		  return OPCODE_XSR_IBREAKA1;
		case 144:
		  return OPCODE_XSR_DBREAKA0;
		case 145:
		  return OPCODE_XSR_DBREAKA1;
		case 160:
		  return OPCODE_XSR_DBREAKC0;
		case 161:
		  return OPCODE_XSR_DBREAKC1;
		case 177:
		  return OPCODE_XSR_EPC1;
		case 178:
		  return OPCODE_XSR_EPC2;
		case 179:
		  return OPCODE_XSR_EPC3;
		case 180:
		  return OPCODE_XSR_EPC4;
		case 181:
		  return OPCODE_XSR_EPC5;
		case 182:
		  return OPCODE_XSR_EPC6;
		case 183:
		  return OPCODE_XSR_EPC7;
		case 192:
		  return OPCODE_XSR_DEPC;
		case 194:
		  return OPCODE_XSR_EPS2;
		case 195:
		  return OPCODE_XSR_EPS3;
		case 196:
		  return OPCODE_XSR_EPS4;
		case 197:
		  return OPCODE_XSR_EPS5;
		case 198:
		  return OPCODE_XSR_EPS6;
		case 199:
		  return OPCODE_XSR_EPS7;
		case 209:
		  return OPCODE_XSR_EXCSAVE1;
		case 210:
		  return OPCODE_XSR_EXCSAVE2;
		case 211:
		  return OPCODE_XSR_EXCSAVE3;
		case 212:
		  return OPCODE_XSR_EXCSAVE4;
		case 213:
		  return OPCODE_XSR_EXCSAVE5;
		case 214:
		  return OPCODE_XSR_EXCSAVE6;
		case 215:
		  return OPCODE_XSR_EXCSAVE7;
		case 224:
		  return OPCODE_XSR_CPENABLE;
		case 228:
		  return OPCODE_XSR_INTENABLE;
		case 230:
		  return OPCODE_XSR_PS;
		case 231:
		  return OPCODE_XSR_VECBASE;
		case 232:
		  return OPCODE_XSR_EXCCAUSE;
		case 233:
		  return OPCODE_XSR_DEBUGCAUSE;
		case 234:
		  return OPCODE_XSR_CCOUNT;
		case 236:
		  return OPCODE_XSR_ICOUNT;
		case 237:
		  return OPCODE_XSR_ICOUNTLEVEL;
		case 238:
		  return OPCODE_XSR_EXCVADDR;
		case 240:
		  return OPCODE_XSR_CCOMPARE0;
		case 241:
		  return OPCODE_XSR_CCOMPARE1;
		case 242:
		  return OPCODE_XSR_CCOMPARE2;
		case 244:
		  return OPCODE_XSR_MISC0;
		case 245:
		  return OPCODE_XSR_MISC1;
		}
	      break;
	    case 8:
	      return OPCODE_SRC;
	    case 9:
	      if (Field_s_Slot_inst_get (insn) == 0)
		return OPCODE_SRL;
	      break;
	    case 10:
	      if (Field_t_Slot_inst_get (insn) == 0)
		return OPCODE_SLL;
	      break;
	    case 11:
	      if (Field_s_Slot_inst_get (insn) == 0)
		return OPCODE_SRA;
	      break;
	    case 12:
	      return OPCODE_MUL16U;
	    case 13:
	      return OPCODE_MUL16S;
	    case 15:
	      switch (Field_r_Slot_inst_get (insn))
		{
		case 0:
		  return OPCODE_LICT;
		case 1:
		  return OPCODE_SICT;
		case 2:
		  return OPCODE_LICW;
		case 3:
		  return OPCODE_SICW;
		case 8:
		  return OPCODE_LDCT;
		case 9:
		  return OPCODE_SDCT;
		case 14:
		  if (Field_t_Slot_inst_get (insn) == 0)
		    return OPCODE_RFDO;
		  if (Field_t_Slot_inst_get (insn) == 1)
		    return OPCODE_RFDD;
		  break;
		case 15:
		  return OPCODE_LDPTE;
		}
	      break;
	    }
	  break;
	case 2:
	  switch (Field_op2_Slot_inst_get (insn))
	    {
	    case 8:
	      return OPCODE_MULL;
	    case 12:
	      return OPCODE_QUOU;
	    case 13:
	      return OPCODE_QUOS;
	    case 14:
	      return OPCODE_REMU;
	    case 15:
	      return OPCODE_REMS;
	    }
	  break;
	case 3:
	  switch (Field_op2_Slot_inst_get (insn))
	    {
	    case 0:
	      switch (Field_sr_Slot_inst_get (insn))
		{
		case 0:
		  return OPCODE_RSR_LBEG;
		case 1:
		  return OPCODE_RSR_LEND;
		case 2:
		  return OPCODE_RSR_LCOUNT;
		case 3:
		  return OPCODE_RSR_SAR;
		case 5:
		  return OPCODE_RSR_LITBASE;
		case 12:
		  return OPCODE_RSR_SCOMPARE1;
		case 16:
		  return OPCODE_RSR_ACCLO;
		case 17:
		  return OPCODE_RSR_ACCHI;
		case 32:
		  return OPCODE_RSR_M0;
		case 33:
		  return OPCODE_RSR_M1;
		case 34:
		  return OPCODE_RSR_M2;
		case 35:
		  return OPCODE_RSR_M3;
		case 72:
		  return OPCODE_RSR_WINDOWBASE;
		case 73:
		  return OPCODE_RSR_WINDOWSTART;
		case 83:
		  return OPCODE_RSR_PTEVADDR;
		case 90:
		  return OPCODE_RSR_RASID;
		case 91:
		  return OPCODE_RSR_ITLBCFG;
		case 92:
		  return OPCODE_RSR_DTLBCFG;
		case 96:
		  return OPCODE_RSR_IBREAKENABLE;
		case 99:
		  return OPCODE_RSR_ATOMCTL;
		case 104:
		  return OPCODE_RSR_DDR;
		case 128:
		  return OPCODE_RSR_IBREAKA0;
		case 129:
		  return OPCODE_RSR_IBREAKA1;
		case 144:
		  return OPCODE_RSR_DBREAKA0;
		case 145:
		  return OPCODE_RSR_DBREAKA1;
		case 160:
		  return OPCODE_RSR_DBREAKC0;
		case 161:
		  return OPCODE_RSR_DBREAKC1;
		case 176:
		  return OPCODE_RSR_176;
		case 177:
		  return OPCODE_RSR_EPC1;
		case 178:
		  return OPCODE_RSR_EPC2;
		case 179:
		  return OPCODE_RSR_EPC3;
		case 180:
		  return OPCODE_RSR_EPC4;
		case 181:
		  return OPCODE_RSR_EPC5;
		case 182:
		  return OPCODE_RSR_EPC6;
		case 183:
		  return OPCODE_RSR_EPC7;
		case 192:
		  return OPCODE_RSR_DEPC;
		case 194:
		  return OPCODE_RSR_EPS2;
		case 195:
		  return OPCODE_RSR_EPS3;
		case 196:
		  return OPCODE_RSR_EPS4;
		case 197:
		  return OPCODE_RSR_EPS5;
		case 198:
		  return OPCODE_RSR_EPS6;
		case 199:
		  return OPCODE_RSR_EPS7;
		case 208:
		  return OPCODE_RSR_208;
		case 209:
		  return OPCODE_RSR_EXCSAVE1;
		case 210:
		  return OPCODE_RSR_EXCSAVE2;
		case 211:
		  return OPCODE_RSR_EXCSAVE3;
		case 212:
		  return OPCODE_RSR_EXCSAVE4;
		case 213:
		  return OPCODE_RSR_EXCSAVE5;
		case 214:
		  return OPCODE_RSR_EXCSAVE6;
		case 215:
		  return OPCODE_RSR_EXCSAVE7;
		case 224:
		  return OPCODE_RSR_CPENABLE;
		case 226:
		  return OPCODE_RSR_INTERRUPT;
		case 228:
		  return OPCODE_RSR_INTENABLE;
		case 230:
		  return OPCODE_RSR_PS;
		case 231:
		  return OPCODE_RSR_VECBASE;
		case 232:
		  return OPCODE_RSR_EXCCAUSE;
		case 233:
		  return OPCODE_RSR_DEBUGCAUSE;
		case 234:
		  return OPCODE_RSR_CCOUNT;
		case 235:
		  return OPCODE_RSR_PRID;
		case 236:
		  return OPCODE_RSR_ICOUNT;
		case 237:
		  return OPCODE_RSR_ICOUNTLEVEL;
		case 238:
		  return OPCODE_RSR_EXCVADDR;
		case 240:
		  return OPCODE_RSR_CCOMPARE0;
		case 241:
		  return OPCODE_RSR_CCOMPARE1;
		case 242:
		  return OPCODE_RSR_CCOMPARE2;
		case 244:
		  return OPCODE_RSR_MISC0;
		case 245:
		  return OPCODE_RSR_MISC1;
		}
	      break;
	    case 1:
	      switch (Field_sr_Slot_inst_get (insn))
		{
		case 0:
		  return OPCODE_WSR_LBEG;
		case 1:
		  return OPCODE_WSR_LEND;
		case 2:
		  return OPCODE_WSR_LCOUNT;
		case 3:
		  return OPCODE_WSR_SAR;
		case 5:
		  return OPCODE_WSR_LITBASE;
		case 12:
		  return OPCODE_WSR_SCOMPARE1;
		case 16:
		  return OPCODE_WSR_ACCLO;
		case 17:
		  return OPCODE_WSR_ACCHI;
		case 32:
		  return OPCODE_WSR_M0;
		case 33:
		  return OPCODE_WSR_M1;
		case 34:
		  return OPCODE_WSR_M2;
		case 35:
		  return OPCODE_WSR_M3;
		case 72:
		  return OPCODE_WSR_WINDOWBASE;
		case 73:
		  return OPCODE_WSR_WINDOWSTART;
		case 83:
		  return OPCODE_WSR_PTEVADDR;
		case 89:
		  return OPCODE_WSR_MMID;
		case 90:
		  return OPCODE_WSR_RASID;
		case 91:
		  return OPCODE_WSR_ITLBCFG;
		case 92:
		  return OPCODE_WSR_DTLBCFG;
		case 96:
		  return OPCODE_WSR_IBREAKENABLE;
		case 99:
		  return OPCODE_WSR_ATOMCTL;
		case 104:
		  return OPCODE_WSR_DDR;
		case 128:
		  return OPCODE_WSR_IBREAKA0;
		case 129:
		  return OPCODE_WSR_IBREAKA1;
		case 144:
		  return OPCODE_WSR_DBREAKA0;
		case 145:
		  return OPCODE_WSR_DBREAKA1;
		case 160:
		  return OPCODE_WSR_DBREAKC0;
		case 161:
		  return OPCODE_WSR_DBREAKC1;
		case 176:
		  return OPCODE_WSR_176;
		case 177:
		  return OPCODE_WSR_EPC1;
		case 178:
		  return OPCODE_WSR_EPC2;
		case 179:
		  return OPCODE_WSR_EPC3;
		case 180:
		  return OPCODE_WSR_EPC4;
		case 181:
		  return OPCODE_WSR_EPC5;
		case 182:
		  return OPCODE_WSR_EPC6;
		case 183:
		  return OPCODE_WSR_EPC7;
		case 192:
		  return OPCODE_WSR_DEPC;
		case 194:
		  return OPCODE_WSR_EPS2;
		case 195:
		  return OPCODE_WSR_EPS3;
		case 196:
		  return OPCODE_WSR_EPS4;
		case 197:
		  return OPCODE_WSR_EPS5;
		case 198:
		  return OPCODE_WSR_EPS6;
		case 199:
		  return OPCODE_WSR_EPS7;
		case 209:
		  return OPCODE_WSR_EXCSAVE1;
		case 210:
		  return OPCODE_WSR_EXCSAVE2;
		case 211:
		  return OPCODE_WSR_EXCSAVE3;
		case 212:
		  return OPCODE_WSR_EXCSAVE4;
		case 213:
		  return OPCODE_WSR_EXCSAVE5;
		case 214:
		  return OPCODE_WSR_EXCSAVE6;
		case 215:
		  return OPCODE_WSR_EXCSAVE7;
		case 224:
		  return OPCODE_WSR_CPENABLE;
		case 226:
		  return OPCODE_WSR_INTSET;
		case 227:
		  return OPCODE_WSR_INTCLEAR;
		case 228:
		  return OPCODE_WSR_INTENABLE;
		case 230:
		  return OPCODE_WSR_PS;
		case 231:
		  return OPCODE_WSR_VECBASE;
		case 232:
		  return OPCODE_WSR_EXCCAUSE;
		case 233:
		  return OPCODE_WSR_DEBUGCAUSE;
		case 234:
		  return OPCODE_WSR_CCOUNT;
		case 236:
		  return OPCODE_WSR_ICOUNT;
		case 237:
		  return OPCODE_WSR_ICOUNTLEVEL;
		case 238:
		  return OPCODE_WSR_EXCVADDR;
		case 240:
		  return OPCODE_WSR_CCOMPARE0;
		case 241:
		  return OPCODE_WSR_CCOMPARE1;
		case 242:
		  return OPCODE_WSR_CCOMPARE2;
		case 244:
		  return OPCODE_WSR_MISC0;
		case 245:
		  return OPCODE_WSR_MISC1;
		}
	      break;
	    case 2:
	      return OPCODE_SEXT;
	    case 3:
	      return OPCODE_CLAMPS;
	    case 4:
	      return OPCODE_MIN;
	    case 5:
	      return OPCODE_MAX;
	    case 6:
	      return OPCODE_MINU;
	    case 7:
	      return OPCODE_MAXU;
	    case 8:
	      return OPCODE_MOVEQZ;
	    case 9:
	      return OPCODE_MOVNEZ;
	    case 10:
	      return OPCODE_MOVLTZ;
	    case 11:
	      return OPCODE_MOVGEZ;
	    case 14:
	      switch (Field_st_Slot_inst_get (insn))
		{
		case 230:
		  return OPCODE_RUR_EXPSTATE;
		case 231:
		  return OPCODE_RUR_THREADPTR;
		}
	      break;
	    case 15:
	      switch (Field_sr_Slot_inst_get (insn))
		{
		case 230:
		  return OPCODE_WUR_EXPSTATE;
		case 231:
		  return OPCODE_WUR_THREADPTR;
		}
	      break;
	    }
	  break;
	case 4:
	case 5:
	  return OPCODE_EXTUI;
	case 9:
	  switch (Field_op2_Slot_inst_get (insn))
	    {
	    case 0:
	      return OPCODE_L32E;
	    case 4:
	      return OPCODE_S32E;
	    }
	  break;
	}
      switch (Field_r_Slot_inst_get (insn))
	{
	case 0:
	  if (Field_s_Slot_inst_get (insn) == 0 &&
	      Field_op2_Slot_inst_get (insn) == 0 &&
	      Field_op1_Slot_inst_get (insn) == 14)
	    return OPCODE_READ_IMPWIRE;
	  break;
	case 1:
	  if (Field_s3to1_Slot_inst_get (insn) == 0 &&
	      Field_op2_Slot_inst_get (insn) == 0 &&
	      Field_op1_Slot_inst_get (insn) == 14)
	    return OPCODE_SETB_EXPSTATE;
	  if (Field_s3to1_Slot_inst_get (insn) == 1 &&
	      Field_op2_Slot_inst_get (insn) == 0 &&
	      Field_op1_Slot_inst_get (insn) == 14)
	    return OPCODE_CLRB_EXPSTATE;
	  break;
	case 2:
	  if (Field_op2_Slot_inst_get (insn) == 0 &&
	      Field_op1_Slot_inst_get (insn) == 14)
	    return OPCODE_WRMSK_EXPSTATE;
	  break;
	}
      break;
    case 1:
      return OPCODE_L32R;
    case 2:
      switch (Field_r_Slot_inst_get (insn))
	{
	case 0:
	  return OPCODE_L8UI;
	case 1:
	  return OPCODE_L16UI;
	case 2:
	  return OPCODE_L32I;
	case 4:
	  return OPCODE_S8I;
	case 5:
	  return OPCODE_S16I;
	case 6:
	  return OPCODE_S32I;
	case 7:
	  switch (Field_t_Slot_inst_get (insn))
	    {
	    case 0:
	      return OPCODE_DPFR;
	    case 1:
	      return OPCODE_DPFW;
	    case 2:
	      return OPCODE_DPFRO;
	    case 3:
	      return OPCODE_DPFWO;
	    case 4:
	      return OPCODE_DHWB;
	    case 5:
	      return OPCODE_DHWBI;
	    case 6:
	      return OPCODE_DHI;
	    case 7:
	      return OPCODE_DII;
	    case 8:
	      switch (Field_op1_Slot_inst_get (insn))
		{
		case 0:
		  return OPCODE_DPFL;
		case 2:
		  return OPCODE_DHU;
		case 3:
		  return OPCODE_DIU;
		case 4:
		  return OPCODE_DIWB;
		case 5:
		  return OPCODE_DIWBI;
		}
	      break;
	    case 12:
	      return OPCODE_IPF;
	    case 13:
	      switch (Field_op1_Slot_inst_get (insn))
		{
		case 0:
		  return OPCODE_IPFL;
		case 2:
		  return OPCODE_IHU;
		case 3:
		  return OPCODE_IIU;
		}
	      break;
	    case 14:
	      return OPCODE_IHI;
	    case 15:
	      return OPCODE_III;
	    }
	  break;
	case 9:
	  return OPCODE_L16SI;
	case 10:
	  return OPCODE_MOVI;
	case 11:
	  return OPCODE_L32AI;
	case 12:
	  return OPCODE_ADDI;
	case 13:
	  return OPCODE_ADDMI;
	case 14:
	  return OPCODE_S32C1I;
	case 15:
	  return OPCODE_S32RI;
	}
      break;
    case 4:
      switch (Field_op2_Slot_inst_get (insn))
	{
	case 0:
	  switch (Field_op1_Slot_inst_get (insn))
	    {
	    case 8:
	      if (Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DD_LL_LDINC;
	      break;
	    case 9:
	      if (Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DD_HL_LDINC;
	      break;
	    case 10:
	      if (Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DD_LH_LDINC;
	      break;
	    case 11:
	      if (Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DD_HH_LDINC;
	      break;
	    }
	  break;
	case 1:
	  switch (Field_op1_Slot_inst_get (insn))
	    {
	    case 8:
	      if (Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DD_LL_LDDEC;
	      break;
	    case 9:
	      if (Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DD_HL_LDDEC;
	      break;
	    case 10:
	      if (Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DD_LH_LDDEC;
	      break;
	    case 11:
	      if (Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DD_HH_LDDEC;
	      break;
	    }
	  break;
	case 2:
	  switch (Field_op1_Slot_inst_get (insn))
	    {
	    case 4:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MUL_DD_LL;
	      break;
	    case 5:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MUL_DD_HL;
	      break;
	    case 6:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MUL_DD_LH;
	      break;
	    case 7:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MUL_DD_HH;
	      break;
	    case 8:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DD_LL;
	      break;
	    case 9:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DD_HL;
	      break;
	    case 10:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DD_LH;
	      break;
	    case 11:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DD_HH;
	      break;
	    case 12:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MULS_DD_LL;
	      break;
	    case 13:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MULS_DD_HL;
	      break;
	    case 14:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MULS_DD_LH;
	      break;
	    case 15:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MULS_DD_HH;
	      break;
	    }
	  break;
	case 3:
	  switch (Field_op1_Slot_inst_get (insn))
	    {
	    case 4:
	      if (Field_r_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MUL_AD_LL;
	      break;
	    case 5:
	      if (Field_r_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MUL_AD_HL;
	      break;
	    case 6:
	      if (Field_r_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MUL_AD_LH;
	      break;
	    case 7:
	      if (Field_r_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MUL_AD_HH;
	      break;
	    case 8:
	      if (Field_r_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_AD_LL;
	      break;
	    case 9:
	      if (Field_r_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_AD_HL;
	      break;
	    case 10:
	      if (Field_r_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_AD_LH;
	      break;
	    case 11:
	      if (Field_r_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_AD_HH;
	      break;
	    case 12:
	      if (Field_r_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MULS_AD_LL;
	      break;
	    case 13:
	      if (Field_r_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MULS_AD_HL;
	      break;
	    case 14:
	      if (Field_r_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MULS_AD_LH;
	      break;
	    case 15:
	      if (Field_r_Slot_inst_get (insn) == 0 &&
		  Field_t3_Slot_inst_get (insn) == 0 &&
		  Field_tlo_Slot_inst_get (insn) == 0)
		return OPCODE_MULS_AD_HH;
	      break;
	    }
	  break;
	case 4:
	  switch (Field_op1_Slot_inst_get (insn))
	    {
	    case 8:
	      if (Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DA_LL_LDINC;
	      break;
	    case 9:
	      if (Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DA_HL_LDINC;
	      break;
	    case 10:
	      if (Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DA_LH_LDINC;
	      break;
	    case 11:
	      if (Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DA_HH_LDINC;
	      break;
	    }
	  break;
	case 5:
	  switch (Field_op1_Slot_inst_get (insn))
	    {
	    case 8:
	      if (Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DA_LL_LDDEC;
	      break;
	    case 9:
	      if (Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DA_HL_LDDEC;
	      break;
	    case 10:
	      if (Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DA_LH_LDDEC;
	      break;
	    case 11:
	      if (Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DA_HH_LDDEC;
	      break;
	    }
	  break;
	case 6:
	  switch (Field_op1_Slot_inst_get (insn))
	    {
	    case 4:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MUL_DA_LL;
	      break;
	    case 5:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MUL_DA_HL;
	      break;
	    case 6:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MUL_DA_LH;
	      break;
	    case 7:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MUL_DA_HH;
	      break;
	    case 8:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DA_LL;
	      break;
	    case 9:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DA_HL;
	      break;
	    case 10:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DA_LH;
	      break;
	    case 11:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_DA_HH;
	      break;
	    case 12:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULS_DA_LL;
	      break;
	    case 13:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULS_DA_HL;
	      break;
	    case 14:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULS_DA_LH;
	      break;
	    case 15:
	      if (Field_s_Slot_inst_get (insn) == 0 &&
		  Field_w_Slot_inst_get (insn) == 0 &&
		  Field_r3_Slot_inst_get (insn) == 0)
		return OPCODE_MULS_DA_HH;
	      break;
	    }
	  break;
	case 7:
	  switch (Field_op1_Slot_inst_get (insn))
	    {
	    case 0:
	      if (Field_r_Slot_inst_get (insn) == 0)
		return OPCODE_UMUL_AA_LL;
	      break;
	    case 1:
	      if (Field_r_Slot_inst_get (insn) == 0)
		return OPCODE_UMUL_AA_HL;
	      break;
	    case 2:
	      if (Field_r_Slot_inst_get (insn) == 0)
		return OPCODE_UMUL_AA_LH;
	      break;
	    case 3:
	      if (Field_r_Slot_inst_get (insn) == 0)
		return OPCODE_UMUL_AA_HH;
	      break;
	    case 4:
	      if (Field_r_Slot_inst_get (insn) == 0)
		return OPCODE_MUL_AA_LL;
	      break;
	    case 5:
	      if (Field_r_Slot_inst_get (insn) == 0)
		return OPCODE_MUL_AA_HL;
	      break;
	    case 6:
	      if (Field_r_Slot_inst_get (insn) == 0)
		return OPCODE_MUL_AA_LH;
	      break;
	    case 7:
	      if (Field_r_Slot_inst_get (insn) == 0)
		return OPCODE_MUL_AA_HH;
	      break;
	    case 8:
	      if (Field_r_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_AA_LL;
	      break;
	    case 9:
	      if (Field_r_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_AA_HL;
	      break;
	    case 10:
	      if (Field_r_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_AA_LH;
	      break;
	    case 11:
	      if (Field_r_Slot_inst_get (insn) == 0)
		return OPCODE_MULA_AA_HH;
	      break;
	    case 12:
	      if (Field_r_Slot_inst_get (insn) == 0)
		return OPCODE_MULS_AA_LL;
	      break;
	    case 13:
	      if (Field_r_Slot_inst_get (insn) == 0)
		return OPCODE_MULS_AA_HL;
	      break;
	    case 14:
	      if (Field_r_Slot_inst_get (insn) == 0)
		return OPCODE_MULS_AA_LH;
	      break;
	    case 15:
	      if (Field_r_Slot_inst_get (insn) == 0)
		return OPCODE_MULS_AA_HH;
	      break;
	    }
	  break;
	case 8:
	  if (Field_op1_Slot_inst_get (insn) == 0 &&
	      Field_t_Slot_inst_get (insn) == 0 &&
	      Field_rhi_Slot_inst_get (insn) == 0)
	    return OPCODE_LDINC;
	  break;
	case 9:
	  if (Field_op1_Slot_inst_get (insn) == 0 &&
	      Field_t_Slot_inst_get (insn) == 0 &&
	      Field_rhi_Slot_inst_get (insn) == 0)
	    return OPCODE_LDDEC;
	  break;
	}
      break;
    case 5:
      switch (Field_n_Slot_inst_get (insn))
	{
	case 0:
	  return OPCODE_CALL0;
	case 1:
	  return OPCODE_CALL4;
	case 2:
	  return OPCODE_CALL8;
	case 3:
	  return OPCODE_CALL12;
	}
      break;
    case 6:
      switch (Field_n_Slot_inst_get (insn))
	{
	case 0:
	  return OPCODE_J;
	case 1:
	  switch (Field_m_Slot_inst_get (insn))
	    {
	    case 0:
	      return OPCODE_BEQZ;
	    case 1:
	      return OPCODE_BNEZ;
	    case 2:
	      return OPCODE_BLTZ;
	    case 3:
	      return OPCODE_BGEZ;
	    }
	  break;
	case 2:
	  switch (Field_m_Slot_inst_get (insn))
	    {
	    case 0:
	      return OPCODE_BEQI;
	    case 1:
	      return OPCODE_BNEI;
	    case 2:
	      return OPCODE_BLTI;
	    case 3:
	      return OPCODE_BGEI;
	    }
	  break;
	case 3:
	  switch (Field_m_Slot_inst_get (insn))
	    {
	    case 0:
	      return OPCODE_ENTRY;
	    case 1:
	      switch (Field_r_Slot_inst_get (insn))
		{
		case 8:
		  return OPCODE_LOOP;
		case 9:
		  return OPCODE_LOOPNEZ;
		case 10:
		  return OPCODE_LOOPGTZ;
		}
	      break;
	    case 2:
	      return OPCODE_BLTUI;
	    case 3:
	      return OPCODE_BGEUI;
	    }
	  break;
	}
      break;
    case 7:
      switch (Field_r_Slot_inst_get (insn))
	{
	case 0:
	  return OPCODE_BNONE;
	case 1:
	  return OPCODE_BEQ;
	case 2:
	  return OPCODE_BLT;
	case 3:
	  return OPCODE_BLTU;
	case 4:
	  return OPCODE_BALL;
	case 5:
	  return OPCODE_BBC;
	case 6:
	case 7:
	  return OPCODE_BBCI;
	case 8:
	  return OPCODE_BANY;
	case 9:
	  return OPCODE_BNE;
	case 10:
	  return OPCODE_BGE;
	case 11:
	  return OPCODE_BGEU;
	case 12:
	  return OPCODE_BNALL;
	case 13:
	  return OPCODE_BBS;
	case 14:
	case 15:
	  return OPCODE_BBSI;
	}
      break;
    }
  return XTENSA_UNDEFINED;
}

static int
Slot_inst16b_decode (const xtensa_insnbuf insn)
{
  switch (Field_op0_Slot_inst16b_get (insn))
    {
    case 12:
      switch (Field_i_Slot_inst16b_get (insn))
	{
	case 0:
	  return OPCODE_MOVI_N;
	case 1:
	  switch (Field_z_Slot_inst16b_get (insn))
	    {
	    case 0:
	      return OPCODE_BEQZ_N;
	    case 1:
	      return OPCODE_BNEZ_N;
	    }
	  break;
	}
      break;
    case 13:
      switch (Field_r_Slot_inst16b_get (insn))
	{
	case 0:
	  return OPCODE_MOV_N;
	case 15:
	  switch (Field_t_Slot_inst16b_get (insn))
	    {
	    case 0:
	      return OPCODE_RET_N;
	    case 1:
	      return OPCODE_RETW_N;
	    case 2:
	      return OPCODE_BREAK_N;
	    case 3:
	      if (Field_s_Slot_inst16b_get (insn) == 0)
		return OPCODE_NOP_N;
	      break;
	    case 6:
	      if (Field_s_Slot_inst16b_get (insn) == 0)
		return OPCODE_ILL_N;
	      break;
	    }
	  break;
	}
      break;
    }
  return XTENSA_UNDEFINED;
}

static int
Slot_inst16a_decode (const xtensa_insnbuf insn)
{
  switch (Field_op0_Slot_inst16a_get (insn))
    {
    case 8:
      return OPCODE_L32I_N;
    case 9:
      return OPCODE_S32I_N;
    case 10:
      return OPCODE_ADD_N;
    case 11:
      return OPCODE_ADDI_N;
    }
  return XTENSA_UNDEFINED;
}


/* Instruction slots.  */

static void
Slot_x24_Format_inst_0_get (const xtensa_insnbuf insn,
			    xtensa_insnbuf slotbuf)
{
  slotbuf[0] = (insn[0] & 0xffffff);
}

static void
Slot_x24_Format_inst_0_set (xtensa_insnbuf insn,
			    const xtensa_insnbuf slotbuf)
{
  insn[0] = (insn[0] & ~0xffffff) | (slotbuf[0] & 0xffffff);
}

static void
Slot_x16a_Format_inst16a_0_get (const xtensa_insnbuf insn,
				xtensa_insnbuf slotbuf)
{
  slotbuf[0] = (insn[0] & 0xffff);
}

static void
Slot_x16a_Format_inst16a_0_set (xtensa_insnbuf insn,
				const xtensa_insnbuf slotbuf)
{
  insn[0] = (insn[0] & ~0xffff) | (slotbuf[0] & 0xffff);
}

static void
Slot_x16b_Format_inst16b_0_get (const xtensa_insnbuf insn,
				xtensa_insnbuf slotbuf)
{
  slotbuf[0] = (insn[0] & 0xffff);
}

static void
Slot_x16b_Format_inst16b_0_set (xtensa_insnbuf insn,
				const xtensa_insnbuf slotbuf)
{
  insn[0] = (insn[0] & ~0xffff) | (slotbuf[0] & 0xffff);
}

static xtensa_get_field_fn
Slot_inst_get_field_fns[] = {
  Field_t_Slot_inst_get,
  Field_bbi4_Slot_inst_get,
  Field_bbi_Slot_inst_get,
  Field_imm12_Slot_inst_get,
  Field_imm8_Slot_inst_get,
  Field_s_Slot_inst_get,
  Field_imm12b_Slot_inst_get,
  Field_imm16_Slot_inst_get,
  Field_m_Slot_inst_get,
  Field_n_Slot_inst_get,
  Field_offset_Slot_inst_get,
  Field_op0_Slot_inst_get,
  Field_op1_Slot_inst_get,
  Field_op2_Slot_inst_get,
  Field_r_Slot_inst_get,
  Field_sa4_Slot_inst_get,
  Field_sae4_Slot_inst_get,
  Field_sae_Slot_inst_get,
  Field_sal_Slot_inst_get,
  Field_sargt_Slot_inst_get,
  Field_sas4_Slot_inst_get,
  Field_sas_Slot_inst_get,
  Field_sr_Slot_inst_get,
  Field_st_Slot_inst_get,
  Field_thi3_Slot_inst_get,
  Field_imm4_Slot_inst_get,
  Field_mn_Slot_inst_get,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  Field_r3_Slot_inst_get,
  Field_rbit2_Slot_inst_get,
  Field_rhi_Slot_inst_get,
  Field_t3_Slot_inst_get,
  Field_tbit2_Slot_inst_get,
  Field_tlo_Slot_inst_get,
  Field_w_Slot_inst_get,
  Field_y_Slot_inst_get,
  Field_x_Slot_inst_get,
  Field_xt_wbr15_imm_Slot_inst_get,
  Field_xt_wbr18_imm_Slot_inst_get,
  Field_bitindex_Slot_inst_get,
  Field_s3to1_Slot_inst_get,
  Implicit_Field_ar0_get,
  Implicit_Field_ar4_get,
  Implicit_Field_ar8_get,
  Implicit_Field_ar12_get,
  Implicit_Field_mr0_get,
  Implicit_Field_mr1_get,
  Implicit_Field_mr2_get,
  Implicit_Field_mr3_get
};

static xtensa_set_field_fn
Slot_inst_set_field_fns[] = {
  Field_t_Slot_inst_set,
  Field_bbi4_Slot_inst_set,
  Field_bbi_Slot_inst_set,
  Field_imm12_Slot_inst_set,
  Field_imm8_Slot_inst_set,
  Field_s_Slot_inst_set,
  Field_imm12b_Slot_inst_set,
  Field_imm16_Slot_inst_set,
  Field_m_Slot_inst_set,
  Field_n_Slot_inst_set,
  Field_offset_Slot_inst_set,
  Field_op0_Slot_inst_set,
  Field_op1_Slot_inst_set,
  Field_op2_Slot_inst_set,
  Field_r_Slot_inst_set,
  Field_sa4_Slot_inst_set,
  Field_sae4_Slot_inst_set,
  Field_sae_Slot_inst_set,
  Field_sal_Slot_inst_set,
  Field_sargt_Slot_inst_set,
  Field_sas4_Slot_inst_set,
  Field_sas_Slot_inst_set,
  Field_sr_Slot_inst_set,
  Field_st_Slot_inst_set,
  Field_thi3_Slot_inst_set,
  Field_imm4_Slot_inst_set,
  Field_mn_Slot_inst_set,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  Field_r3_Slot_inst_set,
  Field_rbit2_Slot_inst_set,
  Field_rhi_Slot_inst_set,
  Field_t3_Slot_inst_set,
  Field_tbit2_Slot_inst_set,
  Field_tlo_Slot_inst_set,
  Field_w_Slot_inst_set,
  Field_y_Slot_inst_set,
  Field_x_Slot_inst_set,
  Field_xt_wbr15_imm_Slot_inst_set,
  Field_xt_wbr18_imm_Slot_inst_set,
  Field_bitindex_Slot_inst_set,
  Field_s3to1_Slot_inst_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set
};

static xtensa_get_field_fn
Slot_inst16a_get_field_fns[] = {
  Field_t_Slot_inst16a_get,
  0,
  0,
  0,
  0,
  Field_s_Slot_inst16a_get,
  0,
  0,
  0,
  0,
  0,
  Field_op0_Slot_inst16a_get,
  0,
  0,
  Field_r_Slot_inst16a_get,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  Field_sr_Slot_inst16a_get,
  Field_st_Slot_inst16a_get,
  0,
  Field_imm4_Slot_inst16a_get,
  0,
  Field_i_Slot_inst16a_get,
  Field_imm6lo_Slot_inst16a_get,
  Field_imm6hi_Slot_inst16a_get,
  Field_imm7lo_Slot_inst16a_get,
  Field_imm7hi_Slot_inst16a_get,
  Field_z_Slot_inst16a_get,
  Field_imm6_Slot_inst16a_get,
  Field_imm7_Slot_inst16a_get,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  Field_bitindex_Slot_inst16a_get,
  Field_s3to1_Slot_inst16a_get,
  Implicit_Field_ar0_get,
  Implicit_Field_ar4_get,
  Implicit_Field_ar8_get,
  Implicit_Field_ar12_get,
  Implicit_Field_mr0_get,
  Implicit_Field_mr1_get,
  Implicit_Field_mr2_get,
  Implicit_Field_mr3_get
};

static xtensa_set_field_fn
Slot_inst16a_set_field_fns[] = {
  Field_t_Slot_inst16a_set,
  0,
  0,
  0,
  0,
  Field_s_Slot_inst16a_set,
  0,
  0,
  0,
  0,
  0,
  Field_op0_Slot_inst16a_set,
  0,
  0,
  Field_r_Slot_inst16a_set,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  Field_sr_Slot_inst16a_set,
  Field_st_Slot_inst16a_set,
  0,
  Field_imm4_Slot_inst16a_set,
  0,
  Field_i_Slot_inst16a_set,
  Field_imm6lo_Slot_inst16a_set,
  Field_imm6hi_Slot_inst16a_set,
  Field_imm7lo_Slot_inst16a_set,
  Field_imm7hi_Slot_inst16a_set,
  Field_z_Slot_inst16a_set,
  Field_imm6_Slot_inst16a_set,
  Field_imm7_Slot_inst16a_set,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  Field_bitindex_Slot_inst16a_set,
  Field_s3to1_Slot_inst16a_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set
};

static xtensa_get_field_fn
Slot_inst16b_get_field_fns[] = {
  Field_t_Slot_inst16b_get,
  0,
  0,
  0,
  0,
  Field_s_Slot_inst16b_get,
  0,
  0,
  0,
  0,
  0,
  Field_op0_Slot_inst16b_get,
  0,
  0,
  Field_r_Slot_inst16b_get,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  Field_sr_Slot_inst16b_get,
  Field_st_Slot_inst16b_get,
  0,
  Field_imm4_Slot_inst16b_get,
  0,
  Field_i_Slot_inst16b_get,
  Field_imm6lo_Slot_inst16b_get,
  Field_imm6hi_Slot_inst16b_get,
  Field_imm7lo_Slot_inst16b_get,
  Field_imm7hi_Slot_inst16b_get,
  Field_z_Slot_inst16b_get,
  Field_imm6_Slot_inst16b_get,
  Field_imm7_Slot_inst16b_get,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  Field_bitindex_Slot_inst16b_get,
  Field_s3to1_Slot_inst16b_get,
  Implicit_Field_ar0_get,
  Implicit_Field_ar4_get,
  Implicit_Field_ar8_get,
  Implicit_Field_ar12_get,
  Implicit_Field_mr0_get,
  Implicit_Field_mr1_get,
  Implicit_Field_mr2_get,
  Implicit_Field_mr3_get
};

static xtensa_set_field_fn
Slot_inst16b_set_field_fns[] = {
  Field_t_Slot_inst16b_set,
  0,
  0,
  0,
  0,
  Field_s_Slot_inst16b_set,
  0,
  0,
  0,
  0,
  0,
  Field_op0_Slot_inst16b_set,
  0,
  0,
  Field_r_Slot_inst16b_set,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  Field_sr_Slot_inst16b_set,
  Field_st_Slot_inst16b_set,
  0,
  Field_imm4_Slot_inst16b_set,
  0,
  Field_i_Slot_inst16b_set,
  Field_imm6lo_Slot_inst16b_set,
  Field_imm6hi_Slot_inst16b_set,
  Field_imm7lo_Slot_inst16b_set,
  Field_imm7hi_Slot_inst16b_set,
  Field_z_Slot_inst16b_set,
  Field_imm6_Slot_inst16b_set,
  Field_imm7_Slot_inst16b_set,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  Field_bitindex_Slot_inst16b_set,
  Field_s3to1_Slot_inst16b_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set
};

static xtensa_slot_internal slots[] = {
  { "Inst", "x24", 0,
    Slot_x24_Format_inst_0_get, Slot_x24_Format_inst_0_set,
    Slot_inst_get_field_fns, Slot_inst_set_field_fns,
    Slot_inst_decode, "nop" },
  { "Inst16a", "x16a", 0,
    Slot_x16a_Format_inst16a_0_get, Slot_x16a_Format_inst16a_0_set,
    Slot_inst16a_get_field_fns, Slot_inst16a_set_field_fns,
    Slot_inst16a_decode, "" },
  { "Inst16b", "x16b", 0,
    Slot_x16b_Format_inst16b_0_get, Slot_x16b_Format_inst16b_0_set,
    Slot_inst16b_get_field_fns, Slot_inst16b_set_field_fns,
    Slot_inst16b_decode, "nop.n" }
};


/* Instruction formats.  */

static void
Format_x24_encode (xtensa_insnbuf insn)
{
  insn[0] = 0;
}

static void
Format_x16a_encode (xtensa_insnbuf insn)
{
  insn[0] = 0x8;
}

static void
Format_x16b_encode (xtensa_insnbuf insn)
{
  insn[0] = 0xc;
}

static int Format_x24_slots[] = { 0 };

static int Format_x16a_slots[] = { 1 };

static int Format_x16b_slots[] = { 2 };

static xtensa_format_internal formats[] = {
  { "x24", 3, Format_x24_encode, 1, Format_x24_slots },
  { "x16a", 2, Format_x16a_encode, 1, Format_x16a_slots },
  { "x16b", 2, Format_x16b_encode, 1, Format_x16b_slots }
};


static int
format_decoder (const xtensa_insnbuf insn)
{
  if ((insn[0] & 0x8) == 0)
    return 0; /* x24 */
  if ((insn[0] & 0xc) == 0x8)
    return 1; /* x16a */
  if ((insn[0] & 0xe) == 0xc)
    return 2; /* x16b */
  return -1;
}

static int length_table[16] = {
  3,
  3,
  3,
  3,
  3,
  3,
  3,
  3,
  2,
  2,
  2,
  2,
  2,
  2,
  -1,
  -1
};

static int
length_decoder (const unsigned char *insn)
{
  int op0 = insn[0] & 0xf;
  return length_table[op0];
}


/* Top-level ISA structure.  */

xtensa_isa_internal xtensa_modules = {
  0 /* little-endian */,
  3 /* insn_size */, 0,
  3, formats, format_decoder, length_decoder,
  3, slots,
  56 /* num_fields */,
  93, operands,
  326, iclasses,
  452, opcodes, 0,
  2, regfiles,
  NUM_STATES, states, 0,
  NUM_SYSREGS, sysregs, 0,
  { MAX_SPECIAL_REG, MAX_USER_REG }, { 0, 0 },
  1, interfaces, 0,
  0, funcUnits, 0
};
