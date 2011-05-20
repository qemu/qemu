#include "hw/hw.h"
#include "hw/boards.h"

static int get_fpcr(QEMUFile *f, void *opaque, size_t size)
{
    CPUAlphaState *env = opaque;
    cpu_alpha_store_fpcr(env, qemu_get_be64(f));
    return 0;
}

static void put_fpcr(QEMUFile *f, void *opaque, size_t size)
{
    CPUAlphaState *env = opaque;
    qemu_put_be64(f, cpu_alpha_load_fpcr(env));
}

static const VMStateInfo vmstate_fpcr = {
    .name = "fpcr",
    .get = get_fpcr,
    .put = put_fpcr,
};

static VMStateField vmstate_cpu_fields[] = {
    VMSTATE_UINTTL_ARRAY(ir, CPUState, 31),
    VMSTATE_UINTTL_ARRAY(fir, CPUState, 31),
    /* Save the architecture value of the fpcr, not the internally
       expanded version.  Since this architecture value does not
       exist in memory to be stored, this requires a but of hoop
       jumping.  We want OFFSET=0 so that we effectively pass ENV
       to the helper functions, and we need to fill in the name by
       hand since there's no field of that name.  */
    {
        .name = "fpcr",
        .version_id = 0,
        .size = sizeof(uint64_t),
        .info = &vmstate_fpcr,
        .flags = VMS_SINGLE,
        .offset = 0
    },
    VMSTATE_UINTTL(pc, CPUState),
    VMSTATE_UINTTL(unique, CPUState),
    VMSTATE_UINTTL(lock_addr, CPUState),
    VMSTATE_UINTTL(lock_value, CPUState),
    /* Note that lock_st_addr is not saved; it is a temporary
       used during the execution of the st[lq]_c insns.  */

    VMSTATE_UINT8(ps, CPUState),
    VMSTATE_UINT8(intr_flag, CPUState),
    VMSTATE_UINT8(pal_mode, CPUState),

    VMSTATE_UINTTL(trap_arg0, CPUState),
    VMSTATE_UINTTL(trap_arg1, CPUState),
    VMSTATE_UINTTL(trap_arg2, CPUState),

    VMSTATE_END_OF_LIST()
};

static const VMStateDescription vmstate_cpu = {
    .name = "cpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = vmstate_cpu_fields,
};

void cpu_save(QEMUFile *f, void *opaque)
{
    vmstate_save_state(f, &vmstate_cpu, opaque);
}

int cpu_load(QEMUFile *f, void *opaque, int version_id)
{
    return vmstate_load_state(f, &vmstate_cpu, opaque, version_id);
}
