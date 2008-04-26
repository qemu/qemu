/*
 * This file contains parts from Linux header files (several authors)
 * Copyright (c) 2006 Stefan Weil (modifications for QEMU integration)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#if !defined(__pflash_h)
#define __pflash_h

#define PFLASH_DEBUG

#define P_ID_NONE 0
#define P_ID_INTEL_EXT 1
#define P_ID_AMD_STD 2
#define P_ID_INTEL_STD 3
#define P_ID_AMD_EXT 4
#define P_ID_MITSUBISHI_STD 256
#define P_ID_MITSUBISHI_EXT 257
#define P_ID_RESERVED 65535

/* flashchip.h */

typedef enum {
        FL_READY,
        FL_STATUS,
        FL_CFI_QUERY,
        FL_JEDEC_QUERY,
        FL_ERASING,
        FL_ERASE_SUSPENDING,
        FL_ERASE_SUSPENDED,
        FL_WRITING,
        FL_WRITING_TO_BUFFER,
        FL_WRITE_SUSPENDING,
        FL_WRITE_SUSPENDED,
        FL_PM_SUSPENDED,
        FL_SYNCING,
        FL_UNLOADING,
        FL_LOCKING,
        FL_UNLOCKING,
        FL_POINT,
        FL_UNKNOWN
} flstate_t;

/* jedec_probe.c */

/* Manufacturers */
#define MANUFACTURER_AMD	0x0001
#define MANUFACTURER_ATMEL	0x001f
#define MANUFACTURER_FUJITSU	0x0004
#define MANUFACTURER_HYUNDAI	0x00ad
#define MANUFACTURER_INTEL	0x0089
#define MANUFACTURER_MACRONIX	0x00c2
#define MANUFACTURER_NEC	0x0010
#define MANUFACTURER_PMC	0x009d
#define MANUFACTURER_SHARP	0x00b0
#define MANUFACTURER_SPANSION	0x0001
#define MANUFACTURER_SST	0x00bf
#define MANUFACTURER_ST		0x0020
#define MANUFACTURER_TOSHIBA	0x0098
#define MANUFACTURER_WINBOND	0x00da
#define MANUFACTURER_004A       0x004a

/* AMD */
#define AM29DL800BB	0x22C8
#define AM29DL800BT	0x224A

#define AM29F800BB	0x2258
#define AM29F800BT	0x22D6
#define AM29LV400BB	0x22BA
#define AM29LV400BT	0x22B9
#define AM29LV800BB	0x225B
#define AM29LV800BT	0x22DA
#define AM29LV160DT	0x22C4
#define AM29LV160DB	0x2249
#define AM29F017D	0x003D
#define AM29F016D	0x00AD
#define AM29F080	0x00D5
#define AM29F040	0x00A4
#define AM29LV040B	0x004F
#define AM29F032B	0x0041
#define AM29F002T	0x00B0

/* Atmel */
#define AT49BV512	0x0003
#define AT29LV512	0x003d
#define AT49BV16X	0x00C0
#define AT49BV16XT	0x00C2
#define AT49BV32X	0x00C8
#define AT49BV32XT	0x00C9

/* ??? 0x004a */
#define ES29LV160DB     0x2249

/* Fujitsu */
#define MBM29F040C	0x00A4
#define MBM29LV650UE	0x22D7
#define MBM29LV320TE	0x22F6
#define MBM29LV320BE	0x22F9
#define MBM29LV160TE	0x22C4
#define MBM29LV160BE	0x2249
#define MBM29LV800BA	0x225B
#define MBM29LV800TA	0x22DA
#define MBM29LV400TC	0x22B9
#define MBM29LV400BC	0x22BA

/* Hyundai */
#define HY29F002T	0x00B0

/* Intel */
#define I28F004B3T	0x00d4
#define I28F004B3B	0x00d5
#define I28F400B3T	0x8894
#define I28F400B3B	0x8895
#define I28F008S5	0x00a6
#define I28F016S5	0x00a0
#define I28F008SA	0x00a2
#define I28F008B3T	0x00d2
#define I28F008B3B	0x00d3
#define I28F800B3T	0x8892
#define I28F800B3B	0x8893
#define I28F016S3	0x00aa
#define I28F016B3T	0x00d0
#define I28F016B3B	0x00d1
#define I28F160S5	0x00d0
#define I28F160B3T	0x8890
#define I28F160B3B	0x8891
#define I28F160C3B	0x88c3
#define I28F320B3T	0x8896
#define I28F320B3B	0x8897
#define I28F640B3T	0x8898
#define I28F640B3B	0x8899
#define I82802AB	0x00ad
#define I82802AC	0x00ac

/* Macronix */
#define MX29LV040C	0x004F
#define MX29LV160T	0x22C4
#define MX29LV160B	0x2249
#define MX29LV320CT	0x22a7
#define MX29LV320CB	0x22a8
#define MX29LV640BT	0x22c9
#define MX29LV640BB	0x22cb
#define MX29F016	0x00AD
#define MX29F002T	0x00B0
#define MX29F004T	0x0045
#define MX29F004B	0x0046

/* NEC */
#define UPD29F064115	0x221C

/* PMC */
#define PM49FL002	0x006D
#define PM49FL004	0x006E
#define PM49FL008	0x006A

/* Sharp */
#define LH28F640BF	0x00b0

/* Spansion (AMD + Fujitsu) */
#define S29AL016DT	0x22C4
#define S29AL016DB	0x2249

/* ST - www.st.com */
#define M29W800DT	0x00D7
#define M29W800DB	0x005B
#define M29W160DT	0x22C4
#define M29W160DB	0x2249
#define M29W040B	0x00E3
#define M50FW040	0x002C
#define M50FW080	0x002D
#define M50FW016	0x002E
#define M50LPW080       0x002F

/* SST */
#define SST29EE020	0x0010
#define SST29LE020	0x0012
#define SST29EE512	0x005d
#define SST29LE512	0x003d
#define SST39LF800	0x2781
#define SST39LF160	0x2782
#define SST39VF1601	0x234b
#define SST39LF512	0x00D4
#define SST39LF010	0x00D5
#define SST39LF020	0x00D6
#define SST39LF040	0x00D7
#define SST39SF010A	0x00B5
#define SST39SF020A	0x00B6
#define SST49LF004B	0x0060
#define SST49LF008A	0x005a
#define SST49LF030A	0x001C
#define SST49LF040A	0x0051
#define SST49LF080A	0x005B

/* Toshiba */
#define TC58FVT160	0x00C2
#define TC58FVB160	0x0043
#define TC58FVT321	0x009A
#define TC58FVB321	0x009C
#define TC58FVT641	0x0093
#define TC58FVB641	0x0095

/* Winbond */
#define W49V002A	0x00b0

/* QEMU interface */

#include "cpu-all.h"            /* ram_addr_t */
#include "cpu-defs.h"           /* target_phys_addr_t, target_ulong */
#include "qemu-common.h"        /* BlockDriverState */
#include "hw/flash.h"           /* pflash_t */

/* NOR flash devices */
//~ typedef struct pflash_t pflash_t;

/* Special interfaces used by pflash_register. */

target_phys_addr_t *x1;
ram_addr_t *x2;
BlockDriverState *x3;
uint32_t *x4;

pflash_t *pflash_amd_register (target_phys_addr_t base, ram_addr_t off,
                               BlockDriverState *bs,
                               uint32_t sector_len, int nb_blocs, int width,
                               uint16_t id0, uint16_t id1, 
                               uint16_t id2, uint16_t id3);

/* User interface. */

pflash_t *pflash_device_register (target_phys_addr_t base, ram_addr_t off,
                           BlockDriverState *bs, uint32_t size, int width,
                           uint16_t flash_manufacturer, uint16_t flash_type);

#endif /* __pflash_h */
