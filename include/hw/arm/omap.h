/*
 * Texas Instruments OMAP processors.
 *
 * Copyright (C) 2006-2008 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_ARM_OMAP_H
#define HW_ARM_OMAP_H

#include "system/memory.h"
#include "target/arm/cpu-qom.h"
#include "qemu/log.h"
#include "qom/object.h"

# define OMAP_EMIFS_BASE	0x00000000
# define OMAP_CS0_BASE		0x00000000
# define OMAP_CS1_BASE		0x04000000
# define OMAP_CS2_BASE		0x08000000
# define OMAP_CS3_BASE		0x0c000000
# define OMAP_EMIFF_BASE	0x10000000
# define OMAP_IMIF_BASE		0x20000000
# define OMAP_LOCALBUS_BASE	0x30000000
# define OMAP_MPUI_BASE		0xe1000000

# define OMAP730_SRAM_SIZE	0x00032000
# define OMAP15XX_SRAM_SIZE	0x00030000
# define OMAP16XX_SRAM_SIZE	0x00004000
# define OMAP1611_SRAM_SIZE	0x0003e800
# define OMAP_CS0_SIZE		0x04000000
# define OMAP_CS1_SIZE		0x04000000
# define OMAP_CS2_SIZE		0x04000000
# define OMAP_CS3_SIZE		0x04000000

/* omap_clk.c */
struct omap_mpu_state_s;
typedef struct clk *omap_clk;
omap_clk omap_findclk(struct omap_mpu_state_s *mpu, const char *name);
void omap_clk_init(struct omap_mpu_state_s *mpu);
void omap_clk_adduser(struct clk *clk, qemu_irq user);
void omap_clk_get(omap_clk clk);
void omap_clk_put(omap_clk clk);
void omap_clk_onoff(omap_clk clk, int on);
void omap_clk_canidle(omap_clk clk, int can);
void omap_clk_setrate(omap_clk clk, int divide, int multiply);
int64_t omap_clk_getrate(omap_clk clk);
void omap_clk_reparent(omap_clk clk, omap_clk parent);

/* omap_intc.c */
#define TYPE_OMAP_INTC "omap-intc"
typedef struct OMAPIntcState OMAPIntcState;
DECLARE_INSTANCE_CHECKER(OMAPIntcState, OMAP_INTC, TYPE_OMAP_INTC)


/*
 * TODO: Ideally we should have a clock framework that
 * let us wire these clocks up with QOM properties or links.
 *
 * qdev should support a generic means of defining a 'port' with
 * an arbitrary interface for connecting two devices. Then we
 * could reframe the omap clock API in terms of clock ports,
 * and get some type safety. For now the best qdev provides is
 * passing an arbitrary pointer.
 * (It's not possible to pass in the string which is the clock
 * name, because this device does not have the necessary information
 * (ie the struct omap_mpu_state_s*) to do the clockname to pointer
 * translation.)
 */
void omap_intc_set_iclk(OMAPIntcState *intc, omap_clk clk);
void omap_intc_set_fclk(OMAPIntcState *intc, omap_clk clk);

/* omap_i2c.c */
#define TYPE_OMAP_I2C "omap_i2c"
OBJECT_DECLARE_SIMPLE_TYPE(OMAPI2CState, OMAP_I2C)


/* TODO: clock framework (see above) */
void omap_i2c_set_iclk(OMAPI2CState *i2c, omap_clk clk);
void omap_i2c_set_fclk(OMAPI2CState *i2c, omap_clk clk);

/* omap_gpio.c */
#define TYPE_OMAP1_GPIO "omap-gpio"
typedef struct Omap1GpioState Omap1GpioState;
DECLARE_INSTANCE_CHECKER(Omap1GpioState, OMAP1_GPIO,
                         TYPE_OMAP1_GPIO)

/* TODO: clock framework (see above) */
void omap_gpio_set_clk(Omap1GpioState *gpio, omap_clk clk);

/*
 * Common IRQ numbers for level 1 interrupt handler
 * See /usr/include/asm-arm/arch-omap/irqs.h in Linux.
 */
# define OMAP_INT_CAMERA		1
# define OMAP_INT_FIQ			3
# define OMAP_INT_RTDX			6
# define OMAP_INT_DSP_MMU_ABORT		7
# define OMAP_INT_HOST			8
# define OMAP_INT_ABORT			9
# define OMAP_INT_BRIDGE_PRIV		13
# define OMAP_INT_GPIO_BANK1		14
# define OMAP_INT_UART3			15
# define OMAP_INT_TIMER3		16
# define OMAP_INT_DMA_CH0_6		19
# define OMAP_INT_DMA_CH1_7		20
# define OMAP_INT_DMA_CH2_8		21
# define OMAP_INT_DMA_CH3		22
# define OMAP_INT_DMA_CH4		23
# define OMAP_INT_DMA_CH5		24
# define OMAP_INT_DMA_LCD		25
# define OMAP_INT_TIMER1		26
# define OMAP_INT_WD_TIMER		27
# define OMAP_INT_BRIDGE_PUB		28
# define OMAP_INT_TIMER2		30
# define OMAP_INT_LCD_CTRL		31

/*
 * Common OMAP-15xx IRQ numbers for level 1 interrupt handler
 */
# define OMAP_INT_15XX_IH2_IRQ		0
# define OMAP_INT_15XX_LB_MMU		17
# define OMAP_INT_15XX_LOCAL_BUS	29

/*
 * OMAP-1510 specific IRQ numbers for level 1 interrupt handler
 */
# define OMAP_INT_1510_SPI_TX		4
# define OMAP_INT_1510_SPI_RX		5
# define OMAP_INT_1510_DSP_MAILBOX1	10
# define OMAP_INT_1510_DSP_MAILBOX2	11

/*
 * OMAP-310 specific IRQ numbers for level 1 interrupt handler
 */
# define OMAP_INT_310_McBSP2_TX		4
# define OMAP_INT_310_McBSP2_RX		5
# define OMAP_INT_310_HSB_MAILBOX1	12
# define OMAP_INT_310_HSAB_MMU		18

/*
 * OMAP-1610 specific IRQ numbers for level 1 interrupt handler
 */
# define OMAP_INT_1610_IH2_IRQ		0
# define OMAP_INT_1610_IH2_FIQ		2
# define OMAP_INT_1610_McBSP2_TX	4
# define OMAP_INT_1610_McBSP2_RX	5
# define OMAP_INT_1610_DSP_MAILBOX1	10
# define OMAP_INT_1610_DSP_MAILBOX2	11
# define OMAP_INT_1610_LCD_LINE		12
# define OMAP_INT_1610_GPTIMER1		17
# define OMAP_INT_1610_GPTIMER2		18
# define OMAP_INT_1610_SSR_FIFO_0	29

/*
 * OMAP-730 specific IRQ numbers for level 1 interrupt handler
 */
# define OMAP_INT_730_IH2_FIQ		0
# define OMAP_INT_730_IH2_IRQ		1
# define OMAP_INT_730_USB_NON_ISO	2
# define OMAP_INT_730_USB_ISO		3
# define OMAP_INT_730_ICR		4
# define OMAP_INT_730_EAC		5
# define OMAP_INT_730_GPIO_BANK1	6
# define OMAP_INT_730_GPIO_BANK2	7
# define OMAP_INT_730_GPIO_BANK3	8
# define OMAP_INT_730_McBSP2TX		10
# define OMAP_INT_730_McBSP2RX		11
# define OMAP_INT_730_McBSP2RX_OVF	12
# define OMAP_INT_730_LCD_LINE		14
# define OMAP_INT_730_GSM_PROTECT	15
# define OMAP_INT_730_TIMER3		16
# define OMAP_INT_730_GPIO_BANK5	17
# define OMAP_INT_730_GPIO_BANK6	18
# define OMAP_INT_730_SPGIO_WR		29

/*
 * Common IRQ numbers for level 2 interrupt handler
 */
# define OMAP_INT_KEYBOARD		1
# define OMAP_INT_uWireTX		2
# define OMAP_INT_uWireRX		3
# define OMAP_INT_I2C			4
# define OMAP_INT_MPUIO			5
# define OMAP_INT_USB_HHC_1		6
# define OMAP_INT_McBSP3TX		10
# define OMAP_INT_McBSP3RX		11
# define OMAP_INT_McBSP1TX		12
# define OMAP_INT_McBSP1RX		13
# define OMAP_INT_UART1			14
# define OMAP_INT_UART2			15
# define OMAP_INT_USB_W2FC		20
# define OMAP_INT_1WIRE			21
# define OMAP_INT_OS_TIMER		22
# define OMAP_INT_OQN			23
# define OMAP_INT_GAUGE_32K		24
# define OMAP_INT_RTC_TIMER		25
# define OMAP_INT_RTC_ALARM		26
# define OMAP_INT_DSP_MMU		28

/*
 * OMAP-1510 specific IRQ numbers for level 2 interrupt handler
 */
# define OMAP_INT_1510_BT_MCSI1TX	16
# define OMAP_INT_1510_BT_MCSI1RX	17
# define OMAP_INT_1510_SoSSI_MATCH	19
# define OMAP_INT_1510_MEM_STICK	27
# define OMAP_INT_1510_COM_SPI_RO	31

/*
 * OMAP-310 specific IRQ numbers for level 2 interrupt handler
 */
# define OMAP_INT_310_FAC		0
# define OMAP_INT_310_USB_HHC_2		7
# define OMAP_INT_310_MCSI1_FE		16
# define OMAP_INT_310_MCSI2_FE		17
# define OMAP_INT_310_USB_W2FC_ISO	29
# define OMAP_INT_310_USB_W2FC_NON_ISO	30
# define OMAP_INT_310_McBSP2RX_OF	31

/*
 * OMAP-1610 specific IRQ numbers for level 2 interrupt handler
 */
# define OMAP_INT_1610_FAC		0
# define OMAP_INT_1610_USB_HHC_2	7
# define OMAP_INT_1610_USB_OTG		8
# define OMAP_INT_1610_SoSSI		9
# define OMAP_INT_1610_BT_MCSI1TX	16
# define OMAP_INT_1610_BT_MCSI1RX	17
# define OMAP_INT_1610_SoSSI_MATCH	19
# define OMAP_INT_1610_MEM_STICK	27
# define OMAP_INT_1610_McBSP2RX_OF	31
# define OMAP_INT_1610_STI		32
# define OMAP_INT_1610_STI_WAKEUP	33
# define OMAP_INT_1610_GPTIMER3		34
# define OMAP_INT_1610_GPTIMER4		35
# define OMAP_INT_1610_GPTIMER5		36
# define OMAP_INT_1610_GPTIMER6		37
# define OMAP_INT_1610_GPTIMER7		38
# define OMAP_INT_1610_GPTIMER8		39
# define OMAP_INT_1610_GPIO_BANK2	40
# define OMAP_INT_1610_GPIO_BANK3	41
# define OMAP_INT_1610_MMC2		42
# define OMAP_INT_1610_CF		43
# define OMAP_INT_1610_WAKE_UP_REQ	46
# define OMAP_INT_1610_GPIO_BANK4	48
# define OMAP_INT_1610_SPI		49
# define OMAP_INT_1610_DMA_CH6		53
# define OMAP_INT_1610_DMA_CH7		54
# define OMAP_INT_1610_DMA_CH8		55
# define OMAP_INT_1610_DMA_CH9		56
# define OMAP_INT_1610_DMA_CH10		57
# define OMAP_INT_1610_DMA_CH11		58
# define OMAP_INT_1610_DMA_CH12		59
# define OMAP_INT_1610_DMA_CH13		60
# define OMAP_INT_1610_DMA_CH14		61
# define OMAP_INT_1610_DMA_CH15		62
# define OMAP_INT_1610_NAND		63

/*
 * OMAP-730 specific IRQ numbers for level 2 interrupt handler
 */
# define OMAP_INT_730_HW_ERRORS		0
# define OMAP_INT_730_NFIQ_PWR_FAIL	1
# define OMAP_INT_730_CFCD		2
# define OMAP_INT_730_CFIREQ		3
# define OMAP_INT_730_I2C		4
# define OMAP_INT_730_PCC		5
# define OMAP_INT_730_MPU_EXT_NIRQ	6
# define OMAP_INT_730_SPI_100K_1	7
# define OMAP_INT_730_SYREN_SPI		8
# define OMAP_INT_730_VLYNQ		9
# define OMAP_INT_730_GPIO_BANK4	10
# define OMAP_INT_730_McBSP1TX		11
# define OMAP_INT_730_McBSP1RX		12
# define OMAP_INT_730_McBSP1RX_OF	13
# define OMAP_INT_730_UART_MODEM_IRDA_2	14
# define OMAP_INT_730_UART_MODEM_1	15
# define OMAP_INT_730_MCSI		16
# define OMAP_INT_730_uWireTX		17
# define OMAP_INT_730_uWireRX		18
# define OMAP_INT_730_SMC_CD		19
# define OMAP_INT_730_SMC_IREQ		20
# define OMAP_INT_730_HDQ_1WIRE		21
# define OMAP_INT_730_TIMER32K		22
# define OMAP_INT_730_MMC_SDIO		23
# define OMAP_INT_730_UPLD		24
# define OMAP_INT_730_USB_HHC_1		27
# define OMAP_INT_730_USB_HHC_2		28
# define OMAP_INT_730_USB_GENI		29
# define OMAP_INT_730_USB_OTG		30
# define OMAP_INT_730_CAMERA_IF		31
# define OMAP_INT_730_RNG		32
# define OMAP_INT_730_DUAL_MODE_TIMER	33
# define OMAP_INT_730_DBB_RF_EN		34
# define OMAP_INT_730_MPUIO_KEYPAD	35
# define OMAP_INT_730_SHA1_MD5		36
# define OMAP_INT_730_SPI_100K_2	37
# define OMAP_INT_730_RNG_IDLE		38
# define OMAP_INT_730_MPUIO		39
# define OMAP_INT_730_LLPC_LCD_CTRL_OFF	40
# define OMAP_INT_730_LLPC_OE_FALLING	41
# define OMAP_INT_730_LLPC_OE_RISING	42
# define OMAP_INT_730_LLPC_VSYNC	43
# define OMAP_INT_730_WAKE_UP_REQ	46
# define OMAP_INT_730_DMA_CH6		53
# define OMAP_INT_730_DMA_CH7		54
# define OMAP_INT_730_DMA_CH8		55
# define OMAP_INT_730_DMA_CH9		56
# define OMAP_INT_730_DMA_CH10		57
# define OMAP_INT_730_DMA_CH11		58
# define OMAP_INT_730_DMA_CH12		59
# define OMAP_INT_730_DMA_CH13		60
# define OMAP_INT_730_DMA_CH14		61
# define OMAP_INT_730_DMA_CH15		62
# define OMAP_INT_730_NAND		63

/* omap_dma.c */
enum omap_dma_model {
    omap_dma_3_0,
    omap_dma_3_1,
    omap_dma_3_2,
};

struct soc_dma_s;
struct soc_dma_s *omap_dma_init(hwaddr base, qemu_irq *irqs,
                MemoryRegion *sysmem,
                qemu_irq lcd_irq, struct omap_mpu_state_s *mpu, omap_clk clk,
                enum omap_dma_model model);
struct soc_dma_s *omap_dma4_init(hwaddr base, qemu_irq *irqs,
                MemoryRegion *sysmem,
                struct omap_mpu_state_s *mpu, int fifo,
                int chans, omap_clk iclk, omap_clk fclk);
void omap_dma_reset(struct soc_dma_s *s);

struct dma_irq_map {
    int ih;
    int intr;
};

/* Only used in OMAP DMA 3.x gigacells */
enum omap_dma_port {
    emiff = 0,
    emifs,
    imif,	/* omap16xx: ocp_t1 */
    tipb,
    local,	/* omap16xx: ocp_t2 */
    tipb_mpui,
    __omap_dma_port_last,
};

typedef enum {
    constant = 0,
    post_incremented,
    single_index,
    double_index,
} omap_dma_addressing_t;

/* Only used in OMAP DMA 3.x gigacells */
struct omap_dma_lcd_channel_s {
    enum omap_dma_port src;
    hwaddr src_f1_top;
    hwaddr src_f1_bottom;
    hwaddr src_f2_top;
    hwaddr src_f2_bottom;

    /* Used in OMAP DMA 3.2 gigacell */
    unsigned char brust_f1;
    unsigned char pack_f1;
    unsigned char data_type_f1;
    unsigned char brust_f2;
    unsigned char pack_f2;
    unsigned char data_type_f2;
    unsigned char end_prog;
    unsigned char repeat;
    unsigned char auto_init;
    unsigned char priority;
    unsigned char fs;
    unsigned char running;
    unsigned char bs;
    unsigned char omap_3_1_compatible_disable;
    unsigned char dst;
    unsigned char lch_type;
    int16_t element_index_f1;
    int16_t element_index_f2;
    int32_t frame_index_f1;
    int32_t frame_index_f2;
    uint16_t elements_f1;
    uint16_t frames_f1;
    uint16_t elements_f2;
    uint16_t frames_f2;
    omap_dma_addressing_t mode_f1;
    omap_dma_addressing_t mode_f2;

    /* Destination port is fixed.  */
    int interrupts;
    int condition;
    int dual;

    int current_frame;
    hwaddr phys_framebuffer[2];
    qemu_irq irq;
    struct omap_mpu_state_s *mpu;
} *omap_dma_get_lcdch(struct soc_dma_s *s);

/*
 * DMA request numbers for OMAP1
 * See /usr/include/asm-arm/arch-omap/dma.h in Linux.
 */
# define OMAP_DMA_NO_DEVICE		0
# define OMAP_DMA_MCSI1_TX		1
# define OMAP_DMA_MCSI1_RX		2
# define OMAP_DMA_I2C_RX		3
# define OMAP_DMA_I2C_TX		4
# define OMAP_DMA_EXT_NDMA_REQ0		5
# define OMAP_DMA_EXT_NDMA_REQ1		6
# define OMAP_DMA_UWIRE_TX		7
# define OMAP_DMA_MCBSP1_TX		8
# define OMAP_DMA_MCBSP1_RX		9
# define OMAP_DMA_MCBSP3_TX		10
# define OMAP_DMA_MCBSP3_RX		11
# define OMAP_DMA_UART1_TX		12
# define OMAP_DMA_UART1_RX		13
# define OMAP_DMA_UART2_TX		14
# define OMAP_DMA_UART2_RX		15
# define OMAP_DMA_MCBSP2_TX		16
# define OMAP_DMA_MCBSP2_RX		17
# define OMAP_DMA_UART3_TX		18
# define OMAP_DMA_UART3_RX		19
# define OMAP_DMA_CAMERA_IF_RX		20
# define OMAP_DMA_MMC_TX		21
# define OMAP_DMA_MMC_RX		22
# define OMAP_DMA_NAND			23	/* Not in OMAP310 */
# define OMAP_DMA_IRQ_LCD_LINE		24	/* Not in OMAP310 */
# define OMAP_DMA_MEMORY_STICK		25	/* Not in OMAP310 */
# define OMAP_DMA_USB_W2FC_RX0		26
# define OMAP_DMA_USB_W2FC_RX1		27
# define OMAP_DMA_USB_W2FC_RX2		28
# define OMAP_DMA_USB_W2FC_TX0		29
# define OMAP_DMA_USB_W2FC_TX1		30
# define OMAP_DMA_USB_W2FC_TX2		31

/* These are only for 1610 */
# define OMAP_DMA_CRYPTO_DES_IN		32
# define OMAP_DMA_SPI_TX		33
# define OMAP_DMA_SPI_RX		34
# define OMAP_DMA_CRYPTO_HASH		35
# define OMAP_DMA_CCP_ATTN		36
# define OMAP_DMA_CCP_FIFO_NOT_EMPTY	37
# define OMAP_DMA_CMT_APE_TX_CHAN_0	38
# define OMAP_DMA_CMT_APE_RV_CHAN_0	39
# define OMAP_DMA_CMT_APE_TX_CHAN_1	40
# define OMAP_DMA_CMT_APE_RV_CHAN_1	41
# define OMAP_DMA_CMT_APE_TX_CHAN_2	42
# define OMAP_DMA_CMT_APE_RV_CHAN_2	43
# define OMAP_DMA_CMT_APE_TX_CHAN_3	44
# define OMAP_DMA_CMT_APE_RV_CHAN_3	45
# define OMAP_DMA_CMT_APE_TX_CHAN_4	46
# define OMAP_DMA_CMT_APE_RV_CHAN_4	47
# define OMAP_DMA_CMT_APE_TX_CHAN_5	48
# define OMAP_DMA_CMT_APE_RV_CHAN_5	49
# define OMAP_DMA_CMT_APE_TX_CHAN_6	50
# define OMAP_DMA_CMT_APE_RV_CHAN_6	51
# define OMAP_DMA_CMT_APE_TX_CHAN_7	52
# define OMAP_DMA_CMT_APE_RV_CHAN_7	53
# define OMAP_DMA_MMC2_TX		54
# define OMAP_DMA_MMC2_RX		55
# define OMAP_DMA_CRYPTO_DES_OUT	56

struct omap_uart_s;
struct omap_uart_s *omap_uart_init(hwaddr base,
                qemu_irq irq, omap_clk fclk, omap_clk iclk,
                qemu_irq txdma, qemu_irq rxdma,
                const char *label, Chardev *chr);
void omap_uart_reset(struct omap_uart_s *s);

struct omap_mpuio_s;
qemu_irq *omap_mpuio_in_get(struct omap_mpuio_s *s);
void omap_mpuio_out_set(struct omap_mpuio_s *s, int line, qemu_irq handler);
void omap_mpuio_key(struct omap_mpuio_s *s, int row, int col, int down);

struct omap_uwire_s;

struct I2SCodec {
    void *opaque;

    /* The CPU can call this if it is generating the clock signal on the
     * i2s port.  The CODEC can ignore it if it is set up as a clock
     * master and generates its own clock.  */
    void (*set_rate)(void *opaque, int in, int out);

    void (*tx_swallow)(void *opaque);
    qemu_irq rx_swallow;
    qemu_irq tx_start;

    int tx_rate;
    int cts;
    int rx_rate;
    int rts;

    struct i2s_fifo_s {
        uint8_t *fifo;
        int len;
        int start;
        int size;
    } in, out;
};
struct omap_mcbsp_s;
void omap_mcbsp_i2s_attach(struct omap_mcbsp_s *s, I2SCodec *slave);

/* omap_lcdc.c */
struct omap_lcd_panel_s;
void omap_lcdc_reset(struct omap_lcd_panel_s *s);
struct omap_lcd_panel_s *omap_lcdc_init(MemoryRegion *sysmem,
                                        hwaddr base,
                                        qemu_irq irq,
                                        struct omap_dma_lcd_channel_s *dma,
                                        omap_clk clk);

/* omap_mmc.c */
#define TYPE_OMAP_MMC "omap-mmc"
OBJECT_DECLARE_SIMPLE_TYPE(OMAPMMCState, OMAP_MMC)

DeviceState *omap_mmc_init(hwaddr base,
                           MemoryRegion *sysmem,
                           qemu_irq irq, qemu_irq dma[], omap_clk clk);
/* TODO: clock framework (see above) */
void omap_mmc_set_clk(DeviceState *dev, omap_clk clk);


/* omap_i2c.c */
I2CBus *omap_i2c_bus(DeviceState *omap_i2c);

# define cpu_is_omap310(cpu)		(cpu->mpu_model == omap310)
# define cpu_is_omap1510(cpu)		(cpu->mpu_model == omap1510)
# define cpu_is_omap1610(cpu)		(cpu->mpu_model == omap1610)
# define cpu_is_omap1710(cpu)		(cpu->mpu_model == omap1710)

# define cpu_is_omap15xx(cpu)		\
        (cpu_is_omap310(cpu) || cpu_is_omap1510(cpu))
# define cpu_is_omap16xx(cpu)		\
        (cpu_is_omap1610(cpu) || cpu_is_omap1710(cpu))

struct omap_mpu_state_s {
    enum omap_mpu_model {
        omap310,
        omap1510,
        omap1610,
        omap1710,
    } mpu_model;

    ARMCPU *cpu;

    qemu_irq *drq;

    qemu_irq wakeup;

    MemoryRegion ulpd_pm_iomem;
    MemoryRegion pin_cfg_iomem;
    MemoryRegion id_iomem;
    MemoryRegion id_iomem_e18;
    MemoryRegion id_iomem_ed4;
    MemoryRegion id_iomem_e20;
    MemoryRegion mpui_iomem;
    MemoryRegion tcmi_iomem;
    MemoryRegion clkm_iomem;
    MemoryRegion clkdsp_iomem;
    MemoryRegion mpui_io_iomem;
    MemoryRegion tap_iomem;
    MemoryRegion imif_ram;
    MemoryRegion sram;

    struct omap_dma_port_if_s {
        uint32_t (*read[3])(struct omap_mpu_state_s *s,
                        hwaddr offset);
        void (*write[3])(struct omap_mpu_state_s *s,
                        hwaddr offset, uint32_t value);
        int (*addr_valid)(struct omap_mpu_state_s *s,
                        hwaddr addr);
    } port[__omap_dma_port_last];

    uint64_t sdram_size;
    unsigned long sram_size;

    /* MPUI-TIPB peripherals */
    struct omap_uart_s *uart[3];

    DeviceState *gpio;

    struct omap_mcbsp_s *mcbsp1;
    struct omap_mcbsp_s *mcbsp3;

    /* MPU public TIPB peripherals */
    struct omap_32khz_timer_s *os_timer;

    DeviceState *mmc;

    struct omap_mpuio_s *mpuio;

    struct omap_uwire_s *microwire;

    struct omap_pwl_s *pwl;
    struct omap_pwt_s *pwt;
    DeviceState *i2c[2];

    struct omap_rtc_s *rtc;

    struct omap_mcbsp_s *mcbsp2;

    struct omap_lpg_s *led[2];

    /* MPU private TIPB peripherals */
    DeviceState *ih[2];

    struct soc_dma_s *dma;

    struct omap_mpu_timer_s *timer[3];
    struct omap_watchdog_timer_s *wdt;

    struct omap_lcd_panel_s *lcd;

    uint32_t ulpd_pm_regs[21];
    int64_t ulpd_gauge_start;

    uint32_t func_mux_ctrl[14];
    uint32_t comp_mode_ctrl[1];
    uint32_t pull_dwn_ctrl[4];
    uint32_t gate_inh_ctrl[1];
    uint32_t voltage_ctrl[1];
    uint32_t test_dbg_ctrl[1];
    uint32_t mod_conf_ctrl[1];
    int compat1509;

    uint32_t mpui_ctrl;

    struct omap_tipb_bridge_s *private_tipb;
    struct omap_tipb_bridge_s *public_tipb;

    uint32_t tcmi_regs[17];

    struct dpll_ctl_s *dpll[3];

    omap_clk clks;
    struct {
        int cold_start;
        int clocking_scheme;
        uint16_t arm_ckctl;
        uint16_t arm_idlect1;
        uint16_t arm_idlect2;
        uint16_t arm_ewupct;
        uint16_t arm_rstct1;
        uint16_t arm_rstct2;
        uint16_t arm_ckout1;
        int dpll1_mode;
        uint16_t dsp_idlect1;
        uint16_t dsp_idlect2;
        uint16_t dsp_rstct2;
    } clkm;
};

/* omap1.c */
struct omap_mpu_state_s *omap310_mpu_init(MemoryRegion *sdram,
                const char *core);

uint32_t omap_badwidth_read8(void *opaque, hwaddr addr);
void omap_badwidth_write8(void *opaque, hwaddr addr,
                uint32_t value);
uint32_t omap_badwidth_read16(void *opaque, hwaddr addr);
void omap_badwidth_write16(void *opaque, hwaddr addr,
                uint32_t value);
uint32_t omap_badwidth_read32(void *opaque, hwaddr addr);
void omap_badwidth_write32(void *opaque, hwaddr addr,
                uint32_t value);

void omap_mpu_wakeup(void *opaque, int irq, int req);

# define OMAP_BAD_REG(paddr)		\
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad register %#08"HWADDR_PRIx"\n", \
                      __func__, paddr)
# define OMAP_RO_REG(paddr)		\
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Read-only register %#08" \
                                       HWADDR_PRIx "\n", \
                      __func__, paddr)

# define OMAP_MPUI_REG_MASK		0x000007ff

#endif
