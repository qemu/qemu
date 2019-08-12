#ifndef HW_PCNET_H
#define HW_PCNET_H

#define PCNET_IOPORT_SIZE       0x20
#define PCNET_PNPMMIO_SIZE      0x20

#define PCNET_LOOPTEST_CRC	1
#define PCNET_LOOPTEST_NOCRC	2

#include "exec/memory.h"
#include "hw/irq.h"

/* BUS CONFIGURATION REGISTERS */
#define BCR_MSRDA    0
#define BCR_MSWRA    1
#define BCR_MC       2
#define BCR_LNKST    4
#define BCR_LED1     5
#define BCR_LED2     6
#define BCR_LED3     7
#define BCR_FDC      9
#define BCR_BSBC     18
#define BCR_EECAS    19
#define BCR_SWS      20
#define BCR_PLAT     22

#define BCR_TMAULOOP(S)  !!((S)->bcr[BCR_MC  ] & 0x4000)
#define BCR_APROMWE(S)   !!((S)->bcr[BCR_MC  ] & 0x0100)
#define BCR_DWIO(S)      !!((S)->bcr[BCR_BSBC] & 0x0080)
#define BCR_SSIZE32(S)   !!((S)->bcr[BCR_SWS ] & 0x0100)
#define BCR_SWSTYLE(S)     ((S)->bcr[BCR_SWS ] & 0x00FF)

typedef struct PCNetState_st PCNetState;

struct PCNetState_st {
    NICState *nic;
    NICConf conf;
    QEMUTimer *poll_timer;
    int rap, isr, lnkst;
    uint32_t rdra, tdra;
    uint8_t prom[16];
    uint16_t csr[128];
    uint16_t bcr[32];
    int xmit_pos;
    uint64_t timer;
    MemoryRegion mmio;
    uint8_t buffer[4096];
    qemu_irq irq;
    void (*phys_mem_read)(void *dma_opaque, hwaddr addr,
                         uint8_t *buf, int len, int do_bswap);
    void (*phys_mem_write)(void *dma_opaque, hwaddr addr,
                          uint8_t *buf, int len, int do_bswap);
    void *dma_opaque;
    int tx_busy;
    int looptest;
};

void pcnet_h_reset(void *opaque);
void pcnet_ioport_writew(void *opaque, uint32_t addr, uint32_t val);
uint32_t pcnet_ioport_readw(void *opaque, uint32_t addr);
void pcnet_ioport_writel(void *opaque, uint32_t addr, uint32_t val);
uint32_t pcnet_ioport_readl(void *opaque, uint32_t addr);
uint32_t pcnet_bcr_readw(PCNetState *s, uint32_t rap);
ssize_t pcnet_receive(NetClientState *nc, const uint8_t *buf, size_t size_);
void pcnet_set_link_status(NetClientState *nc);
void pcnet_common_init(DeviceState *dev, PCNetState *s, NetClientInfo *info);
extern const VMStateDescription vmstate_pcnet;
#endif
