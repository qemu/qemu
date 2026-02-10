/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Windows Hypervisor Platform accelerator (WHPX)
 *
 * Copyright Microsoft Corp. 2017
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "system/address-spaces.h"
#include "system/ioport.h"
#include "gdbstub/helpers.h"
#include "qemu/accel.h"
#include "accel/accel-ops.h"
#include "system/memory.h"
#include "system/whpx.h"
#include "system/cpus.h"
#include "system/runstate.h"
#include "qemu/main-loop.h"
#include "hw/core/boards.h"
#include "hw/intc/ioapic.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qapi-types-common.h"
#include "qapi/qapi-visit-common.h"
#include "migration/blocker.h"
#include "accel/accel-cpu-target.h"
#include <winerror.h>

#include "system/whpx-internal.h"
#include "system/whpx-accel-ops.h"
#include "system/whpx-common.h"
#include "system/whpx-all.h"

#include <winhvplatform.h>
#include <winhvplatformdefs.h>

bool whpx_allowed;
bool whpx_irqchip_in_kernel;
static bool whp_dispatch_initialized;
static HMODULE hWinHvPlatform;
#ifdef HOST_X86_64
static HMODULE hWinHvEmulation;
#endif

struct whpx_state whpx_global;
struct WHPDispatch whp_dispatch;

/* Tries to find a breakpoint at the specified address. */
struct whpx_breakpoint *whpx_lookup_breakpoint_by_addr(uint64_t address)
{
    struct whpx_state *whpx = &whpx_global;
    int i;

    if (whpx->breakpoints.breakpoints) {
        for (i = 0; i < whpx->breakpoints.breakpoints->used; i++) {
            if (address == whpx->breakpoints.breakpoints->data[i].address) {
                return &whpx->breakpoints.breakpoints->data[i];
            }
        }
    }

    return NULL;
}

/*
 * This function is called when the a VCPU is about to start and no other
 * VCPUs have been started so far. Since the VCPU start order could be
 * arbitrary, it doesn't have to be VCPU#0.
 *
 * It is used to commit the breakpoints into memory, and configure WHPX
 * to intercept debug exceptions.
 *
 * Note that whpx_set_exception_exit_bitmap() cannot be called if one or
 * more VCPUs are already running, so this is the best place to do it.
 */
int whpx_first_vcpu_starting(CPUState *cpu)
{
    struct whpx_state *whpx = &whpx_global;

    g_assert(bql_locked());

    if (!QTAILQ_EMPTY(&cpu->breakpoints) ||
            (whpx->breakpoints.breakpoints &&
             whpx->breakpoints.breakpoints->used)) {
        CPUBreakpoint *bp;
        int i = 0;
        bool update_pending = false;

        QTAILQ_FOREACH(bp, &cpu->breakpoints, entry) {
            if (i >= whpx->breakpoints.original_address_count ||
                bp->pc != whpx->breakpoints.original_addresses[i]) {
                update_pending = true;
            }

            i++;
        }

        if (i != whpx->breakpoints.original_address_count) {
            update_pending = true;
        }

        if (update_pending) {
            /*
             * The CPU breakpoints have changed since the last call to
             * whpx_translate_cpu_breakpoints(). WHPX breakpoints must
             * now be recomputed.
             */
            whpx_translate_cpu_breakpoints(&whpx->breakpoints, cpu, i);
        }
        /* Actually insert the breakpoints into the memory. */
        whpx_apply_breakpoints(whpx->breakpoints.breakpoints, cpu, true);
    }
    HRESULT hr;
    uint64_t exception_mask;
    if (whpx->step_pending ||
        (whpx->breakpoints.breakpoints &&
         whpx->breakpoints.breakpoints->used)) {
        /*
         * We are either attempting to single-step one or more CPUs, or
         * have one or more breakpoints enabled. Both require intercepting
         * the WHvX64ExceptionTypeBreakpointTrap exception.
         */
        exception_mask = 1UL << WHPX_INTERCEPT_DEBUG_TRAPS;
    } else {
        /* Let the guest handle all exceptions. */
        exception_mask = 0;
    }
    hr = whpx_set_exception_exit_bitmap(exception_mask);
    if (!SUCCEEDED(hr)) {
        error_report("WHPX: Failed to update exception exit mask,"
                     "hr=%08lx.", hr);
        return 1;
    }
    return 0;
}

/*
 * This function is called when the last VCPU has finished running.
 * It is used to remove any previously set breakpoints from memory.
 */
int whpx_last_vcpu_stopping(CPUState *cpu)
{
    whpx_apply_breakpoints(whpx_global.breakpoints.breakpoints, cpu, false);
    return 0;
}

static void do_whpx_cpu_synchronize_state(CPUState *cpu, run_on_cpu_data arg)
{
    if (!cpu->vcpu_dirty) {
        whpx_get_registers(cpu);
        cpu->vcpu_dirty = true;
    }
}

static void do_whpx_cpu_synchronize_post_reset(CPUState *cpu,
                                               run_on_cpu_data arg)
{
    whpx_set_registers(cpu, WHPX_SET_RESET_STATE);
    cpu->vcpu_dirty = false;
}

static void do_whpx_cpu_synchronize_post_init(CPUState *cpu,
                                              run_on_cpu_data arg)
{
    whpx_set_registers(cpu, WHPX_SET_FULL_STATE);
    cpu->vcpu_dirty = false;
}

static void do_whpx_cpu_synchronize_pre_loadvm(CPUState *cpu,
                                               run_on_cpu_data arg)
{
    cpu->vcpu_dirty = true;
}

/*
 * CPU support.
 */

void whpx_cpu_synchronize_state(CPUState *cpu)
{
    if (!cpu->vcpu_dirty) {
        run_on_cpu(cpu, do_whpx_cpu_synchronize_state, RUN_ON_CPU_NULL);
    }
}

void whpx_cpu_synchronize_post_reset(CPUState *cpu)
{
    run_on_cpu(cpu, do_whpx_cpu_synchronize_post_reset, RUN_ON_CPU_NULL);
}

void whpx_cpu_synchronize_post_init(CPUState *cpu)
{
    run_on_cpu(cpu, do_whpx_cpu_synchronize_post_init, RUN_ON_CPU_NULL);
}

void whpx_cpu_synchronize_pre_loadvm(CPUState *cpu)
{
    run_on_cpu(cpu, do_whpx_cpu_synchronize_pre_loadvm, RUN_ON_CPU_NULL);
}

static void whpx_pre_resume_vm(AccelState *as, bool step_pending)
{
    whpx_global.step_pending = step_pending;
}

/*
 * Vcpu support.
 */

int whpx_vcpu_exec(CPUState *cpu)
{
    int ret;
    int fatal;

    for (;;) {
        if (cpu->exception_index >= EXCP_INTERRUPT) {
            ret = cpu->exception_index;
            cpu->exception_index = -1;
            break;
        }

        fatal = whpx_vcpu_run(cpu);

        if (fatal) {
            error_report("WHPX: Failed to exec a virtual processor");
            abort();
        }
    }

    return ret;
}

void whpx_destroy_vcpu(CPUState *cpu)
{
    struct whpx_state *whpx = &whpx_global;

    whp_dispatch.WHvDeleteVirtualProcessor(whpx->partition, cpu->cpu_index);
#ifdef HOST_X86_64
    AccelCPUState *vcpu = cpu->accel;
    whp_dispatch.WHvEmulatorDestroyEmulator(vcpu->emulator);
#endif
    g_free(cpu->accel);
}


void whpx_vcpu_kick(CPUState *cpu)
{
    struct whpx_state *whpx = &whpx_global;
    whp_dispatch.WHvCancelRunVirtualProcessor(
        whpx->partition, cpu->cpu_index, 0);
}

/*
 * Memory support.
 */

static void whpx_update_mapping(hwaddr start_pa, ram_addr_t size,
                                void *host_va, int add, int rom,
                                const char *name)
{
    struct whpx_state *whpx = &whpx_global;
    HRESULT hr;

    /*
    if (add) {
        printf("WHPX: ADD PA:%p Size:%p, Host:%p, %s, '%s'\n",
               (void*)start_pa, (void*)size, host_va,
               (rom ? "ROM" : "RAM"), name);
    } else {
        printf("WHPX: DEL PA:%p Size:%p, Host:%p,      '%s'\n",
               (void*)start_pa, (void*)size, host_va, name);
    }
    */

    if (add) {
        hr = whp_dispatch.WHvMapGpaRange(whpx->partition,
                                         host_va,
                                         start_pa,
                                         size,
                                         (WHvMapGpaRangeFlagRead |
                                          WHvMapGpaRangeFlagExecute |
                                          (rom ? 0 : WHvMapGpaRangeFlagWrite)));
    } else {
        hr = whp_dispatch.WHvUnmapGpaRange(whpx->partition,
                                           start_pa,
                                           size);
    }

    if (FAILED(hr)) {
        error_report("WHPX: Failed to %s GPA range '%s' PA:%p, Size:%p bytes,"
                     " Host:%p, hr=%08lx",
                     (add ? "MAP" : "UNMAP"), name,
                     (void *)(uintptr_t)start_pa, (void *)size, host_va, hr);
    }
}

static void whpx_process_section(MemoryRegionSection *section, int add)
{
    MemoryRegion *mr = section->mr;
    hwaddr start_pa = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    unsigned int delta;
    uint64_t host_va;

    if (!memory_region_is_ram(mr)) {
        return;
    }

    delta = qemu_real_host_page_size() - (start_pa & ~qemu_real_host_page_mask());
    delta &= ~qemu_real_host_page_mask();
    if (delta > size) {
        return;
    }
    start_pa += delta;
    size -= delta;
    size &= qemu_real_host_page_mask();
    if (!size || (start_pa & ~qemu_real_host_page_mask())) {
        return;
    }

    host_va = (uintptr_t)memory_region_get_ram_ptr(mr)
            + section->offset_within_region + delta;

    whpx_update_mapping(start_pa, size, (void *)(uintptr_t)host_va, add,
                        memory_region_is_rom(mr), mr->name);
}

static void whpx_region_add(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    memory_region_ref(section->mr);
    whpx_process_section(section, 1);
}

static void whpx_region_del(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    whpx_process_section(section, 0);
    memory_region_unref(section->mr);
}

static void whpx_transaction_begin(MemoryListener *listener)
{
}

static void whpx_transaction_commit(MemoryListener *listener)
{
}

static void whpx_log_sync(MemoryListener *listener,
                         MemoryRegionSection *section)
{
    MemoryRegion *mr = section->mr;

    if (!memory_region_is_ram(mr)) {
        return;
    }

    memory_region_set_dirty(mr, 0, int128_get64(section->size));
}

static MemoryListener whpx_memory_listener = {
    .name = "whpx",
    .begin = whpx_transaction_begin,
    .commit = whpx_transaction_commit,
    .region_add = whpx_region_add,
    .region_del = whpx_region_del,
    .log_sync = whpx_log_sync,
    .priority = MEMORY_LISTENER_PRIORITY_ACCEL,
};

void whpx_memory_init(void)
{
    memory_listener_register(&whpx_memory_listener, &address_space_memory);
}

/*
 * Load the functions from the given library, using the given handle. If a
 * handle is provided, it is used, otherwise the library is opened. The
 * handle will be updated on return with the opened one.
 */
static bool load_whp_dispatch_fns(HMODULE *handle,
    WHPFunctionList function_list)
{
    HMODULE hLib = *handle;

    #define WINHV_PLATFORM_DLL "WinHvPlatform.dll"
    #define WINHV_EMULATION_DLL "WinHvEmulation.dll"
    #define WHP_LOAD_FIELD_OPTIONAL(return_type, function_name, signature) \
        whp_dispatch.function_name = \
            (function_name ## _t)GetProcAddress(hLib, #function_name); \

    #define WHP_LOAD_FIELD(return_type, function_name, signature) \
        whp_dispatch.function_name = \
            (function_name ## _t)GetProcAddress(hLib, #function_name); \
        if (!whp_dispatch.function_name) { \
            error_report("Could not load function %s", #function_name); \
            goto error; \
        } \

    #define WHP_LOAD_LIB(lib_name, handle_lib) \
    if (!handle_lib) { \
        handle_lib = LoadLibrary(lib_name); \
        if (!handle_lib) { \
            error_report("Could not load library %s.", lib_name); \
            goto error; \
        } \
    } \

    switch (function_list) {
    case WINHV_PLATFORM_FNS_DEFAULT:
        WHP_LOAD_LIB(WINHV_PLATFORM_DLL, hLib)
        LIST_WINHVPLATFORM_FUNCTIONS(WHP_LOAD_FIELD)
        break;
    case WINHV_EMULATION_FNS_DEFAULT:
#ifdef HOST_X86_64
        WHP_LOAD_LIB(WINHV_EMULATION_DLL, hLib)
        LIST_WINHVEMULATION_FUNCTIONS(WHP_LOAD_FIELD)
#else
        g_assert_not_reached();
#endif
        break;
    case WINHV_PLATFORM_FNS_SUPPLEMENTAL:
        WHP_LOAD_LIB(WINHV_PLATFORM_DLL, hLib)
        LIST_WINHVPLATFORM_FUNCTIONS_SUPPLEMENTAL(WHP_LOAD_FIELD_OPTIONAL)
        break;
    }

    *handle = hLib;
    return true;

error:
    if (hLib) {
        FreeLibrary(hLib);
    }

    return false;
}

static void whpx_set_kernel_irqchip(Object *obj, Visitor *v,
                                   const char *name, void *opaque,
                                   Error **errp)
{
    struct whpx_state *whpx = &whpx_global;
    OnOffSplit mode;

    if (!visit_type_OnOffSplit(v, name, &mode, errp)) {
        return;
    }

    switch (mode) {
    case ON_OFF_SPLIT_ON:
        whpx->kernel_irqchip_allowed = true;
        whpx->kernel_irqchip_required = true;
        break;

    case ON_OFF_SPLIT_OFF:
        whpx->kernel_irqchip_allowed = false;
        whpx->kernel_irqchip_required = false;
        break;

    case ON_OFF_SPLIT_SPLIT:
        error_setg(errp, "WHPX: split irqchip currently not supported");
        error_append_hint(errp,
            "Try without kernel-irqchip or with kernel-irqchip=on|off");
        break;

    default:
        /*
         * The value was checked in visit_type_OnOffSplit() above. If
         * we get here, then something is wrong in QEMU.
         */
        abort();
    }
}

static void whpx_cpu_accel_class_init(ObjectClass *oc, const void *data)
{
    AccelCPUClass *acc = ACCEL_CPU_CLASS(oc);

    acc->cpu_instance_init = whpx_cpu_instance_init;
}

static const TypeInfo whpx_cpu_accel_type = {
    .name = ACCEL_CPU_NAME("whpx"),

    .parent = TYPE_ACCEL_CPU,
    .class_init = whpx_cpu_accel_class_init,
    .abstract = true,
};

static void whpx_accel_class_init(ObjectClass *oc, const void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "WHPX";
    ac->init_machine = whpx_accel_init;
    ac->pre_resume_vm = whpx_pre_resume_vm;
    ac->allowed = &whpx_allowed;

    object_class_property_add(oc, "kernel-irqchip", "on|off|split",
        NULL, whpx_set_kernel_irqchip,
        NULL, NULL);
    object_class_property_set_description(oc, "kernel-irqchip",
        "Configure WHPX in-kernel irqchip");
}

static void whpx_accel_instance_init(Object *obj)
{
    struct whpx_state *whpx = &whpx_global;

    memset(whpx, 0, sizeof(struct whpx_state));
    /* Turn on kernel-irqchip, by default */
    whpx->kernel_irqchip_allowed = true;
}

static const TypeInfo whpx_accel_type = {
    .name = ACCEL_CLASS_NAME("whpx"),
    .parent = TYPE_ACCEL,
    .instance_init = whpx_accel_instance_init,
    .class_init = whpx_accel_class_init,
};

static void whpx_type_init(void)
{
    type_register_static(&whpx_accel_type);
    type_register_static(&whpx_cpu_accel_type);
}

bool init_whp_dispatch(void)
{
    if (whp_dispatch_initialized) {
        return true;
    }

    if (!load_whp_dispatch_fns(&hWinHvPlatform, WINHV_PLATFORM_FNS_DEFAULT)) {
        goto error;
    }
#ifdef HOST_X86_64
    if (!load_whp_dispatch_fns(&hWinHvEmulation, WINHV_EMULATION_FNS_DEFAULT)) {
        goto error;
    }
#endif
    assert(load_whp_dispatch_fns(&hWinHvPlatform,
        WINHV_PLATFORM_FNS_SUPPLEMENTAL));
    whp_dispatch_initialized = true;

    return true;
error:
    if (hWinHvPlatform) {
        FreeLibrary(hWinHvPlatform);
    }
#ifdef HOST_X86_64
    if (hWinHvEmulation) {
        FreeLibrary(hWinHvEmulation);
    }
#endif
    return false;
}

type_init(whpx_type_init);
