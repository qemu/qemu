#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "hw/display/ramfb.h"
#include "ui/console.h"
#include "qom/object.h"

typedef struct RAMFBStandaloneState RAMFBStandaloneState;
DECLARE_INSTANCE_CHECKER(RAMFBStandaloneState, RAMFB,
                         TYPE_RAMFB_DEVICE)

struct RAMFBStandaloneState {
    SysBusDevice parent_obj;
    QemuConsole *con;
    RAMFBState *state;
    bool migrate;
};

static void display_update_wrapper(void *dev)
{
    RAMFBStandaloneState *ramfb = RAMFB(dev);

    if (0 /* native driver active */) {
        /* non-standalone device would run native display update here */;
    } else {
        ramfb_display_update(ramfb->con, ramfb->state);
    }
}

static const GraphicHwOps wrapper_ops = {
    .gfx_update = display_update_wrapper,
};

static void ramfb_realizefn(DeviceState *dev, Error **errp)
{
    RAMFBStandaloneState *ramfb = RAMFB(dev);

    ramfb->con = graphic_console_init(dev, 0, &wrapper_ops, dev);
    ramfb->state = ramfb_setup(errp);
}

static bool migrate_needed(void *opaque)
{
    RAMFBStandaloneState *ramfb = RAMFB(opaque);

    return ramfb->migrate;
}

static const VMStateDescription ramfb_dev_vmstate = {
    .name = "ramfb-dev",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = migrate_needed,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_POINTER(state, RAMFBStandaloneState, ramfb_vmstate, RAMFBState),
        VMSTATE_END_OF_LIST()
    }
};

static Property ramfb_properties[] = {
    DEFINE_PROP_BOOL("x-migrate", RAMFBStandaloneState, migrate,  true),
    DEFINE_PROP_END_OF_LIST(),
};

static void ramfb_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    dc->vmsd = &ramfb_dev_vmstate;
    dc->realize = ramfb_realizefn;
    dc->desc = "ram framebuffer standalone device";
    dc->user_creatable = true;
    device_class_set_props(dc, ramfb_properties);
}

static const TypeInfo ramfb_info = {
    .name          = TYPE_RAMFB_DEVICE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RAMFBStandaloneState),
    .class_init    = ramfb_class_initfn,
};

static void ramfb_register_types(void)
{
    type_register_static(&ramfb_info);
}

type_init(ramfb_register_types)
