#ifndef QEMU_ARCH_INIT_H
#define QEMU_ARCH_INIT_H

#include "qmp-commands.h"
#include "qemu/option.h"

enum {
    QEMU_ARCH_ALL = -1,
    QEMU_ARCH_ALPHA = 1,
    QEMU_ARCH_ARM = 2,
    QEMU_ARCH_CRIS = 4,
    QEMU_ARCH_I386 = 8,
    QEMU_ARCH_M68K = 16,
    QEMU_ARCH_LM32 = 32,
    QEMU_ARCH_MICROBLAZE = 64,
    QEMU_ARCH_MIPS = 128,
    QEMU_ARCH_PPC = 256,
    QEMU_ARCH_S390X = 512,
    QEMU_ARCH_SH4 = 1024,
    QEMU_ARCH_SPARC = 2048,
    QEMU_ARCH_XTENSA = 4096,
    QEMU_ARCH_OPENRISC = 8192,
    QEMU_ARCH_UNICORE32 = 0x4000,
    QEMU_ARCH_MOXIE = 0x8000,
};

extern const uint32_t arch_type;

void select_soundhw(const char *optarg);
void do_acpitable_option(const QemuOpts *opts);
void do_smbios_option(QemuOpts *opts);
void cpudef_init(void);
void audio_init(void);
int tcg_available(void);
int kvm_available(void);
int xen_available(void);

CpuDefinitionInfoList *arch_query_cpu_definitions(Error **errp);

#endif
