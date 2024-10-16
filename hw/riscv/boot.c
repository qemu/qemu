/*
 * QEMU RISC-V Boot Helper
 *
 * Copyright (c) 2017 SiFive, Inc.
 * Copyright (c) 2019 Alistair Francis <alistair.francis@wdc.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/datadir.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "exec/cpu-defs.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/boot_opensbi.h"
#include "elf.h"
#include "sysemu/device_tree.h"
#include "sysemu/qtest.h"
#include "sysemu/kvm.h"
#include "sysemu/reset.h"

#include <libfdt.h>

bool riscv_is_32bit(RISCVHartArrayState *harts)
{
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(&harts->harts[0]);
    return mcc->misa_mxl_max == MXL_RV32;
}

/*
 * Return the per-socket PLIC hart topology configuration string
 * (caller must free with g_free())
 */
char *riscv_plic_hart_config_string(int hart_count)
{
    g_autofree const char **vals = g_new(const char *, hart_count + 1);
    int i;

    for (i = 0; i < hart_count; i++) {
        CPUState *cs = qemu_get_cpu(i);
        CPURISCVState *env = &RISCV_CPU(cs)->env;

        if (kvm_enabled()) {
            vals[i] = "S";
        } else if (riscv_has_ext(env, RVS)) {
            vals[i] = "MS";
        } else {
            vals[i] = "M";
        }
    }
    vals[i] = NULL;

    /* g_strjoinv() obliges us to cast away const here */
    return g_strjoinv(",", (char **)vals);
}

target_ulong riscv_calc_kernel_start_addr(RISCVHartArrayState *harts,
                                          target_ulong firmware_end_addr) {
    if (riscv_is_32bit(harts)) {
        return QEMU_ALIGN_UP(firmware_end_addr, 4 * MiB);
    } else {
        return QEMU_ALIGN_UP(firmware_end_addr, 2 * MiB);
    }
}

const char *riscv_default_firmware_name(RISCVHartArrayState *harts)
{
    if (riscv_is_32bit(harts)) {
        return RISCV32_BIOS_BIN;
    }

    return RISCV64_BIOS_BIN;
}

static char *riscv_find_bios(const char *bios_filename)
{
    char *filename;

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_filename);
    if (filename == NULL) {
        if (!qtest_enabled()) {
            /*
             * We only ship OpenSBI binary bios images in the QEMU source.
             * For machines that use images other than the default bios,
             * running QEMU test will complain hence let's suppress the error
             * report for QEMU testing.
             */
            error_report("Unable to find the RISC-V BIOS \"%s\"",
                         bios_filename);
            exit(1);
        }
    }

    return filename;
}

char *riscv_find_firmware(const char *firmware_filename,
                          const char *default_machine_firmware)
{
    char *filename = NULL;

    if ((!firmware_filename) || (!strcmp(firmware_filename, "default"))) {
        /*
         * The user didn't specify -bios, or has specified "-bios default".
         * That means we are going to load the OpenSBI binary included in
         * the QEMU source.
         */
        filename = riscv_find_bios(default_machine_firmware);
    } else if (strcmp(firmware_filename, "none")) {
        filename = riscv_find_bios(firmware_filename);
    }

    return filename;
}

target_ulong riscv_find_and_load_firmware(MachineState *machine,
                                          const char *default_machine_firmware,
                                          hwaddr *firmware_load_addr,
                                          symbol_fn_t sym_cb)
{
    char *firmware_filename;
    target_ulong firmware_end_addr = *firmware_load_addr;

    firmware_filename = riscv_find_firmware(machine->firmware,
                                            default_machine_firmware);

    if (firmware_filename) {
        /* If not "none" load the firmware */
        firmware_end_addr = riscv_load_firmware(firmware_filename,
                                                firmware_load_addr, sym_cb);
        g_free(firmware_filename);
    }

    return firmware_end_addr;
}

target_ulong riscv_load_firmware(const char *firmware_filename,
                                 hwaddr *firmware_load_addr,
                                 symbol_fn_t sym_cb)
{
    uint64_t firmware_entry, firmware_end;
    ssize_t firmware_size;

    g_assert(firmware_filename != NULL);

    if (load_elf_ram_sym(firmware_filename, NULL, NULL, NULL,
                         &firmware_entry, NULL, &firmware_end, NULL,
                         0, EM_RISCV, 1, 0, NULL, true, sym_cb) > 0) {
        *firmware_load_addr = firmware_entry;
        return firmware_end;
    }

    firmware_size = load_image_targphys_as(firmware_filename,
                                           *firmware_load_addr,
                                           current_machine->ram_size, NULL);

    if (firmware_size > 0) {
        return *firmware_load_addr + firmware_size;
    }

    error_report("could not load firmware '%s'", firmware_filename);
    exit(1);
}

static void riscv_load_initrd(MachineState *machine, uint64_t kernel_entry)
{
    const char *filename = machine->initrd_filename;
    uint64_t mem_size = machine->ram_size;
    void *fdt = machine->fdt;
    hwaddr start, end;
    ssize_t size;

    g_assert(filename != NULL);

    /*
     * We want to put the initrd far enough into RAM that when the
     * kernel is uncompressed it will not clobber the initrd. However
     * on boards without much RAM we must ensure that we still leave
     * enough room for a decent sized initrd, and on boards with large
     * amounts of RAM, we put the initrd at 512MB to allow large kernels
     * to boot.
     * So for boards with less than 1GB of RAM we put the initrd
     * halfway into RAM, and for boards with 1GB of RAM or more we put
     * the initrd at 512MB.
     */
    start = kernel_entry + MIN(mem_size / 2, 512 * MiB);

    size = load_ramdisk(filename, start, mem_size - start);
    if (size == -1) {
        size = load_image_targphys(filename, start, mem_size - start);
        if (size == -1) {
            error_report("could not load ramdisk '%s'", filename);
            exit(1);
        }
    }

    /* Some RISC-V machines (e.g. opentitan) don't have a fdt. */
    if (fdt) {
        end = start + size;
        qemu_fdt_setprop_u64(fdt, "/chosen", "linux,initrd-start", start);
        qemu_fdt_setprop_u64(fdt, "/chosen", "linux,initrd-end", end);
    }
}

target_ulong riscv_load_kernel(MachineState *machine,
                               RISCVHartArrayState *harts,
                               target_ulong kernel_start_addr,
                               bool load_initrd,
                               symbol_fn_t sym_cb)
{
    const char *kernel_filename = machine->kernel_filename;
    uint64_t kernel_load_base, kernel_entry;
    void *fdt = machine->fdt;

    g_assert(kernel_filename != NULL);

    /*
     * NB: Use low address not ELF entry point to ensure that the fw_dynamic
     * behaviour when loading an ELF matches the fw_payload, fw_jump and BBL
     * behaviour, as well as fw_dynamic with a raw binary, all of which jump to
     * the (expected) load address load address. This allows kernels to have
     * separate SBI and ELF entry points (used by FreeBSD, for example).
     */
    if (load_elf_ram_sym(kernel_filename, NULL, NULL, NULL,
                         NULL, &kernel_load_base, NULL, NULL, 0,
                         EM_RISCV, 1, 0, NULL, true, sym_cb) > 0) {
        kernel_entry = kernel_load_base;
        goto out;
    }

    if (load_uimage_as(kernel_filename, &kernel_entry, NULL, NULL,
                       NULL, NULL, NULL) > 0) {
        goto out;
    }

    if (load_image_targphys_as(kernel_filename, kernel_start_addr,
                               current_machine->ram_size, NULL) > 0) {
        kernel_entry = kernel_start_addr;
        goto out;
    }

    error_report("could not load kernel '%s'", kernel_filename);
    exit(1);

out:
    /*
     * For 32 bit CPUs 'kernel_entry' can be sign-extended by
     * load_elf_ram_sym().
     */
    if (riscv_is_32bit(harts)) {
        kernel_entry = extract64(kernel_entry, 0, 32);
    }

    if (load_initrd && machine->initrd_filename) {
        riscv_load_initrd(machine, kernel_entry);
    }

    if (fdt && machine->kernel_cmdline && *machine->kernel_cmdline) {
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
                                machine->kernel_cmdline);
    }

    return kernel_entry;
}

/*
 * This function makes an assumption that the DRAM interval
 * 'dram_base' + 'dram_size' is contiguous.
 *
 * Considering that 'dram_end' is the lowest value between
 * the end of the DRAM block and MachineState->ram_size, the
 * FDT location will vary according to 'dram_base':
 *
 * - if 'dram_base' is less that 3072 MiB, the FDT will be
 * put at the lowest value between 3072 MiB and 'dram_end';
 *
 * - if 'dram_base' is higher than 3072 MiB, the FDT will be
 * put at 'dram_end'.
 *
 * The FDT is fdt_packed() during the calculation.
 */
uint64_t riscv_compute_fdt_addr(hwaddr dram_base, hwaddr dram_size,
                                MachineState *ms)
{
    int ret = fdt_pack(ms->fdt);
    hwaddr dram_end, temp;
    int fdtsize;

    /* Should only fail if we've built a corrupted tree */
    g_assert(ret == 0);

    fdtsize = fdt_totalsize(ms->fdt);
    if (fdtsize <= 0) {
        error_report("invalid device-tree");
        exit(1);
    }

    /*
     * A dram_size == 0, usually from a MemMapEntry[].size element,
     * means that the DRAM block goes all the way to ms->ram_size.
     */
    dram_end = dram_base;
    dram_end += dram_size ? MIN(ms->ram_size, dram_size) : ms->ram_size;

    /*
     * We should put fdt as far as possible to avoid kernel/initrd overwriting
     * its content. But it should be addressable by 32 bit system as well.
     * Thus, put it at an 2MB aligned address that less than fdt size from the
     * end of dram or 3GB whichever is lesser.
     */
    temp = (dram_base < 3072 * MiB) ? MIN(dram_end, 3072 * MiB) : dram_end;

    return QEMU_ALIGN_DOWN(temp - fdtsize, 2 * MiB);
}

/*
 * 'fdt_addr' is received as hwaddr because boards might put
 * the FDT beyond 32-bit addressing boundary.
 */
void riscv_load_fdt(hwaddr fdt_addr, void *fdt)
{
    uint32_t fdtsize = fdt_totalsize(fdt);

    /* copy in the device tree */
    qemu_fdt_dumpdtb(fdt, fdtsize);

    rom_add_blob_fixed_as("fdt", fdt, fdtsize, fdt_addr,
                          &address_space_memory);
    qemu_register_reset_nosnapshotload(qemu_fdt_randomize_seeds,
                        rom_ptr_for_as(&address_space_memory, fdt_addr, fdtsize));
}

void riscv_rom_copy_firmware_info(MachineState *machine, hwaddr rom_base,
                                  hwaddr rom_size, uint32_t reset_vec_size,
                                  uint64_t kernel_entry)
{
    struct fw_dynamic_info dinfo;
    size_t dinfo_len;

    if (sizeof(dinfo.magic) == 4) {
        dinfo.magic = cpu_to_le32(FW_DYNAMIC_INFO_MAGIC_VALUE);
        dinfo.version = cpu_to_le32(FW_DYNAMIC_INFO_VERSION);
        dinfo.next_mode = cpu_to_le32(FW_DYNAMIC_INFO_NEXT_MODE_S);
        dinfo.next_addr = cpu_to_le32(kernel_entry);
    } else {
        dinfo.magic = cpu_to_le64(FW_DYNAMIC_INFO_MAGIC_VALUE);
        dinfo.version = cpu_to_le64(FW_DYNAMIC_INFO_VERSION);
        dinfo.next_mode = cpu_to_le64(FW_DYNAMIC_INFO_NEXT_MODE_S);
        dinfo.next_addr = cpu_to_le64(kernel_entry);
    }
    dinfo.options = 0;
    dinfo.boot_hart = 0;
    dinfo_len = sizeof(dinfo);

    /**
     * copy the dynamic firmware info. This information is specific to
     * OpenSBI but doesn't break any other firmware as long as they don't
     * expect any certain value in "a2" register.
     */
    if (dinfo_len > (rom_size - reset_vec_size)) {
        error_report("not enough space to store dynamic firmware info");
        exit(1);
    }

    rom_add_blob_fixed_as("mrom.finfo", &dinfo, dinfo_len,
                           rom_base + reset_vec_size,
                           &address_space_memory);
}

void riscv_setup_rom_reset_vec(MachineState *machine, RISCVHartArrayState *harts,
                               hwaddr start_addr,
                               hwaddr rom_base, hwaddr rom_size,
                               uint64_t kernel_entry,
                               uint64_t fdt_load_addr)
{
    int i;
    uint32_t start_addr_hi32 = 0x00000000;
    uint32_t fdt_load_addr_hi32 = 0x00000000;

    if (!riscv_is_32bit(harts)) {
        start_addr_hi32 = start_addr >> 32;
        fdt_load_addr_hi32 = fdt_load_addr >> 32;
    }
    /* reset vector */
    uint32_t reset_vec[10] = {
        0x00000297,                  /* 1:  auipc  t0, %pcrel_hi(fw_dyn) */
        0x02828613,                  /*     addi   a2, t0, %pcrel_lo(1b) */
        0xf1402573,                  /*     csrr   a0, mhartid  */
        0,
        0,
        0x00028067,                  /*     jr     t0 */
        start_addr,                  /* start: .dword */
        start_addr_hi32,
        fdt_load_addr,               /* fdt_laddr: .dword */
        fdt_load_addr_hi32,
                                     /* fw_dyn: */
    };
    if (riscv_is_32bit(harts)) {
        reset_vec[3] = 0x0202a583;   /*     lw     a1, 32(t0) */
        reset_vec[4] = 0x0182a283;   /*     lw     t0, 24(t0) */
    } else {
        reset_vec[3] = 0x0202b583;   /*     ld     a1, 32(t0) */
        reset_vec[4] = 0x0182b283;   /*     ld     t0, 24(t0) */
    }

    if (!harts->harts[0].cfg.ext_zicsr) {
        /*
         * The Zicsr extension has been disabled, so let's ensure we don't
         * run the CSR instruction. Let's fill the address with a non
         * compressed nop.
         */
        reset_vec[2] = 0x00000013;   /*     addi   x0, x0, 0 */
    }

    /* copy in the reset vector in little_endian byte order */
    for (i = 0; i < ARRAY_SIZE(reset_vec); i++) {
        reset_vec[i] = cpu_to_le32(reset_vec[i]);
    }
    rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                          rom_base, &address_space_memory);
    riscv_rom_copy_firmware_info(machine, rom_base, rom_size, sizeof(reset_vec),
                                 kernel_entry);
}

void riscv_setup_direct_kernel(hwaddr kernel_addr, hwaddr fdt_addr)
{
    CPUState *cs;

    for (cs = first_cpu; cs; cs = CPU_NEXT(cs)) {
        RISCVCPU *riscv_cpu = RISCV_CPU(cs);
        riscv_cpu->env.kernel_addr = kernel_addr;
        riscv_cpu->env.fdt_addr = fdt_addr;
    }
}

void riscv_setup_firmware_boot(MachineState *machine)
{
    if (machine->kernel_filename) {
        FWCfgState *fw_cfg;
        fw_cfg = fw_cfg_find();

        assert(fw_cfg);
        /*
         * Expose the kernel, the command line, and the initrd in fw_cfg.
         * We don't process them here at all, it's all left to the
         * firmware.
         */
        load_image_to_fw_cfg(fw_cfg,
                             FW_CFG_KERNEL_SIZE, FW_CFG_KERNEL_DATA,
                             machine->kernel_filename,
                             true);
        load_image_to_fw_cfg(fw_cfg,
                             FW_CFG_INITRD_SIZE, FW_CFG_INITRD_DATA,
                             machine->initrd_filename, false);

        if (machine->kernel_cmdline) {
            fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE,
                           strlen(machine->kernel_cmdline) + 1);
            fw_cfg_add_string(fw_cfg, FW_CFG_CMDLINE_DATA,
                              machine->kernel_cmdline);
        }
    }
}
