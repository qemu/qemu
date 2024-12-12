/* PANDABEGINCOMMENT
 *
 * Authors:
 *  Tim Leek               tleek@ll.mit.edu
 *  Ryan Whelan            rwhelan@ll.mit.edu
 *  Joshua Hodosh          josh.hodosh@ll.mit.edu
 *  Michael Zhivich        mzhivich@ll.mit.edu
 *  Brendan Dolan-Gavitt   brendandg@gatech.edu
 *  Luke Craig             luke.craig@ll.mit.edu
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
PANDAENDCOMMENT */
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>

#include "config-host.h"
#include "panda/plugin.h"
#include "qapi/qmp/qdict.h"
#include "qapi/error.h"
#include "monitor/monitor.h"

#ifdef CONFIG_LLVM
#include "tcg.h"
#include "panda/tcg-llvm.h"
#include "panda/helper_runtime.h"
#endif

#include "panda/common.h"
#include "panda/callbacks/cb-trampolines.h"

#define SOFTMMU_DIR "/" TARGET_NAME "-softmmu"
#define LIBRARY_NAME "/libpanda-" TARGET_NAME ".so"
#define PLUGIN_DIR "/" TARGET_NAME "-softmmu/panda/plugins/"

#define INSTALL_PLUGIN_DIR "/usr/local/lib/panda/"
#define INSTALL_BIN_DIR "/usr/local/bin/" // libpanda-arch.so and panda-system-arch in here

const gchar *panda_bool_true_strings[] =  {"y", "yes", "true", "1", NULL};
const gchar *panda_bool_false_strings[] = {"n", "no", "false", "0", NULL};

#if 0
###########################################################
WARNING: This is all gloriously thread-unsafe!!!
###########################################################
#endif

// Array of pointers to PANDA callback lists, one per callback type
panda_cb_list *panda_cbs[PANDA_CB_LAST];

// Storage for command line options
gchar *panda_argv[MAX_PANDA_PLUGIN_ARGS];
int panda_argc;

int nb_panda_plugins = 0;
panda_plugin panda_plugins[MAX_PANDA_PLUGINS];

bool panda_plugin_to_unload = false;

bool panda_please_flush_tb = false;
bool panda_please_break_exec = false;
bool panda_update_pc = false;
bool panda_use_memcb = false;
bool panda_tb_chaining = true;

bool panda_help_wanted = false;
bool panda_plugin_load_failed = false;
bool panda_abort_requested = false;

bool panda_exit_loop = false;

bool panda_add_arg(const char *plugin_name, const char *plugin_arg) {
    if (plugin_name == NULL)    // PANDA argument
        panda_argv[panda_argc++] = g_strdup(plugin_arg);
    else {                       // PANDA plugin argument
        /*  Check if plugin argument is already present and overwrite, if so */
        for (int i = 0; i < panda_argc; i++) {
            if (0 == strncmp(panda_argv[i], plugin_name, strlen(plugin_name))){
                char * p;
                p = strchr(plugin_arg, '=');
                if (0 != p && 0 == strncmp(panda_argv[i]+strlen(plugin_name)+1, plugin_arg, p - plugin_arg)) {
                    g_free(panda_argv[i]);
                    panda_argv[i] = g_strdup_printf("%s:%s", plugin_name, plugin_arg);
                    return true;
                }
            }
        }
        /* We see this argument for the first time, let's add it */
        panda_argv[panda_argc++] = g_strdup_printf("%s:%s", plugin_name, plugin_arg);
    }
    return true;
}

// Forward declaration
static void panda_args_set_help_wanted(const char *);

/*
* This function takes ownership of path.
*/
static char* attempt_normalize_path(char* path){
    char* new_path = g_malloc(PATH_MAX); 
    if (realpath(path, new_path) == NULL) {
        strncpy(new_path, path, PATH_MAX-1);
    }
    g_free((char*)path);
    return new_path;
}

static void *try_open_libpanda(const char *panda_lib) {
    void *libpanda = NULL;
    if(panda_lib != NULL) {
        libpanda = dlopen(panda_lib, RTLD_LAZY | RTLD_NOLOAD | RTLD_GLOBAL);
        if (NULL == libpanda) {
            LOG_ERROR(PANDA_MSG_FMT "Failed to load libpanda: %s from %s\n", PANDA_CORE_NAME, dlerror(), panda_lib);
        }
    }
    return libpanda;
}

// When running as a library, load libpanda
static bool load_libpanda(void) {
    const char *panda_lib = g_getenv("PANDA_LIB"); // Direct path to libpanda
    const char *lib_dir = g_getenv("PANDA_DIR"); // Path to directory containing libpanda
    void *libpanda;
    // We'll search through paths for libpanda. As soon as we find a valid path, we'll call try_open_libpanda on it
    // First: if we have PANDA_LIB set, try that.
    // Next, if we have LIB_DIR set, try to load from there.
    // Next, try loading from standard /usr/local/bin/libpanda-arch.so
    // Finally try the hacky dlopen code that will be removed soon

	// Try PANDA_LIB
    if (panda_lib != NULL) {
        if (g_file_test(panda_lib, G_FILE_TEST_EXISTS)) {
            libpanda = try_open_libpanda(panda_lib);
            return libpanda != NULL;
        }
    }

	// Try relative to PANDA_DIR
    if (lib_dir != NULL) {
        panda_lib = g_strdup_printf("%s%s%s", lib_dir, SOFTMMU_DIR, LIBRARY_NAME);
        if (g_file_test(panda_lib, G_FILE_TEST_EXISTS)) {
            libpanda = try_open_libpanda(panda_lib);
            g_free((char *)panda_lib);
            return libpanda != NULL;
        }
        g_free((char *)panda_lib);
    }

    // Try standard install location
    panda_lib = g_strdup_printf("%s%s", INSTALL_BIN_DIR, LIBRARY_NAME);
    if (g_file_test(panda_lib, G_FILE_TEST_EXISTS)) {
        libpanda = try_open_libpanda(panda_lib);
        g_free((char *)panda_lib);
        return libpanda != NULL;
    }
    g_free((char *)panda_lib);

    // XXX terrible hack: relative path to build directory from the binary(?)
    panda_lib = g_strdup_printf("../../../build/%s%s", SOFTMMU_DIR, LIBRARY_NAME);
    if (g_file_test(panda_lib, G_FILE_TEST_EXISTS)) {
        LOG_WARNING(PANDA_MSG_FMT "WARNING: using hacky dlopen code that will be removed soon\n", PANDA_CORE_NAME);
        libpanda = try_open_libpanda(panda_lib);
        g_free((char *)panda_lib);
        return libpanda != NULL;
    }
    g_free((char *)panda_lib);

    return false;
}

// Internal: remove a plugin from the global array panda_plugins
static void panda_delete_plugin(int i)
{
    g_free(panda_plugins[i].name);
    if (i != nb_panda_plugins - 1) { // not the last element
        memmove(&panda_plugins[i], &panda_plugins[i + 1],
                (nb_panda_plugins - i - 1) * sizeof(panda_plugin));
    }
    nb_panda_plugins--;
}

static void dlclose_plugin(int plugin_idx) {
    void *plugin = panda_plugins[plugin_idx].plugin;
    bool exported_symbols = panda_plugins[plugin_idx].exported_symbols;
    panda_delete_plugin(plugin_idx);
    dlclose(plugin);
    if(exported_symbols) {
        // This plugin was dlopened twice.  dlclose it twice to fully unload it.
        dlclose(plugin);
    }
}

// Determine if the plugin being loaded wants to export symbols to
// subsequently loaded plugins.  If it does, dlopen it a second time
// with RTLD_GLOBAL.
static void do_check_export_symbols(panda_plugin *panda_plugin, const char *filename) {

    char *export_symbol = g_strdup_printf("PANDA_EXPORT_SYMBOLS_%s", panda_plugin->name);

    if(NULL != dlsym(panda_plugin->plugin, export_symbol)) {
        LOG_DEBUG(PANDA_MSG_FMT "Exporting symbols for plugin %s\n", PANDA_CORE_NAME, panda_plugin->name);
        assert(panda_plugin->plugin == dlopen(filename, RTLD_NOW | RTLD_GLOBAL));
        panda_plugin->exported_symbols = true;
    } else {
        // Error condition is not unexpected, clear dlerror(), 
        // otherwise someone might call it later and be confused
        dlerror();
    }

    g_free(export_symbol);
}

static bool _panda_load_plugin(const char *filename, const char *plugin_name, bool library_mode) {

    // static bool libpanda_loaded = false;

    if ((plugin_name == NULL) || (*plugin_name == '\0')) {
        LOG_ERROR(PANDA_MSG_FMT "Fatal error: plugin_name is required\n", PANDA_CORE_NAME);
        abort();
    }

#ifndef CONFIG_LLVM
    // Taint2 seems to be our most commonly used LLVM plugin and it causes some confusion
    // when users build PANDA without LLVM and then claim taint2 is "missing"
    if (strcmp(plugin_name, "taint2") == 0) {
        LOG_ERROR(PANDA_MSG_FMT "Fatal error: PANDA was built with LLVM disabled but LLVM is required for the taint2 plugin\n", PANDA_CORE_NAME);
    }
#endif

    if ((filename == NULL) || (*filename == '\0')) {
        LOG_ERROR(PANDA_MSG_FMT "Fatal error: could not find path for plugin %s\n", PANDA_CORE_NAME, plugin_name);
        abort();
    }

    // don't load the same plugin twice
    uint32_t i;
    for (i=0; i<nb_panda_plugins; i++) {
        if (strcmp(panda_plugins[i].name, plugin_name) == 0) {
            LOG_DEBUG(PANDA_MSG_FMT "%s already loaded\n", PANDA_CORE_NAME, plugin_name);
            return true;
        }
    }

    // Ensure libpanda has been dlopened so its symbols can be used in the plugin we're
    // now loading. XXX: This should probably happen earlier.
    // if (library_mode && (!libpanda_loaded)) {
    //   if(!load_libpanda()) {
    //     printf("Failed to load libpanda\n");
    //     return false;
    //   }
    //   libpanda_loaded = true;
    // }
    if (false){
        load_libpanda();
    }
    // libpanda_loaded = true;

    void *plugin = dlopen(filename, RTLD_NOW);
    if(!plugin) {
        LOG_ERROR(PANDA_MSG_FMT "Failed to load %s: %s\n", PANDA_CORE_NAME, filename, dlerror());
        return false;
    }

    const char *init_plugin = "init_plugin";
    bool (*init_fn)(void *) = dlsym(plugin, init_plugin);
    if(!init_fn) {
        LOG_ERROR(PANDA_MSG_FMT "Couldn't get symbol %s: %s\n", PANDA_CORE_NAME, init_plugin, dlerror());
        dlclose(plugin);
        return false;
    }

    // Populate basic plugin info *before* calling init_fn.
    // This allows plugins accessing handles of other plugins before
    // initialization completes. E.g. osi does a panda_require("wintrospection"),
    // and then wintrospection does a PPP_REG_CB("osi", ...) while initializing.
    panda_plugins[nb_panda_plugins].plugin = plugin;
    panda_plugins[nb_panda_plugins].unload = false;
    panda_plugins[nb_panda_plugins].exported_symbols = false;
    panda_plugins[nb_panda_plugins].name = g_strdup(plugin_name);

    do_check_export_symbols(&panda_plugins[nb_panda_plugins], filename);

    nb_panda_plugins++;

    // Call init_fn and check status.
    LOG_INFO(PANDA_MSG_FMT "initializing %s\n", PANDA_CORE_NAME, panda_plugins[nb_panda_plugins-1].name);
    panda_help_wanted = false;
    panda_args_set_help_wanted(plugin_name);
    if (panda_help_wanted) {
        printf("Options for plugin %s:\n", plugin_name);
        printf("PLUGIN              ARGUMENT                REQUIRED        DESCRIPTION\n");
        printf("======              ========                ========        ===========\n");
    }

    if(!init_fn(plugin) || panda_plugin_load_failed) {
        dlclose_plugin(nb_panda_plugins - 1);
        return false;
    }

    return true;
}

bool panda_load_plugin(const char *filename, const char *plugin_name) {
  return _panda_load_plugin(filename, plugin_name, false);
}

/** Obtains the full path to the current executable */
char *this_executable_path(void);
char *this_executable_path(void)
{
    char buf[PATH_MAX] = {0};

    // readlink method, linux only
    // should fail at runtime on other posix-compatible systems
    ssize_t size = readlink("/proc/self/exe", buf, sizeof(buf));
    if (size > 0 && size < sizeof(buf)) {
        return strdup(buf);
    }

    return NULL;
}

char *qemu_file = NULL;

// Resolve a file in the plugin directory to a path. If the file doesn't
// exist in any of the search paths, then NULL is returned. The search order 
// for files is as follows:
//
//   - Relative to the PANDA_DIR environment variable.
//   - Relative to the QEMU binary
//   - Relative to the standard install location (/usr/local/lib/panda/[arch]/)
//   - Relative to the install prefix directory.
char* resolve_file_from_plugin_directory(const char* file_name_fmt, const char* name){
    char *plugin_path, *name_formatted;
    // makes "taint2" -> "panda_taint2"
    name_formatted = g_strdup_printf(file_name_fmt, name);
    // First try relative to PANDA_PLUGIN_DIR
#ifdef PLUGIN_DIR
    if (g_getenv("PANDA_DIR") != NULL) {
        plugin_path = attempt_normalize_path(g_strdup_printf(
            "%s/%s/%s" , g_getenv("PANDA_DIR"), PLUGIN_DIR, name_formatted));
        if (TRUE == g_file_test(plugin_path, G_FILE_TEST_EXISTS)) {
            return plugin_path;
        }
        g_free(plugin_path);
    }
#endif

    // Note qemu_file is set in the first call to main_aux
    // so if this is called (likely via load_plugin) qemu_file must be set directly
    if (qemu_file == NULL){
        qemu_file = this_executable_path();
    }
    assert(qemu_file != NULL);

    // Second, try relative to PANDA binary as it would be in the build or install directory
    char *dir = g_path_get_dirname(qemu_file);
    plugin_path = attempt_normalize_path(g_strdup_printf(
                                "%s/panda/plugins/%s", dir,
                                  name_formatted));
    printf("plugin_path: %s\n", plugin_path);

    g_free(dir);
    if (TRUE == g_file_test(plugin_path, G_FILE_TEST_EXISTS)) {
        return plugin_path;
    }
    g_free(plugin_path);


    // Third, check relative to the standard install location.
    plugin_path = attempt_normalize_path(
        g_strdup_printf("%s/%s/%s", INSTALL_PLUGIN_DIR,
                        TARGET_NAME, name_formatted));
    if (TRUE == g_file_test(plugin_path, G_FILE_TEST_EXISTS)) {
        return plugin_path;
    }

    // Finally, try relative to the installation path.
    // plugin_path = attempt_normalize_path(
    //     g_strdup_printf("%s/%s/%s", CONFIG_PANDA_PLUGINDIR,
    //                     TARGET_NAME, name_formatted));
    // if (TRUE == g_file_test(plugin_path, G_FILE_TEST_EXISTS)) {
    //     return plugin_path;
    // }
    // g_free(plugin_path);

    // Return null if plugin resolution failed.
    return NULL;
}

// Resolve a shared library in the plugins directory to a path. If the shared
// object doesn't exist in any of paths, then NULL is returned. The search
// order is the same as panda_plugin_path.
// example: "libso.so" might resolve to to
// /path/to/build/x86_64-softmmu/panda/plugins/libso.so
char* panda_shared_library_path(const char* name){
    return resolve_file_from_plugin_directory("%s", name);
}

// Resolve a plugin in the plugins directory to a path.
// example: "taint2" might resolve to
// /path/to/build/x86_64-softmmu/panda/plugins/panda_taint2.so
char *panda_plugin_path(const char *plugin_name) {
    return resolve_file_from_plugin_directory("libpanda-%s_" TARGET_NAME"-softmmu" CONFIG_HOST_DSOSUF, plugin_name);
}

static void _panda_require(const char *plugin_name, char **plugin_args, uint32_t num_args, bool library_mode) {
    // If we're printing help, panda_require will be a no-op.
    if (panda_help_wanted) return;

    for (uint32_t i=0; i<num_args; i++)
        panda_add_arg(plugin_name, plugin_args[i]);

    LOG_INFO(PANDA_MSG_FMT "loading required plugin %s\n", PANDA_CORE_NAME, plugin_name);

    // translate plugin name into a path to .so
    char *plugin_path = panda_plugin_path(plugin_name); // May be NULL, would abort in in _panda_load_plugin
    if (NULL == plugin_path) {
        LOG_ERROR(PANDA_MSG_FMT "FAILED to find required plugin %s\n", PANDA_CORE_NAME, plugin_name);
        abort();
    }

    // load plugin same as in vl.c
    if (!_panda_load_plugin(plugin_path, plugin_name, library_mode)) {
        LOG_ERROR(PANDA_MSG_FMT "FAILED to load required plugin %s from %s\n", PANDA_CORE_NAME, plugin_name, plugin_path);
        abort();
    }
    g_free(plugin_path);
}

void panda_require_from_library(const char *plugin_name, char **plugin_args, uint32_t num_args) {
    _panda_require(plugin_name, plugin_args, num_args, true);
}

void panda_require(const char *plugin_name) {
    _panda_require(plugin_name, NULL, 0, false);
}

void panda_do_unload_plugin(int plugin_idx)
{
    void *plugin = panda_plugins[plugin_idx].plugin;
    void (*uninit_fn)(void *) = dlsym(plugin, "uninit_plugin");
    if (!uninit_fn) {
        LOG_ERROR("Couldn't get symbol %s: %s\n", "uninit_plugin",
                dlerror());
    } else {
        uninit_fn(plugin);
    }
    panda_unregister_callbacks(plugin);
    dlclose_plugin(plugin_idx);
}

void panda_unload_plugin(void *plugin)
{
    int i;
    for (i = 0; i < nb_panda_plugins; i++) {
        if (panda_plugins[i].plugin == plugin) {
            panda_unload_plugin_idx(i);
            break;
        }
    }
}

void panda_unload_plugin_by_name(const char *plugin_name) {
    for (int i = 0; i < nb_panda_plugins; i++) {
        if (strcmp(panda_plugins[i].name, plugin_name) == 0) {
            panda_unload_plugin(panda_plugins[i].plugin);
            break;
        }
    }
}

void panda_unload_plugin_idx(int plugin_idx)
{
    if (plugin_idx >= nb_panda_plugins || plugin_idx < 0) {
        return;
    }
    panda_plugin_to_unload = true;
    panda_plugins[plugin_idx].unload = true;
}

void panda_unload_plugins(void)
{
    // Unload them starting from the end to avoid having to shuffle everything
    // down each time
    while (nb_panda_plugins > 0) {
        panda_do_unload_plugin(nb_panda_plugins - 1);
    }
}

void *panda_get_plugin_by_name(const char *plugin_name)
{
    for (int i = 0; i < nb_panda_plugins; i++) {
        if (strcmp(panda_plugins[i].name, plugin_name) == 0)
            return panda_plugins[i].plugin;
    }
    return NULL;
}

#define CASE_CB_TRAMPOLINE(kind,name) \
    case PANDA_CB_ ## kind: \
        trampoline_cb. name = panda_cb_trampoline_ ## name; \
        break;

panda_cb_with_context panda_get_cb_trampoline(panda_cb_type type) {
    panda_cb_with_context trampoline_cb;
    switch (type) {
        CASE_CB_TRAMPOLINE(BEFORE_BLOCK_TRANSLATE,before_block_translate)
        CASE_CB_TRAMPOLINE(AFTER_BLOCK_TRANSLATE,after_block_translate)
        CASE_CB_TRAMPOLINE(BEFORE_BLOCK_EXEC_INVALIDATE_OPT,before_block_exec_invalidate_opt)
        CASE_CB_TRAMPOLINE(BEFORE_TCG_CODEGEN,before_tcg_codegen)
        CASE_CB_TRAMPOLINE(BEFORE_BLOCK_EXEC,before_block_exec)
        CASE_CB_TRAMPOLINE(AFTER_BLOCK_EXEC,after_block_exec)
        CASE_CB_TRAMPOLINE(INSN_TRANSLATE,insn_translate)
        CASE_CB_TRAMPOLINE(INSN_EXEC,insn_exec)
        CASE_CB_TRAMPOLINE(AFTER_INSN_TRANSLATE,after_insn_translate)
        CASE_CB_TRAMPOLINE(AFTER_INSN_EXEC,after_insn_exec)
        CASE_CB_TRAMPOLINE(VIRT_MEM_BEFORE_READ,virt_mem_before_read)
        CASE_CB_TRAMPOLINE(VIRT_MEM_BEFORE_WRITE,virt_mem_before_write)
        CASE_CB_TRAMPOLINE(PHYS_MEM_BEFORE_READ,phys_mem_before_read)
        CASE_CB_TRAMPOLINE(PHYS_MEM_BEFORE_WRITE,phys_mem_before_write)
        CASE_CB_TRAMPOLINE(VIRT_MEM_AFTER_READ,virt_mem_after_read)
        CASE_CB_TRAMPOLINE(VIRT_MEM_AFTER_WRITE,virt_mem_after_write)
        CASE_CB_TRAMPOLINE(PHYS_MEM_AFTER_READ,phys_mem_after_read)
        CASE_CB_TRAMPOLINE(PHYS_MEM_AFTER_WRITE,phys_mem_after_write)
        CASE_CB_TRAMPOLINE(MMIO_AFTER_READ,mmio_after_read)
        CASE_CB_TRAMPOLINE(MMIO_BEFORE_WRITE,mmio_before_write)
        CASE_CB_TRAMPOLINE(HD_READ,hd_read)
        CASE_CB_TRAMPOLINE(HD_WRITE,hd_write)
        CASE_CB_TRAMPOLINE(GUEST_HYPERCALL,guest_hypercall)
        CASE_CB_TRAMPOLINE(MONITOR,monitor)
        CASE_CB_TRAMPOLINE(QMP,qmp)
        CASE_CB_TRAMPOLINE(CPU_RESTORE_STATE,cpu_restore_state)

        //CASE_CB_TRAMPOLINE(BEFORE_LOADVM,before_loadvm)
        CASE_CB_TRAMPOLINE(ASID_CHANGED,asid_changed)
        // CASE_CB_TRAMPOLINE(REPLAY_HD_TRANSFER,replay_hd_transfer)
        // CASE_CB_TRAMPOLINE(REPLAY_NET_TRANSFER,replay_net_transfer)
        // CASE_CB_TRAMPOLINE(REPLAY_SERIAL_RECEIVE,replay_serial_receive)
        // CASE_CB_TRAMPOLINE(REPLAY_SERIAL_READ,replay_serial_read)
        // CASE_CB_TRAMPOLINE(REPLAY_SERIAL_SEND,replay_serial_send)
        // CASE_CB_TRAMPOLINE(REPLAY_SERIAL_WRITE,replay_serial_write)
        // CASE_CB_TRAMPOLINE(REPLAY_BEFORE_DMA,replay_before_dma)
        // CASE_CB_TRAMPOLINE(REPLAY_AFTER_DMA,replay_after_dma)
        // CASE_CB_TRAMPOLINE(REPLAY_HANDLE_PACKET,replay_handle_packet)
        CASE_CB_TRAMPOLINE(AFTER_CPU_EXEC_ENTER,after_cpu_exec_enter)
        CASE_CB_TRAMPOLINE(BEFORE_CPU_EXEC_EXIT,before_cpu_exec_exit)
        CASE_CB_TRAMPOLINE(AFTER_MACHINE_INIT,after_machine_init)
        CASE_CB_TRAMPOLINE(AFTER_LOADVM,after_loadvm)
        CASE_CB_TRAMPOLINE(TOP_LOOP,top_loop)
        CASE_CB_TRAMPOLINE(DURING_MACHINE_INIT,during_machine_init)
        CASE_CB_TRAMPOLINE(MAIN_LOOP_WAIT,main_loop_wait)
        CASE_CB_TRAMPOLINE(PRE_SHUTDOWN,pre_shutdown)
        CASE_CB_TRAMPOLINE(UNASSIGNED_IO_READ,unassigned_io_read)
        CASE_CB_TRAMPOLINE(UNASSIGNED_IO_WRITE,unassigned_io_write)
        CASE_CB_TRAMPOLINE(BEFORE_HANDLE_EXCEPTION,before_handle_exception)
        CASE_CB_TRAMPOLINE(BEFORE_HANDLE_INTERRUPT,before_handle_interrupt)
        CASE_CB_TRAMPOLINE(START_BLOCK_EXEC,start_block_exec)
        CASE_CB_TRAMPOLINE(END_BLOCK_EXEC,end_block_exec)

        default: assert(false);
    }

    return trampoline_cb;
}

/**
 * @brief Adds callback to the tail of the callback list and enables it.
 *
 * The order of callback registration will determine the order in which
 * callbacks of the same type will be invoked.
 *
 * @note Registering a callback function twice from the same plugin will trigger
 * an assertion error.
 */
void panda_register_callback(void *plugin, panda_cb_type type, panda_cb cb)
{
    panda_cb_with_context trampoline = panda_get_cb_trampoline(type);
    panda_cb* cb_context = malloc(sizeof(panda_cb));
    *cb_context = cb;

    panda_register_callback_with_context(plugin, type, trampoline, cb_context);
}

/**
 * @brief Adds callback to the tail of the callback list and enables it.
 *
 * The order of callback registration will determine the order in which
 * callbacks of the same type will be invoked. Each callback will recieve the
 * context variable it was passed.
 *
 * @note Registering a callback function twice from the same plugin will trigger
 * an assertion error.
 */
void panda_register_callback_with_context(void *plugin, panda_cb_type type, panda_cb_with_context cb, void* context)
{
    panda_cb_list *plist_last = NULL;

    panda_cb_list *new_list = g_new0(panda_cb_list, 1);
    new_list->entry = cb;
    new_list->owner = plugin;
    new_list->enabled = true;
    new_list->context = context;
    assert(type < PANDA_CB_LAST);

    if (panda_cbs[type] != NULL) {
        for (panda_cb_list *plist = panda_cbs[type]; plist != NULL;
             plist = plist->next) {
            // the same plugin can register the same callback function only once
            assert(!(plist->owner == plugin &&
                     (plist->entry.cbaddr) == cb.cbaddr &&
                     plist->context == context));
            plist_last = plist;
        }
        plist_last->next = new_list;
        new_list->prev = plist_last;
    } else {
        panda_cbs[type] = new_list;
    }
}

/**
 * @brief Determine if the specified callback is enabled
 *
 * @note Querying an unregistered callback returns false
 */
bool panda_is_callback_enabled(void *plugin, panda_cb_type type, panda_cb cb) {
    assert(type < PANDA_CB_LAST);
    if (panda_cbs[type] != NULL) {
        for (panda_cb_list *plist = panda_cbs[type]; plist != NULL; plist = plist->next) {
            if (plist->owner == plugin && (plist->entry.cbaddr) == cb.cbaddr) {
                return plist->enabled;
            }
        }
    }
    return false;
}

#define TRAMP_CTXT(context) \
    (*(panda_cb*)context).cbaddr

/**
 * @brief Disables the execution of the specified callback.
 *
 * This is done by setting the `enabled` flag to `false`. The callback remains
 * in the callback list, so when it is enabled again it will execute in the same
 * relative order.
 *
 * @note Disabling an unregistered callback will trigger an assertion error.
 */
void panda_disable_callback(void *plugin, panda_cb_type type, panda_cb cb) {
    panda_cb_with_context trampoline = panda_get_cb_trampoline(type);
    panda_disable_callback_with_context(plugin, type, trampoline, &cb);
}

/**
 * @brief Disables the execution of the specified callback.
 *
 * This is done by setting the `enabled` flag to `false`. The callback remains
 * in the callback list, so when it is enabled again it will execute in the same
 * relative order.
 *
 * @note Disabling an unregistered callback will trigger an assertion error.
 */
void panda_disable_callback_with_context(void *plugin, panda_cb_type type, panda_cb_with_context cb, void* context)
{
    bool found = false;
    assert(type < PANDA_CB_LAST);
    if (panda_cbs[type] != NULL) {
        panda_cb_with_context trampoline = panda_get_cb_trampoline(type);
        for (panda_cb_list *plist = panda_cbs[type]; plist != NULL;
             plist = plist->next) {
            if (plist->owner == plugin &&
                ((((plist->entry.cbaddr) == cb.cbaddr) && plist->context == context) ||
                 (
                     // if and only if it's a trampoline, it's safe to dereference the
                     // context in order to do an equality check
                     plist->entry.cbaddr == trampoline.cbaddr
                     && TRAMP_CTXT(context) == TRAMP_CTXT(plist->context)
                ))
            ) {
                found = true;
                plist->enabled = false;

                // break out of the loop - the same plugin can register the same
                // callback only once
                break;
            }
        }
    }
    // no callback found to disable
    assert(found);
}

/**
 * @brief Enables the execution of the specified callback.
 *
 * This is done by setting the `enabled` flag to `true`. After enabling the
 * callback, it will execute in the same relative order as before having it
 * disabled.
 *
 * @note Enabling an unregistered callback will trigger an assertion error.
 */
void panda_enable_callback(void *plugin, panda_cb_type type, panda_cb cb) {
    panda_cb_with_context trampoline = panda_get_cb_trampoline(type);
    panda_enable_callback_with_context(plugin, type, trampoline, &cb);
}

/**
 * @brief Enables the execution of the specified callback.
 *
 * This is done by setting the `enabled` flag to `true`. After enabling the
 * callback, it will execute in the same relative order as before having it
 * disabled.
 *
 * @note Enabling an unregistered callback will trigger an assertion error.
 */
void panda_enable_callback_with_context(void *plugin, panda_cb_type type, panda_cb_with_context cb, void* context)
{
    bool found = false;
    if (panda_cbs[type] != NULL) {
        panda_cb_with_context trampoline = panda_get_cb_trampoline(type);
        for (panda_cb_list *plist = panda_cbs[type]; plist != NULL; plist = plist->next)
        {
            if (plist->owner == plugin && ((
                    (plist->entry.cbaddr) == cb.cbaddr && plist->context == context
                ) ||
                (
                    // if and only if it's a trampoline, it's safe to dereference the
                    // context in order to do an equality check
                    plist->entry.cbaddr == trampoline.cbaddr
                        && TRAMP_CTXT(plist->context) == TRAMP_CTXT(context)
                ))
            ) {
                found = true;
                plist->enabled = true;

                // break out of the loop - the same plugin can register the same
                // callback only once
                break;
            }
        }
    }
    // no callback found to enable
    assert(found);
}

/**
 * @brief Unregisters all callbacks owned by this plugin.
 *
 * The register callbacks are removed from their respective callback lists.
 * This means that if they are registered again, their execution order may be
 * different.
 */
void panda_unregister_callbacks(void *plugin)
{
    for (int i = 0; i < PANDA_CB_LAST; i++) {
        panda_cb_list *plist;
        plist = panda_cbs[i];
        panda_cb_list *plist_head = plist;
        while (plist != NULL) {
            panda_cb_list *plist_next = plist->next;
            if (plist->owner == plugin) {
                // delete this entry -- it belongs to our plugin
                panda_cb_list *del_plist = plist;
                if (plist->next == NULL && plist->prev == NULL) {
                    // its the only thing in the list -- list is now empty
                    plist_head = NULL;
                } else {
                    // Unlink this entry
                    if (plist->prev)
                        plist->prev->next = plist->next;
                    if (plist->next)
                        plist->next->prev = plist->prev;
                    // new head
                    if (plist == plist_head)
                        plist_head = plist->next;
                }
                // Free the entry we just unlinked
                g_free(del_plist);
            }
            plist = plist_next;
        }
        // update head
        panda_cbs[i] = plist_head;
    }
}

/**
 * @brief Enables the specified plugin.
 *
 * This works by enabling all the callbacks previously registered by
 * the plugin. This means that when execution order of the callbacks
 * is preserved.
 */
void panda_enable_plugin(void *plugin)
{
    for (int i = 0; i < PANDA_CB_LAST; i++) {
        panda_cb_list *plist;
        plist = panda_cbs[i];
        while (plist != NULL) {
            if (plist->owner == plugin) {
                plist->enabled = true;
            }
            plist = plist->next;
        }
    }
}

/**
 * @brief Disables the specified plugin.
 *
 * This works by disabling all the callbacks registered by the plugin.
 * This means that when the plugin is re-enabled, the callback order
 * is preserved.
 */
void panda_disable_plugin(void *plugin)
{
    for (int i = 0; i < PANDA_CB_LAST; i++) {
        panda_cb_list *plist;
        plist = panda_cbs[i];
        while (plist != NULL) {
            if (plist->owner == plugin) {
                plist->enabled = false;
            }
            plist = plist->next;
        }
    }
}

/**
 * @brief Allows to navigate the callback linked list skipping disabled
 * callbacks.
 */
panda_cb_list *panda_cb_list_next(panda_cb_list *plist)
{
    for (panda_cb_list *node = plist->next; node != NULL;
         node = node->next) {
        if (!node || node->enabled)
            return node;
    }
    return NULL;
}

void panda_do_break_exec(void) {
  panda_please_break_exec = true;
}

bool panda_break_exec(void) {
    if (panda_please_break_exec) {
        panda_please_break_exec = false;
        return true;
    } else {
        return false;
    }

}

bool panda_flush_tb(void)
{
    if (panda_please_flush_tb) {
        panda_please_flush_tb = false;
        return true;
    } else
        return false;
}

void panda_do_flush_tb(void)
{
    panda_please_flush_tb = true;
}

void panda_enable_precise_pc(void)
{
    panda_update_pc = true;
}

void panda_disable_precise_pc(void)
{
    panda_update_pc = false;
}

void panda_enable_memcb(void)
{
    panda_use_memcb = true;
}

void panda_disable_memcb(void)
{
    panda_use_memcb = false;
}

void panda_enable_tb_chaining(void)
{
    panda_tb_chaining = true;
}

void panda_disable_tb_chaining(void)
{
    panda_tb_chaining = false;
}

#ifdef CONFIG_LLVM

// Enable translating TCG -> LLVM and executing LLVM
void panda_enable_llvm(void) {
    panda_do_flush_tb();
    execute_llvm = 1;
    generate_llvm = 1;
    tcg_llvm_initialize();
}

// Enable translating TCG -> LLVM, but still execute TCG
void panda_enable_llvm_no_exec(void) {
    panda_do_flush_tb();
    execute_llvm = 0;
    generate_llvm = 1;
    tcg_llvm_initialize();
}

// Disable LLVM translation and execution
void panda_disable_llvm(void) {
    panda_do_flush_tb();
    execute_llvm = 0;
    generate_llvm = 0;
    tcg_llvm_destroy();
    tcg_llvm_translator = NULL;
}

// Enable LLVM helpers
void panda_enable_llvm_helpers(void) {
    init_llvm_helpers();
}

// Disable LLVM helpers
void panda_disable_llvm_helpers(void) {
    uninit_llvm_helpers();
}

// Flush results of latest LLVM bitcode to file
// Reccomend using a RAM-backed path (e.g. /dev/run/, /run/shm, or /dev/shm)
int panda_write_current_llvm_bitcode_to_file(const char* path) {
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if ((tcg_llvm_translator == 0) || (fd == -1)) {
        return -1;
    }

    tcg_llvm_write_module(tcg_llvm_translator, path);
    return 0;
}

uintptr_t panda_get_current_llvm_module(void) {
    return tcg_llvm_get_module_ptr(tcg_llvm_translator);
}
#endif

#ifdef DO_LATER
void panda_memsavep(FILE *f) {
#ifdef CONFIG_SOFTMMU
    if (!f) return;
    uint8_t mem_buf[TARGET_PAGE_SIZE];
    uint8_t zero_buf[TARGET_PAGE_SIZE];
    memset(zero_buf, 0, TARGET_PAGE_SIZE);
    int res;
    ram_addr_t addr;
    for (addr = 0; addr < ram_size; addr += TARGET_PAGE_SIZE) {
        res = panda_physical_memory_rw(addr, mem_buf, TARGET_PAGE_SIZE, 0);
        if (res == -1) { // I/O. Just fill page with zeroes.
            fwrite(zero_buf, TARGET_PAGE_SIZE, 1, f);
        }
        else {
            fwrite(mem_buf, TARGET_PAGE_SIZE, 1, f);
        }
    }
#endif
}

/**
 * @brief Stop and then quit the PANDA VM. Wraps QMP functions for plugins,
 * without having them to pull QMP headers.
 */
int panda_vm_quit(void) {
    qmp_stop(NULL); /* wait for any dumps to finish */
    qmp_quit(NULL); /* quit */
    return RRCTRL_OK;
}


/**
 * @brief Starts recording a PANDA trace. If \p snapshot is not NULL,
 * then the VM state will be reverted to the specified snapshot before
 * starting recording.
 */
int panda_record_begin(const char *name, const char *snapshot) {
    if (rr_on())
        return RRCTRL_EINVALID;
    if (rr_control.next != RR_NOCHANGE)
        return RRCTRL_EPENDING;

    rr_control.next = RR_RECORD;
    rr_control.name = g_strdup(name);
    rr_control.snapshot = (snapshot != NULL) ? g_strdup(snapshot) : NULL;
    return RRCTRL_OK;
}

/**
 * @brief Ends current PANDA recording.
 */
int panda_record_end(void) {
    if (!rr_in_record())
        return RRCTRL_EINVALID;
    if (rr_control.next != RR_NOCHANGE)
        return RRCTRL_EPENDING;

    rr_control.next = RR_OFF;
    return RRCTRL_OK;
}

/**
 * @brief Starts replaying the specified PANDA trace.
 */
int panda_replay_begin(const char *name) {
    if (rr_on())
        return RRCTRL_EINVALID;
    if (rr_control.next != RR_NOCHANGE)
        return RRCTRL_EPENDING;

    rr_control.next = RR_REPLAY;
    rr_control.name = g_strdup(name);
    return RRCTRL_OK;
}

/**
 * @brief Stops the currently running PANDA replay.
 */
int panda_replay_end(void) {
    if (!rr_in_replay())
        return RRCTRL_EINVALID;
    if (rr_control.next != RR_NOCHANGE)
        return RRCTRL_EPENDING;

    rr_control.next = RR_OFF;
    return RRCTRL_OK;
}

/**
 * @brief Return the name of the current PANDA record/replay
 * 
 * @return char* 
 */
char* panda_get_rr_name(void){
    return rr_control.name;
}

#endif

// Parse out arguments and return them to caller
static panda_arg_list *panda_get_args_internal(const char *plugin_name, bool check_only) {
    panda_arg_list *ret = NULL;
    panda_arg *list = NULL;

    ret = g_new0(panda_arg_list, 1);
    if (ret == NULL) goto fail;

    int i;
    int nargs = 0;
    // one pass to get number of matching args
    for (i = 0; i < panda_argc; i++) {
        if (0 == strncmp(plugin_name, panda_argv[i], strlen(plugin_name))) {
            nargs++;
        }
    }

    if (nargs != 0) {
        ret->nargs = nargs;
        list = (panda_arg *) g_malloc(sizeof(panda_arg)*nargs);
        if (list == NULL) goto fail;
    }

    // Put plugin name in here so we can use it
    ret->plugin_name = g_strdup(plugin_name);

    // second pass to copy and parse each arg into key/value
    int ret_idx = 0;
    for (i = 0; i < panda_argc; i++) {
        if (0 == strncmp(plugin_name, panda_argv[i], strlen(plugin_name))) {
            list[ret_idx].argptr = g_strdup(panda_argv[i]);
            bool found_colon = false;
            bool found_equals = false;
            char *p;
            int j;
            for (p = list[ret_idx].argptr, j = 0;
                    *p != '\0' && j < 256; p++, j++) {
                if (*p == ':') {
                    *p = '\0';
                    list[ret_idx].key = p+1;
                    found_colon = true;
                }
                else if (*p == '=') {
                    *p = '\0';
                    list[ret_idx].value = p+1;
                    found_equals = true;
                    break;
                }
            }
            if (!found_colon) {
                // malformed argument
                goto fail;
            }
            if (!found_equals) {
                list[ret_idx].value = (char *) "";
            }
            ret_idx++;
        }
    }

    ret->list = list;

    for (i = 0; i < ret->nargs; i++) {
        if (strcmp(ret->list[i].key, "help") == 0) {
            panda_help_wanted = true;
            panda_abort_requested = true;
        }
    }

    if (check_only) {
        panda_free_args(ret);
        ret = NULL;
    }

    return ret;

fail:
    if (ret != NULL) g_free(ret);
    if (list != NULL) g_free(list);
    return NULL;
}

static void panda_args_set_help_wanted(const char *plugin_name) {
    panda_get_args_internal(plugin_name, true);
}

panda_arg_list *panda_get_args(const char *plugin_name) {
    return panda_get_args_internal(plugin_name, false);
}

static bool panda_parse_bool_internal(panda_arg_list *args, const char *argname, const char *help, bool required) {
    gchar *val = NULL;
    if (panda_help_wanted) goto help;
    if (!args) goto error_handling;
    for (int i = 0; i < args->nargs; i++) {
        if (g_ascii_strcasecmp(args->list[i].key, argname) == 0) {
            val = args->list[i].value;
            for (const gchar **vp=panda_bool_true_strings; *vp != NULL; vp++) {
                if (g_ascii_strcasecmp(*vp, val) == 0) return true;
            }
            for (const gchar **vp=panda_bool_false_strings; *vp != NULL; vp++) {
                if (g_ascii_strcasecmp(*vp, val) == 0) return false;
            }

            // argument name matched
            break;
        }
    }

error_handling:
    if (val != NULL) { // value provided but not in the list of accepted values
        LOG_ERROR(PANDA_MSG_FMT "FAILED to parse value \"%s\" for bool argument \"%s\"\n", PANDA_CORE_NAME, val, argname);
        panda_plugin_load_failed = true;
    }
    else if (required) { // value not provided but required
        LOG_ERROR(PANDA_MSG_FMT "ERROR finding required bool argument \"%s\"\n", PANDA_CORE_NAME, argname);
        LOG_ERROR(PANDA_MSG_FMT "help for \"%s\": %s\n", PANDA_CORE_NAME, argname, help);
        panda_plugin_load_failed = true;
    }
help:
    if (panda_help_wanted) {
        printf("%-20s%-24sOptional        %s (default=true)\n", args->plugin_name, argname, help);
    }

    // not found
    return false;
}

bool panda_parse_bool_req(panda_arg_list *args, const char *argname, const char *help) {
    bool ret= panda_parse_bool_internal(args, argname, help, true);
    if(panda_plugin_load_failed) abort(); // If a required arg is present but we can't parse, abort
    return ret;
}

bool panda_parse_bool_opt(panda_arg_list *args, const char *argname, const char *help) {
    bool ret= panda_parse_bool_internal(args, argname, help, false);
    if(panda_plugin_load_failed) abort(); // If the optional arg is present but we can't parse, abort
    return ret;
}

bool panda_parse_bool(panda_arg_list *args, const char *argname) {
    return panda_parse_bool_internal(args, argname, "Undocumented option. Complain to the developer!", false);
}

static target_ulong panda_parse_ulong_internal(panda_arg_list *args, const char *argname, target_ulong defval, const char *help, bool required) {
    if (panda_help_wanted) goto help;
    if (!args) goto error_handling;
    int i;
    for (i = 0; i < args->nargs; i++) {
        if (strcmp(args->list[i].key, argname) == 0) {
            return strtoul(args->list[i].value, NULL, 0);
        }
    }

error_handling:
    if (required) {
        LOG_ERROR("ERROR: plugin required ulong argument \"%s\" but you did not provide it\n", argname);
        LOG_ERROR(PANDA_MSG_FMT "Help for \"%s\": %s\n", PANDA_CORE_NAME, argname, help);
        panda_plugin_load_failed = true;
    }
help:
    if (panda_help_wanted) {
        if (required) printf("%-20s%-24sRequired        %s\n", args->plugin_name, argname, help);
        else printf("%-20s%-24sOptional        %s (default=" TARGET_FMT_ld ")\n", args->plugin_name, argname, help, defval);
    }

    return defval;
}

target_ulong panda_parse_ulong_req(panda_arg_list *args, const char *argname, const char *help) {
    return panda_parse_ulong_internal(args, argname, 0, help, true);
}

target_ulong panda_parse_ulong_opt(panda_arg_list *args, const char *argname, target_ulong defval, const char *help) {
    return panda_parse_ulong_internal(args, argname, defval, help, false);
}

target_ulong panda_parse_ulong(panda_arg_list *args, const char *argname, target_ulong defval) {
    return panda_parse_ulong_internal(args, argname, defval, "Undocumented option. Complain to the developer!", false);
}

static uint32_t panda_parse_uint32_internal(panda_arg_list *args, const char *argname, uint32_t defval, const char *help, bool required) {
    if (panda_help_wanted) goto help;
    if (!args) goto error_handling;
    int i;
    for (i = 0; i < args->nargs; i++) {
        if (strcmp(args->list[i].key, argname) == 0) {
            return strtoull(args->list[i].value, NULL, 0);
        }
    }

error_handling:
    if (required) {
        LOG_ERROR("ERROR: plugin required uint32 argument \"%s\" but you did not provide it\n", argname);
        LOG_ERROR("Help for \"%s\": %s\n", argname, help);
        panda_plugin_load_failed = true;
    }
help:
    if (panda_help_wanted) {
        if (required) printf("%-20s%-24sRequired        %s\n", args->plugin_name, argname, help);
        else printf("%-20s%-24sOptional        %s (default=%d)\n", args->plugin_name, argname, help, defval);
    }

    return defval;
}

uint32_t panda_parse_uint32_req(panda_arg_list *args, const char *argname, const char *help) {
    return panda_parse_uint32_internal(args, argname, 0, help, true);
}

uint32_t panda_parse_uint32_opt(panda_arg_list *args, const char *argname, uint32_t defval, const char *help) {
    return panda_parse_uint32_internal(args, argname, defval, help, false);
}

uint32_t panda_parse_uint32(panda_arg_list *args, const char *argname, uint32_t defval) {
    return panda_parse_uint32_internal(args, argname, defval, "Undocumented option. Complain to the developer!", false);
}

static uint64_t panda_parse_uint64_internal(panda_arg_list *args, const char *argname, uint64_t defval, const char *help, bool required) {
    if (panda_help_wanted) goto help;
    if (!args) goto error_handling;
    int i;
    for (i = 0; i < args->nargs; i++) {
        if (strcmp(args->list[i].key, argname) == 0) {
            return strtoull(args->list[i].value, NULL, 0);
        }
    }

error_handling:
    if (required) {
        LOG_ERROR("ERROR: plugin required uint64 argument \"%s\" but you did not provide it\n", argname);
        LOG_ERROR("Help for \"%s\": %s\n", argname, help);
        panda_plugin_load_failed = true;
    }
help:
    if (panda_help_wanted) {
        if (required) printf("%-20s%-24sRequired        %s)\n", args->plugin_name, argname, help);
        else printf("%-20s%-24sOptional        %s (default=%" PRId64 ")\n", args->plugin_name, argname, help, defval);
    }

    return defval;
}

uint64_t panda_parse_uint64_req(panda_arg_list *args, const char *argname, const char *help) {
    return panda_parse_uint64_internal(args, argname, 0, help, true);
}

uint64_t panda_parse_uint64_opt(panda_arg_list *args, const char *argname, uint64_t defval, const char *help) {
    return panda_parse_uint64_internal(args, argname, defval, help, false);
}

uint64_t panda_parse_uint64(panda_arg_list *args, const char *argname, uint64_t defval) {
    return panda_parse_uint64_internal(args, argname, defval, "Undocumented option. Complain to the developer!", false);
}

static double panda_parse_double_internal(panda_arg_list *args, const char *argname, double defval, const char *help, bool required) {
    if (panda_help_wanted) goto help;
    if (!args) goto error_handling;
    int i;
    for (i = 0; i < args->nargs; i++) {
        if (strcmp(args->list[i].key, argname) == 0) {
            return strtod(args->list[i].value, NULL);
        }
    }

error_handling:
    if (required) {
        LOG_ERROR("ERROR: plugin required double argument \"%s\" but you did not provide it\n", argname);
        LOG_ERROR("Help for \"%s\": %s\n", argname, help);
        panda_plugin_load_failed = true;
    }
help:
    if (panda_help_wanted) {
        if (required) printf("%-20s%-24sRequired        %s\n", args->plugin_name, argname, help);
        else printf("%-20s%-24sOptional        %s (default=%f)\n", args->plugin_name, argname, help, defval);
    }

    return defval;
}

double panda_parse_double_req(panda_arg_list *args, const char *argname, const char *help) {
    return panda_parse_double_internal(args, argname, 0, help, true);
}

double panda_parse_double_opt(panda_arg_list *args, const char *argname, double defval, const char *help) {
    return panda_parse_double_internal(args, argname, defval, help, false);
}

double panda_parse_double(panda_arg_list *args, const char *argname, double defval) {
    return panda_parse_double_internal(args, argname, defval, "Undocumented option. Complain to the developer!", false);
}

char** str_split(char* a_str, const char a_delim)  {
    char** result    = 0;
    size_t count     = 0;
    char* tmp        = a_str;
    char* last_comma = 0;
    char delim[2];
    delim[0] = a_delim;
    delim[1] = 0;
    /* Count how many elements will be extracted. */
    while (*tmp) {
        if (a_delim == *tmp) {
            count++;
            last_comma = tmp;
        }
        tmp++;
    }
    /* Add space for trailing token. */
    count += last_comma < (a_str + strlen(a_str) - 1);
    /* Add space for terminating null string so caller
       knows where the list of returned strings ends. */
    count++;
    result = malloc(sizeof(char*) * count);
    if (result) {
        size_t idx  = 0;
        char* token = strtok(a_str, delim);
        while (token)  {
            assert(idx < count);
            *(result + idx++) = strdup(token);
            token = strtok(0, delim);
        }
        assert(idx == count - 1);
        *(result + idx) = 0;
    }
    return result;
}


// Returns pointer to string inside arg list, freed when list is freed.
static const char *panda_parse_string_internal(panda_arg_list *args, const char *argname, const char *defval, const char *help, bool required) {
    if (panda_help_wanted) goto help;
    if (!args) goto error_handling;
    int i;
    for (i = 0; i < args->nargs; i++) {
        if (strcmp(args->list[i].key, argname) == 0) {
            return args->list[i].value;
        }
    }

error_handling:
    if (required) {
        LOG_ERROR("ERROR: plugin required string argument \"%s\" but you did not provide it\n", argname);
        LOG_ERROR("Help for \"%s\": %s\n", argname, help);
        panda_plugin_load_failed = true;
    }
help:
    if (panda_help_wanted) {
        if (required) printf("%-20s%-24sRequired        %s\n", args->plugin_name, argname, help);
        else printf("%-20s%-24sOptional        %s (default=\"%s\")\n", args->plugin_name, argname, help, defval);
    }

    return defval;
}

const char *panda_parse_string_req(panda_arg_list *args, const char *argname, const char *help) {
    return panda_parse_string_internal(args, argname, "", help, true);
}

const char *panda_parse_string_opt(panda_arg_list *args, const char *argname, const char *defval, const char *help) {
    return panda_parse_string_internal(args, argname, defval, help, false);
}

const char *panda_parse_string(panda_arg_list *args, const char *argname, const char *defval) {
    return panda_parse_string_internal(args, argname, defval, "Undocumented option. Complain to the developer!", false);
}

// Free a list of parsed arguments
void panda_free_args(panda_arg_list *args) {
    int i;
    if (!args) return;
    for (i = 0; i < args->nargs; i++) {
        g_free(args->list[i].argptr);
    }
    g_free(args->plugin_name);
    g_free(args);
}

#if defined(CONFIG_SOFTMMU) && defined(QMP_LATER)

// QMP

void qmp_load_plugin(bool has_file_name, const char *file_name, const char *plugin_name, bool has_plugin_args, const char *plugin_args, Error **errp);

void qmp_load_plugin(bool has_file_name, const char *file_name, const char *plugin_name, bool has_plugin_args, const char *plugin_args, Error **errp){

    if(!has_file_name)
        file_name = panda_plugin_path(plugin_name);

    if (has_plugin_args){
        gchar *args = g_strdup(plugin_args);
        char *args_start = args;
        char *args_end = args;

        while (args_end != NULL) {
            args_end = strchr(args_start, ',');
            if (args_end != NULL) *args_end = '\0';

            // panda_add_arg() currently always return true
            assert(panda_add_arg(plugin_name, args_start));

            args_start = args_end + 1;
        }

        g_free(args);
    }

    if(!panda_load_plugin(file_name, plugin_name)) {
        // TODO: do something with errp here?
    }

    if(!has_file_name)
        g_free((char *)file_name);
}
void qmp_unload_plugin(int64_t index, Error **errp);

void qmp_unload_plugin(int64_t index, Error **errp) {
    if (index >= nb_panda_plugins || index < 0) {
        // TODO: errp
    } else {
        panda_unload_plugin_idx(index);
    }
}

PandaPluginInfoList *qmp_list_plugins(Error **errp) {
    PandaPluginInfoList *head = NULL;
    int i;

    for (i = 0; i < nb_panda_plugins; i++) {
        PandaPluginInfoList *list_item = g_new0(typeof(*list_item), 1);
        PandaPluginInfo *plugin_item = g_new0(typeof(*plugin_item), 1);

        plugin_item->index = i;
        plugin_item->name = g_strdup(panda_plugins[i].name);
        plugin_item->address = (unsigned long) panda_plugins[i].plugin;

        list_item->value = plugin_item;
        list_item->next = head;
        head = list_item;
    }
    return head;
}

void qmp_plugin_cmd(const char * cmd, Error **errp) {

}

// HMP
void hmp_panda_load_plugin(Monitor *mon, const QDict *qdict) {
    Error *err;
    const char *file_name   = qdict_get_try_str(qdict, "file_name");
    const char *plugin_name = qdict_get_try_str(qdict, "plugin_name");
    const char *plugin_args = qdict_get_try_str(qdict, "plugin_args");
    bool has_file_name   = file_name ? true : false;
    bool has_plugin_args = plugin_args ? true : false;
    qmp_load_plugin(has_file_name, file_name, plugin_name, has_plugin_args, plugin_args, &err);
}

void hmp_panda_unload_plugin(Monitor *mon, const QDict *qdict) {
    Error *err;
    const int index = qdict_get_try_int(qdict, "index", -1);
    qmp_unload_plugin(index, &err);
}

void hmp_panda_list_plugins(Monitor *mon, const QDict *qdict) {
    Error *err;
    PandaPluginInfoList *plugin_item = qmp_list_plugins(&err);
    monitor_printf(mon, "idx\t%-20s\taddr\n", "name");
    while (plugin_item != NULL){
        monitor_printf(mon, "%" PRId64 "\t%-20s\t%" PRIx64 "\n",
                       plugin_item->value->index, plugin_item->value->name,
                       plugin_item->value->address);
        plugin_item = plugin_item->next;

    }
}

void hmp_panda_plugin_cmd(Monitor *mon, const QDict *qdict) {
    panda_cb_list *plist;
    const char *cmd = qdict_get_try_str(qdict, "cmd");
    for(plist = panda_cbs[PANDA_CB_MONITOR]; plist != NULL; plist = panda_cb_list_next(plist)) {
        if (plist->enabled){
            plist->entry.monitor(plist->context, mon, cmd);
        }
    }
}

#endif // CONFIG_SOFTMMU
