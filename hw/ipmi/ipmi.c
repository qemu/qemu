/*
 * QEMU IPMI emulation
 *
 * Copyright (c) 2015 Corey Minyard, MontaVista Software, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/ipmi/ipmi.h"
#include "sysemu/sysemu.h"
#include "qmp-commands.h"
#include "qom/object_interfaces.h"
#include "qapi/visitor.h"

static uint32_t ipmi_current_uuid = 1;

uint32_t ipmi_next_uuid(void)
{
    return ipmi_current_uuid++;
}

static int ipmi_do_hw_op(IPMIInterface *s, enum ipmi_op op, int checkonly)
{
    switch (op) {
    case IPMI_RESET_CHASSIS:
        if (checkonly) {
            return 0;
        }
        qemu_system_reset_request();
        return 0;

    case IPMI_POWEROFF_CHASSIS:
        if (checkonly) {
            return 0;
        }
        qemu_system_powerdown_request();
        return 0;

    case IPMI_SEND_NMI:
        if (checkonly) {
            return 0;
        }
        qmp_inject_nmi(NULL);
        return 0;

    case IPMI_POWERCYCLE_CHASSIS:
    case IPMI_PULSE_DIAG_IRQ:
    case IPMI_SHUTDOWN_VIA_ACPI_OVERTEMP:
    case IPMI_POWERON_CHASSIS:
    default:
        return IPMI_CC_COMMAND_NOT_SUPPORTED;
    }
}

static void ipmi_interface_class_init(ObjectClass *class, void *data)
{
    IPMIInterfaceClass *ik = IPMI_INTERFACE_CLASS(class);

    ik->do_hw_op = ipmi_do_hw_op;
}

static TypeInfo ipmi_interface_type_info = {
    .name = TYPE_IPMI_INTERFACE,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(IPMIInterfaceClass),
    .class_init = ipmi_interface_class_init,
};

static void isa_ipmi_bmc_check(Object *obj, const char *name,
                               Object *val, Error **errp)
{
    IPMIBmc *bmc = IPMI_BMC(val);

    if (bmc->intf)
        error_setg(errp, "BMC object is already in use");
}

void ipmi_bmc_find_and_link(Object *obj, Object **bmc)
{
    object_property_add_link(obj, "bmc", TYPE_IPMI_BMC, bmc,
                             isa_ipmi_bmc_check,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
}

static Property ipmi_bmc_properties[] = {
    DEFINE_PROP_UINT8("slave_addr",  IPMIBmc, slave_addr, 0x20),
    DEFINE_PROP_END_OF_LIST(),
};

static void bmc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->props = ipmi_bmc_properties;
}

static TypeInfo ipmi_bmc_type_info = {
    .name = TYPE_IPMI_BMC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(IPMIBmc),
    .abstract = true,
    .class_size = sizeof(IPMIBmcClass),
    .class_init = bmc_class_init,
};

static void ipmi_register_types(void)
{
    type_register_static(&ipmi_interface_type_info);
    type_register_static(&ipmi_bmc_type_info);
}

type_init(ipmi_register_types)
