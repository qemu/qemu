/*
 * QEMU PC System Emulator
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/nvram/fw_cfg.h"
#include "multiboot.h"
#include "hw/loader.h"
#include "elf.h"
#include "sysemu/sysemu.h"

/* Show multiboot debug output */
//#define DEBUG_MULTIBOOT

#ifdef DEBUG_MULTIBOOT
#define mb_debug(a...) fprintf(stderr, ## a)
#else
#define mb_debug(a...)
#endif

#define MULTIBOOT_STRUCT_ADDR 0x9000

#if MULTIBOOT_STRUCT_ADDR > 0xf0000
#error multiboot struct needs to fit in 16 bit real mode
#endif

enum {
    /* Multiboot info */
    MBI_FLAGS       = 0,
    MBI_MEM_LOWER   = 4,
    MBI_MEM_UPPER   = 8,
    MBI_BOOT_DEVICE = 12,
    MBI_CMDLINE     = 16,
    MBI_MODS_COUNT  = 20,
    MBI_MODS_ADDR   = 24,
    MBI_MMAP_ADDR   = 48,
    MBI_BOOTLOADER  = 64,

    MBI_SIZE        = 88,

    /* Multiboot modules */
    MB_MOD_START    = 0,
    MB_MOD_END      = 4,
    MB_MOD_CMDLINE  = 8,

    MB_MOD_SIZE     = 16,

    /* Region offsets */
    ADDR_E820_MAP = MULTIBOOT_STRUCT_ADDR + 0,
    ADDR_MBI      = ADDR_E820_MAP + 0x500,

    /* Multiboot flags */
    MULTIBOOT_FLAGS_MEMORY      = 1 << 0,
    MULTIBOOT_FLAGS_BOOT_DEVICE = 1 << 1,
    MULTIBOOT_FLAGS_CMDLINE     = 1 << 2,
    MULTIBOOT_FLAGS_MODULES     = 1 << 3,
    MULTIBOOT_FLAGS_MMAP        = 1 << 6,
    MULTIBOOT_FLAGS_BOOTLOADER  = 1 << 9,
};

typedef struct {
    /* buffer holding kernel, cmdlines and mb_infos */
    void *mb_buf;
    /* address in target */
    hwaddr mb_buf_phys;
    /* size of mb_buf in bytes */
    unsigned mb_buf_size;
    /* offset of mb-info's in bytes */
    hwaddr offset_mbinfo;
    /* offset in buffer for cmdlines in bytes */
    hwaddr offset_cmdlines;
    /* offset in buffer for bootloader name in bytes */
    hwaddr offset_bootloader;
    /* offset of modules in bytes */
    hwaddr offset_mods;
    /* available slots for mb modules infos */
    int mb_mods_avail;
    /* currently used slots of mb modules */
    int mb_mods_count;
} MultibootState;

const char *bootloader_name = "qemu";

static uint32_t mb_add_cmdline(MultibootState *s, const char *cmdline)
{
    hwaddr p = s->offset_cmdlines;
    char *b = (char *)s->mb_buf + p;

    get_opt_value(b, strlen(cmdline) + 1, cmdline);
    s->offset_cmdlines += strlen(b) + 1;
    return s->mb_buf_phys + p;
}

static uint32_t mb_add_bootloader(MultibootState *s, const char *bootloader)
{
    hwaddr p = s->offset_bootloader;
    char *b = (char *)s->mb_buf + p;

    memcpy(b, bootloader, strlen(bootloader) + 1);
    s->offset_bootloader += strlen(b) + 1;
    return s->mb_buf_phys + p;
}

static void mb_add_mod(MultibootState *s,
                       hwaddr start, hwaddr end,
                       hwaddr cmdline_phys)
{
    char *p;
    assert(s->mb_mods_count < s->mb_mods_avail);

    p = (char *)s->mb_buf + s->offset_mbinfo + MB_MOD_SIZE * s->mb_mods_count;

    stl_p(p + MB_MOD_START,   start);
    stl_p(p + MB_MOD_END,     end);
    stl_p(p + MB_MOD_CMDLINE, cmdline_phys);

    mb_debug("mod%02d: "TARGET_FMT_plx" - "TARGET_FMT_plx"\n",
             s->mb_mods_count, start, end);

    s->mb_mods_count++;
}

int load_multiboot(FWCfgState *fw_cfg,
                   FILE *f,
                   const char *kernel_filename,
                   const char *initrd_filename,
                   const char *kernel_cmdline,
                   int kernel_file_size,
                   uint8_t *header)
{
    int i, is_multiboot = 0;
    uint32_t flags = 0;
    uint32_t mh_entry_addr;
    uint32_t mh_load_addr;
    uint32_t mb_kernel_size;
    MultibootState mbs;
    uint8_t bootinfo[MBI_SIZE];
    uint8_t *mb_bootinfo_data;
    uint32_t cmdline_len;

    /* Ok, let's see if it is a multiboot image.
       The header is 12x32bit long, so the latest entry may be 8192 - 48. */
    for (i = 0; i < (8192 - 48); i += 4) {
        if (ldl_p(header+i) == 0x1BADB002) {
            uint32_t checksum = ldl_p(header+i+8);
            flags = ldl_p(header+i+4);
            checksum += flags;
            checksum += (uint32_t)0x1BADB002;
            if (!checksum) {
                is_multiboot = 1;
                break;
            }
        }
    }

    if (!is_multiboot)
        return 0; /* no multiboot */

    mb_debug("qemu: I believe we found a multiboot image!\n");
    memset(bootinfo, 0, sizeof(bootinfo));
    memset(&mbs, 0, sizeof(mbs));

    if (flags & 0x00000004) { /* MULTIBOOT_HEADER_HAS_VBE */
        fprintf(stderr, "qemu: multiboot knows VBE. we don't.\n");
    }
    if (!(flags & 0x00010000)) { /* MULTIBOOT_HEADER_HAS_ADDR */
        uint64_t elf_entry;
        uint64_t elf_low, elf_high;
        int kernel_size;
        fclose(f);

        if (((struct elf64_hdr*)header)->e_machine == EM_X86_64) {
            fprintf(stderr, "Cannot load x86-64 image, give a 32bit one.\n");
            exit(1);
        }

        kernel_size = load_elf(kernel_filename, NULL, NULL, &elf_entry,
                               &elf_low, &elf_high, 0, I386_ELF_MACHINE,
                               0, 0);
        if (kernel_size < 0) {
            fprintf(stderr, "Error while loading elf kernel\n");
            exit(1);
        }
        mh_load_addr = elf_low;
        mb_kernel_size = elf_high - elf_low;
        mh_entry_addr = elf_entry;

        mbs.mb_buf = g_malloc(mb_kernel_size);
        if (rom_copy(mbs.mb_buf, mh_load_addr, mb_kernel_size) != mb_kernel_size) {
            fprintf(stderr, "Error while fetching elf kernel from rom\n");
            exit(1);
        }

        mb_debug("qemu: loading multiboot-elf kernel (%#x bytes) with entry %#zx\n",
                  mb_kernel_size, (size_t)mh_entry_addr);
    } else {
        /* Valid if mh_flags sets MULTIBOOT_HEADER_HAS_ADDR. */
        uint32_t mh_header_addr = ldl_p(header+i+12);
        uint32_t mh_load_end_addr = ldl_p(header+i+20);
        uint32_t mh_bss_end_addr = ldl_p(header+i+24);
        mh_load_addr = ldl_p(header+i+16);
        uint32_t mb_kernel_text_offset = i - (mh_header_addr - mh_load_addr);
        uint32_t mb_load_size = 0;
        mh_entry_addr = ldl_p(header+i+28);

        if (mh_load_end_addr) {
            mb_kernel_size = mh_bss_end_addr - mh_load_addr;
            mb_load_size = mh_load_end_addr - mh_load_addr;
        } else {
            mb_kernel_size = kernel_file_size - mb_kernel_text_offset;
            mb_load_size = mb_kernel_size;
        }

        /* Valid if mh_flags sets MULTIBOOT_HEADER_HAS_VBE.
        uint32_t mh_mode_type = ldl_p(header+i+32);
        uint32_t mh_width = ldl_p(header+i+36);
        uint32_t mh_height = ldl_p(header+i+40);
        uint32_t mh_depth = ldl_p(header+i+44); */

        mb_debug("multiboot: mh_header_addr = %#x\n", mh_header_addr);
        mb_debug("multiboot: mh_load_addr = %#x\n", mh_load_addr);
        mb_debug("multiboot: mh_load_end_addr = %#x\n", mh_load_end_addr);
        mb_debug("multiboot: mh_bss_end_addr = %#x\n", mh_bss_end_addr);
        mb_debug("qemu: loading multiboot kernel (%#x bytes) at %#x\n",
                 mb_load_size, mh_load_addr);

        mbs.mb_buf = g_malloc(mb_kernel_size);
        fseek(f, mb_kernel_text_offset, SEEK_SET);
        if (fread(mbs.mb_buf, 1, mb_load_size, f) != mb_load_size) {
            fprintf(stderr, "fread() failed\n");
            exit(1);
        }
        memset(mbs.mb_buf + mb_load_size, 0, mb_kernel_size - mb_load_size);
        fclose(f);
    }

    mbs.mb_buf_phys = mh_load_addr;

    mbs.mb_buf_size = TARGET_PAGE_ALIGN(mb_kernel_size);
    mbs.offset_mbinfo = mbs.mb_buf_size;

    /* Calculate space for cmdlines, bootloader name, and mb_mods */
    cmdline_len = strlen(kernel_filename) + 1;
    cmdline_len += strlen(kernel_cmdline) + 1;
    if (initrd_filename) {
        const char *r = initrd_filename;
        cmdline_len += strlen(r) + 1;
        mbs.mb_mods_avail = 1;
        while (*(r = get_opt_value(NULL, 0, r))) {
           mbs.mb_mods_avail++;
           r++;
        }
    }

    mbs.mb_buf_size += cmdline_len;
    mbs.mb_buf_size += MB_MOD_SIZE * mbs.mb_mods_avail;
    mbs.mb_buf_size += strlen(bootloader_name) + 1;

    mbs.mb_buf_size = TARGET_PAGE_ALIGN(mbs.mb_buf_size);

    /* enlarge mb_buf to hold cmdlines, bootloader, mb-info structs */
    mbs.mb_buf            = g_realloc(mbs.mb_buf, mbs.mb_buf_size);
    mbs.offset_cmdlines   = mbs.offset_mbinfo + mbs.mb_mods_avail * MB_MOD_SIZE;
    mbs.offset_bootloader = mbs.offset_cmdlines + cmdline_len;

    if (initrd_filename) {
        char *next_initrd, not_last;

        mbs.offset_mods = mbs.mb_buf_size;

        do {
            char *next_space;
            int mb_mod_length;
            uint32_t offs = mbs.mb_buf_size;

            next_initrd = (char *)get_opt_value(NULL, 0, initrd_filename);
            not_last = *next_initrd;
            *next_initrd = '\0';
            /* if a space comes after the module filename, treat everything
               after that as parameters */
            hwaddr c = mb_add_cmdline(&mbs, initrd_filename);
            if ((next_space = strchr(initrd_filename, ' ')))
                *next_space = '\0';
            mb_debug("multiboot loading module: %s\n", initrd_filename);
            mb_mod_length = get_image_size(initrd_filename);
            if (mb_mod_length < 0) {
                fprintf(stderr, "Failed to open file '%s'\n", initrd_filename);
                exit(1);
            }

            mbs.mb_buf_size = TARGET_PAGE_ALIGN(mb_mod_length + mbs.mb_buf_size);
            mbs.mb_buf = g_realloc(mbs.mb_buf, mbs.mb_buf_size);

            load_image(initrd_filename, (unsigned char *)mbs.mb_buf + offs);
            mb_add_mod(&mbs, mbs.mb_buf_phys + offs,
                       mbs.mb_buf_phys + offs + mb_mod_length, c);

            mb_debug("mod_start: %p\nmod_end:   %p\n  cmdline: "TARGET_FMT_plx"\n",
                     (char *)mbs.mb_buf + offs,
                     (char *)mbs.mb_buf + offs + mb_mod_length, c);
            initrd_filename = next_initrd+1;
        } while (not_last);
    }

    /* Commandline support */
    char kcmdline[strlen(kernel_filename) + strlen(kernel_cmdline) + 2];
    snprintf(kcmdline, sizeof(kcmdline), "%s %s",
             kernel_filename, kernel_cmdline);
    stl_p(bootinfo + MBI_CMDLINE, mb_add_cmdline(&mbs, kcmdline));

    stl_p(bootinfo + MBI_BOOTLOADER, mb_add_bootloader(&mbs, bootloader_name));

    stl_p(bootinfo + MBI_MODS_ADDR,  mbs.mb_buf_phys + mbs.offset_mbinfo);
    stl_p(bootinfo + MBI_MODS_COUNT, mbs.mb_mods_count); /* mods_count */

    /* the kernel is where we want it to be now */
    stl_p(bootinfo + MBI_FLAGS, MULTIBOOT_FLAGS_MEMORY
                                | MULTIBOOT_FLAGS_BOOT_DEVICE
                                | MULTIBOOT_FLAGS_CMDLINE
                                | MULTIBOOT_FLAGS_MODULES
                                | MULTIBOOT_FLAGS_MMAP
                                | MULTIBOOT_FLAGS_BOOTLOADER);
    stl_p(bootinfo + MBI_BOOT_DEVICE, 0x8000ffff); /* XXX: use the -boot switch? */
    stl_p(bootinfo + MBI_MMAP_ADDR,   ADDR_E820_MAP);

    mb_debug("multiboot: mh_entry_addr = %#x\n", mh_entry_addr);
    mb_debug("           mb_buf_phys   = "TARGET_FMT_plx"\n", mbs.mb_buf_phys);
    mb_debug("           mod_start     = "TARGET_FMT_plx"\n", mbs.mb_buf_phys + mbs.offset_mods);
    mb_debug("           mb_mods_count = %d\n", mbs.mb_mods_count);

    /* save bootinfo off the stack */
    mb_bootinfo_data = g_malloc(sizeof(bootinfo));
    memcpy(mb_bootinfo_data, bootinfo, sizeof(bootinfo));

    /* Pass variables to option rom */
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ENTRY, mh_entry_addr);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ADDR, mh_load_addr);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_SIZE, mbs.mb_buf_size);
    fw_cfg_add_bytes(fw_cfg, FW_CFG_KERNEL_DATA,
                     mbs.mb_buf, mbs.mb_buf_size);

    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_ADDR, ADDR_MBI);
    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_SIZE, sizeof(bootinfo));
    fw_cfg_add_bytes(fw_cfg, FW_CFG_INITRD_DATA, mb_bootinfo_data,
                     sizeof(bootinfo));

    option_rom[nb_option_roms].name = "multiboot.bin";
    option_rom[nb_option_roms].bootindex = 0;
    nb_option_roms++;

    return 1; /* yes, we are multiboot */
}
