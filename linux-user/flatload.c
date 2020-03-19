/****************************************************************************/
/*
 *  QEMU bFLT binary loader.  Based on linux/fs/binfmt_flat.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *      Copyright (C) 2006 CodeSourcery.
 *	Copyright (C) 2000-2003 David McCullough <davidm@snapgear.com>
 *	Copyright (C) 2002 Greg Ungerer <gerg@snapgear.com>
 *	Copyright (C) 2002 SnapGear, by Paul Dale <pauli@snapgear.com>
 *	Copyright (C) 2000, 2001 Lineo, by David McCullough <davidm@lineo.com>
 *  based heavily on:
 *
 *  linux/fs/binfmt_aout.c:
 *      Copyright (C) 1991, 1992, 1996  Linus Torvalds
 *  linux/fs/binfmt_flat.c for 2.0 kernel
 *	    Copyright (C) 1998  Kenneth Albanowski <kjahds@kjahds.com>
 *	JAN/99 -- coded full program relocation (gerg@snapgear.com)
 */

/* ??? ZFLAT and shared library support is currently disabled.  */

/****************************************************************************/

#include "qemu/osdep.h"

#include "qemu.h"
#include "flat.h"
#include "target_flat.h"

//#define DEBUG

#ifdef DEBUG
#define	DBG_FLT(...)	printf(__VA_ARGS__)
#else
#define	DBG_FLT(...)
#endif

#define RELOC_FAILED 0xff00ff01		/* Relocation incorrect somewhere */
#define UNLOADED_LIB 0x7ff000ff		/* Placeholder for unused library */

struct lib_info {
    abi_ulong start_code;       /* Start of text segment */
    abi_ulong start_data;       /* Start of data segment */
    abi_ulong end_data;         /* Start of bss section */
    abi_ulong start_brk;        /* End of data segment */
    abi_ulong text_len;	        /* Length of text segment */
    abi_ulong entry;	        /* Start address for this module */
    abi_ulong build_date;       /* When this one was compiled */
    short loaded;		/* Has this library been loaded? */
};

#ifdef CONFIG_BINFMT_SHARED_FLAT
static int load_flat_shared_library(int id, struct lib_info *p);
#endif

struct linux_binprm;

/****************************************************************************/
/*
 * create_flat_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */

/* Push a block of strings onto the guest stack.  */
static abi_ulong copy_strings(abi_ulong p, int n, char **s)
{
    int len;

    while (n-- > 0) {
        len = strlen(s[n]) + 1;
        p -= len;
        memcpy_to_target(p, s[n], len);
    }

    return p;
}

static int target_pread(int fd, abi_ulong ptr, abi_ulong len,
                        abi_ulong offset)
{
    void *buf;
    int ret;

    buf = lock_user(VERIFY_WRITE, ptr, len, 0);
    if (!buf) {
        return -EFAULT;
    }
    ret = pread(fd, buf, len, offset);
    if (ret < 0) {
        ret = -errno;
    }
    unlock_user(buf, ptr, len);
    return ret;
}
/****************************************************************************/

#ifdef CONFIG_BINFMT_ZFLAT

#include <linux/zlib.h>

#define LBUFSIZE	4000

/* gzip flag byte */
#define ASCII_FLAG   0x01 /* bit 0 set: file probably ASCII text */
#define CONTINUATION 0x02 /* bit 1 set: continuation of multi-part gzip file */
#define EXTRA_FIELD  0x04 /* bit 2 set: extra field present */
#define ORIG_NAME    0x08 /* bit 3 set: original file name present */
#define COMMENT      0x10 /* bit 4 set: file comment present */
#define ENCRYPTED    0x20 /* bit 5 set: file is encrypted */
#define RESERVED     0xC0 /* bit 6,7:   reserved */

static int decompress_exec(
	struct linux_binprm *bprm,
	unsigned long offset,
	char *dst,
	long len,
	int fd)
{
	unsigned char *buf;
	z_stream strm;
	loff_t fpos;
	int ret, retval;

	DBG_FLT("decompress_exec(offset=%x,buf=%x,len=%x)\n",(int)offset, (int)dst, (int)len);

	memset(&strm, 0, sizeof(strm));
	strm.workspace = kmalloc(zlib_inflate_workspacesize(), GFP_KERNEL);
	if (strm.workspace == NULL) {
		DBG_FLT("binfmt_flat: no memory for decompress workspace\n");
		return -ENOMEM;
	}
	buf = kmalloc(LBUFSIZE, GFP_KERNEL);
	if (buf == NULL) {
		DBG_FLT("binfmt_flat: no memory for read buffer\n");
		retval = -ENOMEM;
		goto out_free;
	}

	/* Read in first chunk of data and parse gzip header. */
	fpos = offset;
	ret = bprm->file->f_op->read(bprm->file, buf, LBUFSIZE, &fpos);

	strm.next_in = buf;
	strm.avail_in = ret;
	strm.total_in = 0;

	retval = -ENOEXEC;

	/* Check minimum size -- gzip header */
	if (ret < 10) {
		DBG_FLT("binfmt_flat: file too small?\n");
		goto out_free_buf;
	}

	/* Check gzip magic number */
	if ((buf[0] != 037) || ((buf[1] != 0213) && (buf[1] != 0236))) {
		DBG_FLT("binfmt_flat: unknown compression magic?\n");
		goto out_free_buf;
	}

	/* Check gzip method */
	if (buf[2] != 8) {
		DBG_FLT("binfmt_flat: unknown compression method?\n");
		goto out_free_buf;
	}
	/* Check gzip flags */
	if ((buf[3] & ENCRYPTED) || (buf[3] & CONTINUATION) ||
	    (buf[3] & RESERVED)) {
		DBG_FLT("binfmt_flat: unknown flags?\n");
		goto out_free_buf;
	}

	ret = 10;
	if (buf[3] & EXTRA_FIELD) {
		ret += 2 + buf[10] + (buf[11] << 8);
		if (unlikely(LBUFSIZE == ret)) {
			DBG_FLT("binfmt_flat: buffer overflow (EXTRA)?\n");
			goto out_free_buf;
		}
	}
	if (buf[3] & ORIG_NAME) {
		for (; ret < LBUFSIZE && (buf[ret] != 0); ret++)
			;
		if (unlikely(LBUFSIZE == ret)) {
			DBG_FLT("binfmt_flat: buffer overflow (ORIG_NAME)?\n");
			goto out_free_buf;
		}
	}
	if (buf[3] & COMMENT) {
		for (;  ret < LBUFSIZE && (buf[ret] != 0); ret++)
			;
		if (unlikely(LBUFSIZE == ret)) {
			DBG_FLT("binfmt_flat: buffer overflow (COMMENT)?\n");
			goto out_free_buf;
		}
	}

	strm.next_in += ret;
	strm.avail_in -= ret;

	strm.next_out = dst;
	strm.avail_out = len;
	strm.total_out = 0;

	if (zlib_inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
		DBG_FLT("binfmt_flat: zlib init failed?\n");
		goto out_free_buf;
	}

	while ((ret = zlib_inflate(&strm, Z_NO_FLUSH)) == Z_OK) {
		ret = bprm->file->f_op->read(bprm->file, buf, LBUFSIZE, &fpos);
		if (ret <= 0)
			break;
                if (is_error(ret)) {
			break;
                }
		len -= ret;

		strm.next_in = buf;
		strm.avail_in = ret;
		strm.total_in = 0;
	}

	if (ret < 0) {
		DBG_FLT("binfmt_flat: decompression failed (%d), %s\n",
			ret, strm.msg);
		goto out_zlib;
	}

	retval = 0;
out_zlib:
	zlib_inflateEnd(&strm);
out_free_buf:
	kfree(buf);
out_free:
	kfree(strm.workspace);
out:
	return retval;
}

#endif /* CONFIG_BINFMT_ZFLAT */

/****************************************************************************/

static abi_ulong
calc_reloc(abi_ulong r, struct lib_info *p, int curid, int internalp)
{
    abi_ulong addr;
    int id;
    abi_ulong start_brk;
    abi_ulong start_data;
    abi_ulong text_len;
    abi_ulong start_code;

#ifdef CONFIG_BINFMT_SHARED_FLAT
#error needs checking
    if (r == 0)
        id = curid;	/* Relocs of 0 are always self referring */
    else {
        id = (r >> 24) & 0xff;	/* Find ID for this reloc */
        r &= 0x00ffffff;	/* Trim ID off here */
    }
    if (id >= MAX_SHARED_LIBS) {
        fprintf(stderr, "BINFMT_FLAT: reference 0x%x to shared library %d\n",
                (unsigned) r, id);
        goto failed;
    }
    if (curid != id) {
        if (internalp) {
            fprintf(stderr, "BINFMT_FLAT: reloc address 0x%x not "
                    "in same module (%d != %d)\n",
                    (unsigned) r, curid, id);
            goto failed;
        } else if (!p[id].loaded && is_error(load_flat_shared_library(id, p))) {
            fprintf(stderr, "BINFMT_FLAT: failed to load library %d\n", id);
            goto failed;
        }
        /* Check versioning information (i.e. time stamps) */
        if (p[id].build_date && p[curid].build_date
            && p[curid].build_date < p[id].build_date) {
            fprintf(stderr, "BINFMT_FLAT: library %d is younger than %d\n",
                    id, curid);
            goto failed;
        }
    }
#else
    id = 0;
#endif

    start_brk = p[id].start_brk;
    start_data = p[id].start_data;
    start_code = p[id].start_code;
    text_len = p[id].text_len;

    if (!flat_reloc_valid(r, start_brk - start_data + text_len)) {
        fprintf(stderr, "BINFMT_FLAT: reloc outside program 0x%x "
                "(0 - 0x%x/0x%x)\n",
               (int) r,(int)(start_brk-start_code),(int)text_len);
        goto failed;
    }

    if (r < text_len)			/* In text segment */
        addr = r + start_code;
    else					/* In data segment */
        addr = r - text_len + start_data;

    /* Range checked already above so doing the range tests is redundant...*/
    return(addr);

failed:
    abort();
    return RELOC_FAILED;
}

/****************************************************************************/

/* ??? This does not handle endianness correctly.  */
static void old_reloc(struct lib_info *libinfo, uint32_t rl)
{
#ifdef DEBUG
	const char *segment[] = { "TEXT", "DATA", "BSS", "*UNKNOWN*" };
#endif
	uint32_t *ptr;
        uint32_t offset;
        int reloc_type;

        offset = rl & 0x3fffffff;
        reloc_type = rl >> 30;
        /* ??? How to handle this?  */
#if defined(CONFIG_COLDFIRE)
	ptr = (uint32_t *) ((unsigned long) libinfo->start_code + offset);
#else
	ptr = (uint32_t *) ((unsigned long) libinfo->start_data + offset);
#endif

#ifdef DEBUG
	fprintf(stderr, "Relocation of variable at DATASEG+%x "
		"(address %p, currently %x) into segment %s\n",
		offset, ptr, (int)*ptr, segment[reloc_type]);
#endif

	switch (reloc_type) {
	case OLD_FLAT_RELOC_TYPE_TEXT:
		*ptr += libinfo->start_code;
		break;
	case OLD_FLAT_RELOC_TYPE_DATA:
		*ptr += libinfo->start_data;
		break;
	case OLD_FLAT_RELOC_TYPE_BSS:
		*ptr += libinfo->end_data;
		break;
	default:
		fprintf(stderr, "BINFMT_FLAT: Unknown relocation type=%x\n",
                        reloc_type);
		break;
	}
	DBG_FLT("Relocation became %x\n", (int)*ptr);
}

/****************************************************************************/

static int load_flat_file(struct linux_binprm * bprm,
		struct lib_info *libinfo, int id, abi_ulong *extra_stack)
{
    struct flat_hdr * hdr;
    abi_ulong textpos = 0, datapos = 0;
    abi_long result;
    abi_ulong realdatastart = 0;
    abi_ulong text_len, data_len, bss_len, stack_len, flags;
    abi_ulong extra;
    abi_ulong reloc = 0, rp;
    int i, rev, relocs = 0;
    abi_ulong fpos;
    abi_ulong start_code;
    abi_ulong indx_len;

    hdr = ((struct flat_hdr *) bprm->buf);		/* exec-header */

    text_len  = ntohl(hdr->data_start);
    data_len  = ntohl(hdr->data_end) - ntohl(hdr->data_start);
    bss_len   = ntohl(hdr->bss_end) - ntohl(hdr->data_end);
    stack_len = ntohl(hdr->stack_size);
    if (extra_stack) {
        stack_len += *extra_stack;
        *extra_stack = stack_len;
    }
    relocs    = ntohl(hdr->reloc_count);
    flags     = ntohl(hdr->flags);
    rev       = ntohl(hdr->rev);

    DBG_FLT("BINFMT_FLAT: Loading file: %s\n", bprm->filename);

    if (rev != FLAT_VERSION && rev != OLD_FLAT_VERSION) {
        fprintf(stderr, "BINFMT_FLAT: bad magic/rev (0x%x, need 0x%x)\n",
                rev, (int) FLAT_VERSION);
        return -ENOEXEC;
    }

    /* Don't allow old format executables to use shared libraries */
    if (rev == OLD_FLAT_VERSION && id != 0) {
        fprintf(stderr, "BINFMT_FLAT: shared libraries are not available\n");
        return -ENOEXEC;
    }

    /*
     * fix up the flags for the older format,  there were all kinds
     * of endian hacks,  this only works for the simple cases
     */
    if (rev == OLD_FLAT_VERSION && flat_old_ram_flag(flags))
        flags = FLAT_FLAG_RAM;

#ifndef CONFIG_BINFMT_ZFLAT
    if (flags & (FLAT_FLAG_GZIP|FLAT_FLAG_GZDATA)) {
        fprintf(stderr, "Support for ZFLAT executables is not enabled\n");
        return -ENOEXEC;
    }
#endif

    /*
     * calculate the extra space we need to map in
     */
    extra = relocs * sizeof(abi_ulong);
    if (extra < bss_len + stack_len)
        extra = bss_len + stack_len;

    /* Add space for library base pointers.  Make sure this does not
       misalign the  doesn't misalign the data segment.  */
    indx_len = MAX_SHARED_LIBS * sizeof(abi_ulong);
    indx_len = (indx_len + 15) & ~(abi_ulong)15;

    /*
     * there are a couple of cases here,  the separate code/data
     * case,  and then the fully copied to RAM case which lumps
     * it all together.
     */
    if ((flags & (FLAT_FLAG_RAM|FLAT_FLAG_GZIP)) == 0) {
        /*
         * this should give us a ROM ptr,  but if it doesn't we don't
         * really care
         */
        DBG_FLT("BINFMT_FLAT: ROM mapping of file (we hope)\n");

        textpos = target_mmap(0, text_len, PROT_READ|PROT_EXEC,
                              MAP_PRIVATE, bprm->fd, 0);
        if (textpos == -1) {
            fprintf(stderr, "Unable to mmap process text\n");
            return -1;
        }

        realdatastart = target_mmap(0, data_len + extra + indx_len,
                                    PROT_READ|PROT_WRITE|PROT_EXEC,
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (realdatastart == -1) {
            fprintf(stderr, "Unable to allocate RAM for process data\n");
            return realdatastart;
        }
        datapos = realdatastart + indx_len;

        DBG_FLT("BINFMT_FLAT: Allocated data+bss+stack (%d bytes): %x\n",
                        (int)(data_len + bss_len + stack_len), (int)datapos);

        fpos = ntohl(hdr->data_start);
#ifdef CONFIG_BINFMT_ZFLAT
        if (flags & FLAT_FLAG_GZDATA) {
            result = decompress_exec(bprm, fpos, (char *) datapos,
                                     data_len + (relocs * sizeof(abi_ulong)))
        } else
#endif
        {
            result = target_pread(bprm->fd, datapos,
                                  data_len + (relocs * sizeof(abi_ulong)),
                                  fpos);
        }
        if (result < 0) {
            fprintf(stderr, "Unable to read data+bss\n");
            return result;
        }

        reloc = datapos + (ntohl(hdr->reloc_start) - text_len);

    } else {

        textpos = target_mmap(0, text_len + data_len + extra + indx_len,
                              PROT_READ | PROT_EXEC | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (textpos == -1 ) {
            fprintf(stderr, "Unable to allocate RAM for process text/data\n");
            return -1;
        }

        realdatastart = textpos + ntohl(hdr->data_start);
        datapos = realdatastart + indx_len;
        reloc = (textpos + ntohl(hdr->reloc_start) + indx_len);

#ifdef CONFIG_BINFMT_ZFLAT
#error code needs checking
        /*
         * load it all in and treat it like a RAM load from now on
         */
        if (flags & FLAT_FLAG_GZIP) {
                result = decompress_exec(bprm, sizeof (struct flat_hdr),
                                 (((char *) textpos) + sizeof (struct flat_hdr)),
                                 (text_len + data_len + (relocs * sizeof(unsigned long))
                                          - sizeof (struct flat_hdr)),
                                 0);
                memmove((void *) datapos, (void *) realdatastart,
                                data_len + (relocs * sizeof(unsigned long)));
        } else if (flags & FLAT_FLAG_GZDATA) {
                fpos = 0;
                result = bprm->file->f_op->read(bprm->file,
                                (char *) textpos, text_len, &fpos);
                if (!is_error(result)) {
                        result = decompress_exec(bprm, text_len, (char *) datapos,
                                         data_len + (relocs * sizeof(unsigned long)), 0);
                }
        }
        else
#endif
        {
            result = target_pread(bprm->fd, textpos,
                                  text_len, 0);
            if (result >= 0) {
                result = target_pread(bprm->fd, datapos,
                    data_len + (relocs * sizeof(abi_ulong)),
                    ntohl(hdr->data_start));
            }
        }
        if (result < 0) {
            fprintf(stderr, "Unable to read code+data+bss\n");
            return result;
        }
    }

    DBG_FLT("Mapping is 0x%x, Entry point is 0x%x, data_start is 0x%x\n",
            (int)textpos, 0x00ffffff&ntohl(hdr->entry),
            ntohl(hdr->data_start));

    /* The main program needs a little extra setup in the task structure */
    start_code = textpos + sizeof (struct flat_hdr);

    DBG_FLT("%s %s: TEXT=%x-%x DATA=%x-%x BSS=%x-%x\n",
            id ? "Lib" : "Load", bprm->filename,
            (int) start_code, (int) (textpos + text_len),
            (int) datapos,
            (int) (datapos + data_len),
            (int) (datapos + data_len),
            (int) (((datapos + data_len + bss_len) + 3) & ~3));

    text_len -= sizeof(struct flat_hdr); /* the real code len */

    /* Store the current module values into the global library structure */
    libinfo[id].start_code = start_code;
    libinfo[id].start_data = datapos;
    libinfo[id].end_data = datapos + data_len;
    libinfo[id].start_brk = datapos + data_len + bss_len;
    libinfo[id].text_len = text_len;
    libinfo[id].loaded = 1;
    libinfo[id].entry = (0x00ffffff & ntohl(hdr->entry)) + textpos;
    libinfo[id].build_date = ntohl(hdr->build_date);

    /*
     * We just load the allocations into some temporary memory to
     * help simplify all this mumbo jumbo
     *
     * We've got two different sections of relocation entries.
     * The first is the GOT which resides at the beginning of the data segment
     * and is terminated with a -1.  This one can be relocated in place.
     * The second is the extra relocation entries tacked after the image's
     * data segment. These require a little more processing as the entry is
     * really an offset into the image which contains an offset into the
     * image.
     */
    if (flags & FLAT_FLAG_GOTPIC) {
        rp = datapos;
        while (1) {
            abi_ulong addr;
            if (get_user_ual(addr, rp))
                return -EFAULT;
            if (addr == -1)
                break;
            if (addr) {
                addr = calc_reloc(addr, libinfo, id, 0);
                if (addr == RELOC_FAILED)
                    return -ENOEXEC;
                if (put_user_ual(addr, rp))
                    return -EFAULT;
            }
            rp += sizeof(abi_ulong);
        }
    }

    /*
     * Now run through the relocation entries.
     * We've got to be careful here as C++ produces relocatable zero
     * entries in the constructor and destructor tables which are then
     * tested for being not zero (which will always occur unless we're
     * based from address zero).  This causes an endless loop as __start
     * is at zero.  The solution used is to not relocate zero addresses.
     * This has the negative side effect of not allowing a global data
     * reference to be statically initialised to _stext (I've moved
     * __start to address 4 so that is okay).
     */
    if (rev > OLD_FLAT_VERSION) {
        abi_ulong persistent = 0;
        for (i = 0; i < relocs; i++) {
            abi_ulong addr, relval;

            /* Get the address of the pointer to be
               relocated (of course, the address has to be
               relocated first).  */
            if (get_user_ual(relval, reloc + i * sizeof(abi_ulong)))
                return -EFAULT;
            relval = ntohl(relval);
            if (flat_set_persistent(relval, &persistent))
                continue;
            addr = flat_get_relocate_addr(relval);
            rp = calc_reloc(addr, libinfo, id, 1);
            if (rp == RELOC_FAILED)
                return -ENOEXEC;

            /* Get the pointer's value.  */
            if (get_user_ual(addr, rp))
                return -EFAULT;
            addr = flat_get_addr_from_rp(addr, relval, flags, &persistent);
            if (addr != 0) {
                /*
                 * Do the relocation.  PIC relocs in the data section are
                 * already in target order
                 */
                if ((flags & FLAT_FLAG_GOTPIC) == 0)
                    addr = ntohl(addr);
                addr = calc_reloc(addr, libinfo, id, 0);
                if (addr == RELOC_FAILED)
                    return -ENOEXEC;

                /* Write back the relocated pointer.  */
                if (flat_put_addr_at_rp(rp, addr, relval))
                    return -EFAULT;
            }
        }
    } else {
        for (i = 0; i < relocs; i++) {
            abi_ulong relval;
            if (get_user_ual(relval, reloc + i * sizeof(abi_ulong)))
                return -EFAULT;
            old_reloc(&libinfo[0], relval);
        }
    }

    /* zero the BSS.  */
    memset(g2h(datapos + data_len), 0, bss_len);

    return 0;
}


/****************************************************************************/
#ifdef CONFIG_BINFMT_SHARED_FLAT

/*
 * Load a shared library into memory.  The library gets its own data
 * segment (including bss) but not argv/argc/environ.
 */

static int load_flat_shared_library(int id, struct lib_info *libs)
{
	struct linux_binprm bprm;
	int res;
	char buf[16];

	/* Create the file name */
	sprintf(buf, "/lib/lib%d.so", id);

	/* Open the file up */
	bprm.filename = buf;
	bprm.file = open_exec(bprm.filename);
	res = PTR_ERR(bprm.file);
	if (IS_ERR(bprm.file))
		return res;

	res = prepare_binprm(&bprm);

        if (!is_error(res)) {
		res = load_flat_file(&bprm, libs, id, NULL);
        }
	if (bprm.file) {
		allow_write_access(bprm.file);
		fput(bprm.file);
		bprm.file = NULL;
	}
	return(res);
}

#endif /* CONFIG_BINFMT_SHARED_FLAT */

int load_flt_binary(struct linux_binprm *bprm, struct image_info *info)
{
    struct lib_info libinfo[MAX_SHARED_LIBS];
    abi_ulong p;
    abi_ulong stack_len;
    abi_ulong start_addr;
    abi_ulong sp;
    int res;
    int i, j;

    memset(libinfo, 0, sizeof(libinfo));
    /*
     * We have to add the size of our arguments to our stack size
     * otherwise it's too easy for users to create stack overflows
     * by passing in a huge argument list.  And yes,  we have to be
     * pedantic and include space for the argv/envp array as it may have
     * a lot of entries.
     */
    stack_len = 0;
    for (i = 0; i < bprm->argc; ++i) {
        /* the argv strings */
        stack_len += strlen(bprm->argv[i]);
    }
    for (i = 0; i < bprm->envc; ++i) {
        /* the envp strings */
        stack_len += strlen(bprm->envp[i]);
    }
    stack_len += (bprm->argc + 1) * 4; /* the argv array */
    stack_len += (bprm->envc + 1) * 4; /* the envp array */


    res = load_flat_file(bprm, libinfo, 0, &stack_len);
    if (is_error(res)) {
            return res;
    }

    /* Update data segment pointers for all libraries */
    for (i=0; i<MAX_SHARED_LIBS; i++) {
        if (libinfo[i].loaded) {
            abi_ulong p;
            p = libinfo[i].start_data;
            for (j=0; j<MAX_SHARED_LIBS; j++) {
                p -= 4;
                /* FIXME - handle put_user() failures */
                if (put_user_ual(libinfo[j].loaded
                                 ? libinfo[j].start_data
                                 : UNLOADED_LIB,
                                 p))
                    return -EFAULT;
            }
        }
    }

    p = ((libinfo[0].start_brk + stack_len + 3) & ~3) - 4;
    DBG_FLT("p=%x\n", (int)p);

    /* Copy argv/envp.  */
    p = copy_strings(p, bprm->envc, bprm->envp);
    p = copy_strings(p, bprm->argc, bprm->argv);
    /* Align stack.  */
    sp = p & ~(abi_ulong)(sizeof(abi_ulong) - 1);
    /* Enforce final stack alignment of 16 bytes.  This is sufficient
       for all current targets, and excess alignment is harmless.  */
    stack_len = bprm->envc + bprm->argc + 2;
    stack_len += flat_argvp_envp_on_stack() ? 2 : 0; /* arvg, argp */
    stack_len += 1; /* argc */
    stack_len *= sizeof(abi_ulong);
    sp -= (sp - stack_len) & 15;
    sp = loader_build_argptr(bprm->envc, bprm->argc, sp, p,
                             flat_argvp_envp_on_stack());

    /* Fake some return addresses to ensure the call chain will
     * initialise library in order for us.  We are required to call
     * lib 1 first, then 2, ... and finally the main program (id 0).
     */
    start_addr = libinfo[0].entry;

#ifdef CONFIG_BINFMT_SHARED_FLAT
#error here
    for (i = MAX_SHARED_LIBS-1; i>0; i--) {
            if (libinfo[i].loaded) {
                    /* Push previos first to call address */
                    --sp;
                    if (put_user_ual(start_addr, sp))
                        return -EFAULT;
                    start_addr = libinfo[i].entry;
            }
    }
#endif

    /* Stash our initial stack pointer into the mm structure */
    info->start_code = libinfo[0].start_code;
    info->end_code = libinfo[0].start_code = libinfo[0].text_len;
    info->start_data = libinfo[0].start_data;
    info->end_data = libinfo[0].end_data;
    info->start_brk = libinfo[0].start_brk;
    info->start_stack = sp;
    info->stack_limit = libinfo[0].start_brk;
    info->entry = start_addr;
    info->code_offset = info->start_code;
    info->data_offset = info->start_data - libinfo[0].text_len;

    DBG_FLT("start_thread(entry=0x%x, start_stack=0x%x)\n",
            (int)info->entry, (int)info->start_stack);

    return 0;
}
