/*
 * Copyright (C) 2010       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-migration.h"
#include "trace.h"

#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/irq.h"
#include "hw/i386/apic-msidef.h"
#include "hw/xen/xen-x86.h"
#include "qemu/range.h"

#include "hw/xen/xen-hvm-common.h"
#include "hw/xen/arch_hvm.h"
#include <xen/hvm/e820.h>
#include "exec/target_page.h"
#include "target/i386/cpu.h"
#include "system/runstate.h"
#include "system/xen-mapcache.h"
#include "system/xen.h"

static MemoryRegion ram_640k, ram_lo, ram_hi;
static MemoryRegion *framebuffer;
static bool xen_in_migration;

/* Compatibility with older version */

/*
 * This allows QEMU to build on a system that has Xen 4.5 or earlier installed.
 * This is here (not in hw/xen/xen_native.h) because xen/hvm/ioreq.h needs to
 * be included before this block and hw/xen/xen_native.h needs to be included
 * before xen/hvm/ioreq.h
 */
#ifndef IOREQ_TYPE_VMWARE_PORT
#define IOREQ_TYPE_VMWARE_PORT  3
struct vmware_regs {
    uint32_t esi;
    uint32_t edi;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};
typedef struct vmware_regs vmware_regs_t;

struct shared_vmport_iopage {
    struct vmware_regs vcpu_vmport_regs[1];
};
typedef struct shared_vmport_iopage shared_vmport_iopage_t;
#endif

static shared_vmport_iopage_t *shared_vmport_page;

static QLIST_HEAD(, XenPhysmap) xen_physmap;
static const XenPhysmap *log_for_dirtybit;
/* Buffer used by xen_sync_dirty_bitmap */
static unsigned long *dirty_bitmap;
static Notifier suspend;
static Notifier wakeup;

/* Xen specific function for piix pci */

int xen_pci_slot_get_pirq(PCIDevice *pci_dev, int irq_num)
{
    return irq_num + (PCI_SLOT(pci_dev->devfn) << 2);
}

void xen_intx_set_irq(void *opaque, int irq_num, int level)
{
    xen_set_pci_intx_level(xen_domid, 0, 0, irq_num >> 2,
                           irq_num & 3, level);
}

int xen_set_pci_link_route(uint8_t link, uint8_t irq)
{
    return xendevicemodel_set_pci_link_route(xen_dmod, xen_domid, link, irq);
}

int xen_is_pirq_msi(uint32_t msi_data)
{
    /* If vector is 0, the msi is remapped into a pirq, passed as
     * dest_id.
     */
    return ((msi_data & MSI_DATA_VECTOR_MASK) >> MSI_DATA_VECTOR_SHIFT) == 0;
}

void xen_hvm_inject_msi(uint64_t addr, uint32_t data)
{
    xen_inject_msi(xen_domid, addr, data);
}

static void xen_suspend_notifier(Notifier *notifier, void *data)
{
    xc_set_hvm_param(xen_xc, xen_domid, HVM_PARAM_ACPI_S_STATE, 3);
}

/* Xen Interrupt Controller */

static void xen_set_irq(void *opaque, int irq, int level)
{
    xen_set_isa_irq_level(xen_domid, irq, level);
}

qemu_irq *xen_interrupt_controller_init(void)
{
    return qemu_allocate_irqs(xen_set_irq, NULL, 16);
}

/* Memory Ops */

static void xen_ram_init(PCMachineState *pcms,
                         ram_addr_t ram_size, MemoryRegion **ram_memory_p)
{
    X86MachineState *x86ms = X86_MACHINE(pcms);
    MemoryRegion *sysmem = get_system_memory();
    ram_addr_t block_len;
    uint64_t user_lowmem =
        object_property_get_uint(qdev_get_machine(),
                                 PC_MACHINE_MAX_RAM_BELOW_4G,
                                 &error_abort);

    /* Handle the machine opt max-ram-below-4g.  It is basically doing
     * min(xen limit, user limit).
     */
    if (!user_lowmem) {
        user_lowmem = HVM_BELOW_4G_RAM_END; /* default */
    }
    if (HVM_BELOW_4G_RAM_END <= user_lowmem) {
        user_lowmem = HVM_BELOW_4G_RAM_END;
    }

    if (ram_size >= user_lowmem) {
        x86ms->above_4g_mem_size = ram_size - user_lowmem;
        x86ms->below_4g_mem_size = user_lowmem;
    } else {
        x86ms->above_4g_mem_size = 0;
        x86ms->below_4g_mem_size = ram_size;
    }
    if (!x86ms->above_4g_mem_size) {
        block_len = ram_size;
    } else {
        /*
         * Xen does not allocate the memory continuously, it keeps a
         * hole of the size computed above or passed in.
         */
        block_len = (4 * GiB) + x86ms->above_4g_mem_size;
    }
    memory_region_init_ram(&xen_memory, NULL, "xen.ram", block_len,
                           &error_fatal);
    *ram_memory_p = &xen_memory;

    memory_region_init_alias(&ram_640k, NULL, "xen.ram.640k",
                             &xen_memory, 0, 0xa0000);
    memory_region_add_subregion(sysmem, 0, &ram_640k);
    /* Skip of the VGA IO memory space, it will be registered later by the VGA
     * emulated device.
     *
     * The area between 0xc0000 and 0x100000 will be used by SeaBIOS to load
     * the Options ROM, so it is registered here as RAM.
     */
    memory_region_init_alias(&ram_lo, NULL, "xen.ram.lo",
                             &xen_memory, 0xc0000,
                             x86ms->below_4g_mem_size - 0xc0000);
    memory_region_add_subregion(sysmem, 0xc0000, &ram_lo);
    if (x86ms->above_4g_mem_size > 0) {
        memory_region_init_alias(&ram_hi, NULL, "xen.ram.hi",
                                 &xen_memory, 0x100000000ULL,
                                 x86ms->above_4g_mem_size);
        memory_region_add_subregion(sysmem, 0x100000000ULL, &ram_hi);
    }
}

static XenPhysmap *get_physmapping(hwaddr start_addr, ram_addr_t size,
                                   int page_mask)
{
    XenPhysmap *physmap = NULL;

    start_addr &= page_mask;

    QLIST_FOREACH(physmap, &xen_physmap, list) {
        if (range_covers_byte(physmap->start_addr, physmap->size, start_addr)) {
            return physmap;
        }
    }
    return NULL;
}

static hwaddr xen_phys_offset_to_gaddr(hwaddr phys_offset, ram_addr_t size,
                                       int page_mask)
{
    hwaddr addr = phys_offset & page_mask;
    XenPhysmap *physmap = NULL;

    QLIST_FOREACH(physmap, &xen_physmap, list) {
        if (range_covers_byte(physmap->phys_offset, physmap->size, addr)) {
            return physmap->start_addr + (phys_offset - physmap->phys_offset);
        }
    }

    return phys_offset;
}

#ifdef XEN_COMPAT_PHYSMAP
static int xen_save_physmap(XenIOState *state, XenPhysmap *physmap)
{
    char path[80], value[17];

    snprintf(path, sizeof(path),
            "/local/domain/0/device-model/%d/physmap/%"PRIx64"/start_addr",
            xen_domid, (uint64_t)physmap->phys_offset);
    snprintf(value, sizeof(value), "%"PRIx64, (uint64_t)physmap->start_addr);
    if (!xs_write(state->xenstore, 0, path, value, strlen(value))) {
        return -1;
    }
    snprintf(path, sizeof(path),
            "/local/domain/0/device-model/%d/physmap/%"PRIx64"/size",
            xen_domid, (uint64_t)physmap->phys_offset);
    snprintf(value, sizeof(value), "%"PRIx64, (uint64_t)physmap->size);
    if (!xs_write(state->xenstore, 0, path, value, strlen(value))) {
        return -1;
    }
    if (physmap->name) {
        snprintf(path, sizeof(path),
                "/local/domain/0/device-model/%d/physmap/%"PRIx64"/name",
                xen_domid, (uint64_t)physmap->phys_offset);
        if (!xs_write(state->xenstore, 0, path,
                      physmap->name, strlen(physmap->name))) {
            return -1;
        }
    }
    return 0;
}
#else
static int xen_save_physmap(XenIOState *state, XenPhysmap *physmap)
{
    return 0;
}
#endif

static int xen_add_to_physmap(XenIOState *state,
                              hwaddr start_addr,
                              ram_addr_t size,
                              MemoryRegion *mr,
                              hwaddr offset_within_region)
{
    unsigned target_page_bits = qemu_target_page_bits();
    int page_size = qemu_target_page_size();
    int page_mask = -page_size;
    unsigned long nr_pages;
    int rc = 0;
    XenPhysmap *physmap = NULL;
    hwaddr pfn, start_gpfn;
    hwaddr phys_offset = memory_region_get_ram_addr(mr);
    const char *mr_name;

    if (get_physmapping(start_addr, size, page_mask)) {
        return 0;
    }
    if (size <= 0) {
        return -1;
    }

    /* Xen can only handle a single dirty log region for now and we want
     * the linear framebuffer to be that region.
     * Avoid tracking any regions that is not videoram and avoid tracking
     * the legacy vga region. */
    if (mr == framebuffer && start_addr > 0xbffff) {
        goto go_physmap;
    }
    return -1;

go_physmap:
    DPRINTF("mapping vram to %"HWADDR_PRIx" - %"HWADDR_PRIx"\n",
            start_addr, start_addr + size);

    mr_name = memory_region_name(mr);

    physmap = g_new(XenPhysmap, 1);

    physmap->start_addr = start_addr;
    physmap->size = size;
    physmap->name = mr_name;
    physmap->phys_offset = phys_offset;

    QLIST_INSERT_HEAD(&xen_physmap, physmap, list);

    if (runstate_check(RUN_STATE_INMIGRATE)) {
        /* Now when we have a physmap entry we can replace a dummy mapping with
         * a real one of guest foreign memory. */
        uint8_t *p = xen_replace_cache_entry(phys_offset, start_addr, size);
        assert(p && p == memory_region_get_ram_ptr(mr));

        return 0;
    }

    pfn = phys_offset >> target_page_bits;
    start_gpfn = start_addr >> target_page_bits;
    nr_pages = size >> target_page_bits;
    rc = xendevicemodel_relocate_memory(xen_dmod, xen_domid, nr_pages, pfn,
                                        start_gpfn);
    if (rc) {
        int saved_errno = errno;

        error_report("relocate_memory %lu pages from GFN %"HWADDR_PRIx
                     " to GFN %"HWADDR_PRIx" failed: %s",
                     nr_pages, pfn, start_gpfn, strerror(saved_errno));
        errno = saved_errno;
        return -1;
    }

    rc = xendevicemodel_pin_memory_cacheattr(xen_dmod, xen_domid,
                                   start_addr >> target_page_bits,
                                   (start_addr + size - 1) >> target_page_bits,
                                   XEN_DOMCTL_MEM_CACHEATTR_WB);
    if (rc) {
        error_report("pin_memory_cacheattr failed: %s", strerror(errno));
    }
    return xen_save_physmap(state, physmap);
}

static int xen_remove_from_physmap(XenIOState *state,
                                   hwaddr start_addr,
                                   ram_addr_t size)
{
    unsigned target_page_bits = qemu_target_page_bits();
    int page_size = qemu_target_page_size();
    int page_mask = -page_size;
    int rc = 0;
    XenPhysmap *physmap = NULL;
    hwaddr phys_offset = 0;

    physmap = get_physmapping(start_addr, size, page_mask);
    if (physmap == NULL) {
        return -1;
    }

    phys_offset = physmap->phys_offset;
    size = physmap->size;

    DPRINTF("unmapping vram to %"HWADDR_PRIx" - %"HWADDR_PRIx", at "
            "%"HWADDR_PRIx"\n", start_addr, start_addr + size, phys_offset);

    size >>= target_page_bits;
    start_addr >>= target_page_bits;
    phys_offset >>= target_page_bits;
    rc = xendevicemodel_relocate_memory(xen_dmod, xen_domid, size, start_addr,
                                        phys_offset);
    if (rc) {
        int saved_errno = errno;

        error_report("relocate_memory "RAM_ADDR_FMT" pages"
                     " from GFN %"HWADDR_PRIx
                     " to GFN %"HWADDR_PRIx" failed: %s",
                     size, start_addr, phys_offset, strerror(saved_errno));
        errno = saved_errno;
        return -1;
    }

    QLIST_REMOVE(physmap, list);
    if (log_for_dirtybit == physmap) {
        log_for_dirtybit = NULL;
        g_free(dirty_bitmap);
        dirty_bitmap = NULL;
    }
    g_free(physmap);

    return 0;
}

static void xen_sync_dirty_bitmap(XenIOState *state,
                                  hwaddr start_addr,
                                  ram_addr_t size)
{
    unsigned target_page_bits = qemu_target_page_bits();
    int page_size = qemu_target_page_size();
    int page_mask = -page_size;
    hwaddr npages = size >> target_page_bits;
    const int width = sizeof(unsigned long) * 8;
    size_t bitmap_size = DIV_ROUND_UP(npages, width);
    int rc, i, j;
    const XenPhysmap *physmap = NULL;

    physmap = get_physmapping(start_addr, size, page_mask);
    if (physmap == NULL) {
        /* not handled */
        return;
    }

    if (log_for_dirtybit == NULL) {
        log_for_dirtybit = physmap;
        dirty_bitmap = g_new(unsigned long, bitmap_size);
    } else if (log_for_dirtybit != physmap) {
        /* Only one range for dirty bitmap can be tracked. */
        return;
    }

    rc = xen_track_dirty_vram(xen_domid, start_addr >> target_page_bits,
                              npages, dirty_bitmap);
    if (rc < 0) {
#ifndef ENODATA
#define ENODATA  ENOENT
#endif
        if (errno == ENODATA) {
            memory_region_set_dirty(framebuffer, 0, size);
            DPRINTF("xen: track_dirty_vram failed (0x" HWADDR_FMT_plx
                    ", 0x" HWADDR_FMT_plx "): %s\n",
                    start_addr, start_addr + size, strerror(errno));
        }
        return;
    }

    for (i = 0; i < bitmap_size; i++) {
        unsigned long map = dirty_bitmap[i];
        while (map != 0) {
            j = ctzl(map);
            map &= ~(1ul << j);
            memory_region_set_dirty(framebuffer,
                                    (i * width + j) * page_size, page_size);
        };
    }
}

static void xen_log_start(MemoryListener *listener,
                          MemoryRegionSection *section,
                          int old, int new)
{
    XenIOState *state = container_of(listener, XenIOState, memory_listener);

    if (new & ~old & (1 << DIRTY_MEMORY_VGA)) {
        xen_sync_dirty_bitmap(state, section->offset_within_address_space,
                              int128_get64(section->size));
    }
}

static void xen_log_stop(MemoryListener *listener, MemoryRegionSection *section,
                         int old, int new)
{
    if (old & ~new & (1 << DIRTY_MEMORY_VGA)) {
        log_for_dirtybit = NULL;
        g_free(dirty_bitmap);
        dirty_bitmap = NULL;
        /* Disable dirty bit tracking */
        xen_track_dirty_vram(xen_domid, 0, 0, NULL);
    }
}

static void xen_log_sync(MemoryListener *listener, MemoryRegionSection *section)
{
    XenIOState *state = container_of(listener, XenIOState, memory_listener);

    xen_sync_dirty_bitmap(state, section->offset_within_address_space,
                          int128_get64(section->size));
}

static bool xen_log_global_start(MemoryListener *listener, Error **errp)
{
    if (xen_enabled()) {
        xen_in_migration = true;
    }
    return true;
}

static void xen_log_global_stop(MemoryListener *listener)
{
    xen_in_migration = false;
}

static const MemoryListener xen_memory_listener = {
    .name = "xen-memory",
    .region_add = xen_region_add,
    .region_del = xen_region_del,
    .log_start = xen_log_start,
    .log_stop = xen_log_stop,
    .log_sync = xen_log_sync,
    .log_global_start = xen_log_global_start,
    .log_global_stop = xen_log_global_stop,
    .priority = MEMORY_LISTENER_PRIORITY_ACCEL,
};

static void regs_to_cpu(vmware_regs_t *vmport_regs, ioreq_t *req)
{
    X86CPU *cpu;
    CPUX86State *env;

    cpu = X86_CPU(current_cpu);
    env = &cpu->env;
    env->regs[R_EAX] = req->data;
    env->regs[R_EBX] = vmport_regs->ebx;
    env->regs[R_ECX] = vmport_regs->ecx;
    env->regs[R_EDX] = vmport_regs->edx;
    env->regs[R_ESI] = vmport_regs->esi;
    env->regs[R_EDI] = vmport_regs->edi;
}

static void regs_from_cpu(vmware_regs_t *vmport_regs)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    CPUX86State *env = &cpu->env;

    vmport_regs->ebx = env->regs[R_EBX];
    vmport_regs->ecx = env->regs[R_ECX];
    vmport_regs->edx = env->regs[R_EDX];
    vmport_regs->esi = env->regs[R_ESI];
    vmport_regs->edi = env->regs[R_EDI];
}

static void handle_vmport_ioreq(XenIOState *state, ioreq_t *req)
{
    vmware_regs_t *vmport_regs;

    assert(shared_vmport_page);
    vmport_regs =
        &shared_vmport_page->vcpu_vmport_regs[state->send_vcpu];
    QEMU_BUILD_BUG_ON(sizeof(*req) < sizeof(*vmport_regs));

    current_cpu = state->cpu_by_vcpu_id[state->send_vcpu];
    regs_to_cpu(vmport_regs, req);
    cpu_ioreq_pio(req);
    regs_from_cpu(vmport_regs);
    current_cpu = NULL;
}

#ifdef XEN_COMPAT_PHYSMAP
static void xen_read_physmap(XenIOState *state)
{
    XenPhysmap *physmap = NULL;
    unsigned int len, num, i;
    char path[80], *value = NULL;
    char **entries = NULL;

    snprintf(path, sizeof(path),
            "/local/domain/0/device-model/%d/physmap", xen_domid);
    entries = xs_directory(state->xenstore, 0, path, &num);
    if (entries == NULL)
        return;

    for (i = 0; i < num; i++) {
        physmap = g_new(XenPhysmap, 1);
        physmap->phys_offset = strtoull(entries[i], NULL, 16);
        snprintf(path, sizeof(path),
                "/local/domain/0/device-model/%d/physmap/%s/start_addr",
                xen_domid, entries[i]);
        value = xs_read(state->xenstore, 0, path, &len);
        if (value == NULL) {
            g_free(physmap);
            continue;
        }
        physmap->start_addr = strtoull(value, NULL, 16);
        free(value);

        snprintf(path, sizeof(path),
                "/local/domain/0/device-model/%d/physmap/%s/size",
                xen_domid, entries[i]);
        value = xs_read(state->xenstore, 0, path, &len);
        if (value == NULL) {
            g_free(physmap);
            continue;
        }
        physmap->size = strtoull(value, NULL, 16);
        free(value);

        snprintf(path, sizeof(path),
                "/local/domain/0/device-model/%d/physmap/%s/name",
                xen_domid, entries[i]);
        physmap->name = xs_read(state->xenstore, 0, path, &len);

        QLIST_INSERT_HEAD(&xen_physmap, physmap, list);
    }
    free(entries);
}
#else
static void xen_read_physmap(XenIOState *state)
{
}
#endif

static void xen_wakeup_notifier(Notifier *notifier, void *data)
{
    xc_set_hvm_param(xen_xc, xen_domid, HVM_PARAM_ACPI_S_STATE, 0);
}

static bool xen_check_stubdomain(struct xs_handle *xsh)
{
    char *dm_path = g_strdup_printf(
        "/local/domain/%d/image/device-model-domid", xen_domid);
    char *val;
    int32_t dm_domid;
    bool is_stubdom = false;

    val = xs_read(xsh, 0, dm_path, NULL);
    if (val) {
        if (sscanf(val, "%d", &dm_domid) == 1) {
            is_stubdom = dm_domid != 0;
        }
        free(val);
    }

    g_free(dm_path);
    return is_stubdom;
}

void xen_hvm_init_pc(PCMachineState *pcms, MemoryRegion **ram_memory)
{
    MachineState *ms = MACHINE(pcms);
    unsigned int max_cpus = ms->smp.max_cpus;
    int rc;
    xen_pfn_t ioreq_pfn;
    XenIOState *state;

    state = g_new0(XenIOState, 1);

    xen_register_ioreq(state, max_cpus,
                       HVM_IOREQSRV_BUFIOREQ_ATOMIC,
                       &xen_memory_listener);

    xen_is_stubdomain = xen_check_stubdomain(state->xenstore);

    QLIST_INIT(&xen_physmap);
    xen_read_physmap(state);

    suspend.notify = xen_suspend_notifier;
    qemu_register_suspend_notifier(&suspend);

    wakeup.notify = xen_wakeup_notifier;
    qemu_register_wakeup_notifier(&wakeup);

    rc = xen_get_vmport_regs_pfn(xen_xc, xen_domid, &ioreq_pfn);
    if (!rc) {
        DPRINTF("shared vmport page at pfn %lx\n", ioreq_pfn);
        shared_vmport_page =
            xenforeignmemory_map(xen_fmem, xen_domid, PROT_READ|PROT_WRITE,
                                 1, &ioreq_pfn, NULL);
        if (shared_vmport_page == NULL) {
            error_report("map shared vmport IO page returned error %d handle=%p",
                         errno, xen_xc);
            goto err;
        }
    } else if (rc != -ENOSYS) {
        error_report("get vmport regs pfn returned error %d, rc=%d",
                     errno, rc);
        goto err;
    }

    xen_ram_init(pcms, ms->ram_size, ram_memory);

    /* Disable ACPI build because Xen handles it */
    pcms->acpi_build_enabled = false;

    return;

err:
    error_report("xen hardware virtual machine initialisation failed");
    exit(1);
}

void xen_register_framebuffer(MemoryRegion *mr)
{
    framebuffer = mr;
}

void xen_hvm_modified_memory(ram_addr_t start, ram_addr_t length)
{
    unsigned target_page_bits = qemu_target_page_bits();
    int page_size = qemu_target_page_size();
    int page_mask = -page_size;

    if (unlikely(xen_in_migration)) {
        int rc;
        ram_addr_t start_pfn, nb_pages;

        start = xen_phys_offset_to_gaddr(start, length, page_mask);

        if (length == 0) {
            length = page_size;
        }
        start_pfn = start >> target_page_bits;
        nb_pages = ((start + length + page_size - 1) >> target_page_bits)
            - start_pfn;
        rc = xen_modified_memory(xen_domid, start_pfn, nb_pages);
        if (rc) {
            fprintf(stderr,
                    "%s failed for "RAM_ADDR_FMT" ("RAM_ADDR_FMT"): %i, %s\n",
                    __func__, start, nb_pages, errno, strerror(errno));
        }
    }
}

void qmp_xen_set_global_dirty_log(bool enable, Error **errp)
{
    if (enable) {
        memory_global_dirty_log_start(GLOBAL_DIRTY_MIGRATION, errp);
    } else {
        memory_global_dirty_log_stop(GLOBAL_DIRTY_MIGRATION);
    }
}

void arch_xen_set_memory(XenIOState *state, MemoryRegionSection *section,
                                bool add)
{
    unsigned target_page_bits = qemu_target_page_bits();
    int page_size = qemu_target_page_size();
    int page_mask = -page_size;
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    bool log_dirty = memory_region_is_logging(section->mr, DIRTY_MEMORY_VGA);
    hvmmem_type_t mem_type;

    if (!memory_region_is_ram(section->mr)) {
        return;
    }

    if (log_dirty != add) {
        return;
    }

    trace_xen_client_set_memory(start_addr, size, log_dirty);

    start_addr &= page_mask;
    size = ROUND_UP(size, page_size);

    if (add) {
        if (!memory_region_is_rom(section->mr)) {
            xen_add_to_physmap(state, start_addr, size,
                               section->mr, section->offset_within_region);
        } else {
            mem_type = HVMMEM_ram_ro;
            if (xen_set_mem_type(xen_domid, mem_type,
                                 start_addr >> target_page_bits,
                                 size >> target_page_bits)) {
                DPRINTF("xen_set_mem_type error, addr: "HWADDR_FMT_plx"\n",
                        start_addr);
            }
        }
    } else {
        if (xen_remove_from_physmap(state, start_addr, size) < 0) {
            DPRINTF("physmapping does not exist at "HWADDR_FMT_plx"\n", start_addr);
        }
    }
}

void arch_handle_ioreq(XenIOState *state, ioreq_t *req)
{
    switch (req->type) {
    case IOREQ_TYPE_VMWARE_PORT:
            handle_vmport_ioreq(state, req);
        break;
    default:
        hw_error("Invalid ioreq type 0x%x\n", req->type);
    }
}
