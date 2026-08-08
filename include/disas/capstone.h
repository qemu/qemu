#ifndef QEMU_CAPSTONE_H
#define QEMU_CAPSTONE_H

#ifdef CONFIG_CAPSTONE

#define CAPSTONE_AARCH64_COMPAT_HEADER
#define CAPSTONE_SYSTEMZ_COMPAT_HEADER
#include <capstone.h>

#else

/* Just enough to allow backends to init without ifdefs.  */

#define CS_API_MAJOR     0

#define CS_ARCH_ARM     -1
#define CS_ARCH_ARM64   -1
#define CS_ARCH_MIPS    -1
#define CS_ARCH_X86     -1
#define CS_ARCH_PPC     -1
#define CS_ARCH_SPARC   -1
#define CS_ARCH_SYSZ    -1

#define CS_MODE_LITTLE_ENDIAN    0
#define CS_MODE_BIG_ENDIAN       0
#define CS_MODE_ARM              0
#define CS_MODE_16               0
#define CS_MODE_32               0
#define CS_MODE_64               0
#define CS_MODE_THUMB            0
#define CS_MODE_MCLASS           0
#define CS_MODE_V8               0
#define CS_MODE_MICRO            0
#define CS_MODE_MIPS3            0
#define CS_MODE_MIPS32R6         0
#define CS_MODE_MIPSGP64         0
#define CS_MODE_V9               0
#define CS_MODE_MIPS32           0
#define CS_MODE_MIPS64           0

#endif /* CONFIG_CAPSTONE */

#if CS_API_MAJOR < 5
#define CS_ARCH_RISCV           -1
#define CS_MODE_RISCV32          0
#define CS_MODE_RISCV64          0
#define CS_MODE_RISCV_C          0
#elif CS_API_MAJOR == 5
/* The C symbol name changed between v5 and v6 */
#define CS_MODE_RISCV_C          CS_MODE_RISCVC
#endif
#if CS_API_MAJOR < 6
#define CS_MODE_RISCV_FD         0
#define CS_MODE_RISCV_V          0
#define CS_MODE_RISCV_ZFINX      0
#define CS_MODE_RISCV_ZCMP_ZCMT_ZCE  0
#define CS_MODE_RISCV_ZICFISS    0
#define CS_MODE_RISCV_E          0
#define CS_MODE_RISCV_A          0
#define CS_MODE_RISCV_COREV      0
#define CS_MODE_RISCV_THEAD      0
#define CS_MODE_RISCV_SIFIVE     0
#define CS_MODE_RISCV_BITMANIP   0
#define CS_MODE_RISCV_ZBA        0
#define CS_MODE_RISCV_ZBB        0
#define CS_MODE_RISCV_ZBC        0
#define CS_MODE_RISCV_ZBKB       0
#define CS_MODE_RISCV_ZBKC       0
#define CS_MODE_RISCV_ZBKX       0
#define CS_MODE_RISCV_ZBS        0
#define CS_MODE_RISCV_VENTANA    0
#endif

#if CS_API_MAJOR < 5
#define CS_ARCH_SH              -1
#define CS_MODE_SHFPU            0
#define CS_MODE_SH4              0
#define CS_MODE_SH4A             0
#endif

#endif /* QEMU_CAPSTONE_H */
