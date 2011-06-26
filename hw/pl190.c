/*
 * Arm PrimeCell PL190 Vector Interrupt Controller
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "sysbus.h"

/* The number of virtual priority levels.  16 user vectors plus the
   unvectored IRQ.  Chained interrupts would require an additional level
   if implemented.  */

#define PL190_NUM_PRIO 17

typedef struct {
    SysBusDevice busdev;
    uint32_t level;
    uint32_t soft_level;
    uint32_t irq_enable;
    uint32_t fiq_select;
    uint8_t vect_control[16];
    uint32_t vect_addr[PL190_NUM_PRIO];
    /* Mask containing interrupts with higher priority than this one.  */
    uint32_t prio_mask[PL190_NUM_PRIO + 1];
    int protected;
    /* Current priority level.  */
    int priority;
    int prev_prio[PL190_NUM_PRIO];
    qemu_irq irq;
    qemu_irq fiq;
} pl190_state;

static const unsigned char pl190_id[] =
{ 0x90, 0x11, 0x04, 0x00, 0x0D, 0xf0, 0x05, 0xb1 };

static inline uint32_t pl190_irq_level(pl190_state *s)
{
    return (s->level | s->soft_level) & s->irq_enable & ~s->fiq_select;
}

/* Update interrupts.  */
static void pl190_update(pl190_state *s)
{
    uint32_t level = pl190_irq_level(s);
    int set;

    set = (level & s->prio_mask[s->priority]) != 0;
    qemu_set_irq(s->irq, set);
    set = ((s->level | s->soft_level) & s->fiq_select) != 0;
    qemu_set_irq(s->fiq, set);
}

static void pl190_set_irq(void *opaque, int irq, int level)
{
    pl190_state *s = (pl190_state *)opaque;

    if (level)
        s->level |= 1u << irq;
    else
        s->level &= ~(1u << irq);
    pl190_update(s);
}

static void pl190_update_vectors(pl190_state *s)
{
    uint32_t mask;
    int i;
    int n;

    mask = 0;
    for (i = 0; i < 16; i++)
      {
        s->prio_mask[i] = mask;
        if (s->vect_control[i] & 0x20)
          {
            n = s->vect_control[i] & 0x1f;
            mask |= 1 << n;
          }
      }
    s->prio_mask[16] = mask;
    pl190_update(s);
}

static uint32_t pl190_read(void *opaque, target_phys_addr_t offset)
{
    pl190_state *s = (pl190_state *)opaque;
    int i;

    if (offset >= 0xfe0 && offset < 0x1000) {
        return pl190_id[(offset - 0xfe0) >> 2];
    }
    if (offset >= 0x100 && offset < 0x140) {
        return s->vect_addr[(offset - 0x100) >> 2];
    }
    if (offset >= 0x200 && offset < 0x240) {
        return s->vect_control[(offset - 0x200) >> 2];
    }
    switch (offset >> 2) {
    case 0: /* IRQSTATUS */
        return pl190_irq_level(s);
    case 1: /* FIQSATUS */
        return (s->level | s->soft_level) & s->fiq_select;
    case 2: /* RAWINTR */
        return s->level | s->soft_level;
    case 3: /* INTSELECT */
        return s->fiq_select;
    case 4: /* INTENABLE */
        return s->irq_enable;
    case 6: /* SOFTINT */
        return s->soft_level;
    case 8: /* PROTECTION */
        return s->protected;
    case 12: /* VECTADDR */
        /* Read vector address at the start of an ISR.  Increases the
           current priority level to that of the current interrupt.  */
        for (i = 0; i < s->priority; i++)
          {
            if ((s->level | s->soft_level) & s->prio_mask[i])
              break;
          }
        /* Reading this value with no pending interrupts is undefined.
           We return the default address.  */
        if (i == PL190_NUM_PRIO)
          return s->vect_addr[16];
        if (i < s->priority)
          {
            s->prev_prio[i] = s->priority;
            s->priority = i;
            pl190_update(s);
          }
        return s->vect_addr[s->priority];
    case 13: /* DEFVECTADDR */
        return s->vect_addr[16];
    default:
        hw_error("pl190_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void pl190_write(void *opaque, target_phys_addr_t offset, uint32_t val)
{
    pl190_state *s = (pl190_state *)opaque;

    if (offset >= 0x100 && offset < 0x140) {
        s->vect_addr[(offset - 0x100) >> 2] = val;
        pl190_update_vectors(s);
        return;
    }
    if (offset >= 0x200 && offset < 0x240) {
        s->vect_control[(offset - 0x200) >> 2] = val;
        pl190_update_vectors(s);
        return;
    }
    switch (offset >> 2) {
    case 0: /* SELECT */
        /* This is a readonly register, but linux tries to write to it
           anyway.  Ignore the write.  */
        break;
    case 3: /* INTSELECT */
        s->fiq_select = val;
        break;
    case 4: /* INTENABLE */
        s->irq_enable |= val;
        break;
    case 5: /* INTENCLEAR */
        s->irq_enable &= ~val;
        break;
    case 6: /* SOFTINT */
        s->soft_level |= val;
        break;
    case 7: /* SOFTINTCLEAR */
        s->soft_level &= ~val;
        break;
    case 8: /* PROTECTION */
        /* TODO: Protection (supervisor only access) is not implemented.  */
        s->protected = val & 1;
        break;
    case 12: /* VECTADDR */
        /* Restore the previous priority level.  The value written is
           ignored.  */
        if (s->priority < PL190_NUM_PRIO)
            s->priority = s->prev_prio[s->priority];
        break;
    case 13: /* DEFVECTADDR */
        s->vect_addr[16] = val;
        break;
    case 0xc0: /* ITCR */
        if (val) {
            hw_error("pl190: Test mode not implemented\n");
        }
        break;
    default:
        hw_error("pl190_write: Bad offset %x\n", (int)offset);
        return;
    }
    pl190_update(s);
}

static CPUReadMemoryFunc * const pl190_readfn[] = {
   pl190_read,
   pl190_read,
   pl190_read
};

static CPUWriteMemoryFunc * const pl190_writefn[] = {
   pl190_write,
   pl190_write,
   pl190_write
};

static void pl190_reset(DeviceState *d)
{
  pl190_state *s = DO_UPCAST(pl190_state, busdev.qdev, d);
  int i;

  for (i = 0; i < 16; i++)
    {
      s->vect_addr[i] = 0;
      s->vect_control[i] = 0;
    }
  s->vect_addr[16] = 0;
  s->prio_mask[17] = 0xffffffff;
  s->priority = PL190_NUM_PRIO;
  pl190_update_vectors(s);
}

static int pl190_init(SysBusDevice *dev)
{
    pl190_state *s = FROM_SYSBUS(pl190_state, dev);
    int iomemtype;

    iomemtype = cpu_register_io_memory(pl190_readfn,
                                       pl190_writefn, s,
                                       DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, 0x1000, iomemtype);
    qdev_init_gpio_in(&dev->qdev, pl190_set_irq, 32);
    sysbus_init_irq(dev, &s->irq);
    sysbus_init_irq(dev, &s->fiq);
    return 0;
}

static const VMStateDescription vmstate_pl190 = {
    .name = "pl190",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(level, pl190_state),
        VMSTATE_UINT32(soft_level, pl190_state),
        VMSTATE_UINT32(irq_enable, pl190_state),
        VMSTATE_UINT32(fiq_select, pl190_state),
        VMSTATE_UINT8_ARRAY(vect_control, pl190_state, 16),
        VMSTATE_UINT32_ARRAY(vect_addr, pl190_state, PL190_NUM_PRIO),
        VMSTATE_UINT32_ARRAY(prio_mask, pl190_state, PL190_NUM_PRIO+1),
        VMSTATE_INT32(protected, pl190_state),
        VMSTATE_INT32(priority, pl190_state),
        VMSTATE_INT32_ARRAY(prev_prio, pl190_state, PL190_NUM_PRIO),
        VMSTATE_END_OF_LIST()
    }
};

static SysBusDeviceInfo pl190_info = {
    .init = pl190_init,
    .qdev.name = "pl190",
    .qdev.size = sizeof(pl190_state),
    .qdev.vmsd = &vmstate_pl190,
    .qdev.reset = pl190_reset,
    .qdev.no_user = 1,
};

static void pl190_register_devices(void)
{
    sysbus_register_withprop(&pl190_info);
}

device_init(pl190_register_devices)
