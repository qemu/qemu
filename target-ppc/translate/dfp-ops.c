#define _GEN_DFP_LONG(name, op1, op2, mask) \
GEN_HANDLER_E(name, 0x3B, op1, op2, mask, PPC_NONE, PPC2_DFP)

#define _GEN_DFP_LONG_300(name, op1, op2, mask)                   \
GEN_HANDLER_E(name, 0x3B, op1, op2, mask, PPC_NONE, PPC2_ISA300)

#define _GEN_DFP_LONGx2(name, op1, op2, mask) \
GEN_HANDLER_E(name, 0x3B, op1, 0x00 | op2, mask, PPC_NONE, PPC2_DFP), \
GEN_HANDLER_E(name, 0x3B, op1, 0x10 | op2, mask, PPC_NONE, PPC2_DFP)

#define _GEN_DFP_LONGx4(name, op1, op2, mask) \
GEN_HANDLER_E(name, 0x3B, op1, 0x00 | op2, mask, PPC_NONE, PPC2_DFP), \
GEN_HANDLER_E(name, 0x3B, op1, 0x08 | op2, mask, PPC_NONE, PPC2_DFP), \
GEN_HANDLER_E(name, 0x3B, op1, 0x10 | op2, mask, PPC_NONE, PPC2_DFP), \
GEN_HANDLER_E(name, 0x3B, op1, 0x18 | op2, mask, PPC_NONE, PPC2_DFP)

#define _GEN_DFP_QUAD(name, op1, op2, mask) \
GEN_HANDLER_E(name, 0x3F, op1, op2, mask, PPC_NONE, PPC2_DFP)

#define _GEN_DFP_QUAD_300(name, op1, op2, mask)             \
GEN_HANDLER_E(name, 0x3F, op1, op2, mask, PPC_NONE, PPC2_ISA300)

#define _GEN_DFP_QUADx2(name, op1, op2, mask) \
GEN_HANDLER_E(name, 0x3F, op1, 0x00 | op2, mask, PPC_NONE, PPC2_DFP), \
GEN_HANDLER_E(name, 0x3F, op1, 0x10 | op2, mask, PPC_NONE, PPC2_DFP)

#define _GEN_DFP_QUADx4(name, op1, op2, mask)                         \
GEN_HANDLER_E(name, 0x3F, op1, 0x00 | op2, mask, PPC_NONE, PPC2_DFP), \
GEN_HANDLER_E(name, 0x3F, op1, 0x08 | op2, mask, PPC_NONE, PPC2_DFP), \
GEN_HANDLER_E(name, 0x3F, op1, 0x10 | op2, mask, PPC_NONE, PPC2_DFP), \
GEN_HANDLER_E(name, 0x3F, op1, 0x18 | op2, mask, PPC_NONE, PPC2_DFP)

#define GEN_DFP_T_A_B_Rc(name, op1, op2) \
_GEN_DFP_LONG(name, op1, op2, 0x00000000)

#define GEN_DFP_Tp_Ap_Bp_Rc(name, op1, op2) \
_GEN_DFP_QUAD(name, op1, op2, 0x00210800)

#define GEN_DFP_Tp_A_Bp_Rc(name, op1, op2) \
_GEN_DFP_QUAD(name, op1, op2, 0x00200800)

#define GEN_DFP_T_B_Rc(name, op1, op2) \
_GEN_DFP_LONG(name, op1, op2, 0x001F0000)

#define GEN_DFP_Tp_Bp_Rc(name, op1, op2) \
_GEN_DFP_QUAD(name, op1, op2, 0x003F0800)

#define GEN_DFP_Tp_B_Rc(name, op1, op2) \
_GEN_DFP_QUAD(name, op1, op2, 0x003F0000)

#define GEN_DFP_T_Bp_Rc(name, op1, op2) \
_GEN_DFP_QUAD(name, op1, op2, 0x001F0800)

#define GEN_DFP_BF_A_B(name, op1, op2) \
_GEN_DFP_LONG(name, op1, op2, 0x00000001)

#define GEN_DFP_BF_A_B_300(name, op1, op2)          \
_GEN_DFP_LONG_300(name, op1, op2, 0x00400001)

#define GEN_DFP_BF_Ap_Bp(name, op1, op2) \
_GEN_DFP_QUAD(name, op1, op2, 0x00610801)

#define GEN_DFP_BF_A_Bp(name, op1, op2) \
_GEN_DFP_QUAD(name, op1, op2, 0x00600801)

#define GEN_DFP_BF_A_Bp_300(name, op1, op2)     \
_GEN_DFP_QUAD_300(name, op1, op2, 0x00400001)

#define GEN_DFP_BF_A_DCM(name, op1, op2) \
_GEN_DFP_LONGx2(name, op1, op2, 0x00600001)

#define GEN_DFP_BF_Ap_DCM(name, op1, op2) \
_GEN_DFP_QUADx2(name, op1, op2, 0x00610001)

#define GEN_DFP_T_A_B_RMC_Rc(name, op1, op2) \
_GEN_DFP_LONGx4(name, op1, op2, 0x00000000)

#define GEN_DFP_Tp_Ap_Bp_RMC_Rc(name, op1, op2) \
_GEN_DFP_QUADx4(name, op1, op2, 0x02010800)

#define GEN_DFP_Tp_A_Bp_RMC_Rc(name, op1, op2) \
_GEN_DFP_QUADx4(name, op1, op2, 0x02000800)

#define GEN_DFP_TE_T_B_RMC_Rc(name, op1, op2) \
_GEN_DFP_LONGx4(name, op1, op2, 0x00000000)

#define GEN_DFP_TE_Tp_Bp_RMC_Rc(name, op1, op2) \
_GEN_DFP_QUADx4(name, op1, op2, 0x00200800)

#define GEN_DFP_R_T_B_RMC_Rc(name, op1, op2) \
_GEN_DFP_LONGx4(name, op1, op2, 0x001E0000)

#define GEN_DFP_R_Tp_Bp_RMC_Rc(name, op1, op2) \
_GEN_DFP_QUADx4(name, op1, op2, 0x003E0800)

#define GEN_DFP_SP_T_B_Rc(name, op1, op2) \
_GEN_DFP_LONG(name, op1, op2, 0x00070000)

#define GEN_DFP_SP_Tp_Bp_Rc(name, op1, op2) \
_GEN_DFP_QUAD(name, op1, op2, 0x00270800)

#define GEN_DFP_S_T_B_Rc(name, op1, op2) \
_GEN_DFP_LONG(name, op1, op2, 0x000F0000)

#define GEN_DFP_S_Tp_Bp_Rc(name, op1, op2) \
_GEN_DFP_QUAD(name, op1, op2, 0x002F0800)

#define GEN_DFP_T_A_SH_Rc(name, op1, op2) \
_GEN_DFP_LONGx2(name, op1, op2, 0x00000000)

#define GEN_DFP_Tp_Ap_SH_Rc(name, op1, op2) \
_GEN_DFP_QUADx2(name, op1, op2, 0x00210000)

GEN_DFP_T_A_B_Rc(dadd, 0x02, 0x00),
GEN_DFP_Tp_Ap_Bp_Rc(daddq, 0x02, 0x00),
GEN_DFP_T_A_B_Rc(dsub, 0x02, 0x10),
GEN_DFP_Tp_Ap_Bp_Rc(dsubq, 0x02, 0x10),
GEN_DFP_T_A_B_Rc(dmul, 0x02, 0x01),
GEN_DFP_Tp_Ap_Bp_Rc(dmulq, 0x02, 0x01),
GEN_DFP_T_A_B_Rc(ddiv, 0x02, 0x11),
GEN_DFP_Tp_Ap_Bp_Rc(ddivq, 0x02, 0x11),
GEN_DFP_BF_A_B(dcmpu, 0x02, 0x14),
GEN_DFP_BF_Ap_Bp(dcmpuq, 0x02, 0x14),
GEN_DFP_BF_A_B(dcmpo, 0x02, 0x04),
GEN_DFP_BF_Ap_Bp(dcmpoq, 0x02, 0x04),
GEN_DFP_BF_A_DCM(dtstdc, 0x02, 0x06),
GEN_DFP_BF_Ap_DCM(dtstdcq, 0x02, 0x06),
GEN_DFP_BF_A_DCM(dtstdg, 0x02, 0x07),
GEN_DFP_BF_Ap_DCM(dtstdgq, 0x02, 0x07),
GEN_DFP_BF_A_B(dtstex, 0x02, 0x05),
GEN_DFP_BF_Ap_Bp(dtstexq, 0x02, 0x05),
GEN_DFP_BF_A_B(dtstsf, 0x02, 0x15),
GEN_DFP_BF_A_Bp(dtstsfq, 0x02, 0x15),
GEN_DFP_BF_A_B_300(dtstsfi, 0x03, 0x15),
GEN_DFP_BF_A_Bp_300(dtstsfiq, 0x03, 0x15),
GEN_DFP_TE_T_B_RMC_Rc(dquai, 0x03, 0x02),
GEN_DFP_TE_Tp_Bp_RMC_Rc(dquaiq, 0x03, 0x02),
GEN_DFP_T_A_B_RMC_Rc(dqua, 0x03, 0x00),
GEN_DFP_Tp_Ap_Bp_RMC_Rc(dquaq, 0x03, 0x00),
GEN_DFP_T_A_B_RMC_Rc(drrnd, 0x03, 0x01),
GEN_DFP_Tp_A_Bp_RMC_Rc(drrndq, 0x03, 0x01),
GEN_DFP_R_T_B_RMC_Rc(drintx, 0x03, 0x03),
GEN_DFP_R_Tp_Bp_RMC_Rc(drintxq, 0x03, 0x03),
GEN_DFP_R_T_B_RMC_Rc(drintn, 0x03, 0x07),
GEN_DFP_R_Tp_Bp_RMC_Rc(drintnq, 0x03, 0x07),
GEN_DFP_T_B_Rc(dctdp, 0x02, 0x08),
GEN_DFP_Tp_B_Rc(dctqpq, 0x02, 0x08),
GEN_DFP_T_B_Rc(drsp, 0x02, 0x18),
GEN_DFP_Tp_Bp_Rc(drdpq, 0x02, 0x18),
GEN_DFP_T_B_Rc(dcffix, 0x02, 0x19),
GEN_DFP_Tp_B_Rc(dcffixq, 0x02, 0x19),
GEN_DFP_T_B_Rc(dctfix, 0x02, 0x09),
GEN_DFP_T_Bp_Rc(dctfixq, 0x02, 0x09),
GEN_DFP_SP_T_B_Rc(ddedpd, 0x02, 0x0a),
GEN_DFP_SP_Tp_Bp_Rc(ddedpdq, 0x02, 0x0a),
GEN_DFP_S_T_B_Rc(denbcd, 0x02, 0x1a),
GEN_DFP_S_Tp_Bp_Rc(denbcdq, 0x02, 0x1a),
GEN_DFP_T_B_Rc(dxex, 0x02, 0x0b),
GEN_DFP_T_Bp_Rc(dxexq, 0x02, 0x0b),
GEN_DFP_T_A_B_Rc(diex, 0x02, 0x1b),
GEN_DFP_Tp_A_Bp_Rc(diexq, 0x02, 0x1b),
GEN_DFP_T_A_SH_Rc(dscli, 0x02, 0x02),
GEN_DFP_Tp_Ap_SH_Rc(dscliq, 0x02, 0x02),
GEN_DFP_T_A_SH_Rc(dscri, 0x02, 0x03),
GEN_DFP_Tp_Ap_SH_Rc(dscriq, 0x02, 0x03),
