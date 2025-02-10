#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/s390x/virtio-ccw-md.h"

void virtio_ccw_md_pre_plug(VirtIOMDCcw *vmd, MachineState *ms, Error **errp)
{
    error_setg(errp, "virtio based memory devices not supported");
}

void virtio_ccw_md_plug(VirtIOMDCcw *vmd, MachineState *ms, Error **errp)
{
    error_setg(errp, "virtio based memory devices not supported");
}

void virtio_ccw_md_unplug_request(VirtIOMDCcw *vmd, MachineState *ms,
                                  Error **errp)
{
    error_setg(errp, "virtio based memory devices not supported");
}

void virtio_ccw_md_unplug(VirtIOMDCcw *vmd, MachineState *ms, Error **errp)
{
    error_setg(errp, "virtio based memory devices not supported");
}
