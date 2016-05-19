#ifndef BIOS_LINKER_LOADER_H
#define BIOS_LINKER_LOADER_H

#include <glib.h>

typedef struct BIOSLinker {
    GArray *cmd_blob;
    GArray *file_list;
} BIOSLinker;

BIOSLinker *bios_linker_loader_init(void);

void bios_linker_loader_alloc(BIOSLinker *linker,
                              const char *file_name,
                              GArray *file_blob,
                              uint32_t alloc_align,
                              bool alloc_fseg);

void bios_linker_loader_add_checksum(BIOSLinker *linker, const char *file,
                                     void *start, unsigned size,
                                     uint8_t *checksum);

void bios_linker_loader_add_pointer(BIOSLinker *linker,
                                    const char *dest_file,
                                    const char *src_file,
                                    void *pointer,
                                    uint8_t pointer_size);

void *bios_linker_loader_cleanup(BIOSLinker *linker);
#endif
