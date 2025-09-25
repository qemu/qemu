#include "qemu/osdep.h"
#include "cpu.h"
#include "migration/cpu.h"

static int get_fpcr(QEMUFile *f, void *opaque, size_t size,
                    const VMStateField *field)
{
    CPUAlphaState *env = opaque;
    cpu_alpha_store_fpcr(env, qemu_get_be64(f));
    return 0;
}

static int put_fpcr(QEMUFile *f, void *opaque, size_t size,
                    const VMStateField *field, JSONWriter *vmdesc)
{
    CPUAlphaState *env = opaque;
    qemu_put_be64(f, cpu_alpha_load_fpcr(env));
    return 0;
}

static const VMStateInfo vmstate_fpcr = {
    .name = "fpcr",
    .get = get_fpcr,
    .put = put_fpcr,
};

static const VMStateField vmstate_env_fields[] = {
    VMSTATE_UINT64_ARRAY(ir, CPUAlphaState, 31),
    VMSTATE_UINT64_ARRAY(fir, CPUAlphaState, 31),
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
    VMSTATE_UINT64(pc, CPUAlphaState),
    VMSTATE_UINT64(unique, CPUAlphaState),
    VMSTATE_UINT64(lock_addr, CPUAlphaState),
    VMSTATE_UINT64(lock_value, CPUAlphaState),

    VMSTATE_UINT32(flags, CPUAlphaState),
    VMSTATE_UINT32(pcc_ofs, CPUAlphaState),

    VMSTATE_UINT64(trap_arg0, CPUAlphaState),
    VMSTATE_UINT64(trap_arg1, CPUAlphaState),
    VMSTATE_UINT64(trap_arg2, CPUAlphaState),

    VMSTATE_UINT64(exc_addr, CPUAlphaState),
    VMSTATE_UINT64(palbr, CPUAlphaState),
    VMSTATE_UINT64(ptbr, CPUAlphaState),
    VMSTATE_UINT64(vptptr, CPUAlphaState),
    VMSTATE_UINT64(sysval, CPUAlphaState),
    VMSTATE_UINT64(usp, CPUAlphaState),

    VMSTATE_UINT64_ARRAY(shadow, CPUAlphaState, 8),
    VMSTATE_UINT64_ARRAY(scratch, CPUAlphaState, 24),

    VMSTATE_END_OF_LIST()
};

static const VMStateDescription vmstate_env = {
    .name = "env",
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = vmstate_env_fields,
};

static const VMStateField vmstate_cpu_fields[] = {
    VMSTATE_STRUCT(parent_obj, AlphaCPU, 0, vmstate_cpu_common, CPUState),
    VMSTATE_STRUCT(env, AlphaCPU, 1, vmstate_env, CPUAlphaState),
    VMSTATE_END_OF_LIST()
};

const VMStateDescription vmstate_alpha_cpu = {
    .name = "cpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = vmstate_cpu_fields,
};
