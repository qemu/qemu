/*
 * QEMU L2VIC Interrupt Controller
 *
 * Copyright(c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define L2VIC_VID_GRP_0 0x0 /* Read */
#define L2VIC_VID_GRP_1 0x4 /* Read */
#define L2VIC_VID_GRP_2 0x8 /* Read */
#define L2VIC_VID_GRP_3 0xC /* Read */
#define L2VIC_VID_0 0x10 /* Read SOFTWARE DEFINED */
#define L2VIC_VID_1 0x14 /* Read SOFTWARE DEFINED NOT YET USED */
#define L2VIC_INT_ENABLEn 0x100 /* Read/Write */
#define L2VIC_INT_ENABLE_CLEARn 0x180 /* Write */
#define L2VIC_INT_ENABLE_SETn 0x200 /* Write */
#define L2VIC_INT_TYPEn 0x280 /* Read/Write */
#define L2VIC_INT_STATUSn 0x380 /* Read */
#define L2VIC_INT_CLEARn 0x400 /* Write */
#define L2VIC_SOFT_INTn 0x480 /* Write */
#define L2VIC_INT_PENDINGn 0x500 /* Read */
#define L2VIC_INT_GRPn_0 0x600 /* Read/Write */
#define L2VIC_INT_GRPn_1 0x680 /* Read/Write */
#define L2VIC_INT_GRPn_2 0x700 /* Read/Write */
#define L2VIC_INT_GRPn_3 0x780 /* Read/Write */

#define L2VIC_INTERRUPT_MAX 1024
#define L2VIC_CIAD_INSTRUCTION -1
/*
 * Note about l2vic groups:
 * Each interrupt to L2VIC can be configured to associate with one of
 * four groups.
 * Group 0 interrupts go to IRQ2 via VID 0 (SSR: 0xC2, the default)
 * Group 1 interrupts go to IRQ3 via VID 1 (SSR: 0xC3)
 * Group 2 interrupts go to IRQ4 via VID 2 (SSR: 0xC4)
 * Group 3 interrupts go to IRQ5 via VID 3 (SSR: 0xC5)
 */
