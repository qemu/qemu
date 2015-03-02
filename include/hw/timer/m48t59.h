#ifndef NVRAM_H
#define NVRAM_H

#include "qemu-common.h"
#include "qom/object.h"

#define TYPE_NVRAM "nvram"

#define NVRAM_CLASS(klass) \
    OBJECT_CLASS_CHECK(NvramClass, (klass), TYPE_NVRAM)
#define NVRAM_GET_CLASS(obj) \
    OBJECT_GET_CLASS(NvramClass, (obj), TYPE_NVRAM)
#define NVRAM(obj) \
    INTERFACE_CHECK(Nvram, (obj), TYPE_NVRAM)

typedef struct Nvram {
    Object parent;
} Nvram;

typedef struct NvramClass {
    InterfaceClass parent;

    uint32_t (*read)(Nvram *obj, uint32_t addr);
    void (*write)(Nvram *obj, uint32_t addr, uint32_t val);
    void (*toggle_lock)(Nvram *obj, int lock);
} NvramClass;

Nvram *m48t59_init_isa(ISABus *bus, uint32_t io_base, uint16_t size,
                       int base_year, int type);
Nvram *m48t59_init(qemu_irq IRQ, hwaddr mem_base,
                   uint32_t io_base, uint16_t size, int base_year,
                   int type);

#endif /* !NVRAM_H */
