/*
 * Copyright (C) 2021, Ivanov Arkady <arkadiy.ivanov@ispras.ru>
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

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static char header[] = "DRCOV VERSION: 2\n"
                "DRCOV FLAVOR: drcov-64\n"
                "Module Table: version 2, count 1\n"
                "Columns: id, base, end, entry, path\n";

static FILE *fp;
static const char *file_name = "file.drcov.trace";
static GMutex lock;

typedef struct {
    uint32_t start;
    uint16_t size;
    uint16_t mod_id;
    bool     exec;
} bb_entry_t;

/* Translated blocks */
static GPtrArray *blocks;

static void printf_header(unsigned long count)
{
    fprintf(fp, "%s", header);
    const char *path = qemu_plugin_path_to_binary();
    uint64_t start_code = qemu_plugin_start_code();
    uint64_t end_code = qemu_plugin_end_code();
    uint64_t entry = qemu_plugin_entry_code();
    fprintf(fp, "0, 0x%lx, 0x%lx, 0x%lx, %s\n",
            start_code, end_code, entry, path);
    fprintf(fp, "BB Table: %ld bbs\n", count);
}

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


static void printf_el(gpointer data, gpointer user_data)
{
    bb_entry_t *bb = (bb_entry_t *)data;
    if (bb->exec) {
        printf_char_array32(bb->start);
        printf_char_array16(bb->size);
        printf_char_array16(bb->mod_id);
    }
    g_free(bb);
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
    g_mutex_lock(&lock);
    g_ptr_array_foreach(blocks, count_block, &count);

    /* Print function */
    printf_header(count);
    g_ptr_array_foreach(blocks, printf_el, NULL);

    /* Clear */
    g_ptr_array_free(blocks, true);

    fclose(fp);

    g_mutex_unlock(&lock);
}

static void plugin_init(void)
{
    fp = fopen(file_name, "wb");
    blocks = g_ptr_array_sized_new(128);
}

static void vcpu_tb_exec(unsigned int cpu_index, void *udata)
{
    bb_entry_t *bb = (bb_entry_t *) udata;

    g_mutex_lock(&lock);
    bb->exec = true;
    g_mutex_unlock(&lock);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    uint64_t pc = qemu_plugin_tb_vaddr(tb);
    size_t n = qemu_plugin_tb_n_insns(tb);

    g_mutex_lock(&lock);

    bb_entry_t *bb = g_new0(bb_entry_t, 1);
    for (int i = 0; i < n; i++) {
        bb->size += qemu_plugin_insn_size(qemu_plugin_tb_get_insn(tb, i));
    }

    bb->start = pc;
    bb->mod_id = 0;
    bb->exec = false;
    g_ptr_array_add(blocks, bb);

    g_mutex_unlock(&lock);
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
