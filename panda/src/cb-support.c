#include <stdint.h>
#include "qemu/osdep.h"
#include "cpu.h"
#include "panda/debug.h"
#include "panda/plugin.h"
#include "panda/callbacks/cb-support.h"
#include "panda/common.h"

#include "exec/cpu-common.h"
#include "exec/ram_addr.h"

// For each callback, use MAKE_CALLBACK or MAKE_REPLAY_ONLY_CALLBACK as defined in
#include "panda/callbacks/cb-macros.h"
#include "panda/callbacks/cb-trampolines.h"
#include "exec/tb-flush.h"

#define PCB(name) panda_callbacks_ ## name

// TODO: macro names should be include return type - bools |= all cb fns, voids don't

// MAKE_REPLAY_ONLY_CALLBACK(REPLAY_HD_TRANSFER, replay_hd_transfer,
//                     CPUState*, cpu, Hd_transfer_type, type,
//                     uint64_t, src_addr, uint64_t, dest_addr,
//                     size_t, num_bytes)

// MAKE_REPLAY_ONLY_CALLBACK(REPLAY_HANDLE_PACKET, replay_handle_packet,
//                   CPUState*, cpu, uint8_t*, buf,
//                   size_t, size, uint8_t, direction,
//                   uint64_t, buf_addr_rec)

// MAKE_REPLAY_ONLY_CALLBACK(REPLAY_NET_TRANSFER, replay_net_transfer,
//                     CPUState*, cpu, Net_transfer_type, type,
//                     uint64_t, src_addr, uint64_t, dst_addr,
//                     size_t, num_bytes);

// // These are used in exec.c
// MAKE_REPLAY_ONLY_CALLBACK(REPLAY_BEFORE_DMA, replay_before_dma,
//                     CPUState*, cpu, const uint8_t*, buf,
//                     hwaddr, addr, size_t, size,
//                     bool, is_write);

// MAKE_REPLAY_ONLY_CALLBACK(REPLAY_AFTER_DMA, replay_after_dma,
//                     CPUState*, cpu, const uint8_t*, buf,
//                     hwaddr, addr, size_t ,size,
//                     bool, is_write)

MAKE_CALLBACK(void, BEFORE_TCG_CODEGEN, before_tcg_codegen, CPUState*, cpu, TranslationBlock*, tb);

// These are used in cpu-exec.c
MAKE_CALLBACK(void, BEFORE_BLOCK_EXEC, before_block_exec,
                    CPUState*, cpu, TranslationBlock*, tb);

MAKE_CALLBACK(void, AFTER_BLOCK_EXEC, after_block_exec,
                    CPUState*, cpu, TranslationBlock*, tb,
                    uint8_t, exitCode);

MAKE_CALLBACK(void, BEFORE_BLOCK_TRANSLATE, before_block_translate,
                    CPUState*, cpu, uint64_t, pc);

MAKE_CALLBACK(void, AFTER_BLOCK_TRANSLATE, after_block_translate,
                    CPUState*, cpu, TranslationBlock*, tb);

MAKE_CALLBACK(void, AFTER_CPU_EXEC_ENTER, after_cpu_exec_enter,
                    CPUState*, cpu);

MAKE_CALLBACK(void, BEFORE_CPU_EXEC_EXIT, before_cpu_exec_exit,
                    CPUState*, cpu, bool, ranBlock);

MAKE_CALLBACK(void, AFTER_LOADVM, after_loadvm, CPUState*, env);

// These are used in target-i386/translate.c
MAKE_CALLBACK(bool, INSN_TRANSLATE, insn_translate,
                    CPUState*, env, uint64_t, pc);

MAKE_CALLBACK(bool, AFTER_INSN_TRANSLATE, after_insn_translate,
                    CPUState*, env, uint64_t, pc)

//MAKE_CALLBACK(void, START_BLOCK_EXEC, start_block_exec,
//                    CPUState*, env, TranslationBlock*, tb)

MAKE_CALLBACK(void, END_BLOCK_EXEC, end_block_exec,
                    CPUState*, env, TranslationBlock*, tb)

// Non-macroized version for SBE - if panda_please_retranslate is set, we'll break
void PCB(start_block_exec)(CPUState *cpu, TranslationBlock *tb) {
    panda_cb_list *plist;
    for (plist = panda_cbs[PANDA_CB_START_BLOCK_EXEC]; plist != NULL; plist = panda_cb_list_next(plist)) {
        if (plist->enabled)
            plist->entry.start_block_exec(plist->context, cpu, tb);
    }

    if (panda_break_exec()) {
        cpu_loop_exit_noexc(cpu); // noreturn - lonjmps back to translation logic. Allows you to change pc in an SBE and go
                                  // there immediately. It's like before_block_exec_invalidate_opt, but fast
    }
}
void panda_cb_trampoline_start_block_exec(void* context, CPUState *cpu, TranslationBlock *tb) {\
    (*(panda_cb*)context).start_block_exec(cpu, tb);
}

// these aren't used
MAKE_CALLBACK(void, HD_READ, hd_read, CPUState*, env);
MAKE_CALLBACK(void, HD_WRITE, hd_write, CPUState*, env);

MAKE_CALLBACK(int, MONITOR, monitor, Monitor*, mon, const char*, cmd);
MAKE_CALLBACK(bool, QMP, qmp, char*, cmd, char*, args, char **, result);

#ifdef TARGET_LATER
// Helper - get a physical address
static inline hwaddr get_paddr(CPUState *cpu, uint64_t addr, void *ram_ptr) {
    if (!ram_ptr) {
        return panda_virt_to_phys(cpu, addr);
    }

    ram_addr_t offset = 0;
    RAMBlock *block = qemu_ram_block_from_host(ram_ptr, false, &offset);
    if (!block) {
        return panda_virt_to_phys(cpu, addr);
    } else {
        if (block->mr) {
          return block->mr->addr + offset;
        } else {
          return -1;
        }
    }
}
#endif

// These are used in cputlb.c
MAKE_CALLBACK(void, MMIO_AFTER_READ, mmio_after_read,
                    CPUState*, env, uint64_t, physaddr,
                    uint64_t, vaddr, size_t, size,
                    uint64_t*, val);

MAKE_CALLBACK(void, MMIO_BEFORE_WRITE, mmio_before_write,
                    CPUState*, env, uint64_t, physaddr,
                    uint64_t, vaddr, size_t, size,
                    uint64_t*, val);

// vl.c
MAKE_CALLBACK(void, AFTER_MACHINE_INIT, after_machine_init,
                    CPUState*, env);

MAKE_CALLBACK(void, DURING_MACHINE_INIT, during_machine_init,
                    MachineState*, machine);

// Returns true if any registered&enabled callback returns non-zero.
// If so, we'll silence the memory write error.
MAKE_CALLBACK(bool, UNASSIGNED_IO_WRITE, unassigned_io_write,
                    CPUState*, env, uint64_t, pc,
                    hwaddr, addr, size_t, size,
                   uint64_t, val);

// Returns true if any registered&enabled callback returns non-zero,
// if so, we'll silence the invalid memory read error and return
// the value provided by the last callback in `val`
// Note if multiple callbacks run they can each mutate val
MAKE_CALLBACK(bool, UNASSIGNED_IO_READ, unassigned_io_read,
                    CPUState*, env, uint64_t, pc,
                    hwaddr, addr, size_t, size,
                   uint64_t*, val);

MAKE_CALLBACK(void, TOP_LOOP, top_loop,
                    CPUState*, cpu);

// Returns true if any registered + enabled callback returns nonzero.
// If so, it doesn't let the asid change
MAKE_CALLBACK(bool, ASID_CHANGED, asid_changed,
                    CPUState*, env, uint64_t, old_asid,
                    uint64_t, new_asid);


// target-i386/misc_helpers.c
MAKE_CALLBACK(bool, GUEST_HYPERCALL, guest_hypercall,
                    CPUState*, env);

MAKE_CALLBACK(void, CPU_RESTORE_STATE, cpu_restore_state,
                    CPUState*, env, TranslationBlock*, tb);

// MAKE_REPLAY_ONLY_CALLBACK(REPLAY_SERIAL_RECEIVE, replay_serial_receive,
//                     CPUState*, env, uint64_t, fifo_addr,
//                     uint8_t, value);

// MAKE_REPLAY_ONLY_CALLBACK(REPLAY_SERIAL_READ, replay_serial_read,
//                     CPUState*, env, uint64_t, fifo_addr,
//                     uint32_t, port_addr, uint8_t, value);

// MAKE_REPLAY_ONLY_CALLBACK(REPLAY_SERIAL_SEND, replay_serial_send,
//                     CPUState*, env, uint64_t, fifo_addr,
//                     uint8_t, value);

// MAKE_REPLAY_ONLY_CALLBACK(REPLAY_SERIAL_WRITE, replay_serial_write,
//                     CPUState*, env, uint64_t, fifo_addr,
//                     uint32_t, port_addr, uint8_t, value);

MAKE_CALLBACK_NO_ARGS(void, MAIN_LOOP_WAIT, main_loop_wait);

MAKE_CALLBACK_NO_ARGS(void, PRE_SHUTDOWN, pre_shutdown);


// Non-standard callbacks below here

extern bool panda_plugin_to_unload;
extern int nb_panda_plugins;
extern panda_plugin panda_plugins[MAX_PANDA_PLUGINS];

void PCB(before_find_fast)(void) {
    if (panda_plugin_to_unload) {
        panda_plugin_to_unload = false;
        for (int i = 0; i < nb_panda_plugins;) {
            if (panda_plugins[i].unload) {
                panda_do_unload_plugin(i);
            } else {
                i++;
            }
        }
    }
    if (panda_flush_tb()) {
        tb_flush(first_cpu);
    }
}

bool panda_cb_trampoline_before_block_exec_invalidate_opt(void* context, CPUState *env, TranslationBlock *tb) {
    return (*(panda_cb*)context).before_block_exec_invalidate_opt(env, tb);
}

bool PCB(after_find_fast)(CPUState *cpu, TranslationBlock *tb,
                          bool bb_invalidate_done, bool *invalidate) {
    panda_cb_list *plist;
    if (!bb_invalidate_done) {
        for (plist = panda_cbs[PANDA_CB_BEFORE_BLOCK_EXEC_INVALIDATE_OPT];
             plist != NULL; plist = panda_cb_list_next(plist)) {
            if (plist->enabled)
              *invalidate |=
                  plist->entry.before_block_exec_invalidate_opt(plist->context, cpu, tb);
        }
        return true;
    }
    return false;
}

int32_t panda_cb_trampoline_before_handle_exception(void* context, CPUState *cpu, int32_t exception_index) {
    return (*(panda_cb*)context).before_handle_exception(cpu, exception_index);
}
    
int panda_cb_trampoline_insn_exec(void* context, CPUState *env, uint64_t pc) {
    return (*(panda_cb*)context).insn_exec(env, pc);
}
    
int panda_cb_trampoline_after_insn_exec(void* context, CPUState *env, uint64_t pc) {
    return (*(panda_cb*)context).after_insn_exec(env, pc);
}

// this callback allows us to swallow exceptions
//
// first callback that returns an exception index that *differs* from
// the one passed as an arg wins. That is, that is what we return as
// the new exception index, which will replace cpu->exception_index
//
// Note: We still run all of the callbacks, but only one of them can
// change the current cpu exception.  Sorry.

int32_t PCB(before_handle_exception)(CPUState *cpu, int32_t exception_index) {
    panda_cb_list *plist;
    bool got_new_exception = false;
    int32_t new_exception;

    for (plist = panda_cbs[PANDA_CB_BEFORE_HANDLE_EXCEPTION]; plist != NULL;
         plist = panda_cb_list_next(plist)) {
        if (plist->enabled) {
            int32_t new_e = plist->entry.before_handle_exception(plist->context, cpu, exception_index);
            if (!got_new_exception && new_e != exception_index) {
                got_new_exception = true;
                new_exception = new_e;
            }
        }
    }

    if (got_new_exception)
        return new_exception;

    return exception_index;
}

int32_t panda_cb_trampoline_before_handle_interrupt(void* context, CPUState *cpu, int32_t interrupt_request) {
    return (*(panda_cb*)context).before_handle_interrupt(cpu, interrupt_request);
}

int32_t PCB(before_handle_interrupt)(CPUState *cpu, int32_t interrupt_request) {
    panda_cb_list *plist;
    bool got_new_interrupt = false;
    int32_t new_interrupt;

    for (plist = panda_cbs[PANDA_CB_BEFORE_HANDLE_INTERRUPT]; plist != NULL;
         plist = panda_cb_list_next(plist)) {
        if (plist->enabled) {
            int32_t new_i = plist->entry.before_handle_interrupt(plist->context, cpu, interrupt_request);
            if (!got_new_interrupt && new_i != interrupt_request) {
                got_new_interrupt = true;
                new_interrupt = new_i;
            }
        }
    }

    if (got_new_interrupt)
        return new_interrupt;

    return interrupt_request;
}

#define MEM_CB_TRAMPOLINES(mode) \
    void panda_cb_trampoline_ ## mode ## _mem_before_read(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size) { \
        (*(panda_cb*)context) . mode ## _mem_before_read(env, pc, addr, size); \
    } \
    void panda_cb_trampoline_ ## mode ## _mem_after_read(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf) { \
        (*(panda_cb*)context) . mode ## _mem_after_read(env, pc, addr, size, buf); \
    } \
    void panda_cb_trampoline_ ## mode ## _mem_before_write(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf) { \
        (*(panda_cb*)context) . mode ## _mem_before_write(env, pc, addr, size, buf);\
    } \
    void panda_cb_trampoline_ ## mode ## _mem_after_write(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf) { \
        (*(panda_cb*)context) . mode ## _mem_after_write(env, pc, addr, size, buf); \
    }

MEM_CB_TRAMPOLINES(virt)
MEM_CB_TRAMPOLINES(phys)

#ifdef CONFIG_LATER
// These are used in softmmu_template.h. They are distinct from MAKE_CALLBACK's standard form.
// ram_ptr is a possible pointer into host memory from the TLB code. Can be NULL.
void PCB(mem_before_read)(CPUState *env, uint64_t pc, uint64_t addr,
                          size_t data_size, void *ram_ptr) {
    panda_cb_list *plist;
    for(plist = panda_cbs[PANDA_CB_VIRT_MEM_BEFORE_READ]; plist != NULL;
        plist = panda_cb_list_next(plist)) {
        if (plist->enabled) plist->entry.virt_mem_before_read(plist->context, env, panda_current_pc(env), addr,
                                                              data_size);
    }
    if (panda_cbs[PANDA_CB_PHYS_MEM_BEFORE_READ]) {
        hwaddr paddr = get_paddr(env, addr, ram_ptr);
        if (paddr == -1) return;
        for(plist = panda_cbs[PANDA_CB_PHYS_MEM_BEFORE_READ]; plist != NULL;
            plist = panda_cb_list_next(plist)) {
            if (plist->enabled) plist->entry.phys_mem_before_read(plist->context, env, panda_current_pc(env),
                                                                  paddr, data_size);
        }
    }
}


void PCB(mem_after_read)(CPUState *env, uint64_t pc, uint64_t addr,
                         size_t data_size, uint64_t result, void *ram_ptr) {
    panda_cb_list *plist;
    for(plist = panda_cbs[PANDA_CB_VIRT_MEM_AFTER_READ]; plist != NULL;
        plist = panda_cb_list_next(plist)) {
        /* mstamat: Passing &result as the last cb arg doesn't make much sense. */
        if (plist->enabled) plist->entry.virt_mem_after_read(plist->context, env, panda_current_pc(env), addr,
                                         data_size, (uint8_t *)&result);
    }
    if (panda_cbs[PANDA_CB_PHYS_MEM_AFTER_READ]) {
        hwaddr paddr = get_paddr(env, addr, ram_ptr);
        if (paddr == -1) return;
        for(plist = panda_cbs[PANDA_CB_PHYS_MEM_AFTER_READ]; plist != NULL;
            plist = panda_cb_list_next(plist)) {
            /* mstamat: Passing &result as the last cb arg doesn't make much sense. */
            if (plist->enabled) plist->entry.phys_mem_after_read(plist->context, env, panda_current_pc(env), paddr,
                                             data_size, (uint8_t *)&result);
        }
    }
}


void PCB(mem_before_write)(CPUState *env, uint64_t pc, uint64_t addr,
                           size_t data_size, uint64_t val, void *ram_ptr) {
    panda_cb_list *plist;
    for(plist = panda_cbs[PANDA_CB_VIRT_MEM_BEFORE_WRITE]; plist != NULL;
        plist = panda_cb_list_next(plist)) {
        /* mstamat: Passing &val as the last arg doesn't make much sense. */
        if (plist->enabled) plist->entry.virt_mem_before_write(plist->context, env, panda_current_pc(env), addr,
                                           data_size, (uint8_t *)&val);
    }
    if (panda_cbs[PANDA_CB_PHYS_MEM_BEFORE_WRITE]) {
        hwaddr paddr = get_paddr(env, addr, ram_ptr);
        if (paddr == -1) return;
        for(plist = panda_cbs[PANDA_CB_PHYS_MEM_BEFORE_WRITE]; plist != NULL;
            plist = panda_cb_list_next(plist)) {
            /* mstamat: Passing &val as the last cb arg doesn't make much sense. */
            if (plist->enabled) plist->entry.phys_mem_before_write(plist->context, env, panda_current_pc(env), paddr,
                                               data_size, (uint8_t *)&val);
        }
    }
}


void PCB(mem_after_write)(CPUState *env, uint64_t pc, uint64_t addr,
                          size_t data_size, uint64_t val, void *ram_ptr) {
    panda_cb_list *plist;
    for (plist = panda_cbs[PANDA_CB_VIRT_MEM_AFTER_WRITE]; plist != NULL;
         plist = panda_cb_list_next(plist)) {
        /* mstamat: Passing &val as the last cb arg doesn't make much sense. */
        if (plist->enabled) plist->entry.virt_mem_after_write(plist->context, env, panda_current_pc(env), addr,
                                          data_size, (uint8_t *)&val);
    }
    if (panda_cbs[PANDA_CB_PHYS_MEM_AFTER_WRITE]) {
        hwaddr paddr = get_paddr(env, addr, ram_ptr);
        if (paddr == -1) return;
        for (plist = panda_cbs[PANDA_CB_PHYS_MEM_AFTER_WRITE]; plist != NULL;
             plist = panda_cb_list_next(plist)) {
            /* mstamat: Passing &val as the last cb arg doesn't make much sense. */
            if (plist->enabled) plist->entry.phys_mem_after_write(plist->context, env, panda_current_pc(env), paddr,
                                              data_size, (uint8_t *)&val);
        }
    }
}
#endif