#ifndef IPOD_TOUCH_FMSS_H
#define IPOD_TOUCH_FMSS_H

#include <math.h>
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/irq.h"

#define TYPE_IPOD_TOUCH_FMSS                "ipodtouch.fmss"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchFMSSState, IPOD_TOUCH_FMSS)

#define NAND_BYTES_PER_PAGE 4096
#define NAND_BYTES_PER_SPARE 64

#define FMSS__FMCTRL1             0x4
#define FMSS__CS_IRQ              0xC0C
#define FMSS__CS_IRQMASK          0xC10
#define FMSS__CS_BUF_RST_OK       0xC64
#define FMSS_CINFO_TARGET_ADDR    0xD08
#define FMSS_PAGES_IN_ADDR        0xD0C
#define FMSS_CS_BUF_ADDR          0xD10
#define FMSS_NUM_PAGES            0xD18
#define FMSS_PAGE_SPARE_OUT_ADDR  0xD1C
#define FMSS_PAGES_OUT_ADDR       0xD20
#define FMSS_CSGENRC              0xD30

typedef struct IPodTouchFMSSState
{
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;

    uint8_t *page_buffer;
    uint8_t *page_spare_buffer;

    uint32_t reg_cs_irq_bit;
    uint32_t reg_cinfo_target_addr;
    uint32_t reg_pages_in_addr;
    uint32_t reg_cs_buf_addr;
    uint32_t reg_num_pages;
    uint32_t reg_page_spare_out_addr;
    uint32_t reg_pages_out_addr;
    uint32_t reg_csgenrc;
} IPodTouchFMSSState;

#endif