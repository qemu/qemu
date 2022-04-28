#ifndef KEYASIC_SD_H
#define KEYASIC_SD_H

#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/sd/sd.h"

#define CARD_BLOCK_SIZE_512     512
#define CARD_BLOCK_SIZE_1024    1024
#define CARD_BLOCK_SIZE_2048    2048

#define CARD_BUFFER_COUNT       2

#define SD_RESPONSE_COUNT       4

struct KeyasicSdState {
    /*< private >*/
    SysBusDevice parent;

    /* Address space for internal DMA that can be changed during board init */
    AddressSpace *addr_space;

    /*< public >*/
    MemoryRegion iomem;
    SDBus sdbus;

    // SD Card Block Set Register
    uint32_t scbsr;
    // SD Card Control Register
    uint32_t sccr;
    // SD Card Argument Register
    uint32_t scargr;
    // SD Card Address Register
    uint32_t csaddr;
    // SD Card Status Register
    uint32_t scsr;
    // SD Card Error Enable Register
    uint32_t sceer;
    // SD Card Response 1-4 Register
    uint32_t scrr[SD_RESPONSE_COUNT];
    // DMA Channel 0/1 Control Register
    uint32_t dccr[CARD_BUFFER_COUNT];
    // DMA Channel 0/1 Source Start Address Register
    uint32_t dcssar[CARD_BUFFER_COUNT];
    // DMA Channel 0/1 Destination Start Address Register
    uint32_t dcdsar[CARD_BUFFER_COUNT];
    // DMA Channel 0/1 Transfer Total Register
    uint32_t dcdtr[CARD_BUFFER_COUNT];
    // SD Card Buffer Transfer Response Register
    uint32_t scbtrr;
    // SD Card Buffer Transfer Control Register
    uint32_t scbtcr;

    /* SPI-SDIO interface */
    // SDIO enable
    uint32_t sdio_en;
    // SDIO clock divider
    uint32_t sdio_clk_div;
    // SDIO interrupt status
    uint32_t sdio_int_status;
    // SDIO interrupt mask
    uint32_t sdio_int_mask;
    // SDIO clock polarity
    uint32_t sdio_clk_polarity;

    uint8_t internal_buffer[CARD_BUFFER_COUNT][CARD_BLOCK_SIZE_2048];

    // CMD18 or CMD25 transfer internal data
    uint32_t multi_transfer_count;
    uint8_t multi_cmd_in_progress;

    qemu_irq irq;

    /* GPIO outputs for 'card inserted' */
    qemu_irq card_inserted;
};

typedef struct KeyasicSdState KeyasicSdState;

#define TYPE_KEYASIC_SD "keyasic_sd"
#define KEYASIC_SD(obj) OBJECT_CHECK(KeyasicSdState, (obj), TYPE_KEYASIC_SD)

#define TYPE_KEYASIC_SD_BUS "keyasic_sd-bus"

void keyasic_sd_change_address_space(KeyasicSdState *s, AddressSpace *addr_space, Error **errp);

#endif /* KEYASIC_SD_H */
