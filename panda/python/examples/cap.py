from pandare2 import Panda
from sys import argv
import capstone

panda = Panda(generic="arm")

@panda.queue_blocking
def qb():
    from time import sleep
    sleep(2)
    panda.run_monitor_cmd("q")
    

md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)

@panda.cb_vcpu_tb_trans
def vcpu_tb(id, tb):
    pc = panda.libpanda.qemu_plugin_tb_vaddr(tb)
    n = panda.libpanda.qemu_plugin_tb_n_insns(tb)
    global i
    code = b""
    for j in range(n):
        insn = panda.libpanda.qemu_plugin_tb_get_insn(tb, j)
        data = panda.libpanda.qemu_plugin_insn_data(insn)
        size = panda.libpanda.qemu_plugin_insn_size(insn)
        dcast = panda.ffi.cast("uint8_t*", data)
        code += bytes(dcast[0:size])
    for io in md.disasm(code, pc):
        print("0x%x:\t%s\t%s" %(io.address, io.mnemonic, io.op_str))
    i += 1

print("entering main loop")
panda.run()
print("exiting")