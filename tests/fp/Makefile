BUILD_DIR := $(CURDIR)/../..

include $(BUILD_DIR)/config-host.mak
include $(SRC_PATH)/rules.mak

SOFTFLOAT_DIR := $(SRC_PATH)/tests/fp/berkeley-softfloat-3
TESTFLOAT_DIR := $(SRC_PATH)/tests/fp/berkeley-testfloat-3

SF_SOURCE_DIR  := $(SOFTFLOAT_DIR)/source
SF_INCLUDE_DIR := $(SOFTFLOAT_DIR)/source/include
# we could use any specialize here, it doesn't matter
SF_SPECIALIZE := 8086-SSE
SF_SPECIALIZE_DIR := $(SF_SOURCE_DIR)/$(SF_SPECIALIZE)

TF_SOURCE_DIR := $(TESTFLOAT_DIR)/source

$(call set-vpath, $(SRC_PATH)/fpu $(SRC_PATH)/tests/fp)

LIBQEMUUTIL := $(BUILD_DIR)/libqemuutil.a

# Use this variable to be clear when we pull in our own implementation
# We build the object with a default rule thanks to the vpath above
QEMU_SOFTFLOAT_OBJ := softfloat.o

QEMU_INCLUDES += -I$(SRC_PATH)/tests/fp
QEMU_INCLUDES += -I$(SF_INCLUDE_DIR)
QEMU_INCLUDES += -I$(SF_SPECIALIZE_DIR)
QEMU_INCLUDES += -I$(TF_SOURCE_DIR)

# work around TARGET_* poisoning
QEMU_CFLAGS += -DHW_POISON_H
# define a target to match testfloat's implementation-defined choices, such as
# whether to raise the invalid flag when dealing with NaNs in muladd.
QEMU_CFLAGS += -DTARGET_ARM

# capstone has a platform.h file that clashes with softfloat's
QEMU_CFLAGS := $(filter-out %capstone, $(QEMU_CFLAGS))

# softfloat defines
SF_OPTS :=
SF_OPTS += -DSOFTFLOAT_ROUND_ODD
SF_OPTS += -DINLINE_LEVEL=5
SF_OPTS += -DSOFTFLOAT_FAST_DIV32TO16
SF_OPTS += -DSOFTFLOAT_FAST_DIV64TO32
SF_OPTS += -DSOFTFLOAT_FAST_INT64
QEMU_CFLAGS += $(SF_OPTS)

# silence the build of softfloat objects
SF_CFLAGS += -Wno-missing-prototypes
SF_CFLAGS += -Wno-redundant-decls
SF_CFLAGS += -Wno-return-type
SF_CFLAGS += -Wno-error

# testfloat defines
TF_OPTS :=
TF_OPTS += -DFLOAT16
TF_OPTS += -DFLOAT64
TF_OPTS += -DEXTFLOAT80
TF_OPTS += -DFLOAT128
TF_OPTS += -DFLOAT_ROUND_ODD
TF_OPTS += -DLONG_DOUBLE_IS_EXTFLOAT80
QEMU_CFLAGS += $(TF_OPTS)

# silence the build of testfloat objects
TF_CFLAGS :=
TF_CFLAGS += -Wno-strict-prototypes
TF_CFLAGS += -Wno-unknown-pragmas
TF_CFLAGS += -Wno-uninitialized
TF_CFLAGS += -Wno-missing-prototypes
TF_CFLAGS += -Wno-return-type
TF_CFLAGS += -Wno-unused-function
TF_CFLAGS += -Wno-error

# softfloat objects
SF_OBJS_PRIMITIVES :=
SF_OBJS_PRIMITIVES += s_eq128.o
SF_OBJS_PRIMITIVES += s_le128.o
SF_OBJS_PRIMITIVES += s_lt128.o
SF_OBJS_PRIMITIVES += s_shortShiftLeft128.o
SF_OBJS_PRIMITIVES += s_shortShiftRight128.o
SF_OBJS_PRIMITIVES += s_shortShiftRightJam64.o
SF_OBJS_PRIMITIVES += s_shortShiftRightJam64Extra.o
SF_OBJS_PRIMITIVES += s_shortShiftRightJam128.o
SF_OBJS_PRIMITIVES += s_shortShiftRightJam128Extra.o
SF_OBJS_PRIMITIVES += s_shiftRightJam32.o
SF_OBJS_PRIMITIVES += s_shiftRightJam64.o
SF_OBJS_PRIMITIVES += s_shiftRightJam64Extra.o
SF_OBJS_PRIMITIVES += s_shiftRightJam128.o
SF_OBJS_PRIMITIVES += s_shiftRightJam128Extra.o
SF_OBJS_PRIMITIVES += s_shiftRightJam256M.o
SF_OBJS_PRIMITIVES += s_countLeadingZeros8.o
SF_OBJS_PRIMITIVES += s_countLeadingZeros16.o
SF_OBJS_PRIMITIVES += s_countLeadingZeros32.o
SF_OBJS_PRIMITIVES += s_countLeadingZeros64.o
SF_OBJS_PRIMITIVES += s_add128.o
SF_OBJS_PRIMITIVES += s_add256M.o
SF_OBJS_PRIMITIVES += s_sub128.o
SF_OBJS_PRIMITIVES += s_sub256M.o
SF_OBJS_PRIMITIVES += s_mul64ByShifted32To128.o
SF_OBJS_PRIMITIVES += s_mul64To128.o
SF_OBJS_PRIMITIVES += s_mul128By32.o
SF_OBJS_PRIMITIVES += s_mul128To256M.o
SF_OBJS_PRIMITIVES += s_approxRecip_1Ks.o
SF_OBJS_PRIMITIVES += s_approxRecip32_1.o
SF_OBJS_PRIMITIVES += s_approxRecipSqrt_1Ks.o
SF_OBJS_PRIMITIVES += s_approxRecipSqrt32_1.o

SF_OBJS_SPECIALIZE :=
SF_OBJS_SPECIALIZE += softfloat_raiseFlags.o
SF_OBJS_SPECIALIZE += s_f16UIToCommonNaN.o
SF_OBJS_SPECIALIZE += s_commonNaNToF16UI.o
SF_OBJS_SPECIALIZE += s_propagateNaNF16UI.o
SF_OBJS_SPECIALIZE += s_f32UIToCommonNaN.o
SF_OBJS_SPECIALIZE += s_commonNaNToF32UI.o
SF_OBJS_SPECIALIZE += s_propagateNaNF32UI.o
SF_OBJS_SPECIALIZE += s_f64UIToCommonNaN.o
SF_OBJS_SPECIALIZE += s_commonNaNToF64UI.o
SF_OBJS_SPECIALIZE += s_propagateNaNF64UI.o
SF_OBJS_SPECIALIZE += extF80M_isSignalingNaN.o
SF_OBJS_SPECIALIZE += s_extF80UIToCommonNaN.o
SF_OBJS_SPECIALIZE += s_commonNaNToExtF80UI.o
SF_OBJS_SPECIALIZE += s_propagateNaNExtF80UI.o
SF_OBJS_SPECIALIZE += f128M_isSignalingNaN.o
SF_OBJS_SPECIALIZE += s_f128UIToCommonNaN.o
SF_OBJS_SPECIALIZE += s_commonNaNToF128UI.o
SF_OBJS_SPECIALIZE += s_propagateNaNF128UI.o

SF_OBJS_OTHERS :=
SF_OBJS_OTHERS += s_roundToUI32.o
SF_OBJS_OTHERS += s_roundToUI64.o
SF_OBJS_OTHERS += s_roundToI32.o
SF_OBJS_OTHERS += s_roundToI64.o
SF_OBJS_OTHERS += s_normSubnormalF16Sig.o
SF_OBJS_OTHERS += s_roundPackToF16.o
SF_OBJS_OTHERS += s_normRoundPackToF16.o
SF_OBJS_OTHERS += s_addMagsF16.o
SF_OBJS_OTHERS += s_subMagsF16.o
SF_OBJS_OTHERS += s_mulAddF16.o
SF_OBJS_OTHERS += s_normSubnormalF32Sig.o
SF_OBJS_OTHERS += s_roundPackToF32.o
SF_OBJS_OTHERS += s_normRoundPackToF32.o
SF_OBJS_OTHERS += s_addMagsF32.o
SF_OBJS_OTHERS += s_subMagsF32.o
SF_OBJS_OTHERS += s_mulAddF32.o
SF_OBJS_OTHERS += s_normSubnormalF64Sig.o
SF_OBJS_OTHERS += s_roundPackToF64.o
SF_OBJS_OTHERS += s_normRoundPackToF64.o
SF_OBJS_OTHERS += s_addMagsF64.o
SF_OBJS_OTHERS += s_subMagsF64.o
SF_OBJS_OTHERS += s_mulAddF64.o
SF_OBJS_OTHERS += s_normSubnormalExtF80Sig.o
SF_OBJS_OTHERS += s_roundPackToExtF80.o
SF_OBJS_OTHERS += s_normRoundPackToExtF80.o
SF_OBJS_OTHERS += s_addMagsExtF80.o
SF_OBJS_OTHERS += s_subMagsExtF80.o
SF_OBJS_OTHERS += s_normSubnormalF128Sig.o
SF_OBJS_OTHERS += s_roundPackToF128.o
SF_OBJS_OTHERS += s_normRoundPackToF128.o
SF_OBJS_OTHERS += s_addMagsF128.o
SF_OBJS_OTHERS += s_subMagsF128.o
SF_OBJS_OTHERS += s_mulAddF128.o
SF_OBJS_OTHERS += softfloat_state.o
SF_OBJS_OTHERS += ui32_to_f16.o
SF_OBJS_OTHERS += ui32_to_f32.o
SF_OBJS_OTHERS += ui32_to_f64.o
SF_OBJS_OTHERS += ui32_to_extF80.o
SF_OBJS_OTHERS += ui32_to_extF80M.o
SF_OBJS_OTHERS += ui32_to_f128.o
SF_OBJS_OTHERS += ui32_to_f128M.o
SF_OBJS_OTHERS += ui64_to_f16.o
SF_OBJS_OTHERS += ui64_to_f32.o
SF_OBJS_OTHERS += ui64_to_f64.o
SF_OBJS_OTHERS += ui64_to_extF80.o
SF_OBJS_OTHERS += ui64_to_extF80M.o
SF_OBJS_OTHERS += ui64_to_f128.o
SF_OBJS_OTHERS += ui64_to_f128M.o
SF_OBJS_OTHERS += i32_to_f16.o
SF_OBJS_OTHERS += i32_to_f32.o
SF_OBJS_OTHERS += i32_to_f64.o
SF_OBJS_OTHERS += i32_to_extF80.o
SF_OBJS_OTHERS += i32_to_extF80M.o
SF_OBJS_OTHERS += i32_to_f128.o
SF_OBJS_OTHERS += i32_to_f128M.o
SF_OBJS_OTHERS += i64_to_f16.o
SF_OBJS_OTHERS += i64_to_f32.o
SF_OBJS_OTHERS += i64_to_f64.o
SF_OBJS_OTHERS += i64_to_extF80.o
SF_OBJS_OTHERS += i64_to_extF80M.o
SF_OBJS_OTHERS += i64_to_f128.o
SF_OBJS_OTHERS += i64_to_f128M.o
SF_OBJS_OTHERS += f16_to_ui32.o
SF_OBJS_OTHERS += f16_to_ui64.o
SF_OBJS_OTHERS += f16_to_i32.o
SF_OBJS_OTHERS += f16_to_i64.o
SF_OBJS_OTHERS += f16_to_ui32_r_minMag.o
SF_OBJS_OTHERS += f16_to_ui64_r_minMag.o
SF_OBJS_OTHERS += f16_to_i32_r_minMag.o
SF_OBJS_OTHERS += f16_to_i64_r_minMag.o
SF_OBJS_OTHERS += f16_to_f32.o
SF_OBJS_OTHERS += f16_to_f64.o
SF_OBJS_OTHERS += f16_to_extF80.o
SF_OBJS_OTHERS += f16_to_extF80M.o
SF_OBJS_OTHERS += f16_to_f128.o
SF_OBJS_OTHERS += f16_to_f128M.o
SF_OBJS_OTHERS += f16_roundToInt.o
SF_OBJS_OTHERS += f16_add.o
SF_OBJS_OTHERS += f16_sub.o
SF_OBJS_OTHERS += f16_mul.o
SF_OBJS_OTHERS += f16_mulAdd.o
SF_OBJS_OTHERS += f16_div.o
SF_OBJS_OTHERS += f16_rem.o
SF_OBJS_OTHERS += f16_sqrt.o
SF_OBJS_OTHERS += f16_eq.o
SF_OBJS_OTHERS += f16_le.o
SF_OBJS_OTHERS += f16_lt.o
SF_OBJS_OTHERS += f16_eq_signaling.o
SF_OBJS_OTHERS += f16_le_quiet.o
SF_OBJS_OTHERS += f16_lt_quiet.o
SF_OBJS_OTHERS += f16_isSignalingNaN.o
SF_OBJS_OTHERS += f32_to_ui32.o
SF_OBJS_OTHERS += f32_to_ui64.o
SF_OBJS_OTHERS += f32_to_i32.o
SF_OBJS_OTHERS += f32_to_i64.o
SF_OBJS_OTHERS += f32_to_ui32_r_minMag.o
SF_OBJS_OTHERS += f32_to_ui64_r_minMag.o
SF_OBJS_OTHERS += f32_to_i32_r_minMag.o
SF_OBJS_OTHERS += f32_to_i64_r_minMag.o
SF_OBJS_OTHERS += f32_to_f16.o
SF_OBJS_OTHERS += f32_to_f64.o
SF_OBJS_OTHERS += f32_to_extF80.o
SF_OBJS_OTHERS += f32_to_extF80M.o
SF_OBJS_OTHERS += f32_to_f128.o
SF_OBJS_OTHERS += f32_to_f128M.o
SF_OBJS_OTHERS += f32_roundToInt.o
SF_OBJS_OTHERS += f32_add.o
SF_OBJS_OTHERS += f32_sub.o
SF_OBJS_OTHERS += f32_mul.o
SF_OBJS_OTHERS += f32_mulAdd.o
SF_OBJS_OTHERS += f32_div.o
SF_OBJS_OTHERS += f32_rem.o
SF_OBJS_OTHERS += f32_sqrt.o
SF_OBJS_OTHERS += f32_eq.o
SF_OBJS_OTHERS += f32_le.o
SF_OBJS_OTHERS += f32_lt.o
SF_OBJS_OTHERS += f32_eq_signaling.o
SF_OBJS_OTHERS += f32_le_quiet.o
SF_OBJS_OTHERS += f32_lt_quiet.o
SF_OBJS_OTHERS += f32_isSignalingNaN.o
SF_OBJS_OTHERS += f64_to_ui32.o
SF_OBJS_OTHERS += f64_to_ui64.o
SF_OBJS_OTHERS += f64_to_i32.o
SF_OBJS_OTHERS += f64_to_i64.o
SF_OBJS_OTHERS += f64_to_ui32_r_minMag.o
SF_OBJS_OTHERS += f64_to_ui64_r_minMag.o
SF_OBJS_OTHERS += f64_to_i32_r_minMag.o
SF_OBJS_OTHERS += f64_to_i64_r_minMag.o
SF_OBJS_OTHERS += f64_to_f16.o
SF_OBJS_OTHERS += f64_to_f32.o
SF_OBJS_OTHERS += f64_to_extF80.o
SF_OBJS_OTHERS += f64_to_extF80M.o
SF_OBJS_OTHERS += f64_to_f128.o
SF_OBJS_OTHERS += f64_to_f128M.o
SF_OBJS_OTHERS += f64_roundToInt.o
SF_OBJS_OTHERS += f64_add.o
SF_OBJS_OTHERS += f64_sub.o
SF_OBJS_OTHERS += f64_mul.o
SF_OBJS_OTHERS += f64_mulAdd.o
SF_OBJS_OTHERS += f64_div.o
SF_OBJS_OTHERS += f64_rem.o
SF_OBJS_OTHERS += f64_sqrt.o
SF_OBJS_OTHERS += f64_eq.o
SF_OBJS_OTHERS += f64_le.o
SF_OBJS_OTHERS += f64_lt.o
SF_OBJS_OTHERS += f64_eq_signaling.o
SF_OBJS_OTHERS += f64_le_quiet.o
SF_OBJS_OTHERS += f64_lt_quiet.o
SF_OBJS_OTHERS += f64_isSignalingNaN.o
SF_OBJS_OTHERS += extF80_to_ui32.o
SF_OBJS_OTHERS += extF80_to_ui64.o
SF_OBJS_OTHERS += extF80_to_i32.o
SF_OBJS_OTHERS += extF80_to_i64.o
SF_OBJS_OTHERS += extF80_to_ui32_r_minMag.o
SF_OBJS_OTHERS += extF80_to_ui64_r_minMag.o
SF_OBJS_OTHERS += extF80_to_i32_r_minMag.o
SF_OBJS_OTHERS += extF80_to_i64_r_minMag.o
SF_OBJS_OTHERS += extF80_to_f16.o
SF_OBJS_OTHERS += extF80_to_f32.o
SF_OBJS_OTHERS += extF80_to_f64.o
SF_OBJS_OTHERS += extF80_to_f128.o
SF_OBJS_OTHERS += extF80_roundToInt.o
SF_OBJS_OTHERS += extF80_add.o
SF_OBJS_OTHERS += extF80_sub.o
SF_OBJS_OTHERS += extF80_mul.o
SF_OBJS_OTHERS += extF80_div.o
SF_OBJS_OTHERS += extF80_rem.o
SF_OBJS_OTHERS += extF80_sqrt.o
SF_OBJS_OTHERS += extF80_eq.o
SF_OBJS_OTHERS += extF80_le.o
SF_OBJS_OTHERS += extF80_lt.o
SF_OBJS_OTHERS += extF80_eq_signaling.o
SF_OBJS_OTHERS += extF80_le_quiet.o
SF_OBJS_OTHERS += extF80_lt_quiet.o
SF_OBJS_OTHERS += extF80_isSignalingNaN.o
SF_OBJS_OTHERS += extF80M_to_ui32.o
SF_OBJS_OTHERS += extF80M_to_ui64.o
SF_OBJS_OTHERS += extF80M_to_i32.o
SF_OBJS_OTHERS += extF80M_to_i64.o
SF_OBJS_OTHERS += extF80M_to_ui32_r_minMag.o
SF_OBJS_OTHERS += extF80M_to_ui64_r_minMag.o
SF_OBJS_OTHERS += extF80M_to_i32_r_minMag.o
SF_OBJS_OTHERS += extF80M_to_i64_r_minMag.o
SF_OBJS_OTHERS += extF80M_to_f16.o
SF_OBJS_OTHERS += extF80M_to_f32.o
SF_OBJS_OTHERS += extF80M_to_f64.o
SF_OBJS_OTHERS += extF80M_to_f128M.o
SF_OBJS_OTHERS += extF80M_roundToInt.o
SF_OBJS_OTHERS += extF80M_add.o
SF_OBJS_OTHERS += extF80M_sub.o
SF_OBJS_OTHERS += extF80M_mul.o
SF_OBJS_OTHERS += extF80M_div.o
SF_OBJS_OTHERS += extF80M_rem.o
SF_OBJS_OTHERS += extF80M_sqrt.o
SF_OBJS_OTHERS += extF80M_eq.o
SF_OBJS_OTHERS += extF80M_le.o
SF_OBJS_OTHERS += extF80M_lt.o
SF_OBJS_OTHERS += extF80M_eq_signaling.o
SF_OBJS_OTHERS += extF80M_le_quiet.o
SF_OBJS_OTHERS += extF80M_lt_quiet.o
SF_OBJS_OTHERS += f128_to_ui32.o
SF_OBJS_OTHERS += f128_to_ui64.o
SF_OBJS_OTHERS += f128_to_i32.o
SF_OBJS_OTHERS += f128_to_i64.o
SF_OBJS_OTHERS += f128_to_ui32_r_minMag.o
SF_OBJS_OTHERS += f128_to_ui64_r_minMag.o
SF_OBJS_OTHERS += f128_to_i32_r_minMag.o
SF_OBJS_OTHERS += f128_to_i64_r_minMag.o
SF_OBJS_OTHERS += f128_to_f16.o
SF_OBJS_OTHERS += f128_to_f32.o
SF_OBJS_OTHERS += f128_to_extF80.o
SF_OBJS_OTHERS += f128_to_f64.o
SF_OBJS_OTHERS += f128_roundToInt.o
SF_OBJS_OTHERS += f128_add.o
SF_OBJS_OTHERS += f128_sub.o
SF_OBJS_OTHERS += f128_mul.o
SF_OBJS_OTHERS += f128_mulAdd.o
SF_OBJS_OTHERS += f128_div.o
SF_OBJS_OTHERS += f128_rem.o
SF_OBJS_OTHERS += f128_sqrt.o
SF_OBJS_OTHERS += f128_eq.o
SF_OBJS_OTHERS += f128_le.o
SF_OBJS_OTHERS += f128_lt.o
SF_OBJS_OTHERS += f128_eq_signaling.o
SF_OBJS_OTHERS += f128_le_quiet.o
SF_OBJS_OTHERS += f128_lt_quiet.o
SF_OBJS_OTHERS += f128_isSignalingNaN.o
SF_OBJS_OTHERS += f128M_to_ui32.o
SF_OBJS_OTHERS += f128M_to_ui64.o
SF_OBJS_OTHERS += f128M_to_i32.o
SF_OBJS_OTHERS += f128M_to_i64.o
SF_OBJS_OTHERS += f128M_to_ui32_r_minMag.o
SF_OBJS_OTHERS += f128M_to_ui64_r_minMag.o
SF_OBJS_OTHERS += f128M_to_i32_r_minMag.o
SF_OBJS_OTHERS += f128M_to_i64_r_minMag.o
SF_OBJS_OTHERS += f128M_to_f16.o
SF_OBJS_OTHERS += f128M_to_f32.o
SF_OBJS_OTHERS += f128M_to_extF80M.o
SF_OBJS_OTHERS += f128M_to_f64.o
SF_OBJS_OTHERS += f128M_roundToInt.o
SF_OBJS_OTHERS += f128M_add.o
SF_OBJS_OTHERS += f128M_sub.o
SF_OBJS_OTHERS += f128M_mul.o
SF_OBJS_OTHERS += f128M_mulAdd.o
SF_OBJS_OTHERS += f128M_div.o
SF_OBJS_OTHERS += f128M_rem.o
SF_OBJS_OTHERS += f128M_sqrt.o
SF_OBJS_OTHERS += f128M_eq.o
SF_OBJS_OTHERS += f128M_le.o
SF_OBJS_OTHERS += f128M_lt.o
SF_OBJS_OTHERS += f128M_eq_signaling.o
SF_OBJS_OTHERS += f128M_le_quiet.o
SF_OBJS_OTHERS += f128M_lt_quiet.o

SF_OBJS_ALL_NOSPEC :=
SF_OBJS_ALL_NOSPEC += $(SF_OBJS_PRIMITIVES)
SF_OBJS_ALL_NOSPEC += $(SF_OBJS_OTHERS)

SF_OBJS_ALL :=
SF_OBJS_ALL += $(SF_OBJS_ALL_NOSPEC)
SF_OBJS_ALL += $(SF_OBJS_SPECIALIZE)

# testfloat objects
TF_OBJS_GENCASES :=
TF_OBJS_GENCASES += genCases_ui32.o
TF_OBJS_GENCASES += genCases_ui64.o
TF_OBJS_GENCASES += genCases_i32.o
TF_OBJS_GENCASES += genCases_i64.o
TF_OBJS_GENCASES += genCases_f16.o
TF_OBJS_GENCASES += genCases_f32.o
TF_OBJS_GENCASES += genCases_f64.o
TF_OBJS_GENCASES += genCases_extF80.o
TF_OBJS_GENCASES += genCases_f128.o

TF_OBJS_WRITECASE :=
TF_OBJS_WRITECASE += writeCase_a_ui32.o
TF_OBJS_WRITECASE += writeCase_a_ui64.o
TF_OBJS_WRITECASE += writeCase_a_f16.o
TF_OBJS_WRITECASE += writeCase_ab_f16.o
TF_OBJS_WRITECASE += writeCase_abc_f16.o
TF_OBJS_WRITECASE += writeCase_a_f32.o
TF_OBJS_WRITECASE += writeCase_ab_f32.o
TF_OBJS_WRITECASE += writeCase_abc_f32.o
TF_OBJS_WRITECASE += writeCase_a_f64.o
TF_OBJS_WRITECASE += writeCase_ab_f64.o
TF_OBJS_WRITECASE += writeCase_abc_f64.o
TF_OBJS_WRITECASE += writeCase_a_extF80M.o
TF_OBJS_WRITECASE += writeCase_ab_extF80M.o
TF_OBJS_WRITECASE += writeCase_a_f128M.o
TF_OBJS_WRITECASE += writeCase_ab_f128M.o
TF_OBJS_WRITECASE += writeCase_abc_f128M.o
TF_OBJS_WRITECASE += writeCase_z_bool.o
TF_OBJS_WRITECASE += writeCase_z_ui32.o
TF_OBJS_WRITECASE += writeCase_z_ui64.o
TF_OBJS_WRITECASE += writeCase_z_f16.o
TF_OBJS_WRITECASE += writeCase_z_f32.o
TF_OBJS_WRITECASE += writeCase_z_f64.o
TF_OBJS_WRITECASE += writeCase_z_extF80M.o
TF_OBJS_WRITECASE += writeCase_z_f128M.o

TF_OBJS_TEST :=
TF_OBJS_TEST += test_a_ui32_z_f16.o
TF_OBJS_TEST += test_a_ui32_z_f32.o
TF_OBJS_TEST += test_a_ui32_z_f64.o
TF_OBJS_TEST += test_a_ui32_z_extF80.o
TF_OBJS_TEST += test_a_ui32_z_f128.o
TF_OBJS_TEST += test_a_ui64_z_f16.o
TF_OBJS_TEST += test_a_ui64_z_f32.o
TF_OBJS_TEST += test_a_ui64_z_f64.o
TF_OBJS_TEST += test_a_ui64_z_extF80.o
TF_OBJS_TEST += test_a_ui64_z_f128.o
TF_OBJS_TEST += test_a_i32_z_f16.o
TF_OBJS_TEST += test_a_i32_z_f32.o
TF_OBJS_TEST += test_a_i32_z_f64.o
TF_OBJS_TEST += test_a_i32_z_extF80.o
TF_OBJS_TEST += test_a_i32_z_f128.o
TF_OBJS_TEST += test_a_i64_z_f16.o
TF_OBJS_TEST += test_a_i64_z_f32.o
TF_OBJS_TEST += test_a_i64_z_f64.o
TF_OBJS_TEST += test_a_i64_z_extF80.o
TF_OBJS_TEST += test_a_i64_z_f128.o
TF_OBJS_TEST += test_a_f16_z_ui32_rx.o
TF_OBJS_TEST += test_a_f16_z_ui64_rx.o
TF_OBJS_TEST += test_a_f16_z_i32_rx.o
TF_OBJS_TEST += test_a_f16_z_i64_rx.o
TF_OBJS_TEST += test_a_f16_z_ui32_x.o
TF_OBJS_TEST += test_a_f16_z_ui64_x.o
TF_OBJS_TEST += test_a_f16_z_i32_x.o
TF_OBJS_TEST += test_a_f16_z_i64_x.o
TF_OBJS_TEST += test_a_f16_z_f32.o
TF_OBJS_TEST += test_a_f16_z_f64.o
TF_OBJS_TEST += test_a_f16_z_extF80.o
TF_OBJS_TEST += test_a_f16_z_f128.o
TF_OBJS_TEST += test_az_f16.o
TF_OBJS_TEST += test_az_f16_rx.o
TF_OBJS_TEST += test_abz_f16.o
TF_OBJS_TEST += test_abcz_f16.o
TF_OBJS_TEST += test_ab_f16_z_bool.o
TF_OBJS_TEST += test_a_f32_z_ui32_rx.o
TF_OBJS_TEST += test_a_f32_z_ui64_rx.o
TF_OBJS_TEST += test_a_f32_z_i32_rx.o
TF_OBJS_TEST += test_a_f32_z_i64_rx.o
TF_OBJS_TEST += test_a_f32_z_ui32_x.o
TF_OBJS_TEST += test_a_f32_z_ui64_x.o
TF_OBJS_TEST += test_a_f32_z_i32_x.o
TF_OBJS_TEST += test_a_f32_z_i64_x.o
TF_OBJS_TEST += test_a_f32_z_f16.o
TF_OBJS_TEST += test_a_f32_z_f64.o
TF_OBJS_TEST += test_a_f32_z_extF80.o
TF_OBJS_TEST += test_a_f32_z_f128.o
TF_OBJS_TEST += test_az_f32.o
TF_OBJS_TEST += test_az_f32_rx.o
TF_OBJS_TEST += test_abz_f32.o
TF_OBJS_TEST += test_abcz_f32.o
TF_OBJS_TEST += test_ab_f32_z_bool.o
TF_OBJS_TEST += test_a_f64_z_ui32_rx.o
TF_OBJS_TEST += test_a_f64_z_ui64_rx.o
TF_OBJS_TEST += test_a_f64_z_i32_rx.o
TF_OBJS_TEST += test_a_f64_z_i64_rx.o
TF_OBJS_TEST += test_a_f64_z_ui32_x.o
TF_OBJS_TEST += test_a_f64_z_ui64_x.o
TF_OBJS_TEST += test_a_f64_z_i32_x.o
TF_OBJS_TEST += test_a_f64_z_i64_x.o
TF_OBJS_TEST += test_a_f64_z_f16.o
TF_OBJS_TEST += test_a_f64_z_f32.o
TF_OBJS_TEST += test_a_f64_z_extF80.o
TF_OBJS_TEST += test_a_f64_z_f128.o
TF_OBJS_TEST += test_az_f64.o
TF_OBJS_TEST += test_az_f64_rx.o
TF_OBJS_TEST += test_abz_f64.o
TF_OBJS_TEST += test_abcz_f64.o
TF_OBJS_TEST += test_ab_f64_z_bool.o
TF_OBJS_TEST += test_a_extF80_z_ui32_rx.o
TF_OBJS_TEST += test_a_extF80_z_ui64_rx.o
TF_OBJS_TEST += test_a_extF80_z_i32_rx.o
TF_OBJS_TEST += test_a_extF80_z_i64_rx.o
TF_OBJS_TEST += test_a_extF80_z_ui32_x.o
TF_OBJS_TEST += test_a_extF80_z_ui64_x.o
TF_OBJS_TEST += test_a_extF80_z_i32_x.o
TF_OBJS_TEST += test_a_extF80_z_i64_x.o
TF_OBJS_TEST += test_a_extF80_z_f16.o
TF_OBJS_TEST += test_a_extF80_z_f32.o
TF_OBJS_TEST += test_a_extF80_z_f64.o
TF_OBJS_TEST += test_a_extF80_z_f128.o
TF_OBJS_TEST += test_az_extF80.o
TF_OBJS_TEST += test_az_extF80_rx.o
TF_OBJS_TEST += test_abz_extF80.o
TF_OBJS_TEST += test_ab_extF80_z_bool.o
TF_OBJS_TEST += test_a_f128_z_ui32_rx.o
TF_OBJS_TEST += test_a_f128_z_ui64_rx.o
TF_OBJS_TEST += test_a_f128_z_i32_rx.o
TF_OBJS_TEST += test_a_f128_z_i64_rx.o
TF_OBJS_TEST += test_a_f128_z_ui32_x.o
TF_OBJS_TEST += test_a_f128_z_ui64_x.o
TF_OBJS_TEST += test_a_f128_z_i32_x.o
TF_OBJS_TEST += test_a_f128_z_i64_x.o
TF_OBJS_TEST += test_a_f128_z_f16.o
TF_OBJS_TEST += test_a_f128_z_f32.o
TF_OBJS_TEST += test_a_f128_z_f64.o
TF_OBJS_TEST += test_a_f128_z_extF80.o
TF_OBJS_TEST += test_az_f128.o
TF_OBJS_TEST += test_az_f128_rx.o
TF_OBJS_TEST += test_abz_f128.o
TF_OBJS_TEST += test_abcz_f128.o
TF_OBJS_TEST += test_ab_f128_z_bool.o

TF_OBJS_LIB :=
TF_OBJS_LIB += uint128_inline.o
TF_OBJS_LIB += uint128.o
TF_OBJS_LIB += fail.o
TF_OBJS_LIB += functions_common.o
TF_OBJS_LIB += functionInfos.o
TF_OBJS_LIB += standardFunctionInfos.o
TF_OBJS_LIB += random.o
TF_OBJS_LIB += genCases_common.o
TF_OBJS_LIB += $(TF_OBJS_GENCASES)
TF_OBJS_LIB += genCases_writeTestsTotal.o
TF_OBJS_LIB += verCases_inline.o
TF_OBJS_LIB += verCases_common.o
TF_OBJS_LIB += verCases_writeFunctionName.o
TF_OBJS_LIB += readHex.o
TF_OBJS_LIB += writeHex.o
TF_OBJS_LIB += $(TF_OBJS_WRITECASE)
TF_OBJS_LIB += testLoops_common.o
TF_OBJS_LIB += $(TF_OBJS_TEST)

BINARIES := fp-test$(EXESUF) fp-bench$(EXESUF)

# everything depends on config-host.h because platform.h includes it
all: $(BUILD_DIR)/config-host.h
	$(MAKE) $(BINARIES)

$(LIBQEMUUTIL):
	$(MAKE) -C $(BUILD_DIR) libqemuutil.a

$(BUILD_DIR)/config-host.h:
	$(MAKE) -C $(BUILD_DIR) config-host.h

# libtestfloat.a depends on libsoftfloat.a, so specify it first
FP_TEST_LIBS := libtestfloat.a libsoftfloat.a $(LIBQEMUUTIL)

fp-test$(EXESUF): fp-test.o slowfloat.o $(QEMU_SOFTFLOAT_OBJ) $(FP_TEST_LIBS)

# Custom rule to build with SF_CFLAGS
SF_BUILD = $(call quiet-command,$(CC) $(QEMU_LOCAL_INCLUDES) $(QEMU_INCLUDES) \
		$(QEMU_CFLAGS) $(SF_CFLAGS) $(QEMU_DGFLAGS) $(CFLAGS) \
		$($@-cflags) -c -o $@ $<,"CC","$(TARGET_DIR)$@")

$(SF_OBJS_ALL_NOSPEC): %.o: $(SF_SOURCE_DIR)/%.c
	$(SF_BUILD)
$(SF_OBJS_SPECIALIZE): %.o: $(SF_SPECIALIZE_DIR)/%.c
	$(SF_BUILD)

libsoftfloat.a: $(SF_OBJS_ALL)

# Custom rule to build with TF_CFLAGS
$(TF_OBJS_LIB) slowfloat.o: %.o: $(TF_SOURCE_DIR)/%.c
	$(call quiet-command,$(CC) $(QEMU_LOCAL_INCLUDES) $(QEMU_INCLUDES) \
		$(QEMU_CFLAGS) $(TF_CFLAGS) $(QEMU_DGFLAGS) $(CFLAGS) \
		$($@-cflags) -c -o $@ $<,"CC","$(TARGET_DIR)$@")

libtestfloat.a: $(TF_OBJS_LIB)

fp-bench$(EXESUF): fp-bench.o $(QEMU_SOFTFLOAT_OBJ) $(LIBQEMUUTIL)

clean:
	rm -f *.o *.d $(BINARIES)
	rm -f *.gcno *.gcda *.gcov
	rm -f fp-test$(EXESUF)
	rm -f fp-bench$(EXESUF)
	rm -f libsoftfloat.a
	rm -f libtestfloat.a

-include $(wildcard *.d)
