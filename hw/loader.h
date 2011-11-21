#ifndef LOADER_H
#define LOADER_H

/* loader.c */
int get_image_size(const char *filename);
int load_image(const char *filename, uint8_t *addr); /* deprecated */
int load_image_targphys(const char *filename, target_phys_addr_t, int max_sz);
int load_elf(const char *filename, uint64_t (*translate_fn)(void *, uint64_t),
             void *translate_opaque, uint64_t *pentry, uint64_t *lowaddr,
             uint64_t *highaddr, int big_endian, int elf_machine,
             int clear_lsb);
int load_aout(const char *filename, target_phys_addr_t addr, int max_sz,
              int bswap_needed, target_phys_addr_t target_page_size);
int load_uimage(const char *filename, target_phys_addr_t *ep,
                target_phys_addr_t *loadaddr, int *is_linux);

ssize_t read_targphys(const char *name,
                      int fd, target_phys_addr_t dst_addr, size_t nbytes);
void pstrcpy_targphys(const char *name,
                      target_phys_addr_t dest, int buf_size,
                      const char *source);


int rom_add_file(const char *file, const char *fw_dir,
                 target_phys_addr_t addr, int32_t bootindex);
int rom_add_blob(const char *name, const void *blob, size_t len,
                 target_phys_addr_t addr);
int rom_load_all(void);
void rom_set_fw(void *f);
int rom_copy(uint8_t *dest, target_phys_addr_t addr, size_t size);
void *rom_ptr(target_phys_addr_t addr);
void do_info_roms(Monitor *mon);

#define rom_add_file_fixed(_f, _a, _i)          \
    rom_add_file(_f, NULL, _a, _i)
#define rom_add_blob_fixed(_f, _b, _l, _a)      \
    rom_add_blob(_f, _b, _l, _a)

#define PC_ROM_MIN_VGA     0xc0000
#define PC_ROM_MIN_OPTION  0xc8000
#define PC_ROM_MAX         0xe0000
#define PC_ROM_ALIGN       0x800
#define PC_ROM_SIZE        (PC_ROM_MAX - PC_ROM_MIN_VGA)

int rom_add_vga(const char *file);
int rom_add_option(const char *file, int32_t bootindex);

#endif
