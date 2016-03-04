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
/**
 * load_image_mr: load an image into a memory region
 * @filename: Path to the image file
 * @mr: Memory Region to load into
 *
 * Load the specified file into the memory region.
 * The file loaded is registered as a ROM, so its contents will be
 * reinstated whenever the system is reset.
 * If the file is larger than the memory region's size the call will fail.
 * Returns -1 on failure, or the size of the file.
 */
int load_image_mr(const char *filename, MemoryRegion *mr);

/* This is the limit on the maximum uncompressed image size that
 * load_image_gzipped_buffer() and load_image_gzipped() will read. It prevents
 * g_malloc() in those functions from allocating a huge amount of memory.
 */
#define LOAD_IMAGE_MAX_GUNZIP_BYTES (256 << 20)

int load_image_gzipped_buffer(const char *filename, uint64_t max_sz,
                              uint8_t **buffer);
int load_image_gzipped(const char *filename, hwaddr addr, uint64_t max_sz);

#define ELF_LOAD_FAILED       -1
#define ELF_LOAD_NOT_ELF      -2
#define ELF_LOAD_WRONG_ARCH   -3
#define ELF_LOAD_WRONG_ENDIAN -4
const char *load_elf_strerror(int error);

/** load_elf:
 * @filename: Path of ELF file
 * @translate_fn: optional function to translate load addresses
 * @translate_opaque: opaque data passed to @translate_fn
 * @pentry: Populated with program entry point. Ignored if NULL.
 * @lowaddr: Populated with lowest loaded address. Ignored if NULL.
 * @highaddr: Populated with highest loaded address. Ignored if NULL.
 * @bigendian: Expected ELF endianness. 0 for LE otherwise BE
 * @elf_machine: Expected ELF machine type
 * @clear_lsb: Set to mask off LSB of addresses (Some architectures use
 *             this for non-address data)
 * @data_swab: Set to order of byte swapping for data. 0 for no swap, 1
 *             for swapping bytes within halfwords, 2 for bytes within
 *             words and 3 for within doublewords.
 *
 * Load an ELF file's contents to the emulated system's address space.
 * Clients may optionally specify a callback to perform address
 * translations. @pentry, @lowaddr and @highaddr are optional pointers
 * which will be populated with various load information. @bigendian and
 * @elf_machine give the expected endianness and machine for the ELF the
 * load will fail if the target ELF does not match. Some architectures
 * have some architecture-specific behaviours that come into effect when
 * their particular values for @elf_machine are set.
 */

int load_elf(const char *filename, uint64_t (*translate_fn)(void *, uint64_t),
             void *translate_opaque, uint64_t *pentry, uint64_t *lowaddr,
             uint64_t *highaddr, int big_endian, int elf_machine,
             int clear_lsb, int data_swab);

/** load_elf_hdr:
 * @filename: Path of ELF file
 * @hdr: Buffer to populate with header data. Header data will not be
 * filled if set to NULL.
 * @is64: Set to true if the ELF is 64bit. Ignored if set to NULL
 * @errp: Populated with an error in failure cases
 *
 * Inspect an ELF file's header. Read its full header contents into a
 * buffer and/or determine if the ELF is 64bit.
 */
void load_elf_hdr(const char *filename, void *hdr, bool *is64, Error **errp);

int load_aout(const char *filename, hwaddr addr, int max_sz,
              int bswap_needed, hwaddr target_page_size);
int load_uimage(const char *filename, hwaddr *ep,
                hwaddr *loadaddr, int *is_linux,
                uint64_t (*translate_fn)(void *, uint64_t),
                void *translate_opaque);

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
                 bool option_rom, MemoryRegion *mr);
MemoryRegion *rom_add_blob(const char *name, const void *blob, size_t len,
                           size_t max_len, hwaddr addr,
                           const char *fw_file_name,
                           FWCfgReadCallback fw_callback,
                           void *callback_opaque);
int rom_add_elf_program(const char *name, void *data, size_t datasize,
                        size_t romsize, hwaddr addr);
int rom_check_and_register_reset(void);
void rom_set_fw(FWCfgState *f);
int rom_copy(uint8_t *dest, hwaddr addr, size_t size);
void *rom_ptr(hwaddr addr);
void hmp_info_roms(Monitor *mon, const QDict *qdict);

#define rom_add_file_fixed(_f, _a, _i)          \
    rom_add_file(_f, NULL, _a, _i, false, NULL)
#define rom_add_blob_fixed(_f, _b, _l, _a)      \
    rom_add_blob(_f, _b, _l, _l, _a, NULL, NULL, NULL)
#define rom_add_file_mr(_f, _mr, _i)            \
    rom_add_file(_f, NULL, 0, _i, false, mr)

#define PC_ROM_MIN_VGA     0xc0000
#define PC_ROM_MIN_OPTION  0xc8000
#define PC_ROM_MAX         0xe0000
#define PC_ROM_ALIGN       0x800
#define PC_ROM_SIZE        (PC_ROM_MAX - PC_ROM_MIN_VGA)

int rom_add_vga(const char *file);
int rom_add_option(const char *file, int32_t bootindex);

#endif
