#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/virtio/virtio-md-pci.h"

void virtio_md_pci_pre_plug(VirtIOMDPCI *vmd, MachineState *ms, Error **errp)
{
    error_setg(errp, "virtio based memory devices not supported");
}

void virtio_md_pci_plug(VirtIOMDPCI *vmd, MachineState *ms, Error **errp)
{
    error_setg(errp, "virtio based memory devices not supported");
}

void virtio_md_pci_unplug_request(VirtIOMDPCI *vmd, MachineState *ms,
                                  Error **errp)
{
    error_setg(errp, "virtio based memory devices not supported");
}

void virtio_md_pci_unplug(VirtIOMDPCI *vmd, MachineState *ms, Error **errp)
{
    error_setg(errp, "virtio based memory devices not supported");
}
