#include "qemu/osdep.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "hw/i386/pc.h"
#include "hw/i386/sgx-epc.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc-target.h"

void sgx_epc_build_srat(GArray *table_data)
{
}

SGXInfo *qmp_query_sgx(Error **errp)
{
    error_setg(errp, "SGX support is not compiled in");
    return NULL;
}

SGXInfo *qmp_query_sgx_capabilities(Error **errp)
{
    error_setg(errp, "SGX support is not compiled in");
    return NULL;
}

void hmp_info_sgx(Monitor *mon, const QDict *qdict)
{
    monitor_printf(mon, "SGX is not available in this QEMU\n");
}

void pc_machine_init_sgx_epc(PCMachineState *pcms)
{
    memset(&pcms->sgx_epc, 0, sizeof(SGXEPCState));
}

bool check_sgx_support(void)
{
    return false;
}

bool sgx_epc_get_section(int section_nr, uint64_t *addr, uint64_t *size)
{
    return true;
}
