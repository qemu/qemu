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

#include "hw/hw.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"

#ifdef ZYNQ_ARM_SLCR_ERR_DEBUG
#define DB_PRINT(...) do { \
    fprintf(stderr,  ": %s: ", __func__); \
    fprintf(stderr, ## __VA_ARGS__); \
    } while (0);
#else
    #define DB_PRINT(...)
#endif

#define XILINX_LOCK_KEY 0x767b
#define XILINX_UNLOCK_KEY 0xdf0d

typedef enum {
  ARM_PLL_CTRL,
  DDR_PLL_CTRL,
  IO_PLL_CTRL,
  PLL_STATUS,
  ARM_PPL_CFG,
  DDR_PLL_CFG,
  IO_PLL_CFG,
  PLL_BG_CTRL,
  PLL_MAX
} PLLValues;

typedef enum {
  ARM_CLK_CTRL,
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
  CLK_MAX
} ClkValues;

typedef enum {
  CLK_CTRL,
  THR_CTRL,
  THR_CNT,
  THR_STA,
  FPGA_MAX
} FPGAValues;

typedef enum {
  SYNC_CTRL,
  SYNC_STATUS,
  BANDGAP_TRIP,
  CC_TEST,
  PLL_PREDIVISOR,
  CLK_621_TRUE,
  PICTURE_DBG,
  PICTURE_DBG_UCNT,
  PICTURE_DBG_LCNT,
  MISC_MAX
} MiscValues;

typedef enum {
  PSS,
  DDDR,
  DMAC = 3,
  USB,
  GEM,
  SDIO,
  SPI,
  CAN,
  I2C,
  UART,
  GPIO,
  LQSPI,
  SMC,
  OCM,
  DEVCI,
  FPGA,
  A9_CPU,
  RS_AWDT,
  RST_REASON,
  RST_REASON_CLR,
  REBOOT_STATUS,
  BOOT_MODE,
  RESET_MAX
} ResetValues;

typedef struct {
    SysBusDevice busdev;
    MemoryRegion iomem;

    union {
        struct {
            uint16_t scl;
            uint16_t lockval;
            uint32_t pll[PLL_MAX]; /* 0x100 - 0x11C */
            uint32_t clk[CLK_MAX]; /* 0x120 - 0x16C */
            uint32_t fpga[4][FPGA_MAX]; /* 0x170 - 0x1AC */
            uint32_t misc[MISC_MAX]; /* 0x1B0 - 0x1D8 */
            uint32_t reset[RESET_MAX]; /* 0x200 - 0x25C */
            uint32_t apu_ctrl; /* 0x300 */
            uint32_t wdt_clk_sel; /* 0x304 */
            uint32_t tz_ocm[3]; /* 0x400 - 0x408 */
            uint32_t tz_ddr; /* 0x430 */
            uint32_t tz_dma[3]; /* 0x440 - 0x448 */
            uint32_t tz_misc[3]; /* 0x450 - 0x458 */
            uint32_t tz_fpga[2]; /* 0x484 - 0x488 */
            uint32_t dbg_ctrl; /* 0x500 */
            uint32_t pss_idcode; /* 0x530 */
            uint32_t ddr[8]; /* 0x600 - 0x620 - 0x604-missing */
            uint32_t mio[54]; /* 0x700 - 0x7D4 */
            uint32_t mio_func[4]; /* 0x800 - 0x810 */
            uint32_t sd[2]; /* 0x830 - 0x834 */
            uint32_t lvl_shftr_en; /* 0x900 */
            uint32_t ocm_cfg; /* 0x910 */
            uint32_t cpu_ram[8]; /* 0xA00 - 0xA1C */
            uint32_t iou[7]; /* 0xA30 - 0xA48 */
            uint32_t dmac_ram; /* 0xA50 */
            uint32_t afi[4][3]; /* 0xA60 - 0xA8C */
            uint32_t ocm[3]; /* 0xA90 - 0xA98 */
            uint32_t devci_ram; /* 0xAA0 */
            uint32_t csg_ram; /* 0xAB0 */
            uint32_t gpiob[12]; /* 0xB00 - 0xB2C */
            uint32_t ddriob[14]; /* 0xB40 - 0xB74 */
        };
        uint8_t data[0x1000];
    };
} ZynqSLCRState;

static void zynq_slcr_reset(DeviceState *d)
{
    int i;
    ZynqSLCRState *s =
            FROM_SYSBUS(ZynqSLCRState, SYS_BUS_DEVICE(d));

    DB_PRINT("RESET\n");

    s->lockval = 1;
    /* 0x100 - 0x11C */
    s->pll[ARM_PLL_CTRL] = 0x0001A008;
    s->pll[DDR_PLL_CTRL] = 0x0001A008;
    s->pll[IO_PLL_CTRL] = 0x0001A008;
    s->pll[PLL_STATUS] = 0x0000003F;
    s->pll[ARM_PPL_CFG] = 0x00014000;
    s->pll[DDR_PLL_CFG] = 0x00014000;
    s->pll[IO_PLL_CFG] = 0x00014000;

    /* 0x120 - 0x16C */
    s->clk[ARM_CLK_CTRL] = 0x1F000400;
    s->clk[DDR_CLK_CTRL] = 0x18400003;
    s->clk[DCI_CLK_CTRL] = 0x01E03201;
    s->clk[APER_CLK_CTRL] = 0x01FFCCCD;
    s->clk[USB0_CLK_CTRL] = s->clk[USB1_CLK_CTRL] = 0x00101941;
    s->clk[GEM0_RCLK_CTRL] = s->clk[GEM1_RCLK_CTRL] = 0x00000001;
    s->clk[GEM0_CLK_CTRL] = s->clk[GEM1_CLK_CTRL] = 0x00003C01;
    s->clk[SMC_CLK_CTRL] = 0x00003C01;
    s->clk[LQSPI_CLK_CTRL] = 0x00002821;
    s->clk[SDIO_CLK_CTRL] = 0x00001E03;
    s->clk[UART_CLK_CTRL] = 0x00003F03;
    s->clk[SPI_CLK_CTRL] = 0x00003F03;
    s->clk[CAN_CLK_CTRL] = 0x00501903;
    s->clk[DBG_CLK_CTRL] = 0x00000F03;
    s->clk[PCAP_CLK_CTRL] = 0x00000F01;

    /* 0x170 - 0x1AC */
    s->fpga[0][CLK_CTRL] = s->fpga[1][CLK_CTRL] = s->fpga[2][CLK_CTRL] =
            s->fpga[3][CLK_CTRL] = 0x00101800;
    s->fpga[0][THR_STA] = s->fpga[1][THR_STA] = s->fpga[2][THR_STA] =
            s->fpga[3][THR_STA] = 0x00010000;

    /* 0x1B0 - 0x1D8 */
    s->misc[BANDGAP_TRIP] = 0x0000001F;
    s->misc[PLL_PREDIVISOR] = 0x00000001;
    s->misc[CLK_621_TRUE] = 0x00000001;

    /* 0x200 - 0x25C */
    s->reset[FPGA] = 0x01F33F0F;
    s->reset[RST_REASON] = 0x00000040;

    /* 0x700 - 0x7D4 */
    for (i = 0; i < 54; i++) {
        s->mio[i] = 0x00001601;
    }
    for (i = 2; i <= 8; i++) {
        s->mio[i] = 0x00000601;
    }

    /* MIO_MST_TRI0, MIO_MST_TRI1 */
    s->mio_func[2] = s->mio_func[3] = 0xFFFFFFFF;

    s->cpu_ram[0] = s->cpu_ram[1] = s->cpu_ram[3] =
            s->cpu_ram[4] = s->cpu_ram[7] = 0x00010101;
    s->cpu_ram[2] = s->cpu_ram[5] = 0x01010101;
    s->cpu_ram[6] = 0x00000001;

    s->iou[0] = s->iou[1] = s->iou[2] = s->iou[3] = 0x09090909;
    s->iou[4] = s->iou[5] = 0x00090909;
    s->iou[6] = 0x00000909;

    s->dmac_ram = 0x00000009;

    s->afi[0][0] = s->afi[0][1] = 0x09090909;
    s->afi[1][0] = s->afi[1][1] = 0x09090909;
    s->afi[2][0] = s->afi[2][1] = 0x09090909;
    s->afi[3][0] = s->afi[3][1] = 0x09090909;
    s->afi[0][2] = s->afi[1][2] = s->afi[2][2] = s->afi[3][2] = 0x00000909;

    s->ocm[0] = 0x01010101;
    s->ocm[1] = s->ocm[2] = 0x09090909;

    s->devci_ram = 0x00000909;
    s->csg_ram = 0x00000001;

    s->ddriob[0] = s->ddriob[1] = s->ddriob[2] = s->ddriob[3] = 0x00000e00;
    s->ddriob[4] = s->ddriob[5] = s->ddriob[6] = 0x00000e00;
    s->ddriob[12] = 0x00000021;
}

static inline uint32_t zynq_slcr_read_imp(void *opaque,
    hwaddr offset)
{
    ZynqSLCRState *s = (ZynqSLCRState *)opaque;

    switch (offset) {
    case 0x0: /* SCL */
        return s->scl;
    case 0x4: /* LOCK */
    case 0x8: /* UNLOCK */
        DB_PRINT("Reading SCLR_LOCK/UNLOCK is not enabled\n");
        return 0;
    case 0x0C: /* LOCKSTA */
        return s->lockval;
    case 0x100 ... 0x11C:
        return s->pll[(offset - 0x100) / 4];
    case 0x120 ... 0x16C:
        return s->clk[(offset - 0x120) / 4];
    case 0x170 ... 0x1AC:
        return s->fpga[0][(offset - 0x170) / 4];
    case 0x1B0 ... 0x1D8:
        return s->misc[(offset - 0x1B0) / 4];
    case 0x200 ... 0x258:
        return s->reset[(offset - 0x200) / 4];
    case 0x25c:
        return 1;
    case 0x300:
        return s->apu_ctrl;
    case 0x304:
        return s->wdt_clk_sel;
    case 0x400 ... 0x408:
        return s->tz_ocm[(offset - 0x400) / 4];
    case 0x430:
        return s->tz_ddr;
    case 0x440 ... 0x448:
        return s->tz_dma[(offset - 0x440) / 4];
    case 0x450 ... 0x458:
        return s->tz_misc[(offset - 0x450) / 4];
    case 0x484 ... 0x488:
        return s->tz_fpga[(offset - 0x484) / 4];
    case 0x500:
        return s->dbg_ctrl;
    case 0x530:
        return s->pss_idcode;
    case 0x600 ... 0x620:
        if (offset == 0x604) {
            goto bad_reg;
        }
        return s->ddr[(offset - 0x600) / 4];
    case 0x700 ... 0x7D4:
        return s->mio[(offset - 0x700) / 4];
    case 0x800 ... 0x810:
        return s->mio_func[(offset - 0x800) / 4];
    case 0x830 ... 0x834:
        return s->sd[(offset - 0x830) / 4];
    case 0x900:
        return s->lvl_shftr_en;
    case 0x910:
        return s->ocm_cfg;
    case 0xA00 ... 0xA1C:
        return s->cpu_ram[(offset - 0xA00) / 4];
    case 0xA30 ... 0xA48:
        return s->iou[(offset - 0xA30) / 4];
    case 0xA50:
        return s->dmac_ram;
    case 0xA60 ... 0xA8C:
        return s->afi[0][(offset - 0xA60) / 4];
    case 0xA90 ... 0xA98:
        return s->ocm[(offset - 0xA90) / 4];
    case 0xAA0:
        return s->devci_ram;
    case 0xAB0:
        return s->csg_ram;
    case 0xB00 ... 0xB2C:
        return s->gpiob[(offset - 0xB00) / 4];
    case 0xB40 ... 0xB74:
        return s->ddriob[(offset - 0xB40) / 4];
    default:
    bad_reg:
        DB_PRINT("Bad register offset 0x%x\n", (int)offset);
        return 0;
    }
}

static uint64_t zynq_slcr_read(void *opaque, hwaddr offset,
    unsigned size)
{
    uint32_t ret = zynq_slcr_read_imp(opaque, offset);

    DB_PRINT("addr: %08x data: %08x\n", (unsigned)offset, (unsigned)ret);
    return ret;
}

static void zynq_slcr_write(void *opaque, hwaddr offset,
                          uint64_t val, unsigned size)
{
    ZynqSLCRState *s = (ZynqSLCRState *)opaque;

    DB_PRINT("offset: %08x data: %08x\n", (unsigned)offset, (unsigned)val);

    switch (offset) {
    case 0x00: /* SCL */
        s->scl = val & 0x1;
    return;
    case 0x4: /* SLCR_LOCK */
        if ((val & 0xFFFF) == XILINX_LOCK_KEY) {
            DB_PRINT("XILINX LOCK 0xF8000000 + 0x%x <= 0x%x\n", (int)offset,
                (unsigned)val & 0xFFFF);
            s->lockval = 1;
        } else {
            DB_PRINT("WRONG XILINX LOCK KEY 0xF8000000 + 0x%x <= 0x%x\n",
                (int)offset, (unsigned)val & 0xFFFF);
        }
        return;
    case 0x8: /* SLCR_UNLOCK */
        if ((val & 0xFFFF) == XILINX_UNLOCK_KEY) {
            DB_PRINT("XILINX UNLOCK 0xF8000000 + 0x%x <= 0x%x\n", (int)offset,
                (unsigned)val & 0xFFFF);
            s->lockval = 0;
        } else {
            DB_PRINT("WRONG XILINX UNLOCK KEY 0xF8000000 + 0x%x <= 0x%x\n",
                (int)offset, (unsigned)val & 0xFFFF);
        }
        return;
    case 0xc: /* LOCKSTA */
        DB_PRINT("Writing SCLR_LOCKSTA is not enabled\n");
        return;
    }

    if (!s->lockval) {
        switch (offset) {
        case 0x100 ... 0x11C:
            if (offset == 0x10C) {
                goto bad_reg;
            }
            s->pll[(offset - 0x100) / 4] = val;
            break;
        case 0x120 ... 0x16C:
            s->clk[(offset - 0x120) / 4] = val;
            break;
        case 0x170 ... 0x1AC:
            s->fpga[0][(offset - 0x170) / 4] = val;
            break;
        case 0x1B0 ... 0x1D8:
            s->misc[(offset - 0x1B0) / 4] = val;
            break;
        case 0x200 ... 0x25C:
            if (offset == 0x250) {
                goto bad_reg;
            }
            s->reset[(offset - 0x200) / 4] = val;
            break;
        case 0x300:
            s->apu_ctrl = val;
            break;
        case 0x304:
            s->wdt_clk_sel = val;
            break;
        case 0x400 ... 0x408:
            s->tz_ocm[(offset - 0x400) / 4] = val;
            break;
        case 0x430:
            s->tz_ddr = val;
            break;
        case 0x440 ... 0x448:
            s->tz_dma[(offset - 0x440) / 4] = val;
            break;
        case 0x450 ... 0x458:
            s->tz_misc[(offset - 0x450) / 4] = val;
            break;
        case 0x484 ... 0x488:
            s->tz_fpga[(offset - 0x484) / 4] = val;
            break;
        case 0x500:
            s->dbg_ctrl = val;
            break;
        case 0x530:
            s->pss_idcode = val;
            break;
        case 0x600 ... 0x620:
            if (offset == 0x604) {
                goto bad_reg;
            }
            s->ddr[(offset - 0x600) / 4] = val;
            break;
        case 0x700 ... 0x7D4:
            s->mio[(offset - 0x700) / 4] = val;
            break;
        case 0x800 ... 0x810:
            s->mio_func[(offset - 0x800) / 4] = val;
            break;
        case 0x830 ... 0x834:
            s->sd[(offset - 0x830) / 4] = val;
            break;
        case 0x900:
            s->lvl_shftr_en = val;
            break;
        case 0x910:
            break;
        case 0xA00 ... 0xA1C:
            s->cpu_ram[(offset - 0xA00) / 4] = val;
            break;
        case 0xA30 ... 0xA48:
            s->iou[(offset - 0xA30) / 4] = val;
            break;
        case 0xA50:
            s->dmac_ram = val;
            break;
        case 0xA60 ... 0xA8C:
            s->afi[0][(offset - 0xA60) / 4] = val;
            break;
        case 0xA90:
            s->ocm[0] = val;
            break;
        case 0xAA0:
            s->devci_ram = val;
            break;
        case 0xAB0:
            s->csg_ram = val;
            break;
        case 0xB00 ... 0xB2C:
            if (offset == 0xB20 || offset == 0xB2C) {
                goto bad_reg;
            }
            s->gpiob[(offset - 0xB00) / 4] = val;
            break;
        case 0xB40 ... 0xB74:
            s->ddriob[(offset - 0xB40) / 4] = val;
            break;
        default:
        bad_reg:
            DB_PRINT("Bad register write %x <= %08x\n", (int)offset,
                     (unsigned)val);
        }
    } else {
        DB_PRINT("SCLR registers are locked. Unlock them first\n");
    }
}

static const MemoryRegionOps slcr_ops = {
    .read = zynq_slcr_read,
    .write = zynq_slcr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int zynq_slcr_init(SysBusDevice *dev)
{
    ZynqSLCRState *s = FROM_SYSBUS(ZynqSLCRState, dev);

    memory_region_init_io(&s->iomem, &slcr_ops, s, "slcr", 0x1000);
    sysbus_init_mmio(dev, &s->iomem);

    return 0;
}

static const VMStateDescription vmstate_zynq_slcr = {
    .name = "zynq_slcr",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(data, ZynqSLCRState, 0x1000),
        VMSTATE_END_OF_LIST()
    }
};

static void zynq_slcr_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = zynq_slcr_init;
    dc->vmsd = &vmstate_zynq_slcr;
    dc->reset = zynq_slcr_reset;
}

static const TypeInfo zynq_slcr_info = {
    .class_init = zynq_slcr_class_init,
    .name  = "xilinx,zynq_slcr",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(ZynqSLCRState),
};

static void zynq_slcr_register_types(void)
{
    type_register_static(&zynq_slcr_info);
}

type_init(zynq_slcr_register_types)
