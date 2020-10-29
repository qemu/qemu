/*
 * BCM2835 CPRMAN clock manager
 *
 * Copyright (c) 2020 Luc Michel <luc@lmichel.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_CPRMAN_H
#define HW_MISC_CPRMAN_H

#include "hw/sysbus.h"
#include "hw/qdev-clock.h"

#define TYPE_BCM2835_CPRMAN "bcm2835-cprman"

typedef struct BCM2835CprmanState BCM2835CprmanState;

DECLARE_INSTANCE_CHECKER(BCM2835CprmanState, CPRMAN,
                         TYPE_BCM2835_CPRMAN)

#define CPRMAN_NUM_REGS (0x2000 / sizeof(uint32_t))

typedef enum CprmanPll {
    CPRMAN_PLLA = 0,
    CPRMAN_PLLC,
    CPRMAN_PLLD,
    CPRMAN_PLLH,
    CPRMAN_PLLB,

    CPRMAN_NUM_PLL
} CprmanPll;

typedef enum CprmanPllChannel {
    CPRMAN_PLLA_CHANNEL_DSI0 = 0,
    CPRMAN_PLLA_CHANNEL_CORE,
    CPRMAN_PLLA_CHANNEL_PER,
    CPRMAN_PLLA_CHANNEL_CCP2,

    CPRMAN_PLLC_CHANNEL_CORE2,
    CPRMAN_PLLC_CHANNEL_CORE1,
    CPRMAN_PLLC_CHANNEL_PER,
    CPRMAN_PLLC_CHANNEL_CORE0,

    CPRMAN_PLLD_CHANNEL_DSI0,
    CPRMAN_PLLD_CHANNEL_CORE,
    CPRMAN_PLLD_CHANNEL_PER,
    CPRMAN_PLLD_CHANNEL_DSI1,

    CPRMAN_PLLH_CHANNEL_AUX,
    CPRMAN_PLLH_CHANNEL_RCAL,
    CPRMAN_PLLH_CHANNEL_PIX,

    CPRMAN_PLLB_CHANNEL_ARM,

    CPRMAN_NUM_PLL_CHANNEL,

    /* Special values used when connecting clock sources to clocks */
    CPRMAN_CLOCK_SRC_NORMAL = -1,
    CPRMAN_CLOCK_SRC_FORCE_GROUND = -2,
    CPRMAN_CLOCK_SRC_DSI0HSCK = -3,
} CprmanPllChannel;

typedef enum CprmanClockMux {
    CPRMAN_CLOCK_GNRIC,
    CPRMAN_CLOCK_VPU,
    CPRMAN_CLOCK_SYS,
    CPRMAN_CLOCK_PERIA,
    CPRMAN_CLOCK_PERII,
    CPRMAN_CLOCK_H264,
    CPRMAN_CLOCK_ISP,
    CPRMAN_CLOCK_V3D,
    CPRMAN_CLOCK_CAM0,
    CPRMAN_CLOCK_CAM1,
    CPRMAN_CLOCK_CCP2,
    CPRMAN_CLOCK_DSI0E,
    CPRMAN_CLOCK_DSI0P,
    CPRMAN_CLOCK_DPI,
    CPRMAN_CLOCK_GP0,
    CPRMAN_CLOCK_GP1,
    CPRMAN_CLOCK_GP2,
    CPRMAN_CLOCK_HSM,
    CPRMAN_CLOCK_OTP,
    CPRMAN_CLOCK_PCM,
    CPRMAN_CLOCK_PWM,
    CPRMAN_CLOCK_SLIM,
    CPRMAN_CLOCK_SMI,
    CPRMAN_CLOCK_TEC,
    CPRMAN_CLOCK_TD0,
    CPRMAN_CLOCK_TD1,
    CPRMAN_CLOCK_TSENS,
    CPRMAN_CLOCK_TIMER,
    CPRMAN_CLOCK_UART,
    CPRMAN_CLOCK_VEC,
    CPRMAN_CLOCK_PULSE,
    CPRMAN_CLOCK_SDC,
    CPRMAN_CLOCK_ARM,
    CPRMAN_CLOCK_AVEO,
    CPRMAN_CLOCK_EMMC,
    CPRMAN_CLOCK_EMMC2,

    CPRMAN_NUM_CLOCK_MUX
} CprmanClockMux;

typedef enum CprmanClockMuxSource {
    CPRMAN_CLOCK_SRC_GND = 0,
    CPRMAN_CLOCK_SRC_XOSC,
    CPRMAN_CLOCK_SRC_TD0,
    CPRMAN_CLOCK_SRC_TD1,
    CPRMAN_CLOCK_SRC_PLLA,
    CPRMAN_CLOCK_SRC_PLLC,
    CPRMAN_CLOCK_SRC_PLLD,
    CPRMAN_CLOCK_SRC_PLLH,
    CPRMAN_CLOCK_SRC_PLLC_CORE1,
    CPRMAN_CLOCK_SRC_PLLC_CORE2,

    CPRMAN_NUM_CLOCK_MUX_SRC
} CprmanClockMuxSource;

typedef struct CprmanPllState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    CprmanPll id;

    uint32_t *reg_cm;
    uint32_t *reg_a2w_ctrl;
    uint32_t *reg_a2w_ana; /* ANA[0] .. ANA[3] */
    uint32_t prediv_mask; /* prediv bit in ana[1] */
    uint32_t *reg_a2w_frac;

    Clock *xosc_in;
    Clock *out;
} CprmanPllState;

typedef struct CprmanPllChannelState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    CprmanPllChannel id;
    CprmanPll parent;

    uint32_t *reg_cm;
    uint32_t hold_mask;
    uint32_t load_mask;
    uint32_t *reg_a2w_ctrl;
    int fixed_divider;

    Clock *pll_in;
    Clock *out;
} CprmanPllChannelState;

typedef struct CprmanClockMuxState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    CprmanClockMux id;

    uint32_t *reg_ctl;
    uint32_t *reg_div;
    int int_bits;
    int frac_bits;

    Clock *srcs[CPRMAN_NUM_CLOCK_MUX_SRC];
    Clock *out;

    /*
     * Used by clock srcs update callback to retrieve both the clock and the
     * source number.
     */
    struct CprmanClockMuxState *backref[CPRMAN_NUM_CLOCK_MUX_SRC];
} CprmanClockMuxState;

typedef struct CprmanDsi0HsckMuxState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    CprmanClockMux id;

    uint32_t *reg_cm;

    Clock *plla_in;
    Clock *plld_in;
    Clock *out;
} CprmanDsi0HsckMuxState;

struct BCM2835CprmanState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    CprmanPllState plls[CPRMAN_NUM_PLL];
    CprmanPllChannelState channels[CPRMAN_NUM_PLL_CHANNEL];
    CprmanClockMuxState clock_muxes[CPRMAN_NUM_CLOCK_MUX];
    CprmanDsi0HsckMuxState dsi0hsck_mux;

    uint32_t regs[CPRMAN_NUM_REGS];
    uint32_t xosc_freq;

    Clock *xosc;
    Clock *gnd;
};

#endif
