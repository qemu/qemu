/*
 * QEMU RISC-V Disassembler for xthead.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "disas/riscv.h"
#include "disas/riscv-xthead.h"

typedef enum {
    /* 0 is reserved for rv_op_illegal. */
    /* XTheadBa */
    rv_op_th_addsl = 1,
    /* XTheadBb */
    rv_op_th_srri,
    rv_op_th_srriw,
    rv_op_th_ext,
    rv_op_th_extu,
    rv_op_th_ff0,
    rv_op_th_ff1,
    rv_op_th_rev,
    rv_op_th_revw,
    rv_op_th_tstnbz,
    /* XTheadBs */
    rv_op_th_tst,
    /* XTheadCmo */
    rv_op_th_dcache_call,
    rv_op_th_dcache_ciall,
    rv_op_th_dcache_iall,
    rv_op_th_dcache_cpa,
    rv_op_th_dcache_cipa,
    rv_op_th_dcache_ipa,
    rv_op_th_dcache_cva,
    rv_op_th_dcache_civa,
    rv_op_th_dcache_iva,
    rv_op_th_dcache_csw,
    rv_op_th_dcache_cisw,
    rv_op_th_dcache_isw,
    rv_op_th_dcache_cpal1,
    rv_op_th_dcache_cval1,
    rv_op_th_icache_iall,
    rv_op_th_icache_ialls,
    rv_op_th_icache_ipa,
    rv_op_th_icache_iva,
    rv_op_th_l2cache_call,
    rv_op_th_l2cache_ciall,
    rv_op_th_l2cache_iall,
    /* XTheadCondMov */
    rv_op_th_mveqz,
    rv_op_th_mvnez,
    /* XTheadFMemIdx */
    rv_op_th_flrd,
    rv_op_th_flrw,
    rv_op_th_flurd,
    rv_op_th_flurw,
    rv_op_th_fsrd,
    rv_op_th_fsrw,
    rv_op_th_fsurd,
    rv_op_th_fsurw,
    /* XTheadFmv */
    rv_op_th_fmv_hw_x,
    rv_op_th_fmv_x_hw,
    /* XTheadMac */
    rv_op_th_mula,
    rv_op_th_mulah,
    rv_op_th_mulaw,
    rv_op_th_muls,
    rv_op_th_mulsw,
    rv_op_th_mulsh,
    /* XTheadMemIdx */
    rv_op_th_lbia,
    rv_op_th_lbib,
    rv_op_th_lbuia,
    rv_op_th_lbuib,
    rv_op_th_lhia,
    rv_op_th_lhib,
    rv_op_th_lhuia,
    rv_op_th_lhuib,
    rv_op_th_lwia,
    rv_op_th_lwib,
    rv_op_th_lwuia,
    rv_op_th_lwuib,
    rv_op_th_ldia,
    rv_op_th_ldib,
    rv_op_th_sbia,
    rv_op_th_sbib,
    rv_op_th_shia,
    rv_op_th_shib,
    rv_op_th_swia,
    rv_op_th_swib,
    rv_op_th_sdia,
    rv_op_th_sdib,
    rv_op_th_lrb,
    rv_op_th_lrbu,
    rv_op_th_lrh,
    rv_op_th_lrhu,
    rv_op_th_lrw,
    rv_op_th_lrwu,
    rv_op_th_lrd,
    rv_op_th_srb,
    rv_op_th_srh,
    rv_op_th_srw,
    rv_op_th_srd,
    rv_op_th_lurb,
    rv_op_th_lurbu,
    rv_op_th_lurh,
    rv_op_th_lurhu,
    rv_op_th_lurw,
    rv_op_th_lurwu,
    rv_op_th_lurd,
    rv_op_th_surb,
    rv_op_th_surh,
    rv_op_th_surw,
    rv_op_th_surd,
    /* XTheadMemPair */
    rv_op_th_ldd,
    rv_op_th_lwd,
    rv_op_th_lwud,
    rv_op_th_sdd,
    rv_op_th_swd,
    /* XTheadSync */
    rv_op_th_sfence_vmas,
    rv_op_th_sync,
    rv_op_th_sync_i,
    rv_op_th_sync_is,
    rv_op_th_sync_s,
} rv_xthead_op;

const rv_opcode_data xthead_opcode_data[] = {
    { "th.illegal", rv_codec_illegal, rv_fmt_none, NULL, 0, 0, 0 },
    /* XTheadBa */
    { "th.addsl", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    /* XTheadBb */
    { "th.srri", rv_codec_r2_imm6, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "th.srriw", rv_codec_r2_imm5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "th.ext", rv_codec_r2_immhl, rv_fmt_rd_rs1_immh_imml, NULL, 0, 0, 0 },
    { "th.extu", rv_codec_r2_immhl, rv_fmt_rd_rs1_immh_imml, NULL, 0, 0, 0 },
    { "th.ff0", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "th.ff1", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "th.rev", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "th.revw", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "th.tstnbz", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    /* XTheadBs */
    { "th.tst", rv_codec_r2_imm6, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    /* XTheadCmo */
    { "th.dcache.call", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "th.dcache.ciall", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "th.dcache.iall", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "th.dcache.cpa", rv_codec_r, rv_fmt_rs1, NULL, 0, 0, 0 },
    { "th.dcache.cipa", rv_codec_r, rv_fmt_rs1, NULL, 0, 0, 0 },
    { "th.dcache.ipa", rv_codec_r, rv_fmt_rs1, NULL, 0, 0, 0 },
    { "th.dcache.cva", rv_codec_r, rv_fmt_rs1, NULL, 0, 0, 0 },
    { "th.dcache.civa", rv_codec_r, rv_fmt_rs1, NULL, 0, 0, 0 },
    { "th.dcache.iva", rv_codec_r, rv_fmt_rs1, NULL, 0, 0, 0 },
    { "th.dcache.csw", rv_codec_r, rv_fmt_rs1, NULL, 0, 0, 0 },
    { "th.dcache.cisw", rv_codec_r, rv_fmt_rs1, NULL, 0, 0, 0 },
    { "th.dcache.isw", rv_codec_r, rv_fmt_rs1, NULL, 0, 0, 0 },
    { "th.dcache.cpal1", rv_codec_r, rv_fmt_rs1, NULL, 0, 0, 0 },
    { "th.dcache.cval1", rv_codec_r, rv_fmt_rs1, NULL, 0, 0, 0 },
    { "th.icache.iall", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "th.icache.ialls", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "th.icache.ipa", rv_codec_r, rv_fmt_rs1, NULL, 0, 0, 0 },
    { "th.icache.iva", rv_codec_r, rv_fmt_rs1, NULL, 0, 0, 0 },
    { "th.l2cache.call", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "th.l2cache.ciall", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "th.l2cache.iall", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    /* XTheadCondMov */
    { "th.mveqz", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "th.mvnez", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    /* XTheadFMemIdx */
    { "th.flrd", rv_codec_r_imm2, rv_fmt_frd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.flrw", rv_codec_r_imm2, rv_fmt_frd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.flurd", rv_codec_r_imm2, rv_fmt_frd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.flurw", rv_codec_r_imm2, rv_fmt_frd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.fsrd", rv_codec_r_imm2, rv_fmt_frd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.fsrw", rv_codec_r_imm2, rv_fmt_frd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.fsurd", rv_codec_r_imm2, rv_fmt_frd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.fsurw", rv_codec_r_imm2, rv_fmt_frd_rs1_rs2_imm, NULL, 0, 0, 0 },
    /* XTheadFmv */
    { "th.fmv.hw.x", rv_codec_r, rv_fmt_rd_frs1, NULL, 0, 0, 0 },
    { "th.fmv.x.hw", rv_codec_r, rv_fmt_rd_frs1, NULL, 0, 0, 0 },
    /* XTheadMac */
    { "th.mula", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "th.mulaw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "th.mulah", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "th.muls", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "th.mulsw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "th.mulsh", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    /* XTheadMemIdx */
    { "th.lbia", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.lbib", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml, NULL, 0, 0, 0 },
    { "th.lbuia", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.lbuib", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.lhia", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.lhib", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.lhuia", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.lhuib", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.lwia", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.lwib", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.lwuia", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.lwuib", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.ldia", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.ldib", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.sbia", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.sbib", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.shia", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.shib", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.swia", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.swib", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.sdia", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.sdib", rv_codec_r2_imm2_imm5, rv_fmt_rd_rs1_immh_imml_addr, NULL, 0, 0, 0 },
    { "th.lrb", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.lrbu", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.lrh", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.lrhu", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.lrw", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.lrwu", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.lrd", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.srb", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.srh", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.srw", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.srd", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.lurb", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.lurbu", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.lurh", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.lurhu", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.lurw", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.lurwu", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.lurd", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.surb", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.surh", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.surw", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    { "th.surd", rv_codec_r_imm2, rv_fmt_rd_rs1_rs2_imm, NULL, 0, 0, 0 },
    /* XTheadMemPair */
    { "th.ldd", rv_codec_r_imm2, rv_fmt_rd2_imm, NULL, 0, 0, 0 },
    { "th.lwd", rv_codec_r_imm2, rv_fmt_rd2_imm, NULL, 0, 0, 0 },
    { "th.lwud", rv_codec_r_imm2, rv_fmt_rd2_imm, NULL, 0, 0, 0 },
    { "th.sdd", rv_codec_r_imm2, rv_fmt_rd2_imm, NULL, 0, 0, 0 },
    { "th.swd", rv_codec_r_imm2, rv_fmt_rd2_imm, NULL, 0, 0, 0 },
    /* XTheadSync */
    { "th.sfence.vmas", rv_codec_r, rv_fmt_rs1_rs2, NULL, 0, 0, 0 },
    { "th.sync", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "th.sync.i", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "th.sync.is", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "th.sync.s", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
};

void decode_xtheadba(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;
    rv_opcode op = rv_op_illegal;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 1:
                switch ((inst >> 25) & 0b1111111) {
                case 0b0000000:
                case 0b0000001:
                case 0b0000010:
                case 0b0000011: op = rv_op_th_addsl; break;
                }
                break;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    dec->op = op;
}

void decode_xtheadbb(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;
    rv_opcode op = rv_op_illegal;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 1:
                switch ((inst >> 25) & 0b1111111) {
                case 0b0001010: op = rv_op_th_srriw; break;
                case 0b1000000:
                    if (((inst >> 20) & 0b11111) == 0) {
                        op = rv_op_th_tstnbz;
                    }
                    break;
                case 0b1000001:
                    if (((inst >> 20) & 0b11111) == 0) {
                        op = rv_op_th_rev;
                    }
                    break;
                case 0b1000010:
                    if (((inst >> 20) & 0b11111) == 0) {
                        op = rv_op_th_ff0;
                    }
                    break;
                case 0b1000011:
                    if (((inst >> 20) & 0b11111) == 0) {
                        op = rv_op_th_ff1;
                    }
                    break;
                case 0b1000100:
                case 0b1001000:
                    if (((inst >> 20) & 0b11111) == 0) {
                        op = rv_op_th_revw;
                    }
                    break;
                case 0b0000100:
                case 0b0000101: op = rv_op_th_srri; break;
                }
                break;
            case 2: op = rv_op_th_ext; break;
            case 3: op = rv_op_th_extu; break;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    dec->op = op;
}

void decode_xtheadbs(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;
    rv_opcode op = rv_op_illegal;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 1:
                switch ((inst >> 26) & 0b111111) {
                case 0b100010: op = rv_op_th_tst; break;
                }
                break;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    dec->op = op;
}

void decode_xtheadcmo(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;
    rv_opcode op = rv_op_illegal;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 0:
                switch ((inst >> 20 & 0b111111111111)) {
                case 0b000000000001:
                    if (((inst >> 20) & 0b11111) == 0) {
                        op = rv_op_th_dcache_call;
                    }
                    break;
                case 0b000000000011:
                    if (((inst >> 20) & 0b11111) == 0) {
                        op = rv_op_th_dcache_ciall;
                    }
                    break;
                case 0b000000000010:
                    if (((inst >> 20) & 0b11111) == 0) {
                        op = rv_op_th_dcache_iall;
                    }
                    break;
                case 0b000000101001: op = rv_op_th_dcache_cpa; break;
                case 0b000000101011: op = rv_op_th_dcache_cipa; break;
                case 0b000000101010: op = rv_op_th_dcache_ipa; break;
                case 0b000000100101: op = rv_op_th_dcache_cva; break;
                case 0b000000100111: op = rv_op_th_dcache_civa; break;
                case 0b000000100110: op = rv_op_th_dcache_iva; break;
                case 0b000000100001: op = rv_op_th_dcache_csw; break;
                case 0b000000100011: op = rv_op_th_dcache_cisw; break;
                case 0b000000100010: op = rv_op_th_dcache_isw; break;
                case 0b000000101000: op = rv_op_th_dcache_cpal1; break;
                case 0b000000100100: op = rv_op_th_dcache_cval1; break;
                case 0b000000010000:
                    if (((inst >> 20) & 0b11111) == 0) {
                        op = rv_op_th_icache_iall;
                    }
                    break;
                case 0b000000010001:
                    if (((inst >> 20) & 0b11111) == 0) {
                        op = rv_op_th_icache_ialls;
                    }
                    break;
                case 0b000000111000: op = rv_op_th_icache_ipa; break;
                case 0b000000110000: op = rv_op_th_icache_iva; break;
                case 0b000000010101:
                    if (((inst >> 20) & 0b11111) == 0) {
                        op = rv_op_th_l2cache_call;
                    }
                    break;
                case 0b000000010111:
                    if (((inst >> 20) & 0b11111) == 0) {
                        op = rv_op_th_l2cache_ciall;
                    }
                    break;
                case 0b000000010110:
                    if (((inst >> 20) & 0b11111) == 0) {
                        op = rv_op_th_l2cache_iall;
                    }
                    break;
                }
                break;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    dec->op = op;
}

void decode_xtheadcondmov(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;
    rv_opcode op = rv_op_illegal;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 1:
                switch ((inst >> 25) & 0b1111111) {
                case 0b0100000: op = rv_op_th_mveqz; break;
                case 0b0100001: op = rv_op_th_mvnez; break;
                }
                break;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    dec->op = op;
}

void decode_xtheadfmemidx(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;
    rv_opcode op = rv_op_illegal;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 6:
                switch ((inst >> 27) & 0b11111) {
                case 8: op = rv_op_th_flrw; break;
                case 10: op = rv_op_th_flurw; break;
                case 12: op = rv_op_th_flrd; break;
                case 14: op = rv_op_th_flurd; break;
                }
                break;
            case 7:
                switch ((inst >> 27) & 0b11111) {
                case 8: op = rv_op_th_fsrw; break;
                case 10: op = rv_op_th_fsurw; break;
                case 12: op = rv_op_th_fsrd; break;
                case 14: op = rv_op_th_fsurd; break;
                }
                break;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    dec->op = op;
}

void decode_xtheadfmv(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;
    rv_opcode op = rv_op_illegal;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 1:
                switch ((inst >> 25) & 0b1111111) {
                case 0b1010000:
                    if (((inst >> 20) & 0b11111) == 0) {
                        op = rv_op_th_fmv_hw_x;
                    }
                    break;
                case 0b1100000:
                    if (((inst >> 20) & 0b11111) == 0) {
                        op = rv_op_th_fmv_x_hw;
                    }
                    break;
                }
                break;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    dec->op = op;
}

void decode_xtheadmac(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;
    rv_opcode op = rv_op_illegal;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 1:
                switch ((inst >> 25) & 0b1111111) {
                case 0b0010000: op = rv_op_th_mula; break;
                case 0b0010001: op = rv_op_th_muls; break;
                case 0b0010010: op = rv_op_th_mulaw; break;
                case 0b0010011: op = rv_op_th_mulsw; break;
                case 0b0010100: op = rv_op_th_mulah; break;
                case 0b0010101: op = rv_op_th_mulsh; break;
                }
                break;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    dec->op = op;
}

void decode_xtheadmemidx(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;
    rv_opcode op = rv_op_illegal;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 4:
                switch ((inst >> 27) & 0b11111) {
                case 0: op = rv_op_th_lrb; break;
                case 1: op = rv_op_th_lbib; break;
                case 2: op = rv_op_th_lurb; break;
                case 3: op = rv_op_th_lbia; break;
                case 4: op = rv_op_th_lrh; break;
                case 5: op = rv_op_th_lhib; break;
                case 6: op = rv_op_th_lurh; break;
                case 7: op = rv_op_th_lhia; break;
                case 8: op = rv_op_th_lrw; break;
                case 9: op = rv_op_th_lwib; break;
                case 10: op = rv_op_th_lurw; break;
                case 11: op = rv_op_th_lwia; break;
                case 12: op = rv_op_th_lrd; break;
                case 13: op = rv_op_th_ldib; break;
                case 14: op = rv_op_th_lurd; break;
                case 15: op = rv_op_th_ldia; break;
                case 16: op = rv_op_th_lrbu; break;
                case 17: op = rv_op_th_lbuib; break;
                case 18: op = rv_op_th_lurbu; break;
                case 19: op = rv_op_th_lbuia; break;
                case 20: op = rv_op_th_lrhu; break;
                case 21: op = rv_op_th_lhuib; break;
                case 22: op = rv_op_th_lurhu; break;
                case 23: op = rv_op_th_lhuia; break;
                case 24: op = rv_op_th_lrwu; break;
                case 25: op = rv_op_th_lwuib; break;
                case 26: op = rv_op_th_lurwu; break;
                case 27: op = rv_op_th_lwuia; break;
                }
                break;
            case 5:
                switch ((inst >> 27) & 0b11111) {
                case 0: op = rv_op_th_srb; break;
                case 1: op = rv_op_th_sbib; break;
                case 2: op = rv_op_th_surb; break;
                case 3: op = rv_op_th_sbia; break;
                case 4: op = rv_op_th_srh; break;
                case 5: op = rv_op_th_shib; break;
                case 6: op = rv_op_th_surh; break;
                case 7: op = rv_op_th_shia; break;
                case 8: op = rv_op_th_srw; break;
                case 9: op = rv_op_th_swib; break;
                case 10: op = rv_op_th_surw; break;
                case 11: op = rv_op_th_swia; break;
                case 12: op = rv_op_th_srd; break;
                case 13: op = rv_op_th_sdib; break;
                case 14: op = rv_op_th_surd; break;
                case 15: op = rv_op_th_sdia; break;
                }
                break;
                break;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    dec->op = op;
}

void decode_xtheadmempair(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;
    rv_opcode op = rv_op_illegal;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 4:
                switch ((inst >> 27) & 0b11111) {
                case 28: op = rv_op_th_lwd; break;
                case 30: op = rv_op_th_lwud; break;
                case 31: op = rv_op_th_ldd; break;
                }
                break;
            case 5:
                switch ((inst >> 27) & 0b11111) {
                case 28: op = rv_op_th_swd; break;
                case 31: op = rv_op_th_sdd; break;
                }
                break;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    dec->op = op;
}

void decode_xtheadsync(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;
    rv_opcode op = rv_op_illegal;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 0:
                switch ((inst >> 25) & 0b1111111) {
                case 0b0000010: op = rv_op_th_sfence_vmas; break;
                case 0b0000000:
                    switch ((inst >> 20) & 0b11111) {
                    case 0b11000: op = rv_op_th_sync; break;
                    case 0b11010: op = rv_op_th_sync_i; break;
                    case 0b11011: op = rv_op_th_sync_is; break;
                    case 0b11001: op = rv_op_th_sync_s; break;
                    }
                    break;
                }
                break;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    dec->op = op;
}
