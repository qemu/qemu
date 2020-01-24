/* Xtensa configuration-specific ISA information.

   Copyright (c) 2003-2020 Tensilica Inc.

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

#include "xtensa-isa.h"
#include "xtensa-isa-internal.h"


/* Sysregs.  */

static xtensa_sysreg_internal sysregs[] = {
  { "LBEG", 0, 0 },
  { "LEND", 1, 0 },
  { "LCOUNT", 2, 0 },
  { "BR", 4, 0 },
  { "ACCLO", 16, 0 },
  { "ACCHI", 17, 0 },
  { "M0", 32, 0 },
  { "M1", 33, 0 },
  { "M2", 34, 0 },
  { "M3", 35, 0 },
  { "PTEVADDR", 83, 0 },
  { "MMID", 89, 0 },
  { "DDR", 104, 0 },
  { "CONFIGID0", 176, 0 },
  { "CONFIGID1", 208, 0 },
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
  { "ERACCESS", 95, 0 },
  { "THREADPTR", 231, 1 },
  { "FCR", 232, 1 },
  { "FSR", 233, 1 },
  { "EXPSTATE", 230, 1 }
};

#define NUM_SYSREGS 74
#define MAX_SPECIAL_REG 245
#define MAX_USER_REG 233


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
  { "ERACCESS", 16, 0 },
  { "RoundMode", 2, 0 },
  { "InvalidEnable", 1, 0 },
  { "DivZeroEnable", 1, 0 },
  { "OverflowEnable", 1, 0 },
  { "UnderflowEnable", 1, 0 },
  { "InexactEnable", 1, 0 },
  { "InvalidFlag", 1, XTENSA_STATE_IS_SHARED_OR },
  { "DivZeroFlag", 1, XTENSA_STATE_IS_SHARED_OR },
  { "OverflowFlag", 1, XTENSA_STATE_IS_SHARED_OR },
  { "UnderflowFlag", 1, XTENSA_STATE_IS_SHARED_OR },
  { "InexactFlag", 1, XTENSA_STATE_IS_SHARED_OR },
  { "FPreserved20", 20, 0 },
  { "FPreserved20a", 20, 0 },
  { "FPreserved5", 5, 0 },
  { "FPreserved7", 7, 0 },
  { "EXPSTATE", 32, XTENSA_STATE_IS_EXPORTED }
};

#define NUM_STATES 92

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
  STATE_ERACCESS,
  STATE_RoundMode,
  STATE_InvalidEnable,
  STATE_DivZeroEnable,
  STATE_OverflowEnable,
  STATE_UnderflowEnable,
  STATE_InexactEnable,
  STATE_InvalidFlag,
  STATE_DivZeroFlag,
  STATE_OverflowFlag,
  STATE_UnderflowFlag,
  STATE_InexactFlag,
  STATE_FPreserved20,
  STATE_FPreserved20a,
  STATE_FPreserved5,
  STATE_FPreserved7,
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
Field_s8_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 20) >> 31);
  return tie_t;
}

static void
Field_s8_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x800) | (tie_t << 11);
}

static unsigned
Field_imms8_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 3) | ((insn[0] << 21) >> 29);
  return tie_t;
}

static void
Field_imms8_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 29) >> 29;
  insn[0] = (insn[0] & ~0x700) | (tie_t << 8);
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
Field_r_disp_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 3) | ((insn[0] << 17) >> 29);
  return tie_t;
}

static void
Field_r_disp_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 29) >> 29;
  insn[0] = (insn[0] & ~0x7000) | (tie_t << 12);
}

static unsigned
Field_r_3_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 16) >> 31);
  return tie_t;
}

static void
Field_r_3_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x8000) | (tie_t << 15);
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
Field_t2_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 3) | ((insn[0] << 24) >> 29);
  return tie_t;
}

static void
Field_t2_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 29) >> 29;
  insn[0] = (insn[0] & ~0xe0) | (tie_t << 5);
}

static unsigned
Field_s2_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 3) | ((insn[0] << 20) >> 29);
  return tie_t;
}

static void
Field_s2_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 29) >> 29;
  insn[0] = (insn[0] & ~0xe00) | (tie_t << 9);
}

static unsigned
Field_r2_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 3) | ((insn[0] << 16) >> 29);
  return tie_t;
}

static void
Field_r2_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 29) >> 29;
  insn[0] = (insn[0] & ~0xe000) | (tie_t << 13);
}

static unsigned
Field_t4_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 24) >> 30);
  return tie_t;
}

static void
Field_t4_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 30) >> 30;
  insn[0] = (insn[0] & ~0xc0) | (tie_t << 6);
}

static unsigned
Field_s4_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 20) >> 30);
  return tie_t;
}

static void
Field_s4_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 30) >> 30;
  insn[0] = (insn[0] & ~0xc00) | (tie_t << 10);
}

static unsigned
Field_r4_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 2) | ((insn[0] << 16) >> 30);
  return tie_t;
}

static void
Field_r4_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 30) >> 30;
  insn[0] = (insn[0] & ~0xc000) | (tie_t << 14);
}

static unsigned
Field_t8_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 24) >> 31);
  return tie_t;
}

static void
Field_t8_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x80) | (tie_t << 7);
}

static unsigned
Field_r8_Slot_inst_get (const xtensa_insnbuf insn)
{
  unsigned tie_t = 0;
  tie_t = (tie_t << 1) | ((insn[0] << 16) >> 31);
  return tie_t;
}

static void
Field_r8_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 31) >> 31;
  insn[0] = (insn[0] & ~0x8000) | (tie_t << 15);
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
  tie_t = (tie_t << 5) | ((insn[0] << 23) >> 27);
  return tie_t;
}

static void
Field_bitindex_Slot_inst_set (xtensa_insnbuf insn, uint32 val)
{
  uint32 tie_t;
  tie_t = (val << 27) >> 27;
  insn[0] = (insn[0] & ~0x1f0) | (tie_t << 4);
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

static unsigned
Implicit_Field_bt16_get (const xtensa_insnbuf insn ATTRIBUTE_UNUSED)
{
  return 0;
}

static unsigned
Implicit_Field_bs16_get (const xtensa_insnbuf insn ATTRIBUTE_UNUSED)
{
  return 0;
}

static unsigned
Implicit_Field_br16_get (const xtensa_insnbuf insn ATTRIBUTE_UNUSED)
{
  return 0;
}

static unsigned
Implicit_Field_brall_get (const xtensa_insnbuf insn ATTRIBUTE_UNUSED)
{
  return 0;
}

enum xtensa_field_id {
  FIELD_t,
  FIELD_bbi4,
  FIELD_bbi,
  FIELD_imm12,
  FIELD_imm8,
  FIELD_s,
  FIELD_s8,
  FIELD_imms8,
  FIELD_imm12b,
  FIELD_imm16,
  FIELD_m,
  FIELD_n,
  FIELD_offset,
  FIELD_op0,
  FIELD_op1,
  FIELD_op2,
  FIELD_r,
  FIELD_r_disp,
  FIELD_r_3,
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
  FIELD_t2,
  FIELD_s2,
  FIELD_r2,
  FIELD_t4,
  FIELD_s4,
  FIELD_r4,
  FIELD_t8,
  FIELD_r8,
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
  FIELD__mr3,
  FIELD__bt16,
  FIELD__bs16,
  FIELD__br16,
  FIELD__brall
};


/* Functional units.  */

static xtensa_funcUnit_internal funcUnits[] = {
  {"XT_LOADSTORE_UNIT", 1}
};

enum xtensa_funcUnit_id {
  FUNCUNIT_XT_LOADSTORE_UNIT
};


/* Register files.  */

enum xtensa_regfile_id {
  REGFILE_AR,
  REGFILE_MR,
  REGFILE_BR,
  REGFILE_FR,
  REGFILE_BR2,
  REGFILE_BR4,
  REGFILE_BR8,
  REGFILE_BR16
};

static xtensa_regfile_internal regfiles[] = {
  { "AR", "a", REGFILE_AR, 32, 32 },
  { "MR", "m", REGFILE_MR, 32, 4 },
  { "BR", "b", REGFILE_BR, 1, 16 },
  { "FR", "f", REGFILE_FR, 64, 16 },
  { "BR2", "b", REGFILE_BR, 2, 8 },
  { "BR4", "b", REGFILE_BR, 4, 4 },
  { "BR8", "b", REGFILE_BR, 8, 2 },
  { "BR16", "b", REGFILE_BR, 16, 1 }
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

/* constant table RECIP_Data8 */
static const unsigned CONST_TBL_RECIP_Data8_0[] = {
  0xff & 0xff,
  0xfd & 0xff,
  0xfb & 0xff,
  0xf9 & 0xff,
  0xf7 & 0xff,
  0xf5 & 0xff,
  0xf4 & 0xff,
  0xf2 & 0xff,
  0xf0 & 0xff,
  0xee & 0xff,
  0xed & 0xff,
  0xeb & 0xff,
  0xe9 & 0xff,
  0xe8 & 0xff,
  0xe6 & 0xff,
  0xe4 & 0xff,
  0xe3 & 0xff,
  0xe1 & 0xff,
  0xe0 & 0xff,
  0xde & 0xff,
  0xdd & 0xff,
  0xdb & 0xff,
  0xda & 0xff,
  0xd8 & 0xff,
  0xd7 & 0xff,
  0xd5 & 0xff,
  0xd4 & 0xff,
  0xd3 & 0xff,
  0xd1 & 0xff,
  0xd0 & 0xff,
  0xcf & 0xff,
  0xcd & 0xff,
  0xcc & 0xff,
  0xcb & 0xff,
  0xca & 0xff,
  0xc8 & 0xff,
  0xc7 & 0xff,
  0xc6 & 0xff,
  0xc5 & 0xff,
  0xc4 & 0xff,
  0xc2 & 0xff,
  0xc1 & 0xff,
  0xc0 & 0xff,
  0xbf & 0xff,
  0xbe & 0xff,
  0xbd & 0xff,
  0xbc & 0xff,
  0xbb & 0xff,
  0xba & 0xff,
  0xb9 & 0xff,
  0xb8 & 0xff,
  0xb7 & 0xff,
  0xb6 & 0xff,
  0xb5 & 0xff,
  0xb4 & 0xff,
  0xb3 & 0xff,
  0xb2 & 0xff,
  0xb1 & 0xff,
  0xb0 & 0xff,
  0xaf & 0xff,
  0xae & 0xff,
  0xad & 0xff,
  0xac & 0xff,
  0xab & 0xff,
  0xaa & 0xff,
  0xa9 & 0xff,
  0xa8 & 0xff,
  0xa8 & 0xff,
  0xa7 & 0xff,
  0xa6 & 0xff,
  0xa5 & 0xff,
  0xa4 & 0xff,
  0xa3 & 0xff,
  0xa3 & 0xff,
  0xa2 & 0xff,
  0xa1 & 0xff,
  0xa0 & 0xff,
  0x9f & 0xff,
  0x9f & 0xff,
  0x9e & 0xff,
  0x9d & 0xff,
  0x9c & 0xff,
  0x9c & 0xff,
  0x9b & 0xff,
  0x9a & 0xff,
  0x99 & 0xff,
  0x99 & 0xff,
  0x98 & 0xff,
  0x97 & 0xff,
  0x97 & 0xff,
  0x96 & 0xff,
  0x95 & 0xff,
  0x95 & 0xff,
  0x94 & 0xff,
  0x93 & 0xff,
  0x93 & 0xff,
  0x92 & 0xff,
  0x91 & 0xff,
  0x91 & 0xff,
  0x90 & 0xff,
  0x8f & 0xff,
  0x8f & 0xff,
  0x8e & 0xff,
  0x8e & 0xff,
  0x8d & 0xff,
  0x8c & 0xff,
  0x8c & 0xff,
  0x8b & 0xff,
  0x8b & 0xff,
  0x8a & 0xff,
  0x89 & 0xff,
  0x89 & 0xff,
  0x88 & 0xff,
  0x88 & 0xff,
  0x87 & 0xff,
  0x87 & 0xff,
  0x86 & 0xff,
  0x85 & 0xff,
  0x85 & 0xff,
  0x84 & 0xff,
  0x84 & 0xff,
  0x83 & 0xff,
  0x83 & 0xff,
  0x82 & 0xff,
  0x82 & 0xff,
  0x81 & 0xff,
  0x81 & 0xff,
  0x81 & 0xff,
  0
};

/* constant table RSQRT_Data8 */
static const unsigned CONST_TBL_RSQRT_Data8_0[] = {
  0xb4 & 0xff,
  0xb3 & 0xff,
  0xb2 & 0xff,
  0xb0 & 0xff,
  0xaf & 0xff,
  0xae & 0xff,
  0xac & 0xff,
  0xab & 0xff,
  0xaa & 0xff,
  0xa9 & 0xff,
  0xa8 & 0xff,
  0xa7 & 0xff,
  0xa6 & 0xff,
  0xa5 & 0xff,
  0xa3 & 0xff,
  0xa2 & 0xff,
  0xa1 & 0xff,
  0xa0 & 0xff,
  0x9f & 0xff,
  0x9e & 0xff,
  0x9e & 0xff,
  0x9d & 0xff,
  0x9c & 0xff,
  0x9b & 0xff,
  0x9a & 0xff,
  0x99 & 0xff,
  0x98 & 0xff,
  0x97 & 0xff,
  0x97 & 0xff,
  0x96 & 0xff,
  0x95 & 0xff,
  0x94 & 0xff,
  0x93 & 0xff,
  0x93 & 0xff,
  0x92 & 0xff,
  0x91 & 0xff,
  0x90 & 0xff,
  0x90 & 0xff,
  0x8f & 0xff,
  0x8e & 0xff,
  0x8e & 0xff,
  0x8d & 0xff,
  0x8c & 0xff,
  0x8c & 0xff,
  0x8b & 0xff,
  0x8a & 0xff,
  0x8a & 0xff,
  0x89 & 0xff,
  0x89 & 0xff,
  0x88 & 0xff,
  0x87 & 0xff,
  0x87 & 0xff,
  0x86 & 0xff,
  0x86 & 0xff,
  0x85 & 0xff,
  0x84 & 0xff,
  0x84 & 0xff,
  0x83 & 0xff,
  0x83 & 0xff,
  0x82 & 0xff,
  0x82 & 0xff,
  0x81 & 0xff,
  0x81 & 0xff,
  0x80 & 0xff,
  0xff & 0xff,
  0xfd & 0xff,
  0xfb & 0xff,
  0xf9 & 0xff,
  0xf7 & 0xff,
  0xf6 & 0xff,
  0xf4 & 0xff,
  0xf2 & 0xff,
  0xf1 & 0xff,
  0xef & 0xff,
  0xed & 0xff,
  0xec & 0xff,
  0xea & 0xff,
  0xe9 & 0xff,
  0xe7 & 0xff,
  0xe6 & 0xff,
  0xe4 & 0xff,
  0xe3 & 0xff,
  0xe1 & 0xff,
  0xe0 & 0xff,
  0xdf & 0xff,
  0xdd & 0xff,
  0xdc & 0xff,
  0xdb & 0xff,
  0xda & 0xff,
  0xd8 & 0xff,
  0xd7 & 0xff,
  0xd6 & 0xff,
  0xd5 & 0xff,
  0xd4 & 0xff,
  0xd3 & 0xff,
  0xd2 & 0xff,
  0xd0 & 0xff,
  0xcf & 0xff,
  0xce & 0xff,
  0xcd & 0xff,
  0xcc & 0xff,
  0xcb & 0xff,
  0xca & 0xff,
  0xc9 & 0xff,
  0xc8 & 0xff,
  0xc7 & 0xff,
  0xc6 & 0xff,
  0xc6 & 0xff,
  0xc5 & 0xff,
  0xc4 & 0xff,
  0xc3 & 0xff,
  0xc2 & 0xff,
  0xc1 & 0xff,
  0xc0 & 0xff,
  0xbf & 0xff,
  0xbf & 0xff,
  0xbe & 0xff,
  0xbd & 0xff,
  0xbc & 0xff,
  0xbb & 0xff,
  0xbb & 0xff,
  0xba & 0xff,
  0xb9 & 0xff,
  0xb8 & 0xff,
  0xb8 & 0xff,
  0xb7 & 0xff,
  0xb6 & 0xff,
  0xb5 & 0xff,
  0
};

/* constant table RECIP_Data10_2 */
static const unsigned CONST_TBL_RECIP_Data10_2_0[] = {
  0x3fc & 0x3ff,
  0x3f4 & 0x3ff,
  0x3ec & 0x3ff,
  0x3e5 & 0x3ff,
  0x3dd & 0x3ff,
  0x3d6 & 0x3ff,
  0x3cf & 0x3ff,
  0x3c7 & 0x3ff,
  0x3c0 & 0x3ff,
  0x3b9 & 0x3ff,
  0x3b2 & 0x3ff,
  0x3ac & 0x3ff,
  0x3a5 & 0x3ff,
  0x39e & 0x3ff,
  0x398 & 0x3ff,
  0x391 & 0x3ff,
  0x38b & 0x3ff,
  0x385 & 0x3ff,
  0x37f & 0x3ff,
  0x378 & 0x3ff,
  0x373 & 0x3ff,
  0x36c & 0x3ff,
  0x367 & 0x3ff,
  0x361 & 0x3ff,
  0x35c & 0x3ff,
  0x356 & 0x3ff,
  0x350 & 0x3ff,
  0x34b & 0x3ff,
  0x345 & 0x3ff,
  0x340 & 0x3ff,
  0x33b & 0x3ff,
  0x335 & 0x3ff,
  0x330 & 0x3ff,
  0x32c & 0x3ff,
  0x327 & 0x3ff,
  0x322 & 0x3ff,
  0x31c & 0x3ff,
  0x318 & 0x3ff,
  0x314 & 0x3ff,
  0x30e & 0x3ff,
  0x30a & 0x3ff,
  0x306 & 0x3ff,
  0x300 & 0x3ff,
  0x2fc & 0x3ff,
  0x2f8 & 0x3ff,
  0x2f4 & 0x3ff,
  0x2f0 & 0x3ff,
  0x2ea & 0x3ff,
  0x2e6 & 0x3ff,
  0x2e2 & 0x3ff,
  0x2de & 0x3ff,
  0x2da & 0x3ff,
  0x2d6 & 0x3ff,
  0x2d2 & 0x3ff,
  0x2ce & 0x3ff,
  0x2ca & 0x3ff,
  0x2c6 & 0x3ff,
  0x2c2 & 0x3ff,
  0x2be & 0x3ff,
  0x2ba & 0x3ff,
  0x2b8 & 0x3ff,
  0x2b4 & 0x3ff,
  0x2b0 & 0x3ff,
  0x2ac & 0x3ff,
  0x2a8 & 0x3ff,
  0x2a6 & 0x3ff,
  0x2a2 & 0x3ff,
  0x29e & 0x3ff,
  0x29c & 0x3ff,
  0x298 & 0x3ff,
  0x294 & 0x3ff,
  0x290 & 0x3ff,
  0x28e & 0x3ff,
  0x28a & 0x3ff,
  0x288 & 0x3ff,
  0x284 & 0x3ff,
  0x280 & 0x3ff,
  0x27e & 0x3ff,
  0x27a & 0x3ff,
  0x278 & 0x3ff,
  0x274 & 0x3ff,
  0x272 & 0x3ff,
  0x26e & 0x3ff,
  0x26c & 0x3ff,
  0x268 & 0x3ff,
  0x266 & 0x3ff,
  0x264 & 0x3ff,
  0x260 & 0x3ff,
  0x25e & 0x3ff,
  0x25a & 0x3ff,
  0x258 & 0x3ff,
  0x254 & 0x3ff,
  0x252 & 0x3ff,
  0x250 & 0x3ff,
  0x24c & 0x3ff,
  0x24a & 0x3ff,
  0x248 & 0x3ff,
  0x246 & 0x3ff,
  0x242 & 0x3ff,
  0x240 & 0x3ff,
  0x23e & 0x3ff,
  0x23c & 0x3ff,
  0x238 & 0x3ff,
  0x236 & 0x3ff,
  0x234 & 0x3ff,
  0x232 & 0x3ff,
  0x230 & 0x3ff,
  0x22c & 0x3ff,
  0x22a & 0x3ff,
  0x228 & 0x3ff,
  0x226 & 0x3ff,
  0x224 & 0x3ff,
  0x220 & 0x3ff,
  0x21e & 0x3ff,
  0x21c & 0x3ff,
  0x21a & 0x3ff,
  0x218 & 0x3ff,
  0x216 & 0x3ff,
  0x214 & 0x3ff,
  0x212 & 0x3ff,
  0x210 & 0x3ff,
  0x20e & 0x3ff,
  0x20c & 0x3ff,
  0x208 & 0x3ff,
  0x208 & 0x3ff,
  0x204 & 0x3ff,
  0x204 & 0x3ff,
  0x201 & 0x3ff,
  0
};

/* constant table RSQRT_10b_256 */
static const unsigned CONST_TBL_RSQRT_10b_256_0[] = {
  0x1a5 & 0x3ff,
  0x1a0 & 0x3ff,
  0x19a & 0x3ff,
  0x195 & 0x3ff,
  0x18f & 0x3ff,
  0x18a & 0x3ff,
  0x185 & 0x3ff,
  0x180 & 0x3ff,
  0x17a & 0x3ff,
  0x175 & 0x3ff,
  0x170 & 0x3ff,
  0x16b & 0x3ff,
  0x166 & 0x3ff,
  0x161 & 0x3ff,
  0x15d & 0x3ff,
  0x158 & 0x3ff,
  0x153 & 0x3ff,
  0x14e & 0x3ff,
  0x14a & 0x3ff,
  0x145 & 0x3ff,
  0x140 & 0x3ff,
  0x13c & 0x3ff,
  0x138 & 0x3ff,
  0x133 & 0x3ff,
  0x12f & 0x3ff,
  0x12a & 0x3ff,
  0x126 & 0x3ff,
  0x122 & 0x3ff,
  0x11e & 0x3ff,
  0x11a & 0x3ff,
  0x115 & 0x3ff,
  0x111 & 0x3ff,
  0x10d & 0x3ff,
  0x109 & 0x3ff,
  0x105 & 0x3ff,
  0x101 & 0x3ff,
  0xfd & 0x3ff,
  0xfa & 0x3ff,
  0xf6 & 0x3ff,
  0xf2 & 0x3ff,
  0xee & 0x3ff,
  0xea & 0x3ff,
  0xe7 & 0x3ff,
  0xe3 & 0x3ff,
  0xdf & 0x3ff,
  0xdc & 0x3ff,
  0xd8 & 0x3ff,
  0xd5 & 0x3ff,
  0xd1 & 0x3ff,
  0xce & 0x3ff,
  0xca & 0x3ff,
  0xc7 & 0x3ff,
  0xc3 & 0x3ff,
  0xc0 & 0x3ff,
  0xbd & 0x3ff,
  0xb9 & 0x3ff,
  0xb6 & 0x3ff,
  0xb3 & 0x3ff,
  0xb0 & 0x3ff,
  0xad & 0x3ff,
  0xa9 & 0x3ff,
  0xa6 & 0x3ff,
  0xa3 & 0x3ff,
  0xa0 & 0x3ff,
  0x9d & 0x3ff,
  0x9a & 0x3ff,
  0x97 & 0x3ff,
  0x94 & 0x3ff,
  0x91 & 0x3ff,
  0x8e & 0x3ff,
  0x8b & 0x3ff,
  0x88 & 0x3ff,
  0x85 & 0x3ff,
  0x82 & 0x3ff,
  0x7f & 0x3ff,
  0x7d & 0x3ff,
  0x7a & 0x3ff,
  0x77 & 0x3ff,
  0x74 & 0x3ff,
  0x71 & 0x3ff,
  0x6f & 0x3ff,
  0x6c & 0x3ff,
  0x69 & 0x3ff,
  0x67 & 0x3ff,
  0x64 & 0x3ff,
  0x61 & 0x3ff,
  0x5f & 0x3ff,
  0x5c & 0x3ff,
  0x5a & 0x3ff,
  0x57 & 0x3ff,
  0x54 & 0x3ff,
  0x52 & 0x3ff,
  0x4f & 0x3ff,
  0x4d & 0x3ff,
  0x4a & 0x3ff,
  0x48 & 0x3ff,
  0x45 & 0x3ff,
  0x43 & 0x3ff,
  0x41 & 0x3ff,
  0x3e & 0x3ff,
  0x3c & 0x3ff,
  0x3a & 0x3ff,
  0x37 & 0x3ff,
  0x35 & 0x3ff,
  0x33 & 0x3ff,
  0x30 & 0x3ff,
  0x2e & 0x3ff,
  0x2c & 0x3ff,
  0x29 & 0x3ff,
  0x27 & 0x3ff,
  0x25 & 0x3ff,
  0x23 & 0x3ff,
  0x20 & 0x3ff,
  0x1e & 0x3ff,
  0x1c & 0x3ff,
  0x1a & 0x3ff,
  0x18 & 0x3ff,
  0x16 & 0x3ff,
  0x14 & 0x3ff,
  0x11 & 0x3ff,
  0xf & 0x3ff,
  0xd & 0x3ff,
  0xb & 0x3ff,
  0x9 & 0x3ff,
  0x7 & 0x3ff,
  0x5 & 0x3ff,
  0x3 & 0x3ff,
  0x1 & 0x3ff,
  0x3fc & 0x3ff,
  0x3f4 & 0x3ff,
  0x3ec & 0x3ff,
  0x3e5 & 0x3ff,
  0x3dd & 0x3ff,
  0x3d5 & 0x3ff,
  0x3ce & 0x3ff,
  0x3c7 & 0x3ff,
  0x3bf & 0x3ff,
  0x3b8 & 0x3ff,
  0x3b1 & 0x3ff,
  0x3aa & 0x3ff,
  0x3a3 & 0x3ff,
  0x39c & 0x3ff,
  0x395 & 0x3ff,
  0x38e & 0x3ff,
  0x388 & 0x3ff,
  0x381 & 0x3ff,
  0x37a & 0x3ff,
  0x374 & 0x3ff,
  0x36d & 0x3ff,
  0x367 & 0x3ff,
  0x361 & 0x3ff,
  0x35a & 0x3ff,
  0x354 & 0x3ff,
  0x34e & 0x3ff,
  0x348 & 0x3ff,
  0x342 & 0x3ff,
  0x33c & 0x3ff,
  0x336 & 0x3ff,
  0x330 & 0x3ff,
  0x32b & 0x3ff,
  0x325 & 0x3ff,
  0x31f & 0x3ff,
  0x31a & 0x3ff,
  0x314 & 0x3ff,
  0x30f & 0x3ff,
  0x309 & 0x3ff,
  0x304 & 0x3ff,
  0x2fe & 0x3ff,
  0x2f9 & 0x3ff,
  0x2f4 & 0x3ff,
  0x2ee & 0x3ff,
  0x2e9 & 0x3ff,
  0x2e4 & 0x3ff,
  0x2df & 0x3ff,
  0x2da & 0x3ff,
  0x2d5 & 0x3ff,
  0x2d0 & 0x3ff,
  0x2cb & 0x3ff,
  0x2c6 & 0x3ff,
  0x2c1 & 0x3ff,
  0x2bd & 0x3ff,
  0x2b8 & 0x3ff,
  0x2b3 & 0x3ff,
  0x2ae & 0x3ff,
  0x2aa & 0x3ff,
  0x2a5 & 0x3ff,
  0x2a1 & 0x3ff,
  0x29c & 0x3ff,
  0x298 & 0x3ff,
  0x293 & 0x3ff,
  0x28f & 0x3ff,
  0x28a & 0x3ff,
  0x286 & 0x3ff,
  0x282 & 0x3ff,
  0x27d & 0x3ff,
  0x279 & 0x3ff,
  0x275 & 0x3ff,
  0x271 & 0x3ff,
  0x26d & 0x3ff,
  0x268 & 0x3ff,
  0x264 & 0x3ff,
  0x260 & 0x3ff,
  0x25c & 0x3ff,
  0x258 & 0x3ff,
  0x254 & 0x3ff,
  0x250 & 0x3ff,
  0x24c & 0x3ff,
  0x249 & 0x3ff,
  0x245 & 0x3ff,
  0x241 & 0x3ff,
  0x23d & 0x3ff,
  0x239 & 0x3ff,
  0x235 & 0x3ff,
  0x232 & 0x3ff,
  0x22e & 0x3ff,
  0x22a & 0x3ff,
  0x227 & 0x3ff,
  0x223 & 0x3ff,
  0x220 & 0x3ff,
  0x21c & 0x3ff,
  0x218 & 0x3ff,
  0x215 & 0x3ff,
  0x211 & 0x3ff,
  0x20e & 0x3ff,
  0x20a & 0x3ff,
  0x207 & 0x3ff,
  0x204 & 0x3ff,
  0x200 & 0x3ff,
  0x1fd & 0x3ff,
  0x1f9 & 0x3ff,
  0x1f6 & 0x3ff,
  0x1f3 & 0x3ff,
  0x1f0 & 0x3ff,
  0x1ec & 0x3ff,
  0x1e9 & 0x3ff,
  0x1e6 & 0x3ff,
  0x1e3 & 0x3ff,
  0x1df & 0x3ff,
  0x1dc & 0x3ff,
  0x1d9 & 0x3ff,
  0x1d6 & 0x3ff,
  0x1d3 & 0x3ff,
  0x1d0 & 0x3ff,
  0x1cd & 0x3ff,
  0x1ca & 0x3ff,
  0x1c7 & 0x3ff,
  0x1c4 & 0x3ff,
  0x1c1 & 0x3ff,
  0x1be & 0x3ff,
  0x1bb & 0x3ff,
  0x1b8 & 0x3ff,
  0x1b5 & 0x3ff,
  0x1b2 & 0x3ff,
  0x1af & 0x3ff,
  0x1ac & 0x3ff,
  0x1aa & 0x3ff,
  0
};

/* constant table RECIP_10b_256 */
static const unsigned CONST_TBL_RECIP_10b_256_0[] = {
  0x3fc & 0x3ff,
  0x3f4 & 0x3ff,
  0x3ec & 0x3ff,
  0x3e4 & 0x3ff,
  0x3dd & 0x3ff,
  0x3d5 & 0x3ff,
  0x3cd & 0x3ff,
  0x3c6 & 0x3ff,
  0x3be & 0x3ff,
  0x3b7 & 0x3ff,
  0x3af & 0x3ff,
  0x3a8 & 0x3ff,
  0x3a1 & 0x3ff,
  0x399 & 0x3ff,
  0x392 & 0x3ff,
  0x38b & 0x3ff,
  0x384 & 0x3ff,
  0x37d & 0x3ff,
  0x376 & 0x3ff,
  0x36f & 0x3ff,
  0x368 & 0x3ff,
  0x361 & 0x3ff,
  0x35b & 0x3ff,
  0x354 & 0x3ff,
  0x34d & 0x3ff,
  0x346 & 0x3ff,
  0x340 & 0x3ff,
  0x339 & 0x3ff,
  0x333 & 0x3ff,
  0x32c & 0x3ff,
  0x326 & 0x3ff,
  0x320 & 0x3ff,
  0x319 & 0x3ff,
  0x313 & 0x3ff,
  0x30d & 0x3ff,
  0x307 & 0x3ff,
  0x300 & 0x3ff,
  0x2fa & 0x3ff,
  0x2f4 & 0x3ff,
  0x2ee & 0x3ff,
  0x2e8 & 0x3ff,
  0x2e2 & 0x3ff,
  0x2dc & 0x3ff,
  0x2d7 & 0x3ff,
  0x2d1 & 0x3ff,
  0x2cb & 0x3ff,
  0x2c5 & 0x3ff,
  0x2bf & 0x3ff,
  0x2ba & 0x3ff,
  0x2b4 & 0x3ff,
  0x2af & 0x3ff,
  0x2a9 & 0x3ff,
  0x2a3 & 0x3ff,
  0x29e & 0x3ff,
  0x299 & 0x3ff,
  0x293 & 0x3ff,
  0x28e & 0x3ff,
  0x288 & 0x3ff,
  0x283 & 0x3ff,
  0x27e & 0x3ff,
  0x279 & 0x3ff,
  0x273 & 0x3ff,
  0x26e & 0x3ff,
  0x269 & 0x3ff,
  0x264 & 0x3ff,
  0x25f & 0x3ff,
  0x25a & 0x3ff,
  0x255 & 0x3ff,
  0x250 & 0x3ff,
  0x24b & 0x3ff,
  0x246 & 0x3ff,
  0x241 & 0x3ff,
  0x23c & 0x3ff,
  0x237 & 0x3ff,
  0x232 & 0x3ff,
  0x22e & 0x3ff,
  0x229 & 0x3ff,
  0x224 & 0x3ff,
  0x21f & 0x3ff,
  0x21b & 0x3ff,
  0x216 & 0x3ff,
  0x211 & 0x3ff,
  0x20d & 0x3ff,
  0x208 & 0x3ff,
  0x204 & 0x3ff,
  0x1ff & 0x3ff,
  0x1fb & 0x3ff,
  0x1f6 & 0x3ff,
  0x1f2 & 0x3ff,
  0x1ed & 0x3ff,
  0x1e9 & 0x3ff,
  0x1e5 & 0x3ff,
  0x1e0 & 0x3ff,
  0x1dc & 0x3ff,
  0x1d8 & 0x3ff,
  0x1d4 & 0x3ff,
  0x1cf & 0x3ff,
  0x1cb & 0x3ff,
  0x1c7 & 0x3ff,
  0x1c3 & 0x3ff,
  0x1bf & 0x3ff,
  0x1bb & 0x3ff,
  0x1b6 & 0x3ff,
  0x1b2 & 0x3ff,
  0x1ae & 0x3ff,
  0x1aa & 0x3ff,
  0x1a6 & 0x3ff,
  0x1a2 & 0x3ff,
  0x19e & 0x3ff,
  0x19a & 0x3ff,
  0x197 & 0x3ff,
  0x193 & 0x3ff,
  0x18f & 0x3ff,
  0x18b & 0x3ff,
  0x187 & 0x3ff,
  0x183 & 0x3ff,
  0x17f & 0x3ff,
  0x17c & 0x3ff,
  0x178 & 0x3ff,
  0x174 & 0x3ff,
  0x171 & 0x3ff,
  0x16d & 0x3ff,
  0x169 & 0x3ff,
  0x166 & 0x3ff,
  0x162 & 0x3ff,
  0x15e & 0x3ff,
  0x15b & 0x3ff,
  0x157 & 0x3ff,
  0x154 & 0x3ff,
  0x150 & 0x3ff,
  0x14d & 0x3ff,
  0x149 & 0x3ff,
  0x146 & 0x3ff,
  0x142 & 0x3ff,
  0x13f & 0x3ff,
  0x13b & 0x3ff,
  0x138 & 0x3ff,
  0x134 & 0x3ff,
  0x131 & 0x3ff,
  0x12e & 0x3ff,
  0x12a & 0x3ff,
  0x127 & 0x3ff,
  0x124 & 0x3ff,
  0x120 & 0x3ff,
  0x11d & 0x3ff,
  0x11a & 0x3ff,
  0x117 & 0x3ff,
  0x113 & 0x3ff,
  0x110 & 0x3ff,
  0x10d & 0x3ff,
  0x10a & 0x3ff,
  0x107 & 0x3ff,
  0x103 & 0x3ff,
  0x100 & 0x3ff,
  0xfd & 0x3ff,
  0xfa & 0x3ff,
  0xf7 & 0x3ff,
  0xf4 & 0x3ff,
  0xf1 & 0x3ff,
  0xee & 0x3ff,
  0xeb & 0x3ff,
  0xe8 & 0x3ff,
  0xe5 & 0x3ff,
  0xe2 & 0x3ff,
  0xdf & 0x3ff,
  0xdc & 0x3ff,
  0xd9 & 0x3ff,
  0xd6 & 0x3ff,
  0xd3 & 0x3ff,
  0xd0 & 0x3ff,
  0xcd & 0x3ff,
  0xca & 0x3ff,
  0xc8 & 0x3ff,
  0xc5 & 0x3ff,
  0xc2 & 0x3ff,
  0xbf & 0x3ff,
  0xbc & 0x3ff,
  0xb9 & 0x3ff,
  0xb7 & 0x3ff,
  0xb4 & 0x3ff,
  0xb1 & 0x3ff,
  0xae & 0x3ff,
  0xac & 0x3ff,
  0xa9 & 0x3ff,
  0xa6 & 0x3ff,
  0xa4 & 0x3ff,
  0xa1 & 0x3ff,
  0x9e & 0x3ff,
  0x9c & 0x3ff,
  0x99 & 0x3ff,
  0x96 & 0x3ff,
  0x94 & 0x3ff,
  0x91 & 0x3ff,
  0x8e & 0x3ff,
  0x8c & 0x3ff,
  0x89 & 0x3ff,
  0x87 & 0x3ff,
  0x84 & 0x3ff,
  0x82 & 0x3ff,
  0x7f & 0x3ff,
  0x7c & 0x3ff,
  0x7a & 0x3ff,
  0x77 & 0x3ff,
  0x75 & 0x3ff,
  0x73 & 0x3ff,
  0x70 & 0x3ff,
  0x6e & 0x3ff,
  0x6b & 0x3ff,
  0x69 & 0x3ff,
  0x66 & 0x3ff,
  0x64 & 0x3ff,
  0x61 & 0x3ff,
  0x5f & 0x3ff,
  0x5d & 0x3ff,
  0x5a & 0x3ff,
  0x58 & 0x3ff,
  0x56 & 0x3ff,
  0x53 & 0x3ff,
  0x51 & 0x3ff,
  0x4f & 0x3ff,
  0x4c & 0x3ff,
  0x4a & 0x3ff,
  0x48 & 0x3ff,
  0x45 & 0x3ff,
  0x43 & 0x3ff,
  0x41 & 0x3ff,
  0x3f & 0x3ff,
  0x3c & 0x3ff,
  0x3a & 0x3ff,
  0x38 & 0x3ff,
  0x36 & 0x3ff,
  0x33 & 0x3ff,
  0x31 & 0x3ff,
  0x2f & 0x3ff,
  0x2d & 0x3ff,
  0x2b & 0x3ff,
  0x29 & 0x3ff,
  0x26 & 0x3ff,
  0x24 & 0x3ff,
  0x22 & 0x3ff,
  0x20 & 0x3ff,
  0x1e & 0x3ff,
  0x1c & 0x3ff,
  0x1a & 0x3ff,
  0x18 & 0x3ff,
  0x15 & 0x3ff,
  0x13 & 0x3ff,
  0x11 & 0x3ff,
  0xf & 0x3ff,
  0xd & 0x3ff,
  0xb & 0x3ff,
  0x9 & 0x3ff,
  0x7 & 0x3ff,
  0x5 & 0x3ff,
  0x3 & 0x3ff,
  0x1 & 0x3ff,
  0
};


/* Instruction operands.  */

static int
OperandSem_opnd_sem_MR_0_decode (uint32 *valp)
{
  *valp += 2;
  return 0;
}

static int
OperandSem_opnd_sem_MR_0_encode (uint32 *valp)
{
  int error;
  error = ((*valp & ~0x3) != 0) || ((*valp & 0x2) == 0);
  *valp = *valp & 1;
  return error;
}

static int
OperandSem_opnd_sem_soffsetx4_decode (uint32 *valp)
{
  unsigned soffsetx4_out_0;
  unsigned soffsetx4_in_0;
  soffsetx4_in_0 = *valp & 0x3ffff;
  soffsetx4_out_0 = 0x4 + ((((int) soffsetx4_in_0 << 14) >> 14) << 2);
  *valp = soffsetx4_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_soffsetx4_encode (uint32 *valp)
{
  unsigned soffsetx4_in_0;
  unsigned soffsetx4_out_0;
  soffsetx4_out_0 = *valp;
  soffsetx4_in_0 = ((soffsetx4_out_0 - 0x4) >> 2) & 0x3ffff;
  *valp = soffsetx4_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_immr_decode (uint32 *valp)
{
  unsigned immr_out_0;
  unsigned immr_in_0;
  immr_in_0 = *valp & 0xf;
  immr_out_0 = immr_in_0;
  *valp = immr_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_immr_encode (uint32 *valp)
{
  unsigned immr_in_0;
  unsigned immr_out_0;
  immr_out_0 = *valp;
  immr_in_0 = (immr_out_0 & 0xf);
  *valp = immr_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_uimm12x8_decode (uint32 *valp)
{
  unsigned uimm12x8_out_0;
  unsigned uimm12x8_in_0;
  uimm12x8_in_0 = *valp & 0xfff;
  uimm12x8_out_0 = uimm12x8_in_0 << 3;
  *valp = uimm12x8_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_uimm12x8_encode (uint32 *valp)
{
  unsigned uimm12x8_in_0;
  unsigned uimm12x8_out_0;
  uimm12x8_out_0 = *valp;
  uimm12x8_in_0 = ((uimm12x8_out_0 >> 3) & 0xfff);
  *valp = uimm12x8_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_simm4_decode (uint32 *valp)
{
  unsigned simm4_out_0;
  unsigned simm4_in_0;
  simm4_in_0 = *valp & 0xf;
  simm4_out_0 = ((int) simm4_in_0 << 28) >> 28;
  *valp = simm4_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_simm4_encode (uint32 *valp)
{
  unsigned simm4_in_0;
  unsigned simm4_out_0;
  simm4_out_0 = *valp;
  simm4_in_0 = (simm4_out_0 & 0xf);
  *valp = simm4_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_AR_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
OperandSem_opnd_sem_AR_encode (uint32 *valp)
{
  int error;
  error = (*valp >= 32);
  return error;
}

static int
OperandSem_opnd_sem_AR_0_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
OperandSem_opnd_sem_AR_0_encode (uint32 *valp)
{
  int error;
  error = (*valp >= 32);
  return error;
}

static int
OperandSem_opnd_sem_AR_4_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
OperandSem_opnd_sem_AR_4_encode (uint32 *valp)
{
  int error;
  error = (*valp >= 32);
  return error;
}

static int
OperandSem_opnd_sem_AR_8_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
OperandSem_opnd_sem_AR_8_encode (uint32 *valp)
{
  int error;
  error = (*valp >= 32);
  return error;
}

static int
OperandSem_opnd_sem_AR_12_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
OperandSem_opnd_sem_AR_12_encode (uint32 *valp)
{
  int error;
  error = (*valp >= 32);
  return error;
}

static int
OperandSem_opnd_sem_AR_entry_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
OperandSem_opnd_sem_AR_entry_encode (uint32 *valp)
{
  int error;
  error = (*valp >= 32);
  return error;
}

static int
OperandSem_opnd_sem_immrx4_decode (uint32 *valp)
{
  unsigned immrx4_out_0;
  unsigned immrx4_in_0;
  immrx4_in_0 = *valp & 0xf;
  immrx4_out_0 = (((0xfffffff) << 4) | immrx4_in_0) << 2;
  *valp = immrx4_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_immrx4_encode (uint32 *valp)
{
  unsigned immrx4_in_0;
  unsigned immrx4_out_0;
  immrx4_out_0 = *valp;
  immrx4_in_0 = ((immrx4_out_0 >> 2) & 0xf);
  *valp = immrx4_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_lsi4x4_decode (uint32 *valp)
{
  unsigned lsi4x4_out_0;
  unsigned lsi4x4_in_0;
  lsi4x4_in_0 = *valp & 0xf;
  lsi4x4_out_0 = lsi4x4_in_0 << 2;
  *valp = lsi4x4_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_lsi4x4_encode (uint32 *valp)
{
  unsigned lsi4x4_in_0;
  unsigned lsi4x4_out_0;
  lsi4x4_out_0 = *valp;
  lsi4x4_in_0 = ((lsi4x4_out_0 >> 2) & 0xf);
  *valp = lsi4x4_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_simm7_decode (uint32 *valp)
{
  unsigned simm7_out_0;
  unsigned simm7_in_0;
  simm7_in_0 = *valp & 0x7f;
  simm7_out_0 = ((((-((((simm7_in_0 >> 6) & 1)) & (((simm7_in_0 >> 5) & 1)))) & 0x1ffffff)) << 7) | simm7_in_0;
  *valp = simm7_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_simm7_encode (uint32 *valp)
{
  unsigned simm7_in_0;
  unsigned simm7_out_0;
  simm7_out_0 = *valp;
  simm7_in_0 = (simm7_out_0 & 0x7f);
  *valp = simm7_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_uimm6_decode (uint32 *valp)
{
  unsigned uimm6_out_0;
  unsigned uimm6_in_0;
  uimm6_in_0 = *valp & 0x3f;
  uimm6_out_0 = 0x4 + (((0) << 6) | uimm6_in_0);
  *valp = uimm6_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_uimm6_encode (uint32 *valp)
{
  unsigned uimm6_in_0;
  unsigned uimm6_out_0;
  uimm6_out_0 = *valp;
  uimm6_in_0 = (uimm6_out_0 - 0x4) & 0x3f;
  *valp = uimm6_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_ai4const_decode (uint32 *valp)
{
  unsigned ai4const_out_0;
  unsigned ai4const_in_0;
  ai4const_in_0 = *valp & 0xf;
  ai4const_out_0 = CONST_TBL_ai4c_0[ai4const_in_0 & 0xf];
  *valp = ai4const_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_ai4const_encode (uint32 *valp)
{
  unsigned ai4const_in_0;
  unsigned ai4const_out_0;
  ai4const_out_0 = *valp;
  switch (ai4const_out_0)
    {
    case 0xffffffff: ai4const_in_0 = 0; break;
    case 0x1: ai4const_in_0 = 0x1; break;
    case 0x2: ai4const_in_0 = 0x2; break;
    case 0x3: ai4const_in_0 = 0x3; break;
    case 0x4: ai4const_in_0 = 0x4; break;
    case 0x5: ai4const_in_0 = 0x5; break;
    case 0x6: ai4const_in_0 = 0x6; break;
    case 0x7: ai4const_in_0 = 0x7; break;
    case 0x8: ai4const_in_0 = 0x8; break;
    case 0x9: ai4const_in_0 = 0x9; break;
    case 0xa: ai4const_in_0 = 0xa; break;
    case 0xb: ai4const_in_0 = 0xb; break;
    case 0xc: ai4const_in_0 = 0xc; break;
    case 0xd: ai4const_in_0 = 0xd; break;
    case 0xe: ai4const_in_0 = 0xe; break;
    default: ai4const_in_0 = 0xf; break;
    }
  *valp = ai4const_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_b4const_decode (uint32 *valp)
{
  unsigned b4const_out_0;
  unsigned b4const_in_0;
  b4const_in_0 = *valp & 0xf;
  b4const_out_0 = CONST_TBL_b4c_0[b4const_in_0 & 0xf];
  *valp = b4const_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_b4const_encode (uint32 *valp)
{
  unsigned b4const_in_0;
  unsigned b4const_out_0;
  b4const_out_0 = *valp;
  switch (b4const_out_0)
    {
    case 0xffffffff: b4const_in_0 = 0; break;
    case 0x1: b4const_in_0 = 0x1; break;
    case 0x2: b4const_in_0 = 0x2; break;
    case 0x3: b4const_in_0 = 0x3; break;
    case 0x4: b4const_in_0 = 0x4; break;
    case 0x5: b4const_in_0 = 0x5; break;
    case 0x6: b4const_in_0 = 0x6; break;
    case 0x7: b4const_in_0 = 0x7; break;
    case 0x8: b4const_in_0 = 0x8; break;
    case 0xa: b4const_in_0 = 0x9; break;
    case 0xc: b4const_in_0 = 0xa; break;
    case 0x10: b4const_in_0 = 0xb; break;
    case 0x20: b4const_in_0 = 0xc; break;
    case 0x40: b4const_in_0 = 0xd; break;
    case 0x80: b4const_in_0 = 0xe; break;
    default: b4const_in_0 = 0xf; break;
    }
  *valp = b4const_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_b4constu_decode (uint32 *valp)
{
  unsigned b4constu_out_0;
  unsigned b4constu_in_0;
  b4constu_in_0 = *valp & 0xf;
  b4constu_out_0 = CONST_TBL_b4cu_0[b4constu_in_0 & 0xf];
  *valp = b4constu_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_b4constu_encode (uint32 *valp)
{
  unsigned b4constu_in_0;
  unsigned b4constu_out_0;
  b4constu_out_0 = *valp;
  switch (b4constu_out_0)
    {
    case 0x8000: b4constu_in_0 = 0; break;
    case 0x10000: b4constu_in_0 = 0x1; break;
    case 0x2: b4constu_in_0 = 0x2; break;
    case 0x3: b4constu_in_0 = 0x3; break;
    case 0x4: b4constu_in_0 = 0x4; break;
    case 0x5: b4constu_in_0 = 0x5; break;
    case 0x6: b4constu_in_0 = 0x6; break;
    case 0x7: b4constu_in_0 = 0x7; break;
    case 0x8: b4constu_in_0 = 0x8; break;
    case 0xa: b4constu_in_0 = 0x9; break;
    case 0xc: b4constu_in_0 = 0xa; break;
    case 0x10: b4constu_in_0 = 0xb; break;
    case 0x20: b4constu_in_0 = 0xc; break;
    case 0x40: b4constu_in_0 = 0xd; break;
    case 0x80: b4constu_in_0 = 0xe; break;
    default: b4constu_in_0 = 0xf; break;
    }
  *valp = b4constu_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_immt_decode (uint32 *valp)
{
  unsigned immt_out_0;
  unsigned immt_in_0;
  immt_in_0 = *valp & 0xf;
  immt_out_0 = immt_in_0;
  *valp = immt_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_immt_encode (uint32 *valp)
{
  unsigned immt_in_0;
  unsigned immt_out_0;
  immt_out_0 = *valp;
  immt_in_0 = immt_out_0 & 0xf;
  *valp = immt_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_uimms8_decode (uint32 *valp)
{
  unsigned uimms8_out_0;
  unsigned uimms8_in_0;
  uimms8_in_0 = *valp & 0x7;
  uimms8_out_0 = uimms8_in_0;
  *valp = uimms8_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_uimms8_encode (uint32 *valp)
{
  unsigned uimms8_in_0;
  unsigned uimms8_out_0;
  uimms8_out_0 = *valp;
  uimms8_in_0 = uimms8_out_0 & 0x7;
  *valp = uimms8_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_uimm8_decode (uint32 *valp)
{
  unsigned uimm8_out_0;
  unsigned uimm8_in_0;
  uimm8_in_0 = *valp & 0xff;
  uimm8_out_0 = uimm8_in_0;
  *valp = uimm8_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_uimm8_encode (uint32 *valp)
{
  unsigned uimm8_in_0;
  unsigned uimm8_out_0;
  uimm8_out_0 = *valp;
  uimm8_in_0 = (uimm8_out_0 & 0xff);
  *valp = uimm8_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_uimm8x2_decode (uint32 *valp)
{
  unsigned uimm8x2_out_0;
  unsigned uimm8x2_in_0;
  uimm8x2_in_0 = *valp & 0xff;
  uimm8x2_out_0 = uimm8x2_in_0 << 1;
  *valp = uimm8x2_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_uimm8x2_encode (uint32 *valp)
{
  unsigned uimm8x2_in_0;
  unsigned uimm8x2_out_0;
  uimm8x2_out_0 = *valp;
  uimm8x2_in_0 = ((uimm8x2_out_0 >> 1) & 0xff);
  *valp = uimm8x2_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_uimm8x4_decode (uint32 *valp)
{
  unsigned uimm8x4_out_0;
  unsigned uimm8x4_in_0;
  uimm8x4_in_0 = *valp & 0xff;
  uimm8x4_out_0 = uimm8x4_in_0 << 2;
  *valp = uimm8x4_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_uimm8x4_encode (uint32 *valp)
{
  unsigned uimm8x4_in_0;
  unsigned uimm8x4_out_0;
  uimm8x4_out_0 = *valp;
  uimm8x4_in_0 = ((uimm8x4_out_0 >> 2) & 0xff);
  *valp = uimm8x4_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_uimm4x16_decode (uint32 *valp)
{
  unsigned uimm4x16_out_0;
  unsigned uimm4x16_in_0;
  uimm4x16_in_0 = *valp & 0xf;
  uimm4x16_out_0 = uimm4x16_in_0 << 4;
  *valp = uimm4x16_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_uimm4x16_encode (uint32 *valp)
{
  unsigned uimm4x16_in_0;
  unsigned uimm4x16_out_0;
  uimm4x16_out_0 = *valp;
  uimm4x16_in_0 = ((uimm4x16_out_0 >> 4) & 0xf);
  *valp = uimm4x16_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_uimmrx4_decode (uint32 *valp)
{
  unsigned uimmrx4_out_0;
  unsigned uimmrx4_in_0;
  uimmrx4_in_0 = *valp & 0xf;
  uimmrx4_out_0 = uimmrx4_in_0 << 2;
  *valp = uimmrx4_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_uimmrx4_encode (uint32 *valp)
{
  unsigned uimmrx4_in_0;
  unsigned uimmrx4_out_0;
  uimmrx4_out_0 = *valp;
  uimmrx4_in_0 = ((uimmrx4_out_0 >> 2) & 0xf);
  *valp = uimmrx4_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_simm8_decode (uint32 *valp)
{
  unsigned simm8_out_0;
  unsigned simm8_in_0;
  simm8_in_0 = *valp & 0xff;
  simm8_out_0 = ((int) simm8_in_0 << 24) >> 24;
  *valp = simm8_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_simm8_encode (uint32 *valp)
{
  unsigned simm8_in_0;
  unsigned simm8_out_0;
  simm8_out_0 = *valp;
  simm8_in_0 = (simm8_out_0 & 0xff);
  *valp = simm8_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_simm8x256_decode (uint32 *valp)
{
  unsigned simm8x256_out_0;
  unsigned simm8x256_in_0;
  simm8x256_in_0 = *valp & 0xff;
  simm8x256_out_0 = (((int) simm8x256_in_0 << 24) >> 24) << 8;
  *valp = simm8x256_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_simm8x256_encode (uint32 *valp)
{
  unsigned simm8x256_in_0;
  unsigned simm8x256_out_0;
  simm8x256_out_0 = *valp;
  simm8x256_in_0 = ((simm8x256_out_0 >> 8) & 0xff);
  *valp = simm8x256_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_simm12b_decode (uint32 *valp)
{
  unsigned simm12b_out_0;
  unsigned simm12b_in_0;
  simm12b_in_0 = *valp & 0xfff;
  simm12b_out_0 = ((int) simm12b_in_0 << 20) >> 20;
  *valp = simm12b_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_simm12b_encode (uint32 *valp)
{
  unsigned simm12b_in_0;
  unsigned simm12b_out_0;
  simm12b_out_0 = *valp;
  simm12b_in_0 = (simm12b_out_0 & 0xfff);
  *valp = simm12b_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_msalp32_decode (uint32 *valp)
{
  unsigned msalp32_out_0;
  unsigned msalp32_in_0;
  msalp32_in_0 = *valp & 0x1f;
  msalp32_out_0 = 0x20 - msalp32_in_0;
  *valp = msalp32_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_msalp32_encode (uint32 *valp)
{
  unsigned msalp32_in_0;
  unsigned msalp32_out_0;
  msalp32_out_0 = *valp;
  msalp32_in_0 = (0x20 - msalp32_out_0) & 0x1f;
  *valp = msalp32_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_op2p1_decode (uint32 *valp)
{
  unsigned op2p1_out_0;
  unsigned op2p1_in_0;
  op2p1_in_0 = *valp & 0xf;
  op2p1_out_0 = op2p1_in_0 + 0x1;
  *valp = op2p1_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_op2p1_encode (uint32 *valp)
{
  unsigned op2p1_in_0;
  unsigned op2p1_out_0;
  op2p1_out_0 = *valp;
  op2p1_in_0 = (op2p1_out_0 - 0x1) & 0xf;
  *valp = op2p1_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_label8_decode (uint32 *valp)
{
  unsigned label8_out_0;
  unsigned label8_in_0;
  label8_in_0 = *valp & 0xff;
  label8_out_0 = 0x4 + (((int) label8_in_0 << 24) >> 24);
  *valp = label8_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_label8_encode (uint32 *valp)
{
  unsigned label8_in_0;
  unsigned label8_out_0;
  label8_out_0 = *valp;
  label8_in_0 = (label8_out_0 - 0x4) & 0xff;
  *valp = label8_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_ulabel8_decode (uint32 *valp)
{
  unsigned ulabel8_out_0;
  unsigned ulabel8_in_0;
  ulabel8_in_0 = *valp & 0xff;
  ulabel8_out_0 = 0x4 + (((0) << 8) | ulabel8_in_0);
  *valp = ulabel8_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_ulabel8_encode (uint32 *valp)
{
  unsigned ulabel8_in_0;
  unsigned ulabel8_out_0;
  ulabel8_out_0 = *valp;
  ulabel8_in_0 = (ulabel8_out_0 - 0x4) & 0xff;
  *valp = ulabel8_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_label12_decode (uint32 *valp)
{
  unsigned label12_out_0;
  unsigned label12_in_0;
  label12_in_0 = *valp & 0xfff;
  label12_out_0 = 0x4 + (((int) label12_in_0 << 20) >> 20);
  *valp = label12_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_label12_encode (uint32 *valp)
{
  unsigned label12_in_0;
  unsigned label12_out_0;
  label12_out_0 = *valp;
  label12_in_0 = (label12_out_0 - 0x4) & 0xfff;
  *valp = label12_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_soffset_decode (uint32 *valp)
{
  unsigned soffset_out_0;
  unsigned soffset_in_0;
  soffset_in_0 = *valp & 0x3ffff;
  soffset_out_0 = 0x4 + (((int) soffset_in_0 << 14) >> 14);
  *valp = soffset_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_soffset_encode (uint32 *valp)
{
  unsigned soffset_in_0;
  unsigned soffset_out_0;
  soffset_out_0 = *valp;
  soffset_in_0 = (soffset_out_0 - 0x4) & 0x3ffff;
  *valp = soffset_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_uimm16x4_decode (uint32 *valp)
{
  unsigned uimm16x4_out_0;
  unsigned uimm16x4_in_0;
  uimm16x4_in_0 = *valp & 0xffff;
  uimm16x4_out_0 = (((0xffff) << 16) | uimm16x4_in_0) << 2;
  *valp = uimm16x4_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_uimm16x4_encode (uint32 *valp)
{
  unsigned uimm16x4_in_0;
  unsigned uimm16x4_out_0;
  uimm16x4_out_0 = *valp;
  uimm16x4_in_0 = (uimm16x4_out_0 >> 2) & 0xffff;
  *valp = uimm16x4_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_MR_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
OperandSem_opnd_sem_MR_encode (uint32 *valp)
{
  int error;
  error = (*valp >= 4);
  return error;
}

static int
OperandSem_opnd_sem_MR_1_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
OperandSem_opnd_sem_MR_1_encode (uint32 *valp)
{
  int error;
  error = (*valp >= 4);
  return error;
}

static int
OperandSem_opnd_sem_MR_2_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
OperandSem_opnd_sem_MR_2_encode (uint32 *valp)
{
  int error;
  error = (*valp >= 4);
  return error;
}

static int
OperandSem_opnd_sem_MR_3_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
OperandSem_opnd_sem_MR_3_encode (uint32 *valp)
{
  int error;
  error = (*valp >= 4);
  return error;
}

static int
OperandSem_opnd_sem_MR_4_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
OperandSem_opnd_sem_MR_4_encode (uint32 *valp)
{
  int error;
  error = (*valp >= 4);
  return error;
}

static int
OperandSem_opnd_sem_MR_5_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
OperandSem_opnd_sem_MR_5_encode (uint32 *valp)
{
  int error;
  error = (*valp >= 4);
  return error;
}

static int
OperandSem_opnd_sem_imms_decode (uint32 *valp)
{
  unsigned imms_out_0;
  unsigned imms_in_0;
  imms_in_0 = *valp & 0xf;
  imms_out_0 = imms_in_0;
  *valp = imms_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_imms_encode (uint32 *valp)
{
  unsigned imms_in_0;
  unsigned imms_out_0;
  imms_out_0 = *valp;
  imms_in_0 = imms_out_0 & 0xf;
  *valp = imms_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_BR_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
OperandSem_opnd_sem_BR_encode (uint32 *valp)
{
  int error;
  error = (*valp >= 16);
  return error;
}

static int
OperandSem_opnd_sem_BR2_decode (uint32 *valp)
{
  *valp = *valp << 1;
  return 0;
}

static int
OperandSem_opnd_sem_BR2_encode (uint32 *valp)
{
  int error;
  error = (*valp >= 16) || ((*valp & 1) != 0);
  *valp = *valp >> 1;
  return error;
}

static int
OperandSem_opnd_sem_BR4_decode (uint32 *valp)
{
  *valp = *valp << 2;
  return 0;
}

static int
OperandSem_opnd_sem_BR4_encode (uint32 *valp)
{
  int error;
  error = (*valp >= 16) || ((*valp & 3) != 0);
  *valp = *valp >> 2;
  return error;
}

static int
OperandSem_opnd_sem_BR8_decode (uint32 *valp)
{
  *valp = *valp << 3;
  return 0;
}

static int
OperandSem_opnd_sem_BR8_encode (uint32 *valp)
{
  int error;
  error = (*valp >= 16) || ((*valp & 7) != 0);
  *valp = *valp >> 3;
  return error;
}

static int
OperandSem_opnd_sem_BR16_decode (uint32 *valp)
{
  *valp = *valp << 4;
  return 0;
}

static int
OperandSem_opnd_sem_BR16_encode (uint32 *valp)
{
  int error;
  error = (*valp >= 16) || ((*valp & 15) != 0);
  *valp = *valp >> 4;
  return error;
}

static int
OperandSem_opnd_sem_tp7_decode (uint32 *valp)
{
  unsigned tp7_out_0;
  unsigned tp7_in_0;
  tp7_in_0 = *valp & 0xf;
  tp7_out_0 = tp7_in_0 + 0x7;
  *valp = tp7_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_tp7_encode (uint32 *valp)
{
  unsigned tp7_in_0;
  unsigned tp7_out_0;
  tp7_out_0 = *valp;
  tp7_in_0 = (tp7_out_0 - 0x7) & 0xf;
  *valp = tp7_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_xt_wbr15_label_decode (uint32 *valp)
{
  unsigned xt_wbr15_label_out_0;
  unsigned xt_wbr15_label_in_0;
  xt_wbr15_label_in_0 = *valp & 0x7fff;
  xt_wbr15_label_out_0 = 0x4 + (((int) xt_wbr15_label_in_0 << 17) >> 17);
  *valp = xt_wbr15_label_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_xt_wbr15_label_encode (uint32 *valp)
{
  unsigned xt_wbr15_label_in_0;
  unsigned xt_wbr15_label_out_0;
  xt_wbr15_label_out_0 = *valp;
  xt_wbr15_label_in_0 = (xt_wbr15_label_out_0 - 0x4) & 0x7fff;
  *valp = xt_wbr15_label_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_xt_wbr18_label_decode (uint32 *valp)
{
  unsigned xt_wbr18_label_out_0;
  unsigned xt_wbr18_label_in_0;
  xt_wbr18_label_in_0 = *valp & 0x3ffff;
  xt_wbr18_label_out_0 = 0x4 + (((int) xt_wbr18_label_in_0 << 14) >> 14);
  *valp = xt_wbr18_label_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_xt_wbr18_label_encode (uint32 *valp)
{
  unsigned xt_wbr18_label_in_0;
  unsigned xt_wbr18_label_out_0;
  xt_wbr18_label_out_0 = *valp;
  xt_wbr18_label_in_0 = (xt_wbr18_label_out_0 - 0x4) & 0x3ffff;
  *valp = xt_wbr18_label_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_FR_decode (uint32 *valp ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
OperandSem_opnd_sem_FR_encode (uint32 *valp)
{
  int error;
  error = (*valp >= 16);
  return error;
}

static int
OperandSem_opnd_sem_imm_t_decode (uint32 *valp)
{
  unsigned imm_t_out_0;
  unsigned imm_t_in_0;
  imm_t_in_0 = *valp & 0xf;
  imm_t_out_0 = (0 << 4) | imm_t_in_0;
  *valp = imm_t_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_imm_t_encode (uint32 *valp)
{
  unsigned imm_t_in_0;
  unsigned imm_t_out_0;
  imm_t_out_0 = *valp;
  imm_t_in_0 = (imm_t_out_0 & 0xf);
  *valp = imm_t_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_imm8x4_decode (uint32 *valp)
{
  unsigned imm8x4_out_0;
  unsigned imm8x4_in_0;
  imm8x4_in_0 = *valp & 0xff;
  imm8x4_out_0 = (0 << 10) | (imm8x4_in_0 << 2) | 0;
  *valp = imm8x4_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_imm8x4_encode (uint32 *valp)
{
  unsigned imm8x4_in_0;
  unsigned imm8x4_out_0;
  imm8x4_out_0 = *valp;
  imm8x4_in_0 = ((imm8x4_out_0 >> 2) & 0xff);
  *valp = imm8x4_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_imm8x8_decode (uint32 *valp)
{
  unsigned imm8x8_out_0;
  unsigned imm8x8_in_0;
  imm8x8_in_0 = *valp & 0xff;
  imm8x8_out_0 = (0 << 11) | (imm8x8_in_0 << 3) | 0;
  *valp = imm8x8_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_imm8x8_encode (uint32 *valp)
{
  unsigned imm8x8_in_0;
  unsigned imm8x8_out_0;
  imm8x8_out_0 = *valp;
  imm8x8_in_0 = ((imm8x8_out_0 >> 3) & 0xff);
  *valp = imm8x8_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_bbi_decode (uint32 *valp)
{
  unsigned bbi_out_0;
  unsigned bbi_in_0;
  bbi_in_0 = *valp & 0x1f;
  bbi_out_0 = (0 << 5) | bbi_in_0;
  *valp = bbi_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_bbi_encode (uint32 *valp)
{
  unsigned bbi_in_0;
  unsigned bbi_out_0;
  bbi_out_0 = *valp;
  bbi_in_0 = (bbi_out_0 & 0x1f);
  *valp = bbi_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_s_decode (uint32 *valp)
{
  unsigned s_out_0;
  unsigned s_in_0;
  s_in_0 = *valp & 0xf;
  s_out_0 = (0 << 4) | s_in_0;
  *valp = s_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_s_encode (uint32 *valp)
{
  unsigned s_in_0;
  unsigned s_out_0;
  s_out_0 = *valp;
  s_in_0 = (s_out_0 & 0xf);
  *valp = s_in_0;
  return 0;
}

static int
OperandSem_opnd_sem_bitindex_decode (uint32 *valp)
{
  unsigned bitindex_out_0;
  unsigned bitindex_in_0;
  bitindex_in_0 = *valp & 0x1f;
  bitindex_out_0 = (0 << 5) | bitindex_in_0;
  *valp = bitindex_out_0;
  return 0;
}

static int
OperandSem_opnd_sem_bitindex_encode (uint32 *valp)
{
  unsigned bitindex_in_0;
  unsigned bitindex_out_0;
  bitindex_out_0 = *valp;
  bitindex_in_0 = (bitindex_out_0 & 0x1f);
  *valp = bitindex_in_0;
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
    OperandSem_opnd_sem_soffsetx4_encode, OperandSem_opnd_sem_soffsetx4_decode,
    Operand_soffsetx4_ator, Operand_soffsetx4_rtoa },
  { "immr", FIELD_r, -1, 0,
    0,
    OperandSem_opnd_sem_immr_encode, OperandSem_opnd_sem_immr_decode,
    0, 0 },
  { "uimm12x8", FIELD_imm12, -1, 0,
    0,
    OperandSem_opnd_sem_uimm12x8_encode, OperandSem_opnd_sem_uimm12x8_decode,
    0, 0 },
  { "simm4", FIELD_mn, -1, 0,
    0,
    OperandSem_opnd_sem_simm4_encode, OperandSem_opnd_sem_simm4_decode,
    0, 0 },
  { "arr", FIELD_r, REGFILE_AR, 1,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_AR_encode, OperandSem_opnd_sem_AR_decode,
    0, 0 },
  { "ars", FIELD_s, REGFILE_AR, 1,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_AR_encode, OperandSem_opnd_sem_AR_decode,
    0, 0 },
  { "*ars_invisible", FIELD_s, REGFILE_AR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    OperandSem_opnd_sem_AR_encode, OperandSem_opnd_sem_AR_decode,
    0, 0 },
  { "art", FIELD_t, REGFILE_AR, 1,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_AR_encode, OperandSem_opnd_sem_AR_decode,
    0, 0 },
  { "ar0", FIELD__ar0, REGFILE_AR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    OperandSem_opnd_sem_AR_0_encode, OperandSem_opnd_sem_AR_0_decode,
    0, 0 },
  { "ar4", FIELD__ar4, REGFILE_AR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    OperandSem_opnd_sem_AR_4_encode, OperandSem_opnd_sem_AR_4_decode,
    0, 0 },
  { "ar8", FIELD__ar8, REGFILE_AR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    OperandSem_opnd_sem_AR_8_encode, OperandSem_opnd_sem_AR_8_decode,
    0, 0 },
  { "ar12", FIELD__ar12, REGFILE_AR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    OperandSem_opnd_sem_AR_12_encode, OperandSem_opnd_sem_AR_12_decode,
    0, 0 },
  { "ars_entry", FIELD_s, REGFILE_AR, 1,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_AR_entry_encode, OperandSem_opnd_sem_AR_entry_decode,
    0, 0 },
  { "immrx4", FIELD_r, -1, 0,
    0,
    OperandSem_opnd_sem_immrx4_encode, OperandSem_opnd_sem_immrx4_decode,
    0, 0 },
  { "lsi4x4", FIELD_r, -1, 0,
    0,
    OperandSem_opnd_sem_lsi4x4_encode, OperandSem_opnd_sem_lsi4x4_decode,
    0, 0 },
  { "simm7", FIELD_imm7, -1, 0,
    0,
    OperandSem_opnd_sem_simm7_encode, OperandSem_opnd_sem_simm7_decode,
    0, 0 },
  { "uimm6", FIELD_imm6, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    OperandSem_opnd_sem_uimm6_encode, OperandSem_opnd_sem_uimm6_decode,
    Operand_uimm6_ator, Operand_uimm6_rtoa },
  { "ai4const", FIELD_t, -1, 0,
    0,
    OperandSem_opnd_sem_ai4const_encode, OperandSem_opnd_sem_ai4const_decode,
    0, 0 },
  { "b4const", FIELD_r, -1, 0,
    0,
    OperandSem_opnd_sem_b4const_encode, OperandSem_opnd_sem_b4const_decode,
    0, 0 },
  { "b4constu", FIELD_r, -1, 0,
    0,
    OperandSem_opnd_sem_b4constu_encode, OperandSem_opnd_sem_b4constu_decode,
    0, 0 },
  { "immt", FIELD_t, -1, 0,
    0,
    OperandSem_opnd_sem_immt_encode, OperandSem_opnd_sem_immt_decode,
    0, 0 },
  { "uimms8", FIELD_imms8, -1, 0,
    0,
    OperandSem_opnd_sem_uimms8_encode, OperandSem_opnd_sem_uimms8_decode,
    0, 0 },
  { "uimm8", FIELD_imm8, -1, 0,
    0,
    OperandSem_opnd_sem_uimm8_encode, OperandSem_opnd_sem_uimm8_decode,
    0, 0 },
  { "uimm8x2", FIELD_imm8, -1, 0,
    0,
    OperandSem_opnd_sem_uimm8x2_encode, OperandSem_opnd_sem_uimm8x2_decode,
    0, 0 },
  { "uimm8x4", FIELD_imm8, -1, 0,
    0,
    OperandSem_opnd_sem_uimm8x4_encode, OperandSem_opnd_sem_uimm8x4_decode,
    0, 0 },
  { "uimm4x16", FIELD_op2, -1, 0,
    0,
    OperandSem_opnd_sem_uimm4x16_encode, OperandSem_opnd_sem_uimm4x16_decode,
    0, 0 },
  { "uimmrx4", FIELD_r, -1, 0,
    0,
    OperandSem_opnd_sem_uimmrx4_encode, OperandSem_opnd_sem_uimmrx4_decode,
    0, 0 },
  { "simm8", FIELD_imm8, -1, 0,
    0,
    OperandSem_opnd_sem_simm8_encode, OperandSem_opnd_sem_simm8_decode,
    0, 0 },
  { "simm8x256", FIELD_imm8, -1, 0,
    0,
    OperandSem_opnd_sem_simm8x256_encode, OperandSem_opnd_sem_simm8x256_decode,
    0, 0 },
  { "simm12b", FIELD_imm12b, -1, 0,
    0,
    OperandSem_opnd_sem_simm12b_encode, OperandSem_opnd_sem_simm12b_decode,
    0, 0 },
  { "msalp32", FIELD_sal, -1, 0,
    0,
    OperandSem_opnd_sem_msalp32_encode, OperandSem_opnd_sem_msalp32_decode,
    0, 0 },
  { "op2p1", FIELD_op2, -1, 0,
    0,
    OperandSem_opnd_sem_op2p1_encode, OperandSem_opnd_sem_op2p1_decode,
    0, 0 },
  { "label8", FIELD_imm8, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    OperandSem_opnd_sem_label8_encode, OperandSem_opnd_sem_label8_decode,
    Operand_label8_ator, Operand_label8_rtoa },
  { "ulabel8", FIELD_imm8, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    OperandSem_opnd_sem_ulabel8_encode, OperandSem_opnd_sem_ulabel8_decode,
    Operand_ulabel8_ator, Operand_ulabel8_rtoa },
  { "label12", FIELD_imm12, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    OperandSem_opnd_sem_label12_encode, OperandSem_opnd_sem_label12_decode,
    Operand_label12_ator, Operand_label12_rtoa },
  { "soffset", FIELD_offset, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    OperandSem_opnd_sem_soffset_encode, OperandSem_opnd_sem_soffset_decode,
    Operand_soffset_ator, Operand_soffset_rtoa },
  { "uimm16x4", FIELD_imm16, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    OperandSem_opnd_sem_uimm16x4_encode, OperandSem_opnd_sem_uimm16x4_decode,
    Operand_uimm16x4_ator, Operand_uimm16x4_rtoa },
  { "mx", FIELD_x, REGFILE_MR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_UNKNOWN,
    OperandSem_opnd_sem_MR_encode, OperandSem_opnd_sem_MR_decode,
    0, 0 },
  { "my", FIELD_y, REGFILE_MR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_UNKNOWN,
    OperandSem_opnd_sem_MR_0_encode, OperandSem_opnd_sem_MR_0_decode,
    0, 0 },
  { "mw", FIELD_w, REGFILE_MR, 1,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_MR_1_encode, OperandSem_opnd_sem_MR_1_decode,
    0, 0 },
  { "mr0", FIELD__mr0, REGFILE_MR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    OperandSem_opnd_sem_MR_2_encode, OperandSem_opnd_sem_MR_2_decode,
    0, 0 },
  { "mr1", FIELD__mr1, REGFILE_MR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    OperandSem_opnd_sem_MR_3_encode, OperandSem_opnd_sem_MR_3_decode,
    0, 0 },
  { "mr2", FIELD__mr2, REGFILE_MR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    OperandSem_opnd_sem_MR_4_encode, OperandSem_opnd_sem_MR_4_decode,
    0, 0 },
  { "mr3", FIELD__mr3, REGFILE_MR, 1,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    OperandSem_opnd_sem_MR_5_encode, OperandSem_opnd_sem_MR_5_decode,
    0, 0 },
  { "imms", FIELD_s, -1, 0,
    0,
    OperandSem_opnd_sem_imms_encode, OperandSem_opnd_sem_imms_decode,
    0, 0 },
  { "imms1", FIELD_s, -1, 0,
    0,
    OperandSem_opnd_sem_imms_encode, OperandSem_opnd_sem_imms_decode,
    0, 0 },
  { "bt", FIELD_t, REGFILE_BR, 1,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_BR_encode, OperandSem_opnd_sem_BR_decode,
    0, 0 },
  { "bs", FIELD_s, REGFILE_BR, 1,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_BR_encode, OperandSem_opnd_sem_BR_decode,
    0, 0 },
  { "br", FIELD_r, REGFILE_BR, 1,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_BR_encode, OperandSem_opnd_sem_BR_decode,
    0, 0 },
  { "bt2", FIELD_t2, REGFILE_BR, 2,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_BR2_encode, OperandSem_opnd_sem_BR2_decode,
    0, 0 },
  { "bs2", FIELD_s2, REGFILE_BR, 2,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_BR2_encode, OperandSem_opnd_sem_BR2_decode,
    0, 0 },
  { "br2", FIELD_r2, REGFILE_BR, 2,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_BR2_encode, OperandSem_opnd_sem_BR2_decode,
    0, 0 },
  { "bt4", FIELD_t4, REGFILE_BR, 4,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_BR4_encode, OperandSem_opnd_sem_BR4_decode,
    0, 0 },
  { "bs4", FIELD_s4, REGFILE_BR, 4,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_BR4_encode, OperandSem_opnd_sem_BR4_decode,
    0, 0 },
  { "br4", FIELD_r4, REGFILE_BR, 4,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_BR4_encode, OperandSem_opnd_sem_BR4_decode,
    0, 0 },
  { "bt8", FIELD_t8, REGFILE_BR, 8,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_BR8_encode, OperandSem_opnd_sem_BR8_decode,
    0, 0 },
  { "bs8", FIELD_s8, REGFILE_BR, 8,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_BR8_encode, OperandSem_opnd_sem_BR8_decode,
    0, 0 },
  { "br8", FIELD_r8, REGFILE_BR, 8,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_BR8_encode, OperandSem_opnd_sem_BR8_decode,
    0, 0 },
  { "bt16", FIELD__bt16, REGFILE_BR, 16,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_BR16_encode, OperandSem_opnd_sem_BR16_decode,
    0, 0 },
  { "bs16", FIELD__bs16, REGFILE_BR, 16,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_BR16_encode, OperandSem_opnd_sem_BR16_decode,
    0, 0 },
  { "br16", FIELD__br16, REGFILE_BR, 16,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_BR16_encode, OperandSem_opnd_sem_BR16_decode,
    0, 0 },
  { "brall", FIELD__brall, REGFILE_BR, 16,
    XTENSA_OPERAND_IS_REGISTER | XTENSA_OPERAND_IS_INVISIBLE,
    OperandSem_opnd_sem_BR16_encode, OperandSem_opnd_sem_BR16_decode,
    0, 0 },
  { "tp7", FIELD_t, -1, 0,
    0,
    OperandSem_opnd_sem_tp7_encode, OperandSem_opnd_sem_tp7_decode,
    0, 0 },
  { "xt_wbr15_label", FIELD_xt_wbr15_imm, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    OperandSem_opnd_sem_xt_wbr15_label_encode, OperandSem_opnd_sem_xt_wbr15_label_decode,
    Operand_xt_wbr15_label_ator, Operand_xt_wbr15_label_rtoa },
  { "xt_wbr18_label", FIELD_xt_wbr18_imm, -1, 0,
    XTENSA_OPERAND_IS_PCRELATIVE,
    OperandSem_opnd_sem_xt_wbr18_label_encode, OperandSem_opnd_sem_xt_wbr18_label_decode,
    Operand_xt_wbr18_label_ator, Operand_xt_wbr18_label_rtoa },
  { "frr", FIELD_r, REGFILE_FR, 1,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_FR_encode, OperandSem_opnd_sem_FR_decode,
    0, 0 },
  { "frs", FIELD_s, REGFILE_FR, 1,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_FR_encode, OperandSem_opnd_sem_FR_decode,
    0, 0 },
  { "frt", FIELD_t, REGFILE_FR, 1,
    XTENSA_OPERAND_IS_REGISTER,
    OperandSem_opnd_sem_FR_encode, OperandSem_opnd_sem_FR_decode,
    0, 0 },
  { "imm_t", FIELD_t, -1, 0,
    0,
    OperandSem_opnd_sem_imm_t_encode, OperandSem_opnd_sem_imm_t_decode,
    0, 0 },
  { "imm_s", FIELD_s, -1, 0,
    0,
    OperandSem_opnd_sem_imm_t_encode, OperandSem_opnd_sem_imm_t_decode,
    0, 0 },
  { "imm8x4", FIELD_imm8, -1, 0,
    0,
    OperandSem_opnd_sem_imm8x4_encode, OperandSem_opnd_sem_imm8x4_decode,
    0, 0 },
  { "imm8x8", FIELD_imm8, -1, 0,
    0,
    OperandSem_opnd_sem_imm8x8_encode, OperandSem_opnd_sem_imm8x8_decode,
    0, 0 },
  { "bbi", FIELD_bbi, -1, 0,
    0,
    OperandSem_opnd_sem_bbi_encode, OperandSem_opnd_sem_bbi_decode,
    0, 0 },
  { "sae", FIELD_sae, -1, 0,
    0,
    OperandSem_opnd_sem_bbi_encode, OperandSem_opnd_sem_bbi_decode,
    0, 0 },
  { "sas", FIELD_sas, -1, 0,
    0,
    OperandSem_opnd_sem_bbi_encode, OperandSem_opnd_sem_bbi_decode,
    0, 0 },
  { "sargt", FIELD_sargt, -1, 0,
    0,
    OperandSem_opnd_sem_bbi_encode, OperandSem_opnd_sem_bbi_decode,
    0, 0 },
  { "s", FIELD_s, -1, 0,
    0,
    OperandSem_opnd_sem_s_encode, OperandSem_opnd_sem_s_decode,
    0, 0 },
  { "bitindex", FIELD_bitindex, -1, 0,
    0,
    OperandSem_opnd_sem_bitindex_encode, OperandSem_opnd_sem_bitindex_decode,
    0, 0 },
  { "t", FIELD_t, -1, 0, 0, 0, 0, 0, 0 },
  { "bbi4", FIELD_bbi4, -1, 0, 0, 0, 0, 0, 0 },
  { "imm12", FIELD_imm12, -1, 0, 0, 0, 0, 0, 0 },
  { "imm8", FIELD_imm8, -1, 0, 0, 0, 0, 0, 0 },
  { "s8", FIELD_s8, -1, 0, 0, 0, 0, 0, 0 },
  { "imms8", FIELD_imms8, -1, 0, 0, 0, 0, 0, 0 },
  { "imm12b", FIELD_imm12b, -1, 0, 0, 0, 0, 0, 0 },
  { "imm16", FIELD_imm16, -1, 0, 0, 0, 0, 0, 0 },
  { "m", FIELD_m, -1, 0, 0, 0, 0, 0, 0 },
  { "n", FIELD_n, -1, 0, 0, 0, 0, 0, 0 },
  { "offset", FIELD_offset, -1, 0, 0, 0, 0, 0, 0 },
  { "op0", FIELD_op0, -1, 0, 0, 0, 0, 0, 0 },
  { "op1", FIELD_op1, -1, 0, 0, 0, 0, 0, 0 },
  { "op2", FIELD_op2, -1, 0, 0, 0, 0, 0, 0 },
  { "r", FIELD_r, -1, 0, 0, 0, 0, 0, 0 },
  { "r_disp", FIELD_r_disp, -1, 0, 0, 0, 0, 0, 0 },
  { "r_3", FIELD_r_3, -1, 0, 0, 0, 0, 0, 0 },
  { "sa4", FIELD_sa4, -1, 0, 0, 0, 0, 0, 0 },
  { "sae4", FIELD_sae4, -1, 0, 0, 0, 0, 0, 0 },
  { "sal", FIELD_sal, -1, 0, 0, 0, 0, 0, 0 },
  { "sas4", FIELD_sas4, -1, 0, 0, 0, 0, 0, 0 },
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
  { "t2", FIELD_t2, -1, 0, 0, 0, 0, 0, 0 },
  { "s2", FIELD_s2, -1, 0, 0, 0, 0, 0, 0 },
  { "r2", FIELD_r2, -1, 0, 0, 0, 0, 0, 0 },
  { "t4", FIELD_t4, -1, 0, 0, 0, 0, 0, 0 },
  { "s4", FIELD_s4, -1, 0, 0, 0, 0, 0, 0 },
  { "r4", FIELD_r4, -1, 0, 0, 0, 0, 0, 0 },
  { "t8", FIELD_t8, -1, 0, 0, 0, 0, 0, 0 },
  { "r8", FIELD_r8, -1, 0, 0, 0, 0, 0, 0 },
  { "xt_wbr15_imm", FIELD_xt_wbr15_imm, -1, 0, 0, 0, 0, 0, 0 },
  { "xt_wbr18_imm", FIELD_xt_wbr18_imm, -1, 0, 0, 0, 0, 0, 0 },
  { "s3to1", FIELD_s3to1, -1, 0, 0, 0, 0, 0, 0 }
};

enum xtensa_operand_id {
  OPERAND_soffsetx4,
  OPERAND_immr,
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
  OPERAND_immt,
  OPERAND_uimms8,
  OPERAND_uimm8,
  OPERAND_uimm8x2,
  OPERAND_uimm8x4,
  OPERAND_uimm4x16,
  OPERAND_uimmrx4,
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
  OPERAND_imms,
  OPERAND_imms1,
  OPERAND_bt,
  OPERAND_bs,
  OPERAND_br,
  OPERAND_bt2,
  OPERAND_bs2,
  OPERAND_br2,
  OPERAND_bt4,
  OPERAND_bs4,
  OPERAND_br4,
  OPERAND_bt8,
  OPERAND_bs8,
  OPERAND_br8,
  OPERAND_bt16,
  OPERAND_bs16,
  OPERAND_br16,
  OPERAND_brall,
  OPERAND_tp7,
  OPERAND_xt_wbr15_label,
  OPERAND_xt_wbr18_label,
  OPERAND_frr,
  OPERAND_frs,
  OPERAND_frt,
  OPERAND_imm_t,
  OPERAND_imm_s,
  OPERAND_imm8x4,
  OPERAND_imm8x8,
  OPERAND_bbi,
  OPERAND_sae,
  OPERAND_sas,
  OPERAND_sargt,
  OPERAND_s,
  OPERAND_bitindex,
  OPERAND_t,
  OPERAND_bbi4,
  OPERAND_imm12,
  OPERAND_imm8,
  OPERAND_s8,
  OPERAND_imms8,
  OPERAND_imm12b,
  OPERAND_imm16,
  OPERAND_m,
  OPERAND_n,
  OPERAND_offset,
  OPERAND_op0,
  OPERAND_op1,
  OPERAND_op2,
  OPERAND_r,
  OPERAND_r_disp,
  OPERAND_r_3,
  OPERAND_sa4,
  OPERAND_sae4,
  OPERAND_sal,
  OPERAND_sas4,
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
  OPERAND_t2,
  OPERAND_s2,
  OPERAND_r2,
  OPERAND_t4,
  OPERAND_s4,
  OPERAND_r4,
  OPERAND_t8,
  OPERAND_r8,
  OPERAND_xt_wbr15_imm,
  OPERAND_xt_wbr18_imm,
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
  { { STATE_PSCALLINC }, 'o' },
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

static xtensa_arg_internal Iclass_xt_iclass_simcall_args[] = {
  { { OPERAND_immt }, 'i' }
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

static xtensa_arg_internal Iclass_xt_iclass_s32nb_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_uimmrx4 }, 'i' }
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

static xtensa_arg_internal Iclass_xt_iclass_rsr_memctl_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_memctl_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_memctl_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_configid0_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_configid0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_configid0_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_configid0_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_configid1_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_configid1_stateArgs[] = {
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

static xtensa_arg_internal Iclass_xt_iclass_salt_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_art }, 'i' }
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

static xtensa_arg_internal Iclass_xt_iclass_lddr32_p_args[] = {
  { { OPERAND_ars }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_lddr32_p_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' },
  { { STATE_InOCDMode }, 'i' },
  { { STATE_DDR }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_sddr32_p_args[] = {
  { { OPERAND_ars }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_sddr32_p_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_InOCDMode }, 'i' },
  { { STATE_DDR }, 'i' }
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
  { { STATE_InOCDMode }, 'm' },
  { { STATE_VECBASE }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_mmid_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_mmid_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_XTSYNC }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_bbool1_args[] = {
  { { OPERAND_br }, 'o' },
  { { OPERAND_bs }, 'i' },
  { { OPERAND_bt }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_bbool4_args[] = {
  { { OPERAND_bt }, 'o' },
  { { OPERAND_bs4 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_bbool8_args[] = {
  { { OPERAND_bt }, 'o' },
  { { OPERAND_bs8 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_bbranch_args[] = {
  { { OPERAND_bs }, 'i' },
  { { OPERAND_label8 }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_bmove_args[] = {
  { { OPERAND_arr }, 'm' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_bt }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_RSR_BR_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_brall }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_WSR_BR_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_brall }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_XSR_BR_args[] = {
  { { OPERAND_art }, 'm' },
  { { OPERAND_brall }, 'm' }
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

static xtensa_arg_internal Iclass_xt_iclass_dcache_dyn_args[] = {
  { { OPERAND_ars }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_dcache_dyn_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
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

static xtensa_arg_internal Iclass_xt_iclass_sdcw_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_sdcw_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_ldcw_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_ldcw_stateArgs[] = {
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

static xtensa_arg_internal Iclass_xt_iclass_rsr_eraccess_args[] = {
  { { OPERAND_art }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_rsr_eraccess_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ERACCESS }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eraccess_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wsr_eraccess_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ERACCESS }, 'o' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eraccess_args[] = {
  { { OPERAND_art }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_xsr_eraccess_stateArgs[] = {
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' },
  { { STATE_ERACCESS }, 'm' }
};

static xtensa_arg_internal Iclass_xt_iclass_rer_args[] = {
  { { OPERAND_art }, 'o' },
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_rer_stateArgs[] = {
  { { STATE_ERACCESS }, 'i' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wer_args[] = {
  { { OPERAND_art }, 'i' },
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_xt_iclass_wer_stateArgs[] = {
  { { STATE_ERACCESS }, 'i' },
  { { STATE_PSEXCM }, 'i' },
  { { STATE_PSRING }, 'i' }
};

static xtensa_arg_internal Iclass_rur_fcr_args[] = {
  { { OPERAND_arr }, 'o' }
};

static xtensa_arg_internal Iclass_rur_fcr_stateArgs[] = {
  { { STATE_RoundMode }, 'i' },
  { { STATE_InvalidEnable }, 'i' },
  { { STATE_DivZeroEnable }, 'i' },
  { { STATE_OverflowEnable }, 'i' },
  { { STATE_UnderflowEnable }, 'i' },
  { { STATE_InexactEnable }, 'i' },
  { { STATE_FPreserved20 }, 'i' },
  { { STATE_FPreserved5 }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_wur_fcr_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_wur_fcr_stateArgs[] = {
  { { STATE_RoundMode }, 'o' },
  { { STATE_InvalidEnable }, 'o' },
  { { STATE_DivZeroEnable }, 'o' },
  { { STATE_OverflowEnable }, 'o' },
  { { STATE_UnderflowEnable }, 'o' },
  { { STATE_InexactEnable }, 'o' },
  { { STATE_FPreserved20 }, 'o' },
  { { STATE_FPreserved5 }, 'o' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_rur_fsr_args[] = {
  { { OPERAND_arr }, 'o' }
};

static xtensa_arg_internal Iclass_rur_fsr_stateArgs[] = {
  { { STATE_InvalidFlag }, 'i' },
  { { STATE_DivZeroFlag }, 'i' },
  { { STATE_OverflowFlag }, 'i' },
  { { STATE_UnderflowFlag }, 'i' },
  { { STATE_InexactFlag }, 'i' },
  { { STATE_FPreserved20a }, 'i' },
  { { STATE_FPreserved7 }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_wur_fsr_args[] = {
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_wur_fsr_stateArgs[] = {
  { { STATE_InvalidFlag }, 'o' },
  { { STATE_DivZeroFlag }, 'o' },
  { { STATE_OverflowFlag }, 'o' },
  { { STATE_UnderflowFlag }, 'o' },
  { { STATE_InexactFlag }, 'o' },
  { { STATE_FPreserved20a }, 'o' },
  { { STATE_FPreserved7 }, 'o' },
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

static xtensa_arg_internal Iclass_LSI_args[] = {
  { { OPERAND_frt }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_imm8x4 }, 'i' }
};

static xtensa_arg_internal Iclass_LSI_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_LSIP_args[] = {
  { { OPERAND_frt }, 'o' },
  { { OPERAND_ars }, 'm' },
  { { OPERAND_imm8x4 }, 'i' }
};

static xtensa_arg_internal Iclass_LSIP_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_LSX_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_LSX_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_LSXP_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_ars }, 'm' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_LSXP_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_SSI_args[] = {
  { { OPERAND_frt }, 'i' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_imm8x4 }, 'i' }
};

static xtensa_arg_internal Iclass_SSI_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_SSIP_args[] = {
  { { OPERAND_frt }, 'i' },
  { { OPERAND_ars }, 'm' },
  { { OPERAND_imm8x4 }, 'i' }
};

static xtensa_arg_internal Iclass_SSIP_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_SSX_args[] = {
  { { OPERAND_frr }, 'i' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_SSX_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_SSXP_args[] = {
  { { OPERAND_frr }, 'i' },
  { { OPERAND_ars }, 'm' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_SSXP_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_LDI_args[] = {
  { { OPERAND_frt }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_imm8x8 }, 'i' }
};

static xtensa_arg_internal Iclass_LDI_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_LDIP_args[] = {
  { { OPERAND_frt }, 'o' },
  { { OPERAND_ars }, 'm' },
  { { OPERAND_imm8x8 }, 'i' }
};

static xtensa_arg_internal Iclass_LDIP_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_LDX_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_LDX_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_LDXP_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_ars }, 'm' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_LDXP_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_SDI_args[] = {
  { { OPERAND_frt }, 'i' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_imm8x8 }, 'i' }
};

static xtensa_arg_internal Iclass_SDI_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_SDIP_args[] = {
  { { OPERAND_frt }, 'i' },
  { { OPERAND_ars }, 'm' },
  { { OPERAND_imm8x8 }, 'i' }
};

static xtensa_arg_internal Iclass_SDIP_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_SDX_args[] = {
  { { OPERAND_frr }, 'i' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_SDX_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_SDXP_args[] = {
  { { OPERAND_frr }, 'i' },
  { { OPERAND_ars }, 'm' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_SDXP_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_ABS_S_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_ABS_S_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_NEG_S_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_NEG_S_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_ABS_D_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_ABS_D_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_NEG_D_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_NEG_D_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MOV_S_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_MOV_S_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MOV_D_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_MOV_D_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MOVEQZ_S_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_MOVEQZ_S_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MOVNEZ_S_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_MOVNEZ_S_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MOVLTZ_S_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_MOVLTZ_S_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MOVGEZ_S_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_MOVGEZ_S_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MOVF_S_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_bt }, 'i' }
};

static xtensa_arg_internal Iclass_MOVF_S_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MOVT_S_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_bt }, 'i' }
};

static xtensa_arg_internal Iclass_MOVT_S_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_WFR_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_ars }, 'i' }
};

static xtensa_arg_internal Iclass_WFR_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_RFR_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_RFR_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_RFRD_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_RFRD_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_WFRD_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_art }, 'i' }
};

static xtensa_arg_internal Iclass_WFRD_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_ROUND_S_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_imm_t }, 'i' }
};

static xtensa_arg_internal Iclass_ROUND_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_ROUND_D_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_imm_t }, 'i' }
};

static xtensa_arg_internal Iclass_ROUND_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_CEIL_S_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_imm_t }, 'i' }
};

static xtensa_arg_internal Iclass_CEIL_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_CEIL_D_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_imm_t }, 'i' }
};

static xtensa_arg_internal Iclass_CEIL_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_FLOOR_S_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_imm_t }, 'i' }
};

static xtensa_arg_internal Iclass_FLOOR_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_FLOOR_D_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_imm_t }, 'i' }
};

static xtensa_arg_internal Iclass_FLOOR_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_TRUNC_S_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_imm_t }, 'i' }
};

static xtensa_arg_internal Iclass_TRUNC_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_TRUNC_D_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_imm_t }, 'i' }
};

static xtensa_arg_internal Iclass_TRUNC_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_UTRUNC_S_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_imm_t }, 'i' }
};

static xtensa_arg_internal Iclass_UTRUNC_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_UTRUNC_D_args[] = {
  { { OPERAND_arr }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_imm_t }, 'i' }
};

static xtensa_arg_internal Iclass_UTRUNC_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_FLOAT_S_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_imm_t }, 'i' }
};

static xtensa_arg_internal Iclass_FLOAT_S_stateArgs[] = {
  { { STATE_InexactFlag }, 'm' },
  { { STATE_RoundMode }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_FLOAT_D_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_imm_t }, 'i' }
};

static xtensa_arg_internal Iclass_FLOAT_D_stateArgs[] = {
  { { STATE_InexactFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_UFLOAT_S_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_imm_t }, 'i' }
};

static xtensa_arg_internal Iclass_UFLOAT_S_stateArgs[] = {
  { { STATE_InexactFlag }, 'm' },
  { { STATE_RoundMode }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_UFLOAT_D_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_ars }, 'i' },
  { { OPERAND_imm_t }, 'i' }
};

static xtensa_arg_internal Iclass_UFLOAT_D_stateArgs[] = {
  { { STATE_InexactFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_CVTD_S_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_CVTD_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_CVTS_D_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_CVTS_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_UnderflowFlag }, 'm' },
  { { STATE_OverflowFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_RoundMode }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_UN_S_args[] = {
  { { OPERAND_br }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_UN_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_UN_D_args[] = {
  { { OPERAND_br }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_UN_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_ULT_S_args[] = {
  { { OPERAND_br }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_ULT_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_ULT_D_args[] = {
  { { OPERAND_br }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_ULT_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_ULE_S_args[] = {
  { { OPERAND_br }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_ULE_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_ULE_D_args[] = {
  { { OPERAND_br }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_ULE_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_UEQ_S_args[] = {
  { { OPERAND_br }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_UEQ_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_UEQ_D_args[] = {
  { { OPERAND_br }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_UEQ_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_OLT_S_args[] = {
  { { OPERAND_br }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_OLT_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_OLT_D_args[] = {
  { { OPERAND_br }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_OLT_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_OLE_S_args[] = {
  { { OPERAND_br }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_OLE_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_OLE_D_args[] = {
  { { OPERAND_br }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_OLE_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_OEQ_S_args[] = {
  { { OPERAND_br }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_OEQ_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_OEQ_D_args[] = {
  { { OPERAND_br }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_OEQ_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_ADD_S_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_ADD_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_OverflowFlag }, 'm' },
  { { STATE_UnderflowFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_RoundMode }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_ADD_D_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_ADD_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_OverflowFlag }, 'm' },
  { { STATE_UnderflowFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_RoundMode }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_SUB_S_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_SUB_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_OverflowFlag }, 'm' },
  { { STATE_UnderflowFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_RoundMode }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_SUB_D_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_SUB_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_OverflowFlag }, 'm' },
  { { STATE_UnderflowFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_RoundMode }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MUL_S_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_MUL_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_OverflowFlag }, 'm' },
  { { STATE_UnderflowFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_RoundMode }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MUL_D_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_MUL_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_OverflowFlag }, 'm' },
  { { STATE_UnderflowFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_RoundMode }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MADD_S_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_MADD_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_OverflowFlag }, 'm' },
  { { STATE_UnderflowFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_RoundMode }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MADD_D_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_MADD_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_OverflowFlag }, 'm' },
  { { STATE_UnderflowFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_RoundMode }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MSUB_S_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_MSUB_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_OverflowFlag }, 'm' },
  { { STATE_UnderflowFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_RoundMode }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MSUB_D_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_MSUB_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_OverflowFlag }, 'm' },
  { { STATE_UnderflowFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_RoundMode }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_SQRT0_S_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_SQRT0_S_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_SQRT0_D_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_SQRT0_D_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_DIV0_S_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_DIV0_S_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_DIV0_D_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_DIV0_D_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_RECIP0_S_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_RECIP0_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_DivZeroFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_RECIP0_D_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_RECIP0_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_DivZeroFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_RSQRT0_S_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_RSQRT0_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_DivZeroFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_RSQRT0_D_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_RSQRT0_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_DivZeroFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MADDN_S_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_MADDN_S_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MADDN_D_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_MADDN_D_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_DIVN_S_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_DIVN_S_stateArgs[] = {
  { { STATE_OverflowFlag }, 'm' },
  { { STATE_UnderflowFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_RoundMode }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_DIVN_D_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' },
  { { OPERAND_frt }, 'i' }
};

static xtensa_arg_internal Iclass_DIVN_D_stateArgs[] = {
  { { STATE_OverflowFlag }, 'm' },
  { { STATE_UnderflowFlag }, 'm' },
  { { STATE_InexactFlag }, 'm' },
  { { STATE_RoundMode }, 'i' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_CONST_S_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_imm_s }, 'i' }
};

static xtensa_arg_internal Iclass_CONST_S_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_CONST_D_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_imm_s }, 'i' }
};

static xtensa_arg_internal Iclass_CONST_D_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_NEXP01_S_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_NEXP01_S_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_NEXP01_D_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_NEXP01_D_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_ADDEXP_S_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_ADDEXP_S_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_ADDEXP_D_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_ADDEXP_D_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_ADDEXPM_S_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_ADDEXPM_S_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_ADDEXPM_D_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_ADDEXPM_D_stateArgs[] = {
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MKDADJ_S_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_MKDADJ_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_DivZeroFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MKDADJ_D_args[] = {
  { { OPERAND_frr }, 'm' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_MKDADJ_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_DivZeroFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MKSADJ_S_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_MKSADJ_S_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
  { { STATE_CPENABLE }, 'i' }
};

static xtensa_arg_internal Iclass_MKSADJ_D_args[] = {
  { { OPERAND_frr }, 'o' },
  { { OPERAND_frs }, 'i' }
};

static xtensa_arg_internal Iclass_MKSADJ_D_stateArgs[] = {
  { { STATE_InvalidFlag }, 'm' },
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
    5, Iclass_xt_iclass_retw_stateArgs, 0, 0 },
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
    0, 0, 0, 0 },
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
  { 1, Iclass_xt_iclass_simcall_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_s16i_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_s32i_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_s32nb_args,
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
  { 1, Iclass_xt_iclass_rsr_memctl_args,
    0, 0, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_memctl_args,
    0, 0, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_memctl_args,
    0, 0, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_configid0_args,
    2, Iclass_xt_iclass_rsr_configid0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_configid0_args,
    2, Iclass_xt_iclass_wsr_configid0_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rsr_configid1_args,
    2, Iclass_xt_iclass_rsr_configid1_stateArgs, 0, 0 },
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
  { 3, Iclass_xt_iclass_salt_args,
    0, 0, 0, 0 },
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
  { 1, Iclass_xt_iclass_lddr32_p_args,
    5, Iclass_xt_iclass_lddr32_p_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_sddr32_p_args,
    4, Iclass_xt_iclass_sddr32_p_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_rfdo_args,
    10, Iclass_xt_iclass_rfdo_stateArgs, 0, 0 },
  { 0, 0 /* xt_iclass_rfdd */,
    2, Iclass_xt_iclass_rfdd_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_mmid_args,
    3, Iclass_xt_iclass_wsr_mmid_stateArgs, 0, 0 },
  { 3, Iclass_xt_iclass_bbool1_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_bbool4_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_bbool8_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_bbranch_args,
    0, 0, 0, 0 },
  { 3, Iclass_xt_iclass_bmove_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_RSR_BR_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_WSR_BR_args,
    0, 0, 0, 0 },
  { 2, Iclass_xt_iclass_XSR_BR_args,
    0, 0, 0, 0 },
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
  { 1, Iclass_xt_iclass_dcache_dyn_args,
    2, Iclass_xt_iclass_dcache_dyn_stateArgs, 0, 0 },
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
  { 2, Iclass_xt_iclass_sdcw_args,
    2, Iclass_xt_iclass_sdcw_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_ldcw_args,
    2, Iclass_xt_iclass_ldcw_stateArgs, 0, 0 },
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
  { 1, Iclass_xt_iclass_rsr_eraccess_args,
    3, Iclass_xt_iclass_rsr_eraccess_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_wsr_eraccess_args,
    3, Iclass_xt_iclass_wsr_eraccess_stateArgs, 0, 0 },
  { 1, Iclass_xt_iclass_xsr_eraccess_args,
    3, Iclass_xt_iclass_xsr_eraccess_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_rer_args,
    3, Iclass_xt_iclass_rer_stateArgs, 0, 0 },
  { 2, Iclass_xt_iclass_wer_args,
    3, Iclass_xt_iclass_wer_stateArgs, 0, 0 },
  { 1, Iclass_rur_fcr_args,
    9, Iclass_rur_fcr_stateArgs, 0, 0 },
  { 1, Iclass_wur_fcr_args,
    9, Iclass_wur_fcr_stateArgs, 0, 0 },
  { 1, Iclass_rur_fsr_args,
    8, Iclass_rur_fsr_stateArgs, 0, 0 },
  { 1, Iclass_wur_fsr_args,
    8, Iclass_wur_fsr_stateArgs, 0, 0 },
  { 1, Iclass_iclass_READ_IMPWIRE_args,
    1, Iclass_iclass_READ_IMPWIRE_stateArgs, 1, Iclass_iclass_READ_IMPWIRE_intfArgs },
  { 1, Iclass_iclass_SETB_EXPSTATE_args,
    2, Iclass_iclass_SETB_EXPSTATE_stateArgs, 0, 0 },
  { 1, Iclass_iclass_CLRB_EXPSTATE_args,
    2, Iclass_iclass_CLRB_EXPSTATE_stateArgs, 0, 0 },
  { 2, Iclass_iclass_WRMSK_EXPSTATE_args,
    2, Iclass_iclass_WRMSK_EXPSTATE_stateArgs, 0, 0 },
  { 1, Iclass_rur_expstate_args,
    2, Iclass_rur_expstate_stateArgs, 0, 0 },
  { 1, Iclass_wur_expstate_args,
    2, Iclass_wur_expstate_stateArgs, 0, 0 },
  { 3, Iclass_LSI_args,
    1, Iclass_LSI_stateArgs, 0, 0 },
  { 3, Iclass_LSIP_args,
    1, Iclass_LSIP_stateArgs, 0, 0 },
  { 3, Iclass_LSX_args,
    1, Iclass_LSX_stateArgs, 0, 0 },
  { 3, Iclass_LSXP_args,
    1, Iclass_LSXP_stateArgs, 0, 0 },
  { 3, Iclass_SSI_args,
    1, Iclass_SSI_stateArgs, 0, 0 },
  { 3, Iclass_SSIP_args,
    1, Iclass_SSIP_stateArgs, 0, 0 },
  { 3, Iclass_SSX_args,
    1, Iclass_SSX_stateArgs, 0, 0 },
  { 3, Iclass_SSXP_args,
    1, Iclass_SSXP_stateArgs, 0, 0 },
  { 3, Iclass_LDI_args,
    1, Iclass_LDI_stateArgs, 0, 0 },
  { 3, Iclass_LDIP_args,
    1, Iclass_LDIP_stateArgs, 0, 0 },
  { 3, Iclass_LDX_args,
    1, Iclass_LDX_stateArgs, 0, 0 },
  { 3, Iclass_LDXP_args,
    1, Iclass_LDXP_stateArgs, 0, 0 },
  { 3, Iclass_SDI_args,
    1, Iclass_SDI_stateArgs, 0, 0 },
  { 3, Iclass_SDIP_args,
    1, Iclass_SDIP_stateArgs, 0, 0 },
  { 3, Iclass_SDX_args,
    1, Iclass_SDX_stateArgs, 0, 0 },
  { 3, Iclass_SDXP_args,
    1, Iclass_SDXP_stateArgs, 0, 0 },
  { 2, Iclass_ABS_S_args,
    1, Iclass_ABS_S_stateArgs, 0, 0 },
  { 2, Iclass_NEG_S_args,
    1, Iclass_NEG_S_stateArgs, 0, 0 },
  { 2, Iclass_ABS_D_args,
    1, Iclass_ABS_D_stateArgs, 0, 0 },
  { 2, Iclass_NEG_D_args,
    1, Iclass_NEG_D_stateArgs, 0, 0 },
  { 2, Iclass_MOV_S_args,
    1, Iclass_MOV_S_stateArgs, 0, 0 },
  { 2, Iclass_MOV_D_args,
    1, Iclass_MOV_D_stateArgs, 0, 0 },
  { 3, Iclass_MOVEQZ_S_args,
    1, Iclass_MOVEQZ_S_stateArgs, 0, 0 },
  { 3, Iclass_MOVNEZ_S_args,
    1, Iclass_MOVNEZ_S_stateArgs, 0, 0 },
  { 3, Iclass_MOVLTZ_S_args,
    1, Iclass_MOVLTZ_S_stateArgs, 0, 0 },
  { 3, Iclass_MOVGEZ_S_args,
    1, Iclass_MOVGEZ_S_stateArgs, 0, 0 },
  { 3, Iclass_MOVF_S_args,
    1, Iclass_MOVF_S_stateArgs, 0, 0 },
  { 3, Iclass_MOVT_S_args,
    1, Iclass_MOVT_S_stateArgs, 0, 0 },
  { 2, Iclass_WFR_args,
    1, Iclass_WFR_stateArgs, 0, 0 },
  { 2, Iclass_RFR_args,
    1, Iclass_RFR_stateArgs, 0, 0 },
  { 2, Iclass_RFRD_args,
    1, Iclass_RFRD_stateArgs, 0, 0 },
  { 3, Iclass_WFRD_args,
    1, Iclass_WFRD_stateArgs, 0, 0 },
  { 3, Iclass_ROUND_S_args,
    3, Iclass_ROUND_S_stateArgs, 0, 0 },
  { 3, Iclass_ROUND_D_args,
    3, Iclass_ROUND_D_stateArgs, 0, 0 },
  { 3, Iclass_CEIL_S_args,
    3, Iclass_CEIL_S_stateArgs, 0, 0 },
  { 3, Iclass_CEIL_D_args,
    3, Iclass_CEIL_D_stateArgs, 0, 0 },
  { 3, Iclass_FLOOR_S_args,
    3, Iclass_FLOOR_S_stateArgs, 0, 0 },
  { 3, Iclass_FLOOR_D_args,
    3, Iclass_FLOOR_D_stateArgs, 0, 0 },
  { 3, Iclass_TRUNC_S_args,
    3, Iclass_TRUNC_S_stateArgs, 0, 0 },
  { 3, Iclass_TRUNC_D_args,
    3, Iclass_TRUNC_D_stateArgs, 0, 0 },
  { 3, Iclass_UTRUNC_S_args,
    3, Iclass_UTRUNC_S_stateArgs, 0, 0 },
  { 3, Iclass_UTRUNC_D_args,
    3, Iclass_UTRUNC_D_stateArgs, 0, 0 },
  { 3, Iclass_FLOAT_S_args,
    3, Iclass_FLOAT_S_stateArgs, 0, 0 },
  { 3, Iclass_FLOAT_D_args,
    2, Iclass_FLOAT_D_stateArgs, 0, 0 },
  { 3, Iclass_UFLOAT_S_args,
    3, Iclass_UFLOAT_S_stateArgs, 0, 0 },
  { 3, Iclass_UFLOAT_D_args,
    2, Iclass_UFLOAT_D_stateArgs, 0, 0 },
  { 2, Iclass_CVTD_S_args,
    2, Iclass_CVTD_S_stateArgs, 0, 0 },
  { 2, Iclass_CVTS_D_args,
    6, Iclass_CVTS_D_stateArgs, 0, 0 },
  { 3, Iclass_UN_S_args,
    2, Iclass_UN_S_stateArgs, 0, 0 },
  { 3, Iclass_UN_D_args,
    2, Iclass_UN_D_stateArgs, 0, 0 },
  { 3, Iclass_ULT_S_args,
    2, Iclass_ULT_S_stateArgs, 0, 0 },
  { 3, Iclass_ULT_D_args,
    2, Iclass_ULT_D_stateArgs, 0, 0 },
  { 3, Iclass_ULE_S_args,
    2, Iclass_ULE_S_stateArgs, 0, 0 },
  { 3, Iclass_ULE_D_args,
    2, Iclass_ULE_D_stateArgs, 0, 0 },
  { 3, Iclass_UEQ_S_args,
    2, Iclass_UEQ_S_stateArgs, 0, 0 },
  { 3, Iclass_UEQ_D_args,
    2, Iclass_UEQ_D_stateArgs, 0, 0 },
  { 3, Iclass_OLT_S_args,
    2, Iclass_OLT_S_stateArgs, 0, 0 },
  { 3, Iclass_OLT_D_args,
    2, Iclass_OLT_D_stateArgs, 0, 0 },
  { 3, Iclass_OLE_S_args,
    2, Iclass_OLE_S_stateArgs, 0, 0 },
  { 3, Iclass_OLE_D_args,
    2, Iclass_OLE_D_stateArgs, 0, 0 },
  { 3, Iclass_OEQ_S_args,
    2, Iclass_OEQ_S_stateArgs, 0, 0 },
  { 3, Iclass_OEQ_D_args,
    2, Iclass_OEQ_D_stateArgs, 0, 0 },
  { 3, Iclass_ADD_S_args,
    6, Iclass_ADD_S_stateArgs, 0, 0 },
  { 3, Iclass_ADD_D_args,
    6, Iclass_ADD_D_stateArgs, 0, 0 },
  { 3, Iclass_SUB_S_args,
    6, Iclass_SUB_S_stateArgs, 0, 0 },
  { 3, Iclass_SUB_D_args,
    6, Iclass_SUB_D_stateArgs, 0, 0 },
  { 3, Iclass_MUL_S_args,
    6, Iclass_MUL_S_stateArgs, 0, 0 },
  { 3, Iclass_MUL_D_args,
    6, Iclass_MUL_D_stateArgs, 0, 0 },
  { 3, Iclass_MADD_S_args,
    6, Iclass_MADD_S_stateArgs, 0, 0 },
  { 3, Iclass_MADD_D_args,
    6, Iclass_MADD_D_stateArgs, 0, 0 },
  { 3, Iclass_MSUB_S_args,
    6, Iclass_MSUB_S_stateArgs, 0, 0 },
  { 3, Iclass_MSUB_D_args,
    6, Iclass_MSUB_D_stateArgs, 0, 0 },
  { 2, Iclass_SQRT0_S_args,
    1, Iclass_SQRT0_S_stateArgs, 0, 0 },
  { 2, Iclass_SQRT0_D_args,
    1, Iclass_SQRT0_D_stateArgs, 0, 0 },
  { 2, Iclass_DIV0_S_args,
    1, Iclass_DIV0_S_stateArgs, 0, 0 },
  { 2, Iclass_DIV0_D_args,
    1, Iclass_DIV0_D_stateArgs, 0, 0 },
  { 2, Iclass_RECIP0_S_args,
    3, Iclass_RECIP0_S_stateArgs, 0, 0 },
  { 2, Iclass_RECIP0_D_args,
    3, Iclass_RECIP0_D_stateArgs, 0, 0 },
  { 2, Iclass_RSQRT0_S_args,
    3, Iclass_RSQRT0_S_stateArgs, 0, 0 },
  { 2, Iclass_RSQRT0_D_args,
    3, Iclass_RSQRT0_D_stateArgs, 0, 0 },
  { 3, Iclass_MADDN_S_args,
    1, Iclass_MADDN_S_stateArgs, 0, 0 },
  { 3, Iclass_MADDN_D_args,
    1, Iclass_MADDN_D_stateArgs, 0, 0 },
  { 3, Iclass_DIVN_S_args,
    5, Iclass_DIVN_S_stateArgs, 0, 0 },
  { 3, Iclass_DIVN_D_args,
    5, Iclass_DIVN_D_stateArgs, 0, 0 },
  { 2, Iclass_CONST_S_args,
    1, Iclass_CONST_S_stateArgs, 0, 0 },
  { 2, Iclass_CONST_D_args,
    1, Iclass_CONST_D_stateArgs, 0, 0 },
  { 2, Iclass_NEXP01_S_args,
    1, Iclass_NEXP01_S_stateArgs, 0, 0 },
  { 2, Iclass_NEXP01_D_args,
    1, Iclass_NEXP01_D_stateArgs, 0, 0 },
  { 2, Iclass_ADDEXP_S_args,
    1, Iclass_ADDEXP_S_stateArgs, 0, 0 },
  { 2, Iclass_ADDEXP_D_args,
    1, Iclass_ADDEXP_D_stateArgs, 0, 0 },
  { 2, Iclass_ADDEXPM_S_args,
    1, Iclass_ADDEXPM_S_stateArgs, 0, 0 },
  { 2, Iclass_ADDEXPM_D_args,
    1, Iclass_ADDEXPM_D_stateArgs, 0, 0 },
  { 2, Iclass_MKDADJ_S_args,
    3, Iclass_MKDADJ_S_stateArgs, 0, 0 },
  { 2, Iclass_MKDADJ_D_args,
    3, Iclass_MKDADJ_D_stateArgs, 0, 0 },
  { 2, Iclass_MKSADJ_S_args,
    2, Iclass_MKSADJ_S_stateArgs, 0, 0 },
  { 2, Iclass_MKSADJ_D_args,
    2, Iclass_MKSADJ_D_stateArgs, 0, 0 }
};

enum xtensa_iclass_id {
  ICLASS_xt_iclass_excw,
  ICLASS_xt_iclass_rfe,
  ICLASS_xt_iclass_rfde,
  ICLASS_xt_iclass_syscall,
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
  ICLASS_xt_iclass_simcall,
  ICLASS_xt_iclass_s16i,
  ICLASS_xt_iclass_s32i,
  ICLASS_xt_iclass_s32nb,
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
  ICLASS_xt_iclass_rsr_memctl,
  ICLASS_xt_iclass_wsr_memctl,
  ICLASS_xt_iclass_xsr_memctl,
  ICLASS_xt_iclass_rsr_configid0,
  ICLASS_xt_iclass_wsr_configid0,
  ICLASS_xt_iclass_rsr_configid1,
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
  ICLASS_xt_iclass_salt,
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
  ICLASS_xt_iclass_lddr32_p,
  ICLASS_xt_iclass_sddr32_p,
  ICLASS_xt_iclass_rfdo,
  ICLASS_xt_iclass_rfdd,
  ICLASS_xt_iclass_wsr_mmid,
  ICLASS_xt_iclass_bbool1,
  ICLASS_xt_iclass_bbool4,
  ICLASS_xt_iclass_bbool8,
  ICLASS_xt_iclass_bbranch,
  ICLASS_xt_iclass_bmove,
  ICLASS_xt_iclass_RSR_BR,
  ICLASS_xt_iclass_WSR_BR,
  ICLASS_xt_iclass_XSR_BR,
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
  ICLASS_xt_iclass_dcache_dyn,
  ICLASS_xt_iclass_dcache_ind,
  ICLASS_xt_iclass_dcache_inv,
  ICLASS_xt_iclass_dpf,
  ICLASS_xt_iclass_dcache_lock,
  ICLASS_xt_iclass_sdct,
  ICLASS_xt_iclass_ldct,
  ICLASS_xt_iclass_sdcw,
  ICLASS_xt_iclass_ldcw,
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
  ICLASS_xt_iclass_rsr_eraccess,
  ICLASS_xt_iclass_wsr_eraccess,
  ICLASS_xt_iclass_xsr_eraccess,
  ICLASS_xt_iclass_rer,
  ICLASS_xt_iclass_wer,
  ICLASS_rur_fcr,
  ICLASS_wur_fcr,
  ICLASS_rur_fsr,
  ICLASS_wur_fsr,
  ICLASS_iclass_READ_IMPWIRE,
  ICLASS_iclass_SETB_EXPSTATE,
  ICLASS_iclass_CLRB_EXPSTATE,
  ICLASS_iclass_WRMSK_EXPSTATE,
  ICLASS_rur_expstate,
  ICLASS_wur_expstate,
  ICLASS_LSI,
  ICLASS_LSIP,
  ICLASS_LSX,
  ICLASS_LSXP,
  ICLASS_SSI,
  ICLASS_SSIP,
  ICLASS_SSX,
  ICLASS_SSXP,
  ICLASS_LDI,
  ICLASS_LDIP,
  ICLASS_LDX,
  ICLASS_LDXP,
  ICLASS_SDI,
  ICLASS_SDIP,
  ICLASS_SDX,
  ICLASS_SDXP,
  ICLASS_ABS_S,
  ICLASS_NEG_S,
  ICLASS_ABS_D,
  ICLASS_NEG_D,
  ICLASS_MOV_S,
  ICLASS_MOV_D,
  ICLASS_MOVEQZ_S,
  ICLASS_MOVNEZ_S,
  ICLASS_MOVLTZ_S,
  ICLASS_MOVGEZ_S,
  ICLASS_MOVF_S,
  ICLASS_MOVT_S,
  ICLASS_WFR,
  ICLASS_RFR,
  ICLASS_RFRD,
  ICLASS_WFRD,
  ICLASS_ROUND_S,
  ICLASS_ROUND_D,
  ICLASS_CEIL_S,
  ICLASS_CEIL_D,
  ICLASS_FLOOR_S,
  ICLASS_FLOOR_D,
  ICLASS_TRUNC_S,
  ICLASS_TRUNC_D,
  ICLASS_UTRUNC_S,
  ICLASS_UTRUNC_D,
  ICLASS_FLOAT_S,
  ICLASS_FLOAT_D,
  ICLASS_UFLOAT_S,
  ICLASS_UFLOAT_D,
  ICLASS_CVTD_S,
  ICLASS_CVTS_D,
  ICLASS_UN_S,
  ICLASS_UN_D,
  ICLASS_ULT_S,
  ICLASS_ULT_D,
  ICLASS_ULE_S,
  ICLASS_ULE_D,
  ICLASS_UEQ_S,
  ICLASS_UEQ_D,
  ICLASS_OLT_S,
  ICLASS_OLT_D,
  ICLASS_OLE_S,
  ICLASS_OLE_D,
  ICLASS_OEQ_S,
  ICLASS_OEQ_D,
  ICLASS_ADD_S,
  ICLASS_ADD_D,
  ICLASS_SUB_S,
  ICLASS_SUB_D,
  ICLASS_MUL_S,
  ICLASS_MUL_D,
  ICLASS_MADD_S,
  ICLASS_MADD_D,
  ICLASS_MSUB_S,
  ICLASS_MSUB_D,
  ICLASS_SQRT0_S,
  ICLASS_SQRT0_D,
  ICLASS_DIV0_S,
  ICLASS_DIV0_D,
  ICLASS_RECIP0_S,
  ICLASS_RECIP0_D,
  ICLASS_RSQRT0_S,
  ICLASS_RSQRT0_D,
  ICLASS_MADDN_S,
  ICLASS_MADDN_D,
  ICLASS_DIVN_S,
  ICLASS_DIVN_D,
  ICLASS_CONST_S,
  ICLASS_CONST_D,
  ICLASS_NEXP01_S,
  ICLASS_NEXP01_D,
  ICLASS_ADDEXP_S,
  ICLASS_ADDEXP_D,
  ICLASS_ADDEXPM_S,
  ICLASS_ADDEXPM_D,
  ICLASS_MKDADJ_S,
  ICLASS_MKDADJ_D,
  ICLASS_MKSADJ_S,
  ICLASS_MKSADJ_D
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
Opcode_sub_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc00000;
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
Opcode_bnei_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x66;
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
Opcode_ball_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4007;
}

static void
Opcode_bany_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8007;
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
Opcode_beq_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1007;
}

static void
Opcode_bge_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa007;
}

static void
Opcode_bgeu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb007;
}

static void
Opcode_blt_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2007;
}

static void
Opcode_bltu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3007;
}

static void
Opcode_bnall_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc007;
}

static void
Opcode_bne_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9007;
}

static void
Opcode_bnone_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7;
}

static void
Opcode_beqz_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x16;
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
Opcode_bnez_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x56;
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
Opcode_loopgtz_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa076;
}

static void
Opcode_loopnez_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9076;
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
Opcode_movgez_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb30000;
}

static void
Opcode_movltz_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa30000;
}

static void
Opcode_movnez_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x930000;
}

static void
Opcode_abs_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x600100;
}

static void
Opcode_neg_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x600000;
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
Opcode_simcall_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5100;
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
Opcode_s32nb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x590000;
}

static void
Opcode_s8i_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4002;
}

static void
Opcode_ssa8b_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x403000;
}

static void
Opcode_ssa8l_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x402000;
}

static void
Opcode_ssl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x401000;
}

static void
Opcode_ssr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x400000;
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
Opcode_sra_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb10000;
}

static void
Opcode_srl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x910000;
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
Opcode_dsync_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2030;
}

static void
Opcode_esync_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2020;
}

static void
Opcode_rsync_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2010;
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
Opcode_rsr_memctl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x36100;
}

static void
Opcode_wsr_memctl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x136100;
}

static void
Opcode_xsr_memctl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x616100;
}

static void
Opcode_rsr_configid0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3b000;
}

static void
Opcode_wsr_configid0_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x13b000;
}

static void
Opcode_rsr_configid1_Slot_inst_encode (xtensa_insnbuf slotbuf)
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
Opcode_salt_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x720000;
}

static void
Opcode_saltu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x620000;
}

static void
Opcode_mul16s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd10000;
}

static void
Opcode_mul16u_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc10000;
}

static void
Opcode_mull_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x820000;
}

static void
Opcode_mul_aa_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x770004;
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
Opcode_mul_aa_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x740004;
}

static void
Opcode_umul_aa_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x730004;
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
Opcode_umul_aa_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x700004;
}

static void
Opcode_mul_ad_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x370004;
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
Opcode_mul_ad_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x340004;
}

static void
Opcode_mul_da_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x670004;
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
Opcode_mul_da_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x640004;
}

static void
Opcode_mul_dd_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x270004;
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
Opcode_mul_dd_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x240004;
}

static void
Opcode_mula_aa_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7b0004;
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
Opcode_mula_aa_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x780004;
}

static void
Opcode_muls_aa_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7f0004;
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
Opcode_muls_aa_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7c0004;
}

static void
Opcode_mula_ad_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3b0004;
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
Opcode_mula_ad_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x380004;
}

static void
Opcode_muls_ad_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3f0004;
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
Opcode_muls_ad_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3c0004;
}

static void
Opcode_mula_da_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6b0004;
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
Opcode_mula_da_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x680004;
}

static void
Opcode_muls_da_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6f0004;
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
Opcode_muls_da_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6c0004;
}

static void
Opcode_mula_dd_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2b0004;
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
Opcode_mula_dd_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x280004;
}

static void
Opcode_muls_dd_hh_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2f0004;
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
Opcode_muls_dd_ll_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2c0004;
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
Opcode_lddr32_p_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x70e0;
}

static void
Opcode_sddr32_p_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x70f0;
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
Opcode_andb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x20000;
}

static void
Opcode_andbc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x120000;
}

static void
Opcode_orb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x220000;
}

static void
Opcode_orbc_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x320000;
}

static void
Opcode_xorb_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x420000;
}

static void
Opcode_all4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9000;
}

static void
Opcode_any4_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8000;
}

static void
Opcode_all8_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xb000;
}

static void
Opcode_any8_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa000;
}

static void
Opcode_bf_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x76;
}

static void
Opcode_bt_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1076;
}

static void
Opcode_movf_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc30000;
}

static void
Opcode_movt_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd30000;
}

static void
Opcode_rsr_br_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x30400;
}

static void
Opcode_wsr_br_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x130400;
}

static void
Opcode_xsr_br_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x610400;
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
Opcode_ihi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x70e2;
}

static void
Opcode_ipf_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x70c2;
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
Opcode_ipfl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x70d2;
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
Opcode_diwbui_p_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf7082;
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
Opcode_dpfro_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7022;
}

static void
Opcode_dpfw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7012;
}

static void
Opcode_dpfwo_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7032;
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
Opcode_dpfl_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7082;
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
Opcode_sdcw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf1b000;
}

static void
Opcode_ldcw_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf1a000;
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
Opcode_max_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x530000;
}

static void
Opcode_maxu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x730000;
}

static void
Opcode_min_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x430000;
}

static void
Opcode_minu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x630000;
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
Opcode_quos_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd20000;
}

static void
Opcode_quou_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc20000;
}

static void
Opcode_rems_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf20000;
}

static void
Opcode_remu_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe20000;
}

static void
Opcode_rsr_eraccess_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x35f00;
}

static void
Opcode_wsr_eraccess_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x135f00;
}

static void
Opcode_xsr_eraccess_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x615f00;
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
Opcode_rur_fcr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe30e80;
}

static void
Opcode_wur_fcr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf3e800;
}

static void
Opcode_rur_fsr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xe30e90;
}

static void
Opcode_wur_fsr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf3e900;
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
Opcode_lsi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3;
}

static void
Opcode_lsip_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8003;
}

static void
Opcode_lsx_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x80000;
}

static void
Opcode_lsxp_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x180000;
}

static void
Opcode_ssi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4003;
}

static void
Opcode_ssip_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xc003;
}

static void
Opcode_ssx_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x480000;
}

static void
Opcode_ssxp_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x580000;
}

static void
Opcode_ldi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1003;
}

static void
Opcode_ldip_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9003;
}

static void
Opcode_ldx_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x280000;
}

static void
Opcode_ldxp_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x380000;
}

static void
Opcode_sdi_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5003;
}

static void
Opcode_sdip_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xd003;
}

static void
Opcode_sdx_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x680000;
}

static void
Opcode_sdxp_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x780000;
}

static void
Opcode_abs_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xfa0010;
}

static void
Opcode_neg_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xfa0060;
}

static void
Opcode_abs_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xff0010;
}

static void
Opcode_neg_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xff0060;
}

static void
Opcode_mov_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xfa0000;
}

static void
Opcode_mov_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xff0000;
}

static void
Opcode_moveqz_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8b0000;
}

static void
Opcode_movnez_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9b0000;
}

static void
Opcode_movltz_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xab0000;
}

static void
Opcode_movgez_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xbb0000;
}

static void
Opcode_movf_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xcb0000;
}

static void
Opcode_movt_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xdb0000;
}

static void
Opcode_wfr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xfa0050;
}

static void
Opcode_rfr_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xfa0040;
}

static void
Opcode_rfrd_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xff0040;
}

static void
Opcode_wfrd_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8e0000;
}

static void
Opcode_round_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8a0000;
}

static void
Opcode_round_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x8f0000;
}

static void
Opcode_ceil_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xba0000;
}

static void
Opcode_ceil_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xbf0000;
}

static void
Opcode_floor_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xaa0000;
}

static void
Opcode_floor_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xaf0000;
}

static void
Opcode_trunc_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9a0000;
}

static void
Opcode_trunc_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x9f0000;
}

static void
Opcode_utrunc_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xea0000;
}

static void
Opcode_utrunc_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xef0000;
}

static void
Opcode_float_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xca0000;
}

static void
Opcode_float_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xcf0000;
}

static void
Opcode_ufloat_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xda0000;
}

static void
Opcode_ufloat_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xdf0000;
}

static void
Opcode_cvtd_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xfa0020;
}

static void
Opcode_cvts_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xff0020;
}

static void
Opcode_un_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1b0000;
}

static void
Opcode_un_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1e0000;
}

static void
Opcode_ult_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5b0000;
}

static void
Opcode_ult_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5e0000;
}

static void
Opcode_ule_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7b0000;
}

static void
Opcode_ule_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7e0000;
}

static void
Opcode_ueq_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3b0000;
}

static void
Opcode_ueq_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x3e0000;
}

static void
Opcode_olt_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4b0000;
}

static void
Opcode_olt_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4e0000;
}

static void
Opcode_ole_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6b0000;
}

static void
Opcode_ole_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6e0000;
}

static void
Opcode_oeq_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2b0000;
}

static void
Opcode_oeq_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2e0000;
}

static void
Opcode_add_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xa0000;
}

static void
Opcode_add_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xf0000;
}

static void
Opcode_sub_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1a0000;
}

static void
Opcode_sub_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x1f0000;
}

static void
Opcode_mul_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2a0000;
}

static void
Opcode_mul_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x2f0000;
}

static void
Opcode_madd_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4a0000;
}

static void
Opcode_madd_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x4f0000;
}

static void
Opcode_msub_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5a0000;
}

static void
Opcode_msub_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x5f0000;
}

static void
Opcode_sqrt0_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xfa0090;
}

static void
Opcode_sqrt0_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xff0090;
}

static void
Opcode_div0_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xfa0070;
}

static void
Opcode_div0_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xff0070;
}

static void
Opcode_recip0_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xfa0080;
}

static void
Opcode_recip0_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xff0080;
}

static void
Opcode_rsqrt0_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xfa00a0;
}

static void
Opcode_rsqrt0_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xff00a0;
}

static void
Opcode_maddn_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6a0000;
}

static void
Opcode_maddn_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x6f0000;
}

static void
Opcode_divn_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7a0000;
}

static void
Opcode_divn_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0x7f0000;
}

static void
Opcode_const_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xfa0030;
}

static void
Opcode_const_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xff0030;
}

static void
Opcode_nexp01_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xfa00b0;
}

static void
Opcode_nexp01_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xff00b0;
}

static void
Opcode_addexp_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xfa00e0;
}

static void
Opcode_addexp_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xff00e0;
}

static void
Opcode_addexpm_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xfa00f0;
}

static void
Opcode_addexpm_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xff00f0;
}

static void
Opcode_mkdadj_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xfa00d0;
}

static void
Opcode_mkdadj_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xff00d0;
}

static void
Opcode_mksadj_s_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xfa00c0;
}

static void
Opcode_mksadj_d_Slot_inst_encode (xtensa_insnbuf slotbuf)
{
  slotbuf[0] = 0xff00c0;
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

static xtensa_opcode_encode_fn Opcode_addx2_encode_fns[] = {
  Opcode_addx2_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_addx4_encode_fns[] = {
  Opcode_addx4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_addx8_encode_fns[] = {
  Opcode_addx8_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sub_encode_fns[] = {
  Opcode_sub_Slot_inst_encode, 0, 0
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

static xtensa_opcode_encode_fn Opcode_bgei_encode_fns[] = {
  Opcode_bgei_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_blti_encode_fns[] = {
  Opcode_blti_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bnei_encode_fns[] = {
  Opcode_bnei_Slot_inst_encode, 0, 0
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

static xtensa_opcode_encode_fn Opcode_ball_encode_fns[] = {
  Opcode_ball_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bany_encode_fns[] = {
  Opcode_bany_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bbc_encode_fns[] = {
  Opcode_bbc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bbs_encode_fns[] = {
  Opcode_bbs_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_beq_encode_fns[] = {
  Opcode_beq_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bge_encode_fns[] = {
  Opcode_bge_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bgeu_encode_fns[] = {
  Opcode_bgeu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_blt_encode_fns[] = {
  Opcode_blt_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bltu_encode_fns[] = {
  Opcode_bltu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bnall_encode_fns[] = {
  Opcode_bnall_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bne_encode_fns[] = {
  Opcode_bne_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bnone_encode_fns[] = {
  Opcode_bnone_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_beqz_encode_fns[] = {
  Opcode_beqz_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bgez_encode_fns[] = {
  Opcode_bgez_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bltz_encode_fns[] = {
  Opcode_bltz_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bnez_encode_fns[] = {
  Opcode_bnez_Slot_inst_encode, 0, 0
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

static xtensa_opcode_encode_fn Opcode_loopgtz_encode_fns[] = {
  Opcode_loopgtz_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_loopnez_encode_fns[] = {
  Opcode_loopnez_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movi_encode_fns[] = {
  Opcode_movi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_moveqz_encode_fns[] = {
  Opcode_moveqz_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movgez_encode_fns[] = {
  Opcode_movgez_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movltz_encode_fns[] = {
  Opcode_movltz_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movnez_encode_fns[] = {
  Opcode_movnez_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_abs_encode_fns[] = {
  Opcode_abs_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_neg_encode_fns[] = {
  Opcode_neg_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_nop_encode_fns[] = {
  Opcode_nop_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ret_encode_fns[] = {
  Opcode_ret_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_simcall_encode_fns[] = {
  Opcode_simcall_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_s16i_encode_fns[] = {
  Opcode_s16i_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_s32i_encode_fns[] = {
  Opcode_s32i_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_s32nb_encode_fns[] = {
  Opcode_s32nb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_s8i_encode_fns[] = {
  Opcode_s8i_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ssa8b_encode_fns[] = {
  Opcode_ssa8b_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ssa8l_encode_fns[] = {
  Opcode_ssa8l_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ssl_encode_fns[] = {
  Opcode_ssl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ssr_encode_fns[] = {
  Opcode_ssr_Slot_inst_encode, 0, 0
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

static xtensa_opcode_encode_fn Opcode_sra_encode_fns[] = {
  Opcode_sra_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_srl_encode_fns[] = {
  Opcode_srl_Slot_inst_encode, 0, 0
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

static xtensa_opcode_encode_fn Opcode_dsync_encode_fns[] = {
  Opcode_dsync_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_esync_encode_fns[] = {
  Opcode_esync_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsync_encode_fns[] = {
  Opcode_rsync_Slot_inst_encode, 0, 0
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

static xtensa_opcode_encode_fn Opcode_rsr_memctl_encode_fns[] = {
  Opcode_rsr_memctl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_memctl_encode_fns[] = {
  Opcode_wsr_memctl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_memctl_encode_fns[] = {
  Opcode_xsr_memctl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_configid0_encode_fns[] = {
  Opcode_rsr_configid0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_configid0_encode_fns[] = {
  Opcode_wsr_configid0_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_configid1_encode_fns[] = {
  Opcode_rsr_configid1_Slot_inst_encode, 0, 0
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

static xtensa_opcode_encode_fn Opcode_salt_encode_fns[] = {
  Opcode_salt_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_saltu_encode_fns[] = {
  Opcode_saltu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul16s_encode_fns[] = {
  Opcode_mul16s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul16u_encode_fns[] = {
  Opcode_mul16u_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mull_encode_fns[] = {
  Opcode_mull_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_aa_hh_encode_fns[] = {
  Opcode_mul_aa_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_aa_hl_encode_fns[] = {
  Opcode_mul_aa_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_aa_lh_encode_fns[] = {
  Opcode_mul_aa_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_aa_ll_encode_fns[] = {
  Opcode_mul_aa_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_umul_aa_hh_encode_fns[] = {
  Opcode_umul_aa_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_umul_aa_hl_encode_fns[] = {
  Opcode_umul_aa_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_umul_aa_lh_encode_fns[] = {
  Opcode_umul_aa_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_umul_aa_ll_encode_fns[] = {
  Opcode_umul_aa_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_ad_hh_encode_fns[] = {
  Opcode_mul_ad_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_ad_hl_encode_fns[] = {
  Opcode_mul_ad_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_ad_lh_encode_fns[] = {
  Opcode_mul_ad_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_ad_ll_encode_fns[] = {
  Opcode_mul_ad_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_da_hh_encode_fns[] = {
  Opcode_mul_da_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_da_hl_encode_fns[] = {
  Opcode_mul_da_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_da_lh_encode_fns[] = {
  Opcode_mul_da_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_da_ll_encode_fns[] = {
  Opcode_mul_da_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_dd_hh_encode_fns[] = {
  Opcode_mul_dd_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_dd_hl_encode_fns[] = {
  Opcode_mul_dd_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_dd_lh_encode_fns[] = {
  Opcode_mul_dd_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_dd_ll_encode_fns[] = {
  Opcode_mul_dd_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_aa_hh_encode_fns[] = {
  Opcode_mula_aa_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_aa_hl_encode_fns[] = {
  Opcode_mula_aa_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_aa_lh_encode_fns[] = {
  Opcode_mula_aa_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_aa_ll_encode_fns[] = {
  Opcode_mula_aa_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_aa_hh_encode_fns[] = {
  Opcode_muls_aa_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_aa_hl_encode_fns[] = {
  Opcode_muls_aa_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_aa_lh_encode_fns[] = {
  Opcode_muls_aa_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_aa_ll_encode_fns[] = {
  Opcode_muls_aa_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_ad_hh_encode_fns[] = {
  Opcode_mula_ad_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_ad_hl_encode_fns[] = {
  Opcode_mula_ad_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_ad_lh_encode_fns[] = {
  Opcode_mula_ad_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_ad_ll_encode_fns[] = {
  Opcode_mula_ad_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_ad_hh_encode_fns[] = {
  Opcode_muls_ad_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_ad_hl_encode_fns[] = {
  Opcode_muls_ad_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_ad_lh_encode_fns[] = {
  Opcode_muls_ad_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_ad_ll_encode_fns[] = {
  Opcode_muls_ad_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_hh_encode_fns[] = {
  Opcode_mula_da_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_hl_encode_fns[] = {
  Opcode_mula_da_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_lh_encode_fns[] = {
  Opcode_mula_da_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_ll_encode_fns[] = {
  Opcode_mula_da_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_da_hh_encode_fns[] = {
  Opcode_muls_da_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_da_hl_encode_fns[] = {
  Opcode_muls_da_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_da_lh_encode_fns[] = {
  Opcode_muls_da_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_da_ll_encode_fns[] = {
  Opcode_muls_da_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_hh_encode_fns[] = {
  Opcode_mula_dd_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_hl_encode_fns[] = {
  Opcode_mula_dd_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_lh_encode_fns[] = {
  Opcode_mula_dd_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_ll_encode_fns[] = {
  Opcode_mula_dd_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_dd_hh_encode_fns[] = {
  Opcode_muls_dd_hh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_dd_hl_encode_fns[] = {
  Opcode_muls_dd_hl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_dd_lh_encode_fns[] = {
  Opcode_muls_dd_lh_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_muls_dd_ll_encode_fns[] = {
  Opcode_muls_dd_ll_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_hh_lddec_encode_fns[] = {
  Opcode_mula_da_hh_lddec_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_hh_ldinc_encode_fns[] = {
  Opcode_mula_da_hh_ldinc_Slot_inst_encode, 0, 0
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

static xtensa_opcode_encode_fn Opcode_mula_da_ll_lddec_encode_fns[] = {
  Opcode_mula_da_ll_lddec_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_da_ll_ldinc_encode_fns[] = {
  Opcode_mula_da_ll_ldinc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_hh_lddec_encode_fns[] = {
  Opcode_mula_dd_hh_lddec_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_hh_ldinc_encode_fns[] = {
  Opcode_mula_dd_hh_ldinc_Slot_inst_encode, 0, 0
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

static xtensa_opcode_encode_fn Opcode_mula_dd_ll_lddec_encode_fns[] = {
  Opcode_mula_dd_ll_lddec_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mula_dd_ll_ldinc_encode_fns[] = {
  Opcode_mula_dd_ll_ldinc_Slot_inst_encode, 0, 0
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

static xtensa_opcode_encode_fn Opcode_lddr32_p_encode_fns[] = {
  Opcode_lddr32_p_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sddr32_p_encode_fns[] = {
  Opcode_sddr32_p_Slot_inst_encode, 0, 0
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

static xtensa_opcode_encode_fn Opcode_andb_encode_fns[] = {
  Opcode_andb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_andbc_encode_fns[] = {
  Opcode_andbc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_orb_encode_fns[] = {
  Opcode_orb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_orbc_encode_fns[] = {
  Opcode_orbc_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xorb_encode_fns[] = {
  Opcode_xorb_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_all4_encode_fns[] = {
  Opcode_all4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_any4_encode_fns[] = {
  Opcode_any4_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_all8_encode_fns[] = {
  Opcode_all8_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_any8_encode_fns[] = {
  Opcode_any8_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bf_encode_fns[] = {
  Opcode_bf_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_bt_encode_fns[] = {
  Opcode_bt_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movf_encode_fns[] = {
  Opcode_movf_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movt_encode_fns[] = {
  Opcode_movt_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_br_encode_fns[] = {
  Opcode_rsr_br_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_br_encode_fns[] = {
  Opcode_wsr_br_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_br_encode_fns[] = {
  Opcode_xsr_br_Slot_inst_encode, 0, 0
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

static xtensa_opcode_encode_fn Opcode_ihi_encode_fns[] = {
  Opcode_ihi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ipf_encode_fns[] = {
  Opcode_ipf_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ihu_encode_fns[] = {
  Opcode_ihu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_iiu_encode_fns[] = {
  Opcode_iiu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ipfl_encode_fns[] = {
  Opcode_ipfl_Slot_inst_encode, 0, 0
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

static xtensa_opcode_encode_fn Opcode_diwbui_p_encode_fns[] = {
  Opcode_diwbui_p_Slot_inst_encode, 0, 0
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

static xtensa_opcode_encode_fn Opcode_dpfro_encode_fns[] = {
  Opcode_dpfro_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dpfw_encode_fns[] = {
  Opcode_dpfw_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dpfwo_encode_fns[] = {
  Opcode_dpfwo_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dhu_encode_fns[] = {
  Opcode_dhu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_diu_encode_fns[] = {
  Opcode_diu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_dpfl_encode_fns[] = {
  Opcode_dpfl_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sdct_encode_fns[] = {
  Opcode_sdct_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ldct_encode_fns[] = {
  Opcode_ldct_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sdcw_encode_fns[] = {
  Opcode_sdcw_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ldcw_encode_fns[] = {
  Opcode_ldcw_Slot_inst_encode, 0, 0
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

static xtensa_opcode_encode_fn Opcode_max_encode_fns[] = {
  Opcode_max_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_maxu_encode_fns[] = {
  Opcode_maxu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_min_encode_fns[] = {
  Opcode_min_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_minu_encode_fns[] = {
  Opcode_minu_Slot_inst_encode, 0, 0
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

static xtensa_opcode_encode_fn Opcode_quos_encode_fns[] = {
  Opcode_quos_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_quou_encode_fns[] = {
  Opcode_quou_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rems_encode_fns[] = {
  Opcode_rems_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_remu_encode_fns[] = {
  Opcode_remu_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsr_eraccess_encode_fns[] = {
  Opcode_rsr_eraccess_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wsr_eraccess_encode_fns[] = {
  Opcode_wsr_eraccess_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_xsr_eraccess_encode_fns[] = {
  Opcode_xsr_eraccess_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rer_encode_fns[] = {
  Opcode_rer_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wer_encode_fns[] = {
  Opcode_wer_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rur_fcr_encode_fns[] = {
  Opcode_rur_fcr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wur_fcr_encode_fns[] = {
  Opcode_wur_fcr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rur_fsr_encode_fns[] = {
  Opcode_rur_fsr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wur_fsr_encode_fns[] = {
  Opcode_wur_fsr_Slot_inst_encode, 0, 0
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

static xtensa_opcode_encode_fn Opcode_rur_expstate_encode_fns[] = {
  Opcode_rur_expstate_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wur_expstate_encode_fns[] = {
  Opcode_wur_expstate_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_lsi_encode_fns[] = {
  Opcode_lsi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_lsip_encode_fns[] = {
  Opcode_lsip_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_lsx_encode_fns[] = {
  Opcode_lsx_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_lsxp_encode_fns[] = {
  Opcode_lsxp_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ssi_encode_fns[] = {
  Opcode_ssi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ssip_encode_fns[] = {
  Opcode_ssip_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ssx_encode_fns[] = {
  Opcode_ssx_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ssxp_encode_fns[] = {
  Opcode_ssxp_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ldi_encode_fns[] = {
  Opcode_ldi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ldip_encode_fns[] = {
  Opcode_ldip_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ldx_encode_fns[] = {
  Opcode_ldx_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ldxp_encode_fns[] = {
  Opcode_ldxp_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sdi_encode_fns[] = {
  Opcode_sdi_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sdip_encode_fns[] = {
  Opcode_sdip_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sdx_encode_fns[] = {
  Opcode_sdx_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sdxp_encode_fns[] = {
  Opcode_sdxp_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_abs_s_encode_fns[] = {
  Opcode_abs_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_neg_s_encode_fns[] = {
  Opcode_neg_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_abs_d_encode_fns[] = {
  Opcode_abs_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_neg_d_encode_fns[] = {
  Opcode_neg_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mov_s_encode_fns[] = {
  Opcode_mov_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mov_d_encode_fns[] = {
  Opcode_mov_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_moveqz_s_encode_fns[] = {
  Opcode_moveqz_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movnez_s_encode_fns[] = {
  Opcode_movnez_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movltz_s_encode_fns[] = {
  Opcode_movltz_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movgez_s_encode_fns[] = {
  Opcode_movgez_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movf_s_encode_fns[] = {
  Opcode_movf_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_movt_s_encode_fns[] = {
  Opcode_movt_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wfr_encode_fns[] = {
  Opcode_wfr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rfr_encode_fns[] = {
  Opcode_rfr_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rfrd_encode_fns[] = {
  Opcode_rfrd_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_wfrd_encode_fns[] = {
  Opcode_wfrd_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_round_s_encode_fns[] = {
  Opcode_round_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_round_d_encode_fns[] = {
  Opcode_round_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ceil_s_encode_fns[] = {
  Opcode_ceil_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ceil_d_encode_fns[] = {
  Opcode_ceil_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_floor_s_encode_fns[] = {
  Opcode_floor_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_floor_d_encode_fns[] = {
  Opcode_floor_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_trunc_s_encode_fns[] = {
  Opcode_trunc_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_trunc_d_encode_fns[] = {
  Opcode_trunc_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_utrunc_s_encode_fns[] = {
  Opcode_utrunc_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_utrunc_d_encode_fns[] = {
  Opcode_utrunc_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_float_s_encode_fns[] = {
  Opcode_float_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_float_d_encode_fns[] = {
  Opcode_float_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ufloat_s_encode_fns[] = {
  Opcode_ufloat_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ufloat_d_encode_fns[] = {
  Opcode_ufloat_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_cvtd_s_encode_fns[] = {
  Opcode_cvtd_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_cvts_d_encode_fns[] = {
  Opcode_cvts_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_un_s_encode_fns[] = {
  Opcode_un_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_un_d_encode_fns[] = {
  Opcode_un_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ult_s_encode_fns[] = {
  Opcode_ult_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ult_d_encode_fns[] = {
  Opcode_ult_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ule_s_encode_fns[] = {
  Opcode_ule_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ule_d_encode_fns[] = {
  Opcode_ule_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ueq_s_encode_fns[] = {
  Opcode_ueq_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ueq_d_encode_fns[] = {
  Opcode_ueq_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_olt_s_encode_fns[] = {
  Opcode_olt_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_olt_d_encode_fns[] = {
  Opcode_olt_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ole_s_encode_fns[] = {
  Opcode_ole_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_ole_d_encode_fns[] = {
  Opcode_ole_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_oeq_s_encode_fns[] = {
  Opcode_oeq_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_oeq_d_encode_fns[] = {
  Opcode_oeq_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_add_s_encode_fns[] = {
  Opcode_add_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_add_d_encode_fns[] = {
  Opcode_add_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sub_s_encode_fns[] = {
  Opcode_sub_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sub_d_encode_fns[] = {
  Opcode_sub_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_s_encode_fns[] = {
  Opcode_mul_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mul_d_encode_fns[] = {
  Opcode_mul_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_madd_s_encode_fns[] = {
  Opcode_madd_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_madd_d_encode_fns[] = {
  Opcode_madd_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_msub_s_encode_fns[] = {
  Opcode_msub_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_msub_d_encode_fns[] = {
  Opcode_msub_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sqrt0_s_encode_fns[] = {
  Opcode_sqrt0_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_sqrt0_d_encode_fns[] = {
  Opcode_sqrt0_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_div0_s_encode_fns[] = {
  Opcode_div0_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_div0_d_encode_fns[] = {
  Opcode_div0_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_recip0_s_encode_fns[] = {
  Opcode_recip0_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_recip0_d_encode_fns[] = {
  Opcode_recip0_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsqrt0_s_encode_fns[] = {
  Opcode_rsqrt0_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_rsqrt0_d_encode_fns[] = {
  Opcode_rsqrt0_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_maddn_s_encode_fns[] = {
  Opcode_maddn_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_maddn_d_encode_fns[] = {
  Opcode_maddn_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_divn_s_encode_fns[] = {
  Opcode_divn_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_divn_d_encode_fns[] = {
  Opcode_divn_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_const_s_encode_fns[] = {
  Opcode_const_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_const_d_encode_fns[] = {
  Opcode_const_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_nexp01_s_encode_fns[] = {
  Opcode_nexp01_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_nexp01_d_encode_fns[] = {
  Opcode_nexp01_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_addexp_s_encode_fns[] = {
  Opcode_addexp_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_addexp_d_encode_fns[] = {
  Opcode_addexp_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_addexpm_s_encode_fns[] = {
  Opcode_addexpm_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_addexpm_d_encode_fns[] = {
  Opcode_addexpm_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mkdadj_s_encode_fns[] = {
  Opcode_mkdadj_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mkdadj_d_encode_fns[] = {
  Opcode_mkdadj_d_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mksadj_s_encode_fns[] = {
  Opcode_mksadj_s_Slot_inst_encode, 0, 0
};

static xtensa_opcode_encode_fn Opcode_mksadj_d_encode_fns[] = {
  Opcode_mksadj_d_Slot_inst_encode, 0, 0
};





/* Opcode table.  */

static xtensa_funcUnit_use Opcode_l32e_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_s32e_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_l32i_n_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_s32i_n_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_l16ui_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_l16si_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_l32i_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_l32r_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_l8ui_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_s16i_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_s32i_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_s32nb_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_s8i_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_mula_da_hh_lddec_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_mula_da_hh_ldinc_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_mula_da_hl_lddec_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_mula_da_hl_ldinc_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_mula_da_lh_lddec_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_mula_da_lh_ldinc_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_mula_da_ll_lddec_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_mula_da_ll_ldinc_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_mula_dd_hh_lddec_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_mula_dd_hh_ldinc_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_mula_dd_hl_lddec_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_mula_dd_hl_ldinc_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_mula_dd_lh_lddec_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_mula_dd_lh_ldinc_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_mula_dd_ll_lddec_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_mula_dd_ll_ldinc_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_lddec_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_ldinc_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_lddr32_p_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_sddr32_p_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_lict_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_licw_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_sict_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_sicw_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_sdct_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_ldct_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_sdcw_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_ldcw_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_ldpte_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_l32ai_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_s32ri_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_s32c1i_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_lsi_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_lsip_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_lsx_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_lsxp_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_ssi_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_ssip_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_ssx_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_ssxp_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_ldi_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_ldip_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_ldx_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_ldxp_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_sdi_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_sdip_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_sdx_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

static xtensa_funcUnit_use Opcode_sdxp_funcUnit_uses[] = {
  { FUNCUNIT_XT_LOADSTORE_UNIT, 0 }
};

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
    Opcode_l32e_encode_fns, 1, Opcode_l32e_funcUnit_uses },
  { "s32e", ICLASS_xt_iclass_s32e,
    0,
    Opcode_s32e_encode_fns, 1, Opcode_s32e_funcUnit_uses },
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
    Opcode_l32i_n_encode_fns, 1, Opcode_l32i_n_funcUnit_uses },
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
    Opcode_s32i_n_encode_fns, 1, Opcode_s32i_n_funcUnit_uses },
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
  { "addx2", ICLASS_xt_iclass_addsub,
    0,
    Opcode_addx2_encode_fns, 0, 0 },
  { "addx4", ICLASS_xt_iclass_addsub,
    0,
    Opcode_addx4_encode_fns, 0, 0 },
  { "addx8", ICLASS_xt_iclass_addsub,
    0,
    Opcode_addx8_encode_fns, 0, 0 },
  { "sub", ICLASS_xt_iclass_addsub,
    0,
    Opcode_sub_encode_fns, 0, 0 },
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
  { "bgei", ICLASS_xt_iclass_bsi8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bgei_encode_fns, 0, 0 },
  { "blti", ICLASS_xt_iclass_bsi8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_blti_encode_fns, 0, 0 },
  { "bnei", ICLASS_xt_iclass_bsi8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bnei_encode_fns, 0, 0 },
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
  { "ball", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_ball_encode_fns, 0, 0 },
  { "bany", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bany_encode_fns, 0, 0 },
  { "bbc", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bbc_encode_fns, 0, 0 },
  { "bbs", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bbs_encode_fns, 0, 0 },
  { "beq", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_beq_encode_fns, 0, 0 },
  { "bge", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bge_encode_fns, 0, 0 },
  { "bgeu", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bgeu_encode_fns, 0, 0 },
  { "blt", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_blt_encode_fns, 0, 0 },
  { "bltu", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bltu_encode_fns, 0, 0 },
  { "bnall", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bnall_encode_fns, 0, 0 },
  { "bne", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bne_encode_fns, 0, 0 },
  { "bnone", ICLASS_xt_iclass_bst8,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bnone_encode_fns, 0, 0 },
  { "beqz", ICLASS_xt_iclass_bsz12,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_beqz_encode_fns, 0, 0 },
  { "bgez", ICLASS_xt_iclass_bsz12,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bgez_encode_fns, 0, 0 },
  { "bltz", ICLASS_xt_iclass_bsz12,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bltz_encode_fns, 0, 0 },
  { "bnez", ICLASS_xt_iclass_bsz12,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bnez_encode_fns, 0, 0 },
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
    Opcode_l16ui_encode_fns, 1, Opcode_l16ui_funcUnit_uses },
  { "l16si", ICLASS_xt_iclass_l16si,
    0,
    Opcode_l16si_encode_fns, 1, Opcode_l16si_funcUnit_uses },
  { "l32i", ICLASS_xt_iclass_l32i,
    0,
    Opcode_l32i_encode_fns, 1, Opcode_l32i_funcUnit_uses },
  { "l32r", ICLASS_xt_iclass_l32r,
    0,
    Opcode_l32r_encode_fns, 1, Opcode_l32r_funcUnit_uses },
  { "l8ui", ICLASS_xt_iclass_l8i,
    0,
    Opcode_l8ui_encode_fns, 1, Opcode_l8ui_funcUnit_uses },
  { "loop", ICLASS_xt_iclass_loop,
    XTENSA_OPCODE_IS_LOOP,
    Opcode_loop_encode_fns, 0, 0 },
  { "loopgtz", ICLASS_xt_iclass_loopz,
    XTENSA_OPCODE_IS_LOOP,
    Opcode_loopgtz_encode_fns, 0, 0 },
  { "loopnez", ICLASS_xt_iclass_loopz,
    XTENSA_OPCODE_IS_LOOP,
    Opcode_loopnez_encode_fns, 0, 0 },
  { "movi", ICLASS_xt_iclass_movi,
    0,
    Opcode_movi_encode_fns, 0, 0 },
  { "moveqz", ICLASS_xt_iclass_movz,
    0,
    Opcode_moveqz_encode_fns, 0, 0 },
  { "movgez", ICLASS_xt_iclass_movz,
    0,
    Opcode_movgez_encode_fns, 0, 0 },
  { "movltz", ICLASS_xt_iclass_movz,
    0,
    Opcode_movltz_encode_fns, 0, 0 },
  { "movnez", ICLASS_xt_iclass_movz,
    0,
    Opcode_movnez_encode_fns, 0, 0 },
  { "abs", ICLASS_xt_iclass_neg,
    0,
    Opcode_abs_encode_fns, 0, 0 },
  { "neg", ICLASS_xt_iclass_neg,
    0,
    Opcode_neg_encode_fns, 0, 0 },
  { "nop", ICLASS_xt_iclass_nop,
    0,
    Opcode_nop_encode_fns, 0, 0 },
  { "ret", ICLASS_xt_iclass_return,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_ret_encode_fns, 0, 0 },
  { "simcall", ICLASS_xt_iclass_simcall,
    0,
    Opcode_simcall_encode_fns, 0, 0 },
  { "s16i", ICLASS_xt_iclass_s16i,
    0,
    Opcode_s16i_encode_fns, 1, Opcode_s16i_funcUnit_uses },
  { "s32i", ICLASS_xt_iclass_s32i,
    0,
    Opcode_s32i_encode_fns, 1, Opcode_s32i_funcUnit_uses },
  { "s32nb", ICLASS_xt_iclass_s32nb,
    0,
    Opcode_s32nb_encode_fns, 1, Opcode_s32nb_funcUnit_uses },
  { "s8i", ICLASS_xt_iclass_s8i,
    0,
    Opcode_s8i_encode_fns, 1, Opcode_s8i_funcUnit_uses },
  { "ssa8b", ICLASS_xt_iclass_sar,
    0,
    Opcode_ssa8b_encode_fns, 0, 0 },
  { "ssa8l", ICLASS_xt_iclass_sar,
    0,
    Opcode_ssa8l_encode_fns, 0, 0 },
  { "ssl", ICLASS_xt_iclass_sar,
    0,
    Opcode_ssl_encode_fns, 0, 0 },
  { "ssr", ICLASS_xt_iclass_sar,
    0,
    Opcode_ssr_encode_fns, 0, 0 },
  { "ssai", ICLASS_xt_iclass_sari,
    0,
    Opcode_ssai_encode_fns, 0, 0 },
  { "sll", ICLASS_xt_iclass_shifts,
    0,
    Opcode_sll_encode_fns, 0, 0 },
  { "src", ICLASS_xt_iclass_shiftst,
    0,
    Opcode_src_encode_fns, 0, 0 },
  { "sra", ICLASS_xt_iclass_shiftt,
    0,
    Opcode_sra_encode_fns, 0, 0 },
  { "srl", ICLASS_xt_iclass_shiftt,
    0,
    Opcode_srl_encode_fns, 0, 0 },
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
  { "dsync", ICLASS_xt_iclass_sync,
    0,
    Opcode_dsync_encode_fns, 0, 0 },
  { "esync", ICLASS_xt_iclass_sync,
    0,
    Opcode_esync_encode_fns, 0, 0 },
  { "rsync", ICLASS_xt_iclass_sync,
    0,
    Opcode_rsync_encode_fns, 0, 0 },
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
  { "rsr.memctl", ICLASS_xt_iclass_rsr_memctl,
    0,
    Opcode_rsr_memctl_encode_fns, 0, 0 },
  { "wsr.memctl", ICLASS_xt_iclass_wsr_memctl,
    0,
    Opcode_wsr_memctl_encode_fns, 0, 0 },
  { "xsr.memctl", ICLASS_xt_iclass_xsr_memctl,
    0,
    Opcode_xsr_memctl_encode_fns, 0, 0 },
  { "rsr.configid0", ICLASS_xt_iclass_rsr_configid0,
    0,
    Opcode_rsr_configid0_encode_fns, 0, 0 },
  { "wsr.configid0", ICLASS_xt_iclass_wsr_configid0,
    0,
    Opcode_wsr_configid0_encode_fns, 0, 0 },
  { "rsr.configid1", ICLASS_xt_iclass_rsr_configid1,
    0,
    Opcode_rsr_configid1_encode_fns, 0, 0 },
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
  { "salt", ICLASS_xt_iclass_salt,
    0,
    Opcode_salt_encode_fns, 0, 0 },
  { "saltu", ICLASS_xt_iclass_salt,
    0,
    Opcode_saltu_encode_fns, 0, 0 },
  { "mul16s", ICLASS_xt_mul16,
    0,
    Opcode_mul16s_encode_fns, 0, 0 },
  { "mul16u", ICLASS_xt_mul16,
    0,
    Opcode_mul16u_encode_fns, 0, 0 },
  { "mull", ICLASS_xt_mul32,
    0,
    Opcode_mull_encode_fns, 0, 0 },
  { "mul.aa.hh", ICLASS_xt_iclass_mac16_aa,
    0,
    Opcode_mul_aa_hh_encode_fns, 0, 0 },
  { "mul.aa.hl", ICLASS_xt_iclass_mac16_aa,
    0,
    Opcode_mul_aa_hl_encode_fns, 0, 0 },
  { "mul.aa.lh", ICLASS_xt_iclass_mac16_aa,
    0,
    Opcode_mul_aa_lh_encode_fns, 0, 0 },
  { "mul.aa.ll", ICLASS_xt_iclass_mac16_aa,
    0,
    Opcode_mul_aa_ll_encode_fns, 0, 0 },
  { "umul.aa.hh", ICLASS_xt_iclass_mac16_aa,
    0,
    Opcode_umul_aa_hh_encode_fns, 0, 0 },
  { "umul.aa.hl", ICLASS_xt_iclass_mac16_aa,
    0,
    Opcode_umul_aa_hl_encode_fns, 0, 0 },
  { "umul.aa.lh", ICLASS_xt_iclass_mac16_aa,
    0,
    Opcode_umul_aa_lh_encode_fns, 0, 0 },
  { "umul.aa.ll", ICLASS_xt_iclass_mac16_aa,
    0,
    Opcode_umul_aa_ll_encode_fns, 0, 0 },
  { "mul.ad.hh", ICLASS_xt_iclass_mac16_ad,
    0,
    Opcode_mul_ad_hh_encode_fns, 0, 0 },
  { "mul.ad.hl", ICLASS_xt_iclass_mac16_ad,
    0,
    Opcode_mul_ad_hl_encode_fns, 0, 0 },
  { "mul.ad.lh", ICLASS_xt_iclass_mac16_ad,
    0,
    Opcode_mul_ad_lh_encode_fns, 0, 0 },
  { "mul.ad.ll", ICLASS_xt_iclass_mac16_ad,
    0,
    Opcode_mul_ad_ll_encode_fns, 0, 0 },
  { "mul.da.hh", ICLASS_xt_iclass_mac16_da,
    0,
    Opcode_mul_da_hh_encode_fns, 0, 0 },
  { "mul.da.hl", ICLASS_xt_iclass_mac16_da,
    0,
    Opcode_mul_da_hl_encode_fns, 0, 0 },
  { "mul.da.lh", ICLASS_xt_iclass_mac16_da,
    0,
    Opcode_mul_da_lh_encode_fns, 0, 0 },
  { "mul.da.ll", ICLASS_xt_iclass_mac16_da,
    0,
    Opcode_mul_da_ll_encode_fns, 0, 0 },
  { "mul.dd.hh", ICLASS_xt_iclass_mac16_dd,
    0,
    Opcode_mul_dd_hh_encode_fns, 0, 0 },
  { "mul.dd.hl", ICLASS_xt_iclass_mac16_dd,
    0,
    Opcode_mul_dd_hl_encode_fns, 0, 0 },
  { "mul.dd.lh", ICLASS_xt_iclass_mac16_dd,
    0,
    Opcode_mul_dd_lh_encode_fns, 0, 0 },
  { "mul.dd.ll", ICLASS_xt_iclass_mac16_dd,
    0,
    Opcode_mul_dd_ll_encode_fns, 0, 0 },
  { "mula.aa.hh", ICLASS_xt_iclass_mac16a_aa,
    0,
    Opcode_mula_aa_hh_encode_fns, 0, 0 },
  { "mula.aa.hl", ICLASS_xt_iclass_mac16a_aa,
    0,
    Opcode_mula_aa_hl_encode_fns, 0, 0 },
  { "mula.aa.lh", ICLASS_xt_iclass_mac16a_aa,
    0,
    Opcode_mula_aa_lh_encode_fns, 0, 0 },
  { "mula.aa.ll", ICLASS_xt_iclass_mac16a_aa,
    0,
    Opcode_mula_aa_ll_encode_fns, 0, 0 },
  { "muls.aa.hh", ICLASS_xt_iclass_mac16a_aa,
    0,
    Opcode_muls_aa_hh_encode_fns, 0, 0 },
  { "muls.aa.hl", ICLASS_xt_iclass_mac16a_aa,
    0,
    Opcode_muls_aa_hl_encode_fns, 0, 0 },
  { "muls.aa.lh", ICLASS_xt_iclass_mac16a_aa,
    0,
    Opcode_muls_aa_lh_encode_fns, 0, 0 },
  { "muls.aa.ll", ICLASS_xt_iclass_mac16a_aa,
    0,
    Opcode_muls_aa_ll_encode_fns, 0, 0 },
  { "mula.ad.hh", ICLASS_xt_iclass_mac16a_ad,
    0,
    Opcode_mula_ad_hh_encode_fns, 0, 0 },
  { "mula.ad.hl", ICLASS_xt_iclass_mac16a_ad,
    0,
    Opcode_mula_ad_hl_encode_fns, 0, 0 },
  { "mula.ad.lh", ICLASS_xt_iclass_mac16a_ad,
    0,
    Opcode_mula_ad_lh_encode_fns, 0, 0 },
  { "mula.ad.ll", ICLASS_xt_iclass_mac16a_ad,
    0,
    Opcode_mula_ad_ll_encode_fns, 0, 0 },
  { "muls.ad.hh", ICLASS_xt_iclass_mac16a_ad,
    0,
    Opcode_muls_ad_hh_encode_fns, 0, 0 },
  { "muls.ad.hl", ICLASS_xt_iclass_mac16a_ad,
    0,
    Opcode_muls_ad_hl_encode_fns, 0, 0 },
  { "muls.ad.lh", ICLASS_xt_iclass_mac16a_ad,
    0,
    Opcode_muls_ad_lh_encode_fns, 0, 0 },
  { "muls.ad.ll", ICLASS_xt_iclass_mac16a_ad,
    0,
    Opcode_muls_ad_ll_encode_fns, 0, 0 },
  { "mula.da.hh", ICLASS_xt_iclass_mac16a_da,
    0,
    Opcode_mula_da_hh_encode_fns, 0, 0 },
  { "mula.da.hl", ICLASS_xt_iclass_mac16a_da,
    0,
    Opcode_mula_da_hl_encode_fns, 0, 0 },
  { "mula.da.lh", ICLASS_xt_iclass_mac16a_da,
    0,
    Opcode_mula_da_lh_encode_fns, 0, 0 },
  { "mula.da.ll", ICLASS_xt_iclass_mac16a_da,
    0,
    Opcode_mula_da_ll_encode_fns, 0, 0 },
  { "muls.da.hh", ICLASS_xt_iclass_mac16a_da,
    0,
    Opcode_muls_da_hh_encode_fns, 0, 0 },
  { "muls.da.hl", ICLASS_xt_iclass_mac16a_da,
    0,
    Opcode_muls_da_hl_encode_fns, 0, 0 },
  { "muls.da.lh", ICLASS_xt_iclass_mac16a_da,
    0,
    Opcode_muls_da_lh_encode_fns, 0, 0 },
  { "muls.da.ll", ICLASS_xt_iclass_mac16a_da,
    0,
    Opcode_muls_da_ll_encode_fns, 0, 0 },
  { "mula.dd.hh", ICLASS_xt_iclass_mac16a_dd,
    0,
    Opcode_mula_dd_hh_encode_fns, 0, 0 },
  { "mula.dd.hl", ICLASS_xt_iclass_mac16a_dd,
    0,
    Opcode_mula_dd_hl_encode_fns, 0, 0 },
  { "mula.dd.lh", ICLASS_xt_iclass_mac16a_dd,
    0,
    Opcode_mula_dd_lh_encode_fns, 0, 0 },
  { "mula.dd.ll", ICLASS_xt_iclass_mac16a_dd,
    0,
    Opcode_mula_dd_ll_encode_fns, 0, 0 },
  { "muls.dd.hh", ICLASS_xt_iclass_mac16a_dd,
    0,
    Opcode_muls_dd_hh_encode_fns, 0, 0 },
  { "muls.dd.hl", ICLASS_xt_iclass_mac16a_dd,
    0,
    Opcode_muls_dd_hl_encode_fns, 0, 0 },
  { "muls.dd.lh", ICLASS_xt_iclass_mac16a_dd,
    0,
    Opcode_muls_dd_lh_encode_fns, 0, 0 },
  { "muls.dd.ll", ICLASS_xt_iclass_mac16a_dd,
    0,
    Opcode_muls_dd_ll_encode_fns, 0, 0 },
  { "mula.da.hh.lddec", ICLASS_xt_iclass_mac16al_da,
    0,
    Opcode_mula_da_hh_lddec_encode_fns, 1, Opcode_mula_da_hh_lddec_funcUnit_uses },
  { "mula.da.hh.ldinc", ICLASS_xt_iclass_mac16al_da,
    0,
    Opcode_mula_da_hh_ldinc_encode_fns, 1, Opcode_mula_da_hh_ldinc_funcUnit_uses },
  { "mula.da.hl.lddec", ICLASS_xt_iclass_mac16al_da,
    0,
    Opcode_mula_da_hl_lddec_encode_fns, 1, Opcode_mula_da_hl_lddec_funcUnit_uses },
  { "mula.da.hl.ldinc", ICLASS_xt_iclass_mac16al_da,
    0,
    Opcode_mula_da_hl_ldinc_encode_fns, 1, Opcode_mula_da_hl_ldinc_funcUnit_uses },
  { "mula.da.lh.lddec", ICLASS_xt_iclass_mac16al_da,
    0,
    Opcode_mula_da_lh_lddec_encode_fns, 1, Opcode_mula_da_lh_lddec_funcUnit_uses },
  { "mula.da.lh.ldinc", ICLASS_xt_iclass_mac16al_da,
    0,
    Opcode_mula_da_lh_ldinc_encode_fns, 1, Opcode_mula_da_lh_ldinc_funcUnit_uses },
  { "mula.da.ll.lddec", ICLASS_xt_iclass_mac16al_da,
    0,
    Opcode_mula_da_ll_lddec_encode_fns, 1, Opcode_mula_da_ll_lddec_funcUnit_uses },
  { "mula.da.ll.ldinc", ICLASS_xt_iclass_mac16al_da,
    0,
    Opcode_mula_da_ll_ldinc_encode_fns, 1, Opcode_mula_da_ll_ldinc_funcUnit_uses },
  { "mula.dd.hh.lddec", ICLASS_xt_iclass_mac16al_dd,
    0,
    Opcode_mula_dd_hh_lddec_encode_fns, 1, Opcode_mula_dd_hh_lddec_funcUnit_uses },
  { "mula.dd.hh.ldinc", ICLASS_xt_iclass_mac16al_dd,
    0,
    Opcode_mula_dd_hh_ldinc_encode_fns, 1, Opcode_mula_dd_hh_ldinc_funcUnit_uses },
  { "mula.dd.hl.lddec", ICLASS_xt_iclass_mac16al_dd,
    0,
    Opcode_mula_dd_hl_lddec_encode_fns, 1, Opcode_mula_dd_hl_lddec_funcUnit_uses },
  { "mula.dd.hl.ldinc", ICLASS_xt_iclass_mac16al_dd,
    0,
    Opcode_mula_dd_hl_ldinc_encode_fns, 1, Opcode_mula_dd_hl_ldinc_funcUnit_uses },
  { "mula.dd.lh.lddec", ICLASS_xt_iclass_mac16al_dd,
    0,
    Opcode_mula_dd_lh_lddec_encode_fns, 1, Opcode_mula_dd_lh_lddec_funcUnit_uses },
  { "mula.dd.lh.ldinc", ICLASS_xt_iclass_mac16al_dd,
    0,
    Opcode_mula_dd_lh_ldinc_encode_fns, 1, Opcode_mula_dd_lh_ldinc_funcUnit_uses },
  { "mula.dd.ll.lddec", ICLASS_xt_iclass_mac16al_dd,
    0,
    Opcode_mula_dd_ll_lddec_encode_fns, 1, Opcode_mula_dd_ll_lddec_funcUnit_uses },
  { "mula.dd.ll.ldinc", ICLASS_xt_iclass_mac16al_dd,
    0,
    Opcode_mula_dd_ll_ldinc_encode_fns, 1, Opcode_mula_dd_ll_ldinc_funcUnit_uses },
  { "lddec", ICLASS_xt_iclass_mac16_l,
    0,
    Opcode_lddec_encode_fns, 1, Opcode_lddec_funcUnit_uses },
  { "ldinc", ICLASS_xt_iclass_mac16_l,
    0,
    Opcode_ldinc_encode_fns, 1, Opcode_ldinc_funcUnit_uses },
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
  { "lddr32.p", ICLASS_xt_iclass_lddr32_p,
    0,
    Opcode_lddr32_p_encode_fns, 1, Opcode_lddr32_p_funcUnit_uses },
  { "sddr32.p", ICLASS_xt_iclass_sddr32_p,
    0,
    Opcode_sddr32_p_encode_fns, 1, Opcode_sddr32_p_funcUnit_uses },
  { "rfdo", ICLASS_xt_iclass_rfdo,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_rfdo_encode_fns, 0, 0 },
  { "rfdd", ICLASS_xt_iclass_rfdd,
    XTENSA_OPCODE_IS_JUMP,
    Opcode_rfdd_encode_fns, 0, 0 },
  { "wsr.mmid", ICLASS_xt_iclass_wsr_mmid,
    0,
    Opcode_wsr_mmid_encode_fns, 0, 0 },
  { "andb", ICLASS_xt_iclass_bbool1,
    0,
    Opcode_andb_encode_fns, 0, 0 },
  { "andbc", ICLASS_xt_iclass_bbool1,
    0,
    Opcode_andbc_encode_fns, 0, 0 },
  { "orb", ICLASS_xt_iclass_bbool1,
    0,
    Opcode_orb_encode_fns, 0, 0 },
  { "orbc", ICLASS_xt_iclass_bbool1,
    0,
    Opcode_orbc_encode_fns, 0, 0 },
  { "xorb", ICLASS_xt_iclass_bbool1,
    0,
    Opcode_xorb_encode_fns, 0, 0 },
  { "all4", ICLASS_xt_iclass_bbool4,
    0,
    Opcode_all4_encode_fns, 0, 0 },
  { "any4", ICLASS_xt_iclass_bbool4,
    0,
    Opcode_any4_encode_fns, 0, 0 },
  { "all8", ICLASS_xt_iclass_bbool8,
    0,
    Opcode_all8_encode_fns, 0, 0 },
  { "any8", ICLASS_xt_iclass_bbool8,
    0,
    Opcode_any8_encode_fns, 0, 0 },
  { "bf", ICLASS_xt_iclass_bbranch,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bf_encode_fns, 0, 0 },
  { "bt", ICLASS_xt_iclass_bbranch,
    XTENSA_OPCODE_IS_BRANCH,
    Opcode_bt_encode_fns, 0, 0 },
  { "movf", ICLASS_xt_iclass_bmove,
    0,
    Opcode_movf_encode_fns, 0, 0 },
  { "movt", ICLASS_xt_iclass_bmove,
    0,
    Opcode_movt_encode_fns, 0, 0 },
  { "rsr.br", ICLASS_xt_iclass_RSR_BR,
    0,
    Opcode_rsr_br_encode_fns, 0, 0 },
  { "wsr.br", ICLASS_xt_iclass_WSR_BR,
    0,
    Opcode_wsr_br_encode_fns, 0, 0 },
  { "xsr.br", ICLASS_xt_iclass_XSR_BR,
    0,
    Opcode_xsr_br_encode_fns, 0, 0 },
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
  { "ihi", ICLASS_xt_iclass_icache,
    0,
    Opcode_ihi_encode_fns, 0, 0 },
  { "ipf", ICLASS_xt_iclass_icache,
    0,
    Opcode_ipf_encode_fns, 0, 0 },
  { "ihu", ICLASS_xt_iclass_icache_lock,
    0,
    Opcode_ihu_encode_fns, 0, 0 },
  { "iiu", ICLASS_xt_iclass_icache_lock,
    0,
    Opcode_iiu_encode_fns, 0, 0 },
  { "ipfl", ICLASS_xt_iclass_icache_lock,
    0,
    Opcode_ipfl_encode_fns, 0, 0 },
  { "iii", ICLASS_xt_iclass_icache_inv,
    0,
    Opcode_iii_encode_fns, 0, 0 },
  { "lict", ICLASS_xt_iclass_licx,
    0,
    Opcode_lict_encode_fns, 1, Opcode_lict_funcUnit_uses },
  { "licw", ICLASS_xt_iclass_licx,
    0,
    Opcode_licw_encode_fns, 1, Opcode_licw_funcUnit_uses },
  { "sict", ICLASS_xt_iclass_sicx,
    0,
    Opcode_sict_encode_fns, 1, Opcode_sict_funcUnit_uses },
  { "sicw", ICLASS_xt_iclass_sicx,
    0,
    Opcode_sicw_encode_fns, 1, Opcode_sicw_funcUnit_uses },
  { "dhwb", ICLASS_xt_iclass_dcache,
    0,
    Opcode_dhwb_encode_fns, 0, 0 },
  { "dhwbi", ICLASS_xt_iclass_dcache,
    0,
    Opcode_dhwbi_encode_fns, 0, 0 },
  { "diwbui.p", ICLASS_xt_iclass_dcache_dyn,
    0,
    Opcode_diwbui_p_encode_fns, 0, 0 },
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
  { "dpfro", ICLASS_xt_iclass_dpf,
    0,
    Opcode_dpfro_encode_fns, 0, 0 },
  { "dpfw", ICLASS_xt_iclass_dpf,
    0,
    Opcode_dpfw_encode_fns, 0, 0 },
  { "dpfwo", ICLASS_xt_iclass_dpf,
    0,
    Opcode_dpfwo_encode_fns, 0, 0 },
  { "dhu", ICLASS_xt_iclass_dcache_lock,
    0,
    Opcode_dhu_encode_fns, 0, 0 },
  { "diu", ICLASS_xt_iclass_dcache_lock,
    0,
    Opcode_diu_encode_fns, 0, 0 },
  { "dpfl", ICLASS_xt_iclass_dcache_lock,
    0,
    Opcode_dpfl_encode_fns, 0, 0 },
  { "sdct", ICLASS_xt_iclass_sdct,
    0,
    Opcode_sdct_encode_fns, 1, Opcode_sdct_funcUnit_uses },
  { "ldct", ICLASS_xt_iclass_ldct,
    0,
    Opcode_ldct_encode_fns, 1, Opcode_ldct_funcUnit_uses },
  { "sdcw", ICLASS_xt_iclass_sdcw,
    0,
    Opcode_sdcw_encode_fns, 1, Opcode_sdcw_funcUnit_uses },
  { "ldcw", ICLASS_xt_iclass_ldcw,
    0,
    Opcode_ldcw_encode_fns, 1, Opcode_ldcw_funcUnit_uses },
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
    Opcode_ldpte_encode_fns, 1, Opcode_ldpte_funcUnit_uses },
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
  { "max", ICLASS_xt_iclass_minmax,
    0,
    Opcode_max_encode_fns, 0, 0 },
  { "maxu", ICLASS_xt_iclass_minmax,
    0,
    Opcode_maxu_encode_fns, 0, 0 },
  { "min", ICLASS_xt_iclass_minmax,
    0,
    Opcode_min_encode_fns, 0, 0 },
  { "minu", ICLASS_xt_iclass_minmax,
    0,
    Opcode_minu_encode_fns, 0, 0 },
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
    Opcode_l32ai_encode_fns, 1, Opcode_l32ai_funcUnit_uses },
  { "s32ri", ICLASS_xt_iclass_s32ri,
    0,
    Opcode_s32ri_encode_fns, 1, Opcode_s32ri_funcUnit_uses },
  { "s32c1i", ICLASS_xt_iclass_s32c1i,
    0,
    Opcode_s32c1i_encode_fns, 1, Opcode_s32c1i_funcUnit_uses },
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
  { "quos", ICLASS_xt_iclass_div,
    0,
    Opcode_quos_encode_fns, 0, 0 },
  { "quou", ICLASS_xt_iclass_div,
    0,
    Opcode_quou_encode_fns, 0, 0 },
  { "rems", ICLASS_xt_iclass_div,
    0,
    Opcode_rems_encode_fns, 0, 0 },
  { "remu", ICLASS_xt_iclass_div,
    0,
    Opcode_remu_encode_fns, 0, 0 },
  { "rsr.eraccess", ICLASS_xt_iclass_rsr_eraccess,
    0,
    Opcode_rsr_eraccess_encode_fns, 0, 0 },
  { "wsr.eraccess", ICLASS_xt_iclass_wsr_eraccess,
    0,
    Opcode_wsr_eraccess_encode_fns, 0, 0 },
  { "xsr.eraccess", ICLASS_xt_iclass_xsr_eraccess,
    0,
    Opcode_xsr_eraccess_encode_fns, 0, 0 },
  { "rer", ICLASS_xt_iclass_rer,
    0,
    Opcode_rer_encode_fns, 0, 0 },
  { "wer", ICLASS_xt_iclass_wer,
    0,
    Opcode_wer_encode_fns, 0, 0 },
  { "rur.fcr", ICLASS_rur_fcr,
    0,
    Opcode_rur_fcr_encode_fns, 0, 0 },
  { "wur.fcr", ICLASS_wur_fcr,
    0,
    Opcode_wur_fcr_encode_fns, 0, 0 },
  { "rur.fsr", ICLASS_rur_fsr,
    0,
    Opcode_rur_fsr_encode_fns, 0, 0 },
  { "wur.fsr", ICLASS_wur_fsr,
    0,
    Opcode_wur_fsr_encode_fns, 0, 0 },
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
    Opcode_wrmsk_expstate_encode_fns, 0, 0 },
  { "rur.expstate", ICLASS_rur_expstate,
    0,
    Opcode_rur_expstate_encode_fns, 0, 0 },
  { "wur.expstate", ICLASS_wur_expstate,
    0,
    Opcode_wur_expstate_encode_fns, 0, 0 },
  { "lsi", ICLASS_LSI,
    0,
    Opcode_lsi_encode_fns, 1, Opcode_lsi_funcUnit_uses },
  { "lsip", ICLASS_LSIP,
    0,
    Opcode_lsip_encode_fns, 1, Opcode_lsip_funcUnit_uses },
  { "lsx", ICLASS_LSX,
    0,
    Opcode_lsx_encode_fns, 1, Opcode_lsx_funcUnit_uses },
  { "lsxp", ICLASS_LSXP,
    0,
    Opcode_lsxp_encode_fns, 1, Opcode_lsxp_funcUnit_uses },
  { "ssi", ICLASS_SSI,
    0,
    Opcode_ssi_encode_fns, 1, Opcode_ssi_funcUnit_uses },
  { "ssip", ICLASS_SSIP,
    0,
    Opcode_ssip_encode_fns, 1, Opcode_ssip_funcUnit_uses },
  { "ssx", ICLASS_SSX,
    0,
    Opcode_ssx_encode_fns, 1, Opcode_ssx_funcUnit_uses },
  { "ssxp", ICLASS_SSXP,
    0,
    Opcode_ssxp_encode_fns, 1, Opcode_ssxp_funcUnit_uses },
  { "ldi", ICLASS_LDI,
    0,
    Opcode_ldi_encode_fns, 1, Opcode_ldi_funcUnit_uses },
  { "ldip", ICLASS_LDIP,
    0,
    Opcode_ldip_encode_fns, 1, Opcode_ldip_funcUnit_uses },
  { "ldx", ICLASS_LDX,
    0,
    Opcode_ldx_encode_fns, 1, Opcode_ldx_funcUnit_uses },
  { "ldxp", ICLASS_LDXP,
    0,
    Opcode_ldxp_encode_fns, 1, Opcode_ldxp_funcUnit_uses },
  { "sdi", ICLASS_SDI,
    0,
    Opcode_sdi_encode_fns, 1, Opcode_sdi_funcUnit_uses },
  { "sdip", ICLASS_SDIP,
    0,
    Opcode_sdip_encode_fns, 1, Opcode_sdip_funcUnit_uses },
  { "sdx", ICLASS_SDX,
    0,
    Opcode_sdx_encode_fns, 1, Opcode_sdx_funcUnit_uses },
  { "sdxp", ICLASS_SDXP,
    0,
    Opcode_sdxp_encode_fns, 1, Opcode_sdxp_funcUnit_uses },
  { "abs.s", ICLASS_ABS_S,
    0,
    Opcode_abs_s_encode_fns, 0, 0 },
  { "neg.s", ICLASS_NEG_S,
    0,
    Opcode_neg_s_encode_fns, 0, 0 },
  { "abs.d", ICLASS_ABS_D,
    0,
    Opcode_abs_d_encode_fns, 0, 0 },
  { "neg.d", ICLASS_NEG_D,
    0,
    Opcode_neg_d_encode_fns, 0, 0 },
  { "mov.s", ICLASS_MOV_S,
    0,
    Opcode_mov_s_encode_fns, 0, 0 },
  { "mov.d", ICLASS_MOV_D,
    0,
    Opcode_mov_d_encode_fns, 0, 0 },
  { "moveqz.s", ICLASS_MOVEQZ_S,
    0,
    Opcode_moveqz_s_encode_fns, 0, 0 },
  { "movnez.s", ICLASS_MOVNEZ_S,
    0,
    Opcode_movnez_s_encode_fns, 0, 0 },
  { "movltz.s", ICLASS_MOVLTZ_S,
    0,
    Opcode_movltz_s_encode_fns, 0, 0 },
  { "movgez.s", ICLASS_MOVGEZ_S,
    0,
    Opcode_movgez_s_encode_fns, 0, 0 },
  { "movf.s", ICLASS_MOVF_S,
    0,
    Opcode_movf_s_encode_fns, 0, 0 },
  { "movt.s", ICLASS_MOVT_S,
    0,
    Opcode_movt_s_encode_fns, 0, 0 },
  { "wfr", ICLASS_WFR,
    0,
    Opcode_wfr_encode_fns, 0, 0 },
  { "rfr", ICLASS_RFR,
    0,
    Opcode_rfr_encode_fns, 0, 0 },
  { "rfrd", ICLASS_RFRD,
    0,
    Opcode_rfrd_encode_fns, 0, 0 },
  { "wfrd", ICLASS_WFRD,
    0,
    Opcode_wfrd_encode_fns, 0, 0 },
  { "round.s", ICLASS_ROUND_S,
    0,
    Opcode_round_s_encode_fns, 0, 0 },
  { "round.d", ICLASS_ROUND_D,
    0,
    Opcode_round_d_encode_fns, 0, 0 },
  { "ceil.s", ICLASS_CEIL_S,
    0,
    Opcode_ceil_s_encode_fns, 0, 0 },
  { "ceil.d", ICLASS_CEIL_D,
    0,
    Opcode_ceil_d_encode_fns, 0, 0 },
  { "floor.s", ICLASS_FLOOR_S,
    0,
    Opcode_floor_s_encode_fns, 0, 0 },
  { "floor.d", ICLASS_FLOOR_D,
    0,
    Opcode_floor_d_encode_fns, 0, 0 },
  { "trunc.s", ICLASS_TRUNC_S,
    0,
    Opcode_trunc_s_encode_fns, 0, 0 },
  { "trunc.d", ICLASS_TRUNC_D,
    0,
    Opcode_trunc_d_encode_fns, 0, 0 },
  { "utrunc.s", ICLASS_UTRUNC_S,
    0,
    Opcode_utrunc_s_encode_fns, 0, 0 },
  { "utrunc.d", ICLASS_UTRUNC_D,
    0,
    Opcode_utrunc_d_encode_fns, 0, 0 },
  { "float.s", ICLASS_FLOAT_S,
    0,
    Opcode_float_s_encode_fns, 0, 0 },
  { "float.d", ICLASS_FLOAT_D,
    0,
    Opcode_float_d_encode_fns, 0, 0 },
  { "ufloat.s", ICLASS_UFLOAT_S,
    0,
    Opcode_ufloat_s_encode_fns, 0, 0 },
  { "ufloat.d", ICLASS_UFLOAT_D,
    0,
    Opcode_ufloat_d_encode_fns, 0, 0 },
  { "cvtd.s", ICLASS_CVTD_S,
    0,
    Opcode_cvtd_s_encode_fns, 0, 0 },
  { "cvts.d", ICLASS_CVTS_D,
    0,
    Opcode_cvts_d_encode_fns, 0, 0 },
  { "un.s", ICLASS_UN_S,
    0,
    Opcode_un_s_encode_fns, 0, 0 },
  { "un.d", ICLASS_UN_D,
    0,
    Opcode_un_d_encode_fns, 0, 0 },
  { "ult.s", ICLASS_ULT_S,
    0,
    Opcode_ult_s_encode_fns, 0, 0 },
  { "ult.d", ICLASS_ULT_D,
    0,
    Opcode_ult_d_encode_fns, 0, 0 },
  { "ule.s", ICLASS_ULE_S,
    0,
    Opcode_ule_s_encode_fns, 0, 0 },
  { "ule.d", ICLASS_ULE_D,
    0,
    Opcode_ule_d_encode_fns, 0, 0 },
  { "ueq.s", ICLASS_UEQ_S,
    0,
    Opcode_ueq_s_encode_fns, 0, 0 },
  { "ueq.d", ICLASS_UEQ_D,
    0,
    Opcode_ueq_d_encode_fns, 0, 0 },
  { "olt.s", ICLASS_OLT_S,
    0,
    Opcode_olt_s_encode_fns, 0, 0 },
  { "olt.d", ICLASS_OLT_D,
    0,
    Opcode_olt_d_encode_fns, 0, 0 },
  { "ole.s", ICLASS_OLE_S,
    0,
    Opcode_ole_s_encode_fns, 0, 0 },
  { "ole.d", ICLASS_OLE_D,
    0,
    Opcode_ole_d_encode_fns, 0, 0 },
  { "oeq.s", ICLASS_OEQ_S,
    0,
    Opcode_oeq_s_encode_fns, 0, 0 },
  { "oeq.d", ICLASS_OEQ_D,
    0,
    Opcode_oeq_d_encode_fns, 0, 0 },
  { "add.s", ICLASS_ADD_S,
    0,
    Opcode_add_s_encode_fns, 0, 0 },
  { "add.d", ICLASS_ADD_D,
    0,
    Opcode_add_d_encode_fns, 0, 0 },
  { "sub.s", ICLASS_SUB_S,
    0,
    Opcode_sub_s_encode_fns, 0, 0 },
  { "sub.d", ICLASS_SUB_D,
    0,
    Opcode_sub_d_encode_fns, 0, 0 },
  { "mul.s", ICLASS_MUL_S,
    0,
    Opcode_mul_s_encode_fns, 0, 0 },
  { "mul.d", ICLASS_MUL_D,
    0,
    Opcode_mul_d_encode_fns, 0, 0 },
  { "madd.s", ICLASS_MADD_S,
    0,
    Opcode_madd_s_encode_fns, 0, 0 },
  { "madd.d", ICLASS_MADD_D,
    0,
    Opcode_madd_d_encode_fns, 0, 0 },
  { "msub.s", ICLASS_MSUB_S,
    0,
    Opcode_msub_s_encode_fns, 0, 0 },
  { "msub.d", ICLASS_MSUB_D,
    0,
    Opcode_msub_d_encode_fns, 0, 0 },
  { "sqrt0.s", ICLASS_SQRT0_S,
    0,
    Opcode_sqrt0_s_encode_fns, 0, 0 },
  { "sqrt0.d", ICLASS_SQRT0_D,
    0,
    Opcode_sqrt0_d_encode_fns, 0, 0 },
  { "div0.s", ICLASS_DIV0_S,
    0,
    Opcode_div0_s_encode_fns, 0, 0 },
  { "div0.d", ICLASS_DIV0_D,
    0,
    Opcode_div0_d_encode_fns, 0, 0 },
  { "recip0.s", ICLASS_RECIP0_S,
    0,
    Opcode_recip0_s_encode_fns, 0, 0 },
  { "recip0.d", ICLASS_RECIP0_D,
    0,
    Opcode_recip0_d_encode_fns, 0, 0 },
  { "rsqrt0.s", ICLASS_RSQRT0_S,
    0,
    Opcode_rsqrt0_s_encode_fns, 0, 0 },
  { "rsqrt0.d", ICLASS_RSQRT0_D,
    0,
    Opcode_rsqrt0_d_encode_fns, 0, 0 },
  { "maddn.s", ICLASS_MADDN_S,
    0,
    Opcode_maddn_s_encode_fns, 0, 0 },
  { "maddn.d", ICLASS_MADDN_D,
    0,
    Opcode_maddn_d_encode_fns, 0, 0 },
  { "divn.s", ICLASS_DIVN_S,
    0,
    Opcode_divn_s_encode_fns, 0, 0 },
  { "divn.d", ICLASS_DIVN_D,
    0,
    Opcode_divn_d_encode_fns, 0, 0 },
  { "const.s", ICLASS_CONST_S,
    0,
    Opcode_const_s_encode_fns, 0, 0 },
  { "const.d", ICLASS_CONST_D,
    0,
    Opcode_const_d_encode_fns, 0, 0 },
  { "nexp01.s", ICLASS_NEXP01_S,
    0,
    Opcode_nexp01_s_encode_fns, 0, 0 },
  { "nexp01.d", ICLASS_NEXP01_D,
    0,
    Opcode_nexp01_d_encode_fns, 0, 0 },
  { "addexp.s", ICLASS_ADDEXP_S,
    0,
    Opcode_addexp_s_encode_fns, 0, 0 },
  { "addexp.d", ICLASS_ADDEXP_D,
    0,
    Opcode_addexp_d_encode_fns, 0, 0 },
  { "addexpm.s", ICLASS_ADDEXPM_S,
    0,
    Opcode_addexpm_s_encode_fns, 0, 0 },
  { "addexpm.d", ICLASS_ADDEXPM_D,
    0,
    Opcode_addexpm_d_encode_fns, 0, 0 },
  { "mkdadj.s", ICLASS_MKDADJ_S,
    0,
    Opcode_mkdadj_s_encode_fns, 0, 0 },
  { "mkdadj.d", ICLASS_MKDADJ_D,
    0,
    Opcode_mkdadj_d_encode_fns, 0, 0 },
  { "mksadj.s", ICLASS_MKSADJ_S,
    0,
    Opcode_mksadj_s_encode_fns, 0, 0 },
  { "mksadj.d", ICLASS_MKSADJ_D,
    0,
    Opcode_mksadj_d_encode_fns, 0, 0 }
};

enum xtensa_opcode_id {
  OPCODE_EXCW,
  OPCODE_RFE,
  OPCODE_RFDE,
  OPCODE_SYSCALL,
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
  OPCODE_ADDX2,
  OPCODE_ADDX4,
  OPCODE_ADDX8,
  OPCODE_SUB,
  OPCODE_SUBX2,
  OPCODE_SUBX4,
  OPCODE_SUBX8,
  OPCODE_AND,
  OPCODE_OR,
  OPCODE_XOR,
  OPCODE_BEQI,
  OPCODE_BGEI,
  OPCODE_BLTI,
  OPCODE_BNEI,
  OPCODE_BBCI,
  OPCODE_BBSI,
  OPCODE_BGEUI,
  OPCODE_BLTUI,
  OPCODE_BALL,
  OPCODE_BANY,
  OPCODE_BBC,
  OPCODE_BBS,
  OPCODE_BEQ,
  OPCODE_BGE,
  OPCODE_BGEU,
  OPCODE_BLT,
  OPCODE_BLTU,
  OPCODE_BNALL,
  OPCODE_BNE,
  OPCODE_BNONE,
  OPCODE_BEQZ,
  OPCODE_BGEZ,
  OPCODE_BLTZ,
  OPCODE_BNEZ,
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
  OPCODE_LOOPGTZ,
  OPCODE_LOOPNEZ,
  OPCODE_MOVI,
  OPCODE_MOVEQZ,
  OPCODE_MOVGEZ,
  OPCODE_MOVLTZ,
  OPCODE_MOVNEZ,
  OPCODE_ABS,
  OPCODE_NEG,
  OPCODE_NOP,
  OPCODE_RET,
  OPCODE_SIMCALL,
  OPCODE_S16I,
  OPCODE_S32I,
  OPCODE_S32NB,
  OPCODE_S8I,
  OPCODE_SSA8B,
  OPCODE_SSA8L,
  OPCODE_SSL,
  OPCODE_SSR,
  OPCODE_SSAI,
  OPCODE_SLL,
  OPCODE_SRC,
  OPCODE_SRA,
  OPCODE_SRL,
  OPCODE_SLLI,
  OPCODE_SRAI,
  OPCODE_SRLI,
  OPCODE_MEMW,
  OPCODE_EXTW,
  OPCODE_ISYNC,
  OPCODE_DSYNC,
  OPCODE_ESYNC,
  OPCODE_RSYNC,
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
  OPCODE_RSR_MEMCTL,
  OPCODE_WSR_MEMCTL,
  OPCODE_XSR_MEMCTL,
  OPCODE_RSR_CONFIGID0,
  OPCODE_WSR_CONFIGID0,
  OPCODE_RSR_CONFIGID1,
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
  OPCODE_SALT,
  OPCODE_SALTU,
  OPCODE_MUL16S,
  OPCODE_MUL16U,
  OPCODE_MULL,
  OPCODE_MUL_AA_HH,
  OPCODE_MUL_AA_HL,
  OPCODE_MUL_AA_LH,
  OPCODE_MUL_AA_LL,
  OPCODE_UMUL_AA_HH,
  OPCODE_UMUL_AA_HL,
  OPCODE_UMUL_AA_LH,
  OPCODE_UMUL_AA_LL,
  OPCODE_MUL_AD_HH,
  OPCODE_MUL_AD_HL,
  OPCODE_MUL_AD_LH,
  OPCODE_MUL_AD_LL,
  OPCODE_MUL_DA_HH,
  OPCODE_MUL_DA_HL,
  OPCODE_MUL_DA_LH,
  OPCODE_MUL_DA_LL,
  OPCODE_MUL_DD_HH,
  OPCODE_MUL_DD_HL,
  OPCODE_MUL_DD_LH,
  OPCODE_MUL_DD_LL,
  OPCODE_MULA_AA_HH,
  OPCODE_MULA_AA_HL,
  OPCODE_MULA_AA_LH,
  OPCODE_MULA_AA_LL,
  OPCODE_MULS_AA_HH,
  OPCODE_MULS_AA_HL,
  OPCODE_MULS_AA_LH,
  OPCODE_MULS_AA_LL,
  OPCODE_MULA_AD_HH,
  OPCODE_MULA_AD_HL,
  OPCODE_MULA_AD_LH,
  OPCODE_MULA_AD_LL,
  OPCODE_MULS_AD_HH,
  OPCODE_MULS_AD_HL,
  OPCODE_MULS_AD_LH,
  OPCODE_MULS_AD_LL,
  OPCODE_MULA_DA_HH,
  OPCODE_MULA_DA_HL,
  OPCODE_MULA_DA_LH,
  OPCODE_MULA_DA_LL,
  OPCODE_MULS_DA_HH,
  OPCODE_MULS_DA_HL,
  OPCODE_MULS_DA_LH,
  OPCODE_MULS_DA_LL,
  OPCODE_MULA_DD_HH,
  OPCODE_MULA_DD_HL,
  OPCODE_MULA_DD_LH,
  OPCODE_MULA_DD_LL,
  OPCODE_MULS_DD_HH,
  OPCODE_MULS_DD_HL,
  OPCODE_MULS_DD_LH,
  OPCODE_MULS_DD_LL,
  OPCODE_MULA_DA_HH_LDDEC,
  OPCODE_MULA_DA_HH_LDINC,
  OPCODE_MULA_DA_HL_LDDEC,
  OPCODE_MULA_DA_HL_LDINC,
  OPCODE_MULA_DA_LH_LDDEC,
  OPCODE_MULA_DA_LH_LDINC,
  OPCODE_MULA_DA_LL_LDDEC,
  OPCODE_MULA_DA_LL_LDINC,
  OPCODE_MULA_DD_HH_LDDEC,
  OPCODE_MULA_DD_HH_LDINC,
  OPCODE_MULA_DD_HL_LDDEC,
  OPCODE_MULA_DD_HL_LDINC,
  OPCODE_MULA_DD_LH_LDDEC,
  OPCODE_MULA_DD_LH_LDINC,
  OPCODE_MULA_DD_LL_LDDEC,
  OPCODE_MULA_DD_LL_LDINC,
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
  OPCODE_LDDR32_P,
  OPCODE_SDDR32_P,
  OPCODE_RFDO,
  OPCODE_RFDD,
  OPCODE_WSR_MMID,
  OPCODE_ANDB,
  OPCODE_ANDBC,
  OPCODE_ORB,
  OPCODE_ORBC,
  OPCODE_XORB,
  OPCODE_ALL4,
  OPCODE_ANY4,
  OPCODE_ALL8,
  OPCODE_ANY8,
  OPCODE_BF,
  OPCODE_BT,
  OPCODE_MOVF,
  OPCODE_MOVT,
  OPCODE_RSR_BR,
  OPCODE_WSR_BR,
  OPCODE_XSR_BR,
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
  OPCODE_IHI,
  OPCODE_IPF,
  OPCODE_IHU,
  OPCODE_IIU,
  OPCODE_IPFL,
  OPCODE_III,
  OPCODE_LICT,
  OPCODE_LICW,
  OPCODE_SICT,
  OPCODE_SICW,
  OPCODE_DHWB,
  OPCODE_DHWBI,
  OPCODE_DIWBUI_P,
  OPCODE_DIWB,
  OPCODE_DIWBI,
  OPCODE_DHI,
  OPCODE_DII,
  OPCODE_DPFR,
  OPCODE_DPFRO,
  OPCODE_DPFW,
  OPCODE_DPFWO,
  OPCODE_DHU,
  OPCODE_DIU,
  OPCODE_DPFL,
  OPCODE_SDCT,
  OPCODE_LDCT,
  OPCODE_SDCW,
  OPCODE_LDCW,
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
  OPCODE_MAX,
  OPCODE_MAXU,
  OPCODE_MIN,
  OPCODE_MINU,
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
  OPCODE_QUOS,
  OPCODE_QUOU,
  OPCODE_REMS,
  OPCODE_REMU,
  OPCODE_RSR_ERACCESS,
  OPCODE_WSR_ERACCESS,
  OPCODE_XSR_ERACCESS,
  OPCODE_RER,
  OPCODE_WER,
  OPCODE_RUR_FCR,
  OPCODE_WUR_FCR,
  OPCODE_RUR_FSR,
  OPCODE_WUR_FSR,
  OPCODE_READ_IMPWIRE,
  OPCODE_SETB_EXPSTATE,
  OPCODE_CLRB_EXPSTATE,
  OPCODE_WRMSK_EXPSTATE,
  OPCODE_RUR_EXPSTATE,
  OPCODE_WUR_EXPSTATE,
  OPCODE_LSI,
  OPCODE_LSIP,
  OPCODE_LSX,
  OPCODE_LSXP,
  OPCODE_SSI,
  OPCODE_SSIP,
  OPCODE_SSX,
  OPCODE_SSXP,
  OPCODE_LDI,
  OPCODE_LDIP,
  OPCODE_LDX,
  OPCODE_LDXP,
  OPCODE_SDI,
  OPCODE_SDIP,
  OPCODE_SDX,
  OPCODE_SDXP,
  OPCODE_ABS_S,
  OPCODE_NEG_S,
  OPCODE_ABS_D,
  OPCODE_NEG_D,
  OPCODE_MOV_S,
  OPCODE_MOV_D,
  OPCODE_MOVEQZ_S,
  OPCODE_MOVNEZ_S,
  OPCODE_MOVLTZ_S,
  OPCODE_MOVGEZ_S,
  OPCODE_MOVF_S,
  OPCODE_MOVT_S,
  OPCODE_WFR,
  OPCODE_RFR,
  OPCODE_RFRD,
  OPCODE_WFRD,
  OPCODE_ROUND_S,
  OPCODE_ROUND_D,
  OPCODE_CEIL_S,
  OPCODE_CEIL_D,
  OPCODE_FLOOR_S,
  OPCODE_FLOOR_D,
  OPCODE_TRUNC_S,
  OPCODE_TRUNC_D,
  OPCODE_UTRUNC_S,
  OPCODE_UTRUNC_D,
  OPCODE_FLOAT_S,
  OPCODE_FLOAT_D,
  OPCODE_UFLOAT_S,
  OPCODE_UFLOAT_D,
  OPCODE_CVTD_S,
  OPCODE_CVTS_D,
  OPCODE_UN_S,
  OPCODE_UN_D,
  OPCODE_ULT_S,
  OPCODE_ULT_D,
  OPCODE_ULE_S,
  OPCODE_ULE_D,
  OPCODE_UEQ_S,
  OPCODE_UEQ_D,
  OPCODE_OLT_S,
  OPCODE_OLT_D,
  OPCODE_OLE_S,
  OPCODE_OLE_D,
  OPCODE_OEQ_S,
  OPCODE_OEQ_D,
  OPCODE_ADD_S,
  OPCODE_ADD_D,
  OPCODE_SUB_S,
  OPCODE_SUB_D,
  OPCODE_MUL_S,
  OPCODE_MUL_D,
  OPCODE_MADD_S,
  OPCODE_MADD_D,
  OPCODE_MSUB_S,
  OPCODE_MSUB_D,
  OPCODE_SQRT0_S,
  OPCODE_SQRT0_D,
  OPCODE_DIV0_S,
  OPCODE_DIV0_D,
  OPCODE_RECIP0_S,
  OPCODE_RECIP0_D,
  OPCODE_RSQRT0_S,
  OPCODE_RSQRT0_D,
  OPCODE_MADDN_S,
  OPCODE_MADDN_D,
  OPCODE_DIVN_S,
  OPCODE_DIVN_D,
  OPCODE_CONST_S,
  OPCODE_CONST_D,
  OPCODE_NEXP01_S,
  OPCODE_NEXP01_D,
  OPCODE_ADDEXP_S,
  OPCODE_ADDEXP_D,
  OPCODE_ADDEXPM_S,
  OPCODE_ADDEXPM_D,
  OPCODE_MKDADJ_S,
  OPCODE_MKDADJ_D,
  OPCODE_MKSADJ_S,
  OPCODE_MKSADJ_D
};


/* Slot-specific opcode decode functions.  */

static int
Slot_inst_decode (const xtensa_insnbuf insn)
{
  if (Field_op0_Slot_inst_get (insn) == 0)
    {
      if (Field_op1_Slot_inst_get (insn) == 0)
	{
	  if (Field_op2_Slot_inst_get (insn) == 0)
	    {
	      if (Field_r_Slot_inst_get (insn) == 0)
		{
		  if (Field_m_Slot_inst_get (insn) == 0 &&
		      Field_s_Slot_inst_get (insn) == 0 &&
		      Field_n_Slot_inst_get (insn) == 0)
		    return OPCODE_ILL;
		  if (Field_m_Slot_inst_get (insn) == 2)
		    {
		      if (Field_n_Slot_inst_get (insn) == 0)
			return OPCODE_RET;
		      if (Field_n_Slot_inst_get (insn) == 1)
			return OPCODE_RETW;
		      if (Field_n_Slot_inst_get (insn) == 2)
			return OPCODE_JX;
		    }
		  if (Field_m_Slot_inst_get (insn) == 3)
		    {
		      if (Field_n_Slot_inst_get (insn) == 0)
			return OPCODE_CALLX0;
		      if (Field_n_Slot_inst_get (insn) == 1)
			return OPCODE_CALLX4;
		      if (Field_n_Slot_inst_get (insn) == 2)
			return OPCODE_CALLX8;
		      if (Field_n_Slot_inst_get (insn) == 3)
			return OPCODE_CALLX12;
		    }
		}
	      if (Field_r_Slot_inst_get (insn) == 1)
		return OPCODE_MOVSP;
	      if (Field_r_Slot_inst_get (insn) == 2)
		{
		  if (Field_s_Slot_inst_get (insn) == 0)
		    {
		      if (Field_t_Slot_inst_get (insn) == 0)
			return OPCODE_ISYNC;
		      if (Field_t_Slot_inst_get (insn) == 1)
			return OPCODE_RSYNC;
		      if (Field_t_Slot_inst_get (insn) == 2)
			return OPCODE_ESYNC;
		      if (Field_t_Slot_inst_get (insn) == 3)
			return OPCODE_DSYNC;
		      if (Field_t_Slot_inst_get (insn) == 8)
			return OPCODE_EXCW;
		      if (Field_t_Slot_inst_get (insn) == 12)
			return OPCODE_MEMW;
		      if (Field_t_Slot_inst_get (insn) == 13)
			return OPCODE_EXTW;
		      if (Field_t_Slot_inst_get (insn) == 15)
			return OPCODE_NOP;
		    }
		}
	      if (Field_r_Slot_inst_get (insn) == 3)
		{
		  if (Field_t_Slot_inst_get (insn) == 0)
		    {
		      if (Field_s_Slot_inst_get (insn) == 0)
			return OPCODE_RFE;
		      if (Field_s_Slot_inst_get (insn) == 2)
			return OPCODE_RFDE;
		      if (Field_s_Slot_inst_get (insn) == 4)
			return OPCODE_RFWO;
		      if (Field_s_Slot_inst_get (insn) == 5)
			return OPCODE_RFWU;
		    }
		  if (Field_t_Slot_inst_get (insn) == 1)
		    return OPCODE_RFI;
		}
	      if (Field_r_Slot_inst_get (insn) == 4)
		return OPCODE_BREAK;
	      if (Field_r_Slot_inst_get (insn) == 5)
		{
		  if (Field_s_Slot_inst_get (insn) == 0 &&
		      Field_t_Slot_inst_get (insn) == 0)
		    return OPCODE_SYSCALL;
		  if (Field_s_Slot_inst_get (insn) == 1)
		    return OPCODE_SIMCALL;
		}
	      if (Field_r_Slot_inst_get (insn) == 6)
		return OPCODE_RSIL;
	      if (Field_r_Slot_inst_get (insn) == 7 &&
		  Field_t_Slot_inst_get (insn) == 0)
		return OPCODE_WAITI;
	      if (Field_r_Slot_inst_get (insn) == 7)
		{
		  if (Field_t_Slot_inst_get (insn) == 14)
		    return OPCODE_LDDR32_P;
		  if (Field_t_Slot_inst_get (insn) == 15)
		    return OPCODE_SDDR32_P;
		}
	      if (Field_r_Slot_inst_get (insn) == 8)
		return OPCODE_ANY4;
	      if (Field_r_Slot_inst_get (insn) == 9)
		return OPCODE_ALL4;
	      if (Field_r_Slot_inst_get (insn) == 10)
		return OPCODE_ANY8;
	      if (Field_r_Slot_inst_get (insn) == 11)
		return OPCODE_ALL8;
	    }
	  if (Field_op2_Slot_inst_get (insn) == 1)
	    return OPCODE_AND;
	  if (Field_op2_Slot_inst_get (insn) == 2)
	    return OPCODE_OR;
	  if (Field_op2_Slot_inst_get (insn) == 3)
	    return OPCODE_XOR;
	  if (Field_op2_Slot_inst_get (insn) == 4)
	    {
	      if (Field_r_Slot_inst_get (insn) == 0 &&
		  Field_t_Slot_inst_get (insn) == 0)
		return OPCODE_SSR;
	      if (Field_r_Slot_inst_get (insn) == 1 &&
		  Field_t_Slot_inst_get (insn) == 0)
		return OPCODE_SSL;
	      if (Field_r_Slot_inst_get (insn) == 2 &&
		  Field_t_Slot_inst_get (insn) == 0)
		return OPCODE_SSA8L;
	      if (Field_r_Slot_inst_get (insn) == 3 &&
		  Field_t_Slot_inst_get (insn) == 0)
		return OPCODE_SSA8B;
	      if (Field_r_Slot_inst_get (insn) == 4 &&
		  Field_thi3_Slot_inst_get (insn) == 0)
		return OPCODE_SSAI;
	      if (Field_r_Slot_inst_get (insn) == 6)
		return OPCODE_RER;
	      if (Field_r_Slot_inst_get (insn) == 7)
		return OPCODE_WER;
	      if (Field_r_Slot_inst_get (insn) == 8 &&
		  Field_s_Slot_inst_get (insn) == 0)
		return OPCODE_ROTW;
	      if (Field_r_Slot_inst_get (insn) == 14)
		return OPCODE_NSA;
	      if (Field_r_Slot_inst_get (insn) == 15)
		return OPCODE_NSAU;
	    }
	  if (Field_op2_Slot_inst_get (insn) == 5)
	    {
	      if (Field_r_Slot_inst_get (insn) == 1)
		return OPCODE_HWWITLBA;
	      if (Field_r_Slot_inst_get (insn) == 3)
		return OPCODE_RITLB0;
	      if (Field_r_Slot_inst_get (insn) == 4 &&
		  Field_t_Slot_inst_get (insn) == 0)
		return OPCODE_IITLB;
	      if (Field_r_Slot_inst_get (insn) == 5)
		return OPCODE_PITLB;
	      if (Field_r_Slot_inst_get (insn) == 6)
		return OPCODE_WITLB;
	      if (Field_r_Slot_inst_get (insn) == 7)
		return OPCODE_RITLB1;
	      if (Field_r_Slot_inst_get (insn) == 9)
		return OPCODE_HWWDTLBA;
	      if (Field_r_Slot_inst_get (insn) == 11)
		return OPCODE_RDTLB0;
	      if (Field_r_Slot_inst_get (insn) == 12 &&
		  Field_t_Slot_inst_get (insn) == 0)
		return OPCODE_IDTLB;
	      if (Field_r_Slot_inst_get (insn) == 13)
		return OPCODE_PDTLB;
	      if (Field_r_Slot_inst_get (insn) == 14)
		return OPCODE_WDTLB;
	      if (Field_r_Slot_inst_get (insn) == 15)
		return OPCODE_RDTLB1;
	    }
	  if (Field_op2_Slot_inst_get (insn) == 6)
	    {
	      if (Field_s_Slot_inst_get (insn) == 0)
		return OPCODE_NEG;
	      if (Field_s_Slot_inst_get (insn) == 1)
		return OPCODE_ABS;
	    }
	  if (Field_op2_Slot_inst_get (insn) == 8)
	    return OPCODE_ADD;
	  if (Field_op2_Slot_inst_get (insn) == 9)
	    return OPCODE_ADDX2;
	  if (Field_op2_Slot_inst_get (insn) == 10)
	    return OPCODE_ADDX4;
	  if (Field_op2_Slot_inst_get (insn) == 11)
	    return OPCODE_ADDX8;
	  if (Field_op2_Slot_inst_get (insn) == 12)
	    return OPCODE_SUB;
	  if (Field_op2_Slot_inst_get (insn) == 13)
	    return OPCODE_SUBX2;
	  if (Field_op2_Slot_inst_get (insn) == 14)
	    return OPCODE_SUBX4;
	  if (Field_op2_Slot_inst_get (insn) == 15)
	    return OPCODE_SUBX8;
	}
      if (Field_op1_Slot_inst_get (insn) == 1)
	{
	  if ((Field_op2_Slot_inst_get (insn) == 0 ||
	       Field_op2_Slot_inst_get (insn) == 1))
	    return OPCODE_SLLI;
	  if ((Field_op2_Slot_inst_get (insn) == 2 ||
	       Field_op2_Slot_inst_get (insn) == 3))
	    return OPCODE_SRAI;
	  if (Field_op2_Slot_inst_get (insn) == 4)
	    return OPCODE_SRLI;
	  if (Field_op2_Slot_inst_get (insn) == 6)
	    {
	      if (Field_sr_Slot_inst_get (insn) == 0)
		return OPCODE_XSR_LBEG;
	      if (Field_sr_Slot_inst_get (insn) == 1)
		return OPCODE_XSR_LEND;
	      if (Field_sr_Slot_inst_get (insn) == 2)
		return OPCODE_XSR_LCOUNT;
	      if (Field_sr_Slot_inst_get (insn) == 3)
		return OPCODE_XSR_SAR;
	      if (Field_sr_Slot_inst_get (insn) == 4)
		return OPCODE_XSR_BR;
	      if (Field_sr_Slot_inst_get (insn) == 12)
		return OPCODE_XSR_SCOMPARE1;
	      if (Field_sr_Slot_inst_get (insn) == 16)
		return OPCODE_XSR_ACCLO;
	      if (Field_sr_Slot_inst_get (insn) == 17)
		return OPCODE_XSR_ACCHI;
	      if (Field_sr_Slot_inst_get (insn) == 32)
		return OPCODE_XSR_M0;
	      if (Field_sr_Slot_inst_get (insn) == 33)
		return OPCODE_XSR_M1;
	      if (Field_sr_Slot_inst_get (insn) == 34)
		return OPCODE_XSR_M2;
	      if (Field_sr_Slot_inst_get (insn) == 35)
		return OPCODE_XSR_M3;
	      if (Field_sr_Slot_inst_get (insn) == 72)
		return OPCODE_XSR_WINDOWBASE;
	      if (Field_sr_Slot_inst_get (insn) == 73)
		return OPCODE_XSR_WINDOWSTART;
	      if (Field_sr_Slot_inst_get (insn) == 83)
		return OPCODE_XSR_PTEVADDR;
	      if (Field_sr_Slot_inst_get (insn) == 90)
		return OPCODE_XSR_RASID;
	      if (Field_sr_Slot_inst_get (insn) == 91)
		return OPCODE_XSR_ITLBCFG;
	      if (Field_sr_Slot_inst_get (insn) == 92)
		return OPCODE_XSR_DTLBCFG;
	      if (Field_sr_Slot_inst_get (insn) == 95)
		return OPCODE_XSR_ERACCESS;
	      if (Field_sr_Slot_inst_get (insn) == 96)
		return OPCODE_XSR_IBREAKENABLE;
	      if (Field_sr_Slot_inst_get (insn) == 97)
		return OPCODE_XSR_MEMCTL;
	      if (Field_sr_Slot_inst_get (insn) == 99)
		return OPCODE_XSR_ATOMCTL;
	      if (Field_sr_Slot_inst_get (insn) == 104)
		return OPCODE_XSR_DDR;
	      if (Field_sr_Slot_inst_get (insn) == 128)
		return OPCODE_XSR_IBREAKA0;
	      if (Field_sr_Slot_inst_get (insn) == 129)
		return OPCODE_XSR_IBREAKA1;
	      if (Field_sr_Slot_inst_get (insn) == 144)
		return OPCODE_XSR_DBREAKA0;
	      if (Field_sr_Slot_inst_get (insn) == 145)
		return OPCODE_XSR_DBREAKA1;
	      if (Field_sr_Slot_inst_get (insn) == 160)
		return OPCODE_XSR_DBREAKC0;
	      if (Field_sr_Slot_inst_get (insn) == 161)
		return OPCODE_XSR_DBREAKC1;
	      if (Field_sr_Slot_inst_get (insn) == 177)
		return OPCODE_XSR_EPC1;
	      if (Field_sr_Slot_inst_get (insn) == 178)
		return OPCODE_XSR_EPC2;
	      if (Field_sr_Slot_inst_get (insn) == 179)
		return OPCODE_XSR_EPC3;
	      if (Field_sr_Slot_inst_get (insn) == 180)
		return OPCODE_XSR_EPC4;
	      if (Field_sr_Slot_inst_get (insn) == 181)
		return OPCODE_XSR_EPC5;
	      if (Field_sr_Slot_inst_get (insn) == 182)
		return OPCODE_XSR_EPC6;
	      if (Field_sr_Slot_inst_get (insn) == 183)
		return OPCODE_XSR_EPC7;
	      if (Field_sr_Slot_inst_get (insn) == 192)
		return OPCODE_XSR_DEPC;
	      if (Field_sr_Slot_inst_get (insn) == 194)
		return OPCODE_XSR_EPS2;
	      if (Field_sr_Slot_inst_get (insn) == 195)
		return OPCODE_XSR_EPS3;
	      if (Field_sr_Slot_inst_get (insn) == 196)
		return OPCODE_XSR_EPS4;
	      if (Field_sr_Slot_inst_get (insn) == 197)
		return OPCODE_XSR_EPS5;
	      if (Field_sr_Slot_inst_get (insn) == 198)
		return OPCODE_XSR_EPS6;
	      if (Field_sr_Slot_inst_get (insn) == 199)
		return OPCODE_XSR_EPS7;
	      if (Field_sr_Slot_inst_get (insn) == 209)
		return OPCODE_XSR_EXCSAVE1;
	      if (Field_sr_Slot_inst_get (insn) == 210)
		return OPCODE_XSR_EXCSAVE2;
	      if (Field_sr_Slot_inst_get (insn) == 211)
		return OPCODE_XSR_EXCSAVE3;
	      if (Field_sr_Slot_inst_get (insn) == 212)
		return OPCODE_XSR_EXCSAVE4;
	      if (Field_sr_Slot_inst_get (insn) == 213)
		return OPCODE_XSR_EXCSAVE5;
	      if (Field_sr_Slot_inst_get (insn) == 214)
		return OPCODE_XSR_EXCSAVE6;
	      if (Field_sr_Slot_inst_get (insn) == 215)
		return OPCODE_XSR_EXCSAVE7;
	      if (Field_sr_Slot_inst_get (insn) == 224)
		return OPCODE_XSR_CPENABLE;
	      if (Field_sr_Slot_inst_get (insn) == 228)
		return OPCODE_XSR_INTENABLE;
	      if (Field_sr_Slot_inst_get (insn) == 230)
		return OPCODE_XSR_PS;
	      if (Field_sr_Slot_inst_get (insn) == 231)
		return OPCODE_XSR_VECBASE;
	      if (Field_sr_Slot_inst_get (insn) == 232)
		return OPCODE_XSR_EXCCAUSE;
	      if (Field_sr_Slot_inst_get (insn) == 233)
		return OPCODE_XSR_DEBUGCAUSE;
	      if (Field_sr_Slot_inst_get (insn) == 234)
		return OPCODE_XSR_CCOUNT;
	      if (Field_sr_Slot_inst_get (insn) == 236)
		return OPCODE_XSR_ICOUNT;
	      if (Field_sr_Slot_inst_get (insn) == 237)
		return OPCODE_XSR_ICOUNTLEVEL;
	      if (Field_sr_Slot_inst_get (insn) == 238)
		return OPCODE_XSR_EXCVADDR;
	      if (Field_sr_Slot_inst_get (insn) == 240)
		return OPCODE_XSR_CCOMPARE0;
	      if (Field_sr_Slot_inst_get (insn) == 241)
		return OPCODE_XSR_CCOMPARE1;
	      if (Field_sr_Slot_inst_get (insn) == 242)
		return OPCODE_XSR_CCOMPARE2;
	      if (Field_sr_Slot_inst_get (insn) == 244)
		return OPCODE_XSR_MISC0;
	      if (Field_sr_Slot_inst_get (insn) == 245)
		return OPCODE_XSR_MISC1;
	    }
	  if (Field_op2_Slot_inst_get (insn) == 8)
	    return OPCODE_SRC;
	  if (Field_op2_Slot_inst_get (insn) == 9 &&
	      Field_s_Slot_inst_get (insn) == 0)
	    return OPCODE_SRL;
	  if (Field_op2_Slot_inst_get (insn) == 10 &&
	      Field_t_Slot_inst_get (insn) == 0)
	    return OPCODE_SLL;
	  if (Field_op2_Slot_inst_get (insn) == 11 &&
	      Field_s_Slot_inst_get (insn) == 0)
	    return OPCODE_SRA;
	  if (Field_op2_Slot_inst_get (insn) == 12)
	    return OPCODE_MUL16U;
	  if (Field_op2_Slot_inst_get (insn) == 13)
	    return OPCODE_MUL16S;
	  if (Field_op2_Slot_inst_get (insn) == 15)
	    {
	      if (Field_r_Slot_inst_get (insn) == 0)
		return OPCODE_LICT;
	      if (Field_r_Slot_inst_get (insn) == 1)
		return OPCODE_SICT;
	      if (Field_r_Slot_inst_get (insn) == 2)
		return OPCODE_LICW;
	      if (Field_r_Slot_inst_get (insn) == 3)
		return OPCODE_SICW;
	      if (Field_r_Slot_inst_get (insn) == 8)
		return OPCODE_LDCT;
	      if (Field_r_Slot_inst_get (insn) == 9)
		return OPCODE_SDCT;
	      if (Field_r_Slot_inst_get (insn) == 10)
		return OPCODE_LDCW;
	      if (Field_r_Slot_inst_get (insn) == 11)
		return OPCODE_SDCW;
	      if (Field_r_Slot_inst_get (insn) == 14 &&
		  Field_t_Slot_inst_get (insn) == 0)
		return OPCODE_RFDO;
	      if (Field_r_Slot_inst_get (insn) == 14 &&
		  Field_t_Slot_inst_get (insn) == 1)
		return OPCODE_RFDD;
	      if (Field_r_Slot_inst_get (insn) == 15)
		return OPCODE_LDPTE;
	    }
	}
      if (Field_op1_Slot_inst_get (insn) == 2)
	{
	  if (Field_op2_Slot_inst_get (insn) == 0)
	    return OPCODE_ANDB;
	  if (Field_op2_Slot_inst_get (insn) == 1)
	    return OPCODE_ANDBC;
	  if (Field_op2_Slot_inst_get (insn) == 2)
	    return OPCODE_ORB;
	  if (Field_op2_Slot_inst_get (insn) == 3)
	    return OPCODE_ORBC;
	  if (Field_op2_Slot_inst_get (insn) == 4)
	    return OPCODE_XORB;
	  if (Field_op2_Slot_inst_get (insn) == 6)
	    return OPCODE_SALTU;
	  if (Field_op2_Slot_inst_get (insn) == 7)
	    return OPCODE_SALT;
	  if (Field_op2_Slot_inst_get (insn) == 8)
	    return OPCODE_MULL;
	  if (Field_op2_Slot_inst_get (insn) == 12)
	    return OPCODE_QUOU;
	  if (Field_op2_Slot_inst_get (insn) == 13)
	    return OPCODE_QUOS;
	  if (Field_op2_Slot_inst_get (insn) == 14)
	    return OPCODE_REMU;
	  if (Field_op2_Slot_inst_get (insn) == 15)
	    return OPCODE_REMS;
	}
      if (Field_op1_Slot_inst_get (insn) == 3)
	{
	  if (Field_op2_Slot_inst_get (insn) == 0)
	    {
	      if (Field_sr_Slot_inst_get (insn) == 0)
		return OPCODE_RSR_LBEG;
	      if (Field_sr_Slot_inst_get (insn) == 1)
		return OPCODE_RSR_LEND;
	      if (Field_sr_Slot_inst_get (insn) == 2)
		return OPCODE_RSR_LCOUNT;
	      if (Field_sr_Slot_inst_get (insn) == 3)
		return OPCODE_RSR_SAR;
	      if (Field_sr_Slot_inst_get (insn) == 4)
		return OPCODE_RSR_BR;
	      if (Field_sr_Slot_inst_get (insn) == 12)
		return OPCODE_RSR_SCOMPARE1;
	      if (Field_sr_Slot_inst_get (insn) == 16)
		return OPCODE_RSR_ACCLO;
	      if (Field_sr_Slot_inst_get (insn) == 17)
		return OPCODE_RSR_ACCHI;
	      if (Field_sr_Slot_inst_get (insn) == 32)
		return OPCODE_RSR_M0;
	      if (Field_sr_Slot_inst_get (insn) == 33)
		return OPCODE_RSR_M1;
	      if (Field_sr_Slot_inst_get (insn) == 34)
		return OPCODE_RSR_M2;
	      if (Field_sr_Slot_inst_get (insn) == 35)
		return OPCODE_RSR_M3;
	      if (Field_sr_Slot_inst_get (insn) == 72)
		return OPCODE_RSR_WINDOWBASE;
	      if (Field_sr_Slot_inst_get (insn) == 73)
		return OPCODE_RSR_WINDOWSTART;
	      if (Field_sr_Slot_inst_get (insn) == 83)
		return OPCODE_RSR_PTEVADDR;
	      if (Field_sr_Slot_inst_get (insn) == 90)
		return OPCODE_RSR_RASID;
	      if (Field_sr_Slot_inst_get (insn) == 91)
		return OPCODE_RSR_ITLBCFG;
	      if (Field_sr_Slot_inst_get (insn) == 92)
		return OPCODE_RSR_DTLBCFG;
	      if (Field_sr_Slot_inst_get (insn) == 95)
		return OPCODE_RSR_ERACCESS;
	      if (Field_sr_Slot_inst_get (insn) == 96)
		return OPCODE_RSR_IBREAKENABLE;
	      if (Field_sr_Slot_inst_get (insn) == 97)
		return OPCODE_RSR_MEMCTL;
	      if (Field_sr_Slot_inst_get (insn) == 99)
		return OPCODE_RSR_ATOMCTL;
	      if (Field_sr_Slot_inst_get (insn) == 104)
		return OPCODE_RSR_DDR;
	      if (Field_sr_Slot_inst_get (insn) == 128)
		return OPCODE_RSR_IBREAKA0;
	      if (Field_sr_Slot_inst_get (insn) == 129)
		return OPCODE_RSR_IBREAKA1;
	      if (Field_sr_Slot_inst_get (insn) == 144)
		return OPCODE_RSR_DBREAKA0;
	      if (Field_sr_Slot_inst_get (insn) == 145)
		return OPCODE_RSR_DBREAKA1;
	      if (Field_sr_Slot_inst_get (insn) == 160)
		return OPCODE_RSR_DBREAKC0;
	      if (Field_sr_Slot_inst_get (insn) == 161)
		return OPCODE_RSR_DBREAKC1;
	      if (Field_sr_Slot_inst_get (insn) == 176)
		return OPCODE_RSR_CONFIGID0;
	      if (Field_sr_Slot_inst_get (insn) == 177)
		return OPCODE_RSR_EPC1;
	      if (Field_sr_Slot_inst_get (insn) == 178)
		return OPCODE_RSR_EPC2;
	      if (Field_sr_Slot_inst_get (insn) == 179)
		return OPCODE_RSR_EPC3;
	      if (Field_sr_Slot_inst_get (insn) == 180)
		return OPCODE_RSR_EPC4;
	      if (Field_sr_Slot_inst_get (insn) == 181)
		return OPCODE_RSR_EPC5;
	      if (Field_sr_Slot_inst_get (insn) == 182)
		return OPCODE_RSR_EPC6;
	      if (Field_sr_Slot_inst_get (insn) == 183)
		return OPCODE_RSR_EPC7;
	      if (Field_sr_Slot_inst_get (insn) == 192)
		return OPCODE_RSR_DEPC;
	      if (Field_sr_Slot_inst_get (insn) == 194)
		return OPCODE_RSR_EPS2;
	      if (Field_sr_Slot_inst_get (insn) == 195)
		return OPCODE_RSR_EPS3;
	      if (Field_sr_Slot_inst_get (insn) == 196)
		return OPCODE_RSR_EPS4;
	      if (Field_sr_Slot_inst_get (insn) == 197)
		return OPCODE_RSR_EPS5;
	      if (Field_sr_Slot_inst_get (insn) == 198)
		return OPCODE_RSR_EPS6;
	      if (Field_sr_Slot_inst_get (insn) == 199)
		return OPCODE_RSR_EPS7;
	      if (Field_sr_Slot_inst_get (insn) == 208)
		return OPCODE_RSR_CONFIGID1;
	      if (Field_sr_Slot_inst_get (insn) == 209)
		return OPCODE_RSR_EXCSAVE1;
	      if (Field_sr_Slot_inst_get (insn) == 210)
		return OPCODE_RSR_EXCSAVE2;
	      if (Field_sr_Slot_inst_get (insn) == 211)
		return OPCODE_RSR_EXCSAVE3;
	      if (Field_sr_Slot_inst_get (insn) == 212)
		return OPCODE_RSR_EXCSAVE4;
	      if (Field_sr_Slot_inst_get (insn) == 213)
		return OPCODE_RSR_EXCSAVE5;
	      if (Field_sr_Slot_inst_get (insn) == 214)
		return OPCODE_RSR_EXCSAVE6;
	      if (Field_sr_Slot_inst_get (insn) == 215)
		return OPCODE_RSR_EXCSAVE7;
	      if (Field_sr_Slot_inst_get (insn) == 224)
		return OPCODE_RSR_CPENABLE;
	      if (Field_sr_Slot_inst_get (insn) == 226)
		return OPCODE_RSR_INTERRUPT;
	      if (Field_sr_Slot_inst_get (insn) == 228)
		return OPCODE_RSR_INTENABLE;
	      if (Field_sr_Slot_inst_get (insn) == 230)
		return OPCODE_RSR_PS;
	      if (Field_sr_Slot_inst_get (insn) == 231)
		return OPCODE_RSR_VECBASE;
	      if (Field_sr_Slot_inst_get (insn) == 232)
		return OPCODE_RSR_EXCCAUSE;
	      if (Field_sr_Slot_inst_get (insn) == 233)
		return OPCODE_RSR_DEBUGCAUSE;
	      if (Field_sr_Slot_inst_get (insn) == 234)
		return OPCODE_RSR_CCOUNT;
	      if (Field_sr_Slot_inst_get (insn) == 235)
		return OPCODE_RSR_PRID;
	      if (Field_sr_Slot_inst_get (insn) == 236)
		return OPCODE_RSR_ICOUNT;
	      if (Field_sr_Slot_inst_get (insn) == 237)
		return OPCODE_RSR_ICOUNTLEVEL;
	      if (Field_sr_Slot_inst_get (insn) == 238)
		return OPCODE_RSR_EXCVADDR;
	      if (Field_sr_Slot_inst_get (insn) == 240)
		return OPCODE_RSR_CCOMPARE0;
	      if (Field_sr_Slot_inst_get (insn) == 241)
		return OPCODE_RSR_CCOMPARE1;
	      if (Field_sr_Slot_inst_get (insn) == 242)
		return OPCODE_RSR_CCOMPARE2;
	      if (Field_sr_Slot_inst_get (insn) == 244)
		return OPCODE_RSR_MISC0;
	      if (Field_sr_Slot_inst_get (insn) == 245)
		return OPCODE_RSR_MISC1;
	    }
	  if (Field_op2_Slot_inst_get (insn) == 1)
	    {
	      if (Field_sr_Slot_inst_get (insn) == 0)
		return OPCODE_WSR_LBEG;
	      if (Field_sr_Slot_inst_get (insn) == 1)
		return OPCODE_WSR_LEND;
	      if (Field_sr_Slot_inst_get (insn) == 2)
		return OPCODE_WSR_LCOUNT;
	      if (Field_sr_Slot_inst_get (insn) == 3)
		return OPCODE_WSR_SAR;
	      if (Field_sr_Slot_inst_get (insn) == 4)
		return OPCODE_WSR_BR;
	      if (Field_sr_Slot_inst_get (insn) == 12)
		return OPCODE_WSR_SCOMPARE1;
	      if (Field_sr_Slot_inst_get (insn) == 16)
		return OPCODE_WSR_ACCLO;
	      if (Field_sr_Slot_inst_get (insn) == 17)
		return OPCODE_WSR_ACCHI;
	      if (Field_sr_Slot_inst_get (insn) == 32)
		return OPCODE_WSR_M0;
	      if (Field_sr_Slot_inst_get (insn) == 33)
		return OPCODE_WSR_M1;
	      if (Field_sr_Slot_inst_get (insn) == 34)
		return OPCODE_WSR_M2;
	      if (Field_sr_Slot_inst_get (insn) == 35)
		return OPCODE_WSR_M3;
	      if (Field_sr_Slot_inst_get (insn) == 72)
		return OPCODE_WSR_WINDOWBASE;
	      if (Field_sr_Slot_inst_get (insn) == 73)
		return OPCODE_WSR_WINDOWSTART;
	      if (Field_sr_Slot_inst_get (insn) == 83)
		return OPCODE_WSR_PTEVADDR;
	      if (Field_sr_Slot_inst_get (insn) == 89)
		return OPCODE_WSR_MMID;
	      if (Field_sr_Slot_inst_get (insn) == 90)
		return OPCODE_WSR_RASID;
	      if (Field_sr_Slot_inst_get (insn) == 91)
		return OPCODE_WSR_ITLBCFG;
	      if (Field_sr_Slot_inst_get (insn) == 92)
		return OPCODE_WSR_DTLBCFG;
	      if (Field_sr_Slot_inst_get (insn) == 95)
		return OPCODE_WSR_ERACCESS;
	      if (Field_sr_Slot_inst_get (insn) == 96)
		return OPCODE_WSR_IBREAKENABLE;
	      if (Field_sr_Slot_inst_get (insn) == 97)
		return OPCODE_WSR_MEMCTL;
	      if (Field_sr_Slot_inst_get (insn) == 99)
		return OPCODE_WSR_ATOMCTL;
	      if (Field_sr_Slot_inst_get (insn) == 104)
		return OPCODE_WSR_DDR;
	      if (Field_sr_Slot_inst_get (insn) == 128)
		return OPCODE_WSR_IBREAKA0;
	      if (Field_sr_Slot_inst_get (insn) == 129)
		return OPCODE_WSR_IBREAKA1;
	      if (Field_sr_Slot_inst_get (insn) == 144)
		return OPCODE_WSR_DBREAKA0;
	      if (Field_sr_Slot_inst_get (insn) == 145)
		return OPCODE_WSR_DBREAKA1;
	      if (Field_sr_Slot_inst_get (insn) == 160)
		return OPCODE_WSR_DBREAKC0;
	      if (Field_sr_Slot_inst_get (insn) == 161)
		return OPCODE_WSR_DBREAKC1;
	      if (Field_sr_Slot_inst_get (insn) == 176)
		return OPCODE_WSR_CONFIGID0;
	      if (Field_sr_Slot_inst_get (insn) == 177)
		return OPCODE_WSR_EPC1;
	      if (Field_sr_Slot_inst_get (insn) == 178)
		return OPCODE_WSR_EPC2;
	      if (Field_sr_Slot_inst_get (insn) == 179)
		return OPCODE_WSR_EPC3;
	      if (Field_sr_Slot_inst_get (insn) == 180)
		return OPCODE_WSR_EPC4;
	      if (Field_sr_Slot_inst_get (insn) == 181)
		return OPCODE_WSR_EPC5;
	      if (Field_sr_Slot_inst_get (insn) == 182)
		return OPCODE_WSR_EPC6;
	      if (Field_sr_Slot_inst_get (insn) == 183)
		return OPCODE_WSR_EPC7;
	      if (Field_sr_Slot_inst_get (insn) == 192)
		return OPCODE_WSR_DEPC;
	      if (Field_sr_Slot_inst_get (insn) == 194)
		return OPCODE_WSR_EPS2;
	      if (Field_sr_Slot_inst_get (insn) == 195)
		return OPCODE_WSR_EPS3;
	      if (Field_sr_Slot_inst_get (insn) == 196)
		return OPCODE_WSR_EPS4;
	      if (Field_sr_Slot_inst_get (insn) == 197)
		return OPCODE_WSR_EPS5;
	      if (Field_sr_Slot_inst_get (insn) == 198)
		return OPCODE_WSR_EPS6;
	      if (Field_sr_Slot_inst_get (insn) == 199)
		return OPCODE_WSR_EPS7;
	      if (Field_sr_Slot_inst_get (insn) == 209)
		return OPCODE_WSR_EXCSAVE1;
	      if (Field_sr_Slot_inst_get (insn) == 210)
		return OPCODE_WSR_EXCSAVE2;
	      if (Field_sr_Slot_inst_get (insn) == 211)
		return OPCODE_WSR_EXCSAVE3;
	      if (Field_sr_Slot_inst_get (insn) == 212)
		return OPCODE_WSR_EXCSAVE4;
	      if (Field_sr_Slot_inst_get (insn) == 213)
		return OPCODE_WSR_EXCSAVE5;
	      if (Field_sr_Slot_inst_get (insn) == 214)
		return OPCODE_WSR_EXCSAVE6;
	      if (Field_sr_Slot_inst_get (insn) == 215)
		return OPCODE_WSR_EXCSAVE7;
	      if (Field_sr_Slot_inst_get (insn) == 224)
		return OPCODE_WSR_CPENABLE;
	      if (Field_sr_Slot_inst_get (insn) == 226)
		return OPCODE_WSR_INTSET;
	      if (Field_sr_Slot_inst_get (insn) == 227)
		return OPCODE_WSR_INTCLEAR;
	      if (Field_sr_Slot_inst_get (insn) == 228)
		return OPCODE_WSR_INTENABLE;
	      if (Field_sr_Slot_inst_get (insn) == 230)
		return OPCODE_WSR_PS;
	      if (Field_sr_Slot_inst_get (insn) == 231)
		return OPCODE_WSR_VECBASE;
	      if (Field_sr_Slot_inst_get (insn) == 232)
		return OPCODE_WSR_EXCCAUSE;
	      if (Field_sr_Slot_inst_get (insn) == 233)
		return OPCODE_WSR_DEBUGCAUSE;
	      if (Field_sr_Slot_inst_get (insn) == 234)
		return OPCODE_WSR_CCOUNT;
	      if (Field_sr_Slot_inst_get (insn) == 236)
		return OPCODE_WSR_ICOUNT;
	      if (Field_sr_Slot_inst_get (insn) == 237)
		return OPCODE_WSR_ICOUNTLEVEL;
	      if (Field_sr_Slot_inst_get (insn) == 238)
		return OPCODE_WSR_EXCVADDR;
	      if (Field_sr_Slot_inst_get (insn) == 240)
		return OPCODE_WSR_CCOMPARE0;
	      if (Field_sr_Slot_inst_get (insn) == 241)
		return OPCODE_WSR_CCOMPARE1;
	      if (Field_sr_Slot_inst_get (insn) == 242)
		return OPCODE_WSR_CCOMPARE2;
	      if (Field_sr_Slot_inst_get (insn) == 244)
		return OPCODE_WSR_MISC0;
	      if (Field_sr_Slot_inst_get (insn) == 245)
		return OPCODE_WSR_MISC1;
	    }
	  if (Field_op2_Slot_inst_get (insn) == 2)
	    return OPCODE_SEXT;
	  if (Field_op2_Slot_inst_get (insn) == 3)
	    return OPCODE_CLAMPS;
	  if (Field_op2_Slot_inst_get (insn) == 4)
	    return OPCODE_MIN;
	  if (Field_op2_Slot_inst_get (insn) == 5)
	    return OPCODE_MAX;
	  if (Field_op2_Slot_inst_get (insn) == 6)
	    return OPCODE_MINU;
	  if (Field_op2_Slot_inst_get (insn) == 7)
	    return OPCODE_MAXU;
	  if (Field_op2_Slot_inst_get (insn) == 8)
	    return OPCODE_MOVEQZ;
	  if (Field_op2_Slot_inst_get (insn) == 9)
	    return OPCODE_MOVNEZ;
	  if (Field_op2_Slot_inst_get (insn) == 10)
	    return OPCODE_MOVLTZ;
	  if (Field_op2_Slot_inst_get (insn) == 11)
	    return OPCODE_MOVGEZ;
	  if (Field_op2_Slot_inst_get (insn) == 12)
	    return OPCODE_MOVF;
	  if (Field_op2_Slot_inst_get (insn) == 13)
	    return OPCODE_MOVT;
	  if (Field_op2_Slot_inst_get (insn) == 14)
	    {
	      if (Field_st_Slot_inst_get (insn) == 230)
		return OPCODE_RUR_EXPSTATE;
	      if (Field_st_Slot_inst_get (insn) == 231)
		return OPCODE_RUR_THREADPTR;
	      if (Field_st_Slot_inst_get (insn) == 232)
		return OPCODE_RUR_FCR;
	      if (Field_st_Slot_inst_get (insn) == 233)
		return OPCODE_RUR_FSR;
	    }
	  if (Field_op2_Slot_inst_get (insn) == 15)
	    {
	      if (Field_sr_Slot_inst_get (insn) == 230)
		return OPCODE_WUR_EXPSTATE;
	      if (Field_sr_Slot_inst_get (insn) == 231)
		return OPCODE_WUR_THREADPTR;
	      if (Field_sr_Slot_inst_get (insn) == 232)
		return OPCODE_WUR_FCR;
	      if (Field_sr_Slot_inst_get (insn) == 233)
		return OPCODE_WUR_FSR;
	    }
	}
      if ((Field_op1_Slot_inst_get (insn) == 4 ||
	   Field_op1_Slot_inst_get (insn) == 5))
	return OPCODE_EXTUI;
      if (Field_op1_Slot_inst_get (insn) == 8)
	{
	  if (Field_op2_Slot_inst_get (insn) == 0)
	    return OPCODE_LSX;
	  if (Field_op2_Slot_inst_get (insn) == 1)
	    return OPCODE_LSXP;
	  if (Field_op2_Slot_inst_get (insn) == 2)
	    return OPCODE_LDX;
	  if (Field_op2_Slot_inst_get (insn) == 3)
	    return OPCODE_LDXP;
	  if (Field_op2_Slot_inst_get (insn) == 4)
	    return OPCODE_SSX;
	  if (Field_op2_Slot_inst_get (insn) == 5)
	    return OPCODE_SSXP;
	  if (Field_op2_Slot_inst_get (insn) == 6)
	    return OPCODE_SDX;
	  if (Field_op2_Slot_inst_get (insn) == 7)
	    return OPCODE_SDXP;
	}
      if (Field_op1_Slot_inst_get (insn) == 9)
	{
	  if (Field_op2_Slot_inst_get (insn) == 0)
	    return OPCODE_L32E;
	  if (Field_op2_Slot_inst_get (insn) == 4)
	    return OPCODE_S32E;
	  if (Field_op2_Slot_inst_get (insn) == 5)
	    return OPCODE_S32NB;
	}
      if (Field_op1_Slot_inst_get (insn) == 10)
	{
	  if (Field_op2_Slot_inst_get (insn) == 0)
	    return OPCODE_ADD_S;
	  if (Field_op2_Slot_inst_get (insn) == 1)
	    return OPCODE_SUB_S;
	  if (Field_op2_Slot_inst_get (insn) == 2)
	    return OPCODE_MUL_S;
	  if (Field_op2_Slot_inst_get (insn) == 4)
	    return OPCODE_MADD_S;
	  if (Field_op2_Slot_inst_get (insn) == 5)
	    return OPCODE_MSUB_S;
	  if (Field_op2_Slot_inst_get (insn) == 6)
	    return OPCODE_MADDN_S;
	  if (Field_op2_Slot_inst_get (insn) == 7)
	    return OPCODE_DIVN_S;
	  if (Field_op2_Slot_inst_get (insn) == 8)
	    return OPCODE_ROUND_S;
	  if (Field_op2_Slot_inst_get (insn) == 9)
	    return OPCODE_TRUNC_S;
	  if (Field_op2_Slot_inst_get (insn) == 10)
	    return OPCODE_FLOOR_S;
	  if (Field_op2_Slot_inst_get (insn) == 11)
	    return OPCODE_CEIL_S;
	  if (Field_op2_Slot_inst_get (insn) == 12)
	    return OPCODE_FLOAT_S;
	  if (Field_op2_Slot_inst_get (insn) == 13)
	    return OPCODE_UFLOAT_S;
	  if (Field_op2_Slot_inst_get (insn) == 14)
	    return OPCODE_UTRUNC_S;
	  if (Field_op2_Slot_inst_get (insn) == 15)
	    {
	      if (Field_t_Slot_inst_get (insn) == 0)
		return OPCODE_MOV_S;
	      if (Field_t_Slot_inst_get (insn) == 1)
		return OPCODE_ABS_S;
	      if (Field_t_Slot_inst_get (insn) == 2)
		return OPCODE_CVTD_S;
	      if (Field_t_Slot_inst_get (insn) == 3)
		return OPCODE_CONST_S;
	      if (Field_t_Slot_inst_get (insn) == 4)
		return OPCODE_RFR;
	      if (Field_t_Slot_inst_get (insn) == 5)
		return OPCODE_WFR;
	      if (Field_t_Slot_inst_get (insn) == 6)
		return OPCODE_NEG_S;
	      if (Field_t_Slot_inst_get (insn) == 7)
		return OPCODE_DIV0_S;
	      if (Field_t_Slot_inst_get (insn) == 8)
		return OPCODE_RECIP0_S;
	      if (Field_t_Slot_inst_get (insn) == 9)
		return OPCODE_SQRT0_S;
	      if (Field_t_Slot_inst_get (insn) == 10)
		return OPCODE_RSQRT0_S;
	      if (Field_t_Slot_inst_get (insn) == 11)
		return OPCODE_NEXP01_S;
	      if (Field_t_Slot_inst_get (insn) == 12)
		return OPCODE_MKSADJ_S;
	      if (Field_t_Slot_inst_get (insn) == 13)
		return OPCODE_MKDADJ_S;
	      if (Field_t_Slot_inst_get (insn) == 14)
		return OPCODE_ADDEXP_S;
	      if (Field_t_Slot_inst_get (insn) == 15)
		return OPCODE_ADDEXPM_S;
	    }
	}
      if (Field_op1_Slot_inst_get (insn) == 11)
	{
	  if (Field_op2_Slot_inst_get (insn) == 1)
	    return OPCODE_UN_S;
	  if (Field_op2_Slot_inst_get (insn) == 2)
	    return OPCODE_OEQ_S;
	  if (Field_op2_Slot_inst_get (insn) == 3)
	    return OPCODE_UEQ_S;
	  if (Field_op2_Slot_inst_get (insn) == 4)
	    return OPCODE_OLT_S;
	  if (Field_op2_Slot_inst_get (insn) == 5)
	    return OPCODE_ULT_S;
	  if (Field_op2_Slot_inst_get (insn) == 6)
	    return OPCODE_OLE_S;
	  if (Field_op2_Slot_inst_get (insn) == 7)
	    return OPCODE_ULE_S;
	  if (Field_op2_Slot_inst_get (insn) == 8)
	    return OPCODE_MOVEQZ_S;
	  if (Field_op2_Slot_inst_get (insn) == 9)
	    return OPCODE_MOVNEZ_S;
	  if (Field_op2_Slot_inst_get (insn) == 10)
	    return OPCODE_MOVLTZ_S;
	  if (Field_op2_Slot_inst_get (insn) == 11)
	    return OPCODE_MOVGEZ_S;
	  if (Field_op2_Slot_inst_get (insn) == 12)
	    return OPCODE_MOVF_S;
	  if (Field_op2_Slot_inst_get (insn) == 13)
	    return OPCODE_MOVT_S;
	}
      if (Field_op1_Slot_inst_get (insn) == 14)
	{
	  if (Field_op2_Slot_inst_get (insn) == 1)
	    return OPCODE_UN_D;
	  if (Field_op2_Slot_inst_get (insn) == 2)
	    return OPCODE_OEQ_D;
	  if (Field_op2_Slot_inst_get (insn) == 3)
	    return OPCODE_UEQ_D;
	  if (Field_op2_Slot_inst_get (insn) == 4)
	    return OPCODE_OLT_D;
	  if (Field_op2_Slot_inst_get (insn) == 5)
	    return OPCODE_ULT_D;
	  if (Field_op2_Slot_inst_get (insn) == 6)
	    return OPCODE_OLE_D;
	  if (Field_op2_Slot_inst_get (insn) == 7)
	    return OPCODE_ULE_D;
	  if (Field_op2_Slot_inst_get (insn) == 8)
	    return OPCODE_WFRD;
	}
      if (Field_op1_Slot_inst_get (insn) == 15)
	{
	  if (Field_op2_Slot_inst_get (insn) == 0)
	    return OPCODE_ADD_D;
	  if (Field_op2_Slot_inst_get (insn) == 1)
	    return OPCODE_SUB_D;
	  if (Field_op2_Slot_inst_get (insn) == 2)
	    return OPCODE_MUL_D;
	  if (Field_op2_Slot_inst_get (insn) == 4)
	    return OPCODE_MADD_D;
	  if (Field_op2_Slot_inst_get (insn) == 5)
	    return OPCODE_MSUB_D;
	  if (Field_op2_Slot_inst_get (insn) == 6)
	    return OPCODE_MADDN_D;
	  if (Field_op2_Slot_inst_get (insn) == 7)
	    return OPCODE_DIVN_D;
	  if (Field_op2_Slot_inst_get (insn) == 8)
	    return OPCODE_ROUND_D;
	  if (Field_op2_Slot_inst_get (insn) == 9)
	    return OPCODE_TRUNC_D;
	  if (Field_op2_Slot_inst_get (insn) == 10)
	    return OPCODE_FLOOR_D;
	  if (Field_op2_Slot_inst_get (insn) == 11)
	    return OPCODE_CEIL_D;
	  if (Field_op2_Slot_inst_get (insn) == 12)
	    return OPCODE_FLOAT_D;
	  if (Field_op2_Slot_inst_get (insn) == 13)
	    return OPCODE_UFLOAT_D;
	  if (Field_op2_Slot_inst_get (insn) == 14)
	    return OPCODE_UTRUNC_D;
	  if (Field_op2_Slot_inst_get (insn) == 15)
	    {
	      if (Field_t_Slot_inst_get (insn) == 0)
		return OPCODE_MOV_D;
	      if (Field_t_Slot_inst_get (insn) == 1)
		return OPCODE_ABS_D;
	      if (Field_t_Slot_inst_get (insn) == 2)
		return OPCODE_CVTS_D;
	      if (Field_t_Slot_inst_get (insn) == 3)
		return OPCODE_CONST_D;
	      if (Field_t_Slot_inst_get (insn) == 4)
		return OPCODE_RFRD;
	      if (Field_t_Slot_inst_get (insn) == 6)
		return OPCODE_NEG_D;
	      if (Field_t_Slot_inst_get (insn) == 7)
		return OPCODE_DIV0_D;
	      if (Field_t_Slot_inst_get (insn) == 8)
		return OPCODE_RECIP0_D;
	      if (Field_t_Slot_inst_get (insn) == 9)
		return OPCODE_SQRT0_D;
	      if (Field_t_Slot_inst_get (insn) == 10)
		return OPCODE_RSQRT0_D;
	      if (Field_t_Slot_inst_get (insn) == 11)
		return OPCODE_NEXP01_D;
	      if (Field_t_Slot_inst_get (insn) == 12)
		return OPCODE_MKSADJ_D;
	      if (Field_t_Slot_inst_get (insn) == 13)
		return OPCODE_MKDADJ_D;
	      if (Field_t_Slot_inst_get (insn) == 14)
		return OPCODE_ADDEXP_D;
	      if (Field_t_Slot_inst_get (insn) == 15)
		return OPCODE_ADDEXPM_D;
	    }
	}
      if (Field_r_Slot_inst_get (insn) == 0 &&
	  Field_s_Slot_inst_get (insn) == 0 &&
	  Field_op2_Slot_inst_get (insn) == 0 &&
	  Field_op1_Slot_inst_get (insn) == 14)
	return OPCODE_READ_IMPWIRE;
      if (Field_r_Slot_inst_get (insn) == 1 &&
	  Field_s3to1_Slot_inst_get (insn) == 0 &&
	  Field_op2_Slot_inst_get (insn) == 0 &&
	  Field_op1_Slot_inst_get (insn) == 14)
	return OPCODE_SETB_EXPSTATE;
      if (Field_r_Slot_inst_get (insn) == 1 &&
	  Field_s3to1_Slot_inst_get (insn) == 1 &&
	  Field_op2_Slot_inst_get (insn) == 0 &&
	  Field_op1_Slot_inst_get (insn) == 14)
	return OPCODE_CLRB_EXPSTATE;
      if (Field_r_Slot_inst_get (insn) == 2 &&
	  Field_op2_Slot_inst_get (insn) == 0 &&
	  Field_op1_Slot_inst_get (insn) == 14)
	return OPCODE_WRMSK_EXPSTATE;
    }
  if (Field_op0_Slot_inst_get (insn) == 1)
    return OPCODE_L32R;
  if (Field_op0_Slot_inst_get (insn) == 2)
    {
      if (Field_r_Slot_inst_get (insn) == 0)
	return OPCODE_L8UI;
      if (Field_r_Slot_inst_get (insn) == 1)
	return OPCODE_L16UI;
      if (Field_r_Slot_inst_get (insn) == 2)
	return OPCODE_L32I;
      if (Field_r_Slot_inst_get (insn) == 4)
	return OPCODE_S8I;
      if (Field_r_Slot_inst_get (insn) == 5)
	return OPCODE_S16I;
      if (Field_r_Slot_inst_get (insn) == 6)
	return OPCODE_S32I;
      if (Field_r_Slot_inst_get (insn) == 7)
	{
	  if (Field_t_Slot_inst_get (insn) == 0)
	    return OPCODE_DPFR;
	  if (Field_t_Slot_inst_get (insn) == 1)
	    return OPCODE_DPFW;
	  if (Field_t_Slot_inst_get (insn) == 2)
	    return OPCODE_DPFRO;
	  if (Field_t_Slot_inst_get (insn) == 3)
	    return OPCODE_DPFWO;
	  if (Field_t_Slot_inst_get (insn) == 4)
	    return OPCODE_DHWB;
	  if (Field_t_Slot_inst_get (insn) == 5)
	    return OPCODE_DHWBI;
	  if (Field_t_Slot_inst_get (insn) == 6)
	    return OPCODE_DHI;
	  if (Field_t_Slot_inst_get (insn) == 7)
	    return OPCODE_DII;
	  if (Field_t_Slot_inst_get (insn) == 8)
	    {
	      if (Field_op1_Slot_inst_get (insn) == 0)
		return OPCODE_DPFL;
	      if (Field_op1_Slot_inst_get (insn) == 2)
		return OPCODE_DHU;
	      if (Field_op1_Slot_inst_get (insn) == 3)
		return OPCODE_DIU;
	      if (Field_op1_Slot_inst_get (insn) == 4)
		return OPCODE_DIWB;
	      if (Field_op1_Slot_inst_get (insn) == 5)
		return OPCODE_DIWBI;
	      if (Field_op1_Slot_inst_get (insn) == 15 &&
		  Field_op2_Slot_inst_get (insn) == 0)
		return OPCODE_DIWBUI_P;
	    }
	  if (Field_t_Slot_inst_get (insn) == 12)
	    return OPCODE_IPF;
	  if (Field_t_Slot_inst_get (insn) == 13)
	    {
	      if (Field_op1_Slot_inst_get (insn) == 0)
		return OPCODE_IPFL;
	      if (Field_op1_Slot_inst_get (insn) == 2)
		return OPCODE_IHU;
	      if (Field_op1_Slot_inst_get (insn) == 3)
		return OPCODE_IIU;
	    }
	  if (Field_t_Slot_inst_get (insn) == 14)
	    return OPCODE_IHI;
	  if (Field_t_Slot_inst_get (insn) == 15)
	    return OPCODE_III;
	}
      if (Field_r_Slot_inst_get (insn) == 9)
	return OPCODE_L16SI;
      if (Field_r_Slot_inst_get (insn) == 10)
	return OPCODE_MOVI;
      if (Field_r_Slot_inst_get (insn) == 11)
	return OPCODE_L32AI;
      if (Field_r_Slot_inst_get (insn) == 12)
	return OPCODE_ADDI;
      if (Field_r_Slot_inst_get (insn) == 13)
	return OPCODE_ADDMI;
      if (Field_r_Slot_inst_get (insn) == 14)
	return OPCODE_S32C1I;
      if (Field_r_Slot_inst_get (insn) == 15)
	return OPCODE_S32RI;
    }
  if (Field_op0_Slot_inst_get (insn) == 3)
    {
      if (Field_r_Slot_inst_get (insn) == 0)
	return OPCODE_LSI;
      if (Field_r_Slot_inst_get (insn) == 1)
	return OPCODE_LDI;
      if (Field_r_Slot_inst_get (insn) == 4)
	return OPCODE_SSI;
      if (Field_r_Slot_inst_get (insn) == 5)
	return OPCODE_SDI;
      if (Field_r_Slot_inst_get (insn) == 8)
	return OPCODE_LSIP;
      if (Field_r_Slot_inst_get (insn) == 9)
	return OPCODE_LDIP;
      if (Field_r_Slot_inst_get (insn) == 12)
	return OPCODE_SSIP;
      if (Field_r_Slot_inst_get (insn) == 13)
	return OPCODE_SDIP;
    }
  if (Field_op0_Slot_inst_get (insn) == 4)
    {
      if (Field_op2_Slot_inst_get (insn) == 0)
	{
	  if (Field_op1_Slot_inst_get (insn) == 8 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DD_LL_LDINC;
	  if (Field_op1_Slot_inst_get (insn) == 9 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DD_HL_LDINC;
	  if (Field_op1_Slot_inst_get (insn) == 10 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DD_LH_LDINC;
	  if (Field_op1_Slot_inst_get (insn) == 11 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DD_HH_LDINC;
	}
      if (Field_op2_Slot_inst_get (insn) == 1)
	{
	  if (Field_op1_Slot_inst_get (insn) == 8 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DD_LL_LDDEC;
	  if (Field_op1_Slot_inst_get (insn) == 9 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DD_HL_LDDEC;
	  if (Field_op1_Slot_inst_get (insn) == 10 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DD_LH_LDDEC;
	  if (Field_op1_Slot_inst_get (insn) == 11 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DD_HH_LDDEC;
	}
      if (Field_op2_Slot_inst_get (insn) == 2)
	{
	  if (Field_op1_Slot_inst_get (insn) == 4 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MUL_DD_LL;
	  if (Field_op1_Slot_inst_get (insn) == 5 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MUL_DD_HL;
	  if (Field_op1_Slot_inst_get (insn) == 6 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MUL_DD_LH;
	  if (Field_op1_Slot_inst_get (insn) == 7 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MUL_DD_HH;
	  if (Field_op1_Slot_inst_get (insn) == 8 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DD_LL;
	  if (Field_op1_Slot_inst_get (insn) == 9 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DD_HL;
	  if (Field_op1_Slot_inst_get (insn) == 10 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DD_LH;
	  if (Field_op1_Slot_inst_get (insn) == 11 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DD_HH;
	  if (Field_op1_Slot_inst_get (insn) == 12 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MULS_DD_LL;
	  if (Field_op1_Slot_inst_get (insn) == 13 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MULS_DD_HL;
	  if (Field_op1_Slot_inst_get (insn) == 14 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MULS_DD_LH;
	  if (Field_op1_Slot_inst_get (insn) == 15 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MULS_DD_HH;
	}
      if (Field_op2_Slot_inst_get (insn) == 3)
	{
	  if (Field_op1_Slot_inst_get (insn) == 4 &&
	      Field_r_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MUL_AD_LL;
	  if (Field_op1_Slot_inst_get (insn) == 5 &&
	      Field_r_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MUL_AD_HL;
	  if (Field_op1_Slot_inst_get (insn) == 6 &&
	      Field_r_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MUL_AD_LH;
	  if (Field_op1_Slot_inst_get (insn) == 7 &&
	      Field_r_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MUL_AD_HH;
	  if (Field_op1_Slot_inst_get (insn) == 8 &&
	      Field_r_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_AD_LL;
	  if (Field_op1_Slot_inst_get (insn) == 9 &&
	      Field_r_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_AD_HL;
	  if (Field_op1_Slot_inst_get (insn) == 10 &&
	      Field_r_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_AD_LH;
	  if (Field_op1_Slot_inst_get (insn) == 11 &&
	      Field_r_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_AD_HH;
	  if (Field_op1_Slot_inst_get (insn) == 12 &&
	      Field_r_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MULS_AD_LL;
	  if (Field_op1_Slot_inst_get (insn) == 13 &&
	      Field_r_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MULS_AD_HL;
	  if (Field_op1_Slot_inst_get (insn) == 14 &&
	      Field_r_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MULS_AD_LH;
	  if (Field_op1_Slot_inst_get (insn) == 15 &&
	      Field_r_Slot_inst_get (insn) == 0 &&
	      Field_t3_Slot_inst_get (insn) == 0 &&
	      Field_tlo_Slot_inst_get (insn) == 0)
	    return OPCODE_MULS_AD_HH;
	}
      if (Field_op2_Slot_inst_get (insn) == 4)
	{
	  if (Field_op1_Slot_inst_get (insn) == 8 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DA_LL_LDINC;
	  if (Field_op1_Slot_inst_get (insn) == 9 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DA_HL_LDINC;
	  if (Field_op1_Slot_inst_get (insn) == 10 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DA_LH_LDINC;
	  if (Field_op1_Slot_inst_get (insn) == 11 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DA_HH_LDINC;
	}
      if (Field_op2_Slot_inst_get (insn) == 5)
	{
	  if (Field_op1_Slot_inst_get (insn) == 8 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DA_LL_LDDEC;
	  if (Field_op1_Slot_inst_get (insn) == 9 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DA_HL_LDDEC;
	  if (Field_op1_Slot_inst_get (insn) == 10 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DA_LH_LDDEC;
	  if (Field_op1_Slot_inst_get (insn) == 11 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DA_HH_LDDEC;
	}
      if (Field_op2_Slot_inst_get (insn) == 6)
	{
	  if (Field_op1_Slot_inst_get (insn) == 4 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MUL_DA_LL;
	  if (Field_op1_Slot_inst_get (insn) == 5 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MUL_DA_HL;
	  if (Field_op1_Slot_inst_get (insn) == 6 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MUL_DA_LH;
	  if (Field_op1_Slot_inst_get (insn) == 7 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MUL_DA_HH;
	  if (Field_op1_Slot_inst_get (insn) == 8 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DA_LL;
	  if (Field_op1_Slot_inst_get (insn) == 9 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DA_HL;
	  if (Field_op1_Slot_inst_get (insn) == 10 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DA_LH;
	  if (Field_op1_Slot_inst_get (insn) == 11 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_DA_HH;
	  if (Field_op1_Slot_inst_get (insn) == 12 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULS_DA_LL;
	  if (Field_op1_Slot_inst_get (insn) == 13 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULS_DA_HL;
	  if (Field_op1_Slot_inst_get (insn) == 14 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULS_DA_LH;
	  if (Field_op1_Slot_inst_get (insn) == 15 &&
	      Field_s_Slot_inst_get (insn) == 0 &&
	      Field_w_Slot_inst_get (insn) == 0 &&
	      Field_r3_Slot_inst_get (insn) == 0)
	    return OPCODE_MULS_DA_HH;
	}
      if (Field_op2_Slot_inst_get (insn) == 7)
	{
	  if (Field_op1_Slot_inst_get (insn) == 0 &&
	      Field_r_Slot_inst_get (insn) == 0)
	    return OPCODE_UMUL_AA_LL;
	  if (Field_op1_Slot_inst_get (insn) == 1 &&
	      Field_r_Slot_inst_get (insn) == 0)
	    return OPCODE_UMUL_AA_HL;
	  if (Field_op1_Slot_inst_get (insn) == 2 &&
	      Field_r_Slot_inst_get (insn) == 0)
	    return OPCODE_UMUL_AA_LH;
	  if (Field_op1_Slot_inst_get (insn) == 3 &&
	      Field_r_Slot_inst_get (insn) == 0)
	    return OPCODE_UMUL_AA_HH;
	  if (Field_op1_Slot_inst_get (insn) == 4 &&
	      Field_r_Slot_inst_get (insn) == 0)
	    return OPCODE_MUL_AA_LL;
	  if (Field_op1_Slot_inst_get (insn) == 5 &&
	      Field_r_Slot_inst_get (insn) == 0)
	    return OPCODE_MUL_AA_HL;
	  if (Field_op1_Slot_inst_get (insn) == 6 &&
	      Field_r_Slot_inst_get (insn) == 0)
	    return OPCODE_MUL_AA_LH;
	  if (Field_op1_Slot_inst_get (insn) == 7 &&
	      Field_r_Slot_inst_get (insn) == 0)
	    return OPCODE_MUL_AA_HH;
	  if (Field_op1_Slot_inst_get (insn) == 8 &&
	      Field_r_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_AA_LL;
	  if (Field_op1_Slot_inst_get (insn) == 9 &&
	      Field_r_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_AA_HL;
	  if (Field_op1_Slot_inst_get (insn) == 10 &&
	      Field_r_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_AA_LH;
	  if (Field_op1_Slot_inst_get (insn) == 11 &&
	      Field_r_Slot_inst_get (insn) == 0)
	    return OPCODE_MULA_AA_HH;
	  if (Field_op1_Slot_inst_get (insn) == 12 &&
	      Field_r_Slot_inst_get (insn) == 0)
	    return OPCODE_MULS_AA_LL;
	  if (Field_op1_Slot_inst_get (insn) == 13 &&
	      Field_r_Slot_inst_get (insn) == 0)
	    return OPCODE_MULS_AA_HL;
	  if (Field_op1_Slot_inst_get (insn) == 14 &&
	      Field_r_Slot_inst_get (insn) == 0)
	    return OPCODE_MULS_AA_LH;
	  if (Field_op1_Slot_inst_get (insn) == 15 &&
	      Field_r_Slot_inst_get (insn) == 0)
	    return OPCODE_MULS_AA_HH;
	}
      if (Field_op2_Slot_inst_get (insn) == 8)
	{
	  if (Field_op1_Slot_inst_get (insn) == 0 &&
	      Field_t_Slot_inst_get (insn) == 0 &&
	      Field_rhi_Slot_inst_get (insn) == 0)
	    return OPCODE_LDINC;
	}
      if (Field_op2_Slot_inst_get (insn) == 9)
	{
	  if (Field_op1_Slot_inst_get (insn) == 0 &&
	      Field_t_Slot_inst_get (insn) == 0 &&
	      Field_rhi_Slot_inst_get (insn) == 0)
	    return OPCODE_LDDEC;
	}
    }
  if (Field_op0_Slot_inst_get (insn) == 5)
    {
      if (Field_n_Slot_inst_get (insn) == 0)
	return OPCODE_CALL0;
      if (Field_n_Slot_inst_get (insn) == 1)
	return OPCODE_CALL4;
      if (Field_n_Slot_inst_get (insn) == 2)
	return OPCODE_CALL8;
      if (Field_n_Slot_inst_get (insn) == 3)
	return OPCODE_CALL12;
    }
  if (Field_op0_Slot_inst_get (insn) == 6)
    {
      if (Field_n_Slot_inst_get (insn) == 0)
	return OPCODE_J;
      if (Field_n_Slot_inst_get (insn) == 1)
	{
	  if (Field_m_Slot_inst_get (insn) == 0)
	    return OPCODE_BEQZ;
	  if (Field_m_Slot_inst_get (insn) == 1)
	    return OPCODE_BNEZ;
	  if (Field_m_Slot_inst_get (insn) == 2)
	    return OPCODE_BLTZ;
	  if (Field_m_Slot_inst_get (insn) == 3)
	    return OPCODE_BGEZ;
	}
      if (Field_n_Slot_inst_get (insn) == 2)
	{
	  if (Field_m_Slot_inst_get (insn) == 0)
	    return OPCODE_BEQI;
	  if (Field_m_Slot_inst_get (insn) == 1)
	    return OPCODE_BNEI;
	  if (Field_m_Slot_inst_get (insn) == 2)
	    return OPCODE_BLTI;
	  if (Field_m_Slot_inst_get (insn) == 3)
	    return OPCODE_BGEI;
	}
      if (Field_n_Slot_inst_get (insn) == 3)
	{
	  if (Field_m_Slot_inst_get (insn) == 0)
	    return OPCODE_ENTRY;
	  if (Field_m_Slot_inst_get (insn) == 1)
	    {
	      if (Field_r_Slot_inst_get (insn) == 0)
		return OPCODE_BF;
	      if (Field_r_Slot_inst_get (insn) == 1)
		return OPCODE_BT;
	      if (Field_r_Slot_inst_get (insn) == 8)
		return OPCODE_LOOP;
	      if (Field_r_Slot_inst_get (insn) == 9)
		return OPCODE_LOOPNEZ;
	      if (Field_r_Slot_inst_get (insn) == 10)
		return OPCODE_LOOPGTZ;
	    }
	  if (Field_m_Slot_inst_get (insn) == 2)
	    return OPCODE_BLTUI;
	  if (Field_m_Slot_inst_get (insn) == 3)
	    return OPCODE_BGEUI;
	}
    }
  if (Field_op0_Slot_inst_get (insn) == 7)
    {
      if (Field_r_Slot_inst_get (insn) == 0)
	return OPCODE_BNONE;
      if (Field_r_Slot_inst_get (insn) == 1)
	return OPCODE_BEQ;
      if (Field_r_Slot_inst_get (insn) == 2)
	return OPCODE_BLT;
      if (Field_r_Slot_inst_get (insn) == 3)
	return OPCODE_BLTU;
      if (Field_r_Slot_inst_get (insn) == 4)
	return OPCODE_BALL;
      if (Field_r_Slot_inst_get (insn) == 5)
	return OPCODE_BBC;
      if ((Field_r_Slot_inst_get (insn) == 6 ||
	   Field_r_Slot_inst_get (insn) == 7))
	return OPCODE_BBCI;
      if (Field_r_Slot_inst_get (insn) == 8)
	return OPCODE_BANY;
      if (Field_r_Slot_inst_get (insn) == 9)
	return OPCODE_BNE;
      if (Field_r_Slot_inst_get (insn) == 10)
	return OPCODE_BGE;
      if (Field_r_Slot_inst_get (insn) == 11)
	return OPCODE_BGEU;
      if (Field_r_Slot_inst_get (insn) == 12)
	return OPCODE_BNALL;
      if (Field_r_Slot_inst_get (insn) == 13)
	return OPCODE_BBS;
      if ((Field_r_Slot_inst_get (insn) == 14 ||
	   Field_r_Slot_inst_get (insn) == 15))
	return OPCODE_BBSI;
    }
  return XTENSA_UNDEFINED;
}

static int
Slot_inst16b_decode (const xtensa_insnbuf insn)
{
  if (Field_op0_Slot_inst16b_get (insn) == 12)
    {
      if (Field_i_Slot_inst16b_get (insn) == 0)
	return OPCODE_MOVI_N;
      if (Field_i_Slot_inst16b_get (insn) == 1)
	{
	  if (Field_z_Slot_inst16b_get (insn) == 0)
	    return OPCODE_BEQZ_N;
	  if (Field_z_Slot_inst16b_get (insn) == 1)
	    return OPCODE_BNEZ_N;
	}
    }
  if (Field_op0_Slot_inst16b_get (insn) == 13)
    {
      if (Field_r_Slot_inst16b_get (insn) == 0)
	return OPCODE_MOV_N;
      if (Field_r_Slot_inst16b_get (insn) == 15)
	{
	  if (Field_t_Slot_inst16b_get (insn) == 0)
	    return OPCODE_RET_N;
	  if (Field_t_Slot_inst16b_get (insn) == 1)
	    return OPCODE_RETW_N;
	  if (Field_t_Slot_inst16b_get (insn) == 2)
	    return OPCODE_BREAK_N;
	  if (Field_t_Slot_inst16b_get (insn) == 3 &&
	      Field_s_Slot_inst16b_get (insn) == 0)
	    return OPCODE_NOP_N;
	  if (Field_t_Slot_inst16b_get (insn) == 6 &&
	      Field_s_Slot_inst16b_get (insn) == 0)
	    return OPCODE_ILL_N;
	}
    }
  return XTENSA_UNDEFINED;
}

static int
Slot_inst16a_decode (const xtensa_insnbuf insn)
{
  if (Field_op0_Slot_inst16a_get (insn) == 8)
    return OPCODE_L32I_N;
  if (Field_op0_Slot_inst16a_get (insn) == 9)
    return OPCODE_S32I_N;
  if (Field_op0_Slot_inst16a_get (insn) == 10)
    return OPCODE_ADD_N;
  if (Field_op0_Slot_inst16a_get (insn) == 11)
    return OPCODE_ADDI_N;
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
  Field_s8_Slot_inst_get,
  Field_imms8_Slot_inst_get,
  Field_imm12b_Slot_inst_get,
  Field_imm16_Slot_inst_get,
  Field_m_Slot_inst_get,
  Field_n_Slot_inst_get,
  Field_offset_Slot_inst_get,
  Field_op0_Slot_inst_get,
  Field_op1_Slot_inst_get,
  Field_op2_Slot_inst_get,
  Field_r_Slot_inst_get,
  Field_r_disp_Slot_inst_get,
  Field_r_3_Slot_inst_get,
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
  Field_t2_Slot_inst_get,
  Field_s2_Slot_inst_get,
  Field_r2_Slot_inst_get,
  Field_t4_Slot_inst_get,
  Field_s4_Slot_inst_get,
  Field_r4_Slot_inst_get,
  Field_t8_Slot_inst_get,
  Field_r8_Slot_inst_get,
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
  Implicit_Field_mr3_get,
  Implicit_Field_bt16_get,
  Implicit_Field_bs16_get,
  Implicit_Field_br16_get,
  Implicit_Field_brall_get
};

static xtensa_set_field_fn
Slot_inst_set_field_fns[] = {
  Field_t_Slot_inst_set,
  Field_bbi4_Slot_inst_set,
  Field_bbi_Slot_inst_set,
  Field_imm12_Slot_inst_set,
  Field_imm8_Slot_inst_set,
  Field_s_Slot_inst_set,
  Field_s8_Slot_inst_set,
  Field_imms8_Slot_inst_set,
  Field_imm12b_Slot_inst_set,
  Field_imm16_Slot_inst_set,
  Field_m_Slot_inst_set,
  Field_n_Slot_inst_set,
  Field_offset_Slot_inst_set,
  Field_op0_Slot_inst_set,
  Field_op1_Slot_inst_set,
  Field_op2_Slot_inst_set,
  Field_r_Slot_inst_set,
  Field_r_disp_Slot_inst_set,
  Field_r_3_Slot_inst_set,
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
  Field_t2_Slot_inst_set,
  Field_s2_Slot_inst_set,
  Field_r2_Slot_inst_set,
  Field_t4_Slot_inst_set,
  Field_s4_Slot_inst_set,
  Field_r4_Slot_inst_set,
  Field_t8_Slot_inst_set,
  Field_r8_Slot_inst_set,
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
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  Field_i_Slot_inst16a_get,
  Field_imm6lo_Slot_inst16a_get,
  Field_imm6hi_Slot_inst16a_get,
  Field_imm7lo_Slot_inst16a_get,
  Field_imm7hi_Slot_inst16a_get,
  Field_z_Slot_inst16a_get,
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
  0,
  Implicit_Field_ar0_get,
  Implicit_Field_ar4_get,
  Implicit_Field_ar8_get,
  Implicit_Field_ar12_get,
  Implicit_Field_mr0_get,
  Implicit_Field_mr1_get,
  Implicit_Field_mr2_get,
  Implicit_Field_mr3_get,
  Implicit_Field_bt16_get,
  Implicit_Field_bs16_get,
  Implicit_Field_br16_get,
  Implicit_Field_brall_get
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
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  Field_i_Slot_inst16a_set,
  Field_imm6lo_Slot_inst16a_set,
  Field_imm6hi_Slot_inst16a_set,
  Field_imm7lo_Slot_inst16a_set,
  Field_imm7hi_Slot_inst16a_set,
  Field_z_Slot_inst16a_set,
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
  0,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
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
  0,
  0,
  0,
  0,
  0,
  0,
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
  Implicit_Field_ar0_get,
  Implicit_Field_ar4_get,
  Implicit_Field_ar8_get,
  Implicit_Field_ar12_get,
  Implicit_Field_mr0_get,
  Implicit_Field_mr1_get,
  Implicit_Field_mr2_get,
  Implicit_Field_mr3_get,
  Implicit_Field_bt16_get,
  Implicit_Field_bs16_get,
  Implicit_Field_br16_get,
  Implicit_Field_brall_get
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
  0,
  0,
  0,
  0,
  0,
  0,
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
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
  Implicit_Field_set,
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

static int length_table[256] = {
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
  -1,
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
  -1,
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
  -1,
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
  -1,
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
  -1,
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
  -1,
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
  -1,
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
  -1,
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
  -1,
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
  -1,
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
  -1,
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
  -1,
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
  -1,
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
  -1,
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
  -1,
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
  int l = insn[0];
  return length_table[l];
}


/* Top-level ISA structure.  */

xtensa_isa_internal xtensa_modules = {
  0 /* little-endian */,
  3 /* insn_size */, 0,
  3, formats, format_decoder, length_decoder,
  3, slots,
  72 /* num_fields */,
  132, operands,
  444, iclasses,
  579, opcodes, 0,
  8, regfiles,
  NUM_STATES, states, 0,
  NUM_SYSREGS, sysregs, 0,
  { MAX_SPECIAL_REG, MAX_USER_REG }, { 0, 0 },
  1, interfaces, 0,
  1, funcUnits, 0
};
