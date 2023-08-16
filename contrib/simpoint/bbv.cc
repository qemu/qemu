extern "C" {
#include <qemu/qemu-plugin.h>
}

#include <glib.h>
#include <iostream>
#include <unordered_map>
#include <memory>
#include <string>
#include <string_view>
#include <cassert>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

struct match_t {
    uint64_t start_addr;
    uint64_t insn_count;
};

bool operator==(const match_t& lhr, const match_t& rhs) {
    return (lhr.insn_count == rhs.insn_count) && (lhr.start_addr == rhs.start_addr);
}

namespace std{
    template<>
    struct hash<match_t> {
        std::size_t operator()(const match_t& m) const {
             return std::hash<uint64_t>()(m.start_addr) ^ std::hash<uint64_t>()(m.insn_count);
        }
    };
}

struct basic_block_t
{
    uint64_t id;
    uint64_t start_addr;
    uint64_t insn_count;
};

static uint64_t interval = 10000000;

static std::unordered_map<match_t,basic_block_t> basic_blocks;

static std::unordered_map<uint64_t,uint64_t> exec_bb_counts;

static uint64_t exec_insn_counts = 0;

static uint64_t exec_interval_counts = 0;

static uint64_t unique_id = 1;

static FILE* bbv_file;

static FILE* profile_file;

static void plugin_init(void)
{

    bbv_file = fopen("sp_profile.bbv", "w");
    profile_file = fopen("sp_profile.txt","w");
    if (bbv_file == NULL || profile_file == NULL) {
        qemu_plugin_outs("Error opening the file.\n");
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) report = g_string_new("");
    g_string_append_printf(report,"Interval : %ld\n", interval);
    g_string_append_printf(report,"Total instruction : %ld\n", exec_insn_counts);
    fprintf(profile_file,"%s",report->str);
    fclose(bbv_file);
    fclose(profile_file);
}

static void dump_bbv()
{
    g_autoptr(GString) report = g_string_new("T");
    for(auto it = exec_bb_counts.begin(); it != exec_bb_counts.end(); it++){
        g_string_append_printf(report,":%ld:%ld ",it->first,it->second);
    }
    g_string_append_printf(report,"\n");
    fprintf(bbv_file,"%s",report->str);
}

static void vcpu_tb_exec(unsigned int cpu_index, void *udata)
{

    basic_block_t* exec_bb = (basic_block_t*) udata;

    auto it_count = exec_bb_counts.find(exec_bb->id);

    if(it_count != exec_bb_counts.end()){
        it_count->second++;
    } else {
        exec_bb_counts.emplace(exec_bb->id,1);
    }

    exec_interval_counts += exec_bb->insn_count;
    exec_insn_counts += exec_bb->insn_count;

    if(exec_interval_counts > interval){
        dump_bbv();
        exec_bb_counts.clear();
        exec_interval_counts = 0;
    }

}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    uint64_t start_addr = qemu_plugin_tb_vaddr(tb);
    uint64_t insn_count = qemu_plugin_tb_n_insns(tb);
    match_t match = {start_addr,insn_count};
    basic_block_t new_bb = {unique_id,start_addr,insn_count};

    if(basic_blocks.find(match) == basic_blocks.end()){
        basic_blocks.emplace(match,new_bb);
        unique_id++;
    } else{
        assert(basic_blocks.find(match)->second.start_addr == start_addr);
        assert(basic_blocks.find(match)->second.insn_count == insn_count);
    }
    
    qemu_plugin_register_vcpu_tb_exec_cb(tb, vcpu_tb_exec,
                                             QEMU_PLUGIN_CB_NO_REGS,
                                             (void *)&basic_blocks.find(match)->second);

    
}




QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{

    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_autofree char **tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "interval") == 0) {
            interval = atoi(tokens[1]);
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
