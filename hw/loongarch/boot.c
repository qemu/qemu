/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch boot helper functions.
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "target/loongarch/cpu.h"
#include "hw/loongarch/virt.h"
#include "hw/loader.h"
#include "elf.h"
#include "qemu/error-report.h"
#include "system/reset.h"
#include "system/qtest.h"

/*
 * Linux Image Format
 * https://docs.kernel.org/arch/loongarch/booting.html
 */
#define LINUX_PE_MAGIC  0x818223cd
#define MZ_MAGIC        0x5a4d /* "MZ" */

struct loongarch_linux_hdr {
    uint32_t mz_magic;
    uint32_t res0;
    uint64_t kernel_entry;
    uint64_t kernel_size;
    uint64_t load_offset;
    uint64_t res1;
    uint64_t res2;
    uint64_t res3;
    uint32_t linux_pe_magic;
    uint32_t pe_header_offset;
} QEMU_PACKED;

static const unsigned int aux_boot_code[] = {
                  /* Configure reset ebase.                    */
    0x0400302c,   /* csrwr      $t0, LOONGARCH_CSR_EENTRY      */

                  /* Disable interrupt.                        */
    0x0380100c,   /* ori        $t0, $zero,0x4                 */
    0x04000180,   /* csrxchg    $zero, $t0, LOONGARCH_CSR_CRMD */
    0x03400000,   /* nop                                       */

    0x0400800c,   /* csrrd      $t0, LOONGARCH_CSR_CPUNUM      */
    0x034ffd8c,   /* andi       $t0, $t0, 0x3ff                */
    0x0015000d,   /* move       $t1, $zero                     */
    0x5800718d,   /* beq        $t0, $t1, 112                  */

                  /* Clear mailbox.                            */
    0x1400002d,   /* lu12i.w    $t1, 1(0x1)                    */
    0x038081ad,   /* ori        $t1, $t1, CORE_BUF_20  */
    0x06481da0,   /* iocsrwr.d  $zero, $t1                     */

                  /* Enable IPI interrupt.                     */
    0x1400002c,   /* lu12i.w    $t0, 1(0x1)                    */
    0x0400118c,   /* csrxchg    $t0, $t0, LOONGARCH_CSR_ECFG   */
    0x02fffc0c,   /* addi.d     $t0, $r0,-1(0xfff)             */
    0x1400002d,   /* lu12i.w    $t1, 1(0x1)                    */
    0x038011ad,   /* ori        $t1, $t1, CORE_EN_OFF          */
    0x064819ac,   /* iocsrwr.w  $t0, $t1                       */
    0x1400002d,   /* lu12i.w    $t1, 1(0x1)                    */
    0x038081ad,   /* ori        $t1, $t1, CORE_BUF_20          */

                  /* Wait for wakeup  <.L11>:                  */
    0x06488000,   /* idle       0x0                            */
    0x03400000,   /* andi       $zero, $zero, 0x0              */
    0x064809ac,   /* iocsrrd.w  $t0, $t1                       */
    0x43fff59f,   /* beqz       $t0, -12(0x7ffff4) # 48 <.L11> */

                  /* Read and clear IPI interrupt.             */
    0x1400002d,   /* lu12i.w    $t1, 1(0x1)                    */
    0x064809ac,   /* iocsrrd.w  $t0, $t1                       */
    0x1400002d,   /* lu12i.w    $t1, 1(0x1)                    */
    0x038031ad,   /* ori        $t1, $t1, CORE_CLEAR_OFF       */
    0x064819ac,   /* iocsrwr.w  $t0, $t1                       */

                  /* Disable  IPI interrupt.                   */
    0x1400002c,   /* lu12i.w    $t0, 1(0x1)                    */
    0x04001180,   /* csrxchg    $zero, $t0, LOONGARCH_CSR_ECFG */

                  /* Read mail buf and jump to specified entry */
    0x1400002d,   /* lu12i.w    $t1, 1(0x1)                    */
    0x038081ad,   /* ori        $t1, $t1, CORE_BUF_20          */
    0x06480dac,   /* iocsrrd.d  $t0, $t1                       */
    0x00150181,   /* move       $ra, $t0                       */
    0x4c000020,   /* jirl       $zero, $ra,0                   */
                  /* BSP Core                                  */
    0x03400000,   /* nop                                       */
    0x1800000d,   /* pcaddi     $t1, 0                         */
    0x28c0a1a4,   /* ld.d       $a0, $t1, 40                   */
    0x1800000d,   /* pcaddi     $t1, 0                         */
    0x28c0a1a5,   /* ld.d       $a1, $t1, 40                   */
    0x1800000d,   /* pcaddi     $t1, 0                         */
    0x28c0a1a6,   /* ld.d       $a2, $t1, 40                   */
    0x1800000d,   /* pcaddi     $t1, 0                         */
    0x28c0a1ac,   /* ld.d       $t0, $t1, 40                   */
    0x00150181,   /* move       $ra, $t0                       */
    0x4c000020,   /* jirl       $zero, $ra,0                   */
    0x00000000,   /* .dword 0   A0                             */
    0x00000000,
    0x00000000,   /* .dword 0   A1                             */
    0x00000000,
    0x00000000,   /* .dword 0   A2                             */
    0x00000000,
    0x00000000,   /* .dword 0   PC                             */
    0x00000000,
};

static inline void *guidcpy(void *dst, const void *src)
{
    return memcpy(dst, src, sizeof(efi_guid_t));
}

static void init_efi_boot_memmap(MachineState *ms,
                                 struct efi_system_table *systab,
                                 void *p, void *start)
{
    unsigned i;
    struct efi_boot_memmap *boot_memmap = p;
    efi_guid_t tbl_guid = LINUX_EFI_BOOT_MEMMAP_GUID;
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(ms);
    struct memmap_entry *memmap_table;
    unsigned int memmap_entries;

    /* efi_configuration_table 1 */
    guidcpy(&systab->tables[0].guid, &tbl_guid);
    systab->tables[0].table = (struct efi_configuration_table *)(p - start);
    systab->nr_tables = 1;

    boot_memmap->desc_size = sizeof(efi_memory_desc_t);
    boot_memmap->desc_ver = 1;
    boot_memmap->map_size = 0;

    efi_memory_desc_t *map = p + sizeof(struct efi_boot_memmap);
    memmap_table = lvms->memmap_table;
    memmap_entries = lvms->memmap_entries;
    for (i = 0; i < memmap_entries; i++) {
        map = (void *)boot_memmap + sizeof(*map);
        map[i].type = memmap_table[i].type;
        map[i].phys_addr = ROUND_UP(memmap_table[i].address, 64 * KiB);
        map[i].num_pages = ROUND_DOWN(memmap_table[i].address +
                        memmap_table[i].length - map[i].phys_addr, 64 * KiB);
        p += sizeof(efi_memory_desc_t);
    }
}

static void init_efi_initrd_table(struct loongarch_boot_info *info,
                                  struct efi_system_table *systab,
                                  void *p, void *start)
{
    efi_guid_t tbl_guid = LINUX_EFI_INITRD_MEDIA_GUID;
    struct efi_initrd *initrd_table  = p;

    /* efi_configuration_table 2 */
    guidcpy(&systab->tables[1].guid, &tbl_guid);
    systab->tables[1].table = (struct efi_configuration_table *)(p - start);
    systab->nr_tables = 2;

    initrd_table->base = info->initrd_addr;
    initrd_table->size = info->initrd_size;
}

static void init_efi_fdt_table(struct efi_system_table *systab)
{
    efi_guid_t tbl_guid = DEVICE_TREE_GUID;

    /* efi_configuration_table 3 */
    guidcpy(&systab->tables[2].guid, &tbl_guid);
    systab->tables[2].table = (void *)FDT_BASE;
    systab->nr_tables = 3;
}

static void init_systab(MachineState *ms,
                        struct loongarch_boot_info *info, void *p, void *start)
{
    void *bp_tables_start;
    struct efi_system_table *systab = p;
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(ms);

    info->a2 = p - start;

    systab->hdr.signature = EFI_SYSTEM_TABLE_SIGNATURE;
    systab->hdr.revision = EFI_SPECIFICATION_VERSION;
    systab->hdr.revision = sizeof(struct efi_system_table),
    systab->fw_revision = FW_VERSION << 16 | FW_PATCHLEVEL << 8;
    systab->runtime = 0;
    systab->boottime = 0;
    systab->nr_tables = 0;

    p += ROUND_UP(sizeof(struct efi_system_table), 64 * KiB);

    systab->tables = p;
    bp_tables_start = p;

    init_efi_boot_memmap(ms, systab, p, start);
    p += ROUND_UP(sizeof(struct efi_boot_memmap) +
                  sizeof(efi_memory_desc_t) * lvms->memmap_entries, 64 * KiB);
    init_efi_initrd_table(info, systab, p, start);
    p += ROUND_UP(sizeof(struct efi_initrd), 64 * KiB);
    init_efi_fdt_table(systab);

    systab->tables = (struct efi_configuration_table *)(bp_tables_start - start);
}

static void init_cmdline(struct loongarch_boot_info *info, void *p, void *start)
{
    hwaddr cmdline_addr = p - start;

    info->a0 = 1;
    info->a1 = cmdline_addr;

    g_strlcpy(p, info->kernel_cmdline, COMMAND_LINE_SIZE);
}

static uint64_t cpu_loongarch_virt_to_phys(void *opaque, uint64_t addr)
{
    return addr & MAKE_64BIT_MASK(0, TARGET_PHYS_ADDR_SPACE_BITS);
}

static int64_t load_loongarch_linux_image(const char *filename,
                                          uint64_t *kernel_entry,
                                          uint64_t *kernel_low,
                                          uint64_t *kernel_high)
{
    gsize len;
    ssize_t size;
    uint8_t *buffer;
    struct loongarch_linux_hdr *hdr;

    /* Load as raw file otherwise */
    if (!g_file_get_contents(filename, (char **)&buffer, &len, NULL)) {
        return -1;
    }
    size = len;

    /* Unpack the image if it is a EFI zboot image */
    if (unpack_efi_zboot_image(&buffer, &size) < 0) {
        g_free(buffer);
        return -1;
    }

    hdr = (struct loongarch_linux_hdr *)buffer;

    if (extract32(le32_to_cpu(hdr->mz_magic), 0, 16) != MZ_MAGIC ||
        le32_to_cpu(hdr->linux_pe_magic) != LINUX_PE_MAGIC) {
        g_free(buffer);
        return -1;
    }

    /* Early kernel versions may have those fields in virtual address */
    *kernel_entry = extract64(le64_to_cpu(hdr->kernel_entry),
                              0, TARGET_PHYS_ADDR_SPACE_BITS);
    *kernel_low = extract64(le64_to_cpu(hdr->load_offset),
                            0, TARGET_PHYS_ADDR_SPACE_BITS);
    *kernel_high = *kernel_low + size;

    rom_add_blob_fixed(filename, buffer, size, *kernel_low);

    g_free(buffer);

    return size;
}

static ram_addr_t alloc_initrd_memory(struct loongarch_boot_info *info,
                uint64_t advice_start, ssize_t rd_size)
{
    hwaddr base, ram_size, gap, low_end;
    ram_addr_t initrd_end, initrd_start;

    base = VIRT_LOWMEM_BASE;
    gap = VIRT_LOWMEM_SIZE;
    initrd_start = advice_start;
    initrd_end = initrd_start + rd_size;

    ram_size = info->ram_size;
    low_end = base + MIN(ram_size, gap);
    if (initrd_end <= low_end) {
        return initrd_start;
    }

    if (ram_size <= gap) {
        error_report("The low memory too small for initial ram disk '%s',"
             "You need to expand the ram",
             info->initrd_filename);
        exit(1);
    }

    /*
     * Try to load initrd in the high memory
     */
    ram_size -= gap;
    initrd_start = VIRT_HIGHMEM_BASE;
    if (rd_size <= ram_size) {
        return initrd_start;
    }

    error_report("The high memory too small for initial ram disk '%s',"
         "You need to expand the ram",
         info->initrd_filename);
    exit(1);
}

static int64_t load_kernel_info(struct loongarch_boot_info *info)
{
    uint64_t kernel_entry, kernel_low, kernel_high, initrd_offset = 0;
    ssize_t kernel_size;

    kernel_size = load_elf(info->kernel_filename, NULL,
                           cpu_loongarch_virt_to_phys, NULL,
                           &kernel_entry, &kernel_low,
                           &kernel_high, NULL, ELFDATA2LSB,
                           EM_LOONGARCH, 1, 0);
    kernel_entry = cpu_loongarch_virt_to_phys(NULL, kernel_entry);
    if (kernel_size < 0) {
        kernel_size = load_loongarch_linux_image(info->kernel_filename,
                                                 &kernel_entry, &kernel_low,
                                                 &kernel_high);
    }

    if (kernel_size < 0) {
        error_report("could not load kernel '%s': %s",
                     info->kernel_filename,
                     load_elf_strerror(kernel_size));
        exit(1);
    }

    if (info->initrd_filename) {
        ssize_t initrd_size = get_image_size(info->initrd_filename, NULL);
        if (initrd_size > 0) {
            initrd_offset = ROUND_UP(kernel_high + 4 * kernel_size, 64 * KiB);
            initrd_offset = alloc_initrd_memory(info, initrd_offset,
                                                initrd_size);
            initrd_size = load_image_targphys(info->initrd_filename,
                                              initrd_offset, initrd_size, NULL);
        }

        if (initrd_size == -1) {
            error_report("could not load initial ram disk '%s'",
                         info->initrd_filename);
            exit(1);
        }

        info->initrd_addr = initrd_offset;
        info->initrd_size = initrd_size;
    }

    return kernel_entry;
}

static void fw_cfg_add_kernel_info(struct loongarch_boot_info *info,
                                   FWCfgState *fw_cfg)
{
    /*
     * Expose the kernel, the command line, and the initrd in fw_cfg.
     * We don't process them here at all, it's all left to the
     * firmware.
     */
    load_image_to_fw_cfg(fw_cfg,
                         FW_CFG_KERNEL_SIZE, FW_CFG_KERNEL_DATA,
                         info->kernel_filename,
                         false);

    if (info->initrd_filename) {
        load_image_to_fw_cfg(fw_cfg,
                             FW_CFG_INITRD_SIZE, FW_CFG_INITRD_DATA,
                             info->initrd_filename, false);
    }

    if (info->kernel_cmdline) {
        fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE,
                       strlen(info->kernel_cmdline) + 1);
        fw_cfg_add_string(fw_cfg, FW_CFG_CMDLINE_DATA,
                          info->kernel_cmdline);
    }
}

static void loongarch_firmware_boot(LoongArchVirtMachineState *lvms,
                                    struct loongarch_boot_info *info)
{
    fw_cfg_add_kernel_info(info, lvms->fw_cfg);
}

static void init_boot_rom(MachineState *ms,
                          struct loongarch_boot_info *info, void *p)
{
    void *start = p;

    init_cmdline(info, p, start);
    p += COMMAND_LINE_SIZE;

    init_systab(ms, info, p, start);
}

static void loongarch_direct_kernel_boot(MachineState *ms,
                                         struct loongarch_boot_info *info)
{
    void *p, *bp;
    int64_t kernel_addr = VIRT_FLASH0_BASE;
    uint64_t *data;

    if (info->kernel_filename) {
        kernel_addr = load_kernel_info(info);
    } else {
        if (!qtest_enabled()) {
            warn_report("No kernel provided, booting from flash drive.");
        }
    }

    /* Load cmdline and system tables at [0 - 1 MiB] */
    p = g_malloc0(1 * MiB);
    bp = p;
    init_boot_rom(ms, info, p);
    rom_add_blob_fixed_as("boot_info", bp, 1 * MiB, 0, &address_space_memory);

    /* Load slave boot code at pflash0 . */
    void *boot_code = g_malloc0(VIRT_FLASH0_SIZE);
    memcpy(boot_code, &aux_boot_code, sizeof(aux_boot_code));
    data = (uint64_t *)(boot_code + sizeof(aux_boot_code));
    *(data - 4) = cpu_to_le64(info->a0);
    *(data - 3) = cpu_to_le64(info->a1);
    *(data - 2) = cpu_to_le64(info->a2);
    *(data - 1) = cpu_to_le64(kernel_addr);
    rom_add_blob_fixed("boot_code", boot_code, VIRT_FLASH0_SIZE, VIRT_FLASH0_BASE);

    g_free(boot_code);
    g_free(bp);
}

void loongarch_load_kernel(MachineState *ms, struct loongarch_boot_info *info)
{
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(ms);

    info->kernel_filename = ms->kernel_filename;
    info->kernel_cmdline = ms->kernel_cmdline;
    info->initrd_filename = ms->initrd_filename;

    if (lvms->bios_loaded) {
        loongarch_firmware_boot(lvms, info);
    } else {
        loongarch_direct_kernel_boot(ms, info);
    }
}
