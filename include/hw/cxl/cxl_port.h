/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef CXL_PORT_H
#define CXL_PORT_H

#include "qemu/thread.h"

/* CXL r3.2 Table 7-19: Get Physical Port State Port Information Block Format */
#define CXL_PORT_CONFIG_STATE_DISABLED           0x0
#define CXL_PORT_CONFIG_STATE_BIND_IN_PROGRESS   0x1
#define CXL_PORT_CONFIG_STATE_UNBIND_IN_PROGRESS 0x2
#define CXL_PORT_CONFIG_STATE_DSP                0x3
#define CXL_PORT_CONFIG_STATE_USP                0x4
#define CXL_PORT_CONFIG_STATE_FABRIC_PORT        0x5
#define CXL_PORT_CONFIG_STATE_INVALID_PORT_ID    0xF

#define CXL_PORT_CONNECTED_DEV_MODE_NOT_CXL_OR_DISCONN 0x00
#define CXL_PORT_CONNECTED_DEV_MODE_RCD                0x01
#define CXL_PORT_CONNECTED_DEV_MODE_68B_VH             0x02
#define CXL_PORT_CONNECTED_DEV_MODE_256B               0x03
#define CXL_PORT_CONNECTED_DEV_MODE_LO_256B            0x04
#define CXL_PORT_CONNECTED_DEV_MODE_PBR                0x05

#define CXL_PORT_CONNECTED_DEV_TYPE_NONE            0x00
#define CXL_PORT_CONNECTED_DEV_TYPE_PCIE            0x01
#define CXL_PORT_CONNECTED_DEV_TYPE_1               0x02
#define CXL_PORT_CONNECTED_DEV_TYPE_2_OR_HBR_SWITCH 0x03
#define CXL_PORT_CONNECTED_DEV_TYPE_3_SLD           0x04
#define CXL_PORT_CONNECTED_DEV_TYPE_3_MLD           0x05
#define CXL_PORT_CONNECTED_DEV_PBR_COMPONENT        0x06

#define CXL_PORT_SUPPORTS_RCD        BIT(0)
#define CXL_PORT_SUPPORTS_68B_VH     BIT(1)
#define CXL_PORT_SUPPORTS_256B       BIT(2)
#define CXL_PORT_SUPPORTS_LO_256B    BIT(3)
#define CXL_PORT_SUPPORTS_PBR        BIT(4)

#define CXL_PORT_LTSSM_DETECT        0x00
#define CXL_PORT_LTSSM_POLLING       0x01
#define CXL_PORT_LTSSM_CONFIGURATION 0x02
#define CXL_PORT_LTSSM_RECOVERY      0x03
#define CXL_PORT_LTSSM_L0            0x04
#define CXL_PORT_LTSSM_L0S           0x05
#define CXL_PORT_LTSSM_L1            0x06
#define CXL_PORT_LTSSM_L2            0x07
#define CXL_PORT_LTSSM_DISABLED      0x08
#define CXL_PORT_LTSSM_LOOPBACK      0x09
#define CXL_PORT_LTSSM_HOT_RESET     0x0A

#define CXL_PORT_LINK_STATE_FLAG_LANE_REVERSED    BIT(0)
#define CXL_PORT_LINK_STATE_FLAG_PERST_ASSERTED   BIT(1)
#define CXL_PORT_LINK_STATE_FLAG_PRSNT            BIT(2)
#define CXL_PORT_LINK_STATE_FLAG_POWER_OFF        BIT(3)

#define CXL_MAX_PHY_PORTS 256
#define ASSERT_WAIT_TIME_MS 100 /* Assert - Deassert PERST */

/* Assert - Deassert PERST */
typedef struct CXLPhyPortPerst {
    bool issued_assert_perst;
    QemuMutex lock; /* protecting assert-deassert reset request */
    uint64_t asrt_time;
    QemuThread asrt_thread; /* thread for 100ms delay */
} CXLPhyPortPerst;

void cxl_init_physical_port_control(CXLPhyPortPerst *perst);

static inline bool cxl_perst_asserted(CXLPhyPortPerst *perst)
{
    return perst->issued_assert_perst || perst->asrt_time < ASSERT_WAIT_TIME_MS;
}

#endif /* CXL_PORT_H */
