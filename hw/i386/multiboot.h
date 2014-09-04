#ifndef QEMU_MULTIBOOT_H
#define QEMU_MULTIBOOT_H

#include "hw/nvram/fw_cfg.h"

int load_multiboot(FWCfgState *fw_cfg,
                   FILE *f,
                   const char *kernel_filename,
                   const char *initrd_filename,
                   const char *kernel_cmdline,
                   int kernel_file_size,
                   uint8_t *header);

#endif
