#define _GEN_FLOAT_ACB(name, op, op1, op2, isfloat, set_fprf, type)           \
GEN_HANDLER(f##name, op1, op2, 0xFF, 0x00000000, type)
#define GEN_FLOAT_ACB(name, op2, set_fprf, type)                              \
_GEN_FLOAT_ACB(name, name, 0x3F, op2, 0, set_fprf, type),                     \
_GEN_FLOAT_ACB(name##s, name, 0x3B, op2, 1, set_fprf, type)
#define _GEN_FLOAT_AB(name, op, op1, op2, inval, isfloat, set_fprf, type)     \
GEN_HANDLER(f##name, op1, op2, 0xFF, inval, type)
#define GEN_FLOAT_AB(name, op2, inval, set_fprf, type)                        \
_GEN_FLOAT_AB(name, name, 0x3F, op2, inval, 0, set_fprf, type),               \
_GEN_FLOAT_AB(name##s, name, 0x3B, op2, inval, 1, set_fprf, type)
#define _GEN_FLOAT_AC(name, op, op1, op2, inval, isfloat, set_fprf, type)     \
GEN_HANDLER(f##name, op1, op2, 0xFF, inval, type)
#define GEN_FLOAT_AC(name, op2, inval, set_fprf, type)                        \
_GEN_FLOAT_AC(name, name, 0x3F, op2, inval, 0, set_fprf, type),               \
_GEN_FLOAT_AC(name##s, name, 0x3B, op2, inval, 1, set_fprf, type)
#define GEN_FLOAT_B(name, op2, op3, set_fprf, type)                           \
GEN_HANDLER(f##name, 0x3F, op2, op3, 0x001F0000, type)
#define GEN_FLOAT_BS(name, op1, op2, set_fprf, type)                          \
GEN_HANDLER(f##name, op1, op2, 0xFF, 0x001F07C0, type)

GEN_FLOAT_AB(add, 0x15, 0x000007C0, 1, PPC_FLOAT),
GEN_FLOAT_AB(div, 0x12, 0x000007C0, 1, PPC_FLOAT),
GEN_FLOAT_AC(mul, 0x19, 0x0000F800, 1, PPC_FLOAT),
GEN_FLOAT_BS(re, 0x3F, 0x18, 1, PPC_FLOAT_EXT),
GEN_FLOAT_BS(res, 0x3B, 0x18, 1, PPC_FLOAT_FRES),
GEN_FLOAT_BS(rsqrte, 0x3F, 0x1A, 1, PPC_FLOAT_FRSQRTE),
_GEN_FLOAT_ACB(sel, sel, 0x3F, 0x17, 0, 0, PPC_FLOAT_FSEL),
GEN_FLOAT_AB(sub, 0x14, 0x000007C0, 1, PPC_FLOAT),
GEN_FLOAT_ACB(madd, 0x1D, 1, PPC_FLOAT),
GEN_FLOAT_ACB(msub, 0x1C, 1, PPC_FLOAT),
GEN_FLOAT_ACB(nmadd, 0x1F, 1, PPC_FLOAT),
GEN_FLOAT_ACB(nmsub, 0x1E, 1, PPC_FLOAT),
GEN_HANDLER_E(ftdiv, 0x3F, 0x00, 0x04, 1, PPC_NONE, PPC2_FP_TST_ISA206),
GEN_HANDLER_E(ftsqrt, 0x3F, 0x00, 0x05, 1, PPC_NONE, PPC2_FP_TST_ISA206),
GEN_FLOAT_B(ctiw, 0x0E, 0x00, 0, PPC_FLOAT),
GEN_HANDLER_E(fctiwu, 0x3F, 0x0E, 0x04, 0, PPC_NONE, PPC2_FP_CVT_ISA206),
GEN_FLOAT_B(ctiwz, 0x0F, 0x00, 0, PPC_FLOAT),
GEN_HANDLER_E(fctiwuz, 0x3F, 0x0F, 0x04, 0, PPC_NONE, PPC2_FP_CVT_ISA206),
GEN_FLOAT_B(rsp, 0x0C, 0x00, 1, PPC_FLOAT),
GEN_HANDLER_E(fcfid, 0x3F, 0x0E, 0x1A, 0x001F0000, PPC_NONE, PPC2_FP_CVT_S64),
GEN_HANDLER_E(fcfids, 0x3B, 0x0E, 0x1A, 0, PPC_NONE, PPC2_FP_CVT_ISA206),
GEN_HANDLER_E(fcfidu, 0x3F, 0x0E, 0x1E, 0, PPC_NONE, PPC2_FP_CVT_ISA206),
GEN_HANDLER_E(fcfidus, 0x3B, 0x0E, 0x1E, 0, PPC_NONE, PPC2_FP_CVT_ISA206),
GEN_HANDLER_E(fctid, 0x3F, 0x0E, 0x19, 0x001F0000, PPC_NONE, PPC2_FP_CVT_S64),
GEN_HANDLER_E(fctidu, 0x3F, 0x0E, 0x1D, 0, PPC_NONE, PPC2_FP_CVT_ISA206),
GEN_HANDLER_E(fctidz, 0x3F, 0x0F, 0x19, 0x001F0000, PPC_NONE, PPC2_FP_CVT_S64),
GEN_HANDLER_E(fctiduz, 0x3F, 0x0F, 0x1D, 0, PPC_NONE, PPC2_FP_CVT_ISA206),
GEN_FLOAT_B(rin, 0x08, 0x0C, 1, PPC_FLOAT_EXT),
GEN_FLOAT_B(riz, 0x08, 0x0D, 1, PPC_FLOAT_EXT),
GEN_FLOAT_B(rip, 0x08, 0x0E, 1, PPC_FLOAT_EXT),
GEN_FLOAT_B(rim, 0x08, 0x0F, 1, PPC_FLOAT_EXT),

#define GEN_LDF(name, ldop, opc, type)                                        \
GEN_HANDLER(name, opc, 0xFF, 0xFF, 0x00000000, type),
#define GEN_LDUF(name, ldop, opc, type)                                       \
GEN_HANDLER(name##u, opc, 0xFF, 0xFF, 0x00000000, type),
#define GEN_LDUXF(name, ldop, opc, type)                                      \
GEN_HANDLER(name##ux, 0x1F, 0x17, opc, 0x00000001, type),
#define GEN_LDXF(name, ldop, opc2, opc3, type)                                \
GEN_HANDLER(name##x, 0x1F, opc2, opc3, 0x00000001, type),
#define GEN_LDFS(name, ldop, op, type)                                        \
GEN_LDF(name, ldop, op | 0x20, type)                                          \
GEN_LDUF(name, ldop, op | 0x21, type)                                         \
GEN_LDUXF(name, ldop, op | 0x01, type)                                        \
GEN_LDXF(name, ldop, 0x17, op | 0x00, type)

GEN_LDFS(lfd, ld64, 0x12, PPC_FLOAT)
GEN_LDFS(lfs, ld32fs, 0x10, PPC_FLOAT)
GEN_HANDLER_E(lfiwax, 0x1f, 0x17, 0x1a, 0x00000001, PPC_NONE, PPC2_ISA205),
GEN_HANDLER_E(lfiwzx, 0x1f, 0x17, 0x1b, 0x1, PPC_NONE, PPC2_FP_CVT_ISA206),
GEN_HANDLER_E(lfdpx, 0x1F, 0x17, 0x18, 0x00200001, PPC_NONE, PPC2_ISA205),

#define GEN_STF(name, stop, opc, type)                                        \
GEN_HANDLER(name, opc, 0xFF, 0xFF, 0x00000000, type),
#define GEN_STUF(name, stop, opc, type)                                       \
GEN_HANDLER(name##u, opc, 0xFF, 0xFF, 0x00000000, type),
#define GEN_STUXF(name, stop, opc, type)                                      \
GEN_HANDLER(name##ux, 0x1F, 0x17, opc, 0x00000001, type),
#define GEN_STXF(name, stop, opc2, opc3, type)                                \
GEN_HANDLER(name##x, 0x1F, opc2, opc3, 0x00000001, type),
#define GEN_STFS(name, stop, op, type)                                        \
GEN_STF(name, stop, op | 0x20, type)                                          \
GEN_STUF(name, stop, op | 0x21, type)                                         \
GEN_STUXF(name, stop, op | 0x01, type)                                        \
GEN_STXF(name, stop, 0x17, op | 0x00, type)

GEN_STFS(stfd, st64_i64, 0x16, PPC_FLOAT)
GEN_STFS(stfs, st32fs, 0x14, PPC_FLOAT)
GEN_STXF(stfiw, st32fiw, 0x17, 0x1E, PPC_FLOAT_STFIWX)
GEN_HANDLER_E(stfdpx, 0x1F, 0x17, 0x1C, 0x00200001, PPC_NONE, PPC2_ISA205),

GEN_HANDLER(frsqrtes, 0x3B, 0x1A, 0xFF, 0x001F07C0, PPC_FLOAT_FRSQRTES),
GEN_HANDLER(fsqrt, 0x3F, 0x16, 0xFF, 0x001F07C0, PPC_FLOAT_FSQRT),
GEN_HANDLER(fsqrts, 0x3B, 0x16, 0xFF, 0x001F07C0, PPC_FLOAT_FSQRT),
GEN_HANDLER(fcmpo, 0x3F, 0x00, 0x01, 0x00600001, PPC_FLOAT),
GEN_HANDLER(fcmpu, 0x3F, 0x00, 0x00, 0x00600001, PPC_FLOAT),
GEN_HANDLER(fabs, 0x3F, 0x08, 0x08, 0x001F0000, PPC_FLOAT),
GEN_HANDLER(fmr, 0x3F, 0x08, 0x02, 0x001F0000, PPC_FLOAT),
GEN_HANDLER(fnabs, 0x3F, 0x08, 0x04, 0x001F0000, PPC_FLOAT),
GEN_HANDLER(fneg, 0x3F, 0x08, 0x01, 0x001F0000, PPC_FLOAT),
GEN_HANDLER_E(fcpsgn, 0x3F, 0x08, 0x00, 0x00000000, PPC_NONE, PPC2_ISA205),
GEN_HANDLER_E(fmrgew, 0x3F, 0x06, 0x1E, 0x00000001, PPC_NONE, PPC2_VSX207),
GEN_HANDLER_E(fmrgow, 0x3F, 0x06, 0x1A, 0x00000001, PPC_NONE, PPC2_VSX207),
GEN_HANDLER(mcrfs, 0x3F, 0x00, 0x02, 0x0063F801, PPC_FLOAT),
GEN_HANDLER(mffs, 0x3F, 0x07, 0x12, 0x001FF800, PPC_FLOAT),
GEN_HANDLER(mtfsb0, 0x3F, 0x06, 0x02, 0x001FF800, PPC_FLOAT),
GEN_HANDLER(mtfsb1, 0x3F, 0x06, 0x01, 0x001FF800, PPC_FLOAT),
GEN_HANDLER(mtfsf, 0x3F, 0x07, 0x16, 0x00000000, PPC_FLOAT),
GEN_HANDLER(mtfsfi, 0x3F, 0x06, 0x04, 0x006e0800, PPC_FLOAT),
