#ifndef EXEC_ALL_H
// If this file is included from a file that doesn't define TranslationBlock (e.g., memory.c), we still need to be valid
typedef struct {} TranslationBlock;
#endif

/***************************************************************************
 *                          CALLBACK TRAMPOLINES                           *
 ***************************************************************************/

void panda_cb_trampoline_virt_mem_before_read(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size);
void panda_cb_trampoline_virt_mem_after_read(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf);
void panda_cb_trampoline_virt_mem_before_write(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf);
void panda_cb_trampoline_virt_mem_after_write(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf);
void panda_cb_trampoline_phys_mem_before_read(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size);
void panda_cb_trampoline_phys_mem_after_read(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf);
void panda_cb_trampoline_phys_mem_before_write(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf);
void panda_cb_trampoline_phys_mem_after_write(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf);

int panda_cb_trampoline_insn_exec(void* context, CPUState *env, uint64_t pc);
int panda_cb_trampoline_after_insn_exec(void* context, CPUState *env, uint64_t pc);
int panda_cb_trampoline_monitor(void* context, Monitor *mon, const char *cmd);
bool panda_cb_trampoline_qmp(void* context, char *command, char *args, char **result);
//int panda_cb_trampoline_before_loadvm(void* context);
void panda_cb_trampoline_replay_hd_transfer(void* context, CPUState *env, uint32_t type, uint64_t src_addr, uint64_t dest_addr, size_t num_bytes);
void panda_cb_trampoline_after_machine_init(void* context, CPUState *env);
void panda_cb_trampoline_after_loadvm(void* context, CPUState *env);

/* invoked from cpu-exec.c */
void panda_cb_trampoline_before_block_exec(void* context, CPUState *env, TranslationBlock *tb);
void panda_cb_trampoline_after_block_exec(void* context, CPUState *env, TranslationBlock *tb, uint8_t exitCode);
void panda_cb_trampoline_before_block_translate(void* context, CPUState *env, uint64_t pc);
void panda_cb_trampoline_after_block_translate(void* context, CPUState *env, TranslationBlock *tb);
void panda_cb_trampoline_after_cpu_exec_enter(void* context, CPUState *env);
void panda_cb_trampoline_before_cpu_exec_exit(void* context, CPUState *env, bool ranBlock);

/* invoked from cpu-exec.c (indirectly) */
bool panda_cb_trampoline_before_block_exec_invalidate_opt(void* context, CPUState *env, TranslationBlock *tb);

/* invoked from cpus.c */
void panda_cb_trampoline_top_loop(void* context, CPUState *env);
void panda_cb_trampoline_during_machine_init(void* context, MachineState *machine);
void panda_cb_trampoline_main_loop_wait(void* context);
void panda_cb_trampoline_pre_shutdown(void* context);
bool panda_cb_trampoline_unassigned_io_read(void* context, CPUState *env, uint64_t pc, hwaddr addr, size_t size, uint64_t *val);
bool panda_cb_trampoline_unassigned_io_write(void* context, CPUState *env, uint64_t pc, hwaddr addr, size_t size, uint64_t val);
int32_t panda_cb_trampoline_before_handle_exception(void* context, CPUState *cpu, int32_t exception_index);
int32_t panda_cb_trampoline_before_handle_interrupt(void* context, CPUState *cpu, int32_t exception_index);
void panda_cb_trampoline_cbaddr(void* context);

/* invoked from cputlb.c */
void panda_cb_trampoline_mmio_after_read(void* context, CPUState *env, uint64_t physaddr, uint64_t vaddr, size_t size, uint64_t *val);
void panda_cb_trampoline_mmio_before_write(void* context, CPUState *env, uint64_t physaddr, uint64_t vaddr, size_t size, uint64_t *val);
void panda_cb_trampoline_hd_read(void* context, CPUState *env);
void panda_cb_trampoline_hd_write(void* context, CPUState *env);

/* invoked from exec.c */
// void panda_cb_trampoline_replay_before_dma(void* context, CPUState *env, const uint8_t *buf, hwaddr addr, size_t size, bool is_write);
// void panda_cb_trampoline_replay_after_dma(void* context, CPUState *env, const uint8_t *buf, hwaddr addr, size_t size, bool is_write);

/* invoked from panda/src/rr/rr_log.c */
// void panda_cb_trampoline_replay_handle_packet(void* context, CPUState *env, uint8_t *buf, size_t size, uint8_t direction, uint64_t buf_addr_rec);
// void panda_cb_trampoline_replay_net_transfer(void* context, CPUState *env, uint32_t type, uint64_t src_addr, uint64_t dest_addr, size_t num_bytes);
// void panda_cb_trampoline_replay_serial_receive(void* context, CPUState *env, uint64_t fifo_addr, uint8_t value);
// void panda_cb_trampoline_replay_serial_read(void* context, CPUState *env, uint64_t fifo_addr, uint32_t port_addr, uint8_t value);
// void panda_cb_trampoline_replay_serial_send(void* context, CPUState *env, uint64_t fifo_addr, uint8_t value);
// void panda_cb_trampoline_replay_serial_write(void* context, CPUState *env, uint64_t fifo_addr, uint32_t port_addr, uint8_t value);

/* invoked from panda/target/ARCH/translate.c */
bool panda_cb_trampoline_insn_translate(void* context, CPUState *env, uint64_t pc);
bool panda_cb_trampoline_after_insn_translate(void* context, CPUState *env, uint64_t pc);

/* invoked from target/i386/helper.c */
bool panda_cb_trampoline_asid_changed(void* context, CPUState *env, uint64_t oldval, uint64_t newval);

/* invoked from target/i386/misc_helper.c */
bool panda_cb_trampoline_guest_hypercall(void* context, CPUState *env);

/* invoked from translate-all.c */
void panda_cb_trampoline_cpu_restore_state(void* context, CPUState *env, TranslationBlock *tb);


void panda_cb_trampoline_before_tcg_codegen(void* context, CPUState *env, TranslationBlock *tb);
void panda_cb_trampoline_start_block_exec(void* context, CPUState *env, TranslationBlock *tb);
void panda_cb_trampoline_end_block_exec(void* context, CPUState *env, TranslationBlock *tb);
