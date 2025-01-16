/*
 * QTest testcase for intel-iommu
 *
 * Copyright (c) 2024 Intel, Inc.
 *
 * Author: Zhenzhong Duan <zhenzhong.duan@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "hw/i386/intel_iommu_internal.h"

#define CAP_STAGE_1_FIXED1    (VTD_CAP_FRO | VTD_CAP_NFR | VTD_CAP_ND | \
                              VTD_CAP_MAMV | VTD_CAP_PSI | VTD_CAP_SLLPS)
#define ECAP_STAGE_1_FIXED1   (VTD_ECAP_QI |  VTD_ECAP_IR | VTD_ECAP_IRO | \
                              VTD_ECAP_MHMV | VTD_ECAP_SMTS | VTD_ECAP_FLTS)

static inline uint64_t vtd_reg_readq(QTestState *s, uint64_t offset)
{
    return qtest_readq(s, Q35_HOST_BRIDGE_IOMMU_ADDR + offset);
}

static void test_intel_iommu_stage_1(void)
{
    uint8_t init_csr[DMAR_REG_SIZE];     /* register values */
    uint8_t post_reset_csr[DMAR_REG_SIZE];     /* register values */
    uint64_t cap, ecap, tmp;
    QTestState *s;

    s = qtest_init("-M q35 -device intel-iommu,x-scalable-mode=on,x-flts=on");

    cap = vtd_reg_readq(s, DMAR_CAP_REG);
    g_assert((cap & CAP_STAGE_1_FIXED1) == CAP_STAGE_1_FIXED1);

    tmp = cap & VTD_CAP_SAGAW_MASK;
    g_assert(tmp == (VTD_CAP_SAGAW_39bit | VTD_CAP_SAGAW_48bit));

    tmp = VTD_MGAW_FROM_CAP(cap);
    g_assert(tmp == VTD_HOST_AW_48BIT - 1);

    ecap = vtd_reg_readq(s, DMAR_ECAP_REG);
    g_assert((ecap & ECAP_STAGE_1_FIXED1) == ECAP_STAGE_1_FIXED1);

    qtest_memread(s, Q35_HOST_BRIDGE_IOMMU_ADDR, init_csr, DMAR_REG_SIZE);

    qobject_unref(qtest_qmp(s, "{ 'execute': 'system_reset' }"));
    qtest_qmp_eventwait(s, "RESET");

    qtest_memread(s, Q35_HOST_BRIDGE_IOMMU_ADDR, post_reset_csr, DMAR_REG_SIZE);
    /* Ensure registers are consistent after hard reset */
    g_assert(!memcmp(init_csr, post_reset_csr, DMAR_REG_SIZE));

    qtest_quit(s);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/q35/intel-iommu/stage-1", test_intel_iommu_stage_1);

    return g_test_run();
}
