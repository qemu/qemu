#ifndef HW_I8257_H
#define HW_I8257_H

#include "hw/isa/isa.h"
#include "exec/ioport.h"
#include "qom/object.h"

#define TYPE_I8257 "i8257"
OBJECT_DECLARE_SIMPLE_TYPE(I8257State, I8257)

typedef struct I8257Regs {
    int now[2];
    uint16_t base[2];
    uint8_t mode;
    uint8_t page;
    uint8_t pageh;
    uint8_t dack;
    uint8_t eop;
    IsaDmaTransferHandler transfer_handler;
    void *opaque;
} I8257Regs;

struct I8257State {
    /* <private> */
    ISADevice parent_obj;

    /* <public> */
    int32_t base;
    int32_t page_base;
    int32_t pageh_base;
    int32_t dshift;

    uint8_t status;
    uint8_t command;
    uint8_t mask;
    uint8_t flip_flop;
    I8257Regs regs[4];
    MemoryRegion channel_io;
    MemoryRegion cont_io;

    QEMUBH *dma_bh;
    bool dma_bh_scheduled;
    int running;
    PortioList portio_page;
    PortioList portio_pageh;
};

void i8257_dma_init(Object *parent, ISABus *bus, bool high_page_enable);

#endif
