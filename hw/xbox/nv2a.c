/*
 * QEMU Geforce NV2A implementation
 *
 * Copyright (c) 2012 espes
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "ui/console.h"
#include "hw/pci/pci.h"
#include "hw/display/vga.h"
#include "hw/display/vga_int.h"
#include "qemu/queue.h"
#include "qemu/thread.h"
#include "qapi/qmp/qstring.h"
#include "gl/gloffscreen.h"

#include "hw/xbox/u_format_r11g11b10f.h"
#include "hw/xbox/nv2a_vsh.h"

#include "hw/xbox/nv2a.h"

#define DEBUG_NV2A
#ifdef DEBUG_NV2A
# define NV2A_DPRINTF(format, ...)       printf(format, ## __VA_ARGS__)
#else
# define NV2A_DPRINTF(format, ...)       do { } while (0)
#endif


#define NV_NUM_BLOCKS 21
#define NV_PMC          0   /* card master control */
#define NV_PBUS         1   /* bus control */
#define NV_PFIFO        2   /* MMIO and DMA FIFO submission to PGRAPH and VPE */
#define NV_PFIFO_CACHE  3
#define NV_PRMA         4   /* access to BAR0/BAR1 from real mode */
#define NV_PVIDEO       5   /* video overlay */
#define NV_PTIMER       6   /* time measurement and time-based alarms */
#define NV_PCOUNTER     7   /* performance monitoring counters */
#define NV_PVPE         8   /* MPEG2 decoding engine */
#define NV_PTV          9   /* TV encoder */
#define NV_PRMFB        10  /* aliases VGA memory window */
#define NV_PRMVIO       11  /* aliases VGA sequencer and graphics controller registers */
#define NV_PFB          12  /* memory interface */
#define NV_PSTRAPS      13  /* straps readout / override */
#define NV_PGRAPH       14  /* accelerated 2d/3d drawing engine */
#define NV_PCRTC        15  /* more CRTC controls */
#define NV_PRMCIO       16  /* aliases VGA CRTC and attribute controller registers */
#define NV_PRAMDAC      17  /* RAMDAC, cursor, and PLL control */
#define NV_PRMDIO       18  /* aliases VGA palette registers */
#define NV_PRAMIN       19  /* RAMIN access */
#define NV_USER         20  /* PFIFO MMIO and DMA submission area */



#define NV_PMC_BOOT_0                                    0x00000000
#define NV_PMC_INTR_0                                    0x00000100
#   define NV_PMC_INTR_0_PFIFO                                 (1 << 8)
#   define NV_PMC_INTR_0_PGRAPH                               (1 << 12)
#   define NV_PMC_INTR_0_PCRTC                                (1 << 24)
#   define NV_PMC_INTR_0_PBUS                                 (1 << 28)
#   define NV_PMC_INTR_0_SOFTWARE                             (1 << 31)
#define NV_PMC_INTR_EN_0                                 0x00000140
#   define NV_PMC_INTR_EN_0_HARDWARE                            1
#   define NV_PMC_INTR_EN_0_SOFTWARE                            2
#define NV_PMC_ENABLE                                    0x00000200
#   define NV_PMC_ENABLE_PFIFO                                 (1 << 8)
#   define NV_PMC_ENABLE_PGRAPH                               (1 << 12)


/* These map approximately to the pci registers */
#define NV_PBUS_PCI_NV_0                                 0x00000800
#   define NV_PBUS_PCI_NV_0_VENDOR_ID                         0x0000FFFF
#   define NV_CONFIG_PCI_NV_0_DEVICE_ID                       0xFFFF0000
#define NV_PBUS_PCI_NV_1                                 0x00000804
#define NV_PBUS_PCI_NV_2                                 0x00000808
#   define NV_PBUS_PCI_NV_2_REVISION_ID                       0x000000FF
#   define NV_PBUS_PCI_NV_2_CLASS_CODE                        0xFFFFFF00


#define NV_PFIFO_INTR_0                                  0x00000100
#   define NV_PFIFO_INTR_0_CACHE_ERROR                          (1 << 0)
#   define NV_PFIFO_INTR_0_RUNOUT                               (1 << 4)
#   define NV_PFIFO_INTR_0_RUNOUT_OVERFLOW                      (1 << 8)
#   define NV_PFIFO_INTR_0_DMA_PUSHER                          (1 << 12)
#   define NV_PFIFO_INTR_0_DMA_PT                              (1 << 16)
#   define NV_PFIFO_INTR_0_SEMAPHORE                           (1 << 20)
#   define NV_PFIFO_INTR_0_ACQUIRE_TIMEOUT                     (1 << 24)
#define NV_PFIFO_INTR_EN_0                               0x00000140
#   define NV_PFIFO_INTR_EN_0_CACHE_ERROR                       (1 << 0)
#   define NV_PFIFO_INTR_EN_0_RUNOUT                            (1 << 4)
#   define NV_PFIFO_INTR_EN_0_RUNOUT_OVERFLOW                   (1 << 8)
#   define NV_PFIFO_INTR_EN_0_DMA_PUSHER                       (1 << 12)
#   define NV_PFIFO_INTR_EN_0_DMA_PT                           (1 << 16)
#   define NV_PFIFO_INTR_EN_0_SEMAPHORE                        (1 << 20)
#   define NV_PFIFO_INTR_EN_0_ACQUIRE_TIMEOUT                  (1 << 24)
#define NV_PFIFO_RAMHT                                   0x00000210
#   define NV_PFIFO_RAMHT_BASE_ADDRESS                        0x000001F0
#   define NV_PFIFO_RAMHT_SIZE                                0x00030000
#       define NV_PFIFO_RAMHT_SIZE_4K                             0
#       define NV_PFIFO_RAMHT_SIZE_8K                             1
#       define NV_PFIFO_RAMHT_SIZE_16K                            2
#       define NV_PFIFO_RAMHT_SIZE_32K                            3
#   define NV_PFIFO_RAMHT_SEARCH                              0x03000000
#       define NV_PFIFO_RAMHT_SEARCH_16                           0
#       define NV_PFIFO_RAMHT_SEARCH_32                           1
#       define NV_PFIFO_RAMHT_SEARCH_64                           2
#       define NV_PFIFO_RAMHT_SEARCH_128                          3
#define NV_PFIFO_RAMFC                                   0x00000214
#   define NV_PFIFO_RAMFC_BASE_ADDRESS1                       0x000001FC
#   define NV_PFIFO_RAMFC_SIZE                                0x00010000
#   define NV_PFIFO_RAMFC_BASE_ADDRESS2                       0x00FE0000
#define NV_PFIFO_RAMRO                                   0x00000218
#   define NV_PFIFO_RAMRO_BASE_ADDRESS                        0x000001FE
#   define NV_PFIFO_RAMRO_SIZE                                0x00010000
#define NV_PFIFO_RUNOUT_STATUS                           0x00000400
#   define NV_PFIFO_RUNOUT_STATUS_RANOUT                       (1 << 0)
#   define NV_PFIFO_RUNOUT_STATUS_LOW_MARK                     (1 << 4)
#   define NV_PFIFO_RUNOUT_STATUS_HIGH_MARK                    (1 << 8)
#define NV_PFIFO_MODE                                    0x00000504
#define NV_PFIFO_DMA                                     0x00000508
#define NV_PFIFO_CACHE1_PUSH0                            0x00001200
#   define NV_PFIFO_CACHE1_PUSH0_ACCESS                         (1 << 0)
#define NV_PFIFO_CACHE1_PUSH1                            0x00001204
#   define NV_PFIFO_CACHE1_PUSH1_CHID                         0x0000001F
#   define NV_PFIFO_CACHE1_PUSH1_MODE                         0x00000100
#define NV_PFIFO_CACHE1_STATUS                           0x00001214
#   define NV_PFIFO_CACHE1_STATUS_LOW_MARK                      (1 << 4)
#   define NV_PFIFO_CACHE1_STATUS_HIGH_MARK                     (1 << 8)
#define NV_PFIFO_CACHE1_DMA_PUSH                         0x00001220
#   define NV_PFIFO_CACHE1_DMA_PUSH_ACCESS                      (1 << 0)
#   define NV_PFIFO_CACHE1_DMA_PUSH_STATE                       (1 << 4)
#   define NV_PFIFO_CACHE1_DMA_PUSH_BUFFER                      (1 << 8)
#   define NV_PFIFO_CACHE1_DMA_PUSH_STATUS                     (1 << 12)
#   define NV_PFIFO_CACHE1_DMA_PUSH_ACQUIRE                    (1 << 16)
#define NV_PFIFO_CACHE1_DMA_FETCH                        0x00001224
#   define NV_PFIFO_CACHE1_DMA_FETCH_TRIG                     0x000000F8
#   define NV_PFIFO_CACHE1_DMA_FETCH_SIZE                     0x0000E000
#   define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS                 0x001F0000
#define NV_PFIFO_CACHE1_DMA_STATE                        0x00001228
#   define NV_PFIFO_CACHE1_DMA_STATE_METHOD_TYPE                (1 << 0)
#   define NV_PFIFO_CACHE1_DMA_STATE_METHOD                   0x00001FFC
#   define NV_PFIFO_CACHE1_DMA_STATE_SUBCHANNEL               0x0000E000
#   define NV_PFIFO_CACHE1_DMA_STATE_METHOD_COUNT             0x1FFC0000
#   define NV_PFIFO_CACHE1_DMA_STATE_ERROR                    0xE0000000
#       define NV_PFIFO_CACHE1_DMA_STATE_ERROR_NONE               0
#       define NV_PFIFO_CACHE1_DMA_STATE_ERROR_CALL               1
#       define NV_PFIFO_CACHE1_DMA_STATE_ERROR_NON_CACHE          2
#       define NV_PFIFO_CACHE1_DMA_STATE_ERROR_RETURN             3
#       define NV_PFIFO_CACHE1_DMA_STATE_ERROR_RESERVED_CMD       4
#       define NV_PFIFO_CACHE1_DMA_STATE_ERROR_PROTECTION         6
#define NV_PFIFO_CACHE1_DMA_INSTANCE                     0x0000122C
#   define NV_PFIFO_CACHE1_DMA_INSTANCE_ADDRESS               0x0000FFFF
#define NV_PFIFO_CACHE1_DMA_PUT                          0x00001240
#define NV_PFIFO_CACHE1_DMA_GET                          0x00001244
#define NV_PFIFO_CACHE1_DMA_SUBROUTINE                   0x0000124C
#   define NV_PFIFO_CACHE1_DMA_SUBROUTINE_RETURN_OFFSET       0x1FFFFFFC
#   define NV_PFIFO_CACHE1_DMA_SUBROUTINE_STATE                (1 << 0)
#define NV_PFIFO_CACHE1_PULL0                            0x00001250
#   define NV_PFIFO_CACHE1_PULL0_ACCESS                        (1 << 0)
#define NV_PFIFO_CACHE1_ENGINE                           0x00001280
#define NV_PFIFO_CACHE1_DMA_DCOUNT                       0x000012A0
#   define NV_PFIFO_CACHE1_DMA_DCOUNT_VALUE                   0x00001FFC
#define NV_PFIFO_CACHE1_DMA_GET_JMP_SHADOW               0x000012A4
#   define NV_PFIFO_CACHE1_DMA_GET_JMP_SHADOW_OFFSET          0x1FFFFFFC
#define NV_PFIFO_CACHE1_DMA_RSVD_SHADOW                  0x000012A8
#define NV_PFIFO_CACHE1_DMA_DATA_SHADOW                  0x000012AC


#define NV_PGRAPH_INTR                                   0x00000100
#   define NV_PGRAPH_INTR_NOTIFY                              (1 << 0)
#   define NV_PGRAPH_INTR_MISSING_HW                          (1 << 4)
#   define NV_PGRAPH_INTR_TLB_PRESENT_DMA_R                   (1 << 6)
#   define NV_PGRAPH_INTR_TLB_PRESENT_DMA_W                   (1 << 7)
#   define NV_PGRAPH_INTR_TLB_PRESENT_TEX_A                   (1 << 8)
#   define NV_PGRAPH_INTR_TLB_PRESENT_TEX_B                   (1 << 9)
#   define NV_PGRAPH_INTR_TLB_PRESENT_VTX                    (1 << 10)
#   define NV_PGRAPH_INTR_CONTEXT_SWITCH                     (1 << 12)
#   define NV_PGRAPH_INTR_STATE3D                            (1 << 13)
#   define NV_PGRAPH_INTR_BUFFER_NOTIFY                      (1 << 16)
#   define NV_PGRAPH_INTR_ERROR                              (1 << 20)
#   define NV_PGRAPH_INTR_SINGLE_STEP                        (1 << 24)
#define NV_PGRAPH_NSOURCE                                0x00000108
#   define NV_PGRAPH_NSOURCE_NOTIFICATION                     (1 << 0) 
#define NV_PGRAPH_INTR_EN                                0x00000140
#   define NV_PGRAPH_INTR_EN_NOTIFY                           (1 << 0)
#   define NV_PGRAPH_INTR_EN_MISSING_HW                       (1 << 4)
#   define NV_PGRAPH_INTR_EN_TLB_PRESENT_DMA_R                (1 << 6)
#   define NV_PGRAPH_INTR_EN_TLB_PRESENT_DMA_W                (1 << 7)
#   define NV_PGRAPH_INTR_EN_TLB_PRESENT_TEX_A                (1 << 8)
#   define NV_PGRAPH_INTR_EN_TLB_PRESENT_TEX_B                (1 << 9)
#   define NV_PGRAPH_INTR_EN_TLB_PRESENT_VTX                 (1 << 10)
#   define NV_PGRAPH_INTR_EN_CONTEXT_SWITCH                  (1 << 12)
#   define NV_PGRAPH_INTR_EN_STATE3D                         (1 << 13)
#   define NV_PGRAPH_INTR_EN_BUFFER_NOTIFY                   (1 << 16)
#   define NV_PGRAPH_INTR_EN_ERROR                           (1 << 20)
#   define NV_PGRAPH_INTR_EN_SINGLE_STEP                     (1 << 24)
#define NV_PGRAPH_CTX_CONTROL                            0x00000144
#   define NV_PGRAPH_CTX_CONTROL_MINIMUM_TIME                 0x00000003
#   define NV_PGRAPH_CTX_CONTROL_TIME                           (1 << 8)
#   define NV_PGRAPH_CTX_CONTROL_CHID                          (1 << 16)
#   define NV_PGRAPH_CTX_CONTROL_CHANGE                        (1 << 20)
#   define NV_PGRAPH_CTX_CONTROL_SWITCHING                     (1 << 24)
#   define NV_PGRAPH_CTX_CONTROL_DEVICE                        (1 << 28)
#define NV_PGRAPH_CTX_USER                               0x00000148
#   define NV_PGRAPH_CTX_USER_CHANNEL_3D                        (1 << 0)
#   define NV_PGRAPH_CTX_USER_CHANNEL_3D_VALID                  (1 << 4)
#   define NV_PGRAPH_CTX_USER_SUBCH                           0x0000E000
#   define NV_PGRAPH_CTX_USER_CHID                            0x1F000000
#   define NV_PGRAPH_CTX_USER_SINGLE_STEP                      (1 << 31)
#define NV_PGRAPH_CTX_SWITCH1                            0x0000014C
#   define NV_PGRAPH_CTX_SWITCH1_GRCLASS                      0x000000FF
#   define NV_PGRAPH_CTX_SWITCH1_CHROMA_KEY                    (1 << 12)
#   define NV_PGRAPH_CTX_SWITCH1_SWIZZLE                       (1 << 14)
#   define NV_PGRAPH_CTX_SWITCH1_PATCH_CONFIG                 0x00038000
#   define NV_PGRAPH_CTX_SWITCH1_SYNCHRONIZE                   (1 << 18)
#   define NV_PGRAPH_CTX_SWITCH1_ENDIAN_MODE                   (1 << 19)
#   define NV_PGRAPH_CTX_SWITCH1_CLASS_TYPE                    (1 << 22)
#   define NV_PGRAPH_CTX_SWITCH1_SINGLE_STEP                   (1 << 23)
#   define NV_PGRAPH_CTX_SWITCH1_PATCH_STATUS                  (1 << 24)
#   define NV_PGRAPH_CTX_SWITCH1_CONTEXT_SURFACE0              (1 << 25)
#   define NV_PGRAPH_CTX_SWITCH1_CONTEXT_SURFACE1              (1 << 26)
#   define NV_PGRAPH_CTX_SWITCH1_CONTEXT_PATTERN               (1 << 27)
#   define NV_PGRAPH_CTX_SWITCH1_CONTEXT_ROP                   (1 << 28)
#   define NV_PGRAPH_CTX_SWITCH1_CONTEXT_BETA1                 (1 << 29)
#   define NV_PGRAPH_CTX_SWITCH1_CONTEXT_BETA4                 (1 << 30)
#   define NV_PGRAPH_CTX_SWITCH1_VOLATILE_RESET                (1 << 31)
#define NV_PGRAPH_TRAPPED_ADDR                           0x00000704
#   define NV_PGRAPH_TRAPPED_ADDR_MTHD                        0x00001FFF
#   define NV_PGRAPH_TRAPPED_ADDR_SUBCH                       0x00070000
#   define NV_PGRAPH_TRAPPED_ADDR_CHID                        0x01F00000
#   define NV_PGRAPH_TRAPPED_ADDR_DHV                         0x10000000
#define NV_PGRAPH_TRAPPED_DATA_LOW                       0x00000708
#define NV_PGRAPH_FIFO                                   0x00000720
#   define NV_PGRAPH_FIFO_ACCESS                                (1 << 0)
#define NV_PGRAPH_CHANNEL_CTX_TABLE                      0x00000780
#   define NV_PGRAPH_CHANNEL_CTX_TABLE_INST                   0x0000FFFF
#define NV_PGRAPH_CHANNEL_CTX_POINTER                    0x00000784
#   define NV_PGRAPH_CHANNEL_CTX_POINTER_INST                 0x0000FFFF
#define NV_PGRAPH_CHANNEL_CTX_TRIGGER                    0x00000788
#   define NV_PGRAPH_CHANNEL_CTX_TRIGGER_READ_IN                (1 << 0)
#   define NV_PGRAPH_CHANNEL_CTX_TRIGGER_WRITE_OUT              (1 << 1)
#define NV_PGRAPH_COLORCLEARVALUE                        0x0000186C
#define NV_PGRAPH_ZSTENCILCLEARVALUE                     0x00001A88

#define NV_PCRTC_INTR_0                                  0x00000100
#   define NV_PCRTC_INTR_0_VBLANK                               (1 << 0)
#define NV_PCRTC_INTR_EN_0                               0x00000140
#   define NV_PCRTC_INTR_EN_0_VBLANK                            (1 << 0)
#define NV_PCRTC_START                                   0x00000800
#define NV_PCRTC_CONFIG                                  0x00000804


#define NV_PTIMER_INTR_0                                 0x00000100
#   define NV_PTIMER_INTR_0_ALARM                               (1 << 0)
#define NV_PTIMER_INTR_EN_0                              0x00000140
#   define NV_PTIMER_INTR_EN_0_ALARM                            (1 << 0)
#define NV_PTIMER_NUMERATOR                              0x00000200
#define NV_PTIMER_DENOMINATOR                            0x00000210
#define NV_PTIMER_TIME_0                                 0x00000400
#define NV_PTIMER_TIME_1                                 0x00000410
#define NV_PTIMER_ALARM_0                                0x00000420


#define NV_PFB_CFG0                                      0x00000200
#   define NV_PFB_CFG0_PART                                   0x00000003
#define NV_PFB_CSTATUS                                   0x0000020C


#define NV_PRAMDAC_NVPLL_COEFF                           0x00000500
#   define NV_PRAMDAC_NVPLL_COEFF_MDIV                        0x000000FF
#   define NV_PRAMDAC_NVPLL_COEFF_NDIV                        0x0000FF00
#   define NV_PRAMDAC_NVPLL_COEFF_PDIV                        0x00070000
#define NV_PRAMDAC_MPLL_COEFF                            0x00000504
#   define NV_PRAMDAC_MPLL_COEFF_MDIV                         0x000000FF
#   define NV_PRAMDAC_MPLL_COEFF_NDIV                         0x0000FF00
#   define NV_PRAMDAC_MPLL_COEFF_PDIV                         0x00070000
#define NV_PRAMDAC_VPLL_COEFF                            0x00000508
#   define NV_PRAMDAC_VPLL_COEFF_MDIV                         0x000000FF
#   define NV_PRAMDAC_VPLL_COEFF_NDIV                         0x0000FF00
#   define NV_PRAMDAC_VPLL_COEFF_PDIV                         0x00070000
#define NV_PRAMDAC_PLL_TEST_COUNTER                      0x00000514
#   define NV_PRAMDAC_PLL_TEST_COUNTER_NOOFIPCLKS             0x000003FF
#   define NV_PRAMDAC_PLL_TEST_COUNTER_VALUE                  0x0000FFFF
#   define NV_PRAMDAC_PLL_TEST_COUNTER_ENABLE                  (1 << 16)
#   define NV_PRAMDAC_PLL_TEST_COUNTER_RESET                   (1 << 20)
#   define NV_PRAMDAC_PLL_TEST_COUNTER_SOURCE                 0x03000000
#   define NV_PRAMDAC_PLL_TEST_COUNTER_VPLL2_LOCK              (1 << 27)
#   define NV_PRAMDAC_PLL_TEST_COUNTER_PDIV_RST                (1 << 28)
#   define NV_PRAMDAC_PLL_TEST_COUNTER_NVPLL_LOCK              (1 << 29)
#   define NV_PRAMDAC_PLL_TEST_COUNTER_MPLL_LOCK               (1 << 30)
#   define NV_PRAMDAC_PLL_TEST_COUNTER_VPLL_LOCK               (1 << 31)


#define NV_USER_DMA_PUT                                  0x40
#define NV_USER_DMA_GET                                  0x44
#define NV_USER_REF                                      0x48



/* DMA objects */
#define NV_DMA_FROM_MEMORY_CLASS                         0x02
#define NV_DMA_TO_MEMORY_CLASS                           0x03
#define NV_DMA_IN_MEMORY_CLASS                           0x3d

#define NV_DMA_CLASS                                          0x00000FFF
#define NV_DMA_PAGE_TABLE                                      (1 << 12)
#define NV_DMA_PAGE_ENTRY                                      (1 << 13)
#define NV_DMA_FLAGS_ACCESS                                    (1 << 14)
#define NV_DMA_FLAGS_MAPPING_COHERENCY                         (1 << 15)
#define NV_DMA_TARGET                                         0x00030000
#   define NV_DMA_TARGET_NVM                                      0x00000000
#   define NV_DMA_TARGET_NVM_TILED                                0x00010000
#   define NV_DMA_TARGET_PCI                                      0x00020000
#   define NV_DMA_TARGET_AGP                                      0x00030000
#define NV_DMA_ADJUST                                         0xFFF00000

#define NV_DMA_ADDRESS                                        0xFFFFF000


#define NV_RAMHT_HANDLE                                       0xFFFFFFFF
#define NV_RAMHT_INSTANCE                                     0x0000FFFF
#define NV_RAMHT_ENGINE                                       0x00030000
#   define NV_RAMHT_ENGINE_SW                                     0x00000000
#   define NV_RAMHT_ENGINE_GRAPHICS                               0x00010000
#   define NV_RAMHT_ENGINE_DVD                                    0x00020000
#define NV_RAMHT_CHID                                         0x1F000000
#define NV_RAMHT_STATUS                                       0x80000000



/* graphic classes and methods */
#define NV_SET_OBJECT                                        0x00000000


#define NV_CONTEXT_SURFACES_2D                           0x0062
#   define NV062_SET_CONTEXT_DMA_IMAGE_SOURCE                 0x00620184
#   define NV062_SET_CONTEXT_DMA_IMAGE_DESTIN                 0x00620188
#   define NV062_SET_COLOR_FORMAT                             0x00620300
#       define NV062_SET_COLOR_FORMAT_LE_Y8                    0x01
#       define NV062_SET_COLOR_FORMAT_LE_A8R8G8B8              0x0A
#   define NV062_SET_PITCH                                    0x00620304
#   define NV062_SET_OFFSET_SOURCE                            0x00620308
#   define NV062_SET_OFFSET_DESTIN                            0x0062030C

#define NV_IMAGE_BLIT                                    0x009F
#   define NV09F_SET_CONTEXT_SURFACES                         0x009F019C
#   define NV09F_SET_OPERATION                                0x009F02FC
#       define NV09F_SET_OPERATION_SRCCOPY                        3
#   define NV09F_CONTROL_POINT_IN                             0x009F0300
#   define NV09F_CONTROL_POINT_OUT                            0x009F0304
#   define NV09F_SIZE                                         0x009F0308


#define NV_KELVIN_PRIMITIVE                              0x0097
#   define NV097_NO_OPERATION                                 0x00970100
#   define NV097_WAIT_FOR_IDLE                                0x00970110
#   define NV097_FLIP_STALL                                   0x00970130
#   define NV097_SET_CONTEXT_DMA_NOTIFIES                     0x00970180
#   define NV097_SET_CONTEXT_DMA_A                            0x00970184
#   define NV097_SET_CONTEXT_DMA_B                            0x00970188
#   define NV097_SET_CONTEXT_DMA_STATE                        0x00970190
#   define NV097_SET_CONTEXT_DMA_COLOR                        0x00970194
#   define NV097_SET_CONTEXT_DMA_ZETA                         0x00970198
#   define NV097_SET_CONTEXT_DMA_VERTEX_A                     0x0097019C
#   define NV097_SET_CONTEXT_DMA_VERTEX_B                     0x009701A0
#   define NV097_SET_CONTEXT_DMA_SEMAPHORE                    0x009701A4
#   define NV097_SET_SURFACE_CLIP_HORIZONTAL                  0x00970200
#       define NV097_SET_SURFACE_CLIP_HORIZONTAL_X                0x0000FFFF
#       define NV097_SET_SURFACE_CLIP_HORIZONTAL_WIDTH            0xFFFF0000
#   define NV097_SET_SURFACE_CLIP_VERTICAL                    0x00970204
#       define NV097_SET_SURFACE_CLIP_VERTICAL_Y                  0x0000FFFF
#       define NV097_SET_SURFACE_CLIP_VERTICAL_HEIGHT             0xFFFF0000
#   define NV097_SET_SURFACE_FORMAT                           0x00970208
#       define NV097_SET_SURFACE_FORMAT_COLOR                     0x0000000F
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_Z1R5G5B5     0x01
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_O1R5G5B5     0x02
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5                0x03
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8     0x04
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_O8R8G8B8     0x05
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_X1A7R8G8B8_Z1A7R8G8B8 0x06
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_X1A7R8G8B8_O1A7R8G8B8 0x07
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8              0x08
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_B8                    0x09
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_G8B8                  0x0A
#       define NV097_SET_SURFACE_FORMAT_ZETA                      0x000000F0
#   define NV097_SET_SURFACE_PITCH                            0x0097020C
#       define NV097_SET_SURFACE_PITCH_COLOR                      0x0000FFFF
#       define NV097_SET_SURFACE_PITCH_ZETA                       0xFFFF0000
#   define NV097_SET_SURFACE_COLOR_OFFSET                     0x00970210
#   define NV097_SET_SURFACE_ZETA_OFFSET                      0x00970214
#   define NV097_SET_COLOR_MASK                               0x00970358
#   define NV097_SET_VIEWPORT_OFFSET                          0x00970A20
#   define NV097_SET_VIEWPORT_SCALE                           0x00970AF0
#   define NV097_SET_TRANSFORM_PROGRAM                        0x00970B00
#   define NV097_SET_TRANSFORM_CONSTANT                       0x00970B80
#   define NV097_SET_VERTEX_DATA_ARRAY_OFFSET                 0x00971720
#   define NV097_SET_VERTEX_DATA_ARRAY_FORMAT                 0x00971760
#       define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE            0x0000000F
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D     0
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1         1
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F          2
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL     3
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K       5
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP        6
#       define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE            0x000000F0
#       define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE          0xFFFFFF00
#   define NV097_SET_BEGIN_END                                0x009717fC
#       define NV097_SET_BEGIN_END_OP_END                         0x00
#       define NV097_SET_BEGIN_END_OP_POINTS                      0x01
#       define NV097_SET_BEGIN_END_OP_LINES                       0x02
#       define NV097_SET_BEGIN_END_OP_LINE_LOOP                   0x03
#       define NV097_SET_BEGIN_END_OP_LINE_STRIP                  0x04
#       define NV097_SET_BEGIN_END_OP_TRIANGLES                   0x05
#       define NV097_SET_BEGIN_END_OP_TRIANGLE_STRIP              0x06
#       define NV097_SET_BEGIN_END_OP_TRIANGLE_FAN                0x07
#       define NV097_SET_BEGIN_END_OP_QUADS                       0x08
#       define NV097_SET_BEGIN_END_OP_QUAD_STRIP                  0x09
#       define NV097_SET_BEGIN_END_OP_POLYGON                     0x0A
#   define NV097_ARRAY_ELEMENT16                              0x00971800
#   define NV097_ARRAY_ELEMENT32                              0x00971808
#   define NV097_DRAW_ARRAYS                                  0x00971810
#       define NV097_DRAW_ARRAYS_COUNT                            0xFF000000
#       define NV097_DRAW_ARRAYS_START_INDEX                      0x00FFFFFF
#   define NV097_INLINE_ARRAY                                 0x00971818
#   define NV097_SET_TEXTURE_OFFSET                           0x00971B00
#   define NV097_SET_TEXTURE_FORMAT                           0x00971B04
#       define NV097_SET_TEXTURE_FORMAT_CONTEXT_DMA               0x00000003
#       define NV097_SET_TEXTURE_FORMAT_DIMENSIONALITY            0x000000F0
#       define NV097_SET_TEXTURE_FORMAT_COLOR                     0x0000FF00
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8       0x06
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8       0x07
#           define NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5   0x0C
#           define NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8  0x0E
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8 0x12
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8             0x19
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8 0x1E
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FIXED 0x30
#       define NV097_SET_TEXTURE_FORMAT_BASE_SIZE_U               0x00F00000
#       define NV097_SET_TEXTURE_FORMAT_BASE_SIZE_V               0x0F000000
#       define NV097_SET_TEXTURE_FORMAT_BASE_SIZE_P               0xF0000000
#   define NV097_SET_TEXTURE_ADDRESS                          0x00971B08
#   define NV097_SET_TEXTURE_CONTROL0                         0x00971B0C
#       define NV097_SET_TEXTURE_CONTROL0_ENABLE                 (1 << 30)
#       define NV097_SET_TEXTURE_CONTROL0_MIN_LOD_CLAMP           0x3FFC0000
#       define NV097_SET_TEXTURE_CONTROL0_MAX_LOD_CLAMP           0x0003FFC0
#   define NV097_SET_TEXTURE_CONTROL1                         0x00971B10
#       define NV097_SET_TEXTURE_CONTROL1_IMAGE_PITCH             0xFFFF0000
#   define NV097_SET_TEXTURE_FILTER                           0x00971B14
#       define NV097_SET_TEXTURE_FILTER_MIN                       0x00FF0000
#       define NV097_SET_TEXTURE_FILTER_MAG                       0x0F000000
#   define NV097_SET_TEXTURE_IMAGE_RECT                       0x00971B1C
#       define NV097_SET_TEXTURE_IMAGE_RECT_WIDTH                 0xFFFF0000
#       define NV097_SET_TEXTURE_IMAGE_RECT_HEIGHT                0x0000FFFF
#   define NV097_SET_SEMAPHORE_OFFSET                         0x00971D6C
#   define NV097_BACK_END_WRITE_SEMAPHORE_RELEASE             0x00971D70
#   define NV097_SET_ZSTENCIL_CLEAR_VALUE                     0x00971D8C
#   define NV097_SET_COLOR_CLEAR_VALUE                        0x00971D90
#   define NV097_CLEAR_SURFACE                                0x00971D94
#       define NV097_CLEAR_SURFACE_Z                              (1 << 0)
#       define NV097_CLEAR_SURFACE_STENCIL                        (1 << 1)
#       define NV097_CLEAR_SURFACE_R                              (1 << 4)
#       define NV097_CLEAR_SURFACE_G                              (1 << 5)
#       define NV097_CLEAR_SURFACE_B                              (1 << 6)
#       define NV097_CLEAR_SURFACE_A                              (1 << 7)
#   define NV097_SET_TRANSFORM_EXECUTION_MODE                 0x00971E94
#   define NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN           0x00971E98
#   define NV097_SET_TRANSFORM_PROGRAM_LOAD                   0x00971E9C
#   define NV097_SET_TRANSFORM_PROGRAM_START                  0x00971EA0
#   define NV097_SET_TRANSFORM_CONSTANT_LOAD                  0x00971EA4


static const GLenum kelvin_primitive_map[] = {
    0,
    GL_POINTS,
    GL_LINES,
    GL_LINE_LOOP,
    GL_LINE_STRIP,
    GL_TRIANGLES,
    GL_TRIANGLE_STRIP,
    GL_TRIANGLE_FAN,
    GL_QUADS,
    GL_QUAD_STRIP,
    GL_POLYGON,
};

static const GLenum kelvin_texture_min_filter_map[] = {
    0,
    GL_NEAREST,
    GL_LINEAR,
    GL_NEAREST_MIPMAP_NEAREST,
    GL_LINEAR_MIPMAP_NEAREST,
    GL_NEAREST_MIPMAP_LINEAR,
    GL_LINEAR_MIPMAP_LINEAR,
    GL_LINEAR, /* TODO: Convolution filter... */
};

static const GLenum kelvin_texture_mag_filter_map[] = {
    0,
    GL_NEAREST,
    GL_LINEAR,
    0,
    GL_LINEAR /* TODO: Convolution filter... */
};

typedef struct ColorFormatInfo {
    unsigned int bytes_per_pixel;
    bool swizzled;
    bool linear;
    GLint gl_internal_format;
    GLenum gl_format;
    GLenum gl_type;
} ColorFormatInfo;

static const ColorFormatInfo kelvin_color_format_map[66] = {
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8] =
        {4, true,  false, GL_RGBA, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8] =
        {4, true,  false, GL_RGB,  GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV},

    [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5] =
        {4, true,  false, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 0, GL_RGBA},
    [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8] =
        {4, true,  false, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 0, GL_RGBA},

    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8] =
        {4, false, true,  GL_RGBA, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV},
    /* TODO: how do opengl alpha textures work? */
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8] =
        {2, true,  false, GL_RED,  GL_RED,  GL_UNSIGNED_BYTE},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8] =
        {4, false, true,  GL_RGB,  GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FIXED] =
        {2, false, true,  GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT, GL_SHORT},
};



#define NV2A_CRYSTAL_FREQ 13500000
#define NV2A_NUM_CHANNELS 32
#define NV2A_NUM_SUBCHANNELS 8

#define NV2A_MAX_BATCH_LENGTH 0xFFFF
#define NV2A_VERTEXSHADER_SLOTS  32 /*???*/
#define NV2A_MAX_VERTEXSHADER_LENGTH 136
#define NV2A_VERTEXSHADER_CONSTANTS 192
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
#define NV2A_MAX_TEXTURES 4

#define GET_MASK(v, mask) (((v) & (mask)) >> (ffs(mask)-1))

#define SET_MASK(v, mask, val)                                       \
    do {                                                             \
        (v) &= ~(mask);                                              \
        (v) |= ((val) << (ffs(mask)-1)) & (mask);                    \
    } while (0)

#define CASE_4(v, step)                                              \
    case (v):                                                        \
    case (v)+(step):                                                 \
    case (v)+(step)*2:                                               \
    case (v)+(step)*3


enum FifoMode {
    FIFO_PIO = 0,
    FIFO_DMA = 1,
};

enum FIFOEngine {
    ENGINE_SOFTWARE = 0,
    ENGINE_GRAPHICS = 1,
    ENGINE_DVD = 2,
};



typedef struct RAMHTEntry {
    uint32_t handle;
    hwaddr instance;
    enum FIFOEngine engine;
    unsigned int channel_id : 5;
    bool valid;
} RAMHTEntry;

typedef struct DMAObject {
    unsigned int dma_class;
    unsigned int dma_target;
    hwaddr address;
    hwaddr limit;
} DMAObject;

typedef struct VertexAttribute {
    bool dma_select;
    hwaddr offset;

    /* inline arrays are packed in order?
     * Need to pass the offset to converted attributes */
    unsigned int inline_offset;

    unsigned int format;
    unsigned int size; /* size of the data type */
    unsigned int count; /* number of components */
    uint32_t stride;

    bool needs_conversion;
    uint8_t *converted_buffer;
    unsigned int converted_elements;
    unsigned int converted_size;
    unsigned int converted_count;

    GLenum gl_type;
    GLboolean gl_normalize;
} VertexAttribute;

typedef struct VertexShaderConstant {
    bool dirty;
    uint32 data[4];
} VertexShaderConstant;

typedef struct VertexShader {
    bool dirty;
    unsigned int program_length;
    uint32_t program_data[NV2A_MAX_VERTEXSHADER_LENGTH];

    GLuint gl_program;
} VertexShader;

typedef struct Texture {
    bool dirty;
    bool enabled;

    unsigned int dimensionality;
    unsigned int color_format;
    unsigned int log_width, log_height;

    unsigned int rect_width, rect_height;

    unsigned int min_mipmap_level, max_mipmap_level;
    unsigned int pitch;

    unsigned int min_filter, mag_filter;

    bool dma_select;
    hwaddr offset;

    GLuint gl_texture;
    /* once bound as GL_TEXTURE_RECTANGLE_ARB, it seems textures
     * can't be rebound as GL_TEXTURE_*D... */
    GLuint gl_texture_rect;
} Texture;

typedef struct Surface {
    unsigned int pitch;
    unsigned int x, y;
    unsigned int width, height;
    unsigned int format;

    hwaddr offset;
} Surface;

typedef struct KelvinState {
    hwaddr dma_notifies;
    hwaddr dma_a, dma_b;
    hwaddr dma_state;
    hwaddr dma_color;
    hwaddr dma_zeta;
    hwaddr dma_vertex_a, dma_vertex_b;
    hwaddr dma_semaphore;
    unsigned int semaphore_offset;

    bool surface_dirty;
    Surface surface_color;
    Surface surface_zeta;
    uint32_t color_mask;

    unsigned int vertexshader_start_slot;
    unsigned int vertexshader_load_slot;
    VertexShader vertexshaders[NV2A_VERTEXSHADER_SLOTS];

    unsigned int constant_load_slot;
    VertexShaderConstant constants[NV2A_VERTEXSHADER_CONSTANTS];

    bool fragment_program_dirty;
    GLuint gl_fragment_program;

    GLenum gl_primitive_mode;

    VertexAttribute vertex_attributes[NV2A_VERTEXSHADER_ATTRIBUTES];

    unsigned int inline_vertex_data_length;
    uint32_t inline_vertex_data[NV2A_MAX_BATCH_LENGTH];

    unsigned int array_batch_length;
    uint32_t array_batch[NV2A_MAX_BATCH_LENGTH];

    Texture textures[NV2A_MAX_TEXTURES];

    bool use_vertex_program;
    bool enable_vertex_program_write;
} KelvinState;

typedef struct ContextSurfaces2DState {
    hwaddr dma_image_source;
    hwaddr dma_image_dest;
    unsigned int color_format;
    unsigned int source_pitch, dest_pitch;
    hwaddr source_offset, dest_offset;

} ContextSurfaces2DState;

typedef struct ImageBlitState {
    hwaddr context_surfaces;
    unsigned int operation;
    unsigned int in_x, in_y;
    unsigned int out_x, out_y;
    unsigned int width, height;

} ImageBlitState;

typedef struct GraphicsObject {
    uint8_t graphics_class;
    union {
        ContextSurfaces2DState context_surfaces_2d;
        
        ImageBlitState image_blit;

        KelvinState kelvin;
    } data;
} GraphicsObject;

typedef struct GraphicsSubchannel {
    hwaddr object_instance;
    GraphicsObject object;
    uint32_t object_cache[5];
} GraphicsSubchannel;

typedef struct GraphicsContext {
    bool channel_3d;
    unsigned int subchannel;

    GraphicsSubchannel subchannel_data[NV2A_NUM_SUBCHANNELS];

    uint32_t zstencil_clear_value;
    uint32_t color_clear_value;

    GloContext *gl_context;

    GLuint gl_framebuffer;
    GLuint gl_renderbuffer;
} GraphicsContext;




typedef struct CacheEntry {
    QSIMPLEQ_ENTRY(CacheEntry) entry;

    unsigned int method : 14;
    unsigned int subchannel : 3;
    bool nonincreasing;
    uint32_t parameter;
} CacheEntry;

typedef struct Cache1State {
    unsigned int channel_id;
    enum FifoMode mode;

    /* Pusher state */
    bool push_enabled;
    bool dma_push_enabled;
    bool dma_push_suspended;
    hwaddr dma_instance;

    bool method_nonincreasing;
    unsigned int method : 14;
    unsigned int subchannel : 3;
    unsigned int method_count : 24;
    uint32_t dcount;
    bool subroutine_active;
    hwaddr subroutine_return;
    hwaddr get_jmp_shadow;
    uint32_t rsvd_shadow;
    uint32_t data_shadow;
    uint32_t error;


    /* Puller state */
    QemuMutex pull_lock;

    bool pull_enabled;
    enum FIFOEngine bound_engines[NV2A_NUM_SUBCHANNELS];
    enum FIFOEngine last_engine;

    /* The actual command queue */
    QemuMutex cache_lock;
    QemuCond cache_cond;
    QSIMPLEQ_HEAD(, CacheEntry) cache;
} Cache1State;

typedef struct ChannelControl {
    hwaddr dma_put;
    hwaddr dma_get;
    uint32_t ref;
} ChannelControl;



typedef struct NV2AState {
    PCIDevice dev;
    qemu_irq irq;

    VGACommonState vga;
    GraphicHwOps hw_ops;

    MemoryRegion *vram;
    MemoryRegion vram_pci;
    uint8_t *vram_ptr;
    MemoryRegion ramin;
    uint8_t *ramin_ptr;

    MemoryRegion mmio;

    MemoryRegion block_mmio[NV_NUM_BLOCKS];

    struct {
        uint32_t pending_interrupts;
        uint32_t enabled_interrupts;
    } pmc;

    struct {
        uint32_t pending_interrupts;
        uint32_t enabled_interrupts;

        hwaddr ramht_address;
        unsigned int ramht_size;
        uint32_t ramht_search;

        hwaddr ramfc_address1;
        hwaddr ramfc_address2;
        unsigned int ramfc_size;

        QemuThread puller_thread;

        /* Weather the fifo chanels are PIO or DMA */
        uint32_t channel_modes;

        uint32_t channels_pending_push;

        Cache1State cache1;
    } pfifo;

    struct {
        uint32_t pending_interrupts;
        uint32_t enabled_interrupts;

        uint32_t numerator;
        uint32_t denominator;

        uint32_t alarm_time;
    } ptimer;

    struct {
        QemuMutex lock;

        uint32_t pending_interrupts;
        uint32_t enabled_interrupts;
        QemuCond interrupt_cond;

        hwaddr context_table;
        hwaddr context_address;


        unsigned int trapped_method;
        unsigned int trapped_subchannel;
        unsigned int trapped_channel_id;
        uint32_t trapped_data[2];
        uint32_t notify_source;

        bool fifo_access;
        QemuCond fifo_access_cond;

        unsigned int channel_id;
        bool channel_valid;
        GraphicsContext context[NV2A_NUM_CHANNELS];
    } pgraph;

    struct {
        uint32_t pending_interrupts;
        uint32_t enabled_interrupts;

        hwaddr start;
    } pcrtc;

    struct {
        uint32_t core_clock_coeff;
        uint64_t core_clock_freq;
        uint32_t memory_clock_coeff;
        uint32_t video_clock_coeff;
    } pramdac;

    struct {
        ChannelControl channel_control[NV2A_NUM_CHANNELS];
    } user;

} NV2AState;


#define NV2A_DEVICE(obj) \
    OBJECT_CHECK(NV2AState, (obj), "nv2a")

static void reg_log_read(int block, hwaddr addr, uint64_t val);
static void reg_log_write(int block, hwaddr addr, uint64_t val);
static void pgraph_method_log(unsigned int subchannel,
                              unsigned int graphics_class,
                              unsigned int method, uint32_t parameter);

static void update_irq(NV2AState *d)
{
    /* PFIFO */
    if (d->pfifo.pending_interrupts & d->pfifo.enabled_interrupts) {
        d->pmc.pending_interrupts |= NV_PMC_INTR_0_PFIFO;
    } else {
        d->pmc.pending_interrupts &= ~NV_PMC_INTR_0_PFIFO;
    }

    /* PCRTC */
    if (d->pcrtc.pending_interrupts & d->pcrtc.enabled_interrupts) {
        d->pmc.pending_interrupts |= NV_PMC_INTR_0_PCRTC;
    } else {
        d->pmc.pending_interrupts &= ~NV_PMC_INTR_0_PCRTC;
    }

    /* PGRAPH */
    if (d->pgraph.pending_interrupts & d->pgraph.enabled_interrupts) {
        d->pmc.pending_interrupts |= NV_PMC_INTR_0_PGRAPH;
    } else {
        d->pmc.pending_interrupts &= ~NV_PMC_INTR_0_PGRAPH;
    }

    if (d->pmc.pending_interrupts && d->pmc.enabled_interrupts) {
        qemu_irq_raise(d->irq);
    } else {
        qemu_irq_lower(d->irq);
    }
}

static uint32_t ramht_hash(NV2AState *d, uint32_t handle)
{
    uint32_t hash = 0;
    /* XXX: Think this is different to what nouveau calculates... */
    uint32_t bits = ffs(d->pfifo.ramht_size)-2;

    while (handle) {
        hash ^= (handle & ((1 << bits) - 1));
        handle >>= bits;
    }
    hash ^= d->pfifo.cache1.channel_id << (bits - 4);

    return hash;
}


static RAMHTEntry ramht_lookup(NV2AState *d, uint32_t handle)
{
    uint32_t hash;
    uint8_t *entry_ptr;
    uint32_t entry_handle;
    uint32_t entry_context;


    hash = ramht_hash(d, handle);
    assert(hash * 8 < d->pfifo.ramht_size);

    entry_ptr = d->ramin_ptr + d->pfifo.ramht_address + hash * 8;

    entry_handle = le32_to_cpupu((uint32_t*)entry_ptr);
    entry_context = le32_to_cpupu((uint32_t*)(entry_ptr + 4));

    return (RAMHTEntry){
        .handle = entry_handle,
        .instance = (entry_context & NV_RAMHT_INSTANCE) << 4,
        .engine = (entry_context & NV_RAMHT_ENGINE) >> 16,
        .channel_id = (entry_context & NV_RAMHT_CHID) >> 24,
        .valid = entry_context & NV_RAMHT_STATUS,
    };
}

static DMAObject nv_dma_load(NV2AState *d, hwaddr dma_obj_address)
{
    assert(dma_obj_address < memory_region_size(&d->ramin));

    uint32_t *dma_obj = (uint32_t*)(d->ramin_ptr + dma_obj_address);
    uint32_t flags = le32_to_cpupu(dma_obj);
    uint32_t limit = le32_to_cpupu(dma_obj + 1);
    uint32_t frame = le32_to_cpupu(dma_obj + 2);

    return (DMAObject){
        .dma_class = GET_MASK(flags, NV_DMA_CLASS),
        .dma_target = GET_MASK(flags, NV_DMA_TARGET),
        .address = (frame & NV_DMA_ADDRESS) | GET_MASK(flags, NV_DMA_ADJUST),
        .limit = limit,
    };
}

static void *nv_dma_map(NV2AState *d, hwaddr dma_obj_address, hwaddr *len)
{
    assert(dma_obj_address < memory_region_size(&d->ramin));

    DMAObject dma = nv_dma_load(d, dma_obj_address);

    /* TODO: Handle targets and classes properly */
    assert(dma.address + dma.limit < memory_region_size(d->vram));
    *len = dma.limit;
    return d->vram_ptr + dma.address;
}

static void load_graphics_object(NV2AState *d, hwaddr instance_address,
                                 GraphicsObject *obj)
{
    int i;
    uint8_t *obj_ptr;
    uint32_t switch1, switch2, switch3;

    assert(instance_address < memory_region_size(&d->ramin));

    obj_ptr = d->ramin_ptr + instance_address;

    switch1 = le32_to_cpupu((uint32_t*)obj_ptr);
    switch2 = le32_to_cpupu((uint32_t*)(obj_ptr+4));
    switch3 = le32_to_cpupu((uint32_t*)(obj_ptr+8));

    obj->graphics_class = switch1 & NV_PGRAPH_CTX_SWITCH1_GRCLASS;

    /* init graphics object */
    KelvinState *kelvin;
    switch (obj->graphics_class) {
    case NV_KELVIN_PRIMITIVE:
        kelvin = &obj->data.kelvin;

        /* generate vertex programs */
        for (i = 0; i < NV2A_VERTEXSHADER_SLOTS; i++) {
            VertexShader *shader = &kelvin->vertexshaders[i];
            glGenProgramsARB(1, &shader->gl_program);
        }
        assert(glGetError() == GL_NO_ERROR);

        /* fragment program */
        glGenProgramsARB(1, &kelvin->gl_fragment_program);
        kelvin->fragment_program_dirty = true;

        /* generate textures */
        for (i = 0; i < NV2A_MAX_TEXTURES; i++) {
            Texture *texture = &kelvin->textures[i];
            glGenTextures(1, &texture->gl_texture);
            glGenTextures(1, &texture->gl_texture_rect);
        }

        break;
    default:
        break;
    }
}

static GraphicsObject* lookup_graphics_object(GraphicsContext *ctx,
                                              hwaddr instance_address)
{
    int i;
    for (i=0; i<NV2A_NUM_SUBCHANNELS; i++) {
        if (ctx->subchannel_data[i].object_instance == instance_address) {
            return &ctx->subchannel_data[i].object;
        }
    }
    return NULL;
}


static void kelvin_bind_converted_vertex_attributes(NV2AState *d,
                                                    KelvinState *kelvin,
                                                    bool inline_data,
                                                    unsigned int num_elements)
{
    int i, j;
    for (i=0; i<NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *attribute = &kelvin->vertex_attributes[i];
        if (attribute->count && attribute->needs_conversion) {

            uint8_t *data;
            if (inline_data) {
                data = (uint8_t*)kelvin->inline_vertex_data
                        + attribute->inline_offset;
            } else {
                hwaddr dma_len;
                if (attribute->dma_select) {
                    data = nv_dma_map(d, kelvin->dma_vertex_b, &dma_len);
                } else {
                    data = nv_dma_map(d, kelvin->dma_vertex_a, &dma_len);
                }

                assert(attribute->offset < dma_len);
                data += attribute->offset;
            }

            unsigned int stride = attribute->converted_size
                                    * attribute->converted_count;
            
            if (num_elements > attribute->converted_elements) {
                attribute->converted_buffer = realloc(
                    attribute->converted_buffer,
                    num_elements * stride);
            }

            for (j=attribute->converted_elements; j<num_elements; j++) {
                uint8_t *in = data + j * attribute->stride;
                uint8_t *out = attribute->converted_buffer + j * stride;

                switch (attribute->format) {
                case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP:
                    r11g11b10f_to_float3(le32_to_cpupu((uint32_t*)in),
                                         (float*)out);
                    break;
                default:
                    assert(false);
                }
            }

            attribute->converted_elements = num_elements;

            glVertexAttribPointer(i,
                attribute->converted_count,
                attribute->gl_type,
                attribute->gl_normalize,
                stride,
                data);

        }
    }
}

static unsigned int kelvin_bind_inline_vertex_data(KelvinState *kelvin)
{
    int i;
    unsigned int offset = 0;
    for (i=0; i<NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *attribute = &kelvin->vertex_attributes[i];
        if (attribute->count) {

            attribute->inline_offset = offset;

            if (!attribute->needs_conversion) {
                glVertexAttribPointer(i,
                    attribute->count,
                    attribute->gl_type,
                    attribute->gl_normalize,
                    attribute->stride,
                    (uint8_t*)kelvin->inline_vertex_data + offset);
            }

            glEnableVertexAttribArray(i);

            offset += attribute->size * attribute->count;
        } else {
            glDisableVertexAttribArray(i);
        }
    }
    return offset;
}

static void kelvin_bind_vertex_attribute_offsets(NV2AState *d,
                                                 KelvinState *kelvin)
{
    int i;

    for (i=0; i<NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *attribute = &kelvin->vertex_attributes[i];
        if (attribute->count) {
            if (!attribute->needs_conversion) {
                hwaddr dma_len;
                uint8_t *vertex_data;

                /* TODO: cache coherence */
                if (attribute->dma_select) {
                    vertex_data = nv_dma_map(d, kelvin->dma_vertex_b, &dma_len);
                } else {
                    vertex_data = nv_dma_map(d, kelvin->dma_vertex_a, &dma_len);
                }
                assert(attribute->offset < dma_len);
                vertex_data += attribute->offset;

                glVertexAttribPointer(i,
                    attribute->count,
                    attribute->gl_type,
                    attribute->gl_normalize,
                    attribute->stride,
                    vertex_data);
            }

            glEnableVertexAttribArray(i);
        } else {
            glDisableVertexAttribArray(i);
        }
    }
}

static void kelvin_bind_vertexshader(KelvinState *kelvin)
{
    int i;
    VertexShader *shader;

    assert(kelvin->use_vertex_program);

    /* TODO */
    assert(!kelvin->enable_vertex_program_write);

    shader = &kelvin->vertexshaders[kelvin->vertexshader_start_slot];

    glBindProgramARB(GL_VERTEX_PROGRAM_ARB, shader->gl_program);

    if (shader->dirty) {
        QString *shader_code = vsh_translate(VSH_VERSION_XVS,
                                             shader->program_data,
                                             shader->program_length);
        const char* shader_code_str = qstring_get_str(shader_code);

        NV2A_DPRINTF("nv2a bind shader %d, code:\n%s\n",
                     kelvin->vertexshader_start_slot,
                     shader_code_str);

        glProgramStringARB(GL_VERTEX_PROGRAM_ARB,
                           GL_PROGRAM_FORMAT_ASCII_ARB,
                           strlen(shader_code_str),
                           shader_code_str);

        /* Check it compiled */
        GLint pos;
        glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &pos);
        if (pos != -1) {
            fprintf(stderr, "nv2a: Vertex shader compilation failed:\n"
                            "      pos %d, %s\n",
                    pos, glGetString(GL_PROGRAM_ERROR_STRING_ARB));
            fprintf(stderr, "ucode:\n");
            for (i=0; i<shader->program_length; i++) {
                fprintf(stderr, "    0x%08x,\n", shader->program_data[i]);
            }
            abort();
        }

        /* Check we're within resource limits */
        GLint native;
        glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB,
                          GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB,
                          &native);
        assert(native);

        assert(glGetError() == GL_NO_ERROR);

        QDECREF(shader_code);
        shader->dirty = false;
    }

    /* load constants */
    for (i=0; i<NV2A_VERTEXSHADER_CONSTANTS; i++) {
        VertexShaderConstant *constant = &kelvin->constants[i];
        if (!constant->dirty) continue;

        glProgramEnvParameter4fvARB(GL_VERTEX_PROGRAM_ARB,
                                    i,
                                    (const GLfloat*)constant->data);
        constant->dirty = false;
    }

    assert(glGetError() == GL_NO_ERROR);
}

static void kelvin_bind_textures(NV2AState *d, KelvinState *kelvin)
{
    int i;

    for (i=0; i<NV2A_MAX_TEXTURES; i++) {
        Texture *texture = &kelvin->textures[i];

        if (texture->dimensionality != 2) continue;
        
        glActiveTexture(GL_TEXTURE0_ARB + i);
        if (texture->enabled) {
            
            assert(texture->color_format
                    < sizeof(kelvin_color_format_map)/sizeof(ColorFormatInfo));
            ColorFormatInfo f = kelvin_color_format_map[texture->color_format];
            assert(f.bytes_per_pixel != 0);

            GLenum gl_target;
            GLuint gl_texture;
            unsigned int width, height;
            if (f.linear) {
                /* linear textures use unnormalised texcoords.
                 * GL_TEXTURE_RECTANGLE_ARB conveniently also does, but
                 * does not allow repeat and mirror wrap modes.
                 *  (or mipmapping, but xbox d3d says 'Non swizzled and non
                 *   compressed textures cannot be mip mapped.')
                 * Not sure if that'll be an issue. */
                gl_target = GL_TEXTURE_RECTANGLE_ARB;
                gl_texture = texture->gl_texture_rect;
                
                width = texture->rect_width;
                height = texture->rect_height;
            } else {
                gl_target = GL_TEXTURE_2D;
                gl_texture = texture->gl_texture;

                width = 1 << texture->log_width;
                height = 1 << texture->log_height;
            }

            glBindTexture(gl_target, gl_texture);

            if (!texture->dirty) continue;

            /* set parameters */
            glTexParameteri(gl_target, GL_TEXTURE_BASE_LEVEL,
                texture->min_mipmap_level);
            glTexParameteri(gl_target, GL_TEXTURE_MAX_LEVEL,
                texture->max_mipmap_level);

            glTexParameteri(gl_target, GL_TEXTURE_MIN_FILTER,
                kelvin_texture_min_filter_map[texture->min_filter]);
            glTexParameteri(gl_target, GL_TEXTURE_MAG_FILTER,
                kelvin_texture_mag_filter_map[texture->mag_filter]);

            /* load texture data*/

            hwaddr dma_len;
            uint8_t *texture_data;
            if (texture->dma_select) {
                texture_data = nv_dma_map(d, kelvin->dma_b, &dma_len);
            } else {
                texture_data = nv_dma_map(d, kelvin->dma_a, &dma_len);
            }
            assert(texture->offset < dma_len);
            texture_data += texture->offset;

            NV2A_DPRINTF(" texture %d is format 0x%x, (%d, %d; %d)\n",
                         i, texture->color_format,
                         width, height, texture->pitch);

            /* TODO: handle swizzling */


            if (f.gl_format == 0) { /* retarded way of indicating compressed */
                unsigned int block_size;
                if (f.gl_internal_format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) {
                    block_size = 8;
                } else {
                    block_size = 16;
                }
                glCompressedTexImage2D(gl_target, 0, f.gl_internal_format,
                                       width, height, 0,
                                       width/4 * height/4 * block_size,
                                       texture_data);
            } else {
                if (f.linear) {
                    /* Can't handle retarded strides */
                    assert(texture->pitch % f.bytes_per_pixel == 0);
                    glPixelStorei(GL_UNPACK_ROW_LENGTH,
                                  texture->pitch / f.bytes_per_pixel);
                }
                glTexImage2D(gl_target, 0, f.gl_internal_format,
                             width, height, 0,
                             f.gl_format, f.gl_type,
                             texture_data);
                if (f.linear) {
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                }
            }

            texture->dirty = false;
        } else {
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);
        }

    }
}

static void kelvin_bind_fragment_shader(NV2AState *d, KelvinState *kelvin)
{
    const char *shader_code = "!!ARBfp1.0\n"
"TEX result.color, fragment.texcoord[0], texture[0], RECT;\n"
"END\n";


    glEnable(GL_FRAGMENT_PROGRAM_ARB);
    glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, kelvin->gl_fragment_program);

    if (kelvin->fragment_program_dirty) {

        glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB,
                           GL_PROGRAM_FORMAT_ASCII_ARB,
                           strlen(shader_code),
                           shader_code);

        /* Check it compiled */
        GLint pos;
        glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &pos);
        if (pos != -1) {
            fprintf(stderr, "nv2a: Fragment shader compilation failed:\n"
                            "      pos %d, %s\n",
                    pos, glGetString(GL_PROGRAM_ERROR_STRING_ARB));
            abort();
        }

        kelvin->fragment_program_dirty = false;
    }

}

static void kelvin_read_surface(NV2AState *d, KelvinState *kelvin)
{
    /* read the renderbuffer into the set surface */
    if (kelvin->surface_color.format != 0 && kelvin->color_mask) {

        /* There's a bunch of bugs that could cause us to hit this functino
         * at the wrong time and get a invalid dma object.
         * Check that it's sane. */
        DMAObject color_dma = nv_dma_load(d, kelvin->dma_color);
        assert(color_dma.dma_class == NV_DMA_IN_MEMORY_CLASS);


        assert(color_dma.address + kelvin->surface_color.offset != 0);
        assert(kelvin->surface_color.offset <= color_dma.limit);
        assert(kelvin->surface_color.offset 
                + kelvin->surface_color.pitch * kelvin->surface_color.height
                    <= color_dma.limit + 1);


        hwaddr color_len;
        uint8_t *color_data = nv_dma_map(d, kelvin->dma_color, &color_len);
        
        GLenum gl_format;
        GLenum gl_type;
        unsigned int bytes_per_pixel;
        switch (kelvin->surface_color.format) {
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5:
            bytes_per_pixel = 2;
            gl_format = GL_RGB;
            gl_type = GL_UNSIGNED_SHORT_5_6_5_REV;
            break;
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8:
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8:
            bytes_per_pixel = 4;
            gl_format = GL_RGBA;
            gl_type = GL_UNSIGNED_INT_8_8_8_8_REV;
            break;
        default:
            assert(false);
        }

        /* TODO */
        assert(kelvin->surface_color.x == 0 && kelvin->surface_color.y == 0);

        glo_readpixels(gl_format, gl_type,
                       bytes_per_pixel, kelvin->surface_color.pitch,
                       kelvin->surface_color.width, kelvin->surface_color.height,
                       color_data + kelvin->surface_color.offset);
        assert(glGetError() == GL_NO_ERROR);

        memory_region_set_dirty(d->vram,
                                color_dma.address + kelvin->surface_color.offset,
                                kelvin->surface_color.pitch
                                    * kelvin->surface_color.height);
    }
}

static void kelvin_update_surface(NV2AState *d, KelvinState *kelvin)
{
    if (kelvin->surface_dirty) {
        kelvin_read_surface(d, kelvin);
        kelvin->surface_dirty = false;
    }
}


static void pgraph_context_init(GraphicsContext *context)
{

    context->gl_context = glo_context_create(GLO_FF_DEFAULT);

    /* TODO: create glo functions for Mac */

    /* Check context capabilities */
    const GLubyte *extensions;
    extensions = glGetString (GL_EXTENSIONS);

    assert(glo_check_extension((const GLubyte *)
                             "GL_EXT_texture_compression_s3tc",
                             extensions));

    assert(glo_check_extension((const GLubyte *)
                             "GL_EXT_framebuffer_object",
                             extensions));

    assert(glo_check_extension((const GLubyte *)
                             "GL_ARB_vertex_program",
                             extensions));

    assert(glo_check_extension((const GLubyte *)
                             "GL_ARB_fragment_program",
                             extensions));

    assert(glo_check_extension((const GLubyte *)
                             "GL_ARB_texture_rectangle",
                             extensions));

    GLint max_vertex_attributes;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &max_vertex_attributes);
    assert(max_vertex_attributes >= NV2A_VERTEXSHADER_ATTRIBUTES);

    glGenFramebuffersEXT(1, &context->gl_framebuffer);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, context->gl_framebuffer);

    glGenRenderbuffersEXT(1, &context->gl_renderbuffer);
    glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, context->gl_renderbuffer);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8,
                             640, 480);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,
                                 GL_COLOR_ATTACHMENT0_EXT,
                                 GL_RENDERBUFFER_EXT,
                                 context->gl_renderbuffer);

    assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)
            == GL_FRAMEBUFFER_COMPLETE_EXT);


    glViewport(0, 0, 640, 480);

    assert(glGetError() == GL_NO_ERROR);

    glo_set_current(NULL);
}

static void pgraph_context_set_current(GraphicsContext *context)
{
    if (context) {
        glo_set_current(context->gl_context);
    } else {
        glo_set_current(NULL);
    }
}

static void pgraph_context_destroy(GraphicsContext *context)
{
    glo_set_current(context->gl_context);

    glDeleteRenderbuffersEXT(1, &context->gl_renderbuffer);
    glDeleteFramebuffersEXT(1, &context->gl_framebuffer);

    glo_set_current(NULL);

    glo_context_destroy(context->gl_context);
}

static void pgraph_method(NV2AState *d,
                          unsigned int subchannel,
                          unsigned int method,
                          uint32_t parameter)
{
    int i;
    GraphicsContext *context;
    GraphicsSubchannel *subchannel_data;
    GraphicsObject *object;

    unsigned int slot;
    VertexAttribute *vertex_attribute;
    VertexShader *vertexshader;
    VertexShaderConstant *constant;

    qemu_mutex_lock(&d->pgraph.lock);

    assert(d->pgraph.channel_valid);
    context = &d->pgraph.context[d->pgraph.channel_id];
    subchannel_data = &context->subchannel_data[subchannel];
    object = &subchannel_data->object;

    ContextSurfaces2DState *context_surfaces_2d
        = &object->data.context_surfaces_2d;
    ImageBlitState *image_blit = &object->data.image_blit;
    KelvinState *kelvin = &object->data.kelvin;



    pgraph_method_log(subchannel, object->graphics_class, method, parameter);

    pgraph_context_set_current(context);

    if (method == NV_SET_OBJECT) {
        subchannel_data->object_instance = parameter;

        qemu_mutex_unlock(&d->pgraph.lock);
        //qemu_mutex_lock_iothread();
        load_graphics_object(d, parameter, object);
        //qemu_mutex_unlock_iothread();
        return;
    }

    uint32_t class_method = (object->graphics_class << 16) | method;
    switch (class_method) {
    case NV062_SET_CONTEXT_DMA_IMAGE_SOURCE:
        context_surfaces_2d->dma_image_source = parameter;
        break;
    case NV062_SET_CONTEXT_DMA_IMAGE_DESTIN:
        context_surfaces_2d->dma_image_dest = parameter;
        break;
    case NV062_SET_COLOR_FORMAT:
        context_surfaces_2d->color_format = parameter;
        break;
    case NV062_SET_PITCH:
        context_surfaces_2d->source_pitch = parameter & 0xFFFF;
        context_surfaces_2d->dest_pitch = parameter >> 16;
        break;
    case NV062_SET_OFFSET_SOURCE:
        context_surfaces_2d->source_offset = parameter;
        break;
    case NV062_SET_OFFSET_DESTIN:
        context_surfaces_2d->dest_offset = parameter;
        break;

    case NV09F_SET_CONTEXT_SURFACES:
        image_blit->context_surfaces = parameter;
        break;
    case NV09F_SET_OPERATION:
        image_blit->operation = parameter;
        break;
    case NV09F_CONTROL_POINT_IN:
        image_blit->in_x = parameter & 0xFFFF;
        image_blit->in_y = parameter >> 16;
        break;
    case NV09F_CONTROL_POINT_OUT:
        image_blit->out_x = parameter & 0xFFFF;
        image_blit->out_y = parameter >> 16;
        break;
    case NV09F_SIZE:
        image_blit->width = parameter & 0xFFFF;
        image_blit->height = parameter >> 16;

        /* I guess this kicks it off? */
        if (image_blit->operation == NV09F_SET_OPERATION_SRCCOPY) { 
            GraphicsObject *context_surfaces_obj =
                lookup_graphics_object(context, image_blit->context_surfaces);
            assert(context_surfaces_obj);
            assert(context_surfaces_obj->graphics_class
                == NV_CONTEXT_SURFACES_2D);

            ContextSurfaces2DState *context_surfaces =
                &context_surfaces_obj->data.context_surfaces_2d;

            unsigned int bytes_per_pixel;
            switch (context_surfaces->color_format) {
            case NV062_SET_COLOR_FORMAT_LE_Y8:
                bytes_per_pixel = 1;
                break;
            case NV062_SET_COLOR_FORMAT_LE_A8R8G8B8:
                bytes_per_pixel = 4;
                break;
            default:
                assert(false);
            }

            hwaddr source_dma_len, dest_dma_len;
            uint8_t *source, *dest;

            source = nv_dma_map(d, context_surfaces->dma_image_source,
                                &source_dma_len);
            assert(context_surfaces->source_offset < source_dma_len);
            source += context_surfaces->source_offset;

            dest = nv_dma_map(d, context_surfaces->dma_image_dest,
                              &dest_dma_len);
            assert(context_surfaces->dest_offset < dest_dma_len);
            dest += context_surfaces->dest_offset;

            int y;
            for (y=0; y<image_blit->height; y++) {
                uint8_t *source_row = source
                    + (image_blit->in_y + y) * context_surfaces->source_pitch
                    + image_blit->in_x * bytes_per_pixel;
                
                uint8_t *dest_row = dest
                    + (image_blit->out_y + y) * context_surfaces->dest_pitch
                    + image_blit->out_x * bytes_per_pixel;

                memmove(dest_row, source_row,
                        image_blit->width * bytes_per_pixel);
            }

        } else {
            assert(false);
        }

        break;


    case NV097_NO_OPERATION:
        /* The bios uses nop as a software method call -
         * it seems to expect a notify interrupt if the parameter isn't 0.
         * According to a nouveau guy it should still be a nop regardless
         * of the parameter. It's possible a debug register enables this,
         * but nothing obvious sticks out. Weird.
         */
        if (parameter != 0) {
            assert(!(d->pgraph.pending_interrupts & NV_PGRAPH_INTR_NOTIFY));

            d->pgraph.trapped_channel_id = d->pgraph.channel_id;
            d->pgraph.trapped_subchannel = subchannel;
            d->pgraph.trapped_method = method;
            d->pgraph.trapped_data[0] = parameter;
            d->pgraph.notify_source = NV_PGRAPH_NSOURCE_NOTIFICATION; /* TODO: check this */
            d->pgraph.pending_interrupts |= NV_PGRAPH_INTR_NOTIFY;

            qemu_mutex_unlock(&d->pgraph.lock);
            qemu_mutex_lock_iothread();
            update_irq(d);
            qemu_mutex_lock(&d->pgraph.lock);
            qemu_mutex_unlock_iothread();

            while (d->pgraph.pending_interrupts & NV_PGRAPH_INTR_NOTIFY) {
                qemu_cond_wait(&d->pgraph.interrupt_cond, &d->pgraph.lock);
            }
        }
        break;
    
    case NV097_WAIT_FOR_IDLE:
        glFinish();
        kelvin_update_surface(d, kelvin);
        break;

    case NV097_FLIP_STALL:
        kelvin_update_surface(d, kelvin);
        break;
    
    case NV097_SET_CONTEXT_DMA_NOTIFIES:
        kelvin->dma_notifies = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_A:
        kelvin->dma_a = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_B:
        kelvin->dma_b = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_STATE:
        kelvin->dma_state = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_COLOR:
        /* try to get any straggling draws in before the surface's changed :/ */
        kelvin_update_surface(d, kelvin);

        kelvin->dma_color = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_ZETA:
        kelvin->dma_zeta = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_VERTEX_A:
        kelvin->dma_vertex_a = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_VERTEX_B:
        kelvin->dma_vertex_b = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_SEMAPHORE:
        kelvin->dma_semaphore = parameter;
        break;

    case NV097_SET_SURFACE_CLIP_HORIZONTAL:
        kelvin_update_surface(d, kelvin);

        kelvin->surface_color.x =
            GET_MASK(parameter, NV097_SET_SURFACE_CLIP_HORIZONTAL_X);
        kelvin->surface_color.width =
            GET_MASK(parameter, NV097_SET_SURFACE_CLIP_HORIZONTAL_WIDTH);
        break;
    case NV097_SET_SURFACE_CLIP_VERTICAL:
        kelvin_update_surface(d, kelvin);

        kelvin->surface_color.y =
            GET_MASK(parameter, NV097_SET_SURFACE_CLIP_VERTICAL_Y);
        kelvin->surface_color.height =
            GET_MASK(parameter, NV097_SET_SURFACE_CLIP_VERTICAL_HEIGHT);
        break;
    case NV097_SET_SURFACE_FORMAT:
        kelvin_update_surface(d, kelvin);

        kelvin->surface_color.format =
            GET_MASK(parameter, NV097_SET_SURFACE_FORMAT_COLOR);
        kelvin->surface_zeta.format =
            GET_MASK(parameter, NV097_SET_SURFACE_FORMAT_ZETA);
        break;
    case NV097_SET_SURFACE_PITCH:
        kelvin_update_surface(d, kelvin);

        kelvin->surface_color.pitch =
            GET_MASK(parameter, NV097_SET_SURFACE_PITCH_COLOR);
        kelvin->surface_zeta.pitch =
            GET_MASK(parameter, NV097_SET_SURFACE_PITCH_ZETA);
        break;
    case NV097_SET_SURFACE_COLOR_OFFSET:
        kelvin_update_surface(d, kelvin);

        kelvin->surface_color.offset = parameter;
        break;
    case NV097_SET_SURFACE_ZETA_OFFSET:
        kelvin_update_surface(d, kelvin);

        kelvin->surface_zeta.offset = parameter;
        break;
    case NV097_SET_COLOR_MASK:
        kelvin->color_mask = parameter;
        break;

    case NV097_SET_VIEWPORT_OFFSET ...
            NV097_SET_VIEWPORT_OFFSET + 12:

        slot = (class_method - NV097_SET_VIEWPORT_OFFSET) / 4;

        /* populate magic viewport offset constant */
        kelvin->constants[59].data[slot] = parameter;
        kelvin->constants[59].dirty = true;
        break;
    case NV097_SET_VIEWPORT_SCALE ...
            NV097_SET_VIEWPORT_SCALE + 12:

        slot = (class_method - NV097_SET_VIEWPORT_SCALE) / 4;

        /* populate magic viewport scale constant */
        kelvin->constants[58].data[slot] = parameter;
        kelvin->constants[58].dirty = true;
        break;

    case NV097_SET_TRANSFORM_PROGRAM ...
            NV097_SET_TRANSFORM_PROGRAM + 0x7c:

        slot = (class_method - NV097_SET_TRANSFORM_PROGRAM) / 4;
        /* TODO: It should still work using a non-increasing slot??? */

        vertexshader = &kelvin->vertexshaders[kelvin->vertexshader_load_slot];
        assert(vertexshader->program_length < NV2A_MAX_VERTEXSHADER_LENGTH);
        vertexshader->program_data[
            vertexshader->program_length++] = parameter;
        break;

    case NV097_SET_TRANSFORM_CONSTANT ...
            NV097_SET_TRANSFORM_CONSTANT + 0x7c:

        slot = (class_method - NV097_SET_TRANSFORM_CONSTANT) / 4;

        constant = &kelvin->constants[kelvin->constant_load_slot+slot/4];
        constant->data[slot%4] = parameter;
        constant->dirty = true;
        break;


    case NV097_SET_VERTEX_DATA_ARRAY_FORMAT ...
            NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 0x3c:

        slot = (class_method - NV097_SET_VERTEX_DATA_ARRAY_FORMAT) / 4;
        vertex_attribute = &kelvin->vertex_attributes[slot];

        vertex_attribute->format =
            GET_MASK(parameter, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE);
        vertex_attribute->count =
            GET_MASK(parameter, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE);
        vertex_attribute->stride =
            GET_MASK(parameter, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE);


        switch (vertex_attribute->format) {
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D:
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL:
            vertex_attribute->gl_type = GL_UNSIGNED_BYTE;
            vertex_attribute->gl_normalize = GL_TRUE;
            vertex_attribute->size = 1;
            vertex_attribute->needs_conversion = false;
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1:
            vertex_attribute->gl_type = GL_SHORT;
            vertex_attribute->gl_normalize = GL_FALSE;
            vertex_attribute->size = 2;
            vertex_attribute->needs_conversion = false;
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F:
            vertex_attribute->gl_type = GL_FLOAT;
            vertex_attribute->gl_normalize = GL_FALSE;
            vertex_attribute->size = 4;
            vertex_attribute->needs_conversion = false;
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K:
            vertex_attribute->gl_type = GL_UNSIGNED_SHORT;
            vertex_attribute->gl_normalize = GL_FALSE;
            vertex_attribute->size = 2;
            vertex_attribute->needs_conversion = false;
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP:
            /* "3 signed, normalized components packed in 32-bits. (11,11,10)" */
            vertex_attribute->size = 4;
            vertex_attribute->gl_type = GL_FLOAT;
            vertex_attribute->gl_normalize = GL_FALSE;
            vertex_attribute->needs_conversion = true;
            vertex_attribute->converted_size = 4;
            vertex_attribute->converted_count = 3 * vertex_attribute->count;
            break;
        default:
            assert(false);
            break;
        }

        if (vertex_attribute->needs_conversion) {
            vertex_attribute->converted_elements = 0;
        } else {
            if (vertex_attribute->converted_buffer) {
                free(vertex_attribute->converted_buffer);
                vertex_attribute->converted_buffer = NULL;
            }
        }

        break;
    case NV097_SET_VERTEX_DATA_ARRAY_OFFSET ...
            NV097_SET_VERTEX_DATA_ARRAY_OFFSET + 0x3c:

        slot = (class_method - NV097_SET_VERTEX_DATA_ARRAY_OFFSET) / 4;

        kelvin->vertex_attributes[slot].dma_select =
            parameter & 0x80000000;
        kelvin->vertex_attributes[slot].offset =
            parameter & 0x7fffffff;

        kelvin->vertex_attributes[slot].converted_elements = 0;

        break;

    case NV097_SET_BEGIN_END:
        if (parameter == NV097_SET_BEGIN_END_OP_END) {

            if (kelvin->inline_vertex_data_length) {
                unsigned int vertex_size =
                    kelvin_bind_inline_vertex_data(kelvin);
                unsigned int index_count =
                    kelvin->inline_vertex_data_length*4 / vertex_size;
                
                kelvin_bind_converted_vertex_attributes(d, kelvin,
                    true, index_count);
                glDrawArrays(kelvin->gl_primitive_mode,
                             0, index_count);
            } else if (kelvin->array_batch_length) {


                uint32_t max_element = 0;
                uint32_t min_elemenet = (uint32_t)-1;
                for (i=0; i<kelvin->array_batch_length; i++) {
                    max_element = MAX(kelvin->array_batch[i], max_element);
                    min_elemenet = MIN(kelvin->array_batch[i], min_elemenet);
                }

                kelvin_bind_converted_vertex_attributes(d, kelvin,
                    false, max_element+1);
                glDrawElements(kelvin->gl_primitive_mode,
                               kelvin->array_batch_length,
                               GL_UNSIGNED_INT,
                               kelvin->array_batch);
            }/* else {
                assert(false);
            }*/
            assert(glGetError() == GL_NO_ERROR);
        } else {
            assert(parameter <= NV097_SET_BEGIN_END_OP_POLYGON);

            if (kelvin->use_vertex_program) {
                glEnable(GL_VERTEX_PROGRAM_ARB);
                kelvin_bind_vertexshader(kelvin);
            } else {
                glDisable(GL_VERTEX_PROGRAM_ARB);
            }

            kelvin_bind_fragment_shader(d, kelvin);

            kelvin_bind_textures(d, kelvin);
            kelvin_bind_vertex_attribute_offsets(d, kelvin);


            kelvin->gl_primitive_mode = kelvin_primitive_map[parameter];

            kelvin->array_batch_length = 0;
            kelvin->inline_vertex_data_length = 0;
        }
        kelvin->surface_dirty = true;
        break;
    CASE_4(NV097_SET_TEXTURE_OFFSET, 64):
        slot = (class_method - NV097_SET_TEXTURE_OFFSET) / 64;
        
        kelvin->textures[slot].offset = parameter;

        kelvin->textures[slot].dirty = true;
        break;
    CASE_4(NV097_SET_TEXTURE_FORMAT, 64):
        slot = (class_method - NV097_SET_TEXTURE_FORMAT) / 64;
        
        kelvin->textures[slot].dma_select =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_CONTEXT_DMA) == 2;
        kelvin->textures[slot].dimensionality =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_DIMENSIONALITY);
        kelvin->textures[slot].color_format = 
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_COLOR);
        kelvin->textures[slot].log_width =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_BASE_SIZE_U);
        kelvin->textures[slot].log_height =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_BASE_SIZE_V);
        
        kelvin->textures[slot].dirty = true;
        break;
    CASE_4(NV097_SET_TEXTURE_CONTROL0, 64):
        slot = (class_method - NV097_SET_TEXTURE_CONTROL0) / 64;
        
        kelvin->textures[slot].enabled =
            parameter & NV097_SET_TEXTURE_CONTROL0_ENABLE;
        kelvin->textures[slot].min_mipmap_level =
            GET_MASK(parameter, NV097_SET_TEXTURE_CONTROL0_MIN_LOD_CLAMP);
        kelvin->textures[slot].max_mipmap_level =
            GET_MASK(parameter, NV097_SET_TEXTURE_CONTROL0_MAX_LOD_CLAMP);
        
        break;
    CASE_4(NV097_SET_TEXTURE_CONTROL1, 64):
        slot = (class_method - NV097_SET_TEXTURE_CONTROL1) / 64;

        kelvin->textures[slot].pitch =
            GET_MASK(parameter, NV097_SET_TEXTURE_CONTROL1_IMAGE_PITCH);

        break;
    CASE_4(NV097_SET_TEXTURE_FILTER, 64):
        slot = (class_method - NV097_SET_TEXTURE_FILTER) / 64;

        kelvin->textures[slot].min_filter =
            GET_MASK(parameter, NV097_SET_TEXTURE_FILTER_MIN);
        kelvin->textures[slot].mag_filter =
            GET_MASK(parameter, NV097_SET_TEXTURE_FILTER_MAG);

        break;
    CASE_4(NV097_SET_TEXTURE_IMAGE_RECT, 64):
        slot = (class_method - NV097_SET_TEXTURE_IMAGE_RECT) / 64;
        
        kelvin->textures[slot].rect_width = 
            GET_MASK(parameter, NV097_SET_TEXTURE_IMAGE_RECT_WIDTH);
        kelvin->textures[slot].rect_height =
            GET_MASK(parameter, NV097_SET_TEXTURE_IMAGE_RECT_HEIGHT);
        
        kelvin->textures[slot].dirty = true;
        break;
    case NV097_ARRAY_ELEMENT16:
        assert(kelvin->array_batch_length < NV2A_MAX_BATCH_LENGTH);
        kelvin->array_batch[
            kelvin->array_batch_length++] = parameter & 0xFFFF;
        kelvin->array_batch[
            kelvin->array_batch_length++] = parameter >> 16;
        break;
    case NV097_ARRAY_ELEMENT32:
        assert(kelvin->array_batch_length < NV2A_MAX_BATCH_LENGTH);
        kelvin->array_batch[
            kelvin->array_batch_length++] = parameter;
        break;
    case NV097_DRAW_ARRAYS: {
        unsigned int start = GET_MASK(parameter, NV097_DRAW_ARRAYS_START_INDEX);
        unsigned int count = GET_MASK(parameter, NV097_DRAW_ARRAYS_COUNT)+1;

        kelvin_bind_converted_vertex_attributes(d, kelvin,
            false, start + count);
        glDrawArrays(kelvin->gl_primitive_mode, start, count);

        kelvin->surface_dirty = true;
        break;
    }
    case NV097_INLINE_ARRAY:
        assert(kelvin->inline_vertex_data_length < NV2A_MAX_BATCH_LENGTH);
        kelvin->inline_vertex_data[
            kelvin->inline_vertex_data_length++] = parameter;
        break;

    case NV097_SET_SEMAPHORE_OFFSET:
        kelvin->semaphore_offset = parameter;
        break;
    case NV097_BACK_END_WRITE_SEMAPHORE_RELEASE: {
        //qemu_mutex_unlock(&d->pgraph.lock);
        //qemu_mutex_lock_iothread();

        hwaddr semaphore_dma_len;
        uint8_t *semaphore_data = nv_dma_map(d, kelvin->dma_semaphore, 
                                             &semaphore_dma_len);
        assert(kelvin->semaphore_offset < semaphore_dma_len);
        semaphore_data += kelvin->semaphore_offset;

        cpu_to_le32wu((uint32_t*)semaphore_data, parameter);

        //qemu_mutex_lock(&d->pgraph.lock);
        //qemu_mutex_unlock_iothread();

        break;
    }
    case NV097_SET_ZSTENCIL_CLEAR_VALUE:
        context->zstencil_clear_value = parameter;
        break;

    case NV097_SET_COLOR_CLEAR_VALUE:
        glClearColor( ((parameter >> 16) & 0xFF) / 255.0f, /* red */
                      ((parameter >> 8) & 0xFF) / 255.0f,  /* green */
                      (parameter & 0xFF) / 255.0f,         /* blue */
                      ((parameter >> 24) & 0xFF) / 255.0f);/* alpha */
        context->color_clear_value = parameter;
        break;

    case NV097_CLEAR_SURFACE:
        /* QQQ */
        NV2A_DPRINTF("------------------CLEAR 0x%x---------------\n", parameter);
        //glClearColor(1, 0, 0, 1);

        GLbitfield gl_mask = 0;
        if (parameter & NV097_CLEAR_SURFACE_Z) {
            gl_mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (parameter & NV097_CLEAR_SURFACE_STENCIL) {
            gl_mask |= GL_STENCIL_BUFFER_BIT;
        }
        if (parameter & (
                NV097_CLEAR_SURFACE_R | NV097_CLEAR_SURFACE_G
                | NV097_CLEAR_SURFACE_B | NV097_CLEAR_SURFACE_A)) {
            gl_mask |= GL_COLOR_BUFFER_BIT;
        }
        glClear(gl_mask);

        kelvin->surface_dirty = true;
        break;

    case NV097_SET_TRANSFORM_EXECUTION_MODE:
        kelvin->use_vertex_program = (parameter & 3) == 2;
        break;
    case NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN:
        kelvin->enable_vertex_program_write = parameter;
        break;
    case NV097_SET_TRANSFORM_PROGRAM_LOAD:
        assert(parameter < NV2A_VERTEXSHADER_SLOTS);
        kelvin->vertexshader_load_slot = parameter;
        kelvin->vertexshaders[parameter].program_length = 0; /* ??? */
        kelvin->vertexshaders[parameter].dirty = true;
        break;
    case NV097_SET_TRANSFORM_PROGRAM_START:
        assert(parameter < NV2A_VERTEXSHADER_SLOTS);
        /* if the shader changed, dirty all the constants */
        if (parameter != kelvin->vertexshader_start_slot) {
            for (i=0; i<NV2A_VERTEXSHADER_CONSTANTS; i++) {
                kelvin->constants[i].dirty = true;
            }
        }
        kelvin->vertexshader_start_slot = parameter;
        break;
    case NV097_SET_TRANSFORM_CONSTANT_LOAD:
        assert(parameter < NV2A_VERTEXSHADER_CONSTANTS);
        kelvin->constant_load_slot = parameter;
        NV2A_DPRINTF("load to %d\n", parameter);
        break;

    default:
        NV2A_DPRINTF("    unhandled  (0x%02x 0x%08x)\n",
                     object->graphics_class, method);
        break;
    }
    qemu_mutex_unlock(&d->pgraph.lock);

}


static void pgraph_context_switch(NV2AState *d, unsigned int channel_id)
{
    bool valid;
    qemu_mutex_lock(&d->pgraph.lock);
    valid = d->pgraph.channel_valid && d->pgraph.channel_id == channel_id;
    if (!valid) {
        d->pgraph.trapped_channel_id = channel_id;
    }
    qemu_mutex_unlock(&d->pgraph.lock);
    if (!valid) {
        NV2A_DPRINTF("nv2a: puller needs to switch to ch %d\n", channel_id);
        
        qemu_mutex_lock_iothread();
        d->pgraph.pending_interrupts |= NV_PGRAPH_INTR_CONTEXT_SWITCH;
        update_irq(d);
        qemu_mutex_unlock_iothread();

        qemu_mutex_lock(&d->pgraph.lock);
        while (d->pgraph.pending_interrupts & NV_PGRAPH_INTR_CONTEXT_SWITCH) {
            qemu_cond_wait(&d->pgraph.interrupt_cond, &d->pgraph.lock);
        }
        qemu_mutex_unlock(&d->pgraph.lock);
    }
}

static void pgraph_wait_fifo_access(NV2AState *d) {
    qemu_mutex_lock(&d->pgraph.lock);
    while (!d->pgraph.fifo_access) {
        qemu_cond_wait(&d->pgraph.fifo_access_cond, &d->pgraph.lock);
    }
    qemu_mutex_unlock(&d->pgraph.lock);
}

static void *pfifo_puller_thread(void *arg)
{
    NV2AState *d = arg;
    Cache1State *state = &d->pfifo.cache1;
    CacheEntry *command;
    RAMHTEntry entry;

    while (true) {
        qemu_mutex_lock(&state->pull_lock);
        if (!state->pull_enabled) {
            qemu_mutex_unlock(&state->pull_lock);
            return NULL;
        }
        qemu_mutex_unlock(&state->pull_lock);

        qemu_mutex_lock(&state->cache_lock);
        while (QSIMPLEQ_EMPTY(&state->cache)) {
            qemu_cond_wait(&state->cache_cond, &state->cache_lock);

            /* we could have been woken up to tell us we should die */
            qemu_mutex_lock(&state->pull_lock);
            if (!state->pull_enabled) {
                qemu_mutex_unlock(&state->pull_lock);
                qemu_mutex_unlock(&state->cache_lock);
                return NULL;
            }
            qemu_mutex_unlock(&state->pull_lock);
        }
        command = QSIMPLEQ_FIRST(&state->cache);
        QSIMPLEQ_REMOVE_HEAD(&state->cache, entry);
        qemu_mutex_unlock(&state->cache_lock);

        if (command->method == 0) {
            //qemu_mutex_lock_iothread();
            entry = ramht_lookup(d, command->parameter);
            assert(entry.valid);

            assert(entry.channel_id == state->channel_id);
            //qemu_mutex_unlock_iothread();

            switch (entry.engine) {
            case ENGINE_GRAPHICS:
                pgraph_context_switch(d, entry.channel_id);
                pgraph_wait_fifo_access(d);
                pgraph_method(d, command->subchannel, 0, entry.instance);
                break;
            default:
                assert(false);
                break;
            }

            /* the engine is bound to the subchannel */
            qemu_mutex_lock(&state->pull_lock);
            state->bound_engines[command->subchannel] = entry.engine;
            state->last_engine = entry.engine;
            qemu_mutex_unlock(&state->pull_lock);
        } else if (command->method >= 0x100) {
            /* method passed to engine */

            uint32_t parameter = command->parameter;

            /* methods that take objects.
             * TODO: Check this range is correct for the nv2a */
            if (command->method >= 0x180 && command->method < 0x200) {
                //qemu_mutex_lock_iothread();
                entry = ramht_lookup(d, parameter);
                assert(entry.valid);
                assert(entry.channel_id == state->channel_id);
                parameter = entry.instance;
                //qemu_mutex_unlock_iothread();
            }

            qemu_mutex_lock(&state->pull_lock);
            enum FIFOEngine engine = state->bound_engines[command->subchannel];
            qemu_mutex_unlock(&state->pull_lock);

            switch (engine) {
            case ENGINE_GRAPHICS:
                pgraph_wait_fifo_access(d);
                pgraph_method(d, command->subchannel,
                                   command->method, parameter);
                break;
            default:
                assert(false);
                break;
            }

            qemu_mutex_lock(&state->pull_lock);
            state->last_engine = state->bound_engines[command->subchannel];
            qemu_mutex_unlock(&state->pull_lock);
        }
    }

    return NULL;
}

/* pusher should be fine to run from a mimo handler
 * whenever's it's convenient */
static void pfifo_run_pusher(NV2AState *d) {
    uint8_t channel_id;
    ChannelControl *control;
    Cache1State *state;
    CacheEntry *command;
    uint8_t *dma;
    hwaddr dma_len;
    uint32_t word;

    /* TODO: How is cache1 selected? */
    state = &d->pfifo.cache1;
    channel_id = state->channel_id;
    control = &d->user.channel_control[channel_id];

    if (!state->push_enabled) return;


    /* only handling DMA for now... */

    /* Channel running DMA */
    assert(d->pfifo.channel_modes & (1 << channel_id));
    assert(state->mode == FIFO_DMA);

    if (!state->dma_push_enabled) return;
    if (state->dma_push_suspended) return;

    /* We're running so there should be no pending errors... */
    assert(state->error == NV_PFIFO_CACHE1_DMA_STATE_ERROR_NONE);

    dma = nv_dma_map(d, state->dma_instance, &dma_len);

    NV2A_DPRINTF("nv2a DMA pusher: max 0x%llx, 0x%llx - 0x%llx\n",
                 dma_len, control->dma_get, control->dma_put);

    /* based on the convenient pseudocode in envytools */
    while (control->dma_get != control->dma_put) {
        if (control->dma_get >= dma_len) {

            state->error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_PROTECTION;
            break;
        }

        word = le32_to_cpupu((uint32_t*)(dma + control->dma_get));
        control->dma_get += 4;

        if (state->method_count) {
            /* data word of methods command */
            state->data_shadow = word;

            command = g_malloc0(sizeof(CacheEntry));
            command->method = state->method;
            command->subchannel = state->subchannel;
            command->nonincreasing = state->method_nonincreasing;
            command->parameter = word;
            qemu_mutex_lock(&state->cache_lock);
            QSIMPLEQ_INSERT_TAIL(&state->cache, command, entry);
            qemu_cond_signal(&state->cache_cond);
            qemu_mutex_unlock(&state->cache_lock);

            if (!state->method_nonincreasing) {
                state->method += 4;
            }
            state->method_count--;
            state->dcount++;
        } else {
            /* no command active - this is the first word of a new one */
            state->rsvd_shadow = word;
            /* match all forms */
            if ((word & 0xe0000003) == 0x20000000) {
                /* old jump */
                state->get_jmp_shadow = control->dma_get;
                control->dma_get = word & 0x1fffffff;
                NV2A_DPRINTF("nv2a pb OLD_JMP 0x%llx\n", control->dma_get);
            } else if ((word & 3) == 1) {
                /* jump */
                state->get_jmp_shadow = control->dma_get;
                control->dma_get = word & 0xfffffffc;
                NV2A_DPRINTF("nv2a pb JMP 0x%llx\n", control->dma_get);
            } else if ((word & 3) == 2) {
                /* call */
                if (state->subroutine_active) {
                    state->error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_CALL;
                    break;
                }
                state->subroutine_return = control->dma_get;
                state->subroutine_active = true;
                control->dma_get = word & 0xfffffffc;
                NV2A_DPRINTF("nv2a pb CALL 0x%llx\n", control->dma_get);
            } else if (word == 0x00020000) {
                /* return */
                if (!state->subroutine_active) {
                    state->error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_RETURN;
                    break;
                }
                control->dma_get = state->subroutine_return;
                state->subroutine_active = false;
                NV2A_DPRINTF("nv2a pb RET 0x%llx\n", control->dma_get);
            } else if ((word & 0xe0030003) == 0) {
                /* increasing methods */
                state->method = word & 0x1fff;
                state->subchannel = (word >> 13) & 7;
                state->method_count = (word >> 18) & 0x7ff;
                state->method_nonincreasing = false;
                state->dcount = 0;
            } else if ((word & 0xe0030003) == 0x40000000) {
                /* non-increasing methods */
                state->method = word & 0x1fff;
                state->subchannel = (word >> 13) & 7;
                state->method_count = (word >> 18) & 0x7ff;
                state->method_nonincreasing = true;
                state->dcount = 0;
            } else {
                state->error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_RESERVED_CMD;
                break;
            }
        }
    }

    if (state->error) {
        NV2A_DPRINTF("nv2a pb error: %d\n", state->error);
        state->dma_push_suspended = true;

        d->pfifo.pending_interrupts |= NV_PFIFO_INTR_0_DMA_PUSHER;
        update_irq(d);
    }
}





/* PMC - card master control */
static uint64_t pmc_read(void *opaque,
                              hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PMC_BOOT_0:
        /* chipset and stepping:
         * NV2A, A02, Rev 0 */

        r = 0x02A000A2;
        break;
    case NV_PMC_INTR_0:
        /* Shows which functional units have pending IRQ */
        r = d->pmc.pending_interrupts;
        break;
    case NV_PMC_INTR_EN_0:
        /* Selects which functional units can cause IRQs */
        r = d->pmc.enabled_interrupts;
        break;
    default:
        break;
    }

    reg_log_read(NV_PMC, addr, r);
    return r;
}
static void pmc_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    reg_log_write(NV_PMC, addr, val);

    switch (addr) {
    case NV_PMC_INTR_0:
        /* the bits of the interrupts to clear are wrtten */
        d->pmc.pending_interrupts &= ~val;
        update_irq(d);
        break;
    case NV_PMC_INTR_EN_0:
        d->pmc.enabled_interrupts = val;
        update_irq(d);
        break;
    default:
        break;
    }
}


/* PBUS - bus control */
static uint64_t pbus_read(void *opaque,
                               hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PBUS_PCI_NV_0:
        r = pci_get_long(d->dev.config + PCI_VENDOR_ID);
        break;
    case NV_PBUS_PCI_NV_1:
        r = pci_get_long(d->dev.config + PCI_COMMAND);
        break;
    case NV_PBUS_PCI_NV_2:
        r = pci_get_long(d->dev.config + PCI_CLASS_REVISION);
        break;
    default:
        break;
    }

    reg_log_read(NV_PBUS, addr, r);
    return r;
}
static void pbus_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    reg_log_write(NV_PBUS, addr, val);

    switch (addr) {
    case NV_PBUS_PCI_NV_1:
        pci_set_long(d->dev.config + PCI_COMMAND, val);
        break;
    default:
        break;
    }
}


/* PFIFO - MMIO and DMA FIFO submission to PGRAPH and VPE */
static uint64_t pfifo_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    int i;
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PFIFO_INTR_0:
        r = d->pfifo.pending_interrupts;
        break;
    case NV_PFIFO_INTR_EN_0:
        r = d->pfifo.enabled_interrupts;
        break;
    case NV_PFIFO_RAMHT:
        SET_MASK(r, NV_PFIFO_RAMHT_BASE_ADDRESS, d->pfifo.ramht_address >> 12);
        SET_MASK(r, NV_PFIFO_RAMHT_SEARCH, d->pfifo.ramht_search);
        SET_MASK(r, NV_PFIFO_RAMHT_SIZE, ffs(d->pfifo.ramht_size)-13);
        break;
    case NV_PFIFO_RAMFC:
        SET_MASK(r, NV_PFIFO_RAMFC_BASE_ADDRESS1,
                 d->pfifo.ramfc_address1 >> 10);
        SET_MASK(r, NV_PFIFO_RAMFC_BASE_ADDRESS2,
                 d->pfifo.ramfc_address2 >> 10);
        SET_MASK(r, NV_PFIFO_RAMFC_SIZE, d->pfifo.ramfc_size);
        break;
    case NV_PFIFO_RUNOUT_STATUS:
        r = NV_PFIFO_RUNOUT_STATUS_LOW_MARK; /* low mark empty */
        break;
    case NV_PFIFO_MODE:
        r = d->pfifo.channel_modes;
        break;
    case NV_PFIFO_DMA:
        r = d->pfifo.channels_pending_push;
        break;

    case NV_PFIFO_CACHE1_PUSH0:
        r = d->pfifo.cache1.push_enabled;
        break;
    case NV_PFIFO_CACHE1_PUSH1:
        SET_MASK(r, NV_PFIFO_CACHE1_PUSH1_CHID, d->pfifo.cache1.channel_id);
        SET_MASK(r, NV_PFIFO_CACHE1_PUSH1_MODE, d->pfifo.cache1.mode);
        break;
    case NV_PFIFO_CACHE1_STATUS:
        qemu_mutex_lock(&d->pfifo.cache1.cache_lock);
        if (QSIMPLEQ_EMPTY(&d->pfifo.cache1.cache)) {
            r |= NV_PFIFO_CACHE1_STATUS_LOW_MARK; /* low mark empty */
        }
        qemu_mutex_unlock(&d->pfifo.cache1.cache_lock);
        break;
    case NV_PFIFO_CACHE1_DMA_PUSH:
        SET_MASK(r, NV_PFIFO_CACHE1_DMA_PUSH_ACCESS,
                 d->pfifo.cache1.dma_push_enabled);
        SET_MASK(r, NV_PFIFO_CACHE1_DMA_PUSH_STATUS,
                 d->pfifo.cache1.dma_push_suspended);
        SET_MASK(r, NV_PFIFO_CACHE1_DMA_PUSH_BUFFER, 1); /* buffer emoty */
        break;
    case NV_PFIFO_CACHE1_DMA_STATE:
        SET_MASK(r, NV_PFIFO_CACHE1_DMA_STATE_METHOD_TYPE,
                 d->pfifo.cache1.method_nonincreasing);
        SET_MASK(r, NV_PFIFO_CACHE1_DMA_STATE_METHOD,
                 d->pfifo.cache1.method >> 2);
        SET_MASK(r, NV_PFIFO_CACHE1_DMA_STATE_SUBCHANNEL,
                 d->pfifo.cache1.subchannel);
        SET_MASK(r, NV_PFIFO_CACHE1_DMA_STATE_METHOD_COUNT,
                 d->pfifo.cache1.method_count);
        SET_MASK(r, NV_PFIFO_CACHE1_DMA_STATE_ERROR,
                 d->pfifo.cache1.error);
        break;
    case NV_PFIFO_CACHE1_DMA_INSTANCE:
        SET_MASK(r, NV_PFIFO_CACHE1_DMA_INSTANCE_ADDRESS,
                 d->pfifo.cache1.dma_instance >> 4);
        break;
    case NV_PFIFO_CACHE1_DMA_PUT:
        r = d->user.channel_control[d->pfifo.cache1.channel_id].dma_put;
        break;
    case NV_PFIFO_CACHE1_DMA_GET:
        r = d->user.channel_control[d->pfifo.cache1.channel_id].dma_get;
        break;
    case NV_PFIFO_CACHE1_DMA_SUBROUTINE:
        r = d->pfifo.cache1.subroutine_return
            | d->pfifo.cache1.subroutine_active;
        break;
    case NV_PFIFO_CACHE1_PULL0:
        qemu_mutex_lock(&d->pfifo.cache1.pull_lock);
        r = d->pfifo.cache1.pull_enabled;
        qemu_mutex_unlock(&d->pfifo.cache1.pull_lock);
        break;
    case NV_PFIFO_CACHE1_ENGINE:
        qemu_mutex_lock(&d->pfifo.cache1.pull_lock);
        for (i=0; i<NV2A_NUM_SUBCHANNELS; i++) {
            r |= d->pfifo.cache1.bound_engines[i] << (i*2);
        }
        qemu_mutex_unlock(&d->pfifo.cache1.pull_lock);
        break;
    case NV_PFIFO_CACHE1_DMA_DCOUNT:
        r = d->pfifo.cache1.dcount;
        break;
    case NV_PFIFO_CACHE1_DMA_GET_JMP_SHADOW:
        r = d->pfifo.cache1.get_jmp_shadow;
        break;
    case NV_PFIFO_CACHE1_DMA_RSVD_SHADOW:
        r = d->pfifo.cache1.rsvd_shadow;
        break;
    case NV_PFIFO_CACHE1_DMA_DATA_SHADOW:
        r = d->pfifo.cache1.data_shadow;
        break;
    default:
        break;
    }

    reg_log_read(NV_PFIFO, addr, r);
    return r;
}
static void pfifo_write(void *opaque, hwaddr addr,
                        uint64_t val, unsigned int size)
{
    int i;
    NV2AState *d = opaque;

    reg_log_write(NV_PFIFO, addr, val);

    switch (addr) {
    case NV_PFIFO_INTR_0:
        d->pfifo.pending_interrupts &= ~val;
        update_irq(d);
        break;
    case NV_PFIFO_INTR_EN_0:
        d->pfifo.enabled_interrupts = val;
        update_irq(d);
        break;
    case NV_PFIFO_RAMHT:
        d->pfifo.ramht_address =
            GET_MASK(val, NV_PFIFO_RAMHT_BASE_ADDRESS) << 12;
        d->pfifo.ramht_size = 1 << (GET_MASK(val, NV_PFIFO_RAMHT_SIZE)+12);
        d->pfifo.ramht_search = GET_MASK(val, NV_PFIFO_RAMHT_SEARCH);
        break;
    case NV_PFIFO_RAMFC:
        d->pfifo.ramfc_address1 =
            GET_MASK(val, NV_PFIFO_RAMFC_BASE_ADDRESS1) << 10;
        d->pfifo.ramfc_address2 =
            GET_MASK(val, NV_PFIFO_RAMFC_BASE_ADDRESS2) << 10;
        d->pfifo.ramfc_size = GET_MASK(val, NV_PFIFO_RAMFC_SIZE);
        break;
    case NV_PFIFO_MODE:
        d->pfifo.channel_modes = val;
        break;
    case NV_PFIFO_DMA:
        d->pfifo.channels_pending_push = val;
        break;

    case NV_PFIFO_CACHE1_PUSH0:
        d->pfifo.cache1.push_enabled = val & NV_PFIFO_CACHE1_PUSH0_ACCESS;
        break;
    case NV_PFIFO_CACHE1_PUSH1:
        d->pfifo.cache1.channel_id = GET_MASK(val, NV_PFIFO_CACHE1_PUSH1_CHID);
        d->pfifo.cache1.mode = GET_MASK(val, NV_PFIFO_CACHE1_PUSH1_MODE);
        assert(d->pfifo.cache1.channel_id < NV2A_NUM_CHANNELS);
        break;
    case NV_PFIFO_CACHE1_DMA_PUSH:
        d->pfifo.cache1.dma_push_enabled =
            GET_MASK(val, NV_PFIFO_CACHE1_DMA_PUSH_ACCESS);
        if (d->pfifo.cache1.dma_push_suspended
             && !GET_MASK(val, NV_PFIFO_CACHE1_DMA_PUSH_STATUS)) {
            d->pfifo.cache1.dma_push_suspended = false;
            pfifo_run_pusher(d);
        }
        d->pfifo.cache1.dma_push_suspended =
            GET_MASK(val, NV_PFIFO_CACHE1_DMA_PUSH_STATUS);
        break;
    case NV_PFIFO_CACHE1_DMA_STATE:
        d->pfifo.cache1.method_nonincreasing =
            GET_MASK(val, NV_PFIFO_CACHE1_DMA_STATE_METHOD_TYPE);
        d->pfifo.cache1.method =
            GET_MASK(val, NV_PFIFO_CACHE1_DMA_STATE_METHOD) << 2;
        d->pfifo.cache1.subchannel =
            GET_MASK(val, NV_PFIFO_CACHE1_DMA_STATE_SUBCHANNEL);
        d->pfifo.cache1.method_count =
            GET_MASK(val, NV_PFIFO_CACHE1_DMA_STATE_METHOD_COUNT);
        d->pfifo.cache1.error =
            GET_MASK(val, NV_PFIFO_CACHE1_DMA_STATE_ERROR);
        break;
    case NV_PFIFO_CACHE1_DMA_INSTANCE:
        d->pfifo.cache1.dma_instance =
            GET_MASK(val, NV_PFIFO_CACHE1_DMA_INSTANCE_ADDRESS) << 4;
        break;
    case NV_PFIFO_CACHE1_DMA_PUT:
        d->user.channel_control[d->pfifo.cache1.channel_id].dma_put = val;
        break;
    case NV_PFIFO_CACHE1_DMA_GET:
        d->user.channel_control[d->pfifo.cache1.channel_id].dma_get = val;
        break;
    case NV_PFIFO_CACHE1_DMA_SUBROUTINE:
        d->pfifo.cache1.subroutine_return =
            (val & NV_PFIFO_CACHE1_DMA_SUBROUTINE_RETURN_OFFSET);
        d->pfifo.cache1.subroutine_active =
            (val & NV_PFIFO_CACHE1_DMA_SUBROUTINE_STATE);
        break;
    case NV_PFIFO_CACHE1_PULL0:
        qemu_mutex_lock(&d->pfifo.cache1.pull_lock);
        if ((val & NV_PFIFO_CACHE1_PULL0_ACCESS)
             && !d->pfifo.cache1.pull_enabled) {
            d->pfifo.cache1.pull_enabled = true;

            /* fire up puller thread */
            qemu_thread_create(&d->pfifo.puller_thread,
                               pfifo_puller_thread,
                               d, QEMU_THREAD_DETACHED);
        } else if (!(val & NV_PFIFO_CACHE1_PULL0_ACCESS)
                     && d->pfifo.cache1.pull_enabled) {
            d->pfifo.cache1.pull_enabled = false;

            /* the puller thread should die, wake it up. */
            qemu_cond_broadcast(&d->pfifo.cache1.cache_cond);
        }
        qemu_mutex_unlock(&d->pfifo.cache1.pull_lock);
        break;
    case NV_PFIFO_CACHE1_ENGINE:
        qemu_mutex_lock(&d->pfifo.cache1.pull_lock);
        for (i=0; i<NV2A_NUM_SUBCHANNELS; i++) {
            d->pfifo.cache1.bound_engines[i] = (val >> (i*2)) & 3;
        }
        qemu_mutex_unlock(&d->pfifo.cache1.pull_lock);
        break;
    case NV_PFIFO_CACHE1_DMA_DCOUNT:
        d->pfifo.cache1.dcount =
            (val & NV_PFIFO_CACHE1_DMA_DCOUNT_VALUE);
        break;
    case NV_PFIFO_CACHE1_DMA_GET_JMP_SHADOW:
        d->pfifo.cache1.get_jmp_shadow =
            (val & NV_PFIFO_CACHE1_DMA_GET_JMP_SHADOW_OFFSET);
        break;
    case NV_PFIFO_CACHE1_DMA_RSVD_SHADOW:
        d->pfifo.cache1.rsvd_shadow = val;
        break;
    case NV_PFIFO_CACHE1_DMA_DATA_SHADOW:
        d->pfifo.cache1.data_shadow = val;
        break;
    default:
        break;
    }
}


static uint64_t prma_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    reg_log_read(NV_PRMA, addr, 0);
    return 0;
}
static void prma_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    reg_log_write(NV_PRMA, addr, val);
}


static uint64_t pvideo_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    reg_log_read(NV_PVIDEO, addr, 0);
    return 0;
}
static void pvideo_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    reg_log_write(NV_PVIDEO, addr, val);
}




/* PIMTER - time measurement and time-based alarms */
static uint64_t ptimer_get_clock(NV2AState *d)
{
    return muldiv64(qemu_get_clock_ns(vm_clock),
                    d->pramdac.core_clock_freq * d->ptimer.numerator,
                    get_ticks_per_sec() * d->ptimer.denominator);
}
static uint64_t ptimer_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PTIMER_INTR_0:
        r = d->ptimer.pending_interrupts;
        break;
    case NV_PTIMER_INTR_EN_0:
        r = d->ptimer.enabled_interrupts;
        break;
    case NV_PTIMER_NUMERATOR:
        r = d->ptimer.numerator;
        break;
    case NV_PTIMER_DENOMINATOR:
        r = d->ptimer.denominator;
        break;
    case NV_PTIMER_TIME_0:
        r = (ptimer_get_clock(d) & 0x7ffffff) << 5;
        break;
    case NV_PTIMER_TIME_1:
        r = (ptimer_get_clock(d) >> 27) & 0x1fffffff;
        break;
    default:
        break;
    }

    reg_log_read(NV_PTIMER, addr, r);
    return r;
}
static void ptimer_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    reg_log_write(NV_PTIMER, addr, val);

    switch (addr) {
    case NV_PTIMER_INTR_0:
        d->ptimer.pending_interrupts &= ~val;
        update_irq(d);
        break;
    case NV_PTIMER_INTR_EN_0:
        d->ptimer.enabled_interrupts = val;
        update_irq(d);
        break;
    case NV_PTIMER_DENOMINATOR:
        d->ptimer.denominator = val;
        break;
    case NV_PTIMER_NUMERATOR:
        d->ptimer.numerator = val;
        break;
    case NV_PTIMER_ALARM_0:
        d->ptimer.alarm_time = val;
        break;
    default:
        break;
    }
}


static uint64_t pcounter_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    reg_log_read(NV_PCOUNTER, addr, 0);
    return 0;
}
static void pcounter_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    reg_log_write(NV_PCOUNTER, addr, val);
}


static uint64_t pvpe_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    reg_log_read(NV_PVPE, addr, 0);
    return 0;
}
static void pvpe_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    reg_log_write(NV_PVPE, addr, val);
}


static uint64_t ptv_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    reg_log_read(NV_PTV, addr, 0);
    return 0;
}
static void ptv_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    reg_log_write(NV_PTV, addr, val);
}


static uint64_t prmfb_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    reg_log_read(NV_PRMFB, addr, 0);
    return 0;
}
static void prmfb_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    reg_log_write(NV_PRMFB, addr, val);
}


/* PRMVIO - aliases VGA sequencer and graphics controller registers */
static uint64_t prmvio_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;
    uint64_t r = vga_ioport_read(&d->vga, addr);

    reg_log_read(NV_PRMVIO, addr, r);
    return r;
}
static void prmvio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    reg_log_write(NV_PRMVIO, addr, val);

    vga_ioport_write(&d->vga, addr, val);
}


static uint64_t pfb_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PFB_CFG0:
        /* 3-4 memory partitions. The debug bios checks this. */
        r = 3;
        break;
    case NV_PFB_CSTATUS:
        r = memory_region_size(d->vram);
        break;
    default:
        break;
    }

    reg_log_read(NV_PFB, addr, r);
    return r;
}
static void pfb_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    reg_log_write(NV_PFB, addr, val);
}


static uint64_t pstraps_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    reg_log_read(NV_PSTRAPS, addr, 0);
    return 0;
}
static void pstraps_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    reg_log_write(NV_PSTRAPS, addr, val);
}

/* PGRAPH - accelerated 2d/3d drawing engine */
static uint64_t pgraph_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PGRAPH_INTR:
        r = d->pgraph.pending_interrupts;
        break;
    case NV_PGRAPH_INTR_EN:
        r = d->pgraph.enabled_interrupts;
        break;
    case NV_PGRAPH_NSOURCE:
        r = d->pgraph.notify_source;
        break;
    case NV_PGRAPH_CTX_USER:
        qemu_mutex_lock(&d->pgraph.lock);
        SET_MASK(r, NV_PGRAPH_CTX_USER_CHANNEL_3D,
                 d->pgraph.context[d->pgraph.channel_id].channel_3d);
        SET_MASK(r, NV_PGRAPH_CTX_USER_CHANNEL_3D_VALID, 1);
        SET_MASK(r, NV_PGRAPH_CTX_USER_SUBCH, 
                 d->pgraph.context[d->pgraph.channel_id].subchannel << 13);
        SET_MASK(r, NV_PGRAPH_CTX_USER_CHID, d->pgraph.channel_id);
        qemu_mutex_unlock(&d->pgraph.lock);
        break;
    case NV_PGRAPH_TRAPPED_ADDR:
        SET_MASK(r, NV_PGRAPH_TRAPPED_ADDR_CHID, d->pgraph.trapped_channel_id);
        SET_MASK(r, NV_PGRAPH_TRAPPED_ADDR_SUBCH, d->pgraph.trapped_subchannel);
        SET_MASK(r, NV_PGRAPH_TRAPPED_ADDR_MTHD, d->pgraph.trapped_method);
        break;
    case NV_PGRAPH_TRAPPED_DATA_LOW:
        r = d->pgraph.trapped_data[0];
        break;
    case NV_PGRAPH_FIFO:
        SET_MASK(r, NV_PGRAPH_FIFO_ACCESS, d->pgraph.fifo_access);
        break;
    case NV_PGRAPH_CHANNEL_CTX_TABLE:
        r = d->pgraph.context_table >> 4;
        break;
    case NV_PGRAPH_CHANNEL_CTX_POINTER:
        r = d->pgraph.context_address >> 4;
        break;
    case NV_PGRAPH_COLORCLEARVALUE:
        r = d->pgraph.context[d->pgraph.channel_id].color_clear_value;
        break;
    case NV_PGRAPH_ZSTENCILCLEARVALUE:
        r = d->pgraph.context[d->pgraph.channel_id].zstencil_clear_value;
        break;
    default:
        break;
    }

    reg_log_read(NV_PGRAPH, addr, r);
    return r;
}
static void pgraph_set_context_user(NV2AState *d, uint32_t val)
{
    d->pgraph.channel_id = (val & NV_PGRAPH_CTX_USER_CHID) >> 24;

    d->pgraph.context[d->pgraph.channel_id].channel_3d =
        GET_MASK(val, NV_PGRAPH_CTX_USER_CHANNEL_3D);
    d->pgraph.context[d->pgraph.channel_id].subchannel =
        GET_MASK(val, NV_PGRAPH_CTX_USER_SUBCH);
}
static void pgraph_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    reg_log_write(NV_PGRAPH, addr, val);

    switch (addr) {
    case NV_PGRAPH_INTR:
        qemu_mutex_lock(&d->pgraph.lock);
        d->pgraph.pending_interrupts &= ~val;
        qemu_cond_broadcast(&d->pgraph.interrupt_cond);
        qemu_mutex_unlock(&d->pgraph.lock);
        break;
    case NV_PGRAPH_INTR_EN:
        d->pgraph.enabled_interrupts = val;
        break;
    case NV_PGRAPH_CTX_CONTROL:
        qemu_mutex_lock(&d->pgraph.lock);
        d->pgraph.channel_valid = (val & NV_PGRAPH_CTX_CONTROL_CHID);
        qemu_mutex_unlock(&d->pgraph.lock);
        break;
    case NV_PGRAPH_CTX_USER:
        qemu_mutex_lock(&d->pgraph.lock);
        pgraph_set_context_user(d, val);
        qemu_mutex_unlock(&d->pgraph.lock);
        break;
    case NV_PGRAPH_FIFO:
        qemu_mutex_lock(&d->pgraph.lock);
        d->pgraph.fifo_access = GET_MASK(val, NV_PGRAPH_FIFO_ACCESS);
        qemu_cond_broadcast(&d->pgraph.fifo_access_cond);
        qemu_mutex_unlock(&d->pgraph.lock);
        break;
    case NV_PGRAPH_CHANNEL_CTX_TABLE:
        d->pgraph.context_table =
            (val & NV_PGRAPH_CHANNEL_CTX_TABLE_INST) << 4;
        break;
    case NV_PGRAPH_CHANNEL_CTX_POINTER:
        d->pgraph.context_address =
            (val & NV_PGRAPH_CHANNEL_CTX_POINTER_INST) << 4;
        break;
    case NV_PGRAPH_CHANNEL_CTX_TRIGGER:
        qemu_mutex_lock(&d->pgraph.lock);

        if (val & NV_PGRAPH_CHANNEL_CTX_TRIGGER_READ_IN) {
            NV2A_DPRINTF("nv2a PGRAPH: read channel %d context from %llx\n",
                         d->pgraph.channel_id, d->pgraph.context_address);

            uint8_t *context_ptr = d->ramin_ptr + d->pgraph.context_address;
            uint32_t context_user = le32_to_cpupu((uint32_t*)context_ptr);

            NV2A_DPRINTF("    - CTX_USER = 0x%x\n", context_user);


            pgraph_set_context_user(d, context_user);
        }
        if (val & NV_PGRAPH_CHANNEL_CTX_TRIGGER_WRITE_OUT) {
            /* do stuff ... */
        }

        qemu_mutex_unlock(&d->pgraph.lock);
        break;
    case NV_PGRAPH_ZSTENCILCLEARVALUE:
        d->pgraph.context[d->pgraph.channel_id].zstencil_clear_value = val;
        break;
    default:
        break;
    }
}


static uint64_t pcrtc_read(void *opaque,
                                hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
        case NV_PCRTC_INTR_0:
            r = d->pcrtc.pending_interrupts;
            break;
        case NV_PCRTC_INTR_EN_0:
            r = d->pcrtc.enabled_interrupts;
            break;
        case NV_PCRTC_START:
            r = d->pcrtc.start;
            break;
        default:
            break;
    }

    reg_log_read(NV_PCRTC, addr, r);
    return r;
}
static void pcrtc_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    reg_log_write(NV_PCRTC, addr, val);

    switch (addr) {
    case NV_PCRTC_INTR_0:
        d->pcrtc.pending_interrupts &= ~val;
        update_irq(d);
        break;
    case NV_PCRTC_INTR_EN_0:
        d->pcrtc.enabled_interrupts = val;
        update_irq(d);
        break;
    case NV_PCRTC_START:
        val &= 0x03FFFFFF;
        assert(val < memory_region_size(d->vram));
        d->pcrtc.start = val;
        break;
    default:
        break;
    }
}


/* PRMCIO - aliases VGA CRTC and attribute controller registers */
static uint64_t prmcio_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;
    uint64_t r = vga_ioport_read(&d->vga, addr);

    reg_log_read(NV_PRMCIO, addr, r);
    return r;
}
static void prmcio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    reg_log_write(NV_PRMCIO, addr, val);

    switch (addr) {
    case VGA_ATT_W:
        /* Cromwell sets attrs without enabling VGA_AR_ENABLE_DISPLAY
         * (which should result in a blank screen).
         * Either nvidia's hardware is lenient or it is set through
         * something else. The former seems more likely.
         */
        if (d->vga.ar_flip_flop == 0) {
            val |= VGA_AR_ENABLE_DISPLAY;
        }
        break;
    default:
        break;
    }

    vga_ioport_write(&d->vga, addr, val);
}


static uint64_t pramdac_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr & ~3) {
    case NV_PRAMDAC_NVPLL_COEFF:
        r = d->pramdac.core_clock_coeff;
        break;
    case NV_PRAMDAC_MPLL_COEFF:
        r = d->pramdac.memory_clock_coeff;
        break;
    case NV_PRAMDAC_VPLL_COEFF:
        r = d->pramdac.video_clock_coeff;
        break;
    case NV_PRAMDAC_PLL_TEST_COUNTER:
        /* emulated PLLs locked instantly? */
        r = NV_PRAMDAC_PLL_TEST_COUNTER_VPLL2_LOCK
             | NV_PRAMDAC_PLL_TEST_COUNTER_NVPLL_LOCK
             | NV_PRAMDAC_PLL_TEST_COUNTER_MPLL_LOCK
             | NV_PRAMDAC_PLL_TEST_COUNTER_VPLL_LOCK;
        break;
    default:
        break;
    }

    /* Surprisingly, QEMU doesn't handle unaligned access for you properly */
    r >>= 32 - 8 * size - 8 * (addr & 3);

    NV2A_DPRINTF("nv2a PRAMDAC: read %d [0x%llx] -> %llx\n", size, addr, r);
    return r;
}
static void pramdac_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;
    uint32_t m, n, p;

    reg_log_write(NV_PRAMDAC, addr, val);

    switch (addr) {
    case NV_PRAMDAC_NVPLL_COEFF:
        d->pramdac.core_clock_coeff = val;

        m = val & NV_PRAMDAC_NVPLL_COEFF_MDIV;
        n = (val & NV_PRAMDAC_NVPLL_COEFF_NDIV) >> 8;
        p = (val & NV_PRAMDAC_NVPLL_COEFF_PDIV) >> 16;

        if (m == 0) {
            d->pramdac.core_clock_freq = 0;
        } else {
            d->pramdac.core_clock_freq = (NV2A_CRYSTAL_FREQ * n)
                                          / (1 << p) / m;
        }

        break;
    case NV_PRAMDAC_MPLL_COEFF:
        d->pramdac.memory_clock_coeff = val;
        break;
    case NV_PRAMDAC_VPLL_COEFF:
        d->pramdac.video_clock_coeff = val;
        break;
    default:
        break;
    }
}


static uint64_t prmdio_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    reg_log_read(NV_PRMDIO, addr, 0);
    return 0;
}
static void prmdio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    reg_log_write(NV_PRMDIO, addr, val);
}


/* PRAMIN - RAMIN access */
/*
static uint64_t pramin_read(void *opaque,
                                 hwaddr addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRAMIN: read [0x%llx] -> 0x%llx\n", addr, r);
    return 0;
}
static void pramin_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRAMIN: [0x%llx] = 0x%02llx\n", addr, val);
}*/


/* USER - PFIFO MMIO and DMA submission area */
static uint64_t user_read(void *opaque,
                               hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    unsigned int channel_id = addr >> 16;
    assert(channel_id < NV2A_NUM_CHANNELS);

    ChannelControl *control = &d->user.channel_control[channel_id];

    uint64_t r = 0;
    if (d->pfifo.channel_modes & (1 << channel_id)) {
        /* DMA Mode */
        switch (addr & 0xFFFF) {
        case NV_USER_DMA_PUT:
            r = control->dma_put;
            break;
        case NV_USER_DMA_GET:
            r = control->dma_get;
            break;
        case NV_USER_REF:
            r = control->ref;
            break;
        default:
            break;
        }
    } else {
        /* PIO Mode */
        /* dunno */
    }

    reg_log_read(NV_USER, addr, r);
    return r;
}
static void user_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    reg_log_write(NV_USER, addr, val);

    unsigned int channel_id = addr >> 16;
    assert(channel_id < NV2A_NUM_CHANNELS);

    ChannelControl *control = &d->user.channel_control[channel_id];

    if (d->pfifo.channel_modes & (1 << channel_id)) {
        /* DMA Mode */
        switch (addr & 0xFFFF) {
        case NV_USER_DMA_PUT:
            control->dma_put = val;

            if (d->pfifo.cache1.push_enabled) {
                pfifo_run_pusher(d);
            }
            break;
        case NV_USER_DMA_GET:
            control->dma_get = val;
            break;
        case NV_USER_REF:
            control->ref = val;
            break;
        default:
            break;
        }
    } else {
        /* PIO Mode */
        assert(false);
    }

}




typedef struct NV2ABlockInfo {
    const char* name;
    hwaddr offset;
    uint64_t size;
    MemoryRegionOps ops;
} NV2ABlockInfo;

static const struct NV2ABlockInfo blocktable[] = {
    [ NV_PMC ]  = {
        .name = "PMC",
        .offset = 0x000000,
        .size   = 0x001000,
        .ops = {
            .read = pmc_read,
            .write = pmc_write,
        },
    },
    [ NV_PBUS ]  = {
        .name = "PBUS",
        .offset = 0x001000,
        .size   = 0x001000,
        .ops = {
            .read = pbus_read,
            .write = pbus_write,
        },
    },
    [ NV_PFIFO ]  = {
        .name = "PFIFO",
        .offset = 0x002000,
        .size   = 0x002000,
        .ops = {
            .read = pfifo_read,
            .write = pfifo_write,
        },
    },
    [ NV_PRMA ]  = {
        .name = "PRMA",
        .offset = 0x007000,
        .size   = 0x001000,
        .ops = {
            .read = prma_read,
            .write = prma_write,
        },
    },
    [ NV_PVIDEO ]  = {
        .name = "PVIDEO",
        .offset = 0x008000,
        .size   = 0x001000,
        .ops = {
            .read = pvideo_read,
            .write = pvideo_write,
        },
    },
    [ NV_PTIMER ]  = {
        .name = "PTIMER",
        .offset = 0x009000,
        .size   = 0x001000,
        .ops = {
            .read = ptimer_read,
            .write = ptimer_write,
        },
    },
    [ NV_PCOUNTER ]  = {
        .name = "PCOUNTER",
        .offset = 0x00a000,
        .size   = 0x001000,
        .ops = {
            .read = pcounter_read,
            .write = pcounter_write,
        },
    },
    [ NV_PVPE ]  = {
        .name = "PVPE",
        .offset = 0x00b000,
        .size   = 0x001000,
        .ops = {
            .read = pvpe_read,
            .write = pvpe_write,
        },
    },
    [ NV_PTV ]  = {
        .name = "PTV",
        .offset = 0x00d000,
        .size   = 0x001000,
        .ops = {
            .read = ptv_read,
            .write = ptv_write,
        },
    },
    [ NV_PRMFB ]  = {
        .name = "PRMFB",
        .offset = 0x0a0000,
        .size   = 0x020000,
        .ops = {
            .read = prmfb_read,
            .write = prmfb_write,
        },
    },
    [ NV_PRMVIO ]  = {
        .name = "PRMVIO",
        .offset = 0x0c0000,
        .size   = 0x001000,
        .ops = {
            .read = prmvio_read,
            .write = prmvio_write,
        },
    },
    [ NV_PFB ]  = {
        .name = "PFB",
        .offset = 0x100000,
        .size   = 0x001000,
        .ops = {
            .read = pfb_read,
            .write = pfb_write,
        },
    },
    [ NV_PSTRAPS ]  = {
        .name = "PSTRAPS",
        .offset = 0x101000,
        .size   = 0x001000,
        .ops = {
            .read = pstraps_read,
            .write = pstraps_write,
        },
    },
    [ NV_PGRAPH ]  = {
        .name = "PGRAPH",
        .offset = 0x400000,
        .size   = 0x002000,
        .ops = {
            .read = pgraph_read,
            .write = pgraph_write,
        },
    },
    [ NV_PCRTC ]  = {
        .name = "PCRTC",
        .offset = 0x600000,
        .size   = 0x001000,
        .ops = {
            .read = pcrtc_read,
            .write = pcrtc_write,
        },
    },
    [ NV_PRMCIO ]  = {
        .name = "PRMCIO",
        .offset = 0x601000,
        .size   = 0x001000,
        .ops = {
            .read = prmcio_read,
            .write = prmcio_write,
        },
    },
    [ NV_PRAMDAC ]  = {
        .name = "PRAMDAC",
        .offset = 0x680000,
        .size   = 0x001000,
        .ops = {
            .read = pramdac_read,
            .write = pramdac_write,
        },
    },
    [ NV_PRMDIO ]  = {
        .name = "PRMDIO",
        .offset = 0x681000,
        .size   = 0x001000,
        .ops = {
            .read = prmdio_read,
            .write = prmdio_write,
        },
    },
    /*[ NV_PRAMIN ]  = {
        .name = "PRAMIN",
        .offset = 0x700000,
        .size   = 0x100000,
        .ops = {
            .read = pramin_read,
            .write = pramin_write,
        },
    },*/
    [ NV_USER ]  = {
        .name = "USER",
        .offset = 0x800000,
        .size   = 0x800000,
        .ops = {
            .read = user_read,
            .write = user_write,
        },
    },
};

static const char* nv2a_reg_names[] = {};
static const char* nv2a_method_names[] = {};

static void reg_log_read(int block, hwaddr addr, uint64_t val) {
    if (blocktable[block].name) {
        hwaddr naddr = blocktable[block].offset + addr;
        if (naddr < sizeof(nv2a_reg_names)/sizeof(const char*)
                && nv2a_reg_names[naddr]) {
            NV2A_DPRINTF("nv2a %s: read [%s] -> 0x%" PRIx64 "\n",
                    blocktable[block].name, nv2a_reg_names[naddr], val);
        } else {
            NV2A_DPRINTF("nv2a %s: read [" TARGET_FMT_plx "] -> 0x%" PRIx64 "\n",
                    blocktable[block].name, addr, val);
        }
    } else {
        NV2A_DPRINTF("nv2a (%d?): read [" TARGET_FMT_plx "] -> 0x%" PRIx64 "\n",
                block, addr, val);
    }
}

static void reg_log_write(int block, hwaddr addr, uint64_t val) {
    if (blocktable[block].name) {
        hwaddr naddr = blocktable[block].offset + addr;
        if (naddr < sizeof(nv2a_reg_names)/sizeof(const char*)
                && nv2a_reg_names[naddr]) {
            NV2A_DPRINTF("nv2a %s: [%s] = 0x%" PRIx64 "\n",
                    blocktable[block].name, nv2a_reg_names[naddr], val);
        } else {
            NV2A_DPRINTF("nv2a %s: [" TARGET_FMT_plx "] = 0x%" PRIx64 "\n",
                    blocktable[block].name, addr, val);
        }
    } else {
        NV2A_DPRINTF("nv2a (%d?): [" TARGET_FMT_plx "] = 0x%" PRIx64 "\n",
                block, addr, val);
    }
}
static void pgraph_method_log(unsigned int subchannel,
                              unsigned int graphics_class,
                              unsigned int method, uint32_t parameter) {
    static unsigned int last = 0;
    static unsigned int count = 0;
    if (last == 0x1800 && method != last) {
        NV2A_DPRINTF("nv2a pgraph method (%d) 0x%x * %d\n",
                        subchannel, last, count);  
    }
    if (method != 0x1800) {
        const char* method_name = NULL;
        unsigned int nmethod = 0;
        switch (graphics_class) {
            case NV_KELVIN_PRIMITIVE:
                nmethod = method | (0x5c << 16);
                break;
            case NV_CONTEXT_SURFACES_2D:
                nmethod = method | (0x6d << 16);
                break;
            default:
                break;
        }
        if (nmethod != 0
            && nmethod < sizeof(nv2a_method_names)/sizeof(const char*)) {
            method_name = nv2a_method_names[nmethod];
        }
        if (method_name) {
            NV2A_DPRINTF("nv2a pgraph method (%d): %s (0x%x)\n",
                     subchannel, method_name, parameter);
        } else {
            NV2A_DPRINTF("nv2a pgraph method (%d): 0x%x -> 0x%04x (0x%x)\n",
                     subchannel, graphics_class, method, parameter);
        }

    }
    if (method == last) { count++; }
    else {count = 0; }
    last = method;
}


static int nv2a_get_bpp(VGACommonState *s)
{
    if ((s->cr[0x28] & 3) == 3) {
        return 32;
    }
    return (s->cr[0x28] & 3) * 8;
}

static void nv2a_get_offsets(VGACommonState *s,
                             uint32_t *pline_offset,
                             uint32_t *pstart_addr,
                             uint32_t *pline_compare)
{
    NV2AState *d = container_of(s, NV2AState, vga);
    uint32_t start_addr, line_offset, line_compare;

    line_offset = s->cr[0x13]
        | ((s->cr[0x19] & 0xe0) << 3)
        | ((s->cr[0x25] & 0x20) << 6);
    line_offset <<= 3;
    *pline_offset = line_offset;

    start_addr = d->pcrtc.start / 4;
    *pstart_addr = start_addr;

    line_compare = s->cr[VGA_CRTC_LINE_COMPARE] |
        ((s->cr[VGA_CRTC_OVERFLOW] & 0x10) << 4) |
        ((s->cr[VGA_CRTC_MAX_SCAN] & 0x40) << 3);
    *pline_compare = line_compare;
}


static void nv2a_vga_gfx_update(void *opaque)
{
    VGACommonState *vga = opaque;
    vga->hw_ops->gfx_update(vga);

    NV2AState *d = container_of(vga, NV2AState, vga);
    d->pcrtc.pending_interrupts |= NV_PCRTC_INTR_0_VBLANK;
    update_irq(d);
}

static void nv2a_init_memory(NV2AState *d, MemoryRegion *ram)
{
    /* xbox is UMA - vram *is* ram */
    d->vram = ram;

     /* PCI exposed vram */
    memory_region_init_alias(&d->vram_pci, OBJECT(d), "nv2a-vram-pci", d->vram,
                             0, memory_region_size(d->vram));
    pci_register_bar(&d->dev, 1, PCI_BASE_ADDRESS_MEM_PREFETCH, &d->vram_pci);


    /* RAMIN - should be in vram somewhere, but not quite sure where atm */
    memory_region_init_ram(&d->ramin, OBJECT(d), "nv2a-ramin", 0x100000);
    /* memory_region_init_alias(&d->ramin, "nv2a-ramin", &d->vram,
                         memory_region_size(&d->vram) - 0x100000,
                         0x100000); */

    memory_region_add_subregion(&d->mmio, 0x700000, &d->ramin);


    d->vram_ptr = memory_region_get_ram_ptr(d->vram);
    d->ramin_ptr = memory_region_get_ram_ptr(&d->ramin);

    /* hacky. swap out vga's vram */
    memory_region_destroy(&d->vga.vram);
    memory_region_init_alias(&d->vga.vram, OBJECT(d), "vga.vram",
                             d->vram, 0, memory_region_size(d->vram));
    d->vga.vram_ptr = memory_region_get_ram_ptr(&d->vga.vram);
    vga_dirty_log_start(&d->vga);
}

static int nv2a_initfn(PCIDevice *dev)
{
    int i;
    NV2AState *d;

    d = NV2A_DEVICE(dev);

    d->pcrtc.start = 0;

    d->pramdac.core_clock_coeff = 0x00011c01; /* 189MHz...? */
    d->pramdac.core_clock_freq = 189000000;
    d->pramdac.memory_clock_coeff = 0;
    d->pramdac.video_clock_coeff = 0x0003C20D; /* 25182Khz...? */



    /* legacy VGA shit */
    VGACommonState *vga = &d->vga;
    vga->vram_size_mb = 4;
    /* seems to start in color mode */
    vga->msr = VGA_MIS_COLOR;

    vga_common_init(vga, OBJECT(dev));
    vga->get_bpp = nv2a_get_bpp;
    vga->get_offsets = nv2a_get_offsets;

    d->hw_ops = *vga->hw_ops;
    d->hw_ops.gfx_update = nv2a_vga_gfx_update;
    vga->con = graphic_console_init(DEVICE(dev), &d->hw_ops, vga);


    /* mmio */
    memory_region_init(&d->mmio, OBJECT(dev), "nv2a-mmio", 0x1000000);
    pci_register_bar(&d->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);

    for (i=0; i<sizeof(blocktable)/sizeof(blocktable[0]); i++) {
        if (!blocktable[i].name) continue;
        memory_region_init_io(&d->block_mmio[i], OBJECT(dev),
                              &blocktable[i].ops, d,
                              blocktable[i].name, blocktable[i].size);
        memory_region_add_subregion(&d->mmio, blocktable[i].offset,
                                    &d->block_mmio[i]);
    }

    /* init fifo cache1 */
    qemu_mutex_init(&d->pfifo.cache1.pull_lock);
    qemu_mutex_init(&d->pfifo.cache1.cache_lock);
    qemu_cond_init(&d->pfifo.cache1.cache_cond);
    QSIMPLEQ_INIT(&d->pfifo.cache1.cache);

    qemu_mutex_init(&d->pgraph.lock);
    qemu_cond_init(&d->pgraph.interrupt_cond);
    qemu_cond_init(&d->pgraph.fifo_access_cond);

    /* fire up graphics contexts */
    for (i=0; i<NV2A_NUM_CHANNELS; i++) {
        pgraph_context_init(&d->pgraph.context[i]);
    }

    return 0;
}

static void nv2a_exitfn(PCIDevice *dev)
{
    int i;
    NV2AState *d;
    d = NV2A_DEVICE(dev);

    qemu_mutex_destroy(&d->pfifo.cache1.pull_lock);
    qemu_mutex_destroy(&d->pfifo.cache1.cache_lock);
    qemu_cond_destroy(&d->pfifo.cache1.cache_cond);

    qemu_mutex_destroy(&d->pgraph.lock);
    qemu_cond_destroy(&d->pgraph.interrupt_cond);
    qemu_cond_destroy(&d->pgraph.fifo_access_cond);

    for (i=0; i<NV2A_NUM_CHANNELS; i++) {
        pgraph_context_destroy(&d->pgraph.context[i]);
    }
}

static void nv2a_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_NVIDIA;
    k->device_id = PCI_DEVICE_ID_NVIDIA_GEFORCE_NV2A;
    k->revision = 161;
    k->class_id = PCI_CLASS_DISPLAY_3D;
    k->init = nv2a_initfn;
    k->exit = nv2a_exitfn;

    dc->desc = "GeForce NV2A Integrated Graphics";
}

static const TypeInfo nv2a_info = {
    .name          = "nv2a",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(NV2AState),
    .class_init    = nv2a_class_init,
};

static void nv2a_register(void)
{
    type_register_static(&nv2a_info);
}
type_init(nv2a_register);





void nv2a_init(PCIBus *bus, int devfn, qemu_irq irq, MemoryRegion *ram)
{
    PCIDevice *dev;
    NV2AState *d;
    dev = pci_create_simple(bus, devfn, "nv2a");
    d = NV2A_DEVICE(dev);

    nv2a_init_memory(d, ram);

    d->irq = irq;
}