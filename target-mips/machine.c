#include "hw/hw.h"
#include "hw/boards.h"

void register_machines(void)
{
    qemu_register_machine(&mips_malta_machine);
    qemu_register_machine(&mips_magnum_machine);
    qemu_register_machine(&mips_pica61_machine);
    qemu_register_machine(&mips_mipssim_machine);
    qemu_register_machine(&mips_machine);
}

void cpu_save(QEMUFile *f, void *opaque)
{
}

int cpu_load(QEMUFile *f, void *opaque, int version_id)
{
    return 0;
}
