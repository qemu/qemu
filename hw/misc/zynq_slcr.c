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
#include "hw/hw.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"

#ifndef ZYNQ_SLCR_ERR_DEBUG
#define ZYNQ_SLCR_ERR_DEBUG 0
#endif

#define DB_PRINT(...) do { \
        if (ZYNQ_SLCR_ERR_DEBUG) { \
            fprintf(stderr,  ": %s: ", __func__); \
            fprintf(stderr, ## __VA_ARGS__); \
        } \
    } while (0);

#define XILINX_LOCK_KEY 0x767b
#define XILINX_UNLOCK_KEY 0xdf0d

#define R_PSS_RST_CTRL_SOFT_RST 0x1

enum {
    SCL             = 0x000 / 4,
    LOCK,
    UNLOCK,
    LOCKSTA,

    ARM_PLL_CTRL    = 0x100 / 4,
    DDR_PLL_CTRL,
    IO_PLL_CTRL,
    PLL_STATUS,
    ARM_PLL_CFG,
    DDR_PLL_CFG,
    IO_PLL_CFG,

    ARM_CLK_CTRL    = 0x120 / 4,
    DDR_CLK_CTRL,
    DCI_CLK_CTRL,
    APER_CLK_CTRL,
    USB0_CLK_CTRL,
    USB1_CLK_CTRL,
    GEM0_RCLK_CTRL,
    GEM1_RCLK_CTRL,
    GEM0_CLK_CTRL,
    GEM1_CLK_CTRL,
    SMC_CLK_CTRL,
    LQSPI_CLK_CTRL,
    SDIO_CLK_CTRL,
    UART_CLK_CTRL,
    SPI_CLK_CTRL,
    CAN_CLK_CTRL,
    CAN_MIOCLK_CTRL,
    DBG_CLK_CTRL,
    PCAP_CLK_CTRL,
    TOPSW_CLK_CTRL,

#define FPGA_CTRL_REGS(n, start) \
    FPGA ## n ## _CLK_CTRL = (start) / 4, \
    FPGA ## n ## _THR_CTRL, \
    FPGA ## n ## _THR_CNT, \
    FPGA ## n ## _THR_STA,
    FPGA_CTRL_REGS(0, 0x170)
    FPGA_CTRL_REGS(1, 0x180)
    FPGA_CTRL_REGS(2, 0x190)
    FPGA_CTRL_REGS(3, 0x1a0)

    BANDGAP_TRIP    = 0x1b8 / 4,
    PLL_PREDIVISOR  = 0x1c0 / 4,
    CLK_621_TRUE,

    PSS_RST_CTRL    = 0x200 / 4,
    DDR_RST_CTRL,
    TOPSW_RESET_CTRL,
    DMAC_RST_CTRL,
    USB_RST_CTRL,
    GEM_RST_CTRL,
    SDIO_RST_CTRL,
    SPI_RST_CTRL,
    CAN_RST_CTRL,
    I2C_RST_CTRL,
    UART_RST_CTRL,
    GPIO_RST_CTRL,
    LQSPI_RST_CTRL,
    SMC_RST_CTRL,
    OCM_RST_CTRL,
    FPGA_RST_CTRL   = 0x240 / 4,
    A9_CPU_RST_CTRL,

    RS_AWDT_CTRL    = 0x24c / 4,
    RST_REASON,

    REBOOT_STATUS   = 0x258 / 4,
    BOOT_MODE,

    APU_CTRL        = 0x300 / 4,
    WDT_CLK_SEL,

    TZ_DMA_NS       = 0x440 / 4,
    TZ_DMA_IRQ_NS,
    TZ_DMA_PERIPH_NS,

    PSS_IDCODE      = 0x530 / 4,

    DDR_URGENT      = 0x600 / 4,
    DDR_CAL_START   = 0x60c / 4,
    DDR_REF_START   = 0x614 / 4,
    DDR_CMD_STA,
    DDR_URGENT_SEL,
    DDR_DFI_STATUS,

    MIO             = 0x700 / 4,
#define MIO_LENGTH 54

    MIO_LOOPBACK    = 0x804 / 4,
    MIO_MST_TRI0,
    MIO_MST_TRI1,

    SD0_WP_CD_SEL   = 0x830 / 4,
    SD1_WP_CD_SEL,

    LVL_SHFTR_EN    = 0x900 / 4,
    OCM_CFG         = 0x910 / 4,

    CPU_RAM         = 0xa00 / 4,

    IOU             = 0xa30 / 4,

    DMAC_RAM        = 0xa50 / 4,

    AFI0            = 0xa60 / 4,
    AFI1 = AFI0 + 3,
    AFI2 = AFI1 + 3,
    AFI3 = AFI2 + 3,
#define AFI_LENGTH 3

    OCM             = 0xa90 / 4,

    DEVCI_RAM       = 0xaa0 / 4,

    CSG_RAM         = 0xab0 / 4,

    GPIOB_CTRL      = 0xb00 / 4,
    GPIOB_CFG_CMOS18,
    GPIOB_CFG_CMOS25,
    GPIOB_CFG_CMOS33,
    GPIOB_CFG_HSTL  = 0xb14 / 4,
    GPIOB_DRVR_BIAS_CTRL,

    DDRIOB          = 0xb40 / 4,
#define DDRIOB_LENGTH 14
};

#define ZYNQ_SLCR_MMIO_SIZE     0x1000
#define ZYNQ_SLCR_NUM_REGS      (ZYNQ_SLCR_MMIO_SIZE / 4)

#define TYPE_ZYNQ_SLCR "xilinx,zynq_slcr"
#define ZYNQ_SLCR(obj) OBJECT_CHECK(ZynqSLCRState, (obj), TYPE_ZYNQ_SLCR)

typedef struct ZynqSLCRState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t regs[ZYNQ_SLCR_NUM_REGS];
} ZynqSLCRState;

static void zynq_slcr_reset(DeviceState *d)
{
    ZynqSLCRState *s = ZYNQ_SLCR(d);
    int i;

    DB_PRINT("RESET\n");

    s->regs[LOCKSTA] = 1;
    /* 0x100 - 0x11C */
    s->regs[ARM_PLL_CTRL]   = 0x0001A008;
    s->regs[DDR_PLL_CTRL]   = 0x0001A008;
    s->regs[IO_PLL_CTRL]    = 0x0001A008;
    s->regs[PLL_STATUS]     = 0x0000003F;
    s->regs[ARM_PLL_CFG]    = 0x00014000;
    s->regs[DDR_PLL_CFG]    = 0x00014000;
    s->regs[IO_PLL_CFG]     = 0x00014000;

    /* 0x120 - 0x16C */
    s->regs[ARM_CLK_CTRL]   = 0x1F000400;
    s->regs[DDR_CLK_CTRL]   = 0x18400003;
    s->regs[DCI_CLK_CTRL]   = 0x01E03201;
    s->regs[APER_CLK_CTRL]  = 0x01FFCCCD;
    s->regs[USB0_CLK_CTRL]  = s->regs[USB1_CLK_CTRL]    = 0x00101941;
    s->regs[GEM0_RCLK_CTRL] = s->regs[GEM1_RCLK_CTRL]   = 0x00000001;
    s->regs[GEM0_CLK_CTRL]  = s->regs[GEM1_CLK_CTRL]    = 0x00003C01;
    s->regs[SMC_CLK_CTRL]   = 0x00003C01;
    s->regs[LQSPI_CLK_CTRL] = 0x00002821;
    s->regs[SDIO_CLK_CTRL]  = 0x00001E03;
    s->regs[UART_CLK_CTRL]  = 0x00003F03;
    s->regs[SPI_CLK_CTRL]   = 0x00003F03;
    s->regs[CAN_CLK_CTRL]   = 0x00501903;
    s->regs[DBG_CLK_CTRL]   = 0x00000F03;
    s->regs[PCAP_CLK_CTRL]  = 0x00000F01;

    /* 0x170 - 0x1AC */
    s->regs[FPGA0_CLK_CTRL] = s->regs[FPGA1_CLK_CTRL] = s->regs[FPGA2_CLK_CTRL]
                            = s->regs[FPGA3_CLK_CTRL] = 0x00101800;
    s->regs[FPGA0_THR_STA] = s->regs[FPGA1_THR_STA] = s->regs[FPGA2_THR_STA]
                           = s->regs[FPGA3_THR_STA] = 0x00010000;

    /* 0x1B0 - 0x1D8 */
    s->regs[BANDGAP_TRIP]   = 0x0000001F;
    s->regs[PLL_PREDIVISOR] = 0x00000001;
    s->regs[CLK_621_TRUE]   = 0x00000001;

    /* 0x200 - 0x25C */
    s->regs[FPGA_RST_CTRL]  = 0x01F33F0F;
    s->regs[RST_REASON]     = 0x00000040;

    s->regs[BOOT_MODE]      = 0x00000001;

    /* 0x700 - 0x7D4 */
    for (i = 0; i < 54; i++) {
        s->regs[MIO + i] = 0x00001601;
    }
    for (i = 2; i <= 8; i++) {
        s->regs[MIO + i] = 0x00000601;
    }

    s->regs[MIO_MST_TRI0] = s->regs[MIO_MST_TRI1] = 0xFFFFFFFF;

    s->regs[CPU_RAM + 0] = s->regs[CPU_RAM + 1] = s->regs[CPU_RAM + 3]
                         = s->regs[CPU_RAM + 4] = s->regs[CPU_RAM + 7]
                         = 0x00010101;
    s->regs[CPU_RAM + 2] = s->regs[CPU_RAM + 5] = 0x01010101;
    s->regs[CPU_RAM + 6] = 0x00000001;

    s->regs[IOU + 0] = s->regs[IOU + 1] = s->regs[IOU + 2] = s->regs[IOU + 3]
                     = 0x09090909;
    s->regs[IOU + 4] = s->regs[IOU + 5] = 0x00090909;
    s->regs[IOU + 6] = 0x00000909;

    s->regs[DMAC_RAM] = 0x00000009;

    s->regs[AFI0 + 0] = s->regs[AFI0 + 1] = 0x09090909;
    s->regs[AFI1 + 0] = s->regs[AFI1 + 1] = 0x09090909;
    s->regs[AFI2 + 0] = s->regs[AFI2 + 1] = 0x09090909;
    s->regs[AFI3 + 0] = s->regs[AFI3 + 1] = 0x09090909;
    s->regs[AFI0 + 2] = s->regs[AFI1 + 2] = s->regs[AFI2 + 2]
                      = s->regs[AFI3 + 2] = 0x00000909;

    s->regs[OCM + 0]    = 0x01010101;
    s->regs[OCM + 1]    = s->regs[OCM + 2] = 0x09090909;

    s->regs[DEVCI_RAM]  = 0x00000909;
    s->regs[CSG_RAM]    = 0x00000001;

    s->regs[DDRIOB + 0] = s->regs[DDRIOB + 1] = s->regs[DDRIOB + 2]
                        = s->regs[DDRIOB + 3] = 0x00000e00;
    s->regs[DDRIOB + 4] = s->regs[DDRIOB + 5] = s->regs[DDRIOB + 6]
                        = 0x00000e00;
    s->regs[DDRIOB + 12] = 0x00000021;
}


static bool zynq_slcr_check_offset(hwaddr offset, bool rnw)
{
    switch (offset) {
    case LOCK:
    case UNLOCK:
    case DDR_CAL_START:
    case DDR_REF_START:
        return !rnw; /* Write only */
    case LOCKSTA:
    case FPGA0_THR_STA:
    case FPGA1_THR_STA:
    case FPGA2_THR_STA:
    case FPGA3_THR_STA:
    case BOOT_MODE:
    case PSS_IDCODE:
    case DDR_CMD_STA:
    case DDR_DFI_STATUS:
    case PLL_STATUS:
        return rnw;/* read only */
    case SCL:
    case ARM_PLL_CTRL ... IO_PLL_CTRL:
    case ARM_PLL_CFG ... IO_PLL_CFG:
    case ARM_CLK_CTRL ... TOPSW_CLK_CTRL:
    case FPGA0_CLK_CTRL ... FPGA0_THR_CNT:
    case FPGA1_CLK_CTRL ... FPGA1_THR_CNT:
    case FPGA2_CLK_CTRL ... FPGA2_THR_CNT:
    case FPGA3_CLK_CTRL ... FPGA3_THR_CNT:
    case BANDGAP_TRIP:
    case PLL_PREDIVISOR:
    case CLK_621_TRUE:
    case PSS_RST_CTRL ... A9_CPU_RST_CTRL:
    case RS_AWDT_CTRL:
    case RST_REASON:
    case REBOOT_STATUS:
    case APU_CTRL:
    case WDT_CLK_SEL:
    case TZ_DMA_NS ... TZ_DMA_PERIPH_NS:
    case DDR_URGENT:
    case DDR_URGENT_SEL:
    case MIO ... MIO + MIO_LENGTH - 1:
    case MIO_LOOPBACK ... MIO_MST_TRI1:
    case SD0_WP_CD_SEL:
    case SD1_WP_CD_SEL:
    case LVL_SHFTR_EN:
    case OCM_CFG:
    case CPU_RAM:
    case IOU:
    case DMAC_RAM:
    case AFI0 ... AFI3 + AFI_LENGTH - 1:
    case OCM:
    case DEVCI_RAM:
    case CSG_RAM:
    case GPIOB_CTRL ... GPIOB_CFG_CMOS33:
    case GPIOB_CFG_HSTL:
    case GPIOB_DRVR_BIAS_CTRL:
    case DDRIOB ... DDRIOB + DDRIOB_LENGTH - 1:
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
    case SCL:
        s->regs[SCL] = val & 0x1;
        return;
    case LOCK:
        if ((val & 0xFFFF) == XILINX_LOCK_KEY) {
            DB_PRINT("XILINX LOCK 0xF8000000 + 0x%x <= 0x%x\n", (int)offset,
                (unsigned)val & 0xFFFF);
            s->regs[LOCKSTA] = 1;
        } else {
            DB_PRINT("WRONG XILINX LOCK KEY 0xF8000000 + 0x%x <= 0x%x\n",
                (int)offset, (unsigned)val & 0xFFFF);
        }
        return;
    case UNLOCK:
        if ((val & 0xFFFF) == XILINX_UNLOCK_KEY) {
            DB_PRINT("XILINX UNLOCK 0xF8000000 + 0x%x <= 0x%x\n", (int)offset,
                (unsigned)val & 0xFFFF);
            s->regs[LOCKSTA] = 0;
        } else {
            DB_PRINT("WRONG XILINX UNLOCK KEY 0xF8000000 + 0x%x <= 0x%x\n",
                (int)offset, (unsigned)val & 0xFFFF);
        }
        return;
    }

    if (s->regs[LOCKSTA]) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SCLR registers are locked. Unlock them first\n");
        return;
    }
    s->regs[offset] = val;

    switch (offset) {
    case PSS_RST_CTRL:
        if (val & R_PSS_RST_CTRL_SOFT_RST) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
        break;
    }
}

static const MemoryRegionOps slcr_ops = {
    .read = zynq_slcr_read,
    .write = zynq_slcr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void zynq_slcr_init(Object *obj)
{
    ZynqSLCRState *s = ZYNQ_SLCR(obj);

    memory_region_init_io(&s->iomem, obj, &slcr_ops, s, "slcr",
                          ZYNQ_SLCR_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_zynq_slcr = {
    .name = "zynq_slcr",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, ZynqSLCRState, ZYNQ_SLCR_NUM_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void zynq_slcr_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_zynq_slcr;
    dc->reset = zynq_slcr_reset;
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
