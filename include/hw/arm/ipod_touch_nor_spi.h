#ifndef IPOD_TOUCH_NOR_SPI_H
#define IPOD_TOUCH_NOR_SPI_H

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/ssi/ssi.h"
#include "hw/hw.h"

#define TYPE_IPOD_TOUCH_NOR_SPI                "ipodtouch.norspi"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchNORSPIState, IPOD_TOUCH_NOR_SPI)

#define NOR_WRITE_TO_STATUS_REG 0x1
#define NOR_WRITE_DATA_CMD 0x2
#define NOR_READ_DATA_CMD  0x3
#define NOR_DISABLE_WRITE  0x4
#define NOR_GET_STATUS_CMD 0x5
#define NOR_ENABLE_WRITE   0x6
#define NOR_ERASE_BLOCK    0x20
#define NOR_GET_JEDECID    0x9F

typedef struct IPodTouchNORSPIState {
    SSIPeripheral ssidev;
    uint32_t cur_cmd;
    uint8_t *in_buf;
    uint8_t *out_buf;
    uint32_t in_buf_size;
    uint32_t out_buf_size;
    uint32_t in_buf_cur_ind;
    uint32_t out_buf_cur_ind;
    uint8_t *nor_data;
    uint8_t write_enabled;
    uint32_t nor_read_ind;
    char nor_path[1024];
    bool nor_initialized;
} IPodTouchNORSPIState;

#endif