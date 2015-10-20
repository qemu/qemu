/*
 * QEMU Geforce NV2A implementation
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
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
#include "ui/console.h"
#include "hw/display/vga.h"
#include "hw/display/vga_int.h"
#include "qemu/queue.h"
#include "qemu/thread.h"
#include "qapi/qmp/qstring.h"
#include "gl/gloffscreen.h"
#include "gl/glextensions.h"

#include "hw/xbox/g-lru-cache.h"
#include "hw/xbox/swizzle.h"
#include "hw/xbox/nv2a_shaders.h"
#include "hw/xbox/nv2a_debug.h"

#include "hw/xbox/nv2a.h"

#define USE_TEXTURE_CACHE

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
#define NV_PGRAPH_SURFACE                                0x00000710
#   define NV_PGRAPH_SURFACE_WRITE_3D                         0x00700000
#   define NV_PGRAPH_SURFACE_READ_3D                          0x07000000
#   define NV_PGRAPH_SURFACE_MODULO_3D                        0x70000000
#define NV_PGRAPH_INCREMENT                              0x0000071C
#   define NV_PGRAPH_INCREMENT_READ_BLIT                        (1 << 0)
#   define NV_PGRAPH_INCREMENT_READ_3D                          (1 << 1)
#define NV_PGRAPH_FIFO                                   0x00000720
#   define NV_PGRAPH_FIFO_ACCESS                                (1 << 0)
#define NV_PGRAPH_CHANNEL_CTX_TABLE                      0x00000780
#   define NV_PGRAPH_CHANNEL_CTX_TABLE_INST                   0x0000FFFF
#define NV_PGRAPH_CHANNEL_CTX_POINTER                    0x00000784
#   define NV_PGRAPH_CHANNEL_CTX_POINTER_INST                 0x0000FFFF
#define NV_PGRAPH_CHANNEL_CTX_TRIGGER                    0x00000788
#   define NV_PGRAPH_CHANNEL_CTX_TRIGGER_READ_IN                (1 << 0)
#   define NV_PGRAPH_CHANNEL_CTX_TRIGGER_WRITE_OUT              (1 << 1)
#define NV_PGRAPH_CSV0_D                                 0x00000FB4
#   define NV_PGRAPH_CSV0_D_LIGHTS                              0x0000FFFF
#   define NV_PGRAPH_CSV0_D_LIGHT0                              0x00000003
#       define NV_PGRAPH_CSV0_D_LIGHT0_OFF                          0
#       define NV_PGRAPH_CSV0_D_LIGHT0_INFINITE                     1
#       define NV_PGRAPH_CSV0_D_LIGHT0_LOCAL                        2
#       define NV_PGRAPH_CSV0_D_LIGHT0_SPOT                         3
#   define NV_PGRAPH_CSV0_D_RANGE_MODE                          (1 << 18)
#   define NV_PGRAPH_CSV0_D_FOGENABLE                           (1 << 19)
#   define NV_PGRAPH_CSV0_D_TEXGEN_REF                          (1 << 20)
#       define NV_PGRAPH_CSV0_D_TEXGEN_REF_LOCAL_VIEWER             0
#       define NV_PGRAPH_CSV0_D_TEXGEN_REF_INFINITE_VIEWER          1
#   define NV_PGRAPH_CSV0_D_FOG_MODE                            (1 << 21)
#       define NV_PGRAPH_CSV0_D_FOG_MODE_LINEAR                     0
#       define NV_PGRAPH_CSV0_D_FOG_MODE_EXP                        1
#   define NV_PGRAPH_CSV0_D_FOGGENMODE                          0x01C00000
#       define NV_PGRAPH_CSV0_D_FOGGENMODE_SPEC_ALPHA               0
#       define NV_PGRAPH_CSV0_D_FOGGENMODE_RADIAL                   1
#       define NV_PGRAPH_CSV0_D_FOGGENMODE_PLANAR                   2
#       define NV_PGRAPH_CSV0_D_FOGGENMODE_ABS_PLANAR               3
#       define NV_PGRAPH_CSV0_D_FOGGENMODE_FOG_X                    4
#   define NV_PGRAPH_CSV0_D_MODE                                0xC0000000
#   define NV_PGRAPH_CSV0_D_SKIN                                0x1C000000
#       define NV_PGRAPH_CSV0_D_SKIN_OFF                            0
#       define NV_PGRAPH_CSV0_D_SKIN_2G                             1
#       define NV_PGRAPH_CSV0_D_SKIN_2                              2
#       define NV_PGRAPH_CSV0_D_SKIN_3G                             3
#       define NV_PGRAPH_CSV0_D_SKIN_3                              4
#       define NV_PGRAPH_CSV0_D_SKIN_4G                             5
#       define NV_PGRAPH_CSV0_D_SKIN_4                              6
#define NV_PGRAPH_CSV0_C                                 0x00000FB8
#   define NV_PGRAPH_CSV0_C_CHEOPS_PROGRAM_START                0x0000FF00
#   define NV_PGRAPH_CSV0_C_NORMALIZATION_ENABLE                (1 << 27)
#   define NV_PGRAPH_CSV0_C_LIGHTING                            (1 << 31)
#define NV_PGRAPH_CSV1_B                                 0x00000FBC
#define NV_PGRAPH_CSV1_A                                 0x00000FC0
#   define NV_PGRAPH_CSV1_A_T0_ENABLE                           (1 << 0)
#   define NV_PGRAPH_CSV1_A_T0_MODE                             (1 << 1)
#   define NV_PGRAPH_CSV1_A_T0_TEXTURE                          (1 << 2)
#       define NV_PGRAPH_CSV1_A_T0_TEXTURE_2D                       0
#       define NV_PGRAPH_CSV1_A_T0_TEXTURE_3D                       1
#   define NV_PGRAPH_CSV1_A_T0_S                                0x00000070
#       define NV_PGRAPH_CSV1_A_T0_S_DISABLE                        0
#       define NV_PGRAPH_CSV1_A_T0_S_NORMAL_MAP                     4
#       define NV_PGRAPH_CSV1_A_T0_S_REFLECTION_MAP                 5
#       define NV_PGRAPH_CSV1_A_T0_S_EYE_LINEAR                     1
#       define NV_PGRAPH_CSV1_A_T0_S_OBJECT_LINEAR                  2
#       define NV_PGRAPH_CSV1_A_T0_S_SPHERE_MAP                     3
#   define NV_PGRAPH_CSV1_A_T0_T                                0x00000380
#   define NV_PGRAPH_CSV1_A_T0_R                                0x00001C00
#   define NV_PGRAPH_CSV1_A_T0_Q                                0x0000E000
#   define NV_PGRAPH_CSV1_A_T1_ENABLE                           (1 << 16)
#   define NV_PGRAPH_CSV1_A_T1_MODE                             (1 << 17)
#   define NV_PGRAPH_CSV1_A_T1_TEXTURE                          (1 << 18)
#   define NV_PGRAPH_CSV1_A_T1_S                                0x00700000
#   define NV_PGRAPH_CSV1_A_T1_T                                0x03800000
#   define NV_PGRAPH_CSV1_A_T1_R                                0x1C000000
#   define NV_PGRAPH_CSV1_A_T1_Q                                0xE0000000
#define NV_PGRAPH_CHEOPS_OFFSET                          0x00000FC4
#   define NV_PGRAPH_CHEOPS_OFFSET_PROG_LD_PTR                  0x000000FF
#   define NV_PGRAPH_CHEOPS_OFFSET_CONST_LD_PTR                 0x0000FF00
#define NV_PGRAPH_BLEND                                  0x00001804
#   define NV_PGRAPH_BLEND_EQN                                  0x00000007
#   define NV_PGRAPH_BLEND_EN                                   (1 << 3)
#   define NV_PGRAPH_BLEND_SFACTOR                              0x000000F0
#       define NV_PGRAPH_BLEND_SFACTOR_ZERO                         0
#       define NV_PGRAPH_BLEND_SFACTOR_ONE                          1
#       define NV_PGRAPH_BLEND_SFACTOR_SRC_COLOR                    2
#       define NV_PGRAPH_BLEND_SFACTOR_ONE_MINUS_SRC_COLOR          3
#       define NV_PGRAPH_BLEND_SFACTOR_SRC_ALPHA                    4
#       define NV_PGRAPH_BLEND_SFACTOR_ONE_MINUS_SRC_ALPHA          5
#       define NV_PGRAPH_BLEND_SFACTOR_DST_ALPHA                    6
#       define NV_PGRAPH_BLEND_SFACTOR_ONE_MINUS_DST_ALPHA          7
#       define NV_PGRAPH_BLEND_SFACTOR_DST_COLOR                    8
#       define NV_PGRAPH_BLEND_SFACTOR_ONE_MINUS_DST_COLOR          9
#       define NV_PGRAPH_BLEND_SFACTOR_SRC_ALPHA_SATURATE           10
#       define NV_PGRAPH_BLEND_SFACTOR_CONSTANT_COLOR               12
#       define NV_PGRAPH_BLEND_SFACTOR_ONE_MINUS_CONSTANT_COLOR     13
#       define NV_PGRAPH_BLEND_SFACTOR_CONSTANT_ALPHA               14
#       define NV_PGRAPH_BLEND_SFACTOR_ONE_MINUS_CONSTANT_ALPHA     15
#   define NV_PGRAPH_BLEND_DFACTOR                              0x00000F00
#       define NV_PGRAPH_BLEND_DFACTOR_ZERO                         0
#       define NV_PGRAPH_BLEND_DFACTOR_ONE                          1
#       define NV_PGRAPH_BLEND_DFACTOR_SRC_COLOR                    2
#       define NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_SRC_COLOR          3
#       define NV_PGRAPH_BLEND_DFACTOR_SRC_ALPHA                    4
#       define NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_SRC_ALPHA          5
#       define NV_PGRAPH_BLEND_DFACTOR_DST_ALPHA                    6
#       define NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_DST_ALPHA          7
#       define NV_PGRAPH_BLEND_DFACTOR_DST_COLOR                    8
#       define NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_DST_COLOR          9
#       define NV_PGRAPH_BLEND_DFACTOR_SRC_ALPHA_SATURATE           10
#       define NV_PGRAPH_BLEND_DFACTOR_CONSTANT_COLOR               12
#       define NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_CONSTANT_COLOR     13
#       define NV_PGRAPH_BLEND_DFACTOR_CONSTANT_ALPHA               14
#       define NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_CONSTANT_ALPHA     15
#   define NV_PGRAPH_BLEND_LOGICOP_ENABLE                       (1 << 16)
#   define NV_PGRAPH_BLEND_LOGICOP                              0x0000F000
#define NV_PGRAPH_BLENDCOLOR                             0x00001808
#define NV_PGRAPH_BORDERCOLOR0                           0x0000180C
#define NV_PGRAPH_BORDERCOLOR1                           0x00001810
#define NV_PGRAPH_BORDERCOLOR2                           0x00001814
#define NV_PGRAPH_BORDERCOLOR3                           0x00001818
#define NV_PGRAPH_BUMPOFFSET1                            0x0000184C
#define NV_PGRAPH_BUMPSCALE1                             0x00001858
#define NV_PGRAPH_CLEARRECTX                             0x00001864
#       define NV_PGRAPH_CLEARRECTX_XMIN                          0x00000FFF
#       define NV_PGRAPH_CLEARRECTX_XMAX                          0x0FFF0000
#define NV_PGRAPH_CLEARRECTY                             0x00001868
#       define NV_PGRAPH_CLEARRECTY_YMIN                          0x00000FFF
#       define NV_PGRAPH_CLEARRECTY_YMAX                          0x0FFF0000
#define NV_PGRAPH_COLORCLEARVALUE                        0x0000186C
#define NV_PGRAPH_COMBINEFACTOR0                         0x00001880
#define NV_PGRAPH_COMBINEFACTOR1                         0x000018A0
#define NV_PGRAPH_COMBINEALPHAI0                         0x000018C0
#define NV_PGRAPH_COMBINEALPHAO0                         0x000018E0
#define NV_PGRAPH_COMBINECOLORI0                         0x00001900
#define NV_PGRAPH_COMBINECOLORO0                         0x00001920
#define NV_PGRAPH_COMBINECTL                             0x00001940
#define NV_PGRAPH_COMBINESPECFOG0                        0x00001944
#define NV_PGRAPH_COMBINESPECFOG1                        0x00001948
#define NV_PGRAPH_CONTROL_0                              0x0000194C
#   define NV_PGRAPH_CONTROL_0_ALPHAREF                         0x000000FF
#   define NV_PGRAPH_CONTROL_0_ALPHAFUNC                        0x00000F00
#   define NV_PGRAPH_CONTROL_0_ALPHATESTENABLE                  (1 << 12)
#   define NV_PGRAPH_CONTROL_0_ZENABLE                          (1 << 14)
#   define NV_PGRAPH_CONTROL_0_ZFUNC                            0x000F0000
#       define NV_PGRAPH_CONTROL_0_ZFUNC_NEVER                      0
#       define NV_PGRAPH_CONTROL_0_ZFUNC_LESS                       1
#       define NV_PGRAPH_CONTROL_0_ZFUNC_EQUAL                      2
#       define NV_PGRAPH_CONTROL_0_ZFUNC_LEQUAL                     3
#       define NV_PGRAPH_CONTROL_0_ZFUNC_GREATER                    4
#       define NV_PGRAPH_CONTROL_0_ZFUNC_NOTEQUAL                   5
#       define NV_PGRAPH_CONTROL_0_ZFUNC_GEQUAL                     6
#       define NV_PGRAPH_CONTROL_0_ZFUNC_ALWAYS                     7
#   define NV_PGRAPH_CONTROL_0_DITHERENABLE                     (1 << 22)
#   define NV_PGRAPH_CONTROL_0_Z_PERSPECTIVE_ENABLE             (1 << 23)
#   define NV_PGRAPH_CONTROL_0_ZWRITEENABLE                     (1 << 24)
#   define NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE             (1 << 25)
#   define NV_PGRAPH_CONTROL_0_ALPHA_WRITE_ENABLE               (1 << 26)
#   define NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE                 (1 << 27)
#   define NV_PGRAPH_CONTROL_0_GREEN_WRITE_ENABLE               (1 << 28)
#   define NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE                (1 << 29)
#define NV_PGRAPH_CONTROL_1                              0x00001950
#   define NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE              (1 << 0)
#   define NV_PGRAPH_CONTROL_1_STENCIL_FUNC                     0x000000F0
#       define NV_PGRAPH_CONTROL_1_STENCIL_FUNC_NEVER               0
#       define NV_PGRAPH_CONTROL_1_STENCIL_FUNC_LESS                1
#       define NV_PGRAPH_CONTROL_1_STENCIL_FUNC_EQUAL               2
#       define NV_PGRAPH_CONTROL_1_STENCIL_FUNC_LEQUAL              3
#       define NV_PGRAPH_CONTROL_1_STENCIL_FUNC_GREATER             4
#       define NV_PGRAPH_CONTROL_1_STENCIL_FUNC_NOTEQUAL            5
#       define NV_PGRAPH_CONTROL_1_STENCIL_FUNC_GEQUAL              6
#       define NV_PGRAPH_CONTROL_1_STENCIL_FUNC_ALWAYS              7
#   define NV_PGRAPH_CONTROL_1_STENCIL_REF                      0x0000FF00
#   define NV_PGRAPH_CONTROL_1_STENCIL_MASK_READ                0x00FF0000
#   define NV_PGRAPH_CONTROL_1_STENCIL_MASK_WRITE               0xFF000000
#define NV_PGRAPH_CONTROL_2                              0x00001954
#   define NV_PGRAPH_CONTROL_2_STENCIL_OP_FAIL                  0x0000000F
#   define NV_PGRAPH_CONTROL_2_STENCIL_OP_ZFAIL                 0x000000F0
#   define NV_PGRAPH_CONTROL_2_STENCIL_OP_ZPASS                 0x00000F00
#       define NV_PGRAPH_CONTROL_2_STENCIL_OP_V_KEEP                1
#       define NV_PGRAPH_CONTROL_2_STENCIL_OP_V_ZERO                2
#       define NV_PGRAPH_CONTROL_2_STENCIL_OP_V_REPLACE             3
#       define NV_PGRAPH_CONTROL_2_STENCIL_OP_V_INCRSAT             4
#       define NV_PGRAPH_CONTROL_2_STENCIL_OP_V_DECRSAT             5
#       define NV_PGRAPH_CONTROL_2_STENCIL_OP_V_INVERT              6
#       define NV_PGRAPH_CONTROL_2_STENCIL_OP_V_INCR                7
#       define NV_PGRAPH_CONTROL_2_STENCIL_OP_V_DECR                8
#define NV_PGRAPH_CONTROL_3                              0x00001958
#   define NV_PGRAPH_CONTROL_3_FOGENABLE                        (1 << 8)
#   define NV_PGRAPH_CONTROL_3_FOG_MODE                         0x00070000
#       define NV_PGRAPH_CONTROL_3_FOG_MODE_LINEAR                  0
#       define NV_PGRAPH_CONTROL_3_FOG_MODE_EXP                     1
#       define NV_PGRAPH_CONTROL_3_FOG_MODE_EXP2                    3
#       define NV_PGRAPH_CONTROL_3_FOG_MODE_EXP_ABS                 5
#       define NV_PGRAPH_CONTROL_3_FOG_MODE_EXP2_ABS                7
#       define NV_PGRAPH_CONTROL_3_FOG_MODE_LINEAR_ABS              4
#define NV_PGRAPH_FOGCOLOR                               0x00001980
#   define NV_PGRAPH_FOGCOLOR_RED                               0x00FF0000
#   define NV_PGRAPH_FOGCOLOR_GREEN                             0x0000FF00
#   define NV_PGRAPH_FOGCOLOR_BLUE                              0x000000FF
#   define NV_PGRAPH_FOGCOLOR_ALPHA                             0xFF000000
#define NV_PGRAPH_FOGPARAM0                              0x00001984
#define NV_PGRAPH_FOGPARAM1                              0x00001988
#define NV_PGRAPH_SETUPRASTER                            0x00001990
#   define NV_PGRAPH_SETUPRASTER_FRONTFACEMODE                  0x00000003
#       define NV_PGRAPH_SETUPRASTER_FRONTFACEMODE_FILL             0
#       define NV_PGRAPH_SETUPRASTER_FRONTFACEMODE_POINT            1
#       define NV_PGRAPH_SETUPRASTER_FRONTFACEMODE_LINE             2
#   define NV_PGRAPH_SETUPRASTER_BACKFACEMODE                   0x0000000C
#   define NV_PGRAPH_SETUPRASTER_POFFSETPOINTENABLE             (1 << 6)
#   define NV_PGRAPH_SETUPRASTER_POFFSETLINEENABLE              (1 << 7)
#   define NV_PGRAPH_SETUPRASTER_POFFSETFILLENABLE              (1 << 8)
#   define NV_PGRAPH_SETUPRASTER_CULLCTRL                       0x00600000
#       define NV_PGRAPH_SETUPRASTER_CULLCTRL_FRONT                 1
#       define NV_PGRAPH_SETUPRASTER_CULLCTRL_BACK                  2
#       define NV_PGRAPH_SETUPRASTER_CULLCTRL_FRONT_AND_BACK        3
#   define NV_PGRAPH_SETUPRASTER_FRONTFACE                      (1 << 23)
#   define NV_PGRAPH_SETUPRASTER_CULLENABLE                     (1 << 28)
#   define NV_PGRAPH_SETUPRASTER_Z_FORMAT                       (1 << 29)
#define NV_PGRAPH_SHADERCLIPMODE                         0x00001994
#define NV_PGRAPH_SHADERCTL                              0x00001998
#define NV_PGRAPH_SHADERPROG                             0x0000199C
#define NV_PGRAPH_SHADOWZSLOPETHRESHOLD                  0x000019A8
#define NV_PGRAPH_SPECFOGFACTOR0                         0x000019AC
#define NV_PGRAPH_SPECFOGFACTOR1                         0x000019B0
#define NV_PGRAPH_TEXADDRESS0                            0x000019BC
#   define NV_PGRAPH_TEXADDRESS0_ADDRU                          0x00000007
#       define NV_PGRAPH_TEXADDRESS0_ADDRU_WRAP                      1
#       define NV_PGRAPH_TEXADDRESS0_ADDRU_MIRROR                    2
#       define NV_PGRAPH_TEXADDRESS0_ADDRU_CLAMP_TO_EDGE             3
#       define NV_PGRAPH_TEXADDRESS0_ADDRU_BORDER                    4
#       define NV_PGRAPH_TEXADDRESS0_ADDRU_CLAMP_OGL                 5
#   define NV_PGRAPH_TEXADDRESS0_WRAP_U                         (1 << 4)
#   define NV_PGRAPH_TEXADDRESS0_ADDRV                          0x00000700
#   define NV_PGRAPH_TEXADDRESS0_WRAP_V                         (1 << 12)
#   define NV_PGRAPH_TEXADDRESS0_ADDRP                          0x00070000
#   define NV_PGRAPH_TEXADDRESS0_WRAP_P                         (1 << 20)
#   define NV_PGRAPH_TEXADDRESS0_WRAP_Q                         (1 << 24)
#define NV_PGRAPH_TEXADDRESS1                            0x000019C0
#define NV_PGRAPH_TEXADDRESS2                            0x000019C4
#define NV_PGRAPH_TEXADDRESS3                            0x000019C8
#define NV_PGRAPH_TEXCTL0_0                              0x000019CC
#   define NV_PGRAPH_TEXCTL0_0_ALPHAKILLEN                      (1 << 2)
#   define NV_PGRAPH_TEXCTL0_0_MAX_LOD_CLAMP                    0x0003FFC0
#   define NV_PGRAPH_TEXCTL0_0_MIN_LOD_CLAMP                    0x3FFC0000
#   define NV_PGRAPH_TEXCTL0_0_ENABLE                           (1 << 30)
#define NV_PGRAPH_TEXCTL0_1                              0x000019D0
#define NV_PGRAPH_TEXCTL0_2                              0x000019D4
#define NV_PGRAPH_TEXCTL0_3                              0x000019D8
#define NV_PGRAPH_TEXCTL1_0                              0x000019DC
#   define NV_PGRAPH_TEXCTL1_0_IMAGE_PITCH                      0xFFFF0000
#define NV_PGRAPH_TEXCTL1_1                              0x000019E0
#define NV_PGRAPH_TEXCTL1_2                              0x000019E4
#define NV_PGRAPH_TEXCTL1_3                              0x000019E8
#define NV_PGRAPH_TEXCTL2_0                              0x000019EC
#define NV_PGRAPH_TEXCTL2_1                              0x000019F0
#define NV_PGRAPH_TEXFILTER0                             0x000019F4
#   define NV_PGRAPH_TEXFILTER0_MIPMAP_LOD_BIAS                 0x00001FFF
#   define NV_PGRAPH_TEXFILTER0_MIN                             0x003F0000
#       define NV_PGRAPH_TEXFILTER0_MIN_BOX_LOD0                    1
#       define NV_PGRAPH_TEXFILTER0_MIN_TENT_LOD0                   2
#       define NV_PGRAPH_TEXFILTER0_MIN_BOX_NEARESTLOD              3
#       define NV_PGRAPH_TEXFILTER0_MIN_TENT_NEARESTLOD             4
#       define NV_PGRAPH_TEXFILTER0_MIN_BOX_TENT_LOD                5
#       define NV_PGRAPH_TEXFILTER0_MIN_TENT_TENT_LOD               6
#       define NV_PGRAPH_TEXFILTER0_MIN_CONVOLUTION_2D_LOD0         7
#   define NV_PGRAPH_TEXFILTER0_MAG                             0x0F000000
#   define NV_PGRAPH_TEXFILTER0_ASIGNED                         (1 << 28)
#   define NV_PGRAPH_TEXFILTER0_RSIGNED                         (1 << 29)
#   define NV_PGRAPH_TEXFILTER0_GSIGNED                         (1 << 30)
#   define NV_PGRAPH_TEXFILTER0_BSIGNED                         (1 << 31)
#define NV_PGRAPH_TEXFILTER1                             0x000019F8
#define NV_PGRAPH_TEXFILTER2                             0x000019FC
#define NV_PGRAPH_TEXFILTER3                             0x00001A00
#define NV_PGRAPH_TEXFMT0                                0x00001A04
#   define NV_PGRAPH_TEXFMT0_CONTEXT_DMA                        (1 << 1)
#   define NV_PGRAPH_TEXFMT0_CUBEMAPENABLE                      (1 << 2)
#   define NV_PGRAPH_TEXFMT0_BORDER_SOURCE                      (1 << 3)
#       define NV_PGRAPH_TEXFMT0_BORDER_SOURCE_TEXTURE              0
#       define NV_PGRAPH_TEXFMT0_BORDER_SOURCE_COLOR                1
#   define NV_PGRAPH_TEXFMT0_DIMENSIONALITY                     0x000000C0
#   define NV_PGRAPH_TEXFMT0_COLOR                              0x00007F00
#   define NV_PGRAPH_TEXFMT0_MIPMAP_LEVELS                      0x000F0000
#   define NV_PGRAPH_TEXFMT0_BASE_SIZE_U                        0x00F00000
#   define NV_PGRAPH_TEXFMT0_BASE_SIZE_V                        0x0F000000
#   define NV_PGRAPH_TEXFMT0_BASE_SIZE_P                        0xF0000000
#define NV_PGRAPH_TEXFMT1                                0x00001A08
#define NV_PGRAPH_TEXFMT2                                0x00001A0C
#define NV_PGRAPH_TEXFMT3                                0x00001A10
#define NV_PGRAPH_TEXIMAGERECT0                          0x00001A14
#   define NV_PGRAPH_TEXIMAGERECT0_WIDTH                        0x1FFF0000
#   define NV_PGRAPH_TEXIMAGERECT0_HEIGHT                       0x00001FFF
#define NV_PGRAPH_TEXIMAGERECT1                          0x00001A18
#define NV_PGRAPH_TEXIMAGERECT2                          0x00001A1C
#define NV_PGRAPH_TEXIMAGERECT3                          0x00001A20
#define NV_PGRAPH_TEXOFFSET0                             0x00001A24
#define NV_PGRAPH_TEXOFFSET1                             0x00001A28
#define NV_PGRAPH_TEXOFFSET2                             0x00001A2C
#define NV_PGRAPH_TEXOFFSET3                             0x00001A30
#define NV_PGRAPH_TEXPALETTE0                            0x00001A34
#   define NV_PGRAPH_TEXPALETTE0_CONTEXT_DMA                    (1 << 0)
#   define NV_PGRAPH_TEXPALETTE0_LENGTH                         0x0000000C
#       define NV_PGRAPH_TEXPALETTE0_LENGTH_256                     0
#       define NV_PGRAPH_TEXPALETTE0_LENGTH_128                     1
#       define NV_PGRAPH_TEXPALETTE0_LENGTH_64                      2
#       define NV_PGRAPH_TEXPALETTE0_LENGTH_32                      3
#   define NV_PGRAPH_TEXPALETTE0_OFFSET                         0xFFFFFFC0
#define NV_PGRAPH_TEXPALETTE1                            0x00001A38
#define NV_PGRAPH_TEXPALETTE2                            0x00001A3C
#define NV_PGRAPH_TEXPALETTE3                            0x00001A40
#define NV_PGRAPH_ZSTENCILCLEARVALUE                     0x00001A88
#define NV_PGRAPH_ZCLIPMIN                               0x00001A90
#define NV_PGRAPH_ZOFFSETBIAS                            0x00001AA4
#define NV_PGRAPH_ZOFFSETFACTOR                          0x00001AA8
#define NV_PGRAPH_EYEVEC0                                0x00001AAC
#define NV_PGRAPH_EYEVEC1                                0x00001AB0
#define NV_PGRAPH_EYEVEC2                                0x00001AB4
#define NV_PGRAPH_ZCLIPMAX                               0x00001ABC


#define NV_PCRTC_INTR_0                                  0x00000100
#   define NV_PCRTC_INTR_0_VBLANK                               (1 << 0)
#define NV_PCRTC_INTR_EN_0                               0x00000140
#   define NV_PCRTC_INTR_EN_0_VBLANK                            (1 << 0)
#define NV_PCRTC_START                                   0x00000800
#define NV_PCRTC_CONFIG                                  0x00000804


#define NV_PVIDEO_INTR                                   0x00000100
#   define NV_PVIDEO_INTR_BUFFER_0                              (1 << 0)
#   define NV_PVIDEO_INTR_BUFFER_1                              (1 << 4)
#define NV_PVIDEO_INTR_EN                                0x00000140
#   define NV_PVIDEO_INTR_EN_BUFFER_0                           (1 << 0)
#   define NV_PVIDEO_INTR_EN_BUFFER_1                           (1 << 4)
#define NV_PVIDEO_BUFFER                                 0x00000700
#   define NV_PVIDEO_BUFFER_0_USE                               (1 << 0)
#   define NV_PVIDEO_BUFFER_1_USE                               (1 << 4)
#define NV_PVIDEO_STOP                                   0x00000704
#define NV_PVIDEO_BASE                                   0x00000900
#define NV_PVIDEO_LIMIT                                  0x00000908
#define NV_PVIDEO_LUMINANCE                              0x00000910
#define NV_PVIDEO_CHROMINANCE                            0x00000918
#define NV_PVIDEO_OFFSET                                 0x00000920
#define NV_PVIDEO_SIZE_IN                                0x00000928
#   define NV_PVIDEO_SIZE_IN_WIDTH                            0x000007FF
#   define NV_PVIDEO_SIZE_IN_HEIGHT                           0x07FF0000
#define NV_PVIDEO_POINT_IN                               0x00000930
#   define NV_PVIDEO_POINT_IN_S                               0x00007FFF
#   define NV_PVIDEO_POINT_IN_T                               0xFFFE0000
#define NV_PVIDEO_DS_DX                                  0x00000938
#define NV_PVIDEO_DT_DY                                  0x00000940
#define NV_PVIDEO_POINT_OUT                              0x00000948
#   define NV_PVIDEO_POINT_OUT_X                              0x00000FFF
#   define NV_PVIDEO_POINT_OUT_Y                              0x0FFF0000
#define NV_PVIDEO_SIZE_OUT                               0x00000950
#   define NV_PVIDEO_SIZE_OUT_WIDTH                           0x00000FFF
#   define NV_PVIDEO_SIZE_OUT_HEIGHT                          0x0FFF0000
#define NV_PVIDEO_FORMAT                                 0x00000958
#   define NV_PVIDEO_FORMAT_PITCH                             0x00001FFF
#   define NV_PVIDEO_FORMAT_COLOR                             0x00030000
#       define NV_PVIDEO_FORMAT_COLOR_LE_CR8YB8CB8YA8             1
#   define NV_PVIDEO_FORMAT_DISPLAY                            (1 << 20)


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
#define NV_PFB_WBC                                       0x00000410
#   define NV_PFB_WBC_FLUSH                                     (1 << 16)


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
#   define NV097_SET_FLIP_READ                                0x00970120
#   define NV097_SET_FLIP_WRITE                               0x00970124
#   define NV097_SET_FLIP_MODULO                              0x00970128
#   define NV097_FLIP_INCREMENT_WRITE                         0x0097012C
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
#   define NV097_SET_CONTEXT_DMA_REPORT                       0x009701A8
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
#           define NV097_SET_SURFACE_FORMAT_ZETA_Z16                       1
#           define NV097_SET_SURFACE_FORMAT_ZETA_Z24S8                     2
#       define NV097_SET_SURFACE_FORMAT_TYPE                      0x00000F00
#           define NV097_SET_SURFACE_FORMAT_TYPE_PITCH                     0x1
#           define NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE                   0x2
#       define NV097_SET_SURFACE_FORMAT_ANTI_ALIASING             0x0000F000
#           define NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_1         0
#           define NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_CORNER_2  1
#           define NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_SQUARE_OFFSET_4  2
#       define NV097_SET_SURFACE_FORMAT_WIDTH                     0x00FF0000
#       define NV097_SET_SURFACE_FORMAT_HEIGHT                    0xFF000000
#   define NV097_SET_SURFACE_PITCH                            0x0097020C
#       define NV097_SET_SURFACE_PITCH_COLOR                      0x0000FFFF
#       define NV097_SET_SURFACE_PITCH_ZETA                       0xFFFF0000
#   define NV097_SET_SURFACE_COLOR_OFFSET                     0x00970210
#   define NV097_SET_SURFACE_ZETA_OFFSET                      0x00970214
#   define NV097_SET_COMBINER_ALPHA_ICW                       0x00970260
#   define NV097_SET_COMBINER_SPECULAR_FOG_CW0                0x00970288
#   define NV097_SET_COMBINER_SPECULAR_FOG_CW1                0x0097028C
#   define NV097_SET_CONTROL0                                 0x00970290
#       define NV097_SET_CONTROL0_STENCIL_WRITE_ENABLE            (1 << 0)
#       define NV097_SET_CONTROL0_Z_FORMAT                        (1 << 12)
#       define NV097_SET_CONTROL0_Z_PERSPECTIVE_ENABLE            (1 << 16)
#   define NV097_SET_FOG_MODE                                 0x0097029C
#       define NV097_SET_FOG_MODE_V_LINEAR                        0x2601
#       define NV097_SET_FOG_MODE_V_EXP                           0x800
#       define NV097_SET_FOG_MODE_V_EXP2                          0x801
#       define NV097_SET_FOG_MODE_V_EXP_ABS                       0x802
#       define NV097_SET_FOG_MODE_V_EXP2_ABS                      0x803
#       define NV097_SET_FOG_MODE_V_LINEAR_ABS                    0x804
#   define NV097_SET_FOG_GEN_MODE                             0x009702A0
#       define NV097_SET_FOG_GEN_MODE_V_SPEC_ALPHA                0
#       define NV097_SET_FOG_GEN_MODE_V_RADIAL                    1
#       define NV097_SET_FOG_GEN_MODE_V_PLANAR                    2
#       define NV097_SET_FOG_GEN_MODE_V_ABS_PLANAR                3
#       define NV097_SET_FOG_GEN_MODE_V_FOG_X                     6
#   define NV097_SET_FOG_ENABLE                               0x009702A4
#   define NV097_SET_FOG_COLOR                                0x009702A8
#       define NV097_SET_FOG_COLOR_RED                            0x000000FF
#       define NV097_SET_FOG_COLOR_GREEN                          0x0000FF00
#       define NV097_SET_FOG_COLOR_BLUE                           0x00FF0000
#       define NV097_SET_FOG_COLOR_ALPHA                          0xFF000000
#   define NV097_SET_ALPHA_TEST_ENABLE                        0x00970300
#   define NV097_SET_BLEND_ENABLE                             0x00970304
#   define NV097_SET_CULL_FACE_ENABLE                         0x00970308
#   define NV097_SET_DEPTH_TEST_ENABLE                        0x0097030C
#   define NV097_SET_DITHER_ENABLE                            0x00970310
#   define NV097_SET_LIGHTING_ENABLE                          0x00970314
#   define NV097_SET_SKIN_MODE                                0x00970328
#       define NV097_SET_SKIN_MODE_OFF                            0
#       define NV097_SET_SKIN_MODE_2G                             1
#       define NV097_SET_SKIN_MODE_2                              2
#       define NV097_SET_SKIN_MODE_3G                             3
#       define NV097_SET_SKIN_MODE_3                              4
#       define NV097_SET_SKIN_MODE_4G                             5
#       define NV097_SET_SKIN_MODE_4                              6
#   define NV097_SET_STENCIL_TEST_ENABLE                      0x0097032C
#   define NV097_SET_POLY_OFFSET_POINT_ENABLE                 0x00970330
#   define NV097_SET_POLY_OFFSET_LINE_ENABLE                  0x00970334
#   define NV097_SET_POLY_OFFSET_FILL_ENABLE                  0x00970338
#   define NV097_SET_ALPHA_FUNC                               0x0097033C
#   define NV097_SET_ALPHA_REF                                0x00970340
#   define NV097_SET_BLEND_FUNC_SFACTOR                       0x00970344
#       define NV097_SET_BLEND_FUNC_SFACTOR_V_ZERO                0x0000
#       define NV097_SET_BLEND_FUNC_SFACTOR_V_ONE                 0x0001
#       define NV097_SET_BLEND_FUNC_SFACTOR_V_SRC_COLOR           0x0300
#       define NV097_SET_BLEND_FUNC_SFACTOR_V_ONE_MINUS_SRC_COLOR 0x0301
#       define NV097_SET_BLEND_FUNC_SFACTOR_V_SRC_ALPHA           0x0302
#       define NV097_SET_BLEND_FUNC_SFACTOR_V_ONE_MINUS_SRC_ALPHA 0x0303
#       define NV097_SET_BLEND_FUNC_SFACTOR_V_DST_ALPHA           0x0304
#       define NV097_SET_BLEND_FUNC_SFACTOR_V_ONE_MINUS_DST_ALPHA 0x0305
#       define NV097_SET_BLEND_FUNC_SFACTOR_V_DST_COLOR           0x0306
#       define NV097_SET_BLEND_FUNC_SFACTOR_V_ONE_MINUS_DST_COLOR 0x0307
#       define NV097_SET_BLEND_FUNC_SFACTOR_V_SRC_ALPHA_SATURATE  0x0308
#       define NV097_SET_BLEND_FUNC_SFACTOR_V_CONSTANT_COLOR      0x8001
#       define NV097_SET_BLEND_FUNC_SFACTOR_V_ONE_MINUS_CONSTANT_COLOR 0x8002
#       define NV097_SET_BLEND_FUNC_SFACTOR_V_CONSTANT_ALPHA      0x8003
#       define NV097_SET_BLEND_FUNC_SFACTOR_V_ONE_MINUS_CONSTANT_ALPHA 0x8004
#   define NV097_SET_BLEND_FUNC_DFACTOR                       0x00970348
#       define NV097_SET_BLEND_FUNC_DFACTOR_V_ZERO                0x0000
#       define NV097_SET_BLEND_FUNC_DFACTOR_V_ONE                 0x0001
#       define NV097_SET_BLEND_FUNC_DFACTOR_V_SRC_COLOR           0x0300
#       define NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_SRC_COLOR 0x0301
#       define NV097_SET_BLEND_FUNC_DFACTOR_V_SRC_ALPHA           0x0302
#       define NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_SRC_ALPHA 0x0303
#       define NV097_SET_BLEND_FUNC_DFACTOR_V_DST_ALPHA           0x0304
#       define NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_DST_ALPHA 0x0305
#       define NV097_SET_BLEND_FUNC_DFACTOR_V_DST_COLOR           0x0306
#       define NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_DST_COLOR 0x0307
#       define NV097_SET_BLEND_FUNC_DFACTOR_V_SRC_ALPHA_SATURATE  0x0308
#       define NV097_SET_BLEND_FUNC_DFACTOR_V_CONSTANT_COLOR      0x8001
#       define NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_CONSTANT_COLOR 0x8002
#       define NV097_SET_BLEND_FUNC_DFACTOR_V_CONSTANT_ALPHA      0x8003
#       define NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_CONSTANT_ALPHA 0x8004
#   define NV097_SET_BLEND_COLOR                              0x0097034C
#   define NV097_SET_BLEND_EQUATION                           0x00970350
#       define NV097_SET_BLEND_EQUATION_V_FUNC_SUBTRACT           0x800A
#       define NV097_SET_BLEND_EQUATION_V_FUNC_REVERSE_SUBTRACT   0x800B
#       define NV097_SET_BLEND_EQUATION_V_FUNC_ADD                0x8006
#       define NV097_SET_BLEND_EQUATION_V_MIN                     0x8007
#       define NV097_SET_BLEND_EQUATION_V_MAX                     0x8008
#       define NV097_SET_BLEND_EQUATION_V_FUNC_REVERSE_SUBTRACT_SIGNED 0xF005
#       define NV097_SET_BLEND_EQUATION_V_FUNC_ADD_SIGNED         0xF006
#   define NV097_SET_DEPTH_FUNC                               0x00970354
#   define NV097_SET_COLOR_MASK                               0x00970358
#       define NV097_SET_COLOR_MASK_BLUE_WRITE_ENABLE             (1 << 0)
#       define NV097_SET_COLOR_MASK_GREEN_WRITE_ENABLE            (1 << 8)
#       define NV097_SET_COLOR_MASK_RED_WRITE_ENABLE              (1 << 16)
#       define NV097_SET_COLOR_MASK_ALPHA_WRITE_ENABLE            (1 << 24)
#   define NV097_SET_DEPTH_MASK                               0x0097035C
#   define NV097_SET_STENCIL_MASK                             0x00970360
#   define NV097_SET_STENCIL_FUNC                             0x00970364
#   define NV097_SET_STENCIL_FUNC_REF                         0x00970368
#   define NV097_SET_STENCIL_FUNC_MASK                        0x0097036C
#   define NV097_SET_STENCIL_OP_FAIL                          0x00970370
#   define NV097_SET_STENCIL_OP_ZFAIL                         0x00970374
#   define NV097_SET_STENCIL_OP_ZPASS                         0x00970378
#       define NV097_SET_STENCIL_OP_V_KEEP                        0x1E00
#       define NV097_SET_STENCIL_OP_V_ZERO                        0x0000
#       define NV097_SET_STENCIL_OP_V_REPLACE                     0x1E01
#       define NV097_SET_STENCIL_OP_V_INCRSAT                     0x1E02
#       define NV097_SET_STENCIL_OP_V_DECRSAT                     0x1E03
#       define NV097_SET_STENCIL_OP_V_INVERT                      0x150A
#       define NV097_SET_STENCIL_OP_V_INCR                        0x8507
#       define NV097_SET_STENCIL_OP_V_DECR                        0x8508
#   define NV097_SET_POLYGON_OFFSET_SCALE_FACTOR              0x00970384
#   define NV097_SET_POLYGON_OFFSET_BIAS                      0x00970388
#   define NV097_SET_FRONT_POLYGON_MODE                       0x0097038C
#       define NV097_SET_FRONT_POLYGON_MODE_V_POINT               0x1B00
#       define NV097_SET_FRONT_POLYGON_MODE_V_LINE                0x1B01
#       define NV097_SET_FRONT_POLYGON_MODE_V_FILL                0x1B02
#   define NV097_SET_BACK_POLYGON_MODE                        0x00970390
#   define NV097_SET_CLIP_MIN                                 0x00970394
#   define NV097_SET_CLIP_MAX                                 0x00970398
#   define NV097_SET_CULL_FACE                                0x0097039C
#       define NV097_SET_CULL_FACE_V_FRONT                         0x404
#       define NV097_SET_CULL_FACE_V_BACK                          0x405
#       define NV097_SET_CULL_FACE_V_FRONT_AND_BACK                0x408
#   define NV097_SET_FRONT_FACE                               0x009703A0
#       define NV097_SET_FRONT_FACE_V_CW                           0x900
#       define NV097_SET_FRONT_FACE_V_CCW                          0x901
#   define NV097_SET_NORMALIZATION_ENABLE                     0x009703A4
#   define NV097_SET_LIGHT_ENABLE_MASK                        0x009703BC
#           define NV097_SET_LIGHT_ENABLE_MASK_LIGHT0_OFF           0
#           define NV097_SET_LIGHT_ENABLE_MASK_LIGHT0_INFINITE      1
#           define NV097_SET_LIGHT_ENABLE_MASK_LIGHT0_LOCAL         2
#           define NV097_SET_LIGHT_ENABLE_MASK_LIGHT0_SPOT          3
#   define NV097_SET_TEXGEN_S                                 0x009703C0
#       define NV097_SET_TEXGEN_S_DISABLE                         0x0000
#       define NV097_SET_TEXGEN_S_EYE_LINEAR                      0x2400
#       define NV097_SET_TEXGEN_S_OBJECT_LINEAR                   0x2401
#       define NV097_SET_TEXGEN_S_SPHERE_MAP                      0x2402
#       define NV097_SET_TEXGEN_S_REFLECTION_MAP                  0x8512
#       define NV097_SET_TEXGEN_S_NORMAL_MAP                      0x8511
#   define NV097_SET_TEXGEN_T                                 0x009703C4
#   define NV097_SET_TEXGEN_R                                 0x009703C8
#   define NV097_SET_TEXGEN_Q                                 0x009703CC
#   define NV097_SET_TEXTURE_MATRIX_ENABLE                    0x00970420
#   define NV097_SET_PROJECTION_MATRIX                        0x00970440
#   define NV097_SET_MODEL_VIEW_MATRIX                        0x00970480
#   define NV097_SET_INVERSE_MODEL_VIEW_MATRIX                0x00970580
#   define NV097_SET_COMPOSITE_MATRIX                         0x00970680
#   define NV097_SET_TEXTURE_MATRIX                           0x009706C0
#   define NV097_SET_FOG_PARAMS                               0x009709C0
#   define NV097_SET_TEXGEN_PLANE_S                           0x00970840
#   define NV097_SET_TEXGEN_PLANE_T                           0x00970850
#   define NV097_SET_TEXGEN_PLANE_R                           0x00970860
#   define NV097_SET_TEXGEN_PLANE_Q                           0x00970870
#   define NV097_SET_TEXGEN_VIEW_MODEL                        0x009709CC
#       define NV097_SET_TEXGEN_VIEW_MODEL_LOCAL_VIEWER           0
#       define NV097_SET_TEXGEN_VIEW_MODEL_INFINITE_VIEWER        1
#   define NV097_SET_FOG_PLANE                                0x009709D0
#   define NV097_SET_SCENE_AMBIENT_COLOR                      0x00970A10
#   define NV097_SET_VIEWPORT_OFFSET                          0x00970A20
#   define NV097_SET_EYE_POSITION                             0x00970A50
#   define NV097_SET_COMBINER_FACTOR0                         0x00970A60
#   define NV097_SET_COMBINER_FACTOR1                         0x00970A80
#   define NV097_SET_COMBINER_ALPHA_OCW                       0x00970AA0
#   define NV097_SET_COMBINER_COLOR_ICW                       0x00970AC0
#   define NV097_SET_VIEWPORT_SCALE                           0x00970AF0
#   define NV097_SET_TRANSFORM_PROGRAM                        0x00970B00
#   define NV097_SET_TRANSFORM_CONSTANT                       0x00970B80
#   define NV097_SET_VERTEX3F                                 0x00971500
#   define NV097_SET_BACK_LIGHT_AMBIENT_COLOR                 0x00970C00
#   define NV097_SET_BACK_LIGHT_DIFFUSE_COLOR                 0x00970C0C
#   define NV097_SET_BACK_LIGHT_SPECULAR_COLOR                0x00970C18
#   define NV097_SET_LIGHT_AMBIENT_COLOR                      0x00971000
#   define NV097_SET_LIGHT_DIFFUSE_COLOR                      0x0097100C
#   define NV097_SET_LIGHT_SPECULAR_COLOR                     0x00971018
#   define NV097_SET_LIGHT_LOCAL_RANGE                        0x00971024
#   define NV097_SET_LIGHT_INFINITE_HALF_VECTOR               0x00971028
#   define NV097_SET_LIGHT_INFINITE_DIRECTION                 0x00971034
#   define NV097_SET_LIGHT_SPOT_FALLOFF                       0x00971040
#   define NV097_SET_LIGHT_SPOT_DIRECTION                     0x0097104C
#   define NV097_SET_LIGHT_LOCAL_POSITION                     0x0097105C
#   define NV097_SET_LIGHT_LOCAL_ATTENUATION                  0x00971068
#   define NV097_SET_VERTEX4F                                 0x00971518
#   define NV097_SET_VERTEX_DATA_ARRAY_OFFSET                 0x00971720
#   define NV097_SET_VERTEX_DATA_ARRAY_FORMAT                 0x00971760
#       define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE            0x0000000F
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D     0
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1         1
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F          2
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL     4
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K       5
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP        6
#       define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE            0x000000F0
#       define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE          0xFFFFFF00
#   define NV097_SET_LOGIC_OP_ENABLE                          0x009717BC
#   define NV097_SET_LOGIC_OP                                 0x009717C0
#   define NV097_CLEAR_REPORT_VALUE                           0x009717C8
#       define NV097_CLEAR_REPORT_VALUE_TYPE                      0xFFFFFFFF
#           define NV097_CLEAR_REPORT_VALUE_TYPE_ZPASS_PIXEL_CNT      1
#   define NV097_SET_ZPASS_PIXEL_COUNT_ENABLE                 0x009717CC
#   define NV097_GET_REPORT                                   0x009717D0
#       define NV097_GET_REPORT_OFFSET                            0x00FFFFFF
#       define NV097_GET_REPORT_TYPE                              0xFF000000
#           define NV097_GET_REPORT_TYPE_ZPASS_PIXEL_CNT              1
#   define NV097_SET_EYE_DIRECTION                            0x009717E0
#   define NV097_SET_SHADER_CLIP_PLANE_MODE                   0x009717F8
#   define NV097_SET_BEGIN_END                                0x009717FC
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
#   define NV097_SET_EYE_VECTOR                               0x0097181C
#   define NV097_SET_VERTEX_DATA2F_M                          0x00971880
#   define NV097_SET_VERTEX_DATA4F_M                          0x00971A00
#   define NV097_SET_VERTEX_DATA2S                            0x00971900
#   define NV097_SET_VERTEX_DATA4UB                           0x00971940
#   define NV097_SET_VERTEX_DATA4S_M                          0x00971980
#   define NV097_SET_TEXTURE_OFFSET                           0x00971B00
#   define NV097_SET_TEXTURE_FORMAT                           0x00971B04
#       define NV097_SET_TEXTURE_FORMAT_CONTEXT_DMA               0x00000003
#       define NV097_SET_TEXTURE_FORMAT_CUBEMAP_ENABLE            (1 << 2)
#       define NV097_SET_TEXTURE_FORMAT_BORDER_SOURCE             (1 << 3)
#           define NV097_SET_TEXTURE_FORMAT_BORDER_SOURCE_TEXTURE   0
#           define NV097_SET_TEXTURE_FORMAT_BORDER_SOURCE_COLOR     1
#       define NV097_SET_TEXTURE_FORMAT_DIMENSIONALITY            0x000000F0
#       define NV097_SET_TEXTURE_FORMAT_COLOR                     0x0000FF00
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_Y8             0x00
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_AY8            0x01
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A1R5G5B5       0x02
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X1R5G5B5       0x03
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A4R4G4B4       0x04
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5         0x05
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8       0x06
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8       0x07
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8    0x0B
#           define NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5   0x0C
#           define NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8  0x0E
#           define NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT45_A8R8G8B8  0x0F
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A1R5G5B5 0x10
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5   0x11
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8 0x12
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y8       0x13
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8             0x19
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8Y8           0x1A
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_AY8      0x1B
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X1R5G5B5 0x1C
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A4R4G4B4 0x1D
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8 0x1E
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8       0x1F
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8Y8     0x20
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_CR8YB8CB8YA8 0x24
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5         0x27
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8           0x28
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8B8           0x29
# define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_X8_Y24_FIXED 0x2E
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FIXED 0x30
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y16      0x35
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8       0x3A
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8G8B8A8       0x3C
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8B8G8R8 0x3F
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_B8G8R8A8 0x40
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R8G8B8A8 0x41
#       define NV097_SET_TEXTURE_FORMAT_MIPMAP_LEVELS             0x000F0000
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
#       define NV097_SET_TEXTURE_FILTER_MIPMAP_LOD_BIAS           0x00001FFF
#       define NV097_SET_TEXTURE_FILTER_MIN                       0x00FF0000
#       define NV097_SET_TEXTURE_FILTER_MAG                       0x0F000000
#       define NV097_SET_TEXTURE_FILTER_ASIGNED                   (1 << 28)
#       define NV097_SET_TEXTURE_FILTER_RSIGNED                   (1 << 29)
#       define NV097_SET_TEXTURE_FILTER_GSIGNED                   (1 << 30)
#       define NV097_SET_TEXTURE_FILTER_BSIGNED                   (1 << 31)
#   define NV097_SET_TEXTURE_IMAGE_RECT                       0x00971B1C
#       define NV097_SET_TEXTURE_IMAGE_RECT_WIDTH                 0xFFFF0000
#       define NV097_SET_TEXTURE_IMAGE_RECT_HEIGHT                0x0000FFFF
#   define NV097_SET_TEXTURE_PALETTE                          0x00971B20
#       define NV097_SET_TEXTURE_PALETTE_CONTEXT_DMA              (1 << 0)
#       define NV097_SET_TEXTURE_PALETTE_LENGTH                   0x0000000C
#         define NV097_SET_TEXTURE_PALETTE_LENGTH_256               0
#         define NV097_SET_TEXTURE_PALETTE_LENGTH_128               1
#         define NV097_SET_TEXTURE_PALETTE_LENGTH_64                2
#         define NV097_SET_TEXTURE_PALETTE_LENGTH_32                3
#       define NV097_SET_TEXTURE_PALETTE_OFFSET                   0xFFFFFFC0
#   define NV097_SET_TEXTURE_BORDER_COLOR                     0x00971B24
#   define NV097_SET_TEXTURE_SET_BUMP_ENV_MAT                 0x00971B28
#   define NV097_SET_TEXTURE_SET_BUMP_ENV_SCALE               0x00971B38
#   define NV097_SET_TEXTURE_SET_BUMP_ENV_OFFSET              0x00971B3C
#   define NV097_SET_SEMAPHORE_OFFSET                         0x00971D6C
#   define NV097_BACK_END_WRITE_SEMAPHORE_RELEASE             0x00971D70
#   define NV097_SET_ZSTENCIL_CLEAR_VALUE                     0x00971D8C
#   define NV097_SET_COLOR_CLEAR_VALUE                        0x00971D90
#   define NV097_CLEAR_SURFACE                                0x00971D94
#       define NV097_CLEAR_SURFACE_Z                              (1 << 0)
#       define NV097_CLEAR_SURFACE_STENCIL                        (1 << 1)
#       define NV097_CLEAR_SURFACE_COLOR                          0x000000F0
#       define NV097_CLEAR_SURFACE_R                                (1 << 4)
#       define NV097_CLEAR_SURFACE_G                                (1 << 5)
#       define NV097_CLEAR_SURFACE_B                                (1 << 6)
#       define NV097_CLEAR_SURFACE_A                                (1 << 7)
#   define NV097_SET_CLEAR_RECT_HORIZONTAL                    0x00971D98
#   define NV097_SET_CLEAR_RECT_VERTICAL                      0x00971D9C
#   define NV097_SET_SPECULAR_FOG_FACTOR                      0x00971E20
#   define NV097_SET_COMBINER_COLOR_OCW                       0x00971E40
#   define NV097_SET_COMBINER_CONTROL                         0x00971E60
#   define NV097_SET_SHADOW_ZSLOPE_THRESHOLD                  0x00971E68
#   define NV097_SET_SHADER_STAGE_PROGRAM                     0x00971E70
#   define NV097_SET_SHADER_OTHER_STAGE_INPUT                 0x00971E78
#   define NV097_SET_TRANSFORM_EXECUTION_MODE                 0x00971E94
#       define NV097_SET_TRANSFORM_EXECUTION_MODE_MODE            0x00000003
#       define NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE      0xFFFFFFFC
#   define NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN           0x00971E98
#   define NV097_SET_TRANSFORM_PROGRAM_LOAD                   0x00971E9C
#   define NV097_SET_TRANSFORM_PROGRAM_START                  0x00971EA0
#   define NV097_SET_TRANSFORM_CONSTANT_LOAD                  0x00971EA4

static const GLenum pgraph_texture_min_filter_map[] = {
    0,
    GL_NEAREST,
    GL_LINEAR,
    GL_NEAREST_MIPMAP_NEAREST,
    GL_LINEAR_MIPMAP_NEAREST,
    GL_NEAREST_MIPMAP_LINEAR,
    GL_LINEAR_MIPMAP_LINEAR,
    GL_LINEAR, /* TODO: Convolution filter... */
};

static const GLenum pgraph_texture_mag_filter_map[] = {
    0,
    GL_NEAREST,
    GL_LINEAR,
    0,
    GL_LINEAR /* TODO: Convolution filter... */
};

static const GLenum pgraph_texture_addr_map[] = {
    0,
    GL_REPEAT,
    GL_MIRRORED_REPEAT,
    GL_CLAMP_TO_EDGE,
    GL_CLAMP_TO_BORDER,
    // GL_CLAMP
};

static const GLenum pgraph_blend_factor_map[] = {
    GL_ZERO,
    GL_ONE,
    GL_SRC_COLOR,
    GL_ONE_MINUS_SRC_COLOR,
    GL_SRC_ALPHA,
    GL_ONE_MINUS_SRC_ALPHA,
    GL_DST_ALPHA,
    GL_ONE_MINUS_DST_ALPHA,
    GL_DST_COLOR,
    GL_ONE_MINUS_DST_COLOR,
    GL_SRC_ALPHA_SATURATE,
    0,
    GL_CONSTANT_COLOR,
    GL_ONE_MINUS_CONSTANT_COLOR,
    GL_CONSTANT_ALPHA,
    GL_ONE_MINUS_CONSTANT_ALPHA,
};

static const GLenum pgraph_blend_equation_map[] = {
    GL_FUNC_SUBTRACT,
    GL_FUNC_REVERSE_SUBTRACT,
    GL_FUNC_ADD,
    GL_MIN,
    GL_MAX,
    GL_FUNC_REVERSE_SUBTRACT,
    GL_FUNC_ADD,
};

static const GLenum pgraph_blend_logicop_map[] = {
    GL_CLEAR,
    GL_AND,
    GL_AND_REVERSE,
    GL_COPY,
    GL_AND_INVERTED,
    GL_NOOP,
    GL_XOR,
    GL_OR,
    GL_NOR,
    GL_EQUIV,
    GL_INVERT,
    GL_OR_REVERSE,
    GL_COPY_INVERTED,
    GL_OR_INVERTED,
    GL_NAND,
    GL_SET,
};

static const GLenum pgraph_cull_face_map[] = {
    0,
    GL_FRONT,
    GL_BACK,
    GL_FRONT_AND_BACK
};

static const GLenum pgraph_depth_func_map[] = {
    GL_NEVER,
    GL_LESS,
    GL_EQUAL,
    GL_LEQUAL,
    GL_GREATER,
    GL_NOTEQUAL,
    GL_GEQUAL,
    GL_ALWAYS,
};

static const GLenum pgraph_stencil_func_map[] = {
    GL_NEVER,
    GL_LESS,
    GL_EQUAL,
    GL_LEQUAL,
    GL_GREATER,
    GL_NOTEQUAL,
    GL_GEQUAL,
    GL_ALWAYS,
};

static const GLenum pgraph_stencil_op_map[] = {
    0,
    GL_KEEP,
    GL_ZERO,
    GL_REPLACE,
    GL_INCR,
    GL_DECR,
    GL_INVERT,
    GL_INCR_WRAP,
    GL_DECR_WRAP,
};

typedef struct ColorFormatInfo {
    unsigned int bytes_per_pixel;
    bool linear;
    GLint gl_internal_format;
    GLenum gl_format;
    GLenum gl_type;
    GLenum gl_swizzle_mask[4];
} ColorFormatInfo;

static const ColorFormatInfo kelvin_color_format_map[66] = {
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_Y8] =
        {1, false, GL_R8, GL_RED, GL_UNSIGNED_BYTE,
         {GL_RED, GL_RED, GL_RED, GL_ONE}},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_AY8] =
        {1, false, GL_R8, GL_RED, GL_UNSIGNED_BYTE,
         {GL_RED, GL_RED, GL_RED, GL_RED}},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A1R5G5B5] =
        {2, false, GL_RGB5_A1, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X1R5G5B5] =
        {2, false, GL_RGB5, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A4R4G4B4] =
        {2, false, GL_RGBA4, GL_BGRA, GL_UNSIGNED_SHORT_4_4_4_4_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5] =
        {2, false, GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8] =
        {4, false, GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8] =
        {4, false, GL_RGB8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV},

    /* paletted texture */
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8] =
        {1, false, GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV},

    [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5] =
        {4, false, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 0, GL_RGBA},
    [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8] =
        {4, false, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 0, GL_RGBA},
    [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT45_A8R8G8B8] =
        {4, false, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 0, GL_RGBA},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A1R5G5B5] =
        {2, true, GL_RGB5_A1, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5] =
        {2, true, GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8] =
        {4, true, GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y8] =
        {1, true, GL_R8, GL_RED, GL_UNSIGNED_BYTE,
         {GL_RED, GL_RED, GL_RED, GL_ONE}},

    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8] =
        {1, false, GL_R8, GL_RED, GL_UNSIGNED_BYTE,
         {GL_ONE, GL_ONE, GL_ONE, GL_RED}},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8Y8] =
        {2, false, GL_RG8, GL_RG, GL_UNSIGNED_BYTE,
         {GL_GREEN, GL_GREEN, GL_GREEN, GL_RED}},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_AY8] =
        {1, true, GL_R8, GL_RED, GL_UNSIGNED_BYTE,
         {GL_RED, GL_RED, GL_RED, GL_RED}},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X1R5G5B5] =
        {2, true, GL_RGB5, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A4R4G4B4] =
        {2, false, GL_RGBA4, GL_BGRA, GL_UNSIGNED_SHORT_4_4_4_4_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8] =
        {4, true, GL_RGB8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8] =
        {1, true, GL_R8, GL_RED, GL_UNSIGNED_BYTE,
         {GL_ONE, GL_ONE, GL_ONE, GL_RED}},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8Y8] =
        {2, true, GL_RG8, GL_RG, GL_UNSIGNED_BYTE,
         {GL_GREEN, GL_GREEN, GL_GREEN, GL_RED}},

    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5] =
        {2, false, GL_RGB8_SNORM, GL_RGB, GL_BYTE}, /* FIXME: This might be signed */
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8] =
        {2, false, GL_RG8_SNORM, GL_RG, GL_BYTE, /* FIXME: This might be signed */
         {GL_ZERO, GL_RED, GL_GREEN, GL_ONE}},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8B8] =
        {2, false, GL_RG8_SNORM, GL_RG, GL_BYTE, /* FIXME: This might be signed */
         {GL_RED, GL_ZERO, GL_GREEN, GL_ONE}},


    /* TODO: format conversion */
    [NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_CR8YB8CB8YA8] =
        {2, true, GL_RGBA8,  GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_X8_Y24_FIXED] =
        {4, true, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FIXED] =
        {2, true, GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y16] =
        {2, true, GL_R16, GL_RED, GL_UNSIGNED_SHORT,
         {GL_RED, GL_RED, GL_RED, GL_ONE}},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8] =
        {4, false, GL_RGBA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV},

    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8G8B8A8] =
        {4, false, GL_RGBA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8},

    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8B8G8R8] =
        {4, true, GL_RGBA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_B8G8R8A8] =
        {4, true, GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R8G8B8A8] =
        {4, true, GL_RGBA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8}
};

typedef struct SurfaceColorFormatInfo {
    unsigned int bytes_per_pixel;
    GLint gl_internal_format;
    GLenum gl_format;
    GLenum gl_type;
} SurfaceColorFormatInfo;

static const SurfaceColorFormatInfo kelvin_surface_color_format_map[] = {
    [NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_Z1R5G5B5] =
        {2, GL_RGB5_A1, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV},
    [NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5] =
        {2, GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5},
    [NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8] =
        {4, GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV},
    [NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8] =
        {4, GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV},
};

#define NV2A_VERTEX_ATTR_POSITION       0
#define NV2A_VERTEX_ATTR_WEIGHT         1
#define NV2A_VERTEX_ATTR_NORMAL         2
#define NV2A_VERTEX_ATTR_DIFFUSE        3
#define NV2A_VERTEX_ATTR_SPECULAR       4
#define NV2A_VERTEX_ATTR_FOG            5
#define NV2A_VERTEX_ATTR_POINT_SIZE     6
#define NV2A_VERTEX_ATTR_BACK_DIFFUSE   7
#define NV2A_VERTEX_ATTR_BACK_SPECULAR  8
#define NV2A_VERTEX_ATTR_TEXTURE0       9
#define NV2A_VERTEX_ATTR_TEXTURE1       10
#define NV2A_VERTEX_ATTR_TEXTURE2       11
#define NV2A_VERTEX_ATTR_TEXTURE3       12
#define NV2A_VERTEX_ATTR_RESERVED1      13
#define NV2A_VERTEX_ATTR_RESERVED2      14
#define NV2A_VERTEX_ATTR_RESERVED3      15


#define NV2A_CRYSTAL_FREQ 13500000
#define NV2A_NUM_CHANNELS 32
#define NV2A_NUM_SUBCHANNELS 8

#define NV2A_MAX_BATCH_LENGTH 0x1FFFF
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
#define NV2A_MAX_TEXTURES 4

#define ARRAYSIZE(x) (sizeof(x)/sizeof(x[0]))

#define GET_MASK(v, mask) (((v) & (mask)) >> (ffs(mask)-1))

#define SET_MASK(v, mask, val) ({                                    \
        const unsigned int __val = (val);                             \
        const unsigned int __mask = (mask);                          \
        (v) &= ~(__mask);                                            \
        (v) |= ((__val) << (ffs(__mask)-1)) & (__mask);              \
    })

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
    unsigned int inline_array_offset;

    float inline_value[4];

    unsigned int format;
    unsigned int size; /* size of the data type */
    unsigned int count; /* number of components */
    uint32_t stride;

    bool needs_conversion;
    uint8_t *converted_buffer;
    unsigned int converted_elements;
    unsigned int converted_size;
    unsigned int converted_count;

    float *inline_buffer;

    GLint gl_count;
    GLenum gl_type;
    GLboolean gl_normalize;

    GLuint gl_converted_buffer;
    GLuint gl_inline_buffer;
} VertexAttribute;

typedef struct VertexShaderConstant {
    bool dirty;
    uint32_t data[4];
} VertexShaderConstant;

typedef struct Surface {
    bool draw_dirty;
    bool buffer_dirty;
    bool write_enabled_cache;
    unsigned int pitch;

    hwaddr offset;
} Surface;

typedef struct SurfaceShape {
    unsigned int z_format;
    unsigned int color_format;
    unsigned int zeta_format;
    unsigned int log_width, log_height;
    unsigned int clip_x, clip_y;
    unsigned int clip_width, clip_height;
    unsigned int anti_aliasing;
} SurfaceShape;

typedef struct TextureShape {
    bool cubemap;
    unsigned int dimensionality;
    unsigned int color_format;
    unsigned int levels;
    unsigned int width, height, depth;

    unsigned int min_mipmap_level, max_mipmap_level;
    unsigned int pitch;
} TextureShape;

typedef struct TextureKey {
    TextureShape state;
    uint64_t data_hash;
    uint8_t* texture_data;
    uint8_t* palette_data;
} TextureKey;

typedef struct TextureBinding {
    GLenum gl_target;
    GLuint gl_texture;
    unsigned int refcnt;
} TextureBinding;

typedef struct KelvinState {
    hwaddr dma_notifies;
    hwaddr dma_state;
    hwaddr dma_semaphore;
    unsigned int semaphore_offset;
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
} GraphicsContext;


typedef struct PGRAPHState {
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

    QemuCond flip_3d;

    unsigned int channel_id;
    bool channel_valid;
    GraphicsContext context[NV2A_NUM_CHANNELS];

    hwaddr dma_color, dma_zeta;
    Surface surface_color, surface_zeta;
    unsigned int surface_type;
    SurfaceShape surface_shape;
    SurfaceShape last_surface_shape;

    hwaddr dma_a, dma_b;
    GLruCache *texture_cache;
    bool texture_dirty[NV2A_MAX_TEXTURES];
    TextureBinding *texture_binding[NV2A_MAX_TEXTURES];

    GHashTable *shader_cache;
    ShaderBinding *shader_binding;

    float composite_matrix[16];

    /* FIXME: These are probably stored in the vshader consts */
    bool texture_matrix_enable[NV2A_MAX_TEXTURES];
    float texture_matrix[NV2A_MAX_TEXTURES][16]; /* 4 stages with 4x4 matrix each */
    float texture_plane[NV2A_MAX_TEXTURES][4][4]; /* 4 stages, 4 components + plane for each */
    float projection_matrix[16];
    float inverse_model_view_matrix[4][16]; /* 4 weights with 4x4 matrix each */
    float model_view_matrix[4][16]; /* 4 weights with 4x4 matrix each */

    /* FIXME: Also in vshader consts? */
    float fog_plane[4];

    /* FIXME: These are probably stored in the vshader consts */
    float scene_ambient_color[3];
    float back_light_ambient_color[NV2A_MAX_LIGHTS][3];
    float back_light_diffuse_color[NV2A_MAX_LIGHTS][3];
    float back_light_specular_color[NV2A_MAX_LIGHTS][3];
    float light_ambient_color[NV2A_MAX_LIGHTS][3];
    float light_diffuse_color[NV2A_MAX_LIGHTS][3];
    float light_specular_color[NV2A_MAX_LIGHTS][3];
    float light_local_range[NV2A_MAX_LIGHTS];
    float light_infinite_half_vector[NV2A_MAX_LIGHTS][3];
    float light_infinite_direction[NV2A_MAX_LIGHTS][3];
    float light_spot_falloff[NV2A_MAX_LIGHTS][3];
    float light_spot_direction[NV2A_MAX_LIGHTS][4];
    float light_local_position[NV2A_MAX_LIGHTS][3];
    float light_local_attenuation[NV2A_MAX_LIGHTS][3];

    /* FIXME: These are probably stored in the vshader consts */
    float eye_position[4];
    float eye_direction[3];

    /* FIXME: Move to NV_PGRAPH_BUMPMAT... */
    float bump_env_matrix[NV2A_MAX_TEXTURES-1][4]; /* 3 allowed stages with 2x2 matrix each */

    GloContext *gl_context;
    GLuint gl_framebuffer;
    GLuint gl_color_buffer, gl_zeta_buffer;
    GraphicsSubchannel subchannel_data[NV2A_NUM_SUBCHANNELS];

    hwaddr dma_report;
    hwaddr report_offset;
    bool zpass_pixel_count_enable;
    unsigned int zpass_pixel_count_result;
    unsigned int gl_zpass_pixel_count_query_count;
    GLuint* gl_zpass_pixel_count_queries;

    hwaddr dma_vertex_a, dma_vertex_b;

    unsigned int primitive_mode;

    bool enable_vertex_program_write;

    uint32_t program_data[NV2A_MAX_TRANSFORM_PROGRAM_LENGTH][VSH_TOKEN_SIZE];

    VertexShaderConstant constants[NV2A_VERTEXSHADER_CONSTANTS];

    VertexAttribute vertex_attributes[NV2A_VERTEXSHADER_ATTRIBUTES];

    unsigned int inline_array_length;
    uint32_t inline_array[NV2A_MAX_BATCH_LENGTH];
    GLuint gl_inline_array_buffer;

    unsigned int inline_elements_length;
    uint32_t inline_elements[NV2A_MAX_BATCH_LENGTH];

    unsigned int inline_buffer_length;

    unsigned int draw_arrays_length;
    unsigned int draw_arrays_max_count;
    /* FIXME: Unknown size, possibly endless, 1000 will do for now */
    GLint gl_draw_arrays_start[1000];
    GLsizei gl_draw_arrays_count[1000];

    GLuint gl_element_buffer;
    GLuint gl_memory_buffer;

    GLuint gl_vertex_constants_buffer;

    GLuint gl_vertex_array;

    uint32_t regs[0x2000];
} PGRAPHState;


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

    bool pull_enabled;
    enum FIFOEngine bound_engines[NV2A_NUM_SUBCHANNELS];
    enum FIFOEngine last_engine;

    /* The actual command queue */
    QemuMutex cache_lock;
    QemuCond cache_cond;
    QSIMPLEQ_HEAD(, CacheEntry) cache;
    QSIMPLEQ_HEAD(, CacheEntry) working_cache;
} Cache1State;

typedef struct ChannelControl {
    hwaddr dma_put;
    hwaddr dma_get;
    uint32_t ref;
} ChannelControl;



typedef struct NV2AState {
    PCIDevice dev;
    qemu_irq irq;

    bool exiting;

    VGACommonState vga;
    GraphicHwOps hw_ops;

    QEMUTimer *vblank_timer;

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

        QemuThread puller_thread;
        Cache1State cache1;

        uint32_t regs[0x2000];
    } pfifo;

    struct {
        uint32_t regs[0x1000];
    } pvideo;

    struct {
        uint32_t pending_interrupts;
        uint32_t enabled_interrupts;

        uint32_t numerator;
        uint32_t denominator;

        uint32_t alarm_time;
    } ptimer;

    struct {
        uint32_t regs[0x1000];
    } pfb;

    struct PGRAPHState pgraph;

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

static uint64_t fnv_hash(const uint8_t *data, size_t len)
{
    /* 64 bit Fowler/Noll/Vo FNV-1a hash code */
    uint64_t hval = 0xcbf29ce484222325ULL;
    const uint8_t *dp = data;
    const uint8_t *de = data + len;
    while (dp < de) {
        hval ^= (uint64_t) *dp++;
        hval += (hval << 1) + (hval << 4) + (hval << 5) +
            (hval << 7) + (hval << 8) + (hval << 40);
    }

    return (guint)hval;
}

static uint64_t fast_hash(const uint8_t *data, size_t len, unsigned int samples)
{
#ifdef __SSE4_2__
    uint64_t h[4] = {len, 0, 0, 0};
    assert(samples > 0);

    if (len < 8 || len % 8) {
        return fnv_hash(data, len);
    }

    assert(len >= 8 && len % 8 == 0);
    const uint64_t *dp = (const uint64_t*)data;
    const uint64_t *de = dp + (len / 8);
    size_t step = len / 8 / samples;
    if (step == 0) step = 1;

    while (dp < de - step * 3) {
        h[0] = __builtin_ia32_crc32di(h[0], dp[step * 0]);
        h[1] = __builtin_ia32_crc32di(h[1], dp[step * 1]);
        h[2] = __builtin_ia32_crc32di(h[2], dp[step * 2]);
        h[3] = __builtin_ia32_crc32di(h[3], dp[step * 3]);
        dp += step * 4;
    }
    if (dp < de - step * 0)
        h[0] = __builtin_ia32_crc32di(h[0], dp[step * 0]);
    if (dp < de - step * 1)
        h[1] = __builtin_ia32_crc32di(h[1], dp[step * 1]);
    if (dp < de - step * 2)
        h[2] = __builtin_ia32_crc32di(h[2], dp[step * 2]);

    return h[0] + (h[1] << 10) + (h[2] << 21) + (h[3] << 32);
#else
    return fnv_hash(data, len);
#endif
}

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
        NV2A_DPRINTF("raise irq\n");
        pci_irq_assert(&d->dev);
    } else {
        pci_irq_deassert(&d->dev);
    }
}

static uint32_t ramht_hash(NV2AState *d, uint32_t handle)
{
    unsigned int ramht_size =
        1 << (GET_MASK(d->pfifo.regs[NV_PFIFO_RAMHT], NV_PFIFO_RAMHT_SIZE)+12);

    /* XXX: Think this is different to what nouveau calculates... */
    unsigned int bits = ffs(ramht_size)-2;

    uint32_t hash = 0;
    while (handle) {
        hash ^= (handle & ((1 << bits) - 1));
        handle >>= bits;
    }
    hash ^= d->pfifo.cache1.channel_id << (bits - 4);

    return hash;
}


static RAMHTEntry ramht_lookup(NV2AState *d, uint32_t handle)
{
    unsigned int ramht_size =
        1 << (GET_MASK(d->pfifo.regs[NV_PFIFO_RAMHT], NV_PFIFO_RAMHT_SIZE)+12);

    uint32_t hash = ramht_hash(d, handle);
    assert(hash * 8 < ramht_size);

    uint32_t ramht_address =
        GET_MASK(d->pfifo.regs[NV_PFIFO_RAMHT],
                 NV_PFIFO_RAMHT_BASE_ADDRESS) << 12;

    uint8_t *entry_ptr = d->ramin_ptr + ramht_address + hash * 8;

    uint32_t entry_handle = ldl_le_p((uint32_t*)entry_ptr);
    uint32_t entry_context = ldl_le_p((uint32_t*)(entry_ptr + 4));

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
    uint32_t flags = ldl_le_p(dma_obj);
    uint32_t limit = ldl_le_p(dma_obj + 1);
    uint32_t frame = ldl_le_p(dma_obj + 2);

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
    NV2A_DPRINTF("dma_map %x, %x, %" HWADDR_PRIx " %" HWADDR_PRIx "\n",
                 dma.dma_class, dma.dma_target, dma.address, dma.limit);

    dma.address &= 0x07FFFFFF;

    // assert(dma.address + dma.limit < memory_region_size(d->vram));
    *len = dma.limit;
    return d->vram_ptr + dma.address;
}

static void load_graphics_object(NV2AState *d, hwaddr instance_address,
                                 GraphicsObject *obj)
{
    uint8_t *obj_ptr;
    uint32_t switch1, switch2, switch3;

    assert(instance_address < memory_region_size(&d->ramin));

    obj_ptr = d->ramin_ptr + instance_address;

    switch1 = ldl_le_p((uint32_t*)obj_ptr);
    switch2 = ldl_le_p((uint32_t*)(obj_ptr+4));
    switch3 = ldl_le_p((uint32_t*)(obj_ptr+8));

    obj->graphics_class = switch1 & NV_PGRAPH_CTX_SWITCH1_GRCLASS;

    /* init graphics object */
    switch (obj->graphics_class) {
    case NV_KELVIN_PRIMITIVE:
        // kelvin->vertex_attributes[NV2A_VERTEX_ATTR_DIFFUSE].inline_value = 0xFFFFFFF;
        break;
    default:
        break;
    }
}

static GraphicsObject* lookup_graphics_object(PGRAPHState *s,
                                              hwaddr instance_address)
{
    int i;
    for (i=0; i<NV2A_NUM_SUBCHANNELS; i++) {
        if (s->subchannel_data[i].object_instance == instance_address) {
            return &s->subchannel_data[i].object;
        }
    }
    return NULL;
}

/* 16 bit to [0.0, F16_MAX = 511.9375] */
static float convert_f16_to_float(uint16_t f16) {
    if (f16 == 0x0000) { return 0.0; }
    uint32_t i = (f16 << 11) + 0x3C000000;
    return *(float*)&i;
}

/* 24 bit to [0.0, F24_MAX] */
static float convert_f24_to_float(uint32_t f24) {
    assert(!(f24 >> 24));
    f24 &= 0xFFFFFF;
    if (f24 == 0x000000) { return 0.0; }
    uint32_t i = f24 << 7;
    return *(float*)&i;
}

static void pgraph_update_memory_buffer(NV2AState *d, hwaddr addr, hwaddr size,
                                        bool f)
{
    glBindBuffer(GL_ARRAY_BUFFER, d->pgraph.gl_memory_buffer);

    hwaddr end = TARGET_PAGE_ALIGN(addr + size);
    addr &= TARGET_PAGE_MASK;
    assert(end < memory_region_size(d->vram));
    if (f || memory_region_test_and_clear_dirty(d->vram,
                                                addr,
                                                end - addr,
                                                DIRTY_MEMORY_NV2A)) {
        glBufferSubData(GL_ARRAY_BUFFER, addr, end - addr, d->vram_ptr + addr);
    }
}

static void pgraph_bind_vertex_attributes(NV2AState *d,
                                          unsigned int num_elements,
                                          bool inline_data,
                                          unsigned int inline_stride)
{
    int i, j;
    PGRAPHState *pg = &d->pgraph;

    if (inline_data) {
        NV2A_GL_DGROUP_BEGIN("%s (num_elements: %d inline stride: %d)",
                             __func__, num_elements, inline_stride);
    } else {
        NV2A_GL_DGROUP_BEGIN("%s (num_elements: %d)", __func__, num_elements);
    }

    for (i=0; i<NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *attribute = &pg->vertex_attributes[i];
        if (attribute->count) {
            uint8_t *data;
            unsigned int in_stride;
            if (inline_data && attribute->needs_conversion) {
                data = (uint8_t*)pg->inline_array
                        + attribute->inline_array_offset;
                in_stride = inline_stride;
            } else {
                hwaddr dma_len;
                if (attribute->dma_select) {
                    data = nv_dma_map(d, pg->dma_vertex_b, &dma_len);
                } else {
                    data = nv_dma_map(d, pg->dma_vertex_a, &dma_len);
                }

                assert(attribute->offset < dma_len);
                data += attribute->offset;

                in_stride = attribute->stride;
            }

            if (attribute->needs_conversion) {
                NV2A_DPRINTF("converted %d\n", i);

                unsigned int out_stride = attribute->converted_size
                                        * attribute->converted_count;

                if (num_elements > attribute->converted_elements) {
                    attribute->converted_buffer = g_realloc(
                        attribute->converted_buffer,
                        num_elements * out_stride);
                }

                for (j=attribute->converted_elements; j<num_elements; j++) {
                    uint8_t *in = data + j * in_stride;
                    uint8_t *out = attribute->converted_buffer + j * out_stride;

                    switch (attribute->format) {
                    case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP: {
                        uint32_t p = ldl_le_p((uint32_t*)in);
                        float *xyz = (float*)out;
                        xyz[0] = ((int32_t)(((p >>  0) & 0x7FF) << 21) >> 21)
                                                                      / 1023.0f;
                        xyz[1] = ((int32_t)(((p >> 11) & 0x7FF) << 21) >> 21)
                                                                      / 1023.0f;
                        xyz[2] = ((int32_t)(((p >> 22) & 0x3FF) << 22) >> 22)
                                                                       / 511.0f;
                        break;
                    }
                    default:
                        assert(false);
                        break;
                    }
                }


                glBindBuffer(GL_ARRAY_BUFFER, attribute->gl_converted_buffer);
                if (num_elements != attribute->converted_elements) {
                    glBufferData(GL_ARRAY_BUFFER,
                                 num_elements * out_stride,
                                 attribute->converted_buffer,
                                 GL_DYNAMIC_DRAW);
                    attribute->converted_elements = num_elements;
                }


                glVertexAttribPointer(i,
                    attribute->converted_count,
                    attribute->gl_type,
                    attribute->gl_normalize,
                    out_stride,
                    0);
            } else if (inline_data) {
                glBindBuffer(GL_ARRAY_BUFFER, pg->gl_inline_array_buffer);
                glVertexAttribPointer(i,
                                      attribute->gl_count,
                                      attribute->gl_type,
                                      attribute->gl_normalize,
                                      inline_stride,
                                      (void*)(uintptr_t)attribute->inline_array_offset);
            } else {
                hwaddr addr = data - d->vram_ptr;
                pgraph_update_memory_buffer(d, addr,
                                            num_elements * attribute->stride,
                                            false);
                glVertexAttribPointer(i,
                    attribute->gl_count,
                    attribute->gl_type,
                    attribute->gl_normalize,
                    attribute->stride,
                    (void*)addr);
            }
            glEnableVertexAttribArray(i);
        } else {
            glDisableVertexAttribArray(i);

            glVertexAttrib4fv(i, attribute->inline_value);
        }
    }
    NV2A_GL_DGROUP_END();
}

static unsigned int pgraph_bind_inline_array(NV2AState *d)
{
    int i;

    PGRAPHState *pg = &d->pgraph;

    unsigned int offset = 0;
    for (i=0; i<NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *attribute = &pg->vertex_attributes[i];
        if (attribute->count) {
            attribute->inline_array_offset = offset;

            NV2A_DPRINTF("bind inline attribute %d size=%d, count=%d\n",
                i, attribute->size, attribute->count);
            offset += attribute->size * attribute->count;
            assert(offset % 4 == 0);
        }
    }

    unsigned int vertex_size = offset;


    unsigned int index_count = pg->inline_array_length*4 / vertex_size;

    NV2A_DPRINTF("draw inline array %d, %d\n", vertex_size, index_count);

    glBindBuffer(GL_ARRAY_BUFFER, pg->gl_inline_array_buffer);
    glBufferData(GL_ARRAY_BUFFER, pg->inline_array_length*4, pg->inline_array,
                 GL_DYNAMIC_DRAW);

    pgraph_bind_vertex_attributes(d, index_count, true, vertex_size);

    return index_count;
}

static uint8_t cliptobyte(int x)
{
    return (uint8_t)((x < 0) ? 0 : ((x > 255) ? 255 : x));
}

static void convert_yuy2_to_rgb(const uint8_t *line, unsigned int ix,
                                uint8_t *r, uint8_t *g, uint8_t* b) {
    int c, d, e;
    c = (int)line[ix * 2] - 16;
    if (ix % 2) {
        d = (int)line[ix * 2 - 1] - 128;
        e = (int)line[ix * 2 + 1] - 128;
    } else {
        d = (int)line[ix * 2 + 1] - 128;
        e = (int)line[ix * 2 + 3] - 128;
    }
    *r = cliptobyte((298 * c + 409 * e + 128) >> 8);
    *g = cliptobyte((298 * c - 100 * d - 208 * e + 128) >> 8);
    *b = cliptobyte((298 * c + 516 * d + 128) >> 8);
}

static uint8_t* convert_texture_data(const TextureShape s,
                                     const uint8_t *data,
                                     const uint8_t *palette_data,
                                     unsigned int width,
                                     unsigned int height,
                                     unsigned int depth,
                                     unsigned int row_pitch,
                                     unsigned int slice_pitch)
{
    if (s.color_format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8) {
        assert(depth == 1); /* FIXME */
        uint8_t* converted_data = g_malloc(width * height * 4);
        int x, y;
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                uint8_t index = data[y * row_pitch + x];
                uint32_t color = *(uint32_t*)(palette_data + index * 4);
                *(uint32_t*)(converted_data + y * width * 4 + x * 4) = color;
            }
        }
        return converted_data;
    } else if (s.color_format
                   == NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_CR8YB8CB8YA8) {
        assert(depth == 1); /* FIXME */
        uint8_t* converted_data = g_malloc(width * height * 4);
        int x, y;
        for (y = 0; y < height; y++) {
            const uint8_t* line = &data[y * s.width * 2];
            for (x = 0; x < width; x++) {
                uint8_t* pixel = &converted_data[(y * s.width + x) * 4];
                /* FIXME: Actually needs uyvy? */
                convert_yuy2_to_rgb(line, x, &pixel[0], &pixel[1], &pixel[2]);
                pixel[3] = 255;
          }
        }
        return converted_data;
    } else if (s.color_format
                   == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5) {
        assert(depth == 1); /* FIXME */
        uint8_t *converted_data = g_malloc(width * height * 3);
        int x, y;
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                uint16_t rgb655 = *(uint16_t*)(data + y * row_pitch + x * 2);
                int8_t *pixel = (int8_t*)&converted_data[(y * width + x) * 3];
                /* Maps 5 bit G and B signed value range to 8 bit
                 * signed values. R is probably unsigned.
                 */
                rgb655 ^= (1 << 9) | (1 << 4);
                pixel[0] = ((rgb655 & 0xFC00) >> 10) * 0x7F / 0x3F;
                pixel[1] = ((rgb655 & 0x03E0) >> 5) * 0xFF / 0x1F - 0x80;
                pixel[2] = (rgb655 & 0x001F) * 0xFF / 0x1F - 0x80;
            }
        }
        return converted_data;
    } else {
        return NULL;
    }
}

static void upload_gl_texture(GLenum gl_target,
                              const TextureShape s,
                              const uint8_t *texture_data,
                              const uint8_t *palette_data)
{
    ColorFormatInfo f = kelvin_color_format_map[s.color_format];

    switch(gl_target) {
    case GL_TEXTURE_1D:
        assert(false);
        break;
    case GL_TEXTURE_RECTANGLE: {
        /* Can't handle strides unaligned to pixels */
        assert(s.pitch % f.bytes_per_pixel == 0);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,
                      s.pitch / f.bytes_per_pixel);

        uint8_t *converted = convert_texture_data(s, texture_data,
                                                  palette_data,
                                                  s.width, s.height, 1,
                                                  s.pitch, 0);

        glTexImage2D(gl_target, 0, f.gl_internal_format,
                     s.width, s.height, 0,
                     f.gl_format, f.gl_type,
                     converted ? converted : texture_data);

        if (converted) {
          g_free(converted);
        }

        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        break;
    }
    case GL_TEXTURE_2D:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z: {

        unsigned int width = s.width, height = s.height;

        int level;
        for (level = 0; level < s.levels; level++) {
            if (f.gl_format == 0) { /* compressed */

                width = MAX(width, 4); height = MAX(height, 4);

                unsigned int block_size;
                if (f.gl_internal_format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) {
                    block_size = 8;
                } else {
                    block_size = 16;
                }

                glCompressedTexImage2D(gl_target, level, f.gl_internal_format,
                                       width, height, 0,
                                       width/4 * height/4 * block_size,
                                       texture_data);

                texture_data += width/4 * height/4 * block_size;
            } else {

                width = MAX(width, 1); height = MAX(height, 1);

                unsigned int pitch = width * f.bytes_per_pixel;
                uint8_t *unswizzled = g_malloc(height * pitch);
                unswizzle_rect(texture_data, width, height,
                               unswizzled, pitch, f.bytes_per_pixel);

                uint8_t *converted = convert_texture_data(s, unswizzled,
                                                          palette_data,
                                                          width, height, 1,
                                                          pitch, 0);

                glTexImage2D(gl_target, level, f.gl_internal_format,
                             width, height, 0,
                             f.gl_format, f.gl_type,
                             converted ? converted : unswizzled);

                if (converted) {
                    g_free(converted);
                }
                g_free(unswizzled);

                texture_data += width * height * f.bytes_per_pixel;
            }

            width /= 2;
            height /= 2;
        }

        break;
    }
    case GL_TEXTURE_3D: {

        unsigned int width = s.width, height = s.height, depth = s.depth;

        assert(f.gl_format != 0); /* FIXME: compressed not supported yet */
        assert(f.linear == false);

        int level;
        for (level = 0; level < s.levels; level++) {

            unsigned int row_pitch = width * f.bytes_per_pixel;
            unsigned int slice_pitch = row_pitch * height;
            uint8_t *unswizzled = g_malloc(slice_pitch * depth);
            unswizzle_box(texture_data, width, height, depth, unswizzled,
                           row_pitch, slice_pitch, f.bytes_per_pixel);

            uint8_t *converted = convert_texture_data(s, unswizzled,
                                                      palette_data,
                                                      width, height, depth,
                                                      row_pitch, slice_pitch);

            glTexImage3D(gl_target, level, f.gl_internal_format,
                         width, height, depth, 0,
                         f.gl_format, f.gl_type,
                         converted ? converted : unswizzled);

            if (converted) {
                g_free(converted);
            }
            g_free(unswizzled);

            texture_data += width * height * depth * f.bytes_per_pixel;

            width /= 2;
            height /= 2;
            depth /= 2;
        }
        break;
    }
    default:
        assert(false);
        break;
    }
}

static TextureBinding* generate_texture(const TextureShape s,
                                        const uint8_t *texture_data,
                                        const uint8_t *palette_data)
{
    ColorFormatInfo f = kelvin_color_format_map[s.color_format];

    /* Create a new opengl texture */
    GLuint gl_texture;
    glGenTextures(1, &gl_texture);

    GLenum gl_target;
    if (s.cubemap) {
        assert(f.linear == false);
        assert(s.dimensionality == 2);
        gl_target = GL_TEXTURE_CUBE_MAP;
    } else {
        if (f.linear) {
            /* linear textures use unnormalised texcoords.
             * GL_TEXTURE_RECTANGLE_ARB conveniently also does, but
             * does not allow repeat and mirror wrap modes.
             *  (or mipmapping, but xbox d3d says 'Non swizzled and non
             *   compressed textures cannot be mip mapped.')
             * Not sure if that'll be an issue. */

            /* FIXME: GLSL 330 provides us with textureSize()! Use that? */
            gl_target = GL_TEXTURE_RECTANGLE;
            assert(s.dimensionality == 2);
        } else {
            switch(s.dimensionality) {
            case 1: gl_target = GL_TEXTURE_1D; break;
            case 2: gl_target = GL_TEXTURE_2D; break;
            case 3: gl_target = GL_TEXTURE_3D; break;
            default:
                assert(false);
                break;
            }
        }
    }

    glBindTexture(gl_target, gl_texture);

    NV2A_GL_DLABEL(GL_TEXTURE, gl_texture,
                   "format: 0x%02X%s, %d dimensions%s, width: %d, height: %d, depth: %d",
                   s.color_format, f.linear ? "" : " (SZ)",
                   s.dimensionality, s.cubemap ? " (Cubemap)" : "",
                   s.width, s.height, s.depth);

    if (gl_target == GL_TEXTURE_CUBE_MAP) {

        size_t length = 0;
        unsigned int w = s.width, h = s.height;
        int level;
        for (level = 0; level < s.levels; level++) {
            /* FIXME: This is wrong for compressed textures and textures with 1x? non-square mipmaps */
            length += w * h * f.bytes_per_pixel;
            w /= 2;
            h /= 2;
        }

        upload_gl_texture(GL_TEXTURE_CUBE_MAP_POSITIVE_X,
                          s, texture_data + 0 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
                          s, texture_data + 1 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
                          s, texture_data + 2 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
                          s, texture_data + 3 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
                          s, texture_data + 4 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
                          s, texture_data + 5 * length, palette_data);
    } else {
        upload_gl_texture(gl_target, s, texture_data, palette_data);
    }

    /* Linear textures don't support mipmapping */
    if (!f.linear) {
        glTexParameteri(gl_target, GL_TEXTURE_BASE_LEVEL,
            s.min_mipmap_level);
        glTexParameteri(gl_target, GL_TEXTURE_MAX_LEVEL,
            s.levels - 1);
    }

    if (f.gl_swizzle_mask[0] != 0 || f.gl_swizzle_mask[1] != 0
        || f.gl_swizzle_mask[2] != 0 || f.gl_swizzle_mask[3] != 0) {
        glTexParameteriv(gl_target, GL_TEXTURE_SWIZZLE_RGBA,
                         (const GLint *)f.gl_swizzle_mask);
    }

    TextureBinding* ret = g_malloc(sizeof(TextureBinding));
    ret->gl_target = gl_target;
    ret->gl_texture = gl_texture;
    ret->refcnt = 1;
    return ret;
}

/* functions for texture LRU cache */
static guint texture_key_hash(gconstpointer key)
{
    const TextureKey *k = key;
    uint64_t state_hash = fnv_hash(
        (const uint8_t*)&k->state, sizeof(TextureShape));
    return state_hash ^ k->data_hash;
}
static gboolean texture_key_equal(gconstpointer a, gconstpointer b)
{
    const TextureKey *ak = a, *bk = b;
    return memcmp(&ak->state, &bk->state, sizeof(TextureShape)) == 0
            && ak->data_hash == bk->data_hash;
}
static gpointer texture_key_retrieve(gpointer key, gpointer user_data)
{
    const TextureKey *k = key;
    TextureBinding *v = generate_texture(k->state,
                                         k->texture_data,
                                         k->palette_data);
    return v;
}
static void texture_key_destroy(gpointer data)
{
    g_free(data);
}
static void texture_binding_destroy(gpointer data)
{
    TextureBinding *binding = data;
    assert(binding->refcnt > 0);
    binding->refcnt--;
    if (binding->refcnt == 0) {
        glDeleteTextures(1, &binding->gl_texture);
        g_free(binding);
    }
}

static void pgraph_bind_textures(NV2AState *d)
{
    int i;
    PGRAPHState *pg = &d->pgraph;

    NV2A_GL_DGROUP_BEGIN("%s", __func__);

    for (i=0; i<NV2A_MAX_TEXTURES; i++) {

        uint32_t ctl_0 = pg->regs[NV_PGRAPH_TEXCTL0_0 + i*4];
        uint32_t ctl_1 = pg->regs[NV_PGRAPH_TEXCTL1_0 + i*4];
        uint32_t fmt = pg->regs[NV_PGRAPH_TEXFMT0 + i*4];
        uint32_t filter = pg->regs[NV_PGRAPH_TEXFILTER0 + i*4];
        uint32_t address =  pg->regs[NV_PGRAPH_TEXADDRESS0 + i*4];
        uint32_t palette =  pg->regs[NV_PGRAPH_TEXPALETTE0 + i*4];

        bool enabled = GET_MASK(ctl_0, NV_PGRAPH_TEXCTL0_0_ENABLE);
        unsigned int min_mipmap_level =
            GET_MASK(ctl_0, NV_PGRAPH_TEXCTL0_0_MIN_LOD_CLAMP);
        unsigned int max_mipmap_level =
            GET_MASK(ctl_0, NV_PGRAPH_TEXCTL0_0_MAX_LOD_CLAMP);

        unsigned int pitch =
            GET_MASK(ctl_1, NV_PGRAPH_TEXCTL1_0_IMAGE_PITCH);

        unsigned int dma_select =
            GET_MASK(fmt, NV_PGRAPH_TEXFMT0_CONTEXT_DMA);
        bool cubemap =
            GET_MASK(fmt, NV_PGRAPH_TEXFMT0_CUBEMAPENABLE);
        unsigned int dimensionality =
            GET_MASK(fmt, NV_PGRAPH_TEXFMT0_DIMENSIONALITY);
        unsigned int color_format = GET_MASK(fmt, NV_PGRAPH_TEXFMT0_COLOR);
        unsigned int levels = GET_MASK(fmt, NV_PGRAPH_TEXFMT0_MIPMAP_LEVELS);
        unsigned int log_width = GET_MASK(fmt, NV_PGRAPH_TEXFMT0_BASE_SIZE_U);
        unsigned int log_height = GET_MASK(fmt, NV_PGRAPH_TEXFMT0_BASE_SIZE_V);
        unsigned int log_depth = GET_MASK(fmt, NV_PGRAPH_TEXFMT0_BASE_SIZE_P);

        unsigned int rect_width =
            GET_MASK(pg->regs[NV_PGRAPH_TEXIMAGERECT0 + i*4],
                     NV_PGRAPH_TEXIMAGERECT0_WIDTH);
        unsigned int rect_height =
            GET_MASK(pg->regs[NV_PGRAPH_TEXIMAGERECT0 + i*4],
                     NV_PGRAPH_TEXIMAGERECT0_HEIGHT);

        unsigned int lod_bias =
            GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MIPMAP_LOD_BIAS);
        unsigned int min_filter = GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MIN);
        unsigned int mag_filter = GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MAG);

        unsigned int addru = GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRU);
        unsigned int addrv = GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRV);
        unsigned int addrp = GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRP);

        unsigned int border_source = GET_MASK(fmt,
                                              NV_PGRAPH_TEXFMT0_BORDER_SOURCE);
        uint32_t border_color = pg->regs[NV_PGRAPH_BORDERCOLOR0 + i*4];

        unsigned int offset = pg->regs[NV_PGRAPH_TEXOFFSET0 + i*4];

        bool palette_dma_select =
            GET_MASK(palette, NV_PGRAPH_TEXPALETTE0_CONTEXT_DMA);
        unsigned int palette_length_index =
            GET_MASK(palette, NV_PGRAPH_TEXPALETTE0_LENGTH);
        unsigned int palette_offset =
            palette & NV_PGRAPH_TEXPALETTE0_OFFSET;

        unsigned int palette_length = 0;
        switch (palette_length_index) {
        case NV_PGRAPH_TEXPALETTE0_LENGTH_256: palette_length = 256; break;
        case NV_PGRAPH_TEXPALETTE0_LENGTH_128: palette_length = 128; break;
        case NV_PGRAPH_TEXPALETTE0_LENGTH_64: palette_length = 64; break;
        case NV_PGRAPH_TEXPALETTE0_LENGTH_32: palette_length = 32; break;
        default: assert(false); break;
        }

        /* Check for unsupported features */
        assert(!(filter & NV_PGRAPH_TEXFILTER0_ASIGNED));
        assert(!(filter & NV_PGRAPH_TEXFILTER0_RSIGNED));
        assert(!(filter & NV_PGRAPH_TEXFILTER0_GSIGNED));
        assert(!(filter & NV_PGRAPH_TEXFILTER0_BSIGNED));

        glActiveTexture(GL_TEXTURE0 + i);
        if (!enabled) {
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            glBindTexture(GL_TEXTURE_RECTANGLE, 0);
            glBindTexture(GL_TEXTURE_1D, 0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindTexture(GL_TEXTURE_3D, 0);
            continue;
        }

        if (!pg->texture_dirty[i] && pg->texture_binding[i]) {
            glBindTexture(pg->texture_binding[i]->gl_target,
                          pg->texture_binding[i]->gl_texture);
            continue;
        }

        NV2A_DPRINTF(" texture %d is format 0x%x, (r %d, %d or %d, %d, %d; %d%s),"
                        " filter %x %x, levels %d-%d %d bias %d\n",
                     i, color_format,
                     rect_width, rect_height,
                     1 << log_width, 1 << log_height, 1 << log_depth,
                     pitch,
                     cubemap ? "; cubemap" : "",
                     min_filter, mag_filter,
                     min_mipmap_level, max_mipmap_level, levels,
                     lod_bias);

        assert(color_format < ARRAYSIZE(kelvin_color_format_map));
        ColorFormatInfo f = kelvin_color_format_map[color_format];
        if (f.bytes_per_pixel == 0) {
            fprintf(stderr, "nv2a: unimplemented texture color format 0x%x\n",
                    color_format);
            abort();
        }

        unsigned int width, height, depth;
        if (f.linear) {
            assert(dimensionality == 2);
            width = rect_width;
            height = rect_height;
            depth = 1;
        } else {
            width = 1 << log_width;
            height = 1 << log_height;
            depth = 1 << log_depth;

            /* FIXME: What about 3D mipmaps? */
            levels = MIN(levels, max_mipmap_level + 1);
            if (f.gl_format != 0) {
                /* Discard mipmap levels that would be smaller than 1x1.
                 * FIXME: Is this actually needed?
                 *
                 * >> Level 0: 32 x 4
                 *    Level 1: 16 x 2
                 *    Level 2: 8 x 1
                 *    Level 3: 4 x 1
                 *    Level 4: 2 x 1
                 *    Level 5: 1 x 1
                 */
                levels = MIN(levels, MAX(log_width, log_height) + 1);
            } else {
                /* OpenGL requires DXT textures to always have a width and
                 * height a multiple of 4. The Xbox and DirectX handles DXT
                 * textures smaller than 4 by padding the reset of the block.
                 *
                 * See:
                 * https://msdn.microsoft.com/en-us/library/windows/desktop/bb204843(v=vs.85).aspx
                 * https://msdn.microsoft.com/en-us/library/windows/desktop/bb694531%28v=vs.85%29.aspx#Virtual_Size
                 *
                 * Work around this for now by discarding mipmap levels that
                 * would result in too-small textures. A correct solution
                 * will be to decompress these levels manually, or add texture
                 * sampling logic.
                 *
                 * >> Level 0: 64 x 8
                 *    Level 1: 32 x 4
                 *    Level 2: 16 x 2 << Ignored
                 * >> Level 0: 16 x 16
                 *    Level 1: 8 x 8
                 *    Level 2: 4 x 4 << OK!
                 */
                if (log_width < 2 || log_height < 2) {
                    /* Base level is smaller than 4x4... */
                    levels = 1;
                } else {
                    levels = MIN(levels, MIN(log_width, log_height) - 1);
                }
            }
            assert(levels > 0);
        }

        hwaddr dma_len;
        uint8_t *texture_data;
        if (dma_select) {
            texture_data = nv_dma_map(d, pg->dma_b, &dma_len);
        } else {
            texture_data = nv_dma_map(d, pg->dma_a, &dma_len);
        }
        assert(offset < dma_len);
        texture_data += offset;

        hwaddr palette_dma_len;
        uint8_t *palette_data;
        if (palette_dma_select) {
            palette_data = nv_dma_map(d, pg->dma_b, &palette_dma_len);
        } else {
            palette_data = nv_dma_map(d, pg->dma_a, &palette_dma_len);
        }
        assert(palette_offset < palette_dma_len);
        palette_data += palette_offset;

        NV2A_DPRINTF(" - 0x%tx\n", texture_data - d->vram_ptr);

        size_t length = 0;
        if (f.linear) {
            assert(cubemap == false);
            assert(dimensionality == 2);
            length = height * pitch;
        } else {
            if (dimensionality >= 2) {
                unsigned int w = width, h = height;
                int level;
                if (f.gl_format != 0) {
                    for (level = 0; level < levels; level++) {
                        w = MAX(w, 1); h = MAX(h, 1);
                        length += w * h * f.bytes_per_pixel;
                        w /= 2;
                        h /= 2;
                    }
                } else {
                    /* Compressed textures are a bit different */
                    unsigned int block_size;
                    if (f.gl_internal_format ==
                            GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) {
                        block_size = 8;
                    } else {
                        block_size = 16;
                    }

                    for (level = 0; level < levels; level++) {
                        w = MAX(w, 4); h = MAX(h, 4);
                        length += w/4 * h/4 * block_size;
                        w /= 2; h /= 2;
                    }
                }
                if (cubemap) {
                    assert(dimensionality == 2);
                    length *= 6;
                }
                if (dimensionality >= 3) {
                    length *= depth;
                }
            }
        }

        TextureShape state = {
            .cubemap = cubemap,
            .dimensionality = dimensionality,
            .color_format = color_format,
            .levels = levels,
            .width = width,
            .height = height,
            .depth = depth,
            .min_mipmap_level = min_mipmap_level,
            .max_mipmap_level = max_mipmap_level,
            .pitch = pitch,
        };

#ifdef USE_TEXTURE_CACHE
        TextureKey key = {
            .state = state,
            .data_hash = fast_hash(texture_data, length, 5003)
                            ^ fnv_hash(palette_data, palette_length),
            .texture_data = texture_data,
            .palette_data = palette_data,
        };

        gpointer cache_key = g_malloc(sizeof(TextureKey));
        memcpy(cache_key, &key, sizeof(TextureKey));

        TextureBinding *binding = g_lru_cache_get(pg->texture_cache, cache_key);
        assert(binding);
        binding->refcnt++;
#else
        TextureBinding *binding = generate_texture(state,
                                                   texture_data, palette_data);
#endif

        glBindTexture(binding->gl_target, binding->gl_texture);


        if (f.linear) {
            /* somtimes games try to set mipmap min filters on linear textures.
             * this could indicate a bug... */
            switch (min_filter) {
            case NV_PGRAPH_TEXFILTER0_MIN_BOX_NEARESTLOD:
            case NV_PGRAPH_TEXFILTER0_MIN_BOX_TENT_LOD:
                min_filter = NV_PGRAPH_TEXFILTER0_MIN_BOX_LOD0;
                break;
            case NV_PGRAPH_TEXFILTER0_MIN_TENT_NEARESTLOD:
            case NV_PGRAPH_TEXFILTER0_MIN_TENT_TENT_LOD:
                min_filter = NV_PGRAPH_TEXFILTER0_MIN_TENT_LOD0;
                break;
            }
        }

        glTexParameteri(binding->gl_target, GL_TEXTURE_MIN_FILTER,
            pgraph_texture_min_filter_map[min_filter]);
        glTexParameteri(binding->gl_target, GL_TEXTURE_MAG_FILTER,
            pgraph_texture_mag_filter_map[mag_filter]);

        /* Texture wrapping */
        assert(addru < ARRAYSIZE(pgraph_texture_addr_map));
        glTexParameteri(binding->gl_target, GL_TEXTURE_WRAP_S,
            pgraph_texture_addr_map[addru]);
        if (dimensionality > 1) {
            assert(addrv < ARRAYSIZE(pgraph_texture_addr_map));
            glTexParameteri(binding->gl_target, GL_TEXTURE_WRAP_T,
                pgraph_texture_addr_map[addrv]);
        }
        if (dimensionality > 2) {
            assert(addrp < ARRAYSIZE(pgraph_texture_addr_map));
            glTexParameteri(binding->gl_target, GL_TEXTURE_WRAP_R,
                pgraph_texture_addr_map[addrp]);
        }

        /* FIXME: Only upload if necessary? [s, t or r = GL_CLAMP_TO_BORDER] */
        if (border_source == NV_PGRAPH_TEXFMT0_BORDER_SOURCE_COLOR) {
            GLfloat gl_border_color[] = {
                /* FIXME: Color channels might be wrong order */
                ((border_color >> 16) & 0xFF) / 255.0f, /* red */
                ((border_color >> 8) & 0xFF) / 255.0f,  /* green */
                (border_color & 0xFF) / 255.0f,         /* blue */
                ((border_color >> 24) & 0xFF) / 255.0f  /* alpha */
            };
            glTexParameterfv(binding->gl_target, GL_TEXTURE_BORDER_COLOR,
                gl_border_color);
        }

        if (pg->texture_binding[i]) {
            texture_binding_destroy(pg->texture_binding[i]);
        }
        pg->texture_binding[i] = binding;
        pg->texture_dirty[i] = false;
    }
    NV2A_GL_DGROUP_END();
}

static void pgraph_apply_anti_aliasing_factor(PGRAPHState *pg,
                                              unsigned int *width,
                                              unsigned int *height)
{
    switch (pg->surface_shape.anti_aliasing) {
    case NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_1:
        break;
    case NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_CORNER_2:
        if (width) { *width *= 2; }
        break;
    case NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_SQUARE_OFFSET_4:
        if (width) { *width *= 2; }
        if (height) { *height *= 2; }
        break;
    default:
        assert(false);
        break;
    }
}

static void pgraph_get_surface_dimensions(PGRAPHState *pg,
                                          unsigned int *width,
                                          unsigned int *height)
{
    bool swizzle = (pg->surface_type == NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE);
    if (swizzle) {
        *width = 1 << pg->surface_shape.log_width;
        *height = 1 << pg->surface_shape.log_height;
    } else {
        *width = pg->surface_shape.clip_width;
        *height = pg->surface_shape.clip_height;
    }
}

/* hash and equality for shader cache hash table */
static guint shader_hash(gconstpointer key)
{
    return fnv_hash(key, sizeof(ShaderState));
}
static gboolean shader_equal(gconstpointer a, gconstpointer b)
{
    const ShaderState *as = a, *bs = b;
    return memcmp(as, bs, sizeof(ShaderState)) == 0;
}

static void pgraph_bind_shaders(PGRAPHState *pg)
{
    int i, j;

    bool vertex_program = GET_MASK(pg->regs[NV_PGRAPH_CSV0_D],
                                   NV_PGRAPH_CSV0_D_MODE) == 2;

    bool fixed_function = GET_MASK(pg->regs[NV_PGRAPH_CSV0_D],
                                   NV_PGRAPH_CSV0_D_MODE) == 0;

    int program_start = GET_MASK(pg->regs[NV_PGRAPH_CSV0_C],
                                 NV_PGRAPH_CSV0_C_CHEOPS_PROGRAM_START);

    NV2A_GL_DGROUP_BEGIN("%s (VP: %s FFP: %s)", __func__,
                         vertex_program ? "yes" : "no",
                         fixed_function ? "yes" : "no");

    ShaderBinding* old_binding = pg->shader_binding;
    bool binding_changed = false;

    ShaderState state = {
        .psh = (PshState){
            /* register combier stuff */
            .combiner_control = pg->regs[NV_PGRAPH_COMBINECTL],
            .shader_stage_program = pg->regs[NV_PGRAPH_SHADERPROG],
            .other_stage_input = pg->regs[NV_PGRAPH_SHADERCTL],
            .final_inputs_0 = pg->regs[NV_PGRAPH_COMBINESPECFOG0],
            .final_inputs_1 = pg->regs[NV_PGRAPH_COMBINESPECFOG1],

            .alpha_test = pg->regs[NV_PGRAPH_CONTROL_0]
                            & NV_PGRAPH_CONTROL_0_ALPHATESTENABLE,
            .alpha_func = GET_MASK(pg->regs[NV_PGRAPH_CONTROL_0],
                                   NV_PGRAPH_CONTROL_0_ALPHAFUNC),
        },

        /* fixed function stuff */
        .skinning = GET_MASK(pg->regs[NV_PGRAPH_CSV0_D],
                             NV_PGRAPH_CSV0_D_SKIN),
        .lighting = GET_MASK(pg->regs[NV_PGRAPH_CSV0_C],
                             NV_PGRAPH_CSV0_C_LIGHTING),
        .normalization = pg->regs[NV_PGRAPH_CSV0_C]
                           & NV_PGRAPH_CSV0_C_NORMALIZATION_ENABLE,

        .fixed_function = fixed_function,

        /* vertex program stuff */
        .vertex_program = vertex_program,
        .z_perspective = pg->regs[NV_PGRAPH_CONTROL_0]
                            & NV_PGRAPH_CONTROL_0_Z_PERSPECTIVE_ENABLE,

        /* geometry shader stuff */
        .primitive_mode = pg->primitive_mode,
        .polygon_front_mode = GET_MASK(pg->regs[NV_PGRAPH_SETUPRASTER],
                                       NV_PGRAPH_SETUPRASTER_FRONTFACEMODE),
        .polygon_back_mode = GET_MASK(pg->regs[NV_PGRAPH_SETUPRASTER],
                                      NV_PGRAPH_SETUPRASTER_BACKFACEMODE),
    };

    state.program_length = 0;
    memset(state.program_data, 0, sizeof(state.program_data));

    if (vertex_program) {
        // copy in vertex program tokens
        for (i = program_start; i < NV2A_MAX_TRANSFORM_PROGRAM_LENGTH; i++) {
            uint32_t *cur_token = (uint32_t*)&pg->program_data[i];
            memcpy(&state.program_data[state.program_length],
                   cur_token,
                   VSH_TOKEN_SIZE * sizeof(uint32_t));
            state.program_length++;

            if (vsh_get_field(cur_token, FLD_FINAL)) {
                break;
            }
        }
    }

    /* Texgen */
    for (i = 0; i < 4; i++) {
        unsigned int reg = (i < 2) ? NV_PGRAPH_CSV1_A : NV_PGRAPH_CSV1_B;
        for (j = 0; j < 4; j++) {
            unsigned int masks[] = {
                (i % 2) ? NV_PGRAPH_CSV1_A_T1_S : NV_PGRAPH_CSV1_A_T0_S,
                (i % 2) ? NV_PGRAPH_CSV1_A_T1_T : NV_PGRAPH_CSV1_A_T0_T,
                (i % 2) ? NV_PGRAPH_CSV1_A_T1_R : NV_PGRAPH_CSV1_A_T0_R,
                (i % 2) ? NV_PGRAPH_CSV1_A_T1_Q : NV_PGRAPH_CSV1_A_T0_Q
            };
            state.texgen[i][j] = GET_MASK(pg->regs[reg], masks[j]);
        }
    }

    /* Fog */
    state.fog_enable = pg->regs[NV_PGRAPH_CONTROL_3]
                           & NV_PGRAPH_CONTROL_3_FOGENABLE;
    if (state.fog_enable) {
        /*FIXME: Use CSV0_D? */
        state.fog_mode = GET_MASK(pg->regs[NV_PGRAPH_CONTROL_3],
                                  NV_PGRAPH_CONTROL_3_FOG_MODE);
        state.foggen = GET_MASK(pg->regs[NV_PGRAPH_CSV0_D],
                                NV_PGRAPH_CSV0_D_FOGGENMODE);
    } else {
        /* FIXME: Do we still pass the fogmode? */
        state.fog_mode = 0;
        state.foggen = 0;
    }

    /* Texture matrices */
    for (i = 0; i < 4; i++) {
        state.texture_matrix_enable[i] = pg->texture_matrix_enable[i];
    }

    /* Lighting */
    if (state.lighting) {
        for (i = 0; i < NV2A_MAX_LIGHTS; i++) {
            state.light[i] = GET_MASK(pg->regs[NV_PGRAPH_CSV0_D],
                                      NV_PGRAPH_CSV0_D_LIGHT0 << (i * 2));
        }
    }

    for (i = 0; i < 8; i++) {
        state.psh.rgb_inputs[i] = pg->regs[NV_PGRAPH_COMBINECOLORI0 + i * 4];
        state.psh.rgb_outputs[i] = pg->regs[NV_PGRAPH_COMBINECOLORO0 + i * 4];
        state.psh.alpha_inputs[i] = pg->regs[NV_PGRAPH_COMBINEALPHAI0 + i * 4];
        state.psh.alpha_outputs[i] = pg->regs[NV_PGRAPH_COMBINEALPHAO0 + i * 4];
        //constant_0[i] = pg->regs[NV_PGRAPH_COMBINEFACTOR0 + i * 4];
        //constant_1[i] = pg->regs[NV_PGRAPH_COMBINEFACTOR1 + i * 4];
    }

    for (i = 0; i < 4; i++) {
        state.psh.rect_tex[i] = false;
        bool enabled = pg->regs[NV_PGRAPH_TEXCTL0_0 + i*4]
                         & NV_PGRAPH_TEXCTL0_0_ENABLE;
        unsigned int color_format =
            GET_MASK(pg->regs[NV_PGRAPH_TEXFMT0 + i*4],
                     NV_PGRAPH_TEXFMT0_COLOR);

        if (enabled && kelvin_color_format_map[color_format].linear) {
            state.psh.rect_tex[i] = true;
        }

        for (j = 0; j < 4; j++) {
            state.psh.compare_mode[i][j] =
                (pg->regs[NV_PGRAPH_SHADERCLIPMODE] >> (4 * i + j)) & 1;
        }
        state.psh.alphakill[i] = pg->regs[NV_PGRAPH_TEXCTL0_0 + i*4]
                               & NV_PGRAPH_TEXCTL0_0_ALPHAKILLEN;
    }

    ShaderBinding* cached_shader = g_hash_table_lookup(pg->shader_cache, &state);
    if (cached_shader) {
        pg->shader_binding = cached_shader;
    } else {
        pg->shader_binding = generate_shaders(state);

        /* Bind uniform blocks */
        if (pg->shader_binding->gl_constants_loc != GL_INVALID_INDEX) {
            glUniformBlockBinding(pg->shader_binding->gl_program,
                                  pg->shader_binding->gl_constants_loc,
                                  0 /* FIXME: Use #define */);
        }

        /* cache it */
        ShaderState *cache_state = g_malloc(sizeof(*cache_state));
        memcpy(cache_state, &state, sizeof(*cache_state));
        g_hash_table_insert(pg->shader_cache, cache_state,
                            (gpointer)pg->shader_binding);
    }

    binding_changed = (pg->shader_binding != old_binding);

    glUseProgram(pg->shader_binding->gl_program);


    /* update combiner constants */
    for (i = 0; i<= 8; i++) {
        uint32_t constant[2];
        if (i == 8) {
            /* final combiner */
            constant[0] = pg->regs[NV_PGRAPH_SPECFOGFACTOR0];
            constant[1] = pg->regs[NV_PGRAPH_SPECFOGFACTOR1];
        } else {
            constant[0] = pg->regs[NV_PGRAPH_COMBINEFACTOR0 + i * 4];
            constant[1] = pg->regs[NV_PGRAPH_COMBINEFACTOR1 + i * 4];
        }

        for (j = 0; j < 2; j++) {
            GLint loc = pg->shader_binding->psh_constant_loc[i][j];
            if (loc != -1) {
                float value[4];
                value[0] = (float) ((constant[j] >> 16) & 0xFF) / 255.0f;
                value[1] = (float) ((constant[j] >> 8) & 0xFF) / 255.0f;
                value[2] = (float) (constant[j] & 0xFF) / 255.0f;
                value[3] = (float) ((constant[j] >> 24) & 0xFF) / 255.0f;

                glUniform4fv(loc, 1, value);
            }
        }
    }
    GLint alpha_ref_loc = glGetUniformLocation(pg->shader_binding->gl_program,
                                               "alphaRef");
    if (alpha_ref_loc != -1) {
        float alpha_ref = GET_MASK(pg->regs[NV_PGRAPH_CONTROL_0],
                                   NV_PGRAPH_CONTROL_0_ALPHAREF) / 255.0;
        glUniform1f(alpha_ref_loc, alpha_ref);
    }

    /* For each texture stage */
    for (i = 0; i < NV2A_MAX_TEXTURES; i++) {
        char name[32];
        GLint loc;

        /* Bump luminance only during stages 1 - 3 */
        if (i > 0) {

            snprintf(name, sizeof(name), "bumpMat%d", i);
            loc = glGetUniformLocation(pg->shader_binding->gl_program, name);
            if (loc != -1) {
                glUniformMatrix2fv(loc, 1, GL_FALSE, pg->bump_env_matrix[i - 1]);
            }

            snprintf(name, sizeof(name), "bumpScale%d", i);
            loc = glGetUniformLocation(pg->shader_binding->gl_program, name);
            if (loc != -1) {
                glUniform1f(loc, *(float*)&pg->regs[
                                NV_PGRAPH_BUMPSCALE1 + (i - 1) * 4]);
            }

            snprintf(name, sizeof(name), "bumpOffset%d", i);
            loc = glGetUniformLocation(pg->shader_binding->gl_program, name);
            if (loc != -1) {
                glUniform1f(loc, *(float*)&pg->regs[
                                NV_PGRAPH_BUMPOFFSET1 + (i - 1) * 4]);
            }

        }

        /* Texture matrices */
        snprintf(name, sizeof(name), "texMat%d", i);
        loc = glGetUniformLocation(pg->shader_binding->gl_program, name);
        if (loc != -1) {
            glUniformMatrix4fv(loc, 1, GL_FALSE, pg->texture_matrix[i]);
        }

        /* TexGen planes */
        for(j = 0; j < 4; j++) {
            char cSuffix = "STRQ"[j];
            snprintf(name, sizeof(name), "texPlane%c%d", cSuffix, i);
            loc = glGetUniformLocation(pg->shader_binding->gl_program, name);
            if (loc != -1) {
                glUniform4fv(loc, 1, pg->texture_plane[i][j]);
            }
        }

    }

    /* Fog */
    {
        GLint loc;
        uint32_t fog_color = pg->regs[NV_PGRAPH_FOGCOLOR];
        loc  = glGetUniformLocation(pg->shader_binding->gl_program, "fogColor");
        if (loc != -1) {
            glUniform4f(loc,
                        GET_MASK(fog_color, NV_PGRAPH_FOGCOLOR_RED) / 255.0,
                        GET_MASK(fog_color, NV_PGRAPH_FOGCOLOR_GREEN) / 255.0,
                        GET_MASK(fog_color, NV_PGRAPH_FOGCOLOR_BLUE) / 255.0,
                        GET_MASK(fog_color, NV_PGRAPH_FOGCOLOR_ALPHA) / 255.0);
        }

        /* FIXME: PGRAPH regs have a 16 byte stride in emulation! can't just
         *        upload this as an array =(
         */
        loc = glGetUniformLocation(pg->shader_binding->gl_program,
                                   "fogParam[0]");
        if (loc != -1) {
            glUniform1f(loc, *(float*)&pg->regs[NV_PGRAPH_FOGPARAM0]);
        }
        loc = glGetUniformLocation(pg->shader_binding->gl_program,
                                   "fogParam[1]");
        if (loc != -1) {
            glUniform1f(loc, *(float*)&pg->regs[NV_PGRAPH_FOGPARAM1]);
        }

        loc = glGetUniformLocation(pg->shader_binding->gl_program, "fogPlane");
        if (loc != -1) {
            glUniform4fv(loc, 1, pg->fog_plane);
        }
    }

    /* For each vertex weight */
    for (i = 0; i < 4; i++) {
        char name[32];
        GLint loc;

        snprintf(name, sizeof(name), "modelViewMat%d", i);
        loc = glGetUniformLocation(pg->shader_binding->gl_program, name);
        if (loc != -1) {
            glUniformMatrix4fv(loc, 1, GL_FALSE,
                               pg->model_view_matrix[i]);
        }

        snprintf(name, sizeof(name), "invModelViewMat%d", i);
        loc = glGetUniformLocation(pg->shader_binding->gl_program, name);
        if (loc != -1) {
            glUniformMatrix4fv(loc, 1, GL_FALSE,
                               pg->inverse_model_view_matrix[i]);
        }

    }

    GLint projLoc = glGetUniformLocation(pg->shader_binding->gl_program,
                                     "projectionMat");
    if (projLoc != -1) {
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, pg->projection_matrix);
    }

    GLint eyeVecLoc = glGetUniformLocation(pg->shader_binding->gl_program,
                                           "eyeVector");
    if (eyeVecLoc != -1) {
        glUniform3f(eyeVecLoc, *(float*)&pg->regs[NV_PGRAPH_EYEVEC0],
                               *(float*)&pg->regs[NV_PGRAPH_EYEVEC1],
                               *(float*)&pg->regs[NV_PGRAPH_EYEVEC2]);
    }
    GLint eyePosLoc = glGetUniformLocation(pg->shader_binding->gl_program,
                                           "eyePosition");
    if (eyePosLoc != -1) {
        glUniform4fv(eyePosLoc, 1, pg->eye_position);
    }
    GLint eyeDirLoc = glGetUniformLocation(pg->shader_binding->gl_program,
                                           "eyeDirection");
    if (eyeDirLoc != -1) {
        glUniform3fv(eyeDirLoc, 1, pg->eye_direction);
    }

    /* FIXME: Only do this if lighting is allowed? I'd believe lighting works
     *        with both FFP and VPs.
     */
    NV2A_GL_DGROUP_BEGIN("Lighting uniforms");
    GLint ambLoc = glGetUniformLocation(pg->shader_binding->gl_program,
                                        "sceneAmbientColor");
    if (ambLoc != -1) {
        glUniform3fv(ambLoc, 1, pg->scene_ambient_color);
    }
    for (i = 0; i < NV2A_MAX_LIGHTS; i++) {
        GLint loc;
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "backLightAmbientColor%d", i);
        loc = glGetUniformLocation(pg->shader_binding->gl_program, tmp);
        if (loc != -1) {
            glUniform3fv(loc, 1, pg->back_light_ambient_color[i]);
        }
        snprintf(tmp, sizeof(tmp), "backLightDiffuseColor%d", i);
        loc = glGetUniformLocation(pg->shader_binding->gl_program, tmp);
        if (loc != -1) {
            glUniform3fv(loc, 1, pg->back_light_diffuse_color[i]);
        }
        snprintf(tmp, sizeof(tmp), "backLightSpecularColor%d", i);
        loc = glGetUniformLocation(pg->shader_binding->gl_program, tmp);
        if (loc != -1) {
            glUniform3fv(loc, 1, pg->back_light_specular_color[i]);
        }
        snprintf(tmp, sizeof(tmp), "lightAmbientColor%d", i);
        loc = glGetUniformLocation(pg->shader_binding->gl_program, tmp);
        if (loc != -1) {
            glUniform3fv(loc, 1, pg->light_ambient_color[i]);
        }
        snprintf(tmp, sizeof(tmp), "lightDiffuseColor%d", i);
        loc = glGetUniformLocation(pg->shader_binding->gl_program, tmp);
        if (loc != -1) {
            glUniform3fv(loc, 1, pg->light_diffuse_color[i]);
        }
        snprintf(tmp, sizeof(tmp), "lightSpecularColor%d", i);
        loc = glGetUniformLocation(pg->shader_binding->gl_program, tmp);
        if (loc != -1) {
            glUniform3fv(loc, 1, pg->light_specular_color[i]);
        }
        snprintf(tmp, sizeof(tmp), "lightLocalRange%d", i);
        loc = glGetUniformLocation(pg->shader_binding->gl_program, tmp);
        if (loc != -1) {
            glUniform1f(loc, pg->light_local_range[i]);
        }
        snprintf(tmp, sizeof(tmp), "lightInfiniteHalfVector%d", i);
        loc = glGetUniformLocation(pg->shader_binding->gl_program, tmp);
        if (loc != -1) {
            glUniform3fv(loc, 1, pg->light_infinite_half_vector[i]);
        }
        snprintf(tmp, sizeof(tmp), "lightInfiniteDirection%d", i);
        loc = glGetUniformLocation(pg->shader_binding->gl_program, tmp);
        if (loc != -1) {
            glUniform3fv(loc, 1, pg->light_infinite_direction[i]);
        }
        snprintf(tmp, sizeof(tmp), "lightSpotFalloff%d", i);
        loc = glGetUniformLocation(pg->shader_binding->gl_program, tmp);
        if (loc != -1) {
            glUniform3fv(loc, 1, pg->light_spot_falloff[i]);
        }
        snprintf(tmp, sizeof(tmp), "lightSpotDirection%d", i);
        loc = glGetUniformLocation(pg->shader_binding->gl_program, tmp);
        if (loc != -1) {
            glUniform4fv(loc, 1, pg->light_spot_direction[i]);
        }
        snprintf(tmp, sizeof(tmp), "lightLocalPosition%d", i);
        loc = glGetUniformLocation(pg->shader_binding->gl_program, tmp);
        if (loc != -1) {
            glUniform3fv(loc, 1, pg->light_local_position[i]);
        }
        snprintf(tmp, sizeof(tmp), "lightLocalAttenuation%d", i);
        loc = glGetUniformLocation(pg->shader_binding->gl_program, tmp);
        if (loc != -1) {
            glUniform3fv(loc, 1, pg->light_local_attenuation[i]);
        }
    }
    NV2A_GL_DGROUP_END();

    float zclip_max = *(float*)&pg->regs[NV_PGRAPH_ZCLIPMAX];
    float zclip_min = *(float*)&pg->regs[NV_PGRAPH_ZCLIPMIN];

    if (fixed_function) {
        /* update fixed function composite matrix */

        GLint comLoc = glGetUniformLocation(pg->shader_binding->gl_program,
                                            "compositeMat");
        if (comLoc != -1) {
            glUniformMatrix4fv(comLoc, 1, GL_FALSE, pg->composite_matrix);
        }

        /* estimate the viewport by assuming it matches the surface ... */
        //FIXME: Get surface dimensions?
        float m11 = 0.5 * pg->surface_shape.clip_width;
        float m22 = -0.5 * pg->surface_shape.clip_height;
        float m33 = zclip_max - zclip_min;
        //float m41 = m11;
        //float m42 = -m22;
        float m43 = zclip_min;
        //float m44 = 1.0;

        if (m33 == 0.0) {
            m33 = 1.0;
        }
        float invViewport[16] = {
            1.0/m11, 0, 0, 0,
            0, 1.0/m22, 0, 0,
            0, 0, 1.0/m33, 0,
            -1.0, 1.0, -m43/m33, 1.0
        };

        GLint view_loc = glGetUniformLocation(pg->shader_binding->gl_program,
                                              "invViewport");
        if (view_loc != -1) {
            glUniformMatrix4fv(view_loc, 1, GL_FALSE, &invViewport[0]);
        }

    } else if (vertex_program) {
        /* update vertex program constants */

        GLint loc = glGetUniformLocation(pg->shader_binding->gl_program,
                                          "surfaceSize");
        if (loc != -1) {
            glUniform2f(loc, pg->surface_shape.clip_width,
                        pg->surface_shape.clip_height);
        }

        loc = glGetUniformLocation(pg->shader_binding->gl_program,
                                   "clipRange");
        if (loc != -1) {
            glUniform2f(loc, zclip_min, zclip_max);
        }

    }

    /* Update constants */
    if (pg->shader_binding->gl_constants_loc != GL_INVALID_INDEX) {
        /* FIXME: Can be done on buffer creation? */
        glBindBufferBase(GL_UNIFORM_BUFFER, 0 /* FIXME: Use def */,
                         pg->gl_vertex_constants_buffer);
        for (i=0; i<NV2A_VERTEXSHADER_CONSTANTS; i++) {
            VertexShaderConstant *constant = &pg->constants[i];
            if (!constant->dirty) continue;

            /* FIXME: Query GL_UNIFORM_ARRAY_STRIDE instead of assuming 16 */

            glBufferSubData(GL_UNIFORM_BUFFER, i * 16, 16, constant->data);
            constant->dirty = false;
        }
    }

    NV2A_GL_DGROUP_END();
}

static bool pgraph_framebuffer_dirty(PGRAPHState *pg)
{
    bool shape_changed = memcmp(&pg->surface_shape, &pg->last_surface_shape,
                                sizeof(SurfaceShape)) != 0;
    if (!shape_changed || (!pg->surface_shape.color_format
            && !pg->surface_shape.zeta_format)) {
        return false;
    }
    return true;
}

static bool pgraph_color_write_enabled(PGRAPHState *pg)
{
    return pg->regs[NV_PGRAPH_CONTROL_0] & (
        NV_PGRAPH_CONTROL_0_ALPHA_WRITE_ENABLE
        | NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE
        | NV_PGRAPH_CONTROL_0_GREEN_WRITE_ENABLE
        | NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE);
}

static bool pgraph_zeta_write_enabled(PGRAPHState *pg)
{
    return pg->regs[NV_PGRAPH_CONTROL_0] & (
        NV_PGRAPH_CONTROL_0_ZWRITEENABLE
        | NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE);
}

static void pgraph_set_surface_dirty(PGRAPHState *pg, bool color, bool zeta)
{
    NV2A_DPRINTF("pgraph_set_surface_dirty(%d, %d) -- %d %d\n",
                 color, zeta,
                 pgraph_color_write_enabled(pg), pgraph_zeta_write_enabled(pg));
    /* FIXME: Does this apply to CLEARs too? */
    color = color && pgraph_color_write_enabled(pg);
    zeta = zeta && pgraph_zeta_write_enabled(pg);
    pg->surface_color.draw_dirty |= color;
    pg->surface_zeta.draw_dirty |= zeta;
}

static void pgraph_update_surface_part(NV2AState *d, bool upload, bool color) {
    PGRAPHState *pg = &d->pgraph;

    unsigned int width, height;
    pgraph_get_surface_dimensions(pg, &width, &height);
    pgraph_apply_anti_aliasing_factor(pg, &width, &height);

    Surface *surface;
    hwaddr dma_address;
    GLuint *gl_buffer;
    unsigned int bytes_per_pixel;
    GLenum gl_internal_format, gl_format, gl_type, gl_attachment;

    if (color) {
        surface = &pg->surface_color;
        dma_address = pg->dma_color;
        gl_buffer = &pg->gl_color_buffer;

        assert(pg->surface_shape.color_format != 0);
        assert(pg->surface_shape.color_format
                < ARRAYSIZE(kelvin_surface_color_format_map));
        SurfaceColorFormatInfo f =
            kelvin_surface_color_format_map[pg->surface_shape.color_format];
        if (f.bytes_per_pixel == 0) {
            fprintf(stderr, "nv2a: unimplemented color surface format 0x%x\n",
                    pg->surface_shape.color_format);
            abort();
        }

        bytes_per_pixel = f.bytes_per_pixel;
        gl_internal_format = f.gl_internal_format;
        gl_format = f.gl_format;
        gl_type = f.gl_type;
        gl_attachment = GL_COLOR_ATTACHMENT0;

    } else {
        surface = &pg->surface_zeta;
        dma_address = pg->dma_zeta;
        gl_buffer = &pg->gl_zeta_buffer;

        assert(pg->surface_shape.zeta_format != 0);
        switch (pg->surface_shape.zeta_format) {
        case NV097_SET_SURFACE_FORMAT_ZETA_Z16:
            bytes_per_pixel = 2;
            gl_format = GL_DEPTH_COMPONENT;
            gl_attachment = GL_DEPTH_ATTACHMENT;
            if (pg->surface_shape.z_format) {
                gl_type = GL_HALF_FLOAT;
                gl_internal_format = GL_DEPTH_COMPONENT32F;
            } else {
                gl_type = GL_UNSIGNED_SHORT;
                gl_internal_format = GL_DEPTH_COMPONENT16;
            }
            break;
        case NV097_SET_SURFACE_FORMAT_ZETA_Z24S8:
            bytes_per_pixel = 4;
            gl_format = GL_DEPTH_STENCIL;
            gl_attachment = GL_DEPTH_STENCIL_ATTACHMENT;
            if (pg->surface_shape.z_format) {
                assert(false);
                gl_type = GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
                gl_internal_format = GL_DEPTH32F_STENCIL8;
            } else {
                gl_type = GL_UNSIGNED_INT_24_8;
                gl_internal_format = GL_DEPTH24_STENCIL8;
            }
            break;
        default:
            assert(false);
            break;
        }
    }


    DMAObject dma = nv_dma_load(d, dma_address);
    /* There's a bunch of bugs that could cause us to hit this function
     * at the wrong time and get a invalid dma object.
     * Check that it's sane. */
    assert(dma.dma_class == NV_DMA_IN_MEMORY_CLASS);

    assert(dma.address + surface->offset != 0);
    assert(surface->offset <= dma.limit);
    assert(surface->offset + surface->pitch * height <= dma.limit + 1);

    hwaddr data_len;
    uint8_t *data = nv_dma_map(d, dma_address, &data_len);

    /* TODO */
    // assert(pg->surface_clip_x == 0 && pg->surface_clip_y == 0);

    bool swizzle = (pg->surface_type == NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE);

    uint8_t *buf = data + surface->offset;
    if (swizzle) {
        buf = g_malloc(height * surface->pitch);
    }

    bool dirty = surface->buffer_dirty;
    if (color) {
        dirty |= memory_region_test_and_clear_dirty(d->vram,
                                               dma.address + surface->offset,
                                               surface->pitch * height,
                                               DIRTY_MEMORY_NV2A);
    }
    if (upload && dirty) {
        /* surface modified (or moved) by the cpu.
         * copy it into the opengl renderbuffer */
        assert(!surface->draw_dirty);

        assert(surface->pitch % bytes_per_pixel == 0);

        if (swizzle) {
            unswizzle_rect(data + surface->offset,
                           width, height,
                           buf,
                           surface->pitch,
                           bytes_per_pixel);
        }

        if (!color) {
            /* need to clear the depth_stencil and depth attachment for zeta */
            glFramebufferTexture2D(GL_FRAMEBUFFER,
                                   GL_DEPTH_ATTACHMENT,
                                   GL_TEXTURE_2D,
                                   0, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER,
                                   GL_DEPTH_STENCIL_ATTACHMENT,
                                   GL_TEXTURE_2D,
                                   0, 0);
        }

        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               gl_attachment,
                               GL_TEXTURE_2D,
                               0, 0);

        if (*gl_buffer) {
            glDeleteTextures(1, gl_buffer);
            *gl_buffer = 0;
        }

        glGenTextures(1, gl_buffer);
        glBindTexture(GL_TEXTURE_2D, *gl_buffer);

        /* This is VRAM so we can't do this inplace! */
        uint8_t *flipped_buf = g_malloc(width * height * bytes_per_pixel);
        unsigned int irow;
        for (irow = 0; irow < height; irow++) {
            memcpy(&flipped_buf[width * (height - irow - 1)
                                     * bytes_per_pixel],
                   &buf[surface->pitch * irow],
                   width * bytes_per_pixel);
        }

        glTexImage2D(GL_TEXTURE_2D, 0, gl_internal_format,
                     width, height, 0,
                     gl_format, gl_type,
                     flipped_buf);

        g_free(flipped_buf);

        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               gl_attachment,
                               GL_TEXTURE_2D,
                               *gl_buffer, 0);

        assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)
            == GL_FRAMEBUFFER_COMPLETE);

        if (color) {
            pgraph_update_memory_buffer(d, dma.address + surface->offset,
                                        surface->pitch * height, true);
        }
        surface->buffer_dirty = false;


        uint8_t *out = data + surface->offset + 64;
        NV2A_DPRINTF("upload_surface %s 0x%" HWADDR_PRIx " - 0x%" HWADDR_PRIx ", "
                      "(0x%" HWADDR_PRIx " - 0x%" HWADDR_PRIx ", "
                        "%d %d, %d %d, %d) - %x %x %x %x\n",
            color ? "color" : "zeta",
            dma.address, dma.address + dma.limit,
            dma.address + surface->offset,
            dma.address + surface->pitch * height,
            pg->surface_shape.clip_x, pg->surface_shape.clip_y,
            pg->surface_shape.clip_width,
            pg->surface_shape.clip_height,
            surface->pitch,
            out[0], out[1], out[2], out[3]);

    }

    if (!upload && surface->draw_dirty) {
        /* read the opengl framebuffer into the surface */

        glo_readpixels(gl_format, gl_type,
                       bytes_per_pixel, surface->pitch,
                       width, height,
                       buf);
        assert(glGetError() == GL_NO_ERROR);

        if (swizzle) {
            swizzle_rect(buf,
                         width, height,
                         data + surface->offset,
                         surface->pitch,
                         bytes_per_pixel);
        }

        memory_region_set_client_dirty(d->vram,
                                       dma.address + surface->offset,
                                       surface->pitch * height,
                                       DIRTY_MEMORY_VGA);

        if (color) {
            pgraph_update_memory_buffer(d, dma.address + surface->offset,
                                        surface->pitch * height, true);
        }

        surface->draw_dirty = false;
        surface->write_enabled_cache = false;

        uint8_t *out = data + surface->offset + 64;
        NV2A_DPRINTF("read_surface %s 0x%" HWADDR_PRIx " - 0x%" HWADDR_PRIx ", "
                      "(0x%" HWADDR_PRIx " - 0x%" HWADDR_PRIx ", "
                        "%d %d, %d %d, %d) - %x %x %x %x\n",
            color ? "color" : "zeta",
            dma.address, dma.address + dma.limit,
            dma.address + surface->offset,
            dma.address + surface->pitch * pg->surface_shape.clip_height,
            pg->surface_shape.clip_x, pg->surface_shape.clip_y,
            pg->surface_shape.clip_width, pg->surface_shape.clip_height,
            surface->pitch,
            out[0], out[1], out[2], out[3]);

    }

    if (swizzle) {
        g_free(buf);
    }
}

static void pgraph_update_surface(NV2AState *d, bool upload,
                                  bool color_write, bool zeta_write)
{
    PGRAPHState *pg = &d->pgraph;

    pg->surface_shape.z_format = GET_MASK(pg->regs[NV_PGRAPH_SETUPRASTER],
                                          NV_PGRAPH_SETUPRASTER_Z_FORMAT);

    /* FIXME: Does this apply to CLEARs too? */
    color_write = color_write && pgraph_color_write_enabled(pg);
    zeta_write = zeta_write && pgraph_zeta_write_enabled(pg);

    if (upload && pgraph_framebuffer_dirty(pg)) {
        assert(!pg->surface_color.draw_dirty);
        assert(!pg->surface_zeta.draw_dirty);

        pg->surface_color.buffer_dirty = true;
        pg->surface_zeta.buffer_dirty = true;

        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D,
                               0, 0);

        if (pg->gl_color_buffer) {
            glDeleteTextures(1, &pg->gl_color_buffer);
            pg->gl_color_buffer = 0;
        }

        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D,
                               0, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_DEPTH_STENCIL_ATTACHMENT,
                               GL_TEXTURE_2D,
                               0, 0);

        if (pg->gl_zeta_buffer) {
            glDeleteTextures(1, &pg->gl_zeta_buffer);
            pg->gl_zeta_buffer = 0;
        }

        memcpy(&pg->last_surface_shape, &pg->surface_shape,
               sizeof(SurfaceShape));
    }

    if ((color_write || (!upload && pg->surface_color.write_enabled_cache))
        && (upload || pg->surface_color.draw_dirty)) {
        pgraph_update_surface_part(d, upload, true);
    }


    if ((zeta_write || (!upload && pg->surface_zeta.write_enabled_cache))
        && (upload || pg->surface_zeta.draw_dirty)) {
        pgraph_update_surface_part(d, upload, false);
    }
}


static void pgraph_init(NV2AState *d)
{
    int i;

    PGRAPHState *pg = &d->pgraph;

    qemu_mutex_init(&pg->lock);
    qemu_cond_init(&pg->interrupt_cond);
    qemu_cond_init(&pg->fifo_access_cond);
    qemu_cond_init(&pg->flip_3d);

    /* fire up opengl */

    pg->gl_context = glo_context_create();
    assert(pg->gl_context);

#ifdef DEBUG_NV2A_GL
    glEnable(GL_DEBUG_OUTPUT);
#endif

    glextensions_init();

    /* DXT textures */
    assert(glo_check_extension("GL_EXT_texture_compression_s3tc"));
    /*  Internal RGB565 texture format */
    assert(glo_check_extension("GL_ARB_ES2_compatibility"));

    GLint max_vertex_attributes;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &max_vertex_attributes);
    assert(max_vertex_attributes >= NV2A_VERTEXSHADER_ATTRIBUTES);


    glGenFramebuffers(1, &pg->gl_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, pg->gl_framebuffer);

    /* need a valid framebuffer to start with */
    glGenTextures(1, &pg->gl_color_buffer);
    glBindTexture(GL_TEXTURE_2D, pg->gl_color_buffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 640, 480,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, pg->gl_color_buffer, 0);

    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)
            == GL_FRAMEBUFFER_COMPLETE);

    //glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );

    pg->texture_cache = g_lru_cache_new(
        texture_key_hash, texture_key_equal,
        NULL, texture_key_retrieve,
        texture_key_destroy, texture_binding_destroy,
        NULL, NULL);
    g_lru_cache_set_max_size(pg->texture_cache, 512);

    pg->shader_cache = g_hash_table_new(shader_hash, shader_equal);


    for (i=0; i<NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        glGenBuffers(1, &pg->vertex_attributes[i].gl_converted_buffer);
        glGenBuffers(1, &pg->vertex_attributes[i].gl_inline_buffer);
    }
    glGenBuffers(1, &pg->gl_inline_array_buffer);
    glGenBuffers(1, &pg->gl_element_buffer);

    glGenBuffers(1, &pg->gl_vertex_constants_buffer);
    glBindBuffer(GL_UNIFORM_BUFFER, pg->gl_vertex_constants_buffer);
    glBufferData(GL_UNIFORM_BUFFER,
                 16 * NV2A_VERTEXSHADER_CONSTANTS,
                 NULL,
                 GL_DYNAMIC_DRAW);

    glGenBuffers(1, &pg->gl_memory_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, pg->gl_memory_buffer);
    glBufferData(GL_ARRAY_BUFFER,
                 memory_region_size(d->vram),
                 NULL,
                 GL_DYNAMIC_DRAW);

    glGenVertexArrays(1, &pg->gl_vertex_array);
    glBindVertexArray(pg->gl_vertex_array);

    assert(glGetError() == GL_NO_ERROR);

    glo_set_current(NULL);
}

static void pgraph_destroy(PGRAPHState *pg)
{
    qemu_mutex_destroy(&pg->lock);
    qemu_cond_destroy(&pg->interrupt_cond);
    qemu_cond_destroy(&pg->fifo_access_cond);
    qemu_cond_destroy(&pg->flip_3d);

    glo_set_current(pg->gl_context);

    if (pg->gl_color_buffer) {
        glDeleteTextures(1, &pg->gl_color_buffer);
    }
    if (pg->gl_zeta_buffer) {
        glDeleteTextures(1, &pg->gl_zeta_buffer);
    }
    glDeleteFramebuffers(1, &pg->gl_framebuffer);

    // TODO: clear out shader cached
    // TODO: clear out texture cache

    glo_set_current(NULL);

    glo_context_destroy(pg->gl_context);
}

static unsigned int kelvin_map_stencil_op(uint32_t parameter)
{
    unsigned int op;
    switch (parameter) {
    case NV097_SET_STENCIL_OP_V_KEEP:
        op = NV_PGRAPH_CONTROL_2_STENCIL_OP_V_KEEP; break;
    case NV097_SET_STENCIL_OP_V_ZERO:
        op = NV_PGRAPH_CONTROL_2_STENCIL_OP_V_ZERO; break;
    case NV097_SET_STENCIL_OP_V_REPLACE:
        op = NV_PGRAPH_CONTROL_2_STENCIL_OP_V_REPLACE; break;
    case NV097_SET_STENCIL_OP_V_INCRSAT:
        op = NV_PGRAPH_CONTROL_2_STENCIL_OP_V_INCRSAT; break;
    case NV097_SET_STENCIL_OP_V_DECRSAT:
        op = NV_PGRAPH_CONTROL_2_STENCIL_OP_V_DECRSAT; break;
    case NV097_SET_STENCIL_OP_V_INVERT:
        op = NV_PGRAPH_CONTROL_2_STENCIL_OP_V_INVERT; break;
    case NV097_SET_STENCIL_OP_V_INCR:
        op = NV_PGRAPH_CONTROL_2_STENCIL_OP_V_INCR; break;
    case NV097_SET_STENCIL_OP_V_DECR:
        op = NV_PGRAPH_CONTROL_2_STENCIL_OP_V_DECR; break;
    default:
        assert(false);
        break;
    }
    return op;
}

static unsigned int kelvin_map_polygon_mode(uint32_t parameter)
{
    unsigned int mode;
    switch (parameter) {
    case NV097_SET_FRONT_POLYGON_MODE_V_POINT:
        mode = NV_PGRAPH_SETUPRASTER_FRONTFACEMODE_POINT; break;
    case NV097_SET_FRONT_POLYGON_MODE_V_LINE:
        mode = NV_PGRAPH_SETUPRASTER_FRONTFACEMODE_LINE; break;
    case NV097_SET_FRONT_POLYGON_MODE_V_FILL:
        mode = NV_PGRAPH_SETUPRASTER_FRONTFACEMODE_FILL; break;
    default:
        assert(false);
        break;
    }
    return mode;
}

static unsigned int kelvin_map_texgen(uint32_t parameter, unsigned int channel)
{
    assert(channel < 4);
    unsigned int texgen;
    switch (parameter) {
    case NV097_SET_TEXGEN_S_DISABLE:
        texgen = NV_PGRAPH_CSV1_A_T0_S_DISABLE; break;
    case NV097_SET_TEXGEN_S_EYE_LINEAR:
        texgen = NV_PGRAPH_CSV1_A_T0_S_EYE_LINEAR; break;
    case NV097_SET_TEXGEN_S_OBJECT_LINEAR:
        texgen = NV_PGRAPH_CSV1_A_T0_S_OBJECT_LINEAR; break;
    case NV097_SET_TEXGEN_S_SPHERE_MAP:
        assert(channel < 2);
        texgen = NV_PGRAPH_CSV1_A_T0_S_SPHERE_MAP; break;
    case NV097_SET_TEXGEN_S_REFLECTION_MAP:
        assert(channel < 3);
        texgen = NV_PGRAPH_CSV1_A_T0_S_REFLECTION_MAP; break;
    case NV097_SET_TEXGEN_S_NORMAL_MAP:
        assert(channel < 3);
        texgen = NV_PGRAPH_CSV1_A_T0_S_NORMAL_MAP; break;
    default:
        assert(false);
        break;
    }
    return texgen;
}

static void pgraph_allocate_inline_buffer_vertices(PGRAPHState *pg,
                                                   unsigned int attr)
{
    int i;
    VertexAttribute *attribute = &pg->vertex_attributes[attr];

    if (attribute->inline_buffer || pg->inline_buffer_length == 0) {
        return;
    }

    /* Now upload the previous attribute value */
    attribute->inline_buffer = g_malloc(NV2A_MAX_BATCH_LENGTH
                                                  * sizeof(float) * 4);
    for (i = 0; i < pg->inline_buffer_length; i++) {
        memcpy(&attribute->inline_buffer[i * 4],
               attribute->inline_value,
               sizeof(float) * 4);
    }
}

static void pgraph_finish_inline_buffer_vertex(PGRAPHState *pg)
{
    int i;

    assert(pg->inline_buffer_length < NV2A_MAX_BATCH_LENGTH);

    for (i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *attribute = &pg->vertex_attributes[i];
        if (attribute->inline_buffer) {
            memcpy(&attribute->inline_buffer[
                      pg->inline_buffer_length * 4],
                   attribute->inline_value,
                   sizeof(float) * 4);
        }
    }

    pg->inline_buffer_length++;
}

static void pgraph_method(NV2AState *d,
                          unsigned int subchannel,
                          unsigned int method,
                          uint32_t parameter)
{
    int i;
    GraphicsSubchannel *subchannel_data;
    GraphicsObject *object;

    unsigned int slot;

    PGRAPHState *pg = &d->pgraph;

    assert(pg->channel_valid);
    subchannel_data = &pg->subchannel_data[subchannel];
    object = &subchannel_data->object;

    ContextSurfaces2DState *context_surfaces_2d
        = &object->data.context_surfaces_2d;
    ImageBlitState *image_blit = &object->data.image_blit;
    KelvinState *kelvin = &object->data.kelvin;



    pgraph_method_log(subchannel, object->graphics_class, method, parameter);

    if (method == NV_SET_OBJECT) {
        subchannel_data->object_instance = parameter;

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
        context_surfaces_2d->source_offset = parameter & 0x07FFFFFF;
        break;
    case NV062_SET_OFFSET_DESTIN:
        context_surfaces_2d->dest_offset = parameter & 0x07FFFFFF;
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

            NV2A_GL_DPRINTF(true, "NV09F_SET_OPERATION_SRCCOPY");

            GraphicsObject *context_surfaces_obj =
                lookup_graphics_object(pg, image_blit->context_surfaces);
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

            NV2A_DPRINTF("  - 0x%tx -> 0x%tx\n", source - d->vram_ptr,
                                                 dest - d->vram_ptr);

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
            assert(!(pg->pending_interrupts & NV_PGRAPH_INTR_ERROR));


            pg->trapped_channel_id = pg->channel_id;
            pg->trapped_subchannel = subchannel;
            pg->trapped_method = method;
            pg->trapped_data[0] = parameter;
            pg->notify_source = NV_PGRAPH_NSOURCE_NOTIFICATION; /* TODO: check this */
            pg->pending_interrupts |= NV_PGRAPH_INTR_ERROR;

            qemu_mutex_unlock(&pg->lock);
            qemu_mutex_lock_iothread();
            update_irq(d);
            qemu_mutex_lock(&pg->lock);
            qemu_mutex_unlock_iothread();

            while (pg->pending_interrupts & NV_PGRAPH_INTR_ERROR) {
                qemu_cond_wait(&pg->interrupt_cond, &pg->lock);
            }
        }
        break;

    case NV097_WAIT_FOR_IDLE:
        pgraph_update_surface(d, false, true, true);
        break;


    case NV097_SET_FLIP_READ:
        SET_MASK(pg->regs[NV_PGRAPH_SURFACE], NV_PGRAPH_SURFACE_READ_3D,
                 parameter);
        break;
    case NV097_SET_FLIP_WRITE:
        SET_MASK(pg->regs[NV_PGRAPH_SURFACE], NV_PGRAPH_SURFACE_WRITE_3D,
                 parameter);
        break;
    case NV097_SET_FLIP_MODULO:
        SET_MASK(pg->regs[NV_PGRAPH_SURFACE], NV_PGRAPH_SURFACE_MODULO_3D,
                 parameter);
        break;
    case NV097_FLIP_INCREMENT_WRITE: {
        NV2A_DPRINTF("flip increment write %d -> ",
            GET_MASK(pg->regs[NV_PGRAPH_SURFACE],
                          NV_PGRAPH_SURFACE_WRITE_3D));
        SET_MASK(pg->regs[NV_PGRAPH_SURFACE],
                 NV_PGRAPH_SURFACE_WRITE_3D,
                 (GET_MASK(pg->regs[NV_PGRAPH_SURFACE],
                          NV_PGRAPH_SURFACE_WRITE_3D)+1)
                    % GET_MASK(pg->regs[NV_PGRAPH_SURFACE],
                               NV_PGRAPH_SURFACE_MODULO_3D) );
        NV2A_DPRINTF("%d\n",
            GET_MASK(pg->regs[NV_PGRAPH_SURFACE],
                          NV_PGRAPH_SURFACE_WRITE_3D));

        if (glFrameTerminatorGREMEDY) {
            glFrameTerminatorGREMEDY();
        }

        break;
    }
    case NV097_FLIP_STALL:
        pgraph_update_surface(d, false, true, true);

        while (true) {
            NV2A_DPRINTF("flip stall read: %d, write: %d, modulo: %d\n",
                GET_MASK(pg->regs[NV_PGRAPH_SURFACE], NV_PGRAPH_SURFACE_READ_3D),
                GET_MASK(pg->regs[NV_PGRAPH_SURFACE], NV_PGRAPH_SURFACE_WRITE_3D),
                GET_MASK(pg->regs[NV_PGRAPH_SURFACE], NV_PGRAPH_SURFACE_MODULO_3D));

            uint32_t s = pg->regs[NV_PGRAPH_SURFACE];
            if (GET_MASK(s, NV_PGRAPH_SURFACE_READ_3D)
                != GET_MASK(s, NV_PGRAPH_SURFACE_WRITE_3D)) {
                break;
            }
            qemu_cond_wait(&pg->flip_3d, &pg->lock);
        }
        NV2A_DPRINTF("flip stall done\n");
        break;

    case NV097_SET_CONTEXT_DMA_NOTIFIES:
        kelvin->dma_notifies = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_A:
        pg->dma_a = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_B:
        pg->dma_b = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_STATE:
        kelvin->dma_state = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_COLOR:
        /* try to get any straggling draws in before the surface's changed :/ */
        pgraph_update_surface(d, false, true, true);

        pg->dma_color = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_ZETA:
        pg->dma_zeta = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_VERTEX_A:
        pg->dma_vertex_a = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_VERTEX_B:
        pg->dma_vertex_b = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_SEMAPHORE:
        kelvin->dma_semaphore = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_REPORT:
        pg->dma_report = parameter;
        break;

    case NV097_SET_SURFACE_CLIP_HORIZONTAL:
        pgraph_update_surface(d, false, true, true);

        pg->surface_shape.clip_x =
            GET_MASK(parameter, NV097_SET_SURFACE_CLIP_HORIZONTAL_X);
        pg->surface_shape.clip_width =
            GET_MASK(parameter, NV097_SET_SURFACE_CLIP_HORIZONTAL_WIDTH);
        break;
    case NV097_SET_SURFACE_CLIP_VERTICAL:
        pgraph_update_surface(d, false, true, true);

        pg->surface_shape.clip_y =
            GET_MASK(parameter, NV097_SET_SURFACE_CLIP_VERTICAL_Y);
        pg->surface_shape.clip_height =
            GET_MASK(parameter, NV097_SET_SURFACE_CLIP_VERTICAL_HEIGHT);
        break;
    case NV097_SET_SURFACE_FORMAT:
        pgraph_update_surface(d, false, true, true);

        pg->surface_shape.color_format =
            GET_MASK(parameter, NV097_SET_SURFACE_FORMAT_COLOR);
        pg->surface_shape.zeta_format =
            GET_MASK(parameter, NV097_SET_SURFACE_FORMAT_ZETA);
        pg->surface_type =
            GET_MASK(parameter, NV097_SET_SURFACE_FORMAT_TYPE);
        pg->surface_shape.anti_aliasing =
            GET_MASK(parameter, NV097_SET_SURFACE_FORMAT_ANTI_ALIASING);
        pg->surface_shape.log_width =
            GET_MASK(parameter, NV097_SET_SURFACE_FORMAT_WIDTH);
        pg->surface_shape.log_height =
            GET_MASK(parameter, NV097_SET_SURFACE_FORMAT_HEIGHT);
        break;
    case NV097_SET_SURFACE_PITCH:
        pgraph_update_surface(d, false, true, true);

        pg->surface_color.pitch =
            GET_MASK(parameter, NV097_SET_SURFACE_PITCH_COLOR);
        pg->surface_zeta.pitch =
            GET_MASK(parameter, NV097_SET_SURFACE_PITCH_ZETA);
        break;
    case NV097_SET_SURFACE_COLOR_OFFSET:
        pgraph_update_surface(d, false, true, true);

        pg->surface_color.offset = parameter;
        break;
    case NV097_SET_SURFACE_ZETA_OFFSET:
        pgraph_update_surface(d, false, true, true);

        pg->surface_zeta.offset = parameter;
        break;

    case NV097_SET_COMBINER_ALPHA_ICW ...
            NV097_SET_COMBINER_ALPHA_ICW + 28:
        slot = (class_method - NV097_SET_COMBINER_ALPHA_ICW) / 4;
        pg->regs[NV_PGRAPH_COMBINEALPHAI0 + slot*4] = parameter;
        break;

    case NV097_SET_COMBINER_SPECULAR_FOG_CW0:
        pg->regs[NV_PGRAPH_COMBINESPECFOG0] = parameter;
        break;

    case NV097_SET_COMBINER_SPECULAR_FOG_CW1:
        pg->regs[NV_PGRAPH_COMBINESPECFOG1] = parameter;
        break;

    CASE_4(NV097_SET_TEXTURE_ADDRESS, 64):
        slot = (class_method - NV097_SET_TEXTURE_ADDRESS) / 64;
        pg->regs[NV_PGRAPH_TEXADDRESS0 + slot * 4] = parameter;
        break;
    case NV097_SET_CONTROL0: {
        pgraph_update_surface(d, false, true, true);

        bool stencil_write_enable =
            parameter & NV097_SET_CONTROL0_STENCIL_WRITE_ENABLE;
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_0],
                 NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE,
                 stencil_write_enable);

        uint32_t z_format = GET_MASK(parameter, NV097_SET_CONTROL0_Z_FORMAT);
        SET_MASK(pg->regs[NV_PGRAPH_SETUPRASTER],
                 NV_PGRAPH_SETUPRASTER_Z_FORMAT, z_format);

        bool z_perspective =
            parameter & NV097_SET_CONTROL0_Z_PERSPECTIVE_ENABLE;
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_0],
                 NV_PGRAPH_CONTROL_0_Z_PERSPECTIVE_ENABLE,
                 z_perspective);
        break;
    }

    case NV097_SET_FOG_MODE: {
        /* FIXME: There is also NV_PGRAPH_CSV0_D_FOG_MODE */
        unsigned int mode;
        switch (parameter) {
        case NV097_SET_FOG_MODE_V_LINEAR:
            mode = NV_PGRAPH_CONTROL_3_FOG_MODE_LINEAR; break;
        case NV097_SET_FOG_MODE_V_EXP:
            mode = NV_PGRAPH_CONTROL_3_FOG_MODE_EXP; break;
        case NV097_SET_FOG_MODE_V_EXP2:
            mode = NV_PGRAPH_CONTROL_3_FOG_MODE_EXP2; break;
        case NV097_SET_FOG_MODE_V_EXP_ABS:
            mode = NV_PGRAPH_CONTROL_3_FOG_MODE_EXP_ABS; break;
        case NV097_SET_FOG_MODE_V_EXP2_ABS:
            mode = NV_PGRAPH_CONTROL_3_FOG_MODE_EXP2_ABS; break;
        case NV097_SET_FOG_MODE_V_LINEAR_ABS:
            mode = NV_PGRAPH_CONTROL_3_FOG_MODE_LINEAR_ABS; break;
        default:
            assert(false);
            break;
        }
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_3], NV_PGRAPH_CONTROL_3_FOG_MODE,
                 mode);
        break;
    }
    case NV097_SET_FOG_GEN_MODE: {
        unsigned int mode;
        switch (parameter) {
        case NV097_SET_FOG_GEN_MODE_V_SPEC_ALPHA:
            mode = NV_PGRAPH_CSV0_D_FOGGENMODE_SPEC_ALPHA; break;
        case NV097_SET_FOG_GEN_MODE_V_RADIAL:
            mode = NV_PGRAPH_CSV0_D_FOGGENMODE_RADIAL; break;
        case NV097_SET_FOG_GEN_MODE_V_PLANAR:
            mode = NV_PGRAPH_CSV0_D_FOGGENMODE_PLANAR; break;
        case NV097_SET_FOG_GEN_MODE_V_ABS_PLANAR:
            mode = NV_PGRAPH_CSV0_D_FOGGENMODE_ABS_PLANAR; break;
        case NV097_SET_FOG_GEN_MODE_V_FOG_X:
            mode = NV_PGRAPH_CSV0_D_FOGGENMODE_FOG_X; break;
        default:
            assert(false);
            break;
        }
        SET_MASK(pg->regs[NV_PGRAPH_CSV0_D], NV_PGRAPH_CSV0_D_FOGGENMODE, mode);
        break;
    }
    case NV097_SET_FOG_ENABLE:
/*
      FIXME: There is also:
        SET_MASK(pg->regs[NV_PGRAPH_CSV0_D], NV_PGRAPH_CSV0_D_FOGENABLE,
             parameter);
*/
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_3], NV_PGRAPH_CONTROL_3_FOGENABLE,
             parameter);
        break;
    case NV097_SET_FOG_COLOR: {
        /* PGRAPH channels are ARGB, parameter channels are ABGR */
        uint8_t red = GET_MASK(parameter, NV097_SET_FOG_COLOR_RED);
        uint8_t green = GET_MASK(parameter, NV097_SET_FOG_COLOR_GREEN);
        uint8_t blue = GET_MASK(parameter, NV097_SET_FOG_COLOR_BLUE);
        uint8_t alpha = GET_MASK(parameter, NV097_SET_FOG_COLOR_ALPHA);
        SET_MASK(pg->regs[NV_PGRAPH_FOGCOLOR], NV_PGRAPH_FOGCOLOR_RED, red);
        SET_MASK(pg->regs[NV_PGRAPH_FOGCOLOR], NV_PGRAPH_FOGCOLOR_GREEN, green);
        SET_MASK(pg->regs[NV_PGRAPH_FOGCOLOR], NV_PGRAPH_FOGCOLOR_BLUE, blue);
        SET_MASK(pg->regs[NV_PGRAPH_FOGCOLOR], NV_PGRAPH_FOGCOLOR_ALPHA, alpha);
        break;
    }
    case NV097_SET_ALPHA_TEST_ENABLE:
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_0],
                 NV_PGRAPH_CONTROL_0_ALPHATESTENABLE, parameter);
        break;
    case NV097_SET_BLEND_ENABLE:
        SET_MASK(pg->regs[NV_PGRAPH_BLEND], NV_PGRAPH_BLEND_EN, parameter);
        break;
    case NV097_SET_CULL_FACE_ENABLE:
        SET_MASK(pg->regs[NV_PGRAPH_SETUPRASTER],
                 NV_PGRAPH_SETUPRASTER_CULLENABLE,
                 parameter);
        break;
    case NV097_SET_DEPTH_TEST_ENABLE:
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_0], NV_PGRAPH_CONTROL_0_ZENABLE,
                 parameter);
        break;
    case NV097_SET_DITHER_ENABLE:
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_0],
                 NV_PGRAPH_CONTROL_0_DITHERENABLE, parameter);
        break;
    case NV097_SET_LIGHTING_ENABLE:
        SET_MASK(pg->regs[NV_PGRAPH_CSV0_C], NV_PGRAPH_CSV0_C_LIGHTING,
                 parameter);
        break;
    case NV097_SET_SKIN_MODE:
        SET_MASK(pg->regs[NV_PGRAPH_CSV0_D], NV_PGRAPH_CSV0_D_SKIN,
                 parameter);
        break;
    case NV097_SET_STENCIL_TEST_ENABLE:
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_1],
                 NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE, parameter);
        break;
    case NV097_SET_POLY_OFFSET_POINT_ENABLE:
        SET_MASK(pg->regs[NV_PGRAPH_SETUPRASTER],
                 NV_PGRAPH_SETUPRASTER_POFFSETPOINTENABLE, parameter);
        break;
    case NV097_SET_POLY_OFFSET_LINE_ENABLE:
        SET_MASK(pg->regs[NV_PGRAPH_SETUPRASTER],
                 NV_PGRAPH_SETUPRASTER_POFFSETLINEENABLE, parameter);
        break;
    case NV097_SET_POLY_OFFSET_FILL_ENABLE:
        SET_MASK(pg->regs[NV_PGRAPH_SETUPRASTER],
                 NV_PGRAPH_SETUPRASTER_POFFSETFILLENABLE, parameter);
        break;
    case NV097_SET_ALPHA_FUNC:
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_0],
                 NV_PGRAPH_CONTROL_0_ALPHAFUNC, parameter & 0xF);
        break;
    case NV097_SET_ALPHA_REF:
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_0],
                 NV_PGRAPH_CONTROL_0_ALPHAREF, parameter);
        break;
    case NV097_SET_BLEND_FUNC_SFACTOR: {
        unsigned int factor;
        switch (parameter) {
        case NV097_SET_BLEND_FUNC_SFACTOR_V_ZERO:
            factor = NV_PGRAPH_BLEND_SFACTOR_ZERO; break;
        case NV097_SET_BLEND_FUNC_SFACTOR_V_ONE:
            factor = NV_PGRAPH_BLEND_SFACTOR_ONE; break;
        case NV097_SET_BLEND_FUNC_SFACTOR_V_SRC_COLOR:
            factor = NV_PGRAPH_BLEND_SFACTOR_SRC_COLOR; break;
        case NV097_SET_BLEND_FUNC_SFACTOR_V_ONE_MINUS_SRC_COLOR:
            factor = NV_PGRAPH_BLEND_SFACTOR_ONE_MINUS_SRC_COLOR; break;
        case NV097_SET_BLEND_FUNC_SFACTOR_V_SRC_ALPHA:
            factor = NV_PGRAPH_BLEND_SFACTOR_SRC_ALPHA; break;
        case NV097_SET_BLEND_FUNC_SFACTOR_V_ONE_MINUS_SRC_ALPHA:
            factor = NV_PGRAPH_BLEND_SFACTOR_ONE_MINUS_SRC_ALPHA; break;
        case NV097_SET_BLEND_FUNC_SFACTOR_V_DST_ALPHA:
            factor = NV_PGRAPH_BLEND_SFACTOR_DST_ALPHA; break;
        case NV097_SET_BLEND_FUNC_SFACTOR_V_ONE_MINUS_DST_ALPHA:
            factor = NV_PGRAPH_BLEND_SFACTOR_ONE_MINUS_DST_ALPHA; break;
        case NV097_SET_BLEND_FUNC_SFACTOR_V_DST_COLOR:
            factor = NV_PGRAPH_BLEND_SFACTOR_DST_COLOR; break;
        case NV097_SET_BLEND_FUNC_SFACTOR_V_ONE_MINUS_DST_COLOR:
            factor = NV_PGRAPH_BLEND_SFACTOR_ONE_MINUS_DST_COLOR; break;
        case NV097_SET_BLEND_FUNC_SFACTOR_V_SRC_ALPHA_SATURATE:
            factor = NV_PGRAPH_BLEND_SFACTOR_SRC_ALPHA_SATURATE; break;
        case NV097_SET_BLEND_FUNC_SFACTOR_V_CONSTANT_COLOR:
            factor = NV_PGRAPH_BLEND_SFACTOR_CONSTANT_COLOR; break;
        case NV097_SET_BLEND_FUNC_SFACTOR_V_ONE_MINUS_CONSTANT_COLOR:
            factor = NV_PGRAPH_BLEND_SFACTOR_ONE_MINUS_CONSTANT_COLOR; break;
        case NV097_SET_BLEND_FUNC_SFACTOR_V_CONSTANT_ALPHA:
            factor = NV_PGRAPH_BLEND_SFACTOR_CONSTANT_ALPHA; break;
        case NV097_SET_BLEND_FUNC_SFACTOR_V_ONE_MINUS_CONSTANT_ALPHA:
            factor = NV_PGRAPH_BLEND_SFACTOR_ONE_MINUS_CONSTANT_ALPHA; break;
        default:
            assert(false);
            break;
        }
        SET_MASK(pg->regs[NV_PGRAPH_BLEND], NV_PGRAPH_BLEND_SFACTOR, factor);

        break;
    }

    case NV097_SET_BLEND_FUNC_DFACTOR: {
        unsigned int factor;
        switch (parameter) {
        case NV097_SET_BLEND_FUNC_DFACTOR_V_ZERO:
            factor = NV_PGRAPH_BLEND_DFACTOR_ZERO; break;
        case NV097_SET_BLEND_FUNC_DFACTOR_V_ONE:
            factor = NV_PGRAPH_BLEND_DFACTOR_ONE; break;
        case NV097_SET_BLEND_FUNC_DFACTOR_V_SRC_COLOR:
            factor = NV_PGRAPH_BLEND_DFACTOR_SRC_COLOR; break;
        case NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_SRC_COLOR:
            factor = NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_SRC_COLOR; break;
        case NV097_SET_BLEND_FUNC_DFACTOR_V_SRC_ALPHA:
            factor = NV_PGRAPH_BLEND_DFACTOR_SRC_ALPHA; break;
        case NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_SRC_ALPHA:
            factor = NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_SRC_ALPHA; break;
        case NV097_SET_BLEND_FUNC_DFACTOR_V_DST_ALPHA:
            factor = NV_PGRAPH_BLEND_DFACTOR_DST_ALPHA; break;
        case NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_DST_ALPHA:
            factor = NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_DST_ALPHA; break;
        case NV097_SET_BLEND_FUNC_DFACTOR_V_DST_COLOR:
            factor = NV_PGRAPH_BLEND_DFACTOR_DST_COLOR; break;
        case NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_DST_COLOR:
            factor = NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_DST_COLOR; break;
        case NV097_SET_BLEND_FUNC_DFACTOR_V_SRC_ALPHA_SATURATE:
            factor = NV_PGRAPH_BLEND_DFACTOR_SRC_ALPHA_SATURATE; break;
        case NV097_SET_BLEND_FUNC_DFACTOR_V_CONSTANT_COLOR:
            factor = NV_PGRAPH_BLEND_DFACTOR_CONSTANT_COLOR; break;
        case NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_CONSTANT_COLOR:
            factor = NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_CONSTANT_COLOR; break;
        case NV097_SET_BLEND_FUNC_DFACTOR_V_CONSTANT_ALPHA:
            factor = NV_PGRAPH_BLEND_DFACTOR_CONSTANT_ALPHA; break;
        case NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_CONSTANT_ALPHA:
            factor = NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_CONSTANT_ALPHA; break;
        default:
            assert(false);
            break;
        }
        SET_MASK(pg->regs[NV_PGRAPH_BLEND], NV_PGRAPH_BLEND_DFACTOR, factor);

        break;
    }

    case NV097_SET_BLEND_COLOR:
        pg->regs[NV_PGRAPH_BLENDCOLOR] = parameter;
        break;

    case NV097_SET_BLEND_EQUATION: {
        unsigned int equation;
        switch (parameter) {
        case NV097_SET_BLEND_EQUATION_V_FUNC_SUBTRACT:
            equation = 0; break;
        case NV097_SET_BLEND_EQUATION_V_FUNC_REVERSE_SUBTRACT:
            equation = 1; break;
        case NV097_SET_BLEND_EQUATION_V_FUNC_ADD:
            equation = 2; break;
        case NV097_SET_BLEND_EQUATION_V_MIN:
            equation = 3; break;
        case NV097_SET_BLEND_EQUATION_V_MAX:
            equation = 4; break;
        case NV097_SET_BLEND_EQUATION_V_FUNC_REVERSE_SUBTRACT_SIGNED:
            equation = 5; break;
        case NV097_SET_BLEND_EQUATION_V_FUNC_ADD_SIGNED:
            equation = 6; break;
        default:
            assert(false);
            break;
        }
        SET_MASK(pg->regs[NV_PGRAPH_BLEND], NV_PGRAPH_BLEND_EQN, equation);

        break;
    }

    case NV097_SET_DEPTH_FUNC:
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_0], NV_PGRAPH_CONTROL_0_ZFUNC,
                 parameter & 0xF);
        break;

    case NV097_SET_COLOR_MASK: {
        pg->surface_color.write_enabled_cache |= pgraph_color_write_enabled(pg);

        bool alpha = parameter & NV097_SET_COLOR_MASK_ALPHA_WRITE_ENABLE;
        bool red = parameter & NV097_SET_COLOR_MASK_RED_WRITE_ENABLE;
        bool green = parameter & NV097_SET_COLOR_MASK_GREEN_WRITE_ENABLE;
        bool blue = parameter & NV097_SET_COLOR_MASK_BLUE_WRITE_ENABLE;
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_0],
                 NV_PGRAPH_CONTROL_0_ALPHA_WRITE_ENABLE, alpha);
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_0],
                 NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE, red);
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_0],
                 NV_PGRAPH_CONTROL_0_GREEN_WRITE_ENABLE, green);
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_0],
                 NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE, blue);
        break;
    }
    case NV097_SET_DEPTH_MASK:
        pg->surface_zeta.write_enabled_cache |= pgraph_zeta_write_enabled(pg);

        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_0],
                 NV_PGRAPH_CONTROL_0_ZWRITEENABLE, parameter);
        break;
    case NV097_SET_STENCIL_MASK:
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_1],
                 NV_PGRAPH_CONTROL_1_STENCIL_MASK_WRITE, parameter);
        break;
    case NV097_SET_STENCIL_FUNC:
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_1],
                 NV_PGRAPH_CONTROL_1_STENCIL_FUNC, parameter & 0xF);
        break;
    case NV097_SET_STENCIL_FUNC_REF:
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_1],
                 NV_PGRAPH_CONTROL_1_STENCIL_REF, parameter);
        break;
    case NV097_SET_STENCIL_FUNC_MASK:
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_1],
                 NV_PGRAPH_CONTROL_1_STENCIL_MASK_READ, parameter);
        break;
    case NV097_SET_STENCIL_OP_FAIL:
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_2],
                 NV_PGRAPH_CONTROL_2_STENCIL_OP_FAIL,
                 kelvin_map_stencil_op(parameter));
        break;
    case NV097_SET_STENCIL_OP_ZFAIL:
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_2],
                 NV_PGRAPH_CONTROL_2_STENCIL_OP_ZFAIL,
                 kelvin_map_stencil_op(parameter));
        break;
    case NV097_SET_STENCIL_OP_ZPASS:
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_2],
                 NV_PGRAPH_CONTROL_2_STENCIL_OP_ZPASS,
                 kelvin_map_stencil_op(parameter));
        break;

    case NV097_SET_POLYGON_OFFSET_SCALE_FACTOR:
        pg->regs[NV_PGRAPH_ZOFFSETFACTOR] = parameter;
        break;
    case NV097_SET_POLYGON_OFFSET_BIAS:
        pg->regs[NV_PGRAPH_ZOFFSETBIAS] = parameter;
        break;
    case NV097_SET_FRONT_POLYGON_MODE:
        SET_MASK(pg->regs[NV_PGRAPH_SETUPRASTER],
                 NV_PGRAPH_SETUPRASTER_FRONTFACEMODE,
                 kelvin_map_polygon_mode(parameter));
        break;
    case NV097_SET_BACK_POLYGON_MODE:
        SET_MASK(pg->regs[NV_PGRAPH_SETUPRASTER],
                 NV_PGRAPH_SETUPRASTER_BACKFACEMODE,
                 kelvin_map_polygon_mode(parameter));
        break;
    case NV097_SET_CLIP_MIN:
        pg->regs[NV_PGRAPH_ZCLIPMIN] = parameter;
        break;
    case NV097_SET_CLIP_MAX:
        pg->regs[NV_PGRAPH_ZCLIPMAX] = parameter;
        break;
    case NV097_SET_CULL_FACE: {
        unsigned int face;
        switch (parameter) {
        case NV097_SET_CULL_FACE_V_FRONT:
            face = NV_PGRAPH_SETUPRASTER_CULLCTRL_FRONT; break;
        case NV097_SET_CULL_FACE_V_BACK:
            face = NV_PGRAPH_SETUPRASTER_CULLCTRL_BACK; break;
        case NV097_SET_CULL_FACE_V_FRONT_AND_BACK:
            face = NV_PGRAPH_SETUPRASTER_CULLCTRL_FRONT_AND_BACK; break;
        default:
            assert(false);
            break;
        }
        SET_MASK(pg->regs[NV_PGRAPH_SETUPRASTER],
                 NV_PGRAPH_SETUPRASTER_CULLCTRL,
                 face);
        break;
    }
    case NV097_SET_FRONT_FACE: {
        bool ccw;
        switch (parameter) {
        case NV097_SET_FRONT_FACE_V_CW:
            ccw = false; break;
        case NV097_SET_FRONT_FACE_V_CCW:
            ccw = true; break;
        default:
            assert(false);
            break;
        }
        SET_MASK(pg->regs[NV_PGRAPH_SETUPRASTER],
                 NV_PGRAPH_SETUPRASTER_FRONTFACE,
                 ccw ? 1 : 0);
        break;
    }
    case NV097_SET_NORMALIZATION_ENABLE:
        SET_MASK(pg->regs[NV_PGRAPH_CSV0_C],
                 NV_PGRAPH_CSV0_C_NORMALIZATION_ENABLE,
                 parameter);
        break;

    case NV097_SET_LIGHT_ENABLE_MASK:
        SET_MASK(d->pgraph.regs[NV_PGRAPH_CSV0_D],
                 NV_PGRAPH_CSV0_D_LIGHTS,
                 parameter);
        break;

    CASE_4(NV097_SET_TEXGEN_S, 16): {
        slot = (class_method - NV097_SET_TEXGEN_S) / 16;
        unsigned int reg = (slot < 2) ? NV_PGRAPH_CSV1_A
                                      : NV_PGRAPH_CSV1_B;
        unsigned int mask = (slot % 2) ? NV_PGRAPH_CSV1_A_T1_S
                                       : NV_PGRAPH_CSV1_A_T0_S;
        SET_MASK(pg->regs[reg], mask, kelvin_map_texgen(parameter, 0));
        break;
    }
    CASE_4(NV097_SET_TEXGEN_T, 16): {
        slot = (class_method - NV097_SET_TEXGEN_T) / 16;
        unsigned int reg = (slot < 2) ? NV_PGRAPH_CSV1_A
                                      : NV_PGRAPH_CSV1_B;
        unsigned int mask = (slot % 2) ? NV_PGRAPH_CSV1_A_T1_T
                                       : NV_PGRAPH_CSV1_A_T0_T;
        SET_MASK(pg->regs[reg], mask, kelvin_map_texgen(parameter, 1));
        break;
    }
    CASE_4(NV097_SET_TEXGEN_R, 16): {
        slot = (class_method - NV097_SET_TEXGEN_R) / 16;
        unsigned int reg = (slot < 2) ? NV_PGRAPH_CSV1_A
                                      : NV_PGRAPH_CSV1_B;
        unsigned int mask = (slot % 2) ? NV_PGRAPH_CSV1_A_T1_R
                                       : NV_PGRAPH_CSV1_A_T0_R;
        SET_MASK(pg->regs[reg], mask, kelvin_map_texgen(parameter, 2));
        break;
    }
    CASE_4(NV097_SET_TEXGEN_Q, 16): {
        slot = (class_method - NV097_SET_TEXGEN_Q) / 16;
        unsigned int reg = (slot < 2) ? NV_PGRAPH_CSV1_A
                                      : NV_PGRAPH_CSV1_B;
        unsigned int mask = (slot % 2) ? NV_PGRAPH_CSV1_A_T1_Q
                                       : NV_PGRAPH_CSV1_A_T0_Q;
        SET_MASK(pg->regs[reg], mask, kelvin_map_texgen(parameter, 3));
        break;
    }
    CASE_4(NV097_SET_TEXTURE_MATRIX_ENABLE,4):
        slot = (class_method - NV097_SET_TEXTURE_MATRIX_ENABLE) / 4;
        pg->texture_matrix_enable[slot] = parameter;
        break;

    case NV097_SET_PROJECTION_MATRIX ...
            NV097_SET_PROJECTION_MATRIX + 0x3c:
        slot = (class_method - NV097_SET_PROJECTION_MATRIX) / 4;
        pg->projection_matrix[slot] = *(float*)&parameter;
        break;

    case NV097_SET_MODEL_VIEW_MATRIX ...
            NV097_SET_MODEL_VIEW_MATRIX + 0xfc:
        slot = (class_method - NV097_SET_MODEL_VIEW_MATRIX) / 4;
        pg->model_view_matrix[slot / 16][slot % 16] = *(float*)&parameter;
        break;

    case NV097_SET_INVERSE_MODEL_VIEW_MATRIX ...
            NV097_SET_INVERSE_MODEL_VIEW_MATRIX + 0xfc:
        slot = (class_method - NV097_SET_INVERSE_MODEL_VIEW_MATRIX) / 4;
        pg->inverse_model_view_matrix[slot / 16][slot % 16] = *(float*)&parameter;
        break;

    case NV097_SET_COMPOSITE_MATRIX ...
            NV097_SET_COMPOSITE_MATRIX + 0x3c:
        slot = (class_method - NV097_SET_COMPOSITE_MATRIX) / 4;
        pg->composite_matrix[slot] = *(float*)&parameter;
        break;

    case NV097_SET_TEXTURE_MATRIX ...
            NV097_SET_TEXTURE_MATRIX + 0xfc:
        slot = (class_method - NV097_SET_TEXTURE_MATRIX) / 4;
        pg->texture_matrix[slot / 16][slot % 16] = *(float*)&parameter;
        break;

    case NV097_SET_FOG_PARAMS ...
            NV097_SET_FOG_PARAMS + 8:
        slot = (class_method - NV097_SET_FOG_PARAMS) / 4;
        if (slot < 2) {
            pg->regs[NV_PGRAPH_FOGPARAM0 + slot*4] = parameter;
        } else {
            /* FIXME: No idea where slot = 2 is */
        }
        break;

    /* Handles NV097_SET_TEXGEN_PLANE_S,T,R,Q */
    case NV097_SET_TEXGEN_PLANE_S ...
            NV097_SET_TEXGEN_PLANE_S + 0xfc: {
        slot = (class_method - NV097_SET_TEXGEN_PLANE_S) / 4;
        unsigned int part = slot % 16;
        pg->texture_plane[slot / 16][part / 4][part % 4] = *(float*)&parameter;
        break;
    }

    case NV097_SET_TEXGEN_VIEW_MODEL:
        SET_MASK(pg->regs[NV_PGRAPH_CSV0_D], NV_PGRAPH_CSV0_D_TEXGEN_REF,
                 parameter);
        break;

    case NV097_SET_FOG_PLANE ...
            NV097_SET_FOG_PLANE + 12:
        slot = (class_method - NV097_SET_FOG_PLANE) / 4;
        pg->fog_plane[slot] = *(float*)&parameter;
        break;

    case NV097_SET_SCENE_AMBIENT_COLOR ...
            NV097_SET_SCENE_AMBIENT_COLOR + 8:
        slot = (class_method - NV097_SET_SCENE_AMBIENT_COLOR) / 4;
        pg->scene_ambient_color[slot] = *(float*)&parameter;
        break;

    case NV097_SET_VIEWPORT_OFFSET ...
            NV097_SET_VIEWPORT_OFFSET + 12:

        slot = (class_method - NV097_SET_VIEWPORT_OFFSET) / 4;

        /* populate magic viewport offset constant */
        pg->constants[59].data[slot] = parameter;
        pg->constants[59].dirty = true;
        break;

    case NV097_SET_EYE_POSITION ...
            NV097_SET_EYE_POSITION + 12:
        slot = (class_method - NV097_SET_EYE_POSITION) / 4;
        pg->eye_position[slot] = *(float*)&parameter;
        break;
    case NV097_SET_COMBINER_FACTOR0 ...
            NV097_SET_COMBINER_FACTOR0 + 28:
        slot = (class_method - NV097_SET_COMBINER_FACTOR0) / 4;
        pg->regs[NV_PGRAPH_COMBINEFACTOR0 + slot*4] = parameter;
        break;

    case NV097_SET_COMBINER_FACTOR1 ...
            NV097_SET_COMBINER_FACTOR1 + 28:
        slot = (class_method - NV097_SET_COMBINER_FACTOR1) / 4;
        pg->regs[NV_PGRAPH_COMBINEFACTOR1 + slot*4] = parameter;
        break;

    case NV097_SET_COMBINER_ALPHA_OCW ...
            NV097_SET_COMBINER_ALPHA_OCW + 28:
        slot = (class_method - NV097_SET_COMBINER_ALPHA_OCW) / 4;
        pg->regs[NV_PGRAPH_COMBINEALPHAO0 + slot*4] = parameter;
        break;

    case NV097_SET_COMBINER_COLOR_ICW ...
            NV097_SET_COMBINER_COLOR_ICW + 28:
        slot = (class_method - NV097_SET_COMBINER_COLOR_ICW) / 4;
        pg->regs[NV_PGRAPH_COMBINECOLORI0 + slot*4] = parameter;
        break;

    case NV097_SET_VIEWPORT_SCALE ...
            NV097_SET_VIEWPORT_SCALE + 12:

        slot = (class_method - NV097_SET_VIEWPORT_SCALE) / 4;

        /* populate magic viewport scale constant */
        pg->constants[58].data[slot] = parameter;
        pg->constants[58].dirty = true;
        break;

    case NV097_SET_TRANSFORM_PROGRAM ...
            NV097_SET_TRANSFORM_PROGRAM + 0x7c: {

        slot = (class_method - NV097_SET_TRANSFORM_PROGRAM) / 4;

        int program_load = GET_MASK(pg->regs[NV_PGRAPH_CHEOPS_OFFSET],
                                    NV_PGRAPH_CHEOPS_OFFSET_PROG_LD_PTR);

        assert(program_load < NV2A_MAX_TRANSFORM_PROGRAM_LENGTH);
        pg->program_data[program_load][slot%4] = parameter;

        if (slot % 4 == 3) {
            SET_MASK(pg->regs[NV_PGRAPH_CHEOPS_OFFSET],
                     NV_PGRAPH_CHEOPS_OFFSET_PROG_LD_PTR, program_load+1);
        }

        break;
    }

    case NV097_SET_TRANSFORM_CONSTANT ...
            NV097_SET_TRANSFORM_CONSTANT + 0x7c: {

        slot = (class_method - NV097_SET_TRANSFORM_CONSTANT) / 4;

        int const_load = GET_MASK(pg->regs[NV_PGRAPH_CHEOPS_OFFSET],
                                  NV_PGRAPH_CHEOPS_OFFSET_CONST_LD_PTR);

        assert(const_load < NV2A_VERTEXSHADER_CONSTANTS);
        VertexShaderConstant *constant = &pg->constants[const_load];
        constant->dirty |= (parameter != constant->data[slot%4]);
        constant->data[slot%4] = parameter;

        if (slot % 4 == 3) {
            SET_MASK(pg->regs[NV_PGRAPH_CHEOPS_OFFSET],
                     NV_PGRAPH_CHEOPS_OFFSET_CONST_LD_PTR, const_load+1);
        }
        break;
    }

    case NV097_SET_VERTEX3F ...
            NV097_SET_VERTEX3F + 8: {
        slot = (class_method - NV097_SET_VERTEX3F) / 4;
        VertexAttribute *attribute =
            &pg->vertex_attributes[NV2A_VERTEX_ATTR_POSITION];
        pgraph_allocate_inline_buffer_vertices(pg, NV2A_VERTEX_ATTR_POSITION);
        attribute->inline_value[slot] = *(float*)&parameter;
        attribute->inline_value[3] = 1.0f;
        if (slot == 2) {
            pgraph_finish_inline_buffer_vertex(pg);
        }
        break;
    }

    /* Handles NV097_SET_BACK_LIGHT_* */
    case NV097_SET_BACK_LIGHT_AMBIENT_COLOR ...
            NV097_SET_BACK_LIGHT_SPECULAR_COLOR + 0x1C8: {
        slot = (class_method - NV097_SET_BACK_LIGHT_AMBIENT_COLOR) / 4;
        unsigned int part = NV097_SET_BACK_LIGHT_AMBIENT_COLOR / 4 + slot % 16;
        slot /= 16; /* [Light index] */
        assert(slot < 8);
        switch(part * 4) {
        case NV097_SET_BACK_LIGHT_AMBIENT_COLOR ...
                NV097_SET_BACK_LIGHT_AMBIENT_COLOR + 8:
            part -= NV097_SET_BACK_LIGHT_AMBIENT_COLOR / 4;
            pg->back_light_ambient_color[slot][part] = *(float*)&parameter;
            break;
        case NV097_SET_BACK_LIGHT_DIFFUSE_COLOR ...
                NV097_SET_BACK_LIGHT_DIFFUSE_COLOR + 8:
            part -= NV097_SET_BACK_LIGHT_DIFFUSE_COLOR / 4;
            pg->back_light_diffuse_color[slot][part] = *(float*)&parameter;
            break;
        case NV097_SET_BACK_LIGHT_SPECULAR_COLOR ...
                NV097_SET_BACK_LIGHT_SPECULAR_COLOR + 8:
            part -= NV097_SET_BACK_LIGHT_SPECULAR_COLOR / 4;
            pg->back_light_specular_color[slot][part] = *(float*)&parameter;
            break;
        default:
            assert(false);
            break;
        }
        break;
    }
    /* Handles all the light source props except for NV097_SET_BACK_LIGHT_* */
    case NV097_SET_LIGHT_AMBIENT_COLOR ...
            NV097_SET_LIGHT_LOCAL_ATTENUATION + 0x38C: {
        slot = (class_method - NV097_SET_LIGHT_AMBIENT_COLOR) / 4;
        unsigned int part = NV097_SET_LIGHT_AMBIENT_COLOR / 4 + slot % 32;
        slot /= 32; /* [Light index] */
        assert(slot < 8);
        switch(part * 4) {
        case NV097_SET_LIGHT_AMBIENT_COLOR ...
                NV097_SET_LIGHT_AMBIENT_COLOR + 8:
            part -= NV097_SET_LIGHT_AMBIENT_COLOR / 4;
            pg->light_ambient_color[slot][part] = *(float*)&parameter;
            break;
        case NV097_SET_LIGHT_DIFFUSE_COLOR ...
               NV097_SET_LIGHT_DIFFUSE_COLOR + 8:
            part -= NV097_SET_LIGHT_DIFFUSE_COLOR / 4;
            pg->light_diffuse_color[slot][part] = *(float*)&parameter;
            break;
        case NV097_SET_LIGHT_SPECULAR_COLOR ...
                NV097_SET_LIGHT_SPECULAR_COLOR + 8:
            part -= NV097_SET_LIGHT_SPECULAR_COLOR / 4;
            pg->light_specular_color[slot][part] = *(float*)&parameter;
            break;
        case NV097_SET_LIGHT_LOCAL_RANGE:
            pg->light_local_range[slot] = *(float*)&parameter;
            break;
        case NV097_SET_LIGHT_INFINITE_HALF_VECTOR ...
                NV097_SET_LIGHT_INFINITE_HALF_VECTOR + 8:
            part -= NV097_SET_LIGHT_INFINITE_HALF_VECTOR / 4;
            pg->light_infinite_half_vector[slot][part] = *(float*)&parameter;
            break;
        case NV097_SET_LIGHT_INFINITE_DIRECTION ...
                NV097_SET_LIGHT_INFINITE_DIRECTION + 8:
            part -= NV097_SET_LIGHT_INFINITE_DIRECTION / 4;
            pg->light_infinite_direction[slot][part] = *(float*)&parameter;
            break;
        case NV097_SET_LIGHT_SPOT_FALLOFF ...
                NV097_SET_LIGHT_SPOT_FALLOFF + 8:
            part -= NV097_SET_LIGHT_SPOT_FALLOFF / 4;
            pg->light_spot_falloff[slot][part] = *(float*)&parameter;
            break;
        case NV097_SET_LIGHT_SPOT_DIRECTION ...
                NV097_SET_LIGHT_SPOT_DIRECTION + 12:
            part -= NV097_SET_LIGHT_SPOT_DIRECTION / 4;
            pg->light_spot_direction[slot][part] = *(float*)&parameter;
            break;
        case NV097_SET_LIGHT_LOCAL_POSITION ...
                NV097_SET_LIGHT_LOCAL_POSITION + 8:
            part -= NV097_SET_LIGHT_LOCAL_POSITION / 4;
            pg->light_local_position[slot][part] = *(float*)&parameter;
            break;
        case NV097_SET_LIGHT_LOCAL_ATTENUATION ...
                NV097_SET_LIGHT_LOCAL_ATTENUATION + 8:
            part -= NV097_SET_LIGHT_LOCAL_ATTENUATION / 4;
            pg->light_local_attenuation[slot][part] = *(float*)&parameter;
            break;
        default:
            assert(false);
            break;
        }
        break;
    }

    case NV097_SET_VERTEX4F ...
            NV097_SET_VERTEX4F + 12: {
        slot = (class_method - NV097_SET_VERTEX4F) / 4;
        VertexAttribute *attribute =
            &pg->vertex_attributes[NV2A_VERTEX_ATTR_POSITION];
        pgraph_allocate_inline_buffer_vertices(pg, NV2A_VERTEX_ATTR_POSITION);
        attribute->inline_value[slot] = *(float*)&parameter;
        if (slot == 3) {
            pgraph_finish_inline_buffer_vertex(pg);
        }
        break;
    }

    case NV097_SET_VERTEX_DATA_ARRAY_FORMAT ...
            NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 0x3c: {

        slot = (class_method - NV097_SET_VERTEX_DATA_ARRAY_FORMAT) / 4;
        VertexAttribute *vertex_attribute = &pg->vertex_attributes[slot];

        vertex_attribute->format =
            GET_MASK(parameter, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE);
        vertex_attribute->count =
            GET_MASK(parameter, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE);
        vertex_attribute->stride =
            GET_MASK(parameter, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE);

        NV2A_DPRINTF("vertex data array format=%d, count=%d, stride=%d\n",
            vertex_attribute->format,
            vertex_attribute->count,
            vertex_attribute->stride);

        vertex_attribute->gl_count = vertex_attribute->count;

        switch (vertex_attribute->format) {
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D:
            vertex_attribute->gl_type = GL_UNSIGNED_BYTE;
            vertex_attribute->gl_normalize = GL_TRUE;
            vertex_attribute->size = 1;
            assert(vertex_attribute->count == 4);
            // http://www.opengl.org/registry/specs/ARB/vertex_array_bgra.txt
            vertex_attribute->gl_count = GL_BGRA;
            vertex_attribute->needs_conversion = false;
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL:
            vertex_attribute->gl_type = GL_UNSIGNED_BYTE;
            vertex_attribute->gl_normalize = GL_TRUE;
            vertex_attribute->size = 1;
            vertex_attribute->needs_conversion = false;
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1:
            vertex_attribute->gl_type = GL_SHORT;
            vertex_attribute->gl_normalize = GL_TRUE;
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
            vertex_attribute->gl_type = GL_SHORT;
            vertex_attribute->gl_normalize = GL_FALSE;
            vertex_attribute->size = 2;
            vertex_attribute->needs_conversion = false;
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP:
            /* 3 signed, normalized components packed in 32-bits. (11,11,10) */
            vertex_attribute->size = 4;
            vertex_attribute->gl_type = GL_FLOAT;
            vertex_attribute->gl_normalize = GL_FALSE;
            vertex_attribute->needs_conversion = true;
            vertex_attribute->converted_size = sizeof(float);
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
                g_free(vertex_attribute->converted_buffer);
                vertex_attribute->converted_buffer = NULL;
            }
        }

        break;
    }

    case NV097_SET_VERTEX_DATA_ARRAY_OFFSET ...
            NV097_SET_VERTEX_DATA_ARRAY_OFFSET + 0x3c:

        slot = (class_method - NV097_SET_VERTEX_DATA_ARRAY_OFFSET) / 4;

        pg->vertex_attributes[slot].dma_select =
            parameter & 0x80000000;
        pg->vertex_attributes[slot].offset =
            parameter & 0x7fffffff;

        pg->vertex_attributes[slot].converted_elements = 0;

        break;

    case NV097_SET_LOGIC_OP_ENABLE:
        SET_MASK(pg->regs[NV_PGRAPH_BLEND],
                 NV_PGRAPH_BLEND_LOGICOP_ENABLE, parameter);
        break;

    case NV097_SET_LOGIC_OP:
        SET_MASK(pg->regs[NV_PGRAPH_BLEND],
                 NV_PGRAPH_BLEND_LOGICOP, parameter & 0xF);
        break;

    case NV097_CLEAR_REPORT_VALUE:
        /* FIXME: Does this have a value in parameter? Also does this (also?) modify
         *        the report memory block?
         */
        if (pg->gl_zpass_pixel_count_query_count) {
            glDeleteQueries(pg->gl_zpass_pixel_count_query_count,
                            pg->gl_zpass_pixel_count_queries);
            pg->gl_zpass_pixel_count_query_count = 0;
        }
        pg->zpass_pixel_count_result = 0;
        break;

    case NV097_SET_ZPASS_PIXEL_COUNT_ENABLE:
        pg->zpass_pixel_count_enable = parameter;
        break;

    case NV097_GET_REPORT: {
        /* FIXME: This was first intended to be watchpoint-based. However,
         *        qemu / kvm only supports virtual-address watchpoints.
         *        This'll do for now, but accuracy and performance with other
         *        approaches could be better
         */
        uint8_t type = GET_MASK(parameter, NV097_GET_REPORT_TYPE);
        assert(type == NV097_GET_REPORT_TYPE_ZPASS_PIXEL_CNT);
        hwaddr offset = GET_MASK(parameter, NV097_GET_REPORT_OFFSET);

        uint64_t timestamp = 0x0011223344556677; /* FIXME: Update timestamp?! */
        uint32_t done = 0;

        /* FIXME: Multisampling affects this (both: OGL and Xbox GPU),
         *        not sure if CLEARs also count
         */
        /* FIXME: What about clipping regions etc? */
        for(i = 0; i < pg->gl_zpass_pixel_count_query_count; i++) {
            GLuint gl_query_result;
            glGetQueryObjectuiv(pg->gl_zpass_pixel_count_queries[i],
                                GL_QUERY_RESULT,
                                &gl_query_result);
            pg->zpass_pixel_count_result += gl_query_result;
        }
        if (pg->gl_zpass_pixel_count_query_count) {
            glDeleteQueries(pg->gl_zpass_pixel_count_query_count,
                            pg->gl_zpass_pixel_count_queries);
        }
        pg->gl_zpass_pixel_count_query_count = 0;

        hwaddr report_dma_len;
        uint8_t *report_data = nv_dma_map(d, pg->dma_report,
                                             &report_dma_len);
        assert(offset < report_dma_len);
        report_data += offset;

        stq_le_p((uint64_t*)&report_data[0], timestamp);
        stl_le_p((uint32_t*)&report_data[8], pg->zpass_pixel_count_result);
        stl_le_p((uint32_t*)&report_data[12], done);

        break;
    }

    case NV097_SET_EYE_DIRECTION ...
            NV097_SET_EYE_DIRECTION + 8:
        slot = (class_method - NV097_SET_EYE_DIRECTION) / 4;
        pg->eye_direction[slot] = *(float*)&parameter;
        break;

    case NV097_SET_BEGIN_END: {
        bool depth_test =
            pg->regs[NV_PGRAPH_CONTROL_0] & NV_PGRAPH_CONTROL_0_ZENABLE;
        bool stencil_test = pg->regs[NV_PGRAPH_CONTROL_1]
                                & NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE;

        if (parameter == NV097_SET_BEGIN_END_OP_END) {

            assert(pg->shader_binding);

            if (pg->draw_arrays_length) {

                NV2A_GL_DPRINTF(false, "Draw Arrays");

                assert(pg->inline_buffer_length == 0);
                assert(pg->inline_array_length == 0);
                assert(pg->inline_elements_length == 0);

                pgraph_bind_vertex_attributes(d, pg->draw_arrays_max_count,
                                              false, 0);
                glMultiDrawArrays(pg->shader_binding->gl_primitive_mode,
                                  pg->gl_draw_arrays_start,
                                  pg->gl_draw_arrays_count,
                                  pg->draw_arrays_length);
            } else if (pg->inline_buffer_length) {

                NV2A_GL_DPRINTF(false, "Inline Buffer");

                assert(pg->draw_arrays_length == 0);
                assert(pg->inline_array_length == 0);
                assert(pg->inline_elements_length == 0);

                for (i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
                    VertexAttribute *attribute = &pg->vertex_attributes[i];

                    if (attribute->inline_buffer) {

                        glBindBuffer(GL_ARRAY_BUFFER,
                                     attribute->gl_inline_buffer);
                        glBufferData(GL_ARRAY_BUFFER,
                                     pg->inline_buffer_length
                                        * sizeof(float) * 4,
                                     attribute->inline_buffer,
                                     GL_DYNAMIC_DRAW);

                        /* Clear buffer for next batch */
                        g_free(attribute->inline_buffer);
                        attribute->inline_buffer = NULL;

                        glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, 0, 0);
                        glEnableVertexAttribArray(i);
                    } else {
                        glDisableVertexAttribArray(i);

                        glVertexAttrib4fv(i, attribute->inline_value);
                    }

                }

                glDrawArrays(pg->shader_binding->gl_primitive_mode,
                             0, pg->inline_buffer_length);
            } else if (pg->inline_array_length) {

                NV2A_GL_DPRINTF(false, "Inline Array");

                assert(pg->draw_arrays_length == 0);
                assert(pg->inline_buffer_length == 0);
                assert(pg->inline_elements_length == 0);

                unsigned int index_count = pgraph_bind_inline_array(d);
                glDrawArrays(pg->shader_binding->gl_primitive_mode,
                             0, index_count);
            } else if (pg->inline_elements_length) {

                NV2A_GL_DPRINTF(false, "Inline Elements");

                assert(pg->draw_arrays_length == 0);
                assert(pg->inline_buffer_length == 0);
                assert(pg->inline_array_length == 0);

                uint32_t max_element = 0;
                uint32_t min_element = (uint32_t)-1;
                for (i=0; i<pg->inline_elements_length; i++) {
                    max_element = MAX(pg->inline_elements[i], max_element);
                    min_element = MIN(pg->inline_elements[i], min_element);
                }

                pgraph_bind_vertex_attributes(d, max_element+1, false, 0);

                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pg->gl_element_buffer);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                             pg->inline_elements_length*4,
                             pg->inline_elements,
                             GL_DYNAMIC_DRAW);

                glDrawRangeElements(pg->shader_binding->gl_primitive_mode,
                                    min_element, max_element,
                                    pg->inline_elements_length,
                                    GL_UNSIGNED_INT,
                                    (void*)0);

            } else {
                NV2A_GL_DPRINTF(true, "EMPTY NV097_SET_BEGIN_END");
                assert(false);
            }

            /* End of visibility testing */
            if (pg->zpass_pixel_count_enable) {
                glEndQuery(GL_SAMPLES_PASSED);
            }

            NV2A_GL_DGROUP_END();
        } else {
            NV2A_GL_DGROUP_BEGIN("NV097_SET_BEGIN_END: 0x%x", parameter);
            assert(parameter <= NV097_SET_BEGIN_END_OP_POLYGON);

            pgraph_update_surface(d, true, true, depth_test || stencil_test);

            pg->primitive_mode = parameter;

            uint32_t control_0 = pg->regs[NV_PGRAPH_CONTROL_0];

            bool alpha = control_0 & NV_PGRAPH_CONTROL_0_ALPHA_WRITE_ENABLE;
            bool red = control_0 & NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE;
            bool green = control_0 & NV_PGRAPH_CONTROL_0_GREEN_WRITE_ENABLE;
            bool blue = control_0 & NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE;
            glColorMask(red, green, blue, alpha);
            glDepthMask(!!(control_0 & NV_PGRAPH_CONTROL_0_ZWRITEENABLE));
            glStencilMask(GET_MASK(pg->regs[NV_PGRAPH_CONTROL_1],
                                   NV_PGRAPH_CONTROL_1_STENCIL_MASK_WRITE));

            if (pg->regs[NV_PGRAPH_BLEND] & NV_PGRAPH_BLEND_EN) {
                glEnable(GL_BLEND);
                uint32_t sfactor = GET_MASK(pg->regs[NV_PGRAPH_BLEND],
                                            NV_PGRAPH_BLEND_SFACTOR);
                uint32_t dfactor = GET_MASK(pg->regs[NV_PGRAPH_BLEND],
                                            NV_PGRAPH_BLEND_DFACTOR);
                assert(sfactor < ARRAYSIZE(pgraph_blend_factor_map));
                assert(dfactor < ARRAYSIZE(pgraph_blend_factor_map));
                glBlendFunc(pgraph_blend_factor_map[sfactor],
                            pgraph_blend_factor_map[dfactor]);

                uint32_t equation = GET_MASK(pg->regs[NV_PGRAPH_BLEND],
                                             NV_PGRAPH_BLEND_EQN);
                assert(equation < ARRAYSIZE(pgraph_blend_equation_map));
                glBlendEquation(pgraph_blend_equation_map[equation]);

                uint32_t blend_color = pg->regs[NV_PGRAPH_BLENDCOLOR];
                glBlendColor( ((blend_color >> 16) & 0xFF) / 255.0f, /* red */
                              ((blend_color >> 8) & 0xFF) / 255.0f,  /* green */
                              (blend_color & 0xFF) / 255.0f,         /* blue */
                              ((blend_color >> 24) & 0xFF) / 255.0f);/* alpha */
            } else {
                glDisable(GL_BLEND);
            }

            /* Face culling */
            if (pg->regs[NV_PGRAPH_SETUPRASTER]
                    & NV_PGRAPH_SETUPRASTER_CULLENABLE) {
                uint32_t cull_face = GET_MASK(pg->regs[NV_PGRAPH_SETUPRASTER],
                                              NV_PGRAPH_SETUPRASTER_CULLCTRL);
                assert(cull_face < ARRAYSIZE(pgraph_cull_face_map));
                glCullFace(pgraph_cull_face_map[cull_face]);
                glEnable(GL_CULL_FACE);
            } else {
                glDisable(GL_CULL_FACE);
            }

            /* Front-face select */
            glFrontFace(pg->regs[NV_PGRAPH_SETUPRASTER]
                            & NV_PGRAPH_SETUPRASTER_FRONTFACE
                                ? GL_CCW : GL_CW);

            /* Polygon offset */
            /* FIXME: GL implementation-specific, maybe do this in VS? */
            if (pg->regs[NV_PGRAPH_SETUPRASTER] &
                    NV_PGRAPH_SETUPRASTER_POFFSETFILLENABLE) {
                glEnable(GL_POLYGON_OFFSET_FILL);
            } else {
                glDisable(GL_POLYGON_OFFSET_FILL);
            }
            if (pg->regs[NV_PGRAPH_SETUPRASTER] &
                    NV_PGRAPH_SETUPRASTER_POFFSETLINEENABLE) {
                glEnable(GL_POLYGON_OFFSET_LINE);
            } else {
                glDisable(GL_POLYGON_OFFSET_LINE);
            }
            if (pg->regs[NV_PGRAPH_SETUPRASTER] &
                    NV_PGRAPH_SETUPRASTER_POFFSETPOINTENABLE) {
                glEnable(GL_POLYGON_OFFSET_POINT);
            } else {
                glDisable(GL_POLYGON_OFFSET_POINT);
            }
            if (pg->regs[NV_PGRAPH_SETUPRASTER] &
                    (NV_PGRAPH_SETUPRASTER_POFFSETFILLENABLE |
                     NV_PGRAPH_SETUPRASTER_POFFSETLINEENABLE |
                     NV_PGRAPH_SETUPRASTER_POFFSETPOINTENABLE)) {
                GLfloat zfactor = *(float*)&pg->regs[NV_PGRAPH_ZOFFSETFACTOR];
                GLfloat zbias = *(float*)&pg->regs[NV_PGRAPH_ZOFFSETBIAS];
                glPolygonOffset(zfactor, zbias);
            }

            /* Depth testing */
            if (depth_test) {
                glEnable(GL_DEPTH_TEST);

                uint32_t depth_func = GET_MASK(pg->regs[NV_PGRAPH_CONTROL_0],
                                               NV_PGRAPH_CONTROL_0_ZFUNC);
                assert(depth_func < ARRAYSIZE(pgraph_depth_func_map));
                glDepthFunc(pgraph_depth_func_map[depth_func]);
            } else {
                glDisable(GL_DEPTH_TEST);
            }

            if (stencil_test) {
                glEnable(GL_STENCIL_TEST);

                uint32_t stencil_func = GET_MASK(pg->regs[NV_PGRAPH_CONTROL_1],
                                            NV_PGRAPH_CONTROL_1_STENCIL_FUNC);
                uint32_t stencil_ref = GET_MASK(pg->regs[NV_PGRAPH_CONTROL_1],
                                            NV_PGRAPH_CONTROL_1_STENCIL_REF);
                uint32_t func_mask = GET_MASK(pg->regs[NV_PGRAPH_CONTROL_1],
                                        NV_PGRAPH_CONTROL_1_STENCIL_MASK_READ);
                uint32_t op_fail = GET_MASK(pg->regs[NV_PGRAPH_CONTROL_2],
                                        NV_PGRAPH_CONTROL_2_STENCIL_OP_FAIL);
                uint32_t op_zfail = GET_MASK(pg->regs[NV_PGRAPH_CONTROL_2],
                                        NV_PGRAPH_CONTROL_2_STENCIL_OP_ZFAIL);
                uint32_t op_zpass = GET_MASK(pg->regs[NV_PGRAPH_CONTROL_2],
                                        NV_PGRAPH_CONTROL_2_STENCIL_OP_ZPASS);

                assert(stencil_func < ARRAYSIZE(pgraph_stencil_func_map));
                assert(op_fail < ARRAYSIZE(pgraph_stencil_op_map));
                assert(op_zfail < ARRAYSIZE(pgraph_stencil_op_map));
                assert(op_zpass < ARRAYSIZE(pgraph_stencil_op_map));

                glStencilFunc(
                    pgraph_stencil_func_map[stencil_func],
                    stencil_ref,
                    func_mask);

                glStencilOp(
                    pgraph_stencil_op_map[op_fail],
                    pgraph_stencil_op_map[op_zfail],
                    pgraph_stencil_op_map[op_zpass]);

            } else {
                glDisable(GL_STENCIL_TEST);
            }

            /* Dither */
            /* FIXME: GL implementation dependent */
            if (pg->regs[NV_PGRAPH_CONTROL_0] &
                    NV_PGRAPH_CONTROL_0_DITHERENABLE) {
                glEnable(GL_DITHER);
            } else {
                glDisable(GL_DITHER);
            }

            pgraph_bind_shaders(pg);
            pgraph_bind_textures(d);

            //glDisableVertexAttribArray(NV2A_VERTEX_ATTR_DIFFUSE);
            //glVertexAttrib4f(NV2A_VERTEX_ATTR_DIFFUSE, 1.0, 1.0, 1.0, 1.0);


            unsigned int width, height;
            pgraph_get_surface_dimensions(pg, &width, &height);
            pgraph_apply_anti_aliasing_factor(pg, &width, &height);
            glViewport(0, 0, width, height);

            pg->inline_elements_length = 0;
            pg->inline_array_length = 0;
            pg->inline_buffer_length = 0;
            pg->draw_arrays_length = 0;
            pg->draw_arrays_max_count = 0;

            /* Visibility testing */
            if (pg->zpass_pixel_count_enable) {
                GLuint gl_query;
                glGenQueries(1, &gl_query);
                pg->gl_zpass_pixel_count_query_count++;
                pg->gl_zpass_pixel_count_queries = g_realloc(
                    pg->gl_zpass_pixel_count_queries,
                    sizeof(GLuint) * pg->gl_zpass_pixel_count_query_count);
                pg->gl_zpass_pixel_count_queries[
                    pg->gl_zpass_pixel_count_query_count - 1] = gl_query;
                glBeginQuery(GL_SAMPLES_PASSED, gl_query);
            }

        }

        pgraph_set_surface_dirty(pg, true, depth_test || stencil_test);
        break;
    }
    CASE_4(NV097_SET_TEXTURE_OFFSET, 64):
        slot = (class_method - NV097_SET_TEXTURE_OFFSET) / 64;
        pg->regs[NV_PGRAPH_TEXOFFSET0 + slot * 4] = parameter;
        pg->texture_dirty[slot] = true;
        break;
    CASE_4(NV097_SET_TEXTURE_FORMAT, 64): {
        slot = (class_method - NV097_SET_TEXTURE_FORMAT) / 64;

        bool dma_select =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_CONTEXT_DMA) == 2;
        bool cubemap =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_CUBEMAP_ENABLE);
        unsigned int border_source =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_BORDER_SOURCE);
        unsigned int dimensionality =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_DIMENSIONALITY);
        unsigned int color_format =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_COLOR);
        unsigned int levels =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_MIPMAP_LEVELS);
        unsigned int log_width =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_BASE_SIZE_U);
        unsigned int log_height =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_BASE_SIZE_V);
        unsigned int log_depth =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_BASE_SIZE_P);

        uint32_t *reg = &pg->regs[NV_PGRAPH_TEXFMT0 + slot * 4];
        SET_MASK(*reg, NV_PGRAPH_TEXFMT0_CONTEXT_DMA, dma_select);
        SET_MASK(*reg, NV_PGRAPH_TEXFMT0_CUBEMAPENABLE, cubemap);
        SET_MASK(*reg, NV_PGRAPH_TEXFMT0_BORDER_SOURCE, border_source);
        SET_MASK(*reg, NV_PGRAPH_TEXFMT0_DIMENSIONALITY, dimensionality);
        SET_MASK(*reg, NV_PGRAPH_TEXFMT0_COLOR, color_format);
        SET_MASK(*reg, NV_PGRAPH_TEXFMT0_MIPMAP_LEVELS, levels);
        SET_MASK(*reg, NV_PGRAPH_TEXFMT0_BASE_SIZE_U, log_width);
        SET_MASK(*reg, NV_PGRAPH_TEXFMT0_BASE_SIZE_V, log_height);
        SET_MASK(*reg, NV_PGRAPH_TEXFMT0_BASE_SIZE_P, log_depth);

        pg->texture_dirty[slot] = true;
        break;
    }
    CASE_4(NV097_SET_TEXTURE_CONTROL0, 64):
        slot = (class_method - NV097_SET_TEXTURE_CONTROL0) / 64;
        pg->regs[NV_PGRAPH_TEXCTL0_0 + slot*4] = parameter;
        break;
    CASE_4(NV097_SET_TEXTURE_CONTROL1, 64):
        slot = (class_method - NV097_SET_TEXTURE_CONTROL1) / 64;
        pg->regs[NV_PGRAPH_TEXCTL1_0 + slot*4] = parameter;
        break;
    CASE_4(NV097_SET_TEXTURE_FILTER, 64):
        slot = (class_method - NV097_SET_TEXTURE_FILTER) / 64;
        pg->regs[NV_PGRAPH_TEXFILTER0 + slot * 4] = parameter;
        break;
    CASE_4(NV097_SET_TEXTURE_IMAGE_RECT, 64):
        slot = (class_method - NV097_SET_TEXTURE_IMAGE_RECT) / 64;
        pg->regs[NV_PGRAPH_TEXIMAGERECT0 + slot * 4] = parameter;
        pg->texture_dirty[slot] = true;
        break;
    CASE_4(NV097_SET_TEXTURE_PALETTE, 64): {
        slot = (class_method - NV097_SET_TEXTURE_PALETTE) / 64;

        bool dma_select =
            GET_MASK(parameter, NV097_SET_TEXTURE_PALETTE_CONTEXT_DMA) == 1;
        unsigned int length =
            GET_MASK(parameter, NV097_SET_TEXTURE_PALETTE_LENGTH);
        unsigned int offset =
            GET_MASK(parameter, NV097_SET_TEXTURE_PALETTE_OFFSET);

        uint32_t *reg = &pg->regs[NV_PGRAPH_TEXPALETTE0 + slot * 4];
        SET_MASK(*reg, NV_PGRAPH_TEXPALETTE0_CONTEXT_DMA, dma_select);
        SET_MASK(*reg, NV_PGRAPH_TEXPALETTE0_LENGTH, length);
        SET_MASK(*reg, NV_PGRAPH_TEXPALETTE0_OFFSET, offset);

        pg->texture_dirty[slot] = true;
        break;
    }

    CASE_4(NV097_SET_TEXTURE_BORDER_COLOR, 64):
        slot = (class_method - NV097_SET_TEXTURE_BORDER_COLOR) / 64;
        pg->regs[NV_PGRAPH_BORDERCOLOR0 + slot * 4] = parameter;
        break;
    CASE_4(NV097_SET_TEXTURE_SET_BUMP_ENV_MAT + 0x0, 64):
    CASE_4(NV097_SET_TEXTURE_SET_BUMP_ENV_MAT + 0x4, 64):
    CASE_4(NV097_SET_TEXTURE_SET_BUMP_ENV_MAT + 0x8, 64):
    CASE_4(NV097_SET_TEXTURE_SET_BUMP_ENV_MAT + 0xc, 64):
        slot = (class_method - NV097_SET_TEXTURE_SET_BUMP_ENV_MAT) / 4;
        assert((slot / 16) > 0);
        slot -= 16;
        pg->bump_env_matrix[slot / 16][slot % 4] = *(float*)&parameter;
        break;

    CASE_4(NV097_SET_TEXTURE_SET_BUMP_ENV_SCALE, 64):
        slot = (class_method - NV097_SET_TEXTURE_SET_BUMP_ENV_SCALE) / 64;
        assert(slot > 0);
        slot--;
        pg->regs[NV_PGRAPH_BUMPSCALE1 + slot * 4] = parameter;
        break;
    CASE_4(NV097_SET_TEXTURE_SET_BUMP_ENV_OFFSET, 64):
        slot = (class_method - NV097_SET_TEXTURE_SET_BUMP_ENV_OFFSET) / 64;
        assert(slot > 0);
        slot--;
        pg->regs[NV_PGRAPH_BUMPOFFSET1 + slot * 4] = parameter;
        break;

    case NV097_ARRAY_ELEMENT16:
        assert(pg->inline_elements_length < NV2A_MAX_BATCH_LENGTH);
        pg->inline_elements[
            pg->inline_elements_length++] = parameter & 0xFFFF;
        pg->inline_elements[
            pg->inline_elements_length++] = parameter >> 16;
        break;
    case NV097_ARRAY_ELEMENT32:
        assert(pg->inline_elements_length < NV2A_MAX_BATCH_LENGTH);
        pg->inline_elements[
            pg->inline_elements_length++] = parameter;
        break;
    case NV097_DRAW_ARRAYS: {

        unsigned int start = GET_MASK(parameter, NV097_DRAW_ARRAYS_START_INDEX);
        unsigned int count = GET_MASK(parameter, NV097_DRAW_ARRAYS_COUNT)+1;

        pg->draw_arrays_max_count = MAX(pg->draw_arrays_max_count, start + count);

        assert(pg->draw_arrays_length < ARRAYSIZE(pg->gl_draw_arrays_start));

        /* Attempt to connect primitives */
        if (pg->draw_arrays_length > 0) {
            unsigned int last_start =
                pg->gl_draw_arrays_start[pg->draw_arrays_length - 1];
            GLsizei* last_count =
                &pg->gl_draw_arrays_count[pg->draw_arrays_length - 1];
            if (start == (last_start + *last_count)) {
                *last_count += count;
                break;
            }
        }

        pg->gl_draw_arrays_start[pg->draw_arrays_length] = start;
        pg->gl_draw_arrays_count[pg->draw_arrays_length] = count;
        pg->draw_arrays_length++;
        break;
    }
    case NV097_INLINE_ARRAY:
        assert(pg->inline_array_length < NV2A_MAX_BATCH_LENGTH);
        pg->inline_array[
            pg->inline_array_length++] = parameter;
        break;
    case NV097_SET_EYE_VECTOR ...
            NV097_SET_EYE_VECTOR + 8:
        slot = (class_method - NV097_SET_EYE_VECTOR) / 4;
        pg->regs[NV_PGRAPH_EYEVEC0 + slot * 4] = parameter;
        break;

    case NV097_SET_VERTEX_DATA2F_M ...
            NV097_SET_VERTEX_DATA2F_M + 0x7c: {
        slot = (class_method - NV097_SET_VERTEX_DATA2F_M) / 4;
        unsigned int part = slot % 2;
        slot /= 2;
        VertexAttribute *attribute = &pg->vertex_attributes[slot];
        pgraph_allocate_inline_buffer_vertices(pg, slot);
        attribute->inline_value[part] = *(float*)&parameter;
        /* FIXME: Should these really be set to 0.0 and 1.0 ? Conditions? */
        attribute->inline_value[2] = 0.0;
        attribute->inline_value[3] = 1.0;
        if ((slot == 0) && (part == 1)) {
            pgraph_finish_inline_buffer_vertex(pg);
        }
        break;
    }
    case NV097_SET_VERTEX_DATA4F_M ...
            NV097_SET_VERTEX_DATA4F_M + 0xfc: {
        slot = (class_method - NV097_SET_VERTEX_DATA4F_M) / 4;
        unsigned int part = slot % 4;
        slot /= 4;
        VertexAttribute *attribute = &pg->vertex_attributes[slot];
        pgraph_allocate_inline_buffer_vertices(pg, slot);
        attribute->inline_value[part] = *(float*)&parameter;
        if ((slot == 0) && (part == 3)) {
            pgraph_finish_inline_buffer_vertex(pg);
        }
        break;
    }
    case NV097_SET_VERTEX_DATA2S ...
            NV097_SET_VERTEX_DATA2S + 0x3c: {
        slot = (class_method - NV097_SET_VERTEX_DATA2S) / 4;
        assert(false); /* FIXME: Untested! */
        VertexAttribute *attribute = &pg->vertex_attributes[slot];
        pgraph_allocate_inline_buffer_vertices(pg, slot);
        /* FIXME: Is mapping to [-1,+1] correct? */
        attribute->inline_value[0] = ((int16_t)(parameter & 0xFFFF) * 2.0 + 1)
                                         / 65535.0;
        attribute->inline_value[1] = ((int16_t)(parameter >> 16) * 2.0 + 1)
                                         / 65535.0;
        /* FIXME: Should these really be set to 0.0 and 1.0 ? Conditions? */
        attribute->inline_value[2] = 0.0;
        attribute->inline_value[3] = 1.0;
        if (slot == 0) {
            pgraph_finish_inline_buffer_vertex(pg);
            assert(false); /* FIXME: Untested */
        }
        break;
    }
    case NV097_SET_VERTEX_DATA4UB ...
            NV097_SET_VERTEX_DATA4UB + 0x3c: {
        slot = (class_method - NV097_SET_VERTEX_DATA4UB) / 4;
        VertexAttribute *attribute = &pg->vertex_attributes[slot];
        pgraph_allocate_inline_buffer_vertices(pg, slot);
        attribute->inline_value[0] = (parameter & 0xFF) / 255.0;
        attribute->inline_value[1] = ((parameter >> 8) & 0xFF) / 255.0;
        attribute->inline_value[2] = ((parameter >> 16) & 0xFF) / 255.0;
        attribute->inline_value[3] = ((parameter >> 24) & 0xFF) / 255.0;
        if (slot == 0) {
            pgraph_finish_inline_buffer_vertex(pg);
            assert(false); /* FIXME: Untested */
        }
        break;
    }
    case NV097_SET_VERTEX_DATA4S_M ...
            NV097_SET_VERTEX_DATA4S_M + 0x7c: {
        slot = (class_method - NV097_SET_VERTEX_DATA4S_M) / 4;
        unsigned int part = slot % 2;
        slot /= 2;
        assert(false); /* FIXME: Untested! */
        VertexAttribute *attribute = &pg->vertex_attributes[slot];
        pgraph_allocate_inline_buffer_vertices(pg, slot);
        /* FIXME: Is mapping to [-1,+1] correct? */
        attribute->inline_value[part * 2 + 0] = ((int16_t)(parameter & 0xFFFF)
                                                     * 2.0 + 1) / 65535.0;
        attribute->inline_value[part * 2 + 1] = ((int16_t)(parameter >> 16)
                                                     * 2.0 + 1) / 65535.0;
        if ((slot == 0) && (part == 1)) {
            pgraph_finish_inline_buffer_vertex(pg);
            assert(false); /* FIXME: Untested */
        }
        break;
    }

    case NV097_SET_SEMAPHORE_OFFSET:
        kelvin->semaphore_offset = parameter;
        break;
    case NV097_BACK_END_WRITE_SEMAPHORE_RELEASE: {

        pgraph_update_surface(d, false, true, true);

        //qemu_mutex_unlock(&d->pgraph.lock);
        //qemu_mutex_lock_iothread();

        hwaddr semaphore_dma_len;
        uint8_t *semaphore_data = nv_dma_map(d, kelvin->dma_semaphore,
                                             &semaphore_dma_len);
        assert(kelvin->semaphore_offset < semaphore_dma_len);
        semaphore_data += kelvin->semaphore_offset;

        stl_le_p((uint32_t*)semaphore_data, parameter);

        //qemu_mutex_lock(&d->pgraph.lock);
        //qemu_mutex_unlock_iothread();

        break;
    }
    case NV097_SET_ZSTENCIL_CLEAR_VALUE:
        pg->regs[NV_PGRAPH_ZSTENCILCLEARVALUE] = parameter;
        break;

    case NV097_SET_COLOR_CLEAR_VALUE:
        pg->regs[NV_PGRAPH_COLORCLEARVALUE] = parameter;
        break;

    case NV097_CLEAR_SURFACE: {
        NV2A_DPRINTF("---------PRE CLEAR ------\n");
        GLbitfield gl_mask = 0;

        bool write_color = (parameter & NV097_CLEAR_SURFACE_COLOR);
        bool write_zeta =
            (parameter & (NV097_CLEAR_SURFACE_Z | NV097_CLEAR_SURFACE_STENCIL));

        if (write_zeta) {
            uint32_t clear_zstencil =
                d->pgraph.regs[NV_PGRAPH_ZSTENCILCLEARVALUE];
            GLint gl_clear_stencil;
            GLfloat gl_clear_depth;

            /* FIXME: Put these in some lookup table */
            const float f16_max = 511.9375f;
            /* FIXME: 7 bits of mantissa unused. maybe use full buffer? */
            const float f24_max = 3.4027977E38;

            switch(pg->surface_shape.zeta_format) {
            case NV097_SET_SURFACE_FORMAT_ZETA_Z16: {
                uint16_t z = clear_zstencil & 0xFFFF;
                /* FIXME: Remove bit for stencil clear? */
                if (pg->surface_shape.z_format) {
                    gl_clear_depth = convert_f16_to_float(z) / f16_max;
                    assert(false); /* FIXME: Untested */
                } else {
                    gl_clear_depth = z / (float)0xFFFF;
                }
                break;
            }
            case NV097_SET_SURFACE_FORMAT_ZETA_Z24S8: {
                gl_clear_stencil = clear_zstencil & 0xFF;
                uint32_t z = clear_zstencil >> 8;
                if (pg->surface_shape.z_format) {
                    gl_clear_depth = convert_f24_to_float(z) / f24_max;
                    assert(false); /* FIXME: Untested */
                } else {
                    gl_clear_depth = z / (float)0xFFFFFF;
                }
                break;
            }
            default:
                assert(false);
                break;
            }
            if (parameter & NV097_CLEAR_SURFACE_Z) {
                gl_mask |= GL_DEPTH_BUFFER_BIT;
                glDepthMask(GL_TRUE);
                glClearDepth(gl_clear_depth);
            }
            if (parameter & NV097_CLEAR_SURFACE_STENCIL) {
                gl_mask |= GL_STENCIL_BUFFER_BIT;
                glStencilMask(0xff);
                glClearStencil(gl_clear_stencil);            
            }
        }
        if (write_color) {
            gl_mask |= GL_COLOR_BUFFER_BIT;
            glColorMask((parameter & NV097_CLEAR_SURFACE_R)
                             ? GL_TRUE : GL_FALSE,
                        (parameter & NV097_CLEAR_SURFACE_G)
                             ? GL_TRUE : GL_FALSE,
                        (parameter & NV097_CLEAR_SURFACE_B)
                             ? GL_TRUE : GL_FALSE,
                        (parameter & NV097_CLEAR_SURFACE_A)
                             ? GL_TRUE : GL_FALSE);
            uint32_t clear_color = d->pgraph.regs[NV_PGRAPH_COLORCLEARVALUE];

            /* Handle RGB */
            GLfloat red, green, blue;
            switch(pg->surface_shape.color_format) {
            case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_Z1R5G5B5:
            case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_O1R5G5B5:
                red = ((clear_color >> 10) & 0x1F) / 31.0f;
                green = ((clear_color >> 5) & 0x1F) / 31.0f;
                blue = (clear_color & 0x1F) / 31.0f;
                assert(false); /* Untested */
                break;
            case NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5:
                red = ((clear_color >> 11) & 0x1F) / 31.0f;
                green = ((clear_color >> 5) & 0x3F) / 63.0f;
                blue = (clear_color & 0x1F) / 31.0f;
                break;
            case NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8:
            case NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_O8R8G8B8:
            case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1A7R8G8B8_Z1A7R8G8B8:
            case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1A7R8G8B8_O1A7R8G8B8:
            case NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8:
                red = ((clear_color >> 16) & 0xFF) / 255.0f;
                green = ((clear_color >> 8) & 0xFF) / 255.0f;
                blue = (clear_color & 0xFF) / 255.0f;
                break;
            case NV097_SET_SURFACE_FORMAT_COLOR_LE_B8:
            case NV097_SET_SURFACE_FORMAT_COLOR_LE_G8B8:
                /* Xbox D3D doesn't support clearing those */
            default:
                red = 1.0f;
                green = 0.0f;
                blue = 1.0f;
                fprintf(stderr, "CLEAR_SURFACE for color_format 0x%x unsupported",
                        pg->surface_shape.color_format);
                assert(false);
                break;
            }

            /* Handle alpha */
            GLfloat alpha;
            switch(pg->surface_shape.color_format) {
            /* FIXME: CLEAR_SURFACE seems to work like memset, so maybe we
             *        also have to clear non-alpha bits with alpha value?
             *        As GL doesn't own those pixels we'd have to do this on
             *        our own in xbox memory.
             */
            case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1A7R8G8B8_Z1A7R8G8B8:
            case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1A7R8G8B8_O1A7R8G8B8:
                alpha = ((clear_color >> 24) & 0x7F) / 127.0f;
                assert(false); /* Untested */
                break;
            case NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8:
                alpha = ((clear_color >> 24) & 0xFF) / 255.0f;
                break;
            default:
                alpha = 1.0f;
                break;
            }

            glClearColor(red, green, blue, alpha);
        }
        pgraph_update_surface(d, true, write_color, write_zeta);

        glEnable(GL_SCISSOR_TEST);

        unsigned int xmin = GET_MASK(pg->regs[NV_PGRAPH_CLEARRECTX],
                NV_PGRAPH_CLEARRECTX_XMIN);
        unsigned int xmax = GET_MASK(pg->regs[NV_PGRAPH_CLEARRECTX],
                NV_PGRAPH_CLEARRECTX_XMAX);
        unsigned int ymin = GET_MASK(pg->regs[NV_PGRAPH_CLEARRECTY],
                NV_PGRAPH_CLEARRECTY_YMIN);
        unsigned int ymax = GET_MASK(pg->regs[NV_PGRAPH_CLEARRECTY],
                NV_PGRAPH_CLEARRECTY_YMAX);

        unsigned int scissor_x = xmin;
        unsigned int scissor_y = pg->surface_shape.clip_height - ymax - 1;

        unsigned int scissor_width = xmax - xmin + 1;
        unsigned int scissor_height = ymax - ymin + 1;

        pgraph_apply_anti_aliasing_factor(pg, &scissor_x, &scissor_y);
        pgraph_apply_anti_aliasing_factor(pg, &scissor_width, &scissor_height);

        /* FIXME: Should this really be inverted instead of ymin? */
        glScissor(scissor_x, scissor_y, scissor_width, scissor_height);

        NV2A_DPRINTF("------------------CLEAR 0x%x %d,%d - %d,%d  %x---------------\n",
            parameter, xmin, ymin, xmax, ymax, d->pgraph.regs[NV_PGRAPH_COLORCLEARVALUE]);

        /* Dither */
        /* FIXME: Maybe also disable it here? + GL implementation dependent */
        if (pg->regs[NV_PGRAPH_CONTROL_0] &
                NV_PGRAPH_CONTROL_0_DITHERENABLE) {
            glEnable(GL_DITHER);
        } else {
            glDisable(GL_DITHER);
        }

        glClear(gl_mask);

        glDisable(GL_SCISSOR_TEST);

        pgraph_set_surface_dirty(pg, write_color, write_zeta);
        break;
    }

    case NV097_SET_CLEAR_RECT_HORIZONTAL:
        pg->regs[NV_PGRAPH_CLEARRECTX] = parameter;
        break;
    case NV097_SET_CLEAR_RECT_VERTICAL:
        pg->regs[NV_PGRAPH_CLEARRECTY] = parameter;
        break;

    case NV097_SET_SPECULAR_FOG_FACTOR ...
            NV097_SET_SPECULAR_FOG_FACTOR + 4:
        slot = (class_method - NV097_SET_SPECULAR_FOG_FACTOR) / 4;
        pg->regs[NV_PGRAPH_SPECFOGFACTOR0 + slot*4] = parameter;
        break;

    case NV097_SET_SHADER_CLIP_PLANE_MODE:
        pg->regs[NV_PGRAPH_SHADERCLIPMODE] = parameter;
        break;

    case NV097_SET_COMBINER_COLOR_OCW ...
            NV097_SET_COMBINER_COLOR_OCW + 28:
        slot = (class_method - NV097_SET_COMBINER_COLOR_OCW) / 4;
        pg->regs[NV_PGRAPH_COMBINECOLORO0 + slot*4] = parameter;
        break;

    case NV097_SET_COMBINER_CONTROL:
        pg->regs[NV_PGRAPH_COMBINECTL] = parameter;
        break;

    case NV097_SET_SHADOW_ZSLOPE_THRESHOLD:
        pg->regs[NV_PGRAPH_SHADOWZSLOPETHRESHOLD] = parameter;
        assert(parameter == 0x7F800000); /* FIXME: Unimplemented */
        break;

    case NV097_SET_SHADER_STAGE_PROGRAM:
        pg->regs[NV_PGRAPH_SHADERPROG] = parameter;
        break;

    case NV097_SET_SHADER_OTHER_STAGE_INPUT:
        pg->regs[NV_PGRAPH_SHADERCTL] = parameter;
        break;

    case NV097_SET_TRANSFORM_EXECUTION_MODE:
        SET_MASK(pg->regs[NV_PGRAPH_CSV0_D], NV_PGRAPH_CSV0_D_MODE,
                 GET_MASK(parameter,
                          NV097_SET_TRANSFORM_EXECUTION_MODE_MODE));
        SET_MASK(pg->regs[NV_PGRAPH_CSV0_D], NV_PGRAPH_CSV0_D_RANGE_MODE,
                 GET_MASK(parameter,
                          NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE));
        break;
    case NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN:
        pg->enable_vertex_program_write = parameter;
        break;
    case NV097_SET_TRANSFORM_PROGRAM_LOAD:
        assert(parameter < NV2A_MAX_TRANSFORM_PROGRAM_LENGTH);
        SET_MASK(pg->regs[NV_PGRAPH_CHEOPS_OFFSET],
                 NV_PGRAPH_CHEOPS_OFFSET_PROG_LD_PTR, parameter);
        break;
    case NV097_SET_TRANSFORM_PROGRAM_START:
        assert(parameter < NV2A_MAX_TRANSFORM_PROGRAM_LENGTH);
        SET_MASK(pg->regs[NV_PGRAPH_CSV0_C],
                 NV_PGRAPH_CSV0_C_CHEOPS_PROGRAM_START, parameter);
        break;
    case NV097_SET_TRANSFORM_CONSTANT_LOAD:
        assert(parameter < NV2A_VERTEXSHADER_CONSTANTS);
        SET_MASK(pg->regs[NV_PGRAPH_CHEOPS_OFFSET],
                 NV_PGRAPH_CHEOPS_OFFSET_CONST_LD_PTR, parameter);
        NV2A_DPRINTF("load to %d\n", parameter);
        break;

    default:
        NV2A_GL_DPRINTF(true, "    unhandled  (0x%02x 0x%08x)",
                        object->graphics_class, method);
        break;
    }
}


static void pgraph_context_switch(NV2AState *d, unsigned int channel_id)
{
    bool valid;
    valid = d->pgraph.channel_valid && d->pgraph.channel_id == channel_id;
    if (!valid) {
        d->pgraph.trapped_channel_id = channel_id;
    }
    if (!valid) {
        NV2A_DPRINTF("puller needs to switch to ch %d\n", channel_id);

        qemu_mutex_unlock(&d->pgraph.lock);
        qemu_mutex_lock_iothread();
        d->pgraph.pending_interrupts |= NV_PGRAPH_INTR_CONTEXT_SWITCH;
        update_irq(d);

        qemu_mutex_lock(&d->pgraph.lock);
        qemu_mutex_unlock_iothread();

        while (d->pgraph.pending_interrupts & NV_PGRAPH_INTR_CONTEXT_SWITCH) {
            qemu_cond_wait(&d->pgraph.interrupt_cond, &d->pgraph.lock);
        }
    }
}

static void pgraph_wait_fifo_access(NV2AState *d) {
    while (!d->pgraph.fifo_access) {
        qemu_cond_wait(&d->pgraph.fifo_access_cond, &d->pgraph.lock);
    }
}

static void* pfifo_puller_thread(void *arg)
{
    NV2AState *d = arg;
    Cache1State *state = &d->pfifo.cache1;

    glo_set_current(d->pgraph.gl_context);

    while (true) {
        qemu_mutex_lock(&state->cache_lock);
        while (QSIMPLEQ_EMPTY(&state->cache) || !state->pull_enabled) {
            qemu_cond_wait(&state->cache_cond, &state->cache_lock);

            if (d->exiting) {
                qemu_mutex_unlock(&state->cache_lock);
                glo_set_current(NULL);
                return NULL;
            }
        }
        QSIMPLEQ_CONCAT(&state->working_cache, &state->cache);
        qemu_mutex_unlock(&state->cache_lock);

        qemu_mutex_lock(&d->pgraph.lock);

        while (!QSIMPLEQ_EMPTY(&state->working_cache)) {
            CacheEntry * command = QSIMPLEQ_FIRST(&state->working_cache);
            QSIMPLEQ_REMOVE_HEAD(&state->working_cache, entry);

            if (command->method == 0) {
                // qemu_mutex_lock_iothread();
                RAMHTEntry entry = ramht_lookup(d, command->parameter);
                assert(entry.valid);

                assert(entry.channel_id == state->channel_id);
                // qemu_mutex_unlock_iothread();

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
                qemu_mutex_lock(&state->cache_lock);
                state->bound_engines[command->subchannel] = entry.engine;
                state->last_engine = entry.engine;
                qemu_mutex_unlock(&state->cache_lock);
            } else if (command->method >= 0x100) {
                /* method passed to engine */

                uint32_t parameter = command->parameter;

                /* methods that take objects.
                 * TODO: Check this range is correct for the nv2a */
                if (command->method >= 0x180 && command->method < 0x200) {
                    //qemu_mutex_lock_iothread();
                    RAMHTEntry entry = ramht_lookup(d, parameter);
                    assert(entry.valid);
                    assert(entry.channel_id == state->channel_id);
                    parameter = entry.instance;
                    //qemu_mutex_unlock_iothread();
                }

                // qemu_mutex_lock(&state->cache_lock);
                enum FIFOEngine engine = state->bound_engines[command->subchannel];
                // qemu_mutex_unlock(&state->cache_lock);

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

                // qemu_mutex_lock(&state->cache_lock);
                state->last_engine = state->bound_engines[command->subchannel];
                // qemu_mutex_unlock(&state->cache_lock);
            }

            g_free(command);
        }

        qemu_mutex_unlock(&d->pgraph.lock);
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
    uint32_t channel_modes = d->pfifo.regs[NV_PFIFO_MODE];
    assert(channel_modes & (1 << channel_id));
    assert(state->mode == FIFO_DMA);

    if (!state->dma_push_enabled) return;
    if (state->dma_push_suspended) return;

    /* We're running so there should be no pending errors... */
    assert(state->error == NV_PFIFO_CACHE1_DMA_STATE_ERROR_NONE);

    dma = nv_dma_map(d, state->dma_instance, &dma_len);

    NV2A_DPRINTF("DMA pusher: max 0x%" HWADDR_PRIx ", 0x%" HWADDR_PRIx " - 0x%" HWADDR_PRIx "\n",
                 dma_len, control->dma_get, control->dma_put);

    /* based on the convenient pseudocode in envytools */
    while (control->dma_get != control->dma_put) {
        if (control->dma_get >= dma_len) {

            state->error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_PROTECTION;
            break;
        }

        word = ldl_le_p((uint32_t*)(dma + control->dma_get));
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
                NV2A_DPRINTF("pb OLD_JMP 0x%" HWADDR_PRIx "\n", control->dma_get);
            } else if ((word & 3) == 1) {
                /* jump */
                state->get_jmp_shadow = control->dma_get;
                control->dma_get = word & 0xfffffffc;
                NV2A_DPRINTF("pb JMP 0x%" HWADDR_PRIx "\n", control->dma_get);
            } else if ((word & 3) == 2) {
                /* call */
                if (state->subroutine_active) {
                    state->error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_CALL;
                    break;
                }
                state->subroutine_return = control->dma_get;
                state->subroutine_active = true;
                control->dma_get = word & 0xfffffffc;
                NV2A_DPRINTF("pb CALL 0x%" HWADDR_PRIx "\n", control->dma_get);
            } else if (word == 0x00020000) {
                /* return */
                if (!state->subroutine_active) {
                    state->error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_RETURN;
                    break;
                }
                control->dma_get = state->subroutine_return;
                state->subroutine_active = false;
                NV2A_DPRINTF("pb RET 0x%" HWADDR_PRIx "\n", control->dma_get);
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
                NV2A_DPRINTF("pb reserved cmd 0x%" HWADDR_PRIx " - 0x%x\n",
                             control->dma_get, word);
                state->error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_RESERVED_CMD;
                break;
            }
        }
    }

    NV2A_DPRINTF("DMA pusher done: max 0x%" HWADDR_PRIx ", 0x%" HWADDR_PRIx " - 0x%" HWADDR_PRIx "\n",
                 dma_len, control->dma_get, control->dma_put);

    if (state->error) {
        NV2A_DPRINTF("pb error: %d\n", state->error);
        assert(false);

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
    case NV_PFIFO_RUNOUT_STATUS:
        r = NV_PFIFO_RUNOUT_STATUS_LOW_MARK; /* low mark empty */
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
        qemu_mutex_lock(&d->pfifo.cache1.cache_lock);
        r = d->pfifo.cache1.pull_enabled;
        qemu_mutex_unlock(&d->pfifo.cache1.cache_lock);
        break;
    case NV_PFIFO_CACHE1_ENGINE:
        qemu_mutex_lock(&d->pfifo.cache1.cache_lock);
        for (i=0; i<NV2A_NUM_SUBCHANNELS; i++) {
            r |= d->pfifo.cache1.bound_engines[i] << (i*2);
        }
        qemu_mutex_unlock(&d->pfifo.cache1.cache_lock);
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
        r = d->pfifo.regs[addr];
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
        qemu_mutex_lock(&d->pfifo.cache1.cache_lock);
        if ((val & NV_PFIFO_CACHE1_PULL0_ACCESS)
             && !d->pfifo.cache1.pull_enabled) {
            d->pfifo.cache1.pull_enabled = true;

            /* the puller thread should wake up */
            qemu_cond_signal(&d->pfifo.cache1.cache_cond);
        } else if (!(val & NV_PFIFO_CACHE1_PULL0_ACCESS)
                     && d->pfifo.cache1.pull_enabled) {
            d->pfifo.cache1.pull_enabled = false;
        }
        qemu_mutex_unlock(&d->pfifo.cache1.cache_lock);
        break;
    case NV_PFIFO_CACHE1_ENGINE:
        qemu_mutex_lock(&d->pfifo.cache1.cache_lock);
        for (i=0; i<NV2A_NUM_SUBCHANNELS; i++) {
            d->pfifo.cache1.bound_engines[i] = (val >> (i*2)) & 3;
        }
        qemu_mutex_unlock(&d->pfifo.cache1.cache_lock);
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
        d->pfifo.regs[addr] = val;
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


static void pvideo_vga_invalidate(NV2AState *d)
{
    int y1 = GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_OUT],
                      NV_PVIDEO_POINT_OUT_Y);
    int y2 = y1 + GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_OUT],
                           NV_PVIDEO_SIZE_OUT_HEIGHT);
    NV2A_DPRINTF("pvideo_vga_invalidate %d %d\n", y1, y2);
    vga_invalidate_scanlines(&d->vga, y1, y2);
}

static uint64_t pvideo_read(void *opaque,
                            hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PVIDEO_STOP:
        r = 0;
        break;
    default:
        r = d->pvideo.regs[addr];
        break;
    }

    reg_log_read(NV_PVIDEO, addr, r);
    return r;
}
static void pvideo_write(void *opaque, hwaddr addr,
                         uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    reg_log_write(NV_PVIDEO, addr, val);

    switch (addr) {
    case NV_PVIDEO_BUFFER:
        d->pvideo.regs[addr] = val;
        d->vga.enable_overlay = true;
        pvideo_vga_invalidate(d);
        break;
    case NV_PVIDEO_STOP:
        d->pvideo.regs[NV_PVIDEO_BUFFER] = 0;
        d->vga.enable_overlay = false;
        pvideo_vga_invalidate(d);
        break;
    default:
        d->pvideo.regs[addr] = val;
        break;
    }
}




/* PIMTER - time measurement and time-based alarms */
static uint64_t ptimer_get_clock(NV2AState *d)
{
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
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
    case NV_PFB_WBC:
        r = 0; /* Flush not pending. */
        break;
    default:
        r = d->pfb.regs[addr];
        break;
    }

    reg_log_read(NV_PFB, addr, r);
    return r;
}
static void pfb_write(void *opaque, hwaddr addr,
                       uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    reg_log_write(NV_PFB, addr, val);

    switch (addr) {
    default:
        d->pfb.regs[addr] = val;
        break;
    }
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

    qemu_mutex_lock(&d->pgraph.lock);

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
        SET_MASK(r, NV_PGRAPH_CTX_USER_CHANNEL_3D,
                 d->pgraph.context[d->pgraph.channel_id].channel_3d);
        SET_MASK(r, NV_PGRAPH_CTX_USER_CHANNEL_3D_VALID, 1);
        SET_MASK(r, NV_PGRAPH_CTX_USER_SUBCH,
                 d->pgraph.context[d->pgraph.channel_id].subchannel << 13);
        SET_MASK(r, NV_PGRAPH_CTX_USER_CHID, d->pgraph.channel_id);
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
    default:
        r = d->pgraph.regs[addr];
        break;
    }

    qemu_mutex_unlock(&d->pgraph.lock);

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

    qemu_mutex_lock(&d->pgraph.lock);

    switch (addr) {
    case NV_PGRAPH_INTR:
        d->pgraph.pending_interrupts &= ~val;
        qemu_cond_broadcast(&d->pgraph.interrupt_cond);
        break;
    case NV_PGRAPH_INTR_EN:
        d->pgraph.enabled_interrupts = val;
        break;
    case NV_PGRAPH_CTX_CONTROL:
        d->pgraph.channel_valid = (val & NV_PGRAPH_CTX_CONTROL_CHID);
        break;
    case NV_PGRAPH_CTX_USER:
        pgraph_set_context_user(d, val);
        break;
    case NV_PGRAPH_INCREMENT:
        if (val & NV_PGRAPH_INCREMENT_READ_3D) {
            SET_MASK(d->pgraph.regs[NV_PGRAPH_SURFACE],
                     NV_PGRAPH_SURFACE_READ_3D,
                     (GET_MASK(d->pgraph.regs[NV_PGRAPH_SURFACE],
                              NV_PGRAPH_SURFACE_READ_3D)+1)
                        % GET_MASK(d->pgraph.regs[NV_PGRAPH_SURFACE],
                                   NV_PGRAPH_SURFACE_MODULO_3D) );
            qemu_cond_broadcast(&d->pgraph.flip_3d);
        }
        break;
    case NV_PGRAPH_FIFO:
        d->pgraph.fifo_access = GET_MASK(val, NV_PGRAPH_FIFO_ACCESS);
        qemu_cond_broadcast(&d->pgraph.fifo_access_cond);
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

        if (val & NV_PGRAPH_CHANNEL_CTX_TRIGGER_READ_IN) {
            NV2A_DPRINTF("PGRAPH: read channel %d context from %" HWADDR_PRIx "\n",
                         d->pgraph.channel_id, d->pgraph.context_address);

            uint8_t *context_ptr = d->ramin_ptr + d->pgraph.context_address;
            uint32_t context_user = ldl_le_p((uint32_t*)context_ptr);

            NV2A_DPRINTF("    - CTX_USER = 0x%x\n", context_user);


            pgraph_set_context_user(d, context_user);
        }
        if (val & NV_PGRAPH_CHANNEL_CTX_TRIGGER_WRITE_OUT) {
            /* do stuff ... */
        }

        break;
    default:
        d->pgraph.regs[addr] = val;
        break;
    }

    qemu_mutex_unlock(&d->pgraph.lock);
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
        val &= 0x07FFFFFF;
        assert(val < memory_region_size(d->vram));
        d->pcrtc.start = val;

        NV2A_DPRINTF("PCRTC_START - %x %x %x %x\n",
                d->vram_ptr[val+64], d->vram_ptr[val+64+1],
                d->vram_ptr[val+64+2], d->vram_ptr[val+64+3]);
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

    NV2A_DPRINTF("PRAMDAC: read %d [0x%" HWADDR_PRIx "] -> %" HWADDR_PRIx "\n", size, addr, r);
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
    NV2A_DPRINTF("nv2a PRAMIN: read [0x%" HWADDR_PRIx "] -> 0x%" HWADDR_PRIx "\n", addr, r);
    return 0;
}
static void pramin_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRAMIN: [0x%" HWADDR_PRIx "] = 0x%02llx\n", addr, val);
}*/


/* USER - PFIFO MMIO and DMA submission area */
static uint64_t user_read(void *opaque,
                               hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    unsigned int channel_id = addr >> 16;
    assert(channel_id < NV2A_NUM_CHANNELS);

    ChannelControl *control = &d->user.channel_control[channel_id];

    uint32_t channel_modes = d->pfifo.regs[NV_PFIFO_MODE];

    uint64_t r = 0;
    if (channel_modes & (1 << channel_id)) {
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
        assert(false);
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

    uint32_t channel_modes = d->pfifo.regs[NV_PFIFO_MODE];
    if (channel_modes & (1 << channel_id)) {
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
        if (naddr < ARRAY_SIZE(nv2a_reg_names) && nv2a_reg_names[naddr]) {
            NV2A_DPRINTF("%s: read [%s] -> 0x%" PRIx64 "\n",
                    blocktable[block].name, nv2a_reg_names[naddr], val);
        } else {
            NV2A_DPRINTF("%s: read [%" HWADDR_PRIx "] -> 0x%" PRIx64 "\n",
                    blocktable[block].name, addr, val);
        }
    } else {
        NV2A_DPRINTF("(%d?): read [%" HWADDR_PRIx "] -> 0x%" PRIx64 "\n",
                block, addr, val);
    }
}

static void reg_log_write(int block, hwaddr addr, uint64_t val) {
    if (blocktable[block].name) {
        hwaddr naddr = blocktable[block].offset + addr;
        if (naddr < ARRAYSIZE(nv2a_reg_names) && nv2a_reg_names[naddr]) {
            NV2A_DPRINTF("%s: [%s] = 0x%" PRIx64 "\n",
                    blocktable[block].name, nv2a_reg_names[naddr], val);
        } else {
            NV2A_DPRINTF("%s: [%" HWADDR_PRIx "] = 0x%" PRIx64 "\n",
                    blocktable[block].name, addr, val);
        }
    } else {
        NV2A_DPRINTF("(%d?): [%" HWADDR_PRIx "] = 0x%" PRIx64 "\n",
                block, addr, val);
    }
}
static void pgraph_method_log(unsigned int subchannel,
                              unsigned int graphics_class,
                              unsigned int method, uint32_t parameter) {
    static unsigned int last = 0;
    static unsigned int count = 0;
    if (last == 0x1800 && method != last) {
        NV2A_GL_DPRINTF(true, "pgraph method (%d) 0x%x * %d",
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
        if (nmethod != 0 && nmethod < ARRAYSIZE(nv2a_method_names)) {
            method_name = nv2a_method_names[nmethod];
        }
        if (method_name) {
            NV2A_DPRINTF("pgraph method (%d): %s (0x%x)\n",
                         subchannel, method_name, parameter);
        } else {
            NV2A_DPRINTF("pgraph method (%d): 0x%x -> 0x%04x (0x%x)\n",
                         subchannel, graphics_class, method, parameter);
        }

    }
    if (method == last) { count++; }
    else {count = 0; }
    last = method;
}

static void nv2a_overlay_draw_line(VGACommonState *vga, uint8_t *line, int y)
{
    NV2A_DPRINTF("nv2a_overlay_draw_line\n");

    NV2AState *d = container_of(vga, NV2AState, vga);
    DisplaySurface *surface = qemu_console_surface(d->vga.con);

    int surf_bpp = surface_bytes_per_pixel(surface);
    int surf_width = surface_width(surface);

    if (!(d->pvideo.regs[NV_PVIDEO_BUFFER] & NV_PVIDEO_BUFFER_0_USE)) return;

    hwaddr base = d->pvideo.regs[NV_PVIDEO_BASE];
    hwaddr limit = d->pvideo.regs[NV_PVIDEO_LIMIT];
    hwaddr offset = d->pvideo.regs[NV_PVIDEO_OFFSET];

    int in_width = GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_IN],
                            NV_PVIDEO_SIZE_IN_WIDTH);
    int in_height = GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_IN],
                             NV_PVIDEO_SIZE_IN_HEIGHT);
    int in_s = GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_IN],
                        NV_PVIDEO_POINT_IN_S);
    int in_t = GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_IN],
                        NV_PVIDEO_POINT_IN_T);
    int in_pitch = GET_MASK(d->pvideo.regs[NV_PVIDEO_FORMAT],
                            NV_PVIDEO_FORMAT_PITCH);
    int in_color = GET_MASK(d->pvideo.regs[NV_PVIDEO_FORMAT],
                            NV_PVIDEO_FORMAT_COLOR);

    // TODO: support other color formats
    assert(in_color == NV_PVIDEO_FORMAT_COLOR_LE_CR8YB8CB8YA8);

    int out_width = GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_OUT],
                             NV_PVIDEO_SIZE_OUT_WIDTH);
    int out_height = GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_OUT],
                             NV_PVIDEO_SIZE_OUT_HEIGHT);
    int out_x = GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_OUT],
                         NV_PVIDEO_POINT_OUT_X);
    int out_y = GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_OUT],
                         NV_PVIDEO_POINT_OUT_Y);


    if (y < out_y || y >= out_y + out_height) return;

    // TODO: scaling, color keys

    int in_y = y - out_y;
    if (in_y >= in_height) return;

    assert(offset + in_pitch * (in_y + 1) <= limit);
    uint8_t *in_line = d->vram_ptr + base + offset + in_pitch * in_y;

    int x;
    for (x=0; x<out_width; x++) {
        int ox = out_x + x;
        if (ox >= surf_width) break;
        int ix = in_s + x;
        if (ix >= in_width) break;

        uint8_t r,g,b;
        convert_yuy2_to_rgb(in_line, ix, &r, &g, &b);

        unsigned int pixel = vga->rgb_to_pixel(r, g, b);
        switch (surf_bpp) {
        case 1:
            ((uint8_t*)line)[ox] = pixel;
            break;
        case 2:
            ((uint16_t*)line)[ox] = pixel;
            break;
        case 4:
            ((uint32_t*)line)[ox] = pixel;
            break;
        default:
            assert(false);
            break;
        }
    }
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

    memory_region_set_log(d->vram, true, DIRTY_MEMORY_NV2A);
    memory_region_set_dirty(d->vram, 0, memory_region_size(d->vram));

    /* hacky. swap out vga's vram */
    memory_region_destroy(&d->vga.vram);
    memory_region_init_alias(&d->vga.vram, OBJECT(d), "vga.vram",
                             d->vram, 0, memory_region_size(d->vram));
    d->vga.vram_ptr = memory_region_get_ram_ptr(&d->vga.vram);
    vga_dirty_log_start(&d->vga);


    pgraph_init(d);

    /* fire up puller */
    qemu_thread_create(&d->pfifo.puller_thread,
                       pfifo_puller_thread,
                       d, QEMU_THREAD_JOINABLE);
}

static int nv2a_initfn(PCIDevice *dev)
{
    int i;
    NV2AState *d;

    d = NV2A_DEVICE(dev);

    dev->config[PCI_INTERRUPT_PIN] = 0x01;

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
    vga->overlay_draw_line = nv2a_overlay_draw_line;

    d->hw_ops = *vga->hw_ops;
    d->hw_ops.gfx_update = nv2a_vga_gfx_update;
    vga->con = graphic_console_init(DEVICE(dev), &d->hw_ops, vga);


    /* mmio */
    memory_region_init(&d->mmio, OBJECT(dev), "nv2a-mmio", 0x1000000);
    pci_register_bar(&d->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);

    for (i=0; i<ARRAYSIZE(blocktable); i++) {
        if (!blocktable[i].name) continue;
        memory_region_init_io(&d->block_mmio[i], OBJECT(dev),
                              &blocktable[i].ops, d,
                              blocktable[i].name, blocktable[i].size);
        memory_region_add_subregion(&d->mmio, blocktable[i].offset,
                                    &d->block_mmio[i]);
    }

    /* init fifo cache1 */
    qemu_mutex_init(&d->pfifo.cache1.cache_lock);
    qemu_cond_init(&d->pfifo.cache1.cache_cond);
    QSIMPLEQ_INIT(&d->pfifo.cache1.cache);
    QSIMPLEQ_INIT(&d->pfifo.cache1.working_cache);

    return 0;
}

static void nv2a_exitfn(PCIDevice *dev)
{
    NV2AState *d;
    d = NV2A_DEVICE(dev);

    d->exiting = true;
    qemu_cond_signal(&d->pfifo.cache1.cache_cond);
    qemu_thread_join(&d->pfifo.puller_thread);

    qemu_mutex_destroy(&d->pfifo.cache1.cache_lock);
    qemu_cond_destroy(&d->pfifo.cache1.cache_cond);

    pgraph_destroy(&d->pgraph);
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

void nv2a_init(PCIBus *bus, int devfn, MemoryRegion *ram)
{
    PCIDevice *dev = pci_create_simple(bus, devfn, "nv2a");
    NV2AState *d = NV2A_DEVICE(dev);
    nv2a_init_memory(d, ram);
}
