#ifndef PCI_SLOTID_CAP_H
#define PCI_SLOTID_CAP_H


int slotid_cap_init(PCIDevice *dev, int nslots,
                    uint8_t chassis,
                    unsigned offset,
                    Error **errp);
void slotid_cap_cleanup(PCIDevice *dev);

#endif
