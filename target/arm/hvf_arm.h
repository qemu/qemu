/*
 * QEMU Hypervisor.framework (HVF) support -- ARM specifics
 *
 * Copyright (c) 2021 Alexander Graf
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_HVF_ARM_H
#define QEMU_HVF_ARM_H

#include "target/arm/cpu-qom.h"

/**
 * hvf_arm_init_debug() - initialize guest debug capabilities
 *
 * Should be called only once before using guest debug capabilities.
 */
void hvf_arm_init_debug(void);

void hvf_arm_set_cpu_features_from_host(ARMCPU *cpu);

/*
 * We need access to types from macOS SDK >=15.2, so expose stubs if the
 * headers are not available until we raise our minimum macOS version.
 */
#ifdef __MAC_OS_X_VERSION_MAX_ALLOWED
  #if (__MAC_OS_X_VERSION_MAX_ALLOWED >= 150200)
    #include "system/hvf_int.h"

    static inline bool hvf_arm_sme2_supported(void)
    {
        if (__builtin_available(macOS 15.2, *)) {
            size_t svl_bytes;
            hv_return_t result = hv_sme_config_get_max_svl_bytes(&svl_bytes);
            if (result == HV_UNSUPPORTED) {
                return false;
            }
            assert_hvf_ok(result);
            return svl_bytes > 0;
        } else {
            return false;
        }
    }

    static inline uint32_t hvf_arm_sme2_get_svl(void)
    {
        if (__builtin_available(macOS 15.2, *)) {
            size_t svl_bytes;
            hv_return_t result = hv_sme_config_get_max_svl_bytes(&svl_bytes);
            assert_hvf_ok(result);
            return svl_bytes;
        } else {
            abort();
        }
    }
  #else /* (__MAC_OS_X_VERSION_MAX_ALLOWED >= 150200) */
      #include "hvf/hvf_sme_stubs.h"
  #endif /* (__MAC_OS_X_VERSION_MAX_ALLOWED >= 150200) */
#else /* ifdef __MAC_OS_X_VERSION_MAX_ALLOWED */
  #include "hvf/hvf_sme_stubs.h"
#endif /* ifdef __MAC_OS_X_VERSION_MAX_ALLOWED */

#endif
