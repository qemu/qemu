A-profile CPU architecture support
==================================

QEMU's TCG emulation includes support for the Armv5, Armv6, Armv7 and
Armv8 versions of the A-profile architecture. It also has support for
the following architecture extensions:

- FEAT_AA32BF16 (AArch32 BFloat16 instructions)
- FEAT_AA32HPD (AArch32 hierarchical permission disables)
- FEAT_AA32I8MM (AArch32 Int8 matrix multiplication instructions)
- FEAT_AES (AESD and AESE instructions)
- FEAT_BF16 (AArch64 BFloat16 instructions)
- FEAT_BTI (Branch Target Identification)
- FEAT_DIT (Data Independent Timing instructions)
- FEAT_DPB (DC CVAP instruction)
- FEAT_DotProd (Advanced SIMD dot product instructions)
- FEAT_FCMA (Floating-point complex number instructions)
- FEAT_FHM (Floating-point half-precision multiplication instructions)
- FEAT_FP16 (Half-precision floating-point data processing)
- FEAT_FRINTTS (Floating-point to integer instructions)
- FEAT_FlagM (Flag manipulation instructions v2)
- FEAT_FlagM2 (Enhancements to flag manipulation instructions)
- FEAT_HPDS (Hierarchical permission disables)
- FEAT_I8MM (AArch64 Int8 matrix multiplication instructions)
- FEAT_JSCVT (JavaScript conversion instructions)
- FEAT_LOR (Limited ordering regions)
- FEAT_LPA (Large Physical Address space)
- FEAT_LPA2 (Large Physical and virtual Address space v2)
- FEAT_LRCPC (Load-acquire RCpc instructions)
- FEAT_LRCPC2 (Load-acquire RCpc instructions v2)
- FEAT_LSE (Large System Extensions)
- FEAT_LVA (Large Virtual Address space)
- FEAT_MTE (Memory Tagging Extension)
- FEAT_MTE2 (Memory Tagging Extension)
- FEAT_MTE3 (MTE Asymmetric Fault Handling)
- FEAT_PAN (Privileged access never)
- FEAT_PAN2 (AT S1E1R and AT S1E1W instruction variants affected by PSTATE.PAN)
- FEAT_PAuth (Pointer authentication)
- FEAT_PMULL (PMULL, PMULL2 instructions)
- FEAT_PMUv3p1 (PMU Extensions v3.1)
- FEAT_PMUv3p4 (PMU Extensions v3.4)
- FEAT_RDM (Advanced SIMD rounding double multiply accumulate instructions)
- FEAT_RNG (Random number generator)
- FEAT_SB (Speculation Barrier)
- FEAT_SEL2 (Secure EL2)
- FEAT_SHA1 (SHA1 instructions)
- FEAT_SHA256 (SHA256 instructions)
- FEAT_SHA3 (Advanced SIMD SHA3 instructions)
- FEAT_SHA512 (Advanced SIMD SHA512 instructions)
- FEAT_SM3 (Advanced SIMD SM3 instructions)
- FEAT_SM4 (Advanced SIMD SM4 instructions)
- FEAT_SPECRES (Speculation restriction instructions)
- FEAT_SSBS (Speculative Store Bypass Safe)
- FEAT_TLBIOS (TLB invalidate instructions in Outer Shareable domain)
- FEAT_TLBIRANGE (TLB invalidate range instructions)
- FEAT_TTCNP (Translation table Common not private translations)
- FEAT_TTST (Small translation tables)
- FEAT_UAO (Unprivileged Access Override control)
- FEAT_VHE (Virtualization Host Extensions)
- FEAT_VMID16 (16-bit VMID)
- FEAT_XNX (Translation table stage 2 Unprivileged Execute-never)
- SVE (The Scalable Vector Extension)
- SVE2 (The Scalable Vector Extension v2)

For information on the specifics of these extensions, please refer
to the `Armv8-A Arm Architecture Reference Manual
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
