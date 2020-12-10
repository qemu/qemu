#ifndef __AFL_QEMU_API_H__
#define __AFL_QEMU_API_H__

#include <stdint.h>

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
  
  uint8_t xmm_regs[16][8];

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
  
  uint8_t zmm_regs[64][32];

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

#endif
