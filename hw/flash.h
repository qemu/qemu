/* NOR flash devices */
typedef struct pflash_t pflash_t;

/* pflash_cfi01.c */
pflash_t *pflash_cfi01_register(target_phys_addr_t base, ram_addr_t off,
                                BlockDriverState *bs,
                                uint32_t sector_len, int nb_blocs, int width,
                                uint16_t id0, uint16_t id1,
                                uint16_t id2, uint16_t id3, int be);

/* pflash_cfi02.c */
pflash_t *pflash_cfi02_register(target_phys_addr_t base, ram_addr_t off,
                                BlockDriverState *bs, uint32_t sector_len,
                                int nb_blocs, int nb_mappings, int width,
                                uint16_t id0, uint16_t id1,
                                uint16_t id2, uint16_t id3,
                                uint16_t unlock_addr0, uint16_t unlock_addr1,
                                int be);

/* nand.c */
typedef struct NANDFlashState NANDFlashState;
NANDFlashState *nand_init(int manf_id, int chip_id);
void nand_done(NANDFlashState *s);
void nand_setpins(NANDFlashState *s,
                int cle, int ale, int ce, int wp, int gnd);
void nand_getpins(NANDFlashState *s, int *rb);
void nand_setio(NANDFlashState *s, uint8_t value);
uint8_t nand_getio(NANDFlashState *s);

#define NAND_MFR_TOSHIBA	0x98
#define NAND_MFR_SAMSUNG	0xec
#define NAND_MFR_FUJITSU	0x04
#define NAND_MFR_NATIONAL	0x8f
#define NAND_MFR_RENESAS	0x07
#define NAND_MFR_STMICRO	0x20
#define NAND_MFR_HYNIX		0xad
#define NAND_MFR_MICRON		0x2c

/* onenand.c */
void onenand_base_update(void *opaque, target_phys_addr_t new);
void onenand_base_unmap(void *opaque);
void *onenand_init(uint32_t id, int regshift, qemu_irq irq);
void *onenand_raw_otp(void *opaque);

/* ecc.c */
typedef struct {
    uint8_t cp;		/* Column parity */
    uint16_t lp[2];	/* Line parity */
    uint16_t count;
} ECCState;

uint8_t ecc_digest(ECCState *s, uint8_t sample);
void ecc_reset(ECCState *s);
extern VMStateDescription vmstate_ecc_state;
