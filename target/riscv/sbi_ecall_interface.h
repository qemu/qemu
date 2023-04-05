/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *   Anup Patel <anup.patel@wdc.com>
 */

#ifndef SBI_ECALL_INTERFACE_H
#define SBI_ECALL_INTERFACE_H

/* clang-format off */

/* SBI Extension IDs */
#define SBI_EXT_0_1_SET_TIMER           0x0
#define SBI_EXT_0_1_CONSOLE_PUTCHAR     0x1
#define SBI_EXT_0_1_CONSOLE_GETCHAR     0x2
#define SBI_EXT_0_1_CLEAR_IPI           0x3
#define SBI_EXT_0_1_SEND_IPI            0x4
#define SBI_EXT_0_1_REMOTE_FENCE_I      0x5
#define SBI_EXT_0_1_REMOTE_SFENCE_VMA   0x6
#define SBI_EXT_0_1_REMOTE_SFENCE_VMA_ASID 0x7
#define SBI_EXT_0_1_SHUTDOWN            0x8
#define SBI_EXT_BASE                    0x10
#define SBI_EXT_TIME                    0x54494D45
#define SBI_EXT_IPI                     0x735049
#define SBI_EXT_RFENCE                  0x52464E43
#define SBI_EXT_HSM                     0x48534D

/* SBI function IDs for BASE extension */
#define SBI_EXT_BASE_GET_SPEC_VERSION   0x0
#define SBI_EXT_BASE_GET_IMP_ID         0x1
#define SBI_EXT_BASE_GET_IMP_VERSION    0x2
#define SBI_EXT_BASE_PROBE_EXT          0x3
#define SBI_EXT_BASE_GET_MVENDORID      0x4
#define SBI_EXT_BASE_GET_MARCHID        0x5
#define SBI_EXT_BASE_GET_MIMPID         0x6

/* SBI function IDs for TIME extension */
#define SBI_EXT_TIME_SET_TIMER          0x0

/* SBI function IDs for IPI extension */
#define SBI_EXT_IPI_SEND_IPI            0x0

/* SBI function IDs for RFENCE extension */
#define SBI_EXT_RFENCE_REMOTE_FENCE_I       0x0
#define SBI_EXT_RFENCE_REMOTE_SFENCE_VMA    0x1
#define SBI_EXT_RFENCE_REMOTE_SFENCE_VMA_ASID  0x2
#define SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA   0x3
#define SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA_VMID 0x4
#define SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA   0x5
#define SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA_ASID 0x6

/* SBI function IDs for HSM extension */
#define SBI_EXT_HSM_HART_START          0x0
#define SBI_EXT_HSM_HART_STOP           0x1
#define SBI_EXT_HSM_HART_GET_STATUS     0x2

#define SBI_HSM_HART_STATUS_STARTED     0x0
#define SBI_HSM_HART_STATUS_STOPPED     0x1
#define SBI_HSM_HART_STATUS_START_PENDING   0x2
#define SBI_HSM_HART_STATUS_STOP_PENDING    0x3

#define SBI_SPEC_VERSION_MAJOR_OFFSET   24
#define SBI_SPEC_VERSION_MAJOR_MASK     0x7f
#define SBI_SPEC_VERSION_MINOR_MASK     0xffffff
#define SBI_EXT_VENDOR_START            0x09000000
#define SBI_EXT_VENDOR_END              0x09FFFFFF
/* clang-format on */

#endif
