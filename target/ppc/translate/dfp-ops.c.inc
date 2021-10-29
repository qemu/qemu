#define _GEN_DFP_LONG(name, op1, op2, mask) \
GEN_HANDLER_E(name, 0x3B, op1, op2, mask, PPC_NONE, PPC2_DFP)

#define _GEN_DFP_LONGx2(name, op1, op2, mask) \
GEN_HANDLER_E(name, 0x3B, op1, 0x00 | op2, mask, PPC_NONE, PPC2_DFP), \
GEN_HANDLER_E(name, 0x3B, op1, 0x10 | op2, mask, PPC_NONE, PPC2_DFP)

#define _GEN_DFP_QUAD(name, op1, op2, mask) \
GEN_HANDLER_E(name, 0x3F, op1, op2, mask, PPC_NONE, PPC2_DFP)

#define _GEN_DFP_QUADx2(name, op1, op2, mask) \
GEN_HANDLER_E(name, 0x3F, op1, 0x00 | op2, mask, PPC_NONE, PPC2_DFP), \
GEN_HANDLER_E(name, 0x3F, op1, 0x10 | op2, mask, PPC_NONE, PPC2_DFP)

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

GEN_DFP_SP_T_B_Rc(ddedpd, 0x02, 0x0a),
GEN_DFP_SP_Tp_Bp_Rc(ddedpdq, 0x02, 0x0a),
GEN_DFP_S_T_B_Rc(denbcd, 0x02, 0x1a),
GEN_DFP_S_Tp_Bp_Rc(denbcdq, 0x02, 0x1a),
GEN_DFP_T_A_SH_Rc(dscli, 0x02, 0x02),
GEN_DFP_Tp_Ap_SH_Rc(dscliq, 0x02, 0x02),
GEN_DFP_T_A_SH_Rc(dscri, 0x02, 0x03),
GEN_DFP_Tp_Ap_SH_Rc(dscriq, 0x02, 0x03),
