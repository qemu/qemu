/*
 * QTest testcase for PowerNV 10 interrupt controller (xive2)
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TEST_PNV_XIVE2_COMMON_H
#define TEST_PNV_XIVE2_COMMON_H

#define PPC_BIT(bit)            (0x8000000000000000ULL >> (bit))
#define PPC_BIT32(bit)          (0x80000000 >> (bit))
#define PPC_BIT8(bit)           (0x80 >> (bit))
#define PPC_BITMASK(bs, be)     ((PPC_BIT(bs) - PPC_BIT(be)) | PPC_BIT(bs))
#define PPC_BITMASK32(bs, be)   ((PPC_BIT32(bs) - PPC_BIT32(be)) | \
                                 PPC_BIT32(bs))
#include "qemu/bswap.h"
#include "hw/intc/pnv_xive2_regs.h"
#include "hw/ppc/xive_regs.h"
#include "hw/ppc/xive2_regs.h"

/*
 * sizing:
 * 128 interrupts
 *   => ESB BAR range: 16M
 * 256 ENDs
 *   => END BAR range: 16M
 * 256 VPs
 *   => NVPG,NVC BAR range: 32M
 */
#define MAX_IRQS                128
#define MAX_ENDS                256
#define MAX_VPS                 256

#define XIVE_PAGE_SHIFT         16

#define XIVE_TRIGGER_PAGE       0
#define XIVE_EOI_PAGE           1

#define XIVE_IC_ADDR            0x0006030200000000ull
#define XIVE_IC_TM_INDIRECT     (XIVE_IC_ADDR + (256 << XIVE_PAGE_SHIFT))
#define XIVE_IC_BAR             ((0x3ull << 62) | XIVE_IC_ADDR)
#define XIVE_TM_BAR             0xc006030203180000ull
#define XIVE_ESB_ADDR           0x0006050000000000ull
#define XIVE_ESB_BAR            ((0x3ull << 62) | XIVE_ESB_ADDR)
#define XIVE_END_BAR            0xc006060000000000ull
#define XIVE_NVPG_ADDR          0x0006040000000000ull
#define XIVE_NVPG_BAR           ((0x3ull << 62) | XIVE_NVPG_ADDR)
#define XIVE_NVC_ADDR           0x0006030208000000ull
#define XIVE_NVC_BAR            ((0x3ull << 62) | XIVE_NVC_ADDR)

/*
 * Memory layout
 * A check is done when a table is configured to ensure that the max
 * size of the resource fits in the table.
 */
#define XIVE_VST_SIZE           0x10000ull /* must be at least 4k */

#define XIVE_MEM_START          0x10000000ull
#define XIVE_ESB_MEM            XIVE_MEM_START
#define XIVE_EAS_MEM            (XIVE_ESB_MEM + XIVE_VST_SIZE)
#define XIVE_END_MEM            (XIVE_EAS_MEM + XIVE_VST_SIZE)
#define XIVE_NVP_MEM            (XIVE_END_MEM + XIVE_VST_SIZE)
#define XIVE_NVG_MEM            (XIVE_NVP_MEM + XIVE_VST_SIZE)
#define XIVE_NVC_MEM            (XIVE_NVG_MEM + XIVE_VST_SIZE)
#define XIVE_SYNC_MEM           (XIVE_NVC_MEM + XIVE_VST_SIZE)
#define XIVE_QUEUE_MEM          (XIVE_SYNC_MEM + XIVE_VST_SIZE)
#define XIVE_QUEUE_SIZE         4096 /* per End */
#define XIVE_REPORT_MEM         (XIVE_QUEUE_MEM + XIVE_QUEUE_SIZE * MAX_VPS)
#define XIVE_REPORT_SIZE        256 /* two cache lines per NVP */
#define XIVE_MEM_END            (XIVE_REPORT_MEM + XIVE_REPORT_SIZE * MAX_VPS)

#define P10_XSCOM_BASE          0x000603fc00000000ull
#define XIVE_XSCOM              0x2010800ull

#define XIVE_ESB_RESET          0b00
#define XIVE_ESB_OFF            0b01
#define XIVE_ESB_PENDING        0b10
#define XIVE_ESB_QUEUED         0b11

#define XIVE_ESB_GET            0x800
#define XIVE_ESB_SET_PQ_00      0xc00 /* Load */
#define XIVE_ESB_SET_PQ_01      0xd00 /* Load */
#define XIVE_ESB_SET_PQ_10      0xe00 /* Load */
#define XIVE_ESB_SET_PQ_11      0xf00 /* Load */

#define XIVE_ESB_STORE_EOI      0x400 /* Store */


extern uint64_t pnv_xive_xscom_read(QTestState *qts, uint32_t reg);
extern void pnv_xive_xscom_write(QTestState *qts, uint32_t reg, uint64_t val);
extern uint64_t xive_get_queue_addr(uint32_t end_index);
extern uint8_t get_esb(QTestState *qts, uint32_t index, uint8_t page,
                       uint32_t offset);
extern void set_esb(QTestState *qts, uint32_t index, uint8_t page,
                    uint32_t offset, uint32_t val);
extern void get_nvp(QTestState *qts, uint32_t index, Xive2Nvp* nvp);
extern void set_nvp(QTestState *qts, uint32_t index, uint8_t first);
extern void get_cl_pair(QTestState *qts, Xive2Nvp *nvp, uint8_t *cl_pair);
extern void set_cl_pair(QTestState *qts, Xive2Nvp *nvp, uint8_t *cl_pair);
extern void set_nvg(QTestState *qts, uint32_t index, uint8_t next);
extern void set_eas(QTestState *qts, uint32_t index, uint32_t end_index,
                    uint32_t data);
extern void set_end(QTestState *qts, uint32_t index, uint32_t nvp_index,
                    uint8_t priority, bool i);


void test_flush_sync_inject(QTestState *qts);
void test_nvpg_bar(QTestState *qts);

#endif /* TEST_PNV_XIVE2_COMMON_H */
