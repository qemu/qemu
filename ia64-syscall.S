/* derived from glibc sysdeps/unix/sysv/linux/ia64/sysdep.S */
	
#define __ASSEMBLY__
	
#include <asm/asmmacro.h>
#include <asm/unistd.h>

ENTRY(__syscall_error)
        .prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(0)
        alloc   r33=ar.pfs, 0, 4, 0, 0
        mov     r32=rp
        .body
        mov     r35=r8
        mov     r34=r1
        ;;
        br.call.sptk.many b0 = __errno_location
.Lret0:         /* force new bundle */
        st4     [r8]=r35
        mov     r1=r34
        mov     rp=r32
        mov     r8=-1
        mov     ar.pfs=r33
        br.ret.sptk.few b0
END(__syscall_error)
	
GLOBAL_ENTRY(__ia64_syscall)
        mov r15=r37             /* syscall number */
        break __BREAK_SYSCALL
        cmp.eq p6,p0=-1,r10     /* r10 = -1 on error */
(p6)    br.cond.spnt.few __syscall_error
        br.ret.sptk.few b0
.endp __ia64_syscall
