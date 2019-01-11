#define __NR_SYSCALL_BASE	0x900000
#define __NR_exit1			(__NR_SYSCALL_BASE+  1)
#define __NR_write			(__NR_SYSCALL_BASE+  4)

#define __sys2(x) #x
#define __sys1(x) __sys2(x)

#ifndef __syscall
#define __syscall(name) "swi\t" __sys1(__NR_##name) "\n\t"
#endif

#define __syscall_return(type, res)					\
do {									\
        return (type) (res);						\
} while (0)

#define _syscall0(type,name)						\
type name(void) {							\
  long __res;								\
  __asm__ __volatile__ (						\
  __syscall(name)							\
  "mov %0,r0"								\
  :"=r" (__res) : : "r0","lr");						\
  __syscall_return(type,__res);						\
}

#define _syscall1(type,name,type1,arg1)					\
type name(type1 arg1) {							\
  long __res;								\
  __asm__ __volatile__ (						\
  "mov\tr0,%1\n\t"							\
  __syscall(name)							\
  "mov %0,r0"								\
        : "=r" (__res)							\
        : "r" ((long)(arg1))						\
        : "r0","lr");							\
  __syscall_return(type,__res);						\
}

#define _syscall2(type,name,type1,arg1,type2,arg2)			\
type name(type1 arg1,type2 arg2) {					\
  long __res;								\
  __asm__ __volatile__ (						\
  "mov\tr0,%1\n\t"							\
  "mov\tr1,%2\n\t"							\
  __syscall(name)							\
  "mov\t%0,r0"								\
        : "=r" (__res)							\
        : "r" ((long)(arg1)),"r" ((long)(arg2))				\
        : "r0","r1","lr");						\
  __syscall_return(type,__res);						\
}


#define _syscall3(type,name,type1,arg1,type2,arg2,type3,arg3)		\
type name(type1 arg1,type2 arg2,type3 arg3) {				\
  long __res;								\
  __asm__ __volatile__ (						\
  "mov\tr0,%1\n\t"							\
  "mov\tr1,%2\n\t"							\
  "mov\tr2,%3\n\t"							\
  __syscall(name)							\
  "mov\t%0,r0"								\
        : "=r" (__res)							\
        : "r" ((long)(arg1)),"r" ((long)(arg2)),"r" ((long)(arg3))	\
        : "r0","r1","r2","lr");						\
  __syscall_return(type,__res);						\
}


#define _syscall4(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4)		\
type name(type1 arg1, type2 arg2, type3 arg3, type4 arg4) {				\
  long __res;										\
  __asm__ __volatile__ (								\
  "mov\tr0,%1\n\t"									\
  "mov\tr1,%2\n\t"									\
  "mov\tr2,%3\n\t"									\
  "mov\tr3,%4\n\t"									\
  __syscall(name)									\
  "mov\t%0,r0"										\
        : "=r" (__res)									\
        : "r" ((long)(arg1)),"r" ((long)(arg2)),"r" ((long)(arg3)),"r" ((long)(arg4))	\
        : "r0","r1","r2","r3","lr");							\
  __syscall_return(type,__res);								\
}


#define _syscall5(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4,type5,arg5)	\
type name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, type5 arg5) {			\
  long __res;										\
  __asm__ __volatile__ (								\
  "mov\tr0,%1\n\t"									\
  "mov\tr1,%2\n\t"									\
  "mov\tr2,%3\n\t"									\
  "mov\tr3,%4\n\t"									\
  "mov\tr4,%5\n\t"									\
  __syscall(name)									\
  "mov\t%0,r0"										\
        : "=r" (__res)									\
        : "r" ((long)(arg1)),"r" ((long)(arg2)),"r" ((long)(arg3)),"r" ((long)(arg4)),	\
          "r" ((long)(arg5))								\
        : "r0","r1","r2","r3","r4","lr");						\
  __syscall_return(type,__res);								\
}

_syscall1(int,exit1,int,status);
_syscall3(int,write,int,fd,const char *,buf, int, len);

void _start(void)
{
    write(1, "Hello World\n", 12);
    exit1(0);
}
