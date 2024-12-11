#include <assert.h>
#include <stdio.h>
#include <stdbool.h>

#include "panda/plugin.h"
#include "panda/panda_api.h"
#include "panda/common.h"
#include "sysemu/sysemu.h"

// for map_memory
#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "qapi/error.h"
#include "migration/vmstate.h"

// for panda_{set/get}_library_mode
#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"

// call main_aux and run everything up to and including panda_callbacks_after_machine_init
int panda_init(int argc, char **argv, char **envp) {
    qemu_init(argc, argv);
    return 0;
}

// extern void pandalog_cc_init_write(const char * fname);
int panda_in_main_loop;

// vl.c
extern char *panda_snap_name;
// extern bool panda_aborted;

int panda_run(void) {
    int status = 0;
    panda_in_main_loop = 1;
    status = qemu_main_loop();
    panda_in_main_loop = 0;
    qemu_cleanup(status);
    return status;
}

void panda_stop(int code) {
    // default of 4 = run_state_paused
    vm_stop(code);
}

// void panda_cont(void) {
// //    printf ("panda_api: cont cpu\n");
//     panda_exit_loop = false; // is this unnecessary?
//     vm_start();
// }

// int panda_delvm(char *snapshot_name) {
//     delvm_name(snapshot_name);
//     return 1;
// }

// void panda_start_pandalog(const char * name) {
//     pandalog = 1;
//     pandalog_cc_init_write(name);
//     printf ("pandalogging to [%s]\n", name);
// }

// int panda_revert(char *snapshot_name) {
//     int ret = load_vmstate(snapshot_name);
// //    printf ("Got back load_vmstate ret=%d\n", ret);
//     return ret;
// }

// void panda_reset(void) {
//     qemu_system_reset_request();
// }

// int panda_snap(char *snapshot_name) {
//     return save_vmstate(NULL, snapshot_name);
// }

// void panda_cleanup_record(void){
//     if(rr_in_record()){
//         rr_do_end_record();
//     }
// }

// int panda_finish(void) {
//     return main_aux(0, 0, 0, PANDA_FINISH);
// }

// bool panda_was_aborted(void) {
//   return panda_aborted;
// }

// extern const char *qemu_file;

// void panda_set_qemu_path(char* filepath) {
//     // *copy* filepath into a new buffer, also free & update qemu_file
//     if (qemu_file != NULL)
//       free((void*)qemu_file);

//     qemu_file=strdup(filepath);
// }

int panda_init_plugin(char *plugin_name, char **plugin_args, uint32_t num_args) {
    for (uint32_t i=0; i<num_args; i++)
        panda_add_arg(plugin_name, plugin_args[i]);
    char *plugin_path = panda_plugin_path((const char *) plugin_name);
    return panda_load_plugin(plugin_path, plugin_name);
}


// panda_cb is defined in callbacks/cb-defs.h
void panda_register_callback_helper(void *plugin, panda_cb_type type, panda_cb* cb) {
	panda_cb cb_copy;
	memcpy(&cb_copy,cb, sizeof(panda_cb));
	panda_register_callback(plugin, type, cb_copy);
}

void panda_enable_callback_helper(void *plugin, panda_cb_type type, panda_cb* cb) {
	panda_cb cb_copy;
	memcpy(&cb_copy,cb, sizeof(panda_cb));
	panda_enable_callback(plugin, type, cb_copy);
}

void panda_disable_callback_helper(void *plugin, panda_cb_type type, panda_cb* cb) {
	panda_cb cb_copy;
	memcpy(&cb_copy,cb, sizeof(panda_cb));
	panda_disable_callback(plugin, type, cb_copy);
}

//int panda_replay(char *replay_name) -> Now use panda_replay_being(char * replay_name)

// uint64_t rr_get_guest_instr_count_external(void){
// 	return rr_get_guest_instr_count();
// }

// XXX: why do we have these as _external wrappers instead of just using the real fns?
// XXX: b/c funcs called via wrapper are inlined, don't otherwise get exported. Unclear if inlining has any significat perf benefit.
int panda_virtual_memory_read_external(CPUState *env, target_ulong addr, char *buf, int len){
	return panda_virtual_memory_read(env, addr, (uint8_t*) buf, len);
}

int panda_virtual_memory_write_external(CPUState *env, target_ulong addr, char *buf, int len){
	return panda_virtual_memory_write(env, addr, (uint8_t*) buf, len);
}

#ifdef CONFIG_SOFTMMU
int panda_physical_memory_read_external(hwaddr addr, uint8_t *buf, int len){
	return panda_physical_memory_read(addr, buf, len);
}

int panda_physical_memory_write_external(hwaddr addr, uint8_t *buf, int len){
	return panda_physical_memory_write(addr,buf,len);
}
#endif

bool panda_in_kernel_external(const CPUState *cpu){
	return panda_in_kernel(cpu);
}

bool panda_in_kernel_mode_external(const CPUState *cpu){
	return panda_in_kernel_mode(cpu);
}

bool panda_in_kernel_code_linux_external(CPUState *cpu){
	return panda_in_kernel_code_linux(cpu);
}

target_ulong panda_current_sp_external(const CPUState *cpu){
	return panda_current_sp(cpu);
}

target_ulong panda_current_ksp_external(CPUState *cpu){
	return panda_current_ksp(cpu);
}

target_ulong panda_current_sp_masked_pagesize_external(const CPUState *cpu, target_ulong mask){
	return (panda_current_sp(cpu) & (~(mask+mask-1)));
}

#ifdef CONFIG_SOFTMMU
target_ulong panda_virt_to_phys_external(CPUState *cpu, target_ulong virt_addr) {
  return panda_virt_to_phys(cpu, virt_addr);
}
#endif

target_ulong panda_get_retval_external(const CPUState *cpu){
	return panda_get_retval(cpu);
}

// MemTxResult PandaPhysicalAddressToRamOffset_external(ram_addr_t* out, hwaddr addr, bool is_write){
//     return PandaPhysicalAddressToRamOffset(out, addr, is_write);
// }

void panda_setup_signal_handling(void (*f) (int, void*, void *))
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = (void(*)(int,siginfo_t*,void*))f;
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGINT,  &act, NULL);
    sigaction(SIGHUP,  &act, NULL);
    sigaction(SIGTERM, &act, NULL);
    panda_external_signal_handler = (void(*)(int,siginfo_t*,void*)) f;
}

// Taken from Avatar2's Configurable Machine - see hw/avatar/configurable_machine.c
// void map_memory(char* name, uint64_t size, uint64_t address) {
//     //const char * name; /// XXX const?
//     MemoryRegion * ram;
//     bool is_rom = false; // For now, only ram

//     // Get memory from system. XXX may be unsafe to run too early (before machine_init)
//     MemoryRegion *sysmem = get_system_memory();

//     // Make memory region and initialize
//     ram =  g_new(MemoryRegion, 1);
//     g_assert(ram);

//     if(!is_rom) {
//         memory_region_init_ram(ram, NULL, name, size, &error_fatal);
//     } else {
//         memory_region_init_rom(ram, NULL, name, size, &error_fatal);
//     }
//     vmstate_register_ram(ram, NULL);

//     printf("Adding memory region %s (size: 0x%"
//            PRIx64 ") at address 0x%" PRIx64 "\n", name, size, address);

//     // Add memory region to sysmem
//     memory_region_add_subregion(sysmem, address, ram);
// }

// CPUState* get_cpu(void) {
//   return first_cpu;
// }

// Get the length of a GArray list
unsigned long garray_len(GArray *list) {
    if (list == NULL)
        return 0;
    return list->len;
}

// For enabling library mode
// void _panda_set_library_mode(const bool b) {
//   panda_set_library_mode(b);
// }
