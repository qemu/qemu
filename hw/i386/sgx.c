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
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "hw/i386/sgx.h"
#include "sysemu/hw_accel.h"

#define SGX_MAX_EPC_SECTIONS            8
#define SGX_CPUID_EPC_INVALID           0x0

/* A valid EPC section. */
#define SGX_CPUID_EPC_SECTION           0x1
#define SGX_CPUID_EPC_MASK              0xF

static uint64_t sgx_calc_section_metric(uint64_t low, uint64_t high)
{
    return (low & MAKE_64BIT_MASK(12, 20)) +
           ((high & MAKE_64BIT_MASK(0, 20)) << 32);
}

static uint64_t sgx_calc_host_epc_section_size(void)
{
    uint32_t i, type;
    uint32_t eax, ebx, ecx, edx;
    uint64_t size = 0;

    for (i = 0; i < SGX_MAX_EPC_SECTIONS; i++) {
        host_cpuid(0x12, i + 2, &eax, &ebx, &ecx, &edx);

        type = eax & SGX_CPUID_EPC_MASK;
        if (type == SGX_CPUID_EPC_INVALID) {
            break;
        }

        if (type != SGX_CPUID_EPC_SECTION) {
            break;
        }

        size += sgx_calc_section_metric(ecx, edx);
    }

    return size;
}

SGXInfo *sgx_get_capabilities(Error **errp)
{
    SGXInfo *info = NULL;
    uint32_t eax, ebx, ecx, edx;

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

    info->section_size = sgx_calc_host_epc_section_size();

    close(fd);

    return info;
}

SGXInfo *sgx_get_info(Error **errp)
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

    return info;
}

int sgx_epc_get_section(int section_nr, uint64_t *addr, uint64_t *size)
{
    PCMachineState *pcms = PC_MACHINE(qdev_get_machine());
    SGXEPCDevice *epc;

    if (pcms->sgx_epc.size == 0 || pcms->sgx_epc.nr_sections <= section_nr) {
        return 1;
    }

    epc = pcms->sgx_epc.sections[section_nr];

    *addr = epc->addr;
    *size = memory_device_get_region_size(MEMORY_DEVICE(epc), &error_fatal);

    return 0;
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
        object_property_set_bool(obj, "realized", true, &error_fatal);
        object_unref(obj);
    }

    if ((sgx_epc->base + sgx_epc->size) < sgx_epc->base) {
        error_report("Size of all 'sgx-epc' =0x%"PRIu64" causes EPC to wrap",
                     sgx_epc->size);
        exit(EXIT_FAILURE);
    }

    memory_region_set_size(&sgx_epc->mr, sgx_epc->size);
}
