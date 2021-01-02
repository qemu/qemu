#ifndef HW_VT82C686_H
#define HW_VT82C686_H


#define TYPE_VT82C686B_SUPERIO "vt82c686b-superio"
#define TYPE_VIA_AC97 "via-ac97"
#define TYPE_VIA_MC97 "via-mc97"

/* vt82c686.c */
ISABus *vt82c686b_isa_init(PCIBus * bus, int devfn);
I2CBus *vt82c686b_pm_init(PCIBus *bus, int devfn, uint32_t smb_io_base,
                          qemu_irq sci_irq);

#endif
