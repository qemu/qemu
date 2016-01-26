#include "qemu/osdep.h"
#include "hw/pci/slotid_cap.h"
#include "hw/pci/pci.h"
#include "qemu/error-report.h"

#define SLOTID_CAP_LENGTH 4
#define SLOTID_NSLOTS_SHIFT ctz32(PCI_SID_ESR_NSLOTS)

int slotid_cap_init(PCIDevice *d, int nslots,
                    uint8_t chassis,
                    unsigned offset)
{
    int cap;
    if (!chassis) {
        error_report("Bridge chassis not specified. Each bridge is required "
                     "to be assigned a unique chassis id > 0.");
        return -EINVAL;
    }
    if (nslots < 0 || nslots > (PCI_SID_ESR_NSLOTS >> SLOTID_NSLOTS_SHIFT)) {
        /* TODO: error report? */
        return -EINVAL;
    }

    cap = pci_add_capability(d, PCI_CAP_ID_SLOTID, offset, SLOTID_CAP_LENGTH);
    if (cap < 0) {
        return cap;
    }
    /* We make each chassis unique, this way each bridge is First in Chassis */
    d->config[cap + PCI_SID_ESR] = PCI_SID_ESR_FIC |
        (nslots << SLOTID_NSLOTS_SHIFT);
    d->cmask[cap + PCI_SID_ESR] = 0xff;
    d->config[cap + PCI_SID_CHASSIS_NR] = chassis;
    /* Note: Chassis number register is non-volatile,
       so we don't reset it. */
    /* TODO: store in eeprom? */
    d->wmask[cap + PCI_SID_CHASSIS_NR] = 0xff;

    d->cap_present |= QEMU_PCI_CAP_SLOTID;
    return 0;
}

void slotid_cap_cleanup(PCIDevice *d)
{
    /* TODO: cleanup config space? */
    d->cap_present &= ~QEMU_PCI_CAP_SLOTID;
}
