/* Dynamic linker/loader of ACPI tables
 *
 * Copyright (C) 2013 Red Hat Inc
 *
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/acpi/bios-linker-loader.h"
#include "hw/nvram/fw_cfg.h"

#include "qemu/bswap.h"

/*
 * Linker/loader is a paravirtualized interface that passes commands to guest.
 * The commands can be used to request guest to
 * - allocate memory chunks and initialize them from QEMU FW CFG files
 * - link allocated chunks by storing pointer to one chunk into another
 * - calculate ACPI checksum of part of the chunk and store into same chunk
 */
#define BIOS_LINKER_LOADER_FILESZ FW_CFG_MAX_FILE_PATH

struct BiosLinkerLoaderEntry {
    uint32_t command;
    union {
        /*
         * COMMAND_ALLOCATE - allocate a table from @alloc.file
         * subject to @alloc.align alignment (must be power of 2)
         * and @alloc.zone (can be HIGH or FSEG) requirements.
         *
         * Must appear exactly once for each file, and before
         * this file is referenced by any other command.
         */
        struct {
            char file[BIOS_LINKER_LOADER_FILESZ];
            uint32_t align;
            uint8_t zone;
        } alloc;

        /*
         * COMMAND_ADD_POINTER - patch the table (originating from
         * @dest_file) at @pointer.offset, by adding a pointer to the table
         * originating from @src_file. 1,2,4 or 8 byte unsigned
         * addition is used depending on @pointer.size.
         */
        struct {
            char dest_file[BIOS_LINKER_LOADER_FILESZ];
            char src_file[BIOS_LINKER_LOADER_FILESZ];
            uint32_t offset;
            uint8_t size;
        } pointer;

        /*
         * COMMAND_ADD_CHECKSUM - calculate checksum of the range specified by
         * @cksum_start and @cksum_length fields,
         * and then add the value at @cksum.offset.
         * Checksum simply sums -X for each byte X in the range
         * using 8-bit math.
         */
        struct {
            char file[BIOS_LINKER_LOADER_FILESZ];
            uint32_t offset;
            uint32_t start;
            uint32_t length;
        } cksum;

        /* padding */
        char pad[124];
    };
} QEMU_PACKED;
typedef struct BiosLinkerLoaderEntry BiosLinkerLoaderEntry;

enum {
    BIOS_LINKER_LOADER_COMMAND_ALLOCATE     = 0x1,
    BIOS_LINKER_LOADER_COMMAND_ADD_POINTER  = 0x2,
    BIOS_LINKER_LOADER_COMMAND_ADD_CHECKSUM = 0x3,
};

enum {
    BIOS_LINKER_LOADER_ALLOC_ZONE_HIGH = 0x1,
    BIOS_LINKER_LOADER_ALLOC_ZONE_FSEG = 0x2,
};

/*
 * BiosLinkerFileEntry:
 *
 * An internal type used for book-keeping file entries
 */
typedef struct BiosLinkerFileEntry {
    char *name; /* file name */
    GArray *blob; /* data accosiated with @name */
} BiosLinkerFileEntry;

/*
 * bios_linker_loader_init: allocate a new linker object instance.
 *
 * After initialization, linker commands can be added, and will
 * be stored in the linker.cmd_blob array.
 */
BIOSLinker *bios_linker_loader_init(void)
{
    BIOSLinker *linker = g_new(BIOSLinker, 1);

    linker->cmd_blob = g_array_new(false, true /* clear */, 1);
    linker->file_list = g_array_new(false, true /* clear */,
                                    sizeof(BiosLinkerFileEntry));
    return linker;
}

/* Free linker wrapper */
void bios_linker_loader_cleanup(BIOSLinker *linker)
{
    int i;
    BiosLinkerFileEntry *entry;

    g_array_free(linker->cmd_blob, true);

    for (i = 0; i < linker->file_list->len; i++) {
        entry = &g_array_index(linker->file_list, BiosLinkerFileEntry, i);
        g_free(entry->name);
    }
    g_array_free(linker->file_list, true);
    g_free(linker);
}

static const BiosLinkerFileEntry *
bios_linker_find_file(const BIOSLinker *linker, const char *name)
{
    int i;
    BiosLinkerFileEntry *entry;

    for (i = 0; i < linker->file_list->len; i++) {
        entry = &g_array_index(linker->file_list, BiosLinkerFileEntry, i);
        if (!strcmp(entry->name, name)) {
            return entry;
        }
    }
    return NULL;
}

/*
 * bios_linker_loader_alloc: ask guest to load file into guest memory.
 *
 * @linker: linker object instance
 * @file_name: name of the file blob to be loaded
 * @file_blob: pointer to blob corresponding to @file_name
 * @alloc_align: required minimal alignment in bytes. Must be a power of 2.
 * @alloc_fseg: request allocation in FSEG zone (useful for the RSDP ACPI table)
 *
 * Note: this command must precede any other linker command using this file.
 */
void bios_linker_loader_alloc(BIOSLinker *linker,
                              const char *file_name,
                              GArray *file_blob,
                              uint32_t alloc_align,
                              bool alloc_fseg)
{
    BiosLinkerLoaderEntry entry;
    BiosLinkerFileEntry file = { g_strdup(file_name), file_blob};

    assert(!(alloc_align & (alloc_align - 1)));

    assert(!bios_linker_find_file(linker, file_name));
    g_array_append_val(linker->file_list, file);

    memset(&entry, 0, sizeof entry);
    strncpy(entry.alloc.file, file_name, sizeof entry.alloc.file - 1);
    entry.command = cpu_to_le32(BIOS_LINKER_LOADER_COMMAND_ALLOCATE);
    entry.alloc.align = cpu_to_le32(alloc_align);
    entry.alloc.zone = alloc_fseg ? BIOS_LINKER_LOADER_ALLOC_ZONE_FSEG :
                                    BIOS_LINKER_LOADER_ALLOC_ZONE_HIGH;

    /* Alloc entries must come first, so prepend them */
    g_array_prepend_vals(linker->cmd_blob, &entry, sizeof entry);
}

/*
 * bios_linker_loader_add_checksum: ask guest to add checksum of file data
 * into (same) file at the specified pointer.
 *
 * Checksum calculation simply sums -X for each byte X in the range
 * using 8-bit math (i.e. ACPI checksum).
 *
 * @linker: linker object instance
 * @file: file that includes the checksum to be calculated
 *        and the data to be checksummed
 * @start, @size: range of data to checksum
 * @checksum: location of the checksum to be patched within file blob
 *
 * Notes:
 * - checksum byte initial value must have been pushed into blob
 *   associated with @file and reside at address @checksum.
 * - @size bytes must have been pushed into blob associated wtih @file
 *   and reside at address @start.
 * - Guest calculates checksum of specified range of data, result is added to
 *   initial value at @checksum into copy of @file in Guest memory.
 * - Range might include the checksum itself.
 * - To avoid confusion, caller must always put 0x0 at @checksum.
 * - @file must be loaded into Guest memory using bios_linker_loader_alloc
 */
void bios_linker_loader_add_checksum(BIOSLinker *linker, const char *file_name,
                                     void *start, unsigned size,
                                     uint8_t *checksum)
{
    BiosLinkerLoaderEntry entry;
    const BiosLinkerFileEntry *file = bios_linker_find_file(linker, file_name);
    ptrdiff_t checksum_offset = (gchar *)checksum - file->blob->data;
    ptrdiff_t start_offset = (gchar *)start - file->blob->data;

    assert(checksum_offset >= 0);
    assert(start_offset >= 0);
    assert(checksum_offset + 1 <= file->blob->len);
    assert(start_offset + size <= file->blob->len);
    assert(*checksum == 0x0);

    memset(&entry, 0, sizeof entry);
    strncpy(entry.cksum.file, file_name, sizeof entry.cksum.file - 1);
    entry.command = cpu_to_le32(BIOS_LINKER_LOADER_COMMAND_ADD_CHECKSUM);
    entry.cksum.offset = cpu_to_le32(checksum_offset);
    entry.cksum.start = cpu_to_le32(start_offset);
    entry.cksum.length = cpu_to_le32(size);

    g_array_append_vals(linker->cmd_blob, &entry, sizeof entry);
}

/*
 * bios_linker_loader_add_pointer: ask guest to add address of source file
 * into destination file at the specified pointer.
 *
 * @linker: linker object instance
 * @dest_file: destination file that must be changed
 * @src_file: source file who's address must be taken
 * @pointer: location of the pointer to be patched within destination file blob
 * @pointer_size: size of pointer to be patched, in bytes
 *
 * Notes:
 * - @pointer_size bytes must have been pushed into blob associated with
 *   @dest_file and reside at address @pointer.
 * - Guest address is added to initial value at @pointer
 *   into copy of @dest_file in Guest memory.
 *   e.g. to get start of src_file in guest memory, put 0x0 there
 *   to get address of a field at offset 0x10 in src_file, put 0x10 there
 * - Both @dest_file and @src_file must be
 *   loaded into Guest memory using bios_linker_loader_alloc
 */
void bios_linker_loader_add_pointer(BIOSLinker *linker,
                                    const char *dest_file,
                                    const char *src_file,
                                    void *pointer,
                                    uint8_t pointer_size)
{
    BiosLinkerLoaderEntry entry;
    const BiosLinkerFileEntry *file = bios_linker_find_file(linker, dest_file);
    ptrdiff_t offset = (gchar *)pointer - file->blob->data;

    assert(offset >= 0);
    assert(offset + pointer_size <= file->blob->len);

    memset(&entry, 0, sizeof entry);
    strncpy(entry.pointer.dest_file, dest_file,
            sizeof entry.pointer.dest_file - 1);
    strncpy(entry.pointer.src_file, src_file,
            sizeof entry.pointer.src_file - 1);
    entry.command = cpu_to_le32(BIOS_LINKER_LOADER_COMMAND_ADD_POINTER);
    entry.pointer.offset = cpu_to_le32(offset);
    entry.pointer.size = pointer_size;
    assert(pointer_size == 1 || pointer_size == 2 ||
           pointer_size == 4 || pointer_size == 8);

    g_array_append_vals(linker->cmd_blob, &entry, sizeof entry);
}
