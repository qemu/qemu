 /*
  * SPDX-License-Identifier: GPL-2.0-or-later
  *
  * SCLP event type 11 - Control-Program Identification (CPI):
  *    CPI is used to send program identifiers from the guest to the
  *    Service-Call Logical Processor (SCLP). It is not sent by the SCLP.
  *
  *    Control-program identifiers provide data about the guest operating
  *    system.  The control-program identifiers are: system type, system name,
  *    system level and sysplex name.
  *
  *    In Linux, all the control-program identifiers are user configurable. The
  *    system type, system name, and sysplex name use EBCDIC characters from
  *    this set: capital A-Z, 0-9, $, @, #, and blank.  In Linux, the system
  *    type, system name and sysplex name are arbitrary free-form texts.
  *
  *    In Linux, the 8-byte hexadecimal system-level has the format
  *    0x<a><b><cc><dd><eeee><ff><gg><hh>, where:
  *    <a>: is a 4-bit digit, its most significant bit indicates hypervisor use
  *    <b>: is one digit that represents Linux distributions as follows
  *    0: generic Linux
  *    1: Red Hat Enterprise Linux
  *    2: SUSE Linux Enterprise Server
  *    3: Canonical Ubuntu
  *    4: Fedora
  *    5: openSUSE Leap
  *    6: Debian GNU/Linux
  *    7: Red Hat Enterprise Linux CoreOS
  *    <cc>: are two digits for a distribution-specific encoding of the major
  *    version of the distribution
  *    <dd>: are two digits for a distribution-specific encoding of the minor
  *    version of the distribution
  *    <eeee>: are four digits for the patch level of the distribution
  *    <ff>: are two digits for the major version of the kernel
  *    <gg>: are two digits for the minor version of the kernel
  *    <hh>: are two digits for the stable version of the kernel
  *    (e.g. 74872343805430528, when converted to hex is 0x010a000000060b00). On
  *    machines prior to z16, some of the values are not available to display.
  *
  *    Sysplex refers to a cluster of logical partitions that communicates and
  *    co-operates with each other.
  *
  *    The CPI feature is supported since 10.1.
  *
  * Copyright IBM, Corp. 2024
  *
  * Authors:
  *  Shalini Chellathurai Saroja <shalini@linux.ibm.com>
  *
  */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "hw/s390x/event-facility.h"
#include "hw/s390x/ebcdic.h"
#include "qapi/qapi-visit-machine.h"
#include "qapi/qapi-events-machine-s390x.h"
#include "migration/vmstate.h"

typedef struct Data {
    uint8_t id_format;
    uint8_t reserved0;
    uint8_t system_type[8];
    uint64_t reserved1;
    uint8_t system_name[8];
    uint64_t reserved2;
    uint64_t system_level;
    uint64_t reserved3;
    uint8_t sysplex_name[8];
    uint8_t reserved4[16];
} QEMU_PACKED Data;

typedef struct ControlProgramIdMsg {
    EventBufferHeader ebh;
    Data data;
} QEMU_PACKED ControlProgramIdMsg;

static bool can_handle_event(uint8_t type)
{
    return type == SCLP_EVENT_CTRL_PGM_ID;
}

static sccb_mask_t send_mask(void)
{
    return 0;
}

/* Enable SCLP to accept buffers of event type CPI from the control-program. */
static sccb_mask_t receive_mask(void)
{
    return SCLP_EVENT_MASK_CTRL_PGM_ID;
}

static int write_event_data(SCLPEvent *event, EventBufferHeader *evt_buf_hdr)
{
    ControlProgramIdMsg *cpim = container_of(evt_buf_hdr, ControlProgramIdMsg,
                                             ebh);
    SCLPEventCPI *e = SCLP_EVENT_CPI(event);

    ascii_put(e->system_type, (char *)cpim->data.system_type,
              sizeof(cpim->data.system_type));
    ascii_put(e->system_name, (char *)cpim->data.system_name,
              sizeof(cpim->data.system_name));
    ascii_put(e->sysplex_name, (char *)cpim->data.sysplex_name,
              sizeof(cpim->data.sysplex_name));
    e->system_level = ldq_be_p(&cpim->data.system_level);
    e->timestamp = qemu_clock_get_ns(QEMU_CLOCK_HOST);

    cpim->ebh.flags = SCLP_EVENT_BUFFER_ACCEPTED;

    qapi_event_send_sclp_cpi_info_available();

    return SCLP_RC_NORMAL_COMPLETION;
}

static char *get_system_type(Object *obj, Error **errp)
{
    SCLPEventCPI *e = SCLP_EVENT_CPI(obj);

    return g_strndup((char *) e->system_type, sizeof(e->system_type));
}

static char *get_system_name(Object *obj, Error **errp)
{
    SCLPEventCPI *e = SCLP_EVENT_CPI(obj);

    return g_strndup((char *) e->system_name, sizeof(e->system_name));
}

static char *get_sysplex_name(Object *obj, Error **errp)
{
    SCLPEventCPI *e = SCLP_EVENT_CPI(obj);

    return g_strndup((char *) e->sysplex_name, sizeof(e->sysplex_name));
}

static void get_system_level(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    SCLPEventCPI *e = SCLP_EVENT_CPI(obj);

    visit_type_uint64(v, name, &e->system_level, errp);
}

static void get_timestamp(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    SCLPEventCPI *e = SCLP_EVENT_CPI(obj);

    visit_type_uint64(v, name, &e->timestamp, errp);
}

static const VMStateDescription vmstate_sclpcpi = {
    .name = "s390_control_program_id",
    .version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(system_type, SCLPEventCPI, 8),
        VMSTATE_UINT8_ARRAY(system_name, SCLPEventCPI, 8),
        VMSTATE_UINT64(system_level, SCLPEventCPI),
        VMSTATE_UINT8_ARRAY(sysplex_name, SCLPEventCPI, 8),
        VMSTATE_UINT64(timestamp, SCLPEventCPI),
        VMSTATE_END_OF_LIST()
    }
};

static void cpi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SCLPEventClass *k = SCLP_EVENT_CLASS(klass);

    dc->user_creatable = false;
    dc->vmsd =  &vmstate_sclpcpi;

    k->can_handle_event = can_handle_event;
    k->get_send_mask = send_mask;
    k->get_receive_mask = receive_mask;
    k->write_event_data = write_event_data;

    object_class_property_add_str(klass, "system_type", get_system_type, NULL);
    object_class_property_set_description(klass, "system_type",
            "operating system e.g. \"LINUX   \"");

    object_class_property_add_str(klass, "system_name", get_system_name, NULL);
    object_class_property_set_description(klass, "system_name",
            "user configurable name of the VM e.g. \"TESTVM  \"");

    object_class_property_add_str(klass, "sysplex_name", get_sysplex_name,
                                  NULL);
    object_class_property_set_description(klass, "sysplex_name",
            "name of the cluster which the VM belongs to, if any"
            " e.g. \"PLEX    \"");

    object_class_property_add(klass, "system_level", "uint64", get_system_level,
                              NULL, NULL, NULL);
    object_class_property_set_description(klass, "system_level",
            "distribution and kernel version in Linux e.g. 74872343805430528");

    object_class_property_add(klass, "timestamp", "uint64", get_timestamp,
                              NULL, NULL, NULL);
    object_class_property_set_description(klass, "timestamp",
            "latest update of CPI data in nanoseconds since the UNIX EPOCH");
}

static const TypeInfo sclp_cpi_info = {
    .name          = TYPE_SCLP_EVENT_CPI,
    .parent        = TYPE_SCLP_EVENT,
    .instance_size = sizeof(SCLPEventCPI),
    .class_init    = cpi_class_init,
};

static void sclp_cpi_register_types(void)
{
    type_register_static(&sclp_cpi_info);
}

type_init(sclp_cpi_register_types)
