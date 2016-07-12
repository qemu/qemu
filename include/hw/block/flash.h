#ifndef HW_FLASH_H
#define HW_FLASH_H

/* NOR flash devices */

#include "exec/memory.h"

#define TYPE_CFI_PFLASH01 "cfi.pflash01"
#define TYPE_CFI_PFLASH02 "cfi.pflash02"

typedef struct pflash_t pflash_t;

/* pflash_cfi01.c */
pflash_t *pflash_cfi01_register(hwaddr base,
                                DeviceState *qdev, const char *name,
                                hwaddr size,
                                BlockBackend *blk,
                                uint32_t sector_len, int nb_blocs, int width,
                                uint16_t id0, uint16_t id1,
                                uint16_t id2, uint16_t id3, int be);

/* pflash_cfi02.c */
pflash_t *pflash_cfi02_register(hwaddr base,
                                DeviceState *qdev, const char *name,
                                hwaddr size,
                                BlockBackend *blk, uint32_t sector_len,
                                int nb_blocs, int nb_mappings, int width,
                                uint16_t id0, uint16_t id1,
                                uint16_t id2, uint16_t id3,
                                uint16_t unlock_addr0, uint16_t unlock_addr1,
                                int be);

MemoryRegion *pflash_cfi01_get_memory(pflash_t *fl);

/* nand.c */
DeviceState *nand_init(BlockBackend *blk, int manf_id, int chip_id);
void nand_setpins(DeviceState *dev, uint8_t cle, uint8_t ale,
                  uint8_t ce, uint8_t wp, uint8_t gnd);
void nand_getpins(DeviceState *dev, int *rb);
void nand_setio(DeviceState *dev, uint32_t value);
uint32_t nand_getio(DeviceState *dev);
uint32_t nand_getbuswidth(DeviceState *dev);

#define NAND_MFR_TOSHIBA	0x98
#define NAND_MFR_SAMSUNG	0xec
#define NAND_MFR_FUJITSU	0x04
#define NAND_MFR_NATIONAL	0x8f
#define NAND_MFR_RENESAS	0x07
#define NAND_MFR_STMICRO	0x20
#define NAND_MFR_HYNIX		0xad
#define NAND_MFR_MICRON		0x2c

/* onenand.c */
void *onenand_raw_otp(DeviceState *onenand_device);

/* ecc.c */
typedef struct {
    uint8_t cp;		/* Column parity */
    uint16_t lp[2];	/* Line parity */
    uint16_t count;
} ECCState;

uint8_t ecc_digest(ECCState *s, uint8_t sample);
void ecc_reset(ECCState *s);
extern VMStateDescription vmstate_ecc_state;

#endif
