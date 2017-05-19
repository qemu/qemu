#ifndef HW_AUDIO_H
#define HW_AUDIO_H

void isa_register_soundhw(const char *name, const char *descr,
                          int (*init_isa)(ISABus *bus));

void pci_register_soundhw(const char *name, const char *descr,
                          int (*init_pci)(PCIBus *bus));

void soundhw_init(void);
void select_soundhw(const char *optarg);

#endif
