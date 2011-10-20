#ifndef _STRONGARM_H
#define _STRONGARM_H

#include "memory.h"

#define SA_CS0          0x00000000
#define SA_CS1          0x08000000
#define SA_CS2          0x10000000
#define SA_CS3          0x18000000
#define SA_PCMCIA_CS0   0x20000000
#define SA_PCMCIA_CS1   0x30000000
#define SA_CS4          0x40000000
#define SA_CS5          0x48000000
/* system registers here */
#define SA_SDCS0        0xc0000000
#define SA_SDCS1        0xc8000000
#define SA_SDCS2        0xd0000000
#define SA_SDCS3        0xd8000000

enum {
    SA_PIC_GPIO0_EDGE = 0,
    SA_PIC_GPIO1_EDGE,
    SA_PIC_GPIO2_EDGE,
    SA_PIC_GPIO3_EDGE,
    SA_PIC_GPIO4_EDGE,
    SA_PIC_GPIO5_EDGE,
    SA_PIC_GPIO6_EDGE,
    SA_PIC_GPIO7_EDGE,
    SA_PIC_GPIO8_EDGE,
    SA_PIC_GPIO9_EDGE,
    SA_PIC_GPIO10_EDGE,
    SA_PIC_GPIOX_EDGE,
    SA_PIC_LCD,
    SA_PIC_UDC,
    SA_PIC_RSVD1,
    SA_PIC_UART1,
    SA_PIC_UART2,
    SA_PIC_UART3,
    SA_PIC_MCP,
    SA_PIC_SSP,
    SA_PIC_DMA_CH0,
    SA_PIC_DMA_CH1,
    SA_PIC_DMA_CH2,
    SA_PIC_DMA_CH3,
    SA_PIC_DMA_CH4,
    SA_PIC_DMA_CH5,
    SA_PIC_OSTC0,
    SA_PIC_OSTC1,
    SA_PIC_OSTC2,
    SA_PIC_OSTC3,
    SA_PIC_RTC_HZ,
    SA_PIC_RTC_ALARM,
};

typedef struct {
    CPUState *env;
    MemoryRegion sdram;
    DeviceState *pic;
    DeviceState *gpio;
    DeviceState *ppc;
    DeviceState *ssp;
    SSIBus *ssp_bus;
} StrongARMState;

StrongARMState *sa1110_init(MemoryRegion *sysmem,
                            unsigned int sdram_size, const char *rev);

#endif
