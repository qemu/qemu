#include "vl.h"
#include "disas.h"
#include "exec-all.h"

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


#define ELF_CLASS   ELFCLASS32
#define ELF_DATA    ELFDATA2MSB
#define ELF_ARCH    EM_SPARC

#include "elf.h"

#ifndef BSWAP_NEEDED
#define bswap_ehdr32(e) do { } while (0)
#define bswap_phdr32(e) do { } while (0)
#define bswap_shdr32(e) do { } while (0)
#define bswap_sym32(e) do { } while (0)
#ifdef TARGET_SPARC64
#define bswap_ehdr64(e) do { } while (0)
#define bswap_phdr64(e) do { } while (0)
#define bswap_shdr64(e) do { } while (0)
#define bswap_sym64(e) do { } while (0)
#endif
#endif

#define SZ		32
#define elf_word        uint32_t
#define bswapSZs	bswap32s
#include "elf_ops.h"

#ifdef TARGET_SPARC64
#undef elfhdr
#undef elf_phdr
#undef elf_shdr
#undef elf_sym
#undef elf_note
#undef elf_word
#undef bswapSZs
#undef SZ
#define elfhdr		elf64_hdr
#define elf_phdr	elf64_phdr
#define elf_note	elf64_note
#define elf_shdr	elf64_shdr
#define elf_sym		elf64_sym
#define elf_word        uint64_t
#define bswapSZs	bswap64s
#define SZ		64
#include "elf_ops.h"
#endif

int load_elf(const char *filename, uint8_t *addr)
{
    struct elf32_hdr ehdr;
    int retval, fd;
    Elf32_Half machine;

    fd = open(filename, O_RDONLY | O_BINARY);
    if (fd < 0)
	goto error;

    retval = read(fd, &ehdr, sizeof(ehdr));
    if (retval < 0)
	goto error;

    if (ehdr.e_ident[0] != 0x7f || ehdr.e_ident[1] != 'E'
	|| ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F')
	goto error;
    machine = tswap16(ehdr.e_machine);
    if (machine == EM_SPARC || machine == EM_SPARC32PLUS) {
	struct elf32_phdr phdr;

	bswap_ehdr32(&ehdr);

	if (find_phdr32(&ehdr, fd, &phdr, PT_LOAD))
	    goto error;
	retval = read_program32(fd, &phdr, addr, ehdr.e_entry);
	if (retval < 0)
	    goto error;
	load_symbols32(&ehdr, fd);
    }
#ifdef TARGET_SPARC64
    else if (machine == EM_SPARCV9) {
	struct elf64_hdr ehdr64;
	struct elf64_phdr phdr;

	lseek(fd, 0, SEEK_SET);

	retval = read(fd, &ehdr64, sizeof(ehdr64));
	if (retval < 0)
	    goto error;

	bswap_ehdr64(&ehdr64);

	if (find_phdr64(&ehdr64, fd, &phdr, PT_LOAD))
	    goto error;
	retval = read_program64(fd, &phdr, addr, ehdr64.e_entry);
	if (retval < 0)
	    goto error;
	load_symbols64(&ehdr64, fd);
    }
#endif

    close(fd);
    return retval;
 error:
    close(fd);
    return -1;
}

int load_aout(const char *filename, uint8_t *addr)
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
	lseek(fd, N_TXTOFF(e), SEEK_SET);
	size = read(fd, addr, e.a_text + e.a_data);
	if (size < 0)
	    goto fail;
	break;
    case NMAGIC:
	lseek(fd, N_TXTOFF(e), SEEK_SET);
	size = read(fd, addr, e.a_text);
	if (size < 0)
	    goto fail;
	ret = read(fd, addr + N_DATADDR(e), e.a_data);
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

