/*
 * ARM Integrator CP System emulation.
 *
 * Copyright (c) 2005-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/boards.h"
#include "hw/arm/boot.h"
#include "hw/misc/arm_integrator_debug.h"
#include "hw/net/smc91c111.h"
#include "net/net.h"
#include "exec/address-spaces.h"
#include "system/runstate.h"
#include "system/system.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/char/pl011.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/sd/sd.h"
#include "qom/object.h"
#include "audio/audio.h"
#include "target/arm/cpu-qom.h"

#define TYPE_INTEGRATOR_CM "integrator_core"
OBJECT_DECLARE_SIMPLE_TYPE(IntegratorCMState, INTEGRATOR_CM)

struct IntegratorCMState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    uint32_t memsz;
    MemoryRegion flash;
    uint32_t cm_osc;
    uint32_t cm_ctrl;
    uint32_t cm_lock;
    uint32_t cm_auxosc;
    uint32_t cm_sdram;
    uint32_t cm_init;
    uint32_t cm_flags;
    uint32_t cm_nvflags;
    uint32_t cm_refcnt_offset;
    uint32_t int_level;
    uint32_t irq_enabled;
    uint32_t fiq_enabled;
};

static uint8_t integrator_spd[128] = {
   128, 8, 4, 11, 9, 1, 64, 0,  2, 0xa0, 0xa0, 0, 0, 8, 0, 1,
   0xe, 4, 0x1c, 1, 2, 0x20, 0xc0, 0, 0, 0, 0, 0x30, 0x28, 0x30, 0x28, 0x40
};

static const VMStateDescription vmstate_integratorcm = {
    .name = "integratorcm",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cm_osc, IntegratorCMState),
        VMSTATE_UINT32(cm_ctrl, IntegratorCMState),
        VMSTATE_UINT32(cm_lock, IntegratorCMState),
        VMSTATE_UINT32(cm_auxosc, IntegratorCMState),
        VMSTATE_UINT32(cm_sdram, IntegratorCMState),
        VMSTATE_UINT32(cm_init, IntegratorCMState),
        VMSTATE_UINT32(cm_flags, IntegratorCMState),
        VMSTATE_UINT32(cm_nvflags, IntegratorCMState),
        VMSTATE_UINT32(int_level, IntegratorCMState),
        VMSTATE_UINT32(irq_enabled, IntegratorCMState),
        VMSTATE_UINT32(fiq_enabled, IntegratorCMState),
        VMSTATE_END_OF_LIST()
    }
};

static uint64_t integratorcm_read(void *opaque, hwaddr offset,
                                  unsigned size)
{
    IntegratorCMState *s = opaque;
    if (offset >= 0x100 && offset < 0x200) {
        /* CM_SPD */
        if (offset >= 0x180)
            return 0;
        return integrator_spd[offset >> 2];
    }
    switch (offset >> 2) {
    case 0: /* CM_ID */
        return 0x411a3001;
    case 1: /* CM_PROC */
        return 0;
    case 2: /* CM_OSC */
        return s->cm_osc;
    case 3: /* CM_CTRL */
        return s->cm_ctrl;
    case 4: /* CM_STAT */
        return 0x00100000;
    case 5: /* CM_LOCK */
        if (s->cm_lock == 0xa05f) {
            return 0x1a05f;
        } else {
            return s->cm_lock;
        }
    case 6: /* CM_LMBUSCNT */
        /* ??? High frequency timer.  */
        hw_error("integratorcm_read: CM_LMBUSCNT");
    case 7: /* CM_AUXOSC */
        return s->cm_auxosc;
    case 8: /* CM_SDRAM */
        return s->cm_sdram;
    case 9: /* CM_INIT */
        return s->cm_init;
    case 10: /* CM_REFCNT */
        /* This register, CM_REFCNT, provides a 32-bit count value.
         * The count increments at the fixed reference clock frequency of 24MHz
         * and can be used as a real-time counter.
         */
        return (uint32_t)muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), 24,
                                  1000) - s->cm_refcnt_offset;
    case 12: /* CM_FLAGS */
        return s->cm_flags;
    case 14: /* CM_NVFLAGS */
        return s->cm_nvflags;
    case 16: /* CM_IRQ_STAT */
        return s->int_level & s->irq_enabled;
    case 17: /* CM_IRQ_RSTAT */
        return s->int_level;
    case 18: /* CM_IRQ_ENSET */
        return s->irq_enabled;
    case 20: /* CM_SOFT_INTSET */
        return s->int_level & 1;
    case 24: /* CM_FIQ_STAT */
        return s->int_level & s->fiq_enabled;
    case 25: /* CM_FIQ_RSTAT */
        return s->int_level;
    case 26: /* CM_FIQ_ENSET */
        return s->fiq_enabled;
    case 32: /* CM_VOLTAGE_CTL0 */
    case 33: /* CM_VOLTAGE_CTL1 */
    case 34: /* CM_VOLTAGE_CTL2 */
    case 35: /* CM_VOLTAGE_CTL3 */
        /* ??? Voltage control unimplemented.  */
        return 0;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Unimplemented offset 0x%" HWADDR_PRIX "\n",
                      __func__, offset);
        return 0;
    }
}

static void integratorcm_do_remap(IntegratorCMState *s)
{
    /* Sync memory region state with CM_CTRL REMAP bit:
     * bit 0 => flash at address 0; bit 1 => RAM
     */
    memory_region_set_enabled(&s->flash, !(s->cm_ctrl & 4));
}

static void integratorcm_set_ctrl(IntegratorCMState *s, uint32_t value)
{
    if (value & 8) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
    if ((s->cm_ctrl ^ value) & 1) {
        /* (value & 1) != 0 means the green "MISC LED" is lit.
         * We don't have any nice place to display LEDs. printf is a bad
         * idea because Linux uses the LED as a heartbeat and the output
         * will swamp anything else on the terminal.
         */
    }
    /* Note that the RESET bit [3] always reads as zero */
    s->cm_ctrl = (s->cm_ctrl & ~5) | (value & 5);
    integratorcm_do_remap(s);
}

static void integratorcm_update(IntegratorCMState *s)
{
    /* ??? The CPU irq/fiq is raised when either the core module or base PIC
       are active.  */
    if (s->int_level & (s->irq_enabled | s->fiq_enabled))
        hw_error("Core module interrupt\n");
}

static void integratorcm_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    IntegratorCMState *s = opaque;
    switch (offset >> 2) {
    case 2: /* CM_OSC */
        if (s->cm_lock == 0xa05f)
            s->cm_osc = value;
        break;
    case 3: /* CM_CTRL */
        integratorcm_set_ctrl(s, value);
        break;
    case 5: /* CM_LOCK */
        s->cm_lock = value & 0xffff;
        break;
    case 7: /* CM_AUXOSC */
        if (s->cm_lock == 0xa05f)
            s->cm_auxosc = value;
        break;
    case 8: /* CM_SDRAM */
        s->cm_sdram = value;
        break;
    case 9: /* CM_INIT */
        /* ??? This can change the memory bus frequency.  */
        s->cm_init = value;
        break;
    case 12: /* CM_FLAGSS */
        s->cm_flags |= value;
        break;
    case 13: /* CM_FLAGSC */
        s->cm_flags &= ~value;
        break;
    case 14: /* CM_NVFLAGSS */
        s->cm_nvflags |= value;
        break;
    case 15: /* CM_NVFLAGSS */
        s->cm_nvflags &= ~value;
        break;
    case 18: /* CM_IRQ_ENSET */
        s->irq_enabled |= value;
        integratorcm_update(s);
        break;
    case 19: /* CM_IRQ_ENCLR */
        s->irq_enabled &= ~value;
        integratorcm_update(s);
        break;
    case 20: /* CM_SOFT_INTSET */
        s->int_level |= (value & 1);
        integratorcm_update(s);
        break;
    case 21: /* CM_SOFT_INTCLR */
        s->int_level &= ~(value & 1);
        integratorcm_update(s);
        break;
    case 26: /* CM_FIQ_ENSET */
        s->fiq_enabled |= value;
        integratorcm_update(s);
        break;
    case 27: /* CM_FIQ_ENCLR */
        s->fiq_enabled &= ~value;
        integratorcm_update(s);
        break;
    case 32: /* CM_VOLTAGE_CTL0 */
    case 33: /* CM_VOLTAGE_CTL1 */
    case 34: /* CM_VOLTAGE_CTL2 */
    case 35: /* CM_VOLTAGE_CTL3 */
        /* ??? Voltage control unimplemented.  */
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Unimplemented offset 0x%" HWADDR_PRIX "\n",
                      __func__, offset);
        break;
    }
}

/* Integrator/CM control registers.  */

static const MemoryRegionOps integratorcm_ops = {
    .read = integratorcm_read,
    .write = integratorcm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void integratorcm_init(Object *obj)
{
    IntegratorCMState *s = INTEGRATOR_CM(obj);

    s->cm_osc = 0x01000048;
    /* ??? What should the high bits of this value be?  */
    s->cm_auxosc = 0x0007feff;
    s->cm_sdram = 0x00011122;
    memcpy(integrator_spd + 73, "QEMU-MEMORY", 11);
    s->cm_init = 0x00000112;
    s->cm_refcnt_offset = muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), 24,
                                   1000);

    /* ??? Save/restore.  */
}

static void integratorcm_realize(DeviceState *d, Error **errp)
{
    IntegratorCMState *s = INTEGRATOR_CM(d);
    SysBusDevice *dev = SYS_BUS_DEVICE(d);

    if (!memory_region_init_ram(&s->flash, OBJECT(d), "integrator.flash",
                                0x100000, errp)) {
        return;
    }

    memory_region_init_io(&s->iomem, OBJECT(d), &integratorcm_ops, s,
                          "integratorcm", 0x00800000);
    sysbus_init_mmio(dev, &s->iomem);

    integratorcm_do_remap(s);

    if (s->memsz >= 256) {
        integrator_spd[31] = 64;
        s->cm_sdram |= 0x10;
    } else if (s->memsz >= 128) {
        integrator_spd[31] = 32;
        s->cm_sdram |= 0x0c;
    } else if (s->memsz >= 64) {
        integrator_spd[31] = 16;
        s->cm_sdram |= 0x08;
    } else if (s->memsz >= 32) {
        integrator_spd[31] = 4;
        s->cm_sdram |= 0x04;
    } else {
        integrator_spd[31] = 2;
    }
}

/* Integrator/CP hardware emulation.  */
/* Primary interrupt controller.  */

#define TYPE_INTEGRATOR_PIC "integrator_pic"
OBJECT_DECLARE_SIMPLE_TYPE(icp_pic_state, INTEGRATOR_PIC)

struct icp_pic_state {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    uint32_t level;
    uint32_t irq_enabled;
    uint32_t fiq_enabled;
    qemu_irq parent_irq;
    qemu_irq parent_fiq;
};

static const VMStateDescription vmstate_icp_pic = {
    .name = "icp_pic",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(level, icp_pic_state),
        VMSTATE_UINT32(irq_enabled, icp_pic_state),
        VMSTATE_UINT32(fiq_enabled, icp_pic_state),
        VMSTATE_END_OF_LIST()
    }
};

static void icp_pic_update(icp_pic_state *s)
{
    uint32_t flags;

    flags = (s->level & s->irq_enabled);
    qemu_set_irq(s->parent_irq, flags != 0);
    flags = (s->level & s->fiq_enabled);
    qemu_set_irq(s->parent_fiq, flags != 0);
}

static void icp_pic_set_irq(void *opaque, int irq, int level)
{
    icp_pic_state *s = (icp_pic_state *)opaque;
    if (level)
        s->level |= 1 << irq;
    else
        s->level &= ~(1 << irq);
    icp_pic_update(s);
}

static uint64_t icp_pic_read(void *opaque, hwaddr offset,
                             unsigned size)
{
    icp_pic_state *s = (icp_pic_state *)opaque;

    switch (offset >> 2) {
    case 0: /* IRQ_STATUS */
        return s->level & s->irq_enabled;
    case 1: /* IRQ_RAWSTAT */
        return s->level;
    case 2: /* IRQ_ENABLESET */
        return s->irq_enabled;
    case 4: /* INT_SOFTSET */
        return s->level & 1;
    case 8: /* FRQ_STATUS */
        return s->level & s->fiq_enabled;
    case 9: /* FRQ_RAWSTAT */
        return s->level;
    case 10: /* FRQ_ENABLESET */
        return s->fiq_enabled;
    case 3: /* IRQ_ENABLECLR */
    case 5: /* INT_SOFTCLR */
    case 11: /* FRQ_ENABLECLR */
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIX "\n",
                      __func__, offset);
        return 0;
    }
}

static void icp_pic_write(void *opaque, hwaddr offset,
                          uint64_t value, unsigned size)
{
    icp_pic_state *s = (icp_pic_state *)opaque;

    switch (offset >> 2) {
    case 2: /* IRQ_ENABLESET */
        s->irq_enabled |= value;
        break;
    case 3: /* IRQ_ENABLECLR */
        s->irq_enabled &= ~value;
        break;
    case 4: /* INT_SOFTSET */
        if (value & 1)
            icp_pic_set_irq(s, 0, 1);
        break;
    case 5: /* INT_SOFTCLR */
        if (value & 1)
            icp_pic_set_irq(s, 0, 0);
        break;
    case 10: /* FRQ_ENABLESET */
        s->fiq_enabled |= value;
        break;
    case 11: /* FRQ_ENABLECLR */
        s->fiq_enabled &= ~value;
        break;
    case 0: /* IRQ_STATUS */
    case 1: /* IRQ_RAWSTAT */
    case 8: /* FRQ_STATUS */
    case 9: /* FRQ_RAWSTAT */
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIX "\n",
                      __func__, offset);
        return;
    }
    icp_pic_update(s);
}

static const MemoryRegionOps icp_pic_ops = {
    .read = icp_pic_read,
    .write = icp_pic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void icp_pic_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    icp_pic_state *s = INTEGRATOR_PIC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    qdev_init_gpio_in(dev, icp_pic_set_irq, 32);
    sysbus_init_irq(sbd, &s->parent_irq);
    sysbus_init_irq(sbd, &s->parent_fiq);
    memory_region_init_io(&s->iomem, obj, &icp_pic_ops, s,
                          "icp-pic", 0x00800000);
    sysbus_init_mmio(sbd, &s->iomem);
}

/* CP control registers.  */

#define TYPE_ICP_CONTROL_REGS "icp-ctrl-regs"
OBJECT_DECLARE_SIMPLE_TYPE(ICPCtrlRegsState, ICP_CONTROL_REGS)

struct ICPCtrlRegsState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;

    qemu_irq mmc_irq;
    uint32_t intreg_state;
};

#define ICP_GPIO_MMC_WPROT      "mmc-wprot"
#define ICP_GPIO_MMC_CARDIN     "mmc-cardin"

#define ICP_INTREG_WPROT        (1 << 0)
#define ICP_INTREG_CARDIN       (1 << 3)

static const VMStateDescription vmstate_icp_control = {
    .name = "icp_control",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(intreg_state, ICPCtrlRegsState),
        VMSTATE_END_OF_LIST()
    }
};

static uint64_t icp_control_read(void *opaque, hwaddr offset,
                                 unsigned size)
{
    ICPCtrlRegsState *s = opaque;

    switch (offset >> 2) {
    case 0: /* CP_IDFIELD */
        return 0x41034003;
    case 1: /* CP_FLASHPROG */
        return 0;
    case 2: /* CP_INTREG */
        return s->intreg_state;
    case 3: /* CP_DECODE */
        return 0x11;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIX "\n",
                      __func__, offset);
        return 0;
    }
}

static void icp_control_write(void *opaque, hwaddr offset,
                          uint64_t value, unsigned size)
{
    ICPCtrlRegsState *s = opaque;

    switch (offset >> 2) {
    case 2: /* CP_INTREG */
        s->intreg_state &= ~(value & ICP_INTREG_CARDIN);
        qemu_set_irq(s->mmc_irq, !!(s->intreg_state & ICP_INTREG_CARDIN));
        break;
    case 1: /* CP_FLASHPROG */
    case 3: /* CP_DECODE */
        /* Nothing interesting implemented yet.  */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIX "\n",
                      __func__, offset);
    }
}

static const MemoryRegionOps icp_control_ops = {
    .read = icp_control_read,
    .write = icp_control_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void icp_control_mmc_wprot(void *opaque, int line, int level)
{
    ICPCtrlRegsState *s = opaque;

    s->intreg_state &= ~ICP_INTREG_WPROT;
    if (level) {
        s->intreg_state |= ICP_INTREG_WPROT;
    }
}

static void icp_control_mmc_cardin(void *opaque, int line, int level)
{
    ICPCtrlRegsState *s = opaque;

    /* line is released by writing to CP_INTREG */
    if (level) {
        s->intreg_state |= ICP_INTREG_CARDIN;
        qemu_set_irq(s->mmc_irq, 1);
    }
}

static void icp_control_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    ICPCtrlRegsState *s = ICP_CONTROL_REGS(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &icp_control_ops, s,
                          "icp_ctrl_regs", 0x00800000);
    sysbus_init_mmio(sbd, &s->iomem);

    qdev_init_gpio_in_named(dev, icp_control_mmc_wprot, ICP_GPIO_MMC_WPROT, 1);
    qdev_init_gpio_in_named(dev, icp_control_mmc_cardin,
                            ICP_GPIO_MMC_CARDIN, 1);
    sysbus_init_irq(sbd, &s->mmc_irq);
}


/* Board init.  */

static struct arm_boot_info integrator_binfo = {
    .loader_start = 0x0,
    .board_id = 0x113,
};

static void integratorcp_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    Object *cpuobj;
    ARMCPU *cpu;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ram_alias = g_new(MemoryRegion, 1);
    qemu_irq pic[32];
    DeviceState *dev, *sic, *icp;
    DriveInfo *dinfo;
    int i;

    cpuobj = object_new(machine->cpu_type);

    /* By default ARM1176 CPUs have EL3 enabled.  This board does not
     * currently support EL3 so the CPU EL3 property is disabled before
     * realization.
     */
    if (object_property_find(cpuobj, "has_el3")) {
        object_property_set_bool(cpuobj, "has_el3", false, &error_fatal);
    }

    qdev_realize(DEVICE(cpuobj), NULL, &error_fatal);

    cpu = ARM_CPU(cpuobj);

    /* ??? On a real system the first 1Mb is mapped as SSRAM or boot flash.  */
    /* ??? RAM should repeat to fill physical memory space.  */
    /* SDRAM at address zero*/
    memory_region_add_subregion(address_space_mem, 0, machine->ram);
    /* And again at address 0x80000000 */
    memory_region_init_alias(ram_alias, NULL, "ram.alias", machine->ram,
                             0, ram_size);
    memory_region_add_subregion(address_space_mem, 0x80000000, ram_alias);

    dev = qdev_new(TYPE_INTEGRATOR_CM);
    qdev_prop_set_uint32(dev, "memsz", ram_size >> 20);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map((SysBusDevice *)dev, 0, 0x10000000);

    dev = sysbus_create_varargs(TYPE_INTEGRATOR_PIC, 0x14000000,
                                qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ),
                                qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_FIQ),
                                NULL);
    for (i = 0; i < 32; i++) {
        pic[i] = qdev_get_gpio_in(dev, i);
    }
    sic = sysbus_create_simple(TYPE_INTEGRATOR_PIC, 0xca000000, pic[26]);
    sysbus_create_varargs("integrator_pit", 0x13000000,
                          pic[5], pic[6], pic[7], NULL);
    sysbus_create_simple("pl031", 0x15000000, pic[8]);
    pl011_create(0x16000000, pic[1], serial_hd(0));
    pl011_create(0x17000000, pic[2], serial_hd(1));
    icp = sysbus_create_simple(TYPE_ICP_CONTROL_REGS, 0xcb000000,
                               qdev_get_gpio_in(sic, 3));
    sysbus_create_simple("pl050_keyboard", 0x18000000, pic[3]);
    sysbus_create_simple("pl050_mouse", 0x19000000, pic[4]);
    sysbus_create_simple(TYPE_INTEGRATOR_DEBUG, 0x1a000000, 0);

    dev = sysbus_create_varargs("pl181", 0x1c000000, pic[23], pic[24], NULL);
    qdev_connect_gpio_out_named(dev, "card-read-only", 0,
                          qdev_get_gpio_in_named(icp, ICP_GPIO_MMC_WPROT, 0));
    qdev_connect_gpio_out_named(dev, "card-inserted", 0,
                          qdev_get_gpio_in_named(icp, ICP_GPIO_MMC_CARDIN, 0));
    dinfo = drive_get(IF_SD, 0, 0);
    if (dinfo) {
        DeviceState *card;

        card = qdev_new(TYPE_SD_CARD);
        qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
        qdev_realize_and_unref(card, qdev_get_child_bus(dev, "sd-bus"),
                               &error_fatal);
    }

    dev = qdev_new("pl041");
    if (machine->audiodev) {
        qdev_prop_set_string(dev, "audiodev", machine->audiodev);
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0x1d000000);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic[25]);

    if (qemu_find_nic_info("smc91c111", true, NULL)) {
        smc91c111_init(0xc8000000, pic[27]);
    }

    dev = qdev_new("pl110");
    object_property_set_link(OBJECT(dev), "framebuffer-memory",
                             OBJECT(address_space_mem), &error_fatal);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0xc0000000);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic[22]);

    integrator_binfo.ram_size = ram_size;
    arm_load_kernel(cpu, machine, &integrator_binfo);
}

static void integratorcp_machine_init(MachineClass *mc)
{
    mc->desc = "ARM Integrator/CP (ARM926EJ-S)";
    mc->init = integratorcp_init;
    mc->ignore_memory_transaction_failures = true;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm926");
    mc->default_ram_id = "integrator.ram";

    machine_add_audiodev_property(mc);
}

DEFINE_MACHINE("integratorcp", integratorcp_machine_init)

static const Property core_properties[] = {
    DEFINE_PROP_UINT32("memsz", IntegratorCMState, memsz, 0),
};

static void core_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, core_properties);
    dc->realize = integratorcm_realize;
    dc->vmsd = &vmstate_integratorcm;
}

static void icp_pic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_icp_pic;
}

static void icp_control_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_icp_control;
}

static const TypeInfo core_info = {
    .name          = TYPE_INTEGRATOR_CM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IntegratorCMState),
    .instance_init = integratorcm_init,
    .class_init    = core_class_init,
};

static const TypeInfo icp_pic_info = {
    .name          = TYPE_INTEGRATOR_PIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(icp_pic_state),
    .instance_init = icp_pic_init,
    .class_init    = icp_pic_class_init,
};

static const TypeInfo icp_ctrl_regs_info = {
    .name          = TYPE_ICP_CONTROL_REGS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ICPCtrlRegsState),
    .instance_init = icp_control_init,
    .class_init    = icp_control_class_init,
};

static void integratorcp_register_types(void)
{
    type_register_static(&icp_pic_info);
    type_register_static(&core_info);
    type_register_static(&icp_ctrl_regs_info);
}

type_init(integratorcp_register_types)
