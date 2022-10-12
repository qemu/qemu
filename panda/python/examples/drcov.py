from pandare2 import Panda
from sys import argv

panda = Panda(generic="arm")

i = 0
d = {}

outfile = "out"

header = ["DRCOV VERSION: 2\n",
                "DRCOV FLAVOR: drcov-64\n",
                "Module Table: version 2, count 1\n",
                "Columns: id, base, end, entry, path\n"]

@panda.queue_blocking
def qb():
    from time import sleep
    sleep(10)
    panda.run_monitor_cmd("q")

@panda.ffi.callback("void(unsigned int, void*)")
def vcpu_tb_exec(cpu_index, udata):
    i = panda.ffi.cast("int", udata)
    d[i]['exec'] = True

@panda.cb_vcpu_tb_trans
def vcpu_tb(id, tb):
    # print(f"vcpu_tb in Python!!! {id} {tb}")
    pc = panda.libpanda.qemu_plugin_tb_vaddr(tb)
    n = panda.libpanda.qemu_plugin_tb_n_insns(tb)
    
    global i
    d[i] = {'start': pc, 'mod_id': 0, 'exec': False, 'size': 0}
    
    for j in range(n):
        d[i]['size'] += panda.libpanda.qemu_plugin_insn_size(panda.libpanda.qemu_plugin_tb_get_insn(tb, j))
    # panda.cb_vcpu_tb_exec(vcpu_tb_exec, args=[tb, vcpu_tb_exec, panda.libpanda.QEMU_PLUGIN_CB_NO_REGS, panda.ffi.cast("void*", i)])
    panda.libpanda.qemu_plugin_register_vcpu_tb_exec_cb(tb, vcpu_tb_exec, panda.libpanda.QEMU_PLUGIN_CB_NO_REGS, panda.ffi.cast("void*", i))
    i += 1

# @panda.ffi.callback("void(qemu_plugin_id_t, void*)")
@panda.cb_atexit
def atexit(id, p):
    print("at exit")
    with open(outfile,"w") as f:
        path = panda.libpanda.qemu_plugin_path_to_binary()
        if path == panda.ffi.NULL:
            path = "?"
        start_code = panda.libpanda.qemu_plugin_start_code()
        end_code = panda.libpanda.qemu_plugin_end_code()
        entry = panda.libpanda.qemu_plugin_entry_code()
        for line in header:
            f.write(line)
        f.write(f"0, {start_code:#x}, {end_code:#x}, {entry:#x}, {path}\n")
        f.write(f"BB Table: {len(d)} entries\n")
        from struct import pack
        for block in d:
            if block['exec']:
                f.write(pack('I', block['start'])+pack('H', block['size'])+pack('H',block['mod_id']))


def init(id, info, argc, argv):
    print("got to init")
    panda.libpanda.qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb)
    panda.libpanda.qemu_plugin_register_atexit_cb(id, atexit,panda.ffi.NULL)
    return 0 

print("entering main loop")
panda.run()
print("exiting")