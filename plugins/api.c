/*
 * QEMU Plugin API
 *
 * This provides the API that is available to the plugins to interact
 * with QEMU. We have to be careful not to expose internal details of
 * how QEMU works so we abstract out things like translation and
 * instructions to anonymous data types:
 *
 *  qemu_plugin_tb
 *  qemu_plugin_insn
 *  qemu_plugin_register
 *
 * Which can then be passed back into the API to do additional things.
 * As such all the public functions in here are exported in
 * qemu-plugin.h.
 *
 * The general life-cycle of a plugin is:
 *
 *  - plugin is loaded, public qemu_plugin_install called
 *    - the install func registers callbacks for events
 *    - usually an atexit_cb is registered to dump info at the end
 *  - when a registered event occurs the plugin is called
 *     - some events pass additional info
 *     - during translation the plugin can decide to instrument any
 *       instruction
 *  - when QEMU exits all the registered atexit callbacks are called
 *
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 * Copyright (C) 2019, Linaro
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/plugin.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "tcg/tcg.h"
#include "exec/exec-all.h"
#include "exec/gdbstub.h"
#include "exec/translator.h"
#include "disas/disas.h"
#include "plugin.h"
#ifndef CONFIG_USER_ONLY
#include "qapi/error.h"
#include "migration/blocker.h"
#include "exec/ram_addr.h"
#include "qemu/plugin-memory.h"
#include "hw/boards.h"
#else
#include "qemu.h"
#ifdef CONFIG_LINUX
#include "loader.h"
#endif
#endif

/* Uninstall and Reset handlers */

void qemu_plugin_uninstall(qemu_plugin_id_t id, qemu_plugin_simple_cb_t cb)
{
    plugin_reset_uninstall(id, cb, false);
}

void qemu_plugin_reset(qemu_plugin_id_t id, qemu_plugin_simple_cb_t cb)
{
    plugin_reset_uninstall(id, cb, true);
}

/*
 * Plugin Register Functions
 *
 * This allows the plugin to register callbacks for various events
 * during the translation.
 */

void qemu_plugin_register_vcpu_init_cb(qemu_plugin_id_t id,
                                       qemu_plugin_vcpu_simple_cb_t cb)
{
    plugin_register_cb(id, QEMU_PLUGIN_EV_VCPU_INIT, cb);
}

void qemu_plugin_register_vcpu_exit_cb(qemu_plugin_id_t id,
                                       qemu_plugin_vcpu_simple_cb_t cb)
{
    plugin_register_cb(id, QEMU_PLUGIN_EV_VCPU_EXIT, cb);
}

static bool tb_is_mem_only(void)
{
    return tb_cflags(tcg_ctx->gen_tb) & CF_MEMI_ONLY;
}

void qemu_plugin_register_vcpu_tb_exec_cb(struct qemu_plugin_tb *tb,
                                          qemu_plugin_vcpu_udata_cb_t cb,
                                          enum qemu_plugin_cb_flags flags,
                                          void *udata)
{
    if (!tb_is_mem_only()) {
        plugin_register_dyn_cb__udata(&tb->cbs, cb, flags, udata);
    }
}

void qemu_plugin_register_vcpu_tb_exec_cond_cb(struct qemu_plugin_tb *tb,
                                               qemu_plugin_vcpu_udata_cb_t cb,
                                               enum qemu_plugin_cb_flags flags,
                                               enum qemu_plugin_cond cond,
                                               qemu_plugin_u64 entry,
                                               uint64_t imm,
                                               void *udata)
{
    if (cond == QEMU_PLUGIN_COND_NEVER || tb_is_mem_only()) {
        return;
    }
    if (cond == QEMU_PLUGIN_COND_ALWAYS) {
        qemu_plugin_register_vcpu_tb_exec_cb(tb, cb, flags, udata);
        return;
    }
    plugin_register_dyn_cond_cb__udata(&tb->cbs, cb, flags,
                                       cond, entry, imm, udata);
}

void qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
    struct qemu_plugin_tb *tb,
    enum qemu_plugin_op op,
    qemu_plugin_u64 entry,
    uint64_t imm)
{
    if (!tb_is_mem_only()) {
        plugin_register_inline_op_on_entry(&tb->cbs, 0, op, entry, imm);
    }
}

void qemu_plugin_register_vcpu_insn_exec_cb(struct qemu_plugin_insn *insn,
                                            qemu_plugin_vcpu_udata_cb_t cb,
                                            enum qemu_plugin_cb_flags flags,
                                            void *udata)
{
    if (!tb_is_mem_only()) {
        plugin_register_dyn_cb__udata(&insn->insn_cbs, cb, flags, udata);
    }
}

void qemu_plugin_register_vcpu_insn_exec_cond_cb(
    struct qemu_plugin_insn *insn,
    qemu_plugin_vcpu_udata_cb_t cb,
    enum qemu_plugin_cb_flags flags,
    enum qemu_plugin_cond cond,
    qemu_plugin_u64 entry,
    uint64_t imm,
    void *udata)
{
    if (cond == QEMU_PLUGIN_COND_NEVER || tb_is_mem_only()) {
        return;
    }
    if (cond == QEMU_PLUGIN_COND_ALWAYS) {
        qemu_plugin_register_vcpu_insn_exec_cb(insn, cb, flags, udata);
        return;
    }
    plugin_register_dyn_cond_cb__udata(&insn->insn_cbs, cb, flags,
                                       cond, entry, imm, udata);
}

void qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
    struct qemu_plugin_insn *insn,
    enum qemu_plugin_op op,
    qemu_plugin_u64 entry,
    uint64_t imm)
{
    if (!tb_is_mem_only()) {
        plugin_register_inline_op_on_entry(&insn->insn_cbs, 0, op, entry, imm);
    }
}


/*
 * We always plant memory instrumentation because they don't finalise until
 * after the operation has complete.
 */
void qemu_plugin_register_vcpu_mem_cb(struct qemu_plugin_insn *insn,
                                      qemu_plugin_vcpu_mem_cb_t cb,
                                      enum qemu_plugin_cb_flags flags,
                                      enum qemu_plugin_mem_rw rw,
                                      void *udata)
{
    plugin_register_vcpu_mem_cb(&insn->mem_cbs, cb, flags, rw, udata);
}

void qemu_plugin_register_vcpu_mem_inline_per_vcpu(
    struct qemu_plugin_insn *insn,
    enum qemu_plugin_mem_rw rw,
    enum qemu_plugin_op op,
    qemu_plugin_u64 entry,
    uint64_t imm)
{
    plugin_register_inline_op_on_entry(&insn->mem_cbs, rw, op, entry, imm);
}

void qemu_plugin_register_vcpu_tb_trans_cb(qemu_plugin_id_t id,
                                           qemu_plugin_vcpu_tb_trans_cb_t cb)
{
    plugin_register_cb(id, QEMU_PLUGIN_EV_VCPU_TB_TRANS, cb);
}

void qemu_plugin_register_vcpu_syscall_cb(qemu_plugin_id_t id,
                                          qemu_plugin_vcpu_syscall_cb_t cb)
{
    plugin_register_cb(id, QEMU_PLUGIN_EV_VCPU_SYSCALL, cb);
}

void
qemu_plugin_register_vcpu_syscall_ret_cb(qemu_plugin_id_t id,
                                         qemu_plugin_vcpu_syscall_ret_cb_t cb)
{
    plugin_register_cb(id, QEMU_PLUGIN_EV_VCPU_SYSCALL_RET, cb);
}

/*
 * Plugin Queries
 *
 * These are queries that the plugin can make to gauge information
 * from our opaque data types. We do not want to leak internal details
 * here just information useful to the plugin.
 */

/*
 * Translation block information:
 *
 * A plugin can query the virtual address of the start of the block
 * and the number of instructions in it. It can also get access to
 * each translated instruction.
 */

size_t qemu_plugin_tb_n_insns(const struct qemu_plugin_tb *tb)
{
    return tb->n;
}

uint64_t qemu_plugin_tb_vaddr(const struct qemu_plugin_tb *tb)
{
    const DisasContextBase *db = tcg_ctx->plugin_db;
    return db->pc_first;
}

struct qemu_plugin_insn *
qemu_plugin_tb_get_insn(const struct qemu_plugin_tb *tb, size_t idx)
{
    struct qemu_plugin_insn *insn;
    if (unlikely(idx >= tb->n)) {
        return NULL;
    }
    insn = g_ptr_array_index(tb->insns, idx);
    return insn;
}

/*
 * Instruction information
 *
 * These queries allow the plugin to retrieve information about each
 * instruction being translated.
 */

size_t qemu_plugin_insn_data(const struct qemu_plugin_insn *insn,
                             void *dest, size_t len)
{
    const DisasContextBase *db = tcg_ctx->plugin_db;

    len = MIN(len, insn->len);
    return translator_st(db, dest, insn->vaddr, len) ? len : 0;
}

size_t qemu_plugin_insn_size(const struct qemu_plugin_insn *insn)
{
    return insn->len;
}

uint64_t qemu_plugin_insn_vaddr(const struct qemu_plugin_insn *insn)
{
    return insn->vaddr;
}

void *qemu_plugin_insn_haddr(const struct qemu_plugin_insn *insn)
{
    const DisasContextBase *db = tcg_ctx->plugin_db;
    vaddr page0_last = db->pc_first | ~TARGET_PAGE_MASK;

    if (db->fake_insn) {
        return NULL;
    }

    /*
     * ??? The return value is not intended for use of host memory,
     * but as a proxy for address space and physical address.
     * Thus we are only interested in the first byte and do not
     * care about spanning pages.
     */
    if (insn->vaddr <= page0_last) {
        if (db->host_addr[0] == NULL) {
            return NULL;
        }
        return db->host_addr[0] + insn->vaddr - db->pc_first;
    } else {
        if (db->host_addr[1] == NULL) {
            return NULL;
        }
        return db->host_addr[1] + insn->vaddr - (page0_last + 1);
    }
}

char *qemu_plugin_insn_disas(const struct qemu_plugin_insn *insn)
{
    return plugin_disas(tcg_ctx->cpu, tcg_ctx->plugin_db,
                        insn->vaddr, insn->len);
}

const char *qemu_plugin_insn_symbol(const struct qemu_plugin_insn *insn)
{
    const char *sym = lookup_symbol(insn->vaddr);
    return sym[0] != 0 ? sym : NULL;
}

/*
 * The memory queries allow the plugin to query information about a
 * memory access.
 */

unsigned qemu_plugin_mem_size_shift(qemu_plugin_meminfo_t info)
{
    MemOp op = get_memop(info);
    return op & MO_SIZE;
}

bool qemu_plugin_mem_is_sign_extended(qemu_plugin_meminfo_t info)
{
    MemOp op = get_memop(info);
    return op & MO_SIGN;
}

bool qemu_plugin_mem_is_big_endian(qemu_plugin_meminfo_t info)
{
    MemOp op = get_memop(info);
    return (op & MO_BSWAP) == MO_BE;
}

bool qemu_plugin_mem_is_store(qemu_plugin_meminfo_t info)
{
    return get_plugin_meminfo_rw(info) & QEMU_PLUGIN_MEM_W;
}

qemu_plugin_mem_value qemu_plugin_mem_get_value(qemu_plugin_meminfo_t info)
{
    uint64_t low = current_cpu->neg.plugin_mem_value_low;
    qemu_plugin_mem_value value;

    switch (qemu_plugin_mem_size_shift(info)) {
    case 0:
        value.type = QEMU_PLUGIN_MEM_VALUE_U8;
        value.data.u8 = (uint8_t)low;
        break;
    case 1:
        value.type = QEMU_PLUGIN_MEM_VALUE_U16;
        value.data.u16 = (uint16_t)low;
        break;
    case 2:
        value.type = QEMU_PLUGIN_MEM_VALUE_U32;
        value.data.u32 = (uint32_t)low;
        break;
    case 3:
        value.type = QEMU_PLUGIN_MEM_VALUE_U64;
        value.data.u64 = low;
        break;
    case 4:
        value.type = QEMU_PLUGIN_MEM_VALUE_U128;
        value.data.u128.low = low;
        value.data.u128.high = current_cpu->neg.plugin_mem_value_high;
        break;
    default:
        g_assert_not_reached();
    }
    return value;
}

/*
 * Virtual Memory queries
 */

#ifdef CONFIG_SOFTMMU
static __thread struct qemu_plugin_hwaddr hwaddr_info;
#endif

struct qemu_plugin_hwaddr *qemu_plugin_get_hwaddr(qemu_plugin_meminfo_t info,
                                                  uint64_t vaddr)
{
#ifdef CONFIG_SOFTMMU
    CPUState *cpu = current_cpu;
    unsigned int mmu_idx = get_mmuidx(info);
    enum qemu_plugin_mem_rw rw = get_plugin_meminfo_rw(info);
    hwaddr_info.is_store = (rw & QEMU_PLUGIN_MEM_W) != 0;

    assert(mmu_idx < NB_MMU_MODES);

    if (!tlb_plugin_lookup(cpu, vaddr, mmu_idx,
                           hwaddr_info.is_store, &hwaddr_info)) {
        error_report("invalid use of qemu_plugin_get_hwaddr");
        return NULL;
    }

    return &hwaddr_info;
#else
    return NULL;
#endif
}

bool qemu_plugin_hwaddr_is_io(const struct qemu_plugin_hwaddr *haddr)
{
#ifdef CONFIG_SOFTMMU
    return haddr->is_io;
#else
    return false;
#endif
}

uint64_t qemu_plugin_hwaddr_phys_addr(const struct qemu_plugin_hwaddr *haddr)
{
#ifdef CONFIG_SOFTMMU
    if (haddr) {
        return haddr->phys_addr;
    }
#endif
    return 0;
}

const char *qemu_plugin_hwaddr_device_name(const struct qemu_plugin_hwaddr *h)
{
#ifdef CONFIG_SOFTMMU
    if (h && h->is_io) {
        MemoryRegion *mr = h->mr;
        if (!mr->name) {
            unsigned maddr = (uintptr_t)mr;
            g_autofree char *temp = g_strdup_printf("anon%08x", maddr);
            return g_intern_string(temp);
        } else {
            return g_intern_string(mr->name);
        }
    } else {
        return g_intern_static_string("RAM");
    }
#else
    return g_intern_static_string("Invalid");
#endif
}

int qemu_plugin_num_vcpus(void)
{
    return plugin_num_vcpus();
}

/*
 * Plugin output
 */
void qemu_plugin_outs(const char *string)
{
    qemu_log_mask(CPU_LOG_PLUGIN, "%s", string);
}

bool qemu_plugin_bool_parse(const char *name, const char *value, bool *ret)
{
    return name && value && qapi_bool_parse(name, value, ret, NULL);
}

/*
 * Binary path, start and end locations
 */
const char *qemu_plugin_path_to_binary(void)
{
    char *path = NULL;
#ifdef CONFIG_USER_ONLY
    TaskState *ts = get_task_state(current_cpu);
    path = g_strdup(ts->bprm->filename);
#endif
    return path;
}

uint64_t qemu_plugin_start_code(void)
{
    uint64_t start = 0;
#ifdef CONFIG_USER_ONLY
    TaskState *ts = get_task_state(current_cpu);
    start = ts->info->start_code;
#endif
    return start;
}

uint64_t qemu_plugin_end_code(void)
{
    uint64_t end = 0;
#ifdef CONFIG_USER_ONLY
    TaskState *ts = get_task_state(current_cpu);
    end = ts->info->end_code;
#endif
    return end;
}

uint64_t qemu_plugin_entry_code(void)
{
    uint64_t entry = 0;
#ifdef CONFIG_USER_ONLY
    TaskState *ts = get_task_state(current_cpu);
    entry = ts->info->entry;
#endif
    return entry;
}

/*
 * Create register handles.
 *
 * We need to create a handle for each register so the plugin
 * infrastructure can call gdbstub to read a register. They are
 * currently just a pointer encapsulation of the gdb_reg but in
 * future may hold internal plugin state so its important plugin
 * authors are not tempted to treat them as numbers.
 *
 * We also construct a result array with those handles and some
 * ancillary data the plugin might find useful.
 */

static GArray *create_register_handles(GArray *gdbstub_regs)
{
    GArray *find_data = g_array_new(true, true,
                                    sizeof(qemu_plugin_reg_descriptor));

    for (int i = 0; i < gdbstub_regs->len; i++) {
        GDBRegDesc *grd = &g_array_index(gdbstub_regs, GDBRegDesc, i);
        qemu_plugin_reg_descriptor desc;

        /* skip "un-named" regs */
        if (!grd->name) {
            continue;
        }

        /* Create a record for the plugin */
        desc.handle = GINT_TO_POINTER(grd->gdb_reg + 1);
        desc.name = g_intern_string(grd->name);
        desc.feature = g_intern_string(grd->feature_name);
        g_array_append_val(find_data, desc);
    }

    return find_data;
}

GArray *qemu_plugin_get_registers(void)
{
    g_assert(current_cpu);

    g_autoptr(GArray) regs = gdb_get_register_list(current_cpu);
    return create_register_handles(regs);
}

bool qemu_plugin_read_memory_vaddr(vaddr addr, GByteArray *data, size_t len)
{
    g_assert(current_cpu);

    if (len == 0) {
        return false;
    }

    g_byte_array_set_size(data, len);

    int result = cpu_memory_rw_debug(current_cpu, addr, data->data,
                                     data->len, false);

    if (result < 0) {
        return false;
    }

    return true;
}

int qemu_plugin_read_register(struct qemu_plugin_register *reg, GByteArray *buf)
{
    g_assert(current_cpu);

    return gdb_read_register(current_cpu, buf, GPOINTER_TO_INT(reg) - 1);
}

struct qemu_plugin_scoreboard *qemu_plugin_scoreboard_new(size_t element_size)
{
    return plugin_scoreboard_new(element_size);
}

void qemu_plugin_scoreboard_free(struct qemu_plugin_scoreboard *score)
{
    plugin_scoreboard_free(score);
}

void *qemu_plugin_scoreboard_find(struct qemu_plugin_scoreboard *score,
                                  unsigned int vcpu_index)
{
    g_assert(vcpu_index < qemu_plugin_num_vcpus());
    /* we can't use g_array_index since entry size is not statically known */
    char *base_ptr = score->data->data;
    return base_ptr + vcpu_index * g_array_get_element_size(score->data);
}

static uint64_t *plugin_u64_address(qemu_plugin_u64 entry,
                                    unsigned int vcpu_index)
{
    char *ptr = qemu_plugin_scoreboard_find(entry.score, vcpu_index);
    return (uint64_t *)(ptr + entry.offset);
}

void qemu_plugin_u64_add(qemu_plugin_u64 entry, unsigned int vcpu_index,
                         uint64_t added)
{
    *plugin_u64_address(entry, vcpu_index) += added;
}

uint64_t qemu_plugin_u64_get(qemu_plugin_u64 entry,
                             unsigned int vcpu_index)
{
    return *plugin_u64_address(entry, vcpu_index);
}

void qemu_plugin_u64_set(qemu_plugin_u64 entry, unsigned int vcpu_index,
                         uint64_t val)
{
    *plugin_u64_address(entry, vcpu_index) = val;
}

uint64_t qemu_plugin_u64_sum(qemu_plugin_u64 entry)
{
    uint64_t total = 0;
    for (int i = 0, n = qemu_plugin_num_vcpus(); i < n; ++i) {
        total += qemu_plugin_u64_get(entry, i);
    }
    return total;
}

/*
 * Time control
 */
static bool has_control;
#ifdef CONFIG_SOFTMMU
static Error *migration_blocker;
#endif

const void *qemu_plugin_request_time_control(void)
{
    if (!has_control) {
        has_control = true;
#ifdef CONFIG_SOFTMMU
        error_setg(&migration_blocker,
                   "TCG plugin time control does not support migration");
        migrate_add_blocker(&migration_blocker, NULL);
#endif
        return &has_control;
    }
    return NULL;
}

#ifdef CONFIG_SOFTMMU
static void advance_virtual_time__async(CPUState *cpu, run_on_cpu_data data)
{
    int64_t new_time = data.host_ulong;
    qemu_clock_advance_virtual_time(new_time);
}
#endif

void qemu_plugin_update_ns(const void *handle, int64_t new_time)
{
#ifdef CONFIG_SOFTMMU
    if (handle == &has_control) {
        /* Need to execute out of cpu_exec, so bql can be locked. */
        async_run_on_cpu(current_cpu,
                         advance_virtual_time__async,
                         RUN_ON_CPU_HOST_ULONG(new_time));
    }
#endif
}
