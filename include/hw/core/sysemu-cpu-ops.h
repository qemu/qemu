/*
 * CPU operations specific to system emulation
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef SYSTEM_CPU_OPS_H
#define SYSTEM_CPU_OPS_H

#include "hw/core/cpu.h"

/*
 * struct SysemuCPUOps: System operations specific to a CPU class
 */
typedef struct SysemuCPUOps {
    /**
     * @has_work: Callback for checking if there is work to do.
     */
    bool (*has_work)(CPUState *cpu); /* MANDATORY NON-NULL */
    /**
     * @get_memory_mapping: Callback for obtaining the memory mappings.
     */
    bool (*get_memory_mapping)(CPUState *cpu, MemoryMappingList *list,
                               Error **errp);
    /**
     * @get_paging_enabled: Callback for inquiring whether paging is enabled.
     */
    bool (*get_paging_enabled)(const CPUState *cpu);
    /**
     * @get_phys_addr_debug: Callback for obtaining a physical address.
     * This must be able to handle a non-page-aligned address, and will
     * return the physical address corresponding to that address.
     *
     * CPUs should prefer to implement translate_for_debug instead of
     * this (and must do so if their translations are not always valid
     * for a complete target page or they use memory attributes).
     */
    hwaddr (*get_phys_addr_debug)(CPUState *cpu, vaddr addr);
    /**
     * @translate_for_debug: Callback for translating a virtual address into
     * a physical address for debug purposes.
     * The implementation should fill in @result with the physical address,
     * transaction attributes, and log2 of the size of the aligned block of
     * memory that the translation is valid for.
     * This must be able to handle a non-page-aligned address, and will
     * return the physical address corresponding to that address.
     * The attributes must include the debug flag being set.
     * Returns false on translation failure; on success returns true and
     * fills in @result.
     *
     * This is the preferred method to implement for new CPUs.
     */
    bool (*translate_for_debug)(CPUState *cpu, vaddr addr,
                                TranslateForDebugResult *result);
    /**
     * @asidx_from_attrs: Callback to return the CPU AddressSpace to use for
     *       a memory access with the specified memory transaction attributes.
     */
    int (*asidx_from_attrs)(CPUState *cpu, MemTxAttrs attrs);
    /**
     * @get_crash_info: Callback for reporting guest crash information in
     * GUEST_PANICKED events.
     */
    GuestPanicInformation* (*get_crash_info)(CPUState *cpu);
    /**
     * @write_elf32_note: Callback for writing a CPU-specific ELF note to a
     * 32-bit VM coredump.
     */
    int (*write_elf32_note)(WriteCoreDumpFunction f, CPUState *cpu,
                            int cpuid, DumpState *s);
    /**
     * @write_elf64_note: Callback for writing a CPU-specific ELF note to a
     * 64-bit VM coredump.
     */
    int (*write_elf64_note)(WriteCoreDumpFunction f, CPUState *cpu,
                            int cpuid, DumpState *s);
    /**
     * @write_elf32_qemunote: Callback for writing a CPU- and QEMU-specific ELF
     * note to a 32-bit VM coredump.
     */
    int (*write_elf32_qemunote)(WriteCoreDumpFunction f, CPUState *cpu,
                                DumpState *s);
    /**
     * @write_elf64_qemunote: Callback for writing a CPU- and QEMU-specific ELF
     * note to a 64-bit VM coredump.
     */
    int (*write_elf64_qemunote)(WriteCoreDumpFunction f, CPUState *cpu,
                                DumpState *s);
    /**
     * @internal_is_big_endian: Callback to return %true if a CPU which supports
     * runtime configurable endianness is currently big-endian.
     * Non-configurable CPUs can use the default implementation of this method.
     * This method should not be used by any callers other than the pre-1.0
     * virtio devices and the semihosting interface.
     */
    bool (*internal_is_big_endian)(CPUState *cpu);

    /**
     * @monitor_get_register: Callback to fill @pval with register @name value.
     *                        This field is legacy, use @gdb_core_xml_file
     *                        to dump registers instead.
     * Returns: 0 on success or negative errno on failure.
     */
    int (*monitor_get_register)(CPUState *cs, const char *name, int64_t *pval);

    /**
     * @monitor_defs: Array of MonitorDef entries. This field is legacy,
     *                use @gdb_core_xml_file to dump registers instead.
     */
    const MonitorDef *monitor_defs;

    /**
     * @legacy_vmsd: Legacy state for migration.
     *               Do not use in new targets, use #DeviceClass::vmsd instead.
     */
    const VMStateDescription *legacy_vmsd;

} SysemuCPUOps;

#endif /* SYSTEM_CPU_OPS_H */
