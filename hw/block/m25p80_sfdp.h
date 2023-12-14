/*
 * M25P80 SFDP
 *
 * Copyright (c) 2020, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef HW_M25P80_SFDP_H
#define HW_M25P80_SFDP_H

/*
 * SFDP area has a 3 bytes address space.
 */
#define M25P80_SFDP_MAX_SIZE  (1 << 24)

uint8_t m25p80_sfdp_n25q256a(uint32_t addr);

uint8_t m25p80_sfdp_mx25l25635e(uint32_t addr);
uint8_t m25p80_sfdp_mx25l25635f(uint32_t addr);
uint8_t m25p80_sfdp_mx66l1g45g(uint32_t addr);

uint8_t m25p80_sfdp_w25q256(uint32_t addr);
uint8_t m25p80_sfdp_w25q512jv(uint32_t addr);

uint8_t m25p80_sfdp_w25q01jvq(uint32_t addr);

uint8_t m25p80_sfdp_is25wp256(uint32_t addr);

#endif
