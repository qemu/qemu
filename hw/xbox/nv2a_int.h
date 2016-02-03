/*
 * QEMU Geforce NV2A internal definitions
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
#   define NV062_SET_CONTEXT_DMA_IMAGE_SOURCE                 0x00000184
#   define NV062_SET_CONTEXT_DMA_IMAGE_DESTIN                 0x00000188
#   define NV062_SET_COLOR_FORMAT                             0x00000300
#       define NV062_SET_COLOR_FORMAT_LE_Y8                    0x01
#       define NV062_SET_COLOR_FORMAT_LE_A8R8G8B8              0x0A
#   define NV062_SET_PITCH                                    0x00000304
#   define NV062_SET_OFFSET_SOURCE                            0x00000308
#   define NV062_SET_OFFSET_DESTIN                            0x0000030C

#define NV_IMAGE_BLIT                                    0x009F
#   define NV09F_SET_CONTEXT_SURFACES                         0x0000019C
#   define NV09F_SET_OPERATION                                0x000002FC
#       define NV09F_SET_OPERATION_SRCCOPY                        3
#   define NV09F_CONTROL_POINT_IN                             0x00000300
#   define NV09F_CONTROL_POINT_OUT                            0x00000304
#   define NV09F_SIZE                                         0x00000308


#define NV_KELVIN_PRIMITIVE                              0x0097
#   define NV097_NO_OPERATION                                 0x00000100
#   define NV097_WAIT_FOR_IDLE                                0x00000110
#   define NV097_SET_FLIP_READ                                0x00000120
#   define NV097_SET_FLIP_WRITE                               0x00000124
#   define NV097_SET_FLIP_MODULO                              0x00000128
#   define NV097_FLIP_INCREMENT_WRITE                         0x0000012C
#   define NV097_FLIP_STALL                                   0x00000130
#   define NV097_SET_CONTEXT_DMA_NOTIFIES                     0x00000180
#   define NV097_SET_CONTEXT_DMA_A                            0x00000184
#   define NV097_SET_CONTEXT_DMA_B                            0x00000188
#   define NV097_SET_CONTEXT_DMA_STATE                        0x00000190
#   define NV097_SET_CONTEXT_DMA_COLOR                        0x00000194
#   define NV097_SET_CONTEXT_DMA_ZETA                         0x00000198
#   define NV097_SET_CONTEXT_DMA_VERTEX_A                     0x0000019C
#   define NV097_SET_CONTEXT_DMA_VERTEX_B                     0x000001A0
#   define NV097_SET_CONTEXT_DMA_SEMAPHORE                    0x000001A4
#   define NV097_SET_CONTEXT_DMA_REPORT                       0x000001A8
#   define NV097_SET_SURFACE_CLIP_HORIZONTAL                  0x00000200
#       define NV097_SET_SURFACE_CLIP_HORIZONTAL_X                0x0000FFFF
#       define NV097_SET_SURFACE_CLIP_HORIZONTAL_WIDTH            0xFFFF0000
#   define NV097_SET_SURFACE_CLIP_VERTICAL                    0x00000204
#       define NV097_SET_SURFACE_CLIP_VERTICAL_Y                  0x0000FFFF
#       define NV097_SET_SURFACE_CLIP_VERTICAL_HEIGHT             0xFFFF0000
#   define NV097_SET_SURFACE_FORMAT                           0x00000208
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
#   define NV097_SET_SURFACE_PITCH                            0x0000020C
#       define NV097_SET_SURFACE_PITCH_COLOR                      0x0000FFFF
#       define NV097_SET_SURFACE_PITCH_ZETA                       0xFFFF0000
#   define NV097_SET_SURFACE_COLOR_OFFSET                     0x00000210
#   define NV097_SET_SURFACE_ZETA_OFFSET                      0x00000214
#   define NV097_SET_COMBINER_ALPHA_ICW                       0x00000260
#   define NV097_SET_COMBINER_SPECULAR_FOG_CW0                0x00000288
#   define NV097_SET_COMBINER_SPECULAR_FOG_CW1                0x0000028C
#   define NV097_SET_CONTROL0                                 0x00000290
#       define NV097_SET_CONTROL0_STENCIL_WRITE_ENABLE            (1 << 0)
#       define NV097_SET_CONTROL0_Z_FORMAT                        (1 << 12)
#       define NV097_SET_CONTROL0_Z_PERSPECTIVE_ENABLE            (1 << 16)
#   define NV097_SET_FOG_MODE                                 0x0000029C
#       define NV097_SET_FOG_MODE_V_LINEAR                        0x2601
#       define NV097_SET_FOG_MODE_V_EXP                           0x800
#       define NV097_SET_FOG_MODE_V_EXP2                          0x801
#       define NV097_SET_FOG_MODE_V_EXP_ABS                       0x802
#       define NV097_SET_FOG_MODE_V_EXP2_ABS                      0x803
#       define NV097_SET_FOG_MODE_V_LINEAR_ABS                    0x804
#   define NV097_SET_FOG_GEN_MODE                             0x000002A0
#       define NV097_SET_FOG_GEN_MODE_V_SPEC_ALPHA                0
#       define NV097_SET_FOG_GEN_MODE_V_RADIAL                    1
#       define NV097_SET_FOG_GEN_MODE_V_PLANAR                    2
#       define NV097_SET_FOG_GEN_MODE_V_ABS_PLANAR                3
#       define NV097_SET_FOG_GEN_MODE_V_FOG_X                     6
#   define NV097_SET_FOG_ENABLE                               0x000002A4
#   define NV097_SET_FOG_COLOR                                0x000002A8
#       define NV097_SET_FOG_COLOR_RED                            0x000000FF
#       define NV097_SET_FOG_COLOR_GREEN                          0x0000FF00
#       define NV097_SET_FOG_COLOR_BLUE                           0x00FF0000
#       define NV097_SET_FOG_COLOR_ALPHA                          0xFF000000
#   define NV097_SET_ALPHA_TEST_ENABLE                        0x00000300
#   define NV097_SET_BLEND_ENABLE                             0x00000304
#   define NV097_SET_CULL_FACE_ENABLE                         0x00000308
#   define NV097_SET_DEPTH_TEST_ENABLE                        0x0000030C
#   define NV097_SET_DITHER_ENABLE                            0x00000310
#   define NV097_SET_LIGHTING_ENABLE                          0x00000314
#   define NV097_SET_SKIN_MODE                                0x00000328
#       define NV097_SET_SKIN_MODE_OFF                            0
#       define NV097_SET_SKIN_MODE_2G                             1
#       define NV097_SET_SKIN_MODE_2                              2
#       define NV097_SET_SKIN_MODE_3G                             3
#       define NV097_SET_SKIN_MODE_3                              4
#       define NV097_SET_SKIN_MODE_4G                             5
#       define NV097_SET_SKIN_MODE_4                              6
#   define NV097_SET_STENCIL_TEST_ENABLE                      0x0000032C
#   define NV097_SET_POLY_OFFSET_POINT_ENABLE                 0x00000330
#   define NV097_SET_POLY_OFFSET_LINE_ENABLE                  0x00000334
#   define NV097_SET_POLY_OFFSET_FILL_ENABLE                  0x00000338
#   define NV097_SET_ALPHA_FUNC                               0x0000033C
#   define NV097_SET_ALPHA_REF                                0x00000340
#   define NV097_SET_BLEND_FUNC_SFACTOR                       0x00000344
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
#   define NV097_SET_BLEND_FUNC_DFACTOR                       0x00000348
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
#   define NV097_SET_BLEND_COLOR                              0x0000034C
#   define NV097_SET_BLEND_EQUATION                           0x00000350
#       define NV097_SET_BLEND_EQUATION_V_FUNC_SUBTRACT           0x800A
#       define NV097_SET_BLEND_EQUATION_V_FUNC_REVERSE_SUBTRACT   0x800B
#       define NV097_SET_BLEND_EQUATION_V_FUNC_ADD                0x8006
#       define NV097_SET_BLEND_EQUATION_V_MIN                     0x8007
#       define NV097_SET_BLEND_EQUATION_V_MAX                     0x8008
#       define NV097_SET_BLEND_EQUATION_V_FUNC_REVERSE_SUBTRACT_SIGNED 0xF005
#       define NV097_SET_BLEND_EQUATION_V_FUNC_ADD_SIGNED         0xF006
#   define NV097_SET_DEPTH_FUNC                               0x00000354
#   define NV097_SET_COLOR_MASK                               0x00000358
#       define NV097_SET_COLOR_MASK_BLUE_WRITE_ENABLE             (1 << 0)
#       define NV097_SET_COLOR_MASK_GREEN_WRITE_ENABLE            (1 << 8)
#       define NV097_SET_COLOR_MASK_RED_WRITE_ENABLE              (1 << 16)
#       define NV097_SET_COLOR_MASK_ALPHA_WRITE_ENABLE            (1 << 24)
#   define NV097_SET_DEPTH_MASK                               0x0000035C
#   define NV097_SET_STENCIL_MASK                             0x00000360
#   define NV097_SET_STENCIL_FUNC                             0x00000364
#   define NV097_SET_STENCIL_FUNC_REF                         0x00000368
#   define NV097_SET_STENCIL_FUNC_MASK                        0x0000036C
#   define NV097_SET_STENCIL_OP_FAIL                          0x00000370
#   define NV097_SET_STENCIL_OP_ZFAIL                         0x00000374
#   define NV097_SET_STENCIL_OP_ZPASS                         0x00000378
#       define NV097_SET_STENCIL_OP_V_KEEP                        0x1E00
#       define NV097_SET_STENCIL_OP_V_ZERO                        0x0000
#       define NV097_SET_STENCIL_OP_V_REPLACE                     0x1E01
#       define NV097_SET_STENCIL_OP_V_INCRSAT                     0x1E02
#       define NV097_SET_STENCIL_OP_V_DECRSAT                     0x1E03
#       define NV097_SET_STENCIL_OP_V_INVERT                      0x150A
#       define NV097_SET_STENCIL_OP_V_INCR                        0x8507
#       define NV097_SET_STENCIL_OP_V_DECR                        0x8508
#   define NV097_SET_POLYGON_OFFSET_SCALE_FACTOR              0x00000384
#   define NV097_SET_POLYGON_OFFSET_BIAS                      0x00000388
#   define NV097_SET_FRONT_POLYGON_MODE                       0x0000038C
#       define NV097_SET_FRONT_POLYGON_MODE_V_POINT               0x1B00
#       define NV097_SET_FRONT_POLYGON_MODE_V_LINE                0x1B01
#       define NV097_SET_FRONT_POLYGON_MODE_V_FILL                0x1B02
#   define NV097_SET_BACK_POLYGON_MODE                        0x00000390
#   define NV097_SET_CLIP_MIN                                 0x00000394
#   define NV097_SET_CLIP_MAX                                 0x00000398
#   define NV097_SET_CULL_FACE                                0x0000039C
#       define NV097_SET_CULL_FACE_V_FRONT                         0x404
#       define NV097_SET_CULL_FACE_V_BACK                          0x405
#       define NV097_SET_CULL_FACE_V_FRONT_AND_BACK                0x408
#   define NV097_SET_FRONT_FACE                               0x000003A0
#       define NV097_SET_FRONT_FACE_V_CW                           0x900
#       define NV097_SET_FRONT_FACE_V_CCW                          0x901
#   define NV097_SET_NORMALIZATION_ENABLE                     0x000003A4
#   define NV097_SET_LIGHT_ENABLE_MASK                        0x000003BC
#           define NV097_SET_LIGHT_ENABLE_MASK_LIGHT0_OFF           0
#           define NV097_SET_LIGHT_ENABLE_MASK_LIGHT0_INFINITE      1
#           define NV097_SET_LIGHT_ENABLE_MASK_LIGHT0_LOCAL         2
#           define NV097_SET_LIGHT_ENABLE_MASK_LIGHT0_SPOT          3
#   define NV097_SET_TEXGEN_S                                 0x000003C0
#       define NV097_SET_TEXGEN_S_DISABLE                         0x0000
#       define NV097_SET_TEXGEN_S_EYE_LINEAR                      0x2400
#       define NV097_SET_TEXGEN_S_OBJECT_LINEAR                   0x2401
#       define NV097_SET_TEXGEN_S_SPHERE_MAP                      0x2402
#       define NV097_SET_TEXGEN_S_REFLECTION_MAP                  0x8512
#       define NV097_SET_TEXGEN_S_NORMAL_MAP                      0x8511
#   define NV097_SET_TEXGEN_T                                 0x000003C4
#   define NV097_SET_TEXGEN_R                                 0x000003C8
#   define NV097_SET_TEXGEN_Q                                 0x000003CC
#   define NV097_SET_TEXTURE_MATRIX_ENABLE                    0x00000420
#   define NV097_SET_PROJECTION_MATRIX                        0x00000440
#   define NV097_SET_MODEL_VIEW_MATRIX                        0x00000480
#   define NV097_SET_INVERSE_MODEL_VIEW_MATRIX                0x00000580
#   define NV097_SET_COMPOSITE_MATRIX                         0x00000680
#   define NV097_SET_TEXTURE_MATRIX                           0x000006C0
#   define NV097_SET_FOG_PARAMS                               0x000009C0
#   define NV097_SET_TEXGEN_PLANE_S                           0x00000840
#   define NV097_SET_TEXGEN_PLANE_T                           0x00000850
#   define NV097_SET_TEXGEN_PLANE_R                           0x00000860
#   define NV097_SET_TEXGEN_PLANE_Q                           0x00000870
#   define NV097_SET_TEXGEN_VIEW_MODEL                        0x000009CC
#       define NV097_SET_TEXGEN_VIEW_MODEL_LOCAL_VIEWER           0
#       define NV097_SET_TEXGEN_VIEW_MODEL_INFINITE_VIEWER        1
#   define NV097_SET_FOG_PLANE                                0x000009D0
#   define NV097_SET_SCENE_AMBIENT_COLOR                      0x00000A10
#   define NV097_SET_VIEWPORT_OFFSET                          0x00000A20
#   define NV097_SET_EYE_POSITION                             0x00000A50
#   define NV097_SET_COMBINER_FACTOR0                         0x00000A60
#   define NV097_SET_COMBINER_FACTOR1                         0x00000A80
#   define NV097_SET_COMBINER_ALPHA_OCW                       0x00000AA0
#   define NV097_SET_COMBINER_COLOR_ICW                       0x00000AC0
#   define NV097_SET_VIEWPORT_SCALE                           0x00000AF0
#   define NV097_SET_TRANSFORM_PROGRAM                        0x00000B00
#   define NV097_SET_TRANSFORM_CONSTANT                       0x00000B80
#   define NV097_SET_VERTEX3F                                 0x00001500
#   define NV097_SET_BACK_LIGHT_AMBIENT_COLOR                 0x00000C00
#   define NV097_SET_BACK_LIGHT_DIFFUSE_COLOR                 0x00000C0C
#   define NV097_SET_BACK_LIGHT_SPECULAR_COLOR                0x00000C18
#   define NV097_SET_LIGHT_AMBIENT_COLOR                      0x00001000
#   define NV097_SET_LIGHT_DIFFUSE_COLOR                      0x0000100C
#   define NV097_SET_LIGHT_SPECULAR_COLOR                     0x00001018
#   define NV097_SET_LIGHT_LOCAL_RANGE                        0x00001024
#   define NV097_SET_LIGHT_INFINITE_HALF_VECTOR               0x00001028
#   define NV097_SET_LIGHT_INFINITE_DIRECTION                 0x00001034
#   define NV097_SET_LIGHT_SPOT_FALLOFF                       0x00001040
#   define NV097_SET_LIGHT_SPOT_DIRECTION                     0x0000104C
#   define NV097_SET_LIGHT_LOCAL_POSITION                     0x0000105C
#   define NV097_SET_LIGHT_LOCAL_ATTENUATION                  0x00001068
#   define NV097_SET_VERTEX4F                                 0x00001518
#   define NV097_SET_VERTEX_DATA_ARRAY_OFFSET                 0x00001720
#   define NV097_SET_VERTEX_DATA_ARRAY_FORMAT                 0x00001760
#       define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE            0x0000000F
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D     0
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1         1
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F          2
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL     4
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K       5
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP        6
#       define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE            0x000000F0
#       define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE          0xFFFFFF00
#   define NV097_SET_LOGIC_OP_ENABLE                          0x000017BC
#   define NV097_SET_LOGIC_OP                                 0x000017C0
#   define NV097_CLEAR_REPORT_VALUE                           0x000017C8
#       define NV097_CLEAR_REPORT_VALUE_TYPE                      0xFFFFFFFF
#           define NV097_CLEAR_REPORT_VALUE_TYPE_ZPASS_PIXEL_CNT      1
#   define NV097_SET_ZPASS_PIXEL_COUNT_ENABLE                 0x000017CC
#   define NV097_GET_REPORT                                   0x000017D0
#       define NV097_GET_REPORT_OFFSET                            0x00FFFFFF
#       define NV097_GET_REPORT_TYPE                              0xFF000000
#           define NV097_GET_REPORT_TYPE_ZPASS_PIXEL_CNT              1
#   define NV097_SET_EYE_DIRECTION                            0x000017E0
#   define NV097_SET_SHADER_CLIP_PLANE_MODE                   0x000017F8
#   define NV097_SET_BEGIN_END                                0x000017FC
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
#   define NV097_ARRAY_ELEMENT16                              0x00001800
#   define NV097_ARRAY_ELEMENT32                              0x00001808
#   define NV097_DRAW_ARRAYS                                  0x00001810
#       define NV097_DRAW_ARRAYS_COUNT                            0xFF000000
#       define NV097_DRAW_ARRAYS_START_INDEX                      0x00FFFFFF
#   define NV097_INLINE_ARRAY                                 0x00001818
#   define NV097_SET_EYE_VECTOR                               0x0000181C
#   define NV097_SET_VERTEX_DATA2F_M                          0x00001880
#   define NV097_SET_VERTEX_DATA4F_M                          0x00001A00
#   define NV097_SET_VERTEX_DATA2S                            0x00001900
#   define NV097_SET_VERTEX_DATA4UB                           0x00001940
#   define NV097_SET_VERTEX_DATA4S_M                          0x00001980
#   define NV097_SET_TEXTURE_OFFSET                           0x00001B00
#   define NV097_SET_TEXTURE_FORMAT                           0x00001B04
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
#   define NV097_SET_TEXTURE_ADDRESS                          0x00001B08
#   define NV097_SET_TEXTURE_CONTROL0                         0x00001B0C
#       define NV097_SET_TEXTURE_CONTROL0_ENABLE                 (1 << 30)
#       define NV097_SET_TEXTURE_CONTROL0_MIN_LOD_CLAMP           0x3FFC0000
#       define NV097_SET_TEXTURE_CONTROL0_MAX_LOD_CLAMP           0x0003FFC0
#   define NV097_SET_TEXTURE_CONTROL1                         0x00001B10
#       define NV097_SET_TEXTURE_CONTROL1_IMAGE_PITCH             0xFFFF0000
#   define NV097_SET_TEXTURE_FILTER                           0x00001B14
#       define NV097_SET_TEXTURE_FILTER_MIPMAP_LOD_BIAS           0x00001FFF
#       define NV097_SET_TEXTURE_FILTER_MIN                       0x00FF0000
#       define NV097_SET_TEXTURE_FILTER_MAG                       0x0F000000
#       define NV097_SET_TEXTURE_FILTER_ASIGNED                   (1 << 28)
#       define NV097_SET_TEXTURE_FILTER_RSIGNED                   (1 << 29)
#       define NV097_SET_TEXTURE_FILTER_GSIGNED                   (1 << 30)
#       define NV097_SET_TEXTURE_FILTER_BSIGNED                   (1 << 31)
#   define NV097_SET_TEXTURE_IMAGE_RECT                       0x00001B1C
#       define NV097_SET_TEXTURE_IMAGE_RECT_WIDTH                 0xFFFF0000
#       define NV097_SET_TEXTURE_IMAGE_RECT_HEIGHT                0x0000FFFF
#   define NV097_SET_TEXTURE_PALETTE                          0x00001B20
#       define NV097_SET_TEXTURE_PALETTE_CONTEXT_DMA              (1 << 0)
#       define NV097_SET_TEXTURE_PALETTE_LENGTH                   0x0000000C
#         define NV097_SET_TEXTURE_PALETTE_LENGTH_256               0
#         define NV097_SET_TEXTURE_PALETTE_LENGTH_128               1
#         define NV097_SET_TEXTURE_PALETTE_LENGTH_64                2
#         define NV097_SET_TEXTURE_PALETTE_LENGTH_32                3
#       define NV097_SET_TEXTURE_PALETTE_OFFSET                   0xFFFFFFC0
#   define NV097_SET_TEXTURE_BORDER_COLOR                     0x00001B24
#   define NV097_SET_TEXTURE_SET_BUMP_ENV_MAT                 0x00001B28
#   define NV097_SET_TEXTURE_SET_BUMP_ENV_SCALE               0x00001B38
#   define NV097_SET_TEXTURE_SET_BUMP_ENV_OFFSET              0x00001B3C
#   define NV097_SET_SEMAPHORE_OFFSET                         0x00001D6C
#   define NV097_BACK_END_WRITE_SEMAPHORE_RELEASE             0x00001D70
#   define NV097_SET_ZSTENCIL_CLEAR_VALUE                     0x00001D8C
#   define NV097_SET_COLOR_CLEAR_VALUE                        0x00001D90
#   define NV097_CLEAR_SURFACE                                0x00001D94
#       define NV097_CLEAR_SURFACE_Z                              (1 << 0)
#       define NV097_CLEAR_SURFACE_STENCIL                        (1 << 1)
#       define NV097_CLEAR_SURFACE_COLOR                          0x000000F0
#       define NV097_CLEAR_SURFACE_R                                (1 << 4)
#       define NV097_CLEAR_SURFACE_G                                (1 << 5)
#       define NV097_CLEAR_SURFACE_B                                (1 << 6)
#       define NV097_CLEAR_SURFACE_A                                (1 << 7)
#   define NV097_SET_CLEAR_RECT_HORIZONTAL                    0x00001D98
#   define NV097_SET_CLEAR_RECT_VERTICAL                      0x00001D9C
#   define NV097_SET_SPECULAR_FOG_FACTOR                      0x00001E20
#   define NV097_SET_COMBINER_COLOR_OCW                       0x00001E40
#   define NV097_SET_COMBINER_CONTROL                         0x00001E60
#   define NV097_SET_SHADOW_ZSLOPE_THRESHOLD                  0x00001E68
#   define NV097_SET_SHADER_STAGE_PROGRAM                     0x00001E70
#   define NV097_SET_SHADER_OTHER_STAGE_INPUT                 0x00001E78
#   define NV097_SET_TRANSFORM_EXECUTION_MODE                 0x00001E94
#       define NV097_SET_TRANSFORM_EXECUTION_MODE_MODE            0x00000003
#       define NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE      0xFFFFFFFC
#   define NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN           0x00001E98
#   define NV097_SET_TRANSFORM_PROGRAM_LOAD                   0x00001E9C
#   define NV097_SET_TRANSFORM_PROGRAM_START                  0x00001EA0
#   define NV097_SET_TRANSFORM_CONSTANT_LOAD                  0x00001EA4

/* vertex processing (cheops) context layout */
#define NV_IGRAPH_XF_XFCTX_CMAT0                     0x00
#define NV_IGRAPH_XF_XFCTX_PMAT0                     0x04
#define NV_IGRAPH_XF_XFCTX_MMAT0                     0x08
#define NV_IGRAPH_XF_XFCTX_IMMAT0                    0x0c
#define NV_IGRAPH_XF_XFCTX_MMAT1                     0x10
#define NV_IGRAPH_XF_XFCTX_IMMAT1                    0x14
#define NV_IGRAPH_XF_XFCTX_MMAT2                     0x18
#define NV_IGRAPH_XF_XFCTX_IMMAT2                    0x1c
#define NV_IGRAPH_XF_XFCTX_MMAT3                     0x20
#define NV_IGRAPH_XF_XFCTX_IMMAT3                    0x24
#define NV_IGRAPH_XF_XFCTX_LIT0                      0x28
#define NV_IGRAPH_XF_XFCTX_LIT1                      0x29
#define NV_IGRAPH_XF_XFCTX_LIT2                      0x2a
#define NV_IGRAPH_XF_XFCTX_LIT3                      0x2b
#define NV_IGRAPH_XF_XFCTX_LIT4                      0x2c
#define NV_IGRAPH_XF_XFCTX_LIT5                      0x2d
#define NV_IGRAPH_XF_XFCTX_LIT6                      0x2e
#define NV_IGRAPH_XF_XFCTX_LIT7                      0x2f
#define NV_IGRAPH_XF_XFCTX_SPOT0                     0x30
#define NV_IGRAPH_XF_XFCTX_SPOT1                     0x31
#define NV_IGRAPH_XF_XFCTX_SPOT2                     0x32
#define NV_IGRAPH_XF_XFCTX_SPOT3                     0x33
#define NV_IGRAPH_XF_XFCTX_SPOT4                     0x34
#define NV_IGRAPH_XF_XFCTX_SPOT5                     0x35
#define NV_IGRAPH_XF_XFCTX_SPOT6                     0x36
#define NV_IGRAPH_XF_XFCTX_SPOT7                     0x37
#define NV_IGRAPH_XF_XFCTX_EYEP                      0x38
#define NV_IGRAPH_XF_XFCTX_FOG                       0x39
#define NV_IGRAPH_XF_XFCTX_VPSCL                     0x3a
#define NV_IGRAPH_XF_XFCTX_VPOFF                     0x3b
#define NV_IGRAPH_XF_XFCTX_CONS0                     0x3c
#define NV_IGRAPH_XF_XFCTX_CONS1                     0x3d
#define NV_IGRAPH_XF_XFCTX_CONS2                     0x3e
#define NV_IGRAPH_XF_XFCTX_CONS3                     0x3f
#define NV_IGRAPH_XF_XFCTX_TG0MAT                    0x40
#define NV_IGRAPH_XF_XFCTX_T0MAT                     0x44
#define NV_IGRAPH_XF_XFCTX_TG1MAT                    0x48
#define NV_IGRAPH_XF_XFCTX_T1MAT                     0x4c
#define NV_IGRAPH_XF_XFCTX_TG2MAT                    0x50
#define NV_IGRAPH_XF_XFCTX_T2MAT                     0x54
#define NV_IGRAPH_XF_XFCTX_TG3MAT                    0x58
#define NV_IGRAPH_XF_XFCTX_T3MAT                     0x5c
#define NV_IGRAPH_XF_XFCTX_PRSPACE                   0x60

/* lighting (zoser) context layout */
#define NV_IGRAPH_XF_LTCTXA_L0_K                     0x00
#define NV_IGRAPH_XF_LTCTXA_L0_SPT                   0x01
#define NV_IGRAPH_XF_LTCTXA_L1_K                     0x02
#define NV_IGRAPH_XF_LTCTXA_L1_SPT                   0x03
#define NV_IGRAPH_XF_LTCTXA_L2_K                     0x04
#define NV_IGRAPH_XF_LTCTXA_L2_SPT                   0x05
#define NV_IGRAPH_XF_LTCTXA_L3_K                     0x06
#define NV_IGRAPH_XF_LTCTXA_L3_SPT                   0x07
#define NV_IGRAPH_XF_LTCTXA_L4_K                     0x08
#define NV_IGRAPH_XF_LTCTXA_L4_SPT                   0x09
#define NV_IGRAPH_XF_LTCTXA_L5_K                     0x0a
#define NV_IGRAPH_XF_LTCTXA_L5_SPT                   0x0b
#define NV_IGRAPH_XF_LTCTXA_L6_K                     0x0c
#define NV_IGRAPH_XF_LTCTXA_L6_SPT                   0x0d
#define NV_IGRAPH_XF_LTCTXA_L7_K                     0x0e
#define NV_IGRAPH_XF_LTCTXA_L7_SPT                   0x0f
#define NV_IGRAPH_XF_LTCTXA_EYED                     0x10
#define NV_IGRAPH_XF_LTCTXA_FR_AMB                   0x11
#define NV_IGRAPH_XF_LTCTXA_BR_AMB                   0x12
#define NV_IGRAPH_XF_LTCTXA_CM_COL                   0x13
#define NV_IGRAPH_XF_LTCTXA_BCM_COL                  0x14
#define NV_IGRAPH_XF_LTCTXA_FOG_K                    0x15
#define NV_IGRAPH_XF_LTCTXA_ZERO                     0x16
#define NV_IGRAPH_XF_LTCTXA_PT0                      0x17
#define NV_IGRAPH_XF_LTCTXA_FOGLIN                   0x18

#define NV_IGRAPH_XF_LTCTXB_L0_AMB                   0x00
#define NV_IGRAPH_XF_LTCTXB_L0_DIF                   0x01
#define NV_IGRAPH_XF_LTCTXB_L0_SPC                   0x02
#define NV_IGRAPH_XF_LTCTXB_L0_BAMB                  0x03
#define NV_IGRAPH_XF_LTCTXB_L0_BDIF                  0x04
#define NV_IGRAPH_XF_LTCTXB_L0_BSPC                  0x05
#define NV_IGRAPH_XF_LTCTXB_L1_AMB                   0x06
#define NV_IGRAPH_XF_LTCTXB_L1_DIF                   0x07
#define NV_IGRAPH_XF_LTCTXB_L1_SPC                   0x08
#define NV_IGRAPH_XF_LTCTXB_L1_BAMB                  0x09
#define NV_IGRAPH_XF_LTCTXB_L1_BDIF                  0x0a
#define NV_IGRAPH_XF_LTCTXB_L1_BSPC                  0x0b
#define NV_IGRAPH_XF_LTCTXB_L2_AMB                   0x0c
#define NV_IGRAPH_XF_LTCTXB_L2_DIF                   0x0d
#define NV_IGRAPH_XF_LTCTXB_L2_SPC                   0x0e
#define NV_IGRAPH_XF_LTCTXB_L2_BAMB                  0x0f
#define NV_IGRAPH_XF_LTCTXB_L2_BDIF                  0x10
#define NV_IGRAPH_XF_LTCTXB_L2_BSPC                  0x11
#define NV_IGRAPH_XF_LTCTXB_L3_AMB                   0x12
#define NV_IGRAPH_XF_LTCTXB_L3_DIF                   0x13
#define NV_IGRAPH_XF_LTCTXB_L3_SPC                   0x14
#define NV_IGRAPH_XF_LTCTXB_L3_BAMB                  0x15
#define NV_IGRAPH_XF_LTCTXB_L3_BDIF                  0x16
#define NV_IGRAPH_XF_LTCTXB_L3_BSPC                  0x17
#define NV_IGRAPH_XF_LTCTXB_L4_AMB                   0x18
#define NV_IGRAPH_XF_LTCTXB_L4_DIF                   0x19
#define NV_IGRAPH_XF_LTCTXB_L4_SPC                   0x1a
#define NV_IGRAPH_XF_LTCTXB_L4_BAMB                  0x1b
#define NV_IGRAPH_XF_LTCTXB_L4_BDIF                  0x1c
#define NV_IGRAPH_XF_LTCTXB_L4_BSPC                  0x1d
#define NV_IGRAPH_XF_LTCTXB_L5_AMB                   0x1e
#define NV_IGRAPH_XF_LTCTXB_L5_DIF                   0x1f
#define NV_IGRAPH_XF_LTCTXB_L5_SPC                   0x20
#define NV_IGRAPH_XF_LTCTXB_L5_BAMB                  0x21
#define NV_IGRAPH_XF_LTCTXB_L5_BDIF                  0x22
#define NV_IGRAPH_XF_LTCTXB_L5_BSPC                  0x23
#define NV_IGRAPH_XF_LTCTXB_L6_AMB                   0x24
#define NV_IGRAPH_XF_LTCTXB_L6_DIF                   0x25
#define NV_IGRAPH_XF_LTCTXB_L6_SPC                   0x26
#define NV_IGRAPH_XF_LTCTXB_L6_BAMB                  0x27
#define NV_IGRAPH_XF_LTCTXB_L6_BDIF                  0x28
#define NV_IGRAPH_XF_LTCTXB_L6_BSPC                  0x29
#define NV_IGRAPH_XF_LTCTXB_L7_AMB                   0x2a
#define NV_IGRAPH_XF_LTCTXB_L7_DIF                   0x2b
#define NV_IGRAPH_XF_LTCTXB_L7_SPC                   0x2c
#define NV_IGRAPH_XF_LTCTXB_L7_BAMB                  0x2d
#define NV_IGRAPH_XF_LTCTXB_L7_BDIF                  0x2e
#define NV_IGRAPH_XF_LTCTXB_L7_BSPC                  0x2f
#define NV_IGRAPH_XF_LTCTXB_PT1                      0x30
#define NV_IGRAPH_XF_LTCTXB_ONE                      0x31
#define NV_IGRAPH_XF_LTCTXB_VPOFFSET                 0x32

#define NV_IGRAPH_XF_LTC1_ZERO1                      0x00
#define NV_IGRAPH_XF_LTC1_l0                         0x01
#define NV_IGRAPH_XF_LTC1_Bl0                        0x02
#define NV_IGRAPH_XF_LTC1_PP                         0x03
#define NV_IGRAPH_XF_LTC1_r0                         0x04
#define NV_IGRAPH_XF_LTC1_r1                         0x05
#define NV_IGRAPH_XF_LTC1_r2                         0x06
#define NV_IGRAPH_XF_LTC1_r3                         0x07
#define NV_IGRAPH_XF_LTC1_r4                         0x08
#define NV_IGRAPH_XF_LTC1_r5                         0x09
#define NV_IGRAPH_XF_LTC1_r6                         0x0a
#define NV_IGRAPH_XF_LTC1_r7                         0x0b
#define NV_IGRAPH_XF_LTC1_L0                         0x0c
#define NV_IGRAPH_XF_LTC1_L1                         0x0d
#define NV_IGRAPH_XF_LTC1_L2                         0x0e
#define NV_IGRAPH_XF_LTC1_L3                         0x0f
#define NV_IGRAPH_XF_LTC1_L4                         0x10
#define NV_IGRAPH_XF_LTC1_L5                         0x11
#define NV_IGRAPH_XF_LTC1_L6                         0x12
#define NV_IGRAPH_XF_LTC1_L7                         0x13


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

#define NV2A_MAX_TRANSFORM_PROGRAM_LENGTH 136
#define NV2A_VERTEXSHADER_CONSTANTS 192
#define NV2A_MAX_LIGHTS 8

#define NV2A_LTCTXA_COUNT  26
#define NV2A_LTCTXB_COUNT  52
#define NV2A_LTC1_COUNT    20
