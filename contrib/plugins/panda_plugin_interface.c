#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
QEMU_PLUGIN_EXPORT int (*external_plugin_install)(qemu_plugin_id_t id, const qemu_info_t *info,int argc, char **argv);

static void before_block_exec(unsigned int cpu_index, void *udata)
{
    // global_val = panda_get_asid()
    // std::cout << "before_block_exec " << hex <<  (uint64_t) udata << std::endl;
}

static void after_block_exec(unsigned int cpu_index, void *udata)
{
    // if (panda_get_asid() != global_val) { panda_callbacks_asid_changed() }
    // unsigned int 
    // std::cout << "after_block_exec " <<  hex <<  (uint64_t) udata << std::endl;
}

static void insn_exec(unsigned int cpu_index, void *udata)
{
    // std::cout << "insn_exec " << hex << udata << std::endl;
}

static void vcpu_mem(unsigned int cpu_index, qemu_plugin_meminfo_t info,
                     uint64_t vaddr, void *udata){
    
    struct qemu_plugin_hwaddr* hwaddr = qemu_plugin_get_hwaddr(info, vaddr);
    // uint64_t paddr = qemu_plugin_hwaddr_phys_addr(hwaddr);
    bool is_store = qemu_plugin_mem_is_store(info);

    if (qemu_plugin_hwaddr_is_io(hwaddr)){
        // panda_callbacks_mem_io()
    }

    if (is_store){
        //panda_callbacks_phys_mem_write
        //panda_callbacks_phys_mem_read
    }else{
        //panda_callbacks_phys_mem_read
        //panda_callbacks_phys_mem_write
    }
    // std::cout << "vcpu_mem " << std::endl;
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    struct qemu_plugin_insn *last_instr;
    size_t n_insns;

    n_insns = qemu_plugin_tb_n_insns(tb);
    
    // std::cout << "tb_trans " << n_insns << std::endl;

    /**
     * This replicates insn_translate + insn_exec behavior
     */
    for (size_t i=0; i<n_insns; i++){
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        // std::cout << "insn_translate" << std::endl;
        if (1) {// panda_callbacks_insn_translate
            qemu_plugin_register_vcpu_insn_exec_cb(insn, insn_exec, QEMU_PLUGIN_CB_NO_REGS, (void*)qemu_plugin_insn_vaddr(insn));
        }
        if (1){ // panda_callbacks_mem_callbacks enabled
            qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem,
                                             QEMU_PLUGIN_CB_NO_REGS,
                                             QEMU_PLUGIN_MEM_RW, NULL);
        }
    }

    uint64_t vaddr = qemu_plugin_tb_vaddr(tb);
    // install before_block_exec
    qemu_plugin_register_vcpu_tb_exec_cb(tb, before_block_exec,
                                        QEMU_PLUGIN_CB_NO_REGS,
                                         (void *)vaddr);
    
    // install after_block_exec
    last_instr = qemu_plugin_tb_get_insn(tb, n_insns - 1);
    uint64_t addr = qemu_plugin_insn_vaddr(last_instr);
    qemu_plugin_register_vcpu_insn_exec_cb(last_instr, after_block_exec,
                                QEMU_PLUGIN_CB_NO_REGS, (void *)addr);
    
}

// void plugin_exit(qemu_plugin_id_t id, void* userdata){
    // clean up data structures
// }

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{
    // qemu_plugin_outs("Hello from pandacore plugin");
    // cout << "hello from pandacore plugin" << endl;
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    // qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    if (external_plugin_install) {
        return external_plugin_install(id, info, argc, argv);
    }else{
        return 0;
    }
}
