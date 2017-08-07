#ifndef BIOS_LINKER_LOADER_H
#define BIOS_LINKER_LOADER_H


typedef struct BIOSLinker {
    GArray *cmd_blob;
    GArray *file_list;
} BIOSLinker;

bool bios_linker_loader_can_write_pointer(void);

BIOSLinker *bios_linker_loader_init(void);

void bios_linker_loader_alloc(BIOSLinker *linker,
                              const char *file_name,
                              GArray *file_blob,
                              uint32_t alloc_align,
                              bool alloc_fseg);

void bios_linker_loader_add_checksum(BIOSLinker *linker, const char *file,
                                     unsigned start_offset, unsigned size,
                                     unsigned checksum_offset);

void bios_linker_loader_add_pointer(BIOSLinker *linker,
                                    const char *dest_file,
                                    uint32_t dst_patched_offset,
                                    uint8_t dst_patched_size,
                                    const char *src_file,
                                    uint32_t src_offset);

void bios_linker_loader_write_pointer(BIOSLinker *linker,
                                      const char *dest_file,
                                      uint32_t dst_patched_offset,
                                      uint8_t dst_patched_size,
                                      const char *src_file,
                                      uint32_t src_offset);

void bios_linker_loader_cleanup(BIOSLinker *linker);
#endif
