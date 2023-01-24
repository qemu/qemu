/*
 * QEMU monitor, target-dependent part
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "monitor-internal.h"
#include "monitor/qdev.h"
#include "net/slirp.h"
#include "sysemu/device_tree.h"
#include "monitor/hmp-target.h"
#include "monitor/hmp.h"
#include "block/block-hmp-cmds.h"
#include "qapi/qapi-commands-control.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/error.h"
#include "qemu/cutils.h"

#if defined(TARGET_S390X)
#include "hw/s390x/storage-keys.h"
#include "hw/s390x/storage-attributes.h"
#endif

/* Make devices configuration available for use in hmp-commands*.hx templates */
#include CONFIG_DEVICES

static HMPCommand hmp_info_cmds[];

/**
 * Is @name in the '|' separated list of names @list?
 */
int hmp_compare_cmd(const char *name, const char *list)
{
    const char *p, *pstart;
    int len;
    len = strlen(name);
    p = list;
    for (;;) {
        pstart = p;
        p = qemu_strchrnul(p, '|');
        if ((p - pstart) == len && !memcmp(pstart, name, len)) {
            return 1;
        }
        if (*p == '\0') {
            break;
        }
        p++;
    }
    return 0;
}

/* Please update hmp-commands.hx when adding or changing commands */
static HMPCommand hmp_info_cmds[] = {
#include "hmp-commands-info.h"
    { NULL, NULL, },
};

/* hmp_cmds and hmp_info_cmds would be sorted at runtime */
HMPCommand hmp_cmds[] = {
#include "hmp-commands.h"
    { NULL, NULL, },
};

/*
 * Set @pval to the value in the register identified by @name.
 * return 0 if OK, -1 if not found
 */
int get_monitor_def(Monitor *mon, int64_t *pval, const char *name)
{
    const MonitorDef *md = target_monitor_defs();
    CPUState *cs = mon_get_cpu(mon);
    void *ptr;
    uint64_t tmp = 0;
    int ret;

    if (cs == NULL || md == NULL) {
        return -1;
    }

    for(; md->name != NULL; md++) {
        if (hmp_compare_cmd(name, md->name)) {
            if (md->get_value) {
                *pval = md->get_value(mon, md, md->offset);
            } else {
                CPUArchState *env = mon_get_cpu_env(mon);
                ptr = (uint8_t *)env + md->offset;
                switch(md->type) {
                case MD_I32:
                    *pval = *(int32_t *)ptr;
                    break;
                case MD_TLONG:
                    *pval = *(target_long *)ptr;
                    break;
                default:
                    *pval = 0;
                    break;
                }
            }
            return 0;
        }
    }

    ret = target_get_monitor_def(cs, name, &tmp);
    if (!ret) {
        *pval = (target_long) tmp;
    }

    return ret;
}

static int
compare_mon_cmd(const void *a, const void *b)
{
    return strcmp(((const HMPCommand *)a)->name,
            ((const HMPCommand *)b)->name);
}

static void __attribute__((__constructor__)) sortcmdlist(void)
{
    qsort(hmp_cmds, ARRAY_SIZE(hmp_cmds) - 1,
          sizeof(*hmp_cmds),
          compare_mon_cmd);
    qsort(hmp_info_cmds, ARRAY_SIZE(hmp_info_cmds) - 1,
          sizeof(*hmp_info_cmds),
          compare_mon_cmd);
}

void monitor_register_hmp(const char *name, bool info,
                          void (*cmd)(Monitor *mon, const QDict *qdict))
{
    HMPCommand *table = info ? hmp_info_cmds : hmp_cmds;

    while (table->name != NULL) {
        if (strcmp(table->name, name) == 0) {
            g_assert(table->cmd == NULL && table->cmd_info_hrt == NULL);
            table->cmd = cmd;
            return;
        }
        table++;
    }
    g_assert_not_reached();
}

void monitor_register_hmp_info_hrt(const char *name,
                                   HumanReadableText *(*handler)(Error **errp))
{
    HMPCommand *table = hmp_info_cmds;

    while (table->name != NULL) {
        if (strcmp(table->name, name) == 0) {
            g_assert(table->cmd == NULL && table->cmd_info_hrt == NULL);
            table->cmd_info_hrt = handler;
            return;
        }
        table++;
    }
    g_assert_not_reached();
}
