/*
 *  PowerPC CPU initialization for qemu.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
 *  Copyright 2011 Freescale Semiconductor, Inc.
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

#include "qemu/osdep.h"
#include "disas/dis-asm.h"
#include "exec/gdbstub.h"
#include "kvm_ppc.h"
#include "sysemu/cpus.h"
#include "sysemu/hw_accel.h"
#include "sysemu/tcg.h"
#include "cpu-models.h"
#include "mmu-hash32.h"
#include "mmu-hash64.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/qemu-print.h"
#include "qapi/error.h"
#include "qapi/qmp/qnull.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/ppc.h"
#include "mmu-book3s-v3.h"
#include "qemu/cutils.h"
#include "disas/capstone.h"
#include "fpu/softfloat.h"
#include "qapi/qapi-commands-machine-target.h"

#include "helper_regs.h"
#include "internal.h"
#include "spr_common.h"
#include "power8-pmu.h"

#ifndef CONFIG_USER_ONLY
#include "hw/boards.h"
#endif

/* #define PPC_DEBUG_SPR */
/* #define USE_APPLE_GDB */

static inline void vscr_init(CPUPPCState *env, uint32_t val)
{
    /* Altivec always uses round-to-nearest */
    set_float_rounding_mode(float_round_nearest_even, &env->vec_status);
    ppc_store_vscr(env, val);
}

static void register_745_sprs(CPUPPCState *env)
{
    /* SGPRs */
    spr_register(env, SPR_SPRG4, "SPRG4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG5, "SPRG5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG6, "SPRG6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG7, "SPRG7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    /* Hardware implementation registers */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_HID2, "HID2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

static void register_755_sprs(CPUPPCState *env)
{
    /* L2 cache control */
    spr_register(env, SPR_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, spr_access_nop,
                 0x00000000);

    spr_register(env, SPR_L2PMCR, "L2PMCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* SPR common to all 7xx PowerPC implementations */
static void register_7xx_sprs(CPUPPCState *env)
{
    /* Breakpoints */
    spr_register_kvm(env, SPR_DABR, "DABR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_DABR, 0x00000000);

    spr_register(env, SPR_IABR, "IABR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Cache management */
    spr_register(env, SPR_ICTC, "ICTC",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Performance monitors */
    spr_register(env, SPR_7XX_MMCR0, "MMCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_MMCR1, "MMCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC1, "PMC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC2, "PMC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC3, "PMC3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC4, "PMC4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_SIAR, "SIAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_UMMCR0, "UMMCR0",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_UMMCR1, "UMMCR1",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC1, "UPMC1",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC2, "UPMC2",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC3, "UPMC3",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC4, "UPMC4",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_USIAR, "USIAR",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* External access control */
    spr_register(env, SPR_EAR, "EAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    /* Hardware implementation registers */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

#ifdef TARGET_PPC64
static void register_amr_sprs(CPUPPCState *env)
{
#ifndef CONFIG_USER_ONLY
    /*
     * Virtual Page Class Key protection
     *
     * The AMR is accessible either via SPR 13 or SPR 29.  13 is
     * userspace accessible, 29 is privileged.  So we only need to set
     * the kvm ONE_REG id on one of them, we use 29
     */
    spr_register(env, SPR_UAMR, "UAMR",
                 &spr_read_generic, &spr_write_amr,
                 &spr_read_generic, &spr_write_amr,
                 0);
    spr_register_kvm_hv(env, SPR_AMR, "AMR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_amr,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_AMR, 0);
    spr_register_kvm_hv(env, SPR_UAMOR, "UAMOR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_uamor,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_UAMOR, 0);
    spr_register_hv(env, SPR_AMOR, "AMOR",
                    SPR_NOACCESS, SPR_NOACCESS,
                    SPR_NOACCESS, SPR_NOACCESS,
                    &spr_read_generic, &spr_write_generic,
                    0);
#endif /* !CONFIG_USER_ONLY */
}

static void register_iamr_sprs(CPUPPCState *env)
{
#ifndef CONFIG_USER_ONLY
    spr_register_kvm_hv(env, SPR_IAMR, "IAMR",
                        SPR_NOACCESS, SPR_NOACCESS,
                        &spr_read_generic, &spr_write_iamr,
                        &spr_read_generic, &spr_write_generic,
                        KVM_REG_PPC_IAMR, 0);
#endif /* !CONFIG_USER_ONLY */
}
#endif /* TARGET_PPC64 */

/* SPR specific to PowerPC 604 implementation */
static void register_604_sprs(CPUPPCState *env)
{
    /* Processor identification */
    spr_register(env, SPR_PIR, "PIR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_pir,
                 0x00000000);
    /* Breakpoints */
    spr_register(env, SPR_IABR, "IABR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register_kvm(env, SPR_DABR, "DABR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_DABR, 0x00000000);
    /* Performance counters */
    spr_register(env, SPR_7XX_MMCR0, "MMCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC1, "PMC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC2, "PMC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_SIAR, "SIAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_SDA, "SDA",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* External access control */
    spr_register(env, SPR_EAR, "EAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    /* Hardware implementation registers */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

static void register_604e_sprs(CPUPPCState *env)
{
    spr_register(env, SPR_7XX_MMCR1, "MMCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC3, "PMC3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC4, "PMC4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Hardware implementation registers */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* SPR specific to PowerPC 603 implementation */
static void register_603_sprs(CPUPPCState *env)
{
    /* External access control */
    spr_register(env, SPR_EAR, "EAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Breakpoints */
    spr_register(env, SPR_IABR, "IABR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

static void register_e300_sprs(CPUPPCState *env)
{
    /* hardware implementation registers */
    spr_register(env, SPR_HID2, "HID2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Breakpoints */
    spr_register(env, SPR_DABR, "DABR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_DABR2, "DABR2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_IABR2, "IABR2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_IBCR, "IBCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_DBCR, "DBCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* SPR specific to PowerPC G2 implementation */
static void register_G2_sprs(CPUPPCState *env)
{
    /* Memory base address */
    /* MBAR */
    spr_register(env, SPR_MBAR, "MBAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Exception processing */
    spr_register(env, SPR_BOOKE_CSRR0, "CSRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_CSRR1, "CSRR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Breakpoints */
    spr_register(env, SPR_DABR, "DABR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_DABR2, "DABR2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_IABR, "IABR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_IABR2, "IABR2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_IBCR, "IBCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_DBCR, "DBCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    /* External access control */
    spr_register(env, SPR_EAR, "EAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Hardware implementation register */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_HID2, "HID2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    /* SGPRs */
    spr_register(env, SPR_SPRG4, "SPRG4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG5, "SPRG5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG6, "SPRG6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG7, "SPRG7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

static void register_74xx_sprs(CPUPPCState *env)
{
    /* Breakpoints */
    spr_register_kvm(env, SPR_DABR, "DABR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_DABR, 0x00000000);

    spr_register(env, SPR_IABR, "IABR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Cache management */
    spr_register(env, SPR_ICTC, "ICTC",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Performance monitors */
    spr_register(env, SPR_7XX_MMCR0, "MMCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_MMCR1, "MMCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC1, "PMC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC2, "PMC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC3, "PMC3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC4, "PMC4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_SIAR, "SIAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_UMMCR0, "UMMCR0",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_UMMCR1, "UMMCR1",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC1, "UPMC1",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC2, "UPMC2",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC3, "UPMC3",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC4, "UPMC4",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_USIAR, "USIAR",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* External access control */
    spr_register(env, SPR_EAR, "EAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    /* Processor identification */
    spr_register(env, SPR_PIR, "PIR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_pir,
                 0x00000000);

    spr_register(env, SPR_74XX_MMCR2, "MMCR2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_74XX_UMMCR2, "UMMCR2",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_BAMR, "BAMR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MSSCR0, "MSSCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Hardware implementation registers */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Altivec */
    spr_register(env, SPR_VRSAVE, "VRSAVE",
                 &spr_read_generic, &spr_write_generic,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, spr_access_nop,
                 0x00000000);
}

static void register_l3_ctrl(CPUPPCState *env)
{
    /* L3CR */
    spr_register(env, SPR_L3CR, "L3CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* L3ITCR0 */
    spr_register(env, SPR_L3ITCR0, "L3ITCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* L3PM */
    spr_register(env, SPR_L3PM, "L3PM",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* PowerPC BookE SPR */
static void register_BookE_sprs(CPUPPCState *env, uint64_t ivor_mask)
{
    const char *ivor_names[64] = {
        "IVOR0",  "IVOR1",  "IVOR2",  "IVOR3",
        "IVOR4",  "IVOR5",  "IVOR6",  "IVOR7",
        "IVOR8",  "IVOR9",  "IVOR10", "IVOR11",
        "IVOR12", "IVOR13", "IVOR14", "IVOR15",
        "IVOR16", "IVOR17", "IVOR18", "IVOR19",
        "IVOR20", "IVOR21", "IVOR22", "IVOR23",
        "IVOR24", "IVOR25", "IVOR26", "IVOR27",
        "IVOR28", "IVOR29", "IVOR30", "IVOR31",
        "IVOR32", "IVOR33", "IVOR34", "IVOR35",
        "IVOR36", "IVOR37", "IVOR38", "IVOR39",
        "IVOR40", "IVOR41", "IVOR42", "IVOR43",
        "IVOR44", "IVOR45", "IVOR46", "IVOR47",
        "IVOR48", "IVOR49", "IVOR50", "IVOR51",
        "IVOR52", "IVOR53", "IVOR54", "IVOR55",
        "IVOR56", "IVOR57", "IVOR58", "IVOR59",
        "IVOR60", "IVOR61", "IVOR62", "IVOR63",
    };
#define SPR_BOOKE_IVORxx (-1)
    int ivor_sprn[64] = {
        SPR_BOOKE_IVOR0,  SPR_BOOKE_IVOR1,  SPR_BOOKE_IVOR2,  SPR_BOOKE_IVOR3,
        SPR_BOOKE_IVOR4,  SPR_BOOKE_IVOR5,  SPR_BOOKE_IVOR6,  SPR_BOOKE_IVOR7,
        SPR_BOOKE_IVOR8,  SPR_BOOKE_IVOR9,  SPR_BOOKE_IVOR10, SPR_BOOKE_IVOR11,
        SPR_BOOKE_IVOR12, SPR_BOOKE_IVOR13, SPR_BOOKE_IVOR14, SPR_BOOKE_IVOR15,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVOR32, SPR_BOOKE_IVOR33, SPR_BOOKE_IVOR34, SPR_BOOKE_IVOR35,
        SPR_BOOKE_IVOR36, SPR_BOOKE_IVOR37, SPR_BOOKE_IVOR38, SPR_BOOKE_IVOR39,
        SPR_BOOKE_IVOR40, SPR_BOOKE_IVOR41, SPR_BOOKE_IVOR42, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
    };
    int i;

    /* Interrupt processing */
    spr_register(env, SPR_BOOKE_CSRR0, "CSRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_CSRR1, "CSRR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Debug */
    spr_register(env, SPR_BOOKE_IAC1, "IAC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_BOOKE_IAC2, "IAC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_BOOKE_DAC1, "DAC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_BOOKE_DAC2, "DAC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_BOOKE_DBCR0, "DBCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_40x_dbcr0,
                 0x00000000);

    spr_register(env, SPR_BOOKE_DBCR1, "DBCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_BOOKE_DBCR2, "DBCR2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_DSRR0, "DSRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_DSRR1, "DSRR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_BOOKE_DBSR, "DBSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_clear,
                 0x00000000);
    spr_register(env, SPR_BOOKE_DEAR, "DEAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_ESR, "ESR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_IVPR, "IVPR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_excp_prefix,
                 0x00000000);
    /* Exception vectors */
    for (i = 0; i < 64; i++) {
        if (ivor_mask & (1ULL << i)) {
            if (ivor_sprn[i] == SPR_BOOKE_IVORxx) {
                fprintf(stderr, "ERROR: IVOR %d SPR is not defined\n", i);
                exit(1);
            }
            spr_register(env, ivor_sprn[i], ivor_names[i],
                         SPR_NOACCESS, SPR_NOACCESS,
                         &spr_read_generic, &spr_write_excp_vector,
                         0x00000000);
        }
    }
    spr_register(env, SPR_BOOKE_PID, "PID",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_booke_pid,
                 0x00000000);
    spr_register(env, SPR_BOOKE_TCR, "TCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_booke_tcr,
                 0x00000000);
    spr_register(env, SPR_BOOKE_TSR, "TSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_booke_tsr,
                 0x00000000);
    /* Timer */
    spr_register(env, SPR_DECR, "DECR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_decr, &spr_write_decr,
                 0x00000000);
    spr_register(env, SPR_BOOKE_DECAR, "DECAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, &spr_write_generic,
                 0x00000000);
    /* SPRGs */
    spr_register(env, SPR_USPRG0, "USPRG0",
                 &spr_read_generic, &spr_write_generic,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG4, "SPRG4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG5, "SPRG5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG6, "SPRG6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG7, "SPRG7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_SPRG8, "SPRG8",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_SPRG9, "SPRG9",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

#if !defined(CONFIG_USER_ONLY)
static inline uint32_t register_tlbncfg(uint32_t assoc, uint32_t minsize,
                                   uint32_t maxsize, uint32_t flags,
                                   uint32_t nentries)
{
    return (assoc << TLBnCFG_ASSOC_SHIFT) |
           (minsize << TLBnCFG_MINSIZE_SHIFT) |
           (maxsize << TLBnCFG_MAXSIZE_SHIFT) |
           flags | nentries;
}
#endif /* !CONFIG_USER_ONLY */

/* BookE 2.06 storage control registers */
static void register_BookE206_sprs(CPUPPCState *env, uint32_t mas_mask,
                             uint32_t *tlbncfg, uint32_t mmucfg)
{
#if !defined(CONFIG_USER_ONLY)
    const char *mas_names[8] = {
        "MAS0", "MAS1", "MAS2", "MAS3", "MAS4", "MAS5", "MAS6", "MAS7",
    };
    int mas_sprn[8] = {
        SPR_BOOKE_MAS0, SPR_BOOKE_MAS1, SPR_BOOKE_MAS2, SPR_BOOKE_MAS3,
        SPR_BOOKE_MAS4, SPR_BOOKE_MAS5, SPR_BOOKE_MAS6, SPR_BOOKE_MAS7,
    };
    int i;

    /* TLB assist registers */
    for (i = 0; i < 8; i++) {
        if (mas_mask & (1 << i)) {
            spr_register(env, mas_sprn[i], mas_names[i],
                         SPR_NOACCESS, SPR_NOACCESS,
                         &spr_read_generic,
                         (i == 2 && (env->insns_flags & PPC_64B))
                         ? &spr_write_generic : &spr_write_generic32,
                         0x00000000);
        }
    }
    if (env->nb_pids > 1) {
        spr_register(env, SPR_BOOKE_PID1, "PID1",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_booke_pid,
                     0x00000000);
    }
    if (env->nb_pids > 2) {
        spr_register(env, SPR_BOOKE_PID2, "PID2",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_booke_pid,
                     0x00000000);
    }

    spr_register(env, SPR_BOOKE_EPLC, "EPLC",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_eplc,
                 0x00000000);
    spr_register(env, SPR_BOOKE_EPSC, "EPSC",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_epsc,
                 0x00000000);

    spr_register(env, SPR_MMUCFG, "MMUCFG",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 mmucfg);
    switch (env->nb_ways) {
    case 4:
        spr_register(env, SPR_BOOKE_TLB3CFG, "TLB3CFG",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, SPR_NOACCESS,
                     tlbncfg[3]);
        /* Fallthru */
    case 3:
        spr_register(env, SPR_BOOKE_TLB2CFG, "TLB2CFG",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, SPR_NOACCESS,
                     tlbncfg[2]);
        /* Fallthru */
    case 2:
        spr_register(env, SPR_BOOKE_TLB1CFG, "TLB1CFG",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, SPR_NOACCESS,
                     tlbncfg[1]);
        /* Fallthru */
    case 1:
        spr_register(env, SPR_BOOKE_TLB0CFG, "TLB0CFG",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, SPR_NOACCESS,
                     tlbncfg[0]);
        /* Fallthru */
    case 0:
    default:
        break;
    }
#endif
}

/* SPR specific to PowerPC 440 implementation */
static void register_440_sprs(CPUPPCState *env)
{
    /* Cache control */
    spr_register(env, SPR_440_DNV0, "DNV0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_DNV1, "DNV1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_DNV2, "DNV2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_DNV3, "DNV3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_DTV0, "DTV0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_DTV1, "DTV1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_DTV2, "DTV2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_DTV3, "DTV3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_DVLIM, "DVLIM",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_INV0, "INV0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_INV1, "INV1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_INV2, "INV2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_INV3, "INV3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_ITV0, "ITV0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_ITV1, "ITV1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_ITV2, "ITV2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_ITV3, "ITV3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_IVLIM, "IVLIM",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Cache debug */
    spr_register(env, SPR_BOOKE_DCDBTRH, "DCDBTRH",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_BOOKE_DCDBTRL, "DCDBTRL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_BOOKE_ICDBDR, "ICDBDR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_BOOKE_ICDBTRH, "ICDBTRH",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_BOOKE_ICDBTRL, "ICDBTRL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_440_DBDR, "DBDR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Processor control */
    spr_register(env, SPR_4xx_CCR0, "CCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_440_RSTCFG, "RSTCFG",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* Storage control */
    spr_register(env, SPR_440_MMUCR, "MMUCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    /* Processor identification */
    spr_register(env, SPR_BOOKE_PIR, "PIR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_pir,
                 0x00000000);

    spr_register(env, SPR_BOOKE_IAC3, "IAC3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_BOOKE_IAC4, "IAC4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_BOOKE_DVC1, "DVC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_BOOKE_DVC2, "DVC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* SPR shared between PowerPC 40x implementations */
static void register_40x_sprs(CPUPPCState *env)
{
    /* Cache */
    /* not emulated, as QEMU do not emulate caches */
    spr_register(env, SPR_40x_DCCR, "DCCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* not emulated, as QEMU do not emulate caches */
    spr_register(env, SPR_40x_ICCR, "ICCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* not emulated, as QEMU do not emulate caches */
    spr_register(env, SPR_BOOKE_ICDBDR, "ICDBDR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* Exception */
    spr_register(env, SPR_40x_DEAR, "DEAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_40x_ESR, "ESR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_40x_EVPR, "EVPR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_excp_prefix,
                 0x00000000);
    spr_register(env, SPR_40x_SRR2, "SRR2",
                 &spr_read_generic, &spr_write_generic,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_40x_SRR3, "SRR3",
                 &spr_read_generic, &spr_write_generic,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Timers */
    spr_register(env, SPR_40x_PIT, "PIT",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_40x_pit, &spr_write_40x_pit,
                 0x00000000);
    spr_register(env, SPR_40x_TCR, "TCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_40x_tcr,
                 0x00000000);
    spr_register(env, SPR_40x_TSR, "TSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_40x_tsr,
                 0x00000000);
}

/* SPR specific to PowerPC 405 implementation */
static void register_405_sprs(CPUPPCState *env)
{
    /* MMU */
    spr_register(env, SPR_40x_PID, "PID",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_40x_pid,
                 0x00000000);
    spr_register(env, SPR_4xx_CCR0, "CCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00700000);
    /* Debug interface */
    spr_register(env, SPR_40x_DBCR0, "DBCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_40x_dbcr0,
                 0x00000000);

    spr_register(env, SPR_405_DBCR1, "DBCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_40x_DBSR, "DBSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_clear,
                 /* Last reset was system reset */
                 0x00000300);

    spr_register(env, SPR_40x_DAC1, "DAC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_40x_DAC2, "DAC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_405_DVC1, "DVC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_405_DVC2, "DVC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_40x_IAC1, "IAC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_40x_IAC2, "IAC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_405_IAC3, "IAC3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_405_IAC4, "IAC4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Storage control */
    spr_register(env, SPR_405_SLER, "SLER",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_40x_sler,
                 0x00000000);
    spr_register(env, SPR_40x_ZPR, "ZPR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_405_SU0R, "SU0R",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* SPRG */
    spr_register(env, SPR_USPRG0, "USPRG0",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG4, "SPRG4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG5, "SPRG5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG6, "SPRG6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG7, "SPRG7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 spr_read_generic, &spr_write_generic,
                 0x00000000);

    /* Bus access control */
    /* not emulated, as QEMU never does speculative access */
    spr_register(env, SPR_40x_SGR, "SGR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0xFFFFFFFF);
    /* not emulated, as QEMU do not emulate caches */
    spr_register(env, SPR_40x_DCWR, "DCWR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}


static void register_5xx_8xx_sprs(CPUPPCState *env)
{
    /* Exception processing */
    spr_register_kvm(env, SPR_DSISR, "DSISR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_DSISR, 0x00000000);
    spr_register_kvm(env, SPR_DAR, "DAR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_DAR, 0x00000000);
    /* Timer */
    spr_register(env, SPR_DECR, "DECR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_decr, &spr_write_decr,
                 0x00000000);

    spr_register(env, SPR_MPC_EIE, "EIE",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_EID, "EID",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_NRI, "NRI",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_CMPA, "CMPA",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_CMPB, "CMPB",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_CMPC, "CMPC",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_CMPD, "CMPD",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_ECR, "ECR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_DER, "DER",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_COUNTA, "COUNTA",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_COUNTB, "COUNTB",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_CMPE, "CMPE",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_CMPF, "CMPF",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_CMPG, "CMPG",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_CMPH, "CMPH",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_LCTRL1, "LCTRL1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_LCTRL2, "LCTRL2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_BAR, "BAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_DPDR, "DPDR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_IMMR, "IMMR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

static void register_5xx_sprs(CPUPPCState *env)
{
    spr_register(env, SPR_RCPU_MI_GRA, "MI_GRA",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_L2U_GRA, "L2U_GRA",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RPCU_BBCMCR, "L2U_BBCMCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_L2U_MCR, "L2U_MCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_MI_RBA0, "MI_RBA0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_MI_RBA1, "MI_RBA1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_MI_RBA2, "MI_RBA2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_MI_RBA3, "MI_RBA3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_L2U_RBA0, "L2U_RBA0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_L2U_RBA1, "L2U_RBA1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_L2U_RBA2, "L2U_RBA2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_L2U_RBA3, "L2U_RBA3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_MI_RA0, "MI_RA0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_MI_RA1, "MI_RA1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_MI_RA2, "MI_RA2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_MI_RA3, "MI_RA3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_L2U_RA0, "L2U_RA0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_L2U_RA1, "L2U_RA1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_L2U_RA2, "L2U_RA2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_L2U_RA3, "L2U_RA3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_RCPU_FPECR, "FPECR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

static void register_8xx_sprs(CPUPPCState *env)
{

    spr_register(env, SPR_MPC_IC_CST, "IC_CST",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_IC_ADR, "IC_ADR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_IC_DAT, "IC_DAT",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_DC_CST, "DC_CST",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_DC_ADR, "DC_ADR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_DC_DAT, "DC_DAT",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MI_CTR, "MI_CTR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MI_AP, "MI_AP",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MI_EPN, "MI_EPN",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MI_TWC, "MI_TWC",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MI_RPN, "MI_RPN",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MI_DBCAM, "MI_DBCAM",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MI_DBRAM0, "MI_DBRAM0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MI_DBRAM1, "MI_DBRAM1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MD_CTR, "MD_CTR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MD_CASID, "MD_CASID",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MD_AP, "MD_AP",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MD_EPN, "MD_EPN",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MD_TWB, "MD_TWB",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MD_TWC, "MD_TWC",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MD_RPN, "MD_RPN",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MD_TW, "MD_TW",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MD_DBCAM, "MD_DBCAM",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MD_DBRAM0, "MD_DBRAM0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MPC_MD_DBRAM1, "MD_DBRAM1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/*
 * AMR     => SPR 29 (Power 2.04)
 * CTRL    => SPR 136 (Power 2.04)
 * CTRL    => SPR 152 (Power 2.04)
 * SCOMC   => SPR 276 (64 bits ?)
 * SCOMD   => SPR 277 (64 bits ?)
 * TBU40   => SPR 286 (Power 2.04 hypv)
 * HSPRG0  => SPR 304 (Power 2.04 hypv)
 * HSPRG1  => SPR 305 (Power 2.04 hypv)
 * HDSISR  => SPR 306 (Power 2.04 hypv)
 * HDAR    => SPR 307 (Power 2.04 hypv)
 * PURR    => SPR 309 (Power 2.04 hypv)
 * HDEC    => SPR 310 (Power 2.04 hypv)
 * HIOR    => SPR 311 (hypv)
 * RMOR    => SPR 312 (970)
 * HRMOR   => SPR 313 (Power 2.04 hypv)
 * HSRR0   => SPR 314 (Power 2.04 hypv)
 * HSRR1   => SPR 315 (Power 2.04 hypv)
 * LPIDR   => SPR 317 (970)
 * EPR     => SPR 702 (Power 2.04 emb)
 * perf    => 768-783 (Power 2.04)
 * perf    => 784-799 (Power 2.04)
 * PPR     => SPR 896 (Power 2.04)
 * DABRX   => 1015    (Power 2.04 hypv)
 * FPECR   => SPR 1022 (?)
 * ... and more (thermal management, performance counters, ...)
 */

/*****************************************************************************/
/* Exception vectors models                                                  */
static void init_excp_4xx_softmmu(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_CRITICAL] = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_PIT]      = 0x00001000;
    env->excp_vectors[POWERPC_EXCP_FIT]      = 0x00001010;
    env->excp_vectors[POWERPC_EXCP_WDT]      = 0x00001020;
    env->excp_vectors[POWERPC_EXCP_DTLB]     = 0x00001100;
    env->excp_vectors[POWERPC_EXCP_ITLB]     = 0x00001200;
    env->excp_vectors[POWERPC_EXCP_DEBUG]    = 0x00002000;
    env->ivor_mask = 0x0000FFF0UL;
    env->ivpr_mask = 0xFFFF0000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

static void init_excp_MPC5xx(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_FPA]      = 0x00000E00;
    env->excp_vectors[POWERPC_EXCP_EMUL]     = 0x00001000;
    env->excp_vectors[POWERPC_EXCP_DABR]     = 0x00001C00;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001C00;
    env->excp_vectors[POWERPC_EXCP_MEXTBR]   = 0x00001E00;
    env->excp_vectors[POWERPC_EXCP_NMEXTBR]  = 0x00001F00;
    env->ivor_mask = 0x0000FFF0UL;
    env->ivpr_mask = 0xFFFF0000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0x00000100UL;
#endif
}

static void init_excp_MPC8xx(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_FPA]      = 0x00000E00;
    env->excp_vectors[POWERPC_EXCP_EMUL]     = 0x00001000;
    env->excp_vectors[POWERPC_EXCP_ITLB]     = 0x00001100;
    env->excp_vectors[POWERPC_EXCP_DTLB]     = 0x00001200;
    env->excp_vectors[POWERPC_EXCP_ITLBE]    = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_DTLBE]    = 0x00001400;
    env->excp_vectors[POWERPC_EXCP_DABR]     = 0x00001C00;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001C00;
    env->excp_vectors[POWERPC_EXCP_MEXTBR]   = 0x00001E00;
    env->excp_vectors[POWERPC_EXCP_NMEXTBR]  = 0x00001F00;
    env->ivor_mask = 0x0000FFF0UL;
    env->ivpr_mask = 0xFFFF0000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0x00000100UL;
#endif
}

static void init_excp_G2(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_CRITICAL] = 0x00000A00;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_IFTLB]    = 0x00001000;
    env->excp_vectors[POWERPC_EXCP_DLTLB]    = 0x00001100;
    env->excp_vectors[POWERPC_EXCP_DSTLB]    = 0x00001200;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_SMI]      = 0x00001400;
    /* Hardware reset vector */
    env->hreset_vector = 0x00000100UL;
#endif
}

static void init_excp_e200(CPUPPCState *env, target_ulong ivpr_mask)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000FFC;
    env->excp_vectors[POWERPC_EXCP_CRITICAL] = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_APU]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_FIT]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_WDT]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_DTLB]     = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_ITLB]     = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_DEBUG]    = 0x00000000;
    /*
     * These two are the same IVOR as POWERPC_EXCP_VPU and
     * POWERPC_EXCP_VPUA. We deal with that when dispatching at
     * powerpc_excp().
     */
    env->excp_vectors[POWERPC_EXCP_SPEU]     = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_EFPDI]    = 0x00000000;

    env->excp_vectors[POWERPC_EXCP_EFPRI]    = 0x00000000;
    env->ivor_mask = 0x0000FFF7UL;
    env->ivpr_mask = ivpr_mask;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

static void init_excp_BookE(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_CRITICAL] = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_APU]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_FIT]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_WDT]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_DTLB]     = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_ITLB]     = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_DEBUG]    = 0x00000000;
    env->ivor_mask = 0x0000FFF0UL;
    env->ivpr_mask = 0xFFFF0000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

static void init_excp_603(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_IFTLB]    = 0x00001000;
    env->excp_vectors[POWERPC_EXCP_DLTLB]    = 0x00001100;
    env->excp_vectors[POWERPC_EXCP_DSTLB]    = 0x00001200;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_SMI]      = 0x00001400;
    /* Hardware reset vector */
    env->hreset_vector = 0x00000100UL;
#endif
}

static void init_excp_604(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_PERFM]    = 0x00000F00;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_SMI]      = 0x00001400;
    /* Hardware reset vector */
    env->hreset_vector = 0x00000100UL;
#endif
}

static void init_excp_7x0(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_PERFM]    = 0x00000F00;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_SMI]      = 0x00001400;
    env->excp_vectors[POWERPC_EXCP_THERM]    = 0x00001700;
    /* Hardware reset vector */
    env->hreset_vector = 0x00000100UL;
#endif
}

static void init_excp_750cl(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_PERFM]    = 0x00000F00;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_SMI]      = 0x00001400;
    /* Hardware reset vector */
    env->hreset_vector = 0x00000100UL;
#endif
}

static void init_excp_750cx(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_PERFM]    = 0x00000F00;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_THERM]    = 0x00001700;
    /* Hardware reset vector */
    env->hreset_vector = 0x00000100UL;
#endif
}

/* XXX: Check if this is correct */
static void init_excp_7x5(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_PERFM]    = 0x00000F00;
    env->excp_vectors[POWERPC_EXCP_IFTLB]    = 0x00001000;
    env->excp_vectors[POWERPC_EXCP_DLTLB]    = 0x00001100;
    env->excp_vectors[POWERPC_EXCP_DSTLB]    = 0x00001200;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_SMI]      = 0x00001400;
    env->excp_vectors[POWERPC_EXCP_THERM]    = 0x00001700;
    /* Hardware reset vector */
    env->hreset_vector = 0x00000100UL;
#endif
}

static void init_excp_7400(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_PERFM]    = 0x00000F00;
    env->excp_vectors[POWERPC_EXCP_VPU]      = 0x00000F20;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_SMI]      = 0x00001400;
    env->excp_vectors[POWERPC_EXCP_VPUA]     = 0x00001600;
    env->excp_vectors[POWERPC_EXCP_THERM]    = 0x00001700;
    /* Hardware reset vector */
    env->hreset_vector = 0x00000100UL;
#endif
}

static void init_excp_7450(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_PERFM]    = 0x00000F00;
    env->excp_vectors[POWERPC_EXCP_VPU]      = 0x00000F20;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_SMI]      = 0x00001400;
    env->excp_vectors[POWERPC_EXCP_VPUA]     = 0x00001600;
    /* Hardware reset vector */
    env->hreset_vector = 0x00000100UL;
#endif
}

#if defined(TARGET_PPC64)
static void init_excp_970(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_DSEG]     = 0x00000380;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_ISEG]     = 0x00000480;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_HDECR]    = 0x00000980;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_PERFM]    = 0x00000F00;
    env->excp_vectors[POWERPC_EXCP_VPU]      = 0x00000F20;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_MAINT]    = 0x00001600;
    env->excp_vectors[POWERPC_EXCP_VPUA]     = 0x00001700;
    env->excp_vectors[POWERPC_EXCP_THERM]    = 0x00001800;
    /* Hardware reset vector */
    env->hreset_vector = 0x0000000000000100ULL;
#endif
}

static void init_excp_POWER7(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_DSEG]     = 0x00000380;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_ISEG]     = 0x00000480;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_HDECR]    = 0x00000980;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_HDSI]     = 0x00000E00;
    env->excp_vectors[POWERPC_EXCP_HISI]     = 0x00000E20;
    env->excp_vectors[POWERPC_EXCP_HV_EMU]   = 0x00000E40;
    env->excp_vectors[POWERPC_EXCP_HV_MAINT] = 0x00000E60;
    env->excp_vectors[POWERPC_EXCP_PERFM]    = 0x00000F00;
    env->excp_vectors[POWERPC_EXCP_VPU]      = 0x00000F20;
    env->excp_vectors[POWERPC_EXCP_VSXU]     = 0x00000F40;
    /* Hardware reset vector */
    env->hreset_vector = 0x0000000000000100ULL;
#endif
}

static void init_excp_POWER8(CPUPPCState *env)
{
    init_excp_POWER7(env);

#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_SDOOR]    = 0x00000A00;
    env->excp_vectors[POWERPC_EXCP_FU]       = 0x00000F60;
    env->excp_vectors[POWERPC_EXCP_HV_FU]    = 0x00000F80;
    env->excp_vectors[POWERPC_EXCP_SDOOR_HV] = 0x00000E80;

    /* Userland exceptions without vector value in PowerISA v3.1 */
    env->excp_vectors[POWERPC_EXCP_PERFM_EBB] = 0x0;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL_EBB] = 0x0;
#endif
}

static void init_excp_POWER9(CPUPPCState *env)
{
    init_excp_POWER8(env);

#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_HVIRT]    = 0x00000EA0;
    env->excp_vectors[POWERPC_EXCP_SYSCALL_VECTORED] = 0x00017000;
#endif
}

static void init_excp_POWER10(CPUPPCState *env)
{
    init_excp_POWER9(env);
}

#endif

static int check_pow_hid0(CPUPPCState *env)
{
    if (env->spr[SPR_HID0] & 0x00E00000) {
        return 1;
    }

    return 0;
}

static int check_pow_hid0_74xx(CPUPPCState *env)
{
    if (env->spr[SPR_HID0] & 0x00600000) {
        return 1;
    }

    return 0;
}

static void init_proc_405(CPUPPCState *env)
{
    register_40x_sprs(env);
    register_405_sprs(env);
    register_usprgh_sprs(env);

    /* Memory management */
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = 64;
    env->nb_ways = 1;
    env->id_tlbs = 0;
    env->tlb_type = TLB_EMB;
#endif
    init_excp_4xx_softmmu(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc40x_irq_init(env_archcpu(env));

    SET_FIT_PERIOD(8, 12, 16, 20);
    SET_WDT_PERIOD(16, 20, 24, 28);
}

POWERPC_FAMILY(405)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 405";
    pcc->init_proc = init_proc_405;
    pcc->check_pow = check_pow_nocheck;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_DCR | PPC_WRTEE |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_40x_ICBT |
                       PPC_CACHE_DCBZ | PPC_CACHE_DCBA |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_40x_TLB | PPC_MEM_TLBIA | PPC_MEM_TLBSYNC |
                       PPC_4xx_COMMON | PPC_405_MAC | PPC_40x_EXCP;
    pcc->msr_mask = (1ull << MSR_WE) |
                    (1ull << MSR_CE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_DWE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR);
    pcc->mmu_model = POWERPC_MMU_SOFT_4xx;
    pcc->excp_model = POWERPC_EXCP_40x;
    pcc->bus_model = PPC_FLAGS_INPUT_405;
    pcc->bfd_mach = bfd_mach_ppc_403;
    pcc->flags = POWERPC_FLAG_CE | POWERPC_FLAG_DWE |
                 POWERPC_FLAG_DE | POWERPC_FLAG_BUS_CLK;
}

static void init_proc_440EP(CPUPPCState *env)
{
    register_BookE_sprs(env, 0x000000000000FFFFULL);
    register_440_sprs(env);
    register_usprgh_sprs(env);

    spr_register(env, SPR_BOOKE_MCSR, "MCSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_MCSRR0, "MCSRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_MCSRR1, "MCSRR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_CCR1, "CCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = 64;
    env->nb_ways = 1;
    env->id_tlbs = 0;
    env->tlb_type = TLB_EMB;
#endif
    init_excp_BookE(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    ppc40x_irq_init(env_archcpu(env));

    SET_FIT_PERIOD(12, 16, 20, 24);
    SET_WDT_PERIOD(20, 24, 28, 32);
}

POWERPC_FAMILY(440EP)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 440 EP";
    pcc->init_proc = init_proc_440EP;
    pcc->check_pow = check_pow_nocheck;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING |
                       PPC_FLOAT | PPC_FLOAT_FRES | PPC_FLOAT_FSEL |
                       PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |
                       PPC_FLOAT_STFIWX |
                       PPC_DCR | PPC_WRTEE | PPC_RFMCI |
                       PPC_CACHE | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBZ | PPC_CACHE_DCBA |
                       PPC_MEM_TLBSYNC | PPC_MFTB |
                       PPC_BOOKE | PPC_4xx_COMMON | PPC_405_MAC |
                       PPC_440_SPEC;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_CE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_DWE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR);
    pcc->mmu_model = POWERPC_MMU_BOOKE;
    pcc->excp_model = POWERPC_EXCP_BOOKE;
    pcc->bus_model = PPC_FLAGS_INPUT_BookE;
    pcc->bfd_mach = bfd_mach_ppc_403;
    pcc->flags = POWERPC_FLAG_CE | POWERPC_FLAG_DWE |
                 POWERPC_FLAG_DE | POWERPC_FLAG_BUS_CLK;
}

POWERPC_FAMILY(460EX)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 460 EX";
    pcc->init_proc = init_proc_440EP;
    pcc->check_pow = check_pow_nocheck;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING |
                       PPC_FLOAT | PPC_FLOAT_FRES | PPC_FLOAT_FSEL |
                       PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |
                       PPC_FLOAT_STFIWX |
                       PPC_DCR | PPC_DCRX | PPC_WRTEE | PPC_RFMCI |
                       PPC_CACHE | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBZ | PPC_CACHE_DCBA |
                       PPC_MEM_TLBSYNC | PPC_MFTB |
                       PPC_BOOKE | PPC_4xx_COMMON | PPC_405_MAC |
                       PPC_440_SPEC;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_CE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_DWE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR);
    pcc->mmu_model = POWERPC_MMU_BOOKE;
    pcc->excp_model = POWERPC_EXCP_BOOKE;
    pcc->bus_model = PPC_FLAGS_INPUT_BookE;
    pcc->bfd_mach = bfd_mach_ppc_403;
    pcc->flags = POWERPC_FLAG_CE | POWERPC_FLAG_DWE |
                 POWERPC_FLAG_DE | POWERPC_FLAG_BUS_CLK;
}

static void init_proc_440GP(CPUPPCState *env)
{
    register_BookE_sprs(env, 0x000000000000FFFFULL);
    register_440_sprs(env);
    register_usprgh_sprs(env);

    /* Memory management */
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = 64;
    env->nb_ways = 1;
    env->id_tlbs = 0;
    env->tlb_type = TLB_EMB;
#endif
    init_excp_BookE(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* XXX: TODO: allocate internal IRQ controller */

    SET_FIT_PERIOD(12, 16, 20, 24);
    SET_WDT_PERIOD(20, 24, 28, 32);
}

POWERPC_FAMILY(440GP)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 440 GP";
    pcc->init_proc = init_proc_440GP;
    pcc->check_pow = check_pow_nocheck;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING |
                       PPC_DCR | PPC_DCRX | PPC_WRTEE | PPC_MFAPIDI |
                       PPC_CACHE | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBZ | PPC_CACHE_DCBA |
                       PPC_MEM_TLBSYNC | PPC_TLBIVA | PPC_MFTB |
                       PPC_BOOKE | PPC_4xx_COMMON | PPC_405_MAC |
                       PPC_440_SPEC;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_CE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_DWE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR);
    pcc->mmu_model = POWERPC_MMU_BOOKE;
    pcc->excp_model = POWERPC_EXCP_BOOKE;
    pcc->bus_model = PPC_FLAGS_INPUT_BookE;
    pcc->bfd_mach = bfd_mach_ppc_403;
    pcc->flags = POWERPC_FLAG_CE | POWERPC_FLAG_DWE |
                 POWERPC_FLAG_DE | POWERPC_FLAG_BUS_CLK;
}

static void init_proc_440x5(CPUPPCState *env)
{
    register_BookE_sprs(env, 0x000000000000FFFFULL);
    register_440_sprs(env);
    register_usprgh_sprs(env);

    spr_register(env, SPR_BOOKE_MCSR, "MCSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_MCSRR0, "MCSRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_MCSRR1, "MCSRR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_440_CCR1, "CCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = 64;
    env->nb_ways = 1;
    env->id_tlbs = 0;
    env->tlb_type = TLB_EMB;
#endif
    init_excp_BookE(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    ppc40x_irq_init(env_archcpu(env));

    SET_FIT_PERIOD(12, 16, 20, 24);
    SET_WDT_PERIOD(20, 24, 28, 32);
}

POWERPC_FAMILY(440x5)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 440x5";
    pcc->init_proc = init_proc_440x5;
    pcc->check_pow = check_pow_nocheck;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING |
                       PPC_DCR | PPC_WRTEE | PPC_RFMCI |
                       PPC_CACHE | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBZ | PPC_CACHE_DCBA |
                       PPC_MEM_TLBSYNC | PPC_MFTB |
                       PPC_BOOKE | PPC_4xx_COMMON | PPC_405_MAC |
                       PPC_440_SPEC;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_CE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_DWE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR);
    pcc->mmu_model = POWERPC_MMU_BOOKE;
    pcc->excp_model = POWERPC_EXCP_BOOKE;
    pcc->bus_model = PPC_FLAGS_INPUT_BookE;
    pcc->bfd_mach = bfd_mach_ppc_403;
    pcc->flags = POWERPC_FLAG_CE | POWERPC_FLAG_DWE |
                 POWERPC_FLAG_DE | POWERPC_FLAG_BUS_CLK;
}

POWERPC_FAMILY(440x5wDFPU)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 440x5 with double precision FPU";
    pcc->init_proc = init_proc_440x5;
    pcc->check_pow = check_pow_nocheck;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING |
                       PPC_FLOAT | PPC_FLOAT_FSQRT |
                       PPC_FLOAT_STFIWX |
                       PPC_DCR | PPC_WRTEE | PPC_RFMCI |
                       PPC_CACHE | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBZ | PPC_CACHE_DCBA |
                       PPC_MEM_TLBSYNC | PPC_MFTB |
                       PPC_BOOKE | PPC_4xx_COMMON | PPC_405_MAC |
                       PPC_440_SPEC;
    pcc->insns_flags2 = PPC2_FP_CVT_S64;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_CE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_DWE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR);
    pcc->mmu_model = POWERPC_MMU_BOOKE;
    pcc->excp_model = POWERPC_EXCP_BOOKE;
    pcc->bus_model = PPC_FLAGS_INPUT_BookE;
    pcc->bfd_mach = bfd_mach_ppc_403;
    pcc->flags = POWERPC_FLAG_CE | POWERPC_FLAG_DWE |
                 POWERPC_FLAG_DE | POWERPC_FLAG_BUS_CLK;
}

static void init_proc_MPC5xx(CPUPPCState *env)
{
    register_5xx_8xx_sprs(env);
    register_5xx_sprs(env);
    init_excp_MPC5xx(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* XXX: TODO: allocate internal IRQ controller */
}

POWERPC_FAMILY(MPC5xx)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "Freescale 5xx cores (aka RCPU)";
    pcc->init_proc = init_proc_MPC5xx;
    pcc->check_pow = check_pow_none;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING |
                       PPC_MEM_EIEIO | PPC_MEM_SYNC |
                       PPC_CACHE_ICBI | PPC_FLOAT | PPC_FLOAT_STFIWX |
                       PPC_MFTB;
    pcc->msr_mask = (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_REAL;
    pcc->excp_model = POWERPC_EXCP_6xx;
    pcc->bus_model = PPC_FLAGS_INPUT_RCPU;
    pcc->bfd_mach = bfd_mach_ppc_505;
    pcc->flags = POWERPC_FLAG_SE | POWERPC_FLAG_BE |
                 POWERPC_FLAG_BUS_CLK;
}

static void init_proc_MPC8xx(CPUPPCState *env)
{
    register_5xx_8xx_sprs(env);
    register_8xx_sprs(env);
    init_excp_MPC8xx(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* XXX: TODO: allocate internal IRQ controller */
}

POWERPC_FAMILY(MPC8xx)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "Freescale 8xx cores (aka PowerQUICC)";
    pcc->init_proc = init_proc_MPC8xx;
    pcc->check_pow = check_pow_none;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING  |
                       PPC_MEM_EIEIO | PPC_MEM_SYNC |
                       PPC_CACHE_ICBI | PPC_MFTB;
    pcc->msr_mask = (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_MPC8xx;
    pcc->excp_model = POWERPC_EXCP_6xx;
    pcc->bus_model = PPC_FLAGS_INPUT_RCPU;
    pcc->bfd_mach = bfd_mach_ppc_860;
    pcc->flags = POWERPC_FLAG_SE | POWERPC_FLAG_BE |
                 POWERPC_FLAG_BUS_CLK;
}

/* Freescale 82xx cores (aka PowerQUICC-II)                                  */

static void init_proc_G2(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_sdr1_sprs(env);
    register_G2_sprs(env);

    /* Memory management */
    register_low_BATs(env);
    register_high_BATs(env);
    register_6xx_7xx_soft_tlb(env, 64, 2);
    init_excp_G2(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(G2)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC G2";
    pcc->init_proc = init_proc_G2;
    pcc->check_pow = check_pow_hid0;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC | PPC_6xx_TLB |
                       PPC_SEGMENT | PPC_EXTERN;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_TGPR) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_AL) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_RI);
    pcc->mmu_model = POWERPC_MMU_SOFT_6xx;
    pcc->excp_model = POWERPC_EXCP_6xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_ec603e;
    pcc->flags = POWERPC_FLAG_TGPR | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_BUS_CLK;
}

POWERPC_FAMILY(G2LE)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC G2LE";
    pcc->init_proc = init_proc_G2;
    pcc->check_pow = check_pow_hid0;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC | PPC_6xx_TLB |
                       PPC_SEGMENT | PPC_EXTERN;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_TGPR) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_AL) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_SOFT_6xx;
    pcc->excp_model = POWERPC_EXCP_6xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_ec603e;
    pcc->flags = POWERPC_FLAG_TGPR | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_BUS_CLK;
}

static void init_proc_e200(CPUPPCState *env)
{
    register_BookE_sprs(env, 0x000000070000FFFFULL);

    spr_register(env, SPR_BOOKE_SPEFSCR, "SPEFSCR",
                 &spr_read_spefscr, &spr_write_spefscr,
                 &spr_read_spefscr, &spr_write_spefscr,
                 0x00000000);
    /* Memory management */
    register_BookE206_sprs(env, 0x0000005D, NULL, 0);
    register_usprgh_sprs(env);

    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_Exxx_ALTCTXCR, "ALTCTXCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_Exxx_BUCSR, "BUCSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_Exxx_CTXCR, "CTXCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_Exxx_DBCNT, "DBCNT",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_Exxx_DBCR3, "DBCR3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_Exxx_L1CFG0, "L1CFG0",
                 &spr_read_generic, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_Exxx_L1CSR0, "L1CSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_Exxx_L1FINV0, "L1FINV0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_BOOKE_TLB0CFG, "TLB0CFG",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_BOOKE_TLB1CFG, "TLB1CFG",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_BOOKE_IAC3, "IAC3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_BOOKE_IAC4, "IAC4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MMUCSR0, "MMUCSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000); /* TOFIX */
    spr_register(env, SPR_BOOKE_DSRR0, "DSRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_DSRR1, "DSRR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = 64;
    env->nb_ways = 1;
    env->id_tlbs = 0;
    env->tlb_type = TLB_EMB;
#endif
    init_excp_e200(env, 0xFFFF0000UL);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* XXX: TODO: allocate internal IRQ controller */
}

POWERPC_FAMILY(e200)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "e200 core";
    pcc->init_proc = init_proc_e200;
    pcc->check_pow = check_pow_hid0;
    /*
     * XXX: unimplemented instructions:
     * dcblc
     * dcbtlst
     * dcbtstls
     * icblc
     * icbtls
     * tlbivax
     * all SPE multiply-accumulate instructions
     */
    pcc->insns_flags = PPC_INSNS_BASE | PPC_ISEL |
                       PPC_SPE | PPC_SPE_SINGLE |
                       PPC_WRTEE | PPC_RFDI |
                       PPC_CACHE | PPC_CACHE_LOCK | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBZ | PPC_CACHE_DCBA |
                       PPC_MEM_TLBSYNC | PPC_TLBIVAX |
                       PPC_BOOKE;
    pcc->msr_mask = (1ull << MSR_UCLE) |
                    (1ull << MSR_SPE) |
                    (1ull << MSR_POW) |
                    (1ull << MSR_CE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_DWE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR);
    pcc->mmu_model = POWERPC_MMU_BOOKE206;
    pcc->excp_model = POWERPC_EXCP_BOOKE;
    pcc->bus_model = PPC_FLAGS_INPUT_BookE;
    pcc->bfd_mach = bfd_mach_ppc_860;
    pcc->flags = POWERPC_FLAG_SPE | POWERPC_FLAG_CE |
                 POWERPC_FLAG_UBLE | POWERPC_FLAG_DE |
                 POWERPC_FLAG_BUS_CLK;
}

enum fsl_e500_version {
    fsl_e500v1,
    fsl_e500v2,
    fsl_e500mc,
    fsl_e5500,
    fsl_e6500,
};

static void init_proc_e500(CPUPPCState *env, int version)
{
    uint32_t tlbncfg[2];
    uint64_t ivor_mask;
    uint64_t ivpr_mask = 0xFFFF0000ULL;
    uint32_t l1cfg0 = 0x3800  /* 8 ways */
                    | 0x0020; /* 32 kb */
    uint32_t l1cfg1 = 0x3800  /* 8 ways */
                    | 0x0020; /* 32 kb */
    uint32_t mmucfg = 0;
#if !defined(CONFIG_USER_ONLY)
    int i;
#endif

    /*
     * XXX The e500 doesn't implement IVOR7 and IVOR9, but doesn't
     *     complain when accessing them.
     * register_BookE_sprs(env, 0x0000000F0000FD7FULL);
     */
    switch (version) {
    case fsl_e500v1:
    case fsl_e500v2:
    default:
        ivor_mask = 0x0000000F0000FFFFULL;
        break;
    case fsl_e500mc:
    case fsl_e5500:
        ivor_mask = 0x000003FE0000FFFFULL;
        break;
    case fsl_e6500:
        ivor_mask = 0x000003FF0000FFFFULL;
        break;
    }
    register_BookE_sprs(env, ivor_mask);

    spr_register(env, SPR_USPRG3, "USPRG3",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    /* Processor identification */
    spr_register(env, SPR_BOOKE_PIR, "PIR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_pir,
                 0x00000000);

    spr_register(env, SPR_BOOKE_SPEFSCR, "SPEFSCR",
                 &spr_read_spefscr, &spr_write_spefscr,
                 &spr_read_spefscr, &spr_write_spefscr,
                 0x00000000);
#if !defined(CONFIG_USER_ONLY)
    /* Memory management */
    env->nb_pids = 3;
    env->nb_ways = 2;
    env->id_tlbs = 0;
    switch (version) {
    case fsl_e500v1:
        tlbncfg[0] = register_tlbncfg(2, 1, 1, 0, 256);
        tlbncfg[1] = register_tlbncfg(16, 1, 9, TLBnCFG_AVAIL | TLBnCFG_IPROT, 16);
        break;
    case fsl_e500v2:
        tlbncfg[0] = register_tlbncfg(4, 1, 1, 0, 512);
        tlbncfg[1] = register_tlbncfg(16, 1, 12, TLBnCFG_AVAIL | TLBnCFG_IPROT, 16);
        break;
    case fsl_e500mc:
    case fsl_e5500:
        tlbncfg[0] = register_tlbncfg(4, 1, 1, 0, 512);
        tlbncfg[1] = register_tlbncfg(64, 1, 12, TLBnCFG_AVAIL | TLBnCFG_IPROT, 64);
        break;
    case fsl_e6500:
        mmucfg = 0x6510B45;
        env->nb_pids = 1;
        tlbncfg[0] = 0x08052400;
        tlbncfg[1] = 0x40028040;
        break;
    default:
        cpu_abort(env_cpu(env), "Unknown CPU: " TARGET_FMT_lx "\n",
                  env->spr[SPR_PVR]);
    }
#endif
    /* Cache sizes */
    switch (version) {
    case fsl_e500v1:
    case fsl_e500v2:
        env->dcache_line_size = 32;
        env->icache_line_size = 32;
        break;
    case fsl_e500mc:
    case fsl_e5500:
        env->dcache_line_size = 64;
        env->icache_line_size = 64;
        l1cfg0 |= 0x1000000; /* 64 byte cache block size */
        l1cfg1 |= 0x1000000; /* 64 byte cache block size */
        break;
    case fsl_e6500:
        env->dcache_line_size = 32;
        env->icache_line_size = 32;
        l1cfg0 |= 0x0F83820;
        l1cfg1 |= 0x0B83820;
        break;
    default:
        cpu_abort(env_cpu(env), "Unknown CPU: " TARGET_FMT_lx "\n",
                  env->spr[SPR_PVR]);
    }
    register_BookE206_sprs(env, 0x000000DF, tlbncfg, mmucfg);
    register_usprgh_sprs(env);

    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_Exxx_BBEAR, "BBEAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_Exxx_BBTAR, "BBTAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_Exxx_MCAR, "MCAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_BOOKE_MCSR, "MCSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_Exxx_NPIDR, "NPIDR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_Exxx_BUCSR, "BUCSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_Exxx_L1CFG0, "L1CFG0",
                 &spr_read_generic, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 l1cfg0);
    spr_register(env, SPR_Exxx_L1CFG1, "L1CFG1",
                 &spr_read_generic, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 l1cfg1);
    spr_register(env, SPR_Exxx_L1CSR0, "L1CSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_e500_l1csr0,
                 0x00000000);
    spr_register(env, SPR_Exxx_L1CSR1, "L1CSR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_e500_l1csr1,
                 0x00000000);
    if (version != fsl_e500v1 && version != fsl_e500v2) {
        spr_register(env, SPR_Exxx_L2CSR0, "L2CSR0",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_e500_l2csr0,
                     0x00000000);
    }
    spr_register(env, SPR_BOOKE_MCSRR0, "MCSRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_MCSRR1, "MCSRR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_MMUCSR0, "MMUCSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_booke206_mmucsr0,
                 0x00000000);
    spr_register(env, SPR_BOOKE_EPR, "EPR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* XXX better abstract into Emb.xxx features */
    if ((version == fsl_e5500) || (version == fsl_e6500)) {
        spr_register(env, SPR_BOOKE_EPCR, "EPCR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     0x00000000);
        spr_register(env, SPR_BOOKE_MAS7_MAS3, "MAS7_MAS3",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_mas73, &spr_write_mas73,
                     0x00000000);
        ivpr_mask = (target_ulong)~0xFFFFULL;
    }

    if (version == fsl_e6500) {
        /* Thread identification */
        spr_register(env, SPR_TIR, "TIR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, SPR_NOACCESS,
                     0x00000000);
        spr_register(env, SPR_BOOKE_TLB0PS, "TLB0PS",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, SPR_NOACCESS,
                     0x00000004);
        spr_register(env, SPR_BOOKE_TLB1PS, "TLB1PS",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, SPR_NOACCESS,
                     0x7FFFFFFC);
    }

#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = 0;
    env->tlb_type = TLB_MAS;
    for (i = 0; i < BOOKE206_MAX_TLBN; i++) {
        env->nb_tlb += booke206_tlb_size(env, i);
    }
#endif

    init_excp_e200(env, ivpr_mask);
    /* Allocate hardware IRQ controller */
    ppce500_irq_init(env_archcpu(env));
}

static void init_proc_e500v1(CPUPPCState *env)
{
    init_proc_e500(env, fsl_e500v1);
}

POWERPC_FAMILY(e500v1)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "e500v1 core";
    pcc->init_proc = init_proc_e500v1;
    pcc->check_pow = check_pow_hid0;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_ISEL |
                       PPC_SPE | PPC_SPE_SINGLE |
                       PPC_WRTEE | PPC_RFDI |
                       PPC_CACHE | PPC_CACHE_LOCK | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBZ | PPC_CACHE_DCBA |
                       PPC_MEM_TLBSYNC | PPC_TLBIVAX | PPC_MEM_SYNC;
    pcc->insns_flags2 = PPC2_BOOKE206;
    pcc->msr_mask = (1ull << MSR_UCLE) |
                    (1ull << MSR_SPE) |
                    (1ull << MSR_POW) |
                    (1ull << MSR_CE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_DWE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR);
    pcc->mmu_model = POWERPC_MMU_BOOKE206;
    pcc->excp_model = POWERPC_EXCP_BOOKE;
    pcc->bus_model = PPC_FLAGS_INPUT_BookE;
    pcc->bfd_mach = bfd_mach_ppc_860;
    pcc->flags = POWERPC_FLAG_SPE | POWERPC_FLAG_CE |
                 POWERPC_FLAG_UBLE | POWERPC_FLAG_DE |
                 POWERPC_FLAG_BUS_CLK;
}

static void init_proc_e500v2(CPUPPCState *env)
{
    init_proc_e500(env, fsl_e500v2);
}

POWERPC_FAMILY(e500v2)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "e500v2 core";
    pcc->init_proc = init_proc_e500v2;
    pcc->check_pow = check_pow_hid0;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_ISEL |
                       PPC_SPE | PPC_SPE_SINGLE | PPC_SPE_DOUBLE |
                       PPC_WRTEE | PPC_RFDI |
                       PPC_CACHE | PPC_CACHE_LOCK | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBZ | PPC_CACHE_DCBA |
                       PPC_MEM_TLBSYNC | PPC_TLBIVAX | PPC_MEM_SYNC;
    pcc->insns_flags2 = PPC2_BOOKE206;
    pcc->msr_mask = (1ull << MSR_UCLE) |
                    (1ull << MSR_SPE) |
                    (1ull << MSR_POW) |
                    (1ull << MSR_CE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_DWE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR);
    pcc->mmu_model = POWERPC_MMU_BOOKE206;
    pcc->excp_model = POWERPC_EXCP_BOOKE;
    pcc->bus_model = PPC_FLAGS_INPUT_BookE;
    pcc->bfd_mach = bfd_mach_ppc_860;
    pcc->flags = POWERPC_FLAG_SPE | POWERPC_FLAG_CE |
                 POWERPC_FLAG_UBLE | POWERPC_FLAG_DE |
                 POWERPC_FLAG_BUS_CLK;
}

static void init_proc_e500mc(CPUPPCState *env)
{
    init_proc_e500(env, fsl_e500mc);
}

POWERPC_FAMILY(e500mc)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "e500mc core";
    pcc->init_proc = init_proc_e500mc;
    pcc->check_pow = check_pow_none;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_ISEL | PPC_MFTB |
                       PPC_WRTEE | PPC_RFDI | PPC_RFMCI |
                       PPC_CACHE | PPC_CACHE_LOCK | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBZ | PPC_CACHE_DCBA |
                       PPC_FLOAT | PPC_FLOAT_FRES |
                       PPC_FLOAT_FRSQRTE | PPC_FLOAT_FSEL |
                       PPC_FLOAT_STFIWX | PPC_WAIT |
                       PPC_MEM_TLBSYNC | PPC_TLBIVAX | PPC_MEM_SYNC;
    pcc->insns_flags2 = PPC2_BOOKE206 | PPC2_PRCNTL;
    pcc->msr_mask = (1ull << MSR_GS) |
                    (1ull << MSR_UCLE) |
                    (1ull << MSR_CE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PX) |
                    (1ull << MSR_RI);
    pcc->mmu_model = POWERPC_MMU_BOOKE206;
    pcc->excp_model = POWERPC_EXCP_BOOKE;
    pcc->bus_model = PPC_FLAGS_INPUT_BookE;
    /* FIXME: figure out the correct flag for e500mc */
    pcc->bfd_mach = bfd_mach_ppc_e500;
    pcc->flags = POWERPC_FLAG_CE | POWERPC_FLAG_DE |
                 POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK;
}

#ifdef TARGET_PPC64
static void init_proc_e5500(CPUPPCState *env)
{
    init_proc_e500(env, fsl_e5500);
}

POWERPC_FAMILY(e5500)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "e5500 core";
    pcc->init_proc = init_proc_e5500;
    pcc->check_pow = check_pow_none;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_ISEL | PPC_MFTB |
                       PPC_WRTEE | PPC_RFDI | PPC_RFMCI |
                       PPC_CACHE | PPC_CACHE_LOCK | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBZ | PPC_CACHE_DCBA |
                       PPC_FLOAT | PPC_FLOAT_FRES |
                       PPC_FLOAT_FRSQRTE | PPC_FLOAT_FSEL |
                       PPC_FLOAT_STFIWX | PPC_WAIT |
                       PPC_MEM_TLBSYNC | PPC_TLBIVAX | PPC_MEM_SYNC |
                       PPC_64B | PPC_POPCNTB | PPC_POPCNTWD;
    pcc->insns_flags2 = PPC2_BOOKE206 | PPC2_PRCNTL | PPC2_PERM_ISA206 |
                        PPC2_FP_CVT_S64;
    pcc->msr_mask = (1ull << MSR_CM) |
                    (1ull << MSR_GS) |
                    (1ull << MSR_UCLE) |
                    (1ull << MSR_CE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PX) |
                    (1ull << MSR_RI);
    pcc->mmu_model = POWERPC_MMU_BOOKE206;
    pcc->excp_model = POWERPC_EXCP_BOOKE;
    pcc->bus_model = PPC_FLAGS_INPUT_BookE;
    /* FIXME: figure out the correct flag for e5500 */
    pcc->bfd_mach = bfd_mach_ppc_e500;
    pcc->flags = POWERPC_FLAG_CE | POWERPC_FLAG_DE |
                 POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK;
}

static void init_proc_e6500(CPUPPCState *env)
{
    init_proc_e500(env, fsl_e6500);
}

POWERPC_FAMILY(e6500)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "e6500 core";
    pcc->init_proc = init_proc_e6500;
    pcc->check_pow = check_pow_none;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_ISEL | PPC_MFTB |
                       PPC_WRTEE | PPC_RFDI | PPC_RFMCI |
                       PPC_CACHE | PPC_CACHE_LOCK | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBZ | PPC_CACHE_DCBA |
                       PPC_FLOAT | PPC_FLOAT_FRES |
                       PPC_FLOAT_FRSQRTE | PPC_FLOAT_FSEL |
                       PPC_FLOAT_STFIWX | PPC_WAIT |
                       PPC_MEM_TLBSYNC | PPC_TLBIVAX | PPC_MEM_SYNC |
                       PPC_64B | PPC_POPCNTB | PPC_POPCNTWD | PPC_ALTIVEC;
    pcc->insns_flags2 = PPC2_BOOKE206 | PPC2_PRCNTL | PPC2_PERM_ISA206 |
                        PPC2_FP_CVT_S64 | PPC2_ATOMIC_ISA206;
    pcc->msr_mask = (1ull << MSR_CM) |
                    (1ull << MSR_GS) |
                    (1ull << MSR_UCLE) |
                    (1ull << MSR_CE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_IS) |
                    (1ull << MSR_DS) |
                    (1ull << MSR_PX) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_VR);
    pcc->mmu_model = POWERPC_MMU_BOOKE206;
    pcc->excp_model = POWERPC_EXCP_BOOKE;
    pcc->bus_model = PPC_FLAGS_INPUT_BookE;
    pcc->bfd_mach = bfd_mach_ppc_e500;
    pcc->flags = POWERPC_FLAG_CE | POWERPC_FLAG_DE |
                 POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK | POWERPC_FLAG_VRE;
}

#endif

/* Non-embedded PowerPC                                                      */
static void init_proc_603(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_sdr1_sprs(env);
    register_603_sprs(env);

    /* Memory management */
    register_low_BATs(env);
    register_6xx_7xx_soft_tlb(env, 64, 2);
    init_excp_603(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(603)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 603";
    pcc->init_proc = init_proc_603;
    pcc->check_pow = check_pow_hid0;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC | PPC_6xx_TLB |
                       PPC_SEGMENT | PPC_EXTERN;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_TGPR) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_SOFT_6xx;
    pcc->excp_model = POWERPC_EXCP_6xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_603;
    pcc->flags = POWERPC_FLAG_TGPR | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_BUS_CLK;
}

POWERPC_FAMILY(603E)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 603e";
    pcc->init_proc = init_proc_603;
    pcc->check_pow = check_pow_hid0;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC | PPC_6xx_TLB |
                       PPC_SEGMENT | PPC_EXTERN;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_TGPR) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_SOFT_6xx;
    pcc->excp_model = POWERPC_EXCP_6xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_ec603e;
    pcc->flags = POWERPC_FLAG_TGPR | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_BUS_CLK;
}

static void init_proc_e300(CPUPPCState *env)
{
    init_proc_603(env);
    register_e300_sprs(env);
}

POWERPC_FAMILY(e300)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "e300 core";
    pcc->init_proc = init_proc_e300;
    pcc->check_pow = check_pow_hid0;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC | PPC_6xx_TLB |
                       PPC_SEGMENT | PPC_EXTERN;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_TGPR) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_AL) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_SOFT_6xx;
    pcc->excp_model = POWERPC_EXCP_6xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_603;
    pcc->flags = POWERPC_FLAG_TGPR | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_BUS_CLK;
}

static void init_proc_604(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_sdr1_sprs(env);
    register_604_sprs(env);

    /* Memory management */
    register_low_BATs(env);
    init_excp_604(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(604)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 604";
    pcc->init_proc = init_proc_604;
    pcc->check_pow = check_pow_nocheck;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_SEGMENT | PPC_EXTERN;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_32B;
    pcc->excp_model = POWERPC_EXCP_6xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_604;
    pcc->flags = POWERPC_FLAG_SE | POWERPC_FLAG_BE |
                 POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK;
}

static void init_proc_604E(CPUPPCState *env)
{
    init_proc_604(env);
    register_604e_sprs(env);
}

POWERPC_FAMILY(604E)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 604E";
    pcc->init_proc = init_proc_604E;
    pcc->check_pow = check_pow_nocheck;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_SEGMENT | PPC_EXTERN;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_32B;
    pcc->excp_model = POWERPC_EXCP_6xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_604;
    pcc->flags = POWERPC_FLAG_SE | POWERPC_FLAG_BE |
                 POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK;
}

static void init_proc_740(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_sdr1_sprs(env);
    register_7xx_sprs(env);
    /* Thermal management */
    register_thrm_sprs(env);

    /* Memory management */
    register_low_BATs(env);
    init_excp_7x0(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(740)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 740";
    pcc->init_proc = init_proc_740;
    pcc->check_pow = check_pow_hid0;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_SEGMENT | PPC_EXTERN;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_32B;
    pcc->excp_model = POWERPC_EXCP_7xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_750;
    pcc->flags = POWERPC_FLAG_SE | POWERPC_FLAG_BE |
                 POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK;
}

static void init_proc_750(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_sdr1_sprs(env);
    register_7xx_sprs(env);

    spr_register(env, SPR_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, spr_access_nop,
                 0x00000000);
    /* Thermal management */
    register_thrm_sprs(env);

    /* Memory management */
    register_low_BATs(env);
    /*
     * XXX: high BATs are also present but are known to be bugged on
     *      die version 1.x
     */
    init_excp_7x0(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(750)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 750";
    pcc->init_proc = init_proc_750;
    pcc->check_pow = check_pow_hid0;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_SEGMENT | PPC_EXTERN;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_32B;
    pcc->excp_model = POWERPC_EXCP_7xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_750;
    pcc->flags = POWERPC_FLAG_SE | POWERPC_FLAG_BE |
                 POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK;
}

static void init_proc_750cl(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_sdr1_sprs(env);
    register_7xx_sprs(env);

    spr_register(env, SPR_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, spr_access_nop,
                 0x00000000);
    /* Thermal management */
    /* Those registers are fake on 750CL */
    spr_register(env, SPR_THRM1, "THRM1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_THRM2, "THRM2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_THRM3, "THRM3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_750_TDCL, "TDCL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_750_TDCH, "TDCH",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* DMA */
    spr_register(env, SPR_750_WPAR, "WPAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_750_DMAL, "DMAL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_750_DMAU, "DMAU",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Hardware implementation registers */
    spr_register(env, SPR_750CL_HID2, "HID2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_750CL_HID4, "HID4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Quantization registers */
    spr_register(env, SPR_750_GQR0, "GQR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_750_GQR1, "GQR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_750_GQR2, "GQR2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_750_GQR3, "GQR3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_750_GQR4, "GQR4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_750_GQR5, "GQR5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_750_GQR6, "GQR6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_750_GQR7, "GQR7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    register_low_BATs(env);
    /* PowerPC 750cl has 8 DBATs and 8 IBATs */
    register_high_BATs(env);
    init_excp_750cl(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(750cl)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 750 CL";
    pcc->init_proc = init_proc_750cl;
    pcc->check_pow = check_pow_hid0;
    /*
     * XXX: not implemented:
     * cache lock instructions:
     * dcbz_l
     * floating point paired instructions
     * psq_lux
     * psq_lx
     * psq_stux
     * psq_stx
     * ps_abs
     * ps_add
     * ps_cmpo0
     * ps_cmpo1
     * ps_cmpu0
     * ps_cmpu1
     * ps_div
     * ps_madd
     * ps_madds0
     * ps_madds1
     * ps_merge00
     * ps_merge01
     * ps_merge10
     * ps_merge11
     * ps_mr
     * ps_msub
     * ps_mul
     * ps_muls0
     * ps_muls1
     * ps_nabs
     * ps_neg
     * ps_nmadd
     * ps_nmsub
     * ps_res
     * ps_rsqrte
     * ps_sel
     * ps_sub
     * ps_sum0
     * ps_sum1
     */
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_SEGMENT | PPC_EXTERN;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_32B;
    pcc->excp_model = POWERPC_EXCP_7xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_750;
    pcc->flags = POWERPC_FLAG_SE | POWERPC_FLAG_BE |
                 POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK;
}

static void init_proc_750cx(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_sdr1_sprs(env);
    register_7xx_sprs(env);

    spr_register(env, SPR_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, spr_access_nop,
                 0x00000000);
    /* Thermal management */
    register_thrm_sprs(env);

    spr_register(env, SPR_SDA, "SDA",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    /* Memory management */
    register_low_BATs(env);
    /* PowerPC 750cx has 8 DBATs and 8 IBATs */
    register_high_BATs(env);
    init_excp_750cx(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(750cx)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 750CX";
    pcc->init_proc = init_proc_750cx;
    pcc->check_pow = check_pow_hid0;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_SEGMENT | PPC_EXTERN;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_32B;
    pcc->excp_model = POWERPC_EXCP_7xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_750;
    pcc->flags = POWERPC_FLAG_SE | POWERPC_FLAG_BE |
                 POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK;
}

static void init_proc_750fx(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_sdr1_sprs(env);
    register_7xx_sprs(env);

    spr_register(env, SPR_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, spr_access_nop,
                 0x00000000);
    /* Thermal management */
    register_thrm_sprs(env);

    spr_register(env, SPR_750_THRM4, "THRM4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Hardware implementation registers */
    spr_register(env, SPR_750FX_HID2, "HID2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    register_low_BATs(env);
    /* PowerPC 750fx & 750gx has 8 DBATs and 8 IBATs */
    register_high_BATs(env);
    init_excp_7x0(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(750fx)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 750FX";
    pcc->init_proc = init_proc_750fx;
    pcc->check_pow = check_pow_hid0;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_SEGMENT | PPC_EXTERN;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_32B;
    pcc->excp_model = POWERPC_EXCP_7xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_750;
    pcc->flags = POWERPC_FLAG_SE | POWERPC_FLAG_BE |
                 POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK;
}

static void init_proc_750gx(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_sdr1_sprs(env);
    register_7xx_sprs(env);

    spr_register(env, SPR_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, spr_access_nop,
                 0x00000000);
    /* Thermal management */
    register_thrm_sprs(env);

    spr_register(env, SPR_750_THRM4, "THRM4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Hardware implementation registers */
    spr_register(env, SPR_750FX_HID2, "HID2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    register_low_BATs(env);
    /* PowerPC 750fx & 750gx has 8 DBATs and 8 IBATs */
    register_high_BATs(env);
    init_excp_7x0(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(750gx)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 750GX";
    pcc->init_proc = init_proc_750gx;
    pcc->check_pow = check_pow_hid0;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_SEGMENT | PPC_EXTERN;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_32B;
    pcc->excp_model = POWERPC_EXCP_7xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_750;
    pcc->flags = POWERPC_FLAG_SE | POWERPC_FLAG_BE |
                 POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK;
}

static void init_proc_745(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_sdr1_sprs(env);
    register_7xx_sprs(env);
    register_745_sprs(env);
    /* Thermal management */
    register_thrm_sprs(env);

    /* Memory management */
    register_low_BATs(env);
    register_high_BATs(env);
    register_6xx_7xx_soft_tlb(env, 64, 2);
    init_excp_7x5(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(745)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 745";
    pcc->init_proc = init_proc_745;
    pcc->check_pow = check_pow_hid0;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC | PPC_6xx_TLB |
                       PPC_SEGMENT | PPC_EXTERN;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_SOFT_6xx;
    pcc->excp_model = POWERPC_EXCP_7xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_750;
    pcc->flags = POWERPC_FLAG_SE | POWERPC_FLAG_BE |
                 POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK;
}

static void init_proc_755(CPUPPCState *env)
{
    init_proc_745(env);
    register_755_sprs(env);
}

POWERPC_FAMILY(755)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 755";
    pcc->init_proc = init_proc_755;
    pcc->check_pow = check_pow_hid0;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC | PPC_6xx_TLB |
                       PPC_SEGMENT | PPC_EXTERN;
    pcc->msr_mask = (1ull << MSR_POW) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_SOFT_6xx;
    pcc->excp_model = POWERPC_EXCP_7xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_750;
    pcc->flags = POWERPC_FLAG_SE | POWERPC_FLAG_BE |
                 POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK;
}

static void init_proc_7400(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_sdr1_sprs(env);
    register_74xx_sprs(env);
    vscr_init(env, 0x00010000);

    spr_register(env, SPR_UBAMR, "UBAMR",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_MSSCR1, "MSSCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Thermal management */
    register_thrm_sprs(env);
    /* Memory management */
    register_low_BATs(env);
    init_excp_7400(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(7400)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 7400 (aka G4)";
    pcc->init_proc = init_proc_7400;
    pcc->check_pow = check_pow_hid0;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |
                       PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBA | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_MEM_TLBIA |
                       PPC_SEGMENT | PPC_EXTERN |
                       PPC_ALTIVEC;
    pcc->msr_mask = (1ull << MSR_VR) |
                    (1ull << MSR_POW) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_32B;
    pcc->excp_model = POWERPC_EXCP_74xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_7400;
    pcc->flags = POWERPC_FLAG_VRE | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_PMM |
                 POWERPC_FLAG_BUS_CLK;
}

static void init_proc_7410(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_sdr1_sprs(env);
    register_74xx_sprs(env);
    vscr_init(env, 0x00010000);

    spr_register(env, SPR_UBAMR, "UBAMR",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* Thermal management */
    register_thrm_sprs(env);
    /* L2PMCR */

    spr_register(env, SPR_L2PMCR, "L2PMCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* LDSTDB */

    spr_register(env, SPR_LDSTDB, "LDSTDB",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    register_low_BATs(env);
    init_excp_7400(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(7410)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 7410 (aka G4)";
    pcc->init_proc = init_proc_7410;
    pcc->check_pow = check_pow_hid0;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |
                       PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBA | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_MEM_TLBIA |
                       PPC_SEGMENT | PPC_EXTERN |
                       PPC_ALTIVEC;
    pcc->msr_mask = (1ull << MSR_VR) |
                    (1ull << MSR_POW) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_32B;
    pcc->excp_model = POWERPC_EXCP_74xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_7400;
    pcc->flags = POWERPC_FLAG_VRE | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_PMM |
                 POWERPC_FLAG_BUS_CLK;
}

static void init_proc_7440(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_sdr1_sprs(env);
    register_74xx_sprs(env);
    vscr_init(env, 0x00010000);

    spr_register(env, SPR_UBAMR, "UBAMR",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* LDSTCR */
    spr_register(env, SPR_LDSTCR, "LDSTCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* ICTRL */
    spr_register(env, SPR_ICTRL, "ICTRL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* MSSSR0 */
    spr_register(env, SPR_MSSSR0, "MSSSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* PMC */
    spr_register(env, SPR_7XX_PMC5, "PMC5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC5, "UPMC5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC6, "PMC6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC6, "UPMC6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* Memory management */
    register_low_BATs(env);
    init_excp_7450(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(7440)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 7440 (aka G4)";
    pcc->init_proc = init_proc_7440;
    pcc->check_pow = check_pow_hid0_74xx;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |
                       PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBA | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_MEM_TLBIA |
                       PPC_SEGMENT | PPC_EXTERN |
                       PPC_ALTIVEC;
    pcc->msr_mask = (1ull << MSR_VR) |
                    (1ull << MSR_POW) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_32B;
    pcc->excp_model = POWERPC_EXCP_74xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_7400;
    pcc->flags = POWERPC_FLAG_VRE | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_PMM |
                 POWERPC_FLAG_BUS_CLK;
}

static void init_proc_7450(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_sdr1_sprs(env);
    register_74xx_sprs(env);
    vscr_init(env, 0x00010000);
    /* Level 3 cache control */
    register_l3_ctrl(env);
    /* L3ITCR1 */
    spr_register(env, SPR_L3ITCR1, "L3ITCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* L3ITCR2 */
    spr_register(env, SPR_L3ITCR2, "L3ITCR2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* L3ITCR3 */
    spr_register(env, SPR_L3ITCR3, "L3ITCR3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* L3OHCR */
    spr_register(env, SPR_L3OHCR, "L3OHCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_UBAMR, "UBAMR",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* LDSTCR */
    spr_register(env, SPR_LDSTCR, "LDSTCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* ICTRL */
    spr_register(env, SPR_ICTRL, "ICTRL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* MSSSR0 */
    spr_register(env, SPR_MSSSR0, "MSSSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* PMC */
    spr_register(env, SPR_7XX_PMC5, "PMC5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC5, "UPMC5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC6, "PMC6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC6, "UPMC6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* Memory management */
    register_low_BATs(env);
    init_excp_7450(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(7450)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 7450 (aka G4)";
    pcc->init_proc = init_proc_7450;
    pcc->check_pow = check_pow_hid0_74xx;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |
                       PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBA | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_MEM_TLBIA |
                       PPC_SEGMENT | PPC_EXTERN |
                       PPC_ALTIVEC;
    pcc->msr_mask = (1ull << MSR_VR) |
                    (1ull << MSR_POW) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_32B;
    pcc->excp_model = POWERPC_EXCP_74xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_7400;
    pcc->flags = POWERPC_FLAG_VRE | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_PMM |
                 POWERPC_FLAG_BUS_CLK;
}

static void init_proc_7445(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_sdr1_sprs(env);
    register_74xx_sprs(env);
    vscr_init(env, 0x00010000);
    /* LDSTCR */
    spr_register(env, SPR_LDSTCR, "LDSTCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* ICTRL */
    spr_register(env, SPR_ICTRL, "ICTRL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* MSSSR0 */
    spr_register(env, SPR_MSSSR0, "MSSSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* PMC */
    spr_register(env, SPR_7XX_PMC5, "PMC5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC5, "UPMC5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC6, "PMC6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC6, "UPMC6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* SPRGs */
    spr_register(env, SPR_SPRG4, "SPRG4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG4, "USPRG4",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG5, "SPRG5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG5, "USPRG5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG6, "SPRG6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG6, "USPRG6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG7, "SPRG7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG7, "USPRG7",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* Memory management */
    register_low_BATs(env);
    register_high_BATs(env);
    init_excp_7450(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(7445)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 7445 (aka G4)";
    pcc->init_proc = init_proc_7445;
    pcc->check_pow = check_pow_hid0_74xx;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |
                       PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBA | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_MEM_TLBIA |
                       PPC_SEGMENT | PPC_EXTERN |
                       PPC_ALTIVEC;
    pcc->msr_mask = (1ull << MSR_VR) |
                    (1ull << MSR_POW) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_32B;
    pcc->excp_model = POWERPC_EXCP_74xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_7400;
    pcc->flags = POWERPC_FLAG_VRE | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_PMM |
                 POWERPC_FLAG_BUS_CLK;
}

static void init_proc_7455(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_sdr1_sprs(env);
    register_74xx_sprs(env);
    vscr_init(env, 0x00010000);
    /* Level 3 cache control */
    register_l3_ctrl(env);
    /* LDSTCR */
    spr_register(env, SPR_LDSTCR, "LDSTCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* ICTRL */
    spr_register(env, SPR_ICTRL, "ICTRL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* MSSSR0 */
    spr_register(env, SPR_MSSSR0, "MSSSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* PMC */
    spr_register(env, SPR_7XX_PMC5, "PMC5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC5, "UPMC5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC6, "PMC6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC6, "UPMC6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* SPRGs */
    spr_register(env, SPR_SPRG4, "SPRG4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG4, "USPRG4",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG5, "SPRG5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG5, "USPRG5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG6, "SPRG6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG6, "USPRG6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG7, "SPRG7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG7, "USPRG7",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* Memory management */
    register_low_BATs(env);
    register_high_BATs(env);
    init_excp_7450(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(7455)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 7455 (aka G4)";
    pcc->init_proc = init_proc_7455;
    pcc->check_pow = check_pow_hid0_74xx;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |
                       PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBA | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_MEM_TLBIA |
                       PPC_SEGMENT | PPC_EXTERN |
                       PPC_ALTIVEC;
    pcc->msr_mask = (1ull << MSR_VR) |
                    (1ull << MSR_POW) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_32B;
    pcc->excp_model = POWERPC_EXCP_74xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_7400;
    pcc->flags = POWERPC_FLAG_VRE | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_PMM |
                 POWERPC_FLAG_BUS_CLK;
}

static void init_proc_7457(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_sdr1_sprs(env);
    register_74xx_sprs(env);
    vscr_init(env, 0x00010000);
    /* Level 3 cache control */
    register_l3_ctrl(env);
    /* L3ITCR1 */
    spr_register(env, SPR_L3ITCR1, "L3ITCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* L3ITCR2 */
    spr_register(env, SPR_L3ITCR2, "L3ITCR2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* L3ITCR3 */
    spr_register(env, SPR_L3ITCR3, "L3ITCR3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* L3OHCR */
    spr_register(env, SPR_L3OHCR, "L3OHCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* LDSTCR */
    spr_register(env, SPR_LDSTCR, "LDSTCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* ICTRL */
    spr_register(env, SPR_ICTRL, "ICTRL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* MSSSR0 */
    spr_register(env, SPR_MSSSR0, "MSSSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* PMC */
    spr_register(env, SPR_7XX_PMC5, "PMC5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC5, "UPMC5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC6, "PMC6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC6, "UPMC6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* SPRGs */
    spr_register(env, SPR_SPRG4, "SPRG4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG4, "USPRG4",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG5, "SPRG5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG5, "USPRG5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG6, "SPRG6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG6, "USPRG6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG7, "SPRG7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG7, "USPRG7",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* Memory management */
    register_low_BATs(env);
    register_high_BATs(env);
    init_excp_7450(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(7457)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 7457 (aka G4)";
    pcc->init_proc = init_proc_7457;
    pcc->check_pow = check_pow_hid0_74xx;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |
                       PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBA | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_MEM_TLBIA |
                       PPC_SEGMENT | PPC_EXTERN |
                       PPC_ALTIVEC;
    pcc->msr_mask = (1ull << MSR_VR) |
                    (1ull << MSR_POW) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_32B;
    pcc->excp_model = POWERPC_EXCP_74xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_7400;
    pcc->flags = POWERPC_FLAG_VRE | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_PMM |
                 POWERPC_FLAG_BUS_CLK;
}

static void init_proc_e600(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_sdr1_sprs(env);
    register_74xx_sprs(env);
    vscr_init(env, 0x00010000);

    spr_register(env, SPR_UBAMR, "UBAMR",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_LDSTCR, "LDSTCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_ICTRL, "ICTRL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_MSSSR0, "MSSSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC5, "PMC5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC5, "UPMC5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);

    spr_register(env, SPR_7XX_PMC6, "PMC6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_7XX_UPMC6, "UPMC6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* SPRGs */
    spr_register(env, SPR_SPRG4, "SPRG4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG4, "USPRG4",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG5, "SPRG5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG5, "USPRG5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG6, "SPRG6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG6, "USPRG6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG7, "SPRG7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG7, "USPRG7",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* Memory management */
    register_low_BATs(env);
    register_high_BATs(env);
    init_excp_7450(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(e600)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC e600";
    pcc->init_proc = init_proc_e600;
    pcc->check_pow = check_pow_hid0_74xx;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |
                       PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI |
                       PPC_CACHE_DCBA | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_MEM_TLBIA |
                       PPC_SEGMENT | PPC_EXTERN |
                       PPC_ALTIVEC;
    pcc->insns_flags2 = PPC_NONE;
    pcc->msr_mask = (1ull << MSR_VR) |
                    (1ull << MSR_POW) |
                    (1ull << MSR_ILE) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_EP) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->mmu_model = POWERPC_MMU_32B;
    pcc->excp_model = POWERPC_EXCP_74xx;
    pcc->bus_model = PPC_FLAGS_INPUT_6xx;
    pcc->bfd_mach = bfd_mach_ppc_7400;
    pcc->flags = POWERPC_FLAG_VRE | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_PMM |
                 POWERPC_FLAG_BUS_CLK;
}

#if defined(TARGET_PPC64)
#if defined(CONFIG_USER_ONLY)
#define POWERPC970_HID5_INIT 0x00000080
#else
#define POWERPC970_HID5_INIT 0x00000000
#endif

static int check_pow_970(CPUPPCState *env)
{
    if (env->spr[SPR_HID0] & (HID0_DEEPNAP | HID0_DOZE | HID0_NAP)) {
        return 1;
    }

    return 0;
}

static void register_970_hid_sprs(CPUPPCState *env)
{
    /* Hardware implementation registers */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_clear,
                 0x60000000);
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_970_HID5, "HID5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 POWERPC970_HID5_INIT);
}

static void register_970_hior_sprs(CPUPPCState *env)
{
    spr_register(env, SPR_HIOR, "SPR_HIOR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_hior, &spr_write_hior,
                 0x00000000);
}

static void register_book3s_ctrl_sprs(CPUPPCState *env)
{
    spr_register(env, SPR_CTRL, "SPR_CTRL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, &spr_write_CTRL,
                 0x00000000);
    spr_register(env, SPR_UCTRL, "SPR_UCTRL",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
}

static void register_book3s_altivec_sprs(CPUPPCState *env)
{
    if (!(env->insns_flags & PPC_ALTIVEC)) {
        return;
    }

    spr_register_kvm(env, SPR_VRSAVE, "VRSAVE",
                     &spr_read_generic, &spr_write_generic,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_VRSAVE, 0x00000000);

}

static void register_book3s_dbg_sprs(CPUPPCState *env)
{
    /*
     * TODO: different specs define different scopes for these,
     * will have to address this:
     * 970: super/write and super/read
     * powerisa 2.03..2.04: hypv/write and super/read.
     * powerisa 2.05 and newer: hypv/write and hypv/read.
     */
    spr_register_kvm(env, SPR_DABR, "DABR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_DABR, 0x00000000);
    spr_register_kvm(env, SPR_DABRX, "DABRX",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_DABRX, 0x00000000);
}

static void register_book3s_207_dbg_sprs(CPUPPCState *env)
{
    spr_register_kvm_hv(env, SPR_DAWR0, "DAWR0",
                        SPR_NOACCESS, SPR_NOACCESS,
                        SPR_NOACCESS, SPR_NOACCESS,
                        &spr_read_generic, &spr_write_generic,
                        KVM_REG_PPC_DAWR, 0x00000000);
    spr_register_kvm_hv(env, SPR_DAWRX0, "DAWRX0",
                        SPR_NOACCESS, SPR_NOACCESS,
                        SPR_NOACCESS, SPR_NOACCESS,
                        &spr_read_generic, &spr_write_generic,
                        KVM_REG_PPC_DAWRX, 0x00000000);
    spr_register_kvm_hv(env, SPR_CIABR, "CIABR",
                        SPR_NOACCESS, SPR_NOACCESS,
                        SPR_NOACCESS, SPR_NOACCESS,
                        &spr_read_generic, &spr_write_generic,
                        KVM_REG_PPC_CIABR, 0x00000000);
}

static void register_970_dbg_sprs(CPUPPCState *env)
{
    /* Breakpoints */
    spr_register(env, SPR_IABR, "IABR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

static void register_book3s_pmu_sup_sprs(CPUPPCState *env)
{
    spr_register_kvm(env, SPR_POWER_MMCR0, "MMCR0",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_MMCR0,
                     KVM_REG_PPC_MMCR0, 0x80000000);
    spr_register_kvm(env, SPR_POWER_MMCR1, "MMCR1",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_MMCR1,
                     KVM_REG_PPC_MMCR1, 0x00000000);
    spr_register_kvm(env, SPR_POWER_MMCRA, "MMCRA",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_MMCRA, 0x00000000);
    spr_register_kvm(env, SPR_POWER_PMC1, "PMC1",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_PMC, &spr_write_PMC,
                     KVM_REG_PPC_PMC1, 0x00000000);
    spr_register_kvm(env, SPR_POWER_PMC2, "PMC2",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_PMC, &spr_write_PMC,
                     KVM_REG_PPC_PMC2, 0x00000000);
    spr_register_kvm(env, SPR_POWER_PMC3, "PMC3",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_PMC, &spr_write_PMC,
                     KVM_REG_PPC_PMC3, 0x00000000);
    spr_register_kvm(env, SPR_POWER_PMC4, "PMC4",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_PMC, &spr_write_PMC,
                     KVM_REG_PPC_PMC4, 0x00000000);
    spr_register_kvm(env, SPR_POWER_PMC5, "PMC5",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_PMC, &spr_write_PMC,
                     KVM_REG_PPC_PMC5, 0x00000000);
    spr_register_kvm(env, SPR_POWER_PMC6, "PMC6",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_PMC, &spr_write_PMC,
                     KVM_REG_PPC_PMC6, 0x00000000);
    spr_register_kvm(env, SPR_POWER_SIAR, "SIAR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_SIAR, 0x00000000);
    spr_register_kvm(env, SPR_POWER_SDAR, "SDAR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_SDAR, 0x00000000);
}

static void register_book3s_pmu_user_sprs(CPUPPCState *env)
{
    spr_register(env, SPR_POWER_UMMCR0, "UMMCR0",
                 &spr_read_MMCR0_ureg, &spr_write_MMCR0_ureg,
                 &spr_read_ureg, &spr_write_ureg,
                 0x80000000);
    spr_register(env, SPR_POWER_UMMCR1, "UMMCR1",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, &spr_write_ureg,
                 0x00000000);
    spr_register(env, SPR_POWER_UMMCRA, "UMMCRA",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, &spr_write_ureg,
                 0x00000000);
    spr_register(env, SPR_POWER_UPMC1, "UPMC1",
                 &spr_read_PMC14_ureg, &spr_write_PMC14_ureg,
                 &spr_read_ureg, &spr_write_ureg,
                 0x00000000);
    spr_register(env, SPR_POWER_UPMC2, "UPMC2",
                 &spr_read_PMC14_ureg, &spr_write_PMC14_ureg,
                 &spr_read_ureg, &spr_write_ureg,
                 0x00000000);
    spr_register(env, SPR_POWER_UPMC3, "UPMC3",
                 &spr_read_PMC14_ureg, &spr_write_PMC14_ureg,
                 &spr_read_ureg, &spr_write_ureg,
                 0x00000000);
    spr_register(env, SPR_POWER_UPMC4, "UPMC4",
                 &spr_read_PMC14_ureg, &spr_write_PMC14_ureg,
                 &spr_read_ureg, &spr_write_ureg,
                 0x00000000);
    spr_register(env, SPR_POWER_UPMC5, "UPMC5",
                 &spr_read_PMC56_ureg, &spr_write_PMC56_ureg,
                 &spr_read_ureg, &spr_write_ureg,
                 0x00000000);
    spr_register(env, SPR_POWER_UPMC6, "UPMC6",
                 &spr_read_PMC56_ureg, &spr_write_PMC56_ureg,
                 &spr_read_ureg, &spr_write_ureg,
                 0x00000000);
    spr_register(env, SPR_POWER_USIAR, "USIAR",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, &spr_write_ureg,
                 0x00000000);
    spr_register(env, SPR_POWER_USDAR, "USDAR",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, &spr_write_ureg,
                 0x00000000);
}

static void register_970_pmu_sup_sprs(CPUPPCState *env)
{
    spr_register_kvm(env, SPR_970_PMC7, "PMC7",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_PMC7, 0x00000000);
    spr_register_kvm(env, SPR_970_PMC8, "PMC8",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_PMC8, 0x00000000);
}

static void register_970_pmu_user_sprs(CPUPPCState *env)
{
    spr_register(env, SPR_970_UPMC7, "UPMC7",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, &spr_write_ureg,
                 0x00000000);
    spr_register(env, SPR_970_UPMC8, "UPMC8",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, &spr_write_ureg,
                 0x00000000);
}

static void register_power8_pmu_sup_sprs(CPUPPCState *env)
{
    spr_register_kvm(env, SPR_POWER_MMCR2, "MMCR2",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_MMCR2, 0x00000000);
    spr_register_kvm(env, SPR_POWER_MMCRS, "MMCRS",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_MMCRS, 0x00000000);
    spr_register_kvm(env, SPR_POWER_SIER, "SIER",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_SIER, 0x00000000);
    spr_register_kvm(env, SPR_POWER_SPMC1, "SPMC1",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_SPMC1, 0x00000000);
    spr_register_kvm(env, SPR_POWER_SPMC2, "SPMC2",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_SPMC2, 0x00000000);
    spr_register_kvm(env, SPR_TACR, "TACR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_TACR, 0x00000000);
    spr_register_kvm(env, SPR_TCSCR, "TCSCR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_TCSCR, 0x00000000);
    spr_register_kvm(env, SPR_CSIGR, "CSIGR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_CSIGR, 0x00000000);
}

static void register_power8_pmu_user_sprs(CPUPPCState *env)
{
    spr_register(env, SPR_POWER_UMMCR2, "UMMCR2",
                 &spr_read_MMCR2_ureg, &spr_write_MMCR2_ureg,
                 &spr_read_ureg, &spr_write_ureg,
                 0x00000000);
    spr_register(env, SPR_POWER_USIER, "USIER",
                 &spr_read_generic, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

static void register_power5p_ear_sprs(CPUPPCState *env)
{
    /* External access control */
    spr_register(env, SPR_EAR, "EAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

static void register_power5p_tb_sprs(CPUPPCState *env)
{
    /* TBU40 (High 40 bits of the Timebase register */
    spr_register_hv(env, SPR_TBU40, "TBU40",
                    SPR_NOACCESS, SPR_NOACCESS,
                    SPR_NOACCESS, SPR_NOACCESS,
                    SPR_NOACCESS, &spr_write_tbu40,
                    0x00000000);
}

static void register_970_lpar_sprs(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    /*
     * PPC970: HID4 covers things later controlled by the LPCR and
     * RMOR in later CPUs, but with a different encoding.  We only
     * support the 970 in "Apple mode" which has all hypervisor
     * facilities disabled by strapping, so we can basically just
     * ignore it
     */
    spr_register(env, SPR_970_HID4, "HID4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
#endif
}

static void register_power5p_lpar_sprs(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    /* Logical partitionning */
    spr_register_kvm_hv(env, SPR_LPCR, "LPCR",
                        SPR_NOACCESS, SPR_NOACCESS,
                        SPR_NOACCESS, SPR_NOACCESS,
                        &spr_read_generic, &spr_write_lpcr,
                        KVM_REG_PPC_LPCR, LPCR_LPES0 | LPCR_LPES1);
    spr_register_hv(env, SPR_HDEC, "HDEC",
                    SPR_NOACCESS, SPR_NOACCESS,
                    SPR_NOACCESS, SPR_NOACCESS,
                    &spr_read_hdecr, &spr_write_hdecr, 0);
#endif
}

static void register_book3s_ids_sprs(CPUPPCState *env)
{
    /* FIXME: Will need to deal with thread vs core only SPRs */

    /* Processor identification */
    spr_register_hv(env, SPR_PIR, "PIR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 &spr_read_generic, NULL,
                 0x00000000);
    spr_register_hv(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register_hv(env, SPR_TSCR, "TSCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register_hv(env, SPR_HMER, "HMER",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_hmer,
                 0x00000000);
    spr_register_hv(env, SPR_HMEER, "HMEER",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register_hv(env, SPR_TFMR, "TFMR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register_hv(env, SPR_LPIDR, "LPIDR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_lpidr,
                 0x00000000);
    spr_register_hv(env, SPR_HFSCR, "HFSCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register_hv(env, SPR_MMCRC, "MMCRC",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register_hv(env, SPR_MMCRH, "MMCRH",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register_hv(env, SPR_HSPRG0, "HSPRG0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register_hv(env, SPR_HSPRG1, "HSPRG1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register_hv(env, SPR_HSRR0, "HSRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register_hv(env, SPR_HSRR1, "HSRR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register_hv(env, SPR_HDAR, "HDAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register_hv(env, SPR_HDSISR, "HDSISR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register_hv(env, SPR_HRMOR, "HRMOR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

static void register_rmor_sprs(CPUPPCState *env)
{
    spr_register_hv(env, SPR_RMOR, "RMOR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

static void register_power8_ids_sprs(CPUPPCState *env)
{
    /* Thread identification */
    spr_register(env, SPR_TIR, "TIR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
}

static void register_book3s_purr_sprs(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    /* PURR & SPURR: Hack - treat these as aliases for the TB for now */
    spr_register_kvm_hv(env, SPR_PURR,   "PURR",
                        &spr_read_purr, SPR_NOACCESS,
                        &spr_read_purr, SPR_NOACCESS,
                        &spr_read_purr, &spr_write_purr,
                        KVM_REG_PPC_PURR, 0x00000000);
    spr_register_kvm_hv(env, SPR_SPURR,   "SPURR",
                        &spr_read_purr, SPR_NOACCESS,
                        &spr_read_purr, SPR_NOACCESS,
                        &spr_read_purr, &spr_write_purr,
                        KVM_REG_PPC_SPURR, 0x00000000);
#endif
}

static void register_power6_dbg_sprs(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    spr_register(env, SPR_CFAR, "SPR_CFAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_cfar, &spr_write_cfar,
                 0x00000000);
#endif
}

static void register_power5p_common_sprs(CPUPPCState *env)
{
    spr_register_kvm(env, SPR_PPR, "PPR",
                     &spr_read_generic, &spr_write_generic,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_PPR, 0x00000000);
}

static void register_power6_common_sprs(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    spr_register_kvm(env, SPR_DSCR, "SPR_DSCR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_DSCR, 0x00000000);
#endif
    /*
     * Register PCR to report POWERPC_EXCP_PRIV_REG instead of
     * POWERPC_EXCP_INVAL_SPR in userspace. Permit hypervisor access.
     */
    spr_register_hv(env, SPR_PCR, "PCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_pcr,
                 0x00000000);
}

static void register_power8_tce_address_control_sprs(CPUPPCState *env)
{
    spr_register_kvm(env, SPR_TAR, "TAR",
                     &spr_read_tar, &spr_write_tar,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_TAR, 0x00000000);
}

static void register_power8_tm_sprs(CPUPPCState *env)
{
    spr_register_kvm(env, SPR_TFHAR, "TFHAR",
                     &spr_read_tm, &spr_write_tm,
                     &spr_read_tm, &spr_write_tm,
                     KVM_REG_PPC_TFHAR, 0x00000000);
    spr_register_kvm(env, SPR_TFIAR, "TFIAR",
                     &spr_read_tm, &spr_write_tm,
                     &spr_read_tm, &spr_write_tm,
                     KVM_REG_PPC_TFIAR, 0x00000000);
    spr_register_kvm(env, SPR_TEXASR, "TEXASR",
                     &spr_read_tm, &spr_write_tm,
                     &spr_read_tm, &spr_write_tm,
                     KVM_REG_PPC_TEXASR, 0x00000000);
    spr_register(env, SPR_TEXASRU, "TEXASRU",
                 &spr_read_tm_upper32, &spr_write_tm_upper32,
                 &spr_read_tm_upper32, &spr_write_tm_upper32,
                 0x00000000);
}

static void register_power8_ebb_sprs(CPUPPCState *env)
{
    spr_register(env, SPR_BESCRS, "BESCRS",
                 &spr_read_ebb, &spr_write_ebb,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BESCRSU, "BESCRSU",
                 &spr_read_ebb_upper32, &spr_write_ebb_upper32,
                 &spr_read_prev_upper32, &spr_write_prev_upper32,
                 0x00000000);
    spr_register(env, SPR_BESCRR, "BESCRR",
                 &spr_read_ebb, &spr_write_ebb,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BESCRRU, "BESCRRU",
                 &spr_read_ebb_upper32, &spr_write_ebb_upper32,
                 &spr_read_prev_upper32, &spr_write_prev_upper32,
                 0x00000000);
    spr_register_kvm(env, SPR_EBBHR, "EBBHR",
                     &spr_read_ebb, &spr_write_ebb,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_EBBHR, 0x00000000);
    spr_register_kvm(env, SPR_EBBRR, "EBBRR",
                     &spr_read_ebb, &spr_write_ebb,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_EBBRR, 0x00000000);
    spr_register_kvm(env, SPR_BESCR, "BESCR",
                     &spr_read_ebb, &spr_write_ebb,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_BESCR, 0x00000000);
}

/* Virtual Time Base */
static void register_vtb_sprs(CPUPPCState *env)
{
    spr_register_kvm_hv(env, SPR_VTB, "VTB",
                        SPR_NOACCESS, SPR_NOACCESS,
                        &spr_read_vtb, SPR_NOACCESS,
                        &spr_read_vtb, &spr_write_vtb,
                        KVM_REG_PPC_VTB, 0x00000000);
}

static void register_power8_fscr_sprs(CPUPPCState *env)
{
#if defined(CONFIG_USER_ONLY)
    target_ulong initval = 1ULL << FSCR_TAR;
#else
    target_ulong initval = 0;
#endif
    spr_register_kvm(env, SPR_FSCR, "FSCR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_FSCR, initval);
}

static void register_power8_pspb_sprs(CPUPPCState *env)
{
    spr_register_kvm(env, SPR_PSPB, "PSPB",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic32,
                     KVM_REG_PPC_PSPB, 0);
}

static void register_power8_dpdes_sprs(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    /* Directed Privileged Door-bell Exception State, used for IPI */
    spr_register_kvm_hv(env, SPR_DPDES, "DPDES",
                        SPR_NOACCESS, SPR_NOACCESS,
                        &spr_read_dpdes, SPR_NOACCESS,
                        &spr_read_dpdes, &spr_write_dpdes,
                        KVM_REG_PPC_DPDES, 0x00000000);
#endif
}

static void register_power8_ic_sprs(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    spr_register_hv(env, SPR_IC, "IC",
                    SPR_NOACCESS, SPR_NOACCESS,
                    &spr_read_generic, SPR_NOACCESS,
                    &spr_read_generic, &spr_write_generic,
                    0);
#endif
}

static void register_power8_book4_sprs(CPUPPCState *env)
{
    /* Add a number of P8 book4 registers */
#if !defined(CONFIG_USER_ONLY)
    spr_register_kvm(env, SPR_ACOP, "ACOP",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_ACOP, 0);
    spr_register_kvm(env, SPR_BOOKS_PID, "PID",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_pidr,
                     KVM_REG_PPC_PID, 0);
    spr_register_kvm(env, SPR_WORT, "WORT",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_WORT, 0);
#endif
}

static void register_power7_book4_sprs(CPUPPCState *env)
{
    /* Add a number of P7 book4 registers */
#if !defined(CONFIG_USER_ONLY)
    spr_register_kvm(env, SPR_ACOP, "ACOP",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_ACOP, 0);
    spr_register_kvm(env, SPR_BOOKS_PID, "PID",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_PID, 0);
#endif
}

static void register_power8_rpr_sprs(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    spr_register_hv(env, SPR_RPR, "RPR",
                    SPR_NOACCESS, SPR_NOACCESS,
                    SPR_NOACCESS, SPR_NOACCESS,
                    &spr_read_generic, &spr_write_generic,
                    0x00000103070F1F3F);
#endif
}

static void register_power9_mmu_sprs(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    /* Partition Table Control */
    spr_register_kvm_hv(env, SPR_PTCR, "PTCR",
                        SPR_NOACCESS, SPR_NOACCESS,
                        SPR_NOACCESS, SPR_NOACCESS,
                        &spr_read_generic, &spr_write_ptcr,
                        KVM_REG_PPC_PTCR, 0x00000000);
    /* Address Segment Descriptor Register */
    spr_register_hv(env, SPR_ASDR, "ASDR",
                    SPR_NOACCESS, SPR_NOACCESS,
                    SPR_NOACCESS, SPR_NOACCESS,
                    &spr_read_generic, &spr_write_generic,
                    0x0000000000000000);
#endif
}

static void register_power10_hash_sprs(CPUPPCState *env)
{
    /*
     * it's the OS responsability to generate a random value for the registers
     * in each process' context. So, initialize it with 0 here.
     */
    uint64_t hashkeyr_initial_value = 0, hashpkeyr_initial_value = 0;
#if defined(CONFIG_USER_ONLY)
    /* in linux-user, setup the hash register with a random value */
    GRand *rand = g_rand_new();
    hashkeyr_initial_value =
        ((uint64_t)g_rand_int(rand) << 32) | (uint64_t)g_rand_int(rand);
    hashpkeyr_initial_value =
        ((uint64_t)g_rand_int(rand) << 32) | (uint64_t)g_rand_int(rand);
    g_rand_free(rand);
#endif
    spr_register(env, SPR_HASHKEYR, "HASHKEYR",
            SPR_NOACCESS, SPR_NOACCESS,
            &spr_read_generic, &spr_write_generic,
            hashkeyr_initial_value);
    spr_register_hv(env, SPR_HASHPKEYR, "HASHPKEYR",
            SPR_NOACCESS, SPR_NOACCESS,
            SPR_NOACCESS, SPR_NOACCESS,
            &spr_read_generic, &spr_write_generic,
            hashpkeyr_initial_value);
}

/*
 * Initialize PMU counter overflow timers for Power8 and
 * newer Power chips when using TCG.
 */
static void init_tcg_pmu_power8(CPUPPCState *env)
{
    /* Init PMU overflow timers */
    if (tcg_enabled()) {
        cpu_ppc_pmu_init(env);
    }
}

static void init_proc_book3s_common(CPUPPCState *env)
{
    register_non_embedded_sprs(env);
    register_book3s_altivec_sprs(env);
    register_book3s_pmu_sup_sprs(env);
    register_book3s_pmu_user_sprs(env);
    register_book3s_ctrl_sprs(env);
    /*
     * Can't find information on what this should be on reset.  This
     * value is the one used by 74xx processors.
     */
    vscr_init(env, 0x00010000);

    spr_register(env, SPR_USPRG3, "USPRG3",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
}

static void init_proc_970(CPUPPCState *env)
{
    /* Common Registers */
    init_proc_book3s_common(env);
    register_sdr1_sprs(env);
    register_book3s_dbg_sprs(env);

    /* 970 Specific Registers */
    register_970_hid_sprs(env);
    register_970_hior_sprs(env);
    register_low_BATs(env);
    register_970_pmu_sup_sprs(env);
    register_970_pmu_user_sprs(env);
    register_970_lpar_sprs(env);
    register_970_dbg_sprs(env);

    /* env variables */
    env->dcache_line_size = 128;
    env->icache_line_size = 128;

    /* Allocate hardware IRQ controller */
    init_excp_970(env);
    ppc970_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(970)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->desc = "PowerPC 970";
    pcc->init_proc = init_proc_970;
    pcc->check_pow = check_pow_970;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |
                       PPC_FLOAT_STFIWX |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_64B | PPC_ALTIVEC |
                       PPC_SEGMENT_64B | PPC_SLBI;
    pcc->insns_flags2 = PPC2_FP_CVT_S64 | PPC2_MEM_LWSYNC;
    pcc->msr_mask = (1ull << MSR_SF) |
                    (1ull << MSR_VR) |
                    (1ull << MSR_POW) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI);
    pcc->mmu_model = POWERPC_MMU_64B;
#if defined(CONFIG_SOFTMMU)
    pcc->hash64_opts = &ppc_hash64_opts_basic;
#endif
    pcc->excp_model = POWERPC_EXCP_970;
    pcc->bus_model = PPC_FLAGS_INPUT_970;
    pcc->bfd_mach = bfd_mach_ppc64;
    pcc->flags = POWERPC_FLAG_VRE | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_PMM |
                 POWERPC_FLAG_BUS_CLK;
    pcc->l1_dcache_size = 0x8000;
    pcc->l1_icache_size = 0x10000;
}

static void init_proc_power5plus(CPUPPCState *env)
{
    /* Common Registers */
    init_proc_book3s_common(env);
    register_sdr1_sprs(env);
    register_book3s_dbg_sprs(env);

    /* POWER5+ Specific Registers */
    register_970_hid_sprs(env);
    register_970_hior_sprs(env);
    register_low_BATs(env);
    register_970_pmu_sup_sprs(env);
    register_970_pmu_user_sprs(env);
    register_power5p_common_sprs(env);
    register_power5p_lpar_sprs(env);
    register_power5p_ear_sprs(env);
    register_power5p_tb_sprs(env);

    /* env variables */
    env->dcache_line_size = 128;
    env->icache_line_size = 128;

    /* Allocate hardware IRQ controller */
    init_excp_970(env);
    ppc970_irq_init(env_archcpu(env));
}

POWERPC_FAMILY(POWER5P)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->fw_name = "PowerPC,POWER5";
    dc->desc = "POWER5+";
    pcc->init_proc = init_proc_power5plus;
    pcc->check_pow = check_pow_970;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |
                       PPC_FLOAT_STFIWX |
                       PPC_FLOAT_EXT |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_64B |
                       PPC_POPCNTB |
                       PPC_SEGMENT_64B | PPC_SLBI;
    pcc->insns_flags2 = PPC2_FP_CVT_S64 | PPC2_MEM_LWSYNC;
    pcc->msr_mask = (1ull << MSR_SF) |
                    (1ull << MSR_VR) |
                    (1ull << MSR_POW) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI);
    pcc->lpcr_mask = LPCR_RMLS | LPCR_ILE | LPCR_LPES0 | LPCR_LPES1 |
        LPCR_RMI | LPCR_HDICE;
    pcc->mmu_model = POWERPC_MMU_2_03;
#if defined(CONFIG_SOFTMMU)
    pcc->hash64_opts = &ppc_hash64_opts_basic;
    pcc->lrg_decr_bits = 32;
#endif
    pcc->excp_model = POWERPC_EXCP_970;
    pcc->bus_model = PPC_FLAGS_INPUT_970;
    pcc->bfd_mach = bfd_mach_ppc64;
    pcc->flags = POWERPC_FLAG_VRE | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_PMM |
                 POWERPC_FLAG_BUS_CLK;
    pcc->l1_dcache_size = 0x8000;
    pcc->l1_icache_size = 0x10000;
}

static void init_proc_POWER7(CPUPPCState *env)
{
    /* Common Registers */
    init_proc_book3s_common(env);
    register_sdr1_sprs(env);
    register_book3s_dbg_sprs(env);

    /* POWER7 Specific Registers */
    register_book3s_ids_sprs(env);
    register_rmor_sprs(env);
    register_amr_sprs(env);
    register_book3s_purr_sprs(env);
    register_power5p_common_sprs(env);
    register_power5p_lpar_sprs(env);
    register_power5p_ear_sprs(env);
    register_power5p_tb_sprs(env);
    register_power6_common_sprs(env);
    register_power6_dbg_sprs(env);
    register_power7_book4_sprs(env);

    /* env variables */
    env->dcache_line_size = 128;
    env->icache_line_size = 128;

    /* Allocate hardware IRQ controller */
    init_excp_POWER7(env);
    ppcPOWER7_irq_init(env_archcpu(env));
}

static bool ppc_pvr_match_power7(PowerPCCPUClass *pcc, uint32_t pvr, bool best)
{
    uint32_t base = pvr & CPU_POWERPC_POWER_SERVER_MASK;
    uint32_t pcc_base = pcc->pvr & CPU_POWERPC_POWER_SERVER_MASK;

    if (!best) {
        if (base == CPU_POWERPC_POWER7_BASE) {
            return true;
        }
        if (base == CPU_POWERPC_POWER7P_BASE) {
            return true;
        }
    }

    if (base != pcc_base) {
        return false;
    }

    return true;
}

POWERPC_FAMILY(POWER7)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->fw_name = "PowerPC,POWER7";
    dc->desc = "POWER7";
    pcc->pvr_match = ppc_pvr_match_power7;
    pcc->pcr_mask = PCR_VEC_DIS | PCR_VSX_DIS | PCR_COMPAT_2_05;
    pcc->pcr_supported = PCR_COMPAT_2_06 | PCR_COMPAT_2_05;
    pcc->init_proc = init_proc_POWER7;
    pcc->check_pow = check_pow_nocheck;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_ISEL | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |
                       PPC_FLOAT_FRSQRTES |
                       PPC_FLOAT_STFIWX |
                       PPC_FLOAT_EXT |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_64B | PPC_64H | PPC_64BX | PPC_ALTIVEC |
                       PPC_SEGMENT_64B | PPC_SLBI |
                       PPC_POPCNTB | PPC_POPCNTWD |
                       PPC_CILDST;
    pcc->insns_flags2 = PPC2_VSX | PPC2_DFP | PPC2_DBRX | PPC2_ISA205 |
                        PPC2_PERM_ISA206 | PPC2_DIVE_ISA206 |
                        PPC2_ATOMIC_ISA206 | PPC2_FP_CVT_ISA206 |
                        PPC2_FP_TST_ISA206 | PPC2_FP_CVT_S64 |
                        PPC2_PM_ISA206 | PPC2_MEM_LWSYNC | PPC2_BCDA_ISA206;
    pcc->msr_mask = (1ull << MSR_SF) |
                    (1ull << MSR_VR) |
                    (1ull << MSR_VSX) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->lpcr_mask = LPCR_VPM0 | LPCR_VPM1 | LPCR_ISL | LPCR_DPFD |
        LPCR_VRMASD | LPCR_RMLS | LPCR_ILE |
        LPCR_P7_PECE0 | LPCR_P7_PECE1 | LPCR_P7_PECE2 |
        LPCR_MER | LPCR_TC |
        LPCR_LPES0 | LPCR_LPES1 | LPCR_HDICE;
    pcc->lpcr_pm = LPCR_P7_PECE0 | LPCR_P7_PECE1 | LPCR_P7_PECE2;
    pcc->mmu_model = POWERPC_MMU_2_06;
#if defined(CONFIG_SOFTMMU)
    pcc->hash64_opts = &ppc_hash64_opts_POWER7;
    pcc->lrg_decr_bits = 32;
#endif
    pcc->excp_model = POWERPC_EXCP_POWER7;
    pcc->bus_model = PPC_FLAGS_INPUT_POWER7;
    pcc->bfd_mach = bfd_mach_ppc64;
    pcc->flags = POWERPC_FLAG_VRE | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_PMM |
                 POWERPC_FLAG_BUS_CLK | POWERPC_FLAG_CFAR |
                 POWERPC_FLAG_VSX;
    pcc->l1_dcache_size = 0x8000;
    pcc->l1_icache_size = 0x8000;
}

static void init_proc_POWER8(CPUPPCState *env)
{
    /* Common Registers */
    init_proc_book3s_common(env);
    register_sdr1_sprs(env);
    register_book3s_207_dbg_sprs(env);

    /* Common TCG PMU */
    init_tcg_pmu_power8(env);

    /* POWER8 Specific Registers */
    register_book3s_ids_sprs(env);
    register_rmor_sprs(env);
    register_amr_sprs(env);
    register_iamr_sprs(env);
    register_book3s_purr_sprs(env);
    register_power5p_common_sprs(env);
    register_power5p_lpar_sprs(env);
    register_power5p_ear_sprs(env);
    register_power5p_tb_sprs(env);
    register_power6_common_sprs(env);
    register_power6_dbg_sprs(env);
    register_power8_tce_address_control_sprs(env);
    register_power8_ids_sprs(env);
    register_power8_ebb_sprs(env);
    register_power8_fscr_sprs(env);
    register_power8_pmu_sup_sprs(env);
    register_power8_pmu_user_sprs(env);
    register_power8_tm_sprs(env);
    register_power8_pspb_sprs(env);
    register_power8_dpdes_sprs(env);
    register_vtb_sprs(env);
    register_power8_ic_sprs(env);
    register_power8_book4_sprs(env);
    register_power8_rpr_sprs(env);

    /* env variables */
    env->dcache_line_size = 128;
    env->icache_line_size = 128;

    /* Allocate hardware IRQ controller */
    init_excp_POWER8(env);
    ppcPOWER7_irq_init(env_archcpu(env));
}

static bool ppc_pvr_match_power8(PowerPCCPUClass *pcc, uint32_t pvr, bool best)
{
    uint32_t base = pvr & CPU_POWERPC_POWER_SERVER_MASK;
    uint32_t pcc_base = pcc->pvr & CPU_POWERPC_POWER_SERVER_MASK;

    if (!best) {
        if (base == CPU_POWERPC_POWER8_BASE) {
            return true;
        }
        if (base == CPU_POWERPC_POWER8E_BASE) {
            return true;
        }
        if (base == CPU_POWERPC_POWER8NVL_BASE) {
            return true;
        }
    }
    if (base != pcc_base) {
        return false;
    }

    return true;
}

POWERPC_FAMILY(POWER8)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->fw_name = "PowerPC,POWER8";
    dc->desc = "POWER8";
    pcc->pvr_match = ppc_pvr_match_power8;
    pcc->pcr_mask = PCR_TM_DIS | PCR_COMPAT_2_06 | PCR_COMPAT_2_05;
    pcc->pcr_supported = PCR_COMPAT_2_07 | PCR_COMPAT_2_06 | PCR_COMPAT_2_05;
    pcc->init_proc = init_proc_POWER8;
    pcc->check_pow = check_pow_nocheck;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_ISEL | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |
                       PPC_FLOAT_FRSQRTES |
                       PPC_FLOAT_STFIWX |
                       PPC_FLOAT_EXT |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_64B | PPC_64H | PPC_64BX | PPC_ALTIVEC |
                       PPC_SEGMENT_64B | PPC_SLBI |
                       PPC_POPCNTB | PPC_POPCNTWD |
                       PPC_CILDST;
    pcc->insns_flags2 = PPC2_VSX | PPC2_VSX207 | PPC2_DFP | PPC2_DBRX |
                        PPC2_PERM_ISA206 | PPC2_DIVE_ISA206 |
                        PPC2_ATOMIC_ISA206 | PPC2_FP_CVT_ISA206 |
                        PPC2_FP_TST_ISA206 | PPC2_BCTAR_ISA207 |
                        PPC2_LSQ_ISA207 | PPC2_ALTIVEC_207 |
                        PPC2_ISA205 | PPC2_ISA207S | PPC2_FP_CVT_S64 |
                        PPC2_TM | PPC2_PM_ISA206 | PPC2_MEM_LWSYNC |
                        PPC2_BCDA_ISA206;
    pcc->msr_mask = (1ull << MSR_SF) |
                    (1ull << MSR_HV) |
                    (1ull << MSR_TM) |
                    (1ull << MSR_VR) |
                    (1ull << MSR_VSX) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_TS0) |
                    (1ull << MSR_TS1) |
                    (1ull << MSR_LE);
    pcc->lpcr_mask = LPCR_VPM0 | LPCR_VPM1 | LPCR_ISL | LPCR_KBV |
        LPCR_DPFD | LPCR_VRMASD | LPCR_RMLS | LPCR_ILE |
        LPCR_AIL | LPCR_ONL | LPCR_P8_PECE0 | LPCR_P8_PECE1 |
        LPCR_P8_PECE2 | LPCR_P8_PECE3 | LPCR_P8_PECE4 |
        LPCR_MER | LPCR_TC | LPCR_LPES0 | LPCR_HDICE;
    pcc->lpcr_pm = LPCR_P8_PECE0 | LPCR_P8_PECE1 | LPCR_P8_PECE2 |
                   LPCR_P8_PECE3 | LPCR_P8_PECE4;
    pcc->mmu_model = POWERPC_MMU_2_07;
#if defined(CONFIG_SOFTMMU)
    pcc->hash64_opts = &ppc_hash64_opts_POWER7;
    pcc->lrg_decr_bits = 32;
    pcc->n_host_threads = 8;
#endif
    pcc->excp_model = POWERPC_EXCP_POWER8;
    pcc->bus_model = PPC_FLAGS_INPUT_POWER7;
    pcc->bfd_mach = bfd_mach_ppc64;
    pcc->flags = POWERPC_FLAG_VRE | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_PMM |
                 POWERPC_FLAG_BUS_CLK | POWERPC_FLAG_CFAR |
                 POWERPC_FLAG_VSX | POWERPC_FLAG_TM;
    pcc->l1_dcache_size = 0x8000;
    pcc->l1_icache_size = 0x8000;
}

#ifdef CONFIG_SOFTMMU
/*
 * Radix pg sizes and AP encodings for dt node ibm,processor-radix-AP-encodings
 * Encoded as array of int_32s in the form:
 *  0bxxxyyyyyyyyyyyyyyyyyyyyyyyyyyyyy
 *  x -> AP encoding
 *  y -> radix mode supported page size (encoded as a shift)
 */
static struct ppc_radix_page_info POWER9_radix_page_info = {
    .count = 4,
    .entries = {
        0x0000000c, /*  4K - enc: 0x0 */
        0xa0000010, /* 64K - enc: 0x5 */
        0x20000015, /*  2M - enc: 0x1 */
        0x4000001e  /*  1G - enc: 0x2 */
    }
};
#endif /* CONFIG_SOFTMMU */

static void init_proc_POWER9(CPUPPCState *env)
{
    /* Common Registers */
    init_proc_book3s_common(env);
    register_book3s_207_dbg_sprs(env);

    /* Common TCG PMU */
    init_tcg_pmu_power8(env);

    /* POWER8 Specific Registers */
    register_book3s_ids_sprs(env);
    register_amr_sprs(env);
    register_iamr_sprs(env);
    register_book3s_purr_sprs(env);
    register_power5p_common_sprs(env);
    register_power5p_lpar_sprs(env);
    register_power5p_ear_sprs(env);
    register_power5p_tb_sprs(env);
    register_power6_common_sprs(env);
    register_power6_dbg_sprs(env);
    register_power8_tce_address_control_sprs(env);
    register_power8_ids_sprs(env);
    register_power8_ebb_sprs(env);
    register_power8_fscr_sprs(env);
    register_power8_pmu_sup_sprs(env);
    register_power8_pmu_user_sprs(env);
    register_power8_tm_sprs(env);
    register_power8_pspb_sprs(env);
    register_power8_dpdes_sprs(env);
    register_vtb_sprs(env);
    register_power8_ic_sprs(env);
    register_power8_book4_sprs(env);
    register_power8_rpr_sprs(env);
    register_power9_mmu_sprs(env);

    /* POWER9 Specific registers */
    spr_register_kvm(env, SPR_TIDR, "TIDR", NULL, NULL,
                     spr_read_generic, spr_write_generic,
                     KVM_REG_PPC_TIDR, 0);

    /* FIXME: Filter fields properly based on privilege level */
    spr_register_kvm_hv(env, SPR_PSSCR, "PSSCR", NULL, NULL, NULL, NULL,
                        spr_read_generic, spr_write_generic,
                        KVM_REG_PPC_PSSCR, 0);

    /* env variables */
    env->dcache_line_size = 128;
    env->icache_line_size = 128;

    /* Allocate hardware IRQ controller */
    init_excp_POWER9(env);
    ppcPOWER9_irq_init(env_archcpu(env));
}

static bool ppc_pvr_match_power9(PowerPCCPUClass *pcc, uint32_t pvr, bool best)
{
    uint32_t base = pvr & CPU_POWERPC_POWER_SERVER_MASK;
    uint32_t pcc_base = pcc->pvr & CPU_POWERPC_POWER_SERVER_MASK;

    if (!best) {
        if (base == CPU_POWERPC_POWER9_BASE) {
            return true;
        }
    }

    if (base != pcc_base) {
        return false;
    }

    if ((pvr & 0x0f00) == (pcc->pvr & 0x0f00)) {
        /* Major DD version matches to power9_v1.0 and power9_v2.0 */
        return true;
    }

    return false;
}

POWERPC_FAMILY(POWER9)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->fw_name = "PowerPC,POWER9";
    dc->desc = "POWER9";
    pcc->pvr_match = ppc_pvr_match_power9;
    pcc->pcr_mask = PCR_COMPAT_2_05 | PCR_COMPAT_2_06 | PCR_COMPAT_2_07;
    pcc->pcr_supported = PCR_COMPAT_3_00 | PCR_COMPAT_2_07 | PCR_COMPAT_2_06 |
                         PCR_COMPAT_2_05;
    pcc->init_proc = init_proc_POWER9;
    pcc->check_pow = check_pow_nocheck;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_ISEL | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |
                       PPC_FLOAT_FRSQRTES |
                       PPC_FLOAT_STFIWX |
                       PPC_FLOAT_EXT |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_64B | PPC_64H | PPC_64BX | PPC_ALTIVEC |
                       PPC_SEGMENT_64B | PPC_SLBI |
                       PPC_POPCNTB | PPC_POPCNTWD |
                       PPC_CILDST;
    pcc->insns_flags2 = PPC2_VSX | PPC2_VSX207 | PPC2_DFP | PPC2_DBRX |
                        PPC2_PERM_ISA206 | PPC2_DIVE_ISA206 |
                        PPC2_ATOMIC_ISA206 | PPC2_FP_CVT_ISA206 |
                        PPC2_FP_TST_ISA206 | PPC2_BCTAR_ISA207 |
                        PPC2_LSQ_ISA207 | PPC2_ALTIVEC_207 |
                        PPC2_ISA205 | PPC2_ISA207S | PPC2_FP_CVT_S64 |
                        PPC2_TM | PPC2_ISA300 | PPC2_PRCNTL | PPC2_MEM_LWSYNC |
                        PPC2_BCDA_ISA206;
    pcc->msr_mask = (1ull << MSR_SF) |
                    (1ull << MSR_HV) |
                    (1ull << MSR_TM) |
                    (1ull << MSR_VR) |
                    (1ull << MSR_VSX) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->lpcr_mask = LPCR_VPM1 | LPCR_ISL | LPCR_KBV | LPCR_DPFD |
        (LPCR_PECE_U_MASK & LPCR_HVEE) | LPCR_ILE | LPCR_AIL |
        LPCR_UPRT | LPCR_EVIRT | LPCR_ONL | LPCR_HR | LPCR_LD |
        (LPCR_PECE_L_MASK & (LPCR_PDEE | LPCR_HDEE | LPCR_EEE |
                             LPCR_DEE | LPCR_OEE))
        | LPCR_MER | LPCR_GTSE | LPCR_TC |
        LPCR_HEIC | LPCR_LPES0 | LPCR_HVICE | LPCR_HDICE;
    pcc->lpcr_pm = LPCR_PDEE | LPCR_HDEE | LPCR_EEE | LPCR_DEE | LPCR_OEE;
    pcc->mmu_model = POWERPC_MMU_3_00;
#if defined(CONFIG_SOFTMMU)
    /* segment page size remain the same */
    pcc->hash64_opts = &ppc_hash64_opts_POWER7;
    pcc->radix_page_info = &POWER9_radix_page_info;
    pcc->lrg_decr_bits = 56;
    pcc->n_host_threads = 4;
#endif
    pcc->excp_model = POWERPC_EXCP_POWER9;
    pcc->bus_model = PPC_FLAGS_INPUT_POWER9;
    pcc->bfd_mach = bfd_mach_ppc64;
    pcc->flags = POWERPC_FLAG_VRE | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_PMM |
                 POWERPC_FLAG_BUS_CLK | POWERPC_FLAG_CFAR |
                 POWERPC_FLAG_VSX | POWERPC_FLAG_TM | POWERPC_FLAG_SCV;
    pcc->l1_dcache_size = 0x8000;
    pcc->l1_icache_size = 0x8000;
}

#ifdef CONFIG_SOFTMMU
/*
 * Radix pg sizes and AP encodings for dt node ibm,processor-radix-AP-encodings
 * Encoded as array of int_32s in the form:
 *  0bxxxyyyyyyyyyyyyyyyyyyyyyyyyyyyyy
 *  x -> AP encoding
 *  y -> radix mode supported page size (encoded as a shift)
 */
static struct ppc_radix_page_info POWER10_radix_page_info = {
    .count = 4,
    .entries = {
        0x0000000c, /*  4K - enc: 0x0 */
        0xa0000010, /* 64K - enc: 0x5 */
        0x20000015, /*  2M - enc: 0x1 */
        0x4000001e  /*  1G - enc: 0x2 */
    }
};
#endif /* CONFIG_SOFTMMU */

static void init_proc_POWER10(CPUPPCState *env)
{
    /* Common Registers */
    init_proc_book3s_common(env);
    register_book3s_207_dbg_sprs(env);

    /* Common TCG PMU */
    init_tcg_pmu_power8(env);

    /* POWER8 Specific Registers */
    register_book3s_ids_sprs(env);
    register_amr_sprs(env);
    register_iamr_sprs(env);
    register_book3s_purr_sprs(env);
    register_power5p_common_sprs(env);
    register_power5p_lpar_sprs(env);
    register_power5p_ear_sprs(env);
    register_power5p_tb_sprs(env);
    register_power6_common_sprs(env);
    register_power6_dbg_sprs(env);
    register_power8_tce_address_control_sprs(env);
    register_power8_ids_sprs(env);
    register_power8_ebb_sprs(env);
    register_power8_fscr_sprs(env);
    register_power8_pmu_sup_sprs(env);
    register_power8_pmu_user_sprs(env);
    register_power8_tm_sprs(env);
    register_power8_pspb_sprs(env);
    register_power8_dpdes_sprs(env);
    register_vtb_sprs(env);
    register_power8_ic_sprs(env);
    register_power8_book4_sprs(env);
    register_power8_rpr_sprs(env);
    register_power9_mmu_sprs(env);
    register_power10_hash_sprs(env);

    /* FIXME: Filter fields properly based on privilege level */
    spr_register_kvm_hv(env, SPR_PSSCR, "PSSCR", NULL, NULL, NULL, NULL,
                        spr_read_generic, spr_write_generic,
                        KVM_REG_PPC_PSSCR, 0);

    /* env variables */
    env->dcache_line_size = 128;
    env->icache_line_size = 128;

    /* Allocate hardware IRQ controller */
    init_excp_POWER10(env);
    ppcPOWER9_irq_init(env_archcpu(env));
}

static bool ppc_pvr_match_power10(PowerPCCPUClass *pcc, uint32_t pvr, bool best)
{
    uint32_t base = pvr & CPU_POWERPC_POWER_SERVER_MASK;
    uint32_t pcc_base = pcc->pvr & CPU_POWERPC_POWER_SERVER_MASK;

    if (!best) {
        if (base == CPU_POWERPC_POWER10_BASE) {
            return true;
        }
    }

    if (base != pcc_base) {
        return false;
    }

    if ((pvr & 0x0f00) == (pcc->pvr & 0x0f00)) {
        /* Major DD version matches to power10_v1.0 and power10_v2.0 */
        return true;
    }

    return false;
}

POWERPC_FAMILY(POWER10)(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);

    dc->fw_name = "PowerPC,POWER10";
    dc->desc = "POWER10";
    pcc->pvr_match = ppc_pvr_match_power10;
    pcc->pcr_mask = PCR_COMPAT_2_05 | PCR_COMPAT_2_06 | PCR_COMPAT_2_07 |
                    PCR_COMPAT_3_00;
    pcc->pcr_supported = PCR_COMPAT_3_10 | PCR_COMPAT_3_00 | PCR_COMPAT_2_07 |
                         PCR_COMPAT_2_06 | PCR_COMPAT_2_05;
    pcc->init_proc = init_proc_POWER10;
    pcc->check_pow = check_pow_nocheck;
    pcc->insns_flags = PPC_INSNS_BASE | PPC_ISEL | PPC_STRING | PPC_MFTB |
                       PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |
                       PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |
                       PPC_FLOAT_FRSQRTES |
                       PPC_FLOAT_STFIWX |
                       PPC_FLOAT_EXT |
                       PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |
                       PPC_MEM_SYNC | PPC_MEM_EIEIO |
                       PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |
                       PPC_64B | PPC_64H | PPC_64BX | PPC_ALTIVEC |
                       PPC_SEGMENT_64B | PPC_SLBI |
                       PPC_POPCNTB | PPC_POPCNTWD |
                       PPC_CILDST;
    pcc->insns_flags2 = PPC2_VSX | PPC2_VSX207 | PPC2_DFP | PPC2_DBRX |
                        PPC2_PERM_ISA206 | PPC2_DIVE_ISA206 |
                        PPC2_ATOMIC_ISA206 | PPC2_FP_CVT_ISA206 |
                        PPC2_FP_TST_ISA206 | PPC2_BCTAR_ISA207 |
                        PPC2_LSQ_ISA207 | PPC2_ALTIVEC_207 |
                        PPC2_ISA205 | PPC2_ISA207S | PPC2_FP_CVT_S64 |
                        PPC2_TM | PPC2_ISA300 | PPC2_PRCNTL | PPC2_ISA310 |
                        PPC2_MEM_LWSYNC | PPC2_BCDA_ISA206;
    pcc->msr_mask = (1ull << MSR_SF) |
                    (1ull << MSR_HV) |
                    (1ull << MSR_TM) |
                    (1ull << MSR_VR) |
                    (1ull << MSR_VSX) |
                    (1ull << MSR_EE) |
                    (1ull << MSR_PR) |
                    (1ull << MSR_FP) |
                    (1ull << MSR_ME) |
                    (1ull << MSR_FE0) |
                    (1ull << MSR_SE) |
                    (1ull << MSR_DE) |
                    (1ull << MSR_FE1) |
                    (1ull << MSR_IR) |
                    (1ull << MSR_DR) |
                    (1ull << MSR_PMM) |
                    (1ull << MSR_RI) |
                    (1ull << MSR_LE);
    pcc->lpcr_mask = LPCR_VPM1 | LPCR_ISL | LPCR_KBV | LPCR_DPFD |
        (LPCR_PECE_U_MASK & LPCR_HVEE) | LPCR_ILE | LPCR_AIL |
        LPCR_UPRT | LPCR_EVIRT | LPCR_ONL | LPCR_HR | LPCR_LD |
        (LPCR_PECE_L_MASK & (LPCR_PDEE | LPCR_HDEE | LPCR_EEE |
                             LPCR_DEE | LPCR_OEE))
        | LPCR_MER | LPCR_GTSE | LPCR_TC |
        LPCR_HEIC | LPCR_LPES0 | LPCR_HVICE | LPCR_HDICE;
    /* DD2 adds an extra HAIL bit */
    pcc->lpcr_mask |= LPCR_HAIL;

    pcc->lpcr_pm = LPCR_PDEE | LPCR_HDEE | LPCR_EEE | LPCR_DEE | LPCR_OEE;
    pcc->mmu_model = POWERPC_MMU_3_00;
#if defined(CONFIG_SOFTMMU)
    /* segment page size remain the same */
    pcc->hash64_opts = &ppc_hash64_opts_POWER7;
    pcc->radix_page_info = &POWER10_radix_page_info;
    pcc->lrg_decr_bits = 56;
#endif
    pcc->excp_model = POWERPC_EXCP_POWER10;
    pcc->bus_model = PPC_FLAGS_INPUT_POWER9;
    pcc->bfd_mach = bfd_mach_ppc64;
    pcc->flags = POWERPC_FLAG_VRE | POWERPC_FLAG_SE |
                 POWERPC_FLAG_BE | POWERPC_FLAG_PMM |
                 POWERPC_FLAG_BUS_CLK | POWERPC_FLAG_CFAR |
                 POWERPC_FLAG_VSX | POWERPC_FLAG_TM | POWERPC_FLAG_SCV;
    pcc->l1_dcache_size = 0x8000;
    pcc->l1_icache_size = 0x8000;
}

#if !defined(CONFIG_USER_ONLY)
void cpu_ppc_set_vhyp(PowerPCCPU *cpu, PPCVirtualHypervisor *vhyp)
{
    CPUPPCState *env = &cpu->env;

    cpu->vhyp = vhyp;

    /*
     * With a virtual hypervisor mode we never allow the CPU to go
     * hypervisor mode itself
     */
    env->msr_mask &= ~MSR_HVB;
}

#endif /* !defined(CONFIG_USER_ONLY) */

#endif /* defined(TARGET_PPC64) */

/*****************************************************************************/
/* Generic CPU instantiation routine                                         */
static void init_ppc_proc(PowerPCCPU *cpu)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    CPUPPCState *env = &cpu->env;
#if !defined(CONFIG_USER_ONLY)
    int i;

    /* Set all exception vectors to an invalid address */
    for (i = 0; i < POWERPC_EXCP_NB; i++) {
        env->excp_vectors[i] = (target_ulong)(-1ULL);
    }
    env->ivor_mask = 0x00000000;
    env->ivpr_mask = 0x00000000;
    /* Default MMU definitions */
    env->nb_BATs = 0;
    env->nb_tlb = 0;
    env->nb_ways = 0;
    env->tlb_type = TLB_NONE;
#endif
    /* Register SPR common to all PowerPC implementations */
    register_generic_sprs(cpu);

    /* PowerPC implementation specific initialisations (SPRs, timers, ...) */
    (*pcc->init_proc)(env);

#if !defined(CONFIG_USER_ONLY)
    ppc_gdb_gen_spr_xml(cpu);
#endif

    /* MSR bits & flags consistency checks */
    if (env->msr_mask & (1 << 25)) {
        switch (env->flags & (POWERPC_FLAG_SPE | POWERPC_FLAG_VRE)) {
        case POWERPC_FLAG_SPE:
        case POWERPC_FLAG_VRE:
            break;
        default:
            fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                    "Should define POWERPC_FLAG_SPE or POWERPC_FLAG_VRE\n");
            exit(1);
        }
    } else if (env->flags & (POWERPC_FLAG_SPE | POWERPC_FLAG_VRE)) {
        fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                "Should not define POWERPC_FLAG_SPE nor POWERPC_FLAG_VRE\n");
        exit(1);
    }
    if (env->msr_mask & (1 << 17)) {
        switch (env->flags & (POWERPC_FLAG_TGPR | POWERPC_FLAG_CE)) {
        case POWERPC_FLAG_TGPR:
        case POWERPC_FLAG_CE:
            break;
        default:
            fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                    "Should define POWERPC_FLAG_TGPR or POWERPC_FLAG_CE\n");
            exit(1);
        }
    } else if (env->flags & (POWERPC_FLAG_TGPR | POWERPC_FLAG_CE)) {
        fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                "Should not define POWERPC_FLAG_TGPR nor POWERPC_FLAG_CE\n");
        exit(1);
    }
    if (env->msr_mask & (1 << 10)) {
        switch (env->flags & (POWERPC_FLAG_SE | POWERPC_FLAG_DWE |
                              POWERPC_FLAG_UBLE)) {
        case POWERPC_FLAG_SE:
        case POWERPC_FLAG_DWE:
        case POWERPC_FLAG_UBLE:
            break;
        default:
            fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                    "Should define POWERPC_FLAG_SE or POWERPC_FLAG_DWE or "
                    "POWERPC_FLAG_UBLE\n");
            exit(1);
        }
    } else if (env->flags & (POWERPC_FLAG_SE | POWERPC_FLAG_DWE |
                             POWERPC_FLAG_UBLE)) {
        fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                "Should not define POWERPC_FLAG_SE nor POWERPC_FLAG_DWE nor "
                "POWERPC_FLAG_UBLE\n");
            exit(1);
    }
    if (env->msr_mask & (1 << 9)) {
        switch (env->flags & (POWERPC_FLAG_BE | POWERPC_FLAG_DE)) {
        case POWERPC_FLAG_BE:
        case POWERPC_FLAG_DE:
            break;
        default:
            fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                    "Should define POWERPC_FLAG_BE or POWERPC_FLAG_DE\n");
            exit(1);
        }
    } else if (env->flags & (POWERPC_FLAG_BE | POWERPC_FLAG_DE)) {
        fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                "Should not define POWERPC_FLAG_BE nor POWERPC_FLAG_DE\n");
        exit(1);
    }
    if (env->msr_mask & (1 << 2)) {
        switch (env->flags & (POWERPC_FLAG_PX | POWERPC_FLAG_PMM)) {
        case POWERPC_FLAG_PX:
        case POWERPC_FLAG_PMM:
            break;
        default:
            fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                    "Should define POWERPC_FLAG_PX or POWERPC_FLAG_PMM\n");
            exit(1);
        }
    } else if (env->flags & (POWERPC_FLAG_PX | POWERPC_FLAG_PMM)) {
        fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                "Should not define POWERPC_FLAG_PX nor POWERPC_FLAG_PMM\n");
        exit(1);
    }
    if ((env->flags & POWERPC_FLAG_BUS_CLK) == 0) {
        fprintf(stderr, "PowerPC flags inconsistency\n"
                "Should define the time-base and decrementer clock source\n");
        exit(1);
    }
    /* Allocate TLBs buffer when needed */
#if !defined(CONFIG_USER_ONLY)
    if (env->nb_tlb != 0) {
        int nb_tlb = env->nb_tlb;
        if (env->id_tlbs != 0) {
            nb_tlb *= 2;
        }
        switch (env->tlb_type) {
        case TLB_6XX:
            env->tlb.tlb6 = g_new0(ppc6xx_tlb_t, nb_tlb);
            break;
        case TLB_EMB:
            env->tlb.tlbe = g_new0(ppcemb_tlb_t, nb_tlb);
            break;
        case TLB_MAS:
            env->tlb.tlbm = g_new0(ppcmas_tlb_t, nb_tlb);
            break;
        }
        /* Pre-compute some useful values */
        env->tlb_per_way = env->nb_tlb / env->nb_ways;
    }
#endif
    if (env->check_pow == NULL) {
        warn_report("no power management check handler registered."
                    " Attempt QEMU to crash very soon !");
    }
}


static void ppc_cpu_realize(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    PowerPCCPU *cpu = POWERPC_CPU(dev);
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }
    if (cpu->vcpu_id == UNASSIGNED_CPU_INDEX) {
        cpu->vcpu_id = cs->cpu_index;
    }

    if (tcg_enabled()) {
        if (ppc_fixup_cpu(cpu) != 0) {
            error_setg(errp, "Unable to emulate selected CPU with TCG");
            goto unrealize;
        }
    }

    create_ppc_opcodes(cpu, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        goto unrealize;
    }
    init_ppc_proc(cpu);

    ppc_gdb_init(cs, pcc);
    qemu_init_vcpu(cs);

    pcc->parent_realize(dev, errp);

    return;

unrealize:
    cpu_exec_unrealizefn(cs);
}

static void ppc_cpu_unrealize(DeviceState *dev)
{
    PowerPCCPU *cpu = POWERPC_CPU(dev);
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);

    pcc->parent_unrealize(dev);

    cpu_remove_sync(CPU(cpu));

    destroy_ppc_opcodes(cpu);
}

static gint ppc_cpu_compare_class_pvr(gconstpointer a, gconstpointer b)
{
    ObjectClass *oc = (ObjectClass *)a;
    uint32_t pvr = *(uint32_t *)b;
    PowerPCCPUClass *pcc = (PowerPCCPUClass *)a;

    /* -cpu host does a PVR lookup during construction */
    if (unlikely(strcmp(object_class_get_name(oc),
                        TYPE_HOST_POWERPC_CPU) == 0)) {
        return -1;
    }

    return pcc->pvr == pvr ? 0 : -1;
}

PowerPCCPUClass *ppc_cpu_class_by_pvr(uint32_t pvr)
{
    GSList *list, *item;
    PowerPCCPUClass *pcc = NULL;

    list = object_class_get_list(TYPE_POWERPC_CPU, false);
    item = g_slist_find_custom(list, &pvr, ppc_cpu_compare_class_pvr);
    if (item != NULL) {
        pcc = POWERPC_CPU_CLASS(item->data);
    }
    g_slist_free(list);

    return pcc;
}

static gint ppc_cpu_compare_class_pvr_mask(gconstpointer a, gconstpointer b)
{
    ObjectClass *oc = (ObjectClass *)a;
    uint32_t pvr = *(uint32_t *)b;
    PowerPCCPUClass *pcc = (PowerPCCPUClass *)a;

    /* -cpu host does a PVR lookup during construction */
    if (unlikely(strcmp(object_class_get_name(oc),
                        TYPE_HOST_POWERPC_CPU) == 0)) {
        return -1;
    }

    if (pcc->pvr_match(pcc, pvr, true)) {
        return 0;
    }

    return -1;
}

PowerPCCPUClass *ppc_cpu_class_by_pvr_mask(uint32_t pvr)
{
    GSList *list, *item;
    PowerPCCPUClass *pcc = NULL;

    list = object_class_get_list(TYPE_POWERPC_CPU, true);
    item = g_slist_find_custom(list, &pvr, ppc_cpu_compare_class_pvr_mask);
    if (item != NULL) {
        pcc = POWERPC_CPU_CLASS(item->data);
    }
    g_slist_free(list);

    return pcc;
}

static const char *ppc_cpu_lookup_alias(const char *alias)
{
    int ai;

    for (ai = 0; ppc_cpu_aliases[ai].alias != NULL; ai++) {
        if (strcmp(ppc_cpu_aliases[ai].alias, alias) == 0) {
            return ppc_cpu_aliases[ai].model;
        }
    }

    return NULL;
}

static ObjectClass *ppc_cpu_class_by_name(const char *name)
{
    char *cpu_model, *typename;
    ObjectClass *oc;
    const char *p;
    unsigned long pvr;

    /*
     * Lookup by PVR if cpu_model is valid 8 digit hex number (excl:
     * 0x prefix if present)
     */
    if (!qemu_strtoul(name, &p, 16, &pvr)) {
        int len = p - name;
        len = (len == 10) && (name[1] == 'x') ? len - 2 : len;
        if ((len == 8) && (*p == '\0')) {
            return OBJECT_CLASS(ppc_cpu_class_by_pvr(pvr));
        }
    }

    /*
     * All ppc CPUs represent hardware that exists in the real world, i.e.: we
     * do not have a "max" CPU with all possible emulated features enabled.
     * Return the default CPU type for the machine because that has greater
     * chance of being useful as the "max" CPU.
     */
#if !defined(CONFIG_USER_ONLY)
    if (strcmp(name, "max") == 0) {
        MachineClass *mc = MACHINE_GET_CLASS(qdev_get_machine());
        if (mc) {
            return object_class_by_name(mc->default_cpu_type);
        }
    }
#endif

    cpu_model = g_ascii_strdown(name, -1);
    p = ppc_cpu_lookup_alias(cpu_model);
    if (p) {
        g_free(cpu_model);
        cpu_model = g_strdup(p);
    }

    typename = g_strdup_printf("%s" POWERPC_CPU_TYPE_SUFFIX, cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    g_free(cpu_model);

    return oc;
}

PowerPCCPUClass *ppc_cpu_get_family_class(PowerPCCPUClass *pcc)
{
    ObjectClass *oc = OBJECT_CLASS(pcc);

    while (oc && !object_class_is_abstract(oc)) {
        oc = object_class_get_parent(oc);
    }
    assert(oc);

    return POWERPC_CPU_CLASS(oc);
}

/* Sort by PVR, ordering special case "host" last. */
static gint ppc_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *oc_a = (ObjectClass *)a;
    ObjectClass *oc_b = (ObjectClass *)b;
    PowerPCCPUClass *pcc_a = POWERPC_CPU_CLASS(oc_a);
    PowerPCCPUClass *pcc_b = POWERPC_CPU_CLASS(oc_b);
    const char *name_a = object_class_get_name(oc_a);
    const char *name_b = object_class_get_name(oc_b);

    if (strcmp(name_a, TYPE_HOST_POWERPC_CPU) == 0) {
        return 1;
    } else if (strcmp(name_b, TYPE_HOST_POWERPC_CPU) == 0) {
        return -1;
    } else {
        /* Avoid an integer overflow during subtraction */
        if (pcc_a->pvr < pcc_b->pvr) {
            return -1;
        } else if (pcc_a->pvr > pcc_b->pvr) {
            return 1;
        } else {
            return 0;
        }
    }
}

static void ppc_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);
    DeviceClass *family = DEVICE_CLASS(ppc_cpu_get_family_class(pcc));
    const char *typename = object_class_get_name(oc);
    char *name;
    int i;

    if (unlikely(strcmp(typename, TYPE_HOST_POWERPC_CPU) == 0)) {
        return;
    }

    name = g_strndup(typename,
                     strlen(typename) - strlen(POWERPC_CPU_TYPE_SUFFIX));
    qemu_printf("PowerPC %-16s PVR %08x\n", name, pcc->pvr);
    for (i = 0; ppc_cpu_aliases[i].alias != NULL; i++) {
        PowerPCCPUAlias *alias = &ppc_cpu_aliases[i];
        ObjectClass *alias_oc = ppc_cpu_class_by_name(alias->model);

        if (alias_oc != oc) {
            continue;
        }
        /*
         * If running with KVM, we might update the family alias later, so
         * avoid printing the wrong alias here and use "preferred" instead
         */
        if (strcmp(alias->alias, family->desc) == 0) {
            qemu_printf("PowerPC %-16s (alias for preferred %s CPU)\n",
                        alias->alias, family->desc);
        } else {
            qemu_printf("PowerPC %-16s (alias for %s)\n",
                        alias->alias, name);
        }
    }
    g_free(name);
}

void ppc_cpu_list(void)
{
    GSList *list;

    list = object_class_get_list(TYPE_POWERPC_CPU, false);
    list = g_slist_sort(list, ppc_cpu_list_compare);
    g_slist_foreach(list, ppc_cpu_list_entry, NULL);
    g_slist_free(list);

#ifdef CONFIG_KVM
    qemu_printf("\n");
    qemu_printf("PowerPC %s\n", "host");
#endif
}

static void ppc_cpu_defs_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CpuDefinitionInfoList **first = user_data;
    const char *typename;
    CpuDefinitionInfo *info;

    typename = object_class_get_name(oc);
    info = g_malloc0(sizeof(*info));
    info->name = g_strndup(typename,
                           strlen(typename) - strlen(POWERPC_CPU_TYPE_SUFFIX));

    QAPI_LIST_PREPEND(*first, info);
}

CpuDefinitionInfoList *qmp_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *cpu_list = NULL;
    GSList *list;
    int i;

    list = object_class_get_list(TYPE_POWERPC_CPU, false);
    g_slist_foreach(list, ppc_cpu_defs_entry, &cpu_list);
    g_slist_free(list);

    for (i = 0; ppc_cpu_aliases[i].alias != NULL; i++) {
        PowerPCCPUAlias *alias = &ppc_cpu_aliases[i];
        ObjectClass *oc;
        CpuDefinitionInfo *info;

        oc = ppc_cpu_class_by_name(alias->model);
        if (oc == NULL) {
            continue;
        }

        info = g_malloc0(sizeof(*info));
        info->name = g_strdup(alias->alias);
        info->q_typename = g_strdup(object_class_get_name(oc));

        QAPI_LIST_PREPEND(cpu_list, info);
    }

    return cpu_list;
}

static void ppc_cpu_set_pc(CPUState *cs, vaddr value)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);

    cpu->env.nip = value;
}

static vaddr ppc_cpu_get_pc(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);

    return cpu->env.nip;
}

#ifdef CONFIG_TCG
static void ppc_restore_state_to_opc(CPUState *cs,
                                     const TranslationBlock *tb,
                                     const uint64_t *data)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);

    cpu->env.nip = data[0];
}
#endif /* CONFIG_TCG */

static bool ppc_cpu_has_work(CPUState *cs)
{
    return cs->interrupt_request & CPU_INTERRUPT_HARD;
}

static void ppc_cpu_reset(DeviceState *dev)
{
    CPUState *s = CPU(dev);
    PowerPCCPU *cpu = POWERPC_CPU(s);
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    CPUPPCState *env = &cpu->env;
    target_ulong msr;
    int i;

    pcc->parent_reset(dev);

    msr = (target_ulong)0;
    msr |= (target_ulong)MSR_HVB;
    msr |= (target_ulong)1 << MSR_EP;
#if defined(DO_SINGLE_STEP) && 0
    /* Single step trace mode */
    msr |= (target_ulong)1 << MSR_SE;
    msr |= (target_ulong)1 << MSR_BE;
#endif
#if defined(CONFIG_USER_ONLY)
    msr |= (target_ulong)1 << MSR_FP; /* Allow floating point usage */
    msr |= (target_ulong)1 << MSR_FE0; /* Allow floating point exceptions */
    msr |= (target_ulong)1 << MSR_FE1;
    msr |= (target_ulong)1 << MSR_VR; /* Allow altivec usage */
    msr |= (target_ulong)1 << MSR_VSX; /* Allow VSX usage */
    msr |= (target_ulong)1 << MSR_SPE; /* Allow SPE usage */
    msr |= (target_ulong)1 << MSR_PR;
#if defined(TARGET_PPC64)
    msr |= (target_ulong)1 << MSR_TM; /* Transactional memory */
#endif
#if !TARGET_BIG_ENDIAN
    msr |= (target_ulong)1 << MSR_LE; /* Little-endian user mode */
    if (!((env->msr_mask >> MSR_LE) & 1)) {
        fprintf(stderr, "Selected CPU does not support little-endian.\n");
        exit(1);
    }
#endif
#endif

#if defined(TARGET_PPC64)
    if (mmu_is_64bit(env->mmu_model)) {
        msr |= (1ULL << MSR_SF);
    }
#endif

    hreg_store_msr(env, msr, 1);

#if !defined(CONFIG_USER_ONLY)
    env->nip = env->hreset_vector | env->excp_prefix;

    if (tcg_enabled()) {
        if (env->mmu_model != POWERPC_MMU_REAL) {
            ppc_tlb_invalidate_all(env);
        }
        pmu_update_summaries(env);
    }

    /* clean any pending stop state */
    env->resume_as_sreset = 0;
#endif
    hreg_compute_hflags(env);
    env->reserve_addr = (target_ulong)-1ULL;
    /* Be sure no exception or interrupt is pending */
    env->pending_interrupts = 0;
    s->exception_index = POWERPC_EXCP_NONE;
    env->error_code = 0;
    ppc_irq_reset(cpu);

    /* tininess for underflow is detected before rounding */
    set_float_detect_tininess(float_tininess_before_rounding,
                              &env->fp_status);

    for (i = 0; i < ARRAY_SIZE(env->spr_cb); i++) {
        ppc_spr_t *spr = &env->spr_cb[i];

        if (!spr->name) {
            continue;
        }
        env->spr[i] = spr->default_value;
    }
}

#ifndef CONFIG_USER_ONLY

static bool ppc_cpu_is_big_endian(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;

    cpu_synchronize_state(cs);

    return !FIELD_EX64(env->msr, MSR, LE);
}

#ifdef CONFIG_TCG
static void ppc_cpu_exec_enter(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);

    if (cpu->vhyp) {
        PPCVirtualHypervisorClass *vhc =
            PPC_VIRTUAL_HYPERVISOR_GET_CLASS(cpu->vhyp);
        vhc->cpu_exec_enter(cpu->vhyp, cpu);
    }
}

static void ppc_cpu_exec_exit(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);

    if (cpu->vhyp) {
        PPCVirtualHypervisorClass *vhc =
            PPC_VIRTUAL_HYPERVISOR_GET_CLASS(cpu->vhyp);
        vhc->cpu_exec_exit(cpu->vhyp, cpu);
    }
}
#endif /* CONFIG_TCG */

#endif /* !CONFIG_USER_ONLY */

static void ppc_cpu_instance_init(Object *obj)
{
    PowerPCCPU *cpu = POWERPC_CPU(obj);
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    CPUPPCState *env = &cpu->env;

    cpu_set_cpustate_pointers(cpu);
    cpu->vcpu_id = UNASSIGNED_CPU_INDEX;

    env->msr_mask = pcc->msr_mask;
    env->mmu_model = pcc->mmu_model;
    env->excp_model = pcc->excp_model;
    env->bus_model = pcc->bus_model;
    env->insns_flags = pcc->insns_flags;
    env->insns_flags2 = pcc->insns_flags2;
    env->flags = pcc->flags;
    env->bfd_mach = pcc->bfd_mach;
    env->check_pow = pcc->check_pow;

    /*
     * Mark HV mode as supported if the CPU has an MSR_HV bit in the
     * msr_mask. The mask can later be cleared by PAPR mode but the hv
     * mode support will remain, thus enforcing that we cannot use
     * priv. instructions in guest in PAPR mode. For 970 we currently
     * simply don't set HV in msr_mask thus simulating an "Apple mode"
     * 970. If we ever want to support 970 HV mode, we'll have to add
     * a processor attribute of some sort.
     */
#if !defined(CONFIG_USER_ONLY)
    env->has_hv_mode = !!(env->msr_mask & MSR_HVB);
#endif

    ppc_hash64_init(cpu);
}

static void ppc_cpu_instance_finalize(Object *obj)
{
    PowerPCCPU *cpu = POWERPC_CPU(obj);

    ppc_hash64_finalize(cpu);
}

static bool ppc_pvr_match_default(PowerPCCPUClass *pcc, uint32_t pvr, bool best)
{
    return pcc->pvr == pvr;
}

static void ppc_disas_set_info(CPUState *cs, disassemble_info *info)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;

    if ((env->hflags >> MSR_LE) & 1) {
        info->endian = BFD_ENDIAN_LITTLE;
    }
    info->mach = env->bfd_mach;
    if (!env->bfd_mach) {
#ifdef TARGET_PPC64
        info->mach = bfd_mach_ppc64;
#else
        info->mach = bfd_mach_ppc;
#endif
    }

    info->cap_arch = CS_ARCH_PPC;
#ifdef TARGET_PPC64
    info->cap_mode = CS_MODE_64;
#endif
}

static Property ppc_cpu_properties[] = {
    DEFINE_PROP_BOOL("pre-2.8-migration", PowerPCCPU, pre_2_8_migration, false),
    DEFINE_PROP_BOOL("pre-2.10-migration", PowerPCCPU, pre_2_10_migration,
                     false),
    DEFINE_PROP_BOOL("pre-3.0-migration", PowerPCCPU, pre_3_0_migration,
                     false),
    DEFINE_PROP_END_OF_LIST(),
};

#ifndef CONFIG_USER_ONLY
#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps ppc_sysemu_ops = {
    .get_phys_page_debug = ppc_cpu_get_phys_page_debug,
    .write_elf32_note = ppc32_cpu_write_elf32_note,
    .write_elf64_note = ppc64_cpu_write_elf64_note,
    .virtio_is_big_endian = ppc_cpu_is_big_endian,
    .legacy_vmsd = &vmstate_ppc_cpu,
};
#endif

#ifdef CONFIG_TCG
#include "hw/core/tcg-cpu-ops.h"

static const struct TCGCPUOps ppc_tcg_ops = {
  .initialize = ppc_translate_init,
  .restore_state_to_opc = ppc_restore_state_to_opc,

#ifdef CONFIG_USER_ONLY
  .record_sigsegv = ppc_cpu_record_sigsegv,
#else
  .tlb_fill = ppc_cpu_tlb_fill,
  .cpu_exec_interrupt = ppc_cpu_exec_interrupt,
  .do_interrupt = ppc_cpu_do_interrupt,
  .cpu_exec_enter = ppc_cpu_exec_enter,
  .cpu_exec_exit = ppc_cpu_exec_exit,
  .do_unaligned_access = ppc_cpu_do_unaligned_access,
#endif /* !CONFIG_USER_ONLY */
};
#endif /* CONFIG_TCG */

static void ppc_cpu_class_init(ObjectClass *oc, void *data)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_parent_realize(dc, ppc_cpu_realize,
                                    &pcc->parent_realize);
    device_class_set_parent_unrealize(dc, ppc_cpu_unrealize,
                                      &pcc->parent_unrealize);
    pcc->pvr_match = ppc_pvr_match_default;
    device_class_set_props(dc, ppc_cpu_properties);

    device_class_set_parent_reset(dc, ppc_cpu_reset, &pcc->parent_reset);

    cc->class_by_name = ppc_cpu_class_by_name;
    cc->has_work = ppc_cpu_has_work;
    cc->dump_state = ppc_cpu_dump_state;
    cc->set_pc = ppc_cpu_set_pc;
    cc->get_pc = ppc_cpu_get_pc;
    cc->gdb_read_register = ppc_cpu_gdb_read_register;
    cc->gdb_write_register = ppc_cpu_gdb_write_register;
#ifndef CONFIG_USER_ONLY
    cc->sysemu_ops = &ppc_sysemu_ops;
#endif

    cc->gdb_num_core_regs = 71;
#ifndef CONFIG_USER_ONLY
    cc->gdb_get_dynamic_xml = ppc_gdb_get_dynamic_xml;
#endif
#ifdef USE_APPLE_GDB
    cc->gdb_read_register = ppc_cpu_gdb_read_register_apple;
    cc->gdb_write_register = ppc_cpu_gdb_write_register_apple;
    cc->gdb_num_core_regs = 71 + 32;
#endif

    cc->gdb_arch_name = ppc_gdb_arch_name;
#if defined(TARGET_PPC64)
    cc->gdb_core_xml_file = "power64-core.xml";
#else
    cc->gdb_core_xml_file = "power-core.xml";
#endif
    cc->disas_set_info = ppc_disas_set_info;

    dc->fw_name = "PowerPC,UNKNOWN";

#ifdef CONFIG_TCG
    cc->tcg_ops = &ppc_tcg_ops;
#endif /* CONFIG_TCG */
}

static const TypeInfo ppc_cpu_type_info = {
    .name = TYPE_POWERPC_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(PowerPCCPU),
    .instance_align = __alignof__(PowerPCCPU),
    .instance_init = ppc_cpu_instance_init,
    .instance_finalize = ppc_cpu_instance_finalize,
    .abstract = true,
    .class_size = sizeof(PowerPCCPUClass),
    .class_init = ppc_cpu_class_init,
};

#ifndef CONFIG_USER_ONLY
static const TypeInfo ppc_vhyp_type_info = {
    .name = TYPE_PPC_VIRTUAL_HYPERVISOR,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(PPCVirtualHypervisorClass),
};
#endif

static void ppc_cpu_register_types(void)
{
    type_register_static(&ppc_cpu_type_info);
#ifndef CONFIG_USER_ONLY
    type_register_static(&ppc_vhyp_type_info);
#endif
}

void ppc_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
#define RGPL  4
#define RFPL  4

    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    int i;

    qemu_fprintf(f, "NIP " TARGET_FMT_lx "   LR " TARGET_FMT_lx " CTR "
                 TARGET_FMT_lx " XER " TARGET_FMT_lx " CPU#%d\n",
                 env->nip, env->lr, env->ctr, cpu_read_xer(env),
                 cs->cpu_index);
    qemu_fprintf(f, "MSR " TARGET_FMT_lx " HID0 " TARGET_FMT_lx "  HF "
                 "%08x iidx %d didx %d\n",
                 env->msr, env->spr[SPR_HID0], env->hflags,
                 cpu_mmu_index(env, true), cpu_mmu_index(env, false));
#if !defined(CONFIG_USER_ONLY)
    if (env->tb_env) {
        qemu_fprintf(f, "TB %08" PRIu32 " %08" PRIu64
                     " DECR " TARGET_FMT_lu "\n", cpu_ppc_load_tbu(env),
                     cpu_ppc_load_tbl(env), cpu_ppc_load_decr(env));
    }
#else
    qemu_fprintf(f, "TB %08" PRIu32 " %08" PRIu64 "\n", cpu_ppc_load_tbu(env),
                 cpu_ppc_load_tbl(env));
#endif
    for (i = 0; i < 32; i++) {
        if ((i & (RGPL - 1)) == 0) {
            qemu_fprintf(f, "GPR%02d", i);
        }
        qemu_fprintf(f, " %016" PRIx64, ppc_dump_gpr(env, i));
        if ((i & (RGPL - 1)) == (RGPL - 1)) {
            qemu_fprintf(f, "\n");
        }
    }
    qemu_fprintf(f, "CR ");
    for (i = 0; i < 8; i++)
        qemu_fprintf(f, "%01x", env->crf[i]);
    qemu_fprintf(f, "  [");
    for (i = 0; i < 8; i++) {
        char a = '-';
        if (env->crf[i] & 0x08) {
            a = 'L';
        } else if (env->crf[i] & 0x04) {
            a = 'G';
        } else if (env->crf[i] & 0x02) {
            a = 'E';
        }
        qemu_fprintf(f, " %c%c", a, env->crf[i] & 0x01 ? 'O' : ' ');
    }
    qemu_fprintf(f, " ]             RES " TARGET_FMT_lx "\n",
                 env->reserve_addr);

    if (flags & CPU_DUMP_FPU) {
        for (i = 0; i < 32; i++) {
            if ((i & (RFPL - 1)) == 0) {
                qemu_fprintf(f, "FPR%02d", i);
            }
            qemu_fprintf(f, " %016" PRIx64, *cpu_fpr_ptr(env, i));
            if ((i & (RFPL - 1)) == (RFPL - 1)) {
                qemu_fprintf(f, "\n");
            }
        }
        qemu_fprintf(f, "FPSCR " TARGET_FMT_lx "\n", env->fpscr);
    }

#if !defined(CONFIG_USER_ONLY)
    qemu_fprintf(f, " SRR0 " TARGET_FMT_lx "  SRR1 " TARGET_FMT_lx
                 "    PVR " TARGET_FMT_lx " VRSAVE " TARGET_FMT_lx "\n",
                 env->spr[SPR_SRR0], env->spr[SPR_SRR1],
                 env->spr[SPR_PVR], env->spr[SPR_VRSAVE]);

    qemu_fprintf(f, "SPRG0 " TARGET_FMT_lx " SPRG1 " TARGET_FMT_lx
                 "  SPRG2 " TARGET_FMT_lx "  SPRG3 " TARGET_FMT_lx "\n",
                 env->spr[SPR_SPRG0], env->spr[SPR_SPRG1],
                 env->spr[SPR_SPRG2], env->spr[SPR_SPRG3]);

    qemu_fprintf(f, "SPRG4 " TARGET_FMT_lx " SPRG5 " TARGET_FMT_lx
                 "  SPRG6 " TARGET_FMT_lx "  SPRG7 " TARGET_FMT_lx "\n",
                 env->spr[SPR_SPRG4], env->spr[SPR_SPRG5],
                 env->spr[SPR_SPRG6], env->spr[SPR_SPRG7]);

    switch (env->excp_model) {
#if defined(TARGET_PPC64)
    case POWERPC_EXCP_POWER7:
    case POWERPC_EXCP_POWER8:
    case POWERPC_EXCP_POWER9:
    case POWERPC_EXCP_POWER10:
        qemu_fprintf(f, "HSRR0 " TARGET_FMT_lx " HSRR1 " TARGET_FMT_lx "\n",
                     env->spr[SPR_HSRR0], env->spr[SPR_HSRR1]);
        break;
#endif
    case POWERPC_EXCP_BOOKE:
        qemu_fprintf(f, "CSRR0 " TARGET_FMT_lx " CSRR1 " TARGET_FMT_lx
                     " MCSRR0 " TARGET_FMT_lx " MCSRR1 " TARGET_FMT_lx "\n",
                     env->spr[SPR_BOOKE_CSRR0], env->spr[SPR_BOOKE_CSRR1],
                     env->spr[SPR_BOOKE_MCSRR0], env->spr[SPR_BOOKE_MCSRR1]);

        qemu_fprintf(f, "  TCR " TARGET_FMT_lx "   TSR " TARGET_FMT_lx
                     "    ESR " TARGET_FMT_lx "   DEAR " TARGET_FMT_lx "\n",
                     env->spr[SPR_BOOKE_TCR], env->spr[SPR_BOOKE_TSR],
                     env->spr[SPR_BOOKE_ESR], env->spr[SPR_BOOKE_DEAR]);

        qemu_fprintf(f, "  PIR " TARGET_FMT_lx " DECAR " TARGET_FMT_lx
                     "   IVPR " TARGET_FMT_lx "   EPCR " TARGET_FMT_lx "\n",
                     env->spr[SPR_BOOKE_PIR], env->spr[SPR_BOOKE_DECAR],
                     env->spr[SPR_BOOKE_IVPR], env->spr[SPR_BOOKE_EPCR]);

        qemu_fprintf(f, " MCSR " TARGET_FMT_lx " SPRG8 " TARGET_FMT_lx
                     "    EPR " TARGET_FMT_lx "\n",
                     env->spr[SPR_BOOKE_MCSR], env->spr[SPR_BOOKE_SPRG8],
                     env->spr[SPR_BOOKE_EPR]);

        /* FSL-specific */
        qemu_fprintf(f, " MCAR " TARGET_FMT_lx "  PID1 " TARGET_FMT_lx
                     "   PID2 " TARGET_FMT_lx "    SVR " TARGET_FMT_lx "\n",
                     env->spr[SPR_Exxx_MCAR], env->spr[SPR_BOOKE_PID1],
                     env->spr[SPR_BOOKE_PID2], env->spr[SPR_E500_SVR]);

        /*
         * IVORs are left out as they are large and do not change often --
         * they can be read with "p $ivor0", "p $ivor1", etc.
         */
        break;
    case POWERPC_EXCP_40x:
        qemu_fprintf(f, "  TCR " TARGET_FMT_lx "   TSR " TARGET_FMT_lx
                     "    ESR " TARGET_FMT_lx "   DEAR " TARGET_FMT_lx "\n",
                     env->spr[SPR_40x_TCR], env->spr[SPR_40x_TSR],
                     env->spr[SPR_40x_ESR], env->spr[SPR_40x_DEAR]);

        qemu_fprintf(f, " EVPR " TARGET_FMT_lx "  SRR2 " TARGET_FMT_lx
                     "   SRR3 " TARGET_FMT_lx  "   PID " TARGET_FMT_lx "\n",
                     env->spr[SPR_40x_EVPR], env->spr[SPR_40x_SRR2],
                     env->spr[SPR_40x_SRR3], env->spr[SPR_40x_PID]);
        break;
    default:
        break;
    }

#if defined(TARGET_PPC64)
    if (env->flags & POWERPC_FLAG_CFAR) {
        qemu_fprintf(f, " CFAR " TARGET_FMT_lx"\n", env->cfar);
    }
#endif

    if (env->spr_cb[SPR_LPCR].name) {
        qemu_fprintf(f, " LPCR " TARGET_FMT_lx "\n", env->spr[SPR_LPCR]);
    }

    switch (env->mmu_model) {
    case POWERPC_MMU_32B:
    case POWERPC_MMU_SOFT_6xx:
#if defined(TARGET_PPC64)
    case POWERPC_MMU_64B:
    case POWERPC_MMU_2_03:
    case POWERPC_MMU_2_06:
    case POWERPC_MMU_2_07:
    case POWERPC_MMU_3_00:
#endif
        if (env->spr_cb[SPR_SDR1].name) { /* SDR1 Exists */
            qemu_fprintf(f, " SDR1 " TARGET_FMT_lx " ", env->spr[SPR_SDR1]);
        }
        if (env->spr_cb[SPR_PTCR].name) { /* PTCR Exists */
            qemu_fprintf(f, " PTCR " TARGET_FMT_lx " ", env->spr[SPR_PTCR]);
        }
        qemu_fprintf(f, "  DAR " TARGET_FMT_lx "  DSISR " TARGET_FMT_lx "\n",
                     env->spr[SPR_DAR], env->spr[SPR_DSISR]);
        break;
    case POWERPC_MMU_BOOKE206:
        qemu_fprintf(f, " MAS0 " TARGET_FMT_lx "  MAS1 " TARGET_FMT_lx
                     "   MAS2 " TARGET_FMT_lx "   MAS3 " TARGET_FMT_lx "\n",
                     env->spr[SPR_BOOKE_MAS0], env->spr[SPR_BOOKE_MAS1],
                     env->spr[SPR_BOOKE_MAS2], env->spr[SPR_BOOKE_MAS3]);

        qemu_fprintf(f, " MAS4 " TARGET_FMT_lx "  MAS6 " TARGET_FMT_lx
                     "   MAS7 " TARGET_FMT_lx "    PID " TARGET_FMT_lx "\n",
                     env->spr[SPR_BOOKE_MAS4], env->spr[SPR_BOOKE_MAS6],
                     env->spr[SPR_BOOKE_MAS7], env->spr[SPR_BOOKE_PID]);

        qemu_fprintf(f, "MMUCFG " TARGET_FMT_lx " TLB0CFG " TARGET_FMT_lx
                     " TLB1CFG " TARGET_FMT_lx "\n",
                     env->spr[SPR_MMUCFG], env->spr[SPR_BOOKE_TLB0CFG],
                     env->spr[SPR_BOOKE_TLB1CFG]);
        break;
    default:
        break;
    }
#endif

#undef RGPL
#undef RFPL
}
type_init(ppc_cpu_register_types)
