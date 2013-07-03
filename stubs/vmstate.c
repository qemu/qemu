#include "qemu-common.h"
#include "migration/vmstate.h"

const VMStateDescription vmstate_dummy = {};

int vmstate_register_with_alias_id(DeviceState *dev,
                                   int instance_id,
                                   const VMStateDescription *vmsd,
                                   void *base, int alias_id,
                                   int required_for_version)
{
    return 0;
}

void vmstate_unregister(DeviceState *dev,
                        const VMStateDescription *vmsd,
                        void *opaque)
{
}
