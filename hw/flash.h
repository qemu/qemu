/* NOR flash devices */
typedef struct pflash_t pflash_t;

/* pflash_cfi01.c */
pflash_t *pflash_cfi01_register(target_phys_addr_t base, ram_addr_t off,
                                BlockDriverState *bs,
                                uint32_t sector_len, int nb_blocs, int width,
                                uint16_t id0, uint16_t id1,
                                uint16_t id2, uint16_t id3);

/* pflash_cfi02.c */
pflash_t *pflash_cfi02_register(target_phys_addr_t base, ram_addr_t off,
                                BlockDriverState *bs, uint32_t sector_len,
                                int nb_blocs, int width,
                                uint16_t id0, uint16_t id1,
                                uint16_t id2, uint16_t id3);

/* nand.c */
struct nand_flash_s;
struct nand_flash_s *nand_init(int manf_id, int chip_id);
void nand_done(struct nand_flash_s *s);
void nand_setpins(struct nand_flash_s *s,
                int cle, int ale, int ce, int wp, int gnd);
void nand_getpins(struct nand_flash_s *s, int *rb);
void nand_setio(struct nand_flash_s *s, uint8_t value);
uint8_t nand_getio(struct nand_flash_s *s);

#define NAND_MFR_TOSHIBA	0x98
#define NAND_MFR_SAMSUNG	0xec
#define NAND_MFR_FUJITSU	0x04
#define NAND_MFR_NATIONAL	0x8f
#define NAND_MFR_RENESAS	0x07
#define NAND_MFR_STMICRO	0x20
#define NAND_MFR_HYNIX		0xad
#define NAND_MFR_MICRON		0x2c

/* ecc.c */
struct ecc_state_s {
    uint8_t cp;		/* Column parity */
    uint16_t lp[2];	/* Line parity */
    uint16_t count;
};

uint8_t ecc_digest(struct ecc_state_s *s, uint8_t sample);
void ecc_reset(struct ecc_state_s *s);
void ecc_put(QEMUFile *f, struct ecc_state_s *s);
void ecc_get(QEMUFile *f, struct ecc_state_s *s);
