// #################################################################################################
// # << NEORV32: neorv32_sysinfo.h - System Information Memory (SYSINFO) HW Driver >>              #
// # ********************************************************************************************* #
// # BSD 3-Clause License                                                                          #
// #                                                                                               #
// # Copyright (c) 2023, Stephan Nolting. All rights reserved.                                     #
// #                                                                                               #
// # Redistribution and use in source and binary forms, with or without modification, are          #
// # permitted provided that the following conditions are met:                                     #
// #                                                                                               #
// # 1. Redistributions of source code must retain the above copyright notice, this list of        #
// #    conditions and the following disclaimer.                                                   #
// #                                                                                               #
// # 2. Redistributions in binary form must reproduce the above copyright notice, this list of     #
// #    conditions and the following disclaimer in the documentation and/or other materials        #
// #    provided with the distribution.                                                            #
// #                                                                                               #
// # 3. Neither the name of the copyright holder nor the names of its contributors may be used to  #
// #    endorse or promote products derived from this software without specific prior written      #
// #    permission.                                                                                #
// #                                                                                               #
// # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS   #
// # OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF               #
// # MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE    #
// # COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,     #
// # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE #
// # GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED    #
// # AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING     #
// # NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED  #
// # OF THE POSSIBILITY OF SUCH DAMAGE.                                                            #
// # ********************************************************************************************* #
// # The NEORV32 Processor - https://github.com/stnolting/neorv32              (c) Stephan Nolting #
// #################################################################################################


/**********************************************************************//**
 * @file neorv32_cfs.h
 * @brief System Configuration Information Memory (SYSINFO) HW driver header file.
 **************************************************************************/

#ifndef neorv32_sysinfo_h
#define neorv32_sysinfo_h

/**********************************************************************//**
 * @name IO Device: System Configuration Information Memory (SYSINFO)
 **************************************************************************/
/**@{*/
/** SYSINFO module prototype - whole module is read-only */
typedef volatile struct __attribute__((packed,aligned(4))) {
        uint32_t CLK;   /**< offset 0:  Clock speed in Hz */
  const uint32_t MISC;  /**< offset 4:  Miscellaneous system configurations (#NEORV32_SYSINFO_MISC_enum) */
  const uint32_t SOC;   /**< offset 8:  SoC features (#NEORV32_SYSINFO_SOC_enum) */
  const uint32_t CACHE; /**< offset 12: Cache configuration (#NEORV32_SYSINFO_CACHE_enum) */
} neorv32_sysinfo_t;

/** SYSINFO module hardware access (#neorv32_sysinfo_t) */
#define NEORV32_SYSINFO ((neorv32_sysinfo_t*) (NEORV32_SYSINFO_BASE))

/** NEORV32_SYSINFO.MISC (r/-): Miscellaneous system configurations */
enum NEORV32_SYSINFO_MISC_enum {
  SYSINFO_MISC_IMEM_LSB =  0, /**< SYSINFO_MISC  (0) (r/-): log2(internal IMEM size in bytes) (via IMEM_SIZE generic), LSB */
  SYSINFO_MISC_IMEM_MBS =  7, /**< SYSINFO_MISC  (7) (r/-): log2(internal IMEM size in bytes) (via IMEM_SIZE generic), MSB */

  SYSINFO_MISC_DMEM_LSB =  8, /**< SYSINFO_MISC  (8) (r/-): log2(internal DMEM size in bytes) (via DMEM_SIZE generic), LSB */
  SYSINFO_MISC_DMEM_MSB = 15, /**< SYSINFO_MISC (15) (r/-): log2(internal DMEM size in bytes) (via DMEM_SIZE generic), MSB */

  SYSINFO_MISC_HART_LSB = 16, /**< SYSINFO_MISC (16) (r/-): number of physical CPU cores ("harts"), LSB */
  SYSINFO_MISC_HART_MSB = 19, /**< SYSINFO_MISC (19) (r/-): number of physical CPU cores ("harts"), MSB */

  SYSINFO_MISC_BOOT_LSB = 20, /**< SYSINFO_MISC (20) (r/-): boot mode configuration (via BOOT_MODE_SELECT generic), LSB */
  SYSINFO_MISC_BOOT_MSB = 21, /**< SYSINFO_MISC (21) (r/-): boot mode configuration (via BOOT_MODE_SELECT generic), MSB */

  SYSINFO_MISC_ITMO_LSB = 22, /**< SYSINFO_MISC (22) (r/-): log2(internal bus timeout cycles), LSB */
  SYSINFO_MISC_ITMO_MSB = 26, /**< SYSINFO_MISC (26) (r/-): log2(internal bus timeout cycles), MSB */

  SYSINFO_MISC_ETMO_LSB = 27, /**< SYSINFO_MISC (27) (r/-): log2(external bus timeout cycles), LSB */
  SYSINFO_MISC_ETMO_MSB = 31  /**< SYSINFO_MISC (31) (r/-): log2(external bus timeout cycles), MSB */
};

/** NEORV32_SYSINFO.SOC (r/-): Implemented processor devices/features */
enum NEORV32_SYSINFO_SOC_enum {
  SYSINFO_SOC_BOOTLOADER =  0, /**< SYSINFO_SOC  (0) (r/-): Bootloader implemented when 1 (via BOOT_MODE_SELECT generic) */
  SYSINFO_SOC_XBUS       =  1, /**< SYSINFO_SOC  (1) (r/-): External bus interface implemented when 1 (via XBUS_EN generic) */
  SYSINFO_SOC_IMEM       =  2, /**< SYSINFO_SOC  (2) (r/-): Processor-internal instruction memory implemented when 1 (via IMEM_EN generic) */
  SYSINFO_SOC_DMEM       =  3, /**< SYSINFO_SOC  (3) (r/-): Processor-internal data memory implemented when 1 (via DMEM_EN generic) */
  SYSINFO_SOC_OCD        =  4, /**< SYSINFO_SOC  (4) (r/-): On-chip debugger implemented when 1 (via OCD_EN generic) */
  SYSINFO_SOC_ICACHE     =  5, /**< SYSINFO_SOC  (5) (r/-): Processor-internal instruction cache implemented when 1 (via ICACHE_EN generic) */
  SYSINFO_SOC_DCACHE     =  6, /**< SYSINFO_SOC  (6) (r/-): Processor-internal instruction cache implemented when 1 (via DCACHE_EN generic) */
//SYSINFO_SOC_reserved   =  7, /**< SYSINFO_SOC  (7) (r/-): reserved */
//SYSINFO_SOC_reserved   =  8, /**< SYSINFO_SOC  (8) (r/-): reserved */
//SYSINFO_SOC_reserved   =  9, /**< SYSINFO_SOC  (9) (r/-): reserved */
//SYSINFO_SOC_reserved   = 10, /**< SYSINFO_SOC (10) (r/-): reserved */
  SYSINFO_SOC_OCD_AUTH   = 11, /**< SYSINFO_SOC (11) (r/-): On-chip debugger authentication implemented when 1 (via OCD_AUTHENTICATION generic) */
  SYSINFO_SOC_IMEM_ROM   = 12, /**< SYSINFO_SOC (12) (r/-): Processor-internal instruction memory implemented as pre-initialized ROM when 1 (via BOOT_MODE_SELECT generic) */
  SYSINFO_SOC_IO_TWD     = 13, /**< SYSINFO_SOC (13) (r/-): Two-wire device implemented when 1 (via IO_TWD_EN generic) */
  SYSINFO_SOC_IO_DMA     = 14, /**< SYSINFO_SOC (14) (r/-): Direct memory access controller implemented when 1 (via IO_DMA_EN generic) */
  SYSINFO_SOC_IO_GPIO    = 15, /**< SYSINFO_SOC (15) (r/-): General purpose input/output port unit implemented when 1 (via IO_GPIO_EN generic) */
  SYSINFO_SOC_IO_CLINT   = 16, /**< SYSINFO_SOC (16) (r/-): Core local interruptor implemented when 1 (via IO_CLINT_EN generic) */
  SYSINFO_SOC_IO_UART0   = 17, /**< SYSINFO_SOC (17) (r/-): Primary universal asynchronous receiver/transmitter 0 implemented when 1 (via IO_UART0_EN generic) */
  SYSINFO_SOC_IO_SPI     = 18, /**< SYSINFO_SOC (18) (r/-): Serial peripheral interface implemented when 1 (via IO_SPI_EN generic) */
  SYSINFO_SOC_IO_TWI     = 19, /**< SYSINFO_SOC (19) (r/-): Two-wire interface implemented when 1 (via IO_TWI_EN generic) */
  SYSINFO_SOC_IO_PWM     = 20, /**< SYSINFO_SOC (20) (r/-): Pulse-width modulation unit implemented when 1 (via IO_PWM_EN generic) */
  SYSINFO_SOC_IO_WDT     = 21, /**< SYSINFO_SOC (21) (r/-): Watchdog timer implemented when 1 (via IO_WDT_EN generic) */
  SYSINFO_SOC_IO_CFS     = 22, /**< SYSINFO_SOC (22) (r/-): Custom functions subsystem implemented when 1 (via IO_CFS_EN generic) */
  SYSINFO_SOC_IO_TRNG    = 23, /**< SYSINFO_SOC (23) (r/-): True random number generator implemented when 1 (via IO_TRNG_EN generic) */
  SYSINFO_SOC_IO_SDI     = 24, /**< SYSINFO_SOC (24) (r/-): Serial data interface implemented when 1 (via IO_SDI_EN generic) */
  SYSINFO_SOC_IO_UART1   = 25, /**< SYSINFO_SOC (25) (r/-): Secondary universal asynchronous receiver/transmitter 1 implemented when 1 (via IO_UART1_EN generic) */
  SYSINFO_SOC_IO_NEOLED  = 26, /**< SYSINFO_SOC (26) (r/-): NeoPixel-compatible smart LED interface implemented when 1 (via IO_NEOLED_EN generic) */
  SYSINFO_SOC_IO_TRACER  = 27, /**< SYSINFO_SOC (10) (r/-): Execution tracer implemented when 1 (via IO_TRACER_EN generic) */
  SYSINFO_SOC_IO_GPTMR   = 28, /**< SYSINFO_SOC (28) (r/-): General purpose timer implemented when 1 (via IO_GPTMR_EN generic) */
  SYSINFO_SOC_IO_SLINK   = 29, /**< SYSINFO_SOC (29) (r/-): Stream link interface implemented when 1 (via IO_SLINK_EN generic) */
  SYSINFO_SOC_IO_ONEWIRE = 30  /**< SYSINFO_SOC (30) (r/-): 1-wire interface controller implemented when 1 (via IO_ONEWIRE_EN generic) */
//SYSINFO_SOC_reserved   = 31  /**< SYSINFO_SOC (31) (r/-): reserved */
};

/** NEORV32_SYSINFO.CACHE (r/-): Cache configuration */
 enum NEORV32_SYSINFO_CACHE_enum {
  SYSINFO_CACHE_INST_BLOCK_SIZE_0 =  0, /**< SYSINFO_CACHE  (0) (r/-): i-cache: log2(Block size in bytes), bit 0 (via CACHE_BLOCK_SIZE generic) */
  SYSINFO_CACHE_INST_BLOCK_SIZE_3 =  3, /**< SYSINFO_CACHE  (3) (r/-): i-cache: log2(Block size in bytes), bit 3 (via CACHE_BLOCK_SIZE generic) */
  SYSINFO_CACHE_INST_NUM_BLOCKS_0 =  4, /**< SYSINFO_CACHE  (4) (r/-): i-cache: log2(Number of cache blocks), bit 0 (via ICACHE_NUM_BLOCKS generic) */
  SYSINFO_CACHE_INST_NUM_BLOCKS_3 =  7, /**< SYSINFO_CACHE  (7) (r/-): i-cache: log2(Number of cache blocks), bit 3 (via ICACHE_NUM_BLOCKS generic) */

  SYSINFO_CACHE_DATA_BLOCK_SIZE_0 =  8, /**< SYSINFO_CACHE  (8) (r/-): d-cache: log2(Block size in bytes), bit 0 (via CACHE_BLOCK_SIZE generic) */
  SYSINFO_CACHE_DATA_BLOCK_SIZE_3 = 11, /**< SYSINFO_CACHE (11) (r/-): d-cache: log2(Block size in bytes), bit 3 (via CACHE_BLOCK_SIZE generic) */
  SYSINFO_CACHE_DATA_NUM_BLOCKS_0 = 12, /**< SYSINFO_CACHE (12) (r/-): d-cache: log2(Number of cache blocks), bit 0 (via DCACHE_NUM_BLOCKS generic) */
  SYSINFO_CACHE_DATA_NUM_BLOCKS_3 = 15, /**< SYSINFO_CACHE (15) (r/-): d-cache: log2(Number of cache blocks), bit 3 (via DCACHE_NUM_BLOCKS generic) */

  SYSINFO_CACHE_INST_BURSTS_EN    = 16, /**< SYSINFO_CACHE (16) (r/-): i-cache: issue burst transfers or cache update (via CACHE_BURSTS_EN generic) */
  SYSINFO_CACHE_DATA_BURSTS_EN    = 24  /**< SYSINFO_CACHE (14) (r/-): d-cache: issue burst transfers or cache update (via CACHE_BURSTS_EN generic) */
};
/**@}*/


#endif // neorv32_sysinfo_h
