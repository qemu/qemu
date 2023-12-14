/*
 * QEMU Module Infrastructure
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_MODULE_H
#define QEMU_MODULE_H


#define DSO_STAMP_FUN         glue(qemu_stamp, CONFIG_STAMP)
#define DSO_STAMP_FUN_STR     stringify(DSO_STAMP_FUN)

#ifdef BUILD_DSO
void DSO_STAMP_FUN(void);
/* This is a dummy symbol to identify a loaded DSO as a QEMU module, so we can
 * distinguish "version mismatch" from "not a QEMU module", when the stamp
 * check fails during module loading */
void qemu_module_dummy(void);

#define module_init(function, type)                                         \
static void __attribute__((constructor)) do_qemu_init_ ## function(void)    \
{                                                                           \
    register_dso_module_init(function, type);                               \
}
#else
/* This should not be used directly.  Use block_init etc. instead.  */
#define module_init(function, type)                                         \
static void __attribute__((constructor)) do_qemu_init_ ## function(void)    \
{                                                                           \
    register_module_init(function, type);                                   \
}
#endif

typedef enum {
    MODULE_INIT_MIGRATION,
    MODULE_INIT_BLOCK,
    MODULE_INIT_OPTS,
    MODULE_INIT_QOM,
    MODULE_INIT_TRACE,
    MODULE_INIT_XEN_BACKEND,
    MODULE_INIT_LIBQOS,
    MODULE_INIT_FUZZ_TARGET,
    MODULE_INIT_MAX
} module_init_type;

#define block_init(function) module_init(function, MODULE_INIT_BLOCK)
#define opts_init(function) module_init(function, MODULE_INIT_OPTS)
#define type_init(function) module_init(function, MODULE_INIT_QOM)
#define trace_init(function) module_init(function, MODULE_INIT_TRACE)
#define xen_backend_init(function) module_init(function, \
                                               MODULE_INIT_XEN_BACKEND)
#define libqos_init(function) module_init(function, MODULE_INIT_LIBQOS)
#define fuzz_target_init(function) module_init(function, \
                                               MODULE_INIT_FUZZ_TARGET)
#define migration_init(function) module_init(function, MODULE_INIT_MIGRATION)
#define block_module_load(lib, errp) module_load("block-", lib, errp)
#define ui_module_load(lib, errp) module_load("ui-", lib, errp)
#define audio_module_load(lib, errp) module_load("audio-", lib, errp)

void register_module_init(void (*fn)(void), module_init_type type);
void register_dso_module_init(void (*fn)(void), module_init_type type);

void module_call_init(module_init_type type);

/*
 * module_load: attempt to load a module from a set of directories
 *
 * directories searched are:
 * - getenv("QEMU_MODULE_DIR")
 * - get_relocated_path(CONFIG_QEMU_MODDIR);
 * - /var/run/qemu/${version_dir}
 *
 * prefix:         a subsystem prefix, or the empty string ("audio-", ..., "")
 * name:           name of the module
 * errp:           error to set in case the module is found, but load failed.
 *
 * Return value:   -1 on error (errp set if not NULL).
 *                 0 if module or one of its dependencies are not installed,
 *                 1 if the module is found and loaded,
 *                 2 if the module is already loaded, or module is built-in.
 */
int module_load(const char *prefix, const char *name, Error **errp);

/*
 * module_load_qom: attempt to load a module to provide a QOM type
 *
 * type:           the type to be provided
 * errp:           error to set.
 *
 * Return value:   as per module_load.
 */
int module_load_qom(const char *type, Error **errp);
void module_load_qom_all(void);
void module_allow_arch(const char *arch);

/**
 * DOC: module info annotation macros
 *
 * ``scripts/modinfo-collect.py`` will collect module info,
 * using the preprocessor and -DQEMU_MODINFO.
 *
 * ``scripts/modinfo-generate.py`` will create a module meta-data database
 * from the collected information so qemu knows about module
 * dependencies and QOM objects implemented by modules.
 *
 * See ``*.modinfo`` and ``modinfo.c`` in the build directory to check the
 * script results.
 */
#ifdef QEMU_MODINFO
# define modinfo(kind, value) \
    MODINFO_START kind value MODINFO_END
#else
# define modinfo(kind, value)
#endif

/**
 * module_obj
 *
 * @name: QOM type.
 *
 * This module implements QOM type @name.
 */
#define module_obj(name) modinfo(obj, name)

/**
 * module_dep
 *
 * @name: module name
 *
 * This module depends on module @name.
 */
#define module_dep(name) modinfo(dep, name)

/**
 * module_arch
 *
 * @name: target architecture
 *
 * This module is for target architecture @arch.
 *
 * Note that target-dependent modules are tagged automatically, so
 * this is only needed in case target-independent modules should be
 * restricted.  Use case example: the ccw bus is implemented by s390x
 * only.
 */
#define module_arch(name) modinfo(arch, name)

/**
 * module_opts
 *
 * @name: QemuOpts name
 *
 * This module registers QemuOpts @name.
 */
#define module_opts(name) modinfo(opts, name)

/**
 * module_kconfig
 *
 * @name: Kconfig requirement necessary to load the module
 *
 * This module requires a core module that should be implemented and
 * enabled in Kconfig.
 */
#define module_kconfig(name) modinfo(kconfig, name)

/*
 * module info database
 *
 * scripts/modinfo-generate.c will build this using the data collected
 * by scripts/modinfo-collect.py
 */
typedef struct QemuModinfo QemuModinfo;
struct QemuModinfo {
    const char *name;
    const char *arch;
    const char **objs;
    const char **deps;
    const char **opts;
};
extern const QemuModinfo qemu_modinfo[];
void module_init_info(const QemuModinfo *info);

#endif
