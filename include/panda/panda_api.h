#ifndef __PANDA_API_H__
#define __PANDA_API_H__

// Functions and variables exclusively used by API consumers.
// Nothing in core-panda should need to include this file

// BEGIN_PYPANDA_NEEDS_THIS -- do not delete this comment bc pypanda
// api autogen needs it.  And don't put any compiler directives
// between this and END_PYPANDA_NEEDS_THIS except includes of other
// files in this directory that contain subsections like this one.

// from panda_api.c

/**
 * panda_init() - Initialize panda guest.
 * @argc: number of command line args
 * @argv: command line args
 * @envp: environment variables
 * 
 * Initialize panda emulator with command line and environment
 * variables.  These may have come from running cmd-line panda or may
 * have been crafted by something using panda as a library, e.g., 
 * by the PYTHON panda interface.
 * 
 * Return: always 0
 */
int panda_init(int argc, char **argv, char **envp);


/**
 * panda_run() - Give control to panda to emulate guest.
 *
 * Allow panda to emulate guest, executing any registered callbacks as
 * well as loaded plugin code. This will return when panda emulation
 * exits its "main loop" and would normally end as a program.
 *
 * Return: always 0
 */
int panda_run(void);


/**
 * panda_stop() - Pauses the guest being emulated.
 * @code: New state to assign to guest cpu.
 * 
 * This is only used to pause emulation, with code=4.
 * XXX: Perhaps rename and un-expose code arg?
 */
void panda_stop(int code);


/** 
 * panda_cont() - Continue guest after pause.
 * 
 * Resume after panda_stop.
 */
void panda_cont(void);


// Not an API function.
void _panda_set_library_mode(const bool);


/**
 * panda_start_pandalog() - Turn on pandalogging.
 * @name: Filename for logging.
 * 
 * Pandalogging will be enabled from this point on and will be output
 * to the file indicated.
 */
void panda_start_pandalog(const char *name);


/**
 * panda_snap() - Take a guest snapshot.
 * @name: Name that will be assigned to the snapshot.
 * 
 * Take a snapshot of guest state which includes RAM, registers, and
 * some device state including hard drive and assign it the name
 * provided, storing all this in the current qcow.
 * 
 * Return: The value returned by the internal Qemu snapshot taking
 * function, which returns 0 on success.
 */
int panda_snap(char *name);


/**
 * panda_delvm() - Delete a guest snapshot.
 * @name: Name of snapshot to delete.
 * 
 * Delete a guest snapshot by name from the current qcow.
 *
 * Return: not used
 */
int panda_delvm(char *name);


/**
 * panda_revert() - Revert to a guest snapshot.
 * @name: The name of the snapshot to revert to.
 *
 * Pause emulation and restore to a snapshot. 
 * 
 * Return: The value returned by internal Qemu revert function which
 * is 0 on success but otherwise ... there are various errors.
 */
int panda_revert(char *name);


/**
 * panda_reset() - Request reboot of guest.
 * 
 * Think ctrl-alt-delete. This is a request, so it won't happen
 * immediately.
 */
void panda_reset(void);


/**
 * panda_finish() - Stop emulating guest and end analysis.
 *
 * XXX This doesnt appear to be used by pypanda. Perhaps it is vestigal?
 */
int panda_finish(void);


/**
 * panda_was_aborted() - Returns true if abort requested.
 *
 * This will be true, e.g., if someone hit ctrl-c or called
 * `panda_reset` to kill analysis but that is still pending.
 *
 * Return: true/false
 */
bool panda_was_aborted(void);


/**
 * panda_set_qemu_path() - Sets path to "qemu" binary, needed internally.
 * @filepath: Full path to qemu (actually panda).
 * 
 * XXX this looks like its not actually used anywhere?
 */  
void panda_set_qemu_path(char* filepath);


/**
 * panda_init_plugin() - Initialize a plugin by name.
 * @plugin_name: The name of the plugin.
 * @plugin_args: The array of string arguments.
 * @num_args: The number of arguments.
 * 
 * Initialize this plugin with these arguments, which sets the
 * arguments as if they had come in on the panda commandline and then
 * loads the plugin. 
 * 
 * XXX: Rename to load_plugin ? 
 */
int panda_init_plugin(char *plugin_name, char **plugin_args, uint32_t num_args);


/**
 * panda_register_callback_helper() - Register a callback function.
 * @plugin: Pointer to plugin.
 * @type: Type of callback, indicating when cb function will run.
 * @cb: The callback function itself and other info.
 *
 * This function can be used to register a callback to run in
 * panda. To call it, you will need a pointer to a plugin as well as a
 * pointer to a fully-formed panda_cb object. The plugin
 * pointer can be fake but should be a handle that uniquely distinguishes plugins.
 * 
 * type is a number. See typedef panda_cb_type.
 * cb is a pointer to a struct. See typedef panda_cb.
 */
void panda_register_callback_helper(void* plugin, panda_cb_type type, panda_cb* cb);


/**
 * panda_enable_callback_helper() - Enable a callback.
 * @plugin: Pointer to plugin to which the callback belongs.
 * @type: Type of callback, indicating when cb function will run.
 * @cb: The callback function itself and other info.
 * 
 * This function can be used to enable a previously registered
 * callback to run in panda. To call it, you will need a pointer to
 * the plugin that owns the callback as well as a pointer to the
 * panda_cb object which contains the callback. 
 *  
 * type is a number. See typedef panda_cb_type.
 * cb is a pointer to a struct. See typedef panda_cb.
 */
void panda_enable_callback_helper(void *plugin, panda_cb_type type, panda_cb* cb);


/**
 * panda_disable_callback_helper() - Disable a callback.
 * @plugin: Pointer to plugin to which the callback belongs.
 * @type: Type of callback, indicating when cb function will run.
 * @cb: The callback function itself and other info.
 * 
 * This function can be used to disable a previously registered
 * callback to run in panda. To call it, you will need a pointer to
 * the plugin that owns the callback as well as a pointer to the
 * panda_cb object which contains the callback. 
 *  
 * type is a number. See typedef panda_cb_type.
 * cb is a pointer to a struct. See typedef panda_cb.
 */
void panda_disable_callback_helper(void *plugin, panda_cb_type type, panda_cb* cb);


/**
 * rr_get_guest_instr_count_external() - Get instruction count for replay.
 * 
 * This will only return the number of guest instruction emulated
 * since replay began.
 *
 * Unclear what this returns if not in replay mode.
 *
 *
 * Return: a uint64_t, the instruction count.
*/
uint64_t rr_get_guest_instr_count_external(void);


/**
 * panda_virtual_memory_read_external() - Copy data from guest (virtual) memory into host buffer.
 * @cpu: Cpu state.
 * @addr: Guest virtual address of start of desired read.
 * @buf: Host pointer to a buffer into which data will be copied from guest.
 * @len: Number of bytes to copy.
 * 
 * Return:
 * * 0      - Read succeeded
 * * -1     - An error 
 */ 
int panda_virtual_memory_read_external(CPUState *cpu, target_ulong addr, char *buf, int len);

/**
 * panda_virtual_memory_write_external() - Copy data from host buffer into guest (virtual) memory.
 * @cpu: Cpu state.
 * @addr: Guest virtual address of start of desired write.
 * @buf: Host pointer to a buffer from which data will be copied into guest.
 * @len: Number of bytes to copy.
 * 
 * Return:
 * * 0      - Write succeeded
 * * -1     - An error 
 */ 
int panda_virtual_memory_write_external(CPUState *cpu, target_ulong addr, char *buf, int len);


/**
 * panda_physical_memory_read_external() - Copy data from guest (physical) memory into host buffer.
 * @addr: Guest physical address of start of read.
 * @buf: Host pointer to a buffer into which data will be copied from guest.
 * @len: Number of bytes to copy.
 * 
 * Return: 
 * * MEMTX_OK      - Read succeeded
 * * MEMTX_ERROR   - An error
 */
int panda_physical_memory_read_external(hwaddr addr, uint8_t *buf, int len);


/**
 * panda_physical_memory_write_external() - Copy data from host buffer into guest (physical)memory.
 * @addr: Guest physical address of start of desired write.
 * @buf: Host pointer to a buffer from which data will be copied into guest.
 * @len: Number of bytes to copy.
 * 
 * Return: 
 * * MEMTX_OK      - Write succeeded
 * * MEMTX_ERROR   - An error
 */
int panda_physical_memory_write_external(hwaddr addr, uint8_t *buf, int len);


/**
 * panda_get_retval_external() - Get return value for function.
 * @cpu: Cpu state.
 *
 * Platform-independent abstraction for retrieving a call return
 * value. This function still has to be called in the proper context
 * to retrieve a meaningful value, such as just after a RET
 * instruction under x86.
 *
 * Return: Guest return value.
 */
target_ulong panda_get_retval_external(const CPUState *cpu);

#ifdef CONFIG_SOFTMMU
/**
 * PandaPhysicalAddressToRamOffset_external() - Translate guest physical to ram offset.
 * 
*/
MemTxResult PandaPhysicalAddressToRamOffset_external(ram_addr_t* out, hwaddr addr, bool is_write);
#endif


/**
 * panda_in_kernel_external() - Determine if guest is in kernel.
 * @cpu: Cpu state.
 *
 * Determines if guest is currently executing in kernel mode, e.g. execution privilege level.
 * 
 * Return: True if in kernel, false otherwise.
 */
bool panda_in_kernel_external(const CPUState *cpu);


/**
 * panda_in_kernel_mode_external() - Determine if guest is in kernel.
 * @cpu: Cpu state.
 *
 * Determines if guest is currently executing in kernel mode, e.g. execution privilege level.
 *
 * XXX Duplicate of panda_in_kernel_external? 
 *
 * Return: True if in kernel, false otherwise.
 */
bool panda_in_kernel_mode_external(const CPUState *cpu);


/**
 * panda_in_kernel_code_linux_external() - Determine if current pc is kernel code.
 * @cpu: Cpu state.
 *
 * Determines if guest is currently executing kernelspace code,
 * regardless of privilege level.  Necessary because there's a small
 * bit of kernelspace code that runs AFTER a switch to usermode
 * privileges. Therefore, certain analysis logic can't rely on
 * panda_in_kernel_mode() alone. 
 *
 * Return: true if pc is in kernel, false otherwise.
 */
bool panda_in_kernel_code_linux_external(CPUState *cpu);


/**
 * panda_current_ksp_external() - Get guest kernel stack pointer.
 * @cpu: Cpu state.
 * 
 * Return: Guest kernel stack pointer value.
 */
target_ulong panda_current_ksp_external(CPUState *cpu);


/**
 * panda_current_sp_external() - Get current guest stack pointer.
 * @cpu: Cpu state.
 * 
 * Return: Returns guest stack pointer.
 */
target_ulong panda_current_sp_external(const CPUState *cpu);


/* not kernel-doc.  This doesnt seem like an API fn.
 * 
 * panda_current_sp_masked_pagesize_external() - Get current sp masked somehow.
 * @cpu: Cpu state.
 * @pagesize: Page size.
 *
 * A little mysterious.  We ask panda to get current sp and do some dodgy math with it. 
 *
 * Return: Some weird number.
 */
target_ulong panda_current_sp_masked_pagesize_external(const CPUState *cpu, target_ulong pagesize);


/**
 * panda_virt_to_phys_external() - Translate guest virtual to physical address.
 * @cpu: Cpu state.
 * @addr: Guest virtual address.
 *
 * This conversion will fail if asked about a virtual address that is
 * not currently mapped to a physical one in the guest.
 *
 * Return: A guest physical address.
 */
target_ulong panda_virt_to_phys_external(CPUState *cpu, target_ulong addr);


/**
 * panda_setup_signal_handling() - Provide panda with a function to be called on certain signals.
 * @sigfun: The function to call on signal.
 * 
 * This function will be called on any of {SIGINT, SIGHUP, SIGTERM}.
 */
void panda_setup_signal_handling(void (*sigfun) (int, void*, void *));


/**
 * map_memory() - Add a region to the memory map.
 * @name: The name of the region.
 * @size: Size of the region, in bytes.
 * @address: Start of the region.
 *
 * If setting up an environment to run code or rehost some embedded
 * system, this function can be used to set up regions in the
 * machine's memory map.  RAM only, at present.
 *
 * XXX: Rename as panda_map_memory? 
 */
void map_memory(char* name, uint64_t size, uint64_t address);

// REDEFINITIONS below here from monitor.h

/**
 * panda_init_monitor() - Create a monitor for panda.
 *
 * Creates a monitor panda can easily interact with.
 */
void panda_init_monitor(void); // Redefinition from monitor.h

/**
 * panda_monitor_run() - Run a command in the panda monitor and collect response.
 * @buf: The command.
 * 
 * Run this command as if it were typed into the qemu monitor, and
 * return what output would have been printed to the monitor.
 *
 * NB: Some commands may cause spinloops.
 *
 * Return: A string, the output of the command.
 */
char* panda_monitor_run(char* buf);// Redefinition from monitor.h


// Map a region of memory in the guest. WIP
//int panda_map_physical_mem(target_ulong addr, int len);


/**
 * get_cpu() - Get cpu state object.
 *
 * Use this to obtain a pointer to a cpu object needed for other API functions.
 * 
 * Return: host pointer to cpu.
 */
CPUState* get_cpu(void);


unsigned long garray_len(GArray *list);

/**
 * panda_cleanup_record() - End recording.
 * 
 * This function ends recording, if that is currently in progress.
 *
 * XXX: Rename to something more like panda_end_record?  Also, where is begin_record then?
 */
void panda_cleanup_record(void);


// END_PYPANDA_NEEDS_THIS -- do not delete this comment!

// don't expose to API  because we don't want to add siginfo_t understanding
// set to true if panda_setup_signal_handling is called
void (*panda_external_signal_handler)(int, siginfo_t*,void*) = NULL;

#endif
