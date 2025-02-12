.. _Arm Emulation:

A-profile CPU architecture support
==================================

QEMU's TCG emulation includes support for the Armv5, Armv6, Armv7,
Armv8 and Armv9 versions of the A-profile architecture. It also has support for
the following architecture extensions:

- FEAT_AA32BF16 (AArch32 BFloat16 instructions)
- FEAT_AA32EL0 (Support for AArch32 at EL0)
- FEAT_AA32EL1 (Support for AArch32 at EL1)
- FEAT_AA32EL2 (Support for AArch32 at EL2)
- FEAT_AA32EL3 (Support for AArch32 at EL3)
- FEAT_AA32HPD (AArch32 hierarchical permission disables)
- FEAT_AA32I8MM (AArch32 Int8 matrix multiplication instructions)
- FEAT_AA64EL0 (Support for AArch64 at EL0)
- FEAT_AA64EL1 (Support for AArch64 at EL1)
- FEAT_AA64EL2 (Support for AArch64 at EL2)
- FEAT_AA64EL3 (Support for AArch64 at EL3)
- FEAT_AdvSIMD (Advanced SIMD Extension)
- FEAT_AES (AESD and AESE instructions)
- FEAT_AFP (Alternate floating-point behavior)
- FEAT_Armv9_Crypto (Armv9 Cryptographic Extension)
- FEAT_ASID16 (16 bit ASID)
- FEAT_BBM at level 2 (Translation table break-before-make levels)
- FEAT_BF16 (AArch64 BFloat16 instructions)
- FEAT_BTI (Branch Target Identification)
- FEAT_CCIDX (Extended cache index)
- FEAT_CMOW (Control for cache maintenance permission)
- FEAT_CRC32 (CRC32 instructions)
- FEAT_Crypto (Cryptographic Extension)
- FEAT_CSV2 (Cache speculation variant 2)
- FEAT_CSV2_1p1 (Cache speculation variant 2, version 1.1)
- FEAT_CSV2_1p2 (Cache speculation variant 2, version 1.2)
- FEAT_CSV2_2 (Cache speculation variant 2, version 2)
- FEAT_CSV2_3 (Cache speculation variant 2, version 3)
- FEAT_CSV3 (Cache speculation variant 3)
- FEAT_DGH (Data gathering hint)
- FEAT_DIT (Data Independent Timing instructions)
- FEAT_DoubleLock (Double Lock)
- FEAT_DPB (DC CVAP instruction)
- FEAT_DPB2 (DC CVADP instruction)
- FEAT_Debugv8p1 (Debug with VHE)
- FEAT_Debugv8p2 (Debug changes for v8.2)
- FEAT_Debugv8p4 (Debug changes for v8.4)
- FEAT_Debugv8p8 (Debug changes for v8.8)
- FEAT_DotProd (Advanced SIMD dot product instructions)
- FEAT_DoubleFault (Double Fault Extension)
- FEAT_E0PD (Preventing EL0 access to halves of address maps)
- FEAT_EBF16 (AArch64 Extended BFloat16 instructions)
- FEAT_ECV (Enhanced Counter Virtualization)
- FEAT_EL0 (Support for execution at EL0)
- FEAT_EL1 (Support for execution at EL1)
- FEAT_EL2 (Support for execution at EL2)
- FEAT_EL3 (Support for execution at EL3)
- FEAT_EPAC (Enhanced pointer authentication)
- FEAT_ETS2 (Enhanced Translation Synchronization)
- FEAT_EVT (Enhanced Virtualization Traps)
- FEAT_F32MM (Single-precision Matrix Multiplication)
- FEAT_F64MM (Double-precision Matrix Multiplication)
- FEAT_FCMA (Floating-point complex number instructions)
- FEAT_FGT (Fine-Grained Traps)
- FEAT_FHM (Floating-point half-precision multiplication instructions)
- FEAT_FP (Floating Point extensions)
- FEAT_FP16 (Half-precision floating-point data processing)
- FEAT_FPAC (Faulting on AUT* instructions)
- FEAT_FPACCOMBINE (Faulting on combined pointer authentication instructions)
- FEAT_FPACC_SPEC (Speculative behavior of combined pointer authentication instructions)
- FEAT_FRINTTS (Floating-point to integer instructions)
- FEAT_FlagM (Flag manipulation instructions v2)
- FEAT_FlagM2 (Enhancements to flag manipulation instructions)
- FEAT_GTG (Guest translation granule size)
- FEAT_HAFDBS (Hardware management of the access flag and dirty bit state)
- FEAT_HBC (Hinted conditional branches)
- FEAT_HCX (Support for the HCRX_EL2 register)
- FEAT_HPDS (Hierarchical permission disables)
- FEAT_HPDS2 (Translation table page-based hardware attributes)
- FEAT_HPMN0 (Setting of MDCR_EL2.HPMN to zero)
- FEAT_I8MM (AArch64 Int8 matrix multiplication instructions)
- FEAT_IDST (ID space trap handling)
- FEAT_IESB (Implicit error synchronization event)
- FEAT_JSCVT (JavaScript conversion instructions)
- FEAT_LOR (Limited ordering regions)
- FEAT_LPA (Large Physical Address space)
- FEAT_LPA2 (Large Physical and virtual Address space v2)
- FEAT_LRCPC (Load-acquire RCpc instructions)
- FEAT_LRCPC2 (Load-acquire RCpc instructions v2)
- FEAT_LSE (Large System Extensions)
- FEAT_LSE2 (Large System Extensions v2)
- FEAT_LVA (Large Virtual Address space)
- FEAT_MixedEnd (Mixed-endian support)
- FEAT_MixedEndEL0 (Mixed-endian support at EL0)
- FEAT_MOPS (Standardization of memory operations)
- FEAT_MTE (Memory Tagging Extension)
- FEAT_MTE2 (Memory Tagging Extension)
- FEAT_MTE3 (MTE Asymmetric Fault Handling)
- FEAT_MTE_ASYM_FAULT (Memory tagging asymmetric faults)
- FEAT_MTE_ASYNC (Asynchronous reporting of Tag Check Fault)
- FEAT_NMI (Non-maskable Interrupt)
- FEAT_NV (Nested Virtualization)
- FEAT_NV2 (Enhanced nested virtualization support)
- FEAT_PACIMP (Pointer authentication - IMPLEMENTATION DEFINED algorithm)
- FEAT_PACQARMA3 (Pointer authentication - QARMA3 algorithm)
- FEAT_PACQARMA5 (Pointer authentication - QARMA5 algorithm)
- FEAT_PAN (Privileged access never)
- FEAT_PAN2 (AT S1E1R and AT S1E1W instruction variants affected by PSTATE.PAN)
- FEAT_PAN3 (Support for SCTLR_ELx.EPAN)
- FEAT_PAuth (Pointer authentication)
- FEAT_PAuth2 (Enhancements to pointer authentication)
- FEAT_PMULL (PMULL, PMULL2 instructions)
- FEAT_PMUv3 (PMU extension version 3)
- FEAT_PMUv3p1 (PMU Extensions v3.1)
- FEAT_PMUv3p4 (PMU Extensions v3.4)
- FEAT_PMUv3p5 (PMU Extensions v3.5)
- FEAT_RAS (Reliability, availability, and serviceability)
- FEAT_RASv1p1 (RAS Extension v1.1)
- FEAT_RDM (Advanced SIMD rounding double multiply accumulate instructions)
- FEAT_RME (Realm Management Extension) (NB: support status in QEMU is experimental)
- FEAT_RNG (Random number generator)
- FEAT_RPRES (Increased precision of FRECPE and FRSQRTE)
- FEAT_S2FWB (Stage 2 forced Write-Back)
- FEAT_SB (Speculation Barrier)
- FEAT_SEL2 (Secure EL2)
- FEAT_SHA1 (SHA1 instructions)
- FEAT_SHA256 (SHA256 instructions)
- FEAT_SHA3 (Advanced SIMD SHA3 instructions)
- FEAT_SHA512 (Advanced SIMD SHA512 instructions)
- FEAT_SM3 (Advanced SIMD SM3 instructions)
- FEAT_SM4 (Advanced SIMD SM4 instructions)
- FEAT_SME (Scalable Matrix Extension)
- FEAT_SME_FA64 (Full A64 instruction set in Streaming SVE mode)
- FEAT_SME_F64F64 (Double-precision floating-point outer product instructions)
- FEAT_SME_I16I64 (16-bit to 64-bit integer widening outer product instructions)
- FEAT_SVE (Scalable Vector Extension)
- FEAT_SVE_AES (Scalable Vector AES instructions)
- FEAT_SVE_BitPerm (Scalable Vector Bit Permutes instructions)
- FEAT_SVE_PMULL128 (Scalable Vector PMULL instructions)
- FEAT_SVE_SHA3 (Scalable Vector SHA3 instructions)
- FEAT_SVE_SM4 (Scalable Vector SM4 instructions)
- FEAT_SVE2 (Scalable Vector Extension version 2)
- FEAT_SPECRES (Speculation restriction instructions)
- FEAT_SSBS (Speculative Store Bypass Safe)
- FEAT_SSBS2 (MRS and MSR instructions for SSBS version 2)
- FEAT_TGran16K (Support for 16KB memory translation granule size at stage 1)
- FEAT_TGran4K (Support for 4KB memory translation granule size at stage 1)
- FEAT_TGran64K (Support for 64KB memory translation granule size at stage 1)
- FEAT_TIDCP1 (EL0 use of IMPLEMENTATION DEFINED functionality)
- FEAT_TLBIOS (TLB invalidate instructions in Outer Shareable domain)
- FEAT_TLBIRANGE (TLB invalidate range instructions)
- FEAT_TTCNP (Translation table Common not private translations)
- FEAT_TTL (Translation Table Level)
- FEAT_TTST (Small translation tables)
- FEAT_UAO (Unprivileged Access Override control)
- FEAT_VHE (Virtualization Host Extensions)
- FEAT_VMID16 (16-bit VMID)
- FEAT_WFxT (WFE and WFI instructions with timeout)
- FEAT_XNX (Translation table stage 2 Unprivileged Execute-never)
- FEAT_XS (XS attribute)

For information on the specifics of these extensions, please refer
to the `Arm Architecture Reference Manual for A-profile architecture
<https://developer.arm.com/documentation/ddi0487/latest>`_.

When a specific named CPU is being emulated, only those features which
are present in hardware for that CPU are emulated. (If a feature is
not in the list above then it is not supported, even if the real
hardware should have it.) The ``max`` CPU enables all features.

R-profile CPU architecture support
==================================

QEMU's TCG emulation support for R-profile CPUs is currently limited.
We emulate only the Cortex-R5 and Cortex-R5F CPUs.

M-profile CPU architecture support
==================================

QEMU's TCG emulation includes support for Armv6-M, Armv7-M, Armv8-M, and
Armv8.1-M versions of the M-profile architucture.  It also has support
for the following architecture extensions:

- FP (Floating-point Extension)
- FPCXT (FPCXT access instructions)
- HP (Half-precision floating-point instructions)
- LOB (Low Overhead loops and Branch future)
- M (Main Extension)
- MPU (Memory Protection Unit Extension)
- PXN (Privileged Execute Never)
- RAS (Reliability, Serviceability and Availability): "minimum RAS Extension" only
- S (Security Extension)
- ST (System Timer Extension)

For information on the specifics of these extensions, please refer
to the `Armv8-M Arm Architecture Reference Manual
<https://developer.arm.com/documentation/ddi0553/latest>`_.

When a specific named CPU is being emulated, only those features which
are present in hardware for that CPU are emulated. (If a feature is
not in the list above then it is not supported, even if the real
hardware should have it.) There is no equivalent of the ``max`` CPU for
M-profile.
