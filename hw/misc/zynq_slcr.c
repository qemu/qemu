/*
 * Status and system control registers for Xilinx Zynq Platform
 *
 * Copyright (c) 2011 Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2012 PetaLogix Pty Ltd.
 * Based on hw/arm_sysctl.c, written by Paul Brook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "sysemu/runstate.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/registerfields.h"
#include "hw/qdev-clock.h"
#include "qom/object.h"

#ifndef ZYNQ_SLCR_ERR_DEBUG
#define ZYNQ_SLCR_ERR_DEBUG 0
#endif

#define DB_PRINT(...) do { \
        if (ZYNQ_SLCR_ERR_DEBUG) { \
            fprintf(stderr,  ": %s: ", __func__); \
            fprintf(stderr, ## __VA_ARGS__); \
        } \
    } while (0)

#define XILINX_LOCK_KEY 0x767b
#define XILINX_UNLOCK_KEY 0xdf0d

REG32(SCL, 0x000)
REG32(LOCK, 0x004)
REG32(UNLOCK, 0x008)
REG32(LOCKSTA, 0x00c)

REG32(ARM_PLL_CTRL, 0x100)
REG32(DDR_PLL_CTRL, 0x104)
REG32(IO_PLL_CTRL, 0x108)
/* fields for [ARM|DDR|IO]_PLL_CTRL registers */
    FIELD(xxx_PLL_CTRL, PLL_RESET, 0, 1)
    FIELD(xxx_PLL_CTRL, PLL_PWRDWN, 1, 1)
    FIELD(xxx_PLL_CTRL, PLL_BYPASS_QUAL, 3, 1)
    FIELD(xxx_PLL_CTRL, PLL_BYPASS_FORCE, 4, 1)
    FIELD(xxx_PLL_CTRL, PLL_FPDIV, 12, 7)
REG32(PLL_STATUS, 0x10c)
REG32(ARM_PLL_CFG, 0x110)
REG32(DDR_PLL_CFG, 0x114)
REG32(IO_PLL_CFG, 0x118)

REG32(ARM_CLK_CTRL, 0x120)
REG32(DDR_CLK_CTRL, 0x124)
REG32(DCI_CLK_CTRL, 0x128)
REG32(APER_CLK_CTRL, 0x12c)
REG32(USB0_CLK_CTRL, 0x130)
REG32(USB1_CLK_CTRL, 0x134)
REG32(GEM0_RCLK_CTRL, 0x138)
REG32(GEM1_RCLK_CTRL, 0x13c)
REG32(GEM0_CLK_CTRL, 0x140)
REG32(GEM1_CLK_CTRL, 0x144)
REG32(SMC_CLK_CTRL, 0x148)
REG32(LQSPI_CLK_CTRL, 0x14c)
REG32(SDIO_CLK_CTRL, 0x150)
REG32(UART_CLK_CTRL, 0x154)
    FIELD(UART_CLK_CTRL, CLKACT0, 0, 1)
    FIELD(UART_CLK_CTRL, CLKACT1, 1, 1)
    FIELD(UART_CLK_CTRL, SRCSEL,  4, 2)
    FIELD(UART_CLK_CTRL, DIVISOR, 8, 6)
REG32(SPI_CLK_CTRL, 0x158)
REG32(CAN_CLK_CTRL, 0x15c)
REG32(CAN_MIOCLK_CTRL, 0x160)
REG32(DBG_CLK_CTRL, 0x164)
REG32(PCAP_CLK_CTRL, 0x168)
REG32(TOPSW_CLK_CTRL, 0x16c)

#define FPGA_CTRL_REGS(n, start) \
    REG32(FPGA ## n ## _CLK_CTRL, (start)) \
    REG32(FPGA ## n ## _THR_CTRL, (start) + 0x4)\
    REG32(FPGA ## n ## _THR_CNT,  (start) + 0x8)\
    REG32(FPGA ## n ## _THR_STA,  (start) + 0xc)
FPGA_CTRL_REGS(0, 0x170)
FPGA_CTRL_REGS(1, 0x180)
FPGA_CTRL_REGS(2, 0x190)
FPGA_CTRL_REGS(3, 0x1a0)

REG32(BANDGAP_TRIP, 0x1b8)
REG32(PLL_PREDIVISOR, 0x1c0)
REG32(CLK_621_TRUE, 0x1c4)

REG32(PSS_RST_CTRL, 0x200)
    FIELD(PSS_RST_CTRL, SOFT_RST, 0, 1)
REG32(DDR_RST_CTRL, 0x204)
REG32(TOPSW_RESET_CTRL, 0x208)
REG32(DMAC_RST_CTRL, 0x20c)
REG32(USB_RST_CTRL, 0x210)
REG32(GEM_RST_CTRL, 0x214)
REG32(SDIO_RST_CTRL, 0x218)
REG32(SPI_RST_CTRL, 0x21c)
REG32(CAN_RST_CTRL, 0x220)
REG32(I2C_RST_CTRL, 0x224)
REG32(UART_RST_CTRL, 0x228)
REG32(GPIO_RST_CTRL, 0x22c)
REG32(LQSPI_RST_CTRL, 0x230)
REG32(SMC_RST_CTRL, 0x234)
REG32(OCM_RST_CTRL, 0x238)
REG32(FPGA_RST_CTRL, 0x240)
REG32(A9_CPU_RST_CTRL, 0x244)

REG32(RS_AWDT_CTRL, 0x24c)
REG32(RST_REASON, 0x250)

REG32(REBOOT_STATUS, 0x258)
REG32(BOOT_MODE, 0x25c)

REG32(APU_CTRL, 0x300)
REG32(WDT_CLK_SEL, 0x304)

REG32(TZ_DMA_NS, 0x440)
REG32(TZ_DMA_IRQ_NS, 0x444)
REG32(TZ_DMA_PERIPH_NS, 0x448)

REG32(PSS_IDCODE, 0x530)

REG32(DDR_URGENT, 0x600)
REG32(DDR_CAL_START, 0x60c)
REG32(DDR_REF_START, 0x614)
REG32(DDR_CMD_STA, 0x618)
REG32(DDR_URGENT_SEL, 0x61c)
REG32(DDR_DFI_STATUS, 0x620)

REG32(MIO, 0x700)
#define MIO_LENGTH 54

REG32(MIO_LOOPBACK, 0x804)
REG32(MIO_MST_TRI0, 0x808)
REG32(MIO_MST_TRI1, 0x80c)

REG32(SD0_WP_CD_SEL, 0x830)
REG32(SD1_WP_CD_SEL, 0x834)

REG32(LVL_SHFTR_EN, 0x900)
REG32(OCM_CFG, 0x910)

REG32(CPU_RAM, 0xa00)

REG32(IOU, 0xa30)

REG32(DMAC_RAM, 0xa50)

REG32(AFI0, 0xa60)
REG32(AFI1, 0xa6c)
REG32(AFI2, 0xa78)
REG32(AFI3, 0xa84)
#define AFI_LENGTH 3

REG32(OCM, 0xa90)

REG32(DEVCI_RAM, 0xaa0)

REG32(CSG_RAM, 0xab0)

REG32(GPIOB_CTRL, 0xb00)
REG32(GPIOB_CFG_CMOS18, 0xb04)
REG32(GPIOB_CFG_CMOS25, 0xb08)
REG32(GPIOB_CFG_CMOS33, 0xb0c)
REG32(GPIOB_CFG_HSTL, 0xb14)
REG32(GPIOB_DRVR_BIAS_CTRL, 0xb18)

REG32(DDRIOB, 0xb40)
#define DDRIOB_LENGTH 14

#define ZYNQ_SLCR_MMIO_SIZE     0x1000
#define ZYNQ_SLCR_NUM_REGS      (ZYNQ_SLCR_MMIO_SIZE / 4)

#define TYPE_ZYNQ_SLCR "xilinx-zynq_slcr"
OBJECT_DECLARE_SIMPLE_TYPE(ZynqSLCRState, ZYNQ_SLCR)

struct ZynqSLCRState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t regs[ZYNQ_SLCR_NUM_REGS];

    Clock *ps_clk;
    Clock *uart0_ref_clk;
    Clock *uart1_ref_clk;
};

/*
 * return the output frequency of ARM/DDR/IO pll
 * using input frequency and PLL_CTRL register
 */
static uint64_t zynq_slcr_compute_pll(uint64_t input, uint32_t ctrl_reg)
{
    uint32_t mult = ((ctrl_reg & R_xxx_PLL_CTRL_PLL_FPDIV_MASK) >>
            R_xxx_PLL_CTRL_PLL_FPDIV_SHIFT);

    /* first, check if pll is bypassed */
    if (ctrl_reg & R_xxx_PLL_CTRL_PLL_BYPASS_FORCE_MASK) {
        return input;
    }

    /* is pll disabled ? */
    if (ctrl_reg & (R_xxx_PLL_CTRL_PLL_RESET_MASK |
                    R_xxx_PLL_CTRL_PLL_PWRDWN_MASK)) {
        return 0;
    }

    /* Consider zero feedback as maximum divide ratio possible */
    if (!mult) {
        mult = 1 << R_xxx_PLL_CTRL_PLL_FPDIV_LENGTH;
    }

    /* frequency multiplier -> period division */
    return input / mult;
}

/*
 * return the output period of a clock given:
 * + the periods in an array corresponding to input mux selector
 * + the register xxx_CLK_CTRL value
 * + enable bit index in ctrl register
 *
 * This function makes the assumption that the ctrl_reg value is organized as
 * follows:
 * + bits[13:8]  clock frequency divisor
 * + bits[5:4]   clock mux selector (index in array)
 * + bits[index] clock enable
 */
static uint64_t zynq_slcr_compute_clock(const uint64_t periods[],
                                        uint32_t ctrl_reg,
                                        unsigned index)
{
    uint32_t srcsel = extract32(ctrl_reg, 4, 2); /* bits [5:4] */
    uint32_t divisor = extract32(ctrl_reg, 8, 6); /* bits [13:8] */

    /* first, check if clock is disabled */
    if (((ctrl_reg >> index) & 1u) == 0) {
        return 0;
    }

    /*
     * according to the Zynq technical ref. manual UG585 v1.12.2 in
     * Clocks chapter, section 25.10.1 page 705:
     * "The 6-bit divider provides a divide range of 1 to 63"
     * We follow here what is implemented in linux kernel and consider
     * the 0 value as a bypass (no division).
     */
    /* frequency divisor -> period multiplication */
    return periods[srcsel] * (divisor ? divisor : 1u);
}

/*
 * macro helper around zynq_slcr_compute_clock to avoid repeating
 * the register name.
 */
#define ZYNQ_COMPUTE_CLK(state, plls, reg, enable_field) \
    zynq_slcr_compute_clock((plls), (state)->regs[reg], \
                            reg ## _ ## enable_field ## _SHIFT)

static void zynq_slcr_compute_clocks_internal(ZynqSLCRState *s, uint64_t ps_clk)
{
    uint64_t io_pll = zynq_slcr_compute_pll(ps_clk, s->regs[R_IO_PLL_CTRL]);
    uint64_t arm_pll = zynq_slcr_compute_pll(ps_clk, s->regs[R_ARM_PLL_CTRL]);
    uint64_t ddr_pll = zynq_slcr_compute_pll(ps_clk, s->regs[R_DDR_PLL_CTRL]);

    uint64_t uart_mux[4] = {io_pll, io_pll, arm_pll, ddr_pll};

    /* compute uartX reference clocks */
    clock_set(s->uart0_ref_clk,
              ZYNQ_COMPUTE_CLK(s, uart_mux, R_UART_CLK_CTRL, CLKACT0));
    clock_set(s->uart1_ref_clk,
              ZYNQ_COMPUTE_CLK(s, uart_mux, R_UART_CLK_CTRL, CLKACT1));
}

/**
 * Compute and set the outputs clocks periods.
 * But do not propagate them further. Connected clocks
 * will not receive any updates (See zynq_slcr_compute_clocks())
 */
static void zynq_slcr_compute_clocks(ZynqSLCRState *s)
{
    uint64_t ps_clk = clock_get(s->ps_clk);

    /* consider outputs clocks are disabled while in reset */
    if (device_is_in_reset(DEVICE(s))) {
        ps_clk = 0;
    }

    zynq_slcr_compute_clocks_internal(s, ps_clk);
}

/**
 * Propagate the outputs clocks.
 * zynq_slcr_compute_clocks() should have been called before
 * to configure them.
 */
static void zynq_slcr_propagate_clocks(ZynqSLCRState *s)
{
    clock_propagate(s->uart0_ref_clk);
    clock_propagate(s->uart1_ref_clk);
}

static void zynq_slcr_ps_clk_callback(void *opaque, ClockEvent event)
{
    ZynqSLCRState *s = (ZynqSLCRState *) opaque;

    zynq_slcr_compute_clocks(s);
    zynq_slcr_propagate_clocks(s);
}

static void zynq_slcr_reset_init(Object *obj, ResetType type)
{
    ZynqSLCRState *s = ZYNQ_SLCR(obj);
    int i;

    DB_PRINT("RESET\n");

    s->regs[R_LOCKSTA] = 1;
    /* 0x100 - 0x11C */
    s->regs[R_ARM_PLL_CTRL]   = 0x0001A008;
    s->regs[R_DDR_PLL_CTRL]   = 0x0001A008;
    s->regs[R_IO_PLL_CTRL]    = 0x0001A008;
    s->regs[R_PLL_STATUS]     = 0x0000003F;
    s->regs[R_ARM_PLL_CFG]    = 0x00014000;
    s->regs[R_DDR_PLL_CFG]    = 0x00014000;
    s->regs[R_IO_PLL_CFG]     = 0x00014000;

    /* 0x120 - 0x16C */
    s->regs[R_ARM_CLK_CTRL]   = 0x1F000400;
    s->regs[R_DDR_CLK_CTRL]   = 0x18400003;
    s->regs[R_DCI_CLK_CTRL]   = 0x01E03201;
    s->regs[R_APER_CLK_CTRL]  = 0x01FFCCCD;
    s->regs[R_USB0_CLK_CTRL]  = s->regs[R_USB1_CLK_CTRL]  = 0x00101941;
    s->regs[R_GEM0_RCLK_CTRL] = s->regs[R_GEM1_RCLK_CTRL] = 0x00000001;
    s->regs[R_GEM0_CLK_CTRL]  = s->regs[R_GEM1_CLK_CTRL]  = 0x00003C01;
    s->regs[R_SMC_CLK_CTRL]   = 0x00003C01;
    s->regs[R_LQSPI_CLK_CTRL] = 0x00002821;
    s->regs[R_SDIO_CLK_CTRL]  = 0x00001E03;
    s->regs[R_UART_CLK_CTRL]  = 0x00003F03;
    s->regs[R_SPI_CLK_CTRL]   = 0x00003F03;
    s->regs[R_CAN_CLK_CTRL]   = 0x00501903;
    s->regs[R_DBG_CLK_CTRL]   = 0x00000F03;
    s->regs[R_PCAP_CLK_CTRL]  = 0x00000F01;

    /* 0x170 - 0x1AC */
    s->regs[R_FPGA0_CLK_CTRL] = s->regs[R_FPGA1_CLK_CTRL]
                              = s->regs[R_FPGA2_CLK_CTRL]
                              = s->regs[R_FPGA3_CLK_CTRL] = 0x00101800;
    s->regs[R_FPGA0_THR_STA] = s->regs[R_FPGA1_THR_STA]
                             = s->regs[R_FPGA2_THR_STA]
                             = s->regs[R_FPGA3_THR_STA] = 0x00010000;

    /* 0x1B0 - 0x1D8 */
    s->regs[R_BANDGAP_TRIP]   = 0x0000001F;
    s->regs[R_PLL_PREDIVISOR] = 0x00000001;
    s->regs[R_CLK_621_TRUE]   = 0x00000001;

    /* 0x200 - 0x25C */
    s->regs[R_FPGA_RST_CTRL]  = 0x01F33F0F;
    s->regs[R_RST_REASON]     = 0x00000040;

    s->regs[R_BOOT_MODE]      = 0x00000001;

    /* 0x700 - 0x7D4 */
    for (i = 0; i < 54; i++) {
        s->regs[R_MIO + i] = 0x00001601;
    }
    for (i = 2; i <= 8; i++) {
        s->regs[R_MIO + i] = 0x00000601;
    }

    s->regs[R_MIO_MST_TRI0] = s->regs[R_MIO_MST_TRI1] = 0xFFFFFFFF;

    s->regs[R_CPU_RAM + 0] = s->regs[R_CPU_RAM + 1] = s->regs[R_CPU_RAM + 3]
                           = s->regs[R_CPU_RAM + 4] = s->regs[R_CPU_RAM + 7]
                           = 0x00010101;
    s->regs[R_CPU_RAM + 2] = s->regs[R_CPU_RAM + 5] = 0x01010101;
    s->regs[R_CPU_RAM + 6] = 0x00000001;

    s->regs[R_IOU + 0] = s->regs[R_IOU + 1] = s->regs[R_IOU + 2]
                       = s->regs[R_IOU + 3] = 0x09090909;
    s->regs[R_IOU + 4] = s->regs[R_IOU + 5] = 0x00090909;
    s->regs[R_IOU + 6] = 0x00000909;

    s->regs[R_DMAC_RAM] = 0x00000009;

    s->regs[R_AFI0 + 0] = s->regs[R_AFI0 + 1] = 0x09090909;
    s->regs[R_AFI1 + 0] = s->regs[R_AFI1 + 1] = 0x09090909;
    s->regs[R_AFI2 + 0] = s->regs[R_AFI2 + 1] = 0x09090909;
    s->regs[R_AFI3 + 0] = s->regs[R_AFI3 + 1] = 0x09090909;
    s->regs[R_AFI0 + 2] = s->regs[R_AFI1 + 2] = s->regs[R_AFI2 + 2]
                        = s->regs[R_AFI3 + 2] = 0x00000909;

    s->regs[R_OCM + 0] = 0x01010101;
    s->regs[R_OCM + 1] = s->regs[R_OCM + 2] = 0x09090909;

    s->regs[R_DEVCI_RAM] = 0x00000909;
    s->regs[R_CSG_RAM]   = 0x00000001;

    s->regs[R_DDRIOB + 0] = s->regs[R_DDRIOB + 1] = s->regs[R_DDRIOB + 2]
                          = s->regs[R_DDRIOB + 3] = 0x00000e00;
    s->regs[R_DDRIOB + 4] = s->regs[R_DDRIOB + 5] = s->regs[R_DDRIOB + 6]
                          = 0x00000e00;
    s->regs[R_DDRIOB + 12] = 0x00000021;
}

static void zynq_slcr_reset_hold(Object *obj)
{
    ZynqSLCRState *s = ZYNQ_SLCR(obj);

    /* will disable all output clocks */
    zynq_slcr_compute_clocks_internal(s, 0);
    zynq_slcr_propagate_clocks(s);
}

static void zynq_slcr_reset_exit(Object *obj)
{
    ZynqSLCRState *s = ZYNQ_SLCR(obj);

    /* will compute output clocks according to ps_clk and registers */
    zynq_slcr_compute_clocks_internal(s, clock_get(s->ps_clk));
    zynq_slcr_propagate_clocks(s);
}

static bool zynq_slcr_check_offset(hwaddr offset, bool rnw)
{
    switch (offset) {
    case R_LOCK:
    case R_UNLOCK:
    case R_DDR_CAL_START:
    case R_DDR_REF_START:
        return !rnw; /* Write only */
    case R_LOCKSTA:
    case R_FPGA0_THR_STA:
    case R_FPGA1_THR_STA:
    case R_FPGA2_THR_STA:
    case R_FPGA3_THR_STA:
    case R_BOOT_MODE:
    case R_PSS_IDCODE:
    case R_DDR_CMD_STA:
    case R_DDR_DFI_STATUS:
    case R_PLL_STATUS:
        return rnw;/* read only */
    case R_SCL:
    case R_ARM_PLL_CTRL ... R_IO_PLL_CTRL:
    case R_ARM_PLL_CFG ... R_IO_PLL_CFG:
    case R_ARM_CLK_CTRL ... R_TOPSW_CLK_CTRL:
    case R_FPGA0_CLK_CTRL ... R_FPGA0_THR_CNT:
    case R_FPGA1_CLK_CTRL ... R_FPGA1_THR_CNT:
    case R_FPGA2_CLK_CTRL ... R_FPGA2_THR_CNT:
    case R_FPGA3_CLK_CTRL ... R_FPGA3_THR_CNT:
    case R_BANDGAP_TRIP:
    case R_PLL_PREDIVISOR:
    case R_CLK_621_TRUE:
    case R_PSS_RST_CTRL ... R_A9_CPU_RST_CTRL:
    case R_RS_AWDT_CTRL:
    case R_RST_REASON:
    case R_REBOOT_STATUS:
    case R_APU_CTRL:
    case R_WDT_CLK_SEL:
    case R_TZ_DMA_NS ... R_TZ_DMA_PERIPH_NS:
    case R_DDR_URGENT:
    case R_DDR_URGENT_SEL:
    case R_MIO ... R_MIO + MIO_LENGTH - 1:
    case R_MIO_LOOPBACK ... R_MIO_MST_TRI1:
    case R_SD0_WP_CD_SEL:
    case R_SD1_WP_CD_SEL:
    case R_LVL_SHFTR_EN:
    case R_OCM_CFG:
    case R_CPU_RAM:
    case R_IOU:
    case R_DMAC_RAM:
    case R_AFI0 ... R_AFI3 + AFI_LENGTH - 1:
    case R_OCM:
    case R_DEVCI_RAM:
    case R_CSG_RAM:
    case R_GPIOB_CTRL ... R_GPIOB_CFG_CMOS33:
    case R_GPIOB_CFG_HSTL:
    case R_GPIOB_DRVR_BIAS_CTRL:
    case R_DDRIOB ... R_DDRIOB + DDRIOB_LENGTH - 1:
        return true;
    default:
        return false;
    }
}

static uint64_t zynq_slcr_read(void *opaque, hwaddr offset,
    unsigned size)
{
    ZynqSLCRState *s = opaque;
    offset /= 4;
    uint32_t ret = s->regs[offset];

    if (!zynq_slcr_check_offset(offset, true)) {
        qemu_log_mask(LOG_GUEST_ERROR, "zynq_slcr: Invalid read access to "
                      " addr %" HWADDR_PRIx "\n", offset * 4);
    }

    DB_PRINT("addr: %08" HWADDR_PRIx " data: %08" PRIx32 "\n", offset * 4, ret);
    return ret;
}

static void zynq_slcr_write(void *opaque, hwaddr offset,
                          uint64_t val, unsigned size)
{
    ZynqSLCRState *s = (ZynqSLCRState *)opaque;
    offset /= 4;

    DB_PRINT("addr: %08" HWADDR_PRIx " data: %08" PRIx64 "\n", offset * 4, val);

    if (!zynq_slcr_check_offset(offset, false)) {
        qemu_log_mask(LOG_GUEST_ERROR, "zynq_slcr: Invalid write access to "
                      "addr %" HWADDR_PRIx "\n", offset * 4);
        return;
    }

    switch (offset) {
    case R_SCL:
        s->regs[R_SCL] = val & 0x1;
        return;
    case R_LOCK:
        if ((val & 0xFFFF) == XILINX_LOCK_KEY) {
            DB_PRINT("XILINX LOCK 0xF8000000 + 0x%x <= 0x%x\n", (int)offset,
                (unsigned)val & 0xFFFF);
            s->regs[R_LOCKSTA] = 1;
        } else {
            DB_PRINT("WRONG XILINX LOCK KEY 0xF8000000 + 0x%x <= 0x%x\n",
                (int)offset, (unsigned)val & 0xFFFF);
        }
        return;
    case R_UNLOCK:
        if ((val & 0xFFFF) == XILINX_UNLOCK_KEY) {
            DB_PRINT("XILINX UNLOCK 0xF8000000 + 0x%x <= 0x%x\n", (int)offset,
                (unsigned)val & 0xFFFF);
            s->regs[R_LOCKSTA] = 0;
        } else {
            DB_PRINT("WRONG XILINX UNLOCK KEY 0xF8000000 + 0x%x <= 0x%x\n",
                (int)offset, (unsigned)val & 0xFFFF);
        }
        return;
    }

    if (s->regs[R_LOCKSTA]) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SCLR registers are locked. Unlock them first\n");
        return;
    }
    s->regs[offset] = val;

    switch (offset) {
    case R_PSS_RST_CTRL:
        if (FIELD_EX32(val, PSS_RST_CTRL, SOFT_RST)) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
        break;
    case R_IO_PLL_CTRL:
    case R_ARM_PLL_CTRL:
    case R_DDR_PLL_CTRL:
    case R_UART_CLK_CTRL:
        zynq_slcr_compute_clocks(s);
        zynq_slcr_propagate_clocks(s);
        break;
    }
}

static const MemoryRegionOps slcr_ops = {
    .read = zynq_slcr_read,
    .write = zynq_slcr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const ClockPortInitArray zynq_slcr_clocks = {
    QDEV_CLOCK_IN(ZynqSLCRState, ps_clk, zynq_slcr_ps_clk_callback, ClockUpdate),
    QDEV_CLOCK_OUT(ZynqSLCRState, uart0_ref_clk),
    QDEV_CLOCK_OUT(ZynqSLCRState, uart1_ref_clk),
    QDEV_CLOCK_END
};

static void zynq_slcr_init(Object *obj)
{
    ZynqSLCRState *s = ZYNQ_SLCR(obj);

    memory_region_init_io(&s->iomem, obj, &slcr_ops, s, "slcr",
                          ZYNQ_SLCR_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    qdev_init_clocks(DEVICE(obj), zynq_slcr_clocks);
}

static const VMStateDescription vmstate_zynq_slcr = {
    .name = "zynq_slcr",
    .version_id = 3,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, ZynqSLCRState, ZYNQ_SLCR_NUM_REGS),
        VMSTATE_CLOCK_V(ps_clk, ZynqSLCRState, 3),
        VMSTATE_END_OF_LIST()
    }
};

static void zynq_slcr_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_zynq_slcr;
    rc->phases.enter = zynq_slcr_reset_init;
    rc->phases.hold  = zynq_slcr_reset_hold;
    rc->phases.exit  = zynq_slcr_reset_exit;
}

static const TypeInfo zynq_slcr_info = {
    .class_init = zynq_slcr_class_init,
    .name  = TYPE_ZYNQ_SLCR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(ZynqSLCRState),
    .instance_init = zynq_slcr_init,
};

static void zynq_slcr_register_types(void)
{
    type_register_static(&zynq_slcr_info);
}

type_init(zynq_slcr_register_types)
