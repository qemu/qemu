/*
 * x86 bios loading functions
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2019, 2024-2026 Red Hat, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "qemu/datadir.h"
#include "qapi/error.h"
#include "qemu/units.h"

#include "hw/core/loader.h"
#include "hw/i386/x86.h"
#include "kvm/tdx.h"
#include "target/i386/sev.h"

void x86_isa_bios_init(MemoryRegion *isa_bios, MemoryRegion *isa_memory,
                       MemoryRegion *bios, bool read_only)
{
    uint64_t bios_size = memory_region_size(bios);
    uint64_t isa_bios_size = MIN(bios_size, 128 * KiB);

    memory_region_init_alias(isa_bios, NULL, "isa-bios", bios,
                             bios_size - isa_bios_size, isa_bios_size);
    memory_region_add_subregion_overlap(isa_memory, 1 * MiB - isa_bios_size,
                                        isa_bios, 1);
    memory_region_set_readonly(isa_bios, read_only);
}

static int get_bios_size(X86MachineState *x86ms,
                         const char *bios_name, char *filename)
{
    int bios_size;

    if (filename) {
        bios_size = get_image_size(filename, NULL);
    } else {
        bios_size = -1;
    }
    if (bios_size <= 0 ||
        (bios_size % 65536) != 0) {
        goto bios_error;
    }

    return bios_size;

 bios_error:
    fprintf(stderr, "qemu: could not load PC BIOS '%s'\n", bios_name);
    exit(1);
}

static void load_bios_from_file(X86MachineState *x86ms, const char *bios_name,
                                char *filename, int bios_size,
                                bool isapc_ram_fw)
{
    ssize_t ret;

    /* BIOS load */
    if (machine_require_guest_memfd_private(MACHINE(x86ms))) {
        memory_region_init_ram_guest_memfd_private(
            &x86ms->bios, NULL, "pc.bios", bios_size, &error_fatal);
        if (is_tdx_vm()) {
            tdx_set_tdvf_region(&x86ms->bios);
        }
    } else {
        memory_region_init_ram(&x86ms->bios, NULL, "pc.bios",
                               bios_size, &error_fatal);
    }
    if (sev_enabled() || is_tdx_vm()) {
        /*
         * The concept of a "reset" simply doesn't exist for
         * confidential computing guests, we have to destroy and
         * re-launch them instead.  So there is no need to register
         * the firmware as rom to properly re-initialize on reset.
         * Just go for a straight file load instead.
         */
        void *ptr = memory_region_get_ram_ptr(&x86ms->bios);
        load_image_size(filename, ptr, bios_size);
        x86_firmware_configure(0x100000000ULL - bios_size, ptr, bios_size);
    } else {
        memory_region_set_readonly(&x86ms->bios, !isapc_ram_fw);
        ret = rom_add_file_fixed(bios_name, (uint32_t)(-bios_size), -1);
        if (ret != 0) {
            goto bios_error;
        }
    }

    return;

 bios_error:
    fprintf(stderr, "qemu: could not load PC BIOS '%s'\n", bios_name);
    exit(1);
}

void x86_bios_rom_reload(X86MachineState *x86ms)
{
    int bios_size;
    const char *bios_name;
    char *filename;

    if (memory_region_size(&x86ms->bios) == 0) {
        /* if -bios is not used */
        return;
    }

    bios_name = MACHINE(x86ms)->firmware ?: "bios.bin";
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);

    bios_size = get_bios_size(x86ms, bios_name, filename);

    void *ptr = memory_region_get_ram_ptr(&x86ms->bios);
    load_image_size(filename, ptr, bios_size);
    x86_firmware_configure(0x100000000ULL - bios_size, ptr, bios_size);
}

void x86_bios_rom_init(X86MachineState *x86ms, const char *default_firmware,
                       MemoryRegion *rom_memory, bool isapc_ram_fw)
{
    int bios_size;
    const char *bios_name;
    g_autofree char *filename;

    bios_name = MACHINE(x86ms)->firmware ?: default_firmware;
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);

    bios_size = get_bios_size(x86ms, bios_name, filename);
    load_bios_from_file(x86ms, bios_name, filename, bios_size, isapc_ram_fw);

    if (!machine_require_guest_memfd_private(MACHINE(x86ms))) {
        /* map the last 128KB of the BIOS in ISA space */
        x86_isa_bios_init(&x86ms->isa_bios, rom_memory, &x86ms->bios,
                          !isapc_ram_fw);
    }

    /* map all the bios at the top of memory */
    memory_region_add_subregion(rom_memory,
                                (uint32_t)(-bios_size),
                                &x86ms->bios);
    return;
}
