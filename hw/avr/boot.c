/*
 * AVR loader helpers
 *
 * Copyright (c) 2019-2020 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/datadir.h"
#include "hw/loader.h"
#include "elf.h"
#include "boot.h"
#include "qemu/error-report.h"

static const char *avr_elf_e_flags_to_cpu_type(uint32_t flags)
{
    switch (flags & EF_AVR_MACH) {
    case bfd_mach_avr1:
        return AVR_CPU_TYPE_NAME("avr1");
    case bfd_mach_avr2:
        return AVR_CPU_TYPE_NAME("avr2");
    case bfd_mach_avr25:
        return AVR_CPU_TYPE_NAME("avr25");
    case bfd_mach_avr3:
        return AVR_CPU_TYPE_NAME("avr3");
    case bfd_mach_avr31:
        return AVR_CPU_TYPE_NAME("avr31");
    case bfd_mach_avr35:
        return AVR_CPU_TYPE_NAME("avr35");
    case bfd_mach_avr4:
        return AVR_CPU_TYPE_NAME("avr4");
    case bfd_mach_avr5:
        return AVR_CPU_TYPE_NAME("avr5");
    case bfd_mach_avr51:
        return AVR_CPU_TYPE_NAME("avr51");
    case bfd_mach_avr6:
        return AVR_CPU_TYPE_NAME("avr6");
    case bfd_mach_avrtiny:
        return AVR_CPU_TYPE_NAME("avrtiny");
    case bfd_mach_avrxmega2:
        return AVR_CPU_TYPE_NAME("xmega2");
    case bfd_mach_avrxmega3:
        return AVR_CPU_TYPE_NAME("xmega3");
    case bfd_mach_avrxmega4:
        return AVR_CPU_TYPE_NAME("xmega4");
    case bfd_mach_avrxmega5:
        return AVR_CPU_TYPE_NAME("xmega5");
    case bfd_mach_avrxmega6:
        return AVR_CPU_TYPE_NAME("xmega6");
    case bfd_mach_avrxmega7:
        return AVR_CPU_TYPE_NAME("xmega7");
    default:
        return NULL;
    }
}

bool avr_load_firmware(AVRCPU *cpu, MachineState *ms,
                       MemoryRegion *program_mr, const char *firmware)
{
    g_autofree char *filename = NULL;
    int bytes_loaded;
    uint64_t entry;
    uint32_t e_flags;

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, firmware);
    if (filename == NULL) {
        error_report("Unable to find %s", firmware);
        return false;
    }

    bytes_loaded = load_elf_ram_sym(filename,
                                    NULL, NULL, NULL,
                                    &entry, NULL, NULL,
                                    &e_flags, 0, EM_AVR, 0, 0,
                                    NULL, true, NULL);
    if (bytes_loaded >= 0) {
        /* If ELF file is provided, determine CPU type reading ELF e_flags. */
        const char *elf_cpu = avr_elf_e_flags_to_cpu_type(e_flags);
        const char *mcu_cpu_type = object_get_typename(OBJECT(cpu));
        int cpu_len = strlen(mcu_cpu_type) - strlen(AVR_CPU_TYPE_SUFFIX);

        if (entry) {
            error_report("BIOS entry_point must be 0x0000 "
                         "(ELF image '%s' has entry_point 0x%04" PRIx64 ")",
                         firmware, entry);
            return false;
        }
        if (!elf_cpu) {
            warn_report("Could not determine CPU type for ELF image '%s', "
                        "assuming '%.*s' CPU",
                         firmware, cpu_len, mcu_cpu_type);
            return true;
        }
        if (strcmp(elf_cpu, mcu_cpu_type)) {
            error_report("Current machine: %s with '%.*s' CPU",
                         MACHINE_GET_CLASS(ms)->desc, cpu_len, mcu_cpu_type);
            error_report("ELF image '%s' is for '%.*s' CPU",
                         firmware,
                         (int)(strlen(elf_cpu) - strlen(AVR_CPU_TYPE_SUFFIX)),
                         elf_cpu);
            return false;
        }
    } else {
        bytes_loaded = load_image_mr(filename, program_mr);
    }
    if (bytes_loaded < 0) {
        error_report("Unable to load firmware image %s as ELF or raw binary",
                     firmware);
        return false;
    }
    return true;
}
