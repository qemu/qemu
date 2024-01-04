#include "kvmxx.hh"
#include "exception.hh"
#include "memmap.hh"
#include "identity.hh"
#include <boost/thread/thread.hpp>
#include <stdlib.h>
#include <stdio.h>

namespace {

void delay_loop(unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        asm volatile("pause");
    }
 }

void write_mem(volatile bool& running, volatile int* shared_var)
{
    while (running) {
        ++*shared_var;
        delay_loop(1000);
    }
}

void check_dirty_log(mem_slot& slot,
                     volatile bool& running,
                     volatile int* shared_var,
                     int& nr_fail)
{
    uint64_t shared_var_gpa = reinterpret_cast<uint64_t>(shared_var);
    slot.set_dirty_logging(true);
    slot.update_dirty_log();
    for (int i = 0; i < 10000000; ++i) {
        int sample1 = *shared_var;
        delay_loop(600);
        int sample2 = *shared_var;
        slot.update_dirty_log();
        if (!slot.is_dirty(shared_var_gpa) && sample1 != sample2) {
            ++nr_fail;
        }
    }
    running = false;
    slot.set_dirty_logging(false);
}

}

using boost::ref;
using std::tr1::bind;

int test_main(int ac, char **av)
{
    kvm::system sys;
    kvm::vm vm(sys);
    mem_map memmap(vm);
    void* logged_slot_virt;
    posix_memalign(&logged_slot_virt, 4096, 4096);
    int* shared_var = static_cast<int*>(logged_slot_virt);
    identity::hole hole(logged_slot_virt, 4096);
    identity::vm ident_vm(vm, memmap, hole);
    kvm::vcpu vcpu(vm, 0);
    bool running = true;
    int nr_fail = 0;
    mem_slot logged_slot(memmap,
                         reinterpret_cast<uintptr_t>(logged_slot_virt),
                         4096, logged_slot_virt);
    boost::thread host_poll_thread(check_dirty_log, ref(logged_slot),
                                   ref(running),
                                   ref(shared_var), ref(nr_fail));
    identity::vcpu guest_write_thread(vcpu,
                                      bind(write_mem,
                                           ref(running),
                                           ref(shared_var)));
    vcpu.run();
    host_poll_thread.join();
    printf("Dirty bitmap failures: %d\n", nr_fail);
    return nr_fail == 0 ? 0 : 1;
}

int main(int ac, char** av)
{
    return try_main(test_main, ac, av);
}
