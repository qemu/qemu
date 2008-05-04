#include "hw/hw.h"
#include "hw/boards.h"

void register_machines(void)
{
#if !defined(TARGET_MIPS64)
    qemu_register_ar7_machines();
#endif
    qemu_register_mips_machines();
    //~ qemu_register_machine(&mips_machine);
    qemu_register_machine(&mips_magnum_machine);
    qemu_register_machine(&mips_malta_machine);
    qemu_register_machine(&mips_pica61_machine);
    qemu_register_machine(&mips_mipssim_machine);
}

void cpu_save(QEMUFile *f, void *opaque)
{
}

int cpu_load(QEMUFile *f, void *opaque, int version_id)
{
    return 0;
}


