/*
 * QEMU RISC-V Disassembler for xthead.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "disas/riscv.h"
#include "disas/riscv-xthead.h"

#define OP(N, ...) static const rv_opcode_data op_##N = { __VA_ARGS__ };
#include "riscv-xthead-op.c.inc"
#undef OP

const rv_opcode_data *decode_xtheadba(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;

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
                case 0b0000011: return &op_th_addsl;
                }
                break;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    return NULL;
}

const rv_opcode_data *decode_xtheadbb(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 1:
                switch ((inst >> 25) & 0b1111111) {
                case 0b0001010: return &op_th_srriw;
                case 0b1000000:
                    if (((inst >> 20) & 0b11111) == 0) {
                        return &op_th_tstnbz;
                    }
                    break;
                case 0b1000001:
                    if (((inst >> 20) & 0b11111) == 0) {
                        return &op_th_rev;
                    }
                    break;
                case 0b1000010:
                    if (((inst >> 20) & 0b11111) == 0) {
                        return &op_th_ff0;
                    }
                    break;
                case 0b1000011:
                    if (((inst >> 20) & 0b11111) == 0) {
                        return &op_th_ff1;
                    }
                    break;
                case 0b1000100:
                case 0b1001000:
                    if (((inst >> 20) & 0b11111) == 0) {
                        return &op_th_revw;
                    }
                    break;
                case 0b0001000:
                case 0b0001001:
                    return &op_th_srri;
                    break;
                }
                break;
            case 2: return &op_th_ext;
            case 3: return &op_th_extu;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    return NULL;
}

const rv_opcode_data *decode_xtheadbs(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 1:
                switch ((inst >> 26) & 0b111111) {
                case 0b100010: return &op_th_tst;
                }
                break;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    return NULL;
}

const rv_opcode_data *decode_xtheadcmo(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;

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
                        return &op_th_dcache_call;
                    }
                    break;
                case 0b000000000011:
                    if (((inst >> 20) & 0b11111) == 0) {
                        return &op_th_dcache_ciall;
                    }
                    break;
                case 0b000000000010:
                    if (((inst >> 20) & 0b11111) == 0) {
                        return &op_th_dcache_iall;
                    }
                    break;
                case 0b000000101001: return &op_th_dcache_cpa;
                case 0b000000101011: return &op_th_dcache_cipa;
                case 0b000000101010: return &op_th_dcache_ipa;
                case 0b000000100101: return &op_th_dcache_cva;
                case 0b000000100111: return &op_th_dcache_civa;
                case 0b000000100110: return &op_th_dcache_iva;
                case 0b000000100001: return &op_th_dcache_csw;
                case 0b000000100011: return &op_th_dcache_cisw;
                case 0b000000100010: return &op_th_dcache_isw;
                case 0b000000101000: return &op_th_dcache_cpal1;
                case 0b000000100100: return &op_th_dcache_cval1;
                case 0b000000010000:
                    if (((inst >> 20) & 0b11111) == 0) {
                        return &op_th_icache_iall;
                    }
                    break;
                case 0b000000010001:
                    if (((inst >> 20) & 0b11111) == 0) {
                        return &op_th_icache_ialls;
                    }
                    break;
                case 0b000000111000: return &op_th_icache_ipa;
                case 0b000000110000: return &op_th_icache_iva;
                case 0b000000010101:
                    if (((inst >> 20) & 0b11111) == 0) {
                        return &op_th_l2cache_call;
                    }
                    break;
                case 0b000000010111:
                    if (((inst >> 20) & 0b11111) == 0) {
                        return &op_th_l2cache_ciall;
                    }
                    break;
                case 0b000000010110:
                    if (((inst >> 20) & 0b11111) == 0) {
                        return &op_th_l2cache_iall;
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

    return NULL;
}

const rv_opcode_data *decode_xtheadcondmov(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 1:
                switch ((inst >> 25) & 0b1111111) {
                case 0b0100000: return &op_th_mveqz;
                case 0b0100001: return &op_th_mvnez;
                }
                break;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    return NULL;
}

const rv_opcode_data *decode_xtheadfmemidx(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 6:
                switch ((inst >> 27) & 0b11111) {
                case 8: return &op_th_flrw;
                case 10: return &op_th_flurw;
                case 12: return &op_th_flrd;
                case 14: return &op_th_flurd;
                }
                break;
            case 7:
                switch ((inst >> 27) & 0b11111) {
                case 8: return &op_th_fsrw;
                case 10: return &op_th_fsurw;
                case 12: return &op_th_fsrd;
                case 14: return &op_th_fsurd;
                }
                break;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    return NULL;
}

const rv_opcode_data *decode_xtheadfmv(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;

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
                        return &op_th_fmv_hw_x;
                    }
                    break;
                case 0b1100000:
                    if (((inst >> 20) & 0b11111) == 0) {
                        return &op_th_fmv_x_hw;
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

    return NULL;
}

const rv_opcode_data *decode_xtheadmac(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 1:
                switch ((inst >> 25) & 0b1111111) {
                case 0b0010000: return &op_th_mula;
                case 0b0010001: return &op_th_muls;
                case 0b0010010: return &op_th_mulaw;
                case 0b0010011: return &op_th_mulsw;
                case 0b0010100: return &op_th_mulah;
                case 0b0010101: return &op_th_mulsh;
                }
                break;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    return NULL;
}

const rv_opcode_data *decode_xtheadmemidx(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 4:
                switch ((inst >> 27) & 0b11111) {
                case 0: return &op_th_lrb;
                case 1: return &op_th_lbib;
                case 2: return &op_th_lurb;
                case 3: return &op_th_lbia;
                case 4: return &op_th_lrh;
                case 5: return &op_th_lhib;
                case 6: return &op_th_lurh;
                case 7: return &op_th_lhia;
                case 8: return &op_th_lrw;
                case 9: return &op_th_lwib;
                case 10: return &op_th_lurw;
                case 11: return &op_th_lwia;
                case 12: return &op_th_lrd;
                case 13: return &op_th_ldib;
                case 14: return &op_th_lurd;
                case 15: return &op_th_ldia;
                case 16: return &op_th_lrbu;
                case 17: return &op_th_lbuib;
                case 18: return &op_th_lurbu;
                case 19: return &op_th_lbuia;
                case 20: return &op_th_lrhu;
                case 21: return &op_th_lhuib;
                case 22: return &op_th_lurhu;
                case 23: return &op_th_lhuia;
                case 24: return &op_th_lrwu;
                case 25: return &op_th_lwuib;
                case 26: return &op_th_lurwu;
                case 27: return &op_th_lwuia;
                }
                break;
            case 5:
                switch ((inst >> 27) & 0b11111) {
                case 0: return &op_th_srb;
                case 1: return &op_th_sbib;
                case 2: return &op_th_surb;
                case 3: return &op_th_sbia;
                case 4: return &op_th_srh;
                case 5: return &op_th_shib;
                case 6: return &op_th_surh;
                case 7: return &op_th_shia;
                case 8: return &op_th_srw;
                case 9: return &op_th_swib;
                case 10: return &op_th_surw;
                case 11: return &op_th_swia;
                case 12: return &op_th_srd;
                case 13: return &op_th_sdib;
                case 14: return &op_th_surd;
                case 15: return &op_th_sdia;
                }
                break;
                break;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    return NULL;
}

const rv_opcode_data *decode_xtheadmempair(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 4:
                switch ((inst >> 27) & 0b11111) {
                case 28: return &op_th_lwd;
                case 30: return &op_th_lwud;
                case 31: return &op_th_ldd;
                }
                break;
            case 5:
                switch ((inst >> 27) & 0b11111) {
                case 28: return &op_th_swd;
                case 31: return &op_th_sdd;
                }
                break;
            }
            break;
            /* custom-0 */
        }
        break;
    }

    return NULL;
}

const rv_opcode_data *decode_xtheadsync(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;

    switch (((inst >> 0) & 0b11)) {
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 2:
            /* custom-0 */
            switch ((inst >> 12) & 0b111) {
            case 0:
                switch ((inst >> 25) & 0b1111111) {
                case 0b0000010: return &op_th_sfence_vmas;
                case 0b0000000:
                    switch ((inst >> 20) & 0b11111) {
                    case 0b11000: return &op_th_sync;
                    case 0b11010: return &op_th_sync_i;
                    case 0b11011: return &op_th_sync_is;
                    case 0b11001: return &op_th_sync_s;
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

    return NULL;
}
