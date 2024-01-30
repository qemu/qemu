/*
 * Debug information support.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/lockable.h"
#include "tcg/debuginfo.h"

#include <elfutils/libdwfl.h>

static QemuMutex lock;
static Dwfl *dwfl;
static const Dwfl_Callbacks dwfl_callbacks = {
    .find_elf = NULL,
    .find_debuginfo = dwfl_standard_find_debuginfo,
    .section_address = NULL,
    .debuginfo_path = NULL,
};

__attribute__((constructor))
static void debuginfo_init(void)
{
    qemu_mutex_init(&lock);
}

void debuginfo_report_elf(const char *name, int fd, uint64_t bias)
{
    QEMU_LOCK_GUARD(&lock);

    if (dwfl) {
        dwfl_report_begin_add(dwfl);
    } else {
        dwfl = dwfl_begin(&dwfl_callbacks);
    }

    if (dwfl) {
        dwfl_report_elf(dwfl, name, name, fd, bias, true);
        dwfl_report_end(dwfl, NULL, NULL);
    }
}

void debuginfo_lock(void)
{
    qemu_mutex_lock(&lock);
}

void debuginfo_query(struct debuginfo_query *q, size_t n)
{
    const char *symbol, *file;
    Dwfl_Module *dwfl_module;
    Dwfl_Line *dwfl_line;
    GElf_Off dwfl_offset;
    GElf_Sym dwfl_sym;
    size_t i;
    int line;

    if (!dwfl) {
        return;
    }

    for (i = 0; i < n; i++) {
        dwfl_module = dwfl_addrmodule(dwfl, q[i].address);
        if (!dwfl_module) {
            continue;
        }

        if (q[i].flags & DEBUGINFO_SYMBOL) {
            symbol = dwfl_module_addrinfo(dwfl_module, q[i].address,
                                          &dwfl_offset, &dwfl_sym,
                                          NULL, NULL, NULL);
            if (symbol) {
                q[i].symbol = symbol;
                q[i].offset = dwfl_offset;
            }
        }

        if (q[i].flags & DEBUGINFO_LINE) {
            dwfl_line = dwfl_module_getsrc(dwfl_module, q[i].address);
            if (dwfl_line) {
                file = dwfl_lineinfo(dwfl_line, NULL, &line, 0, NULL, NULL);
                if (file) {
                    q[i].file = file;
                    q[i].line = line;
                }
            }
        }
    }
}

void debuginfo_unlock(void)
{
    qemu_mutex_unlock(&lock);
}
