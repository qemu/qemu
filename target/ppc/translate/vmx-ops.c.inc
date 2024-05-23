#define GEN_VXFORM(name, opc2, opc3)                                    \
GEN_HANDLER(name, 0x04, opc2, opc3, 0x00000000, PPC_ALTIVEC)

#define GEN_VXFORM_207(name, opc2, opc3) \
GEN_HANDLER_E(name, 0x04, opc2, opc3, 0x00000000, PPC_NONE, PPC2_ALTIVEC_207)

#define GEN_VXFORM_300(name, opc2, opc3)                                \
GEN_HANDLER_E(name, 0x04, opc2, opc3, 0x00000000, PPC_NONE, PPC2_ISA300)

#define GEN_VXFORM_300_EXT(name, opc2, opc3, inval)                     \
GEN_HANDLER_E(name, 0x04, opc2, opc3, inval, PPC_NONE, PPC2_ISA300)

#define GEN_VXFORM_300_EO(name, opc2, opc3, opc4)                     \
GEN_HANDLER_E_2(name, 0x04, opc2, opc3, opc4, 0x00000000, PPC_NONE,     \
                                                       PPC2_ISA300)

#define GEN_VXFORM_310(name, opc2, opc3)                                \
GEN_HANDLER_E(name, 0x04, opc2, opc3, 0x00000000, PPC_NONE, PPC2_ISA310)

#define GEN_VXFORM_DUAL(name0, name1, opc2, opc3, type0, type1) \
GEN_HANDLER_E(name0##_##name1, 0x4, opc2, opc3, 0x00000000, type0, type1)

#define GEN_VXRFORM_DUAL(name0, name1, opc2, opc3, tp0, tp1) \
GEN_HANDLER_E(name0##_##name1, 0x4, opc2, opc3, 0x00000000, tp0, tp1), \
GEN_HANDLER_E(name0##_##name1, 0x4, opc2, (opc3 | 0x10), 0x00000000, tp0, tp1),

GEN_VXFORM_DUAL(vaddubm, vmul10cuq, 0, 0, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM_DUAL(vadduhm, vmul10ecuq, 0, 1, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM(vadduwm, 0, 2),
GEN_VXFORM_207(vaddudm, 0, 3),
GEN_VXFORM_DUAL(vsububm, bcdadd, 0, 16, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM_DUAL(vsubuhm, bcdsub, 0, 17, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM_DUAL(vsubuwm, bcdus, 0, 18, PPC_ALTIVEC, PPC2_ISA300),
GEN_VXFORM_DUAL(vsubudm, bcds, 0, 19, PPC2_ALTIVEC_207, PPC2_ISA300),
GEN_VXFORM_300(bcds, 0, 27),
GEN_VXFORM(vmrghb, 6, 0),
GEN_VXFORM(vmrghh, 6, 1),
GEN_VXFORM(vmrghw, 6, 2),
GEN_VXFORM(vmrglb, 6, 4),
GEN_VXFORM(vmrglh, 6, 5),
GEN_VXFORM(vmrglw, 6, 6),
GEN_VXFORM_300(vextublx, 6, 24),
GEN_VXFORM_300(vextuhlx, 6, 25),
GEN_VXFORM_DUAL(vmrgow, vextuwlx, 6, 26, PPC_NONE, PPC2_ALTIVEC_207),
GEN_VXFORM_300(vextubrx, 6, 28),
GEN_VXFORM_300(vextuhrx, 6, 29),
GEN_VXFORM_DUAL(vmrgew, vextuwrx, 6, 30, PPC_NONE, PPC2_ALTIVEC_207),
GEN_VXFORM_207(vmuluwm, 4, 2),
GEN_VXFORM_300(vsrv, 2, 28),
GEN_VXFORM_300(vslv, 2, 29),
GEN_VXFORM(vslo, 6, 16),
GEN_VXFORM(vsro, 6, 17),

GEN_VXFORM(xpnd04_1, 0, 22),
GEN_VXFORM_300(bcdsr, 0, 23),
GEN_VXFORM_300(bcdsr, 0, 31),
GEN_VXFORM_300_EXT(vmul10uq, 0, 8, 0x0000F800),
GEN_VXFORM_300(vmul10euq, 0, 9),
GEN_VXFORM_300(bcdcpsgn, 0, 13),
GEN_VXFORM_207(bcdadd, 0, 24),
GEN_VXFORM_207(bcdsub, 0, 25),
GEN_VXFORM_300(bcdtrunc, 0, 28),
GEN_VXFORM_300(xpnd04_2, 0, 30),
GEN_VXFORM_300(bcdtrunc, 0, 20),
GEN_VXFORM_300(bcdutrunc, 0, 21),
GEN_VXFORM(vsl, 2, 7),
GEN_VXFORM(vsr, 2, 11),
GEN_VXFORM(vpkuhum, 7, 0),
GEN_VXFORM(vpkuwum, 7, 1),
GEN_VXFORM_207(vpkudum, 7, 17),
GEN_VXFORM(vpkuhus, 7, 2),
GEN_VXFORM(vpkuwus, 7, 3),
GEN_VXFORM_207(vpkudus, 7, 19),
GEN_VXFORM(vpkshus, 7, 4),
GEN_VXFORM(vpkswus, 7, 5),
GEN_VXFORM_207(vpksdus, 7, 21),
GEN_VXFORM(vpkshss, 7, 6),
GEN_VXFORM(vpkswss, 7, 7),
GEN_VXFORM_207(vpksdss, 7, 23),
GEN_VXFORM(vpkpx, 7, 12),
GEN_VXFORM(vsum4ubs, 4, 24),
GEN_VXFORM(vsum4sbs, 4, 28),
GEN_VXFORM(vsum4shs, 4, 25),
GEN_VXFORM(vsum2sws, 4, 26),
GEN_VXFORM(vsumsws, 4, 30),
GEN_VXFORM(vaddfp, 5, 0),
GEN_VXFORM(vsubfp, 5, 1),
GEN_VXFORM(vmaxfp, 5, 16),
GEN_VXFORM(vminfp, 5, 17),

#define GEN_VXRFORM1(opname, name, str, opc2, opc3)                     \
    GEN_HANDLER2(name, str, 0x4, opc2, opc3, 0x00000000, PPC_ALTIVEC),
#define GEN_VXRFORM1_300(opname, name, str, opc2, opc3)                 \
GEN_HANDLER2_E(name, str, 0x4, opc2, opc3, 0x00000000, PPC_NONE, PPC2_ISA300),
#define GEN_VXRFORM(name, opc2, opc3)                                \
    GEN_VXRFORM1(name, name, #name, opc2, opc3)                      \
    GEN_VXRFORM1(name##_dot, name##_, #name ".", opc2, (opc3 | (0x1 << 4)))
#define GEN_VXRFORM_300(name, opc2, opc3)                                   \
    GEN_VXRFORM1_300(name, name, #name, opc2, opc3)                         \
    GEN_VXRFORM1_300(name##_dot, name##_, #name ".", opc2, (opc3 | (0x1 << 4)))

GEN_VXRFORM(vcmpeqfp, 3, 3)
GEN_VXRFORM(vcmpgefp, 3, 7)
GEN_VXRFORM(vcmpgtfp, 3, 11)
GEN_VXRFORM(vcmpbfp, 3, 15)

#define GEN_VXFORM_DUAL_INV(name0, name1, opc2, opc3, inval0, inval1, type) \
GEN_OPCODE_DUAL(name0##_##name1, 0x04, opc2, opc3, inval0, inval1, type, \
                                                               PPC_NONE)
GEN_VXFORM_DUAL_INV(vspltb, vextractub, 6, 8, 0x00000000, 0x100000,
                                               PPC_ALTIVEC),
GEN_VXFORM_DUAL_INV(vsplth, vextractuh, 6, 9, 0x00000000, 0x100000,
                                               PPC_ALTIVEC),
GEN_VXFORM_DUAL_INV(vspltw, vextractuw, 6, 10, 0x00000000, 0x100000,
                                               PPC_ALTIVEC),
GEN_VXFORM_300_EXT(vextractd, 6, 11, 0x100000),
GEN_VXFORM(vspltisb, 6, 12),
GEN_VXFORM(vspltish, 6, 13),
GEN_VXFORM(vspltisw, 6, 14),
GEN_VXFORM_300_EO(vctzb, 0x01, 0x18, 0x1C),
GEN_VXFORM_300_EO(vctzh, 0x01, 0x18, 0x1D),
GEN_VXFORM_300_EO(vctzw, 0x01, 0x18, 0x1E),
GEN_VXFORM_300_EO(vctzd, 0x01, 0x18, 0x1F),
GEN_VXFORM_300_EO(vclzlsbb, 0x01, 0x18, 0x0),
GEN_VXFORM_300_EO(vctzlsbb, 0x01, 0x18, 0x1),

#define GEN_VXFORM_NOA(name, opc2, opc3)                                \
    GEN_HANDLER(name, 0x04, opc2, opc3, 0x001f0000, PPC_ALTIVEC)
GEN_VXFORM_NOA(vupkhsb, 7, 8),
GEN_VXFORM_NOA(vupkhsh, 7, 9),
GEN_VXFORM_207(vupkhsw, 7, 25),
GEN_VXFORM_NOA(vupklsb, 7, 10),
GEN_VXFORM_NOA(vupklsh, 7, 11),
GEN_VXFORM_207(vupklsw, 7, 27),
GEN_VXFORM_NOA(vupkhpx, 7, 13),
GEN_VXFORM_NOA(vupklpx, 7, 15),
GEN_VXFORM_NOA(vrefp, 5, 4),
GEN_VXFORM_NOA(vrsqrtefp, 5, 5),
GEN_VXFORM_NOA(vexptefp, 5, 6),
GEN_VXFORM_NOA(vlogefp, 5, 7),
GEN_VXFORM_NOA(vrfim, 5, 11),
GEN_VXFORM_NOA(vrfin, 5, 8),
GEN_VXFORM_NOA(vrfip, 5, 10),
GEN_VXFORM_NOA(vrfiz, 5, 9),

#define GEN_VXFORM_UIMM(name, opc2, opc3)                               \
    GEN_HANDLER(name, 0x04, opc2, opc3, 0x00000000, PPC_ALTIVEC)
GEN_VXFORM_UIMM(vcfux, 5, 12),
GEN_VXFORM_UIMM(vcfsx, 5, 13),
GEN_VXFORM_UIMM(vctuxs, 5, 14),
GEN_VXFORM_UIMM(vctsxs, 5, 15),

#define GEN_VAFORM_PAIRED(name0, name1, opc2)                           \
    GEN_HANDLER(name0##_##name1, 0x04, opc2, 0xFF, 0x00000000, PPC_ALTIVEC)
GEN_VAFORM_PAIRED(vmaddfp, vnmsubfp, 23),

GEN_VXFORM_DUAL(vclzb, vpopcntb, 1, 28, PPC_NONE, PPC2_ALTIVEC_207),
GEN_VXFORM_DUAL(vclzh, vpopcnth, 1, 29, PPC_NONE, PPC2_ALTIVEC_207),
GEN_VXFORM_DUAL(vclzw, vpopcntw, 1, 30, PPC_NONE, PPC2_ALTIVEC_207),
GEN_VXFORM_DUAL(vclzd, vpopcntd, 1, 31, PPC_NONE, PPC2_ALTIVEC_207),

GEN_VXFORM_300(vbpermd, 6, 23),
GEN_VXFORM_207(vbpermq, 6, 21),
GEN_VXFORM_207(vgbbd, 6, 20),
GEN_VXFORM_207(vpmsumb, 4, 16),
GEN_VXFORM_207(vpmsumh, 4, 17),
GEN_VXFORM_207(vpmsumw, 4, 18),

GEN_VXFORM_207(vsbox, 4, 23),

GEN_VXFORM_DUAL(vcipher, vcipherlast, 4, 20, PPC_NONE, PPC2_ALTIVEC_207),
GEN_VXFORM_DUAL(vncipher, vncipherlast, 4, 21, PPC_NONE, PPC2_ALTIVEC_207),

GEN_VXFORM_207(vshasigmaw, 1, 26),
GEN_VXFORM_207(vshasigmad, 1, 27),

GEN_VXFORM_DUAL(vsldoi, vpermxor, 22, 0xFF, PPC_ALTIVEC, PPC_NONE),
