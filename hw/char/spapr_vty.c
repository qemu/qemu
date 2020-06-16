#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "cpu.h"
#include "migration/vmstate.h"
#include "chardev/char-fe.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_vio.h"
#include "hw/qdev-properties.h"

#define VTERM_BUFSIZE   16

typedef struct SpaprVioVty {
    SpaprVioDevice sdev;
    CharBackend chardev;
    uint32_t in, out;
    uint8_t buf[VTERM_BUFSIZE];
} SpaprVioVty;

#define TYPE_VIO_SPAPR_VTY_DEVICE "spapr-vty"
#define VIO_SPAPR_VTY_DEVICE(obj) \
     OBJECT_CHECK(SpaprVioVty, (obj), TYPE_VIO_SPAPR_VTY_DEVICE)

static int vty_can_receive(void *opaque)
{
    SpaprVioVty *dev = VIO_SPAPR_VTY_DEVICE(opaque);

    return VTERM_BUFSIZE - (dev->in - dev->out);
}

static void vty_receive(void *opaque, const uint8_t *buf, int size)
{
    SpaprVioVty *dev = VIO_SPAPR_VTY_DEVICE(opaque);
    int i;

    if ((dev->in == dev->out) && size) {
        /* toggle line to simulate edge interrupt */
        spapr_vio_irq_pulse(&dev->sdev);
    }
    for (i = 0; i < size; i++) {
        if (dev->in - dev->out >= VTERM_BUFSIZE) {
            static bool reported;
            if (!reported) {
                error_report("VTY input buffer exhausted - characters dropped."
                             " (input size = %i)", size);
                reported = true;
            }
            break;
        }
        dev->buf[dev->in++ % VTERM_BUFSIZE] = buf[i];
    }
}

static int vty_getchars(SpaprVioDevice *sdev, uint8_t *buf, int max)
{
    SpaprVioVty *dev = VIO_SPAPR_VTY_DEVICE(sdev);
    int n = 0;

    while ((n < max) && (dev->out != dev->in)) {
        /*
         * Long ago, PowerVM's vty implementation had a bug where it
         * inserted a \0 after every \r going to the guest.  Existing
         * guests have a workaround for this which removes every \0
         * immediately following a \r.  To avoid triggering this
         * workaround, we stop before inserting a \0 if the preceding
         * character in the output buffer is a \r.
         */
        if (n > 0 && (buf[n - 1] == '\r') &&
                (dev->buf[dev->out % VTERM_BUFSIZE] == '\0')) {
            break;
        }
        buf[n++] = dev->buf[dev->out++ % VTERM_BUFSIZE];
    }

    qemu_chr_fe_accept_input(&dev->chardev);

    return n;
}

void vty_putchars(SpaprVioDevice *sdev, uint8_t *buf, int len)
{
    SpaprVioVty *dev = VIO_SPAPR_VTY_DEVICE(sdev);

    /* XXX this blocks entire thread. Rewrite to use
     * qemu_chr_fe_write and background I/O callbacks */
    qemu_chr_fe_write_all(&dev->chardev, buf, len);
}

static void spapr_vty_realize(SpaprVioDevice *sdev, Error **errp)
{
    SpaprVioVty *dev = VIO_SPAPR_VTY_DEVICE(sdev);

    if (!qemu_chr_fe_backend_connected(&dev->chardev)) {
        error_setg(errp, "chardev property not set");
        return;
    }

    qemu_chr_fe_set_handlers(&dev->chardev, vty_can_receive,
                             vty_receive, NULL, NULL, dev, NULL, true);
}

/* Forward declaration */
static target_ulong h_put_term_char(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                    target_ulong opcode, target_ulong *args)
{
    target_ulong reg = args[0];
    target_ulong len = args[1];
    target_ulong char0_7 = args[2];
    target_ulong char8_15 = args[3];
    SpaprVioDevice *sdev;
    uint8_t buf[16];

    sdev = vty_lookup(spapr, reg);
    if (!sdev) {
        return H_PARAMETER;
    }

    if (len > 16) {
        return H_PARAMETER;
    }

    *((uint64_t *)buf) = cpu_to_be64(char0_7);
    *((uint64_t *)buf + 1) = cpu_to_be64(char8_15);

    vty_putchars(sdev, buf, len);

    return H_SUCCESS;
}

static target_ulong h_get_term_char(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                    target_ulong opcode, target_ulong *args)
{
    target_ulong reg = args[0];
    target_ulong *len = args + 0;
    target_ulong *char0_7 = args + 1;
    target_ulong *char8_15 = args + 2;
    SpaprVioDevice *sdev;
    uint8_t buf[16];

    sdev = vty_lookup(spapr, reg);
    if (!sdev) {
        return H_PARAMETER;
    }

    *len = vty_getchars(sdev, buf, sizeof(buf));
    if (*len < 16) {
        memset(buf + *len, 0, 16 - *len);
    }

    *char0_7 = be64_to_cpu(*((uint64_t *)buf));
    *char8_15 = be64_to_cpu(*((uint64_t *)buf + 1));

    return H_SUCCESS;
}

void spapr_vty_create(SpaprVioBus *bus, Chardev *chardev)
{
    DeviceState *dev;

    dev = qdev_new("spapr-vty");
    qdev_prop_set_chr(dev, "chardev", chardev);
    qdev_realize_and_unref(dev, &bus->bus, &error_fatal);
}

static Property spapr_vty_properties[] = {
    DEFINE_SPAPR_PROPERTIES(SpaprVioVty, sdev),
    DEFINE_PROP_CHR("chardev", SpaprVioVty, chardev),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_spapr_vty = {
    .name = "spapr_vty",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_SPAPR_VIO(sdev, SpaprVioVty),

        VMSTATE_UINT32(in, SpaprVioVty),
        VMSTATE_UINT32(out, SpaprVioVty),
        VMSTATE_BUFFER(buf, SpaprVioVty),
        VMSTATE_END_OF_LIST()
    },
};

static void spapr_vty_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SpaprVioDeviceClass *k = VIO_SPAPR_DEVICE_CLASS(klass);

    k->realize = spapr_vty_realize;
    k->dt_name = "vty";
    k->dt_type = "serial";
    k->dt_compatible = "hvterm1";
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    device_class_set_props(dc, spapr_vty_properties);
    dc->vmsd = &vmstate_spapr_vty;
}

static const TypeInfo spapr_vty_info = {
    .name          = TYPE_VIO_SPAPR_VTY_DEVICE,
    .parent        = TYPE_VIO_SPAPR_DEVICE,
    .instance_size = sizeof(SpaprVioVty),
    .class_init    = spapr_vty_class_init,
};

SpaprVioDevice *spapr_vty_get_default(SpaprVioBus *bus)
{
    SpaprVioDevice *sdev, *selected;
    BusChild *kid;

    /*
     * To avoid the console bouncing around we want one VTY to be
     * the "default". We haven't really got anything to go on, so
     * arbitrarily choose the one with the lowest reg value.
     */

    selected = NULL;
    QTAILQ_FOREACH(kid, &bus->bus.children, sibling) {
        DeviceState *iter = kid->child;

        /* Only look at VTY devices */
        if (!object_dynamic_cast(OBJECT(iter), TYPE_VIO_SPAPR_VTY_DEVICE)) {
            continue;
        }

        sdev = VIO_SPAPR_DEVICE(iter);

        /* First VTY we've found, so it is selected for now */
        if (!selected) {
            selected = sdev;
            continue;
        }

        /* Choose VTY with lowest reg value */
        if (sdev->reg < selected->reg) {
            selected = sdev;
        }
    }

    return selected;
}

SpaprVioDevice *vty_lookup(SpaprMachineState *spapr, target_ulong reg)
{
    SpaprVioDevice *sdev;

    sdev = spapr_vio_find_by_reg(spapr->vio_bus, reg);
    if (!sdev && reg == 0) {
        /* Hack for kernel early debug, which always specifies reg==0.
         * We search all VIO devices, and grab the vty with the lowest
         * reg.  This attempts to mimic existing PowerVM behaviour
         * (early debug does work there, despite having no vty with
         * reg==0. */
        return spapr_vty_get_default(spapr->vio_bus);
    }

    if (!object_dynamic_cast(OBJECT(sdev), TYPE_VIO_SPAPR_VTY_DEVICE)) {
        return NULL;
    }

    return sdev;
}

static void spapr_vty_register_types(void)
{
    spapr_register_hypercall(H_PUT_TERM_CHAR, h_put_term_char);
    spapr_register_hypercall(H_GET_TERM_CHAR, h_get_term_char);
    type_register_static(&spapr_vty_info);
}

type_init(spapr_vty_register_types)
