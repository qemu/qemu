from pandare2 import Panda

panda = Panda(generic="arm",raw_monitor=True)

@panda.cb_vcpu_tb_trans
def vcpu_tb(id, tb):
    print(f"vcpu_tb in Python!!! {id} {tb}")



@panda.cb_atexit
def atexit(id, p):
    print("at exit")

print("entering main loop")
panda.run()