/*
 * HMP commands related to TPM
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qapi/qapi-commands-tpm.h"
#include "monitor/monitor.h"
#include "monitor/hmp.h"
#include "qapi/error.h"

void hmp_info_tpm(MonitorHMP *hmp, const QDict *qdict)
{
#ifdef CONFIG_TPM
    TPMInfoList *info_list, *info;
    Error *err = NULL;
    unsigned int c = 0;
    TPMPassthroughOptions *tpo;
    TPMEmulatorOptions *teo;

    info_list = qmp_query_tpm(&err);
    if (err) {
        monitor_hmp_printf(hmp, "TPM device not supported\n");
        error_free(err);
        return;
    }

    if (info_list) {
        monitor_hmp_printf(hmp, "TPM device:\n");
    }

    for (info = info_list; info; info = info->next) {
        TPMInfo *ti = info->value;
        monitor_hmp_printf(hmp, " tpm%d: model=%s\n",
                           c, TpmModel_str(ti->model));

        monitor_hmp_printf(hmp, "  \\ %s: type=%s",
                           ti->id, TpmType_str(ti->options->type));

        switch (ti->options->type) {
        case TPM_TYPE_PASSTHROUGH:
            tpo = ti->options->u.passthrough.data;
            monitor_hmp_printf(hmp, "%s%s%s%s",
                               tpo->path ? ",path=" : "",
                               tpo->path ?: "",
                               tpo->cancel_path ? ",cancel-path=" : "",
                               tpo->cancel_path ?: "");
            break;
        case TPM_TYPE_EMULATOR:
            teo = ti->options->u.emulator.data;
            monitor_hmp_printf(hmp, ",chardev=%s", teo->chardev);
            break;
        case TPM_TYPE__MAX:
            break;
        }
        monitor_hmp_printf(hmp, "\n");
        c++;
    }
    qapi_free_TPMInfoList(info_list);
#else
    monitor_hmp_printf(hmp, "TPM device not supported\n");
#endif /* CONFIG_TPM */
}
