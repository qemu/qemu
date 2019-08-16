#ifndef QEMU_ARCH_INIT_H
#define QEMU_ARCH_INIT_H


enum {
    QEMU_ARCH_ALL = -1,
    QEMU_ARCH_ALPHA = (1 << 0),
    QEMU_ARCH_ARM = (1 << 1),
    QEMU_ARCH_CRIS = (1 << 2),
    QEMU_ARCH_I386 = (1 << 3),
    QEMU_ARCH_M68K = (1 << 4),
    QEMU_ARCH_LM32 = (1 << 5),
    QEMU_ARCH_MICROBLAZE = (1 << 6),
    QEMU_ARCH_MIPS = (1 << 7),
    QEMU_ARCH_PPC = (1 << 8),
    QEMU_ARCH_S390X = (1 << 9),
    QEMU_ARCH_SH4 = (1 << 10),
    QEMU_ARCH_SPARC = (1 << 11),
    QEMU_ARCH_XTENSA = (1 << 12),
    QEMU_ARCH_OPENRISC = (1 << 13),
    QEMU_ARCH_UNICORE32 = (1 << 14),
    QEMU_ARCH_MOXIE = (1 << 15),
    QEMU_ARCH_TRICORE = (1 << 16),
    QEMU_ARCH_NIOS2 = (1 << 17),
    QEMU_ARCH_HPPA = (1 << 18),
    QEMU_ARCH_RISCV = (1 << 19),
};

extern const uint32_t arch_type;

int kvm_available(void);
int xen_available(void);

#endif
