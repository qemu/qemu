/* This is the Linux kernel elf-loading code, ported into user space */

#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>

#include "gemu.h"

#include "linux_bin.h"
#include "elf.h"
#include "segment.h"

/* Necessary parameters */
#define	ALPHA_PAGE_SIZE 4096
#define	X86_PAGE_SIZE 4096

#define ALPHA_PAGE_MASK (~(ALPHA_PAGE_SIZE-1))
#define X86_PAGE_MASK (~(X86_PAGE_SIZE-1))

#define ALPHA_PAGE_ALIGN(addr) ((((addr)+ALPHA_PAGE_SIZE)-1)&ALPHA_PAGE_MASK)
#define X86_PAGE_ALIGN(addr) ((((addr)+X86_PAGE_SIZE)-1)&X86_PAGE_MASK)

#define NGROUPS 32

#define X86_ELF_EXEC_PAGESIZE X86_PAGE_SIZE
#define X86_ELF_PAGESTART(_v) ((_v) & ~(unsigned long)(X86_ELF_EXEC_PAGESIZE-1))
#define X86_ELF_PAGEOFFSET(_v) ((_v) & (X86_ELF_EXEC_PAGESIZE-1))

#define ALPHA_ELF_PAGESTART(_v) ((_v) & ~(unsigned long)(ALPHA_PAGE_SIZE-1))
#define ALPHA_ELF_PAGEOFFSET(_v) ((_v) & (ALPHA_PAGE_SIZE-1))

#define INTERPRETER_NONE 0
#define INTERPRETER_AOUT 1
#define INTERPRETER_ELF 2

#define DLINFO_ITEMS 12

/* Where we find X86 libraries... */
//#define X86_DEFAULT_LIB_DIR	"/usr/x86/"
#define X86_DEFAULT_LIB_DIR	"/"

//extern void * mmap4k();
#define mmap4k(a, b, c, d, e, f) mmap((void *)(a), b, c, d, e, f)

extern unsigned long x86_stack_size;

static int load_aout_interp(void * exptr, int interp_fd);

#ifdef BSWAP_NEEDED
static void bswap_ehdr(Elf32_Ehdr *ehdr)
{
    bswap16s(&ehdr->e_type);			/* Object file type */
    bswap16s(&ehdr->e_machine);		/* Architecture */
    bswap32s(&ehdr->e_version);		/* Object file version */
    bswap32s(&ehdr->e_entry);		/* Entry point virtual address */
    bswap32s(&ehdr->e_phoff);		/* Program header table file offset */
    bswap32s(&ehdr->e_shoff);		/* Section header table file offset */
    bswap32s(&ehdr->e_flags);		/* Processor-specific flags */
    bswap16s(&ehdr->e_ehsize);		/* ELF header size in bytes */
    bswap16s(&ehdr->e_phentsize);		/* Program header table entry size */
    bswap16s(&ehdr->e_phnum);		/* Program header table entry count */
    bswap16s(&ehdr->e_shentsize);		/* Section header table entry size */
    bswap16s(&ehdr->e_shnum);		/* Section header table entry count */
    bswap16s(&ehdr->e_shstrndx);		/* Section header string table index */
}

static void bswap_phdr(Elf32_Phdr *phdr)
{
    bswap32s(&phdr->p_type);			/* Segment type */
    bswap32s(&phdr->p_offset);		/* Segment file offset */
    bswap32s(&phdr->p_vaddr);		/* Segment virtual address */
    bswap32s(&phdr->p_paddr);		/* Segment physical address */
    bswap32s(&phdr->p_filesz);		/* Segment size in file */
    bswap32s(&phdr->p_memsz);		/* Segment size in memory */
    bswap32s(&phdr->p_flags);		/* Segment flags */
    bswap32s(&phdr->p_align);		/* Segment alignment */
}
#endif

static void * get_free_page(void)
{
    void *	retval;

    /* User-space version of kernel get_free_page.  Returns a page-aligned
     * page-sized chunk of memory.
     */
    retval = mmap4k(0, ALPHA_PAGE_SIZE, PROT_READ|PROT_WRITE, 
			MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

    if((long)retval == -1) {
	perror("get_free_page");
	exit(-1);
    }
    else {
	return(retval);
    }
}

static void free_page(void * pageaddr)
{
    (void)munmap(pageaddr, ALPHA_PAGE_SIZE);
}

/*
 * 'copy_string()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 *
 */
static unsigned long copy_strings(int argc,char ** argv,unsigned long *page,
                unsigned long p)
{
    char *tmp, *tmp1, *pag = NULL;
    int len, offset = 0;

    if (!p) {
	return 0;       /* bullet-proofing */
    }
    while (argc-- > 0) {
	if (!(tmp1 = tmp = get_user(argv+argc))) {
	    fprintf(stderr, "VFS: argc is wrong");
	    exit(-1);
	}
	while (get_user(tmp++));
	len = tmp - tmp1;
	if (p < len) {  /* this shouldn't happen - 128kB */
		return 0;
	}
	while (len) {
	    --p; --tmp; --len;
	    if (--offset < 0) {
		offset = p % X86_PAGE_SIZE;
		if (!(pag = (char *) page[p/X86_PAGE_SIZE]) &&
		    !(pag = (char *) page[p/X86_PAGE_SIZE] =
		      (unsigned long *) get_free_page())) {
			return 0;
		}
	    }
	    if (len == 0 || offset == 0) {
	        *(pag + offset) = get_user(tmp);
	    }
	    else {
	      int bytes_to_copy = (len > offset) ? offset : len;
	      tmp -= bytes_to_copy;
	      p -= bytes_to_copy;
	      offset -= bytes_to_copy;
	      len -= bytes_to_copy;
	      memcpy_fromfs(pag + offset, tmp, bytes_to_copy + 1);
	    }
	}
    }
    return p;
}

static int in_group_p(gid_t g)
{
    /* return TRUE if we're in the specified group, FALSE otherwise */
    int		ngroup;
    int		i;
    gid_t	grouplist[NGROUPS];

    ngroup = getgroups(NGROUPS, grouplist);
    for(i = 0; i < ngroup; i++) {
	if(grouplist[i] == g) {
	    return 1;
	}
    }
    return 0;
}

static int count(char ** vec)
{
    int		i;

    for(i = 0; *vec; i++) {
        vec++;
    }

    return(i);
}

static int prepare_binprm(struct linux_binprm *bprm)
{
    struct stat		st;
    int mode;
    int retval, id_change;

    if(fstat(bprm->fd, &st) < 0) {
	return(-errno);
    }

    mode = st.st_mode;
    if(!S_ISREG(mode)) {	/* Must be regular file */
	return(-EACCES);
    }
    if(!(mode & 0111)) {	/* Must have at least one execute bit set */
	return(-EACCES);
    }

    bprm->e_uid = geteuid();
    bprm->e_gid = getegid();
    id_change = 0;

    /* Set-uid? */
    if(mode & S_ISUID) {
    	bprm->e_uid = st.st_uid;
	if(bprm->e_uid != geteuid()) {
	    id_change = 1;
	}
    }

    /* Set-gid? */
    /*
     * If setgid is set but no group execute bit then this
     * is a candidate for mandatory locking, not a setgid
     * executable.
     */
    if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP)) {
	bprm->e_gid = st.st_gid;
	if (!in_group_p(bprm->e_gid)) {
		id_change = 1;
	}
    }

    memset(bprm->buf, 0, sizeof(bprm->buf));
    retval = lseek(bprm->fd, 0L, SEEK_SET);
    if(retval >= 0) {
        retval = read(bprm->fd, bprm->buf, 128);
    }
    if(retval < 0) {
	perror("prepare_binprm");
	exit(-1);
	/* return(-errno); */
    }
    else {
	return(retval);
    }
}

unsigned long setup_arg_pages(unsigned long p, struct linux_binprm * bprm,
						struct image_info * info)
{
    unsigned long stack_base;
    int i;
    extern unsigned long stktop;

    stack_base = X86_STACK_TOP - MAX_ARG_PAGES*X86_PAGE_SIZE;

    p += stack_base;
    if (bprm->loader) {
	bprm->loader += stack_base;
    }
    bprm->exec += stack_base;

    /* Create enough stack to hold everything.  If we don't use
     * it for args, we'll use it for something else...
     */
    /* XXX: on x86 MAP_GROWSDOWN only works if ESP <= address + 32, so
       we allocate a bigger stack. Need a better solution, for example
       by remapping the process stack directly at the right place */
    if(x86_stack_size >  MAX_ARG_PAGES*X86_PAGE_SIZE) {
        if((long)mmap4k((void *)(X86_STACK_TOP-x86_stack_size), x86_stack_size + X86_PAGE_SIZE,
    		     PROT_READ | PROT_WRITE,
		     MAP_GROWSDOWN | MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == -1) {
	    perror("stk mmap");
	    exit(-1);
	}
    }
    else {
        if((long)mmap4k((void *)stack_base, (MAX_ARG_PAGES+1)*X86_PAGE_SIZE,
    		     PROT_READ | PROT_WRITE,
		     MAP_GROWSDOWN | MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == -1) {
	    perror("stk mmap");
	    exit(-1);
	}
    }
    
    stktop = stack_base;

    for (i = 0 ; i < MAX_ARG_PAGES ; i++) {
	if (bprm->page[i]) {
	    info->rss++;

	    memcpy((void *)stack_base, (void *)bprm->page[i], X86_PAGE_SIZE);
	    free_page((void *)bprm->page[i]);
	}
	stack_base += X86_PAGE_SIZE;
    }
    return p;
}

static void set_brk(unsigned long start, unsigned long end)
{
	/* page-align the start and end addresses... */
        start = ALPHA_PAGE_ALIGN(start);
        end = ALPHA_PAGE_ALIGN(end);
        if (end <= start)
                return;
        if((long)mmap4k(start, end - start,
                PROT_READ | PROT_WRITE | PROT_EXEC,
                MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == -1) {
	    perror("cannot mmap brk");
	    exit(-1);
	}
}


/* We need to explicitly zero any fractional pages
   after the data section (i.e. bss).  This would
   contain the junk from the file that should not
   be in memory */


static void padzero(unsigned long elf_bss)
{
        unsigned long nbyte;
        char * fpnt;

        nbyte = elf_bss & (ALPHA_PAGE_SIZE-1);	/* was X86_PAGE_SIZE - JRP */
        if (nbyte) {
	    nbyte = ALPHA_PAGE_SIZE - nbyte;
	    fpnt = (char *) elf_bss;
	    do {
		*fpnt++ = 0;
	    } while (--nbyte);
        }
}

static unsigned int * create_elf_tables(char *p, int argc, int envc,
                                  struct elfhdr * exec,
                                  unsigned long load_addr,
                                  unsigned long interp_load_addr, int ibcs,
				  struct image_info *info)
{
        target_ulong *argv, *envp, *dlinfo;
        target_ulong *sp;

        /*
         * Force 16 byte alignment here for generality.
         */
        sp = (unsigned int *) (~15UL & (unsigned long) p);
        sp -= exec ? DLINFO_ITEMS*2 : 2;
        dlinfo = sp;
        sp -= envc+1;
        envp = sp;
        sp -= argc+1;
        argv = sp;
        if (!ibcs) {
                put_user(tswapl((target_ulong)envp),--sp);
                put_user(tswapl((target_ulong)argv),--sp);
        }

#define NEW_AUX_ENT(id, val) \
          put_user (tswapl(id), dlinfo++); \
          put_user (tswapl(val), dlinfo++)

        if (exec) { /* Put this here for an ELF program interpreter */
          struct elf_phdr * eppnt;
          eppnt = (struct elf_phdr *)((unsigned long)exec->e_phoff);

          NEW_AUX_ENT (AT_PHDR, (unsigned int)(load_addr + exec->e_phoff));
          NEW_AUX_ENT (AT_PHENT, (unsigned int)(sizeof (struct elf_phdr)));
          NEW_AUX_ENT (AT_PHNUM, (unsigned int)(exec->e_phnum));
          NEW_AUX_ENT (AT_PAGESZ, (unsigned int)(ALPHA_PAGE_SIZE));
          NEW_AUX_ENT (AT_BASE, (unsigned int)(interp_load_addr));
          NEW_AUX_ENT (AT_FLAGS, (unsigned int)0);
          NEW_AUX_ENT (AT_ENTRY, (unsigned int) exec->e_entry);
          NEW_AUX_ENT (AT_UID, (unsigned int) getuid());
          NEW_AUX_ENT (AT_EUID, (unsigned int) geteuid());
          NEW_AUX_ENT (AT_GID, (unsigned int) getgid());
          NEW_AUX_ENT (AT_EGID, (unsigned int) getegid());
        }
        NEW_AUX_ENT (AT_NULL, 0);
#undef NEW_AUX_ENT
        put_user(tswapl(argc),--sp);
        info->arg_start = (unsigned int)((unsigned long)p & 0xffffffff);
        while (argc-->0) {
                put_user(tswapl((target_ulong)p),argv++);
                while (get_user(p++)) /* nothing */ ;
        }
        put_user(0,argv);
        info->arg_end = info->env_start = (unsigned int)((unsigned long)p & 0xffffffff);
        while (envc-->0) {
                put_user(tswapl((target_ulong)p),envp++);
                while (get_user(p++)) /* nothing */ ;
        }
        put_user(0,envp);
        info->env_end = (unsigned int)((unsigned long)p & 0xffffffff);
        return sp;
}



static unsigned long load_elf_interp(struct elfhdr * interp_elf_ex,
				     int interpreter_fd,
				     unsigned long *interp_load_addr)
{
	struct elf_phdr *elf_phdata  =  NULL;
	struct elf_phdr *eppnt;
	unsigned long load_addr;
	int load_addr_set = 0;
	int retval;
	unsigned long last_bss, elf_bss;
	unsigned long error;
	int i;
	
	elf_bss = 0;
	last_bss = 0;
	error = 0;

	/* We put this here so that mmap will search for the *first*
	 * available memory...
	 */
	load_addr = INTERP_LOADADDR;
	
	/* First of all, some simple consistency checks */
	if ((interp_elf_ex->e_type != ET_EXEC && 
	    interp_elf_ex->e_type != ET_DYN) || 
	   !elf_check_arch(interp_elf_ex->e_machine)) {
		return ~0UL;
	}
	
	/* Now read in all of the header information */
	
	if (sizeof(struct elf_phdr) * interp_elf_ex->e_phnum > X86_PAGE_SIZE)
	    return ~0UL;
	
	elf_phdata =  (struct elf_phdr *) 
		malloc(sizeof(struct elf_phdr) * interp_elf_ex->e_phnum);

	if (!elf_phdata)
	  return ~0UL;
	
	/*
	 * If the size of this structure has changed, then punt, since
	 * we will be doing the wrong thing.
	 */
	if (interp_elf_ex->e_phentsize != sizeof(struct elf_phdr))
	  {
	    free(elf_phdata);
	    return ~0UL;
	  }

	retval = lseek(interpreter_fd, interp_elf_ex->e_phoff, SEEK_SET);
	if(retval >= 0) {
	    retval = read(interpreter_fd,
			   (char *) elf_phdata,
			   sizeof(struct elf_phdr) * interp_elf_ex->e_phnum);
	}
	
	if (retval < 0) {
		perror("load_elf_interp");
		exit(-1);
		free (elf_phdata);
		return retval;
 	}
#ifdef BSWAP_NEEDED
	eppnt = elf_phdata;
	for (i=0; i<interp_elf_ex->e_phnum; i++, eppnt++) {
            bswap_phdr(eppnt);
        }
#endif
	eppnt = elf_phdata;
	for(i=0; i<interp_elf_ex->e_phnum; i++, eppnt++)
	  if (eppnt->p_type == PT_LOAD) {
	    int elf_type = MAP_PRIVATE | MAP_DENYWRITE;
	    int elf_prot = 0;
	    unsigned long vaddr = 0;
	    unsigned long k;

	    if (eppnt->p_flags & PF_R) elf_prot =  PROT_READ;
	    if (eppnt->p_flags & PF_W) elf_prot |= PROT_WRITE;
	    if (eppnt->p_flags & PF_X) elf_prot |= PROT_EXEC;
	    if (interp_elf_ex->e_type == ET_EXEC || load_addr_set) {
	    	elf_type |= MAP_FIXED;
	    	vaddr = eppnt->p_vaddr;
	    }
	    error = (unsigned long)mmap4k(load_addr+X86_ELF_PAGESTART(vaddr),
		 eppnt->p_filesz + X86_ELF_PAGEOFFSET(eppnt->p_vaddr),
		 elf_prot,
		 elf_type,
		 interpreter_fd,
		 eppnt->p_offset - X86_ELF_PAGEOFFSET(eppnt->p_vaddr));
	    
	    if (error > -1024UL) {
	      /* Real error */
	      close(interpreter_fd);
	      free(elf_phdata);
	      return ~0UL;
	    }

	    if (!load_addr_set && interp_elf_ex->e_type == ET_DYN) {
	      load_addr = error;
	      load_addr_set = 1;
	    }

	    /*
	     * Find the end of the file  mapping for this phdr, and keep
	     * track of the largest address we see for this.
	     */
	    k = load_addr + eppnt->p_vaddr + eppnt->p_filesz;
	    if (k > elf_bss) elf_bss = k;

	    /*
	     * Do the same thing for the memory mapping - between
	     * elf_bss and last_bss is the bss section.
	     */
	    k = load_addr + eppnt->p_memsz + eppnt->p_vaddr;
	    if (k > last_bss) last_bss = k;
	  }
	
	/* Now use mmap to map the library into memory. */

	close(interpreter_fd);

	/*
	 * Now fill out the bss section.  First pad the last page up
	 * to the page boundary, and then perform a mmap to make sure
	 * that there are zeromapped pages up to and including the last
	 * bss page.
	 */
	padzero(elf_bss);
	elf_bss = X86_ELF_PAGESTART(elf_bss + ALPHA_PAGE_SIZE - 1); /* What we have mapped so far */

	/* Map the last of the bss segment */
	if (last_bss > elf_bss) {
	  mmap4k(elf_bss, last_bss-elf_bss,
		  PROT_READ|PROT_WRITE|PROT_EXEC,
		  MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	}
	free(elf_phdata);

	*interp_load_addr = load_addr;
	return ((unsigned long) interp_elf_ex->e_entry) + load_addr;
}



static int load_elf_binary(struct linux_binprm * bprm, struct target_pt_regs * regs,
                           struct image_info * info)
{
    struct elfhdr elf_ex;
    struct elfhdr interp_elf_ex;
    struct exec interp_ex;
    int interpreter_fd = -1; /* avoid warning */
    unsigned long load_addr;
    int load_addr_set = 0;
    unsigned int interpreter_type = INTERPRETER_NONE;
    unsigned char ibcs2_interpreter;
    int i;
    void * mapped_addr;
    struct elf_phdr * elf_ppnt;
    struct elf_phdr *elf_phdata;
    unsigned long elf_bss, k, elf_brk;
    int retval;
    char * elf_interpreter;
    unsigned long elf_entry, interp_load_addr = 0;
    int status;
    unsigned long start_code, end_code, end_data;
    unsigned long elf_stack;
    char passed_fileno[6];

    ibcs2_interpreter = 0;
    status = 0;
    load_addr = 0;
    elf_ex = *((struct elfhdr *) bprm->buf);          /* exec-header */
#ifdef BSWAP_NEEDED
    bswap_ehdr(&elf_ex);
#endif

    if (elf_ex.e_ident[0] != 0x7f ||
	strncmp(&elf_ex.e_ident[1], "ELF",3) != 0) {
	    return  -ENOEXEC;
    }

    /* First of all, some simple consistency checks */
    if ((elf_ex.e_type != ET_EXEC && elf_ex.e_type != ET_DYN) ||
       				(! elf_check_arch(elf_ex.e_machine))) {
	    return -ENOEXEC;
    }

    /* Now read in all of the header information */

    elf_phdata = (struct elf_phdr *)malloc(elf_ex.e_phentsize*elf_ex.e_phnum);
    if (elf_phdata == NULL) {
	return -ENOMEM;
    }

    retval = lseek(bprm->fd, elf_ex.e_phoff, SEEK_SET);
    if(retval > 0) {
	retval = read(bprm->fd, (char *) elf_phdata, 
				elf_ex.e_phentsize * elf_ex.e_phnum);
    }

    if (retval < 0) {
	perror("load_elf_binary");
	exit(-1);
	free (elf_phdata);
	return -errno;
    }

#ifdef BSWAP_NEEDED
    elf_ppnt = elf_phdata;
    for (i=0; i<elf_ex.e_phnum; i++, elf_ppnt++) {
        bswap_phdr(elf_ppnt);
    }
#endif
    elf_ppnt = elf_phdata;

    elf_bss = 0;
    elf_brk = 0;


    elf_stack = ~0UL;
    elf_interpreter = NULL;
    start_code = ~0UL;
    end_code = 0;
    end_data = 0;

    for(i=0;i < elf_ex.e_phnum; i++) {
	if (elf_ppnt->p_type == PT_INTERP) {
	    if ( elf_interpreter != NULL )
	    {
		free (elf_phdata);
		free(elf_interpreter);
		close(bprm->fd);
		return -EINVAL;
	    }

	    /* This is the program interpreter used for
	     * shared libraries - for now assume that this
	     * is an a.out format binary
	     */

	    elf_interpreter = (char *)malloc(elf_ppnt->p_filesz+strlen(X86_DEFAULT_LIB_DIR));

	    if (elf_interpreter == NULL) {
		free (elf_phdata);
		close(bprm->fd);
		return -ENOMEM;
	    }

	    strcpy(elf_interpreter, X86_DEFAULT_LIB_DIR);
	    retval = lseek(bprm->fd, elf_ppnt->p_offset, SEEK_SET);
	    if(retval >= 0) {
		retval = read(bprm->fd, 
			      elf_interpreter+strlen(X86_DEFAULT_LIB_DIR), 
			      elf_ppnt->p_filesz);
	    }
	    if(retval < 0) {
	 	perror("load_elf_binary2");
		exit(-1);
	    }	

	    /* If the program interpreter is one of these two,
	       then assume an iBCS2 image. Otherwise assume
	       a native linux image. */

	    /* JRP - Need to add X86 lib dir stuff here... */

	    if (strcmp(elf_interpreter,"/usr/lib/libc.so.1") == 0 ||
		strcmp(elf_interpreter,"/usr/lib/ld.so.1") == 0) {
	      ibcs2_interpreter = 1;
	    }

#if 0
	    printf("Using ELF interpreter %s\n", elf_interpreter);
#endif
	    if (retval >= 0) {
		retval = open(elf_interpreter, O_RDONLY);
		if(retval >= 0) {
		    interpreter_fd = retval;
		}
		else {
		    perror(elf_interpreter);
		    exit(-1);
		    /* retval = -errno; */
		}
	    }

	    if (retval >= 0) {
		retval = lseek(interpreter_fd, 0, SEEK_SET);
		if(retval >= 0) {
		    retval = read(interpreter_fd,bprm->buf,128);
		}
	    }
	    if (retval >= 0) {
		interp_ex = *((struct exec *) bprm->buf); /* aout exec-header */
		interp_elf_ex=*((struct elfhdr *) bprm->buf); /* elf exec-header */
	    }
	    if (retval < 0) {
		perror("load_elf_binary3");
		exit(-1);
		free (elf_phdata);
		free(elf_interpreter);
		close(bprm->fd);
		return retval;
	    }
	}
	elf_ppnt++;
    }

    /* Some simple consistency checks for the interpreter */
    if (elf_interpreter){
	interpreter_type = INTERPRETER_ELF | INTERPRETER_AOUT;

	/* Now figure out which format our binary is */
	if ((N_MAGIC(interp_ex) != OMAGIC) && (N_MAGIC(interp_ex) != ZMAGIC) &&
	    	(N_MAGIC(interp_ex) != QMAGIC)) {
	  interpreter_type = INTERPRETER_ELF;
	}

	if (interp_elf_ex.e_ident[0] != 0x7f ||
	    	strncmp(&interp_elf_ex.e_ident[1], "ELF",3) != 0) {
	    interpreter_type &= ~INTERPRETER_ELF;
	}

	if (!interpreter_type) {
	    free(elf_interpreter);
	    free(elf_phdata);
	    close(bprm->fd);
	    return -ELIBBAD;
	}
    }

    /* OK, we are done with that, now set up the arg stuff,
       and then start this sucker up */

    if (!bprm->sh_bang) {
	char * passed_p;

	if (interpreter_type == INTERPRETER_AOUT) {
	    sprintf(passed_fileno, "%d", bprm->fd);
	    passed_p = passed_fileno;

	    if (elf_interpreter) {
		bprm->p = copy_strings(1,&passed_p,bprm->page,bprm->p);
		bprm->argc++;
	    }
	}
	if (!bprm->p) {
	    if (elf_interpreter) {
	        free(elf_interpreter);
	    }
	    free (elf_phdata);
	    close(bprm->fd);
	    return -E2BIG;
	}
    }

    /* OK, This is the point of no return */
    info->end_data = 0;
    info->end_code = 0;
    info->start_mmap = (unsigned long)ELF_START_MMAP;
    info->mmap = 0;
    elf_entry = (unsigned long) elf_ex.e_entry;

    /* Do this so that we can load the interpreter, if need be.  We will
       change some of these later */
    info->rss = 0;
    bprm->p = setup_arg_pages(bprm->p, bprm, info);
    info->start_stack = bprm->p;

    /* Now we do a little grungy work by mmaping the ELF image into
     * the correct location in memory.  At this point, we assume that
     * the image should be loaded at fixed address, not at a variable
     * address.
     */



    for(i = 0, elf_ppnt = elf_phdata; i < elf_ex.e_phnum; i++, elf_ppnt++) {
	if (elf_ppnt->p_type == PT_LOAD) {
	    int elf_prot = 0;
	    if (elf_ppnt->p_flags & PF_R) elf_prot |= PROT_READ;
	    if (elf_ppnt->p_flags & PF_W) elf_prot |= PROT_WRITE;
	    if (elf_ppnt->p_flags & PF_X) elf_prot |= PROT_EXEC;

	    mapped_addr = mmap4k(X86_ELF_PAGESTART(elf_ppnt->p_vaddr),
		    (elf_ppnt->p_filesz +
			    X86_ELF_PAGEOFFSET(elf_ppnt->p_vaddr)),
		    elf_prot,
		    (MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE),
		    bprm->fd,
		    (elf_ppnt->p_offset - 
			    X86_ELF_PAGEOFFSET(elf_ppnt->p_vaddr)));

	    if((unsigned long)mapped_addr == 0xffffffffffffffff) {
		perror("mmap");
		exit(-1);
	    }



#ifdef LOW_ELF_STACK
	    if (X86_ELF_PAGESTART(elf_ppnt->p_vaddr) < elf_stack)
		    elf_stack = X86_ELF_PAGESTART(elf_ppnt->p_vaddr);
#endif

	    if (!load_addr_set) {
	        load_addr = elf_ppnt->p_vaddr - elf_ppnt->p_offset;
	        load_addr_set = 1;
	    }
	    k = elf_ppnt->p_vaddr;
	    if (k < start_code) start_code = k;
	    k = elf_ppnt->p_vaddr + elf_ppnt->p_filesz;
	    if (k > elf_bss) elf_bss = k;
#if 1
	    if ((elf_ppnt->p_flags & PF_X) && end_code <  k)
#else
	    if ( !(elf_ppnt->p_flags & PF_W) && end_code <  k)
#endif
		    end_code = k;
	    if (end_data < k) end_data = k;
	    k = elf_ppnt->p_vaddr + elf_ppnt->p_memsz;
	    if (k > elf_brk) elf_brk = k;
	}
    }

    if (elf_interpreter) {
	if (interpreter_type & 1) {
	    elf_entry = load_aout_interp(&interp_ex, interpreter_fd);
	}
	else if (interpreter_type & 2) {
	    elf_entry = load_elf_interp(&interp_elf_ex, interpreter_fd,
					    &interp_load_addr);
	}

	close(interpreter_fd);
	free(elf_interpreter);

	if (elf_entry == ~0UL) {
	    printf("Unable to load interpreter\n");
	    free(elf_phdata);
	    exit(-1);
	    return 0;
	}
    }

    free(elf_phdata);

    if (interpreter_type != INTERPRETER_AOUT) close(bprm->fd);
    info->personality = (ibcs2_interpreter ? PER_SVR4 : PER_LINUX);

#ifdef LOW_ELF_STACK
    info->start_stack = bprm->p = elf_stack - 4;
#endif
    bprm->p = (unsigned long)
      create_elf_tables((char *)bprm->p,
		    bprm->argc,
		    bprm->envc,
		    (interpreter_type == INTERPRETER_ELF ? &elf_ex : NULL),
		    load_addr,
		    interp_load_addr,
		    (interpreter_type == INTERPRETER_AOUT ? 0 : 1),
		    info);
    if (interpreter_type == INTERPRETER_AOUT)
      info->arg_start += strlen(passed_fileno) + 1;
    info->start_brk = info->brk = elf_brk;
    info->end_code = end_code;
    info->start_code = start_code;
    info->end_data = end_data;
    info->start_stack = bprm->p;

    /* Calling set_brk effectively mmaps the pages that we need for the bss and break
       sections */
    set_brk(elf_bss, elf_brk);

    padzero(elf_bss);

#if 0
    printf("(start_brk) %x\n" , info->start_brk);
    printf("(end_code) %x\n" , info->end_code);
    printf("(start_code) %x\n" , info->start_code);
    printf("(end_data) %x\n" , info->end_data);
    printf("(start_stack) %x\n" , info->start_stack);
    printf("(brk) %x\n" , info->brk);
#endif

    if ( info->personality == PER_SVR4 )
    {
	    /* Why this, you ask???  Well SVr4 maps page 0 as read-only,
	       and some applications "depend" upon this behavior.
	       Since we do not have the power to recompile these, we
	       emulate the SVr4 behavior.  Sigh.  */
	    mapped_addr = mmap4k(NULL, ALPHA_PAGE_SIZE, PROT_READ | PROT_EXEC,
			    MAP_FIXED | MAP_PRIVATE, -1, 0);
    }

#ifdef ELF_PLAT_INIT
    /*
     * The ABI may specify that certain registers be set up in special
     * ways (on i386 %edx is the address of a DT_FINI function, for
     * example.  This macro performs whatever initialization to
     * the regs structure is required.
     */
    ELF_PLAT_INIT(regs);
#endif


    info->entry = elf_entry;

    return 0;
}



int elf_exec(const char * filename, char ** argv, char ** envp, 
             struct target_pt_regs * regs, struct image_info *infop)
{
        struct linux_binprm bprm;
        int retval;
        int i;

        bprm.p = X86_PAGE_SIZE*MAX_ARG_PAGES-sizeof(unsigned int);
        for (i=0 ; i<MAX_ARG_PAGES ; i++)       /* clear page-table */
                bprm.page[i] = 0;
        retval = open(filename, O_RDONLY);
        if (retval == -1) {
	    perror(filename);
	    exit(-1);
            /* return retval; */
	}
	else {
	    bprm.fd = retval;
	}
        bprm.filename = (char *)filename;
        bprm.sh_bang = 0;
        bprm.loader = 0;
        bprm.exec = 0;
        bprm.dont_iput = 0;
	bprm.argc = count(argv);
	bprm.envc = count(envp);

        retval = prepare_binprm(&bprm);

        if(retval>=0) {
	    bprm.p = copy_strings(1, &bprm.filename, bprm.page, bprm.p);
	    bprm.exec = bprm.p;
	    bprm.p = copy_strings(bprm.envc,envp,bprm.page,bprm.p);
	    bprm.p = copy_strings(bprm.argc,argv,bprm.page,bprm.p);
	    if (!bprm.p) {
		retval = -E2BIG;
	    }
        }

        if(retval>=0) {
	    retval = load_elf_binary(&bprm,regs,infop);
	}
        if(retval>=0) {
	    /* success.  Initialize important registers */
	    regs->esp = infop->start_stack;
	    regs->eip = infop->entry;
	    return retval;
	}

        /* Something went wrong, return the inode and free the argument pages*/
        for (i=0 ; i<MAX_ARG_PAGES ; i++) {
	    free_page((void *)bprm.page[i]);
	}
        return(retval);
}


static int load_aout_interp(void * exptr, int interp_fd)
{
    printf("a.out interpreter not yet supported\n");
    return(0);
}

