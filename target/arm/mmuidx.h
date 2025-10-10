/*
 * QEMU Arm software mmu index definitions
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TARGET_ARM_MMUIDX_H
#define TARGET_ARM_MMUIDX_H

/*
 * Arm has the following "translation regimes" (as the Arm ARM calls them):
 *
 * If EL3 is 64-bit:
 *  + NonSecure EL1 & 0 stage 1
 *  + NonSecure EL1 & 0 stage 2
 *  + NonSecure EL2
 *  + NonSecure EL2 & 0   (ARMv8.1-VHE)
 *  + Secure EL1 & 0 stage 1
 *  + Secure EL1 & 0 stage 2 (FEAT_SEL2)
 *  + Secure EL2 (FEAT_SEL2)
 *  + Secure EL2 & 0 (FEAT_SEL2)
 *  + Realm EL1 & 0 stage 1 (FEAT_RME)
 *  + Realm EL1 & 0 stage 2 (FEAT_RME)
 *  + Realm EL2 (FEAT_RME)
 *  + EL3
 * If EL3 is 32-bit:
 *  + NonSecure PL1 & 0 stage 1
 *  + NonSecure PL1 & 0 stage 2
 *  + NonSecure PL2
 *  + Secure PL1 & 0
 * (reminder: for 32 bit EL3, Secure PL1 is *EL3*, not EL1.)
 *
 * For QEMU, an mmu_idx is not quite the same as a translation regime because:
 *  1. we need to split the "EL1 & 0" and "EL2 & 0" regimes into two mmu_idxes,
 *     because they may differ in access permissions even if the VA->PA map is
 *     the same
 *  2. we want to cache in our TLB the full VA->IPA->PA lookup for a stage 1+2
 *     translation, which means that we have one mmu_idx that deals with two
 *     concatenated translation regimes [this sort of combined s1+2 TLB is
 *     architecturally permitted]
 *  3. we don't need to allocate an mmu_idx to translations that we won't be
 *     handling via the TLB. The only way to do a stage 1 translation without
 *     the immediate stage 2 translation is via the ATS or AT system insns,
 *     which can be slow-pathed and always do a page table walk.
 *     The only use of stage 2 translations is either as part of an s1+2
 *     lookup or when loading the descriptors during a stage 1 page table walk,
 *     and in both those cases we don't use the TLB.
 *  4. we can also safely fold together the "32 bit EL3" and "64 bit EL3"
 *     translation regimes, because they map reasonably well to each other
 *     and they can't both be active at the same time.
 *  5. we want to be able to use the TLB for accesses done as part of a
 *     stage1 page table walk, rather than having to walk the stage2 page
 *     table over and over.
 *  6. we need separate EL1/EL2 mmu_idx for handling the Privileged Access
 *     Never (PAN) bit within PSTATE.
 *  7. we fold together most secure and non-secure regimes for A-profile,
 *     because there are no banked system registers for aarch64, so the
 *     process of switching between secure and non-secure is
 *     already heavyweight.
 *  8. we cannot fold together Stage 2 Secure and Stage 2 NonSecure,
 *     because both are in use simultaneously for Secure EL2.
 *  9. we need separate indexes for handling AccessType_GCS.
 *
 * This gives us the following list of cases:
 *
 * EL0 EL1&0 stage 1+2 (aka NS PL0 PL1&0 stage 1+2)
 * EL0 EL1&0 stage 1+2 +GCS
 * EL1 EL1&0 stage 1+2 (aka NS PL1 PL1&0 stage 1+2)
 * EL1 EL1&0 stage 1+2 +PAN (aka NS PL1 P1&0 stage 1+2 +PAN)
 * EL1 EL1&0 stage 1+2 +GCS
 * EL0 EL2&0
 * EL0 EL2&0 +GCS
 * EL2 EL2&0
 * EL2 EL2&0 +PAN
 * EL2 EL2&0 +GCS
 * EL2 (aka NS PL2)
 * EL2 +GCS
 * EL3 (aka AArch32 S PL1 PL1&0)
 * EL3 +GCS
 * AArch32 S PL0 PL1&0 (we call this EL30_0)
 * AArch32 S PL1 PL1&0 +PAN (we call this EL30_3_PAN)
 * Stage2 Secure
 * Stage2 NonSecure
 * plus one TLB per Physical address space: S, NS, Realm, Root
 *
 * for a total of 22 different mmu_idx.
 *
 * R profile CPUs have an MPU, but can use the same set of MMU indexes
 * as A profile. They only need to distinguish EL0 and EL1 (and
 * EL2 for cores like the Cortex-R52).
 *
 * M profile CPUs are rather different as they do not have a true MMU.
 * They have the following different MMU indexes:
 *  User
 *  Privileged
 *  User, execution priority negative (ie the MPU HFNMIENA bit may apply)
 *  Privileged, execution priority negative (ditto)
 * If the CPU supports the v8M Security Extension then there are also:
 *  Secure User
 *  Secure Privileged
 *  Secure User, execution priority negative
 *  Secure Privileged, execution priority negative
 *
 * The ARMMMUIdx and the mmu index value used by the core QEMU TLB code
 * are not quite the same -- different CPU types (most notably M profile
 * vs A/R profile) would like to use MMU indexes with different semantics,
 * but since we don't ever need to use all of those in a single CPU we
 * can avoid having to set NB_MMU_MODES to "total number of A profile MMU
 * modes + total number of M profile MMU modes". The lower bits of
 * ARMMMUIdx are the core TLB mmu index, and the higher bits are always
 * the same for any particular CPU.
 * Variables of type ARMMUIdx are always full values, and the core
 * index values are in variables of type 'int'.
 *
 * Our enumeration includes at the end some entries which are not "true"
 * mmu_idx values in that they don't have corresponding TLBs and are only
 * valid for doing slow path page table walks.
 *
 * The constant names here are patterned after the general style of the names
 * of the AT/ATS operations.
 * The values used are carefully arranged to make mmu_idx => EL lookup easy.
 * For M profile we arrange them to have a bit for priv, a bit for negpri
 * and a bit for secure.
 */
#define ARM_MMU_IDX_A     0x20  /* A profile */
#define ARM_MMU_IDX_NOTLB 0x40  /* does not have a TLB */
#define ARM_MMU_IDX_M     0x80  /* M profile */

/* Meanings of the bits for M profile mmu idx values */
#define ARM_MMU_IDX_M_PRIV   0x1
#define ARM_MMU_IDX_M_NEGPRI 0x2
#define ARM_MMU_IDX_M_S      0x4  /* Secure */

#define ARM_MMU_IDX_TYPE_MASK \
    (ARM_MMU_IDX_A | ARM_MMU_IDX_M | ARM_MMU_IDX_NOTLB)
#define ARM_MMU_IDX_COREIDX_MASK 0x1f

typedef enum ARMMMUIdx {
    /*
     * A-profile.
     */

    ARMMMUIdx_E10_0      = 0 | ARM_MMU_IDX_A,
    ARMMMUIdx_E10_0_GCS  = 1 | ARM_MMU_IDX_A,
    ARMMMUIdx_E10_1      = 2 | ARM_MMU_IDX_A,
    ARMMMUIdx_E10_1_PAN  = 3 | ARM_MMU_IDX_A,
    ARMMMUIdx_E10_1_GCS  = 4 | ARM_MMU_IDX_A,

    ARMMMUIdx_E20_0      = 5 | ARM_MMU_IDX_A,
    ARMMMUIdx_E20_0_GCS  = 6 | ARM_MMU_IDX_A,
    ARMMMUIdx_E20_2      = 7 | ARM_MMU_IDX_A,
    ARMMMUIdx_E20_2_PAN  = 8 | ARM_MMU_IDX_A,
    ARMMMUIdx_E20_2_GCS  = 9 | ARM_MMU_IDX_A,

    ARMMMUIdx_E2         = 10 | ARM_MMU_IDX_A,
    ARMMMUIdx_E2_GCS     = 11 | ARM_MMU_IDX_A,

    ARMMMUIdx_E3         = 12 | ARM_MMU_IDX_A,
    ARMMMUIdx_E3_GCS     = 13 | ARM_MMU_IDX_A,
    ARMMMUIdx_E30_0      = 14 | ARM_MMU_IDX_A,
    ARMMMUIdx_E30_3_PAN  = 15 | ARM_MMU_IDX_A,

    /*
     * Used for second stage of an S12 page table walk, or for descriptor
     * loads during first stage of an S1 page table walk.  Note that both
     * are in use simultaneously for SecureEL2: the security state for
     * the S2 ptw is selected by the NS bit from the S1 ptw.
     */
    ARMMMUIdx_Stage2_S   = 16 | ARM_MMU_IDX_A,
    ARMMMUIdx_Stage2     = 17 | ARM_MMU_IDX_A,

    /* TLBs with 1-1 mapping to the physical address spaces. */
    ARMMMUIdx_Phys_S     = 18 | ARM_MMU_IDX_A,
    ARMMMUIdx_Phys_NS    = 19 | ARM_MMU_IDX_A,
    ARMMMUIdx_Phys_Root  = 20 | ARM_MMU_IDX_A,
    ARMMMUIdx_Phys_Realm = 21 | ARM_MMU_IDX_A,

    /*
     * These are not allocated TLBs and are used only for AT system
     * instructions or for the first stage of an S12 page table walk.
     */
    ARMMMUIdx_Stage1_E0 = 0 | ARM_MMU_IDX_NOTLB,
    ARMMMUIdx_Stage1_E1 = 1 | ARM_MMU_IDX_NOTLB,
    ARMMMUIdx_Stage1_E1_PAN = 2 | ARM_MMU_IDX_NOTLB,
    ARMMMUIdx_Stage1_E0_GCS = 3 | ARM_MMU_IDX_NOTLB,
    ARMMMUIdx_Stage1_E1_GCS = 4 | ARM_MMU_IDX_NOTLB,

    /*
     * M-profile.
     */
    ARMMMUIdx_MUser = ARM_MMU_IDX_M,
    ARMMMUIdx_MPriv = ARM_MMU_IDX_M | ARM_MMU_IDX_M_PRIV,
    ARMMMUIdx_MUserNegPri = ARMMMUIdx_MUser | ARM_MMU_IDX_M_NEGPRI,
    ARMMMUIdx_MPrivNegPri = ARMMMUIdx_MPriv | ARM_MMU_IDX_M_NEGPRI,
    ARMMMUIdx_MSUser = ARMMMUIdx_MUser | ARM_MMU_IDX_M_S,
    ARMMMUIdx_MSPriv = ARMMMUIdx_MPriv | ARM_MMU_IDX_M_S,
    ARMMMUIdx_MSUserNegPri = ARMMMUIdx_MUserNegPri | ARM_MMU_IDX_M_S,
    ARMMMUIdx_MSPrivNegPri = ARMMMUIdx_MPrivNegPri | ARM_MMU_IDX_M_S,
} ARMMMUIdx;

/*
 * Bit macros for the core-mmu-index values for each index,
 * for use when calling tlb_flush_by_mmuidx() and friends.
 */
#define TO_CORE_BIT(NAME) \
    ARMMMUIdxBit_##NAME = 1 << (ARMMMUIdx_##NAME & ARM_MMU_IDX_COREIDX_MASK)

typedef enum ARMMMUIdxBit {
    TO_CORE_BIT(E10_0),
    TO_CORE_BIT(E10_0_GCS),
    TO_CORE_BIT(E10_1),
    TO_CORE_BIT(E10_1_PAN),
    TO_CORE_BIT(E10_1_GCS),
    TO_CORE_BIT(E20_0),
    TO_CORE_BIT(E20_0_GCS),
    TO_CORE_BIT(E20_2),
    TO_CORE_BIT(E20_2_PAN),
    TO_CORE_BIT(E20_2_GCS),
    TO_CORE_BIT(E2),
    TO_CORE_BIT(E2_GCS),
    TO_CORE_BIT(E3),
    TO_CORE_BIT(E3_GCS),
    TO_CORE_BIT(E30_0),
    TO_CORE_BIT(E30_3_PAN),
    TO_CORE_BIT(Stage2),
    TO_CORE_BIT(Stage2_S),

    TO_CORE_BIT(MUser),
    TO_CORE_BIT(MPriv),
    TO_CORE_BIT(MUserNegPri),
    TO_CORE_BIT(MPrivNegPri),
    TO_CORE_BIT(MSUser),
    TO_CORE_BIT(MSPriv),
    TO_CORE_BIT(MSUserNegPri),
    TO_CORE_BIT(MSPrivNegPri),
} ARMMMUIdxBit;

#undef TO_CORE_BIT

#define MMU_USER_IDX 0

#endif /* TARGET_ARM_MMUIDX_H */
