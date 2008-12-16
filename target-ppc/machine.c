#include "hw/hw.h"
#include "hw/boards.h"

void register_machines(void)
{
    qemu_register_machine(&heathrow_machine);
    qemu_register_machine(&core99_machine);
    qemu_register_machine(&prep_machine);
    qemu_register_machine(&ref405ep_machine);
    qemu_register_machine(&taihu_machine);
    qemu_register_machine(&bamboo_machine);
}

void cpu_save(QEMUFile *f, void *opaque)
{
}

int cpu_load(QEMUFile *f, void *opaque, int version_id)
{
    return 0;
}
