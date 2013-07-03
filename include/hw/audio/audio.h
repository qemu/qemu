#ifndef HW_AUDIODEV_H
#define HW_AUDIODEV_H 1

void isa_register_soundhw(const char *name, const char *descr,
                          int (*init_isa)(ISABus *bus));

void pci_register_soundhw(const char *name, const char *descr,
                          int (*init_pci)(PCIBus *bus));

#endif
