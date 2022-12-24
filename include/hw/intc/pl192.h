#ifndef PL192_H
#define PL192_H

#include "qemu/osdep.h"

#define TYPE_PL192 "pl192"
OBJECT_DECLARE_SIMPLE_TYPE(PL192State, PL192)


#define PL192_INT_SOURCES   32
#define PL192_DAISY_IRQ     PL192_INT_SOURCES
#define PL192_NO_IRQ        PL192_INT_SOURCES+1
#define PL192_PRIO_LEVELS   16

#define PL192_IRQSTATUS         0x00
#define PL192_FIQSTATUS         0x04
#define PL192_RAWINTR           0x08
#define PL192_INTSELECT         0x0C
#define PL192_INTENABLE         0x10
#define PL192_INTENCLEAR        0x14
#define PL192_SOFTINT           0x18
#define PL192_SOFTINTCLEAR      0x1C
#define PL192_PROTECTION        0x20
#define PL192_SWPRIORITYMASK    0x24
#define PL192_PRIORITYDAISY     0x28
#define PL192_VECTADDR          0xF00
#define PL192_IOMEM_SIZE        0x1000

#define PL190_ITCR              0x300
#define PL190_VECTADDR          0x30
#define PL190_DEFVECTADDR       0x34

#define PL192_IOMEM_SIZE    0x1000


struct PL192State {
    SysBusDevice busdev;
    MemoryRegion iomem;

    /* Control registers */
    uint32_t irq_status;
    uint32_t fiq_status;
    uint32_t rawintr;
    uint32_t intselect;
    uint32_t intenable;
    uint32_t softint;
    uint32_t protection;
    uint32_t sw_priority_mask;
    uint32_t vect_addr[PL192_INT_SOURCES];
    uint32_t vect_priority[PL192_INT_SOURCES];
    uint32_t address;

    /* Currently processed interrupt and
       highest priority interrupt */
    uint32_t current;
    uint32_t current_highest;

    /* Priority masking logic */
    int32_t stack_i;
    uint32_t priority_stack[PL192_PRIO_LEVELS+1];
    uint8_t irq_stack[PL192_PRIO_LEVELS+1];
    uint32_t priority;

    /* Daisy-chain interface */
    uint32_t daisy_vectaddr;
    uint32_t daisy_priority;
    PL192State *daisy_callback;
    uint8_t  daisy_input;

    /* Parent interrupts */
    qemu_irq irq;
    qemu_irq fiq;

    /* Next controller in chain */
    PL192State *daisy;
};

DeviceState *pl192_manual_init(char *mem_name, ...);

#endif