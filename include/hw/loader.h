#ifndef LOADER_H
#define LOADER_H
#include "hw/nvram/fw_cfg.h"

/* loader.c */
/**
 * get_image_size: retrieve size of an image file
 * @filename: Path to the image file
 *
 * Returns the size of the image file on success, -1 otherwise.
 * On error, errno is also set as appropriate.
 */
int64_t get_image_size(const char *filename);
/**
 * load_image_size: load an image file into specified buffer
 * @filename: Path to the image file
 * @addr: Buffer to load image into
 * @size: Size of buffer in bytes
 *
 * Load an image file from disk into the specified buffer.
 * If the image is larger than the specified buffer, only
 * @size bytes are read (this is not considered an error).
 *
 * Prefer to use the GLib function g_file_get_contents() rather
 * than a "get_image_size()/g_malloc()/load_image_size()" sequence.
 *
 * Returns the number of bytes read, or -1 on error. On error,
 * errno is also set as appropriate.
 */
ssize_t load_image_size(const char *filename, void *addr, size_t size);

/**load_image_targphys_as:
 * @filename: Path to the image file
 * @addr: Address to load the image to
 * @max_sz: The maximum size of the image to load
 * @as: The AddressSpace to load the ELF to. The value of address_space_memory
 *      is used if nothing is supplied here.
 *
 * Load a fixed image into memory.
 *
 * Returns the size of the loaded image on success, -1 otherwise.
 */
ssize_t load_image_targphys_as(const char *filename,
                               hwaddr addr, uint64_t max_sz, AddressSpace *as);

/**load_targphys_hex_as:
 * @filename: Path to the .hex file
 * @entry: Store the entry point given by the .hex file
 * @as: The AddressSpace to load the .hex file to. The value of
 *      address_space_memory is used if nothing is supplied here.
 *
 * Load a fixed .hex file into memory.
 *
 * Returns the size of the loaded .hex file on success, -1 otherwise.
 */
ssize_t load_targphys_hex_as(const char *filename, hwaddr *entry,
                             AddressSpace *as);

/** load_image_targphys:
 * Same as load_image_targphys_as(), but doesn't allow the caller to specify
 * an AddressSpace.
 */
ssize_t load_image_targphys(const char *filename, hwaddr,
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
ssize_t load_image_mr(const char *filename, MemoryRegion *mr);

/* This is the limit on the maximum uncompressed image size that
 * load_image_gzipped_buffer() and load_image_gzipped() will read. It prevents
 * g_malloc() in those functions from allocating a huge amount of memory.
 */
#define LOAD_IMAGE_MAX_GUNZIP_BYTES (256 << 20)

ssize_t load_image_gzipped_buffer(const char *filename, uint64_t max_sz,
                                  uint8_t **buffer);
ssize_t load_image_gzipped(const char *filename, hwaddr addr, uint64_t max_sz);

/**
 * unpack_efi_zboot_image:
 * @buffer: pointer to a variable holding the address of a buffer containing the
 *          image
 * @size: pointer to a variable holding the size of the buffer
 *
 * Check whether the buffer contains a EFI zboot image, and if it does, extract
 * the compressed payload and decompress it into a new buffer. If successful,
 * the old buffer is freed, and the *buffer and size variables pointed to by the
 * function arguments are updated to refer to the newly populated buffer.
 *
 * Returns 0 if the image could not be identified as a EFI zboot image.
 * Returns -1 if the buffer contents were identified as a EFI zboot image, but
 * unpacking failed for any reason.
 * Returns the size of the decompressed payload if decompression was performed
 * successfully.
 */
ssize_t unpack_efi_zboot_image(uint8_t **buffer, int *size);

#define ELF_LOAD_FAILED       -1
#define ELF_LOAD_NOT_ELF      -2
#define ELF_LOAD_WRONG_ARCH   -3
#define ELF_LOAD_WRONG_ENDIAN -4
#define ELF_LOAD_TOO_BIG      -5
const char *load_elf_strerror(ssize_t error);

/** load_elf_ram_sym:
 * @filename: Path of ELF file
 * @elf_note_fn: optional function to parse ELF Note type
 *               passed via @translate_opaque
 * @translate_fn: optional function to translate load addresses
 * @translate_opaque: opaque data passed to @translate_fn
 * @pentry: Populated with program entry point. Ignored if NULL.
 * @lowaddr: Populated with lowest loaded address. Ignored if NULL.
 * @highaddr: Populated with highest loaded address. Ignored if NULL.
 * @pflags: Populated with ELF processor-specific flags. Ignore if NULL.
 * @bigendian: Expected ELF endianness. 0 for LE otherwise BE
 * @elf_machine: Expected ELF machine type
 * @clear_lsb: Set to mask off LSB of addresses (Some architectures use
 *             this for non-address data)
 * @data_swab: Set to order of byte swapping for data. 0 for no swap, 1
 *             for swapping bytes within halfwords, 2 for bytes within
 *             words and 3 for within doublewords.
 * @as: The AddressSpace to load the ELF to. The value of address_space_memory
 *      is used if nothing is supplied here.
 * @load_rom : Load ELF binary as ROM
 * @sym_cb: Callback function for symbol table entries
 *
 * Load an ELF file's contents to the emulated system's address space.
 * Clients may optionally specify a callback to perform address
 * translations. @pentry, @lowaddr and @highaddr are optional pointers
 * which will be populated with various load information. @bigendian and
 * @elf_machine give the expected endianness and machine for the ELF the
 * load will fail if the target ELF does not match. Some architectures
 * have some architecture-specific behaviours that come into effect when
 * their particular values for @elf_machine are set.
 * If @elf_machine is EM_NONE then the machine type will be read from the
 * ELF header and no checks will be carried out against the machine type.
 */
typedef void (*symbol_fn_t)(const char *st_name, int st_info,
                            uint64_t st_value, uint64_t st_size);

ssize_t load_elf_ram_sym(const char *filename,
                         uint64_t (*elf_note_fn)(void *, void *, bool),
                         uint64_t (*translate_fn)(void *, uint64_t),
                         void *translate_opaque, uint64_t *pentry,
                         uint64_t *lowaddr, uint64_t *highaddr,
                         uint32_t *pflags, int big_endian, int elf_machine,
                         int clear_lsb, int data_swab,
                         AddressSpace *as, bool load_rom, symbol_fn_t sym_cb);

/** load_elf_ram:
 * Same as load_elf_ram_sym(), but doesn't allow the caller to specify a
 * symbol callback function
 */
ssize_t load_elf_ram(const char *filename,
                     uint64_t (*elf_note_fn)(void *, void *, bool),
                     uint64_t (*translate_fn)(void *, uint64_t),
                     void *translate_opaque, uint64_t *pentry,
                     uint64_t *lowaddr, uint64_t *highaddr, uint32_t *pflags,
                     int big_endian, int elf_machine, int clear_lsb,
                     int data_swab, AddressSpace *as, bool load_rom);

/** load_elf_as:
 * Same as load_elf_ram(), but always loads the elf as ROM
 */
ssize_t load_elf_as(const char *filename,
                    uint64_t (*elf_note_fn)(void *, void *, bool),
                    uint64_t (*translate_fn)(void *, uint64_t),
                    void *translate_opaque, uint64_t *pentry, uint64_t *lowaddr,
                    uint64_t *highaddr, uint32_t *pflags, int big_endian,
                    int elf_machine, int clear_lsb, int data_swab,
                    AddressSpace *as);

/** load_elf:
 * Same as load_elf_as(), but doesn't allow the caller to specify an
 * AddressSpace.
 */
ssize_t load_elf(const char *filename,
                 uint64_t (*elf_note_fn)(void *, void *, bool),
                 uint64_t (*translate_fn)(void *, uint64_t),
                 void *translate_opaque, uint64_t *pentry, uint64_t *lowaddr,
                 uint64_t *highaddr, uint32_t *pflags, int big_endian,
                 int elf_machine, int clear_lsb, int data_swab);

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

ssize_t load_aout(const char *filename, hwaddr addr, int max_sz,
                  int bswap_needed, hwaddr target_page_size);

#define LOAD_UIMAGE_LOADADDR_INVALID (-1)

/** load_uimage_as:
 * @filename: Path of uimage file
 * @ep: Populated with program entry point. Ignored if NULL.
 * @loadaddr: load address if none specified in the image or when loading a
 *            ramdisk. Populated with the load address. Ignored if NULL or
 *            LOAD_UIMAGE_LOADADDR_INVALID (images which do not specify a load
 *            address will not be loadable).
 * @is_linux: Is set to true if the image loaded is Linux. Ignored if NULL.
 * @translate_fn: optional function to translate load addresses
 * @translate_opaque: opaque data passed to @translate_fn
 * @as: The AddressSpace to load the ELF to. The value of address_space_memory
 *      is used if nothing is supplied here.
 *
 * Loads a u-boot image into memory.
 *
 * Returns the size of the loaded image on success, -1 otherwise.
 */
ssize_t load_uimage_as(const char *filename, hwaddr *ep,
                       hwaddr *loadaddr, int *is_linux,
                       uint64_t (*translate_fn)(void *, uint64_t),
                       void *translate_opaque, AddressSpace *as);

/** load_uimage:
 * Same as load_uimage_as(), but doesn't allow the caller to specify an
 * AddressSpace.
 */
ssize_t load_uimage(const char *filename, hwaddr *ep,
                    hwaddr *loadaddr, int *is_linux,
                    uint64_t (*translate_fn)(void *, uint64_t),
                    void *translate_opaque);

/**
 * load_ramdisk_as:
 * @filename: Path to the ramdisk image
 * @addr: Memory address to load the ramdisk to
 * @max_sz: Maximum allowed ramdisk size (for non-u-boot ramdisks)
 * @as: The AddressSpace to load the ELF to. The value of address_space_memory
 *      is used if nothing is supplied here.
 *
 * Load a ramdisk image with U-Boot header to the specified memory
 * address.
 *
 * Returns the size of the loaded image on success, -1 otherwise.
 */
ssize_t load_ramdisk_as(const char *filename, hwaddr addr, uint64_t max_sz,
                        AddressSpace *as);

/**
 * load_ramdisk:
 * Same as load_ramdisk_as(), but doesn't allow the caller to specify
 * an AddressSpace.
 */
ssize_t load_ramdisk(const char *filename, hwaddr addr, uint64_t max_sz);

ssize_t gunzip(void *dst, size_t dstlen, uint8_t *src, size_t srclen);

ssize_t read_targphys(const char *name,
                      int fd, hwaddr dst_addr, size_t nbytes);
void pstrcpy_targphys(const char *name,
                      hwaddr dest, int buf_size,
                      const char *source);

ssize_t rom_add_file(const char *file, const char *fw_dir,
                     hwaddr addr, int32_t bootindex,
                     bool option_rom, MemoryRegion *mr, AddressSpace *as);
MemoryRegion *rom_add_blob(const char *name, const void *blob, size_t len,
                           size_t max_len, hwaddr addr,
                           const char *fw_file_name,
                           FWCfgCallback fw_callback,
                           void *callback_opaque, AddressSpace *as,
                           bool read_only);
int rom_add_elf_program(const char *name, GMappedFile *mapped_file, void *data,
                        size_t datasize, size_t romsize, hwaddr addr,
                        AddressSpace *as);
int rom_check_and_register_reset(void);
void rom_set_fw(FWCfgState *f);
void rom_set_order_override(int order);
void rom_reset_order_override(void);

/**
 * rom_transaction_begin:
 *
 * Call this before of a series of rom_add_*() calls.  Call
 * rom_transaction_end() afterwards to commit or abort.  These functions are
 * useful for undoing a series of rom_add_*() calls if image file loading fails
 * partway through.
 */
void rom_transaction_begin(void);

/**
 * rom_transaction_end:
 * @commit: true to commit added roms, false to drop added roms
 *
 * Call this after a series of rom_add_*() calls.  See rom_transaction_begin().
 */
void rom_transaction_end(bool commit);

int rom_copy(uint8_t *dest, hwaddr addr, size_t size);
void *rom_ptr(hwaddr addr, size_t size);
/**
 * rom_ptr_for_as: Return a pointer to ROM blob data for the address
 * @as: AddressSpace to look for the ROM blob in
 * @addr: Address within @as
 * @size: size of data required in bytes
 *
 * Returns: pointer into the data which backs the matching ROM blob,
 * or NULL if no blob covers the address range.
 *
 * This function looks for a ROM blob which covers the specified range
 * of bytes of length @size starting at @addr within the address space
 * @as. This is useful for code which runs as part of board
 * initialization or CPU reset which wants to read data that is part
 * of a user-supplied guest image or other guest memory contents, but
 * which runs before the ROM loader's reset function has copied the
 * blobs into guest memory.
 *
 * rom_ptr_for_as() will look not just for blobs loaded directly to
 * the specified address, but also for blobs which were loaded to an
 * alias of the region at a different location in the AddressSpace.
 * In other words, if a machine model has RAM at address 0x0000_0000
 * which is aliased to also appear at 0x1000_0000, rom_ptr_for_as()
 * will return the correct data whether the guest image was linked and
 * loaded at 0x0000_0000 or 0x1000_0000.  Contrast rom_ptr(), which
 * will only return data if the image load address is an exact match
 * with the queried address.
 *
 * New code should prefer to use rom_ptr_for_as() instead of
 * rom_ptr().
 */
void *rom_ptr_for_as(AddressSpace *as, hwaddr addr, size_t size);
void hmp_info_roms(Monitor *mon, const QDict *qdict);

#define rom_add_file_fixed(_f, _a, _i)          \
    rom_add_file(_f, NULL, _a, _i, false, NULL, NULL)
#define rom_add_blob_fixed(_f, _b, _l, _a)      \
    rom_add_blob(_f, _b, _l, _l, _a, NULL, NULL, NULL, NULL, true)
#define rom_add_file_mr(_f, _mr, _i)            \
    rom_add_file(_f, NULL, 0, _i, false, _mr, NULL)
#define rom_add_file_as(_f, _as, _i)            \
    rom_add_file(_f, NULL, 0, _i, false, NULL, _as)
#define rom_add_file_fixed_as(_f, _a, _i, _as)          \
    rom_add_file(_f, NULL, _a, _i, false, NULL, _as)
#define rom_add_blob_fixed_as(_f, _b, _l, _a, _as)      \
    rom_add_blob(_f, _b, _l, _l, _a, NULL, NULL, NULL, _as, true)

ssize_t rom_add_vga(const char *file);
ssize_t rom_add_option(const char *file, int32_t bootindex);

/* This is the usual maximum in uboot, so if a uImage overflows this, it would
 * overflow on real hardware too. */
#define UBOOT_MAX_GUNZIP_BYTES (64 << 20)

typedef struct RomGap {
    hwaddr base;
    size_t size;
} RomGap;

/**
 * rom_find_largest_gap_between: return largest gap between ROMs in given range
 *
 * Given a range of addresses, this function finds the largest
 * contiguous subrange which has no ROMs loaded to it. That is,
 * it finds the biggest gap which is free for use for other things.
 */
RomGap rom_find_largest_gap_between(hwaddr base, size_t size);

#endif
