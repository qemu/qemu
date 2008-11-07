/*
 * QEMU SM501 Device
 *
 * Copyright (c) 2008 Shin-ichiro KAWASAKI
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <assert.h>
#include "hw.h"
#include "pc.h"
#include "console.h"

/*
 * Status: 2008/11/02
 *   - Minimum implementation for Linux console : mmio regs and CRT layer.
 *   - Always updates full screen.
 *
 * TODO:
 *   - Panel support
 *   - Hardware cursor support
 *   - Touch panel support
 *   - USB support
 *   - UART support
 *   - Performance tuning
 */

//#define DEBUG_SM501
//#define DEBUG_BITBLT

#ifdef DEBUG_SM501
#define SM501_DPRINTF(fmt...) printf(fmt)
#else
#define SM501_DPRINTF(fmt...) do {} while(0)
#endif


#define MMIO_BASE_OFFSET 0x3e00000

/* SM501 register definitions taken from "linux/include/linux/sm501-regs.h" */

/* System Configuration area */
/* System config base */
#define SM501_SYS_CONFIG		(0x000000)

/* config 1 */
#define SM501_SYSTEM_CONTROL 		(0x000000)

#define SM501_SYSCTRL_PANEL_TRISTATE	(1<<0)
#define SM501_SYSCTRL_MEM_TRISTATE	(1<<1)
#define SM501_SYSCTRL_CRT_TRISTATE	(1<<2)

#define SM501_SYSCTRL_PCI_SLAVE_BURST_MASK (3<<4)
#define SM501_SYSCTRL_PCI_SLAVE_BURST_1	(0<<4)
#define SM501_SYSCTRL_PCI_SLAVE_BURST_2	(1<<4)
#define SM501_SYSCTRL_PCI_SLAVE_BURST_4	(2<<4)
#define SM501_SYSCTRL_PCI_SLAVE_BURST_8	(3<<4)

#define SM501_SYSCTRL_PCI_CLOCK_RUN_EN	(1<<6)
#define SM501_SYSCTRL_PCI_RETRY_DISABLE	(1<<7)
#define SM501_SYSCTRL_PCI_SUBSYS_LOCK	(1<<11)
#define SM501_SYSCTRL_PCI_BURST_READ_EN	(1<<15)

/* miscellaneous control */

#define SM501_MISC_CONTROL		(0x000004)

#define SM501_MISC_BUS_SH		(0x0)
#define SM501_MISC_BUS_PCI		(0x1)
#define SM501_MISC_BUS_XSCALE		(0x2)
#define SM501_MISC_BUS_NEC		(0x6)
#define SM501_MISC_BUS_MASK		(0x7)

#define SM501_MISC_VR_62MB		(1<<3)
#define SM501_MISC_CDR_RESET		(1<<7)
#define SM501_MISC_USB_LB		(1<<8)
#define SM501_MISC_USB_SLAVE		(1<<9)
#define SM501_MISC_BL_1			(1<<10)
#define SM501_MISC_MC			(1<<11)
#define SM501_MISC_DAC_POWER		(1<<12)
#define SM501_MISC_IRQ_INVERT		(1<<16)
#define SM501_MISC_SH			(1<<17)

#define SM501_MISC_HOLD_EMPTY		(0<<18)
#define SM501_MISC_HOLD_8		(1<<18)
#define SM501_MISC_HOLD_16		(2<<18)
#define SM501_MISC_HOLD_24		(3<<18)
#define SM501_MISC_HOLD_32		(4<<18)
#define SM501_MISC_HOLD_MASK		(7<<18)

#define SM501_MISC_FREQ_12		(1<<24)
#define SM501_MISC_PNL_24BIT		(1<<25)
#define SM501_MISC_8051_LE		(1<<26)



#define SM501_GPIO31_0_CONTROL		(0x000008)
#define SM501_GPIO63_32_CONTROL		(0x00000C)
#define SM501_DRAM_CONTROL		(0x000010)

/* command list */
#define SM501_ARBTRTN_CONTROL		(0x000014)

/* command list */
#define SM501_COMMAND_LIST_STATUS	(0x000024)

/* interrupt debug */
#define SM501_RAW_IRQ_STATUS		(0x000028)
#define SM501_RAW_IRQ_CLEAR		(0x000028)
#define SM501_IRQ_STATUS		(0x00002C)
#define SM501_IRQ_MASK			(0x000030)
#define SM501_DEBUG_CONTROL		(0x000034)

/* power management */
#define SM501_POWERMODE_P2X_SRC		(1<<29)
#define SM501_POWERMODE_V2X_SRC		(1<<20)
#define SM501_POWERMODE_M_SRC		(1<<12)
#define SM501_POWERMODE_M1_SRC		(1<<4)

#define SM501_CURRENT_GATE		(0x000038)
#define SM501_CURRENT_CLOCK		(0x00003C)
#define SM501_POWER_MODE_0_GATE		(0x000040)
#define SM501_POWER_MODE_0_CLOCK	(0x000044)
#define SM501_POWER_MODE_1_GATE		(0x000048)
#define SM501_POWER_MODE_1_CLOCK	(0x00004C)
#define SM501_SLEEP_MODE_GATE		(0x000050)
#define SM501_POWER_MODE_CONTROL	(0x000054)

/* power gates for units within the 501 */
#define SM501_GATE_HOST			(0)
#define SM501_GATE_MEMORY		(1)
#define SM501_GATE_DISPLAY		(2)
#define SM501_GATE_2D_ENGINE		(3)
#define SM501_GATE_CSC			(4)
#define SM501_GATE_ZVPORT		(5)
#define SM501_GATE_GPIO			(6)
#define SM501_GATE_UART0		(7)
#define SM501_GATE_UART1		(8)
#define SM501_GATE_SSP			(10)
#define SM501_GATE_USB_HOST		(11)
#define SM501_GATE_USB_GADGET		(12)
#define SM501_GATE_UCONTROLLER		(17)
#define SM501_GATE_AC97			(18)

/* panel clock */
#define SM501_CLOCK_P2XCLK		(24)
/* crt clock */
#define SM501_CLOCK_V2XCLK		(16)
/* main clock */
#define SM501_CLOCK_MCLK		(8)
/* SDRAM controller clock */
#define SM501_CLOCK_M1XCLK		(0)

/* config 2 */
#define SM501_PCI_MASTER_BASE		(0x000058)
#define SM501_ENDIAN_CONTROL		(0x00005C)
#define SM501_DEVICEID			(0x000060)
/* 0x050100A0 */

#define SM501_DEVICEID_SM501		(0x05010000)
#define SM501_DEVICEID_IDMASK		(0xffff0000)
#define SM501_DEVICEID_REVMASK		(0x000000ff)

#define SM501_PLLCLOCK_COUNT		(0x000064)
#define SM501_MISC_TIMING		(0x000068)
#define SM501_CURRENT_SDRAM_CLOCK	(0x00006C)

#define SM501_PROGRAMMABLE_PLL_CONTROL	(0x000074)

/* GPIO base */
#define SM501_GPIO			(0x010000)
#define SM501_GPIO_DATA_LOW		(0x00)
#define SM501_GPIO_DATA_HIGH		(0x04)
#define SM501_GPIO_DDR_LOW		(0x08)
#define SM501_GPIO_DDR_HIGH		(0x0C)
#define SM501_GPIO_IRQ_SETUP		(0x10)
#define SM501_GPIO_IRQ_STATUS		(0x14)
#define SM501_GPIO_IRQ_RESET		(0x14)

/* I2C controller base */
#define SM501_I2C			(0x010040)
#define SM501_I2C_BYTE_COUNT		(0x00)
#define SM501_I2C_CONTROL		(0x01)
#define SM501_I2C_STATUS		(0x02)
#define SM501_I2C_RESET			(0x02)
#define SM501_I2C_SLAVE_ADDRESS		(0x03)
#define SM501_I2C_DATA			(0x04)

/* SSP base */
#define SM501_SSP			(0x020000)

/* Uart 0 base */
#define SM501_UART0			(0x030000)

/* Uart 1 base */
#define SM501_UART1			(0x030020)

/* USB host port base */
#define SM501_USB_HOST			(0x040000)

/* USB slave/gadget base */
#define SM501_USB_GADGET		(0x060000)

/* USB slave/gadget data port base */
#define SM501_USB_GADGET_DATA		(0x070000)

/* Display controller/video engine base */
#define SM501_DC			(0x080000)

/* common defines for the SM501 address registers */
#define SM501_ADDR_FLIP			(1<<31)
#define SM501_ADDR_EXT			(1<<27)
#define SM501_ADDR_CS1			(1<<26)
#define SM501_ADDR_MASK			(0x3f << 26)

#define SM501_FIFO_MASK			(0x3 << 16)
#define SM501_FIFO_1			(0x0 << 16)
#define SM501_FIFO_3			(0x1 << 16)
#define SM501_FIFO_7			(0x2 << 16)
#define SM501_FIFO_11			(0x3 << 16)

/* common registers for panel and the crt */
#define SM501_OFF_DC_H_TOT		(0x000)
#define SM501_OFF_DC_V_TOT		(0x008)
#define SM501_OFF_DC_H_SYNC		(0x004)
#define SM501_OFF_DC_V_SYNC		(0x00C)

#define SM501_DC_PANEL_CONTROL		(0x000)

#define SM501_DC_PANEL_CONTROL_FPEN	(1<<27)
#define SM501_DC_PANEL_CONTROL_BIAS	(1<<26)
#define SM501_DC_PANEL_CONTROL_DATA	(1<<25)
#define SM501_DC_PANEL_CONTROL_VDD	(1<<24)
#define SM501_DC_PANEL_CONTROL_DP	(1<<23)

#define SM501_DC_PANEL_CONTROL_TFT_888	(0<<21)
#define SM501_DC_PANEL_CONTROL_TFT_333	(1<<21)
#define SM501_DC_PANEL_CONTROL_TFT_444	(2<<21)

#define SM501_DC_PANEL_CONTROL_DE	(1<<20)

#define SM501_DC_PANEL_CONTROL_LCD_TFT	(0<<18)
#define SM501_DC_PANEL_CONTROL_LCD_STN8	(1<<18)
#define SM501_DC_PANEL_CONTROL_LCD_STN12 (2<<18)

#define SM501_DC_PANEL_CONTROL_CP	(1<<14)
#define SM501_DC_PANEL_CONTROL_VSP	(1<<13)
#define SM501_DC_PANEL_CONTROL_HSP	(1<<12)
#define SM501_DC_PANEL_CONTROL_CK	(1<<9)
#define SM501_DC_PANEL_CONTROL_TE	(1<<8)
#define SM501_DC_PANEL_CONTROL_VPD	(1<<7)
#define SM501_DC_PANEL_CONTROL_VP	(1<<6)
#define SM501_DC_PANEL_CONTROL_HPD	(1<<5)
#define SM501_DC_PANEL_CONTROL_HP	(1<<4)
#define SM501_DC_PANEL_CONTROL_GAMMA	(1<<3)
#define SM501_DC_PANEL_CONTROL_EN	(1<<2)

#define SM501_DC_PANEL_CONTROL_8BPP	(0<<0)
#define SM501_DC_PANEL_CONTROL_16BPP	(1<<0)
#define SM501_DC_PANEL_CONTROL_32BPP	(2<<0)


#define SM501_DC_PANEL_PANNING_CONTROL	(0x004)
#define SM501_DC_PANEL_COLOR_KEY	(0x008)
#define SM501_DC_PANEL_FB_ADDR		(0x00C)
#define SM501_DC_PANEL_FB_OFFSET	(0x010)
#define SM501_DC_PANEL_FB_WIDTH		(0x014)
#define SM501_DC_PANEL_FB_HEIGHT	(0x018)
#define SM501_DC_PANEL_TL_LOC		(0x01C)
#define SM501_DC_PANEL_BR_LOC		(0x020)
#define SM501_DC_PANEL_H_TOT		(0x024)
#define SM501_DC_PANEL_H_SYNC		(0x028)
#define SM501_DC_PANEL_V_TOT		(0x02C)
#define SM501_DC_PANEL_V_SYNC		(0x030)
#define SM501_DC_PANEL_CUR_LINE		(0x034)

#define SM501_DC_VIDEO_CONTROL		(0x040)
#define SM501_DC_VIDEO_FB0_ADDR		(0x044)
#define SM501_DC_VIDEO_FB_WIDTH		(0x048)
#define SM501_DC_VIDEO_FB0_LAST_ADDR	(0x04C)
#define SM501_DC_VIDEO_TL_LOC		(0x050)
#define SM501_DC_VIDEO_BR_LOC		(0x054)
#define SM501_DC_VIDEO_SCALE		(0x058)
#define SM501_DC_VIDEO_INIT_SCALE	(0x05C)
#define SM501_DC_VIDEO_YUV_CONSTANTS	(0x060)
#define SM501_DC_VIDEO_FB1_ADDR		(0x064)
#define SM501_DC_VIDEO_FB1_LAST_ADDR	(0x068)

#define SM501_DC_VIDEO_ALPHA_CONTROL	(0x080)
#define SM501_DC_VIDEO_ALPHA_FB_ADDR	(0x084)
#define SM501_DC_VIDEO_ALPHA_FB_OFFSET	(0x088)
#define SM501_DC_VIDEO_ALPHA_FB_LAST_ADDR	(0x08C)
#define SM501_DC_VIDEO_ALPHA_TL_LOC	(0x090)
#define SM501_DC_VIDEO_ALPHA_BR_LOC	(0x094)
#define SM501_DC_VIDEO_ALPHA_SCALE	(0x098)
#define SM501_DC_VIDEO_ALPHA_INIT_SCALE	(0x09C)
#define SM501_DC_VIDEO_ALPHA_CHROMA_KEY	(0x0A0)
#define SM501_DC_VIDEO_ALPHA_COLOR_LOOKUP	(0x0A4)

#define SM501_DC_PANEL_HWC_BASE		(0x0F0)
#define SM501_DC_PANEL_HWC_ADDR		(0x0F0)
#define SM501_DC_PANEL_HWC_LOC		(0x0F4)
#define SM501_DC_PANEL_HWC_COLOR_1_2	(0x0F8)
#define SM501_DC_PANEL_HWC_COLOR_3	(0x0FC)

#define SM501_HWC_EN			(1<<31)

#define SM501_OFF_HWC_ADDR		(0x00)
#define SM501_OFF_HWC_LOC		(0x04)
#define SM501_OFF_HWC_COLOR_1_2		(0x08)
#define SM501_OFF_HWC_COLOR_3		(0x0C)

#define SM501_DC_ALPHA_CONTROL		(0x100)
#define SM501_DC_ALPHA_FB_ADDR		(0x104)
#define SM501_DC_ALPHA_FB_OFFSET	(0x108)
#define SM501_DC_ALPHA_TL_LOC		(0x10C)
#define SM501_DC_ALPHA_BR_LOC		(0x110)
#define SM501_DC_ALPHA_CHROMA_KEY	(0x114)
#define SM501_DC_ALPHA_COLOR_LOOKUP	(0x118)

#define SM501_DC_CRT_CONTROL		(0x200)

#define SM501_DC_CRT_CONTROL_TVP	(1<<15)
#define SM501_DC_CRT_CONTROL_CP		(1<<14)
#define SM501_DC_CRT_CONTROL_VSP	(1<<13)
#define SM501_DC_CRT_CONTROL_HSP	(1<<12)
#define SM501_DC_CRT_CONTROL_VS		(1<<11)
#define SM501_DC_CRT_CONTROL_BLANK	(1<<10)
#define SM501_DC_CRT_CONTROL_SEL	(1<<9)
#define SM501_DC_CRT_CONTROL_TE		(1<<8)
#define SM501_DC_CRT_CONTROL_PIXEL_MASK (0xF << 4)
#define SM501_DC_CRT_CONTROL_GAMMA	(1<<3)
#define SM501_DC_CRT_CONTROL_ENABLE	(1<<2)

#define SM501_DC_CRT_CONTROL_8BPP	(0<<0)
#define SM501_DC_CRT_CONTROL_16BPP	(1<<0)
#define SM501_DC_CRT_CONTROL_32BPP	(2<<0)

#define SM501_DC_CRT_FB_ADDR		(0x204)
#define SM501_DC_CRT_FB_OFFSET		(0x208)
#define SM501_DC_CRT_H_TOT		(0x20C)
#define SM501_DC_CRT_H_SYNC		(0x210)
#define SM501_DC_CRT_V_TOT		(0x214)
#define SM501_DC_CRT_V_SYNC		(0x218)
#define SM501_DC_CRT_SIGNATURE_ANALYZER	(0x21C)
#define SM501_DC_CRT_CUR_LINE		(0x220)
#define SM501_DC_CRT_MONITOR_DETECT	(0x224)

#define SM501_DC_CRT_HWC_BASE		(0x230)
#define SM501_DC_CRT_HWC_ADDR		(0x230)
#define SM501_DC_CRT_HWC_LOC		(0x234)
#define SM501_DC_CRT_HWC_COLOR_1_2	(0x238)
#define SM501_DC_CRT_HWC_COLOR_3	(0x23C)

#define SM501_DC_PANEL_PALETTE		(0x400)

#define SM501_DC_VIDEO_PALETTE		(0x800)

#define SM501_DC_CRT_PALETTE		(0xC00)

/* Zoom Video port base */
#define SM501_ZVPORT			(0x090000)

/* AC97/I2S base */
#define SM501_AC97			(0x0A0000)

/* 8051 micro controller base */
#define SM501_UCONTROLLER		(0x0B0000)

/* 8051 micro controller SRAM base */
#define SM501_UCONTROLLER_SRAM		(0x0C0000)

/* DMA base */
#define SM501_DMA			(0x0D0000)

/* 2d engine base */
#define SM501_2D_ENGINE			(0x100000)
#define SM501_2D_SOURCE			(0x00)
#define SM501_2D_DESTINATION		(0x04)
#define SM501_2D_DIMENSION		(0x08)
#define SM501_2D_CONTROL		(0x0C)
#define SM501_2D_PITCH			(0x10)
#define SM501_2D_FOREGROUND		(0x14)
#define SM501_2D_BACKGROUND		(0x18)
#define SM501_2D_STRETCH		(0x1C)
#define SM501_2D_COLOR_COMPARE		(0x20)
#define SM501_2D_COLOR_COMPARE_MASK 	(0x24)
#define SM501_2D_MASK			(0x28)
#define SM501_2D_CLIP_TL		(0x2C)
#define SM501_2D_CLIP_BR		(0x30)
#define SM501_2D_MONO_PATTERN_LOW	(0x34)
#define SM501_2D_MONO_PATTERN_HIGH	(0x38)
#define SM501_2D_WINDOW_WIDTH		(0x3C)
#define SM501_2D_SOURCE_BASE		(0x40)
#define SM501_2D_DESTINATION_BASE	(0x44)
#define SM501_2D_ALPHA			(0x48)
#define SM501_2D_WRAP			(0x4C)
#define SM501_2D_STATUS			(0x50)

#define SM501_CSC_Y_SOURCE_BASE		(0xC8)
#define SM501_CSC_CONSTANTS		(0xCC)
#define SM501_CSC_Y_SOURCE_X		(0xD0)
#define SM501_CSC_Y_SOURCE_Y		(0xD4)
#define SM501_CSC_U_SOURCE_BASE		(0xD8)
#define SM501_CSC_V_SOURCE_BASE		(0xDC)
#define SM501_CSC_SOURCE_DIMENSION	(0xE0)
#define SM501_CSC_SOURCE_PITCH		(0xE4)
#define SM501_CSC_DESTINATION		(0xE8)
#define SM501_CSC_DESTINATION_DIMENSION	(0xEC)
#define SM501_CSC_DESTINATION_PITCH	(0xF0)
#define SM501_CSC_SCALE_FACTOR		(0xF4)
#define SM501_CSC_DESTINATION_BASE	(0xF8)
#define SM501_CSC_CONTROL		(0xFC)

/* 2d engine data port base */
#define SM501_2D_ENGINE_DATA		(0x110000)

/* end of register definitions */


/* SM501 local memory size taken from "linux/drivers/mfd/sm501.c" */
static const uint32_t sm501_mem_local_size[] = {
	[0]	= 4*1024*1024,
	[1]	= 8*1024*1024,
	[2]	= 16*1024*1024,
	[3]	= 32*1024*1024,
	[4]	= 64*1024*1024,
	[5]	= 2*1024*1024,
};
#define get_local_mem_size(s) sm501_mem_local_size[(s)->local_mem_size_index]

typedef struct SM501State {
    /* graphic console status */
    DisplayState *ds;
    QEMUConsole *console;

    /* status & internal resources */
    target_phys_addr_t base;
    uint32_t local_mem_size_index;
    uint8_t * local_mem;
    uint32_t last_width;
    uint32_t last_height;

    /* mmio registers */
    uint32_t system_control;
    uint32_t misc_control;
    uint32_t gpio_31_0_control;
    uint32_t gpio_63_32_control;
    uint32_t dram_control;
    uint32_t irq_mask;
    uint32_t misc_timing;
    uint32_t power_mode_control;

    uint32_t uart0_ier;
    uint32_t uart0_lcr;
    uint32_t uart0_mcr;
    uint32_t uart0_scr;

    uint8_t dc_palette[0x400 * 3];

    uint32_t dc_panel_control;
    uint32_t dc_panel_panning_control;
    uint32_t dc_panel_fb_addr;
    uint32_t dc_panel_fb_offset;
    uint32_t dc_panel_fb_width;
    uint32_t dc_panel_fb_height;
    uint32_t dc_panel_tl_location;
    uint32_t dc_panel_br_location;
    uint32_t dc_panel_h_total;
    uint32_t dc_panel_h_sync;
    uint32_t dc_panel_v_total;
    uint32_t dc_panel_v_sync;

    uint32_t dc_panel_hwc_addr;
    uint32_t dc_panel_hwc_location;
    uint32_t dc_panel_hwc_color_1_2;
    uint32_t dc_panel_hwc_color_3;

    uint32_t dc_crt_control;
    uint32_t dc_crt_fb_addr;
    uint32_t dc_crt_fb_offset;
    uint32_t dc_crt_h_total;
    uint32_t dc_crt_h_sync;
    uint32_t dc_crt_v_total;
    uint32_t dc_crt_v_sync;

    uint32_t dc_crt_hwc_addr;
    uint32_t dc_crt_hwc_location;
    uint32_t dc_crt_hwc_color_1_2;
    uint32_t dc_crt_hwc_color_3;

} SM501State;

static uint32_t get_local_mem_size_index(uint32_t size)
{
    uint32_t norm_size = 0;
    int i, index = 0;

    for (i = 0; i < sizeof(sm501_mem_local_size)/sizeof(uint32_t); i++) {
	uint32_t new_size = sm501_mem_local_size[i];
	if (new_size >= size) {
	    if (norm_size == 0 || norm_size > new_size) {
		norm_size = new_size;
		index = i;
	    }
	}
    }

    return index;
}

static uint32_t sm501_system_config_read(void *opaque, target_phys_addr_t addr)
{
    SM501State * s = (SM501State *)opaque;
    uint32_t offset = addr - (s->base + MMIO_BASE_OFFSET);
    uint32_t ret = 0;
    SM501_DPRINTF("sm501 system config regs : read addr=%x, offset=%x\n",
		  addr, offset);

    switch(offset) {
    case SM501_SYSTEM_CONTROL:
	ret = s->system_control;
	break;
    case SM501_MISC_CONTROL:
	ret = s->misc_control;
	break;
    case SM501_GPIO31_0_CONTROL:
	ret = s->gpio_31_0_control;
	break;
    case SM501_GPIO63_32_CONTROL:
	ret = s->gpio_63_32_control;
	break;
    case SM501_DEVICEID:
	ret = 0x050100A0;
	break;
    case SM501_DRAM_CONTROL:
	ret = (s->dram_control & 0x07F107C0) | s->local_mem_size_index << 13;
	break;
    case SM501_IRQ_MASK:
	ret = s->irq_mask;
	break;
    case SM501_MISC_TIMING:
	/* TODO : simulate gate control */
	ret = s->misc_timing;
	break;
    case SM501_CURRENT_GATE:
	/* TODO : simulate gate control */
	ret = 0x00021807;
	break;
    case SM501_CURRENT_CLOCK:
	ret = 0x2A1A0A09;
	break;
    case SM501_POWER_MODE_CONTROL:
	ret = s->power_mode_control;
	break;

    default:
	printf("sm501 system config : not implemented register read."
	       " addr=%x, offset=%x\n", addr, offset);
	assert(0);
    }

    return ret;
}

static void sm501_system_config_write(void *opaque,
				      target_phys_addr_t addr, uint32_t value)
{
    SM501State * s = (SM501State *)opaque;
    uint32_t offset = addr - (s->base + MMIO_BASE_OFFSET);
    SM501_DPRINTF("sm501 system config regs : write addr=%x, ofs=%x, val=%x\n",
		  addr, offset, value);

    switch(offset) {
    case SM501_SYSTEM_CONTROL:
	s->system_control = value & 0xE300B8F7;
	break;
    case SM501_MISC_CONTROL:
	s->misc_control = value & 0xFF7FFF20;
	break;
    case SM501_GPIO31_0_CONTROL:
	s->gpio_31_0_control = value;
	break;
    case SM501_GPIO63_32_CONTROL:
	s->gpio_63_32_control = value;
	break;
    case SM501_DRAM_CONTROL:
	s->local_mem_size_index = (value >> 13) & 0x7;
	/* rODO : check validity of size change */
	s->dram_control |=  value & 0x7FFFFFC3;
	break;
    case SM501_IRQ_MASK:
	s->irq_mask = value;
	break;
    case SM501_MISC_TIMING:
	s->misc_timing = value & 0xF31F1FFF;
	break;
    case SM501_POWER_MODE_0_GATE:
    case SM501_POWER_MODE_1_GATE:
    case SM501_POWER_MODE_0_CLOCK:
    case SM501_POWER_MODE_1_CLOCK:
	/* TODO : simulate gate & clock control */
	break;
    case SM501_POWER_MODE_CONTROL:
	s->power_mode_control = value & 0x00000003;
	break;

    default:
	printf("sm501 system config : not implemented register write."
	       " addr=%x, val=%x\n", addr, value);
	assert(0);
    }
}

static CPUReadMemoryFunc *sm501_system_config_readfn[] = {
    NULL,
    NULL,
    &sm501_system_config_read,
};

static CPUWriteMemoryFunc *sm501_system_config_writefn[] = {
    NULL,
    NULL,
    &sm501_system_config_write,
};

static uint32_t sm501_disp_ctrl_read(void *opaque,
					      target_phys_addr_t addr)
{
    SM501State * s = (SM501State *)opaque;
    uint32_t offset = addr - (s->base + MMIO_BASE_OFFSET + SM501_DC);
    uint32_t ret = 0;
    SM501_DPRINTF("sm501 disp ctrl regs : read addr=%x, offset=%x\n",
		  addr, offset);

    switch(offset) {

    case SM501_DC_PANEL_CONTROL:
	ret = s->dc_panel_control;
	break;
    case SM501_DC_PANEL_PANNING_CONTROL:
	ret = s->dc_panel_panning_control;
	break;
    case SM501_DC_PANEL_FB_ADDR:
	ret = s->dc_panel_fb_addr;
	break;
    case SM501_DC_PANEL_FB_OFFSET:
	ret = s->dc_panel_fb_offset;
	break;
    case SM501_DC_PANEL_FB_WIDTH:
	ret = s->dc_panel_fb_width;
	break;
    case SM501_DC_PANEL_FB_HEIGHT:
	ret = s->dc_panel_fb_height;
	break;
    case SM501_DC_PANEL_TL_LOC:
	ret = s->dc_panel_tl_location;
	break;
    case SM501_DC_PANEL_BR_LOC:
	ret = s->dc_panel_br_location;
	break;

    case SM501_DC_PANEL_H_TOT:
	ret = s->dc_panel_h_total;
	break;
    case SM501_DC_PANEL_H_SYNC:
	ret = s->dc_panel_h_sync;
	break;
    case SM501_DC_PANEL_V_TOT:
	ret = s->dc_panel_v_total;
	break;
    case SM501_DC_PANEL_V_SYNC:
	ret = s->dc_panel_v_sync;
	break;

    case SM501_DC_CRT_CONTROL:
	ret = s->dc_crt_control;
	break;
    case SM501_DC_CRT_FB_ADDR:
	ret = s->dc_crt_fb_addr;
	break;
    case SM501_DC_CRT_FB_OFFSET:
	ret = s->dc_crt_fb_offset;
	break;
    case SM501_DC_CRT_H_TOT:
	ret = s->dc_crt_h_total;
	break;
    case SM501_DC_CRT_H_SYNC:
	ret = s->dc_crt_h_sync;
	break;
    case SM501_DC_CRT_V_TOT:
	ret = s->dc_crt_v_total;
	break;
    case SM501_DC_CRT_V_SYNC:
	ret = s->dc_crt_v_sync;
	break;

    case SM501_DC_CRT_HWC_ADDR:
	ret = s->dc_crt_hwc_addr;
	break;
    case SM501_DC_CRT_HWC_LOC:
	ret = s->dc_crt_hwc_addr;
	break;
    case SM501_DC_CRT_HWC_COLOR_1_2:
	ret = s->dc_crt_hwc_addr;
	break;
    case SM501_DC_CRT_HWC_COLOR_3:
	ret = s->dc_crt_hwc_addr;
	break;

    default:
	printf("sm501 disp ctrl : not implemented register read."
	       " addr=%x, offset=%x\n", addr, offset);
	assert(0);
    }

    return ret;
}

static void sm501_disp_ctrl_write(void *opaque,
					   target_phys_addr_t addr,
					   uint32_t value)
{
    SM501State * s = (SM501State *)opaque;
    uint32_t offset = addr - (s->base + MMIO_BASE_OFFSET + SM501_DC);
    SM501_DPRINTF("sm501 disp ctrl regs : write addr=%x, ofs=%x, val=%x\n",
		  addr, offset, value);

    switch(offset) {
    case SM501_DC_PANEL_CONTROL:
	s->dc_panel_control = value & 0x0FFF73FF;
	break;
    case SM501_DC_PANEL_PANNING_CONTROL:
	s->dc_panel_panning_control = value & 0xFF3FFF3F;
	break;
    case SM501_DC_PANEL_FB_ADDR:
	s->dc_panel_fb_addr = value & 0x8FFFFFF0;
	break;
    case SM501_DC_PANEL_FB_OFFSET:
	s->dc_panel_fb_offset = value & 0x3FF03FF0;
	break;
    case SM501_DC_PANEL_FB_WIDTH:
	s->dc_panel_fb_width = value & 0x0FFF0FFF;
	break;
    case SM501_DC_PANEL_FB_HEIGHT:
	s->dc_panel_fb_height = value & 0x0FFF0FFF;
	break;
    case SM501_DC_PANEL_TL_LOC:
	s->dc_panel_tl_location = value & 0x07FF07FF;
	break;
    case SM501_DC_PANEL_BR_LOC:
	s->dc_panel_br_location = value & 0x07FF07FF;
	break;

    case SM501_DC_PANEL_H_TOT:
	s->dc_panel_h_total = value & 0x0FFF0FFF;
	break;
    case SM501_DC_PANEL_H_SYNC:
	s->dc_panel_h_sync = value & 0x00FF0FFF;
	break;
    case SM501_DC_PANEL_V_TOT:
	s->dc_panel_v_total = value & 0x0FFF0FFF;
	break;
    case SM501_DC_PANEL_V_SYNC:
	s->dc_panel_v_sync = value & 0x003F0FFF;
	break;

    case SM501_DC_PANEL_HWC_ADDR:
	s->dc_panel_hwc_addr = value & 0x8FFFFFF0;
	break;
    case SM501_DC_PANEL_HWC_LOC:
	s->dc_panel_hwc_addr = value & 0x0FFF0FFF;
	break;
    case SM501_DC_PANEL_HWC_COLOR_1_2:
	s->dc_panel_hwc_addr = value;
	break;
    case SM501_DC_PANEL_HWC_COLOR_3:
	s->dc_panel_hwc_addr = value & 0x0000FFFF;
	break;

    case SM501_DC_CRT_CONTROL:
	s->dc_crt_control = value & 0x0003FFFF;
	break;
    case SM501_DC_CRT_FB_ADDR:
	s->dc_crt_fb_addr = value & 0x8FFFFFF0;
	break;
    case SM501_DC_CRT_FB_OFFSET:
	s->dc_crt_fb_offset = value & 0x3FF03FF0;
	break;
    case SM501_DC_CRT_H_TOT:
	s->dc_crt_h_total = value & 0x0FFF0FFF;
	break;
    case SM501_DC_CRT_H_SYNC:
	s->dc_crt_h_sync = value & 0x00FF0FFF;
	break;
    case SM501_DC_CRT_V_TOT:
	s->dc_crt_v_total = value & 0x0FFF0FFF;
	break;
    case SM501_DC_CRT_V_SYNC:
	s->dc_crt_v_sync = value & 0x003F0FFF;
	break;

    case SM501_DC_CRT_HWC_ADDR:
	s->dc_crt_hwc_addr = value & 0x8FFFFFF0;
	break;
    case SM501_DC_CRT_HWC_LOC:
	s->dc_crt_hwc_addr = value & 0x0FFF0FFF;
	break;
    case SM501_DC_CRT_HWC_COLOR_1_2:
	s->dc_crt_hwc_addr = value;
	break;
    case SM501_DC_CRT_HWC_COLOR_3:
	s->dc_crt_hwc_addr = value & 0x0000FFFF;
	break;

    default:
	printf("sm501 disp ctrl : not implemented register write."
	       " addr=%x, val=%x\n", addr, value);
	assert(0);
    }
}

static CPUReadMemoryFunc *sm501_disp_ctrl_readfn[] = {
    NULL,
    NULL,
    &sm501_disp_ctrl_read,
};

static CPUWriteMemoryFunc *sm501_disp_ctrl_writefn[] = {
    NULL,
    NULL,
    &sm501_disp_ctrl_write,
};

static uint32_t sm501_palette_read(void *opaque, target_phys_addr_t addr)
{
    SM501State * s = (SM501State *)opaque;
    uint32_t offset = addr - (s->base + MMIO_BASE_OFFSET
			      + SM501_DC + SM501_DC_PANEL_PALETTE);
    SM501_DPRINTF("sm501 palette read addr=%x, offset=%x\n", addr, offset);

    /* TODO : consider BYTE/WORD access */
    /* TODO : consider endian */

    assert(0 <= offset && offset < 0x400 * 3);
    return *(uint32_t*)&s->dc_palette[offset];
}

static void sm501_palette_write(void *opaque,
				target_phys_addr_t addr, uint32_t value)
{
    SM501State * s = (SM501State *)opaque;
    uint32_t offset = addr - (s->base + MMIO_BASE_OFFSET
			      + SM501_DC + SM501_DC_PANEL_PALETTE);
    SM501_DPRINTF("sm501 palette write addr=%x, ofs=%x, val=%x\n",
		  addr, offset, value);

    /* TODO : consider BYTE/WORD access */
    /* TODO : consider endian */

    assert(0 <= offset && offset < 0x400 * 3);
    *(uint32_t*)&s->dc_palette[offset] = value;
}

static CPUReadMemoryFunc *sm501_palette_readfn[] = {
    &sm501_palette_read,
    &sm501_palette_read,
    &sm501_palette_read,
};

static CPUWriteMemoryFunc *sm501_palette_writefn[] = {
    &sm501_palette_write,
    &sm501_palette_write,
    &sm501_palette_write,
};


/* draw line functions for all console modes */

#include "pixel_ops.h"

typedef void draw_line_func(uint8_t *d, const uint8_t *s,
			    int width, const uint32_t *pal);

#define DEPTH 8
#include "sm501_template.h"

#define DEPTH 15
#include "sm501_template.h"

#define BGR_FORMAT
#define DEPTH 15
#include "sm501_template.h"

#define DEPTH 16
#include "sm501_template.h"

#define BGR_FORMAT
#define DEPTH 16
#include "sm501_template.h"

#define DEPTH 32
#include "sm501_template.h"

#define BGR_FORMAT
#define DEPTH 32
#include "sm501_template.h"

static draw_line_func * draw_line8_funcs[] = {
    draw_line8_8,
    draw_line8_15,
    draw_line8_16,
    draw_line8_32,
    draw_line8_32bgr,
    draw_line8_15bgr,
    draw_line8_16bgr,
};

static draw_line_func * draw_line16_funcs[] = {
    draw_line16_8,
    draw_line16_15,
    draw_line16_16,
    draw_line16_32,
    draw_line16_32bgr,
    draw_line16_15bgr,
    draw_line16_16bgr,
};

static draw_line_func * draw_line32_funcs[] = {
    draw_line32_8,
    draw_line32_15,
    draw_line32_16,
    draw_line32_32,
    draw_line32_32bgr,
    draw_line32_15bgr,
    draw_line32_16bgr,
};

static inline int get_depth_index(DisplayState *s)
{
    switch(s->depth) {
    default:
    case 8:
	return 0;
    case 15:
	if (s->bgr)
	    return 5;
	else
	    return 1;
    case 16:
	if (s->bgr)
	    return 6;
	else
	    return 2;
    case 32:
	if (s->bgr)
	    return 4;
	else
	    return 3;
    }
}

static void sm501_draw_crt(SM501State * s)
{
    int y;
    int width = (s->dc_crt_h_total & 0x00000FFF) + 1;
    int height = (s->dc_crt_v_total & 0x00000FFF) + 1;

    uint8_t  * src = s->local_mem;
    int src_bpp = 0;
    int dst_bpp = s->ds->depth / 8 + (s->ds->depth % 8 ? 1 : 0);
    uint32_t * palette = (uint32_t *)&s->dc_palette[SM501_DC_CRT_PALETTE
						    - SM501_DC_PANEL_PALETTE];
    int ds_depth_index = get_depth_index(s->ds);
    draw_line_func * draw_line = NULL;
    int full_update = 0;
    int y_start = -1;
    int page_min = 0x7fffffff;
    int page_max = -1;

    /* choose draw_line function */
    switch (s->dc_crt_control & 3) {
    case SM501_DC_CRT_CONTROL_8BPP:
	src_bpp = 1;
	draw_line = draw_line8_funcs[ds_depth_index];
	break;
    case SM501_DC_CRT_CONTROL_16BPP:
	src_bpp = 2;
	draw_line = draw_line16_funcs[ds_depth_index];
	break;
    case SM501_DC_CRT_CONTROL_32BPP:
	src_bpp = 4;
	draw_line = draw_line32_funcs[ds_depth_index];
	break;
    default:
	printf("sm501 draw crt : invalid DC_CRT_CONTROL=%x.\n",
	       s->dc_crt_control);
	assert(0);
	break;
    }

    /* adjust console size */
    if (s->last_width != width || s->last_height != height) {
	qemu_console_resize(s->console, width, height);
	s->last_width = width;
	s->last_height = height;
	full_update = 1;
    }

    /* draw each line according to conditions */
    for (y = 0; y < height; y++) {
	int update = full_update;
	uint8_t * line_end = &src[width * src_bpp - 1];
	int page0 = (src - phys_ram_base) & TARGET_PAGE_MASK;
	int page1 = (line_end - phys_ram_base) & TARGET_PAGE_MASK;
	int page;

	/* check dirty flags for each line */
	for (page = page0; page <= page1; page += TARGET_PAGE_SIZE)
	    if (cpu_physical_memory_get_dirty(page, VGA_DIRTY_FLAG))
		update = 1;

	/* draw line and change status */
	if (update) {
	    draw_line(&s->ds->data[y * width * dst_bpp], src, width, palette);
	    if (y_start < 0)
		y_start = y;
	    if (page0 < page_min)
		page_min = page0;
	    if (page1 > page_max)
		page_max = page1;
	} else {
	    if (y_start >= 0) {
		/* flush to display */
		dpy_update(s->ds, 0, y_start, width, y - y_start);
		y_start = -1;
	    }
	}

	src += width * src_bpp;
    }

    /* complete flush to display */
    if (y_start >= 0)
	dpy_update(s->ds, 0, y_start, width, y - y_start);

    /* clear dirty flags */
    if (page_max != -1)
	cpu_physical_memory_reset_dirty(page_min, page_max + TARGET_PAGE_SIZE,
					VGA_DIRTY_FLAG);
}

static void sm501_update_display(void *opaque)
{
    SM501State * s = (SM501State *)opaque;

    if (s->dc_crt_control & SM501_DC_CRT_CONTROL_ENABLE)
	sm501_draw_crt(s);
}

void sm501_init(DisplayState *ds, uint32_t base, unsigned long local_mem_base,
		uint32_t local_mem_bytes, CharDriverState *chr)
{
    SM501State * s;
    int sm501_system_config_index;
    int sm501_disp_ctrl_index;
    int sm501_palette_index;

    /* allocate management data region */
    s = (SM501State *)qemu_mallocz(sizeof(SM501State));
    s->base = base;
    s->local_mem_size_index
	= get_local_mem_size_index(local_mem_bytes);
    SM501_DPRINTF("local mem size=%x. index=%d\n", get_local_mem_size(s),
		  s->local_mem_size_index);
    s->system_control = 0x00100000;
    s->misc_control = 0x00001000; /* assumes SH, active=low */
    s->dc_panel_control = 0x00010000;
    s->dc_crt_control = 0x00010000;
    s->ds = ds;

    /* allocate local memory */
    s->local_mem = (uint8 *)phys_ram_base + local_mem_base;
    cpu_register_physical_memory(base, local_mem_bytes, local_mem_base);

    /* map mmio */
    sm501_system_config_index
	= cpu_register_io_memory(0, sm501_system_config_readfn,
				 sm501_system_config_writefn, s);
    cpu_register_physical_memory(base + MMIO_BASE_OFFSET,
				 0x6c, sm501_system_config_index);
    sm501_disp_ctrl_index = cpu_register_io_memory(0, sm501_disp_ctrl_readfn,
						   sm501_disp_ctrl_writefn, s);
    cpu_register_physical_memory(base + MMIO_BASE_OFFSET + SM501_DC,
				 0x400, sm501_disp_ctrl_index);

    sm501_palette_index = cpu_register_io_memory(0, sm501_palette_readfn,
						   sm501_palette_writefn, s);
    cpu_register_physical_memory(base + MMIO_BASE_OFFSET
				 + SM501_DC + SM501_DC_PANEL_PALETTE,
				 0x400 * 3, sm501_palette_index);

    /* bridge to serial emulation module */
    if (chr)
	serial_mm_init(base + MMIO_BASE_OFFSET + SM501_UART0, 2,
		       0, /* TODO : chain irq to IRL */
		       115200, chr, 1);

    /* create qemu graphic console */
    s->console = graphic_console_init(s->ds, sm501_update_display, NULL,
				      NULL, NULL, s);
}
