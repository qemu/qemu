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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include "qemu-common.h"
#include "disas.h"
#include "sysemu.h"
#include "uboot_image.h"

#include <zlib.h>

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

/* return the amount read, just like fread.  0 may mean error or eof */
int fread_targphys(target_phys_addr_t dst_addr, size_t nbytes, FILE *f)
{
    uint8_t buf[4096];
    target_phys_addr_t dst_begin = dst_addr;
    size_t want, did;

    while (nbytes) {
	want = nbytes > sizeof(buf) ? sizeof(buf) : nbytes;
	did = fread(buf, 1, want, f);
	if (did != want) break;

	cpu_physical_memory_write_rom(dst_addr, buf, did);
	dst_addr += did;
	nbytes -= did;
    }
    return dst_addr - dst_begin;
}

/* returns 0 on error, 1 if ok */
int fread_targphys_ok(target_phys_addr_t dst_addr, size_t nbytes, FILE *f)
{
    return fread_targphys(dst_addr, nbytes, f) == nbytes;
}

/* read()-like version */
int read_targphys(int fd, target_phys_addr_t dst_addr, size_t nbytes)
{
    uint8_t buf[4096];
    target_phys_addr_t dst_begin = dst_addr;
    size_t want, did;

    while (nbytes) {
	want = nbytes > sizeof(buf) ? sizeof(buf) : nbytes;
	did = read(fd, buf, want);
	if (did != want) break;

	cpu_physical_memory_write_rom(dst_addr, buf, did);
	dst_addr += did;
	nbytes -= did;
    }
    return dst_addr - dst_begin;
}

/* return the size or -1 if error */
int load_image_targphys(const char *filename,
			target_phys_addr_t addr, int max_sz)
{
    FILE *f;
    size_t got;

    f = fopen(filename, "rb");
    if (!f) return -1;

    got = fread_targphys(addr, max_sz, f);
    if (ferror(f)) { fclose(f); return -1; }
    fclose(f);

    return got;
}

void pstrcpy_targphys(target_phys_addr_t dest, int buf_size,
                      const char *source)
{
    static const uint8_t nul_byte = 0;
    const char *nulp;

    if (buf_size <= 0) return;
    nulp = memchr(source, 0, buf_size);
    if (nulp) {
	cpu_physical_memory_write_rom(dest, (uint8_t *)source,
                                      (nulp - source) + 1);
    } else {
	cpu_physical_memory_write_rom(dest, (uint8_t *)source, buf_size - 1);
	cpu_physical_memory_write_rom(dest, &nul_byte, 1);
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

#ifdef BSWAP_NEEDED
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
#else
#define bswap_ahdr(x) do { } while (0)
#endif

#define N_MAGIC(exec) ((exec).a_info & 0xffff)
#define OMAGIC 0407
#define NMAGIC 0410
#define ZMAGIC 0413
#define QMAGIC 0314
#define _N_HDROFF(x) (1024 - sizeof (struct exec))
#define N_TXTOFF(x)							\
    (N_MAGIC(x) == ZMAGIC ? _N_HDROFF((x)) + sizeof (struct exec) :	\
     (N_MAGIC(x) == QMAGIC ? 0 : sizeof (struct exec)))
#define N_TXTADDR(x) (N_MAGIC(x) == QMAGIC ? TARGET_PAGE_SIZE : 0)
#define N_DATOFF(x) (N_TXTOFF(x) + (x).a_text)
#define _N_SEGMENT_ROUND(x) (((x) + TARGET_PAGE_SIZE - 1) & ~(TARGET_PAGE_SIZE - 1))

#define _N_TXTENDADDR(x) (N_TXTADDR(x)+(x).a_text)

#define N_DATADDR(x) \
    (N_MAGIC(x)==OMAGIC? (_N_TXTENDADDR(x)) \
     : (_N_SEGMENT_ROUND (_N_TXTENDADDR(x))))


int load_aout(const char *filename, target_phys_addr_t addr, int max_sz)
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

    bswap_ahdr(&e);

    magic = N_MAGIC(e);
    switch (magic) {
    case ZMAGIC:
    case QMAGIC:
    case OMAGIC:
        if (e.a_text + e.a_data > max_sz)
            goto fail;
	lseek(fd, N_TXTOFF(e), SEEK_SET);
	size = read_targphys(fd, addr, e.a_text + e.a_data);
	if (size < 0)
	    goto fail;
	break;
    case NMAGIC:
        if (N_DATADDR(e) + e.a_data > max_sz)
            goto fail;
	lseek(fd, N_TXTOFF(e), SEEK_SET);
	size = read_targphys(fd, addr, e.a_text);
	if (size < 0)
	    goto fail;
	ret = read_targphys(fd, addr + N_DATADDR(e), e.a_data);
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
    if (!ptr)
        return NULL;
    if (read(fd, ptr, size) != size) {
        qemu_free(ptr);
        return NULL;
    }
    return ptr;
}


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
int load_elf(const char *filename, int64_t address_offset,
             uint64_t *pentry, uint64_t *lowaddr, uint64_t *highaddr)
{
    int fd, data_order, host_data_order, must_swab, ret;
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
#ifdef WORDS_BIGENDIAN
    data_order = ELFDATA2MSB;
#else
    data_order = ELFDATA2LSB;
#endif
    must_swab = data_order != e_ident[EI_DATA];

#ifdef TARGET_WORDS_BIGENDIAN
    host_data_order = ELFDATA2MSB;
#else
    host_data_order = ELFDATA2LSB;
#endif
    if (host_data_order != e_ident[EI_DATA])
        return -1;

    lseek(fd, 0, SEEK_SET);
    if (e_ident[EI_CLASS] == ELFCLASS64) {
        ret = load_elf64(fd, address_offset, must_swab, pentry,
                         lowaddr, highaddr);
    } else {
        ret = load_elf32(fd, address_offset, must_swab, pentry,
                         lowaddr, highaddr);
    }

    close(fd);
    return ret;

 fail:
    close(fd);
    return -1;
}

static void bswap_uboot_header(uboot_image_header_t *hdr)
{
#ifndef WORDS_BIGENDIAN
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

static void zfree(void *x, void *addr, unsigned nb)
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
    s.zfree = (free_func)zfree;

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
int load_uboot(const char *filename, target_ulong *ep, target_ulong *loadaddr,
               int *is_linux)
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

    /* TODO: Implement Multi-File images.  */
    if (hdr->ih_type == IH_TYPE_MULTI) {
        fprintf(stderr, "Unable to load multi-file u-boot images\n");
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
        if (hdr->ih_type == IH_TYPE_KERNEL && hdr->ih_os == IH_OS_LINUX)
            *is_linux = 1;
        else
            *is_linux = 0;
    }

    *ep = hdr->ih_ep;
    data = qemu_malloc(hdr->ih_size);
    if (!data)
        goto out;

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

    cpu_physical_memory_write_rom(hdr->ih_load, data, hdr->ih_size);

    if (loadaddr)
        *loadaddr = hdr->ih_load;

    ret = hdr->ih_size;

out:
    if (data)
        qemu_free(data);
    close(fd);
    return ret;
}
