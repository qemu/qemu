/*
 * QEMU Executable loader
 *
 * Copyright (c) 2006 Fabrice Bellard
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
 *
 * Gunzip functionality in this file is derived from u-boot:
 *
 * (C) Copyright 2008 Semihalf
 *
 * (C) Copyright 2000-2005
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw.h"
#include "disas.h"
#include "monitor.h"
#include "sysemu.h"
#include "uboot_image.h"
#include "loader.h"
#include "fw_cfg.h"

#include <zlib.h>

static int roms_loaded;

/* return the size or -1 if error */
int get_image_size(const char *filename)
{
    int fd, size;
    fd = open(filename, O_RDONLY | O_BINARY);
    if (fd < 0)
        return -1;
    size = lseek(fd, 0, SEEK_END);
    close(fd);
    return size;
}

/* return the size or -1 if error */
/* deprecated, because caller does not specify buffer size! */
int load_image(const char *filename, uint8_t *addr)
{
    int fd, size;
    fd = open(filename, O_RDONLY | O_BINARY);
    if (fd < 0)
        return -1;
    size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (read(fd, addr, size) != size) {
        close(fd);
        return -1;
    }
    close(fd);
    return size;
}

/* read()-like version */
int read_targphys(const char *name,
                  int fd, target_phys_addr_t dst_addr, size_t nbytes)
{
    uint8_t *buf;
    size_t did;

    buf = qemu_malloc(nbytes);
    did = read(fd, buf, nbytes);
    if (did > 0)
        rom_add_blob_fixed("read", buf, did, dst_addr);
    qemu_free(buf);
    return did;
}

/* return the size or -1 if error */
int load_image_targphys(const char *filename,
			target_phys_addr_t addr, int max_sz)
{
    int size;

    size = get_image_size(filename);
    if (size > 0)
        rom_add_file_fixed(filename, addr, -1);
    return size;
}

void pstrcpy_targphys(const char *name, target_phys_addr_t dest, int buf_size,
                      const char *source)
{
    const char *nulp;
    char *ptr;

    if (buf_size <= 0) return;
    nulp = memchr(source, 0, buf_size);
    if (nulp) {
        rom_add_blob_fixed(name, source, (nulp - source) + 1, dest);
    } else {
        rom_add_blob_fixed(name, source, buf_size, dest);
        ptr = rom_ptr(dest + buf_size - 1);
        *ptr = 0;
    }
}

/* A.OUT loader */

struct exec
{
  uint32_t a_info;   /* Use macros N_MAGIC, etc for access */
  uint32_t a_text;   /* length of text, in bytes */
  uint32_t a_data;   /* length of data, in bytes */
  uint32_t a_bss;    /* length of uninitialized data area, in bytes */
  uint32_t a_syms;   /* length of symbol table data in file, in bytes */
  uint32_t a_entry;  /* start address */
  uint32_t a_trsize; /* length of relocation info for text, in bytes */
  uint32_t a_drsize; /* length of relocation info for data, in bytes */
};

static void bswap_ahdr(struct exec *e)
{
    bswap32s(&e->a_info);
    bswap32s(&e->a_text);
    bswap32s(&e->a_data);
    bswap32s(&e->a_bss);
    bswap32s(&e->a_syms);
    bswap32s(&e->a_entry);
    bswap32s(&e->a_trsize);
    bswap32s(&e->a_drsize);
}

#define N_MAGIC(exec) ((exec).a_info & 0xffff)
#define OMAGIC 0407
#define NMAGIC 0410
#define ZMAGIC 0413
#define QMAGIC 0314
#define _N_HDROFF(x) (1024 - sizeof (struct exec))
#define N_TXTOFF(x)							\
    (N_MAGIC(x) == ZMAGIC ? _N_HDROFF((x)) + sizeof (struct exec) :	\
     (N_MAGIC(x) == QMAGIC ? 0 : sizeof (struct exec)))
#define N_TXTADDR(x, target_page_size) (N_MAGIC(x) == QMAGIC ? target_page_size : 0)
#define _N_SEGMENT_ROUND(x, target_page_size) (((x) + target_page_size - 1) & ~(target_page_size - 1))

#define _N_TXTENDADDR(x, target_page_size) (N_TXTADDR(x, target_page_size)+(x).a_text)

#define N_DATADDR(x, target_page_size) \
    (N_MAGIC(x)==OMAGIC? (_N_TXTENDADDR(x, target_page_size)) \
     : (_N_SEGMENT_ROUND (_N_TXTENDADDR(x, target_page_size), target_page_size)))


int load_aout(const char *filename, target_phys_addr_t addr, int max_sz,
              int bswap_needed, target_phys_addr_t target_page_size)
{
    int fd, size, ret;
    struct exec e;
    uint32_t magic;

    fd = open(filename, O_RDONLY | O_BINARY);
    if (fd < 0)
        return -1;

    size = read(fd, &e, sizeof(e));
    if (size < 0)
        goto fail;

    if (bswap_needed) {
        bswap_ahdr(&e);
    }

    magic = N_MAGIC(e);
    switch (magic) {
    case ZMAGIC:
    case QMAGIC:
    case OMAGIC:
        if (e.a_text + e.a_data > max_sz)
            goto fail;
	lseek(fd, N_TXTOFF(e), SEEK_SET);
	size = read_targphys(filename, fd, addr, e.a_text + e.a_data);
	if (size < 0)
	    goto fail;
	break;
    case NMAGIC:
        if (N_DATADDR(e, target_page_size) + e.a_data > max_sz)
            goto fail;
	lseek(fd, N_TXTOFF(e), SEEK_SET);
	size = read_targphys(filename, fd, addr, e.a_text);
	if (size < 0)
	    goto fail;
        ret = read_targphys(filename, fd, addr + N_DATADDR(e, target_page_size),
                            e.a_data);
	if (ret < 0)
	    goto fail;
	size += ret;
	break;
    default:
	goto fail;
    }
    close(fd);
    return size;
 fail:
    close(fd);
    return -1;
}

/* ELF loader */

static void *load_at(int fd, int offset, int size)
{
    void *ptr;
    if (lseek(fd, offset, SEEK_SET) < 0)
        return NULL;
    ptr = qemu_malloc(size);
    if (read(fd, ptr, size) != size) {
        qemu_free(ptr);
        return NULL;
    }
    return ptr;
}

#ifdef ELF_CLASS
#undef ELF_CLASS
#endif

#define ELF_CLASS   ELFCLASS32
#include "elf.h"

#define SZ		32
#define elf_word        uint32_t
#define elf_sword        int32_t
#define bswapSZs	bswap32s
#include "elf_ops.h"

#undef elfhdr
#undef elf_phdr
#undef elf_shdr
#undef elf_sym
#undef elf_note
#undef elf_word
#undef elf_sword
#undef bswapSZs
#undef SZ
#define elfhdr		elf64_hdr
#define elf_phdr	elf64_phdr
#define elf_note	elf64_note
#define elf_shdr	elf64_shdr
#define elf_sym		elf64_sym
#define elf_word        uint64_t
#define elf_sword        int64_t
#define bswapSZs	bswap64s
#define SZ		64
#include "elf_ops.h"

/* return < 0 if error, otherwise the number of bytes loaded in memory */
int load_elf(const char *filename, uint64_t (*translate_fn)(void *, uint64_t),
             void *translate_opaque, uint64_t *pentry, uint64_t *lowaddr,
             uint64_t *highaddr, int big_endian, int elf_machine, int clear_lsb)
{
    int fd, data_order, target_data_order, must_swab, ret;
    uint8_t e_ident[EI_NIDENT];

    fd = open(filename, O_RDONLY | O_BINARY);
    if (fd < 0) {
        perror(filename);
        return -1;
    }
    if (read(fd, e_ident, sizeof(e_ident)) != sizeof(e_ident))
        goto fail;
    if (e_ident[0] != ELFMAG0 ||
        e_ident[1] != ELFMAG1 ||
        e_ident[2] != ELFMAG2 ||
        e_ident[3] != ELFMAG3)
        goto fail;
#ifdef HOST_WORDS_BIGENDIAN
    data_order = ELFDATA2MSB;
#else
    data_order = ELFDATA2LSB;
#endif
    must_swab = data_order != e_ident[EI_DATA];
    if (big_endian) {
        target_data_order = ELFDATA2MSB;
    } else {
        target_data_order = ELFDATA2LSB;
    }

    if (target_data_order != e_ident[EI_DATA]) {
        goto fail;
    }

    lseek(fd, 0, SEEK_SET);
    if (e_ident[EI_CLASS] == ELFCLASS64) {
        ret = load_elf64(filename, fd, translate_fn, translate_opaque, must_swab,
                         pentry, lowaddr, highaddr, elf_machine, clear_lsb);
    } else {
        ret = load_elf32(filename, fd, translate_fn, translate_opaque, must_swab,
                         pentry, lowaddr, highaddr, elf_machine, clear_lsb);
    }

    close(fd);
    return ret;

 fail:
    close(fd);
    return -1;
}

static void bswap_uboot_header(uboot_image_header_t *hdr)
{
#ifndef HOST_WORDS_BIGENDIAN
    bswap32s(&hdr->ih_magic);
    bswap32s(&hdr->ih_hcrc);
    bswap32s(&hdr->ih_time);
    bswap32s(&hdr->ih_size);
    bswap32s(&hdr->ih_load);
    bswap32s(&hdr->ih_ep);
    bswap32s(&hdr->ih_dcrc);
#endif
}


#define ZALLOC_ALIGNMENT	16

static void *zalloc(void *x, unsigned items, unsigned size)
{
    void *p;

    size *= items;
    size = (size + ZALLOC_ALIGNMENT - 1) & ~(ZALLOC_ALIGNMENT - 1);

    p = qemu_malloc(size);

    return (p);
}

static void zfree(void *x, void *addr)
{
    qemu_free(addr);
}


#define HEAD_CRC	2
#define EXTRA_FIELD	4
#define ORIG_NAME	8
#define COMMENT		0x10
#define RESERVED	0xe0

#define DEFLATED	8

/* This is the maximum in uboot, so if a uImage overflows this, it would
 * overflow on real hardware too. */
#define UBOOT_MAX_GUNZIP_BYTES 0x800000

static ssize_t gunzip(void *dst, size_t dstlen, uint8_t *src,
                      size_t srclen)
{
    z_stream s;
    ssize_t dstbytes;
    int r, i, flags;

    /* skip header */
    i = 10;
    flags = src[3];
    if (src[2] != DEFLATED || (flags & RESERVED) != 0) {
        puts ("Error: Bad gzipped data\n");
        return -1;
    }
    if ((flags & EXTRA_FIELD) != 0)
        i = 12 + src[10] + (src[11] << 8);
    if ((flags & ORIG_NAME) != 0)
        while (src[i++] != 0)
            ;
    if ((flags & COMMENT) != 0)
        while (src[i++] != 0)
            ;
    if ((flags & HEAD_CRC) != 0)
        i += 2;
    if (i >= srclen) {
        puts ("Error: gunzip out of data in header\n");
        return -1;
    }

    s.zalloc = zalloc;
    s.zfree = zfree;

    r = inflateInit2(&s, -MAX_WBITS);
    if (r != Z_OK) {
        printf ("Error: inflateInit2() returned %d\n", r);
        return (-1);
    }
    s.next_in = src + i;
    s.avail_in = srclen - i;
    s.next_out = dst;
    s.avail_out = dstlen;
    r = inflate(&s, Z_FINISH);
    if (r != Z_OK && r != Z_STREAM_END) {
        printf ("Error: inflate() returned %d\n", r);
        return -1;
    }
    dstbytes = s.next_out - (unsigned char *) dst;
    inflateEnd(&s);

    return dstbytes;
}

/* Load a U-Boot image.  */
int load_uimage(const char *filename, target_phys_addr_t *ep,
                target_phys_addr_t *loadaddr, int *is_linux)
{
    int fd;
    int size;
    uboot_image_header_t h;
    uboot_image_header_t *hdr = &h;
    uint8_t *data = NULL;
    int ret = -1;

    fd = open(filename, O_RDONLY | O_BINARY);
    if (fd < 0)
        return -1;

    size = read(fd, hdr, sizeof(uboot_image_header_t));
    if (size < 0)
        goto out;

    bswap_uboot_header(hdr);

    if (hdr->ih_magic != IH_MAGIC)
        goto out;

    /* TODO: Implement other image types.  */
    if (hdr->ih_type != IH_TYPE_KERNEL) {
        fprintf(stderr, "Can only load u-boot image type \"kernel\"\n");
        goto out;
    }

    switch (hdr->ih_comp) {
    case IH_COMP_NONE:
    case IH_COMP_GZIP:
        break;
    default:
        fprintf(stderr,
                "Unable to load u-boot images with compression type %d\n",
                hdr->ih_comp);
        goto out;
    }

    /* TODO: Check CPU type.  */
    if (is_linux) {
        if (hdr->ih_os == IH_OS_LINUX)
            *is_linux = 1;
        else
            *is_linux = 0;
    }

    *ep = hdr->ih_ep;
    data = qemu_malloc(hdr->ih_size);

    if (read(fd, data, hdr->ih_size) != hdr->ih_size) {
        fprintf(stderr, "Error reading file\n");
        goto out;
    }

    if (hdr->ih_comp == IH_COMP_GZIP) {
        uint8_t *compressed_data;
        size_t max_bytes;
        ssize_t bytes;

        compressed_data = data;
        max_bytes = UBOOT_MAX_GUNZIP_BYTES;
        data = qemu_malloc(max_bytes);

        bytes = gunzip(data, max_bytes, compressed_data, hdr->ih_size);
        qemu_free(compressed_data);
        if (bytes < 0) {
            fprintf(stderr, "Unable to decompress gzipped image!\n");
            goto out;
        }
        hdr->ih_size = bytes;
    }

    rom_add_blob_fixed(filename, data, hdr->ih_size, hdr->ih_load);

    if (loadaddr)
        *loadaddr = hdr->ih_load;

    ret = hdr->ih_size;

out:
    if (data)
        qemu_free(data);
    close(fd);
    return ret;
}

/*
 * Functions for reboot-persistent memory regions.
 *  - used for vga bios and option roms.
 *  - also linux kernel (-kernel / -initrd).
 */

typedef struct Rom Rom;

struct Rom {
    char *name;
    char *path;
    size_t romsize;
    uint8_t *data;
    int isrom;
    char *fw_dir;
    char *fw_file;

    target_phys_addr_t addr;
    QTAILQ_ENTRY(Rom) next;
};

static FWCfgState *fw_cfg;
static QTAILQ_HEAD(, Rom) roms = QTAILQ_HEAD_INITIALIZER(roms);

static void rom_insert(Rom *rom)
{
    Rom *item;

    if (roms_loaded) {
        hw_error ("ROM images must be loaded at startup\n");
    }

    /* list is ordered by load address */
    QTAILQ_FOREACH(item, &roms, next) {
        if (rom->addr >= item->addr)
            continue;
        QTAILQ_INSERT_BEFORE(item, rom, next);
        return;
    }
    QTAILQ_INSERT_TAIL(&roms, rom, next);
}

int rom_add_file(const char *file, const char *fw_dir,
                 target_phys_addr_t addr, int32_t bootindex)
{
    Rom *rom;
    int rc, fd = -1;
    char devpath[100];

    rom = qemu_mallocz(sizeof(*rom));
    rom->name = qemu_strdup(file);
    rom->path = qemu_find_file(QEMU_FILE_TYPE_BIOS, rom->name);
    if (rom->path == NULL) {
        rom->path = qemu_strdup(file);
    }

    fd = open(rom->path, O_RDONLY | O_BINARY);
    if (fd == -1) {
        fprintf(stderr, "Could not open option rom '%s': %s\n",
                rom->path, strerror(errno));
        goto err;
    }

    if (fw_dir) {
        rom->fw_dir  = qemu_strdup(fw_dir);
        rom->fw_file = qemu_strdup(file);
    }
    rom->addr    = addr;
    rom->romsize = lseek(fd, 0, SEEK_END);
    rom->data    = qemu_mallocz(rom->romsize);
    lseek(fd, 0, SEEK_SET);
    rc = read(fd, rom->data, rom->romsize);
    if (rc != rom->romsize) {
        fprintf(stderr, "rom: file %-20s: read error: rc=%d (expected %zd)\n",
                rom->name, rc, rom->romsize);
        goto err;
    }
    close(fd);
    rom_insert(rom);
    if (rom->fw_file && fw_cfg) {
        const char *basename;
        char fw_file_name[56];

        basename = strrchr(rom->fw_file, '/');
        if (basename) {
            basename++;
        } else {
            basename = rom->fw_file;
        }
        snprintf(fw_file_name, sizeof(fw_file_name), "%s/%s", rom->fw_dir,
                 basename);
        fw_cfg_add_file(fw_cfg, fw_file_name, rom->data, rom->romsize);
        snprintf(devpath, sizeof(devpath), "/rom@%s", fw_file_name);
    } else {
        snprintf(devpath, sizeof(devpath), "/rom@" TARGET_FMT_plx, addr);
    }

    add_boot_device_path(bootindex, NULL, devpath);
    return 0;

err:
    if (fd != -1)
        close(fd);
    qemu_free(rom->data);
    qemu_free(rom->path);
    qemu_free(rom->name);
    qemu_free(rom);
    return -1;
}

int rom_add_blob(const char *name, const void *blob, size_t len,
                 target_phys_addr_t addr)
{
    Rom *rom;

    rom = qemu_mallocz(sizeof(*rom));
    rom->name    = qemu_strdup(name);
    rom->addr    = addr;
    rom->romsize = len;
    rom->data    = qemu_mallocz(rom->romsize);
    memcpy(rom->data, blob, len);
    rom_insert(rom);
    return 0;
}

int rom_add_vga(const char *file)
{
    return rom_add_file(file, "vgaroms", 0, -1);
}

int rom_add_option(const char *file, int32_t bootindex)
{
    return rom_add_file(file, "genroms", 0, bootindex);
}

static void rom_reset(void *unused)
{
    Rom *rom;

    QTAILQ_FOREACH(rom, &roms, next) {
        if (rom->fw_file) {
            continue;
        }
        if (rom->data == NULL) {
            continue;
        }
        cpu_physical_memory_write_rom(rom->addr, rom->data, rom->romsize);
        if (rom->isrom) {
            /* rom needs to be written only once */
            qemu_free(rom->data);
            rom->data = NULL;
        }
    }
}

int rom_load_all(void)
{
    target_phys_addr_t addr = 0;
    int memtype;
    Rom *rom;

    QTAILQ_FOREACH(rom, &roms, next) {
        if (rom->fw_file) {
            continue;
        }
        if (addr > rom->addr) {
            fprintf(stderr, "rom: requested regions overlap "
                    "(rom %s. free=0x" TARGET_FMT_plx
                    ", addr=0x" TARGET_FMT_plx ")\n",
                    rom->name, addr, rom->addr);
            return -1;
        }
        addr  = rom->addr;
        addr += rom->romsize;
        memtype = cpu_get_physical_page_desc(rom->addr) & (3 << IO_MEM_SHIFT);
        if (memtype == IO_MEM_ROM)
            rom->isrom = 1;
    }
    qemu_register_reset(rom_reset, NULL);
    roms_loaded = 1;
    return 0;
}

void rom_set_fw(void *f)
{
    fw_cfg = f;
}

static Rom *find_rom(target_phys_addr_t addr)
{
    Rom *rom;

    QTAILQ_FOREACH(rom, &roms, next) {
        if (rom->fw_file) {
            continue;
        }
        if (rom->addr > addr) {
            continue;
        }
        if (rom->addr + rom->romsize < addr) {
            continue;
        }
        return rom;
    }
    return NULL;
}

/*
 * Copies memory from registered ROMs to dest. Any memory that is contained in
 * a ROM between addr and addr + size is copied. Note that this can involve
 * multiple ROMs, which need not start at addr and need not end at addr + size.
 */
int rom_copy(uint8_t *dest, target_phys_addr_t addr, size_t size)
{
    target_phys_addr_t end = addr + size;
    uint8_t *s, *d = dest;
    size_t l = 0;
    Rom *rom;

    QTAILQ_FOREACH(rom, &roms, next) {
        if (rom->fw_file) {
            continue;
        }
        if (rom->addr + rom->romsize < addr) {
            continue;
        }
        if (rom->addr > end) {
            break;
        }
        if (!rom->data) {
            continue;
        }

        d = dest + (rom->addr - addr);
        s = rom->data;
        l = rom->romsize;

        if ((d + l) > (dest + size)) {
            l = dest - d;
        }

        memcpy(d, s, l);
    }

    return (d + l) - dest;
}

void *rom_ptr(target_phys_addr_t addr)
{
    Rom *rom;

    rom = find_rom(addr);
    if (!rom || !rom->data)
        return NULL;
    return rom->data + (addr - rom->addr);
}

void do_info_roms(Monitor *mon)
{
    Rom *rom;

    QTAILQ_FOREACH(rom, &roms, next) {
        if (!rom->fw_file) {
            monitor_printf(mon, "addr=" TARGET_FMT_plx
                           " size=0x%06zx mem=%s name=\"%s\" \n",
                           rom->addr, rom->romsize,
                           rom->isrom ? "rom" : "ram",
                           rom->name);
        } else {
            monitor_printf(mon, "fw=%s/%s"
                           " size=0x%06zx name=\"%s\" \n",
                           rom->fw_dir,
                           rom->fw_file,
                           rom->romsize,
                           rom->name);
        }
    }
}
