/* PANDABEGINCOMMENT
 *
 * Authors:
 *  Tim Leek               tleek@ll.mit.edu
 *  Ryan Whelan            rwhelan@ll.mit.edu
 *  Joshua Hodosh          josh.hodosh@ll.mit.edu
 *  Michael Zhivich        mzhivich@ll.mit.edu
 *  Brendan Dolan-Gavitt   brendandg@gatech.edu
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
PANDAENDCOMMENT */
#pragma once
#include "panda/debug.h"
#include "panda/cheaders.h"

#define MAX_PANDA_PLUGINS 16
#define MAX_PANDA_PLUGIN_ARGS 32

#include "panda/callbacks/cb-defs.h"

#ifdef __cplusplus
extern "C" {
#endif

// BEGIN_PYPANDA_NEEDS_THIS -- do not delete this comment bc pypanda
// api autogen needs it.  And don't put any compiler directives
// between this and END_PYPANDA_NEEDS_THIS except includes of other
// files in this directory that contain subsections like this one.

// Doubly linked list that stores a callback, along with its owner
typedef struct _panda_cb_list panda_cb_list;
struct _panda_cb_list {
    panda_cb_with_context entry;
    void *owner;
    panda_cb_list *next;
    panda_cb_list *prev;
    bool enabled;
    void* context;
};
panda_cb_list *panda_cb_list_next(panda_cb_list *plist);


/**
 * panda_enable_plugin() - Enable this plugin.
 * @plugin: Pointer to the plugin (handle).
 *
 * Mark plugin as enabled so that its callbacks will run in future.
 */
void panda_enable_plugin(void *plugin);


/**
 * panda_disable_plugin() - Disable this plugin.
 * @plugin: Pointer to the plugin (handle).
 *
 * Mark plugin as disabled so that its callbacks will NOT run in future.
 */
void panda_disable_plugin(void *plugin);


// Structure to store metadata about a plugin
typedef struct panda_plugin {
    char *name;            // Plugin name: basename(filename)
    void *plugin;          // Handle to the plugin (for use with dlsym())
    bool unload;           // When true, unload plugin when safe
    bool exported_symbols; // True if plugin dlopened with RTLD_GLOBAL
} panda_plugin;


// XXX Not sure what this is exactly or howt to doc.  Is it really an API fn?
// If so, that's concerning...
panda_cb_with_context panda_get_cb_trampoline(panda_cb_type type);


/**
 * panda_register_callback() - Register a callback for a plugin, and enable it.
 * @plugin: Pointer to plugin.
 * @type: Type of callback, indicating when cb function will run.
 * @cb: The callback function itself and other info.
 *
 * This function will register a callback to run in panda and is
 * typically called from plugin code.  
 *
 * The order of callback registration will determine the order in which
 * callbacks of the same type will be invoked.
 *
 * NB: Registering a callback function twice from the same plugin will
 * trigger an assertion error.
 * 
 * type is number. See typedef panda_cb_type.
 * cb is a pointer to a struct. See typedef panda_cb.
 */
void panda_register_callback(void *plugin, panda_cb_type type, panda_cb cb);


/**
 * panda_register_callback_with_context() - Register a callback for a plugin with context.
 * @plugin: Pointer to plugin.
 * @type: Type of callback, indicating when cb function will run.
 * @cb: The callback function itself and other info.
 * @context: Pointer to context.
 *
 * Same as panda_register_callback, but with context.
 */
void panda_register_callback_with_context(void *plugin, panda_cb_type type, panda_cb_with_context cb, void* context);


/**
 * panda_disable_callback() - Disable callback for this plugin from running.
 * @plugin: Pointer to plugin.
 * @type: Type of callback, indicating when cb function will run.
 * @cb: The callback function itself and other info.
 * 
 * Mark this callback as disabled so that it stops running. 
 * 
 * NB: enable/disable are faster than register/unregister since they
 * set a flag rather than adding/removing something from a list.
 */
void panda_disable_callback(void *plugin, panda_cb_type type, panda_cb cb);


/**
 * panda_disable_callback_with_context() - Disable callback for this plugin from running (with context).
 * @plugin: Pointer to plugin.
 * @type: Type of callback, indicating when cb function will run.
 * @cb: The callback function itself and other info.
 * @context: Pointer to context.
 * 
 * Same as padna_disable_callback, but with context.
 */
void panda_disable_callback_with_context(void *plugin, panda_cb_type type, panda_cb_with_context cb, void* context);


/**
 * panda_enable_callback() - Enable callback for this plugin so that it can run.
 * @plugin: Pointer to plugin.
 * @type: Type of callback, indicating when cb function will run.
 * @cb: The callback function itself and other info.
 * 
 * Mark this callback as enabled so that it will run from now on. 
 *
 * NB: enable/disable are faster than register/unregister since they
 * set a flag rather than adding/removing something from a list.
 */
void panda_enable_callback(void *plugin, panda_cb_type type, panda_cb cb);


/**
 * panda_enable_callback_with_context() - Enable this callback for this plugin so that it can run (with context)/
 * @plugin: Pointer to plugin.
 * @type: Type of callback, indicating when cb function will run.
 * @cb: The callback function itself and other info.
 * @context: Pointer to context.
 * 
 * Same as panda_enable_callback, but with context.
 */
void panda_enable_callback_with_context(void *plugin, panda_cb_type type, panda_cb_with_context cb, void* context);


/**
 * panda_unregister_callbacks() - Unregister all callbacks for this plugin.
 * @plugin: Pointer to plugin.
 * 
 */
void panda_unregister_callbacks(void *plugin);


/**
 * panda_load_plugin() - Load this plugin into panda.
 * @filename: The path to the shared object plugin code.
 * @plugin_name: The name of the plugin.
 *
 * This will load the code for this plugin and run its init_plugin function.
 *
 * Return: True if success, False otherwise.
 */
bool panda_load_plugin(const char *filename, const char *plugin_name);

/**
 * panda_add_arg() - Add an argument to those for a plugin.
 * @plugin_name: The name of the plugin.
 * @plugin_arg: The plugin argument, unparsed.
 * 
 * Return: Always returns True
 */
bool panda_add_arg(const char *plugin_name, const char *plugin_arg);

/**
 * panda_get_plugin_by_name() - Returns pointer to the plugin of this name.
 * @name: The name of the desired plugin.
 *
 * Return: Pointer to plugin (handle)
 */
void * panda_get_plugin_by_name(const char *name);

/**
 * panda_unload_plugin_by_name() - Unload plugin.
 * @name: The name of the plugin to unload.
 */
void panda_unload_plugin_by_name(const char* name);

// Not an API function.
void panda_do_unload_plugin(int index);

/**
 * panda_unload_plugin() - Unload plugin.
 * @plugin: Pointer to the plugin (handle) to unload.
 */
void panda_unload_plugin(void *plugin);

// Not an API function
void panda_unload_plugin_idx(int idx);

/**
 * panda_unload_plugins() - Unload all the plugins currently loaded.
 */
void panda_unload_plugins(void);

extern bool panda_update_pc;
extern bool panda_use_memcb;
extern panda_cb_list *panda_cbs[PANDA_CB_LAST];
extern bool panda_tb_chaining;

// this stuff is used by the new qemu cmd-line arg '-os os_name'
typedef enum OSFamilyEnum { OS_UNKNOWN, OS_WINDOWS, OS_LINUX, OS_FREEBSD } PandaOsFamily;

// these are set in panda/src/common.c via call to panda_set_os_name(os_name)
extern char *panda_os_name;           // the full name of the os, as provided by the user
extern char *panda_os_family;         // parsed os family
extern char *panda_os_variant;        // parsed os variant
extern uint32_t panda_os_bits;        // parsed os bits
extern PandaOsFamily panda_os_familyno; // numeric identifier for family



/* Internal callback functions that plugins shouldn't use. These unset the flag when called so must be handled */
bool panda_break_exec(void);
bool panda_flush_tb(void);

/* Regular functions plugins should use */

/**
 * panda_do_flush_tb() - Request flush of translation block cache.
 *
 * Qemu's emulation operates on basic blocks of translated code (a
 * basic block is a sequence of instructions without control flow).
 * These blocks are cached which means if an analysis wants to change
 * how translation injects instrumentation, then the cache should be
 * flushed so that new instrumentation can appear.
 */
void panda_do_flush_tb(void);

/**
 * panda_do_break_exec() - Request break out of emulation loop.
 *
 * Qemu emulates using a cache (see panda_do_flush_tb) but also tends
 * to mostly sit in a tight loop executing basic blocks in succession.
 * Sometimes an anlysis will want to force an exit from that loop,
 * which causes interrupts and device housekeeping code to run.
 */   
void panda_do_break_exec(void);

/**
 * panda_enable_precise_pc() - Turn on accurate PC mode.
 *
 * Qemu does not update the program counter in the middle of a basic
 * block. However, for many analyses, we might want to know the PC at
 * the instruction level, accurately. This enables a mode in which
 * panda updates a shadow PC to serve that purpose.
 */
void panda_enable_precise_pc(void);

/**
 * panda_disable_precise_pc() - Turn off accurate PC mode.
 */
void panda_disable_precise_pc(void);

/** 
 * panda_enable_memcb() - Turn on memory callbacks.
 *
 * Callbacks on LD/ST are expensive in panda. If required, they must
 * be enabled explicitly using this function which swaps out the
 * helper functions used by qemu for loads and stores.
 */
void panda_enable_memcb(void);

/** 
 * panda_disable_memcb() - Turn on memory callbacks.
 */
void panda_disable_memcb(void);

/**
 * panda_enable_llvm() - Turn on LLVM translation-mediated emulation.
 *
 * Analyses involving all or most machine instructions on many
 * architectures are well served by translating emulated code to a
 * simple, common intermediate language first. This function enables a
 * mode in which every basic block of emualted code is translated from
 * TCG to LLVM which can be analyzed or instrumented via LLVM passes.
 * In addition, a benefit of using LLVM, even C-implementations of
 * functionality in "helpers" can additionally be subject to analysis
 * if they compiled with CLANG and thus their code made available for
 * analysis as LLVM.
 * 
 * NB: Beware that LLVM emulation mode is slow. The resulting object
 * code is highly un-optimized.
 */
void panda_enable_llvm(void);

/**
 * panda_enable_llvm_no_exec() - Turn on LLVM translation for inspection.
 *
 * Enable translation of basic blocks to LLVM and make this available
 * for perusal via `-d llvm_ir`.  Whole-system emulation will continue
 * to use its normal faster emultion.
 */
void panda_enable_llvm_no_exec(void);

/**
 * panda_disable_llvm() - Turn off LLVM translation-mediated emulation.
 */
void panda_disable_llvm(void);

// Not API functions.
void panda_enable_llvm_helpers(void);
void panda_disable_llvm_helpers(void);
int panda_write_current_llvm_bitcode_to_file(const char* path);
uintptr_t panda_get_current_llvm_module(void);

/**
 * panda_disable_tb_chaining() - Turn off translation block chaining.
 * 
 * Qemu typically emulates by *chaining* the execution of emulated
 * basic blocks of guest code, meaning the execution of one follows
 * another without returning control to the emulation loop. This is
 * fast because qemu just lets translated code execute, one block
 * after another.
 * 
 * For some analyses this is problematic and so this function disables
 * the behavior, meaning that emulation of a basic block of guest code
 * always returns control to the main emulation loop after it is done.
 */
void panda_disable_tb_chaining(void);

/**
 * panda_enable_tb_chaining() - Turn on translation block chaining.
 *
 * Turns on the chaining behavior described in panda_disable_tb_chaining. 
 */
void panda_enable_tb_chaining(void);

/**
 * panda_memsavep() - Save RAM to a file.
 * @file: An open and writeable file pointer.
 *
 * This function should simply copy the contents of RAM to the
 * provided file pointer. One possible use is to provide this file to
 * memory forensic tools like Volatility.
 */
void panda_memsavep(FILE *file);

int panda_vm_quit(void);

// Not and API function.
char* panda_get_rr_name(void);

// Struct for holding a parsed key/value pair from
// a -panda-arg plugin:key=value style argument.
typedef struct panda_arg {
    char *argptr;   // For internal use only
    char *key;      // Pointer to the key string
    char *value;    // Pointer to the value string
} panda_arg;

typedef struct panda_arg_list {
    int nargs;
    panda_arg *list;
    char *plugin_name;
} panda_arg_list;


/**
 * panda_get_args() - Parse arguments for a plugin into panda_arg_list.
 * @plugin_name: The plugin name.
 *
 * This function is used in a plugin's initialization to parse
 * arguments to a plugin into a panda_arg_list. Arguments are
 * key/value string pairs.
 *
 * Return: pointer to panda_arg_list.
 */
panda_arg_list *panda_get_args(const char *plugin_name);

/** 
 * panda_free_args() - Free plugin arguments from a panda_arg_list.
 * @args: Pointer to panda_arg_list struct.
 *
 * Use this to free the memory allocated by panda_get_args.
 */
void panda_free_args(panda_arg_list *args);

/** 
 * panda_parse_ulong() - Get value corresponding to this plugin arg as a ulong, with default.
 * @args: The previously parsed panda_arg_list.
 * @argname: The name of the argument in args.
 * @defval: A default value.
 *
 * Look through the arguments in args, and if any have name argname,
 * translate the associated value into a ulong and return it.  If no
 * such name is found, use the provided default.
 *
 * Return: a ulong from args or default.
 */
target_ulong panda_parse_ulong(panda_arg_list *args, const char *argname, target_ulong defval);

/** 
 * panda_parse_ulong_req() - Get required value corresponding to this plugin arg as a ulong.
 * @args: The previously parsed panda_arg_list.
 * @argname: The name of the argument in args.
 * @help: Help text.
 *
 * Look through the arguments in args, and if any have name argname,
 * translate the associated value into a ulong and return it. As this
 * argument is required, if no such name is found, plugin load should
 * fail. 
 *
 * Return: a ulong from args.
 */
target_ulong panda_parse_ulong_req(panda_arg_list *args, const char *argname, const char *help);

/** 
 * panda_parse_ulong_opt() - Get optional value corresponding to this plugin arg as a ulong.
 * @args: The previously parsed panda_arg_list.
 * @argname: The name of the argument in args.
 * @defval: A default value.
 * @help: Help text.
 *
 * Look through the arguments in args, and if any have name argname,
 * translate the associated value into a ulong and return it.  If no
 * such name is found, use the default.
 *
 * Return: a ulong from args.
 */
target_ulong panda_parse_ulong_opt(panda_arg_list *args, const char *argname, target_ulong defval, const char *help);

/** 
 * panda_parse_uint32() - Get value corresponding to this plugin arg as a uint32, with default.
 * @args: The previously parsed panda_arg_list.
 * @argname: The name of the argument in args.
 * @defval: A default value.
 *
 * Look through the arguments in args, and if any have name argname,
 * translate the associated value into a uint32 and return it.  If no
 * such name is found, use the provided default.
 *
 * Return: a uint32 from args or default.
 */                     
uint32_t panda_parse_uint32(panda_arg_list *args, const char *argname, uint32_t defval);

/** 
 * panda_parse_uint32_req() - Get required value corresponding to this plugin arg as a uint32.
 * @args: The previously parsed panda_arg_list.
 * @argname: The name of the argument in args.
 * @help: Help text.
 *
 * Look through the arguments in args, and if any have name argname,
 * translate the associated value into a uint32 and return it. As this
 * argument is required, if no such name is found, plugin load should
 * fail.
 *
 * Return: a uint32 from args.
 */
uint32_t panda_parse_uint32_req(panda_arg_list *args, const char *argname, const char *help);

/** 
 * panda_parse_uint32_opt() - Get optional value corresponding to this plugin arg as a uint32.
 * @args: The previously parsed panda_arg_list.
 * @argname: The name of the argument in args.
 * @defval: A default value.
 * @help: Help text.
 *
 * Look through the arguments in args, and if any have name argname,
 * translate the associated value into a uint32 and return it.  If no
 * such name is found, use the default.
 *
 * Return: a uint32 from args.
 */
uint32_t panda_parse_uint32_opt(panda_arg_list *args, const char *argname, uint32_t defval, const char *help);

/** 
 * panda_parse_uint64() - Get value corresponding to this plugin arg as a uint64, with default.
 * @args: The previously parsed panda_arg_list.
 * @argname: The name of the argument in args.
 * @defval: A default value.
 *
 * Look through the arguments in args, and if any have name argname,
 * translate the associated value into a uint64 and return it.  If no
 * such name is found, use the provided default.
 *
 * Return: a uint64 from args or default.
 */                     
uint64_t panda_parse_uint64(panda_arg_list *args, const char *argname, uint64_t defval);

/** 
 * panda_parse_uint64_req() - Get required value corresponding to this plugin arg as a uint64.
 * @args: The previously parsed panda_arg_list.
 * @argname: The name of the argument in args.
 * @help: Help text.
 *
 * Look through the arguments in args, and if any have name argname,
 * translate the associated value into a uint64 and return it. As this
 * argument is required, if no such name is found, plugin load should
 * fail.
 *
 * Return: a uint64 from args.
 */
uint64_t panda_parse_uint64_req(panda_arg_list *args, const char *argname, const char *help);

/** 
 * panda_parse_uint64_opt() - Get optional value corresponding to this plugin arg as a uint64.
 * @args: The previously parsed panda_arg_list.
 * @argname: The name of the argument in args.
 * @defval: A default value.
 * @help: Help text.
 *
 * Look through the arguments in args, and if any have name argname,
 * translate the associated value into a uint64 and return it.  If no
 * such name is found, use the default.
 *
 * Return: a uint64 from args.
 */
uint64_t panda_parse_uint64_opt(panda_arg_list *args, const char *argname, uint64_t defval, const char *help);

/** 
 * panda_parse_double() - Get value corresponding to this plugin arg as a double, with default.
 * @args: The previously parsed panda_arg_list.
 * @argname: The name of the argument in args.
 * @defval: A default value.
 *
 * Look through the arguments in args, and if any have name argname,
 * translate the associated value into a double and return it.  If no
 * such name is found, use the provided default.
 *
 * Return: a double from args or default.
 */                     
double panda_parse_double(panda_arg_list *args, const char *argname, double defval);

/** 
 * panda_parse_double_req() - Get required value corresponding to this plugin arg as a double.
 * @args: The previously parsed panda_arg_list.
 * @argname: The name of the argument in args.
 * @help: Help text.
 *
 * Look through the arguments in args, and if any have name argname,
 * translate the associated value into a double and return it. As this
 * argument is required, if no such name is found, plugin load should
 * fail.
 *
 * Return: a double from args.
 */
double panda_parse_double_req(panda_arg_list *args, const char *argname, const char *help);

/** 
 * panda_parse_double_opt() - Get optional value corresponding to this plugin arg as a double.
 * @args: The previously parsed panda_arg_list.
 * @argname: The name of the argument in args.
 * @defval: A default value.
 * @help: Help text.
 *
 * Look through the arguments in args, and if any have name argname,
 * translate the associated value into a double and return it.  If no
 * such name is found, use the default.
 *
 * Return: a double from args.
 */
double panda_parse_double_opt(panda_arg_list *args, const char *argname, double defval, const char *help);

/** 
 * panda_parse_bool() - Determine if this boolean argument is set for this plugin.
 * @args: The previously parsed panda_arg_list.
 * @argname: The name of the argument in args.
 *
 * Look through the arguments in args, and if any have name argname,
 * compare the associated value with a set of strings that likely mean
 * "true" and another set that likely mean "false" in order to
 * determine the boolean setting for that argument, which is returned.
 * Note: This means to set a boolean argument for a panda plugin you
 * need something like '-panda taint2:opt=true'.
 *
 * NB: If the argument is missing, false will be returned.
 * 
 * Return: the boolean setting, true/false.
 */                     
bool panda_parse_bool(panda_arg_list *args, const char *argname);

/** 
 * panda_parse_bool_req() - Determine if this required boolean argument is set for this plugin.
 * @args: The previously parsed panda_arg_list.
 * @argname: The name of the argument in args.
 * @help: Help text.
 *
 * Look through the arguments in args, and if any have name argname,
 * compare the associated value with a set of strings that likely mean
 * "true" and another set that likely mean "false" in order to
 * determine the boolean setting for that argument, which is returned.
 * Note: This means to set a boolean argument for a panda plugin you
 * need something like '-panda taint2:opt=true'.
 *
 * As this argument is required, if it is not found, plugin load
 * should fail.
 *
 * Return: the boolean setting, true/false.
 */
bool panda_parse_bool_req(panda_arg_list *args, const char *argname, const char *help);

/** 
 * panda_parse_bool_opt() - Determine if this optional boolean argument is set for this plugin.
 * @args: The previously parsed panda_arg_list.
 * @argname: The name of the argument in args.
 * @help: Help text.
 * 
 * Same behavior as panda_parse_bool.
 *
 * Return: the boolean setting, true/false.
 */
bool panda_parse_bool_opt(panda_arg_list *args, const char *argname, const char *help);

/** 
 * panda_parse_string() - Get required value corresponding to this plugin arg as a string.
 * @args: The previously parsed panda_arg_list.
 * @argname: The name of the argument in args.
 * @defval: A default value.
 *
 * Look through the arguments in args, and if any have name argname,
 * return the associated string value.  If the arg is not found, the
 * default will be returned.
 *
 * Return: a string value.
 */
const char *panda_parse_string(panda_arg_list *args, const char *argname, const char *defval);

/** 
 * panda_parse_string_req() - Get value corresponding to this plugin arg as a string.
 * @args: The previously parsed panda_arg_list.
 * @argname: The name of the argument in args.
 * @help: Help text.
 *
 * Look through the arguments in args, and if any have name argname,
 * return the associated string value.
 *
 * As this argument is required, if it is not found, plugin load
 * should fail.
 *
 * Return: a string value.
 */
const char *panda_parse_string_req(panda_arg_list *args, const char *argname, const char *help);

/** 
 * panda_parse_string_opt() - Look for optional string value corresponding to this plugin arg.
 * @args: The previously parsed panda_arg_list.
 * @argname: The name of the argument in args.
 * @defval: A default value.
 * @help: Help text.
 * *
 * Look through the arguments in args, and if any have name argname,
 * return the associated string value.  If the arg is not found, the
 * default will be returned.
 *
 * Return: a string value.
 */
const char *panda_parse_string_opt(panda_arg_list *args, const char *argname, const char *defval, const char *help);


extern gchar *panda_argv[MAX_PANDA_PLUGIN_ARGS];
extern int panda_argc;


// Not API functions
char** str_split(char *a_str, const char a_delim);
char* resolve_file_from_plugin_directory(const char* file_name_fmt, const char* name);

/**
 * panda_plugin_path() - Get path to plugin shared object.
 * @name: Plugin name.
 *
 * Python magic needs this. Returns full path to shared object for this plugin. 
 * For example, "taint2" might resolve to /path/to/build/x86_64-softmmu/panda/plugins/panda_taint2.so
 *
 * Return: A filesystem path.
 */
char *panda_plugin_path(const char *name);

/**
 * panda_shared_library_path() - Get path for plugin shared library.
 * @name: Plugin name.
 * 
 * Find full path to shared library (not plugin .so).
 * For example, "libso.so" might resolve to 
 * /path/to/build/x86_64-softmmu/panda/plugins/libso.so
 */
char* panda_shared_library_path(const char* name);


/**
 * panda_require() - Require (import) a plugin by name, library mode.
 * @name: Plugin name.
 * Load this plugin bc caller requires (depends upon) it.
 */
void panda_require(const char *name);

/**
 * panda_require_from_library() - Require (import) a plugin by name, library mode.
 * @plugin_name: Plugin name.
 * @plugin_args: Plugin args.
 * @num_args: Number of args.
 * 
 * Same as panda_require but in library mode we have to pass plugin args manually.
 */
void panda_require_from_library(const char *plugin_name, char **plugin_args, uint32_t num_args);

/**
 * panda_is_callback_enabled() - Determine if this plugin is loaded and enabled.
 * @plugin: Pointer to plugin (handle).
 * @type: Type of callback
 * @cb: The callback fn.
 *
 * Oddly, this function requires not the name of the plugin but
 * handle. Given that and callback type and fn, search the list of
 * callbacks and return true iff that one is both registered and
 * enabled.
 * 
 * See panda_cb_type. 
 * See panda_cb. 
 *
 * Return: True if enabled, false otherwise. 
*/
bool panda_is_callback_enabled(void *plugin, panda_cb_type type, panda_cb cb);

// END_PYPANDA_NEEDS_THIS -- do not delete this comment!

#ifdef __cplusplus
}
#endif

#include "panda/plugin_plugin.h"


#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

