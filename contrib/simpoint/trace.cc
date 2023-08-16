extern "C" {
#include <qemu/qemu-plugin.h>
}

#include <glib.h>
#include <iostream>
#include <string>
#include <cassert>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <deque>

struct qemu_trace_insn_t
{
    uint32_t hart_id;
    uint64_t uid;
    uint64_t pc_vaddr;
    uint64_t pc_paddr;
    uint32_t opcode;
    uint64_t mem_vaddr;
    uint64_t mem_paddr;
};

static bool system_emulation;

static bool enable_fastforward = false;

static bool fastforward_done = false;

static bool enable_roi = false;

static uint64_t max_insn_counts = 0;

static uint64_t exec_insn_counts = 0;

static uint64_t fast_forward_insn_counts = -1;

static std::vector<std::deque<qemu_trace_insn_t>> last_exec_queue;

static std::mutex last_exec_queue_lock;



static void resize_last_exec_array(unsigned int size){
    
    last_exec_queue_lock.lock();

    assert(size > last_exec_queue.size());

    last_exec_queue.resize(size);

    last_exec_queue_lock.unlock();
}


static void record_last_insn(const qemu_trace_insn_t& insn){
    g_autoptr(GString) report = g_string_new("");
    g_string_append_printf(report,"hart %d :", insn.hart_id);
    g_string_append_printf(report,"uid %08ld, ", insn.uid );
    g_string_append_printf(report,"opcode %08x, ", insn.opcode );
    g_string_append_printf(report,"pc_vaddr 0x%08lx, ", insn.pc_vaddr );
    g_string_append_printf(report,"pc_paddr 0x%08lx, ", insn.pc_paddr );
    g_string_append_printf(report,"mem_vaddr 0x%08lx, ", insn.mem_vaddr );
    g_string_append_printf(report,"mem_paddr 0x%08lx\n", insn.mem_paddr );
    qemu_plugin_outs(report->str);
}

static void plugin_user_exit() {
    for(size_t cpu_index = 0; cpu_index < last_exec_queue.size(); cpu_index++ ) {
        while (last_exec_queue[cpu_index].size())
        {
            record_last_insn(last_exec_queue[cpu_index].back());
            last_exec_queue[cpu_index].pop_back();
        }
    }
}

static void vcpu_mem(unsigned int cpu_index, qemu_plugin_meminfo_t info,
                     uint64_t vaddr, void *udata)
{
    if(!enable_fastforward || fastforward_done) {
        qemu_trace_insn_t& trace_insn = last_exec_queue[cpu_index].front();
        trace_insn.mem_vaddr = vaddr;
        if(system_emulation) {
            qemu_plugin_hwaddr* mem_hwaddr = (qemu_plugin_hwaddr*) qemu_plugin_get_hwaddr(info,vaddr);
            trace_insn.mem_paddr = qemu_plugin_hwaddr_phys_addr(mem_hwaddr);
        } else {
            trace_insn.mem_paddr = trace_insn.mem_vaddr;
        }
    }
}

static void vcpu_insn_exec(unsigned int cpu_index, void *udata)
{
    if(cpu_index >= last_exec_queue.size()) {
        resize_last_exec_array(cpu_index+1);
    }
    if(enable_fastforward && !fastforward_done) {
        if(exec_insn_counts >= fast_forward_insn_counts) {
            fastforward_done = true;
            exec_insn_counts = 0;
        }
    } else {
        if(last_exec_queue[cpu_index].size() > 0) {
            record_last_insn(last_exec_queue[cpu_index].back());
            last_exec_queue[cpu_index].pop_back();
        }

        qemu_trace_insn_t* trace_insn = (qemu_trace_insn_t*) udata;
        trace_insn->hart_id = cpu_index;
        trace_insn->uid = exec_insn_counts;
        last_exec_queue[cpu_index].push_front(*trace_insn);

        if(enable_roi && exec_insn_counts >= max_insn_counts) {
            plugin_user_exit();
            exit(0);
        }
    }
    exec_insn_counts++;
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    
    qemu_plugin_insn* insn; 

    for (size_t i = 0; i < qemu_plugin_tb_n_insns(tb); i++) {

        qemu_trace_insn_t* trace_insn = new qemu_trace_insn_t(); 

        insn = qemu_plugin_tb_get_insn(tb,i);

        trace_insn->pc_vaddr = qemu_plugin_insn_vaddr(insn);
        
        if(system_emulation) {
            qemu_plugin_hwaddr* pc_hwaddr = (qemu_plugin_hwaddr*) qemu_plugin_insn_haddr(insn);
            if(qemu_plugin_hwaddr_is_io(pc_hwaddr)){
                trace_insn->pc_paddr = trace_insn->pc_vaddr;
            } else {
                trace_insn->pc_paddr = qemu_plugin_hwaddr_phys_addr(pc_hwaddr);
            }
        } else {
            trace_insn->pc_paddr = trace_insn->pc_vaddr;
        }

        trace_insn->opcode = *((uint32_t *)qemu_plugin_insn_data(insn));

        if( qemu_plugin_insn_size(insn) == 2){
            trace_insn->opcode = trace_insn->opcode & 0xFFFF;
        }

        
        /* Register callback on memory read or write */
        qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem,
                                            QEMU_PLUGIN_CB_NO_REGS,
                                            QEMU_PLUGIN_MEM_RW, NULL);

        /* Register callback on instruction */
        qemu_plugin_register_vcpu_insn_exec_cb(insn, vcpu_insn_exec,
                                                QEMU_PLUGIN_CB_NO_REGS, (void*) trace_insn);

    }

}

static void plugin_init(void)
{
    
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    plugin_user_exit();
}

extern "C" {

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{

    system_emulation = info->system_emulation; 

    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_autofree char **tokens = g_strsplit(opt, "=", 2);
        if(g_strcmp0(tokens[0], "fastforward") == 0) {
            fast_forward_insn_counts = atoi(tokens[1]);
            enable_fastforward = true;
        }
        else if(g_strcmp0(tokens[0], "maxinsns") == 0) {
            max_insn_counts = atoi(tokens[1]);
            enable_roi = true;
        }
        else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    plugin_init();
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}


}