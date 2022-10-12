from pandare2 import Panda

panda = Panda(generic="arm",raw_monitor=True)

@panda.cb_vcpu_tb_trans
def vcpu_tb(id, tb):
    print(f"vcpu_tb in Python!!! {id} {tb}")

@panda.cb_atexit
def atexit(id, p):
    print("at exit")

# def init(id, info, argc, argv):
#     print("got to init")
#     panda.libpanda.qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb)
#     panda.libpanda.qemu_plugin_register_atexit_cb(id, atexit)
#     return 0 

print("entering main loop")
panda.run()
# exit = panda.libpanda.qemu_main_loop()
# panda.libpanda.qemu_cleanup()