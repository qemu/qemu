/*
 * CSKY kernel loader.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/csky/csky.h"
#include "sysemu/kvm.h"
#include "sysemu/sysemu.h"
#include "sysemu/numa.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "sysemu/device_tree.h"
#include "qemu/config-file.h"
#include "exec/address-spaces.h"
#include "sysemu/qtest.h"
#include "target/csky/translate.h"

#define KERNEL_ARGS_ADDR 0x800000
#define KERNEL_LOAD_ADDR 0x00010000
#define INITRD_LOAD_ADDR 0x0a000000
#define BIOS_SIZE (4 * 1024 * 1024)

#define WRITE_WORD(p, value) do { \
    address_space_stl_notdirty(&address_space_memory, p, value, \
                               MEMTXATTRS_UNSPECIFIED, NULL);   \
    p += 4;                       \
} while (0)

static void set_kernel_args_old(struct csky_boot_info *info, CPUCSKYState *env)
{
    int initrd_size = info->initrd_size;
    hwaddr base = info->loader_start;
    hwaddr p;

    if (info->kernel_flags == KERNEL_BIN_NO_BIOS) {
        if (env->features & CPU_ABIV1) {
            env->regs[2] = 0xa2a25441;
            env->regs[3] = KERNEL_ARGS_ADDR;
        } else if (env->features & CPU_ABIV2) {
            env->regs[0] = 0xa2a25441;
            env->regs[1] = KERNEL_ARGS_ADDR;
        } else {
            fprintf(stderr, "do_cpu_reset: bad CPU ABI\n");
            exit(1);
        }
    }

    p = base + KERNEL_ARGS_ADDR;
    /* ATAG_CORE */
    WRITE_WORD(p, 5);
    WRITE_WORD(p, 0x54410001);
    WRITE_WORD(p, 1);
    WRITE_WORD(p, 0x1000);
    WRITE_WORD(p, 0);
    /* ATAG_MEM_RANGE */
    WRITE_WORD(p, 5);
    WRITE_WORD(p, 0x54410002);
    WRITE_WORD(p, info->loader_start);
    WRITE_WORD(p, info->ram_size);
    WRITE_WORD(p, 1);

    WRITE_WORD(p, 5);
    WRITE_WORD(p, 0x54410004);
    WRITE_WORD(p, 0xa000000);
    WRITE_WORD(p, initrd_size);
    WRITE_WORD(p, 3);

    if (info->kernel_cmdline && *info->kernel_cmdline) {
        /* ATAG_CMDLINE */
        int cmdline_size;

        cmdline_size = strlen(info->kernel_cmdline);
        cpu_physical_memory_write(p + 8, (void *)info->kernel_cmdline,
                                  cmdline_size + 1);
        cmdline_size = (cmdline_size >> 2) + 1;
        WRITE_WORD(p, cmdline_size + 2);
        WRITE_WORD(p, 0x54410003);
        p += cmdline_size * 4;
    }

    /* ATAG_END */
    WRITE_WORD(p, 0);
    WRITE_WORD(p, 0);
}

static void set_kernel_args(struct csky_boot_info *info, CPUCSKYState *env)
{
    if (info->kernel_flags != KERNEL_BIN_AND_BIOS) {
        if (env->features & CPU_ABIV1) {
            env->regs[2] = info->magic;
            env->regs[3] = info->dtb_addr;
        } else if (env->features & CPU_ABIV2) {
            env->regs[0] = info->magic;
            env->regs[1] = info->dtb_addr;
        } else {
            fprintf(stderr, "do_cpu_reset: bad CPU ABI\n");
            exit(1);
        }
    }
}

static void do_cpu_reset(void *opaque)
{
    CSKYCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUCSKYState *env = &cpu->env;
    struct csky_boot_info *info = env->boot_info;

    cpu_reset(cs);

    if (info == NULL) {
        return;
    }

    env->pc = info->entry;
    if (info->kernel_flags == KERNEL_BIN_AND_BIOS) {
        cs->exception_index = EXCP_CSKY_RESET;
    } else {
        cs->exception_index = -1;
    }

    if (old_param) {
        set_kernel_args_old(info, env);
    } else {
        set_kernel_args(info, env);
    }

    if (env->mmu_default) {
        env->cp0.ccr |= 0x1;
        env->tlb_context->get_physical_address = mmu_get_physical_address;
    }
}

static uint64_t cpu_csky_sseg0_to_phys(void *opaque, uint64_t addr)
{
    CPUCSKYState *env = (CPUCSKYState *)opaque;

    if (env->mmu_default) {
        return addr & 0x1fffffffll;
    } else {
        return addr;
    }
}

static int load_dtb(hwaddr addr, struct csky_boot_info *binfo)
{
    void *fdt = NULL;
    int size;

    if (binfo->dtb_filename) {
        char *filename;
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, binfo->dtb_filename);
        if (!filename) {
            fprintf(stderr, "Couldn't open dtb file %s\n", binfo->dtb_filename);
            goto fail;
        }

        fdt = load_device_tree(filename, &size);
        if (!fdt) {
            fprintf(stderr, "Couldn't open dtb file %s\n", filename);
            g_free(filename);
            goto fail;
        }
        g_free(filename);
    } else {
        fprintf(stderr, "Board was unable to create a dtb blob\n");
        goto fail;
    }

    qemu_fdt_dumpdtb(fdt, size);

    /* Put the DTB into the memory map as a ROM image: this will ensure
     * the DTB is copied again upon reset, even if addr points into RAM.
     */
    rom_add_blob_fixed("dtb", fdt, size, addr);

    g_free(fdt);


    return 0;

fail:
    g_free(fdt);
    return -1;
}

static void csky_load_kernel_notify(Notifier *notifier, void *data)
{
    int kernel_size;
    int initrd_size = 0;
    int is_linux = 0;
    uint64_t elf_entry;
    hwaddr entry;
    hwaddr dtb_addr;
    int big_endian;
    int no_bios;
    target_long bios_size;
    int data_swab = 0;
    /* 1 is elf.2 is uimage, 3 is bin with no bios, 4 is bin with bios,
       5 is Kernel bin whithout cmdline and bios */
    int kernel_flags;
    CSKYLoadKernelNotifier *n = DO_UPCAST(CSKYLoadKernelNotifier,
                                         notifier, notifier);
    CSKYCPU *cpu = n->cpu;
    struct csky_boot_info *info = container_of(n, struct csky_boot_info,
                                                load_kernel_notifier);

    CPUCSKYState *env = &cpu->env;

    no_bios = 1;

    info->dtb_filename = qemu_opt_get(qemu_get_machine_opts(), "dtb");

    /* Load the kernel.  */
    if (info->kernel_filename) {
#ifdef TARGET_WORDS_BIGENDIAN
        big_endian = 1;
#else
        big_endian = 0;
#endif
        /* Assume that raw images are linux kernels, and ELF images are not.  */
        kernel_size = load_elf(info->kernel_filename, cpu_csky_sseg0_to_phys,
                               env, &elf_entry,
                               NULL, NULL, big_endian, EM_CSKY, 1, data_swab);
        if (kernel_size == ELF_LOAD_WRONG_ENDIAN) {
            fprintf(stderr, "qemu: wrong endianness in image file\n");
            exit(-1);
        }

        entry = elf_entry;
        kernel_flags = KERNEL_ELF;

        if (info->dtb_filename) {
            dtb_addr = cpu_csky_sseg0_to_phys(env, info->dtb_addr);
            if (load_dtb(dtb_addr, info) == -1) {
                fprintf(stderr, "qemu: Could not load  dtb '%s'\n",
                        info->dtb_filename);
                exit(1);
            }
        }

        if (kernel_size < 0) {
            kernel_size = load_uimage(info->kernel_filename, &entry, NULL,
                                      &is_linux, NULL, NULL);
            kernel_flags = KERNEL_UIMAGE;
        }

        if (kernel_size < 0) {
            if (info->kernel_cmdline && *info->kernel_cmdline) {
                kernel_flags = KERNEL_BIN_NO_BIOS;
            } else {
                /* Kernel bin without cmdline */
                kernel_flags = KERNEL_KBIN_NO_CMDLINE;
            }
            /* Load a BIOS image. */
            if (info->initrd_filename) {
                no_bios = 0;
                kernel_flags = KERNEL_BIN_AND_BIOS;
                if (info->initrd_filename) {
                    bios_size = load_image_targphys(info->initrd_filename, 0x0,
                                                    BIOS_SIZE);
                } else {
                    bios_size = -1;
                    no_bios = 1;
                }
                if ((bios_size < 0 || bios_size > BIOS_SIZE)
                    && !info->kernel_filename) {
                    fprintf(stderr, "qemu: Could not load  bios '%s',"
                            "and no -kernel argument was specified\n",
                            bios_name);
                    exit(1);
                }
            }

            entry = info->loader_start + KERNEL_LOAD_ADDR;
            kernel_size = load_image_targphys(info->kernel_filename, entry,
                                              ram_size - KERNEL_LOAD_ADDR);
            is_linux = 1;
        }
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    info->kernel_filename);
            exit(1);
        }
        if (is_linux) {
            if (info->initrd_filename) {
                initrd_size = load_image_targphys(info->initrd_filename,
                                                  info->loader_start
                                                  + INITRD_LOAD_ADDR,
                                                  ram_size - INITRD_LOAD_ADDR);
                if (initrd_size < 0) {
                    fprintf(stderr, "qemu: could not load initrd '%s'\n",
                            info->initrd_filename);
                    exit(1);
                }
            } else {
                initrd_size = 0;
            }

            if (no_bios == 1) {
                if (!env->binstart) {
                    env->binstart = 0x8000000;
                }
                entry = info->loader_start + env->binstart;
                kernel_size = load_image_targphys(info->kernel_filename, entry,
                                                  ram_size - env->binstart);
                env->regs[2] = 0xa2a25441;
                env->regs[3] = KERNEL_ARGS_ADDR;
                env->pc = entry;
            } else {
                entry = 0x0;
                env->pc = entry;
            }
        }

        info->kernel_flags = kernel_flags;
        info->entry = entry;
        info->initrd_size = initrd_size;
        env->boot_info = info;
    } else if (!qtest_enabled()) {
        fprintf(stderr, "Kernel image must be specified\n");
        exit(-1);
    }
}

void csky_load_kernel(CSKYCPU *cpu, struct csky_boot_info *info)
{
    CPUState *cs;

    info->load_kernel_notifier.cpu = cpu;
    info->load_kernel_notifier.notifier.notify = csky_load_kernel_notify;
    qemu_add_machine_init_done_notifier(&info->load_kernel_notifier.notifier);

    /* CPU objects (unlike devices) are not automatically
     * reset on system reset, so we must always register a handler to
     * do so. If we're actually loading a kernel, the handler is
     * also responsible for arranging that we start it correctly.
     */
    for (cs = CPU(cpu); cs; cs = CPU_NEXT(cs)) {
        qemu_register_reset(do_cpu_reset, CSKY_CPU(cs));
    }
}

