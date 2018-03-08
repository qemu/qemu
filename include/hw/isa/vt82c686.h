#ifndef HW_VT82C686_H
#define HW_VT82C686_H

#define TYPE_VT82C686B_SUPERIO "vt82c686b-superio"

/* vt82c686.c */
ISABus *vt82c686b_isa_init(PCIBus * bus, int devfn);
void vt82c686b_ac97_init(PCIBus *bus, int devfn);
void vt82c686b_mc97_init(PCIBus *bus, int devfn);
I2CBus *vt82c686b_pm_init(PCIBus *bus, int devfn, uint32_t smb_io_base,
                          qemu_irq sci_irq);

#endif
