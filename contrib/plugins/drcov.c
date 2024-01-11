/*
 * Copyright (C) 2021, Ivanov Arkady <arkadiy.ivanov@ispras.ru>
 * Copyright (C) 2023, Jean-Romain Garnier <jean-romain.garnier@airbus.com>
 *
 * Drcov - a DynamoRIO-based tool that collects coverage information
 * from a binary. Primary goal this script is to have coverage log
 * files that work in Lighthouse.
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>

#include <qemu-plugin.h>
#include <selfmap.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static FILE *fp;
static const char *file_name = "file.drcov.trace";
static GMutex bb_lock;
static GMutex mod_lock;

typedef struct {
    uint32_t start;
    uint16_t size;
    uint16_t mod_id;
    bool     exec;
} bb_entry_t;

typedef struct {
    uint16_t id;
    uint64_t base;
    uint64_t end;
    uint64_t entry;
    gchar*   path;
    bool     loaded;
} module_entry_t;

/* Translated blocks */
static GPtrArray *blocks;

/* Loaded modules */
static GPtrArray *modules;
static uint16_t next_mod_id = 0;

/* Plugin */

static void printf_char_array32(uint32_t data)
{
    const uint8_t *bytes = (const uint8_t *)(&data);
    fwrite(bytes, sizeof(char), sizeof(data), fp);
}

static void printf_char_array16(uint16_t data)
{
    const uint8_t *bytes = (const uint8_t *)(&data);
    fwrite(bytes, sizeof(char), sizeof(data), fp);
}

static void printf_mod(gpointer data, gpointer user_data)
{
    module_entry_t *mod = (module_entry_t *)data;
    fprintf(fp, "%d, 0x%" PRIx64 ", 0x%" PRIx64 ", 0x%" PRIx64 ", %s\n",
            mod->id, mod->base, mod->end, mod->entry, mod->path);
    g_free(mod);
}

static void printf_bb(gpointer data, gpointer user_data)
{
    bb_entry_t *bb = (bb_entry_t *)data;
    if (bb->exec) {
        printf_char_array32(bb->start);
        printf_char_array16(bb->size);
        printf_char_array16(bb->mod_id);
    }
    g_free(bb);
}

static void printf_header(unsigned long count)
{
    fprintf(fp, "DRCOV VERSION: 2\n");
    fprintf(fp, "DRCOV FLAVOR: drcov-64\n");
    fprintf(fp, "Module Table: version 2, count %d\n", modules->len);
    fprintf(fp, "Columns: id, base, end, entry, path\n");
    g_ptr_array_foreach(modules, printf_mod, NULL);
    fprintf(fp, "BB Table: %ld bbs\n", count);
}

static module_entry_t *create_mod_entry(MapInfo *info)
{
    module_entry_t *module = g_new0(module_entry_t, 1);
    module->id = next_mod_id++;
    module->base = info->start;
    module->end = info->end;
    module->entry = 0;
    module->path = g_strdup(info->path);
    module->loaded = true;
    return module;
}

static guint insert_mod_entry(module_entry_t *module, guint start_idx)
{
    module_entry_t *entry;
    guint i = start_idx;
    guint insert_idx = 0;

    // Find where to insert this modules, if it doesn't already exist, so we
    // keep the module list sorted
    while (i < modules->len) {
        entry = (module_entry_t *)modules->pdata[i];

        // If the new module ends before the current one starts, insert it here
        // to keep the modules array sorted
        if (entry->base >= module->end) {
            g_ptr_array_insert(modules, i, module);
            return i++;
        }

        // If the new module starts after the current one ends, we'll insert it
        // later
        if (entry->end <= module->base) {
            i++;
            continue;
        }

        // Now, two cases remain: the new module is the same as the current
        // entry, or the new module is different but has intersecting addresses

        // Start by checking if the two modules match
        if (
            entry->base == module->base
            && entry->end == module->end
            && !strcmp(entry->path, module->path)
        ) {
            // This module is already in the array, not need to insert it again
            entry->loaded = true;
            g_free(module);
            return i;
        }

        // We know this is a new module and there is at least one old module
        // with intersecting addresses

        // Mark all modules which start before the new one as unloaded
        // Note: there is no need to check entry->end because of the previous
        // checks
        while (entry->base < module->base && i < modules->len) {
            entry = (module_entry_t *)modules->pdata[i];
            entry->loaded = false;
            i++;
        }

        // This is the right place to insert the new module, so save this index
        insert_idx = i;

        // We still need to mark all the modules which start before the new one
        // ends as unloaded
        while (entry->base < module->end && i < modules->len) {
            entry = (module_entry_t *)modules->pdata[i];
            entry->loaded = false;
            i++;
        }

        // Finally, insert the new module
        g_ptr_array_insert(modules, insert_idx, module);
        return i++;
    }

    // If nowhere was found to insert the module, simply append it
    g_ptr_array_add(modules, module);
    return modules->len;
}

static void update_mod_entries(void)
{
    guint insert_idx;
    module_entry_t *module;
    GSList *maps, *iter;
    MapInfo *info;

    // Read modules from self_maps, which is unfortunately very slow, and insert
    // them in our internal array
    module = NULL;
    insert_idx = 0;
    maps = read_self_maps();
    for (iter = maps; iter; iter = g_slist_next(iter)) {
        info = (MapInfo *)iter->data;
        // We want to merge contiguous entries for the same file into a single
        // module
        if (NULL == module) {
            // There is no previous entry, create one and merge it later
            module = create_mod_entry(info);
        } else if (module->end == info->start && !strcmp(module->path, info->path)) {
            // This new entry can be merged with the existing module and
            // inserted later
            module->end = info->end;
            continue;
        } else if (strlen(info->path) > 0 && info->path[0] != '[') {
            // This is a different entry which also happens to be interesting,
            // so insert the previous one and create a new
            insert_idx = insert_mod_entry(module, insert_idx);
            module = create_mod_entry(info);
        }
    }

    // If there is a module left over, insert it now
    if (NULL != module) {
        insert_mod_entry(module, insert_idx);
   }

    free_self_maps(maps);
}

static module_entry_t *get_cached_exec_mod_entry(uint64_t pc)
{
    guint i;
    module_entry_t *entry;

    // Check if this address is contained within one of the modules we already
    // know about
    for (i = 0; i < modules->len; i++) {
        entry = (module_entry_t *)modules->pdata[i];
        if (pc >= entry->base && pc < entry->end && entry->loaded) {
            return entry;
        }
    }
    return NULL;
}

static module_entry_t *get_exec_mod_entry(uint64_t pc)
{
    module_entry_t *module = NULL;

    g_mutex_lock(&mod_lock);

    // Find module within which pc is contained
    // Important: This will not work properly if a module is dynamically loaded
    // (e.g. using dlopen), unloaded, and then another is loaded at the same
    // address
    module = get_cached_exec_mod_entry(pc);

    // If none is found, try to reload module list and look again
    if (NULL == module) {
        update_mod_entries();
        module = get_cached_exec_mod_entry(pc);
    }

    g_mutex_unlock(&mod_lock);
    return module;
}

static void count_block(gpointer data, gpointer user_data)
{
    unsigned long *count = (unsigned long *) user_data;
    bb_entry_t *bb = (bb_entry_t *)data;
    if (bb->exec) {
        *count = *count + 1;
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    unsigned long count = 0;
    g_mutex_lock(&bb_lock);
    g_mutex_lock(&mod_lock);
    g_ptr_array_foreach(blocks, count_block, &count);

    /* Print function */
    printf_header(count);
    g_ptr_array_foreach(blocks, printf_bb, NULL);

    /* Clear */
    g_ptr_array_free(blocks, true);
    g_ptr_array_free(modules, true);

    fclose(fp);

    g_mutex_unlock(&mod_lock);
    g_mutex_unlock(&bb_lock);
}

static void plugin_init(void)
{
    fp = fopen(file_name, "wb");
    blocks = g_ptr_array_sized_new(128);
    modules = g_ptr_array_sized_new(16);
}

static void vcpu_tb_exec(unsigned int cpu_index, void *udata)
{
    bb_entry_t *bb = (bb_entry_t *) udata;

    g_mutex_lock(&bb_lock);
    bb->exec = true;
    g_mutex_unlock(&bb_lock);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    uint64_t pc = qemu_plugin_tb_vaddr(tb);
    size_t n = qemu_plugin_tb_n_insns(tb);
    module_entry_t *module = get_exec_mod_entry(pc);
    bb_entry_t *bb = g_new0(bb_entry_t, 1);

    for (int i = 0; i < n; i++) {
        bb->size += qemu_plugin_insn_size(qemu_plugin_tb_get_insn(tb, i));
    }

    bb->start = module ? (pc - module->base): pc;
    bb->mod_id = module ? module->id: -1;
    bb->exec = false;

    g_mutex_lock(&bb_lock);
    g_ptr_array_add(blocks, bb);
    g_mutex_unlock(&bb_lock);

    qemu_plugin_register_vcpu_tb_exec_cb(tb, vcpu_tb_exec,
                                         QEMU_PLUGIN_CB_NO_REGS,
                                         (void *)bb);

}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        g_auto(GStrv) tokens = g_strsplit(argv[i], "=", 2);
        if (g_strcmp0(tokens[0], "filename") == 0) {
            file_name = g_strdup(tokens[1]);
        }
    }

    plugin_init();

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    return 0;
}
