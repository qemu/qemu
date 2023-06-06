/*
 *  Arguments to the opcode prototypes
 *
 *  C(OPC,    NAME,    FMT,   FAC, I1, I2, P, W, OP, CC)
 *  D(OPC,    NAME,    FMT,   FAC, I1, I2, P, W, OP, CC, DATA)
 *  E(OPC,    NAME,    FMT,   FAC, I1, I2, P, W, OP, CC, DATA, FLAGS)
 *  F(OPC,    NAME,    FMT,   FAC, I1, I2, P, W, OP, CC, FLAGS)
 *
 *  OPC  = (op << 8) | op2 where op is the major, op2 the minor opcode
 *  NAME = name of the opcode, used internally
 *  FMT  = format of the opcode (defined in insn-format.h.inc)
 *  FAC  = facility the opcode is available in (defined in DisasFacility)
 *  I1   = func in1_xx fills o->in1
 *  I2   = func in2_xx fills o->in2
 *  P    = func prep_xx initializes o->*out*
 *  W    = func wout_xx writes o->*out* somewhere
 *  OP   = func op_xx does the bulk of the operation
 *  CC   = func cout_xx defines how cc should get set
 *  DATA = immediate argument to op_xx function
 *  FLAGS = categorize the type of instruction (e.g. for advanced checks)
 *
 *  The helpers get called in order: I1, I2, P, OP, W, CC
 */

/* ADD */
    C(0x1a00, AR,      RR_a,  Z,   r1, r2, new, r1_32, add, adds32)
    C(0xb9f8, ARK,     RRF_a, DO,  r2, r3, new, r1_32, add, adds32)
    C(0x5a00, A,       RX_a,  Z,   r1, m2_32s, new, r1_32, add, adds32)
    C(0xe35a, AY,      RXY_a, LD,  r1, m2_32s, new, r1_32, add, adds32)
    C(0xb908, AGR,     RRE,   Z,   r1, r2, r1, 0, add, adds64)
    C(0xb918, AGFR,    RRE,   Z,   r1, r2_32s, r1, 0, add, adds64)
    C(0xb9e8, AGRK,    RRF_a, DO,  r2, r3, r1, 0, add, adds64)
    C(0xe308, AG,      RXY_a, Z,   r1, m2_64, r1, 0, add, adds64)
    C(0xe318, AGF,     RXY_a, Z,   r1, m2_32s, r1, 0, add, adds64)
    F(0xb30a, AEBR,    RRE,   Z,   e1, e2, new, e1, aeb, f32, IF_BFP)
    F(0xb31a, ADBR,    RRE,   Z,   f1, f2, new, f1, adb, f64, IF_BFP)
    F(0xb34a, AXBR,    RRE,   Z,   x1, x2, new_x, x1, axb, f128, IF_BFP)
    F(0xed0a, AEB,     RXE,   Z,   e1, m2_32u, new, e1, aeb, f32, IF_BFP)
    F(0xed1a, ADB,     RXE,   Z,   f1, m2_64, new, f1, adb, f64, IF_BFP)
/* ADD HIGH */
    C(0xb9c8, AHHHR,   RRF_a, HW,  r2_sr32, r3_sr32, new, r1_32h, add, adds32)
    C(0xb9d8, AHHLR,   RRF_a, HW,  r2_sr32, r3, new, r1_32h, add, adds32)
/* ADD IMMEDIATE */
    C(0xc209, AFI,     RIL_a, EI,  r1, i2, new, r1_32, add, adds32)
    D(0xeb6a, ASI,     SIY,   GIE, la1, i2, new, 0, asi, adds32, MO_TESL)
    C(0xecd8, AHIK,    RIE_d, DO,  r3, i2, new, r1_32, add, adds32)
    C(0xc208, AGFI,    RIL_a, EI,  r1, i2, r1, 0, add, adds64)
    D(0xeb7a, AGSI,    SIY,   GIE, la1, i2, new, 0, asi, adds64, MO_TEUQ)
    C(0xecd9, AGHIK,   RIE_d, DO,  r3, i2, r1, 0, add, adds64)
/* ADD IMMEDIATE HIGH */
    C(0xcc08, AIH,     RIL_a, HW,  r1_sr32, i2, new, r1_32h, add, adds32)
/* ADD HALFWORD */
    C(0x4a00, AH,      RX_a,  Z,   r1, m2_16s, new, r1_32, add, adds32)
    C(0xe37a, AHY,     RXY_a, LD,  r1, m2_16s, new, r1_32, add, adds32)
    C(0xe338, AGH,     RXY_a, MIE2,r1, m2_16s, r1, 0, add, adds64)
/* ADD HALFWORD IMMEDIATE */
    C(0xa70a, AHI,     RI_a,  Z,   r1, i2, new, r1_32, add, adds32)
    C(0xa70b, AGHI,    RI_a,  Z,   r1, i2, r1, 0, add, adds64)

/* ADD LOGICAL */
    C(0x1e00, ALR,     RR_a,  Z,   r1_32u, r2_32u, new, r1_32, add, addu32)
    C(0xb9fa, ALRK,    RRF_a, DO,  r2_32u, r3_32u, new, r1_32, add, addu32)
    C(0x5e00, AL,      RX_a,  Z,   r1_32u, m2_32u, new, r1_32, add, addu32)
    C(0xe35e, ALY,     RXY_a, LD,  r1_32u, m2_32u, new, r1_32, add, addu32)
    C(0xb90a, ALGR,    RRE,   Z,   r1, r2, r1, 0, addu64, addu64)
    C(0xb91a, ALGFR,   RRE,   Z,   r1, r2_32u, r1, 0, addu64, addu64)
    C(0xb9ea, ALGRK,   RRF_a, DO,  r2, r3, r1, 0, addu64, addu64)
    C(0xe30a, ALG,     RXY_a, Z,   r1, m2_64, r1, 0, addu64, addu64)
    C(0xe31a, ALGF,    RXY_a, Z,   r1, m2_32u, r1, 0, addu64, addu64)
/* ADD LOGICAL HIGH */
    C(0xb9ca, ALHHHR,  RRF_a, HW,  r2_sr32, r3_sr32, new, r1_32h, add, addu32)
    C(0xb9da, ALHHLR,  RRF_a, HW,  r2_sr32, r3_32u, new, r1_32h, add, addu32)
/* ADD LOGICAL IMMEDIATE */
    C(0xc20b, ALFI,    RIL_a, EI,  r1_32u, i2_32u, new, r1_32, add, addu32)
    C(0xc20a, ALGFI,   RIL_a, EI,  r1, i2_32u, r1, 0, addu64, addu64)
/* ADD LOGICAL WITH SIGNED IMMEDIATE */
    D(0xeb6e, ALSI,    SIY,   GIE, la1, i2_32u, new, 0, asi, addu32, MO_TEUL)
    C(0xecda, ALHSIK,  RIE_d, DO,  r3_32u, i2_32u, new, r1_32, add, addu32)
    D(0xeb7e, ALGSI,   SIY,   GIE, la1, i2, new, 0, asiu64, addu64, MO_TEUQ)
    C(0xecdb, ALGHSIK, RIE_d, DO,  r3, i2, r1, 0, addu64, addu64)
/* ADD LOGICAL WITH SIGNED IMMEDIATE HIGH */
    C(0xcc0a, ALSIH,   RIL_a, HW,  r1_sr32, i2_32u, new, r1_32h, add, addu32)
    C(0xcc0b, ALSIHN,  RIL_a, HW,  r1_sr32, i2_32u, new, r1_32h, add, 0)
/* ADD LOGICAL WITH CARRY */
    C(0xb998, ALCR,    RRE,   Z,   r1_32u, r2_32u, new, r1_32, addc32, addu32)
    C(0xb988, ALCGR,   RRE,   Z,   r1, r2, r1, 0, addc64, addu64)
    C(0xe398, ALC,     RXY_a, Z,   r1_32u, m2_32u, new, r1_32, addc32, addu32)
    C(0xe388, ALCG,    RXY_a, Z,   r1, m2_64, r1, 0, addc64, addu64)

/* AND */
    C(0x1400, NR,      RR_a,  Z,   r1, r2, new, r1_32, and, nz32)
    C(0xb9f4, NRK,     RRF_a, DO,  r2, r3, new, r1_32, and, nz32)
    C(0x5400, N,       RX_a,  Z,   r1, m2_32s, new, r1_32, and, nz32)
    C(0xe354, NY,      RXY_a, LD,  r1, m2_32s, new, r1_32, and, nz32)
    C(0xb980, NGR,     RRE,   Z,   r1, r2, r1, 0, and, nz64)
    C(0xb9e4, NGRK,    RRF_a, DO,  r2, r3, r1, 0, and, nz64)
    C(0xe380, NG,      RXY_a, Z,   r1, m2_64, r1, 0, and, nz64)
    C(0xd400, NC,      SS_a,  Z,   la1, a2, 0, 0, nc, 0)
/* AND IMMEDIATE */
    D(0xc00a, NIHF,    RIL_a, EI,  r1_o, i2_32u, r1, 0, andi, 0, 0x2020)
    D(0xc00b, NILF,    RIL_a, EI,  r1_o, i2_32u, r1, 0, andi, 0, 0x2000)
    D(0xa504, NIHH,    RI_a,  Z,   r1_o, i2_16u, r1, 0, andi, 0, 0x1030)
    D(0xa505, NIHL,    RI_a,  Z,   r1_o, i2_16u, r1, 0, andi, 0, 0x1020)
    D(0xa506, NILH,    RI_a,  Z,   r1_o, i2_16u, r1, 0, andi, 0, 0x1010)
    D(0xa507, NILL,    RI_a,  Z,   r1_o, i2_16u, r1, 0, andi, 0, 0x1000)
    D(0x9400, NI,      SI,    Z,   la1, i2_8u, new, 0, ni, nz64, MO_UB)
    D(0xeb54, NIY,     SIY,   LD,  la1, i2_8u, new, 0, ni, nz64, MO_UB)
/* AND WITH COMPLEMENT */
    C(0xb9f5, NCRK,    RRF_a, MIE3, r2, r3, new, r1_32, andc, nz32)
    C(0xb9e5, NCGRK,   RRF_a, MIE3, r2, r3, r1, 0, andc, nz64)

/* BRANCH AND LINK */
    C(0x0500, BALR,    RR_a,  Z,   0, r2_nz, r1, 0, bal, 0)
    C(0x4500, BAL,     RX_a,  Z,   0, a2, r1, 0, bal, 0)
/* BRANCH AND SAVE */
    C(0x0d00, BASR,    RR_a,  Z,   0, r2_nz, r1, 0, bas, 0)
    C(0x4d00, BAS,     RX_a,  Z,   0, a2, r1, 0, bas, 0)
/* BRANCH RELATIVE AND SAVE */
    C(0xa705, BRAS,    RI_b,  Z,   0, 0, r1, 0, basi, 0)
    C(0xc005, BRASL,   RIL_b, Z,   0, 0, r1, 0, basi, 0)
/* BRANCH INDIRECT ON CONDITION */
    C(0xe347, BIC,     RXY_b, MIE2,0, m2_64w, 0, 0, bc, 0)
/* BRANCH ON CONDITION */
    C(0x0700, BCR,     RR_b,  Z,   0, r2_nz, 0, 0, bc, 0)
    C(0x4700, BC,      RX_b,  Z,   0, a2, 0, 0, bc, 0)
/* BRANCH RELATIVE ON CONDITION */
    C(0xa704, BRC,     RI_c,  Z,   0, 0, 0, 0, bc, 0)
    C(0xc004, BRCL,    RIL_c, Z,   0, 0, 0, 0, bc, 0)
/* BRANCH ON COUNT */
    C(0x0600, BCTR,    RR_a,  Z,   0, r2_nz, 0, 0, bct32, 0)
    C(0xb946, BCTGR,   RRE,   Z,   0, r2_nz, 0, 0, bct64, 0)
    C(0x4600, BCT,     RX_a,  Z,   0, a2, 0, 0, bct32, 0)
    C(0xe346, BCTG,    RXY_a, Z,   0, a2, 0, 0, bct64, 0)
/* BRANCH RELATIVE ON COUNT */
    C(0xa706, BRCT,    RI_b,  Z,   0, 0, 0, 0, bct32, 0)
    C(0xa707, BRCTG,   RI_b,  Z,   0, 0, 0, 0, bct64, 0)
/* BRANCH RELATIVE ON COUNT HIGH */
    C(0xcc06, BRCTH,   RIL_b, HW,  0, 0, 0, 0, bcth, 0)
/* BRANCH ON INDEX */
    D(0x8600, BXH,     RS_a,  Z,   0, a2, 0, 0, bx32, 0, 0)
    D(0x8700, BXLE,    RS_a,  Z,   0, a2, 0, 0, bx32, 0, 1)
    D(0xeb44, BXHG,    RSY_a, Z,   0, a2, 0, 0, bx64, 0, 0)
    D(0xeb45, BXLEG,   RSY_a, Z,   0, a2, 0, 0, bx64, 0, 1)
/* BRANCH RELATIVE ON INDEX */
    D(0x8400, BRXH,    RSI,   Z,   0, 0, 0, 0, bx32, 0, 0)
    D(0x8500, BRXLE,   RSI,   Z,   0, 0, 0, 0, bx32, 0, 1)
    D(0xec44, BRXHG,   RIE_e, Z,   0, 0, 0, 0, bx64, 0, 0)
    D(0xec45, BRXHLE,  RIE_e, Z,   0, 0, 0, 0, bx64, 0, 1)
/* BRANCH PREDICTION PRELOAD */
    /* ??? Format is SMI, but implemented as NOP, so we need no fields.  */
    C(0xc700, BPP,     E,     EH,  0, 0, 0, 0, 0, 0)
/* BRANCH PREDICTION RELATIVE PRELOAD */
    /* ??? Format is MII, but implemented as NOP, so we need no fields.  */
    C(0xc500, BPRP,    E,     EH,  0, 0, 0, 0, 0, 0)
/* NEXT INSTRUCTION ACCESS INTENT */
    /* ??? Format is IE, but implemented as NOP, so we need no fields.  */
    C(0xb2fa, NIAI,    E,     EH,  0, 0, 0, 0, 0, 0)

/* CHECKSUM */
    C(0xb241, CKSM,    RRE,   Z,   r1_o, ra2, new, r1_32, cksm, 0)

/* COPY SIGN */
    F(0xb372, CPSDR,   RRF_b, FPSSH, f3, f2, new, f1, cps, 0, IF_AFP1 | IF_AFP2 | IF_AFP3)

/* COMPARE */
    C(0x1900, CR,      RR_a,  Z,   r1_o, r2_o, 0, 0, 0, cmps32)
    C(0x5900, C,       RX_a,  Z,   r1_o, m2_32s, 0, 0, 0, cmps32)
    C(0xe359, CY,      RXY_a, LD,  r1_o, m2_32s, 0, 0, 0, cmps32)
    C(0xb920, CGR,     RRE,   Z,   r1_o, r2_o, 0, 0, 0, cmps64)
    C(0xb930, CGFR,    RRE,   Z,   r1_o, r2_32s, 0, 0, 0, cmps64)
    C(0xe320, CG,      RXY_a, Z,   r1_o, m2_64, 0, 0, 0, cmps64)
    C(0xe330, CGF,     RXY_a, Z,   r1_o, m2_32s, 0, 0, 0, cmps64)
    F(0xb309, CEBR,    RRE,   Z,   e1, e2, 0, 0, ceb, 0, IF_BFP)
    F(0xb319, CDBR,    RRE,   Z,   f1, f2, 0, 0, cdb, 0, IF_BFP)
    F(0xb349, CXBR,    RRE,   Z,   x1, x2, 0, 0, cxb, 0, IF_BFP)
    F(0xed09, CEB,     RXE,   Z,   e1, m2_32u, 0, 0, ceb, 0, IF_BFP)
    F(0xed19, CDB,     RXE,   Z,   f1, m2_64, 0, 0, cdb, 0, IF_BFP)
/* COMPARE AND SIGNAL */
    F(0xb308, KEBR,    RRE,   Z,   e1, e2, 0, 0, keb, 0, IF_BFP)
    F(0xb318, KDBR,    RRE,   Z,   f1, f2, 0, 0, kdb, 0, IF_BFP)
    F(0xb348, KXBR,    RRE,   Z,   x1, x2, 0, 0, kxb, 0, IF_BFP)
    F(0xed08, KEB,     RXE,   Z,   e1, m2_32u, 0, 0, keb, 0, IF_BFP)
    F(0xed18, KDB,     RXE,   Z,   f1, m2_64, 0, 0, kdb, 0, IF_BFP)
/* COMPARE IMMEDIATE */
    C(0xc20d, CFI,     RIL_a, EI,  r1, i2, 0, 0, 0, cmps32)
    C(0xc20c, CGFI,    RIL_a, EI,  r1, i2, 0, 0, 0, cmps64)
/* COMPARE RELATIVE LONG */
    C(0xc60d, CRL,     RIL_b, GIE, r1, mri2_32s, 0, 0, 0, cmps32)
    C(0xc608, CGRL,    RIL_b, GIE, r1, mri2_64, 0, 0, 0, cmps64)
    C(0xc60c, CGFRL,   RIL_b, GIE, r1, mri2_32s, 0, 0, 0, cmps64)
/* COMPARE HALFWORD */
    C(0x4900, CH,      RX_a,  Z,   r1_o, m2_16s, 0, 0, 0, cmps32)
    C(0xe379, CHY,     RXY_a, LD,  r1_o, m2_16s, 0, 0, 0, cmps32)
    C(0xe334, CGH,     RXY_a, GIE, r1_o, m2_16s, 0, 0, 0, cmps64)
/* COMPARE HALFWORD IMMEDIATE */
    C(0xa70e, CHI,     RI_a,  Z,   r1_o, i2, 0, 0, 0, cmps32)
    C(0xa70f, CGHI,    RI_a,  Z,   r1_o, i2, 0, 0, 0, cmps64)
    C(0xe554, CHHSI,   SIL,   GIE, m1_16s, i2, 0, 0, 0, cmps64)
    C(0xe55c, CHSI,    SIL,   GIE, m1_32s, i2, 0, 0, 0, cmps64)
    C(0xe558, CGHSI,   SIL,   GIE, m1_64, i2, 0, 0, 0, cmps64)
/* COMPARE HALFWORD RELATIVE LONG */
    C(0xc605, CHRL,    RIL_b, GIE, r1_o, mri2_16s, 0, 0, 0, cmps32)
    C(0xc604, CGHRL,   RIL_b, GIE, r1_o, mri2_16s, 0, 0, 0, cmps64)
/* COMPARE HIGH */
    C(0xb9cd, CHHR,    RRE,   HW,  r1_sr32, r2_sr32, 0, 0, 0, cmps32)
    C(0xb9dd, CHLR,    RRE,   HW,  r1_sr32, r2_o, 0, 0, 0, cmps32)
    C(0xe3cd, CHF,     RXY_a, HW,  r1_sr32, m2_32s, 0, 0, 0, cmps32)
/* COMPARE IMMEDIATE HIGH */
    C(0xcc0d, CIH,     RIL_a, HW,  r1_sr32, i2, 0, 0, 0, cmps32)

/* COMPARE LOGICAL */
    C(0x1500, CLR,     RR_a,  Z,   r1, r2, 0, 0, 0, cmpu32)
    C(0x5500, CL,      RX_a,  Z,   r1, m2_32s, 0, 0, 0, cmpu32)
    C(0xe355, CLY,     RXY_a, LD,  r1, m2_32s, 0, 0, 0, cmpu32)
    C(0xb921, CLGR,    RRE,   Z,   r1, r2, 0, 0, 0, cmpu64)
    C(0xb931, CLGFR,   RRE,   Z,   r1, r2_32u, 0, 0, 0, cmpu64)
    C(0xe321, CLG,     RXY_a, Z,   r1, m2_64, 0, 0, 0, cmpu64)
    C(0xe331, CLGF,    RXY_a, Z,   r1, m2_32u, 0, 0, 0, cmpu64)
    C(0xd500, CLC,     SS_a,  Z,   la1, a2, 0, 0, clc, 0)
/* COMPARE LOGICAL HIGH */
    C(0xb9cf, CLHHR,   RRE,   HW,  r1_sr32, r2_sr32, 0, 0, 0, cmpu32)
    C(0xb9df, CLHLR,   RRE,   HW,  r1_sr32, r2_o, 0, 0, 0, cmpu32)
    C(0xe3cf, CLHF,    RXY_a, HW,  r1_sr32, m2_32s, 0, 0, 0, cmpu32)
/* COMPARE LOGICAL IMMEDIATE */
    C(0xc20f, CLFI,    RIL_a, EI,  r1, i2, 0, 0, 0, cmpu32)
    C(0xc20e, CLGFI,   RIL_a, EI,  r1, i2_32u, 0, 0, 0, cmpu64)
    C(0x9500, CLI,     SI,    Z,   m1_8u, i2_8u, 0, 0, 0, cmpu64)
    C(0xeb55, CLIY,    SIY,   LD,  m1_8u, i2_8u, 0, 0, 0, cmpu64)
    C(0xe555, CLHHSI,  SIL,   GIE, m1_16u, i2_16u, 0, 0, 0, cmpu64)
    C(0xe55d, CLFHSI,  SIL,   GIE, m1_32u, i2_16u, 0, 0, 0, cmpu64)
    C(0xe559, CLGHSI,  SIL,   GIE, m1_64, i2_16u, 0, 0, 0, cmpu64)
/* COMPARE LOGICAL IMMEDIATE HIGH */
    C(0xcc0f, CLIH,    RIL_a, HW,  r1_sr32, i2, 0, 0, 0, cmpu32)
/* COMPARE LOGICAL RELATIVE LONG */
    C(0xc60f, CLRL,    RIL_b, GIE, r1_o, mri2_32u, 0, 0, 0, cmpu32)
    C(0xc60a, CLGRL,   RIL_b, GIE, r1_o, mri2_64, 0, 0, 0, cmpu64)
    C(0xc60e, CLGFRL,  RIL_b, GIE, r1_o, mri2_32u, 0, 0, 0, cmpu64)
    C(0xc607, CLHRL,   RIL_b, GIE, r1_o, mri2_16u, 0, 0, 0, cmpu32)
    C(0xc606, CLGHRL,  RIL_b, GIE, r1_o, mri2_16u, 0, 0, 0, cmpu64)
/* COMPARE LOGICAL LONG */
    C(0x0f00, CLCL,    RR_a,  Z,   0, 0, 0, 0, clcl, 0)
/* COMPARE LOGICAL LONG EXTENDED */
    C(0xa900, CLCLE,   RS_a,  Z,   0, a2, 0, 0, clcle, 0)
/* COMPARE LOGICAL LONG UNICODE */
    C(0xeb8f, CLCLU,   RSY_a, E2,  0, a2, 0, 0, clclu, 0)
/* COMPARE LOGICAL CHARACTERS UNDER MASK */
    C(0xbd00, CLM,     RS_b,  Z,   r1_o, a2, 0, 0, clm, 0)
    C(0xeb21, CLMY,    RSY_b, LD,  r1_o, a2, 0, 0, clm, 0)
    C(0xeb20, CLMH,    RSY_b, Z,   r1_sr32, a2, 0, 0, clm, 0)
/* COMPARE LOGICAL STRING */
    C(0xb25d, CLST,    RRE,   Z,   r1_o, r2_o, 0, 0, clst, 0)

/* COMPARE AND BRANCH */
    D(0xecf6, CRB,     RRS,   GIE, r1_32s, r2_32s, 0, 0, cj, 0, 0)
    D(0xece4, CGRB,    RRS,   GIE, r1_o, r2_o, 0, 0, cj, 0, 0)
    D(0xec76, CRJ,     RIE_b, GIE, r1_32s, r2_32s, 0, 0, cj, 0, 0)
    D(0xec64, CGRJ,    RIE_b, GIE, r1_o, r2_o, 0, 0, cj, 0, 0)
    D(0xecfe, CIB,     RIS,   GIE, r1_32s, i2, 0, 0, cj, 0, 0)
    D(0xecfc, CGIB,    RIS,   GIE, r1_o, i2, 0, 0, cj, 0, 0)
    D(0xec7e, CIJ,     RIE_c, GIE, r1_32s, i2, 0, 0, cj, 0, 0)
    D(0xec7c, CGIJ,    RIE_c, GIE, r1_o, i2, 0, 0, cj, 0, 0)
/* COMPARE LOGICAL AND BRANCH */
    D(0xecf7, CLRB,    RRS,   GIE, r1_32u, r2_32u, 0, 0, cj, 0, 1)
    D(0xece5, CLGRB,   RRS,   GIE, r1_o, r2_o, 0, 0, cj, 0, 1)
    D(0xec77, CLRJ,    RIE_b, GIE, r1_32u, r2_32u, 0, 0, cj, 0, 1)
    D(0xec65, CLGRJ,   RIE_b, GIE, r1_o, r2_o, 0, 0, cj, 0, 1)
    D(0xecff, CLIB,    RIS,   GIE, r1_32u, i2_8u, 0, 0, cj, 0, 1)
    D(0xecfd, CLGIB,   RIS,   GIE, r1_o, i2_8u, 0, 0, cj, 0, 1)
    D(0xec7f, CLIJ,    RIE_c, GIE, r1_32u, i2_8u, 0, 0, cj, 0, 1)
    D(0xec7d, CLGIJ,   RIE_c, GIE, r1_o, i2_8u, 0, 0, cj, 0, 1)

/* COMPARE AND SWAP */
    D(0xba00, CS,      RS_a,  Z,   r3_32u, r1_32u, new, r1_32, cs, 0, MO_TEUL)
    D(0xeb14, CSY,     RSY_a, LD,  r3_32u, r1_32u, new, r1_32, cs, 0, MO_TEUL)
    D(0xeb30, CSG,     RSY_a, Z,   r3_o, r1_o, new, r1, cs, 0, MO_TEUQ)
/* COMPARE DOUBLE AND SWAP */
    D(0xbb00, CDS,     RS_a,  Z,   r3_D32, r1_D32, new, r1_D32, cs, 0, MO_TEUQ)
    D(0xeb31, CDSY,    RSY_a, LD,  r3_D32, r1_D32, new, r1_D32, cs, 0, MO_TEUQ)
    C(0xeb3e, CDSG,    RSY_a, Z,   la2, r3_D64, 0, r1_D64, cdsg, 0)
/* COMPARE AND SWAP AND STORE */
    C(0xc802, CSST,    SSF,   CASS, la1, a2, 0, 0, csst, 0)

/* COMPARE AND TRAP */
    D(0xb972, CRT,     RRF_c, GIE, r1_32s, r2_32s, 0, 0, ct, 0, 0)
    D(0xb960, CGRT,    RRF_c, GIE, r1_o, r2_o, 0, 0, ct, 0, 0)
    D(0xec72, CIT,     RIE_a, GIE, r1_32s, i2, 0, 0, ct, 0, 0)
    D(0xec70, CGIT,    RIE_a, GIE, r1_o, i2, 0, 0, ct, 0, 0)
/* COMPARE LOGICAL AND TRAP */
    D(0xb973, CLRT,    RRF_c, GIE, r1_32u, r2_32u, 0, 0, ct, 0, 1)
    D(0xb961, CLGRT,   RRF_c, GIE, r1_o, r2_o, 0, 0, ct, 0, 1)
    D(0xeb23, CLT,     RSY_b, MIE, r1_32u, m2_32u, 0, 0, ct, 0, 1)
    D(0xeb2b, CLGT,    RSY_b, MIE, r1_o, m2_64, 0, 0, ct, 0, 1)
    D(0xec73, CLFIT,   RIE_a, GIE, r1_32u, i2_16u, 0, 0, ct, 0, 1)
    D(0xec71, CLGIT,   RIE_a, GIE, r1_o, i2_16u, 0, 0, ct, 0, 1)

/* CONVERT TO DECIMAL */
    C(0x4e00, CVD,     RX_a,  Z,   r1_o, a2, 0, 0, cvd, 0)
    C(0xe326, CVDY,    RXY_a, LD,  r1_o, a2, 0, 0, cvd, 0)
/* CONVERT TO FIXED */
    F(0xb398, CFEBR,   RRF_e, Z,   0, e2, new, r1_32, cfeb, 0, IF_BFP)
    F(0xb399, CFDBR,   RRF_e, Z,   0, f2, new, r1_32, cfdb, 0, IF_BFP)
    F(0xb39a, CFXBR,   RRF_e, Z,   0, x2, new, r1_32, cfxb, 0, IF_BFP)
    F(0xb3a8, CGEBR,   RRF_e, Z,   0, e2, r1, 0, cgeb, 0, IF_BFP)
    F(0xb3a9, CGDBR,   RRF_e, Z,   0, f2, r1, 0, cgdb, 0, IF_BFP)
    F(0xb3aa, CGXBR,   RRF_e, Z,   0, x2, r1, 0, cgxb, 0, IF_BFP)
/* CONVERT FROM FIXED */
    F(0xb394, CEFBR,   RRF_e, Z,   0, r2_32s, new, e1, cegb, 0, IF_BFP)
    F(0xb395, CDFBR,   RRF_e, Z,   0, r2_32s, new, f1, cdgb, 0, IF_BFP)
    F(0xb396, CXFBR,   RRF_e, Z,   0, r2_32s, new_x, x1, cxgb, 0, IF_BFP)
    F(0xb3a4, CEGBR,   RRF_e, Z,   0, r2_o, new, e1, cegb, 0, IF_BFP)
    F(0xb3a5, CDGBR,   RRF_e, Z,   0, r2_o, new, f1, cdgb, 0, IF_BFP)
    F(0xb3a6, CXGBR,   RRF_e, Z,   0, r2_o, new_x, x1, cxgb, 0, IF_BFP)
/* CONVERT TO LOGICAL */
    F(0xb39c, CLFEBR,  RRF_e, FPE, 0, e2, new, r1_32, clfeb, 0, IF_BFP)
    F(0xb39d, CLFDBR,  RRF_e, FPE, 0, f2, new, r1_32, clfdb, 0, IF_BFP)
    F(0xb39e, CLFXBR,  RRF_e, FPE, 0, x2, new, r1_32, clfxb, 0, IF_BFP)
    F(0xb3ac, CLGEBR,  RRF_e, FPE, 0, e2, r1, 0, clgeb, 0, IF_BFP)
    F(0xb3ad, CLGDBR,  RRF_e, FPE, 0, f2, r1, 0, clgdb, 0, IF_BFP)
    F(0xb3ae, CLGXBR,  RRF_e, FPE, 0, x2, r1, 0, clgxb, 0, IF_BFP)
/* CONVERT FROM LOGICAL */
    F(0xb390, CELFBR,  RRF_e, FPE, 0, r2_32u, new, e1, celgb, 0, IF_BFP)
    F(0xb391, CDLFBR,  RRF_e, FPE, 0, r2_32u, new, f1, cdlgb, 0, IF_BFP)
    F(0xb392, CXLFBR,  RRF_e, FPE, 0, r2_32u, new_x, x1, cxlgb, 0, IF_BFP)
    F(0xb3a0, CELGBR,  RRF_e, FPE, 0, r2_o, new, e1, celgb, 0, IF_BFP)
    F(0xb3a1, CDLGBR,  RRF_e, FPE, 0, r2_o, new, f1, cdlgb, 0, IF_BFP)
    F(0xb3a2, CXLGBR,  RRF_e, FPE, 0, r2_o, new_x, x1, cxlgb, 0, IF_BFP)

/* CONVERT UTF-8 TO UTF-16 */
    D(0xb2a7, CU12,    RRF_c, Z,   0, 0, 0, 0, cuXX, 0, 12)
/* CONVERT UTF-8 TO UTF-32 */
    D(0xb9b0, CU14,    RRF_c, ETF3, 0, 0, 0, 0, cuXX, 0, 14)
/* CONVERT UTF-16 to UTF-8 */
    D(0xb2a6, CU21,    RRF_c, Z,   0, 0, 0, 0, cuXX, 0, 21)
/* CONVERT UTF-16 to UTF-32 */
    D(0xb9b1, CU24,    RRF_c, ETF3, 0, 0, 0, 0, cuXX, 0, 24)
/* CONVERT UTF-32 to UTF-8 */
    D(0xb9b2, CU41,    RRF_c, ETF3, 0, 0, 0, 0, cuXX, 0, 41)
/* CONVERT UTF-32 to UTF-16 */
    D(0xb9b3, CU42,    RRF_c, ETF3, 0, 0, 0, 0, cuXX, 0, 42)

/* DIVIDE */
    C(0x1d00, DR,      RR_a,  Z,   r1_D32, r2_32s, new_P, r1_P32, divs32, 0)
    C(0x5d00, D,       RX_a,  Z,   r1_D32, m2_32s, new_P, r1_P32, divs32, 0)
    F(0xb30d, DEBR,    RRE,   Z,   e1, e2, new, e1, deb, 0, IF_BFP)
    F(0xb31d, DDBR,    RRE,   Z,   f1, f2, new, f1, ddb, 0, IF_BFP)
    F(0xb34d, DXBR,    RRE,   Z,   x1, x2, new_x, x1, dxb, 0, IF_BFP)
    F(0xed0d, DEB,     RXE,   Z,   e1, m2_32u, new, e1, deb, 0, IF_BFP)
    F(0xed1d, DDB,     RXE,   Z,   f1, m2_64, new, f1, ddb, 0, IF_BFP)
/* DIVIDE LOGICAL */
    C(0xb997, DLR,     RRE,   Z,   r1_D32, r2_32u, new_P, r1_P32, divu32, 0)
    C(0xe397, DL,      RXY_a, Z,   r1_D32, m2_32u, new_P, r1_P32, divu32, 0)
    C(0xb987, DLGR,    RRE,   Z,   0, r2_o, r1_P, 0, divu64, 0)
    C(0xe387, DLG,     RXY_a, Z,   0, m2_64, r1_P, 0, divu64, 0)
/* DIVIDE SINGLE */
    C(0xb90d, DSGR,    RRE,   Z,   r1p1, r2, r1_P, 0, divs64, 0)
    C(0xb91d, DSGFR,   RRE,   Z,   r1p1, r2_32s, r1_P, 0, divs64, 0)
    C(0xe30d, DSG,     RXY_a, Z,   r1p1, m2_64, r1_P, 0, divs64, 0)
    C(0xe31d, DSGF,    RXY_a, Z,   r1p1, m2_32s, r1_P, 0, divs64, 0)

/* EXCLUSIVE OR */
    C(0x1700, XR,      RR_a,  Z,   r1, r2, new, r1_32, xor, nz32)
    C(0xb9f7, XRK,     RRF_a, DO,  r2, r3, new, r1_32, xor, nz32)
    C(0x5700, X,       RX_a,  Z,   r1, m2_32s, new, r1_32, xor, nz32)
    C(0xe357, XY,      RXY_a, LD,  r1, m2_32s, new, r1_32, xor, nz32)
    C(0xb982, XGR,     RRE,   Z,   r1, r2, r1, 0, xor, nz64)
    C(0xb9e7, XGRK,    RRF_a, DO,  r2, r3, r1, 0, xor, nz64)
    C(0xe382, XG,      RXY_a, Z,   r1, m2_64, r1, 0, xor, nz64)
    C(0xd700, XC,      SS_a,  Z,   0, 0, 0, 0, xc, 0)
/* EXCLUSIVE OR IMMEDIATE */
    D(0xc006, XIHF,    RIL_a, EI,  r1_o, i2_32u, r1, 0, xori, 0, 0x2020)
    D(0xc007, XILF,    RIL_a, EI,  r1_o, i2_32u, r1, 0, xori, 0, 0x2000)
    D(0x9700, XI,      SI,    Z,   la1, i2_8u, new, 0, xi, nz64, MO_UB)
    D(0xeb57, XIY,     SIY,   LD,  la1, i2_8u, new, 0, xi, nz64, MO_UB)

/* EXECUTE */
    C(0x4400, EX,      RX_a,  Z,   0, a2, 0, 0, ex, 0)
/* EXECUTE RELATIVE LONG */
    C(0xc600, EXRL,    RIL_b, EE,  0, ri2, 0, 0, ex, 0)

/* EXTRACT ACCESS */
    C(0xb24f, EAR,     RRE,   Z,   0, 0, new, r1_32, ear, 0)
/* EXTRACT CPU ATTRIBUTE */
    C(0xeb4c, ECAG,    RSY_a, GIE, 0, a2, r1, 0, ecag, 0)
/* EXTRACT CPU TIME */
    F(0xc801, ECTG,    SSF,   ECT, 0, 0, 0, 0, ectg, 0, IF_IO)
/* EXTRACT FPC */
    F(0xb38c, EFPC,    RRE,   Z,   0, 0, new, r1_32, efpc, 0, IF_BFP)
/* EXTRACT PSW */
    C(0xb98d, EPSW,    RRE,   Z,   0, 0, 0, 0, epsw, 0)

/* FIND LEFTMOST ONE */
    C(0xb983, FLOGR,   RRE,   EI,  0, r2_o, r1_P, 0, flogr, 0)

/* INSERT CHARACTER */
    C(0x4300, IC,      RX_a,  Z,   0, m2_8u, 0, r1_8, mov2, 0)
    C(0xe373, ICY,     RXY_a, LD,  0, m2_8u, 0, r1_8, mov2, 0)
/* INSERT CHARACTERS UNDER MASK */
    D(0xbf00, ICM,     RS_b,  Z,   0, a2, r1, 0, icm, 0, 0)
    D(0xeb81, ICMY,    RSY_b, LD,  0, a2, r1, 0, icm, 0, 0)
    D(0xeb80, ICMH,    RSY_b, Z,   0, a2, r1, 0, icm, 0, 32)
/* INSERT IMMEDIATE */
    D(0xc008, IIHF,    RIL_a, EI,  r1_o, i2_32u, r1, 0, insi, 0, 0x2020)
    D(0xc009, IILF,    RIL_a, EI,  r1_o, i2_32u, r1, 0, insi, 0, 0x2000)
    D(0xa500, IIHH,    RI_a,  Z,   r1_o, i2_16u, r1, 0, insi, 0, 0x1030)
    D(0xa501, IIHL,    RI_a,  Z,   r1_o, i2_16u, r1, 0, insi, 0, 0x1020)
    D(0xa502, IILH,    RI_a,  Z,   r1_o, i2_16u, r1, 0, insi, 0, 0x1010)
    D(0xa503, IILL,    RI_a,  Z,   r1_o, i2_16u, r1, 0, insi, 0, 0x1000)
/* INSERT PROGRAM MASK */
    C(0xb222, IPM,     RRE,   Z,   0, 0, r1, 0, ipm, 0)

/* LOAD */
    C(0x1800, LR,      RR_a,  Z,   0, r2_o, 0, cond_r1r2_32, mov2, 0)
    D(0x5800, L,       RX_a,  Z,   0, a2, new, r1_32, ld32s, 0, 0)
    D(0xe358, LY,      RXY_a, LD,  0, a2, new, r1_32, ld32s, 0, 0)
    C(0xb904, LGR,     RRE,   Z,   0, r2_o, 0, r1, mov2, 0)
    C(0xb914, LGFR,    RRE,   Z,   0, r2_32s, 0, r1, mov2, 0)
    D(0xe304, LG,      RXY_a, Z,   0, a2, r1, 0, ld64, 0, 0)
    D(0xe314, LGF,     RXY_a, Z,   0, a2, r1, 0, ld32s, 0, 0)
    F(0x2800, LDR,     RR_a,  Z,   0, f2, 0, f1, mov2, 0, IF_AFP1 | IF_AFP2)
    F(0x6800, LD,      RX_a,  Z,   0, m2_64, 0, f1, mov2, 0, IF_AFP1)
    F(0xed65, LDY,     RXY_a, LD,  0, m2_64, 0, f1, mov2, 0, IF_AFP1)
    F(0x3800, LER,     RR_a,  Z,   0, e2, 0, cond_e1e2, mov2, 0, IF_AFP1 | IF_AFP2)
    F(0x7800, LE,      RX_a,  Z,   0, m2_32u, 0, e1, mov2, 0, IF_AFP1)
    F(0xed64, LEY,     RXY_a, LD,  0, m2_32u, 0, e1, mov2, 0, IF_AFP1)
    F(0xb365, LXR,     RRE,   Z,   x2h, x2l, 0, x1_P, movx, 0, IF_AFP1)
/* LOAD IMMEDIATE */
    C(0xc001, LGFI,    RIL_a, EI,  0, i2, 0, r1, mov2, 0)
/* LOAD RELATIVE LONG */
    D(0xc40d, LRL,     RIL_b, GIE, 0, ri2, new, r1_32, ld32s, 0, MO_ALIGN)
    D(0xc408, LGRL,    RIL_b, GIE, 0, ri2, r1, 0, ld64, 0, MO_ALIGN)
    D(0xc40c, LGFRL,   RIL_b, GIE, 0, ri2, r1, 0, ld32s, 0, MO_ALIGN)
/* LOAD ADDRESS */
    C(0x4100, LA,      RX_a,  Z,   0, a2, 0, r1, mov2, 0)
    C(0xe371, LAY,     RXY_a, LD,  0, a2, 0, r1, mov2, 0)
/* LOAD ADDRESS EXTENDED */
    C(0x5100, LAE,     RX_a,  Z,   0, a2, 0, r1, mov2e, 0)
    C(0xe375, LAEY,    RXY_a, GIE, 0, a2, 0, r1, mov2e, 0)
/* LOAD ADDRESS RELATIVE LONG */
    C(0xc000, LARL,    RIL_b, Z,   0, ri2, 0, r1, mov2, 0)
/* LOAD AND ADD */
    D(0xebf8, LAA,     RSY_a, ILA, r3_32s, a2, new, in2_r1_32, laa, adds32, MO_TESL)
    D(0xebe8, LAAG,    RSY_a, ILA, r3, a2, new, in2_r1, laa, adds64, MO_TEUQ)
/* LOAD AND ADD LOGICAL */
    D(0xebfa, LAAL,    RSY_a, ILA, r3_32u, a2, new, in2_r1_32, laa, addu32, MO_TEUL)
    D(0xebea, LAALG,   RSY_a, ILA, r3, a2, new, in2_r1, laa, addu64, MO_TEUQ)
/* LOAD AND AND */
    D(0xebf4, LAN,     RSY_a, ILA, r3_32s, a2, new, in2_r1_32, lan, nz32, MO_TESL)
    D(0xebe4, LANG,    RSY_a, ILA, r3, a2, new, in2_r1, lan, nz64, MO_TEUQ)
/* LOAD AND EXCLUSIVE OR */
    D(0xebf7, LAX,     RSY_a, ILA, r3_32s, a2, new, in2_r1_32, lax, nz32, MO_TESL)
    D(0xebe7, LAXG,    RSY_a, ILA, r3, a2, new, in2_r1, lax, nz64, MO_TEUQ)
/* LOAD AND OR */
    D(0xebf6, LAO,     RSY_a, ILA, r3_32s, a2, new, in2_r1_32, lao, nz32, MO_TESL)
    D(0xebe6, LAOG,    RSY_a, ILA, r3, a2, new, in2_r1, lao, nz64, MO_TEUQ)
/* LOAD AND TEST */
    C(0x1200, LTR,     RR_a,  Z,   0, r2_o, 0, cond_r1r2_32, mov2, s32)
    C(0xb902, LTGR,    RRE,   Z,   0, r2_o, 0, r1, mov2, s64)
    C(0xb912, LTGFR,   RRE,   Z,   0, r2_32s, 0, r1, mov2, s64)
    D(0xe312, LT,      RXY_a, EI,  0, a2, new, r1_32, ld32s, s64, 0)
    D(0xe302, LTG,     RXY_a, EI,  0, a2, r1, 0, ld64, s64, 0)
    D(0xe332, LTGF,    RXY_a, GIE, 0, a2, r1, 0, ld32s, s64, 0)
    F(0xb302, LTEBR,   RRE,   Z,   0, e2, 0, cond_e1e2, mov2, f32, IF_BFP)
    F(0xb312, LTDBR,   RRE,   Z,   0, f2, 0, f1, mov2, f64, IF_BFP)
    F(0xb342, LTXBR,   RRE,   Z,   x2h, x2l, 0, x1_P, movx, f128, IF_BFP)
/* LOAD AND TRAP */
    C(0xe39f, LAT,     RXY_a, LAT, 0, m2_32u, r1, 0, lat, 0)
    C(0xe385, LGAT,    RXY_a, LAT, 0, a2, r1, 0, lgat, 0)
/* LOAD AND ZERO RIGHTMOST BYTE */
    C(0xe33b, LZRF,    RXY_a, LZRB, 0, m2_32u, new, r1_32, lzrb, 0)
    C(0xe32a, LZRG,    RXY_a, LZRB, 0, m2_64, r1, 0, lzrb, 0)
/* LOAD LOGICAL AND ZERO RIGHTMOST BYTE */
    C(0xe33a, LLZRGF,  RXY_a, LZRB, 0, m2_32u, r1, 0, lzrb, 0)
/* LOAD BYTE */
    C(0xb926, LBR,     RRE,   EI,  0, r2_8s, 0, r1_32, mov2, 0)
    C(0xb906, LGBR,    RRE,   EI,  0, r2_8s, 0, r1, mov2, 0)
    C(0xe376, LB,      RXY_a, LD,  0, a2, new, r1_32, ld8s, 0)
    C(0xe377, LGB,     RXY_a, LD,  0, a2, r1, 0, ld8s, 0)
/* LOAD BYTE HIGH */
    C(0xe3c0, LBH,     RXY_a, HW,  0, a2, new, r1_32h, ld8s, 0)
/* LOAD COMPLEMENT */
    C(0x1300, LCR,     RR_a,  Z,   0, r2, new, r1_32, neg, neg32)
    C(0xb903, LCGR,    RRE,   Z,   0, r2, r1, 0, neg, neg64)
    C(0xb913, LCGFR,   RRE,   Z,   0, r2_32s, r1, 0, neg, neg64)
    F(0xb303, LCEBR,   RRE,   Z,   0, e2, new, e1, negf32, f32, IF_BFP)
    F(0xb313, LCDBR,   RRE,   Z,   0, f2, new, f1, negf64, f64, IF_BFP)
    F(0xb343, LCXBR,   RRE,   Z,   x2h, x2l, new_P, x1_P, negf128, f128, IF_BFP)
    F(0xb373, LCDFR,   RRE,   FPSSH, 0, f2, new, f1, negf64, 0, IF_AFP1 | IF_AFP2)
/* LOAD COUNT TO BLOCK BOUNDARY */
    C(0xe727, LCBB,    RXE,   V,   la2, 0, new, r1_32, lcbb, 0)
/* LOAD HALFWORD */
    C(0xb927, LHR,     RRE,   EI,  0, r2_16s, 0, r1_32, mov2, 0)
    C(0xb907, LGHR,    RRE,   EI,  0, r2_16s, 0, r1, mov2, 0)
    C(0x4800, LH,      RX_a,  Z,   0, a2, new, r1_32, ld16s, 0)
    C(0xe378, LHY,     RXY_a, LD,  0, a2, new, r1_32, ld16s, 0)
    C(0xe315, LGH,     RXY_a, Z,   0, a2, r1, 0, ld16s, 0)
/* LOAD HALFWORD HIGH */
    C(0xe3c4, LHH,     RXY_a, HW,  0, a2, new, r1_32h, ld16s, 0)
/* LOAD HALFWORD IMMEDIATE */
    C(0xa708, LHI,     RI_a,  Z,   0, i2, 0, r1_32, mov2, 0)
    C(0xa709, LGHI,    RI_a,  Z,   0, i2, 0, r1, mov2, 0)
/* LOAD HALFWORD RELATIVE LONG */
    C(0xc405, LHRL,    RIL_b, GIE, 0, ri2, new, r1_32, ld16s, 0)
    C(0xc404, LGHRL,   RIL_b, GIE, 0, ri2, r1, 0, ld16s, 0)
/* LOAD HIGH */
    D(0xe3ca, LFH,     RXY_a, HW,  0, a2, new, r1_32h, ld32u, 0, 0)
/* LOAG HIGH AND TRAP */
    C(0xe3c8, LFHAT,   RXY_a, LAT, 0, m2_32u, r1, 0, lfhat, 0)
/* LOAD LOGICAL */
    C(0xb916, LLGFR,   RRE,   Z,   0, r2_32u, 0, r1, mov2, 0)
    D(0xe316, LLGF,    RXY_a, Z,   0, a2, r1, 0, ld32u, 0, 0)
/* LOAD LOGICAL AND TRAP */
    C(0xe39d, LLGFAT,  RXY_a, LAT, 0, a2, r1, 0, llgfat, 0)
/* LOAD LOGICAL RELATIVE LONG */
    D(0xc40e, LLGFRL,  RIL_b, GIE, 0, ri2, r1, 0, ld32u, 0, MO_ALIGN)
/* LOAD LOGICAL CHARACTER */
    C(0xb994, LLCR,    RRE,   EI,  0, r2_8u, 0, r1_32, mov2, 0)
    C(0xb984, LLGCR,   RRE,   EI,  0, r2_8u, 0, r1, mov2, 0)
    C(0xe394, LLC,     RXY_a, EI,  0, a2, new, r1_32, ld8u, 0)
    C(0xe390, LLGC,    RXY_a, Z,   0, a2, r1, 0, ld8u, 0)
/* LOAD LOGICAL CHARACTER HIGH */
    C(0xe3c2, LLCH,    RXY_a, HW,  0, a2, new, r1_32h, ld8u, 0)
/* LOAD LOGICAL HALFWORD */
    C(0xb995, LLHR,    RRE,   EI,  0, r2_16u, 0, r1_32, mov2, 0)
    C(0xb985, LLGHR,   RRE,   EI,  0, r2_16u, 0, r1, mov2, 0)
    C(0xe395, LLH,     RXY_a, EI,  0, a2, new, r1_32, ld16u, 0)
    C(0xe391, LLGH,    RXY_a, Z,   0, a2, r1, 0, ld16u, 0)
/* LOAD LOGICAL HALFWORD HIGH */
    C(0xe3c6, LLHH,    RXY_a, HW,  0, a2, new, r1_32h, ld16u, 0)
/* LOAD LOGICAL HALFWORD RELATIVE LONG */
    C(0xc402, LLHRL,   RIL_b, GIE, 0, ri2, new, r1_32, ld16u, 0)
    C(0xc406, LLGHRL,  RIL_b, GIE, 0, ri2, r1, 0, ld16u, 0)
/* LOAD LOGICAL IMMEDATE */
    D(0xc00e, LLIHF,   RIL_a, EI, 0, i2_32u_shl, 0, r1, mov2, 0, 32)
    D(0xc00f, LLILF,   RIL_a, EI, 0, i2_32u_shl, 0, r1, mov2, 0, 0)
    D(0xa50c, LLIHH,   RI_a,  Z,  0, i2_16u_shl, 0, r1, mov2, 0, 48)
    D(0xa50d, LLIHL,   RI_a,  Z,  0, i2_16u_shl, 0, r1, mov2, 0, 32)
    D(0xa50e, LLILH,   RI_a,  Z,  0, i2_16u_shl, 0, r1, mov2, 0, 16)
    D(0xa50f, LLILL,   RI_a,  Z,  0, i2_16u_shl, 0, r1, mov2, 0, 0)
/* LOAD LOGICAL THIRTY ONE BITS */
    C(0xb917, LLGTR,   RRE,   Z,  0, r2_o, r1, 0, llgt, 0)
    C(0xe317, LLGT,    RXY_a, Z,  0, m2_32u, r1, 0, llgt, 0)
/* LOAD LOGICAL THIRTY ONE BITS AND TRAP */
    C(0xe39c, LLGTAT,  RXY_a, LAT, 0, m2_32u, r1, 0, llgtat, 0)

/* LOAD FPR FROM GR */
    F(0xb3c1, LDGR,    RRE,   FPRGR, 0, r2_o, 0, f1, mov2, 0, IF_AFP1)
/* LOAD GR FROM FPR */
    F(0xb3cd, LGDR,    RRE,   FPRGR, 0, f2, 0, r1, mov2, 0, IF_AFP2)
/* LOAD NEGATIVE */
    C(0x1100, LNR,     RR_a,  Z,   0, r2_32s, new, r1_32, nabs, nabs32)
    C(0xb901, LNGR,    RRE,   Z,   0, r2, r1, 0, nabs, nabs64)
    C(0xb911, LNGFR,   RRE,   Z,   0, r2_32s, r1, 0, nabs, nabs64)
    F(0xb301, LNEBR,   RRE,   Z,   0, e2, new, e1, nabsf32, f32, IF_BFP)
    F(0xb311, LNDBR,   RRE,   Z,   0, f2, new, f1, nabsf64, f64, IF_BFP)
    F(0xb341, LNXBR,   RRE,   Z,   x2h, x2l, new_P, x1_P, nabsf128, f128, IF_BFP)
    F(0xb371, LNDFR,   RRE,   FPSSH, 0, f2, new, f1, nabsf64, 0, IF_AFP1 | IF_AFP2)
/* LOAD ON CONDITION */
    C(0xb9f2, LOCR,    RRF_c, LOC, r1, r2, new, r1_32, loc, 0)
    C(0xb9e2, LOCGR,   RRF_c, LOC, r1, r2, r1, 0, loc, 0)
    C(0xebf2, LOC,     RSY_b, LOC, r1, m2_32u, new, r1_32, loc, 0)
    C(0xebe2, LOCG,    RSY_b, LOC, r1, m2_64, r1, 0, loc, 0)
/* LOAD HALFWORD IMMEDIATE ON CONDITION */
    C(0xec42, LOCHI,   RIE_g, LOC2, r1, i2, new, r1_32, loc, 0)
    C(0xec46, LOCGHI,  RIE_g, LOC2, r1, i2, r1, 0, loc, 0)
    C(0xec4e, LOCHHI,  RIE_g, LOC2, r1_sr32, i2, new, r1_32h, loc, 0)
/* LOAD HIGH ON CONDITION */
    C(0xb9e0, LOCFHR,  RRF_c, LOC2, r1_sr32, r2_sr32, new, r1_32h, loc, 0)
    C(0xebe0, LOCFH,   RSY_b, LOC2, r1_sr32, m2_32u, new, r1_32h, loc, 0)
/* LOAD PAIR DISJOINT */
    D(0xc804, LPD,     SSF,   ILA, 0, 0, new_P, r3_P32, lpd, 0, MO_TEUL)
    D(0xc805, LPDG,    SSF,   ILA, 0, 0, new_P, r3_P64, lpd, 0, MO_TEUQ)
/* LOAD PAIR FROM QUADWORD */
    C(0xe38f, LPQ,     RXY_a, Z,   0, a2, 0, r1_D64, lpq, 0)
/* LOAD POSITIVE */
    C(0x1000, LPR,     RR_a,  Z,   0, r2_32s, new, r1_32, abs, abs32)
    C(0xb900, LPGR,    RRE,   Z,   0, r2, r1, 0, abs, abs64)
    C(0xb910, LPGFR,   RRE,   Z,   0, r2_32s, r1, 0, abs, abs64)
    F(0xb300, LPEBR,   RRE,   Z,   0, e2, new, e1, absf32, f32, IF_BFP)
    F(0xb310, LPDBR,   RRE,   Z,   0, f2, new, f1, absf64, f64, IF_BFP)
    F(0xb340, LPXBR,   RRE,   Z,   x2h, x2l, new_P, x1_P, absf128, f128, IF_BFP)
    F(0xb370, LPDFR,   RRE,   FPSSH, 0, f2, new, f1, absf64, 0, IF_AFP1 | IF_AFP2)
/* LOAD REVERSED */
    C(0xb91f, LRVR,    RRE,   Z,   0, r2_32u, new, r1_32, rev32, 0)
    C(0xb90f, LRVGR,   RRE,   Z,   0, r2_o, r1, 0, rev64, 0)
    C(0xe31f, LRVH,    RXY_a, Z,   0, m2_16u, new, r1_16, rev16, 0)
    C(0xe31e, LRV,     RXY_a, Z,   0, m2_32u, new, r1_32, rev32, 0)
    C(0xe30f, LRVG,    RXY_a, Z,   0, m2_64, r1, 0, rev64, 0)
/* LOAD ZERO */
    F(0xb374, LZER,    RRE,   Z,   0, 0, 0, e1, zero, 0, IF_AFP1)
    F(0xb375, LZDR,    RRE,   Z,   0, 0, 0, f1, zero, 0, IF_AFP1)
    F(0xb376, LZXR,    RRE,   Z,   0, 0, 0, x1_P, zero2, 0, IF_AFP1)

/* LOAD FPC */
    F(0xb29d, LFPC,    S,     Z,   0, m2_32u, 0, 0, sfpc, 0, IF_BFP)
/* LOAD FPC AND SIGNAL */
    F(0xb2bd, LFAS,    S,     IEEEE_SIM, 0, m2_32u, 0, 0, sfas, 0, IF_DFP)
/* LOAD FP INTEGER */
    F(0xb357, FIEBR,   RRF_e, Z,   0, e2, new, e1, fieb, 0, IF_BFP)
    F(0xb35f, FIDBR,   RRF_e, Z,   0, f2, new, f1, fidb, 0, IF_BFP)
    F(0xb347, FIXBR,   RRF_e, Z,   0, x2, new_x, x1, fixb, 0, IF_BFP)

/* LOAD LENGTHENED */
    F(0xb304, LDEBR,   RRE,   Z,   0, e2, new, f1, ldeb, 0, IF_BFP)
    F(0xb305, LXDBR,   RRE,   Z,   0, f2, new_x, x1, lxdb, 0, IF_BFP)
    F(0xb306, LXEBR,   RRE,   Z,   0, e2, new_x, x1, lxeb, 0, IF_BFP)
    F(0xed04, LDEB,    RXE,   Z,   0, m2_32u, new, f1, ldeb, 0, IF_BFP)
    F(0xed05, LXDB,    RXE,   Z,   0, m2_64, new_x, x1, lxdb, 0, IF_BFP)
    F(0xed06, LXEB,    RXE,   Z,   0, m2_32u, new_x, x1, lxeb, 0, IF_BFP)
    F(0xb324, LDER,    RRE,   Z,   0, e2, new, f1, lde, 0, IF_AFP1)
    F(0xed24, LDE,     RXE,   Z,   0, m2_32u, new, f1, lde, 0, IF_AFP1)
/* LOAD ROUNDED */
    F(0xb344, LEDBR,   RRF_e, Z,   0, f2, new, e1, ledb, 0, IF_BFP)
    F(0xb345, LDXBR,   RRF_e, Z,   0, x2, new, f1, ldxb, 0, IF_BFP)
    F(0xb346, LEXBR,   RRF_e, Z,   0, x2, new, e1, lexb, 0, IF_BFP)

/* LOAD MULTIPLE */
    C(0x9800, LM,      RS_a,  Z,   0, a2, 0, 0, lm32, 0)
    C(0xeb98, LMY,     RSY_a, LD,  0, a2, 0, 0, lm32, 0)
    C(0xeb04, LMG,     RSY_a, Z,   0, a2, 0, 0, lm64, 0)
/* LOAD MULTIPLE HIGH */
    C(0xeb96, LMH,     RSY_a, Z,   0, a2, 0, 0, lmh, 0)
/* LOAD ACCESS MULTIPLE */
    C(0x9a00, LAM,     RS_a,  Z,   0, a2, 0, 0, lam, 0)
    C(0xeb9a, LAMY,    RSY_a, LD,  0, a2, 0, 0, lam, 0)

/* MONITOR CALL */
    C(0xaf00, MC,      SI,    Z,   la1, 0, 0, 0, mc, 0)

/* MOVE */
    C(0xd200, MVC,     SS_a,  Z,   la1, a2, 0, 0, mvc, 0)
    C(0xe544, MVHHI,   SIL,   GIE, la1, i2, 0, m1_16, mov2, 0)
    C(0xe54c, MVHI,    SIL,   GIE, la1, i2, 0, m1_32, mov2, 0)
    C(0xe548, MVGHI,   SIL,   GIE, la1, i2, 0, m1_64, mov2, 0)
    C(0x9200, MVI,     SI,    Z,   la1, i2, 0, m1_8, mov2, 0)
    C(0xeb52, MVIY,    SIY,   LD,  la1, i2, 0, m1_8, mov2, 0)
/* MOVE INVERSE */
    C(0xe800, MVCIN,   SS_a,  Z,   la1, a2, 0, 0, mvcin, 0)
/* MOVE LONG */
    C(0x0e00, MVCL,    RR_a,  Z,   0, 0, 0, 0, mvcl, 0)
/* MOVE LONG EXTENDED */
    C(0xa800, MVCLE,   RS_a,  Z,   0, a2, 0, 0, mvcle, 0)
/* MOVE LONG UNICODE */
    C(0xeb8e, MVCLU,   RSY_a, E2,  0, a2, 0, 0, mvclu, 0)
/* MOVE NUMERICS */
    C(0xd100, MVN,     SS_a,  Z,   la1, a2, 0, 0, mvn, 0)
/* MOVE RIGHT TO LEFT */
    C(0xe50a, MVCRL,   SSE,  MIE3, la1, a2, 0, 0, mvcrl, 0)
/* MOVE PAGE */
    C(0xb254, MVPG,    RRE,   Z,   0, 0, 0, 0, mvpg, 0)
/* MOVE STRING */
    C(0xb255, MVST,    RRE,   Z,   0, 0, 0, 0, mvst, 0)
/* MOVE WITH OPTIONAL SPECIFICATION */
    C(0xc800, MVCOS,   SSF,   MVCOS, la1, a2, 0, 0, mvcos, 0)
/* MOVE WITH OFFSET */
    /* Really format SS_b, but we pack both lengths into one argument
       for the helper call, so we might as well leave one 8-bit field.  */
    C(0xf100, MVO,     SS_a,  Z,   la1, a2, 0, 0, mvo, 0)
/* MOVE ZONES */
    C(0xd300, MVZ,     SS_a,  Z,   la1, a2, 0, 0, mvz, 0)

/* MULTIPLY */
    C(0x1c00, MR,      RR_a,  Z,   r1p1_32s, r2_32s, new, r1_D32, mul, 0)
    C(0xb9ec, MGRK,    RRF_a, MIE2,r3_o, r2_o, r1_P, 0, muls128, 0)
    C(0x5c00, M,       RX_a,  Z,   r1p1_32s, m2_32s, new, r1_D32, mul, 0)
    C(0xe35c, MFY,     RXY_a, GIE, r1p1_32s, m2_32s, new, r1_D32, mul, 0)
    C(0xe384, MG,      RXY_a, MIE2,r1p1_o, m2_64, r1_P, 0, muls128, 0)
    F(0xb317, MEEBR,   RRE,   Z,   e1, e2, new, e1, meeb, 0, IF_BFP)
    F(0xb31c, MDBR,    RRE,   Z,   f1, f2, new, f1, mdb, 0, IF_BFP)
    F(0xb34c, MXBR,    RRE,   Z,   x1, x2, new_x, x1, mxb, 0, IF_BFP)
    F(0xb30c, MDEBR,   RRE,   Z,   f1, e2, new, f1, mdeb, 0, IF_BFP)
    F(0xb307, MXDBR,   RRE,   Z,   f1, f2, new_x, x1, mxdb, 0, IF_BFP)
    F(0xed17, MEEB,    RXE,   Z,   e1, m2_32u, new, e1, meeb, 0, IF_BFP)
    F(0xed1c, MDB,     RXE,   Z,   f1, m2_64, new, f1, mdb, 0, IF_BFP)
    F(0xed0c, MDEB,    RXE,   Z,   f1, m2_32u, new, f1, mdeb, 0, IF_BFP)
    F(0xed07, MXDB,    RXE,   Z,   f1, m2_64, new_x, x1, mxdb, 0, IF_BFP)
/* MULTIPLY HALFWORD */
    C(0x4c00, MH,      RX_a,  Z,   r1_o, m2_16s, new, r1_32, mul, 0)
    C(0xe37c, MHY,     RXY_a, GIE, r1_o, m2_16s, new, r1_32, mul, 0)
    C(0xe33c, MGH,     RXY_a, MIE2,r1_o, m2_16s, r1, 0, mul, 0)
/* MULTIPLY HALFWORD IMMEDIATE */
    C(0xa70c, MHI,     RI_a,  Z,   r1_o, i2, new, r1_32, mul, 0)
    C(0xa70d, MGHI,    RI_a,  Z,   r1_o, i2, r1, 0, mul, 0)
/* MULTIPLY LOGICAL */
    C(0xb996, MLR,     RRE,   Z,   r1p1_32u, r2_32u, new, r1_D32, mul, 0)
    C(0xe396, ML,      RXY_a, Z,   r1p1_32u, m2_32u, new, r1_D32, mul, 0)
    C(0xb986, MLGR,    RRE,   Z,   r1p1, r2_o, r1_P, 0, mul128, 0)
    C(0xe386, MLG,     RXY_a, Z,   r1p1, m2_64, r1_P, 0, mul128, 0)
/* MULTIPLY SINGLE */
    C(0xb252, MSR,     RRE,   Z,   r1_o, r2_o, new, r1_32, mul, 0)
    C(0xb9fd, MSRKC,   RRF_a, MIE2,r3_32s, r2_32s, new, r1_32, mul, muls32)
    C(0x7100, MS,      RX_a,  Z,   r1_o, m2_32s, new, r1_32, mul, 0)
    C(0xe351, MSY,     RXY_a, LD,  r1_o, m2_32s, new, r1_32, mul, 0)
    C(0xe353, MSC,     RXY_a, MIE2,r1_32s, m2_32s, new, r1_32, mul, muls32)
    C(0xb90c, MSGR,    RRE,   Z,   r1_o, r2_o, r1, 0, mul, 0)
    C(0xb9ed, MSGRKC,  RRF_a, MIE2,r3_o, r2_o, new_P, out2_r1, muls128, muls64)
    C(0xb91c, MSGFR,   RRE,   Z,   r1_o, r2_32s, r1, 0, mul, 0)
    C(0xe30c, MSG,     RXY_a, Z,   r1_o, m2_64, r1, 0, mul, 0)
    C(0xe383, MSGC,    RXY_a, MIE2,r1_o, m2_64, new_P, out2_r1, muls128, muls64)
    C(0xe31c, MSGF,    RXY_a, Z,   r1_o, m2_32s, r1, 0, mul, 0)
/* MULTIPLY SINGLE IMMEDIATE */
    C(0xc201, MSFI,    RIL_a, GIE, r1_o, i2, new, r1_32, mul, 0)
    C(0xc200, MSGFI,   RIL_a, GIE, r1_o, i2, r1, 0, mul, 0)

/* MULTIPLY AND ADD */
    F(0xb30e, MAEBR,   RRD,   Z,   e1, e2, new, e1, maeb, 0, IF_BFP)
    F(0xb31e, MADBR,   RRD,   Z,   f1, f2, new, f1, madb, 0, IF_BFP)
    F(0xed0e, MAEB,    RXF,   Z,   e1, m2_32u, new, e1, maeb, 0, IF_BFP)
    F(0xed1e, MADB,    RXF,   Z,   f1, m2_64, new, f1, madb, 0, IF_BFP)
/* MULTIPLY AND SUBTRACT */
    F(0xb30f, MSEBR,   RRD,   Z,   e1, e2, new, e1, mseb, 0, IF_BFP)
    F(0xb31f, MSDBR,   RRD,   Z,   f1, f2, new, f1, msdb, 0, IF_BFP)
    F(0xed0f, MSEB,    RXF,   Z,   e1, m2_32u, new, e1, mseb, 0, IF_BFP)
    F(0xed1f, MSDB,    RXF,   Z,   f1, m2_64, new, f1, msdb, 0, IF_BFP)

/* NAND */
    C(0xb974, NNRK,    RRF_a, MIE3, r2, r3, new, r1_32, nand, nz32)
    C(0xb964, NNGRK,   RRF_a, MIE3, r2, r3, r1, 0, nand, nz64)
/* NOR */
    C(0xb976, NORK,    RRF_a, MIE3, r2, r3, new, r1_32, nor, nz32)
    C(0xb966, NOGRK,   RRF_a, MIE3, r2, r3, r1, 0, nor, nz64)
/* NOT EXCLUSIVE OR */
    C(0xb977, NXRK,    RRF_a, MIE3, r2, r3, new, r1_32, nxor, nz32)
    C(0xb967, NXGRK,   RRF_a, MIE3, r2, r3, r1, 0, nxor, nz64)

/* OR */
    C(0x1600, OR,      RR_a,  Z,   r1, r2, new, r1_32, or, nz32)
    C(0xb9f6, ORK,     RRF_a, DO,  r2, r3, new, r1_32, or, nz32)
    C(0x5600, O,       RX_a,  Z,   r1, m2_32s, new, r1_32, or, nz32)
    C(0xe356, OY,      RXY_a, LD,  r1, m2_32s, new, r1_32, or, nz32)
    C(0xb981, OGR,     RRE,   Z,   r1, r2, r1, 0, or, nz64)
    C(0xb9e6, OGRK,    RRF_a, DO,  r2, r3, r1, 0, or, nz64)
    C(0xe381, OG,      RXY_a, Z,   r1, m2_64, r1, 0, or, nz64)
    C(0xd600, OC,      SS_a,  Z,   la1, a2, 0, 0, oc, 0)
/* OR IMMEDIATE */
    D(0xc00c, OIHF,    RIL_a, EI,  r1_o, i2_32u, r1, 0, ori, 0, 0x2020)
    D(0xc00d, OILF,    RIL_a, EI,  r1_o, i2_32u, r1, 0, ori, 0, 0x2000)
    D(0xa508, OIHH,    RI_a,  Z,   r1_o, i2_16u, r1, 0, ori, 0, 0x1030)
    D(0xa509, OIHL,    RI_a,  Z,   r1_o, i2_16u, r1, 0, ori, 0, 0x1020)
    D(0xa50a, OILH,    RI_a,  Z,   r1_o, i2_16u, r1, 0, ori, 0, 0x1010)
    D(0xa50b, OILL,    RI_a,  Z,   r1_o, i2_16u, r1, 0, ori, 0, 0x1000)
    D(0x9600, OI,      SI,    Z,   la1, i2_8u, new, 0, oi, nz64, MO_UB)
    D(0xeb56, OIY,     SIY,   LD,  la1, i2_8u, new, 0, oi, nz64, MO_UB)
/* OR WITH COMPLEMENT */
    C(0xb975, OCRK,    RRF_a, MIE3, r2, r3, new, r1_32, orc, nz32)
    C(0xb965, OCGRK,   RRF_a, MIE3, r2, r3, r1, 0, orc, nz64)

/* PACK */
    /* Really format SS_b, but we pack both lengths into one argument
       for the helper call, so we might as well leave one 8-bit field.  */
    C(0xf200, PACK,    SS_a,  Z,   la1, a2, 0, 0, pack, 0)
/* PACK ASCII */
    C(0xe900, PKA,     SS_f,  E2,  la1, a2, 0, 0, pka, 0)
/* PACK UNICODE */
    C(0xe100, PKU,     SS_f,  E2,  la1, a2, 0, 0, pku, 0)

/* POPULATION COUNT */
    C(0xb9e1, POPCNT,  RRF_c, PC,  0, r2_o, r1, 0, popcnt, nz64)

/* PREFETCH */
    /* Implemented as nops of course.  */
    C(0xe336, PFD,     RXY_b, GIE, 0, 0, 0, 0, 0, 0)
    C(0xc602, PFDRL,   RIL_c, GIE, 0, 0, 0, 0, 0, 0)
/* PERFORM PROCESSOR ASSIST */
    /* Implemented as nop of course.  */
    C(0xb2e8, PPA,     RRF_c, PPA, 0, 0, 0, 0, 0, 0)

/* ROTATE LEFT SINGLE LOGICAL */
    C(0xeb1d, RLL,     RSY_a, Z,   r3_o, sh, new, r1_32, rll32, 0)
    C(0xeb1c, RLLG,    RSY_a, Z,   r3_o, sh, r1, 0, rll64, 0)

/* ROTATE THEN INSERT SELECTED BITS */
    C(0xec55, RISBG,   RIE_f, GIE, 0, r2, r1, 0, risbg, s64)
    C(0xec59, RISBGN,  RIE_f, MIE, 0, r2, r1, 0, risbg, 0)
    C(0xec5d, RISBHG,  RIE_f, HW,  0, r2, r1, 0, risbg, 0)
    C(0xec51, RISBLG,  RIE_f, HW,  0, r2, r1, 0, risbg, 0)
/* ROTATE_THEN <OP> SELECTED BITS */
    C(0xec54, RNSBG,   RIE_f, GIE, 0, r2, r1, 0, rosbg, 0)
    C(0xec56, ROSBG,   RIE_f, GIE, 0, r2, r1, 0, rosbg, 0)
    C(0xec57, RXSBG,   RIE_f, GIE, 0, r2, r1, 0, rosbg, 0)

/* SEARCH STRING */
    C(0xb25e, SRST,    RRE,   Z,   0, 0, 0, 0, srst, 0)
/* SEARCH STRING UNICODE */
    C(0xb9be, SRSTU,   RRE,   ETF3, 0, 0, 0, 0, srstu, 0)

/* SELECT */
    C(0xb9f0, SELR,    RRF_a, MIE3, r3, r2, new, r1_32, loc, 0)
    C(0xb9e3, SELGR,   RRF_a, MIE3, r3, r2, r1, 0, loc, 0)
/* SELECT HIGH */
    C(0xb9c0, SELFHR,  RRF_a, MIE3, r3_sr32, r2_sr32, new, r1_32h, loc, 0)

/* SET ACCESS */
    C(0xb24e, SAR,     RRE,   Z,   0, r2_o, 0, 0, sar, 0)
/* SET ADDRESSING MODE */
    D(0x010c, SAM24,   E,     Z,   0, 0, 0, 0, sam, 0, 0)
    D(0x010d, SAM31,   E,     Z,   0, 0, 0, 0, sam, 0, 1)
    D(0x010e, SAM64,   E,     Z,   0, 0, 0, 0, sam, 0, 3)
/* SET FPC */
    F(0xb384, SFPC,    RRE,   Z,   0, r1_o, 0, 0, sfpc, 0, IF_BFP)
/* SET FPC AND SIGNAL */
    F(0xb385, SFASR,   RRE,   IEEEE_SIM, 0, r1_o, 0, 0, sfas, 0, IF_DFP)
/* SET BFP ROUNDING MODE */
    F(0xb299, SRNM,    S,     Z,   la2, 0, 0, 0, srnm, 0, IF_BFP)
    F(0xb2b8, SRNMB,   S,     FPE, la2, 0, 0, 0, srnmb, 0, IF_BFP)
/* SET DFP ROUNDING MODE */
    F(0xb2b9, SRNMT,   S,     DFPR, la2, 0, 0, 0, srnmt, 0, IF_DFP)
/* SET PROGRAM MASK */
    C(0x0400, SPM,     RR_a,  Z,   r1, 0, 0, 0, spm, 0)

/* SHIFT LEFT SINGLE */
    D(0x8b00, SLA,     RS_a,  Z,   r1, sh, new, r1_32, sla, 0, 31)
    D(0xebdd, SLAK,    RSY_a, DO,  r3, sh, new, r1_32, sla, 0, 31)
    D(0xeb0b, SLAG,    RSY_a, Z,   r3, sh, r1, 0, sla, 0, 63)
/* SHIFT LEFT SINGLE LOGICAL */
    C(0x8900, SLL,     RS_a,  Z,   r1_o, sh, new, r1_32, sll, 0)
    C(0xebdf, SLLK,    RSY_a, DO,  r3_o, sh, new, r1_32, sll, 0)
    C(0xeb0d, SLLG,    RSY_a, Z,   r3_o, sh, r1, 0, sll, 0)
/* SHIFT RIGHT SINGLE */
    C(0x8a00, SRA,     RS_a,  Z,   r1_32s, sh, new, r1_32, sra, s32)
    C(0xebdc, SRAK,    RSY_a, DO,  r3_32s, sh, new, r1_32, sra, s32)
    C(0xeb0a, SRAG,    RSY_a, Z,   r3_o, sh, r1, 0, sra, s64)
/* SHIFT RIGHT SINGLE LOGICAL */
    C(0x8800, SRL,     RS_a,  Z,   r1_32u, sh, new, r1_32, srl, 0)
    C(0xebde, SRLK,    RSY_a, DO,  r3_32u, sh, new, r1_32, srl, 0)
    C(0xeb0c, SRLG,    RSY_a, Z,   r3_o, sh, r1, 0, srl, 0)
/* SHIFT LEFT DOUBLE */
    D(0x8f00, SLDA,    RS_a,  Z,   r1_D32, sh, new, r1_D32, sla, 0, 63)
/* SHIFT LEFT DOUBLE LOGICAL */
    C(0x8d00, SLDL,    RS_a,  Z,   r1_D32, sh, new, r1_D32, sll, 0)
/* SHIFT RIGHT DOUBLE */
    C(0x8e00, SRDA,    RS_a,  Z,   r1_D32, sh, new, r1_D32, sra, s64)
/* SHIFT RIGHT DOUBLE LOGICAL */
    C(0x8c00, SRDL,    RS_a,  Z,   r1_D32, sh, new, r1_D32, srl, 0)

/* SQUARE ROOT */
    F(0xb314, SQEBR,   RRE,   Z,   0, e2, new, e1, sqeb, 0, IF_BFP)
    F(0xb315, SQDBR,   RRE,   Z,   0, f2, new, f1, sqdb, 0, IF_BFP)
    F(0xb316, SQXBR,   RRE,   Z,   0, x2, new_x, x1, sqxb, 0, IF_BFP)
    F(0xed14, SQEB,    RXE,   Z,   0, m2_32u, new, e1, sqeb, 0, IF_BFP)
    F(0xed15, SQDB,    RXE,   Z,   0, m2_64, new, f1, sqdb, 0, IF_BFP)

/* STORE */
    D(0x5000, ST,      RX_a,  Z,   r1_o, a2, 0, 0, st32, 0, 0)
    D(0xe350, STY,     RXY_a, LD,  r1_o, a2, 0, 0, st32, 0, 0)
    D(0xe324, STG,     RXY_a, Z,   r1_o, a2, 0, 0, st64, 0, 0)
    E(0x6000, STD,     RX_a,  Z,   f1, a2, 0, 0, st64, 0, 0, IF_AFP1)
    E(0xed67, STDY,    RXY_a, LD,  f1, a2, 0, 0, st64, 0, 0, IF_AFP1)
    E(0x7000, STE,     RX_a,  Z,   e1, a2, 0, 0, st32, 0, 0, IF_AFP1)
    E(0xed66, STEY,    RXY_a, LD,  e1, a2, 0, 0, st32, 0, 0, IF_AFP1)
/* STORE RELATIVE LONG */
    D(0xc40f, STRL,    RIL_b, GIE, r1_o, ri2, 0, 0, st32, 0, MO_ALIGN)
    D(0xc40b, STGRL,   RIL_b, GIE, r1_o, ri2, 0, 0, st64, 0, MO_ALIGN)
/* STORE CHARACTER */
    C(0x4200, STC,     RX_a,  Z,   r1_o, a2, 0, 0, st8, 0)
    C(0xe372, STCY,    RXY_a, LD,  r1_o, a2, 0, 0, st8, 0)
/* STORE CHARACTER HIGH */
    C(0xe3c3, STCH,    RXY_a, HW,  r1_sr32, a2, 0, 0, st8, 0)
/* STORE CHARACTERS UNDER MASK */
    D(0xbe00, STCM,    RS_b,  Z,   r1_o, a2, 0, 0, stcm, 0, 0)
    D(0xeb2d, STCMY,   RSY_b, LD,  r1_o, a2, 0, 0, stcm, 0, 0)
    D(0xeb2c, STCMH,   RSY_b, Z,   r1_o, a2, 0, 0, stcm, 0, 32)
/* STORE HALFWORD */
    C(0x4000, STH,     RX_a,  Z,   r1_o, a2, 0, 0, st16, 0)
    C(0xe370, STHY,    RXY_a, LD,  r1_o, a2, 0, 0, st16, 0)
/* STORE HALFWORD HIGH */
    C(0xe3c7, STHH,    RXY_a, HW,  r1_sr32, a2, 0, 0, st16, 0)
/* STORE HALFWORD RELATIVE LONG */
    C(0xc407, STHRL,   RIL_b, GIE, r1_o, ri2, 0, 0, st16, 0)
/* STORE HIGH */
    D(0xe3cb, STFH,    RXY_a, HW,  r1_sr32, a2, 0, 0, st32, 0, 0)
/* STORE ON CONDITION */
    D(0xebf3, STOC,    RSY_b, LOC, 0, 0, 0, 0, soc, 0, 0)
    D(0xebe3, STOCG,   RSY_b, LOC, 0, 0, 0, 0, soc, 0, 1)
/* STORE HIGH ON CONDITION */
    D(0xebe1, STOCFH,  RSY_b, LOC2, 0, 0, 0, 0, soc, 0, 2)
/* STORE REVERSED */
    C(0xe33f, STRVH,   RXY_a, Z,   la2, r1_16u, new, m1_16, rev16, 0)
    C(0xe33e, STRV,    RXY_a, Z,   la2, r1_32u, new, m1_32, rev32, 0)
    C(0xe32f, STRVG,   RXY_a, Z,   la2, r1_o, new, m1_64, rev64, 0)

/* STORE CLOCK */
    F(0xb205, STCK,    S,     Z,   la2, 0, new, m1_64, stck, 0, IF_IO)
    F(0xb27c, STCKF,   S,     SCF, la2, 0, new, m1_64, stck, 0, IF_IO)
/* STORE CLOCK EXTENDED */
    F(0xb278, STCKE,   S,     Z,   0, a2, 0, 0, stcke, 0, IF_IO)

/* STORE FACILITY LIST EXTENDED */
    C(0xb2b0, STFLE,   S,  SFLE,   0, a2, 0, 0, stfle, 0)
/* STORE FPC */
    F(0xb29c, STFPC,   S,     Z,   0, a2, new, m2_32, efpc, 0, IF_BFP)

/* STORE MULTIPLE */
    D(0x9000, STM,     RS_a,  Z,   0, a2, 0, 0, stm, 0, 4)
    D(0xeb90, STMY,    RSY_a, LD,  0, a2, 0, 0, stm, 0, 4)
    D(0xeb24, STMG,    RSY_a, Z,   0, a2, 0, 0, stm, 0, 8)
/* STORE MULTIPLE HIGH */
    C(0xeb26, STMH,    RSY_a, Z,   0, a2, 0, 0, stmh, 0)
/* STORE ACCESS MULTIPLE */
    C(0x9b00, STAM,    RS_a,  Z,   0, a2, 0, 0, stam, 0)
    C(0xeb9b, STAMY,   RSY_a, LD,  0, a2, 0, 0, stam, 0)
/* STORE PAIR TO QUADWORD */
    C(0xe38e, STPQ,    RXY_a, Z,   0, a2, r1_P, 0, stpq, 0)

/* SUBTRACT */
    C(0x1b00, SR,      RR_a,  Z,   r1, r2, new, r1_32, sub, subs32)
    C(0xb9f9, SRK,     RRF_a, DO,  r2, r3, new, r1_32, sub, subs32)
    C(0x5b00, S,       RX_a,  Z,   r1, m2_32s, new, r1_32, sub, subs32)
    C(0xe35b, SY,      RXY_a, LD,  r1, m2_32s, new, r1_32, sub, subs32)
    C(0xb909, SGR,     RRE,   Z,   r1, r2, r1, 0, sub, subs64)
    C(0xb919, SGFR,    RRE,   Z,   r1, r2_32s, r1, 0, sub, subs64)
    C(0xb9e9, SGRK,    RRF_a, DO,  r2, r3, r1, 0, sub, subs64)
    C(0xe309, SG,      RXY_a, Z,   r1, m2_64, r1, 0, sub, subs64)
    C(0xe319, SGF,     RXY_a, Z,   r1, m2_32s, r1, 0, sub, subs64)
    F(0xb30b, SEBR,    RRE,   Z,   e1, e2, new, e1, seb, f32, IF_BFP)
    F(0xb31b, SDBR,    RRE,   Z,   f1, f2, new, f1, sdb, f64, IF_BFP)
    F(0xb34b, SXBR,    RRE,   Z,   x1, x2, new_x, x1, sxb, f128, IF_BFP)
    F(0xed0b, SEB,     RXE,   Z,   e1, m2_32u, new, e1, seb, f32, IF_BFP)
    F(0xed1b, SDB,     RXE,   Z,   f1, m2_64, new, f1, sdb, f64, IF_BFP)
/* SUBTRACT HALFWORD */
    C(0x4b00, SH,      RX_a,  Z,   r1, m2_16s, new, r1_32, sub, subs32)
    C(0xe37b, SHY,     RXY_a, LD,  r1, m2_16s, new, r1_32, sub, subs32)
    C(0xe339, SGH,     RXY_a, MIE2,r1, m2_16s, r1, 0, sub, subs64)
/* SUBTRACT HIGH */
    C(0xb9c9, SHHHR,   RRF_a, HW,  r2_sr32, r3_sr32, new, r1_32h, sub, subs32)
    C(0xb9d9, SHHLR,   RRF_a, HW,  r2_sr32, r3, new, r1_32h, sub, subs32)
/* SUBTRACT LOGICAL */
    C(0x1f00, SLR,     RR_a,  Z,   r1_32u, r2_32u, new, r1_32, sub, subu32)
    C(0xb9fb, SLRK,    RRF_a, DO,  r2_32u, r3_32u, new, r1_32, sub, subu32)
    C(0x5f00, SL,      RX_a,  Z,   r1_32u, m2_32u, new, r1_32, sub, subu32)
    C(0xe35f, SLY,     RXY_a, LD,  r1_32u, m2_32u, new, r1_32, sub, subu32)
    C(0xb90b, SLGR,    RRE,   Z,   r1, r2, r1, 0, subu64, subu64)
    C(0xb91b, SLGFR,   RRE,   Z,   r1, r2_32u, r1, 0, subu64, subu64)
    C(0xb9eb, SLGRK,   RRF_a, DO,  r2, r3, r1, 0, subu64, subu64)
    C(0xe30b, SLG,     RXY_a, Z,   r1, m2_64, r1, 0, subu64, subu64)
    C(0xe31b, SLGF,    RXY_a, Z,   r1, m2_32u, r1, 0, subu64, subu64)
/* SUBTRACT LOCICAL HIGH */
    C(0xb9cb, SLHHHR,  RRF_a, HW,  r2_sr32, r3_sr32, new, r1_32h, sub, subu32)
    C(0xb9db, SLHHLR,  RRF_a, HW,  r2_sr32, r3_32u, new, r1_32h, sub, subu32)
/* SUBTRACT LOGICAL IMMEDIATE */
    C(0xc205, SLFI,    RIL_a, EI,  r1_32u, i2_32u, new, r1_32, sub, subu32)
    C(0xc204, SLGFI,   RIL_a, EI,  r1, i2_32u, r1, 0, subu64, subu64)
/* SUBTRACT LOGICAL WITH BORROW */
    C(0xb999, SLBR,    RRE,   Z,   r1_32u, r2_32u, new, r1_32, subb32, subu32)
    C(0xb989, SLBGR,   RRE,   Z,   r1, r2, r1, 0, subb64, subu64)
    C(0xe399, SLB,     RXY_a, Z,   r1_32u, m2_32u, new, r1_32, subb32, subu32)
    C(0xe389, SLBG,    RXY_a, Z,   r1, m2_64, r1, 0, subb64, subu64)

/* SUPERVISOR CALL */
    C(0x0a00, SVC,     I,     Z,   0, 0, 0, 0, svc, 0)

/* TEST ADDRESSING MODE */
    C(0x010b, TAM,     E,     Z,   0, 0, 0, 0, tam, 0)

/* TEST AND SET */
    C(0x9300, TS,      S,     Z,   0, a2, 0, 0, ts, 0)

/* TEST DATA CLASS */
    F(0xed10, TCEB,    RXE,   Z,   e1, a2, 0, 0, tceb, 0, IF_BFP)
    F(0xed11, TCDB,    RXE,   Z,   f1, a2, 0, 0, tcdb, 0, IF_BFP)
    F(0xed12, TCXB,    RXE,   Z,   x1, a2, 0, 0, tcxb, 0, IF_BFP)

/* TEST DECIMAL */
    C(0xebc0, TP,      RSL,   E2,  la1, 0, 0, 0, tp, 0)

/* TEST UNDER MASK */
    C(0x9100, TM,      SI,    Z,   m1_8u, i2_8u, 0, 0, 0, tm32)
    C(0xeb51, TMY,     SIY,   LD,  m1_8u, i2_8u, 0, 0, 0, tm32)
    D(0xa702, TMHH,    RI_a,  Z,   r1_o, i2_16u_shl, 0, 0, 0, tm64, 48)
    D(0xa703, TMHL,    RI_a,  Z,   r1_o, i2_16u_shl, 0, 0, 0, tm64, 32)
    D(0xa700, TMLH,    RI_a,  Z,   r1_o, i2_16u_shl, 0, 0, 0, tm64, 16)
    D(0xa701, TMLL,    RI_a,  Z,   r1_o, i2_16u_shl, 0, 0, 0, tm64, 0)

/* TRANSLATE */
    C(0xdc00, TR,      SS_a,  Z,   la1, a2, 0, 0, tr, 0)
/* TRANSLATE AND TEST */
    C(0xdd00, TRT,     SS_a,  Z,   la1, a2, 0, 0, trt, 0)
/* TRANSLATE AND TEST REVERSE */
    C(0xd000, TRTR,    SS_a,  ETF3, la1, a2, 0, 0, trtr, 0)
/* TRANSLATE EXTENDED */
    C(0xb2a5, TRE,     RRE,   Z,   0, r2, r1_P, 0, tre, 0)

/* TRANSLATE ONE TO ONE */
    C(0xb993, TROO,    RRF_c, E2,  0, 0, 0, 0, trXX, 0)
/* TRANSLATE ONE TO TWO */
    C(0xb992, TROT,    RRF_c, E2,  0, 0, 0, 0, trXX, 0)
/* TRANSLATE TWO TO ONE */
    C(0xb991, TRTO,    RRF_c, E2,  0, 0, 0, 0, trXX, 0)
/* TRANSLATE TWO TO TWO */
    C(0xb990, TRTT,    RRF_c, E2,  0, 0, 0, 0, trXX, 0)

/* UNPACK */
    /* Really format SS_b, but we pack both lengths into one argument
       for the helper call, so we might as well leave one 8-bit field.  */
    C(0xf300, UNPK,    SS_a,  Z,   la1, a2, 0, 0, unpk, 0)
/* UNPACK ASCII */
    C(0xea00, UNPKA,   SS_a,  E2,  la1, a2, 0, 0, unpka, 0)
/* UNPACK UNICODE */
    C(0xe200, UNPKU,   SS_a,  E2,  la1, a2, 0, 0, unpku, 0)

/* MSA Instructions */
    D(0xb91e, KMAC,    RRE,   MSA,  0, 0, 0, 0, msa, 0, S390_FEAT_TYPE_KMAC)
    D(0xb928, PCKMO,   RRE,   MSA3, 0, 0, 0, 0, msa, 0, S390_FEAT_TYPE_PCKMO)
    D(0xb92a, KMF,     RRE,   MSA4, 0, 0, 0, 0, msa, 0, S390_FEAT_TYPE_KMF)
    D(0xb92b, KMO,     RRE,   MSA4, 0, 0, 0, 0, msa, 0, S390_FEAT_TYPE_KMO)
    D(0xb92c, PCC,     RRE,   MSA4, 0, 0, 0, 0, msa, 0, S390_FEAT_TYPE_PCC)
    D(0xb92d, KMCTR,   RRF_b, MSA4, 0, 0, 0, 0, msa, 0, S390_FEAT_TYPE_KMCTR)
    D(0xb92e, KM,      RRE,   MSA,  0, 0, 0, 0, msa, 0, S390_FEAT_TYPE_KM)
    D(0xb92f, KMC,     RRE,   MSA,  0, 0, 0, 0, msa, 0, S390_FEAT_TYPE_KMC)
    D(0xb929, KMA,     RRF_b, MSA8, 0, 0, 0, 0, msa, 0, S390_FEAT_TYPE_KMA)
    D(0xb93c, PPNO,    RRE,   MSA5, 0, 0, 0, 0, msa, 0, S390_FEAT_TYPE_PPNO)
    D(0xb93e, KIMD,    RRE,   MSA,  0, 0, 0, 0, msa, 0, S390_FEAT_TYPE_KIMD)
    D(0xb93f, KLMD,    RRE,   MSA,  0, 0, 0, 0, msa, 0, S390_FEAT_TYPE_KLMD)

/* === Vector Support Instructions === */

/* VECTOR BIT PERMUTE */
    E(0xe785, VBPERM,  VRR_c, VE,  0, 0, 0, 0, vbperm, 0, 0, IF_VEC)
/* VECTOR GATHER ELEMENT */
    E(0xe713, VGEF,    VRV,   V,   la2, 0, 0, 0, vge, 0, ES_32, IF_VEC)
    E(0xe712, VGEG,    VRV,   V,   la2, 0, 0, 0, vge, 0, ES_64, IF_VEC)
/* VECTOR GENERATE BYTE MASK */
    F(0xe744, VGBM,    VRI_a, V,   0, 0, 0, 0, vgbm, 0, IF_VEC)
/* VECTOR GENERATE MASK */
    F(0xe746, VGM,     VRI_b, V,   0, 0, 0, 0, vgm, 0, IF_VEC)
/* VECTOR LOAD */
    F(0xe706, VL,      VRX,   V,   la2, 0, 0, 0, vl, 0, IF_VEC)
    F(0xe756, VLR,     VRR_a, V,   0, 0, 0, 0, vlr, 0, IF_VEC)
/* VECTOR LOAD AND REPLICATE */
    F(0xe705, VLREP,   VRX,   V,   la2, 0, 0, 0, vlrep, 0, IF_VEC)
/* VECTOR LOAD BYTE REVERSED ELEMENT */
    E(0xe601, VLEBRH,  VRX,   VE2, la2, 0, 0, 0, vlebr, 0, ES_16, IF_VEC)
    E(0xe603, VLEBRF,  VRX,   VE2, la2, 0, 0, 0, vlebr, 0, ES_32, IF_VEC)
    E(0xe602, VLEBRG,  VRX,   VE2, la2, 0, 0, 0, vlebr, 0, ES_64, IF_VEC)
/* VECTOR LOAD BYTE REVERSED ELEMENT AND REPLICATE */
    F(0xe605, VLBRREP, VRX,   VE2, la2, 0, 0, 0, vlbrrep, 0, IF_VEC)
/* VECTOR LOAD BYTE REVERSED ELEMENT AND ZERO */
    F(0xe604, VLLEBRZ, VRX,   VE2, la2, 0, 0, 0, vllebrz, 0, IF_VEC)
/* VECTOR LOAD BYTE REVERSED ELEMENTS */
    F(0xe606, VLBR,    VRX,   VE2, la2, 0, 0, 0, vlbr, 0, IF_VEC)
/* VECTOR LOAD ELEMENT */
    E(0xe700, VLEB,    VRX,   V,   la2, 0, 0, 0, vle, 0, ES_8, IF_VEC)
    E(0xe701, VLEH,    VRX,   V,   la2, 0, 0, 0, vle, 0, ES_16, IF_VEC)
    E(0xe703, VLEF,    VRX,   V,   la2, 0, 0, 0, vle, 0, ES_32, IF_VEC)
    E(0xe702, VLEG,    VRX,   V,   la2, 0, 0, 0, vle, 0, ES_64, IF_VEC)
/* VECTOR LOAD ELEMENT IMMEDIATE */
    E(0xe740, VLEIB,   VRI_a, V,   0, 0, 0, 0, vlei, 0, ES_8, IF_VEC)
    E(0xe741, VLEIH,   VRI_a, V,   0, 0, 0, 0, vlei, 0, ES_16, IF_VEC)
    E(0xe743, VLEIF,   VRI_a, V,   0, 0, 0, 0, vlei, 0, ES_32, IF_VEC)
    E(0xe742, VLEIG,   VRI_a, V,   0, 0, 0, 0, vlei, 0, ES_64, IF_VEC)
/* VECTOR LOAD ELEMENTS REVERSED */
    F(0xe607, VLER,    VRX,   VE2, la2, 0, 0, 0, vler, 0, IF_VEC)
/* VECTOR LOAD GR FROM VR ELEMENT */
    F(0xe721, VLGV,    VRS_c, V,   la2, 0, r1, 0, vlgv, 0, IF_VEC)
/* VECTOR LOAD LOGICAL ELEMENT AND ZERO */
    F(0xe704, VLLEZ,   VRX,   V,   la2, 0, 0, 0, vllez, 0, IF_VEC)
/* VECTOR LOAD MULTIPLE */
    F(0xe736, VLM,     VRS_a, V,   la2, 0, 0, 0, vlm, 0, IF_VEC)
/* VECTOR LOAD TO BLOCK BOUNDARY */
    F(0xe707, VLBB,    VRX,   V,   la2, 0, 0, 0, vlbb, 0, IF_VEC)
/* VECTOR LOAD VR ELEMENT FROM GR */
    F(0xe722, VLVG,    VRS_b, V,   la2, r3, 0, 0, vlvg, 0, IF_VEC)
/* VECTOR LOAD VR FROM GRS DISJOINT */
    F(0xe762, VLVGP,   VRR_f, V,   r2, r3, 0, 0, vlvgp, 0, IF_VEC)
/* VECTOR LOAD WITH LENGTH */
    F(0xe737, VLL,     VRS_b, V,   la2, r3_32u, 0, 0, vll, 0, IF_VEC)
/* VECTOR MERGE HIGH */
    F(0xe761, VMRH,    VRR_c, V,   0, 0, 0, 0, vmr, 0, IF_VEC)
/* VECTOR MERGE LOW */
    F(0xe760, VMRL,    VRR_c, V,   0, 0, 0, 0, vmr, 0, IF_VEC)
/* VECTOR PACK */
    F(0xe794, VPK,     VRR_c, V,   0, 0, 0, 0, vpk, 0, IF_VEC)
/* VECTOR PACK SATURATE */
    F(0xe797, VPKS,    VRR_b, V,   0, 0, 0, 0, vpk, 0, IF_VEC)
/* VECTOR PACK LOGICAL SATURATE */
    F(0xe795, VPKLS,   VRR_b, V,   0, 0, 0, 0, vpk, 0, IF_VEC)
    F(0xe78c, VPERM,   VRR_e, V,   0, 0, 0, 0, vperm, 0, IF_VEC)
/* VECTOR PERMUTE DOUBLEWORD IMMEDIATE */
    F(0xe784, VPDI,    VRR_c, V,   0, 0, 0, 0, vpdi, 0, IF_VEC)
/* VECTOR REPLICATE */
    F(0xe74d, VREP,    VRI_c, V,   0, 0, 0, 0, vrep, 0, IF_VEC)
/* VECTOR REPLICATE IMMEDIATE */
    F(0xe745, VREPI,   VRI_a, V,   0, 0, 0, 0, vrepi, 0, IF_VEC)
/* VECTOR SCATTER ELEMENT */
    E(0xe71b, VSCEF,   VRV,   V,   la2, 0, 0, 0, vsce, 0, ES_32, IF_VEC)
    E(0xe71a, VSCEG,   VRV,   V,   la2, 0, 0, 0, vsce, 0, ES_64, IF_VEC)
/* VECTOR SELECT */
    F(0xe78d, VSEL,    VRR_e, V,   0, 0, 0, 0, vsel, 0, IF_VEC)
/* VECTOR SIGN EXTEND TO DOUBLEWORD */
    F(0xe75f, VSEG,    VRR_a, V,   0, 0, 0, 0, vseg, 0, IF_VEC)
/* VECTOR STORE */
    F(0xe70e, VST,     VRX,   V,   la2, 0, 0, 0, vst, 0, IF_VEC)
/* VECTOR STORE BYTE REVERSED ELEMENT */
    E(0xe609, VSTEBRH,  VRX,   VE2, la2, 0, 0, 0, vstebr, 0, ES_16, IF_VEC)
    E(0xe60b, VSTEBRF,  VRX,   VE2, la2, 0, 0, 0, vstebr, 0, ES_32, IF_VEC)
    E(0xe60a, VSTEBRG,  VRX,   VE2, la2, 0, 0, 0, vstebr, 0, ES_64, IF_VEC)
/* VECTOR STORE BYTE REVERSED ELEMENTS */
    F(0xe60e, VSTBR,    VRX,   VE2, la2, 0, 0, 0, vstbr, 0, IF_VEC)
/* VECTOR STORE ELEMENT */
    E(0xe708, VSTEB,   VRX,   V,   la2, 0, 0, 0, vste, 0, ES_8, IF_VEC)
    E(0xe709, VSTEH,   VRX,   V,   la2, 0, 0, 0, vste, 0, ES_16, IF_VEC)
    E(0xe70b, VSTEF,   VRX,   V,   la2, 0, 0, 0, vste, 0, ES_32, IF_VEC)
    E(0xe70a, VSTEG,   VRX,   V,   la2, 0, 0, 0, vste, 0, ES_64, IF_VEC)
/* VECTOR STORE ELEMENTS REVERSED */
    F(0xe60f, VSTER,   VRX,   VE2, la2, 0, 0, 0, vster, 0, IF_VEC)
/* VECTOR STORE MULTIPLE */
    F(0xe73e, VSTM,    VRS_a, V,   la2, 0, 0, 0, vstm, 0, IF_VEC)
/* VECTOR STORE WITH LENGTH */
    F(0xe73f, VSTL,    VRS_b, V,   la2, r3_32u, 0, 0, vstl, 0, IF_VEC)
/* VECTOR UNPACK HIGH */
    F(0xe7d7, VUPH,    VRR_a, V,   0, 0, 0, 0, vup, 0, IF_VEC)
/* VECTOR UNPACK LOGICAL HIGH */
    F(0xe7d5, VUPLH,   VRR_a, V,   0, 0, 0, 0, vup, 0, IF_VEC)
/* VECTOR UNPACK LOW */
    F(0xe7d6, VUPL,    VRR_a, V,   0, 0, 0, 0, vup, 0, IF_VEC)
/* VECTOR UNPACK LOGICAL LOW */
    F(0xe7d4, VUPLL,   VRR_a, V,   0, 0, 0, 0, vup, 0, IF_VEC)

/* === Vector Integer Instructions === */

/* VECTOR ADD */
    F(0xe7f3, VA,      VRR_c, V,   0, 0, 0, 0, va, 0, IF_VEC)
/* VECTOR ADD COMPUTE CARRY */
    F(0xe7f1, VACC,    VRR_c, V,   0, 0, 0, 0, vacc, 0, IF_VEC)
/* VECTOR ADD WITH CARRY */
    F(0xe7bb, VAC,     VRR_d, V,   0, 0, 0, 0, vac, 0, IF_VEC)
/* VECTOR ADD WITH CARRY COMPUTE CARRY */
    F(0xe7b9, VACCC,   VRR_d, V,   0, 0, 0, 0, vaccc, 0, IF_VEC)
/* VECTOR AND */
    F(0xe768, VN,      VRR_c, V,   0, 0, 0, 0, vn, 0, IF_VEC)
/* VECTOR AND WITH COMPLEMENT */
    F(0xe769, VNC,     VRR_c, V,   0, 0, 0, 0, vnc, 0, IF_VEC)
/* VECTOR AVERAGE */
    F(0xe7f2, VAVG,    VRR_c, V,   0, 0, 0, 0, vavg, 0, IF_VEC)
/* VECTOR AVERAGE LOGICAL */
    F(0xe7f0, VAVGL,   VRR_c, V,   0, 0, 0, 0, vavgl, 0, IF_VEC)
/* VECTOR CHECKSUM */
    F(0xe766, VCKSM,   VRR_c, V,   0, 0, 0, 0, vcksm, 0, IF_VEC)
/* VECTOR ELEMENT COMPARE */
    F(0xe7db, VEC,     VRR_a, V,   0, 0, 0, 0, vec, cmps64, IF_VEC)
/* VECTOR ELEMENT COMPARE LOGICAL */
    F(0xe7d9, VECL,    VRR_a, V,   0, 0, 0, 0, vec, cmpu64, IF_VEC)
/* VECTOR COMPARE EQUAL */
    E(0xe7f8, VCEQ,    VRR_b, V,   0, 0, 0, 0, vc, 0, TCG_COND_EQ, IF_VEC)
/* VECTOR COMPARE HIGH */
    E(0xe7fb, VCH,     VRR_b, V,   0, 0, 0, 0, vc, 0, TCG_COND_GT, IF_VEC)
/* VECTOR COMPARE HIGH LOGICAL */
    E(0xe7f9, VCHL,    VRR_b, V,   0, 0, 0, 0, vc, 0, TCG_COND_GTU, IF_VEC)
/* VECTOR COUNT LEADING ZEROS */
    F(0xe753, VCLZ,    VRR_a, V,   0, 0, 0, 0, vclz, 0, IF_VEC)
/* VECTOR COUNT TRAILING ZEROS */
    F(0xe752, VCTZ,    VRR_a, V,   0, 0, 0, 0, vctz, 0, IF_VEC)
/* VECTOR EXCLUSIVE OR */
    F(0xe76d, VX,      VRR_c, V,   0, 0, 0, 0, vx, 0, IF_VEC)
/* VECTOR GALOIS FIELD MULTIPLY SUM */
    F(0xe7b4, VGFM,    VRR_c, V,   0, 0, 0, 0, vgfm, 0, IF_VEC)
/* VECTOR GALOIS FIELD MULTIPLY SUM AND ACCUMULATE */
    F(0xe7bc, VGFMA,   VRR_d, V,   0, 0, 0, 0, vgfma, 0, IF_VEC)
/* VECTOR LOAD COMPLEMENT */
    F(0xe7de, VLC,     VRR_a, V,   0, 0, 0, 0, vlc, 0, IF_VEC)
/* VECTOR LOAD POSITIVE */
    F(0xe7df, VLP,     VRR_a, V,   0, 0, 0, 0, vlp, 0, IF_VEC)
/* VECTOR MAXIMUM */
    F(0xe7ff, VMX,     VRR_c, V,   0, 0, 0, 0, vmx, 0, IF_VEC)
/* VECTOR MAXIMUM LOGICAL */
    F(0xe7fd, VMXL,    VRR_c, V,   0, 0, 0, 0, vmx, 0, IF_VEC)
/* VECTOR MINIMUM */
    F(0xe7fe, VMN,     VRR_c, V,   0, 0, 0, 0, vmx, 0, IF_VEC)
/* VECTOR MINIMUM LOGICAL */
    F(0xe7fc, VMNL,    VRR_c, V,   0, 0, 0, 0, vmx, 0, IF_VEC)
/* VECTOR MULTIPLY AND ADD LOW */
    F(0xe7aa, VMAL,    VRR_d, V,   0, 0, 0, 0, vma, 0, IF_VEC)
/* VECTOR MULTIPLY AND ADD HIGH */
    F(0xe7ab, VMAH,    VRR_d, V,   0, 0, 0, 0, vma, 0, IF_VEC)
/* VECTOR MULTIPLY AND ADD LOGICAL HIGH */
    F(0xe7a9, VMALH,   VRR_d, V,   0, 0, 0, 0, vma, 0, IF_VEC)
/* VECTOR MULTIPLY AND ADD EVEN */
    F(0xe7ae, VMAE,    VRR_d, V,   0, 0, 0, 0, vma, 0, IF_VEC)
/* VECTOR MULTIPLY AND ADD LOGICAL EVEN */
    F(0xe7ac, VMALE,   VRR_d, V,   0, 0, 0, 0, vma, 0, IF_VEC)
/* VECTOR MULTIPLY AND ADD ODD */
    F(0xe7af, VMAO,    VRR_d, V,   0, 0, 0, 0, vma, 0, IF_VEC)
/* VECTOR MULTIPLY AND ADD LOGICAL ODD */
    F(0xe7ad, VMALO,   VRR_d, V,   0, 0, 0, 0, vma, 0, IF_VEC)
/* VECTOR MULTIPLY HIGH */
    F(0xe7a3, VMH,     VRR_c, V,   0, 0, 0, 0, vm, 0, IF_VEC)
/* VECTOR MULTIPLY LOGICAL HIGH */
    F(0xe7a1, VMLH,    VRR_c, V,   0, 0, 0, 0, vm, 0, IF_VEC)
/* VECTOR MULTIPLY LOW */
    F(0xe7a2, VML,     VRR_c, V,   0, 0, 0, 0, vm, 0, IF_VEC)
/* VECTOR MULTIPLY EVEN */
    F(0xe7a6, VME,     VRR_c, V,   0, 0, 0, 0, vm, 0, IF_VEC)
/* VECTOR MULTIPLY LOGICAL EVEN */
    F(0xe7a4, VMLE,    VRR_c, V,   0, 0, 0, 0, vm, 0, IF_VEC)
/* VECTOR MULTIPLY ODD */
    F(0xe7a7, VMO,     VRR_c, V,   0, 0, 0, 0, vm, 0, IF_VEC)
/* VECTOR MULTIPLY LOGICAL ODD */
    F(0xe7a5, VMLO,    VRR_c, V,   0, 0, 0, 0, vm, 0, IF_VEC)
/* VECTOR MULTIPLY SUM LOGICAL */
    F(0xe7b8, VMSL,    VRR_d, VE,  0, 0, 0, 0, vmsl, 0, IF_VEC)
/* VECTOR NAND */
    F(0xe76e, VNN,     VRR_c, VE,  0, 0, 0, 0, vnn, 0, IF_VEC)
/* VECTOR NOR */
    F(0xe76b, VNO,     VRR_c, V,   0, 0, 0, 0, vno, 0, IF_VEC)
/* VECTOR NOT EXCLUSIVE OR */
    F(0xe76c, VNX,     VRR_c, VE,  0, 0, 0, 0, vnx, 0, IF_VEC)
/* VECTOR OR */
    F(0xe76a, VO,      VRR_c, V,   0, 0, 0, 0, vo, 0, IF_VEC)
/* VECTOR OR WITH COMPLEMENT */
    F(0xe76f, VOC,     VRR_c, VE,  0, 0, 0, 0, voc, 0, IF_VEC)
/* VECTOR POPULATION COUNT */
    F(0xe750, VPOPCT,  VRR_a, V,   0, 0, 0, 0, vpopct, 0, IF_VEC)
/* VECTOR ELEMENT ROTATE LEFT LOGICAL */
    F(0xe773, VERLLV,  VRR_c, V,   0, 0, 0, 0, vesv, 0, IF_VEC)
    F(0xe733, VERLL,   VRS_a, V,   la2, 0, 0, 0, ves, 0, IF_VEC)
/* VECTOR ELEMENT ROTATE AND INSERT UNDER MASK */
    F(0xe772, VERIM,   VRI_d, V,   0, 0, 0, 0, verim, 0, IF_VEC)
/* VECTOR ELEMENT SHIFT LEFT */
    F(0xe770, VESLV,   VRR_c, V,   0, 0, 0, 0, vesv, 0, IF_VEC)
    F(0xe730, VESL,    VRS_a, V,   la2, 0, 0, 0, ves, 0, IF_VEC)
/* VECTOR ELEMENT SHIFT RIGHT ARITHMETIC */
    F(0xe77a, VESRAV,  VRR_c, V,   0, 0, 0, 0, vesv, 0, IF_VEC)
    F(0xe73a, VESRA,   VRS_a, V,   la2, 0, 0, 0, ves, 0, IF_VEC)
/* VECTOR ELEMENT SHIFT RIGHT LOGICAL */
    F(0xe778, VESRLV,  VRR_c, V,   0, 0, 0, 0, vesv, 0, IF_VEC)
    F(0xe738, VESRL,   VRS_a, V,   la2, 0, 0, 0, ves, 0, IF_VEC)
/* VECTOR SHIFT LEFT */
    E(0xe774, VSL,     VRR_c, V,   0, 0, 0, 0, vsl, 0, 0, IF_VEC)
/* VECTOR SHIFT LEFT BY BYTE */
    E(0xe775, VSLB,    VRR_c, V,   0, 0, 0, 0, vsl, 0, 1, IF_VEC)
/* VECTOR SHIFT LEFT DOUBLE BY BIT */
    E(0xe786, VSLD,    VRI_d, VE2, 0, 0, 0, 0, vsld, 0, 0, IF_VEC)
/* VECTOR SHIFT LEFT DOUBLE BY BYTE */
    E(0xe777, VSLDB,   VRI_d, V,   0, 0, 0, 0, vsld, 0, 1, IF_VEC)
/* VECTOR SHIFT RIGHT ARITHMETIC */
    E(0xe77e, VSRA,    VRR_c, V,   0, 0, 0, 0, vsra, 0, 0, IF_VEC)
/* VECTOR SHIFT RIGHT ARITHMETIC BY BYTE */
    E(0xe77f, VSRAB,   VRR_c, V,   0, 0, 0, 0, vsra, 0, 1, IF_VEC)
/* VECTOR SHIFT RIGHT DOUBLE BY BIT */
    F(0xe787, VSRD,    VRI_d, VE2, 0, 0, 0, 0, vsrd, 0, IF_VEC)
/* VECTOR SHIFT RIGHT LOGICAL */
    E(0xe77c, VSRL,    VRR_c, V,   0, 0, 0, 0, vsrl, 0, 0, IF_VEC)
/* VECTOR SHIFT RIGHT LOGICAL BY BYTE */
    E(0xe77d, VSRLB,   VRR_c, V,   0, 0, 0, 0, vsrl, 0, 1, IF_VEC)
/* VECTOR SUBTRACT */
    F(0xe7f7, VS,      VRR_c, V,   0, 0, 0, 0, vs, 0, IF_VEC)
/* VECTOR SUBTRACT COMPUTE BORROW INDICATION */
    F(0xe7f5, VSCBI,   VRR_c, V,   0, 0, 0, 0, vscbi, 0, IF_VEC)
/* VECTOR SUBTRACT WITH BORROW INDICATION */
    F(0xe7bf, VSBI,    VRR_d, V,   0, 0, 0, 0, vsbi, 0, IF_VEC)
/* VECTOR SUBTRACT WITH BORROW COMPUTE BORROW INDICATION */
    F(0xe7bd, VSBCBI,  VRR_d, V,   0, 0, 0, 0, vsbcbi, 0, IF_VEC)
/* VECTOR SUM ACROSS DOUBLEWORD */
    F(0xe765, VSUMG,   VRR_c, V,   0, 0, 0, 0, vsumg, 0, IF_VEC)
/* VECTOR SUM ACROSS QUADWORD */
    F(0xe767, VSUMQ,   VRR_c, V,   0, 0, 0, 0, vsumq, 0, IF_VEC)
/* VECTOR SUM ACROSS WORD */
    F(0xe764, VSUM,    VRR_c, V,   0, 0, 0, 0, vsum, 0, IF_VEC)
/* VECTOR TEST UNDER MASK */
    F(0xe7d8, VTM,     VRR_a, V,   0, 0, 0, 0, vtm, 0, IF_VEC)

/* === Vector String Instructions === */

/* VECTOR FIND ANY ELEMENT EQUAL */
    F(0xe782, VFAE,    VRR_b, V,   0, 0, 0, 0, vfae, 0, IF_VEC)
/* VECTOR FIND ELEMENT EQUAL */
    F(0xe780, VFEE,    VRR_b, V,   0, 0, 0, 0, vfee, 0, IF_VEC)
/* VECTOR FIND ELEMENT NOT EQUAL */
    F(0xe781, VFENE,   VRR_b, V,   0, 0, 0, 0, vfene, 0, IF_VEC)
/* VECTOR ISOLATE STRING */
    F(0xe75c, VISTR,   VRR_a, V,   0, 0, 0, 0, vistr, 0, IF_VEC)
/* VECTOR STRING RANGE COMPARE */
    F(0xe78a, VSTRC,   VRR_d, V,   0, 0, 0, 0, vstrc, 0, IF_VEC)
/* VECTOR STRING SEARCH */
    F(0xe78b, VSTRS,   VRR_d, VE2, 0, 0, 0, 0, vstrs, 0, IF_VEC)

/* === Vector Floating-Point Instructions */

/* VECTOR FP ADD */
    F(0xe7e3, VFA,     VRR_c, V,   0, 0, 0, 0, vfa, 0, IF_VEC)
/* VECTOR FP COMPARE SCALAR */
    F(0xe7cb, WFC,     VRR_a, V,   0, 0, 0, 0, wfc, 0, IF_VEC)
/* VECTOR FP COMPARE AND SIGNAL SCALAR */
    F(0xe7ca, WFK,     VRR_a, V,   0, 0, 0, 0, wfc, 0, IF_VEC)
/* VECTOR FP COMPARE EQUAL */
    F(0xe7e8, VFCE,    VRR_c, V,   0, 0, 0, 0, vfc, 0, IF_VEC)
/* VECTOR FP COMPARE HIGH */
    F(0xe7eb, VFCH,    VRR_c, V,   0, 0, 0, 0, vfc, 0, IF_VEC)
/* VECTOR FP COMPARE HIGH OR EQUAL */
    F(0xe7ea, VFCHE,   VRR_c, V,   0, 0, 0, 0, vfc, 0, IF_VEC)
/* VECTOR FP CONVERT FROM FIXED 64-BIT */
    F(0xe7c3, VCDG,    VRR_a, V,   0, 0, 0, 0, vcdg, 0, IF_VEC)
/* VECTOR FP CONVERT FROM LOGICAL 64-BIT */
    F(0xe7c1, VCDLG,   VRR_a, V,   0, 0, 0, 0, vcdg, 0, IF_VEC)
/* VECTOR FP CONVERT TO FIXED 64-BIT */
    F(0xe7c2, VCGD,    VRR_a, V,   0, 0, 0, 0, vcdg, 0, IF_VEC)
/* VECTOR FP CONVERT TO LOGICAL 64-BIT */
    F(0xe7c0, VCLGD,   VRR_a, V,   0, 0, 0, 0, vcdg, 0, IF_VEC)
/* VECTOR FP DIVIDE */
    F(0xe7e5, VFD,     VRR_c, V,   0, 0, 0, 0, vfa, 0, IF_VEC)
/* VECTOR LOAD FP INTEGER */
    F(0xe7c7, VFI,     VRR_a, V,   0, 0, 0, 0, vcdg, 0, IF_VEC)
/* VECTOR FP LOAD LENGTHENED */
    F(0xe7c4, VFLL,    VRR_a, V,   0, 0, 0, 0, vfll, 0, IF_VEC)
/* VECTOR FP LOAD ROUNDED */
    F(0xe7c5, VFLR,    VRR_a, V,   0, 0, 0, 0, vcdg, 0, IF_VEC)
/* VECTOR FP MAXIMUM */
    F(0xe7ef, VFMAX,   VRR_c, VE,  0, 0, 0, 0, vfmax, 0, IF_VEC)
/* VECTOR FP MINIMUM */
    F(0xe7ee, VFMIN,   VRR_c, VE,  0, 0, 0, 0, vfmax, 0, IF_VEC)
/* VECTOR FP MULTIPLY */
    F(0xe7e7, VFM,     VRR_c, V,   0, 0, 0, 0, vfa, 0, IF_VEC)
/* VECTOR FP MULTIPLY AND ADD */
    F(0xe78f, VFMA,    VRR_e, V,   0, 0, 0, 0, vfma, 0, IF_VEC)
/* VECTOR FP MULTIPLY AND SUBTRACT */
    F(0xe78e, VFMS,    VRR_e, V,   0, 0, 0, 0, vfma, 0, IF_VEC)
/* VECTOR FP NEGATIVE MULTIPLY AND ADD */
    F(0xe79f, VFNMA,   VRR_e, VE,  0, 0, 0, 0, vfma, 0, IF_VEC)
/* VECTOR FP NEGATIVE MULTIPLY AND SUBTRACT */
    F(0xe79e, VFNMS,   VRR_e, VE,   0, 0, 0, 0, vfma, 0, IF_VEC)
/* VECTOR FP PERFORM SIGN OPERATION */
    F(0xe7cc, VFPSO,   VRR_a, V,   0, 0, 0, 0, vfpso, 0, IF_VEC)
/* VECTOR FP SQUARE ROOT */
    F(0xe7ce, VFSQ,    VRR_a, V,   0, 0, 0, 0, vfsq, 0, IF_VEC)
/* VECTOR FP SUBTRACT */
    F(0xe7e2, VFS,     VRR_c, V,   0, 0, 0, 0, vfa, 0, IF_VEC)
/* VECTOR FP TEST DATA CLASS IMMEDIATE */
    F(0xe74a, VFTCI,   VRI_e, V,   0, 0, 0, 0, vftci, 0, IF_VEC)

#ifndef CONFIG_USER_ONLY
/* COMPARE AND SWAP AND PURGE */
    E(0xb250, CSP,     RRE,   Z,   r1_32u, ra2, r1_P, 0, csp, 0, MO_TEUL, IF_PRIV)
    E(0xb98a, CSPG,    RRE, DAT_ENH, r1_o, ra2, r1_P, 0, csp, 0, MO_TEUQ, IF_PRIV)
/* DIAGNOSE (KVM hypercall) */
    F(0x8300, DIAG,    RSI,   Z,   0, 0, 0, 0, diag, 0, IF_PRIV | IF_IO)
/* INSERT STORAGE KEY EXTENDED */
    F(0xb229, ISKE,    RRE,   Z,   0, r2_o, new, r1_8, iske, 0, IF_PRIV)
/* INVALIDATE DAT TABLE ENTRY */
    F(0xb98e, IPDE,    RRF_b, Z,   r1_o, r2_o, 0, 0, idte, 0, IF_PRIV)
/* INVALIDATE PAGE TABLE ENTRY */
    F(0xb221, IPTE,    RRF_a, Z,   r1_o, r2_o, 0, 0, ipte, 0, IF_PRIV)
/* LOAD CONTROL */
    F(0xb700, LCTL,    RS_a,  Z,   0, a2, 0, 0, lctl, 0, IF_PRIV)
    F(0xeb2f, LCTLG,   RSY_a, Z,   0, a2, 0, 0, lctlg, 0, IF_PRIV)
/* LOAD PROGRAM PARAMETER */
    F(0xb280, LPP,     S,   LPP,   0, m2_64, 0, 0, lpp, 0, IF_PRIV)
/* LOAD PSW */
    F(0x8200, LPSW,    S,     Z,   0, a2, 0, 0, lpsw, 0, IF_PRIV)
/* LOAD PSW EXTENDED */
    F(0xb2b2, LPSWE,   S,     Z,   0, a2, 0, 0, lpswe, 0, IF_PRIV)
/* LOAD REAL ADDRESS */
    F(0xb100, LRA,     RX_a,  Z,   0, a2, r1, 0, lra, 0, IF_PRIV)
    F(0xe313, LRAY,    RXY_a, LD,  0, a2, r1, 0, lra, 0, IF_PRIV)
    F(0xe303, LRAG,    RXY_a, Z,   0, a2, r1, 0, lra, 0, IF_PRIV)
/* LOAD USING REAL ADDRESS */
    E(0xb24b, LURA,    RRE,   Z,   0, ra2, new, r1_32, lura, 0, MO_TEUL, IF_PRIV)
    E(0xb905, LURAG,   RRE,   Z,   0, ra2, r1, 0, lura, 0, MO_TEUQ, IF_PRIV)
/* MOVE TO PRIMARY */
    C(0xda00, MVCP,    SS_d,  Z,   la1, a2, 0, 0, mvcp, 0)
/* MOVE TO SECONDARY */
    C(0xdb00, MVCS,    SS_d,  Z,   la1, a2, 0, 0, mvcs, 0)
/* PURGE TLB */
    F(0xb20d, PTLB,    S,     Z,   0, 0, 0, 0, ptlb, 0, IF_PRIV)
/* RESET REFERENCE BIT EXTENDED */
    F(0xb22a, RRBE,    RRE,   Z,   0, r2_o, 0, 0, rrbe, 0, IF_PRIV)
/* SERVICE CALL LOGICAL PROCESSOR (PV hypercall) */
    F(0xb220, SERVC,   RRE,   Z,   r1_o, r2_o, 0, 0, servc, 0, IF_PRIV | IF_IO)
/* SET ADDRESS SPACE CONTROL FAST */
    C(0xb279, SACF,    S,     Z,   0, a2, 0, 0, sacf, 0)
/* SET CLOCK */
    F(0xb204, SCK,     S,     Z,   0, m2_64a, 0, 0, sck, 0, IF_PRIV | IF_IO)
/* SET CLOCK COMPARATOR */
    F(0xb206, SCKC,    S,     Z,   0, m2_64a, 0, 0, sckc, 0, IF_PRIV | IF_IO)
/* SET CLOCK PROGRAMMABLE FIELD */
    F(0x0107, SCKPF,   E,     Z,   0, 0, 0, 0, sckpf, 0, IF_PRIV)
/* SET CPU TIMER */
    F(0xb208, SPT,     S,     Z,   0, m2_64a, 0, 0, spt, 0, IF_PRIV | IF_IO)
/* SET PREFIX */
    F(0xb210, SPX,     S,     Z,   0, m2_32ua, 0, 0, spx, 0, IF_PRIV)
/* SET PSW KEY FROM ADDRESS */
    F(0xb20a, SPKA,    S,     Z,   0, a2, 0, 0, spka, 0, IF_PRIV)
/* SET STORAGE KEY EXTENDED */
    F(0xb22b, SSKE,    RRF_c, Z,   r1_o, r2_o, 0, 0, sske, 0, IF_PRIV)
/* SET SYSTEM MASK */
    F(0x8000, SSM,     S,     Z,   0, m2_8u, 0, 0, ssm, 0, IF_PRIV)
/* SIGNAL PROCESSOR */
    F(0xae00, SIGP,    RS_a,  Z,   0, a2, 0, 0, sigp, 0, IF_PRIV | IF_IO)
/* STORE CLOCK COMPARATOR */
    F(0xb207, STCKC,   S,     Z,   la2, 0, new, m1_64a, stckc, 0, IF_PRIV)
/* STORE CONTROL */
    F(0xb600, STCTL,   RS_a,  Z,   0, a2, 0, 0, stctl, 0, IF_PRIV)
    F(0xeb25, STCTG,   RSY_a, Z,   0, a2, 0, 0, stctg, 0, IF_PRIV)
/* STORE CPU ADDRESS */
    F(0xb212, STAP,    S,     Z,   la2, 0, new, m1_16a, stap, 0, IF_PRIV)
/* STORE CPU ID */
    F(0xb202, STIDP,   S,     Z,   la2, 0, new, m1_64a, stidp, 0, IF_PRIV)
/* STORE CPU TIMER */
    F(0xb209, STPT,    S,     Z,   la2, 0, new, m1_64a, stpt, 0, IF_PRIV | IF_IO)
/* STORE FACILITY LIST */
    F(0xb2b1, STFL,    S,     Z,   0, 0, 0, 0, stfl, 0, IF_PRIV)
/* STORE PREFIX */
    F(0xb211, STPX,    S,     Z,   la2, 0, new, m1_32a, stpx, 0, IF_PRIV)
/* STORE SYSTEM INFORMATION */
    F(0xb27d, STSI,    S,     Z,   0, a2, 0, 0, stsi, 0, IF_PRIV)
/* STORE THEN AND SYSTEM MASK */
    F(0xac00, STNSM,   SI,    Z,   la1, 0, 0, 0, stnosm, 0, IF_PRIV)
/* STORE THEN OR SYSTEM MASK */
    F(0xad00, STOSM,   SI,    Z,   la1, 0, 0, 0, stnosm, 0, IF_PRIV)
/* STORE USING REAL ADDRESS */
    E(0xb246, STURA,   RRE,   Z,   r1_o, ra2, 0, 0, stura, 0, MO_TEUL, IF_PRIV)
    E(0xb925, STURG,   RRE,   Z,   r1_o, ra2, 0, 0, stura, 0, MO_TEUQ, IF_PRIV)
/* TEST BLOCK */
    F(0xb22c, TB,      RRE,   Z,   0, r2_o, 0, 0, testblock, 0, IF_PRIV)
/* TEST PROTECTION */
    C(0xe501, TPROT,   SSE,   Z,   la1, a2, 0, 0, tprot, 0)

/* CCW I/O Instructions */
    F(0xb276, XSCH,    S,     Z,   0, 0, 0, 0, xsch, 0, IF_PRIV | IF_IO)
    F(0xb230, CSCH,    S,     Z,   0, 0, 0, 0, csch, 0, IF_PRIV | IF_IO)
    F(0xb231, HSCH,    S,     Z,   0, 0, 0, 0, hsch, 0, IF_PRIV | IF_IO)
    F(0xb232, MSCH,    S,     Z,   0, insn, 0, 0, msch, 0, IF_PRIV | IF_IO)
    F(0xb23b, RCHP,    S,     Z,   0, 0, 0, 0, rchp, 0, IF_PRIV | IF_IO)
    F(0xb238, RSCH,    S,     Z,   0, 0, 0, 0, rsch, 0, IF_PRIV | IF_IO)
    F(0xb237, SAL,     S,     Z,   0, 0, 0, 0, sal, 0, IF_PRIV | IF_IO)
    F(0xb23c, SCHM,    S,     Z,   0, insn, 0, 0, schm, 0, IF_PRIV | IF_IO)
    F(0xb274, SIGA,    S,     Z,   0, 0, 0, 0, siga, 0, IF_PRIV | IF_IO)
    F(0xb23a, STCPS,   S,     Z,   0, 0, 0, 0, stcps, 0, IF_PRIV | IF_IO)
    F(0xb233, SSCH,    S,     Z,   0, insn, 0, 0, ssch, 0, IF_PRIV | IF_IO)
    F(0xb239, STCRW,   S,     Z,   0, insn, 0, 0, stcrw, 0, IF_PRIV | IF_IO)
    F(0xb234, STSCH,   S,     Z,   0, insn, 0, 0, stsch, 0, IF_PRIV | IF_IO)
    F(0xb236, TPI ,    S,     Z,   la2, 0, 0, 0, tpi, 0, IF_PRIV | IF_IO)
    F(0xb235, TSCH,    S,     Z,   0, insn, 0, 0, tsch, 0, IF_PRIV | IF_IO)
    /* ??? Not listed in PoO ninth edition, but there's a linux driver that
       uses it: "A CHSC subchannel is usually present on LPAR only."  */
    F(0xb25f, CHSC,  RRE,     Z,   0, insn, 0, 0, chsc, 0, IF_PRIV | IF_IO)

/* zPCI Instructions */
    /* None of these instructions are documented in the PoP, so this is all
       based upon target/s390x/kvm.c and Linux code and likely incomplete */
    F(0xebd0, PCISTB, RSY_a, PCI, la2, 0, 0, 0, pcistb, 0, IF_PRIV | IF_IO)
    F(0xebd1, SIC, RSY_a, AIS, r1, r3, 0, 0, sic, 0, IF_PRIV | IF_IO)
    F(0xb9a0, CLP, RRF_c, PCI, 0, 0, 0, 0, clp, 0, IF_PRIV | IF_IO)
    F(0xb9d0, PCISTG, RRE, PCI, 0, 0, 0, 0, pcistg, 0, IF_PRIV | IF_IO)
    F(0xb9d2, PCILG, RRE, PCI, 0, 0, 0, 0, pcilg, 0, IF_PRIV | IF_IO)
    F(0xb9d3, RPCIT, RRE, PCI, 0, 0, 0, 0, rpcit, 0, IF_PRIV | IF_IO)
    F(0xe3d0, MPCIFC, RXY_a, PCI, la2, 0, 0, 0, mpcifc, 0, IF_PRIV | IF_IO)
    F(0xe3d4, STPCIFC, RXY_a, PCI, la2, 0, 0, 0, stpcifc, 0, IF_PRIV | IF_IO)

#endif /* CONFIG_USER_ONLY */
