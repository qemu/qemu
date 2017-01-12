#define GEN_VR_LDX(name, opc2, opc3)                                          \
GEN_HANDLER(name, 0x1F, opc2, opc3, 0x00000001, PPC_ALTIVEC)
#define GEN_VR_STX(name, opc2, opc3)                                          \
GEN_HANDLER(st##name, 0x1F, opc2, opc3, 0x00000001, PPC_ALTIVEC)
#define GEN_VR_LVE(name, opc2, opc3)                                    \
    GEN_HANDLER(lve##name, 0x1F, opc2, opc3, 0x00000001, PPC_ALTIVEC)
#define GEN_VR_STVE(name, opc2, opc3)                                   \
    GEN_HANDLER(stve##name, 0x1F, opc2, opc3, 0x00000001, PPC_ALTIVEC)
GEN_VR_LDX(lvx, 0x07, 0x03),
GEN_VR_LDX(lvxl, 0x07, 0x0B),
GEN_VR_LVE(bx, 0x07, 0x00),
GEN_VR_LVE(hx, 0x07, 0x01),
GEN_VR_LVE(wx, 0x07, 0x02),
GEN_VR_STX(svx, 0x07, 0x07),
GEN_VR_STX(svxl, 0x07, 0x0F),
GEN_VR_STVE(bx, 0x07, 0x04),
GEN_VR_STVE(hx, 0x07, 0x05),
GEN_VR_STVE(wx, 0x07, 0x06),

#define GEN_VX_LOGICAL(name, tcg_op, opc2, opc3)                        \
GEN_HANDLER(name, 0x04, opc2, opc3, 0x00000000, PPC_ALTIVEC)

#define GEN_VX_LOGICAL_207(name, tcg_op, opc2, opc3) \
GEN_HANDLER_E(name, 0x04, opc2, opc3, 0x00000000, PPC_NONE, PPC2_ALTIVEC_207)

GEN_VX_LOGICAL(vand, tcg_gen_and_i64, 2, 16),
GEN_VX_LOGICAL(vandc, tcg_gen_andc_i64, 2, 17),
GEN_VX_LOGICAL(vor, tcg_gen_or_i64, 2, 18),
GEN_VX_LOGICAL(vxor, tcg_gen_xor_i64, 2, 19),
GEN_VX_LOGICAL(vnor, tcg_gen_nor_i64, 2, 20),
GEN_VX_LOGICAL_207(veqv, tcg_gen_eqv_i64, 2, 26),
GEN_VX_LOGICAL_207(vnand, tcg_gen_nand_i64, 2, 22),
GEN_VX_LOGICAL_207(vorc, tcg_gen_orc_i64, 2, 21),

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
GEN_VXFORM(vmaxub, 1, 0),
GEN_VXFORM(vmaxuh, 1, 1),
GEN_VXFORM(vmaxuw, 1, 2),
GEN_VXFORM_207(vmaxud, 1, 3),
GEN_VXFORM(vmaxsb, 1, 4),
GEN_VXFORM(vmaxsh, 1, 5),
GEN_VXFORM(vmaxsw, 1, 6),
GEN_VXFORM_207(vmaxsd, 1, 7),
GEN_VXFORM(vminub, 1, 8),
GEN_VXFORM(vminuh, 1, 9),
GEN_VXFORM(vminuw, 1, 10),
GEN_VXFORM_207(vminud, 1, 11),
GEN_VXFORM(vminsb, 1, 12),
GEN_VXFORM(vminsh, 1, 13),
GEN_VXFORM(vminsw, 1, 14),
GEN_VXFORM_207(vminsd, 1, 15),
GEN_VXFORM_DUAL(vavgub, vabsdub, 1, 16, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM_DUAL(vavguh, vabsduh, 1, 17, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM_DUAL(vavguw, vabsduw, 1, 18, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM(vavgsb, 1, 20),
GEN_VXFORM(vavgsh, 1, 21),
GEN_VXFORM(vavgsw, 1, 22),
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
GEN_VXFORM(vmuloub, 4, 0),
GEN_VXFORM(vmulouh, 4, 1),
GEN_VXFORM_DUAL(vmulouw, vmuluwm, 4, 2, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM(vmulosb, 4, 4),
GEN_VXFORM(vmulosh, 4, 5),
GEN_VXFORM_207(vmulosw, 4, 6),
GEN_VXFORM(vmuleub, 4, 8),
GEN_VXFORM(vmuleuh, 4, 9),
GEN_VXFORM_207(vmuleuw, 4, 10),
GEN_VXFORM(vmulesb, 4, 12),
GEN_VXFORM(vmulesh, 4, 13),
GEN_VXFORM_207(vmulesw, 4, 14),
GEN_VXFORM(vslb, 2, 4),
GEN_VXFORM(vslh, 2, 5),
GEN_VXFORM_DUAL(vslw, vrlwnm, 2, 6, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM_207(vsld, 2, 23),
GEN_VXFORM(vsrb, 2, 8),
GEN_VXFORM(vsrh, 2, 9),
GEN_VXFORM(vsrw, 2, 10),
GEN_VXFORM_207(vsrd, 2, 27),
GEN_VXFORM(vsrab, 2, 12),
GEN_VXFORM(vsrah, 2, 13),
GEN_VXFORM(vsraw, 2, 14),
GEN_VXFORM_207(vsrad, 2, 15),
GEN_VXFORM_300(vsrv, 2, 28),
GEN_VXFORM_300(vslv, 2, 29),
GEN_VXFORM(vslo, 6, 16),
GEN_VXFORM(vsro, 6, 17),
GEN_VXFORM(vaddcuw, 0, 6),
GEN_HANDLER_E_2(vprtybw, 0x4, 0x1, 0x18, 8, 0, PPC_NONE, PPC2_ISA300),
GEN_HANDLER_E_2(vprtybd, 0x4, 0x1, 0x18, 9, 0, PPC_NONE, PPC2_ISA300),
GEN_HANDLER_E_2(vprtybq, 0x4, 0x1, 0x18, 10, 0, PPC_NONE, PPC2_ISA300),

GEN_VXFORM_DUAL(vsubcuw, xpnd04_1, 0, 22, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM_300(bcdsr, 0, 23),
GEN_VXFORM_300(bcdsr, 0, 31),
GEN_VXFORM_DUAL(vaddubs, vmul10uq, 0, 8, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM_DUAL(vadduhs, vmul10euq, 0, 9, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM(vadduws, 0, 10),
GEN_VXFORM(vaddsbs, 0, 12),
GEN_VXFORM_DUAL(vaddshs, bcdcpsgn, 0, 13, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM(vaddsws, 0, 14),
GEN_VXFORM_DUAL(vsububs, bcdadd, 0, 24, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM_DUAL(vsubuhs, bcdsub, 0, 25, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM(vsubuws, 0, 26),
GEN_VXFORM_DUAL(vsubsbs, bcdtrunc, 0, 28, PPC_NONE, PPC2_ISA300),
GEN_VXFORM(vsubshs, 0, 29),
GEN_VXFORM_DUAL(vsubsws, xpnd04_2, 0, 30, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM_207(vadduqm, 0, 4),
GEN_VXFORM_207(vaddcuq, 0, 5),
GEN_VXFORM_DUAL(vaddeuqm, vaddecuq, 30, 0xFF, PPC_NONE, PPC2_ALTIVEC_207),
GEN_VXFORM_DUAL(vsubuqm, bcdtrunc, 0, 20, PPC2_ALTIVEC_207, PPC2_ISA300),
GEN_VXFORM_DUAL(vsubcuq, bcdutrunc, 0, 21, PPC2_ALTIVEC_207, PPC2_ISA300),
GEN_VXFORM_DUAL(vsubeuqm, vsubecuq, 31, 0xFF, PPC_NONE, PPC2_ALTIVEC_207),
GEN_VXFORM(vrlb, 2, 0),
GEN_VXFORM(vrlh, 2, 1),
GEN_VXFORM_DUAL(vrlw, vrlwmi, 2, 2, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM_DUAL(vrld, vrldmi, 2, 3, PPC_NONE, PPC2_ALTIVEC_207),
GEN_VXFORM_DUAL(vsl, vrldnm, 2, 7, PPC_ALTIVEC, PPC_NONE),
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

GEN_VXRFORM_300(vcmpnezb, 3, 4)
GEN_VXRFORM_300(vcmpnezh, 3, 5)
GEN_VXRFORM_300(vcmpnezw, 3, 6)
GEN_VXRFORM(vcmpgtsb, 3, 12)
GEN_VXRFORM(vcmpgtsh, 3, 13)
GEN_VXRFORM(vcmpgtsw, 3, 14)
GEN_VXRFORM(vcmpgtub, 3, 8)
GEN_VXRFORM(vcmpgtuh, 3, 9)
GEN_VXRFORM(vcmpgtuw, 3, 10)
GEN_VXRFORM_DUAL(vcmpeqfp, vcmpequd, 3, 3, PPC_ALTIVEC, PPC_NONE)
GEN_VXRFORM(vcmpgefp, 3, 7)
GEN_VXRFORM_DUAL(vcmpgtfp, vcmpgtud, 3, 11, PPC_ALTIVEC, PPC_NONE)
GEN_VXRFORM_DUAL(vcmpbfp, vcmpgtsd, 3, 15, PPC_ALTIVEC, PPC_NONE)
GEN_VXRFORM_DUAL(vcmpequb, vcmpneb, 3, 0, PPC_ALTIVEC, PPC_NONE)
GEN_VXRFORM_DUAL(vcmpequh, vcmpneh, 3, 1, PPC_ALTIVEC, PPC_NONE)
GEN_VXRFORM_DUAL(vcmpequw, vcmpnew, 3, 2, PPC_ALTIVEC, PPC_NONE)

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
GEN_VXFORM_DUAL_INV(vspltisb, vinsertb, 6, 12, 0x00000000, 0x100000,
                                               PPC_ALTIVEC),
GEN_VXFORM_DUAL_INV(vspltish, vinserth, 6, 13, 0x00000000, 0x100000,
                                               PPC_ALTIVEC),
GEN_VXFORM_DUAL_INV(vspltisw, vinsertw, 6, 14, 0x00000000, 0x100000,
                                               PPC_ALTIVEC),
GEN_VXFORM_300_EXT(vinsertd, 6, 15, 0x100000),
GEN_VXFORM_300_EO(vnegw, 0x01, 0x18, 0x06),
GEN_VXFORM_300_EO(vnegd, 0x01, 0x18, 0x07),
GEN_VXFORM_300_EO(vextsb2w, 0x01, 0x18, 0x10),
GEN_VXFORM_300_EO(vextsh2w, 0x01, 0x18, 0x11),
GEN_VXFORM_300_EO(vextsb2d, 0x01, 0x18, 0x18),
GEN_VXFORM_300_EO(vextsh2d, 0x01, 0x18, 0x19),
GEN_VXFORM_300_EO(vextsw2d, 0x01, 0x18, 0x1A),
GEN_VXFORM_300_EO(vctzb, 0x01, 0x18, 0x1C),
GEN_VXFORM_300_EO(vctzh, 0x01, 0x18, 0x1D),
GEN_VXFORM_300_EO(vctzw, 0x01, 0x18, 0x1E),
GEN_VXFORM_300_EO(vctzd, 0x01, 0x18, 0x1F),
GEN_VXFORM_300_EO(vclzlsbb, 0x01, 0x18, 0x0),
GEN_VXFORM_300_EO(vctzlsbb, 0x01, 0x18, 0x1),
GEN_VXFORM_300(vpermr, 0x1D, 0xFF),

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
GEN_VAFORM_PAIRED(vmhaddshs, vmhraddshs, 16),
GEN_VAFORM_PAIRED(vmsumubm, vmsummbm, 18),
GEN_VAFORM_PAIRED(vmsumuhm, vmsumuhs, 19),
GEN_VAFORM_PAIRED(vmsumshm, vmsumshs, 20),
GEN_VAFORM_PAIRED(vsel, vperm, 21),
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
GEN_VXFORM_207(vpmsumd, 4, 19),

GEN_VXFORM_207(vsbox, 4, 23),

GEN_VXFORM_DUAL(vcipher, vcipherlast, 4, 20, PPC_NONE, PPC2_ALTIVEC_207),
GEN_VXFORM_DUAL(vncipher, vncipherlast, 4, 21, PPC_NONE, PPC2_ALTIVEC_207),

GEN_VXFORM_207(vshasigmaw, 1, 26),
GEN_VXFORM_207(vshasigmad, 1, 27),

GEN_VXFORM_DUAL(vsldoi, vpermxor, 22, 0xFF, PPC_ALTIVEC, PPC_NONE),
