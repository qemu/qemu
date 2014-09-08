#ifndef LOADER_H
#define LOADER_H
#include "qapi/qmp/qdict.h"
#include "hw/nvram/fw_cfg.h"

/* loader.c */
/**
 * get_image_size: retrieve size of an image file
 * @filename: Path to the image file
 *
 * Returns the size of the image file on success, -1 otherwise.
 * On error, errno is also set as appropriate.
 */
int get_image_size(const char *filename);
int load_image(const char *filename, uint8_t *addr); /* deprecated */
ssize_t load_image_size(const char *filename, void *addr, size_t size);
int load_image_targphys(const char *filename, hwaddr,
                        uint64_t max_sz);
int load_image_gzipped(const char *filename, hwaddr addr, uint64_t max_sz);

#define ELF_LOAD_FAILED       -1
#define ELF_LOAD_NOT_ELF      -2
#define ELF_LOAD_WRONG_ARCH   -3
#define ELF_LOAD_WRONG_ENDIAN -4
const char *load_elf_strerror(int error);
int load_elf(const char *filename, uint64_t (*translate_fn)(void *, uint64_t),
             void *translate_opaque, uint64_t *pentry, uint64_t *lowaddr,
             uint64_t *highaddr, int big_endian, int elf_machine,
             int clear_lsb);
int load_aout(const char *filename, hwaddr addr, int max_sz,
              int bswap_needed, hwaddr target_page_size);
int load_uimage(const char *filename, hwaddr *ep,
                hwaddr *loadaddr, int *is_linux);

/**
 * load_ramdisk:
 * @filename: Path to the ramdisk image
 * @addr: Memory address to load the ramdisk to
 * @max_sz: Maximum allowed ramdisk size (for non-u-boot ramdisks)
 *
 * Load a ramdisk image with U-Boot header to the specified memory
 * address.
 *
 * Returns the size of the loaded image on success, -1 otherwise.
 */
int load_ramdisk(const char *filename, hwaddr addr, uint64_t max_sz);

ssize_t read_targphys(const char *name,
                      int fd, hwaddr dst_addr, size_t nbytes);
void pstrcpy_targphys(const char *name,
                      hwaddr dest, int buf_size,
                      const char *source);

extern bool option_rom_has_mr;
extern bool rom_file_has_mr;

int rom_add_file(const char *file, const char *fw_dir,
                 hwaddr addr, int32_t bootindex,
                 bool option_rom);
void *rom_add_blob(const char *name, const void *blob, size_t len,
                   hwaddr addr, const char *fw_file_name,
                   FWCfgReadCallback fw_callback, void *callback_opaque);
int rom_add_elf_program(const char *name, void *data, size_t datasize,
                        size_t romsize, hwaddr addr);
int rom_load_all(void);
void rom_load_done(void);
void rom_set_fw(FWCfgState *f);
int rom_copy(uint8_t *dest, hwaddr addr, size_t size);
void *rom_ptr(hwaddr addr);
void do_info_roms(Monitor *mon, const QDict *qdict);

#define rom_add_file_fixed(_f, _a, _i)          \
    rom_add_file(_f, NULL, _a, _i, false)
#define rom_add_blob_fixed(_f, _b, _l, _a)      \
    rom_add_blob(_f, _b, _l, _a, NULL, NULL, NULL)

#define PC_ROM_MIN_VGA     0xc0000
#define PC_ROM_MIN_OPTION  0xc8000
#define PC_ROM_MAX         0xe0000
#define PC_ROM_ALIGN       0x800
#define PC_ROM_SIZE        (PC_ROM_MAX - PC_ROM_MIN_VGA)

int rom_add_vga(const char *file);
int rom_add_option(const char *file, int32_t bootindex);

#endif
