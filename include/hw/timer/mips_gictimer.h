/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2016 Imagination Technologies
 *
 */

#ifndef MIPS_GICTIMER_H
#define MIPS_GICTIMER_H

typedef struct MIPSGICTimerVPState MIPSGICTimerVPState;
typedef struct MIPSGICTimerState MIPSGICTimerState;

typedef void MIPSGICTimerCB(void *opaque, uint32_t vp_index);

struct MIPSGICTimerVPState {
    QEMUTimer *qtimer;
    uint32_t vp_index;
    uint32_t comparelo;
    MIPSGICTimerState *gictimer;
};

struct MIPSGICTimerState {
    void *opaque;
    uint8_t countstop;
    uint32_t sh_counterlo;
    int32_t num_vps;
    MIPSGICTimerVPState *vptimers;
    MIPSGICTimerCB *cb;
};

uint32_t mips_gictimer_get_freq(MIPSGICTimerState *gic);
uint32_t mips_gictimer_get_sh_count(MIPSGICTimerState *gic);
void mips_gictimer_store_sh_count(MIPSGICTimerState *gic, uint64_t count);
uint32_t mips_gictimer_get_vp_compare(MIPSGICTimerState *gictimer,
                                      uint32_t vp_index);
void mips_gictimer_store_vp_compare(MIPSGICTimerState *gic, uint32_t vp_index,
                                    uint64_t compare);
uint8_t mips_gictimer_get_countstop(MIPSGICTimerState *gic);
void mips_gictimer_start_count(MIPSGICTimerState *gic);
void mips_gictimer_stop_count(MIPSGICTimerState *gic);
MIPSGICTimerState *mips_gictimer_init(void *opaque, uint32_t nvps,
                                      MIPSGICTimerCB *cb);

#endif /* MIPS_GICTIMER_H */
