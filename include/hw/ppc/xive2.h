/*
 * QEMU PowerPC XIVE2 interrupt controller model  (POWER10)
 *
 * Copyright (c) 2019-2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PPC_XIVE2_H
#define PPC_XIVE2_H

#include "hw/ppc/xive.h"
#include "hw/ppc/xive2_regs.h"
#include "hw/sysbus.h"

/*
 * XIVE2 Router (POWER10)
 */
typedef struct Xive2Router {
    SysBusDevice    parent;

    XiveFabric *xfb;
} Xive2Router;

#define TYPE_XIVE2_ROUTER "xive2-router"
OBJECT_DECLARE_TYPE(Xive2Router, Xive2RouterClass, XIVE2_ROUTER);

/*
 * Configuration flags
 */

#define XIVE2_GEN1_TIMA_OS      0x00000001
#define XIVE2_VP_SAVE_RESTORE   0x00000002
#define XIVE2_THREADID_8BITS    0x00000004

typedef struct Xive2RouterClass {
    SysBusDeviceClass parent;

    /* XIVE table accessors */
    int (*get_eas)(Xive2Router *xrtr, uint8_t eas_blk, uint32_t eas_idx,
                   Xive2Eas *eas);
    int (*get_pq)(Xive2Router *xrtr, uint8_t eas_blk, uint32_t eas_idx,
                  uint8_t *pq);
    int (*set_pq)(Xive2Router *xrtr, uint8_t eas_blk, uint32_t eas_idx,
                  uint8_t *pq);
    int (*get_end)(Xive2Router *xrtr, uint8_t end_blk, uint32_t end_idx,
                   Xive2End *end);
    int (*write_end)(Xive2Router *xrtr, uint8_t end_blk, uint32_t end_idx,
                     Xive2End *end, uint8_t word_number);
    int (*get_nvp)(Xive2Router *xrtr, uint8_t nvp_blk, uint32_t nvp_idx,
                   Xive2Nvp *nvp);
    int (*write_nvp)(Xive2Router *xrtr, uint8_t nvp_blk, uint32_t nvp_idx,
                     Xive2Nvp *nvp, uint8_t word_number);
    int (*get_nvgc)(Xive2Router *xrtr, bool crowd,
                    uint8_t nvgc_blk, uint32_t nvgc_idx,
                    Xive2Nvgc *nvgc);
    int (*write_nvgc)(Xive2Router *xrtr, bool crowd,
                      uint8_t nvgc_blk, uint32_t nvgc_idx,
                      Xive2Nvgc *nvgc);
    uint8_t (*get_block_id)(Xive2Router *xrtr);
    uint32_t (*get_config)(Xive2Router *xrtr);
} Xive2RouterClass;

int xive2_router_get_eas(Xive2Router *xrtr, uint8_t eas_blk, uint32_t eas_idx,
                        Xive2Eas *eas);
int xive2_router_get_end(Xive2Router *xrtr, uint8_t end_blk, uint32_t end_idx,
                        Xive2End *end);
int xive2_router_write_end(Xive2Router *xrtr, uint8_t end_blk, uint32_t end_idx,
                          Xive2End *end, uint8_t word_number);
int xive2_router_get_nvp(Xive2Router *xrtr, uint8_t nvp_blk, uint32_t nvp_idx,
                        Xive2Nvp *nvp);
int xive2_router_write_nvp(Xive2Router *xrtr, uint8_t nvp_blk, uint32_t nvp_idx,
                          Xive2Nvp *nvp, uint8_t word_number);
int xive2_router_get_nvgc(Xive2Router *xrtr, bool crowd,
                          uint8_t nvgc_blk, uint32_t nvgc_idx,
                          Xive2Nvgc *nvgc);
int xive2_router_write_nvgc(Xive2Router *xrtr, bool crowd,
                            uint8_t nvgc_blk, uint32_t nvgc_idx,
                            Xive2Nvgc *nvgc);
uint32_t xive2_router_get_config(Xive2Router *xrtr);

void xive2_router_notify(XiveNotifier *xn, uint32_t lisn, bool pq_checked);

/*
 * XIVE2 Presenter (POWER10)
 */

int xive2_presenter_tctx_match(XivePresenter *xptr, XiveTCTX *tctx,
                               uint8_t format,
                               uint8_t nvt_blk, uint32_t nvt_idx,
                               bool crowd, bool cam_ignore,
                               uint32_t logic_serv);

uint64_t xive2_presenter_nvp_backlog_op(XivePresenter *xptr,
                                        uint8_t blk, uint32_t idx,
                                        uint16_t offset);

uint64_t xive2_presenter_nvgc_backlog_op(XivePresenter *xptr,
                                         bool crowd,
                                         uint8_t blk, uint32_t idx,
                                         uint16_t offset, uint16_t val);

/*
 * XIVE2 END ESBs  (POWER10)
 */

#define TYPE_XIVE2_END_SOURCE "xive2-end-source"
OBJECT_DECLARE_SIMPLE_TYPE(Xive2EndSource, XIVE2_END_SOURCE)

typedef struct Xive2EndSource {
    DeviceState parent;

    uint32_t        nr_ends;

    /* ESB memory region */
    uint32_t        esb_shift;
    MemoryRegion    esb_mmio;

    Xive2Router     *xrtr;
} Xive2EndSource;

/*
 * XIVE2 Thread Interrupt Management Area (POWER10)
 */

void xive2_tm_set_hv_cppr(XivePresenter *xptr, XiveTCTX *tctx,
                          hwaddr offset, uint64_t value, unsigned size);
void xive2_tm_set_os_cppr(XivePresenter *xptr, XiveTCTX *tctx,
                          hwaddr offset, uint64_t value, unsigned size);
void xive2_tm_push_os_ctx(XivePresenter *xptr, XiveTCTX *tctx, hwaddr offset,
                           uint64_t value, unsigned size);
uint64_t xive2_tm_pull_os_ctx(XivePresenter *xptr, XiveTCTX *tctx,
                               hwaddr offset, unsigned size);
void xive2_tm_pull_os_ctx_ol(XivePresenter *xptr, XiveTCTX *tctx,
                             hwaddr offset, uint64_t value, unsigned size);
bool xive2_tm_irq_precluded(XiveTCTX *tctx, int ring, uint8_t priority);
void xive2_tm_set_lsmfb(XiveTCTX *tctx, int ring, uint8_t priority);
void xive2_tm_set_hv_target(XivePresenter *xptr, XiveTCTX *tctx,
                            hwaddr offset, uint64_t value, unsigned size);
void xive2_tm_pull_phys_ctx_ol(XivePresenter *xptr, XiveTCTX *tctx,
                               hwaddr offset, uint64_t value, unsigned size);

#endif /* PPC_XIVE2_H */
