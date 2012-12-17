/*
 * Intel XScale PXA255/270 processor support.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GNU GPL v2.
 */
#ifndef PXA_H
# define PXA_H			"pxa.h"

#include "exec/memory.h"

/* Interrupt numbers */
# define PXA2XX_PIC_SSP3	0
# define PXA2XX_PIC_USBH2	2
# define PXA2XX_PIC_USBH1	3
# define PXA2XX_PIC_KEYPAD	4
# define PXA2XX_PIC_PWRI2C	6
# define PXA25X_PIC_HWUART	7
# define PXA27X_PIC_OST_4_11	7
# define PXA2XX_PIC_GPIO_0	8
# define PXA2XX_PIC_GPIO_1	9
# define PXA2XX_PIC_GPIO_X	10
# define PXA2XX_PIC_I2S 	13
# define PXA26X_PIC_ASSP	15
# define PXA25X_PIC_NSSP	16
# define PXA27X_PIC_SSP2	16
# define PXA2XX_PIC_LCD		17
# define PXA2XX_PIC_I2C		18
# define PXA2XX_PIC_ICP		19
# define PXA2XX_PIC_STUART	20
# define PXA2XX_PIC_BTUART	21
# define PXA2XX_PIC_FFUART	22
# define PXA2XX_PIC_MMC		23
# define PXA2XX_PIC_SSP		24
# define PXA2XX_PIC_DMA		25
# define PXA2XX_PIC_OST_0	26
# define PXA2XX_PIC_RTC1HZ	30
# define PXA2XX_PIC_RTCALARM	31

/* DMA requests */
# define PXA2XX_RX_RQ_I2S	2
# define PXA2XX_TX_RQ_I2S	3
# define PXA2XX_RX_RQ_BTUART	4
# define PXA2XX_TX_RQ_BTUART	5
# define PXA2XX_RX_RQ_FFUART	6
# define PXA2XX_TX_RQ_FFUART	7
# define PXA2XX_RX_RQ_SSP1	13
# define PXA2XX_TX_RQ_SSP1	14
# define PXA2XX_RX_RQ_SSP2	15
# define PXA2XX_TX_RQ_SSP2	16
# define PXA2XX_RX_RQ_ICP	17
# define PXA2XX_TX_RQ_ICP	18
# define PXA2XX_RX_RQ_STUART	19
# define PXA2XX_TX_RQ_STUART	20
# define PXA2XX_RX_RQ_MMCI	21
# define PXA2XX_TX_RQ_MMCI	22
# define PXA2XX_USB_RQ(x)	((x) + 24)
# define PXA2XX_RX_RQ_SSP3	66
# define PXA2XX_TX_RQ_SSP3	67

# define PXA2XX_SDRAM_BASE	0xa0000000
# define PXA2XX_INTERNAL_BASE	0x5c000000
# define PXA2XX_INTERNAL_SIZE	0x40000

/* pxa2xx_pic.c */
DeviceState *pxa2xx_pic_init(hwaddr base, ARMCPU *cpu);

/* pxa2xx_gpio.c */
DeviceState *pxa2xx_gpio_init(hwaddr base,
                              ARMCPU *cpu, DeviceState *pic, int lines);
void pxa2xx_gpio_read_notifier(DeviceState *dev, qemu_irq handler);

/* pxa2xx_dma.c */
DeviceState *pxa255_dma_init(hwaddr base, qemu_irq irq);
DeviceState *pxa27x_dma_init(hwaddr base, qemu_irq irq);

/* pxa2xx_lcd.c */
typedef struct PXA2xxLCDState PXA2xxLCDState;
PXA2xxLCDState *pxa2xx_lcdc_init(MemoryRegion *sysmem,
                hwaddr base, qemu_irq irq);
void pxa2xx_lcd_vsync_notifier(PXA2xxLCDState *s, qemu_irq handler);
void pxa2xx_lcdc_oritentation(void *opaque, int angle);

/* pxa2xx_mmci.c */
typedef struct PXA2xxMMCIState PXA2xxMMCIState;
PXA2xxMMCIState *pxa2xx_mmci_init(MemoryRegion *sysmem,
                hwaddr base,
                BlockDriverState *bd, qemu_irq irq,
                qemu_irq rx_dma, qemu_irq tx_dma);
void pxa2xx_mmci_handlers(PXA2xxMMCIState *s, qemu_irq readonly,
                qemu_irq coverswitch);

/* pxa2xx_pcmcia.c */
typedef struct PXA2xxPCMCIAState PXA2xxPCMCIAState;
PXA2xxPCMCIAState *pxa2xx_pcmcia_init(MemoryRegion *sysmem,
                                      hwaddr base);
int pxa2xx_pcmcia_attach(void *opaque, PCMCIACardState *card);
int pxa2xx_pcmcia_dettach(void *opaque);
void pxa2xx_pcmcia_set_irq_cb(void *opaque, qemu_irq irq, qemu_irq cd_irq);

/* pxa2xx_keypad.c */
struct  keymap {
    int column;
    int row;
};
typedef struct PXA2xxKeyPadState PXA2xxKeyPadState;
PXA2xxKeyPadState *pxa27x_keypad_init(MemoryRegion *sysmem,
                                      hwaddr base,
                                      qemu_irq irq);
void pxa27x_register_keypad(PXA2xxKeyPadState *kp, struct keymap *map,
                int size);

/* pxa2xx.c */
typedef struct PXA2xxI2CState PXA2xxI2CState;
PXA2xxI2CState *pxa2xx_i2c_init(hwaddr base,
                qemu_irq irq, uint32_t page_size);
i2c_bus *pxa2xx_i2c_bus(PXA2xxI2CState *s);

typedef struct PXA2xxI2SState PXA2xxI2SState;
typedef struct PXA2xxFIrState PXA2xxFIrState;

typedef struct {
    ARMCPU *cpu;
    DeviceState *pic;
    qemu_irq reset;
    MemoryRegion sdram;
    MemoryRegion internal;
    MemoryRegion cm_iomem;
    MemoryRegion mm_iomem;
    MemoryRegion pm_iomem;
    DeviceState *dma;
    DeviceState *gpio;
    PXA2xxLCDState *lcd;
    SSIBus **ssp;
    PXA2xxI2CState *i2c[2];
    PXA2xxMMCIState *mmc;
    PXA2xxPCMCIAState *pcmcia[2];
    PXA2xxI2SState *i2s;
    PXA2xxFIrState *fir;
    PXA2xxKeyPadState *kp;

    /* Power management */
    hwaddr pm_base;
    uint32_t pm_regs[0x40];

    /* Clock management */
    hwaddr cm_base;
    uint32_t cm_regs[4];
    uint32_t clkcfg;

    /* Memory management */
    hwaddr mm_base;
    uint32_t mm_regs[0x1a];

    /* Performance monitoring */
    uint32_t pmnc;
} PXA2xxState;

struct PXA2xxI2SState {
    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq rx_dma;
    qemu_irq tx_dma;
    void (*data_req)(void *, int, int);

    uint32_t control[2];
    uint32_t status;
    uint32_t mask;
    uint32_t clk;

    int enable;
    int rx_len;
    int tx_len;
    void (*codec_out)(void *, uint32_t);
    uint32_t (*codec_in)(void *);
    void *opaque;

    int fifo_len;
    uint32_t fifo[16];
};

# define PA_FMT			"0x%08lx"
# define REG_FMT		"0x" TARGET_FMT_plx

PXA2xxState *pxa270_init(MemoryRegion *address_space, unsigned int sdram_size,
                         const char *revision);
PXA2xxState *pxa255_init(MemoryRegion *address_space, unsigned int sdram_size);

#endif	/* PXA_H */
