# Copyright (C) 2020 Red Hat Inc.
#
# Authors:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
from .regexps import *
from .qom_macros import *
from .qom_type_info import *

def test_res() -> None:
    def fullmatch(regexp, s):
        return re.fullmatch(regexp, s, re.MULTILINE)

    assert fullmatch(RE_IDENTIFIER, 'sizeof')
    assert fullmatch(RE_IDENTIFIER, 'X86CPU')
    assert fullmatch(RE_FUN_CALL, 'sizeof(X86CPU)')
    assert fullmatch(RE_IDENTIFIER, 'X86_CPU_TYPE_NAME')
    assert fullmatch(RE_SIMPLE_VALUE, '"base"')
    print(RE_FUN_CALL)
    assert fullmatch(RE_FUN_CALL, 'X86_CPU_TYPE_NAME("base")')
    print(RE_TI_FIELD_INIT)
    assert fullmatch(RE_TI_FIELD_INIT, '.name = X86_CPU_TYPE_NAME("base"),\n')


    assert fullmatch(RE_MACRO_CONCAT, 'TYPE_ASPEED_GPIO "-ast2600"')
    assert fullmatch(RE_EXPRESSION, 'TYPE_ASPEED_GPIO "-ast2600"')

    print(RE_MACRO_DEFINE)
    assert re.search(RE_MACRO_DEFINE, r'''
    #define OFFSET_CHECK(c)                     \
    do {                                        \
        if (!(c)) {                             \
            goto bad_offset;                    \
        }                                       \
    } while (0)
    ''', re.MULTILINE)

    print(RE_CHECK_MACRO)
    print(CPP_SPACE)
    assert not re.match(RE_CHECK_MACRO, r'''
    #define OFFSET_CHECK(c)                     \
    do {                                        \
        if (!(c)) {                             \
            goto bad_offset;                    \
        }                                       \
    } while (0)''', re.MULTILINE)

    print(RE_CHECK_MACRO)
    assert fullmatch(RE_CHECK_MACRO, r'''#define PCI_DEVICE(obj) \
                     OBJECT_CHECK(PCIDevice, (obj), TYPE_PCI_DEVICE)
''')
    assert fullmatch(RE_CHECK_MACRO, r'''#define COLLIE_MACHINE(obj) \
                     OBJECT_CHECK(CollieMachineState, obj, TYPE_COLLIE_MACHINE)
''')

    print(RE_TYPEINFO_START)
    assert re.search(RE_TYPEINFO_START, r'''
    cc->open = qmp_chardev_open_file;
}

static const TypeInfo char_file_type_info = {
    .name = TYPE_CHARDEV_FILE,
#ifdef _WIN32
    .parent = TYPE_CHARDEV_WIN,
''', re.MULTILINE)
    assert re.search(RE_TYPEINFO_START, r'''
        TypeInfo ti = {
            .name = armsse_variants[i].name,
            .parent = TYPE_ARMSSE,
            .class_init = armsse_class_init,
            .class_data = (void *)&armsse_variants[i],
        };''', re.MULTILINE)

    print(RE_ARRAY_ITEM)
    assert fullmatch(RE_ARRAY_ITEM, '{ TYPE_HOTPLUG_HANDLER },')
    assert fullmatch(RE_ARRAY_ITEM, '{ TYPE_ACPI_DEVICE_IF },')
    assert fullmatch(RE_ARRAY_ITEM, '{ }')
    assert fullmatch(RE_ARRAY_CAST, '(InterfaceInfo[])')
    assert fullmatch(RE_ARRAY, '''(InterfaceInfo[]) {
            { TYPE_HOTPLUG_HANDLER },
            { TYPE_ACPI_DEVICE_IF },
            { }
    }''')
    print(RE_COMMENT)
    assert fullmatch(RE_COMMENT, r'''/* multi-line
                                      * comment
                                      */''')

    print(RE_TI_FIELDS)
    assert fullmatch(RE_TI_FIELDS,
    r'''/* could be TYPE_SYS_BUS_DEVICE (or LPC etc) */
        .parent = TYPE_DEVICE,
''')
    assert fullmatch(RE_TI_FIELDS, r'''.name = TYPE_TPM_CRB,
        /* could be TYPE_SYS_BUS_DEVICE (or LPC etc) */
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(CRBState),
        .class_init  = tpm_crb_class_init,
        .interfaces = (InterfaceInfo[]) {
            { TYPE_TPM_IF },
            { }
        }
''')
    assert fullmatch(RE_TI_FIELDS + SP + RE_COMMENTS,
        r'''.name = TYPE_PALM_MISC_GPIO,
            .parent = TYPE_SYS_BUS_DEVICE,
            .instance_size = sizeof(PalmMiscGPIOState),
            .instance_init = palm_misc_gpio_init,
            /*
             * No class init required: device has no internal state so does not
             * need to set up reset or vmstate, and has no realize method.
             */''')

    print(TypeInfoVar.regexp)
    test_empty = 'static const TypeInfo x86_base_cpu_type_info = {\n'+\
                 '};\n';
    assert fullmatch(TypeInfoVar.regexp, test_empty)

    test_simple = r'''
    static const TypeInfo x86_base_cpu_type_info = {
        .name = X86_CPU_TYPE_NAME("base"),
        .parent = TYPE_X86_CPU,
        .class_init = x86_cpu_base_class_init,
    };
    '''
    assert re.search(TypeInfoVar.regexp, test_simple, re.MULTILINE)

    test_interfaces = r'''
    static const TypeInfo acpi_ged_info = {
        .name          = TYPE_ACPI_GED,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(AcpiGedState),
        .instance_init  = acpi_ged_initfn,
        .class_init    = acpi_ged_class_init,
        .interfaces = (InterfaceInfo[]) {
            { TYPE_HOTPLUG_HANDLER },
            { TYPE_ACPI_DEVICE_IF },
            { }
        }
    };
    '''
    assert re.search(TypeInfoVar.regexp, test_interfaces, re.MULTILINE)

    test_comments = r'''
    static const TypeInfo palm_misc_gpio_info = {
        .name = TYPE_PALM_MISC_GPIO,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(PalmMiscGPIOState),
        .instance_init = palm_misc_gpio_init,
        /*
         * No class init required: device has no internal state so does not
         * need to set up reset or vmstate, and has no realize method.
         */
    };
    '''
    assert re.search(TypeInfoVar.regexp, test_comments, re.MULTILINE)

    test_comments = r'''
    static const TypeInfo tpm_crb_info = {
        .name = TYPE_TPM_CRB,
        /* could be TYPE_SYS_BUS_DEVICE (or LPC etc) */
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(CRBState),
        .class_init  = tpm_crb_class_init,
        .interfaces = (InterfaceInfo[]) {
            { TYPE_TPM_IF },
            { }
        }
    };
    '''
    assert re.search(TypeInfoVar.regexp, test_comments, re.MULTILINE)

def test_struct_re():
    print('---')
    print(RE_STRUCT_TYPEDEF)
    assert re.search(RE_STRUCT_TYPEDEF, r'''
typedef struct TCGState {
    AccelState parent_obj;

    bool mttcg_enabled;
    unsigned long tb_size;
} TCGState;
''', re.MULTILINE)

    assert re.search(RE_STRUCT_TYPEDEF, r'''
typedef struct {
    ISADevice parent_obj;

    QEMUSoundCard card;
    uint32_t freq;
    uint32_t port;
    int ticking[2];
    int enabled;
    int active;
    int bufpos;
#ifdef DEBUG
    int64_t exp[2];
#endif
    int16_t *mixbuf;
    uint64_t dexp[2];
    SWVoiceOut *voice;
    int left, pos, samples;
    QEMUAudioTimeStamp ats;
    FM_OPL *opl;
    PortioList port_list;
} AdlibState;
''', re.MULTILINE)

    false_positive = r'''
typedef struct dma_pagetable_entry {
    int32_t frame;
    int32_t owner;
} A B C D E;
struct foo {
    int x;
} some_variable;
'''
    assert not re.search(RE_STRUCT_TYPEDEF, false_positive, re.MULTILINE)

def test_initial_includes():
    print(InitialIncludes.regexp)
    c = '''
#ifndef HW_FLASH_H
#define HW_FLASH_H

/* NOR flash devices */

#include "qom/object.h"
#include "exec/hwaddr.h"

/* pflash_cfi01.c */
'''
    print(repr(list(m.groupdict() for m in InitialIncludes.finditer(c))))
    m = InitialIncludes.domatch(c)
    assert m
    print(repr(m.group(0)))
    assert m.group(0).endswith('#include "exec/hwaddr.h"\n')

    c = '''#ifndef QEMU_VIRTIO_9P_H
#define QEMU_VIRTIO_9P_H

#include "standard-headers/linux/virtio_9p.h"
#include "hw/virtio/virtio.h"
#include "9p.h"


'''
    print(repr(list(m.groupdict() for m in InitialIncludes.finditer(c))))
    m = InitialIncludes.domatch(c)
    assert m
    print(repr(m.group(0)))
    assert m.group(0).endswith('#include "9p.h"\n')

    c = '''#include "qom/object.h"
/*
 * QEMU ES1370 emulation
...
 */

/* #define DEBUG_ES1370 */
/* #define VERBOSE_ES1370 */
#define SILENT_ES1370

#include "qemu/osdep.h"
#include "hw/audio/soundhw.h"
#include "audio/audio.h"
#include "hw/pci/pci.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "system/dma.h"

/* Missing stuff:
   SCTRL_P[12](END|ST)INC
'''
    print(repr(list(m.groupdict() for m in InitialIncludes.finditer(c))))
    m = InitialIncludes.domatch(c)
    assert m
    print(repr(m.group(0)))
    assert m.group(0).endswith('#include "system/dma.h"\n')

