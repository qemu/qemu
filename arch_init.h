#ifndef QEMU_ARCH_INIT_H
#define QEMU_ARCH_INIT_H

extern const char arch_config_name[];

enum {
    QEMU_ARCH_ALL = -1,
    QEMU_ARCH_ALPHA = 1,
    QEMU_ARCH_ARM = 2,
    QEMU_ARCH_CRIS = 4,
    QEMU_ARCH_I386 = 8,
    QEMU_ARCH_M68K = 16,
    QEMU_ARCH_MICROBLAZE = 32,
    QEMU_ARCH_MIPS = 64,
    QEMU_ARCH_PPC = 128,
    QEMU_ARCH_S390X = 256,
    QEMU_ARCH_SH4 = 512,
    QEMU_ARCH_SPARC = 1024,
};

extern const uint32_t arch_type;

void select_soundhw(const char *optarg);
int ram_save_live(Monitor *mon, QEMUFile *f, int stage, void *opaque);
int ram_load(QEMUFile *f, void *opaque, int version_id);
void do_acpitable_option(const char *optarg);
void do_smbios_option(const char *optarg);
void cpudef_init(void);
int audio_available(void);
void audio_init(qemu_irq *isa_pic, PCIBus *pci_bus);
int kvm_available(void);
int xen_available(void);

#endif
