/*
 *  PowerPC emulation for qemu: read/write callbacks for SPRs
 *
 *  Copyright (C) 2021 Instituto de Pesquisas Eldorado
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef SPR_COMMON_H
#define SPR_COMMON_H

#define SPR_NOACCESS (&spr_noaccess)

#ifdef CONFIG_TCG
# define USR_ARG(X)    X,
# ifdef CONFIG_USER_ONLY
#  define SYS_ARG(X)
# else
#  define SYS_ARG(X)   X,
# endif
#else
# define USR_ARG(X)
# define SYS_ARG(X)
#endif
#ifdef CONFIG_KVM
# define KVM_ARG(X)    X,
#else
# define KVM_ARG(X)
#endif

typedef void spr_callback(DisasContext *, int, int);

void _spr_register(CPUPPCState *env, int num, const char *name,
                   USR_ARG(spr_callback *uea_read)
                   USR_ARG(spr_callback *uea_write)
                   SYS_ARG(spr_callback *oea_read)
                   SYS_ARG(spr_callback *oea_write)
                   SYS_ARG(spr_callback *hea_read)
                   SYS_ARG(spr_callback *hea_write)
                   KVM_ARG(uint64_t one_reg_id)
                   target_ulong initial_value);

/* spr_register_kvm_hv passes all required arguments. */
#define spr_register_kvm_hv(env, num, name, uea_read, uea_write,             \
                            oea_read, oea_write, hea_read, hea_write,        \
                            one_reg_id, initial_value)                       \
    _spr_register(env, num, name,                                            \
                  USR_ARG(uea_read) USR_ARG(uea_write)                       \
                  SYS_ARG(oea_read) SYS_ARG(oea_write)                       \
                  SYS_ARG(hea_read) SYS_ARG(hea_write)                       \
                  KVM_ARG(one_reg_id) initial_value)

/* spr_register_kvm duplicates the oea callbacks to the hea callbacks. */
#define spr_register_kvm(env, num, name, uea_read, uea_write,                \
                         oea_read, oea_write, one_reg_id, ival)              \
    spr_register_kvm_hv(env, num, name, uea_read, uea_write, oea_read,       \
                        oea_write, oea_read, oea_write, one_reg_id, ival)

/* spr_register_hv and spr_register are similar, except there is no kvm id. */
#define spr_register_hv(env, num, name, uea_read, uea_write,                 \
                        oea_read, oea_write, hea_read, hea_write, ival)      \
    spr_register_kvm_hv(env, num, name, uea_read, uea_write, oea_read,       \
                        oea_write, hea_read, hea_write, 0, ival)

#define spr_register(env, num, name, uea_read, uea_write,                    \
                     oea_read, oea_write, ival)                              \
    spr_register_kvm(env, num, name, uea_read, uea_write,                    \
                     oea_read, oea_write, 0, ival)

/* prototypes for readers and writers for SPRs */
void spr_noaccess(DisasContext *ctx, int gprn, int sprn);
void spr_read_generic(DisasContext *ctx, int gprn, int sprn);
void spr_write_generic(DisasContext *ctx, int sprn, int gprn);
void spr_write_MMCR0(DisasContext *ctx, int sprn, int gprn);
void spr_write_MMCR1(DisasContext *ctx, int sprn, int gprn);
void spr_write_PMC(DisasContext *ctx, int sprn, int gprn);
void spr_write_CTRL(DisasContext *ctx, int sprn, int gprn);
void spr_read_xer(DisasContext *ctx, int gprn, int sprn);
void spr_write_xer(DisasContext *ctx, int sprn, int gprn);
void spr_read_lr(DisasContext *ctx, int gprn, int sprn);
void spr_write_lr(DisasContext *ctx, int sprn, int gprn);
void spr_read_ctr(DisasContext *ctx, int gprn, int sprn);
void spr_write_ctr(DisasContext *ctx, int sprn, int gprn);
void spr_read_ureg(DisasContext *ctx, int gprn, int sprn);
void spr_read_MMCR0_ureg(DisasContext *ctx, int gprn, int sprn);
void spr_read_MMCR2_ureg(DisasContext *ctx, int gprn, int sprn);
void spr_read_PMC(DisasContext *ctx, int gprn, int sprn);
void spr_read_PMC14_ureg(DisasContext *ctx, int gprn, int sprn);
void spr_read_PMC56_ureg(DisasContext *ctx, int gprn, int sprn);
void spr_read_tbl(DisasContext *ctx, int gprn, int sprn);
void spr_read_tbu(DisasContext *ctx, int gprn, int sprn);
void spr_read_atbl(DisasContext *ctx, int gprn, int sprn);
void spr_read_atbu(DisasContext *ctx, int gprn, int sprn);
void spr_read_spefscr(DisasContext *ctx, int gprn, int sprn);
void spr_write_spefscr(DisasContext *ctx, int sprn, int gprn);
void spr_write_MMCR0_ureg(DisasContext *ctx, int sprn, int gprn);
void spr_write_MMCR2_ureg(DisasContext *ctx, int sprn, int gprn);
void spr_write_PMC14_ureg(DisasContext *ctx, int sprn, int gprn);
void spr_write_PMC56_ureg(DisasContext *ctx, int sprn, int gprn);

#ifndef CONFIG_USER_ONLY
void spr_write_generic32(DisasContext *ctx, int sprn, int gprn);
void spr_write_clear(DisasContext *ctx, int sprn, int gprn);
void spr_access_nop(DisasContext *ctx, int sprn, int gprn);
void spr_read_decr(DisasContext *ctx, int gprn, int sprn);
void spr_write_decr(DisasContext *ctx, int sprn, int gprn);
void spr_write_tbl(DisasContext *ctx, int sprn, int gprn);
void spr_write_tbu(DisasContext *ctx, int sprn, int gprn);
void spr_write_atbl(DisasContext *ctx, int sprn, int gprn);
void spr_write_atbu(DisasContext *ctx, int sprn, int gprn);
void spr_read_ibat(DisasContext *ctx, int gprn, int sprn);
void spr_read_ibat_h(DisasContext *ctx, int gprn, int sprn);
void spr_write_ibatu(DisasContext *ctx, int sprn, int gprn);
void spr_write_ibatu_h(DisasContext *ctx, int sprn, int gprn);
void spr_write_ibatl(DisasContext *ctx, int sprn, int gprn);
void spr_write_ibatl_h(DisasContext *ctx, int sprn, int gprn);
void spr_read_dbat(DisasContext *ctx, int gprn, int sprn);
void spr_read_dbat_h(DisasContext *ctx, int gprn, int sprn);
void spr_write_dbatu(DisasContext *ctx, int sprn, int gprn);
void spr_write_dbatu_h(DisasContext *ctx, int sprn, int gprn);
void spr_write_dbatl(DisasContext *ctx, int sprn, int gprn);
void spr_write_dbatl_h(DisasContext *ctx, int sprn, int gprn);
void spr_write_sdr1(DisasContext *ctx, int sprn, int gprn);
void spr_read_40x_pit(DisasContext *ctx, int gprn, int sprn);
void spr_write_40x_pit(DisasContext *ctx, int sprn, int gprn);
void spr_write_40x_dbcr0(DisasContext *ctx, int sprn, int gprn);
void spr_write_40x_sler(DisasContext *ctx, int sprn, int gprn);
void spr_write_40x_tcr(DisasContext *ctx, int sprn, int gprn);
void spr_write_40x_tsr(DisasContext *ctx, int sprn, int gprn);
void spr_write_40x_pid(DisasContext *ctx, int sprn, int gprn);
void spr_write_booke_tcr(DisasContext *ctx, int sprn, int gprn);
void spr_write_booke_tsr(DisasContext *ctx, int sprn, int gprn);
void spr_read_403_pbr(DisasContext *ctx, int gprn, int sprn);
void spr_write_403_pbr(DisasContext *ctx, int sprn, int gprn);
void spr_write_pir(DisasContext *ctx, int sprn, int gprn);
void spr_write_excp_prefix(DisasContext *ctx, int sprn, int gprn);
void spr_write_excp_vector(DisasContext *ctx, int sprn, int gprn);
void spr_read_thrm(DisasContext *ctx, int gprn, int sprn);
void spr_write_e500_l1csr0(DisasContext *ctx, int sprn, int gprn);
void spr_write_e500_l1csr1(DisasContext *ctx, int sprn, int gprn);
void spr_write_e500_l2csr0(DisasContext *ctx, int sprn, int gprn);
void spr_write_booke206_mmucsr0(DisasContext *ctx, int sprn, int gprn);
void spr_write_booke_pid(DisasContext *ctx, int sprn, int gprn);
void spr_write_eplc(DisasContext *ctx, int sprn, int gprn);
void spr_write_epsc(DisasContext *ctx, int sprn, int gprn);
void spr_write_mas73(DisasContext *ctx, int sprn, int gprn);
void spr_read_mas73(DisasContext *ctx, int gprn, int sprn);
#ifdef TARGET_PPC64
void spr_read_cfar(DisasContext *ctx, int gprn, int sprn);
void spr_write_cfar(DisasContext *ctx, int sprn, int gprn);
void spr_write_ureg(DisasContext *ctx, int sprn, int gprn);
void spr_read_purr(DisasContext *ctx, int gprn, int sprn);
void spr_write_purr(DisasContext *ctx, int sprn, int gprn);
void spr_read_hdecr(DisasContext *ctx, int gprn, int sprn);
void spr_write_hdecr(DisasContext *ctx, int sprn, int gprn);
void spr_read_vtb(DisasContext *ctx, int gprn, int sprn);
void spr_write_vtb(DisasContext *ctx, int sprn, int gprn);
void spr_write_tbu40(DisasContext *ctx, int sprn, int gprn);
void spr_write_pidr(DisasContext *ctx, int sprn, int gprn);
void spr_write_lpidr(DisasContext *ctx, int sprn, int gprn);
void spr_read_hior(DisasContext *ctx, int gprn, int sprn);
void spr_write_hior(DisasContext *ctx, int sprn, int gprn);
void spr_write_ptcr(DisasContext *ctx, int sprn, int gprn);
void spr_write_pcr(DisasContext *ctx, int sprn, int gprn);
void spr_read_dpdes(DisasContext *ctx, int gprn, int sprn);
void spr_write_dpdes(DisasContext *ctx, int sprn, int gprn);
void spr_write_amr(DisasContext *ctx, int sprn, int gprn);
void spr_write_uamor(DisasContext *ctx, int sprn, int gprn);
void spr_write_iamr(DisasContext *ctx, int sprn, int gprn);
#endif
#endif

#ifdef TARGET_PPC64
void spr_read_prev_upper32(DisasContext *ctx, int gprn, int sprn);
void spr_write_prev_upper32(DisasContext *ctx, int sprn, int gprn);
void spr_read_tar(DisasContext *ctx, int gprn, int sprn);
void spr_write_tar(DisasContext *ctx, int sprn, int gprn);
void spr_read_tm(DisasContext *ctx, int gprn, int sprn);
void spr_write_tm(DisasContext *ctx, int sprn, int gprn);
void spr_read_tm_upper32(DisasContext *ctx, int gprn, int sprn);
void spr_write_tm_upper32(DisasContext *ctx, int sprn, int gprn);
void spr_read_ebb(DisasContext *ctx, int gprn, int sprn);
void spr_write_ebb(DisasContext *ctx, int sprn, int gprn);
void spr_read_ebb_upper32(DisasContext *ctx, int gprn, int sprn);
void spr_write_ebb_upper32(DisasContext *ctx, int sprn, int gprn);
void spr_write_hmer(DisasContext *ctx, int sprn, int gprn);
void spr_write_lpcr(DisasContext *ctx, int sprn, int gprn);
#endif

void register_low_BATs(CPUPPCState *env);
void register_high_BATs(CPUPPCState *env);
void register_sdr1_sprs(CPUPPCState *env);
void register_thrm_sprs(CPUPPCState *env);
void register_usprgh_sprs(CPUPPCState *env);
void register_non_embedded_sprs(CPUPPCState *env);
void register_6xx_7xx_soft_tlb(CPUPPCState *env, int nb_tlbs, int nb_ways);
void register_generic_sprs(PowerPCCPU *cpu);

#endif
