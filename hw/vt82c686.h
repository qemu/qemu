#ifndef HW_VT82C686_H
#define HW_VT82C686_H

/* vt82c686.c */
int vt82c686b_init(PCIBus * bus, int devfn);
void vt82c686b_ac97_init(PCIBus *bus, int devfn);
void vt82c686b_mc97_init(PCIBus *bus, int devfn);
i2c_bus *vt82c686b_pm_init(PCIBus *bus, int devfn, uint32_t smb_io_base,
            qemu_irq sci_irq);

#endif
