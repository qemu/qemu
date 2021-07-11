#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "exec/exec-all.h"
#include "monitor/monitor.h"
#include "sysemu/tcg.h"

static void hmp_info_jit(Monitor *mon, const QDict *qdict)
{
    if (!tcg_enabled()) {
        error_report("JIT information is only available with accel=tcg");
        return;
    }

    dump_exec_info();
    dump_drift_info();
}

static void hmp_info_opcount(Monitor *mon, const QDict *qdict)
{
    dump_opcount_info();
}

static void hmp_tcg_register(void)
{
    monitor_register_hmp("jit", true, hmp_info_jit);
    monitor_register_hmp("opcount", true, hmp_info_opcount);
}

type_init(hmp_tcg_register);
