/*
 * Disassemble C-SKY instructions
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

#include "qemu/osdep.h"
#include "disas/bfd.h"

/* sourced by binutils-2.20/bfd/bfd-in2.h */
/* architecture */
#define M_CK510   (1 << 0)
#define M_CK610   (1 << 1)
#define M_CK803   (1 << 2)
#define M_CK810   (1 << 3)
/* module */
#define M_DSP     (1 << 14)
#define M_MAC     (1 << 15)  /* security */
#define M_FLOAT   (1 << 16)
#define M_SIMD    (1 << 17)
#define M_CP      (1 << 18)
#define M_MP      (1 << 19)
#define M_CACHE   (1 << 20)

#define bfd_mach_ck510     (M_CK510)
#define bfd_mach_ck510e    (bfd_mach_ck510 | M_DSP)
#define bfd_mach_ck520     (bfd_mach_ck510 | M_MAC)
#define bfd_mach_ck610     (M_CK610)
#define bfd_mach_ck610e    (bfd_mach_ck610 | M_DSP)
#define bfd_mach_ck610f    (bfd_mach_ck610 | M_FLOAT)
#define bfd_mach_ck610ef   (bfd_mach_ck610 | M_DSP | M_FLOAT)

#define bfd_mach_ck803     (M_CK803)
#define bfd_mach_ck803e    (M_CK803 | M_DSP)
#define bfd_mach_ck810     (M_CK810)
#define bfd_mach_ck810e    (M_CK810 | M_DSP)
#define bfd_mach_ck810f    (M_CK810 | M_FLOAT)
#define bfd_mach_ck810ef   (M_CK810 | M_SIMD | M_FLOAT)

/*
 * CKCORE Instruction Class
 */
typedef enum {
    O0,    OT,   O1,   OC,   O2,    X1,    OI,    OB,
    OMa,   SI,   I7,   LS,   BR,    BL,    LR,    LJ,
    RM,    RQ,   JSR,  JMP,  OBRa,  OBRb,  OBRc,  OBR2,
    O1R1,  OMb,  OMc,  SIa,
    MULSH, OPSR,
    O1_CP, O2_CPGR, O2_CPCR,
    O_KWGJ1, O_KWGJ2, /* For CAA */
    JC,    JU,   JL,   RSI,  DO21,  OB2,
    O1_E2, O1_E, O2_E, OI_E, OB_E, O1R1_E, SIa_E
} ckcore_opclass;

/*
 * CKCORE instruction information & opcode.
 */
typedef struct inst {
    const char *name;
    /* the instruction class */
    ckcore_opclass  opclass;
    unsigned char  transfer;
    /* The instruction opcode */
    unsigned short inst;
    /*
     * One bit cpu_flags for the opcode.  These are used to indicate which
     * specific processors support the instructions.  The values are the
     * same as those for the struct powerpc_opcode flags field.
     */
    unsigned long cpu_flag;
} ckcore_opcode_info;


static ckcore_opcode_info ckcore_table[] = {
    { "bkpt",  O0, 0, 0x0000, M_CK510 | M_CK610 },
    { "sync",  O0, 0, 0x0001, M_CK510 | M_CK610 },
    { "rte",   O0, 2, 0x0002, M_CK510 | M_CK610 },
    { "rfe",   O0, 2, 0x0002, M_CK510 | M_CK610 },
    { "rfi",   O0, 2, 0x0003, M_CK510 | M_CK610 },
    { "stop",  O0, 0, 0x0004, M_CK510 | M_CK610 },
    { "wait",  O0, 0, 0x0005, M_CK510 | M_CK610 },
    { "doze",  O0, 0, 0x0006, M_CK510 | M_CK610 },
    { "idly4", O0, 0, 0x0007, M_CK510 | M_CK610 },
    { "trap",  OT, 0, 0x0008, M_CK510 | M_CK610 },
    /* DSP/Coprocessor */
    { "mvtc", O0, 0, 0x000C, M_DSP },
    { "cprc", O0,   0, 0x000D, M_CK610 | M_CP },
    /* SPACE:        0x000E - 0x000F */
    { "cpseti", O1_CP, 0, 0x0010, M_CK610 | M_CP },
    { "mvc",  O1, 0, 0x0020, M_CK510 | M_CK610 },
    { "mvcv", O1, 0, 0x0030, M_CK510 | M_CK610 },
    { "ldq",  RQ, 0, 0x0040, M_CK510 | M_CK610 },
    { "stq",  RQ, 0, 0x0050, M_CK510 | M_CK610 },
    { "ldm",  RM, 0, 0x0060, M_CK510 | M_CK610 },
    { "stm",  RM, 0, 0x0070, M_CK510 | M_CK610 },
    { "dect", O1_E2, 0, 0x0080, M_CK510 | M_CK610 },
    { "decf", O1_E2, 0, 0x0090, M_CK510 | M_CK610 },
    { "inct", O1_E2, 0, 0x00A0, M_CK510 | M_CK610 },
    { "incf", O1_E2, 0, 0x00B0, M_CK510 | M_CK610 },
    { "jmp", JMP, 2, 0x00C0, M_CK510 | M_CK610 },
#define ckcore_INST_JMP 0x00C0
    { "jsr", JSR, 0, 0x00D0, M_CK510 | M_CK610 },
#define ckcore_INST_JSR 0x00E0
    { "ff1",    O1_E, 0, 0x00E0, M_CK510 | M_CK610 },
    { "brev",   O1_E, 0, 0x00F0, M_CK510 | M_CK610 },
    { "xtrb3",  X1,   0, 0x0100, M_CK510 | M_CK610 },
    { "xtrb2",  X1,   0, 0x0110, M_CK510 | M_CK610 },
    { "xtrb1",  X1,   0, 0x0120, M_CK510 | M_CK610 },
    { "xtrb0",  X1,   0, 0x0130, M_CK510 | M_CK610 },
    { "zextb",  O1_E, 0, 0x0140, M_CK510 | M_CK610 },
    { "sextb",  O1_E, 0, 0x0150, M_CK510 | M_CK610 },
    { "zexth",  O1_E, 0, 0x0160, M_CK510 | M_CK610 },
    { "sexth",  O1_E, 0, 0x0170, M_CK510 | M_CK610 },
    { "declt",  O1_E2, 0, 0x0180, M_CK510 | M_CK610 },
    { "tstnbz", O1,    0, 0x0190, M_CK510 | M_CK610 },
    { "decgt",  O1_E2, 0, 0x01A0, M_CK510 | M_CK610 },
    { "decne",  O1_E2, 0, 0x01B0, M_CK510 | M_CK610 },
    { "clrt",   O1,    0, 0x01C0, M_CK510 | M_CK610 },
    { "clrf",   O1,    0, 0x01D0, M_CK510 | M_CK610 },
    { "abs",    O1_E,  0, 0x01E0, M_CK510 | M_CK610 },
    { "not",    O1_E,  0, 0x01F0, M_CK510 | M_CK610 },
    { "movt",   O2,    0, 0x0200, M_CK510 | M_CK610 },
    { "mult",   O2_E,  0, 0x0300, M_CK510 | M_CK610 },
    { "mac",    O2,    0, 0x0400, M_MAC},
    { "subu",   O2_E, 0, 0x0500, M_CK510 | M_CK610 },
    { "sub",    O2_E, 0, 0x0500, M_CK510 | M_CK610 }, /* Official alias.  */
    { "addc",   O2_E, 0, 0x0600, M_CK510 | M_CK610 },
    { "subc",   O2_E, 0, 0x0700, M_CK510 | M_CK610 },
    /*
     * SPACE: 0x0800-0x09ff for a diadic operation,
     *   Used now by "xnor/xadd" and "cprgr" overlapped,
     *   but they are not existed in same time.
     */
    /* CK610 Coprocessor Instruction*/
    { "cprgr", O2_CPGR, 0, 0x0800, M_CK610 | M_CP },

    { "movf",  O2, 0, 0x0A00, M_CK510 | M_CK610 },
    { "lsr",   O2_E, 0, 0x0B00, M_CK510 | M_CK610 },
    { "cmphs", O2, 0, 0x0C00, M_CK510 | M_CK610 },
    { "cmplt", O2, 0, 0x0D00, M_CK510 | M_CK610 },
    { "tst",   O2, 0, 0x0E00, M_CK510 | M_CK610 },
    { "cmpne", O2, 0, 0x0F00, M_CK510 | M_CK610 },
    /*
     *  We must search psrclr or psrset before mfcr,
     *  becase psrclr or psrset is a subcollection.
     *  Or we will get mfcr instruction when disassemble
     *  psrclr or psrset.
     *  Modified by Li Chunqiang (chunqiang_li@c-sky.com)
     */
    { "psrclr", OPSR, 0, 0x11F0, M_CK510 | M_CK610 },
    { "psrset", OPSR, 0, 0x11F8, M_CK510 | M_CK610 },
    { "mfcr",   OC,   0, 0x1000, M_CK510 | M_CK610 },

    { "mov",    O2,   0, 0x1200, M_CK510 | M_CK610 },
    { "bgenr",  O2,   0, 0x1300, M_CK510 | M_CK610 },
    { "rsub",   O2_E,   0, 0x1400, M_CK510 | M_CK610 },
    { "ixw",    O2_E,   0, 0x1500, M_CK510 | M_CK610 },
    { "and",    O2_E,   0, 0x1600, M_CK510 | M_CK610 },
    { "xor",    O2_E,   0, 0x1700, M_CK510 | M_CK610 },
    { "mtcr",   OC,   0, 0x1800, M_CK510 | M_CK610 },
    { "asr",    O2_E,   0, 0x1A00, M_CK510 | M_CK610 },
    { "lsl",    O2_E,   0, 0x1B00, M_CK510 | M_CK610 },
    { "addu",   O2_E,   0, 0x1C00, M_CK510 | M_CK610 },
    { "add",    O2,   0, 0x1C00, M_CK510 | M_CK610 }, /* Official alias.  */
#define ckcore_INST_ADDU    0x1C00
    { "ixh",    O2_E,   0, 0x1D00, M_CK510 | M_CK610 },
    { "or",     O2_E,   0, 0x1E00, M_CK510 | M_CK610 },
    { "andn",   O2_E,   0, 0x1F00, M_CK510 | M_CK610 },
    { "addi",   OI_E,   0, 0x2000, M_CK510 | M_CK610 },
#define ckcore_INST_ADDI    0x2000
    { "cmplti", OI,   0, 0x2200, M_CK510 | M_CK610 },
    { "subi",   OI_E,   0, 0x2400, M_CK510 | M_CK610 }, /* 0x2400 ~ 0x25ff */
#define ckcore_INST_SUBI    0x2400
    /*
     * SPACE: 0x2600-0x27ff,
     *   open for a register+immediate  operation,
     *   Used now by "perm/rxor" and "cpwgr" are overlapped,
     *   but they are not existed in the same time.
     */
    /* CK610 Coprocessor instructions */
    { "cpwgr",  O2_CPGR, 0, 0x2600, M_CK610 | M_CP },

    { "rsubi",  OB_E,   0, 0x2800, M_CK510 | M_CK610 },
    { "cmpnei", OB,   0, 0x2A00, M_CK510 | M_CK610 },
    { "bmaski", OMa,  0, 0x2C00, M_CK510 | M_CK610 },
    { "divu",   O1R1_E, 0, 0x2C10, M_CK510 | M_CK610 },

    /*
     * SPACE: 0x2C20-0x2C3f
     *   Used  by DSP, but they are not existed in
     *   the same time.
     */
    /* DSP instructions */
    { "mflos",  O1,   0, 0x2C20, M_DSP },
    { "mfhis",  O1,   0, 0x2C30, M_DSP },

    { "mtlo",   O1,   0, 0x2C40, M_DSP},
    { "mthi",   O1,   0, 0x2C50, M_DSP},
    { "mflo",   O1,   0, 0x2C60, M_DSP},
    { "mfhi",   O1,   0, 0x2C70, M_DSP},

    { "bmaski", OMb,  0, 0x2C80, M_CK510 | M_CK610 },
    { "bmaski", OMc,  0, 0x2D00, M_CK510 | M_CK610 },
    { "andi",   OB_E,   0, 0x2E00, M_CK510 | M_CK610 },
    { "bclri",  OB_E,   0, 0x3000, M_CK510 | M_CK610 },

    /*
     * SPACE: 0x3200-0x320f
     *   Used  by Coprocessor, but they are not existed in
     *   the same time.
     */
    /* CK610 Coprocessor instructions */
    { "cpwir",  O1,   0, 0x3200, M_CK610 | M_CP },

    { "divs",   O1R1_E, 0, 0x3210, M_CK510 | M_CK610 },
    /*
     * SPACE: 0x3200-0x320f
     *   Used  by Coprocessor, but they are not existed in
     *   the same time.
     */
    /* SPACE:           0x3260 - 0x326f */
    /* CK610 Coprocessor instructions */
    { "cprsr",  O1,   0, 0x3220, M_CK610 | M_CP },
    { "cpwsr",  O1,   0, 0x3230, M_CK610 | M_CP },
    /* SPACE:            0x3240 - 0x326f */

    { "bgeni",  OBRa,  0, 0x3270, M_CK510 | M_CK610 },
    { "bgeni",  OBRb,  0, 0x3280, M_CK510 | M_CK610 },
    { "bgeni",  OBRc,  0, 0x3300, M_CK510 | M_CK610 },
    { "bseti",  OB_E,  0, 0x3400, M_CK510 | M_CK610 },
    { "btsti",  OB,    0, 0x3600, M_CK510 | M_CK610 },
    { "xsr",    O1_E2, 0, 0x3800, M_CK510 | M_CK610 },
    { "rotli",  SIa_E, 0, 0x3800, M_CK510 | M_CK610 },
    { "asrc",   O1_E2, 0, 0x3A00, M_CK510 | M_CK610 },
    { "asri",   SIa_E, 0, 0x3A00, M_CK510 | M_CK610 },
    { "lslc",   O1_E2, 0, 0x3C00, M_CK510 | M_CK610 },
    { "lsli",   SIa_E, 0, 0x3C00, M_CK510 | M_CK610 },
    { "lsrc",   O1_E2, 0, 0x3E00, M_CK510 | M_CK610 },
    { "lsri",   SIa_E, 0, 0x3E00, M_CK510 | M_CK610 },

    { "omflip0", O2, 0, 0x4000, M_MAC },
    { "omflip1", O2, 0, 0x4100, M_MAC },
    { "omflip2", O2, 0, 0x4200, M_MAC },
    { "omflip3", O2, 0, 0x4300, M_MAC },
    { "muls",    O2, 0, 0x5000, M_DSP },
    { "mulsa",   O2, 0, 0x5100, M_DSP },
    { "mulss",   O2, 0, 0x5200, M_DSP },
    /* SPACE:           0x5300 - 0x53FF */
    { "mulu",    O2, 0, 0x5400, M_DSP },
    { "mulua",   O2, 0, 0x5500, M_DSP },
    { "mulus",   O2, 0, 0x5600, M_DSP },
    /* SPACE:           0x5700 - 0x57FF */
    { "vmulsh",  O2, 0, 0x5800, M_DSP },
    { "vmulsha", O2, 0, 0x5900, M_DSP },
    { "vmulshs", O2, 0, 0x5A00, M_DSP },
    /* SPACE:          0x5B00 - 0x5BFF */
    { "vmulsw",  O2, 0, 0x5C00, M_DSP },
    { "vmulswa", O2, 0, 0x5D00, M_DSP },
    { "vmulsws", O2, 0, 0x5E00, M_DSP },
    /* SPACE:           0x5F00 - 0x5FFF */
    /* SPACE:           0x4000 - 0x5fff */
    { "movi",    I7, 0, 0x6000, M_CK510 | M_CK610 },
#define ckcore_INST_BMASKI_ALT  0x6000
#define ckcore_INST_BGENI_ALT   0x6000
    { "mulsh",   O2_E, 0, 0x6800, M_CK510 | M_CK610 },
    { "muls.h",  O2_E, 0, 0x6800, M_CK510 | M_CK610 },

    /*
     * SPACE: 0x6900-0x6fff
     *   Used  by DSP/Coprocessor, but they are not
     *   existed in the same time.
     */
    /* DSP/Coprocessor Instructions */
    { "mulsha",  O2,      0, 0x6900, M_DSP },
    { "mulshs",  O2,      0, 0x6A00, M_DSP },
    { "cprcr",   O2_CPCR, 0, 0x6B00, M_CK610 | M_CP },
    { "mulsw",   O2,      0, 0x6C00, M_DSP },
    { "mulswa",  O2,      0, 0x6D00, M_DSP },
    { "mulsws",  O2,      0, 0x6E00, M_DSP },
    { "cpwcr",   O2_CPCR, 0, 0x6F00, M_CK610 | M_CP },

    /*
     *  We must search jsri/jumpi before lrw,
     *  becase jsri/jumpi is a subcollection.
     *  Or we will get lrw instruction when disassemble
     *  jsri/jumpi.
     */
    { "jmpi", LJ, 1, 0x7000, M_CK510 | M_CK610 },
    { "jsri", LJ, 0, 0x7F00, M_CK510 | M_CK610 },
    { "lrw",  LR, 0, 0x7000, M_CK510 | M_CK610 },
#define ckcore_INST_JMPI    0x7000
#define ckcore_INST_JSRI    0x7F00
#define ckcore_INST_LRW     0x7000
#define ckcore_INST_LDW     0x8000
#define ckcore_INST_STW     0x9000
    { "ld",   LS, 0, 0x8000, M_CK510 | M_CK610 },
    { "ldw",  LS, 0, 0x8000, M_CK510 | M_CK610 },
    { "ld.w", LS, 0, 0x8000, M_CK510 | M_CK610 },
    { "st",   LS, 0, 0x9000, M_CK510 | M_CK610 },
    { "stw",  LS, 0, 0x9000, M_CK510 | M_CK610 },
    { "st.w", LS, 0, 0x9000, M_CK510 | M_CK610 },
    { "ldb",  LS, 0, 0xA000, M_CK510 | M_CK610 },
    { "ld.b", LS, 0, 0xA000, M_CK510 | M_CK610 },
    { "stb",  LS, 0, 0xB000, M_CK510 | M_CK610 },
    { "st.b", LS, 0, 0xB000, M_CK510 | M_CK610 },
    { "ldh",  LS, 0, 0xC000, M_CK510 | M_CK610 },
    { "ld.h", LS, 0, 0xC000, M_CK510 | M_CK610 },
    { "sth",  LS, 0, 0xD000, M_CK510 | M_CK610 },
    { "st.h", LS, 0, 0xD000, M_CK510 | M_CK610 },
    { "bt",   BR, 0, 0xE000, M_CK510 | M_CK610 },
    { "bf",   BR, 0, 0xE800, M_CK510 | M_CK610 },
    { "br",   BR, 1, 0xF000, M_CK510 | M_CK610 },
#define ckcore_INST_BR  0xF000
    { "bsr",  BR, 0, 0xF800, M_CK510 | M_CK610 },
#define ckcore_INST_BSR 0xF800

    /* The following are relaxable branches */
    { "jbt",  JC, 0, 0xE000, M_CK510 | M_CK610 },
    { "jbf",  JC, 0, 0xE800, M_CK510 | M_CK610 },
    { "jbr",  JU, 1, 0xF000, M_CK510 | M_CK610 },
    { "jbsr", JL, 0, 0xF800, M_CK510 | M_CK610 },

    /* The following are aliases for other instructions */
    { "rts",   O0,   2, 0x00CF, M_CK510 | M_CK610 }, /* jmp r15 */
    { "rolc",  DO21, 0, 0x0600, M_CK510 | M_CK610 }, /* addc rd,rd */
    { "rotlc", DO21, 0, 0x0600, M_CK510 | M_CK610 }, /* addc rd,rd */
    { "setc",  O0,   0, 0x0C00, M_CK510 | M_CK610 }, /* cmphs r0,r0 */
    { "clrc",  O0,   0, 0x0F00, M_CK510 | M_CK610 }, /* cmpne r0,r0 */
    { "tstle", O1,   0, 0x2200, M_CK510 | M_CK610 }, /* cmplti rd,1 */
    /* cmplei rd,X -> cmplti rd,X+1 */
    { "cmplei", OB,   0, 0x2200, M_CK510 | M_CK610 },
    { "neg",    O1,   0, 0x2800, M_CK510 | M_CK610 }, /* rsubi rd,0 */
    { "tstne",  O1,   0, 0x2A00, M_CK510 | M_CK610 }, /* cmpnei rd,0 */
    { "tstlt",  O1,   0, 0x37F0, M_CK510 | M_CK610 }, /* btsti rx,31 */
    { "mclri",  OB2,  0, 0x3000, M_CK510 | M_CK610 }, /* bclri rx,log2(imm) */
    { "mgeni",  OBR2, 0, 0x3200, M_CK510 | M_CK610 }, /* bgeni rx,log2(imm) */
    { "mseti",  OB2,  0, 0x3400, M_CK510 | M_CK610 }, /* bseti rx,log2(imm) */
    { "mtsti",  OB2,  0, 0x3600, M_CK510 | M_CK610 }, /* btsti rx,log2(imm) */
    { "rori",   RSI,  0, 0x3800, M_CK510 | M_CK610 },
    { "rotri",  RSI,  0, 0x3800, M_CK510 | M_CK610 },
    { "nop",    O0,   0, 0x1200, M_CK510 | M_CK610 }, /* mov r0, r0 */
    { 0,        0,    0, 0,      0 }
};

/* Mask for each ckcore_opclass: */
static const unsigned short v1_imsk[] = {
    /* O0  */ 0xFFFF,
    /* OT  */ 0xFFFC,
    /* O1  */ 0xFFF0,
    /* OC  */ 0xFE00,
    /* O2  */ 0xFF00,
    /* X1  */ 0xFFF0,
    /* OI  */ 0xFE00,
    /* OB  */ 0xFE00,

    /* OMa */ 0xFFF0,
    /* SI  */ 0xFE00,
    /* I7  */ 0xF800,
    /* LS  */ 0xF000,
    /* BR  */ 0xF800,
    /* BL  */ 0xFF00,
    /* LR  */ 0xF000,
    /* LJ  */ 0xFF00,

    /* RM  */ 0xFFF0,
    /* RQ  */ 0xFFF0,
    /* JSR */ 0xFFF0,
    /* JMP */ 0xFFF0,
    /* OBRa*/ 0xFFF0,
    /* OBRb*/ 0xFF80,
    /* OBRc*/ 0xFF00,
    /* OBR2*/ 0xFE00,

    /* O1R1*/ 0xFFF0,
    /* OMb */ 0xFF80,
    /* OMc */ 0xFF00,
    /* SIa */ 0xFE00,

    /* MULSH */ 0xFF00,
    /* OPSR  */ 0xFFF8,   /* psrset/psrclr */

    /* O1_CP */   0xFFF0,
    /* O2_CPGR */ 0xFE00,
    /* O2_CPCR */ 0xFF00,
    /* O_KWGJ1 */ 0xFF00,
    /* O_KWGJ2 */ 0xFF00,
    /* JC  */ 0,        /* JC,JU,JL don't appear in object */
    /* JU  */ 0,
    /* JL  */ 0,
    /* RSI */ 0,
    /* DO21*/ 0,
    /* OB2 */ 0,        /* OB2 won't appear in object.  */

    /* O1_E2  */ 0xFFF0,
    /* O1_E   */ 0xFFF0,
    /* O2_E   */ 0xFF00,
    /* OI_E   */ 0xFE00,
    /* OB_E   */ 0xFE00,
    /* O1R1_E */ 0xFFF0,
    /* SIa_E  */ 0xFE00

};

static const char *v1_grname[] = {
    "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
    "r8",  "r9", "r10", "r11", "r12", "r13", "r14", "r15"
};

static const char v1_X[] = "??";

static const char *v1_crname[] = {
    "psr",  "vbr",  "epsr", "fpsr", "epc",  "fpc",  "ss0",  "ss1",
    "ss2",  "ss3",  "ss4",  "gcr",  "gsr",  "cr13", "cr14", "cr15",
    "cr16", "cr17", "cr18", "cr19", "cr20", "cr21", "cr22", "cr23",
    "cr24", "cr25", "cr26", "cr27", "cr28", "cr29", "cr30",  v1_X
};

static const char *v1_cpname[] = {
    v1_X, "cp1", "cp2",  "cp3",  "cp4",  "cp5",  "cp6",  "cp7",
    "cp8", "cp9", "cp10", "cp11", "cp12", "cp13", "cp14", "cp15"
};

static const char *v1_cprname[] = {
    "cpr0",  "cpr1",  "cpr2",  "cpr3",  "cpr4",  "cpr5",  "cpr6",  "cpr7",
    "cpr8",  "cpr9",  "cpr10", "cpr11", "cpr12", "cpr13", "cpr14", "cpr15",
    "cpr16", "cpr17", "cpr18", "cpr19", "cpr20", "cpr21", "cpr22", "cpr23",
    "cpr24", "cpr25", "cpr26", "cpr27", "cpr28", "cpr29", "cpr30", "cpr31",
};

static const char *v1_cpcrname[] = {
    "cpcr0",  "cpcr1",  "cpcr2",  "cpcr3",
    "cpcr4",  "cpcr5",  "cpcr6",  "cpcr7",
    "cpcr8",  "cpcr9",  "cpcr10", "cpcr11",
    "cpcr12", "cpcr13", "cpcr14", "cpcr15",
    "cpcr16", "cpcr17", "cpcr18", "cpcr19",
    "cpcr20", "cpcr21", "cpcr22", "cpcr23",
    "cpcr24", "cpcr25", "cpcr26", "cpcr27",
    "cpcr28", "cpcr29", "cpcr30", "cpcr31",
};

static const unsigned v1_isiz[] = { 2, 0, 1, 0 };

int print_insn_csky_v1(bfd_vma memaddr, struct disassemble_info *info)
{
    unsigned char       ibytes[4];
    fprintf_function    func = info->fprintf_func;
    void                *stream = info->stream;
    unsigned short      inst;
    ckcore_opcode_info  *op;
    int                 status;
    unsigned long       cpu_flag = -1; /* CKCORE_INSN_CK510 */;

    info->bytes_per_chunk = 2;

    status = info->read_memory_func(memaddr, ibytes, 2, info);

    if (status != 0) {
        info->memory_error_func(status, memaddr, info);
        return -1;
    }

    if (info->endian == BFD_ENDIAN_BIG) {
        inst = (ibytes[0] << 8) | ibytes[1];
    } else if (info->endian == BFD_ENDIAN_LITTLE) {
        inst = (ibytes[1] << 8) | ibytes[0];
    } else {
        abort();
    }

    /* Just a linear search of the table.  */
    for (op = ckcore_table; op->name != 0; op++) {
        if (op->inst == (inst & v1_imsk[op->opclass]) &&
            (op->cpu_flag & cpu_flag)) {
            break;
        }
    }

    if (op->name == 0) {
        func(stream, ".short 0x%04x", inst);
    } else {
        const char *name = v1_grname[inst & 0x0F];

        func(stream, "%s", op->name);

        switch (op->opclass) {
        case O0:
            break;
        case OT:
            func(stream, "\t%d", inst & 0x3);
            break;
        case O1:
        case JMP:
        case JSR:
            func(stream, "\t%s", name);
            break;
        case O1_E:
            func(stream, "\t%s, %s", name, name);
            break;
        case O1_E2:
            func(stream, "\t%s, %s, 1", name, name);
            break;
        case OC:
            func(stream, "\t%s, %s", name, v1_crname[(inst >> 4) & 0x1F]);
            break;
        case O1R1:
            func(stream, "\t%s, r1", name);
            break;
        case O1R1_E:
            func(stream, "\t%s, %s, r1", name, name);
            break;
        case MULSH:
        case O2:
            func(stream, "\t%s, %s", name, v1_grname[(inst >> 4) & 0xF]);
            break;
        case O2_E:
            func(stream, "\t%s, %s, %s",
                 name, name, v1_grname[(inst >> 4) & 0xF]);
            break;
        case O1_CP:
            func(stream, "\t%s", v1_cpname[inst & 0xF]);
            break;
        case O2_CPGR:
            func(stream, "\t%s, %s",
                  v1_grname[inst & 0x0F], v1_cprname[(inst >> 4) & 0x1F]);
            break;
        case O2_CPCR:
            func(stream, "\t%s, %s",
                  v1_grname[inst & 0x07], v1_cpcrname[(inst >> 3) & 0x1F]);
            break;
        case O_KWGJ1:
            {
                int bank = (inst >> 6) & 0x3;
                int rw = ((inst >> 4) & 0x3) + 4 * bank;
                int ru = (inst & 0x3) + 4 * bank;
                int rv = ((inst >> 2) & 0x3) + 4 * bank;

                func(stream, "\t%s, %s, %s\t// in bank %d",
                      v1_grname[rw], v1_grname[ru], v1_grname[rv], bank);
                break;
            }
        case O_KWGJ2:
            {
                int bank = (inst >> 6) & 0x3;
                int rw = ((inst >> 4) & 0x3) + 4 * bank;
                int ru = (inst & 0x3) + 4 * bank;
                int rv = ((inst >> 2) & 0x3) + 4 * bank;

                func(stream, "\t%s, (%s), @%s\t// in bank %d",
                      v1_grname[rw], v1_grname[ru], v1_grname[rv], bank);
                break;
            }
        case X1:
            func(stream, "\tr1, %s", name);
            break;
        case OI:
            func(stream, "\t%s, %d", name, ((inst >> 4) & 0x1F) + 1);
            break;
        case OI_E:
            func(stream, "\t%s, %s, %d", name, name, ((inst >> 4) & 0x1F) + 1);
            break;
        case RM:
            func(stream, "\t%s-r15, (r0)", name);
            break;
        case RQ:
            func(stream, "\tr4-r7, (%s)", name);
            break;
        case OB:
        case OBRa:
        case OBRb:
        case OBRc:
        case SI:
        case SIa:
        case OMa:
        case OMb:
        case OMc:
            func(stream, "\t%s, %d", name, (inst >> 4) & 0x1F);
            break;
        case OB_E:
        case SIa_E:
            func(stream, "\t%s, %s, %d", name, name, (inst >> 4) & 0x1F);
            break;
        case I7:
            func(stream, "\t%s, %d", name, (inst >> 4) & 0x7F);
            break;
        case LS:
            func(stream, "\t%s, (%s, %d)", v1_grname[(inst >> 8) & 0xF],
                  name, ((inst >> 4) & 0xF) << v1_isiz[(inst >> 13) & 3]);
            break;

        case BR:
            {
                bfd_vma val = inst & 0x3FF;

                if (inst & 0x400) {
                    val |= 0xFFFFFC00;
                }

                func(stream, "\t0x%x", (unsigned)(memaddr + 2 + (val << 1)));

                if (strcmp(op->name, "bsr") == 0) {
                    /* For bsr, we'll try to get a symbol for the target.  */
                    val = (memaddr + 2 + (val << 1)) & 0xffffffff;

                    if (info->print_address_func && val != 0) {
                        func(stream, "\t// ");
                        info->print_address_func(val, info);
                    }
                }
            }
            break;

        case BL:
            {
                long val;

                val = (inst & 0x000F);
                func(stream, "\t%s, 0x%x",
                      v1_grname[(inst >> 4) & 0xF],
                      (unsigned)(memaddr - (val << 1)));
            }
            break;

        case LR:
            {
                unsigned long val;

                val = (memaddr + 2 + ((inst & 0xFF) << 2)) & 0xFFFFFFFC;

                status = info->read_memory_func(val, ibytes, 4, info);
                if (status != 0) {
                    info->memory_error_func(status, memaddr, info);
                    break;
                }

                if (info->endian == BFD_ENDIAN_LITTLE) {
                    val = (ibytes[3] << 24) | (ibytes[2] << 16) |
                        (ibytes[1] <<  8) | (ibytes[0]);
                } else {
                    val = (ibytes[0] << 24) | (ibytes[1] << 16) |
                        (ibytes[2] <<  8) | (ibytes[3]);
                }

                /* Removed [] around literal value to match ABI syntax 12/95. */
                func(stream, "\t%s, 0x%X",
                     v1_grname[(inst >> 8) & 0xF], (unsigned)val);

                if (val == 0) {
                    func(stream, "\t// from address pool at 0x%x",
                          (unsigned)((memaddr + 2 + ((inst & 0xFF) << 2)) &
                                     0xFFFFFFFC));
                }
            }
            break;

        case LJ:
            {
                bfd_vma val;

                val = (memaddr + 2 + ((inst & 0xFF) << 2)) & 0xFFFFFFFC;

                status = info->read_memory_func(val, ibytes, 4, info);
                if (status != 0) {
                    info->memory_error_func(status, memaddr, info);
                    break;
                }

                if (info->endian == BFD_ENDIAN_LITTLE) {
                    val = (ibytes[3] << 24) | (ibytes[2] << 16) |
                        (ibytes[1] <<  8) | (ibytes[0]);
                } else {
                    val = (ibytes[0] << 24) | (ibytes[1] << 16) |
                        (ibytes[2] <<  8) | (ibytes[3]);
                }

                val &= 0xffffffff;
                /* Removed [] around literal value to match ABI syntax 12/95. */
                func(stream, "\t0x%X", (unsigned)val);
                /* For jmpi/jsri, we'll try to get a symbol for the target. */
                if (info->print_address_func && val != 0) {
                    func(stream, "\t// ");
                    info->print_address_func(val, info);
                } else {
                    func(stream, "\t// from address pool at 0x%x",
                          (unsigned)(memaddr + 2 + ((inst & 0xFF) << 2)) &
                          0xFFFFFFFC);
                }
            }
            break;

        case OPSR:
            {
                static const char *fields[] = {
                    "af", "ie",    "fe",    "fe,ie",
                    "ee", "ee,ie", "ee,fe", "ee,fe,ie"
                };

                func(stream, "\t%s", fields[inst & 0x7]);
            }
            break;

        default:
            /* If the disassembler lags the instruction set.  */
            func(stream, "\tundecoded operands, inst is 0x%04x", inst);
            break;
        }
    }

    /* Say how many bytes we consumed.  */
    return 2;
}

#define ALIAS 0

#define CSKY_INST_TYPE unsigned long

typedef enum {
    MEND, /*end of parsing operands*/
    MREG, /*parsing operand as register, two values after 'MREG':
            operand-mask, rightshift*/
    MREG1,/*parsing vr resgister*/
    MREG2,/*parsing fr resgister*/
    MIMM, /*parsing operand as oimm, three values after 'MIMM':
            operand-mask, rightshift, offset */
    MIMMH, /*parsing operand as oimm, three values after 'MIMM':
             operand-mask, rightshift, offset */
    MSYM, /* convert offset to label name */
    MSYMI, /* convert pool offset to lable name*/
    MIMM0, /* parsing operand as imm, two values after 'MIMM0':
              operand-mask, rightshift*/
    MIMM0H, /* parsing operand as imm, two values after 'MIMM0':
               operand-mask, rightshift*/
    MADD, /* calculate as hex */
    MADDD, /*calculate as decimal*/
    MSUBD, /* calculate as decimal */
    MSPE,   /*output a single character*/
    MERR,  /*error*/
    MSPE1, /* suppress nearby comma */
    MBR,
    MBR1,
    MPSR,  /*specially for instruction psrclr,psrset*/
    MLOG2, /* return clog2(x) */
    MPOP16,
    MPOP32,  /*srecially for instruction pop, push*/
    MIMM1, /*specially for addi.sp instruction*/
    MSP,   /*specially for print sp*/
    MADDISP, /*specially for addisp, subisp*/
    MADDISPH, /*specially for ld.wsp, st.wsp*/
    MLDM,   /*specially for ldm,stm instruction*/
    MVLDM , /*specially for Vldm,Vstm instruction*/
    MLRS, /*specially for instruction lrs.b,lrs.h,lrs.w*/
    MPRINT, /*specially for printing lrs information of  lrs instruction*/
    MLRW16, /*specially for instruction lrw16, addi16.sp, subi16.sp*/
    MLRW16_2, /*specially for instruction lrw16,offset 512-1016*/
    MVSHLRI,
    MVLDSTI,/*specially for instruction VLD&VST*/
    MFLDSTI,/*specially for instruction FLD&VST*/
    MVLDSTQ,/*specially for instruction VLDQ&VSTQ*/
    MR28,   /*specially for print r28*/
    MFLRW,  /*specially for print float and double number*/
    MFMOVI  /*specially for print float number for fmovi*/
} PARSE_METHOD;

typedef struct {
    CSKY_INST_TYPE mask;
    CSKY_INST_TYPE opcode;
    const char *name;     /*instruction name*/
    int *data;  /*point to array for parsing operands*/
} INST_PARSE_INFO;

int CBKPT[] = {MEND };
int CBSR[] = {
    MSYM, 0x3FFFFFF, -1, 0x4000000, 0xFC000000,
    MEND };
int CSCE[] = {
    MIMMH, 0x01E00000, 21, 0,
    MEND };
int CTRAP[] = {
    MIMM0, 0xC00, 10,
    MEND };
int CPSRCLR[] = {
    MPSR, 0x03E00000, 21,
    MEND };
int CCLRF[] = {
    MREG, 0x03E00000, 21,
    MEND };
int CMFHI[] = {
    MREG, 0x1F, 0,
    MEND };
int CMTHI[] = {
    MREG, 0x1F0000, 16,
    MEND };
int CJMPI[] = {
    MSYMI, 0xFFFF, -2,
    MEND };
int CBT[] = {
    MSYM, 0xFFFF, -1, 0x10000, 0xFFFF0000,
    MEND };
int CCPOP[] = {MERR };
int CCPRC[] = {MERR };
int CCPRCR[] = {MERR };
int CLDCPR[] = {MERR };
int CBEZ[] = {
    MREG, 0x1F0000, 16,
    MSYM, 0xFFFF, -1, 0x10000, 0xFFFF0000,
    MEND };
int CCMPNEI[] = {
    MREG, 0x1F0000, 16,
    MIMM0, 0xFFFF, 0,
    MEND };
int CCMPHSI[] = {
    MREG, 0x1F0000, 16,
    MIMM, 0xFFFF, 0, 1,
    MEND };
int CPLDR[] = {
    MSPE1, '(',
    MREG, 0x1F0000, 16,
    MIMM0H, 0xFFF, -2,
    MSPE, ')',
    MEND };
int CBGENR[] = {
    MREG, 0x1F, 0,
    MREG, 0x1F0000, 16,
    MEND };
int CCMPNE[] = {
    MREG, 0x1F0000, 16,
    MREG, 0x03E00000, 21,
    MEND };
int CCMPLS[] = {MERR };
int CBTSTI[] = {
    MREG, 0x1F0000, 16,
    MIMM0, 0x03E00000, 21,
    MEND };
int CBMASKI[] = {
    MREG, 0x1F, 0,
    MIMM, 0x03E00000, 21, 1,
    MEND };
int CADDI[] = {
    MREG, 0x03E00000, 21,
    MREG, 0x1F0000, 16,
    MIMM, 0xFFF, 0, 1,
    MEND };
int CANDI[] = {
    MREG, 0x03E00000, 21,
    MREG, 0x1F0000, 16,
    MIMM0, 0xFFF, 0,
    MEND };
int CORI[] = {
    MREG, 0x03E00000, 21,
    MREG, 0x1F0000, 16,
    MIMM0, 0xFFFF, 0,
    MEND };
int CLDB[] = {
    MREG, 0x03E00000, 21,
    MSPE1, '(',
    MREG, 0x1F0000, 16,
    MIMM0H, 0xFFF, 0,
    MSPE, ')',
    MEND };
int CLDH[] = {
    MREG, 0x03E00000, 21,
    MSPE1, '(',
    MREG, 0x1F0000, 16,
    MIMM0H, 0xFFF, -1,
    MSPE, ')',
    MEND };
int CLDW[] = {
    MREG, 0x03E00000, 21,
    MSPE1, '(',
    MREG, 0x1F0000, 16,
    MIMM0H, 0xFFF, -2,
    MSPE, ')',
    MEND };
int CLDD[] = {
    MREG, 0x03E00000, 21,
    MSPE1, '(',
    MREG, 0x1F0000, 16,
    MIMM0H, 0xFFF, -2,
    MSPE, ')',
    MEND };
int CADDU[] = {
    MREG, 0x01F, 0,
    MREG, 0x1F0000, 16,
    MREG, 0x03E00000, 21,
    MEND };
int CNOT[] = {MERR };
int CRSUB[] = {MERR };
int CBE[] = {
    MREG, 0x1F0000, 16,
    MREG, 0x03E00000, 21,
    MSYM, 0xFFFF, -1, 0x10000, 0xFFFF0000,
    MEND };
int CINCF[] = {
    MREG, 0x03E00000, 21,
    MREG, 0x1F0000, 16,
    MIMM0, 0x1F, 0,
    MEND };
int CDECGT[] = {
    MREG, 0x1F, 0,
    MREG, 0x1F0000, 16,
    MIMM0, 0x03E00000, 21,
    MEND };
int CSRC[] = {
    MREG, 0x1F, 0,
    MREG, 0x1F0000, 16,
    MIMM, 0x03E00000, 21, 1,
    MEND };
int CINS[] = {
    MREG, 0x03E00000, 21,
    MREG, 0x1F0000, 16,
    MADD, 0x3E0, 5, 0x1F, 0,
    MIMM0, 0x1F, 0,
    MEND };
int CSEXT[] = {
    MREG, 0x1F, 0,
    MREG, 0x1F0000, 16,
    MIMM0, 0x3E0, 5,
    MIMM0, 0x03E00000, 21,
    MEND };
int CLDM[] = {
    MREG, 0x03E00000, 21,
    MSPE, '-',
    MLDM, 0x1f, 0,
    MSPE1, '(',
    MREG, 0x1F0000, 16,
    MSPE, ')',
    MEND };
int CLDRH[] = {
    MREG, 0x1F, 0,
    MSPE1, '(',
    MREG, 0x001F0000, 16,
    MREG, 0x03E00000, 21,
    MSPE, '<',
    MSPE, '<',
    MLOG2, 0x1E0, 5,
    MSPE, ')',
    MERR };
int CMFCR[] = {
    MREG, 0x1F, 0,
    MSPE1, 'c',
    MSPE, 'r',
    MSPE, '<',
    MADDD, 0x1F0000, 16, 0, 0,
    MIMM0, 0x03E00000, 21,
    MSPE, '>',
    MEND };
int CMTCR[] = {
    MREG, 0x1F0000, 16,
    MSPE1, 'c',
    MSPE, 'r',
    MSPE, '<',
    MADDD, 0x1F, 0, 0, 0,
    MIMM0, 0x03E00000, 21,
    MSPE, '>',
    MEND };
int HLRW[] = {
    MREG, 0x00e0, 5,
    MLRW16, 0x0300, 3, 0x1f, -2,
    MEND };
int CLRW[] = {
    MREG, 0x1F0000, 16,
    MSYMI, 0xFFFF, -2,
    MEND };
int CGRS[] = {
    MREG, 0x03e00000, 21,
    MSYM, 0x3ffff, -1, 0x40000, 0xfffc0000,
    MEND };
int CBR3[] = {
    MSYM, 0xffff, -1, 0x10000, 0xffff0000,
    MEND };
int CPOP[] = {
    MPOP32,
    MEND };
int CADDI18[] = {
    MREG, 0x03e00000, 21,
    MR28,
    MIMM, 0x3ffff, 0x0, 1,
    MEND  };
int CLRSB[] = {
    MREG, 0x03e00000, 21,
    MSPE1, '[',
    MLRS, 0x3ffff, 0,
    MSPE, ']',
    MPRINT,
    MEND };
int CLRSH[] = {
    MREG, 0x03e00000, 21,
    MSPE1, '[',
    MLRS, 0x3ffff, -1,
    MSPE, ']',
    MPRINT,
    MEND };
int CLRSW[] = {
    MREG, 0x03e00000, 21,
    MSPE1, '[',
    MLRS, 0x3ffff, -2,
    MSPE, ']',
    MPRINT,
    MEND };
int CJMPIX[] = {
    MREG, 0x1f0000, 16,
    MIMM, 0x3, -3, 16,
    MEND };
int HBKPT[] = {
    MEND };
int HBR[] = {
    MSYM, 0x3FF, -1, 0x400, 0xFFFFFC00,
    MEND };
int HMVCV[] = {
    MREG, 0x03C0, 6,
    MEND };
int HTSTNBZ[] = {
    MREG, 0x3c, 2,
    MEND };
int HNOT[] = {MERR };
int HCMPHS[] = {
    MREG, 0x3c, 2,
    MREG, 0x3c0, 6,
    MEND };
int HCMPNEI[] = {
    MREG, 0x0700, 8,
    MIMM0, 0x1f, 0,
    MEND };
int HCMPLTI[] = {
    MREG, 0x0700, 8,
    MIMM, 0x1f, 0, 1,
    MEND };
int HMOVI[] = {
    MREG, 0x0700, 8,
    MIMM0H, 0xff, 0,
    MEND };
int HZEXTB[] = {
    MREG, 0x03C0, 6,
    MREG, 0x3C, 2,
    MEND };
int HLSLI[] = {
    MREG, 0x00E0, 5,
    MREG, 0x0700, 8,
    MIMM0, 0x1F, 0,
    MEND };
int HLDB[] = {
    MREG, 0x00E0, 5,
    MSPE1, '(',
    MREG, 0x0700, 8,
    MIMM0H, 0x1F, 0,
    MSPE, ')',
    MEND };
int HLDH[] = {
    MREG, 0x00E0, 5,
    MSPE1, '(',
    MREG, 0x0700, 8,
    MIMM0H, 0x1F, -1,
    MSPE, ')',
    MEND };
int HLDW[] = {
    MREG, 0x00E0, 5,
    MSPE1, '(',
    MREG, 0x0700, 8,
    MIMM0H, 0x1F, -2,
    MSPE, ')',
    MEND };
int HLDWSP[] = {
    MREG, 0x00e0, 5,
    MSPE1, '(',
    MSP,
    MADDISPH, 0x700, 3, 0x1f, -2,
    MSPE, ')',
    MEND };
int HADDI8[] = {
    MREG, 0x0700, 8,
    MIMM, 0xFF, 0, 1,
    MEND };
int HADDI3[] = {
    MREG, 0x00E0, 5,
    MREG, 0x0700, 8,
    MIMM, 0x1C, 2, 1,
    MEND };
int HADDI8SP[] = {
    MREG, 0x0700, 8,
    MSP,
    MIMM1, 0xFF, 0, -2,
    MEND };
int HADDI7SP[] = {
    MSP,
    MSP,
    MADDISP, 0x0300, 3, 0x1f, -2,
    MEND };
int HSUBI7SP[] = {
    MSP,
    MSP,
    MADDISP, 0x0300, 3, 0x1f, -2,
    MEND };
int HPOP[] = {
    MPOP16,
    MEND};
int HADDU2[] = {
    MREG, 0x00E0, 5,
    MREG, 0x0700, 8,
    MREG, 0x001C, 2,
    MEND };
int HJMPIX[] = {
    MREG, 0x0700, 8,
    MIMM, 0x3, -3, 16,
    MEND };
int PSEUDO[] = {MERR };
int CFMPZHSS[] = {
    MREG2, 0xf0000, 16,
    MEND};
int CFSITOS[] = {
    MREG2, 0xf, 0,
    MREG2, 0xf0000, 16,
    MEND };
int CVCADD[] = {
    MREG1, 0xf, 0,
    MREG1, 0xf0000, 16,
    MEND };

int CFCMPHSS[] = {
    MREG2, 0xf0000, 16,
    MREG2, 0x1e00000, 21,
    MEND };
int CFADDS[] = {
    MREG2, 0xf, 0,
    MREG2, 0xf0000, 16,
    MREG2, 0x1e00000, 21,
    MEND };
int CVTRCH[] = {
    MREG1, 0xf, 0,
    MREG1, 0xf0000, 16,
    MREG1, 0x1e00000, 21,
    MEND };
int CSHLI[] = {
    MREG1, 0xf, 0,
    MREG1, 0xf0000, 16,
    MIMM, 0x1e0000, 21,
    MEND};
int CVLDB[] = {
    MREG1, 0x1e00000, 21,
    MSPE1, '(',
    MREG, 0x1f0000, 16,
    MIMM0H, 0x1f, 0,
    MSPE, ')',
    MEND};
int CVLDH[] = {
    MREG1, 0x1e00000, 21,
    MSPE1, '(',
    MREG, 0x1f0000, 16,
    MIMM0H, 0x1f, -1,
    MSPE, ')',
    MEND};
int CVLDW[] = {
    MREG1, 0x1e00000, 21,
    MSPE1, '(',
    MREG, 0x1f0000, 16,
    MIMM0H, 0x1f, -2,
    MSPE, ')',
    MEND};
int CVMTVR[] = {
    MREG1, 0xf, 0,
    MBR1, '[',
    MIMM0, 0x1e00000, 21,
    MBR, ']',
    MREG, 0x1F0000, 16,
    MEND};
int CVMFVR[] = {
    MREG, 0x1f, 0,
    MREG1, 0x1F0000, 16,
    MBR1, '[',
    MIMM0, 0x1e00000, 21,
    MBR, ']',
    MEND};

int CVDUP[] = {
    MREG1, 0xf, 0,
    MREG1, 0xf0000, 16,
    MBR1, '[',
    MIMM0, 0x1e00000, 21,
    MSPE, ']',
    MEND};
int CVSHLRI[] = {
    MREG1, 0XF, 0,
    MREG1, 0xF0000, 16,
    MVSHLRI, 0x01e00000, 21, 0x20, 1,
    MEND };
int CVLDST[] = {
    MREG1, 0xF, 0,
    MSPE1, '(',
    MREG, 0x1f0000, 16,
    MVLDSTI, 0x1e00000, 17, 0xf0, 4,
    MSPE, ')',
    MEND };
int CFLDST[] = {
    MREG2, 0xF, 0,
    MSPE1, '(',
    MREG, 0x1f0000, 16,
    MFLDSTI, 0x1e00000, 17, 0xf0, 4,
    MSPE, ')',
    MEND };
int CVLDSTQ[] = {
    MREG1, 0xF, 0,
    MSPE1, '(',
    MREG, 0x1f0000, 16,
    MVLDSTQ, 0x1e00000, 17, 0xf0, 4,
    MSPE, ')',
    MEND };

int CVLDR[] = {
    MREG1, 0xf, 0,
    MSPE1, '(',
    MREG, 0x1f0000, 16,
    MREG, 0x03e00000, 21,
    MSPE, '<',
    MSPE, '<',
    MIMM0, 0x60, 5,
    MSPE, ')',
    MEND };
int CFLDRS[] = {
    MREG2, 0xf, 0,
    MSPE1, '(',
    MREG, 0x1f0000, 16,
    MREG, 0x03e00000, 21,
    MSPE, '<',
    MSPE, '<',
    MIMM0, 0x60, 5,
    MSPE, ')',
    MEND };
int CFMFVR[] = {
    MREG, 0x1f, 0,
    MREG2, 0xf0000, 16,
    MEND };
int CFMTVR[] = {
    MREG2, 0xf, 0,
    MREG, 0x1f0000, 16,
    MEND };

int CFLDM[] = {
    MREG2, 0xf, 0,
    MSPE, '-',
    MVLDM, 0x1e00000, 21,
    MSPE1, '(',
    MREG, 0x1F0000, 16,
    MSPE, ')',
    MEND };
int CVINS[] = {
    MREG1, 0xf, 0,
    MBR1, '[',
    MIMM0, 0x1E0, 5,
    MBR, ']',
    MREG1, 0xf0000, 16,
    MBR1, '[',
    MIMM0, 0x1e00000, 21,
    MSPE, ']',
    MEND};
int FLRW[] = {
    MREG2, 0xF, 0,
    MFLRW, 0x01e00000, 13, 0xf0, 2,
    MEND };
int FMOVI[] = {
    MREG2, 0xF, 0,
    MFMOVI, 0x1e00000, 17, 0xf0, 4, 0x000f0000, 16, 0x00100000, 20,
    MEND };
int BPOP[] = {
    MREG, 0X1C, 2,
    MEND };


INST_PARSE_INFO *csky_find_inst_info(CSKY_INST_TYPE inst, int length);
static inline int clog2(int s);

INST_PARSE_INFO csky_inst_info_32[] = {
    { 0xffffffff, 0xe8cf0000, "rts", CBKPT },
    { 0xffffffff, 0xc0005020, "doze", CBKPT },
    { 0xffffffff, 0xc0004420, "rfi", CBKPT },
    { 0xffffffff, 0xc0004020, "rte", CBKPT },
    { 0xffffffff, 0xc0005820, "se", CBKPT },
    { 0xffffffff, 0xc0004820, "stop", CBKPT },
    { 0xffffffff, 0xc0000420, "sync", CBKPT },
    { 0xffffffff, 0xc0004c20, "wait", CBKPT },
    { 0xffffffff, 0xc0005420, "we", CBKPT },
    { 0xffffffff, 0xc4009820, "mvtc", CBKPT },
    { 0xffffffff, 0xc0001c20, "idly", CBKPT},
    { 0xffffffff, 0xc0001020, "bmset", CBKPT},
    { 0xffffffff, 0xc0001420, "bmclr", CBKPT},
    { 0xffff0000, 0xe8000000, "br", CBR3 },
    { 0xffff0000, 0xe8400000, "bf", CBR3 },
    { 0xffff0000, 0xe8600000, "bt", CBR3 },
    { 0xfc000000, 0xe0000000, "bsr", CBSR },
    { 0xfe1fffff, 0xc0001820, "sce", CSCE },
    { 0xfffff3ff, 0xc0002020, "trap", CTRAP },
    { 0xfc1fffff, 0xc0007020, "psrclr", CPSRCLR },
    { 0xfc1fffff, 0xc0007420, "psrset", CPSRCLR },
    { 0xfc1fffff, 0xc4002c20, "clrf", CCLRF },
    { 0xfc1fffff, 0xc4002c40, "clrt", CCLRF },
    { 0xffffffe0, 0xc4009c20, "mfhi", CMFHI },
    { 0xffffffe0, 0xc4009820, "mfhis", CMFHI },
    { 0xffffffe0, 0xc4009880, "mflos", CMFHI },
    { 0xffffffe0, 0xc4009c80, "mflo", CMFHI },
    { 0xffffffe0, 0xc4000500, "mvc", CMFHI },
    { 0xffffffe0, 0xc4000600, "mvcv", CMFHI },
    { 0xffe0ffff, 0xc4009c40, "mthi", CMTHI },
    { 0xffe0ffff, 0xc4009d00, "mtlo", CMTHI },
    { 0xffe0ffff, 0xc4002100, "tstnbz", CMTHI },
    { 0xffe0ffff, 0xe8c00000, "jmp", CMTHI },
    { 0xffe0ffff, 0xe8e00000, "jsr", CMTHI },
    { 0xffff0000, 0xeac00000, "jmpi", CJMPI },
    { 0xffff0000, 0xeae00000, "jsri", CJMPI },
    { 0xfffff000, 0xfc00c000, "cpop", CCPOP },
    { 0xfc00f000, 0xfc004000, "cprc", CCPRC },
    { 0xfc00f000, 0xfc002000, "cprcr", CCPRCR },
    { 0xfc00f000, 0xfc000000, "cprgr", CCPRCR },
    { 0xfc00f000, 0xfc003000, "cpwcr", CCPRCR },
    { 0xfc00f000, 0xfc001000, "cpwgr", CCPRCR },
    { 0xffe00000, 0xe8000000, "ldcpr", CLDCPR },
    { 0xffe00000, 0xe8008000, "stcpr", CLDCPR },
    { 0xffe00000, 0xe9000000, "bez", CBEZ },
    { 0xffe00000, 0xe9200000, "bnez", CBEZ },
    { 0xffe00000, 0xe9400000, "bhz", CBEZ },
    { 0xffe00000, 0xe9600000, "blsz", CBEZ },
    { 0xffe00000, 0xe9800000, "blz", CBEZ },
    { 0xffe00000, 0xe9a00000, "bhsz", CBEZ },
    { 0xffe00000, 0xeb400000, "cmpnei", CCMPNEI },
    { 0xffe00000, 0xea000000, "movi", CCMPNEI },
    { 0xffe00000, 0xea200000, "movih", CCMPNEI },
    { 0xffe00000, 0xeb000000, "cmphsi", CCMPHSI },
    { 0xffe00000, 0xeb200000, "cmplti", CCMPHSI },
    { 0xffe0f000, 0xd8006000, "pldr", CPLDR },
    { 0xffe0f000, 0xdc006000, "pldw", CPLDR },
    { 0xffe0ffe0, 0xc4005040, "bgenr", CBGENR },
    { 0xffe0ffe0, 0xc4007020, "xtrb0", CBGENR },
    { 0xffe0ffe0, 0xc4007040, "xtrb1", CBGENR },
    { 0xffe0ffe0, 0xc4007080, "xtrb2", CBGENR },
    { 0xffe0ffe0, 0xc4007100, "xtrb3", CBGENR },
    { 0xffe0ffe0, 0xc4006200, "brev", CBGENR },
    { 0xffe0ffe0, 0xc4006080, "revb", CBGENR },
    { 0xffe0ffe0, 0xc4006100, "revh", CBGENR },
    { 0xffe0ffe0, 0xc4000200, "abs", CBGENR },
    { 0xffe0ffe0, 0xc4007c40, "ff1", CBGENR },
    { 0xffe0ffe0, 0xc4007c20, "ff0", CBGENR },
    { 0xffe0ffe0, 0xc40058e0, "sextb", CBGENR },
    { 0xffe0ffe0, 0xc40059e0, "sexth", CBGENR },
    { 0xffe0ffe0, 0xc40054e0, "zextb", CBGENR },
    { 0xffe0ffe0, 0xc40055e0, "zexth", CBGENR },
    { 0xfc00ffff, 0xc4000480, "cmpne", CCMPNE },
    { 0xfc00ffff, 0xc4000420, "cmphs", CCMPNE },
    { 0xfc00ffff, 0xc4000440, "cmplt", CCMPNE },
    { 0xfc00ffff, 0xc4002080, "tst", CCMPNE },
    { 0xfc00ffff, 0xc4008820, "mulu", CCMPNE },
    { 0xfc00ffff, 0xc4008840, "mulua", CCMPNE },
    { 0xfc00ffff, 0xc4008880, "mulus", CCMPNE },
    { 0xfc00ffff, 0xc4008c20, "muls", CCMPNE },
    { 0xfc00ffff, 0xc4009040, "mulsha", CCMPNE },
    { 0xfc00ffff, 0xc4009080, "mulshs", CCMPNE },
    { 0xfc00ffff, 0xc4008c40, "mulsa", CCMPNE },
    { 0xfc00ffff, 0xc4008c80, "mulss", CCMPNE },
    { 0xfc00ffff, 0xc4001c20, "cmpix", CCMPNE },
    { 0xffe0ffe0, 0xc4000420, "cmpls", CCMPLS },
    { 0xffe0ffe0, 0xc4000440, "cmpgt", CCMPLS },
    { 0xfc00ffff, 0xc4002880, "btsti", CBTSTI },
    { 0xfc1fffe0, 0xc4005020, "bmaski", CBMASKI },
    { 0xfc00f000, 0xe4000000, "addi", CADDI },
    { 0xfc00f000, 0xe4001000, "subi", CADDI },
    { 0xfc1c0000, 0xcc1c0000, "addi", CADDI18 },
    { 0xfc00f000, 0xe4002000, "andi", CANDI },
    { 0xfc00f000, 0xe4003000, "andni", CANDI },
    { 0xfc00f000, 0xe4004000, "xori", CANDI },
    { 0xfc000000, 0xec000000, "ori", CORI },
    { 0xfc00f000, 0xd8000000, "ld.b", CLDB },
    { 0xfc00f000, 0xdc000000, "st.b", CLDB },
    { 0xfc00f000, 0xd8004000, "ld.bs", CLDB },
    { 0xfc00f000, 0xd8001000, "ld.h", CLDH },
    { 0xfc00f000, 0xd8005000, "ld.hs", CLDH },
    { 0xfc00f000, 0xdc001000, "st.h", CLDH },
    { 0xfc00f000, 0xd8002000, "ld.w", CLDW },
    { 0xfc00f000, 0xd8003000, "ld.d", CLDD },
    { 0xfc00f000, 0xd8007000, "ldex.w", CLDW },
    { 0xfc00f000, 0xdc002000, "st.w", CLDW },
    { 0xfc00f000, 0xdc003000, "st.d", CLDD },
    { 0xfc00f000, 0xdc007000, "stex.w", CLDW },
    { 0xfc1c0000, 0xcc000000, "lrs.b", CLRSB },
    { 0xfc1c0000, 0xcc040000, "lrs.h", CLRSH },
    { 0xfc1c0000, 0xcc080000, "lrs.w", CLRSW },
    { 0xfc1c0000, 0xcc100000, "srs.b", CLRSB },
    { 0xfc1c0000, 0xcc140000, "srs.h", CLRSH },
    { 0xfc1c0000, 0xcc180000, "srs.w", CLRSW },
    { 0xfc00ffe0, 0xc4000020, "addu", CADDU },
    { 0xfc00ffe0, 0xc4000040, "addc", CADDU },
    { 0xfc00ffe0, 0xc4000080, "subu", CADDU },
    { 0xfc00ffe0, 0xc4000100, "subc", CADDU },
    { 0xfc00ffe0, 0xc4000820, "ixh", CADDU },
    { 0xfc00ffe0, 0xc4000840, "ixw", CADDU },
    { 0xfc00ffe0, 0xc4000880, "ixd", CADDU },
    { 0xfc00ffe0, 0xc4002020, "and", CADDU },
    { 0xfc00ffe0, 0xc4002040, "andn", CADDU },
    { 0xfc00ffe0, 0xc4002420, "or", CADDU },
    { 0xfc00ffe0, 0xc4002440, "xor", CADDU },
    { 0xfc00ffe0, 0xc4002480, "nor", CADDU },
    { 0xfc00ffe0, 0xc4004020, "lsl", CADDU },
    { 0xfc00ffe0, 0xc4004040, "lsr", CADDU },
    { 0xfc00ffe0, 0xc4004080, "asr", CADDU },
    { 0xfc00ffe0, 0xc4004100, "rotl", CADDU },
    { 0xfc00ffe0, 0xc4008020, "divu", CADDU },
    { 0xfc00ffe0, 0xc4008040, "divs", CADDU },
    { 0xfc00ffe0, 0xc4008420, "mult", CADDU },
    { 0xfc00ffe0, 0xc4009420, "mulsw", CADDU },
    { 0xfc00ffe0, 0xc4009020, "mulsh", CADDU },
    { 0xfc00ffe0, 0xc4000080, "rsub", CRSUB },
    { 0xfc00ffe0, 0xc4000c20, "incf", CINCF },
    { 0xfc00ffe0, 0xc4000c40, "inct", CINCF },
    { 0xfc00ffe0, 0xc4000c80, "decf", CINCF },
    { 0xfc00ffe0, 0xc4000d00, "dect", CINCF },
    { 0xfc00ffe0, 0xc4001020, "decgt", CDECGT },
    { 0xfc00ffe0, 0xc4001040, "declt", CDECGT },
    { 0xfc00ffe0, 0xc4001080, "decne", CDECGT },
    { 0xfc00ffe0, 0xc4004820, "lsli",  CDECGT },
    { 0xfc00ffe0, 0xc4004840, "lsri",  CDECGT },
    { 0xfc00ffe0, 0xc4004880, "asri",  CDECGT },
    { 0xfc00ffe0, 0xc4004900, "rotli", CDECGT },
    { 0xfc00ffe0, 0xc4002820, "bclri", CDECGT },
    { 0xfc00ffe0, 0xc4002840, "bseti", CDECGT },
    { 0xfc00ffe0, 0xc4004c80, "asrc", CSRC },
    { 0xfc00ffe0, 0xc4004d00, "xsr", CSRC },
    { 0xfc00ffe0, 0xc4004c20, "lslc", CSRC },
    { 0xfc00ffe0, 0xc4004c40, "lsrc", CSRC },
    { 0xfc00fc00, 0xc4005c00, "ins", CINS },
    { 0xfc00fc00, 0xc4005800, "sext", CSEXT },
    { 0xfc00fc00, 0xc4005400, "zext", CSEXT },
    { 0xfc00ffe0, 0xd0001c20, "ldm", CLDM },
    { 0xfc00ffe0, 0xd4001c20, "stm", CLDM },
    { 0xfc00fe00, 0xd0000400, "ldr.h", CLDRH },
    { 0xfc00fe00, 0xd0000800, "ldr.w", CLDRH },
    { 0xfc00fe00, 0xd0001000, "ldr.bs", CLDRH },
    { 0xfc00fe00, 0xd0001400, "ldr.hs", CLDRH },
    { 0xfc00fe00, 0xd4000000, "str.b", CLDRH },
    { 0xfc00fe00, 0xd4000400, "str.h", CLDRH },
    { 0xfc00fe00, 0xd4000800, "str.w", CLDRH },
    { 0xfc00fe00, 0xd0000000, "ldr.b", CLDRH },
    { 0xfc00ffe0, 0xc0006020, "mfcr", CMFCR },
    { 0xfc00ffe0, 0xc0006420, "mtcr", CMTCR },
    { 0xffe00000, 0xea800000, "lrw", CLRW},
    { 0xffe00000, 0xe4000000, "cprgr", PSEUDO },
    { 0xffe00000, 0xe4400000, "cprgr", PSEUDO },
    { 0xffe00000, 0xe4200000, "cpwcr", PSEUDO },
    { 0xffe00000, 0xe4600000, "cpwgr", PSEUDO },
    { 0xfc1c0000, 0xcc0c0000, "grs", CGRS},
    { 0xfffffc00, 0xebc00000, "pop", CPOP},
    { 0xfffffc00, 0xebe00000, "push", CPOP},
    { 0xffe0fffc, 0xe9e00000, "jmpix", CJMPIX},
    /* vfp vdsp */
    { 0xfff0ffff, 0xf4000100, "fcmpzhss",      CFMPZHSS},
    { 0xfff0ffff, 0xf4000120, "fcmpzlss",      CFMPZHSS},
    { 0xfff0ffff, 0xf4000140, "fcmpznes",      CFMPZHSS},
    { 0xfff0ffff, 0xf4000160, "fcmpzuos",      CFMPZHSS},
    { 0xfff0ffff, 0xf4000900, "fcmpzhsd",      CFMPZHSS},
    { 0xfff0ffff, 0xf4000920, "fcmpzlsd",      CFMPZHSS},
    { 0xfff0ffff, 0xf4000940, "fcmpzned",      CFMPZHSS},
    { 0xfff0ffff, 0xf4000960, "fcmpzuod",      CFMPZHSS},
    { 0xfff0fff0, 0xf4001800, "fstosi.rn",     CFSITOS },
    { 0xfff0fff0, 0xf4001820, "fstosi.rz",     CFSITOS },
    { 0xfff0fff0, 0xf4001840, "fstosi.rpi",    CFSITOS },
    { 0xfff0fff0, 0xf4001860, "fstosi.rni",    CFSITOS },
    { 0xfff0fff0, 0xf4001880, "fstoui.rn",     CFSITOS },
    { 0xfff0fff0, 0xf40018a0, "fstoui.rz",     CFSITOS },
    { 0xfff0fff0, 0xf40018c0, "fstoui.rpi",    CFSITOS },
    { 0xfff0fff0, 0xf40018e0, "fstoui.rni",    CFSITOS },
    { 0xfff0fff0, 0xf4001900, "fdtosi.rn",     CFSITOS },
    { 0xfff0fff0, 0xf4001920, "fdtosi.rz",     CFSITOS },
    { 0xfff0fff0, 0xf4001940, "fdtosi.rpi",    CFSITOS },
    { 0xfff0fff0, 0xf4001960, "fdtosi.rni",    CFSITOS },
    { 0xfff0fff0, 0xf4001980, "fdtoui.rn",     CFSITOS },
    { 0xfff0fff0, 0xf40019a0, "fdtoui.rz",     CFSITOS },
    { 0xfff0fff0, 0xf40019c0, "fdtoui.rpi",    CFSITOS },
    { 0xfff0fff0, 0xf40019e0, "fdtoui.rni",    CFSITOS },
    { 0xfff0fff0, 0xf4001a00, "fsitos",        CFSITOS },
    { 0xfff0fff0, 0xf4001a20, "fuitos",        CFSITOS },
    { 0xfff0fff0, 0xf4001a80, "fsitod",        CFSITOS },
    { 0xfff0fff0, 0xf4001aa0, "fuitod",        CFSITOS },
    { 0xfff0fff0, 0xf4001ac0, "fdtos" ,        CFSITOS },
    { 0xfff0fff0, 0xf4001ae0, "fstod" ,        CFSITOS },
    { 0xfff0fff0, 0xf4000080, "fmovs" ,        CFSITOS },
    { 0xfff0fff0, 0xf40000c0, "fabss" ,        CFSITOS },
    { 0xfff0fff0, 0xf40000e0, "fnegs" ,        CFSITOS },
    { 0xfff0fff0, 0xf4000340, "fsqrts",        CFSITOS },
    { 0xfff0fff0, 0xf4000320, "frecips",       CFSITOS },
    { 0xfff0fff0, 0xf40010c0, "fabsm",         CFSITOS },
    { 0xfff0fff0, 0xf40010e0, "fnegm" ,        CFSITOS },
    { 0xfff0fff0, 0xf4000880, "fmovd" ,        CFSITOS },
    { 0xfff0fff0, 0xf4001080, "fmovm" ,        CFSITOS },
    { 0xfff0fff0, 0xf40008c0, "fabsd",         CFSITOS },
    { 0xfff0fff0, 0xf40008e0, "fnegd",         CFSITOS },
    { 0xfff0fff0, 0xf4000b40, "fsqrtd",        CFSITOS },
    { 0xfff0fff0, 0xf4000b20, "frecipd",       CFSITOS },
    { 0xfe10ffff, 0xf4000180, "fcmphss",      CFCMPHSS },
    { 0xfe10ffff, 0xf40001a0, "fcmplts",      CFCMPHSS },
    { 0xfe10ffff, 0xf40001c0, "fcmpnes",      CFCMPHSS },
    { 0xfe10ffff, 0xf40001e0, "fcmpuos",      CFCMPHSS },
    { 0xfe10ffff, 0xf4000980, "fcmphsd",      CFCMPHSS },
    { 0xfe10ffff, 0xf40009a0, "fcmpltd",      CFCMPHSS },
    { 0xfe10ffff, 0xf40009c0, "fcmpned",      CFCMPHSS },
    { 0xfe10ffff, 0xf40009e0, "fcmpuod",      CFCMPHSS },
    { 0xfe10fff0, 0xf4000000, "fadds",          CFADDS },
    { 0xfe10fff0, 0xf4000020, "fsubs",          CFADDS },
    { 0xfe10fff0, 0xf4000200, "fmuls",          CFADDS },
    { 0xfe10fff0, 0xf4000300, "fdivs",          CFADDS },
    { 0xfe10fff0, 0xf4000280, "fmacs",          CFADDS },
    { 0xfe10fff0, 0xf40002a0, "fmscs",          CFADDS },
    { 0xfe10fff0, 0xf40002c0, "fnmacs",         CFADDS },
    { 0xfe10fff0, 0xf40002e0, "fnmscs",         CFADDS },
    { 0xfe10fff0, 0xf4000220, "fnmuls",         CFADDS },
    { 0xfe10fff0, 0xf4000800, "faddd",          CFADDS },
    { 0xfe10fff0, 0xf4000820, "fsubd",          CFADDS },
    { 0xfe10fff0, 0xf4000a00, "fmuld",          CFADDS },
    { 0xfe10fff0, 0xf4000b00, "fdivd",          CFADDS },
    { 0xfe10fff0, 0xf4000a80, "fmacd",          CFADDS },
    { 0xfe10fff0, 0xf4000aa0, "fmscd",          CFADDS },
    { 0xfe10fff0, 0xf4000ac0, "fnmacd",         CFADDS },
    { 0xfe10fff0, 0xf4000ae0, "fnmscd",         CFADDS },
    { 0xfe10fff0, 0xf4000a20, "fnmuld",         CFADDS },
    { 0xfe10fff0, 0xf4001000, "faddm",          CFADDS },
    { 0xfe10fff0, 0xf4001020, "fsubm",          CFADDS },
    { 0xfe10fff0, 0xf4001200, "fmulm",          CFADDS },
    { 0xfe10fff0, 0xf4001280, "fmacm",          CFADDS },
    { 0xfe10fff0, 0xf40012a0, "fmscm",          CFADDS },
    { 0xfe10fff0, 0xf40012c0, "fnmacm",         CFADDS },
    { 0xfe10fff0, 0xf40012e0, "fnmscm",         CFADDS },
    { 0xfe10fff0, 0xf4001220, "fnmulm",         CFADDS },
    { 0xfe10fff0, 0xf8000f40, "vtrch.8",        CVTRCH },
    { 0xfe10fff0, 0xf8100f40, "vtrch.16",       CVTRCH },
    { 0xfe10fff0, 0xfa000f40, "vtrch.32",       CVTRCH },
    { 0xfe10fff0, 0xf8000f60, "vtrcl.8",        CVTRCH },
    { 0xfe10fff0, 0xf8100f60, "vtrcl.16",       CVTRCH },
    { 0xfe10fff0, 0xfa000f60, "vtrcl.32",       CVTRCH },
    { 0xfe10fff0, 0xf8000000, "vadd.u8",        CVTRCH },
    { 0xfe10fff0, 0xf8100000, "vadd.u16",       CVTRCH },
    { 0xfe10fff0, 0xfa000000, "vadd.u32",       CVTRCH },
    { 0xfe10fff0, 0xf8000010, "vadd.s8",        CVTRCH },
    { 0xfe10fff0, 0xf8100010, "vadd.s16",       CVTRCH },
    { 0xfe10fff0, 0xfa000010, "vadd.s32",       CVTRCH },
    { 0xfe10fff0, 0xf8000020, "vadd.eu8",       CVTRCH },
    { 0xfe10fff0, 0xf8100020, "vadd.eu16",      CVTRCH },
    { 0xfe10fff0, 0xf8000030, "vadd.es8",       CVTRCH },
    { 0xfe10fff0, 0xf8100030, "vadd.es16",      CVTRCH },
    { 0xfe10fff0, 0xf8000040, "vcadd.u8",       CVTRCH },
    { 0xfe10fff0, 0xf8100040, "vcadd.u16",      CVTRCH },
    { 0xfe10fff0, 0xfa000040, "vcadd.u32",      CVTRCH },
    { 0xfe10fff0, 0xf8000050, "vcadd.s8",       CVTRCH },
    { 0xfe10fff0, 0xf8100050, "vcadd.s16",      CVTRCH },
    { 0xfe10fff0, 0xfa000050, "vcadd.s32",      CVTRCH },
    { 0xfe10fff0, 0xf8000060, "vcadd.eu8",      CVTRCH },
    { 0xfe10fff0, 0xf8100060, "vcadd.eu16",     CVTRCH },
    { 0xfe10fff0, 0xf8000070, "vcadd.es8",      CVTRCH },
    { 0xfe10fff0, 0xf8100070, "vcadd.es16",     CVTRCH },
    { 0xfe10fff0, 0xf8100140, "vadd.xu16.sl",   CVTRCH },
    { 0xfe10fff0, 0xfa000140, "vadd.xu32.sl",   CVTRCH },
    { 0xfe10fff0, 0xf8100150, "vadd.xs16.sl",   CVTRCH },
    { 0xfe10fff0, 0xfa000150, "vadd.xs32.sl",   CVTRCH },
    { 0xfe10fff0, 0xf8100160, "vadd.xu16",      CVTRCH },
    { 0xfe10fff0, 0xfa000160, "vadd.xu32",      CVTRCH },
    { 0xfe10fff0, 0xf8100170, "vadd.xs16",      CVTRCH },
    { 0xfe10fff0, 0xfa000170, "vadd.xs32",      CVTRCH },
    { 0xfe10fff0, 0xf8000180, "vaddh.u8",       CVTRCH },
    { 0xfe10fff0, 0xf8100180, "vaddh.u16",      CVTRCH },
    { 0xfe10fff0, 0xfa000180, "vaddh.u32",      CVTRCH },
    { 0xfe10fff0, 0xf8000190, "vaddh.s8",       CVTRCH },
    { 0xfe10fff0, 0xf8100190, "vaddh.s16",      CVTRCH },
    { 0xfe10fff0, 0xfa000190, "vaddh.s32",      CVTRCH },
    { 0xfe10fff0, 0xf80001a0, "vaddh.u8.r",     CVTRCH },
    { 0xfe10fff0, 0xf81001a0, "vaddh.u16.r",    CVTRCH },
    { 0xfe10fff0, 0xfa0001a0, "vaddh.u32.r",    CVTRCH },
    { 0xfe10fff0, 0xf80001b0, "vaddh.s8.r",     CVTRCH },
    { 0xfe10fff0, 0xf81001b0, "vaddh.s16.r",    CVTRCH },
    { 0xfe10fff0, 0xfa0001b0, "vaddh.s32.r",    CVTRCH },
    { 0xfe10fff0, 0xf80001c0, "vadd.u8.s",      CVTRCH },
    { 0xfe10fff0, 0xf81001c0, "vadd.u16.s",     CVTRCH },
    { 0xfe10fff0, 0xfa0001c0, "vadd.u32.s",     CVTRCH },
    { 0xfe10fff0, 0xf80001d0, "vadd.s8.s",      CVTRCH },
    { 0xfe10fff0, 0xf81001d0, "vadd.s16.s",     CVTRCH },
    { 0xfe10fff0, 0xfa0001d0, "vadd.s32.s",     CVTRCH },
    { 0xfe10fff0, 0xf8000200, "vsub.u8",        CVTRCH },
    { 0xfe10fff0, 0xf8100200, "vsub.u16",       CVTRCH },
    { 0xfe10fff0, 0xfa000200, "vsub.u32",       CVTRCH },
    { 0xfe10fff0, 0xf8000210, "vsub.s8",        CVTRCH },
    { 0xfe10fff0, 0xf8100210, "vsub.s16",       CVTRCH },
    { 0xfe10fff0, 0xfa000210, "vsub.s32",       CVTRCH },
    { 0xfe10fff0, 0xf8000220, "vsub.eu8",       CVTRCH },
    { 0xfe10fff0, 0xf8100220, "vsub.eu16",      CVTRCH },
    { 0xfe10fff0, 0xf8000230, "vsub.es8",       CVTRCH },
    { 0xfe10fff0, 0xf8100230, "vsub.es16",      CVTRCH },
    { 0xfe10fff0, 0xf8000240, "vsabs.u8",       CVTRCH },
    { 0xfe10fff0, 0xf8100240, "vsabs.u16",      CVTRCH },
    { 0xfe10fff0, 0xfa000240, "vsabs.u32",      CVTRCH },
    { 0xfe10fff0, 0xf8000250, "vsabs.s8",       CVTRCH },
    { 0xfe10fff0, 0xf8100250, "vsabs,s16",      CVTRCH },
    { 0xfe10fff0, 0xfa000250, "vsabs.s32",      CVTRCH },
    { 0xfe10fff0, 0xf8000260, "vsabs.eu8",      CVTRCH },
    { 0xfe10fff0, 0xf8100260, "vsabs.eu16",     CVTRCH },
    { 0xfe10fff0, 0xf8000270, "vsabs.es8",      CVTRCH },
    { 0xfe10fff0, 0xf8100270, "vsabs.es16",     CVTRCH },
    { 0xfe10fff0, 0xf8000280, "vsabsa.u8",      CVTRCH },
    { 0xfe10fff0, 0xf8100280, "vsabsa.u16",     CVTRCH },
    { 0xfe10fff0, 0xfa000280, "vsabsa.u32",     CVTRCH },
    { 0xfe10fff0, 0xf8000290, "vsabsa.s8",      CVTRCH },
    { 0xfe10fff0, 0xf8100290, "vsabsa.s16",     CVTRCH },
    { 0xfe10fff0, 0xfa000290, "vsabsa.s32",     CVTRCH },
    { 0xfe10fff0, 0xf80002a0, "vsabsa.eu8",     CVTRCH },
    { 0xfe10fff0, 0xf81002a0, "vsabsa.eu16",    CVTRCH },
    { 0xfe10fff0, 0xf80002b0, "vsabsa.es8",     CVTRCH },
    { 0xfe10fff0, 0xf81002b0, "vsabsa.es16",    CVTRCH },
    { 0xfe10fff0, 0xf8100360, "vsub.xu16",      CVTRCH },
    { 0xfe10fff0, 0xfa000360, "vsub.xu32",      CVTRCH },
    { 0xfe10fff0, 0xf8100370, "vsub.xs16",      CVTRCH },
    { 0xfe10fff0, 0xfa000370, "vsub.xs32",      CVTRCH },
    { 0xfe10fff0, 0xf8000380, "vsubh.u8",       CVTRCH },
    { 0xfe10fff0, 0xf8100380, "vsubh.u16",      CVTRCH },
    { 0xfe10fff0, 0xfa000380, "vsubh.u32",      CVTRCH },
    { 0xfe10fff0, 0xf8000390, "vsubh.s8",       CVTRCH },
    { 0xfe10fff0, 0xf8100390, "vsubh.s16",      CVTRCH },
    { 0xfe10fff0, 0xfa000390, "vsubh.s32",      CVTRCH },
    { 0xfe10fff0, 0xf80003a0, "vsubh.u8.r",     CVTRCH },
    { 0xfe10fff0, 0xf81003a0, "vsubh.u16.r",    CVTRCH },
    { 0xfe10fff0, 0xfa0003a0, "vsubh.u32.r",    CVTRCH },
    { 0xfe10fff0, 0xf80003b0, "vsubh.s8.r",     CVTRCH },
    { 0xfe10fff0, 0xf81003b0, "vsubh.s16.r",    CVTRCH },
    { 0xfe10fff0, 0xfa0003b0, "vsubh.s32.r",    CVTRCH },
    { 0xfe10fff0, 0xf80003c0, "vsub.u8.s",      CVTRCH },
    { 0xfe10fff0, 0xf81003c0, "vsub.u16.s",     CVTRCH },
    { 0xfe10fff0, 0xfa0003c0, "vsub.u32.s",     CVTRCH },
    { 0xfe10fff0, 0xf80003d0, "vsub.s8.s",      CVTRCH },
    { 0xfe10fff0, 0xf81003d0, "vsub.s16.s",     CVTRCH },
    { 0xfe10fff0, 0xfa0003d0, "vsub.s32.s",     CVTRCH },
    { 0xfe10fff0, 0xf8000400, "vmul.u8",        CVTRCH },
    { 0xfe10fff0, 0xf8100400, "vmul.u16",       CVTRCH },
    { 0xfe10fff0, 0xfa000400, "vmul.u32",       CVTRCH },
    { 0xfe10fff0, 0xf8000410, "vmul.s8",        CVTRCH },
    { 0xfe10fff0, 0xf8100410, "vmul.s16",       CVTRCH },
    { 0xfe10fff0, 0xfa000410, "vmul.s32",       CVTRCH },
    { 0xfe10fff0, 0xf8000420, "vmul.eu8.h",     CVTRCH },
    { 0xfe10fff0, 0xf8100420, "vmul.eu16.h",    CVTRCH },
    { 0xfe10fff0, 0xfa000420, "vmul.eu32.h",    CVTRCH },
    { 0xfe10fff0, 0xf8000430, "vmul.es8.h",     CVTRCH },
    { 0xfe10fff0, 0xf8100430, "vmul.es16.h",    CVTRCH },
    { 0xfe10fff0, 0xfa000430, "vmul.es32.h",    CVTRCH },
    { 0xfe10fff0, 0xf8000440, "vmula.u8",       CVTRCH },
    { 0xfe10fff0, 0xf8100440, "vmula.u16",      CVTRCH },
    { 0xfe10fff0, 0xfa000440, "vmula.u32",      CVTRCH },
    { 0xfe10fff0, 0xf8000440, "vmula.s8",       CVTRCH },
    { 0xfe10fff0, 0xf8100450, "vmula.s16",      CVTRCH },
    { 0xfe10fff0, 0xfa000450, "vmula.s32",      CVTRCH },
    { 0xfe10fff0, 0xf8000460, "vmula.eu8",      CVTRCH },
    { 0xfe10fff0, 0xf8100460, "vmula.eu16",     CVTRCH },
    { 0xfe10fff0, 0xfa000460, "vmula.eu32",     CVTRCH },
    { 0xfe10fff0, 0xf8000470, "vmula.es8",      CVTRCH },
    { 0xfe10fff0, 0xf8100470, "vmula.es16",     CVTRCH },
    { 0xfe10fff0, 0xf8000480, "vmuls.u8",       CVTRCH },
    { 0xfe10fff0, 0xf8100480, "vmuls.u16",      CVTRCH },
    { 0xfe10fff0, 0xfa000480, "vmuls.u32",      CVTRCH },
    { 0xfe10fff0, 0xf8000490, "vmuls.s8",       CVTRCH },
    { 0xfe10fff0, 0xf8100490, "vmuls.s16",      CVTRCH },
    { 0xfe10fff0, 0xfa000490, "vmuls.s32",      CVTRCH },
    { 0xfe10fff0, 0xf80004a0, "vmuls.eu8",      CVTRCH },
    { 0xfe10fff0, 0xf81004a0, "vmuls.eu16",     CVTRCH },
    { 0xfe10fff0, 0xf80004b0, "vmuls.es8",      CVTRCH },
    { 0xfe10fff0, 0xf81004b0, "vmuls.es16",     CVTRCH },
    { 0xfe10fff0, 0xf8000680, "vshr.u8",        CVTRCH },
    { 0xfe10fff0, 0xf8100680, "vshr.u16",       CVTRCH },
    { 0xfe10fff0, 0xfa000680, "vshr.u32",       CVTRCH },
    { 0xfe10fff0, 0xf8000690, "vshr.s8",        CVTRCH },
    { 0xfe10fff0, 0xf8100690, "vshr.s16",       CVTRCH },
    { 0xfe10fff0, 0xfa000690, "vshr.s32",       CVTRCH },
    { 0xfe10fff0, 0xf80006c0, "vshr.u8.r",      CVTRCH },
    { 0xfe10fff0, 0xf81006c0, "vshr.u16.r",     CVTRCH },
    { 0xfe10fff0, 0xfa0006c0, "vshr.u32.r",     CVTRCH },
    { 0xfe10fff0, 0xf80006d0, "vshr.s8.r",      CVTRCH },
    { 0xfe10fff0, 0xf81006d0, "vshr.s16.r",     CVTRCH },
    { 0xfe10fff0, 0xfa0006d0, "vshr.s32.r",     CVTRCH },
    { 0xfe10fff0, 0xf8000780, "vshl.u8",        CVTRCH },
    { 0xfe10fff0, 0xf8100780, "vshl.u16",       CVTRCH },
    { 0xfe10fff0, 0xfa000780, "vshl.u32",       CVTRCH },
    { 0xfe10fff0, 0xf8000790, "vshl.s8",        CVTRCH },
    { 0xfe10fff0, 0xf8100790, "vshl.s16",       CVTRCH },
    { 0xfe10fff0, 0xfa000790, "vshl.s32",       CVTRCH },
    { 0xfe10fff0, 0xf80007c0, "vshl.u8.s",      CVTRCH },
    { 0xfe10fff0, 0xf81007c0, "vshl.u16.s",     CVTRCH },
    { 0xfe10fff0, 0xfa0007c0, "vshl.u32.s",     CVTRCH },
    { 0xfe10fff0, 0xf80007d0, "vshl.s8.s",      CVTRCH },
    { 0xfe10fff0, 0xf81007d0, "vshl.s16.s",     CVTRCH },
    { 0xfe10fff0, 0xfa0007d0, "vshl.s32.s",     CVTRCH },
    { 0xfe10fff0, 0xf8000800, "vcmphs.u8",      CVTRCH },
    { 0xfe10fff0, 0xf8100800, "vcmphs.u16",     CVTRCH },
    { 0xfe10fff0, 0xfa000800, "vcmphs.u32",     CVTRCH },
    { 0xfe10fff0, 0xf8000810, "vcmphs.s8",      CVTRCH },
    { 0xfe10fff0, 0xf8100810, "vcmpsh.s16",     CVTRCH },
    { 0xfe10fff0, 0xfa000810, "vcmpsh.s32",     CVTRCH },
    { 0xfe10fff0, 0xf8000820, "vcmplt.u8",      CVTRCH },
    { 0xfe10fff0, 0xf8100820, "vcmplt.u16",     CVTRCH },
    { 0xfe10fff0, 0xfa000820, "vcmplt.u32",     CVTRCH },
    { 0xfe10fff0, 0xf8000830, "vcmplt.s8",      CVTRCH },
    { 0xfe10fff0, 0xf8100830, "vcmplt.s16",     CVTRCH },
    { 0xfe10fff0, 0xfa000830, "vcmplt.s32",     CVTRCH },
    { 0xfe10fff0, 0xf8100840, "vcmpne.u8",      CVTRCH },
    { 0xfe10fff0, 0xf8100840, "vcmpne.u16",     CVTRCH },
    { 0xfe10fff0, 0xfa000840, "vcmpne.u32",     CVTRCH },
    { 0xfe10fff0, 0xf8000850, "vcmpne.s8",      CVTRCH },
    { 0xfe10fff0, 0xf8100850, "vcmpne.s16",     CVTRCH },
    { 0xfe10fff0, 0xfa000850, "vcmpne.s32",     CVTRCH },
    { 0xfe10fff0, 0xf8000880, "vcmphsz.u8",     CVTRCH },
    { 0xfe10fff0, 0xf8100880, "vcmphsz.u16",    CVTRCH },
    { 0xfe10fff0, 0xfa000880, "vcmphsz.u32",    CVTRCH },
    { 0xfe10fff0, 0xf8000890, "vcmphsz.s8",     CVTRCH },
    { 0xfe10fff0, 0xf8100890, "vcmphsz.s16",    CVTRCH },
    { 0xfe10fff0, 0xfa000890, "vcmphsz.s32",    CVTRCH },
    { 0xfe10fff0, 0xf80008a0, "vcmpltz.u8",     CVTRCH },
    { 0xfe10fff0, 0xf81008a0, "vcmpltz.u16",    CVTRCH },
    { 0xfe10fff0, 0xfa0008a0, "vcmpltz.u32",    CVTRCH },
    { 0xfe10fff0, 0xf80008b0, "vcmpltz.s8",     CVTRCH },
    { 0xfe10fff0, 0xf81008b0, "vcmpltz.s16",    CVTRCH },
    { 0xfe10fff0, 0xfa0008b0, "vcmpltz.s32",    CVTRCH },
    { 0xfe10fff0, 0xf80008c0, "vcmpnez.u8",     CVTRCH },
    { 0xfe10fff0, 0xf81008c0, "vcmpnez.u16",    CVTRCH },
    { 0xfe10fff0, 0xfa0008c0, "vcmpnez.u32",    CVTRCH },
    { 0xfe10fff0, 0xf80008d0, "vcmpnez.s8",     CVTRCH },
    { 0xfe10fff0, 0xf81008d0, "vcmpnez.s16",    CVTRCH },
    { 0xfe10fff0, 0xfa0008d0, "vcmpnez.s32",    CVTRCH },
    { 0xfe10fff0, 0xf8000900, "vmax.u8",        CVTRCH },
    { 0xfe10fff0, 0xf8100900, "vmax.u16",       CVTRCH },
    { 0xfe10fff0, 0xfa000900, "vmax.u32",       CVTRCH },
    { 0xfe10fff0, 0xf8000910, "vmax.s8",        CVTRCH },
    { 0xfe10fff0, 0xf8100910, "vmax.s16",       CVTRCH },
    { 0xfe10fff0, 0xfa000910, "vmax.s32",       CVTRCH },
    { 0xfe10fff0, 0xf8000920, "vmin.u8",        CVTRCH },
    { 0xfe10fff0, 0xf8100920, "vmin.u16",       CVTRCH },
    { 0xfe10fff0, 0xfa000920, "vmin.u32",       CVTRCH },
    { 0xfe10fff0, 0xf8000930, "vmin.s8",        CVTRCH },
    { 0xfe10fff0, 0xf8100930, "vmin.s16",       CVTRCH },
    { 0xfe10fff0, 0xfa000930, "vmin.s32",       CVTRCH },
    { 0xfe10fff0, 0xf8000980, "vcmax.u8",       CVTRCH },
    { 0xfe10fff0, 0xf8100980, "vcmax.u16",      CVTRCH },
    { 0xfe10fff0, 0xfa000980, "vcmax.u32",      CVTRCH },
    { 0xfe10fff0, 0xf8000990, "vcmax.s8",       CVTRCH },
    { 0xfe10fff0, 0xf8100990, "vcmax.s16",      CVTRCH },
    { 0xfe10fff0, 0xfa000990, "vcmax.s32",      CVTRCH },
    { 0xfe10fff0, 0xf80009a0, "vcmin.u8",       CVTRCH },
    { 0xfe10fff0, 0xf81009a0, "vcmin.u16",      CVTRCH },
    { 0xfe10fff0, 0xfa0009a0, "vcmin.u32",      CVTRCH },
    { 0xfe10fff0, 0xf80009b0, "vcmin.s8",       CVTRCH },
    { 0xfe10fff0, 0xf81009b0, "vcmin.s16",      CVTRCH },
    { 0xfe10fff0, 0xfa0009b0, "vcmin.s32",      CVTRCH },
    { 0xfe10fff0, 0xf8000a00, "vand.8",         CVTRCH },
    { 0xfe10fff0, 0xf8100a00, "vand.16",        CVTRCH },
    { 0xfe10fff0, 0xfa000a00, "vand.32",        CVTRCH },
    { 0xfe10fff0, 0xf8000a20, "vandn.8",        CVTRCH },
    { 0xfe10fff0, 0xf8100a20, "vandn.16",       CVTRCH },
    { 0xfe10fff0, 0xfa000a20, "vandn.32",       CVTRCH },
    { 0xfe10fff0, 0xf8000a40, "vor.8",          CVTRCH },
    { 0xfe10fff0, 0xf8100a40, "vor.16",         CVTRCH },
    { 0xfe10fff0, 0xfa000a40, "vor.32",         CVTRCH },
    { 0xfe10fff0, 0xf8000a60, "vnor.8",         CVTRCH },
    { 0xfe10fff0, 0xf8100a60, "vnor.16",        CVTRCH },
    { 0xfe10fff0, 0xfa000a60, "vnor.32",        CVTRCH },
    { 0xfe10fff0, 0xf8000a80, "vxor.8",         CVTRCH },
    { 0xfe10fff0, 0xf8100a80, "vxor.16",        CVTRCH },
    { 0xfe10fff0, 0xfa000a80, "vxor.32",        CVTRCH },
    { 0xfe10fff0, 0xf8000b20, "vtst.8",         CVTRCH },
    { 0xfe10fff0, 0xf8100b20, "vtst.16",        CVTRCH },
    { 0xfe10fff0, 0xfa000b20, "vtst.32",        CVTRCH },
    { 0xfe10fff0, 0xf8000f00, "vbpermz.8",      CVTRCH },
    { 0xfe10fff0, 0xf8100f00, "vbpermz.16",     CVTRCH },
    { 0xfe10fff0, 0xfa000f00, "vbpermz.32",     CVTRCH },
    { 0xfe10fff0, 0xf8000f20, "vbperm.8",       CVTRCH },
    { 0xfe10fff0, 0xf8100f20, "vbperm.16",      CVTRCH },
    { 0xfe10fff0, 0xfa000f20, "vbperm.32",      CVTRCH },
    { 0xfe10fff0, 0xf8000fc0,  "vdch.8",        CVTRCH },
    { 0xfe10fff0, 0xf8100fc0,  "vdch.16",       CVTRCH },
    { 0xfe10fff0, 0xfa000fc0,  "vdch.32",       CVTRCH },
    { 0xfe10fff0, 0xf8000fe0,  "vdcl.8",        CVTRCH },
    { 0xfe10fff0, 0xf8100fe0,  "vdcl.16",       CVTRCH },
    { 0xfe10fff0, 0xfa000fe0,  "vdcl.32",       CVTRCH },
    { 0xfe10fff0, 0xf8000f80,  "vich.8",        CVTRCH },
    { 0xfe10fff0, 0xf8100f80,  "vich.16",       CVTRCH },
    { 0xfe10fff0, 0xfa000f80,  "vich.32",       CVTRCH },
    { 0xfe10fff0, 0xf8000fa0,  "vicl.8",        CVTRCH },
    { 0xfe10fff0, 0xf8100fa0,  "vicl.16",       CVTRCH },
    { 0xfe10fff0, 0xfa000fa0,  "vicl.32",       CVTRCH },
    { 0xfe00ff00, 0xf8002000,  "vldd.8",        CVLDST },
    { 0xfe00ff00, 0xf8002100,  "vldd.16",       CVLDST },
    { 0xfe00ff00, 0xf8002200,  "vldd.32",       CVLDST },
    { 0xfe00ff00, 0xf8002400,  "vldq.8",        CVLDSTQ},
    { 0xfe00ff00, 0xf8002500,  "vldq.16",       CVLDSTQ},
    { 0xfe00ff00, 0xf8002600,  "vldq.32",       CVLDSTQ},
    { 0xfe00ff00, 0xf8002800,  "vstd.8",        CVLDST },
    { 0xfe00ff00, 0xf8002900,  "vstd.16",       CVLDST },
    { 0xfe00ff00, 0xf8002a00,  "vstd.32",       CVLDST },
    { 0xfe00ff00, 0xf8002c00,  "vstq.8",        CVLDSTQ},
    { 0xfe00ff00, 0xf8002d00,  "vstq.16",       CVLDSTQ},
    { 0xfe00ff00, 0xf8002e00,  "vstq.32",       CVLDSTQ},
    { 0xfc00ff90, 0xf8003000,  "vldrd.8",        CVLDR },
    { 0xfc00ff90, 0xf8003100,  "vldrd.16",       CVLDR },
    { 0xfc00ff90, 0xf8003200,  "vldrd.32",       CVLDR },
    { 0xfc00ff90, 0xf8003400,  "vldrq.8",        CVLDR },
    { 0xfc00ff90, 0xf8003500,  "vldrq.16",       CVLDR },
    { 0xfc00ff90, 0xf8003600,  "vldrq.32",       CVLDR },
    { 0xfc00ff90, 0xf8003800,  "vstrd.8",        CVLDR },
    { 0xfc00ff90, 0xf8003900,  "vstrd.16",       CVLDR },
    { 0xfc00ff90, 0xf8003a00,  "vstrd.32",       CVLDR },
    { 0xfc00ff90, 0xf8003c00,  "vstrq.8",        CVLDR },
    { 0xfc00ff90, 0xf8003d00,  "vstrq.16",       CVLDR },
    { 0xfc00ff90, 0xf8003e00,  "vstrq.32",       CVLDR },
    { 0xfc00ff90, 0xf4002800,   "fldrs",         CFLDRS},
    { 0xfc00ff90, 0xf4002c00,   "fstrs",         CFLDRS},
    { 0xfc00ff90, 0xf4002900,   "fldrd",         CFLDRS},
    { 0xfc00ff90, 0xf4002d00,   "fstrd",         CFLDRS},
    { 0xfc00ff90, 0xf4002a00,   "fldrm",         CFLDRS},
    { 0xfc00ff90, 0xf4002e00,   "fstrm",         CFLDRS},
    { 0xfe00ff00, 0xf4002000,   "flds",          CFLDST},
    { 0xfe00ff00, 0xf4002400,   "fsts",          CFLDST},
    { 0xfe00ff00, 0xf4002100,   "fldd",          CFLDST},
    { 0xfe00ff00, 0xf4002500,   "fstd",          CFLDST},
    { 0xfe00ff00, 0xf4002200,   "fldm",          CFLDST},
    { 0xfe00ff00, 0xf4002600,   "fstm",          CFLDST},
    { 0xfe00fff0, 0xf4003000,   "fldms",         CFLDM },
    { 0xfe00fff0, 0xf4003400,   "fstms",         CFLDM },
    { 0xfe00fff0, 0xf4003100,   "fldmd",         CFLDM },
    { 0xfe00fff0, 0xf4003500,   "fstmd",         CFLDM },
    { 0xfe00fff0, 0xf4003600,   "fstmm",         CFLDM },
    { 0xfe00fff0, 0xf4003200,   "fldmm",         CFLDM },
    { 0Xfff0fff0, 0xf8000060, "vcadd.eu8",       CVCADD},
    { 0Xfff0fff0, 0xf8100060, "vcadd.eu16",      CVCADD},
    { 0Xfff0fff0, 0xf8000070, "vcadd.es8",       CVCADD},
    { 0Xfff0fff0, 0xf8100070, "vcadd.es16",      CVCADD},
    { 0xfff0fff0, 0xf8000c00, "vmov",            CVCADD},
    { 0xfff0fff0, 0xf8000c20, "vmov.eu8",        CVCADD},
    { 0xfff0fff0, 0xf8100c20, "vmov.eu16",       CVCADD},
    { 0xfff0fff0, 0xf8000c30, "vmov.es8",        CVCADD},
    { 0xfff0fff0, 0xf8100c30, "vmov.es16",       CVCADD},
    { 0xfff0fff0, 0xf8100d00, "vmov.u16.l",      CVCADD},
    { 0xfff0fff0, 0xfa000d00, "vmov.u32.l",      CVCADD},
    { 0xfff0fff0, 0xf8100d10, "vmov.s16.l",      CVCADD},
    { 0xfff0fff0, 0xfa000d10, "vmov.s32.l",      CVCADD},
    { 0xfff0fff0, 0xf8100d40, "vmov.u16.sl",     CVCADD},
    { 0xfff0fff0, 0xfa000d40, "vmov.u32.sl",     CVCADD},
    { 0xfff0fff0, 0xf8100d50, "vmov.s16.sl",     CVCADD},
    { 0xfff0fff0, 0xfa000d50, "vmov.s32.sl",     CVCADD},
    { 0xfff0fff0, 0xf8100d60, "vmov.u16.h",      CVCADD},
    { 0xfff0fff0, 0xfa000d60, "vmov.u32.h",      CVCADD},
    { 0xfff0fff0, 0xf8100d70, "vmov.s16.h",      CVCADD},
    { 0xfff0fff0, 0xfa000d70, "vmov.s32.h",      CVCADD},
    { 0xfff0fff0, 0xf8100d80, "vmov.u16.rh",     CVCADD},
    { 0xfff0fff0, 0xfa000d80, "vmov.u32.rh",     CVCADD},
    { 0xfff0fff0, 0xf8100d90, "vmov.s16.rh",     CVCADD},
    { 0xfff0fff0, 0xfa000d90, "vmov.s32.rh",     CVCADD},
    { 0xfff0fff0, 0xf8100dc0, "vstou.u16.sl",    CVCADD},
    { 0xfff0fff0, 0xfa000dc0, "vstou.u32.sl",    CVCADD},
    { 0xfff0fff0, 0xf8100dd0, "vstou.s16.sl",    CVCADD},
    { 0xfff0fff0, 0xfa000dd0, "vstou.s32.sl",    CVCADD},
    { 0xfff0fff0, 0xf8000e60, "vrev.8",          CVCADD},
    { 0xfff0fff0, 0xf8100e60, "vrev.16",         CVCADD},
    { 0xfff0fff0, 0xfa000e60, "vrev.32",         CVCADD},
    { 0xfff0fff0, 0xf8000ea0, "vcnt1.8",         CVCADD},
    { 0xfff0fff0, 0xf8000ec0, "vclz.8",          CVCADD},
    { 0xfff0fff0, 0xf8100ec0, "vclz.16",         CVCADD},
    { 0xfff0fff0, 0xfa000ec0, "vclz.32",         CVCADD},
    { 0xfff0fff0, 0xf8000ee0, "vcls.u8",         CVCADD},
    { 0xfff0fff0, 0xf8100ee0, "vcls.u16",        CVCADD},
    { 0xfff0fff0, 0xfa000ee0, "vcls.u32",        CVCADD},
    { 0xfff0fff0, 0xf8000ef0, "vcls.s8",         CVCADD},
    { 0xfff0fff0, 0xf8100ef0, "vcls.s16",        CVCADD},
    { 0xfff0fff0, 0xfa000ef0, "vcls.s32",        CVCADD},
    { 0xfff0fff0, 0xf8001000, "vabs.u8",         CVCADD},
    { 0xfff0fff0, 0xf8101000, "vabs.u16",        CVCADD},
    { 0xfff0fff0, 0xfa001000, "vabs.u32",        CVCADD},
    { 0xfff0fff0, 0xf8001010, "vabs.s8",         CVCADD},
    { 0xfff0fff0, 0xf8101010, "vabs.s16",        CVCADD},
    { 0xfff0fff0, 0xfa001010, "vabs.s32",        CVCADD},
    { 0xfff0fff0, 0xf8001040, "vabs.u8.s",       CVCADD},
    { 0xfff0fff0, 0xf8101040, "vabs.u16.s",      CVCADD},
    { 0xfff0fff0, 0xfa001040, "vabs.u32.s",      CVCADD},
    { 0xfff0fff0, 0xf8001050, "vabs.s8.s",       CVCADD},
    { 0xfff0fff0, 0xf8101050, "vabs.s16.s",      CVCADD},
    { 0xfff0fff0, 0xfa001050, "vabs.s32.s",      CVCADD},
    { 0xfff0fff0, 0xf8001080, "vneg.u8",         CVCADD},
    { 0xfff0fff0, 0xf8101080, "vneg.u16",        CVCADD},
    { 0xfff0fff0, 0xfa001080, "vneg.u32",        CVCADD},
    { 0xfff0fff0, 0xf8001090, "vneg.s8",         CVCADD},
    { 0xfff0fff0, 0xf8101090, "vneg.s16",        CVCADD},
    { 0xfff0fff0, 0xfa001090, "vneg.s32",        CVCADD},
    { 0xfff0fff0, 0xf80010c0, "vneg.u8.s",       CVCADD},
    { 0xfff0fff0, 0xf81010c0, "vneg.u16.s",      CVCADD},
    { 0xfff0fff0, 0xfa0010c0, "vneg.u32.s",      CVCADD},
    { 0xfff0fff0, 0xf80010d0, "vneg.s8.s",       CVCADD},
    { 0xfff0fff0, 0xf81010d0, "vneg.s16.s",      CVCADD},
    { 0xfff0fff0, 0xfa0010d0, "vneg.s32.s",      CVCADD},
    { 0xfff0fff0, 0xf8000880, "vcmphsz.u8",      CVCADD},
    { 0xfff0fff0, 0xf8100880, "vcmphsz.u16",     CVCADD},
    { 0xfff0fff0, 0xfa000880, "vcmphsz.u32",     CVCADD},
    { 0xfff0fff0, 0xf8000890, "vcmphsz.s8",      CVCADD},
    { 0xfff0fff0, 0xf8100890, "vcmphsz.s16",     CVCADD},
    { 0xfff0fff0, 0xfa000890, "vcmphsz.s32",     CVCADD},
    { 0xfff0fff0, 0xf80008a0, "vcmpltz.u8",      CVCADD},
    { 0xfff0fff0, 0xf81008a0, "vcmpltz.u16",     CVCADD},
    { 0xfff0fff0, 0xfa0008a0, "vcmpltz.u32",     CVCADD},
    { 0xfff0fff0, 0xf80008b0, "vcmpltz.s8",      CVCADD},
    { 0xfff0fff0, 0xf81008b0, "vcmpltz.s16",     CVCADD},
    { 0xfff0fff0, 0xfa0008b0, "vcmpltz.s32",     CVCADD},
    { 0xfff0fff0, 0xf80008c0, "vcmpnez.u8",      CVCADD},
    { 0xfff0fff0, 0xf81008c0, "vcmpnez.u16",     CVCADD},
    { 0xfff0fff0, 0xfa1008c0, "vcmpnez.u32",     CVCADD},
    { 0xfff0fff0, 0xf80008d0, "vcmpnez.s8",      CVCADD},
    { 0xfff0fff0, 0xf81008d0, "vcmpnez.s16",     CVCADD},
    { 0xfff0fff0, 0xfa0008d0, "vcmpnez.s32",     CVCADD},
    { 0xfe00fff0, 0xf8001300, "vmtvr.u8",       CVMTVR  },
    { 0xfe00fff0, 0xf8001320, "vmtvr.u16",      CVMTVR  },
    { 0xfe00fff0, 0xf8001340, "vmtvr.u32",      CVMTVR  },
    { 0xfe10ffe0, 0xf8001200, "vmfvr.u8",       CVMFVR  },
    { 0xfe10ffe0, 0xf8001220, "vmfvr.u16",      CVMFVR  },
    { 0xfe10ffe0, 0xf8001240, "vmfvr.u32",      CVMFVR  },
    { 0xfe10ffe0, 0xf8001280, "vmfvr.s8",       CVMFVR  },
    { 0xfe10ffe0, 0xf80012a0, "vmfvr.s16",      CVMFVR  },
    { 0xfe00ffe0, 0xf8001a00, "vld.u8",         CVLDB   },
    { 0xfe00ffe0, 0xf8001a20, "vld.u16",        CVLDH   },
    { 0xfe00ffe0, 0xf8001a40, "vld.u32",        CVLDW   },
    { 0xfe00ffe0, 0xf8001a60, "vld.u64",        CVLDW   },
    { 0xfe00ffe0, 0xf8001a80, "vld.u128",       CVLDW   },
    { 0xfe00ffe0, 0xf8001b00, "vst.u8" ,        CVLDB   },
    { 0xfe00ffe0, 0xf8001b20, "vst.u16",        CVLDH   },
    { 0xfe00ffe0, 0xf8001b40, "vst.u32",        CVLDW   },
    { 0xfe00ffe0, 0xf8001b60, "vst.u64",        CVLDW   },
    { 0xfe00ffe0, 0xf8001b80, "vst.u128",       CVLDW   },
    { 0xfe10fff0, 0xf8000e80, "vdup.8",         CVDUP   },
    { 0xfe10fff0, 0xf8100e80, "vdup.16",        CVDUP   },
    { 0xfe10fff0, 0xfa000e80, "vdup.32",        CVDUP   },
    { 0xfe10ffd0, 0xf8000700, "vshli.u8",       CVSHLRI },
    { 0xfe10ffd0, 0xf8100700, "vshli.u16",      CVSHLRI },
    { 0xfe10ffd0, 0xfa000700, "vshli.u32",      CVSHLRI },
    { 0xfe10ffd0, 0xf8000710, "vshli.s8",       CVSHLRI },
    { 0xfe10ffd0, 0xf8100710, "vshli.s16",      CVSHLRI },
    { 0xfe10ffd0, 0xfa000710, "vshli.s32",      CVSHLRI },
    { 0xfe10ffd0, 0xf8000740, "vshli.u8.s",     CVSHLRI },
    { 0xfe10ffd0, 0xf8100740, "vshli.u16.s",    CVSHLRI },
    { 0xfe10ffd0, 0xfa000740, "vshli.u32.s",    CVSHLRI },
    { 0xfe10ffd0, 0xf8000750, "vshli.s8.s",     CVSHLRI },
    { 0xfe10ffd0, 0xf8100750, "vshli.s16.s",    CVSHLRI },
    { 0xfe10ffd0, 0xfa000750, "vshli.s32.s",    CVSHLRI },
    { 0xfe10ffd0, 0xf8000600, "vshri.u8",       CVSHLRI },
    { 0xfe10ffd0, 0xf8100600, "vshri.u16",      CVSHLRI },
    { 0xfe10ffd0, 0xfa000600, "vshri.u32",      CVSHLRI },
    { 0xfe10ffd0, 0xf8000610, "vshri.s8",       CVSHLRI },
    { 0xfe10ffd0, 0xf8100610, "vshri.s16",      CVSHLRI },
    { 0xfe10ffd0, 0xfa000610, "vshri.s32",      CVSHLRI },
    { 0xfe10ffd0, 0xf8000640, "vshri.u8.r",     CVSHLRI },
    { 0xfe10ffd0, 0xf8100640, "vshri.u16.r",    CVSHLRI },
    { 0xfe10ffd0, 0xfa000640, "vshri.u32.r",    CVSHLRI },
    { 0xfe10ffd0, 0xf8000650, "vshri.s8.r",     CVSHLRI },
    { 0xfe10ffd0, 0xf8100650, "vshri.s16.r",    CVSHLRI },
    { 0xfe10ffd0, 0xfa000650, "vshri.s32.r",    CVSHLRI },
    { 0xfff0ffe0, 0xf4001b00, "fmfvrh",         CFMFVR  },
    { 0xfff0ffe0, 0xf4001b20, "fmfvrl",         CFMFVR  },
    { 0xffe0fff0, 0xf4001b40, "fmtvrh",         CFMTVR  },
    { 0xffe0fff0, 0xf4001b60, "fmtvrl",         CFMTVR  },
    { 0xfe10fe10, 0xf8001400, "vins.8",         CVINS   },
    { 0xfe10fe10, 0xf8101400, "vins.16",        CVINS   },
    { 0xfe10fe10, 0xfa001400, "vins.32",        CVINS   },
    { 0xfe1fff00, 0xf4003800, "flrws",          FLRW   },
    { 0xfe1fff00, 0xf4003900, "flrwd",          FLRW   },
    { 0xfe00ff00, 0xf4001c00, "fmovis",         FMOVI   },
    { 0xfe00ff00, 0xf4001e00, "fmovid",         FMOVI   },
    { 0, 0, 0, 0}
};

INST_PARSE_INFO csky_inst_info_16[] = {
    { 0xffff, 0x783c, "rts", HBKPT},
    { 0xffff, 0x0000, "bkpt", HBKPT},

    { 0xfc00, 0x0400, "br", HBR },
    { 0xfc00, 0x0000, "bsr", HBR },
    { 0xfc00, 0x0800, "bt", HBR },
    { 0xfc00, 0x0c00, "bf", HBR },

    { 0xfc3f, 0x6403, "mvcv", HMVCV },

    { 0xfc03, 0x6803, "tstnbz", HTSTNBZ },
    { 0xffc3, 0x7800, "jmp", HTSTNBZ },
    { 0xffc3, 0x7801, "jsr", HTSTNBZ },

    { 0xfc03, 0x6400, "cmphs", HCMPHS },
    { 0xfc03, 0x6401, "cmplt", HCMPHS },
    { 0xfc03, 0x6402, "cmpne", HCMPHS },
    { 0xfc03, 0x6802, "tst", HCMPHS },

    { 0xf8e0, 0x3840, "cmpnei", HCMPNEI },
    { 0xf8e0, 0x3880, "bclri", HCMPNEI   },
    { 0xf8e0, 0x38a0, "bseti", HCMPNEI },

    { 0xf8e0, 0x3820, "cmplti", HCMPLTI },
    { 0xf8e0, 0x3800, "cmphsi", HCMPLTI },

    { 0xf800, 0x3000, "movi", HMOVI },

    { 0xfc03, 0x7400, "zextb", HZEXTB },
    { 0xfc03, 0x7401, "zexth", HZEXTB },
    { 0xfc03, 0x7402, "sextb", HZEXTB },
    { 0xfc03, 0x7403, "sexth", HZEXTB },
    { 0xfc03, 0x7802, "revb", HZEXTB },
    { 0xfc03, 0x7803, "revh", HZEXTB },
    { 0xfc03, 0x6001, "addc", HZEXTB },
    { 0xfc03, 0x6003, "subc", HZEXTB },
    { 0xfc03, 0x6800, "and", HZEXTB },
    { 0xfc03, 0x6801, "andn", HZEXTB },
    { 0xfc03, 0x6c00, "or", HZEXTB },
    { 0xfc03, 0x6c01, "xor", HZEXTB },
    { 0xfc03, 0x6c02, "nor", HZEXTB },
    { 0xfc03, 0x6c03, "mov", HZEXTB },
    { 0xfc03, 0x7000, "lsl", HZEXTB },
    { 0xfc03, 0x7001, "lsr", HZEXTB },
    { 0xfc03, 0x7002, "asr", HZEXTB },
    { 0xfc03, 0x7003, "rotl", HZEXTB },
    { 0xfc03, 0x7c00, "mult", HZEXTB },
    { 0xfc03, 0x7c01, "mulsh", HZEXTB},

    { 0xf800, 0x4000, "lsli", HLSLI },
    { 0xf800, 0x4800, "lsri", HLSLI },
    { 0xf800, 0x5000, "asri", HLSLI },

    { 0xf800, 0x8000, "ld.b", HLDB },
    { 0xf800, 0xa000, "st.b", HLDB },
    { 0xf800, 0x8800, "ld.h", HLDH },
    { 0xf800, 0xa800, "st.h", HLDH },
    { 0xf800, 0x9000, "ld.w", HLDW },
    { 0xf800, 0x9800, "ld.w", HLDWSP },
    { 0xf800, 0xb000, "st.w", HLDW },
    { 0xf800, 0xb800, "st.w", HLDWSP },

    { 0xf800, 0x2000, "addi", HADDI8 },
    { 0xf803, 0x5802, "addi", HADDI3 },
    { 0xf800, 0x1800, "addi", HADDI8SP },
    { 0xfce0, 0x1400, "addi", HADDI7SP },

    { 0xf800, 0x2800, "subi", HADDI8 },
    { 0xf803, 0x5803, "subi", HADDI3 },
    { 0xfce0, 0x1420, "subi", HSUBI7SP },

    { 0xfee0, 0x1480, "pop", HPOP },
    { 0xfee0, 0x14c0, "push", HPOP },
    { 0xffe3, 0x14a0, "bpop.h", BPOP },
    { 0xffe3, 0x14a2, "bpop.w", BPOP },
    { 0xffe3, 0x14e0, "bpush.h", BPOP },
    { 0xffe3, 0x14e2, "bpush.w", BPOP },

    { 0xfc03, 0x6000, "addu", HZEXTB },
    { 0xf803, 0x5800, "addu", HADDU2 },
    { 0xfc03, 0x6002, "subu", HZEXTB },
    { 0xf803, 0x5801, "subu", HADDU2 },

    { 0xfc00, 0x1000, "lrw",  HLRW},

    { 0xf8fc, 0x38e0, "jmpix", HJMPIX},

    { 0, 0, 0, 0}
};

#if ALIAS
/*register corresponding name*/
static const char *v2_grname[] = {
    "a0", "a1", "a2", "a3", "l0", "l1", "l2", "l3",
    "l4", "l5", "l6", "l7", "t0", "t1", "sp", "lr",
    "l8", "l9", "t2", "t3", "t4", "t5", "t6", "t7",
    "t8", "t9", "r26", "r27", "rdb", "gb", "r30", "tls"};

#else
/*register corresponding name*/
static const char *v2_grname[] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "sp", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31"};
#endif
/*concatenate integer to string, according to the given radix*/
static void strcat_int(char *s, int value, int radix)
{
    char tmp[33];
    char *tp = tmp;
    int i;
    unsigned v;
    int sign;

    if (value == 0) {
        strcat(s, "0");
        return;
    }

    sign = (radix == 10 && value < 0);

    if (sign) {
        v = -value;
    } else {
        v = (unsigned)value;
    }

    while (v || tp == tmp) {
        i = v % radix;
        v = v / radix;
        if (i < 10) {
            *tp++ = i + '0';
        } else {
            *tp++ = i + 'a' - 10;
        }
    }

    if (sign) {
        strcat(s, "-");
    }
    s += strlen(s);
    while (tp > tmp) {
        *s++ = *--tp;
    }
}

static inline int clog2(int s)
{
    int r = 0;

    if (s == 0) {
        return -1;
    }
    while (s != 1) {
        s >>= 1;
        r++;
    }

    return r;
}

/*
void number_to_chars_littleendian(char *buf, CSKY_INST_TYPE val, int n)
{
    if (n <= 0) {
        abort();
    }
    while (n--) {
        *buf++ = val & 0xff;
        val >>= 8;
    }
}
*/

/*find instruction info according to instruction inst*/
INST_PARSE_INFO *csky_find_inst_info(CSKY_INST_TYPE inst, int length)
{
    int i;
    CSKY_INST_TYPE v;
    INST_PARSE_INFO *d;

    if (length == 2) {
        d = csky_inst_info_16;   /*16 bit instruction*/
    } else {
        d = csky_inst_info_32;    /*32 bit instruction*/
    }

    for (i = 0; d[i].name; i++) {
        v = d[i].mask & inst;     /*get instruction opcode*/
        if (v == d[i].opcode) {
            return &d[i];
        }
    }
    return NULL;
}

#define csky_read_data                                      \
do {                                                        \
    status = info->read_memory_func(memaddr, buf, 2, info); \
    if (status) {                                           \
        info->memory_error_func(status, memaddr, info);     \
        return info->bytes_per_chunk;                       \
    }                                                       \
    if (info->endian == BFD_ENDIAN_BIG) {                   \
        inst |= (buf[0] << 8) | buf[1];                     \
    } else if (info->endian == BFD_ENDIAN_LITTLE) {         \
        inst |= (buf[1] << 8) | buf[0];                     \
    } else {                                                \
        abort();                                            \
    }                                                       \
    info->bytes_per_chunk += 2;                             \
    memaddr += 2;                                           \
} while (0)

#define csky_output(s)  \
do {                    \
    strcat(str, s);     \
} while (0)

static const char *fields[] = {
    "", "af", "fe", "fe,af", "ie", "ie,af", "ie,fe",
    "ie,fe,af", "ee", "ee,af", "ee,fe", "ee,fe,af",
    "ee,ie", "ee,ie,af", "ee,ie,fe", "ee,ie,fe,af"
};

int print_insn_csky_v2(bfd_vma memaddr, struct disassemble_info *info)
{
    unsigned char buf[2];
    CSKY_INST_TYPE inst;
    CSKY_INST_TYPE inst_lbytes;
    int status;
    char str[40];
    memset(str, 0, sizeof(str));
    info->bytes_per_chunk = 0;
    inst = 0;

    csky_read_data;
#define insn_is_32bit ((inst & 0xc000) == 0xc000)
    if (insn_is_32bit) {
        inst <<= 16;
        csky_read_data;
    }

    INST_PARSE_INFO *ii;
    ii = csky_find_inst_info(inst, info->bytes_per_chunk);
    if (ii == NULL) {
        info->fprintf_func(info->stream, " .long: 0x%08lx", inst);
        return info->bytes_per_chunk;
    }

    /* disassemble start: */

    int method;
    int c;

    int v = -999; /* primary value dump */
    int w = -999; /* secondary value dump */
    int t = -999;

    int need_comma;

    strcat(str, ii->name);

    c = 0;
    need_comma = 0;

#define GetData     (ii->data[c++])
#define NOTEND      (method != MEND && method != MERR)
#define cout(s)     (strcat(str, s))
#define cprint(s)   (info->fprintf_func(info->stream, s))

    if (ii->data[0] != MEND && ii->data[0] != MERR) {
        strcat(str, "\t");
    }

    for (method = GetData; NOTEND;) {
        if (need_comma > 0) {
            if (method != MSPE) {
                strcat(str, ", ");
            }
            need_comma = 0;
        }

        /* calculate */
        switch (method) {
        case MSPE:
        case MSPE1:
        case MSP:
        case MR28:
        case MPRINT:
        case MPOP16:
        case MPOP32:
            break;
        case MIMM1:
            v = (inst & (CSKY_INST_TYPE)GetData);
            t = GetData;
            v = v + t;
            break;
        case MLRW16:
            v = (inst & (CSKY_INST_TYPE)GetData);
            t = GetData;

            if (t >= 0) {
                v >>= t;
            } else {
                v <<= -t;
            }

            w = (inst & (CSKY_INST_TYPE)GetData);
            t = GetData;
            if (t >= 0) {
                v = ((v | w) >> t);
            } else {
                v = ((v | w) << -t);
            }

            break;
        case MADDISPH:
        case MADDISP:
            v = (inst & (CSKY_INST_TYPE)GetData);
            t = GetData;

            if (t >= 0) {
                v >>= t;
            } else {
                v <<= -t;
            }

            w = (inst & (CSKY_INST_TYPE)GetData);
            t = GetData;

            if (t >= 0) {
                v = ((v | w) >> t);
            } else {
                v = ((v | w) << -t);
            }

            break;
        default:
            v = (inst & (CSKY_INST_TYPE)GetData);
            t = GetData;
            if (t >= 0) {
                v >>= t;
            } else {
                v <<= -t;
            }
        }

        switch (method) {
        case MADD:
        case MADDD:
        case MSUBD:
            w = (inst & (CSKY_INST_TYPE)GetData);
            t = GetData;
            if (t > 0) {
                w >>= t;
            } else {
                w <<= -t;
            }
            break;
        }
        switch (method) {
        case MLOG2:
            v = clog2(v);
            break;
        case MIMMH:
        case MIMM:
            v += GetData;
            break;
        case MIMM1:
            t = GetData;
            if (t > 0) {
                v >>= t;
            } else {
                v <<= -t;
            }

            break;
        case MADD:
        case MADDD:
            v += w;
            break;
        case MSUBD:
            v -= w;
            break;
        }

        /* print */
        switch (method) {
        case MREG:
            strcat(str, v2_grname[v]);
            break;
        case MIMM0:
        case MIMM:
        case MIMM1:
        case MADDD:
        case MSUBD:
        case MLOG2:
        case MADDISP:
        case MADD:
            strcat_int(str, v, 10);
            break;
        case MIMMH:
        case MIMM0H:
        case MLRS:
        case MADDISPH:
            strcat(str, "0x");
            strcat_int(str, v, 16);
            break;
        case MLDM:
            strcat(str, v2_grname[v + ((inst & 0x3e00000) >> 21)]);
            break;
        case MSYM:
            /* PC + sign_extend(offset << 1) */
            if (v & (CSKY_INST_TYPE)GetData) {
                v |= (CSKY_INST_TYPE)GetData;   /* sign extension */
            }
            v += memaddr - info->bytes_per_chunk;
            strcat(str, "0x");
            strcat_int(str, v, 16);
            info->fprintf_func(info->stream, "%s", str);
            if (info->print_address_func) {
                info->fprintf_func(info->stream, "\t//");
                info->print_address_func(v, info);
            }
            return info->bytes_per_chunk;
        case MLRW16:
        case MSYMI:
            /* MEM[(PC + unsign_extend(offset << 2)) & 0xFFFFFFFC] */
            v = (memaddr - info->bytes_per_chunk + v) & 0xFFFFFFFC;
            inst = 0;
            memaddr = v;
            if (info->endian == BFD_ENDIAN_BIG) {
                csky_read_data;
                inst <<= 16;
                csky_read_data;
            } else if (info->endian == BFD_ENDIAN_LITTLE) {
                csky_read_data;
                inst_lbytes = inst;
                inst = 0;
                csky_read_data;
                inst <<= 16;
                inst = inst | inst_lbytes;
            } else {
                abort();
            }

            info->bytes_per_chunk -= 4;
            strcat(str, "0x");
            strcat_int(str, inst, 16);
            info->fprintf_func(info->stream, "%s", str);

            if (info->print_address_func && inst != 0) {
                info->fprintf_func(info->stream, "\t//");
                info->print_address_func(inst, info);
            } else {
                info->fprintf_func(info->stream,
                                   "\t// from address pool at 0x%lx",
                                   (long unsigned int)memaddr);
            }

            return info->bytes_per_chunk;
        case MPSR:
            strcat(str, fields[v & 0xf]);
            break;
        case MSP:
            strcat(str, "sp");
            break;
        case MPRINT:
            strcat(str, "\t// the offset is based on .data");
            break;
        case MR28:
            strcat(str, v2_grname[28]);
            break;
        case MSPE:
        case MSPE1:
            str[strlen(str)] = (char)GetData;
            break;
        case MPOP16:
            if (inst & 0xf) {
                strcat(str, v2_grname[4]);
                if ((inst & 0xf) == 1) {
                    /* nothing */
                } else {
                    strcat(str, "-");
                    strcat(str, v2_grname[3 + (inst & 0xf)]);
                }
                need_comma++;
            }
            if (inst & 0x10) {
                if (need_comma > 0) {
                    strcat(str, ", ");
                    need_comma = 0;
                }
                strcat(str, v2_grname[15]);
                need_comma++;
            }
            need_comma = 0;
            break;
        case MPOP32:
            if (inst & 0xf) {
                strcat(str, v2_grname[4]);
                if ((inst & 0xf) == 1) {
                    /* nothing */
                } else {
                    strcat(str, "-");
                    strcat(str, v2_grname[3 + (inst & 0xf)]);
                }
                need_comma++;
            }

            if (inst & 0x10) {
                if (need_comma > 0) {
                    strcat(str, ", ");
                    need_comma = 0;
                }
                strcat(str, v2_grname[15]);
                need_comma++;
            }

            if (inst & 0xe0) {
                if (need_comma > 0) {
                    strcat(str, ", ");
                    need_comma = 0;
                }
                strcat(str, v2_grname[16]);
                if (((inst & 0xe0) >> 5) == 1) {
                    /* nothing */
                } else {
                    strcat(str, "-");
                    strcat(str, v2_grname[17]);
                }
                need_comma++;
            }

            if (inst & 0x100) {
                if (need_comma > 0) {
                    strcat(str, ", ");
                    need_comma = 0;
                }
                strcat(str, v2_grname[29]);
                need_comma++;
            }

            need_comma = 0;
            break;

        }
        switch (method) {
        case MSPE:
        case MSPE1:
            method = GetData;
            continue;
        }

        need_comma++;
        method = GetData;
    }

    info->fprintf_func(info->stream, "%s", str);

    return info->bytes_per_chunk;
}
