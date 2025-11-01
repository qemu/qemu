/*
 * i.MX 8M Plus SoC Implementation
 *
 * Based on hw/arm/fsl-imx6.c
 *
 * Copyright (c) 2024, Bernhard Beschow <shentey@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "system/address-spaces.h"
#include "hw/arm/bsa.h"
#include "hw/arm/fsl-imx8mp.h"
#include "hw/misc/unimp.h"
#include "hw/boards.h"
#include "system/kvm.h"
#include "system/system.h"
#include "target/arm/cpu.h"
#include "target/arm/cpu-qom.h"
#include "target/arm/kvm_arm.h"
#include "qapi/error.h"
#include "qobject/qlist.h"

static const struct {
    hwaddr addr;
    size_t size;
    const char *name;
} fsl_imx8mp_memmap[] = {
    [FSL_IMX8MP_RAM] = { FSL_IMX8MP_RAM_START, FSL_IMX8MP_RAM_SIZE_MAX, "ram" },
    [FSL_IMX8MP_DDR_PHY_BROADCAST] = { 0x3dc00000, 4 * MiB, "ddr_phy_broadcast" },
    [FSL_IMX8MP_DDR_PERF_MON] = { 0x3d800000, 4 * MiB, "ddr_perf_mon" },
    [FSL_IMX8MP_DDR_CTL] = { 0x3d400000, 4 * MiB, "ddr_ctl" },
    [FSL_IMX8MP_DDR_BLK_CTRL] = { 0x3d000000, 1 * MiB, "ddr_blk_ctrl" },
    [FSL_IMX8MP_DDR_PHY] = { 0x3c000000, 16 * MiB, "ddr_phy" },
    [FSL_IMX8MP_AUDIO_DSP] = { 0x3b000000, 16 * MiB, "audio_dsp" },
    [FSL_IMX8MP_GIC_DIST] = { 0x38800000, 512 * KiB, "gic_dist" },
    [FSL_IMX8MP_GIC_REDIST] = { 0x38880000, 512 * KiB, "gic_redist" },
    [FSL_IMX8MP_NPU] = { 0x38500000, 2 * MiB, "npu" },
    [FSL_IMX8MP_VPU] = { 0x38340000, 2 * MiB, "vpu" },
    [FSL_IMX8MP_VPU_BLK_CTRL] = { 0x38330000, 2 * MiB, "vpu_blk_ctrl" },
    [FSL_IMX8MP_VPU_VC8000E_ENCODER] = { 0x38320000, 2 * MiB, "vpu_vc8000e_encoder" },
    [FSL_IMX8MP_VPU_G2_DECODER] = { 0x38310000, 2 * MiB, "vpu_g2_decoder" },
    [FSL_IMX8MP_VPU_G1_DECODER] = { 0x38300000, 2 * MiB, "vpu_g1_decoder" },
    [FSL_IMX8MP_USB2_GLUE] = { 0x382f0000, 0x100, "usb2_glue" },
    [FSL_IMX8MP_USB2_OTG] = { 0x3820cc00, 0x100, "usb2_otg" },
    [FSL_IMX8MP_USB2_DEV] = { 0x3820c700, 0x500, "usb2_dev" },
    [FSL_IMX8MP_USB2] = { 0x38200000, 0xc700, "usb2" },
    [FSL_IMX8MP_USB1_GLUE] = { 0x381f0000, 0x100, "usb1_glue" },
    [FSL_IMX8MP_USB1_OTG] = { 0x3810cc00, 0x100, "usb1_otg" },
    [FSL_IMX8MP_USB1_DEV] = { 0x3810c700, 0x500, "usb1_dev" },
    [FSL_IMX8MP_USB1] = { 0x38100000, 0xc700, "usb1" },
    [FSL_IMX8MP_GPU2D] = { 0x38008000, 32 * KiB, "gpu2d" },
    [FSL_IMX8MP_GPU3D] = { 0x38000000, 32 * KiB, "gpu3d" },
    [FSL_IMX8MP_QSPI1_RX_BUFFER] = { 0x34000000, 32 * MiB, "qspi1_rx_buffer" },
    [FSL_IMX8MP_PCIE1] = { 0x33800000, 4 * MiB, "pcie1" },
    [FSL_IMX8MP_QSPI1_TX_BUFFER] = { 0x33008000, 32 * KiB, "qspi1_tx_buffer" },
    [FSL_IMX8MP_APBH_DMA] = { 0x33000000, 32 * KiB, "apbh_dma" },

    /* AIPS-5 Begin */
    [FSL_IMX8MP_MU_3_B] = { 0x30e90000, 64 * KiB, "mu_3_b" },
    [FSL_IMX8MP_MU_3_A] = { 0x30e80000, 64 * KiB, "mu_3_a" },
    [FSL_IMX8MP_MU_2_B] = { 0x30e70000, 64 * KiB, "mu_2_b" },
    [FSL_IMX8MP_MU_2_A] = { 0x30e60000, 64 * KiB, "mu_2_a" },
    [FSL_IMX8MP_EDMA_CHANNELS] = { 0x30e40000, 128 * KiB, "edma_channels" },
    [FSL_IMX8MP_EDMA_MANAGEMENT_PAGE] = { 0x30e30000, 64 * KiB, "edma_management_page" },
    [FSL_IMX8MP_AUDIO_BLK_CTRL] = { 0x30e20000, 64 * KiB, "audio_blk_ctrl" },
    [FSL_IMX8MP_SDMA2] = { 0x30e10000, 64 * KiB, "sdma2" },
    [FSL_IMX8MP_SDMA3] = { 0x30e00000, 64 * KiB, "sdma3" },
    [FSL_IMX8MP_AIPS5_CONFIGURATION] = { 0x30df0000, 64 * KiB, "aips5_configuration" },
    [FSL_IMX8MP_SPBA2] = { 0x30cf0000, 64 * KiB, "spba2" },
    [FSL_IMX8MP_AUDIO_XCVR_RX] = { 0x30cc0000, 64 * KiB, "audio_xcvr_rx" },
    [FSL_IMX8MP_HDMI_TX_AUDLNK_MSTR] = { 0x30cb0000, 64 * KiB, "hdmi_tx_audlnk_mstr" },
    [FSL_IMX8MP_PDM] = { 0x30ca0000, 64 * KiB, "pdm" },
    [FSL_IMX8MP_ASRC] = { 0x30c90000, 64 * KiB, "asrc" },
    [FSL_IMX8MP_SAI7] = { 0x30c80000, 64 * KiB, "sai7" },
    [FSL_IMX8MP_SAI6] = { 0x30c60000, 64 * KiB, "sai6" },
    [FSL_IMX8MP_SAI5] = { 0x30c50000, 64 * KiB, "sai5" },
    [FSL_IMX8MP_SAI3] = { 0x30c30000, 64 * KiB, "sai3" },
    [FSL_IMX8MP_SAI2] = { 0x30c20000, 64 * KiB, "sai2" },
    [FSL_IMX8MP_SAI1] = { 0x30c10000, 64 * KiB, "sai1" },
    /* AIPS-5 End */

    /* AIPS-4 Begin */
    [FSL_IMX8MP_HDMI_TX] = { 0x32fc0000, 128 * KiB, "hdmi_tx" },
    [FSL_IMX8MP_TZASC] = { 0x32f80000, 64 * KiB, "tzasc" },
    [FSL_IMX8MP_HSIO_BLK_CTL] = { 0x32f10000, 64 * KiB, "hsio_blk_ctl" },
    [FSL_IMX8MP_PCIE_PHY1] = { 0x32f00000, 64 * KiB, "pcie_phy1" },
    [FSL_IMX8MP_MEDIA_BLK_CTL] = { 0x32ec0000, 64 * KiB, "media_blk_ctl" },
    [FSL_IMX8MP_LCDIF2] = { 0x32e90000, 64 * KiB, "lcdif2" },
    [FSL_IMX8MP_LCDIF1] = { 0x32e80000, 64 * KiB, "lcdif1" },
    [FSL_IMX8MP_MIPI_DSI1] = { 0x32e60000, 64 * KiB, "mipi_dsi1" },
    [FSL_IMX8MP_MIPI_CSI2] = { 0x32e50000, 64 * KiB, "mipi_csi2" },
    [FSL_IMX8MP_MIPI_CSI1] = { 0x32e40000, 64 * KiB, "mipi_csi1" },
    [FSL_IMX8MP_IPS_DEWARP] = { 0x32e30000, 64 * KiB, "ips_dewarp" },
    [FSL_IMX8MP_ISP2] = { 0x32e20000, 64 * KiB, "isp2" },
    [FSL_IMX8MP_ISP1] = { 0x32e10000, 64 * KiB, "isp1" },
    [FSL_IMX8MP_ISI] = { 0x32e00000, 64 * KiB, "isi" },
    [FSL_IMX8MP_AIPS4_CONFIGURATION] = { 0x32df0000, 64 * KiB, "aips4_configuration" },
    /* AIPS-4 End */

    [FSL_IMX8MP_INTERCONNECT] = { 0x32700000, 1 * MiB, "interconnect" },

    /* AIPS-3 Begin */
    [FSL_IMX8MP_ENET2_TSN] = { 0x30bf0000, 64 * KiB, "enet2_tsn" },
    [FSL_IMX8MP_ENET1] = { 0x30be0000, 64 * KiB, "enet1" },
    [FSL_IMX8MP_SDMA1] = { 0x30bd0000, 64 * KiB, "sdma1" },
    [FSL_IMX8MP_QSPI] = { 0x30bb0000, 64 * KiB, "qspi" },
    [FSL_IMX8MP_USDHC3] = { 0x30b60000, 64 * KiB, "usdhc3" },
    [FSL_IMX8MP_USDHC2] = { 0x30b50000, 64 * KiB, "usdhc2" },
    [FSL_IMX8MP_USDHC1] = { 0x30b40000, 64 * KiB, "usdhc1" },
    [FSL_IMX8MP_I2C6] = { 0x30ae0000, 64 * KiB, "i2c6" },
    [FSL_IMX8MP_I2C5] = { 0x30ad0000, 64 * KiB, "i2c5" },
    [FSL_IMX8MP_SEMAPHORE_HS] = { 0x30ac0000, 64 * KiB, "semaphore_hs" },
    [FSL_IMX8MP_MU_1_B] = { 0x30ab0000, 64 * KiB, "mu_1_b" },
    [FSL_IMX8MP_MU_1_A] = { 0x30aa0000, 64 * KiB, "mu_1_a" },
    [FSL_IMX8MP_AUD_IRQ_STEER] = { 0x30a80000, 64 * KiB, "aud_irq_steer" },
    [FSL_IMX8MP_UART4] = { 0x30a60000, 64 * KiB, "uart4" },
    [FSL_IMX8MP_I2C4] = { 0x30a50000, 64 * KiB, "i2c4" },
    [FSL_IMX8MP_I2C3] = { 0x30a40000, 64 * KiB, "i2c3" },
    [FSL_IMX8MP_I2C2] = { 0x30a30000, 64 * KiB, "i2c2" },
    [FSL_IMX8MP_I2C1] = { 0x30a20000, 64 * KiB, "i2c1" },
    [FSL_IMX8MP_AIPS3_CONFIGURATION] = { 0x309f0000, 64 * KiB, "aips3_configuration" },
    [FSL_IMX8MP_CAAM] = { 0x30900000, 256 * KiB, "caam" },
    [FSL_IMX8MP_SPBA1] = { 0x308f0000, 64 * KiB, "spba1" },
    [FSL_IMX8MP_FLEXCAN2] = { 0x308d0000, 64 * KiB, "flexcan2" },
    [FSL_IMX8MP_FLEXCAN1] = { 0x308c0000, 64 * KiB, "flexcan1" },
    [FSL_IMX8MP_UART2] = { 0x30890000, 64 * KiB, "uart2" },
    [FSL_IMX8MP_UART3] = { 0x30880000, 64 * KiB, "uart3" },
    [FSL_IMX8MP_UART1] = { 0x30860000, 64 * KiB, "uart1" },
    [FSL_IMX8MP_ECSPI3] = { 0x30840000, 64 * KiB, "ecspi3" },
    [FSL_IMX8MP_ECSPI2] = { 0x30830000, 64 * KiB, "ecspi2" },
    [FSL_IMX8MP_ECSPI1] = { 0x30820000, 64 * KiB, "ecspi1" },
    /* AIPS-3 End */

    /* AIPS-2 Begin */
    [FSL_IMX8MP_QOSC] = { 0x307f0000, 64 * KiB, "qosc" },
    [FSL_IMX8MP_PERFMON2] = { 0x307d0000, 64 * KiB, "perfmon2" },
    [FSL_IMX8MP_PERFMON1] = { 0x307c0000, 64 * KiB, "perfmon1" },
    [FSL_IMX8MP_GPT4] = { 0x30700000, 64 * KiB, "gpt4" },
    [FSL_IMX8MP_GPT5] = { 0x306f0000, 64 * KiB, "gpt5" },
    [FSL_IMX8MP_GPT6] = { 0x306e0000, 64 * KiB, "gpt6" },
    [FSL_IMX8MP_SYSCNT_CTRL] = { 0x306c0000, 64 * KiB, "syscnt_ctrl" },
    [FSL_IMX8MP_SYSCNT_CMP] = { 0x306b0000, 64 * KiB, "syscnt_cmp" },
    [FSL_IMX8MP_SYSCNT_RD] = { 0x306a0000, 64 * KiB, "syscnt_rd" },
    [FSL_IMX8MP_PWM4] = { 0x30690000, 64 * KiB, "pwm4" },
    [FSL_IMX8MP_PWM3] = { 0x30680000, 64 * KiB, "pwm3" },
    [FSL_IMX8MP_PWM2] = { 0x30670000, 64 * KiB, "pwm2" },
    [FSL_IMX8MP_PWM1] = { 0x30660000, 64 * KiB, "pwm1" },
    [FSL_IMX8MP_AIPS2_CONFIGURATION] = { 0x305f0000, 64 * KiB, "aips2_configuration" },
    /* AIPS-2 End */

    /* AIPS-1 Begin */
    [FSL_IMX8MP_CSU] = { 0x303e0000, 64 * KiB, "csu" },
    [FSL_IMX8MP_RDC] = { 0x303d0000, 64 * KiB, "rdc" },
    [FSL_IMX8MP_SEMAPHORE2] = { 0x303c0000, 64 * KiB, "semaphore2" },
    [FSL_IMX8MP_SEMAPHORE1] = { 0x303b0000, 64 * KiB, "semaphore1" },
    [FSL_IMX8MP_GPC] = { 0x303a0000, 64 * KiB, "gpc" },
    [FSL_IMX8MP_SRC] = { 0x30390000, 64 * KiB, "src" },
    [FSL_IMX8MP_CCM] = { 0x30380000, 64 * KiB, "ccm" },
    [FSL_IMX8MP_SNVS_HP] = { 0x30370000, 64 * KiB, "snvs_hp" },
    [FSL_IMX8MP_ANA_PLL] = { 0x30360000, 64 * KiB, "ana_pll" },
    [FSL_IMX8MP_OCOTP_CTRL] = { 0x30350000, 64 * KiB, "ocotp_ctrl" },
    [FSL_IMX8MP_IOMUXC_GPR] = { 0x30340000, 64 * KiB, "iomuxc_gpr" },
    [FSL_IMX8MP_IOMUXC] = { 0x30330000, 64 * KiB, "iomuxc" },
    [FSL_IMX8MP_GPT3] = { 0x302f0000, 64 * KiB, "gpt3" },
    [FSL_IMX8MP_GPT2] = { 0x302e0000, 64 * KiB, "gpt2" },
    [FSL_IMX8MP_GPT1] = { 0x302d0000, 64 * KiB, "gpt1" },
    [FSL_IMX8MP_WDOG3] = { 0x302a0000, 64 * KiB, "wdog3" },
    [FSL_IMX8MP_WDOG2] = { 0x30290000, 64 * KiB, "wdog2" },
    [FSL_IMX8MP_WDOG1] = { 0x30280000, 64 * KiB, "wdog1" },
    [FSL_IMX8MP_ANA_OSC] = { 0x30270000, 64 * KiB, "ana_osc" },
    [FSL_IMX8MP_ANA_TSENSOR] = { 0x30260000, 64 * KiB, "ana_tsensor" },
    [FSL_IMX8MP_GPIO5] = { 0x30240000, 64 * KiB, "gpio5" },
    [FSL_IMX8MP_GPIO4] = { 0x30230000, 64 * KiB, "gpio4" },
    [FSL_IMX8MP_GPIO3] = { 0x30220000, 64 * KiB, "gpio3" },
    [FSL_IMX8MP_GPIO2] = { 0x30210000, 64 * KiB, "gpio2" },
    [FSL_IMX8MP_GPIO1] = { 0x30200000, 64 * KiB, "gpio1" },
    [FSL_IMX8MP_AIPS1_CONFIGURATION] = { 0x301f0000, 64 * KiB, "aips1_configuration" },
    /* AIPS-1 End */

    [FSL_IMX8MP_A53_DAP] = { 0x28000000, 16 * MiB, "a53_dap" },
    [FSL_IMX8MP_PCIE1_MEM] = { 0x18000000, 128 * MiB, "pcie1_mem" },
    [FSL_IMX8MP_QSPI_MEM] = { 0x08000000, 256 * MiB, "qspi_mem" },
    [FSL_IMX8MP_OCRAM] = { 0x00900000, 576 * KiB, "ocram" },
    [FSL_IMX8MP_TCM_DTCM] = { 0x00800000, 128 * KiB, "tcm_dtcm" },
    [FSL_IMX8MP_TCM_ITCM] = { 0x007e0000, 128 * KiB, "tcm_itcm" },
    [FSL_IMX8MP_OCRAM_S] = { 0x00180000, 36 * KiB, "ocram_s" },
    [FSL_IMX8MP_CAAM_MEM] = { 0x00100000, 32 * KiB, "caam_mem" },
    [FSL_IMX8MP_BOOT_ROM_PROTECTED] = { 0x0003f000, 4 * KiB, "boot_rom_protected" },
    [FSL_IMX8MP_BOOT_ROM] = { 0x00000000, 252 * KiB, "boot_rom" },
};

static void fsl_imx8mp_init(Object *obj)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    FslImx8mpState *s = FSL_IMX8MP(obj);
    const char *cpu_type = ms->cpu_type ?: ARM_CPU_TYPE_NAME("cortex-a53");
    int i;

    for (i = 0; i < MIN(ms->smp.cpus, FSL_IMX8MP_NUM_CPUS); i++) {
        g_autofree char *name = g_strdup_printf("cpu%d", i);
        object_initialize_child(obj, name, &s->cpu[i], cpu_type);
    }

    object_initialize_child(obj, "gic", &s->gic, gicv3_class_name());

    object_initialize_child(obj, "ccm", &s->ccm, TYPE_IMX8MP_CCM);

    object_initialize_child(obj, "analog", &s->analog, TYPE_IMX8MP_ANALOG);

    object_initialize_child(obj, "snvs", &s->snvs, TYPE_IMX7_SNVS);

    for (i = 0; i < FSL_IMX8MP_NUM_UARTS; i++) {
        g_autofree char *name = g_strdup_printf("uart%d", i + 1);
        object_initialize_child(obj, name, &s->uart[i], TYPE_IMX_SERIAL);
    }

    for (i = 0; i < FSL_IMX8MP_NUM_GPTS; i++) {
        g_autofree char *name = g_strdup_printf("gpt%d", i + 1);
        object_initialize_child(obj, name, &s->gpt[i], TYPE_IMX8MP_GPT);
    }
    object_initialize_child(obj, "gpt5-gpt6-irq", &s->gpt5_gpt6_irq,
                            TYPE_OR_IRQ);

    for (i = 0; i < FSL_IMX8MP_NUM_I2CS; i++) {
        g_autofree char *name = g_strdup_printf("i2c%d", i + 1);
        object_initialize_child(obj, name, &s->i2c[i], TYPE_IMX_I2C);
    }

    for (i = 0; i < FSL_IMX8MP_NUM_GPIOS; i++) {
        g_autofree char *name = g_strdup_printf("gpio%d", i + 1);
        object_initialize_child(obj, name, &s->gpio[i], TYPE_IMX_GPIO);
    }

    for (i = 0; i < FSL_IMX8MP_NUM_USDHCS; i++) {
        g_autofree char *name = g_strdup_printf("usdhc%d", i + 1);
        object_initialize_child(obj, name, &s->usdhc[i], TYPE_IMX_USDHC);
    }

    for (i = 0; i < FSL_IMX8MP_NUM_USBS; i++) {
        g_autofree char *name = g_strdup_printf("usb%d", i);
        object_initialize_child(obj, name, &s->usb[i], TYPE_USB_DWC3);
    }

    for (i = 0; i < FSL_IMX8MP_NUM_ECSPIS; i++) {
        g_autofree char *name = g_strdup_printf("spi%d", i + 1);
        object_initialize_child(obj, name, &s->spi[i], TYPE_IMX_SPI);
    }

    for (i = 0; i < FSL_IMX8MP_NUM_WDTS; i++) {
        g_autofree char *name = g_strdup_printf("wdt%d", i);
        object_initialize_child(obj, name, &s->wdt[i], TYPE_IMX2_WDT);
    }

    object_initialize_child(obj, "eth0", &s->enet, TYPE_IMX_ENET);

    object_initialize_child(obj, "pcie", &s->pcie, TYPE_DESIGNWARE_PCIE_HOST);
    object_initialize_child(obj, "pcie_phy", &s->pcie_phy,
                            TYPE_FSL_IMX8M_PCIE_PHY);
}

static void fsl_imx8mp_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    FslImx8mpState *s = FSL_IMX8MP(dev);
    DeviceState *gicdev = DEVICE(&s->gic);
    int i;

    if (ms->smp.cpus > FSL_IMX8MP_NUM_CPUS) {
        error_setg(errp, "%s: Only %d CPUs are supported (%d requested)",
                   TYPE_FSL_IMX8MP, FSL_IMX8MP_NUM_CPUS, ms->smp.cpus);
        return;
    }

    /* CPUs */
    for (i = 0; i < ms->smp.cpus; i++) {
        /* On uniprocessor, the CBAR is set to 0 */
        if (ms->smp.cpus > 1 &&
                object_property_find(OBJECT(&s->cpu[i]), "reset-cbar")) {
            object_property_set_int(OBJECT(&s->cpu[i]), "reset-cbar",
                                    fsl_imx8mp_memmap[FSL_IMX8MP_GIC_DIST].addr,
                                    &error_abort);
        }

        /*
         * CNTFID0 base frequency in Hz of system counter
         */
        object_property_set_int(OBJECT(&s->cpu[i]), "cntfrq", 8000000,
                                &error_abort);

        if (object_property_find(OBJECT(&s->cpu[i]), "has_el2")) {
            object_property_set_bool(OBJECT(&s->cpu[i]), "has_el2",
                                     !kvm_enabled(), &error_abort);
        }

        if (object_property_find(OBJECT(&s->cpu[i]), "has_el3")) {
            object_property_set_bool(OBJECT(&s->cpu[i]), "has_el3",
                                     !kvm_enabled(), &error_abort);
        }

        if (i) {
            /*
             * Secondary CPUs start in powered-down state (and can be
             * powered up via the SRC system reset controller)
             */
            object_property_set_bool(OBJECT(&s->cpu[i]), "start-powered-off",
                                     true, &error_abort);
        }

        if (!qdev_realize(DEVICE(&s->cpu[i]), NULL, errp)) {
            return;
        }
    }

    /* GIC */
    {
        SysBusDevice *gicsbd = SYS_BUS_DEVICE(&s->gic);
        QList *redist_region_count;
        bool pmu = object_property_get_bool(OBJECT(first_cpu), "pmu", NULL);

        qdev_prop_set_uint32(gicdev, "num-cpu", ms->smp.cpus);
        qdev_prop_set_uint32(gicdev, "num-irq",
                             FSL_IMX8MP_NUM_IRQS + GIC_INTERNAL);
        redist_region_count = qlist_new();
        qlist_append_int(redist_region_count, ms->smp.cpus);
        qdev_prop_set_array(gicdev, "redist-region-count", redist_region_count);
        object_property_set_link(OBJECT(&s->gic), "sysmem",
                                 OBJECT(get_system_memory()), &error_fatal);
        if (!sysbus_realize(gicsbd, errp)) {
            return;
        }
        sysbus_mmio_map(gicsbd, 0, fsl_imx8mp_memmap[FSL_IMX8MP_GIC_DIST].addr);
        sysbus_mmio_map(gicsbd, 1, fsl_imx8mp_memmap[FSL_IMX8MP_GIC_REDIST].addr);

        /*
         * Wire the outputs from each CPU's generic timer and the GICv3
         * maintenance interrupt signal to the appropriate GIC PPI inputs, and
         * the GIC's IRQ/FIQ interrupt outputs to the CPU's inputs.
         */
        for (i = 0; i < ms->smp.cpus; i++) {
            DeviceState *cpudev = DEVICE(&s->cpu[i]);
            int intidbase = FSL_IMX8MP_NUM_IRQS + i * GIC_INTERNAL;
            qemu_irq irq;

            /*
             * Mapping from the output timer irq lines from the CPU to the
             * GIC PPI inputs.
             */
            static const int timer_irqs[] = {
                [GTIMER_PHYS] = ARCH_TIMER_NS_EL1_IRQ,
                [GTIMER_VIRT] = ARCH_TIMER_VIRT_IRQ,
                [GTIMER_HYP]  = ARCH_TIMER_NS_EL2_IRQ,
                [GTIMER_SEC]  = ARCH_TIMER_S_EL1_IRQ,
            };

            for (int j = 0; j < ARRAY_SIZE(timer_irqs); j++) {
                irq = qdev_get_gpio_in(gicdev, intidbase + timer_irqs[j]);
                qdev_connect_gpio_out(cpudev, j, irq);
            }

            irq = qdev_get_gpio_in(gicdev, intidbase + ARCH_GIC_MAINT_IRQ);
            qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt",
                                        0, irq);

            irq = qdev_get_gpio_in(gicdev, intidbase + VIRTUAL_PMU_IRQ);
            qdev_connect_gpio_out_named(cpudev, "pmu-interrupt", 0, irq);

            sysbus_connect_irq(gicsbd, i,
                               qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
            sysbus_connect_irq(gicsbd, i + ms->smp.cpus,
                               qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
            sysbus_connect_irq(gicsbd, i + 2 * ms->smp.cpus,
                               qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
            sysbus_connect_irq(gicsbd, i + 3 * ms->smp.cpus,
                               qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));

            if (kvm_enabled()) {
                if (pmu) {
                    assert(arm_feature(&s->cpu[i].env, ARM_FEATURE_PMU));
                    if (kvm_irqchip_in_kernel()) {
                        kvm_arm_pmu_set_irq(&s->cpu[i], VIRTUAL_PMU_IRQ);
                    }
                    kvm_arm_pmu_init(&s->cpu[i]);
                }
            }
        }
    }

    /* CCM */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ccm), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ccm), 0,
                    fsl_imx8mp_memmap[FSL_IMX8MP_CCM].addr);

    /* Analog */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->analog), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->analog), 0,
                    fsl_imx8mp_memmap[FSL_IMX8MP_ANA_PLL].addr);

    /* UARTs */
    for (i = 0; i < FSL_IMX8MP_NUM_UARTS; i++) {
        struct {
            hwaddr addr;
            unsigned int irq;
        } serial_table[FSL_IMX8MP_NUM_UARTS] = {
            { fsl_imx8mp_memmap[FSL_IMX8MP_UART1].addr, FSL_IMX8MP_UART1_IRQ },
            { fsl_imx8mp_memmap[FSL_IMX8MP_UART2].addr, FSL_IMX8MP_UART2_IRQ },
            { fsl_imx8mp_memmap[FSL_IMX8MP_UART3].addr, FSL_IMX8MP_UART3_IRQ },
            { fsl_imx8mp_memmap[FSL_IMX8MP_UART4].addr, FSL_IMX8MP_UART4_IRQ },
        };

        qdev_prop_set_chr(DEVICE(&s->uart[i]), "chardev", serial_hd(i));
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart[i]), 0, serial_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart[i]), 0,
                           qdev_get_gpio_in(gicdev, serial_table[i].irq));
    }

    /* GPTs */
    object_property_set_int(OBJECT(&s->gpt5_gpt6_irq), "num-lines", 2,
                            &error_abort);
    if (!qdev_realize(DEVICE(&s->gpt5_gpt6_irq), NULL, errp)) {
        return;
    }

    qdev_connect_gpio_out(DEVICE(&s->gpt5_gpt6_irq), 0,
                          qdev_get_gpio_in(gicdev, FSL_IMX8MP_GPT5_GPT6_IRQ));

    for (i = 0; i < FSL_IMX8MP_NUM_GPTS; i++) {
        hwaddr gpt_addrs[FSL_IMX8MP_NUM_GPTS] = {
            fsl_imx8mp_memmap[FSL_IMX8MP_GPT1].addr,
            fsl_imx8mp_memmap[FSL_IMX8MP_GPT2].addr,
            fsl_imx8mp_memmap[FSL_IMX8MP_GPT3].addr,
            fsl_imx8mp_memmap[FSL_IMX8MP_GPT4].addr,
            fsl_imx8mp_memmap[FSL_IMX8MP_GPT5].addr,
            fsl_imx8mp_memmap[FSL_IMX8MP_GPT6].addr,
        };

        s->gpt[i].ccm = IMX_CCM(&s->ccm);

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpt[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpt[i]), 0, gpt_addrs[i]);

        if (i < FSL_IMX8MP_NUM_GPTS - 2) {
            static const unsigned int gpt_irqs[FSL_IMX8MP_NUM_GPTS - 2] = {
                FSL_IMX8MP_GPT1_IRQ,
                FSL_IMX8MP_GPT2_IRQ,
                FSL_IMX8MP_GPT3_IRQ,
                FSL_IMX8MP_GPT4_IRQ,
            };

            sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpt[i]), 0,
                               qdev_get_gpio_in(gicdev, gpt_irqs[i]));
        } else {
            int irq = i - FSL_IMX8MP_NUM_GPTS + 2;

            sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpt[i]), 0,
                               qdev_get_gpio_in(DEVICE(&s->gpt5_gpt6_irq), irq));
        }
    }

    /* I2Cs */
    for (i = 0; i < FSL_IMX8MP_NUM_I2CS; i++) {
        struct {
            hwaddr addr;
            unsigned int irq;
        } i2c_table[FSL_IMX8MP_NUM_I2CS] = {
            { fsl_imx8mp_memmap[FSL_IMX8MP_I2C1].addr, FSL_IMX8MP_I2C1_IRQ },
            { fsl_imx8mp_memmap[FSL_IMX8MP_I2C2].addr, FSL_IMX8MP_I2C2_IRQ },
            { fsl_imx8mp_memmap[FSL_IMX8MP_I2C3].addr, FSL_IMX8MP_I2C3_IRQ },
            { fsl_imx8mp_memmap[FSL_IMX8MP_I2C4].addr, FSL_IMX8MP_I2C4_IRQ },
            { fsl_imx8mp_memmap[FSL_IMX8MP_I2C5].addr, FSL_IMX8MP_I2C5_IRQ },
            { fsl_imx8mp_memmap[FSL_IMX8MP_I2C6].addr, FSL_IMX8MP_I2C6_IRQ },
        };

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->i2c[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->i2c[i]), 0, i2c_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->i2c[i]), 0,
                           qdev_get_gpio_in(gicdev, i2c_table[i].irq));
    }

    /* GPIOs */
    for (i = 0; i < FSL_IMX8MP_NUM_GPIOS; i++) {
        struct {
            hwaddr addr;
            unsigned int irq_low;
            unsigned int irq_high;
        } gpio_table[FSL_IMX8MP_NUM_GPIOS] = {
            {
                fsl_imx8mp_memmap[FSL_IMX8MP_GPIO1].addr,
                FSL_IMX8MP_GPIO1_LOW_IRQ,
                FSL_IMX8MP_GPIO1_HIGH_IRQ
            },
            {
                fsl_imx8mp_memmap[FSL_IMX8MP_GPIO2].addr,
                FSL_IMX8MP_GPIO2_LOW_IRQ,
                FSL_IMX8MP_GPIO2_HIGH_IRQ
            },
            {
                fsl_imx8mp_memmap[FSL_IMX8MP_GPIO3].addr,
                FSL_IMX8MP_GPIO3_LOW_IRQ,
                FSL_IMX8MP_GPIO3_HIGH_IRQ
            },
            {
                fsl_imx8mp_memmap[FSL_IMX8MP_GPIO4].addr,
                FSL_IMX8MP_GPIO4_LOW_IRQ,
                FSL_IMX8MP_GPIO4_HIGH_IRQ
            },
            {
                fsl_imx8mp_memmap[FSL_IMX8MP_GPIO5].addr,
                FSL_IMX8MP_GPIO5_LOW_IRQ,
                FSL_IMX8MP_GPIO5_HIGH_IRQ
            },
        };

        object_property_set_bool(OBJECT(&s->gpio[i]), "has-edge-sel", true,
                                 &error_abort);
        object_property_set_bool(OBJECT(&s->gpio[i]), "has-upper-pin-irq",
                                 true, &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio[i]), 0, gpio_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio[i]), 0,
                           qdev_get_gpio_in(gicdev, gpio_table[i].irq_low));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio[i]), 1,
                           qdev_get_gpio_in(gicdev, gpio_table[i].irq_high));
    }

    /* USDHCs */
    for (i = 0; i < FSL_IMX8MP_NUM_USDHCS; i++) {
        struct {
            hwaddr addr;
            unsigned int irq;
        } usdhc_table[FSL_IMX8MP_NUM_USDHCS] = {
            { fsl_imx8mp_memmap[FSL_IMX8MP_USDHC1].addr, FSL_IMX8MP_USDHC1_IRQ },
            { fsl_imx8mp_memmap[FSL_IMX8MP_USDHC2].addr, FSL_IMX8MP_USDHC2_IRQ },
            { fsl_imx8mp_memmap[FSL_IMX8MP_USDHC3].addr, FSL_IMX8MP_USDHC3_IRQ },
        };

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->usdhc[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->usdhc[i]), 0, usdhc_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->usdhc[i]), 0,
                           qdev_get_gpio_in(gicdev, usdhc_table[i].irq));
    }

    /* USBs */
    for (i = 0; i < FSL_IMX8MP_NUM_USBS; i++) {
        struct {
            hwaddr addr;
            unsigned int irq;
        } usb_table[FSL_IMX8MP_NUM_USBS] = {
            { fsl_imx8mp_memmap[FSL_IMX8MP_USB1].addr, FSL_IMX8MP_USB1_IRQ },
            { fsl_imx8mp_memmap[FSL_IMX8MP_USB2].addr, FSL_IMX8MP_USB2_IRQ },
        };

        qdev_prop_set_uint32(DEVICE(&s->usb[i].sysbus_xhci), "p2", 1);
        qdev_prop_set_uint32(DEVICE(&s->usb[i].sysbus_xhci), "p3", 1);
        qdev_prop_set_uint32(DEVICE(&s->usb[i].sysbus_xhci), "slots", 2);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->usb[i]), errp)) {
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->usb[i]), 0, usb_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->usb[i].sysbus_xhci), 0,
                           qdev_get_gpio_in(gicdev, usb_table[i].irq));
    }

    /* ECSPIs */
    for (i = 0; i < FSL_IMX8MP_NUM_ECSPIS; i++) {
        struct {
            hwaddr addr;
            unsigned int irq;
        } spi_table[FSL_IMX8MP_NUM_ECSPIS] = {
            { fsl_imx8mp_memmap[FSL_IMX8MP_ECSPI1].addr, FSL_IMX8MP_ECSPI1_IRQ },
            { fsl_imx8mp_memmap[FSL_IMX8MP_ECSPI2].addr, FSL_IMX8MP_ECSPI2_IRQ },
            { fsl_imx8mp_memmap[FSL_IMX8MP_ECSPI3].addr, FSL_IMX8MP_ECSPI3_IRQ },
        };

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->spi[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi[i]), 0, spi_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->spi[i]), 0,
                           qdev_get_gpio_in(gicdev, spi_table[i].irq));
    }

    /* ENET1 */
    object_property_set_uint(OBJECT(&s->enet), "phy-num", s->phy_num,
                             &error_abort);
    object_property_set_uint(OBJECT(&s->enet), "tx-ring-num", 3, &error_abort);
    qemu_configure_nic_device(DEVICE(&s->enet), true, NULL);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->enet), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->enet), 0,
                    fsl_imx8mp_memmap[FSL_IMX8MP_ENET1].addr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->enet), 0,
                       qdev_get_gpio_in(gicdev, FSL_IMX8MP_ENET1_MAC_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->enet), 1,
                       qdev_get_gpio_in(gicdev, FSL_IMX6_ENET1_MAC_1588_IRQ));

    /* SNVS */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->snvs), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->snvs), 0,
                    fsl_imx8mp_memmap[FSL_IMX8MP_SNVS_HP].addr);

    /* Watchdogs */
    for (i = 0; i < FSL_IMX8MP_NUM_WDTS; i++) {
        struct {
            hwaddr addr;
            unsigned int irq;
        } wdog_table[FSL_IMX8MP_NUM_WDTS] = {
            { fsl_imx8mp_memmap[FSL_IMX8MP_WDOG1].addr, FSL_IMX8MP_WDOG1_IRQ },
            { fsl_imx8mp_memmap[FSL_IMX8MP_WDOG2].addr, FSL_IMX8MP_WDOG2_IRQ },
            { fsl_imx8mp_memmap[FSL_IMX8MP_WDOG3].addr, FSL_IMX8MP_WDOG3_IRQ },
        };

        object_property_set_bool(OBJECT(&s->wdt[i]), "pretimeout-support",
                                 true, &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->wdt[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->wdt[i]), 0, wdog_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->wdt[i]), 0,
                           qdev_get_gpio_in(gicdev, wdog_table[i].irq));
    }

    /* PCIe */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pcie), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pcie), 0,
                    fsl_imx8mp_memmap[FSL_IMX8MP_PCIE1].addr);

    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie), 0,
                       qdev_get_gpio_in(gicdev, FSL_IMX8MP_PCI_INTA_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie), 1,
                       qdev_get_gpio_in(gicdev, FSL_IMX8MP_PCI_INTB_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie), 2,
                       qdev_get_gpio_in(gicdev, FSL_IMX8MP_PCI_INTC_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie), 3,
                       qdev_get_gpio_in(gicdev, FSL_IMX8MP_PCI_INTD_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie), 4,
                       qdev_get_gpio_in(gicdev, FSL_IMX8MP_PCI_MSI_IRQ));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pcie_phy), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pcie_phy), 0,
                    fsl_imx8mp_memmap[FSL_IMX8MP_PCIE_PHY1].addr);

    /* On-Chip RAM */
    if (!memory_region_init_ram(&s->ocram, NULL, "imx8mp.ocram",
                                fsl_imx8mp_memmap[FSL_IMX8MP_OCRAM].size,
                                errp)) {
        return;
    }
    memory_region_add_subregion(get_system_memory(),
                                fsl_imx8mp_memmap[FSL_IMX8MP_OCRAM].addr,
                                &s->ocram);

    /* Unimplemented devices */
    for (i = 0; i < ARRAY_SIZE(fsl_imx8mp_memmap); i++) {
        switch (i) {
        case FSL_IMX8MP_ANA_PLL:
        case FSL_IMX8MP_CCM:
        case FSL_IMX8MP_GIC_DIST:
        case FSL_IMX8MP_GIC_REDIST:
        case FSL_IMX8MP_GPIO1 ... FSL_IMX8MP_GPIO5:
        case FSL_IMX8MP_ECSPI1 ... FSL_IMX8MP_ECSPI3:
        case FSL_IMX8MP_ENET1:
        case FSL_IMX8MP_I2C1 ... FSL_IMX8MP_I2C6:
        case FSL_IMX8MP_OCRAM:
        case FSL_IMX8MP_PCIE1:
        case FSL_IMX8MP_PCIE_PHY1:
        case FSL_IMX8MP_RAM:
        case FSL_IMX8MP_SNVS_HP:
        case FSL_IMX8MP_UART1 ... FSL_IMX8MP_UART4:
        case FSL_IMX8MP_USB1 ... FSL_IMX8MP_USB2:
        case FSL_IMX8MP_USDHC1 ... FSL_IMX8MP_USDHC3:
        case FSL_IMX8MP_WDOG1 ... FSL_IMX8MP_WDOG3:
            /* device implemented and treated above */
            break;

        default:
            create_unimplemented_device(fsl_imx8mp_memmap[i].name,
                                        fsl_imx8mp_memmap[i].addr,
                                        fsl_imx8mp_memmap[i].size);
            break;
        }
    }
}

static const Property fsl_imx8mp_properties[] = {
    DEFINE_PROP_UINT32("fec1-phy-num", FslImx8mpState, phy_num, 0),
    DEFINE_PROP_BOOL("fec1-phy-connected", FslImx8mpState, phy_connected, true),
};

static void fsl_imx8mp_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_props(dc, fsl_imx8mp_properties);
    dc->realize = fsl_imx8mp_realize;

    dc->desc = "i.MX 8M Plus SoC";
}

static const TypeInfo fsl_imx8mp_types[] = {
    {
        .name = TYPE_FSL_IMX8MP,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(FslImx8mpState),
        .instance_init = fsl_imx8mp_init,
        .class_init = fsl_imx8mp_class_init,
    },
};

DEFINE_TYPES(fsl_imx8mp_types)
