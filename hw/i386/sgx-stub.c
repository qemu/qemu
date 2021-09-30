#include "qemu/osdep.h"
#include "hw/i386/pc.h"
#include "hw/i386/sgx-epc.h"
#include "hw/i386/sgx.h"

SGXInfo *sgx_get_info(Error **errp)
{
    error_setg(errp, "SGX support is not compiled in");
    return NULL;
}

SGXInfo *sgx_get_capabilities(Error **errp)
{
    error_setg(errp, "SGX support is not compiled in");
    return NULL;
}

void pc_machine_init_sgx_epc(PCMachineState *pcms)
{
    memset(&pcms->sgx_epc, 0, sizeof(SGXEPCState));
}

int sgx_epc_get_section(int section_nr, uint64_t *addr, uint64_t *size)
{
    g_assert_not_reached();
}
