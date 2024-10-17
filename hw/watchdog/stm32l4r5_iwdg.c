

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/watchdog/stm32l4r5_iwdg.h"
#include "sysemu/watchdog.h"
#include "hw/qdev-properties.h"

#define PCLK_HZ 32000

enum {
    IWDG_KR = 0,
    IWDG_PR = 1,
    IWDG_RLR = 2,
    IWDG_SR = 3,
    IWDG_WINR = 4,
};

#define REG_TO_OFFSET(reg) ((reg) * 4)
#define OFFSET_TO_REG(offset) ((offset) / 4)

#define IWDG_REGMAP_SIZE 0x14


static void stm32_iwdg_expired(void *opaque)
{
    qemu_log_mask(CPU_LOG_RESET, "Watchdog timer expired. Performing action...\n");

    watchdog_perform_action();
}

static uint64_t stm32_iwdg_read(void *opaque, hwaddr offset, unsigned size) 
{
    Stm32l4r5IwdgState *s = opaque;
    uint64_t ret = 0;

    if (offset >= IWDG_REGMAP_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    switch (OFFSET_TO_REG(offset)) {
    case IWDG_KR:
        qemu_log_mask(LOG_UNIMP, "%s: read from write-only reg at offset 0x%"
                      HWADDR_PRIx "\n", __func__, offset);
    case IWDG_PR:
        // Check if the PVU bit in the IWDG_SR register is set. Otherwise warn that the value could be outdated.
        if (s->regs[IWDG_SR] & 0x1) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: PR register value could be outdated. Make sure that the PVU value in SR is reset before reading!\n", __func__);
        }
        ret = s->regs[IWDG_PR];
        break;
    case IWDG_RLR:
        // Check if the PVU bit in the IWDG_SR register is set. Otherwise warn that the value could be outdated.
        if (s->regs[IWDG_SR] & 0x1) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: PR register value could be outdated. Make sure that the PVU value in SR is reset before reading!\n", __func__);
        }
        ret = s->regs[IWDG_RLR];
    case IWDG_SR:
        ret = s->regs[IWDG_SR];
        break;
    case IWDG_WINR:
        // Check if the PVU bit in the IWDG_SR register is set. Otherwise warn that the value could be outdated.
        if (s->regs[IWDG_SR] & 0x1) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: PR register value could be outdated. Make sure that the PVU value in SR is reset before reading!\n", __func__);
        }
        ret = s->regs[IWDG_WINR];
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented register 0x%04x\n",
                      __func__, (uint32_t)offset);
    }

    return ret; 
}

static void stm32_iwdg_reload(Stm32l4r5IwdgState *s)
{
    int shift = s->regs[IWDG_PR] + 2;
    uint64_t reload = muldiv64(s->regs[IWDG_RLR], NANOSECONDS_PER_SECOND, (PCLK_HZ >> shift));

    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + reload);
}

static void stm32_iwdg_write(void *opaque, hwaddr offset, uint64_t data, unsigned size)
{
    Stm32l4r5IwdgState *s = opaque;

    if (offset >= IWDG_REGMAP_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return;
    }

    switch (OFFSET_TO_REG(offset)) {
        case IWDG_KR:
            if (data == 0xAAAA) {
                // Reload the counter
                stm32_iwdg_reload(s);
                // Sequence is broken, register access is protected again.
                s->register_locked = true;
            } else if (data == 0x5555) {
                // Unlock the register access
                s->register_locked = false; 
            } else if (data == 0xCCCC) {
                // Start the watchdog
                stm32_iwdg_reload(s);
                // Sequence is broken, register access is protected again.
                s->register_locked = true;
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid value written to KR register. Expected 0xAAAA or 0xCCCC, got 0x%04x\n", __func__, (uint32_t)data);
            }            
            break;
        case IWDG_PR:
            if (s->register_locked) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Write access to PR register is locked. Unlock it first by writing 0x5555 to KR register.\n", __func__);
                return;
            }

            // Check if the PVU bit in the IWDG_SR register is reset.
            if (s->regs[IWDG_SR] & 0x1) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Cannot change the prescalar divider. PVU bit in SR is still set.\n", __func__);
                return;
            }

            // Only the lower 3 bits are allowed to be written.
            if (data & 0xFFFFFFF8) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid value written to PR register. Only the lower 3 bits are allowed to be written.\n", __func__);
                return;
            }
             
            s->regs[IWDG_PR] = data;
            break;
        case IWDG_RLR:
            if (s->register_locked) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Write access to RLR register is locked. Unlock it first by writing 0x5555 to KR register.\n", __func__);
                return;
            }

            // Check if the RVU bit in the IWDG_SR register is reset.
            if (s->regs[IWDG_SR] >> 1 & 0x1) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Cannot change the reload value. RVU bit in SR is still set.\n", __func__);
                return;
            }

            // Only the lower 12 bits are allowed to be written.
            if (data & 0xFFFFF000) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid value written to RLR register. Only the lower 12 bits are allowed to be written.\n", __func__);
                return;
            }

            s->regs[IWDG_RLR] = data;
            break;
        case IWDG_SR:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: SR register is read-only.\n", __func__);
            break;
        case IWDG_WINR:
            if (s->register_locked) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Write access to WINR register is locked. Unlock it first by writing 0x5555 to KR register.\n", __func__);
                return;
            }

            // Check if the WVU bit in the IWDG_SR register is reset.
            if (s->regs[IWDG_SR] >> 2 & 0x1) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Cannot change the window value. WVU bit in SR is still set.\n", __func__);
                return;
            }

            // Only the lower 12 bits are allowed to be written.
            if (data & 0xFFFFF000) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid value written to WINR register. Only the lower 12 bits are allowed to be written.\n", __func__);
                return;
            }

            s->regs[IWDG_WINR] = data;
            break;
    }
}

static const MemoryRegionOps stm32_iwdg_ops = {
    .read = stm32_iwdg_read,
    .write = stm32_iwdg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static void stm32_iwdg_reset(STM32IWDGState *s)
{
    s->regs[IWDG_SR] = 0x00000000;
    s->regs[IWDG_RLR] = 0x00000FFF;
    s->regs[IWDG_PR] = 0x00000000;
    s->regs[IWDG_WINR] = 0x00000FFF;

    /* Reset the timer */
    if (s->timer) {
        timer_del(s->timer);
    }

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, stm32_iwdg_expired, s);
}

static void stm32_iwdg_reset_enter(Object *obj, ResetType type)
{
    Stm32l4r5IwdgState *s = STM32L4R5_IWDG(obj);
    stm32_iwdg_reset(s);
}



static void stm32_iwdg_realize(DeviceState *dev, Error **errp)
{
    Stm32l4r5IwdgState *s = STM32L4R5_IWDG(dev);

    // Write access to the registers is locked by default.
    s->register_locked = true;
    s->timer = NULL;

    memory_region_init_io(&s->iomem, OBJECT(s), &stm32_iwdg_ops, s, TYPE_STM32_IWDG, STM32_IWDG_REGS_NUM * 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);

    stm32_iwdg_reset(s);
}

static void stm32_iwdg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dklass = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    
    rc->phases.enter = stm32_iwdg_reset_enter;
    dklass->desc = "STM32 Independent Watchdog Controller";
    dklass->realize = stm32_iwdg_realize;
}

static TypeInfo stm32_iwdg_info = {
    .name          = TYPE_STM32_IWDG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Stm32l4r5IwdgState),
    .class_init    = stm32_iwdg_class_init,
    .class_size    = sizeof(Stm32l4r5IwdgClass),
};

static void stm32_iwdg_register(void)
{
    type_register_static(&stm32_iwdg_info);
}

type_init(stm32_iwdg_register)

