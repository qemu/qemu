#ifndef BIOS_LINKER_LOADER_H
#define BIOS_LINKER_LOADER_H

#include <glib.h>

GArray *bios_linker_loader_init(void);

void bios_linker_loader_alloc(GArray *linker,
                              const char *file,
                              uint32_t alloc_align,
                              bool alloc_fseg);

void bios_linker_loader_add_checksum(GArray *linker, const char *file,
                                     GArray *table,
                                     void *start, unsigned size,
                                     uint8_t *checksum);

void bios_linker_loader_add_pointer(GArray *linker,
                                    const char *dest_file,
                                    const char *src_file,
                                    GArray *table, void *pointer,
                                    uint8_t pointer_size);

void *bios_linker_loader_cleanup(GArray *linker);
#endif
