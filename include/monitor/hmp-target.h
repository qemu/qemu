/*
 * QEMU monitor
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

#ifndef MONITOR_HMP_TARGET_H
#define MONITOR_HMP_TARGET_H

#define MD_TLONG 0
#define MD_I32   1

struct MonitorDef {
    const char *name;
    int offset;
    target_long (*get_value)(const struct MonitorDef *md, int val);
    int type;
};

const MonitorDef *target_monitor_defs(void);
int target_get_monitor_def(CPUState *cs, const char *name, uint64_t *pval);

CPUArchState *mon_get_cpu_env(void);
CPUState *mon_get_cpu(void);

void hmp_info_mem(Monitor *mon, const QDict *qdict);
void hmp_info_tlb(Monitor *mon, const QDict *qdict);
void hmp_mce(Monitor *mon, const QDict *qdict);
void hmp_info_local_apic(Monitor *mon, const QDict *qdict);
void hmp_info_io_apic(Monitor *mon, const QDict *qdict);

#endif /* MONITOR_HMP_TARGET_H */
