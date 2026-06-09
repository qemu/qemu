/*
 * AArch64 FP8 helper definitions
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

DEF_HELPER_FLAGS_4(advsimd_bfcvtl, TCG_CALL_NO_RWG, void, ptr, ptr, env, i32)
