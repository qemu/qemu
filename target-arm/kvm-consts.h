/*
 * KVM ARM ABI constant definitions
 *
 * Copyright (c) 2013 Linaro Limited
 *
 * Provide versions of KVM constant defines that can be used even
 * when CONFIG_KVM is not set and we don't have access to the
 * KVM headers. If CONFIG_KVM is set, we do a compile-time check
 * that we haven't got out of sync somehow.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef ARM_KVM_CONSTS_H
#define ARM_KVM_CONSTS_H

#ifdef CONFIG_KVM
#include "qemu/compiler.h"
#include <linux/kvm.h>

#define MISMATCH_CHECK(X, Y) QEMU_BUILD_BUG_ON(X != Y)

#else
#define MISMATCH_CHECK(X, Y)
#endif

#define CP_REG_SIZE_SHIFT 52
#define CP_REG_SIZE_MASK       0x00f0000000000000ULL
#define CP_REG_SIZE_U32        0x0020000000000000ULL
#define CP_REG_SIZE_U64        0x0030000000000000ULL
#define CP_REG_ARM             0x4000000000000000ULL

MISMATCH_CHECK(CP_REG_SIZE_SHIFT, KVM_REG_SIZE_SHIFT)
MISMATCH_CHECK(CP_REG_SIZE_MASK, KVM_REG_SIZE_MASK)
MISMATCH_CHECK(CP_REG_SIZE_U32, KVM_REG_SIZE_U32)
MISMATCH_CHECK(CP_REG_SIZE_U64, KVM_REG_SIZE_U64)
MISMATCH_CHECK(CP_REG_ARM, KVM_REG_ARM)

#undef MISMATCH_CHECK

#endif
