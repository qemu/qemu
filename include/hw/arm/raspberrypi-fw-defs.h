/*
 * Raspberry Pi firmware definitions
 *
 * Copyright (C) 2022  Auriga LLC, based on Linux kernel
 *   `include/soc/bcm2835/raspberrypi-firmware.h` (Copyright © 2015 Broadcom)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef INCLUDE_HW_MISC_RASPBERRYPI_FW_DEFS_H_
#define INCLUDE_HW_MISC_RASPBERRYPI_FW_DEFS_H_


enum rpi_firmware_property_tag {
    RPI_FWREQ_PROPERTY_END =                           0,
    RPI_FWREQ_GET_FIRMWARE_REVISION =                  0x00000001,
    RPI_FWREQ_GET_FIRMWARE_VARIANT =                   0x00000002,
    RPI_FWREQ_GET_FIRMWARE_HASH =                      0x00000003,

    RPI_FWREQ_SET_CURSOR_INFO =                        0x00008010,
    RPI_FWREQ_SET_CURSOR_STATE =                       0x00008011,

    RPI_FWREQ_GET_BOARD_MODEL =                        0x00010001,
    RPI_FWREQ_GET_BOARD_REVISION =                     0x00010002,
    RPI_FWREQ_GET_BOARD_MAC_ADDRESS =                  0x00010003,
    RPI_FWREQ_GET_BOARD_SERIAL =                       0x00010004,
    RPI_FWREQ_GET_ARM_MEMORY =                         0x00010005,
    RPI_FWREQ_GET_VC_MEMORY =                          0x00010006,
    RPI_FWREQ_GET_CLOCKS =                             0x00010007,
    RPI_FWREQ_GET_POWER_STATE =                        0x00020001,
    RPI_FWREQ_GET_TIMING =                             0x00020002,
    RPI_FWREQ_SET_POWER_STATE =                        0x00028001,
    RPI_FWREQ_GET_CLOCK_STATE =                        0x00030001,
    RPI_FWREQ_GET_CLOCK_RATE =                         0x00030002,
    RPI_FWREQ_GET_VOLTAGE =                            0x00030003,
    RPI_FWREQ_GET_MAX_CLOCK_RATE =                     0x00030004,
    RPI_FWREQ_GET_MAX_VOLTAGE =                        0x00030005,
    RPI_FWREQ_GET_TEMPERATURE =                        0x00030006,
    RPI_FWREQ_GET_MIN_CLOCK_RATE =                     0x00030007,
    RPI_FWREQ_GET_MIN_VOLTAGE =                        0x00030008,
    RPI_FWREQ_GET_TURBO =                              0x00030009,
    RPI_FWREQ_GET_MAX_TEMPERATURE =                    0x0003000a,
    RPI_FWREQ_GET_STC =                                0x0003000b,
    RPI_FWREQ_ALLOCATE_MEMORY =                        0x0003000c,
    RPI_FWREQ_LOCK_MEMORY =                            0x0003000d,
    RPI_FWREQ_UNLOCK_MEMORY =                          0x0003000e,
    RPI_FWREQ_RELEASE_MEMORY =                         0x0003000f,
    RPI_FWREQ_EXECUTE_CODE =                           0x00030010,
    RPI_FWREQ_EXECUTE_QPU =                            0x00030011,
    RPI_FWREQ_SET_ENABLE_QPU =                         0x00030012,
    RPI_FWREQ_GET_DISPMANX_RESOURCE_MEM_HANDLE =       0x00030014,
    RPI_FWREQ_GET_EDID_BLOCK =                         0x00030020,
    RPI_FWREQ_GET_CUSTOMER_OTP =                       0x00030021,
    RPI_FWREQ_GET_EDID_BLOCK_DISPLAY =                 0x00030023,
    RPI_FWREQ_GET_DOMAIN_STATE =                       0x00030030,
    RPI_FWREQ_GET_THROTTLED =                          0x00030046,
    RPI_FWREQ_GET_CLOCK_MEASURED =                     0x00030047,
    RPI_FWREQ_NOTIFY_REBOOT =                          0x00030048,
    RPI_FWREQ_GET_PRIVATE_KEY =                        0x00030081,
    RPI_FWREQ_SET_CLOCK_STATE =                        0x00038001,
    RPI_FWREQ_SET_CLOCK_RATE =                         0x00038002,
    RPI_FWREQ_SET_VOLTAGE =                            0x00038003,
    RPI_FWREQ_SET_MAX_CLOCK_RATE =                     0x00038004,
    RPI_FWREQ_SET_MIN_CLOCK_RATE =                     0x00038007,
    RPI_FWREQ_SET_TURBO =                              0x00038009,
    RPI_FWREQ_SET_CUSTOMER_OTP =                       0x00038021,
    RPI_FWREQ_SET_DOMAIN_STATE =                       0x00038030,
    RPI_FWREQ_GET_GPIO_STATE =                         0x00030041,
    RPI_FWREQ_SET_GPIO_STATE =                         0x00038041,
    RPI_FWREQ_SET_SDHOST_CLOCK =                       0x00038042,
    RPI_FWREQ_GET_GPIO_CONFIG =                        0x00030043,
    RPI_FWREQ_SET_GPIO_CONFIG =                        0x00038043,
    RPI_FWREQ_GET_PERIPH_REG =                         0x00030045,
    RPI_FWREQ_SET_PERIPH_REG =                         0x00038045,
    RPI_FWREQ_GET_POE_HAT_VAL =                        0x00030049,
    RPI_FWREQ_SET_POE_HAT_VAL =                        0x00038049,
    RPI_FWREQ_SET_PRIVATE_KEY =                        0x00038081,
    RPI_FWREQ_SET_POE_HAT_VAL_OLD =                    0x00030050,
    RPI_FWREQ_NOTIFY_XHCI_RESET =                      0x00030058,
    RPI_FWREQ_GET_REBOOT_FLAGS =                       0x00030064,
    RPI_FWREQ_SET_REBOOT_FLAGS =                       0x00038064,
    RPI_FWREQ_NOTIFY_DISPLAY_DONE =                    0x00030066,

    /* Dispmanx TAGS */
    RPI_FWREQ_FRAMEBUFFER_ALLOCATE =                   0x00040001,
    RPI_FWREQ_FRAMEBUFFER_BLANK =                      0x00040002,
    RPI_FWREQ_FRAMEBUFFER_GET_PHYSICAL_WIDTH_HEIGHT =  0x00040003,
    RPI_FWREQ_FRAMEBUFFER_GET_VIRTUAL_WIDTH_HEIGHT =   0x00040004,
    RPI_FWREQ_FRAMEBUFFER_GET_DEPTH =                  0x00040005,
    RPI_FWREQ_FRAMEBUFFER_GET_PIXEL_ORDER =            0x00040006,
    RPI_FWREQ_FRAMEBUFFER_GET_ALPHA_MODE =             0x00040007,
    RPI_FWREQ_FRAMEBUFFER_GET_PITCH =                  0x00040008,
    RPI_FWREQ_FRAMEBUFFER_GET_VIRTUAL_OFFSET =         0x00040009,
    RPI_FWREQ_FRAMEBUFFER_GET_OVERSCAN =               0x0004000a,
    RPI_FWREQ_FRAMEBUFFER_GET_PALETTE =                0x0004000b,
    RPI_FWREQ_FRAMEBUFFER_GET_LAYER =                  0x0004000c,
    RPI_FWREQ_FRAMEBUFFER_GET_TRANSFORM =              0x0004000d,
    RPI_FWREQ_FRAMEBUFFER_GET_VSYNC =                  0x0004000e,
    RPI_FWREQ_FRAMEBUFFER_GET_TOUCHBUF =               0x0004000f,
    RPI_FWREQ_FRAMEBUFFER_GET_GPIOVIRTBUF =            0x00040010,
    RPI_FWREQ_FRAMEBUFFER_RELEASE =                    0x00048001,
    RPI_FWREQ_FRAMEBUFFER_GET_DISPLAY_ID =             0x00040016,
    RPI_FWREQ_FRAMEBUFFER_SET_DISPLAY_NUM =            0x00048013,
    RPI_FWREQ_FRAMEBUFFER_GET_NUM_DISPLAYS =           0x00040013,
    RPI_FWREQ_FRAMEBUFFER_GET_DISPLAY_SETTINGS =       0x00040014,
    RPI_FWREQ_FRAMEBUFFER_TEST_PHYSICAL_WIDTH_HEIGHT = 0x00044003,
    RPI_FWREQ_FRAMEBUFFER_TEST_VIRTUAL_WIDTH_HEIGHT =  0x00044004,
    RPI_FWREQ_FRAMEBUFFER_TEST_DEPTH =                 0x00044005,
    RPI_FWREQ_FRAMEBUFFER_TEST_PIXEL_ORDER =           0x00044006,
    RPI_FWREQ_FRAMEBUFFER_TEST_ALPHA_MODE =            0x00044007,
    RPI_FWREQ_FRAMEBUFFER_TEST_VIRTUAL_OFFSET =        0x00044009,
    RPI_FWREQ_FRAMEBUFFER_TEST_OVERSCAN =              0x0004400a,
    RPI_FWREQ_FRAMEBUFFER_TEST_PALETTE =               0x0004400b,
    RPI_FWREQ_FRAMEBUFFER_TEST_LAYER =                 0x0004400c,
    RPI_FWREQ_FRAMEBUFFER_TEST_TRANSFORM =             0x0004400d,
    RPI_FWREQ_FRAMEBUFFER_TEST_VSYNC =                 0x0004400e,
    RPI_FWREQ_FRAMEBUFFER_SET_PHYSICAL_WIDTH_HEIGHT =  0x00048003,
    RPI_FWREQ_FRAMEBUFFER_SET_VIRTUAL_WIDTH_HEIGHT =   0x00048004,
    RPI_FWREQ_FRAMEBUFFER_SET_DEPTH =                  0x00048005,
    RPI_FWREQ_FRAMEBUFFER_SET_PIXEL_ORDER =            0x00048006,
    RPI_FWREQ_FRAMEBUFFER_SET_ALPHA_MODE =             0x00048007,
    RPI_FWREQ_FRAMEBUFFER_SET_PITCH =                  0x00048008,
    RPI_FWREQ_FRAMEBUFFER_SET_VIRTUAL_OFFSET =         0x00048009,
    RPI_FWREQ_FRAMEBUFFER_SET_OVERSCAN =               0x0004800a,
    RPI_FWREQ_FRAMEBUFFER_SET_PALETTE =                0x0004800b,

    RPI_FWREQ_FRAMEBUFFER_SET_TOUCHBUF =               0x0004801f,
    RPI_FWREQ_FRAMEBUFFER_SET_GPIOVIRTBUF =            0x00048020,
    RPI_FWREQ_FRAMEBUFFER_SET_VSYNC =                  0x0004800e,
    RPI_FWREQ_FRAMEBUFFER_SET_LAYER =                  0x0004800c,
    RPI_FWREQ_FRAMEBUFFER_SET_TRANSFORM =              0x0004800d,
    RPI_FWREQ_FRAMEBUFFER_SET_BACKLIGHT =              0x0004800f,

    RPI_FWREQ_VCHIQ_INIT =                             0x00048010,

    RPI_FWREQ_SET_PLANE =                              0x00048015,
    RPI_FWREQ_GET_DISPLAY_TIMING =                     0x00040017,
    RPI_FWREQ_SET_TIMING =                             0x00048017,
    RPI_FWREQ_GET_DISPLAY_CFG =                        0x00040018,
    RPI_FWREQ_SET_DISPLAY_POWER =                      0x00048019,
    RPI_FWREQ_GET_COMMAND_LINE =                       0x00050001,
    RPI_FWREQ_GET_DMA_CHANNELS =                       0x00060001,
};

enum rpi_firmware_clk_id {
    RPI_FIRMWARE_EMMC_CLK_ID = 1,
    RPI_FIRMWARE_UART_CLK_ID,
    RPI_FIRMWARE_ARM_CLK_ID,
    RPI_FIRMWARE_CORE_CLK_ID,
    RPI_FIRMWARE_V3D_CLK_ID,
    RPI_FIRMWARE_H264_CLK_ID,
    RPI_FIRMWARE_ISP_CLK_ID,
    RPI_FIRMWARE_SDRAM_CLK_ID,
    RPI_FIRMWARE_PIXEL_CLK_ID,
    RPI_FIRMWARE_PWM_CLK_ID,
    RPI_FIRMWARE_HEVC_CLK_ID,
    RPI_FIRMWARE_EMMC2_CLK_ID,
    RPI_FIRMWARE_M2MC_CLK_ID,
    RPI_FIRMWARE_PIXEL_BVB_CLK_ID,
    RPI_FIRMWARE_VEC_CLK_ID,
    RPI_FIRMWARE_NUM_CLK_ID,
};

struct rpi_firmware_property_tag_header {
    uint32_t tag;
    uint32_t buf_size;
    uint32_t req_resp_size;
};

typedef struct rpi_firmware_prop_request {
    struct rpi_firmware_property_tag_header hdr;
    uint8_t payload[0];
} rpi_firmware_prop_request_t;

#endif /* INCLUDE_HW_MISC_RASPBERRYPI_FW_DEFS_H_ */
