#ifndef QEMU_MULTIBOOT_H
#define QEMU_MULTIBOOT_H

#include "hw/nvram/fw_cfg.h"
#include "hw/i386/x86.h"

int load_multiboot(X86MachineState *x86ms,
                   FWCfgState *fw_cfg,
                   FILE *f,
                   const char *kernel_filename,
                   const char *initrd_filename,
                   const char *kernel_cmdline,
                   int kernel_file_size,
                   uint8_t *header);

#endif
