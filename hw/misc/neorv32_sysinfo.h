#ifndef HW_NEORV32_SYSINFO_H
#define HW_NEORV32_SYSINFO_H

#include "system/memory.h"


/* Internal memory sizes (bytes) */
#define SYSINFO_IMEM_SIZE        0x00008000U  /* 32 KiB IMEM */
#define SYSINFO_DMEM_SIZE        0x00008000U  /* 32 KiB DMEM */

/* Number of harts (physical cores) */
#define SYSINFO_NUM_HARTS        1U

/* Boot mode (matches RTL BOOT_MODE_SELECT encoding used in your firmware) */
#define SYSINFO_BOOTMODE_ID      0U           /* 0..3 */

/* Bus timeout encodings: value is log2(cycles); 0 means "no timeout" per your helper */
#define SYSINFO_INTBUS_TO_LOG2   0U           /* 0 => returns 0 */
#define SYSINFO_EXTBUS_TO_LOG2   0U           /* 0 => returns 0 */

/* Clock (Hz): writable at runtime via SYSINFO.CLK */
#define SYSINFO_CLK_HZ_DEFAULT   100000000U   /* 100 MHz */

/* Cache topology encodings (log2 values ) */
#define ICACHE_BLOCK_SIZE_LOG2   0U           /* bits [3:0] */
#define ICACHE_NUM_BLOCKS_LOG2   0U           /* bits [7:4] */
#define DCACHE_BLOCK_SIZE_LOG2   0U           /* bits [11:8] */
#define DCACHE_NUM_BLOCKS_LOG2   0U           /* bits [15:12] */
#define ICACHE_BURSTS_EN         0U           /* bit 16 */
#define DCACHE_BURSTS_EN         0U           /* bit 24 */

/* Feature bitmap for SOC register. */
#define SYSINFO_SOC_ENABLE(x)    (1U << (x))

// Enable Bootloader, IMEM, DMEM, UART and SPI
#define SYSINFO_SOC_VAL \
    ( SYSINFO_SOC_ENABLE(SYSINFO_SOC_BOOTLOADER) | \
      SYSINFO_SOC_ENABLE(SYSINFO_SOC_IMEM)       | \
      SYSINFO_SOC_ENABLE(SYSINFO_SOC_DMEM)       | \
      SYSINFO_SOC_ENABLE(SYSINFO_SOC_IO_UART0)   | \
      SYSINFO_SOC_ENABLE(SYSINFO_SOC_IO_SPI) )

/* --------------------------------------------------------------------------------------
 * Address map
 * ------------------------------------------------------------------------------------*/
#define NEORV32_BOOTLOADER_BASE_ADDRESS (0xFFE00000U)
#define NEORV32_IO_BASE_ADDRESS         (0xFFE00000U)

#define NEORV32_IMEM_BASE               (0x00000000U)
#define NEORV32_DMEM_BASE               (0x80000000U)

/* IO base addresses */
#define NEORV32_TWD_BASE     (0xFFEA0000U)
#define NEORV32_CFS_BASE     (0xFFEB0000U)
#define NEORV32_SLINK_BASE   (0xFFEC0000U)
#define NEORV32_DMA_BASE     (0xFFED0000U)
#define NEORV32_CRC_BASE     (0xFFEE0000U)
#define NEORV32_XIP_BASE     (0xFFEF0000U)
#define NEORV32_PWM_BASE     (0xFFF00000U)
#define NEORV32_GPTMR_BASE   (0xFFF10000U)
#define NEORV32_ONEWIRE_BASE (0xFFF20000U)
#define NEORV32_XIRQ_BASE    (0xFFF30000U)
#define NEORV32_MTIME_BASE   (0xFFF40000U)
#define NEORV32_UART0_BASE   (0xFFF50000U)
#define NEORV32_UART1_BASE   (0xFFF60000U)
#define NEORV32_SDI_BASE     (0xFFF70000U)
#define NEORV32_SPI_BASE     (0xFFF80000U)
#define NEORV32_TWI_BASE     (0xFFF90000U)
#define NEORV32_TRNG_BASE    (0xFFFA0000U)
#define NEORV32_WDT_BASE     (0xFFFB0000U)
#define NEORV32_GPIO_BASE    (0xFFFC0000U)
#define NEORV32_NEOLED_BASE  (0xFFFD0000U)
#define NEORV32_SYSINFO_BASE (0xFFFE0000U)
#define NEORV32_DM_BASE      (0xFFFF0000U)

/* MMIO creator */
void neorv32_sysinfo_create(MemoryRegion *address_space, hwaddr base);

#endif //HW_NEORV32_SYSINFO_H
