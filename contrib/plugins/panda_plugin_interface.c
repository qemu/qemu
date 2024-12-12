#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <qemu-plugin.h>
typedef void* CPUState;
typedef void* Monitor;
typedef void* MachineState;
typedef uint64_t hwaddr;
typedef uint64_t target_ulong;
#include <../panda/callbacks/cb-support.h>
#include <../panda/panda_qemu_plugin_helpers.h>
#include <../qemu/compiler.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
QEMU_PLUGIN_EXPORT int (*external_plugin_install)(qemu_plugin_id_t id, const qemu_info_t *info,int argc, char **argv);


static void start_block_exec_cb(unsigned int cpu_index, void *udata)
{
    // printf("start_block_exec  %d %p\n", cpu_index, udata);
    CPUState *cpu = panda_current_cpu(cpu_index);
    panda_callbacks_start_block_exec(cpu, (TranslationBlock*) udata);
}

static void end_block_exec_cb(unsigned int cpu_index, void *udata)
{
    // printf("end_block_exec  %d %p\n", cpu_index, udata);
    CPUState *cpu = panda_current_cpu(cpu_index);
    panda_callbacks_end_block_exec(cpu, (TranslationBlock*) udata);
}

static void insn_exec(unsigned int cpu_index, void *udata)
{
    // CPUState *cpu = panda_current_cpu(cpu_index);
    // panda_callbacks_insn_exec(cpu, (uint64_t) udata);
}

#ifdef TODO_LATER
static void vcpu_mem(unsigned int cpu_index, qemu_plugin_meminfo_t info,
                     uint64_t vaddr, void *udata){
    CPUState *cpu = panda_current_cpu(cpu_index);
    struct qemu_plugin_hwaddr* hwaddr_info = qemu_plugin_get_hwaddr(info, vaddr);
    uint64_t hwaddr;
    if (hwaddr_info){
        hwaddr = qemu_plugin_hwaddr_phys_addr(hwaddr_info);
    }
    bool is_store = qemu_plugin_mem_is_store(info);
    qemu_plugin_mem_value m = qemu_plugin_mem_get_value(info);
    size_t size;
    void *data;

    switch (m.type){
        case QEMU_PLUGIN_MEM_VALUE_U8:
            size = 1;
            data = &m.data.u8;
            break;
        case QEMU_PLUGIN_MEM_VALUE_U16:
            size = 2;
            data = &m.data.u16;
            break;
        case QEMU_PLUGIN_MEM_VALUE_U32:
            size = 4;
            data = &m.data.u32;
            break;
        case QEMU_PLUGIN_MEM_VALUE_U64:
            size = 8;
            data = &m.data.u64;
            break;
        case QEMU_PLUGIN_MEM_VALUE_U128:
            size = 16;
            data = &m.data.u128;
            break;
        default:
            assert(false);
    }

    if (hwaddr_info && qemu_plugin_hwaddr_is_io(hwaddr_info)){
        // panda_callbacks_mmio_after_read(cpu, hwaddr, vaddr, size, data);
    }

    if (is_store){
        if (hwaddr_info)
            panda_callbacks_phys_mem_after_write(cpu, hwaddr, size, data);
        panda_callbacks_virt_mem_after_write(cpu, vaddr, size, data);
    }else{
        if (hwaddr_info)
            panda_callbacks_phys_mem_after_read(cpu, hwaddr, size, data);
        panda_callbacks_virt_mem_after_read(cpu, vaddr, size, data);
    }
}
#endif


static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    struct qemu_plugin_insn *last_instr;
    size_t n_insns;
    struct qemu_plugin_insn *insn;

    uint64_t pc = qemu_plugin_tb_vaddr(tb);
    // printf("tb_trans %" PRIu64 "\n", pc);

    n_insns = qemu_plugin_tb_n_insns(tb);
    for (size_t i=0; i<n_insns; i++){
        insn = qemu_plugin_tb_get_insn(tb, i);
        if (unlikely(panda_callbacks_insn_translate(panda_cpu_in_translate(), pc))){
            qemu_plugin_register_vcpu_insn_exec_cb(insn, insn_exec, QEMU_PLUGIN_CB_NO_REGS, (void*)qemu_plugin_insn_vaddr(insn));
        }
#ifdef TODO_LATER
        int memcb_status =  panda_get_memcb_status();
        if (memcb_status){
            qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem,
                                             QEMU_PLUGIN_CB_NO_REGS,
                                             (enum qemu_plugin_mem_rw) memcb_status, NULL);
        }
#endif
    }

    // install before_block_exec
    TranslationBlock *real_tb = panda_get_tb();
    qemu_plugin_register_vcpu_tb_exec_cb(tb, start_block_exec_cb,
                                        QEMU_PLUGIN_CB_NO_REGS,
                                         (void *)real_tb);
    
    // install after_block_exec
    last_instr = qemu_plugin_tb_get_insn(tb, n_insns - 1);
    qemu_plugin_register_vcpu_insn_exec_cb(last_instr, end_block_exec_cb,
                                QEMU_PLUGIN_CB_NO_REGS, (void *)real_tb);
    
}

static void vcpu_init(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    printf("vcpu_init %d\n", vcpu_index);
}

static void vcpu_exit(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    printf("vcpu_exit %d\n", vcpu_index);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{
    printf("Hello from pandacore plugin\n");
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init);
    qemu_plugin_register_vcpu_exit_cb(id, vcpu_exit);
    return 0;
}
