#ifndef __AFL_QEMU_API_H__
#define __AFL_QEMU_API_H__

#include <stdint.h>

#if defined(TARGET_MIPS64) || defined(TARGET_AARCH64) || defined(TARGET_X86_64) || defined(TARGET_PPC64)
# define TARGET_LONG_BITS 64
#else
# define TARGET_LONG_BITS 32
#endif

/* see include/exec/cpu-defs.h */
#define TARGET_LONG_SIZE (TARGET_LONG_BITS / 8)

#if TARGET_LONG_SIZE == 4
typedef int32_t target_long;
typedef uint32_t target_ulong;
#elif TARGET_LONG_SIZE == 8
typedef int64_t target_long;
typedef uint64_t target_ulong;
#else
#error TARGET_LONG_SIZE undefined
#endif


struct x86_regs {

  uint32_t eax, ebx, ecx, edx, edi, esi, ebp;
  
  union {
    uint32_t eip;
    uint32_t pc;
  };
  union {
    uint32_t esp;
    uint32_t sp;
  };
  union {
    uint32_t eflags;
    uint32_t flags;
  };
  
  uint8_t xmm_regs[8][16];

};

struct x86_64_regs {

  uint64_t rax, rbx, rcx, rdx, rdi, rsi, rbp,
           r8, r9, r10, r11, r12, r13, r14, r15;
  
  union {
    uint64_t rip;
    uint64_t pc;
  };
  union {
    uint64_t rsp;
    uint64_t sp;
  };
  union {
    uint64_t rflags;
    uint64_t flags;
  };
  
  uint8_t zmm_regs[32][64];

};

struct arm_regs {

  uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10;
  
  union {
    uint32_t r11;
    uint32_t fp;
  };
  union {
    uint32_t r12;
    uint32_t ip;
  };
  union {
    uint32_t r13;
    uint32_t sp;
  };
  union {
    uint32_t r14;
    uint32_t lr;
  };
  union {
    uint32_t r15;
    uint32_t pc;
  };
  
  uint32_t cpsr;

  uint8_t vfp_zregs[32][16];
  uint32_t vfp_xregs[16];

};

struct arm64_regs {

  uint64_t x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10;
  
  union {
    uint64_t x11;
    uint32_t fp_32;
  };
  union {
    uint64_t x12;
    uint32_t ip_32;
  };
  union {
    uint64_t x13;
    uint32_t sp_32;
  };
  union {
    uint64_t x14;
    uint32_t lr_32;
  };
  union {
    uint64_t x15;
    uint32_t pc_32;
  };
  union {
    uint64_t x16;
    uint64_t ip0;
  };
  union {
    uint64_t x17;
    uint64_t ip1;
  };

  uint64_t x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
  
  union {
    uint64_t x29;
    uint64_t fp;
  };
  union {
    uint64_t x30;
    uint64_t lr;
  };
  union {
    uint64_t x31;
    uint64_t sp;
  };
  // the zero register is not saved here ofc
  
  uint64_t pc;
  
  uint32_t cpsr;

  uint8_t vfp_zregs[32][16*16];
  uint8_t vfp_pregs[17][32];
  uint32_t vfp_xregs[16];

};

/* MIPS_PATCH */
#if defined(TARGET_MIPS) || defined(TARGET_MIPS64)

// check standalone usage
//     if smth in pers hook goes wrong, check constants below with target/mips/cpu.h
#ifndef MIPS_CPU_H
#include <stdbool.h>
#include "../include/fpu/softfloat-types.h"

/* MSA Context */
#define MSA_WRLEN (128)
typedef union wr_t wr_t;
union wr_t {
    int8_t  b[MSA_WRLEN / 8];
    int16_t h[MSA_WRLEN / 16];
    int32_t w[MSA_WRLEN / 32];
    int64_t d[MSA_WRLEN / 64];
};
typedef union fpr_t fpr_t;
union fpr_t {
    float64  fd;   /* ieee double precision */
    float32  fs[2];/* ieee single precision */
    uint64_t d;    /* binary double fixed-point */
    uint32_t w[2]; /* binary single fixed-point */
/* FPU/MSA register mapping is not tested on big-endian hosts. */
    wr_t     wr;   /* vector data */
};
#define MIPS_DSP_ACC 4
#endif

struct mips_regs {
  target_ulong r0, at, v0, v1, a0, a1, a2, a3, t0, t1, t2, t3, t4, t5, t6, t7, s0,
      s1, s2, s3, s4, s5, s6, s7, t8, t9, k0, k1, gp, sp, fp, ra;
  #if defined(TARGET_MIPS64)
    /*
     * For CPUs using 128-bit GPR registers, we put the lower halves in gpr[])
     * and the upper halves in gpr_hi[].
     */
    uint64_t gpr_hi[32];
  #endif /* TARGET_MIPS64 */
  target_ulong HI[MIPS_DSP_ACC];
  target_ulong LO[MIPS_DSP_ACC];
  target_ulong ACX[MIPS_DSP_ACC];
  target_ulong PC;
  fpr_t    fpr[32];
};
#endif

#endif
