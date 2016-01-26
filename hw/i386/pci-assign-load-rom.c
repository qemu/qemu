/*
 * This is splited from hw/i386/kvm/pci-assign.c
 */
#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "qemu/error-report.h"
#include "ui/console.h"
#include "hw/loader.h"
#include "monitor/monitor.h"
#include "qemu/range.h"
#include "sysemu/sysemu.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci-assign.h"

/*
 * Scan the assigned devices for the devices that have an option ROM, and then
 * load the corresponding ROM data to RAM. If an error occurs while loading an
 * option ROM, we just ignore that option ROM and continue with the next one.
 */
void *pci_assign_dev_load_option_rom(PCIDevice *dev, struct Object *owner,
                                     int *size, unsigned int domain,
                                     unsigned int bus, unsigned int slot,
                                     unsigned int function)
{
    char name[32], rom_file[64];
    FILE *fp;
    uint8_t val;
    struct stat st;
    void *ptr = NULL;

    /* If loading ROM from file, pci handles it */
    if (dev->romfile || !dev->rom_bar) {
        return NULL;
    }

    snprintf(rom_file, sizeof(rom_file),
             "/sys/bus/pci/devices/%04x:%02x:%02x.%01x/rom",
             domain, bus, slot, function);

    if (stat(rom_file, &st)) {
        return NULL;
    }

    /* Write "1" to the ROM file to enable it */
    fp = fopen(rom_file, "r+");
    if (fp == NULL) {
        error_report("pci-assign: Cannot open %s: %s", rom_file, strerror(errno));
        return NULL;
    }
    val = 1;
    if (fwrite(&val, 1, 1, fp) != 1) {
        goto close_rom;
    }
    fseek(fp, 0, SEEK_SET);

    snprintf(name, sizeof(name), "%s.rom", object_get_typename(owner));
    memory_region_init_ram(&dev->rom, owner, name, st.st_size, &error_abort);
    vmstate_register_ram(&dev->rom, &dev->qdev);
    ptr = memory_region_get_ram_ptr(&dev->rom);
    memset(ptr, 0xff, st.st_size);

    if (!fread(ptr, 1, st.st_size, fp)) {
        error_report("pci-assign: Cannot read from host %s", rom_file);
        error_printf("Device option ROM contents are probably invalid "
                     "(check dmesg).\nSkip option ROM probe with rombar=0, "
                     "or load from file with romfile=\n");
        goto close_rom;
    }

    pci_register_bar(dev, PCI_ROM_SLOT, 0, &dev->rom);
    dev->has_rom = true;
    *size = st.st_size;
close_rom:
    /* Write "0" to disable ROM */
    fseek(fp, 0, SEEK_SET);
    val = 0;
    if (!fwrite(&val, 1, 1, fp)) {
        DEBUG("%s\n", "Failed to disable pci-sysfs rom file");
    }
    fclose(fp);

    return ptr;
}
