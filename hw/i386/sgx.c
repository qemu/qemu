/*
 * SGX common code
 *
 * Copyright (C) 2021 Intel Corporation
 *
 * Authors:
 *   Yang Zhong<yang.zhong@intel.com>
 *   Sean Christopherson <sean.j.christopherson@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "hw/i386/pc.h"
#include "hw/i386/sgx-epc.h"
#include "hw/mem/memory-device.h"
#include "monitor/qdev.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc-target.h"
#include "exec/address-spaces.h"
#include "sysemu/hw_accel.h"
#include "sysemu/reset.h"
#include <sys/ioctl.h>
#include "hw/acpi/aml-build.h"

#define SGX_MAX_EPC_SECTIONS            8
#define SGX_CPUID_EPC_INVALID           0x0

/* A valid EPC section. */
#define SGX_CPUID_EPC_SECTION           0x1
#define SGX_CPUID_EPC_MASK              0xF

#define SGX_MAGIC 0xA4
#define SGX_IOC_VEPC_REMOVE_ALL       _IO(SGX_MAGIC, 0x04)

#define RETRY_NUM                       2

static int sgx_epc_device_list(Object *obj, void *opaque)
{
    GSList **list = opaque;

    if (object_dynamic_cast(obj, TYPE_SGX_EPC)) {
        *list = g_slist_append(*list, DEVICE(obj));
    }

    object_child_foreach(obj, sgx_epc_device_list, opaque);
    return 0;
}

static GSList *sgx_epc_get_device_list(void)
{
    GSList *list = NULL;

    object_child_foreach(qdev_get_machine(), sgx_epc_device_list, &list);
    return list;
}

void sgx_epc_build_srat(GArray *table_data)
{
    GSList *device_list = sgx_epc_get_device_list();

    for (; device_list; device_list = device_list->next) {
        DeviceState *dev = device_list->data;
        Object *obj = OBJECT(dev);
        uint64_t addr, size;
        int node;

        node = object_property_get_uint(obj, SGX_EPC_NUMA_NODE_PROP,
                                        &error_abort);
        addr = object_property_get_uint(obj, SGX_EPC_ADDR_PROP, &error_abort);
        size = object_property_get_uint(obj, SGX_EPC_SIZE_PROP, &error_abort);

        build_srat_memory(table_data, addr, size, node, MEM_AFFINITY_ENABLED);
    }
    g_slist_free(device_list);
}

static uint64_t sgx_calc_section_metric(uint64_t low, uint64_t high)
{
    return (low & MAKE_64BIT_MASK(12, 20)) +
           ((high & MAKE_64BIT_MASK(0, 20)) << 32);
}

static SGXEPCSectionList *sgx_calc_host_epc_sections(uint64_t *size)
{
    SGXEPCSectionList *head = NULL, **tail = &head;
    SGXEPCSection *section;
    uint32_t i, type;
    uint32_t eax, ebx, ecx, edx;
    uint32_t j = 0;

    for (i = 0; i < SGX_MAX_EPC_SECTIONS; i++) {
        host_cpuid(0x12, i + 2, &eax, &ebx, &ecx, &edx);

        type = eax & SGX_CPUID_EPC_MASK;
        if (type == SGX_CPUID_EPC_INVALID) {
            break;
        }

        if (type != SGX_CPUID_EPC_SECTION) {
            break;
        }

        section = g_new0(SGXEPCSection, 1);
        section->node = j++;
        section->size = sgx_calc_section_metric(ecx, edx);
        *size += section->size;
        QAPI_LIST_APPEND(tail, section);
    }

    return head;
}

static void sgx_epc_reset(void *opaque)
{
    PCMachineState *pcms = PC_MACHINE(qdev_get_machine());
    HostMemoryBackend *hostmem;
    SGXEPCDevice *epc;
    int failures;
    int fd, i, j, r;
    static bool warned = false;

    /*
     * The second pass is needed to remove SECS pages that could not
     * be removed during the first.
     */
    for (i = 0; i < RETRY_NUM; i++) {
        failures = 0;
        for (j = 0; j < pcms->sgx_epc.nr_sections; j++) {
            epc = pcms->sgx_epc.sections[j];
            hostmem = MEMORY_BACKEND(epc->hostmem);
            fd = memory_region_get_fd(host_memory_backend_get_memory(hostmem));

            r = ioctl(fd, SGX_IOC_VEPC_REMOVE_ALL);
            if (r == -ENOTTY && !warned) {
                warned = true;
                warn_report("kernel does not support SGX_IOC_VEPC_REMOVE_ALL");
                warn_report("SGX might operate incorrectly in the guest after reset");
                break;
            } else if (r > 0) {
                /* SECS pages remain */
                failures++;
                if (i == 1) {
                    error_report("cannot reset vEPC section %d", j);
                }
            }
        }
        if (!failures) {
            break;
        }
     }
}

SGXInfo *qmp_query_sgx_capabilities(Error **errp)
{
    SGXInfo *info = NULL;
    uint32_t eax, ebx, ecx, edx;
    uint64_t size = 0;

    int fd = qemu_open_old("/dev/sgx_vepc", O_RDWR);
    if (fd < 0) {
        error_setg(errp, "SGX is not enabled in KVM");
        return NULL;
    }

    info = g_new0(SGXInfo, 1);
    host_cpuid(0x7, 0, &eax, &ebx, &ecx, &edx);

    info->sgx = ebx & (1U << 2) ? true : false;
    info->flc = ecx & (1U << 30) ? true : false;

    host_cpuid(0x12, 0, &eax, &ebx, &ecx, &edx);
    info->sgx1 = eax & (1U << 0) ? true : false;
    info->sgx2 = eax & (1U << 1) ? true : false;

    info->sections = sgx_calc_host_epc_sections(&size);
    info->section_size = size;

    close(fd);

    return info;
}

static SGXEPCSectionList *sgx_get_epc_sections_list(void)
{
    GSList *device_list = sgx_epc_get_device_list();
    SGXEPCSectionList *head = NULL, **tail = &head;
    SGXEPCSection *section;

    for (; device_list; device_list = device_list->next) {
        DeviceState *dev = device_list->data;
        Object *obj = OBJECT(dev);

        section = g_new0(SGXEPCSection, 1);
        section->node = object_property_get_uint(obj, SGX_EPC_NUMA_NODE_PROP,
                                                 &error_abort);
        section->size = object_property_get_uint(obj, SGX_EPC_SIZE_PROP,
                                                 &error_abort);
        QAPI_LIST_APPEND(tail, section);
    }
    g_slist_free(device_list);

    return head;
}

SGXInfo *qmp_query_sgx(Error **errp)
{
    SGXInfo *info = NULL;
    X86MachineState *x86ms;
    PCMachineState *pcms =
        (PCMachineState *)object_dynamic_cast(qdev_get_machine(),
                                              TYPE_PC_MACHINE);
    if (!pcms) {
        error_setg(errp, "SGX is only supported on PC machines");
        return NULL;
    }

    x86ms = X86_MACHINE(pcms);
    if (!x86ms->sgx_epc_list) {
        error_setg(errp, "No EPC regions defined, SGX not available");
        return NULL;
    }

    SGXEPCState *sgx_epc = &pcms->sgx_epc;
    info = g_new0(SGXInfo, 1);

    info->sgx = true;
    info->sgx1 = true;
    info->sgx2 = true;
    info->flc = true;
    info->section_size = sgx_epc->size;
    info->sections = sgx_get_epc_sections_list();

    return info;
}

void hmp_info_sgx(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    SGXEPCSectionList *section_list, *section;
    g_autoptr(SGXInfo) info = qmp_query_sgx(&err);

    if (err) {
        error_report_err(err);
        return;
    }
    monitor_printf(mon, "SGX support: %s\n",
                   info->sgx ? "enabled" : "disabled");
    monitor_printf(mon, "SGX1 support: %s\n",
                   info->sgx1 ? "enabled" : "disabled");
    monitor_printf(mon, "SGX2 support: %s\n",
                   info->sgx2 ? "enabled" : "disabled");
    monitor_printf(mon, "FLC support: %s\n",
                   info->flc ? "enabled" : "disabled");
    monitor_printf(mon, "size: %" PRIu64 "\n",
                   info->section_size);

    section_list = info->sections;
    for (section = section_list; section; section = section->next) {
        monitor_printf(mon, "NUMA node #%" PRId64 ": ",
                       section->value->node);
        monitor_printf(mon, "size=%" PRIu64 "\n",
                       section->value->size);
    }
}

bool sgx_epc_get_section(int section_nr, uint64_t *addr, uint64_t *size)
{
    PCMachineState *pcms = PC_MACHINE(qdev_get_machine());
    SGXEPCDevice *epc;

    if (pcms->sgx_epc.size == 0 || pcms->sgx_epc.nr_sections <= section_nr) {
        return true;
    }

    epc = pcms->sgx_epc.sections[section_nr];

    *addr = epc->addr;
    *size = memory_device_get_region_size(MEMORY_DEVICE(epc), &error_fatal);

    return false;
}

void pc_machine_init_sgx_epc(PCMachineState *pcms)
{
    SGXEPCState *sgx_epc = &pcms->sgx_epc;
    X86MachineState *x86ms = X86_MACHINE(pcms);
    SgxEPCList *list = NULL;
    Object *obj;

    memset(sgx_epc, 0, sizeof(SGXEPCState));
    if (!x86ms->sgx_epc_list) {
        return;
    }

    sgx_epc->base = 0x100000000ULL + x86ms->above_4g_mem_size;

    memory_region_init(&sgx_epc->mr, OBJECT(pcms), "sgx-epc", UINT64_MAX);
    memory_region_add_subregion(get_system_memory(), sgx_epc->base,
                                &sgx_epc->mr);

    for (list = x86ms->sgx_epc_list; list; list = list->next) {
        obj = object_new("sgx-epc");

        /* set the memdev link with memory backend */
        object_property_parse(obj, SGX_EPC_MEMDEV_PROP, list->value->memdev,
                              &error_fatal);
        /* set the numa node property for sgx epc object */
        object_property_set_uint(obj, SGX_EPC_NUMA_NODE_PROP, list->value->node,
                             &error_fatal);
        object_property_set_bool(obj, "realized", true, &error_fatal);
        object_unref(obj);
    }

    if ((sgx_epc->base + sgx_epc->size) < sgx_epc->base) {
        error_report("Size of all 'sgx-epc' =0x%"PRIx64" causes EPC to wrap",
                     sgx_epc->size);
        exit(EXIT_FAILURE);
    }

    memory_region_set_size(&sgx_epc->mr, sgx_epc->size);

    /* register the reset callback for sgx epc */
    qemu_register_reset(sgx_epc_reset, NULL);
}
