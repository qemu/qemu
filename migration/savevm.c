/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2009-2015 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "net/net.h"
#include "migration.h"
#include "migration/snapshot.h"
#include "migration/vmstate.h"
#include "migration/misc.h"
#include "migration/register.h"
#include "migration/global_state.h"
#include "ram.h"
#include "qemu-file-channel.h"
#include "qemu-file.h"
#include "savevm.h"
#include "postcopy-ram.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"
#include "sysemu/cpus.h"
#include "exec/memory.h"
#include "exec/target_page.h"
#include "trace.h"
#include "qemu/iov.h"
#include "qemu/main-loop.h"
#include "block/snapshot.h"
#include "qemu/cutils.h"
#include "io/channel-buffer.h"
#include "io/channel-file.h"
#include "sysemu/replay.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"
#include "sysemu/xen.h"
#include "qjson.h"
#include "migration/colo.h"
#include "qemu/bitmap.h"
#include "net/announce.h"

const unsigned int postcopy_ram_discard_version = 0;

/* Subcommands for QEMU_VM_COMMAND */
enum qemu_vm_cmd {
    MIG_CMD_INVALID = 0,   /* Must be 0 */
    MIG_CMD_OPEN_RETURN_PATH,  /* Tell the dest to open the Return path */
    MIG_CMD_PING,              /* Request a PONG on the RP */

    MIG_CMD_POSTCOPY_ADVISE,       /* Prior to any page transfers, just
                                      warn we might want to do PC */
    MIG_CMD_POSTCOPY_LISTEN,       /* Start listening for incoming
                                      pages as it's running. */
    MIG_CMD_POSTCOPY_RUN,          /* Start execution */

    MIG_CMD_POSTCOPY_RAM_DISCARD,  /* A list of pages to discard that
                                      were previously sent during
                                      precopy but are dirty. */
    MIG_CMD_PACKAGED,          /* Send a wrapped stream within this stream */
    MIG_CMD_ENABLE_COLO,       /* Enable COLO */
    MIG_CMD_POSTCOPY_RESUME,   /* resume postcopy on dest */
    MIG_CMD_RECV_BITMAP,       /* Request for recved bitmap on dst */
    MIG_CMD_MAX
};

#define MAX_VM_CMD_PACKAGED_SIZE UINT32_MAX
static struct mig_cmd_args {
    ssize_t     len; /* -1 = variable */
    const char *name;
} mig_cmd_args[] = {
    [MIG_CMD_INVALID]          = { .len = -1, .name = "INVALID" },
    [MIG_CMD_OPEN_RETURN_PATH] = { .len =  0, .name = "OPEN_RETURN_PATH" },
    [MIG_CMD_PING]             = { .len = sizeof(uint32_t), .name = "PING" },
    [MIG_CMD_POSTCOPY_ADVISE]  = { .len = -1, .name = "POSTCOPY_ADVISE" },
    [MIG_CMD_POSTCOPY_LISTEN]  = { .len =  0, .name = "POSTCOPY_LISTEN" },
    [MIG_CMD_POSTCOPY_RUN]     = { .len =  0, .name = "POSTCOPY_RUN" },
    [MIG_CMD_POSTCOPY_RAM_DISCARD] = {
                                   .len = -1, .name = "POSTCOPY_RAM_DISCARD" },
    [MIG_CMD_POSTCOPY_RESUME]  = { .len =  0, .name = "POSTCOPY_RESUME" },
    [MIG_CMD_PACKAGED]         = { .len =  4, .name = "PACKAGED" },
    [MIG_CMD_RECV_BITMAP]      = { .len = -1, .name = "RECV_BITMAP" },
    [MIG_CMD_MAX]              = { .len = -1, .name = "MAX" },
};

/* Note for MIG_CMD_POSTCOPY_ADVISE:
 * The format of arguments is depending on postcopy mode:
 * - postcopy RAM only
 *   uint64_t host page size
 *   uint64_t taget page size
 *
 * - postcopy RAM and postcopy dirty bitmaps
 *   format is the same as for postcopy RAM only
 *
 * - postcopy dirty bitmaps only
 *   Nothing. Command length field is 0.
 *
 * Be careful: adding a new postcopy entity with some other parameters should
 * not break format self-description ability. Good way is to introduce some
 * generic extendable format with an exception for two old entities.
 */

/***********************************************************/
/* savevm/loadvm support */

static ssize_t block_writev_buffer(void *opaque, struct iovec *iov, int iovcnt,
                                   int64_t pos, Error **errp)
{
    int ret;
    QEMUIOVector qiov;

    qemu_iovec_init_external(&qiov, iov, iovcnt);
    ret = bdrv_writev_vmstate(opaque, &qiov, pos);
    if (ret < 0) {
        return ret;
    }

    return qiov.size;
}

static ssize_t block_get_buffer(void *opaque, uint8_t *buf, int64_t pos,
                                size_t size, Error **errp)
{
    return bdrv_load_vmstate(opaque, buf, pos, size);
}

static int bdrv_fclose(void *opaque, Error **errp)
{
    return bdrv_flush(opaque);
}

static const QEMUFileOps bdrv_read_ops = {
    .get_buffer = block_get_buffer,
    .close =      bdrv_fclose
};

static const QEMUFileOps bdrv_write_ops = {
    .writev_buffer  = block_writev_buffer,
    .close          = bdrv_fclose
};

static QEMUFile *qemu_fopen_bdrv(BlockDriverState *bs, int is_writable)
{
    if (is_writable) {
        return qemu_fopen_ops(bs, &bdrv_write_ops);
    }
    return qemu_fopen_ops(bs, &bdrv_read_ops);
}


/* QEMUFile timer support.
 * Not in qemu-file.c to not add qemu-timer.c as dependency to qemu-file.c
 */

void timer_put(QEMUFile *f, QEMUTimer *ts)
{
    uint64_t expire_time;

    expire_time = timer_expire_time_ns(ts);
    qemu_put_be64(f, expire_time);
}

void timer_get(QEMUFile *f, QEMUTimer *ts)
{
    uint64_t expire_time;

    expire_time = qemu_get_be64(f);
    if (expire_time != -1) {
        timer_mod_ns(ts, expire_time);
    } else {
        timer_del(ts);
    }
}


/* VMState timer support.
 * Not in vmstate.c to not add qemu-timer.c as dependency to vmstate.c
 */

static int get_timer(QEMUFile *f, void *pv, size_t size,
                     const VMStateField *field)
{
    QEMUTimer *v = pv;
    timer_get(f, v);
    return 0;
}

static int put_timer(QEMUFile *f, void *pv, size_t size,
                     const VMStateField *field, QJSON *vmdesc)
{
    QEMUTimer *v = pv;
    timer_put(f, v);

    return 0;
}

const VMStateInfo vmstate_info_timer = {
    .name = "timer",
    .get  = get_timer,
    .put  = put_timer,
};


typedef struct CompatEntry {
    char idstr[256];
    int instance_id;
} CompatEntry;

typedef struct SaveStateEntry {
    QTAILQ_ENTRY(SaveStateEntry) entry;
    char idstr[256];
    uint32_t instance_id;
    int alias_id;
    int version_id;
    /* version id read from the stream */
    int load_version_id;
    int section_id;
    /* section id read from the stream */
    int load_section_id;
    const SaveVMHandlers *ops;
    const VMStateDescription *vmsd;
    void *opaque;
    CompatEntry *compat;
    int is_ram;
} SaveStateEntry;

typedef struct SaveState {
    QTAILQ_HEAD(, SaveStateEntry) handlers;
    SaveStateEntry *handler_pri_head[MIG_PRI_MAX + 1];
    int global_section_id;
    uint32_t len;
    const char *name;
    uint32_t target_page_bits;
    uint32_t caps_count;
    MigrationCapability *capabilities;
    QemuUUID uuid;
} SaveState;

static SaveState savevm_state = {
    .handlers = QTAILQ_HEAD_INITIALIZER(savevm_state.handlers),
    .handler_pri_head = { [MIG_PRI_DEFAULT ... MIG_PRI_MAX] = NULL },
    .global_section_id = 0,
};

static bool should_validate_capability(int capability)
{
    assert(capability >= 0 && capability < MIGRATION_CAPABILITY__MAX);
    /* Validate only new capabilities to keep compatibility. */
    switch (capability) {
    case MIGRATION_CAPABILITY_X_IGNORE_SHARED:
        return true;
    default:
        return false;
    }
}

static uint32_t get_validatable_capabilities_count(void)
{
    MigrationState *s = migrate_get_current();
    uint32_t result = 0;
    int i;
    for (i = 0; i < MIGRATION_CAPABILITY__MAX; i++) {
        if (should_validate_capability(i) && s->enabled_capabilities[i]) {
            result++;
        }
    }
    return result;
}

static int configuration_pre_save(void *opaque)
{
    SaveState *state = opaque;
    const char *current_name = MACHINE_GET_CLASS(current_machine)->name;
    MigrationState *s = migrate_get_current();
    int i, j;

    state->len = strlen(current_name);
    state->name = current_name;
    state->target_page_bits = qemu_target_page_bits();

    state->caps_count = get_validatable_capabilities_count();
    state->capabilities = g_renew(MigrationCapability, state->capabilities,
                                  state->caps_count);
    for (i = j = 0; i < MIGRATION_CAPABILITY__MAX; i++) {
        if (should_validate_capability(i) && s->enabled_capabilities[i]) {
            state->capabilities[j++] = i;
        }
    }
    state->uuid = qemu_uuid;

    return 0;
}

static int configuration_pre_load(void *opaque)
{
    SaveState *state = opaque;

    /* If there is no target-page-bits subsection it means the source
     * predates the variable-target-page-bits support and is using the
     * minimum possible value for this CPU.
     */
    state->target_page_bits = qemu_target_page_bits_min();
    return 0;
}

static bool configuration_validate_capabilities(SaveState *state)
{
    bool ret = true;
    MigrationState *s = migrate_get_current();
    unsigned long *source_caps_bm;
    int i;

    source_caps_bm = bitmap_new(MIGRATION_CAPABILITY__MAX);
    for (i = 0; i < state->caps_count; i++) {
        MigrationCapability capability = state->capabilities[i];
        set_bit(capability, source_caps_bm);
    }

    for (i = 0; i < MIGRATION_CAPABILITY__MAX; i++) {
        bool source_state, target_state;
        if (!should_validate_capability(i)) {
            continue;
        }
        source_state = test_bit(i, source_caps_bm);
        target_state = s->enabled_capabilities[i];
        if (source_state != target_state) {
            error_report("Capability %s is %s, but received capability is %s",
                         MigrationCapability_str(i),
                         target_state ? "on" : "off",
                         source_state ? "on" : "off");
            ret = false;
            /* Don't break here to report all failed capabilities */
        }
    }

    g_free(source_caps_bm);
    return ret;
}

static int configuration_post_load(void *opaque, int version_id)
{
    SaveState *state = opaque;
    const char *current_name = MACHINE_GET_CLASS(current_machine)->name;

    if (strncmp(state->name, current_name, state->len) != 0) {
        error_report("Machine type received is '%.*s' and local is '%s'",
                     (int) state->len, state->name, current_name);
        return -EINVAL;
    }

    if (state->target_page_bits != qemu_target_page_bits()) {
        error_report("Received TARGET_PAGE_BITS is %d but local is %d",
                     state->target_page_bits, qemu_target_page_bits());
        return -EINVAL;
    }

    if (!configuration_validate_capabilities(state)) {
        return -EINVAL;
    }

    return 0;
}

static int get_capability(QEMUFile *f, void *pv, size_t size,
                          const VMStateField *field)
{
    MigrationCapability *capability = pv;
    char capability_str[UINT8_MAX + 1];
    uint8_t len;
    int i;

    len = qemu_get_byte(f);
    qemu_get_buffer(f, (uint8_t *)capability_str, len);
    capability_str[len] = '\0';
    for (i = 0; i < MIGRATION_CAPABILITY__MAX; i++) {
        if (!strcmp(MigrationCapability_str(i), capability_str)) {
            *capability = i;
            return 0;
        }
    }
    error_report("Received unknown capability %s", capability_str);
    return -EINVAL;
}

static int put_capability(QEMUFile *f, void *pv, size_t size,
                          const VMStateField *field, QJSON *vmdesc)
{
    MigrationCapability *capability = pv;
    const char *capability_str = MigrationCapability_str(*capability);
    size_t len = strlen(capability_str);
    assert(len <= UINT8_MAX);

    qemu_put_byte(f, len);
    qemu_put_buffer(f, (uint8_t *)capability_str, len);
    return 0;
}

static const VMStateInfo vmstate_info_capability = {
    .name = "capability",
    .get  = get_capability,
    .put  = put_capability,
};

/* The target-page-bits subsection is present only if the
 * target page size is not the same as the default (ie the
 * minimum page size for a variable-page-size guest CPU).
 * If it is present then it contains the actual target page
 * bits for the machine, and migration will fail if the
 * two ends don't agree about it.
 */
static bool vmstate_target_page_bits_needed(void *opaque)
{
    return qemu_target_page_bits()
        > qemu_target_page_bits_min();
}

static const VMStateDescription vmstate_target_page_bits = {
    .name = "configuration/target-page-bits",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vmstate_target_page_bits_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(target_page_bits, SaveState),
        VMSTATE_END_OF_LIST()
    }
};

static bool vmstate_capabilites_needed(void *opaque)
{
    return get_validatable_capabilities_count() > 0;
}

static const VMStateDescription vmstate_capabilites = {
    .name = "configuration/capabilities",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vmstate_capabilites_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_V(caps_count, SaveState, 1),
        VMSTATE_VARRAY_UINT32_ALLOC(capabilities, SaveState, caps_count, 1,
                                    vmstate_info_capability,
                                    MigrationCapability),
        VMSTATE_END_OF_LIST()
    }
};

static bool vmstate_uuid_needed(void *opaque)
{
    return qemu_uuid_set && migrate_validate_uuid();
}

static int vmstate_uuid_post_load(void *opaque, int version_id)
{
    SaveState *state = opaque;
    char uuid_src[UUID_FMT_LEN + 1];
    char uuid_dst[UUID_FMT_LEN + 1];

    if (!qemu_uuid_set) {
        /*
         * It's warning because user might not know UUID in some cases,
         * e.g. load an old snapshot
         */
        qemu_uuid_unparse(&state->uuid, uuid_src);
        warn_report("UUID is received %s, but local uuid isn't set",
                     uuid_src);
        return 0;
    }
    if (!qemu_uuid_is_equal(&state->uuid, &qemu_uuid)) {
        qemu_uuid_unparse(&state->uuid, uuid_src);
        qemu_uuid_unparse(&qemu_uuid, uuid_dst);
        error_report("UUID received is %s and local is %s", uuid_src, uuid_dst);
        return -EINVAL;
    }
    return 0;
}

static const VMStateDescription vmstate_uuid = {
    .name = "configuration/uuid",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vmstate_uuid_needed,
    .post_load = vmstate_uuid_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY_V(uuid.data, SaveState, sizeof(QemuUUID), 1),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_configuration = {
    .name = "configuration",
    .version_id = 1,
    .pre_load = configuration_pre_load,
    .post_load = configuration_post_load,
    .pre_save = configuration_pre_save,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(len, SaveState),
        VMSTATE_VBUFFER_ALLOC_UINT32(name, SaveState, 0, NULL, len),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_target_page_bits,
        &vmstate_capabilites,
        &vmstate_uuid,
        NULL
    }
};

static void dump_vmstate_vmsd(FILE *out_file,
                              const VMStateDescription *vmsd, int indent,
                              bool is_subsection);

static void dump_vmstate_vmsf(FILE *out_file, const VMStateField *field,
                              int indent)
{
    fprintf(out_file, "%*s{\n", indent, "");
    indent += 2;
    fprintf(out_file, "%*s\"field\": \"%s\",\n", indent, "", field->name);
    fprintf(out_file, "%*s\"version_id\": %d,\n", indent, "",
            field->version_id);
    fprintf(out_file, "%*s\"field_exists\": %s,\n", indent, "",
            field->field_exists ? "true" : "false");
    fprintf(out_file, "%*s\"size\": %zu", indent, "", field->size);
    if (field->vmsd != NULL) {
        fprintf(out_file, ",\n");
        dump_vmstate_vmsd(out_file, field->vmsd, indent, false);
    }
    fprintf(out_file, "\n%*s}", indent - 2, "");
}

static void dump_vmstate_vmss(FILE *out_file,
                              const VMStateDescription **subsection,
                              int indent)
{
    if (*subsection != NULL) {
        dump_vmstate_vmsd(out_file, *subsection, indent, true);
    }
}

static void dump_vmstate_vmsd(FILE *out_file,
                              const VMStateDescription *vmsd, int indent,
                              bool is_subsection)
{
    if (is_subsection) {
        fprintf(out_file, "%*s{\n", indent, "");
    } else {
        fprintf(out_file, "%*s\"%s\": {\n", indent, "", "Description");
    }
    indent += 2;
    fprintf(out_file, "%*s\"name\": \"%s\",\n", indent, "", vmsd->name);
    fprintf(out_file, "%*s\"version_id\": %d,\n", indent, "",
            vmsd->version_id);
    fprintf(out_file, "%*s\"minimum_version_id\": %d", indent, "",
            vmsd->minimum_version_id);
    if (vmsd->fields != NULL) {
        const VMStateField *field = vmsd->fields;
        bool first;

        fprintf(out_file, ",\n%*s\"Fields\": [\n", indent, "");
        first = true;
        while (field->name != NULL) {
            if (field->flags & VMS_MUST_EXIST) {
                /* Ignore VMSTATE_VALIDATE bits; these don't get migrated */
                field++;
                continue;
            }
            if (!first) {
                fprintf(out_file, ",\n");
            }
            dump_vmstate_vmsf(out_file, field, indent + 2);
            field++;
            first = false;
        }
        fprintf(out_file, "\n%*s]", indent, "");
    }
    if (vmsd->subsections != NULL) {
        const VMStateDescription **subsection = vmsd->subsections;
        bool first;

        fprintf(out_file, ",\n%*s\"Subsections\": [\n", indent, "");
        first = true;
        while (*subsection != NULL) {
            if (!first) {
                fprintf(out_file, ",\n");
            }
            dump_vmstate_vmss(out_file, subsection, indent + 2);
            subsection++;
            first = false;
        }
        fprintf(out_file, "\n%*s]", indent, "");
    }
    fprintf(out_file, "\n%*s}", indent - 2, "");
}

static void dump_machine_type(FILE *out_file)
{
    MachineClass *mc;

    mc = MACHINE_GET_CLASS(current_machine);

    fprintf(out_file, "  \"vmschkmachine\": {\n");
    fprintf(out_file, "    \"Name\": \"%s\"\n", mc->name);
    fprintf(out_file, "  },\n");
}

void dump_vmstate_json_to_file(FILE *out_file)
{
    GSList *list, *elt;
    bool first;

    fprintf(out_file, "{\n");
    dump_machine_type(out_file);

    first = true;
    list = object_class_get_list(TYPE_DEVICE, true);
    for (elt = list; elt; elt = elt->next) {
        DeviceClass *dc = OBJECT_CLASS_CHECK(DeviceClass, elt->data,
                                             TYPE_DEVICE);
        const char *name;
        int indent = 2;

        if (!dc->vmsd) {
            continue;
        }

        if (!first) {
            fprintf(out_file, ",\n");
        }
        name = object_class_get_name(OBJECT_CLASS(dc));
        fprintf(out_file, "%*s\"%s\": {\n", indent, "", name);
        indent += 2;
        fprintf(out_file, "%*s\"Name\": \"%s\",\n", indent, "", name);
        fprintf(out_file, "%*s\"version_id\": %d,\n", indent, "",
                dc->vmsd->version_id);
        fprintf(out_file, "%*s\"minimum_version_id\": %d,\n", indent, "",
                dc->vmsd->minimum_version_id);

        dump_vmstate_vmsd(out_file, dc->vmsd, indent, false);

        fprintf(out_file, "\n%*s}", indent - 2, "");
        first = false;
    }
    fprintf(out_file, "\n}\n");
    fclose(out_file);
    g_slist_free(list);
}

static uint32_t calculate_new_instance_id(const char *idstr)
{
    SaveStateEntry *se;
    uint32_t instance_id = 0;

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (strcmp(idstr, se->idstr) == 0
            && instance_id <= se->instance_id) {
            instance_id = se->instance_id + 1;
        }
    }
    /* Make sure we never loop over without being noticed */
    assert(instance_id != VMSTATE_INSTANCE_ID_ANY);
    return instance_id;
}

static int calculate_compat_instance_id(const char *idstr)
{
    SaveStateEntry *se;
    int instance_id = 0;

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!se->compat) {
            continue;
        }

        if (strcmp(idstr, se->compat->idstr) == 0
            && instance_id <= se->compat->instance_id) {
            instance_id = se->compat->instance_id + 1;
        }
    }
    return instance_id;
}

static inline MigrationPriority save_state_priority(SaveStateEntry *se)
{
    if (se->vmsd) {
        return se->vmsd->priority;
    }
    return MIG_PRI_DEFAULT;
}

static void savevm_state_handler_insert(SaveStateEntry *nse)
{
    MigrationPriority priority = save_state_priority(nse);
    SaveStateEntry *se;
    int i;

    assert(priority <= MIG_PRI_MAX);

    for (i = priority - 1; i >= 0; i--) {
        se = savevm_state.handler_pri_head[i];
        if (se != NULL) {
            assert(save_state_priority(se) < priority);
            break;
        }
    }

    if (i >= 0) {
        QTAILQ_INSERT_BEFORE(se, nse, entry);
    } else {
        QTAILQ_INSERT_TAIL(&savevm_state.handlers, nse, entry);
    }

    if (savevm_state.handler_pri_head[priority] == NULL) {
        savevm_state.handler_pri_head[priority] = nse;
    }
}

static void savevm_state_handler_remove(SaveStateEntry *se)
{
    SaveStateEntry *next;
    MigrationPriority priority = save_state_priority(se);

    if (se == savevm_state.handler_pri_head[priority]) {
        next = QTAILQ_NEXT(se, entry);
        if (next != NULL && save_state_priority(next) == priority) {
            savevm_state.handler_pri_head[priority] = next;
        } else {
            savevm_state.handler_pri_head[priority] = NULL;
        }
    }
    QTAILQ_REMOVE(&savevm_state.handlers, se, entry);
}

/* TODO: Individual devices generally have very little idea about the rest
   of the system, so instance_id should be removed/replaced.
   Meanwhile pass -1 as instance_id if you do not already have a clearly
   distinguishing id for all instances of your device class. */
int register_savevm_live(const char *idstr,
                         uint32_t instance_id,
                         int version_id,
                         const SaveVMHandlers *ops,
                         void *opaque)
{
    SaveStateEntry *se;

    se = g_new0(SaveStateEntry, 1);
    se->version_id = version_id;
    se->section_id = savevm_state.global_section_id++;
    se->ops = ops;
    se->opaque = opaque;
    se->vmsd = NULL;
    /* if this is a live_savem then set is_ram */
    if (ops->save_setup != NULL) {
        se->is_ram = 1;
    }

    pstrcat(se->idstr, sizeof(se->idstr), idstr);

    if (instance_id == VMSTATE_INSTANCE_ID_ANY) {
        se->instance_id = calculate_new_instance_id(se->idstr);
    } else {
        se->instance_id = instance_id;
    }
    assert(!se->compat || se->instance_id == 0);
    savevm_state_handler_insert(se);
    return 0;
}

void unregister_savevm(VMStateIf *obj, const char *idstr, void *opaque)
{
    SaveStateEntry *se, *new_se;
    char id[256] = "";

    if (obj) {
        char *oid = vmstate_if_get_id(obj);
        if (oid) {
            pstrcpy(id, sizeof(id), oid);
            pstrcat(id, sizeof(id), "/");
            g_free(oid);
        }
    }
    pstrcat(id, sizeof(id), idstr);

    QTAILQ_FOREACH_SAFE(se, &savevm_state.handlers, entry, new_se) {
        if (strcmp(se->idstr, id) == 0 && se->opaque == opaque) {
            savevm_state_handler_remove(se);
            g_free(se->compat);
            g_free(se);
        }
    }
}

int vmstate_register_with_alias_id(VMStateIf *obj, uint32_t instance_id,
                                   const VMStateDescription *vmsd,
                                   void *opaque, int alias_id,
                                   int required_for_version,
                                   Error **errp)
{
    SaveStateEntry *se;

    /* If this triggers, alias support can be dropped for the vmsd. */
    assert(alias_id == -1 || required_for_version >= vmsd->minimum_version_id);

    se = g_new0(SaveStateEntry, 1);
    se->version_id = vmsd->version_id;
    se->section_id = savevm_state.global_section_id++;
    se->opaque = opaque;
    se->vmsd = vmsd;
    se->alias_id = alias_id;

    if (obj) {
        char *id = vmstate_if_get_id(obj);
        if (id) {
            if (snprintf(se->idstr, sizeof(se->idstr), "%s/", id) >=
                sizeof(se->idstr)) {
                error_setg(errp, "Path too long for VMState (%s)", id);
                g_free(id);
                g_free(se);

                return -1;
            }
            g_free(id);

            se->compat = g_new0(CompatEntry, 1);
            pstrcpy(se->compat->idstr, sizeof(se->compat->idstr), vmsd->name);
            se->compat->instance_id = instance_id == VMSTATE_INSTANCE_ID_ANY ?
                         calculate_compat_instance_id(vmsd->name) : instance_id;
            instance_id = VMSTATE_INSTANCE_ID_ANY;
        }
    }
    pstrcat(se->idstr, sizeof(se->idstr), vmsd->name);

    if (instance_id == VMSTATE_INSTANCE_ID_ANY) {
        se->instance_id = calculate_new_instance_id(se->idstr);
    } else {
        se->instance_id = instance_id;
    }
    assert(!se->compat || se->instance_id == 0);
    savevm_state_handler_insert(se);
    return 0;
}

void vmstate_unregister(VMStateIf *obj, const VMStateDescription *vmsd,
                        void *opaque)
{
    SaveStateEntry *se, *new_se;

    QTAILQ_FOREACH_SAFE(se, &savevm_state.handlers, entry, new_se) {
        if (se->vmsd == vmsd && se->opaque == opaque) {
            savevm_state_handler_remove(se);
            g_free(se->compat);
            g_free(se);
        }
    }
}

static int vmstate_load(QEMUFile *f, SaveStateEntry *se)
{
    trace_vmstate_load(se->idstr, se->vmsd ? se->vmsd->name : "(old)");
    if (!se->vmsd) {         /* Old style */
        return se->ops->load_state(f, se->opaque, se->load_version_id);
    }
    return vmstate_load_state(f, se->vmsd, se->opaque, se->load_version_id);
}

static void vmstate_save_old_style(QEMUFile *f, SaveStateEntry *se, QJSON *vmdesc)
{
    int64_t old_offset, size;

    old_offset = qemu_ftell_fast(f);
    se->ops->save_state(f, se->opaque);
    size = qemu_ftell_fast(f) - old_offset;

    if (vmdesc) {
        json_prop_int(vmdesc, "size", size);
        json_start_array(vmdesc, "fields");
        json_start_object(vmdesc, NULL);
        json_prop_str(vmdesc, "name", "data");
        json_prop_int(vmdesc, "size", size);
        json_prop_str(vmdesc, "type", "buffer");
        json_end_object(vmdesc);
        json_end_array(vmdesc);
    }
}

static int vmstate_save(QEMUFile *f, SaveStateEntry *se, QJSON *vmdesc)
{
    trace_vmstate_save(se->idstr, se->vmsd ? se->vmsd->name : "(old)");
    if (!se->vmsd) {
        vmstate_save_old_style(f, se, vmdesc);
        return 0;
    }
    return vmstate_save_state(f, se->vmsd, se->opaque, vmdesc);
}

/*
 * Write the header for device section (QEMU_VM_SECTION START/END/PART/FULL)
 */
static void save_section_header(QEMUFile *f, SaveStateEntry *se,
                                uint8_t section_type)
{
    qemu_put_byte(f, section_type);
    qemu_put_be32(f, se->section_id);

    if (section_type == QEMU_VM_SECTION_FULL ||
        section_type == QEMU_VM_SECTION_START) {
        /* ID string */
        size_t len = strlen(se->idstr);
        qemu_put_byte(f, len);
        qemu_put_buffer(f, (uint8_t *)se->idstr, len);

        qemu_put_be32(f, se->instance_id);
        qemu_put_be32(f, se->version_id);
    }
}

/*
 * Write a footer onto device sections that catches cases misformatted device
 * sections.
 */
static void save_section_footer(QEMUFile *f, SaveStateEntry *se)
{
    if (migrate_get_current()->send_section_footer) {
        qemu_put_byte(f, QEMU_VM_SECTION_FOOTER);
        qemu_put_be32(f, se->section_id);
    }
}

/**
 * qemu_savevm_command_send: Send a 'QEMU_VM_COMMAND' type element with the
 *                           command and associated data.
 *
 * @f: File to send command on
 * @command: Command type to send
 * @len: Length of associated data
 * @data: Data associated with command.
 */
static void qemu_savevm_command_send(QEMUFile *f,
                                     enum qemu_vm_cmd command,
                                     uint16_t len,
                                     uint8_t *data)
{
    trace_savevm_command_send(command, len);
    qemu_put_byte(f, QEMU_VM_COMMAND);
    qemu_put_be16(f, (uint16_t)command);
    qemu_put_be16(f, len);
    qemu_put_buffer(f, data, len);
    qemu_fflush(f);
}

void qemu_savevm_send_colo_enable(QEMUFile *f)
{
    trace_savevm_send_colo_enable();
    qemu_savevm_command_send(f, MIG_CMD_ENABLE_COLO, 0, NULL);
}

void qemu_savevm_send_ping(QEMUFile *f, uint32_t value)
{
    uint32_t buf;

    trace_savevm_send_ping(value);
    buf = cpu_to_be32(value);
    qemu_savevm_command_send(f, MIG_CMD_PING, sizeof(value), (uint8_t *)&buf);
}

void qemu_savevm_send_open_return_path(QEMUFile *f)
{
    trace_savevm_send_open_return_path();
    qemu_savevm_command_send(f, MIG_CMD_OPEN_RETURN_PATH, 0, NULL);
}

/* We have a buffer of data to send; we don't want that all to be loaded
 * by the command itself, so the command contains just the length of the
 * extra buffer that we then send straight after it.
 * TODO: Must be a better way to organise that
 *
 * Returns:
 *    0 on success
 *    -ve on error
 */
int qemu_savevm_send_packaged(QEMUFile *f, const uint8_t *buf, size_t len)
{
    uint32_t tmp;

    if (len > MAX_VM_CMD_PACKAGED_SIZE) {
        error_report("%s: Unreasonably large packaged state: %zu",
                     __func__, len);
        return -1;
    }

    tmp = cpu_to_be32(len);

    trace_qemu_savevm_send_packaged();
    qemu_savevm_command_send(f, MIG_CMD_PACKAGED, 4, (uint8_t *)&tmp);

    qemu_put_buffer(f, buf, len);

    return 0;
}

/* Send prior to any postcopy transfer */
void qemu_savevm_send_postcopy_advise(QEMUFile *f)
{
    if (migrate_postcopy_ram()) {
        uint64_t tmp[2];
        tmp[0] = cpu_to_be64(ram_pagesize_summary());
        tmp[1] = cpu_to_be64(qemu_target_page_size());

        trace_qemu_savevm_send_postcopy_advise();
        qemu_savevm_command_send(f, MIG_CMD_POSTCOPY_ADVISE,
                                 16, (uint8_t *)tmp);
    } else {
        qemu_savevm_command_send(f, MIG_CMD_POSTCOPY_ADVISE, 0, NULL);
    }
}

/* Sent prior to starting the destination running in postcopy, discard pages
 * that have already been sent but redirtied on the source.
 * CMD_POSTCOPY_RAM_DISCARD consist of:
 *      byte   version (0)
 *      byte   Length of name field (not including 0)
 *  n x byte   RAM block name
 *      byte   0 terminator (just for safety)
 *  n x        Byte ranges within the named RAMBlock
 *      be64   Start of the range
 *      be64   Length
 *
 *  name:  RAMBlock name that these entries are part of
 *  len: Number of page entries
 *  start_list: 'len' addresses
 *  length_list: 'len' addresses
 *
 */
void qemu_savevm_send_postcopy_ram_discard(QEMUFile *f, const char *name,
                                           uint16_t len,
                                           uint64_t *start_list,
                                           uint64_t *length_list)
{
    uint8_t *buf;
    uint16_t tmplen;
    uint16_t t;
    size_t name_len = strlen(name);

    trace_qemu_savevm_send_postcopy_ram_discard(name, len);
    assert(name_len < 256);
    buf = g_malloc0(1 + 1 + name_len + 1 + (8 + 8) * len);
    buf[0] = postcopy_ram_discard_version;
    buf[1] = name_len;
    memcpy(buf + 2, name, name_len);
    tmplen = 2 + name_len;
    buf[tmplen++] = '\0';

    for (t = 0; t < len; t++) {
        stq_be_p(buf + tmplen, start_list[t]);
        tmplen += 8;
        stq_be_p(buf + tmplen, length_list[t]);
        tmplen += 8;
    }
    qemu_savevm_command_send(f, MIG_CMD_POSTCOPY_RAM_DISCARD, tmplen, buf);
    g_free(buf);
}

/* Get the destination into a state where it can receive postcopy data. */
void qemu_savevm_send_postcopy_listen(QEMUFile *f)
{
    trace_savevm_send_postcopy_listen();
    qemu_savevm_command_send(f, MIG_CMD_POSTCOPY_LISTEN, 0, NULL);
}

/* Kick the destination into running */
void qemu_savevm_send_postcopy_run(QEMUFile *f)
{
    trace_savevm_send_postcopy_run();
    qemu_savevm_command_send(f, MIG_CMD_POSTCOPY_RUN, 0, NULL);
}

void qemu_savevm_send_postcopy_resume(QEMUFile *f)
{
    trace_savevm_send_postcopy_resume();
    qemu_savevm_command_send(f, MIG_CMD_POSTCOPY_RESUME, 0, NULL);
}

void qemu_savevm_send_recv_bitmap(QEMUFile *f, char *block_name)
{
    size_t len;
    char buf[256];

    trace_savevm_send_recv_bitmap(block_name);

    buf[0] = len = strlen(block_name);
    memcpy(buf + 1, block_name, len);

    qemu_savevm_command_send(f, MIG_CMD_RECV_BITMAP, len + 1, (uint8_t *)buf);
}

bool qemu_savevm_state_blocked(Error **errp)
{
    SaveStateEntry *se;

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (se->vmsd && se->vmsd->unmigratable) {
            error_setg(errp, "State blocked by non-migratable device '%s'",
                       se->idstr);
            return true;
        }
    }
    return false;
}

void qemu_savevm_state_header(QEMUFile *f)
{
    trace_savevm_state_header();
    qemu_put_be32(f, QEMU_VM_FILE_MAGIC);
    qemu_put_be32(f, QEMU_VM_FILE_VERSION);

    if (migrate_get_current()->send_configuration) {
        qemu_put_byte(f, QEMU_VM_CONFIGURATION);
        vmstate_save_state(f, &vmstate_configuration, &savevm_state, 0);
    }
}

bool qemu_savevm_state_guest_unplug_pending(void)
{
    SaveStateEntry *se;

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (se->vmsd && se->vmsd->dev_unplug_pending &&
            se->vmsd->dev_unplug_pending(se->opaque)) {
            return true;
        }
    }

    return false;
}

void qemu_savevm_state_setup(QEMUFile *f)
{
    SaveStateEntry *se;
    Error *local_err = NULL;
    int ret;

    trace_savevm_state_setup();
    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!se->ops || !se->ops->save_setup) {
            continue;
        }
        if (se->ops->is_active) {
            if (!se->ops->is_active(se->opaque)) {
                continue;
            }
        }
        save_section_header(f, se, QEMU_VM_SECTION_START);

        ret = se->ops->save_setup(f, se->opaque);
        save_section_footer(f, se);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
            break;
        }
    }

    if (precopy_notify(PRECOPY_NOTIFY_SETUP, &local_err)) {
        error_report_err(local_err);
    }
}

int qemu_savevm_state_resume_prepare(MigrationState *s)
{
    SaveStateEntry *se;
    int ret;

    trace_savevm_state_resume_prepare();

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!se->ops || !se->ops->resume_prepare) {
            continue;
        }
        if (se->ops->is_active) {
            if (!se->ops->is_active(se->opaque)) {
                continue;
            }
        }
        ret = se->ops->resume_prepare(s, se->opaque);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

/*
 * this function has three return values:
 *   negative: there was one error, and we have -errno.
 *   0 : We haven't finished, caller have to go again
 *   1 : We have finished, we can go to complete phase
 */
int qemu_savevm_state_iterate(QEMUFile *f, bool postcopy)
{
    SaveStateEntry *se;
    int ret = 1;

    trace_savevm_state_iterate();
    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!se->ops || !se->ops->save_live_iterate) {
            continue;
        }
        if (se->ops->is_active &&
            !se->ops->is_active(se->opaque)) {
            continue;
        }
        if (se->ops->is_active_iterate &&
            !se->ops->is_active_iterate(se->opaque)) {
            continue;
        }
        /*
         * In the postcopy phase, any device that doesn't know how to
         * do postcopy should have saved it's state in the _complete
         * call that's already run, it might get confused if we call
         * iterate afterwards.
         */
        if (postcopy &&
            !(se->ops->has_postcopy && se->ops->has_postcopy(se->opaque))) {
            continue;
        }
        if (qemu_file_rate_limit(f)) {
            return 0;
        }
        trace_savevm_section_start(se->idstr, se->section_id);

        save_section_header(f, se, QEMU_VM_SECTION_PART);

        ret = se->ops->save_live_iterate(f, se->opaque);
        trace_savevm_section_end(se->idstr, se->section_id, ret);
        save_section_footer(f, se);

        if (ret < 0) {
            error_report("failed to save SaveStateEntry with id(name): %d(%s)",
                         se->section_id, se->idstr);
            qemu_file_set_error(f, ret);
        }
        if (ret <= 0) {
            /* Do not proceed to the next vmstate before this one reported
               completion of the current stage. This serializes the migration
               and reduces the probability that a faster changing state is
               synchronized over and over again. */
            break;
        }
    }
    return ret;
}

static bool should_send_vmdesc(void)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    bool in_postcopy = migration_in_postcopy();
    return !machine->suppress_vmdesc && !in_postcopy;
}

/*
 * Calls the save_live_complete_postcopy methods
 * causing the last few pages to be sent immediately and doing any associated
 * cleanup.
 * Note postcopy also calls qemu_savevm_state_complete_precopy to complete
 * all the other devices, but that happens at the point we switch to postcopy.
 */
void qemu_savevm_state_complete_postcopy(QEMUFile *f)
{
    SaveStateEntry *se;
    int ret;

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!se->ops || !se->ops->save_live_complete_postcopy) {
            continue;
        }
        if (se->ops->is_active) {
            if (!se->ops->is_active(se->opaque)) {
                continue;
            }
        }
        trace_savevm_section_start(se->idstr, se->section_id);
        /* Section type */
        qemu_put_byte(f, QEMU_VM_SECTION_END);
        qemu_put_be32(f, se->section_id);

        ret = se->ops->save_live_complete_postcopy(f, se->opaque);
        trace_savevm_section_end(se->idstr, se->section_id, ret);
        save_section_footer(f, se);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
            return;
        }
    }

    qemu_put_byte(f, QEMU_VM_EOF);
    qemu_fflush(f);
}

static
int qemu_savevm_state_complete_precopy_iterable(QEMUFile *f, bool in_postcopy)
{
    SaveStateEntry *se;
    int ret;

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!se->ops ||
            (in_postcopy && se->ops->has_postcopy &&
             se->ops->has_postcopy(se->opaque)) ||
            !se->ops->save_live_complete_precopy) {
            continue;
        }

        if (se->ops->is_active) {
            if (!se->ops->is_active(se->opaque)) {
                continue;
            }
        }
        trace_savevm_section_start(se->idstr, se->section_id);

        save_section_header(f, se, QEMU_VM_SECTION_END);

        ret = se->ops->save_live_complete_precopy(f, se->opaque);
        trace_savevm_section_end(se->idstr, se->section_id, ret);
        save_section_footer(f, se);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
            return -1;
        }
    }

    return 0;
}

static
int qemu_savevm_state_complete_precopy_non_iterable(QEMUFile *f,
                                                    bool in_postcopy,
                                                    bool inactivate_disks)
{
    g_autoptr(QJSON) vmdesc = NULL;
    int vmdesc_len;
    SaveStateEntry *se;
    int ret;

    vmdesc = qjson_new();
    json_prop_int(vmdesc, "page_size", qemu_target_page_size());
    json_start_array(vmdesc, "devices");
    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {

        if ((!se->ops || !se->ops->save_state) && !se->vmsd) {
            continue;
        }
        if (se->vmsd && !vmstate_save_needed(se->vmsd, se->opaque)) {
            trace_savevm_section_skip(se->idstr, se->section_id);
            continue;
        }

        trace_savevm_section_start(se->idstr, se->section_id);

        json_start_object(vmdesc, NULL);
        json_prop_str(vmdesc, "name", se->idstr);
        json_prop_int(vmdesc, "instance_id", se->instance_id);

        save_section_header(f, se, QEMU_VM_SECTION_FULL);
        ret = vmstate_save(f, se, vmdesc);
        if (ret) {
            qemu_file_set_error(f, ret);
            return ret;
        }
        trace_savevm_section_end(se->idstr, se->section_id, 0);
        save_section_footer(f, se);

        json_end_object(vmdesc);
    }

    if (inactivate_disks) {
        /* Inactivate before sending QEMU_VM_EOF so that the
         * bdrv_invalidate_cache_all() on the other end won't fail. */
        ret = bdrv_inactivate_all();
        if (ret) {
            error_report("%s: bdrv_inactivate_all() failed (%d)",
                         __func__, ret);
            qemu_file_set_error(f, ret);
            return ret;
        }
    }
    if (!in_postcopy) {
        /* Postcopy stream will still be going */
        qemu_put_byte(f, QEMU_VM_EOF);
    }

    json_end_array(vmdesc);
    qjson_finish(vmdesc);
    vmdesc_len = strlen(qjson_get_str(vmdesc));

    if (should_send_vmdesc()) {
        qemu_put_byte(f, QEMU_VM_VMDESCRIPTION);
        qemu_put_be32(f, vmdesc_len);
        qemu_put_buffer(f, (uint8_t *)qjson_get_str(vmdesc), vmdesc_len);
    }

    return 0;
}

int qemu_savevm_state_complete_precopy(QEMUFile *f, bool iterable_only,
                                       bool inactivate_disks)
{
    int ret;
    Error *local_err = NULL;
    bool in_postcopy = migration_in_postcopy();

    if (precopy_notify(PRECOPY_NOTIFY_COMPLETE, &local_err)) {
        error_report_err(local_err);
    }

    trace_savevm_state_complete_precopy();

    cpu_synchronize_all_states();

    if (!in_postcopy || iterable_only) {
        ret = qemu_savevm_state_complete_precopy_iterable(f, in_postcopy);
        if (ret) {
            return ret;
        }
    }

    if (iterable_only) {
        goto flush;
    }

    ret = qemu_savevm_state_complete_precopy_non_iterable(f, in_postcopy,
                                                          inactivate_disks);
    if (ret) {
        return ret;
    }

flush:
    qemu_fflush(f);
    return 0;
}

/* Give an estimate of the amount left to be transferred,
 * the result is split into the amount for units that can and
 * for units that can't do postcopy.
 */
void qemu_savevm_state_pending(QEMUFile *f, uint64_t threshold_size,
                               uint64_t *res_precopy_only,
                               uint64_t *res_compatible,
                               uint64_t *res_postcopy_only)
{
    SaveStateEntry *se;

    *res_precopy_only = 0;
    *res_compatible = 0;
    *res_postcopy_only = 0;


    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!se->ops || !se->ops->save_live_pending) {
            continue;
        }
        if (se->ops->is_active) {
            if (!se->ops->is_active(se->opaque)) {
                continue;
            }
        }
        se->ops->save_live_pending(f, se->opaque, threshold_size,
                                   res_precopy_only, res_compatible,
                                   res_postcopy_only);
    }
}

void qemu_savevm_state_cleanup(void)
{
    SaveStateEntry *se;
    Error *local_err = NULL;

    if (precopy_notify(PRECOPY_NOTIFY_CLEANUP, &local_err)) {
        error_report_err(local_err);
    }

    trace_savevm_state_cleanup();
    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (se->ops && se->ops->save_cleanup) {
            se->ops->save_cleanup(se->opaque);
        }
    }
}

static int qemu_savevm_state(QEMUFile *f, Error **errp)
{
    int ret;
    MigrationState *ms = migrate_get_current();
    MigrationStatus status;

    if (migration_is_running(ms->state)) {
        error_setg(errp, QERR_MIGRATION_ACTIVE);
        return -EINVAL;
    }

    if (migrate_use_block()) {
        error_setg(errp, "Block migration and snapshots are incompatible");
        return -EINVAL;
    }

    migrate_init(ms);
    memset(&ram_counters, 0, sizeof(ram_counters));
    ms->to_dst_file = f;

    qemu_mutex_unlock_iothread();
    qemu_savevm_state_header(f);
    qemu_savevm_state_setup(f);
    qemu_mutex_lock_iothread();

    while (qemu_file_get_error(f) == 0) {
        if (qemu_savevm_state_iterate(f, false) > 0) {
            break;
        }
    }

    ret = qemu_file_get_error(f);
    if (ret == 0) {
        qemu_savevm_state_complete_precopy(f, false, false);
        ret = qemu_file_get_error(f);
    }
    qemu_savevm_state_cleanup();
    if (ret != 0) {
        error_setg_errno(errp, -ret, "Error while writing VM state");
    }

    if (ret != 0) {
        status = MIGRATION_STATUS_FAILED;
    } else {
        status = MIGRATION_STATUS_COMPLETED;
    }
    migrate_set_state(&ms->state, MIGRATION_STATUS_SETUP, status);

    /* f is outer parameter, it should not stay in global migration state after
     * this function finished */
    ms->to_dst_file = NULL;

    return ret;
}

void qemu_savevm_live_state(QEMUFile *f)
{
    /* save QEMU_VM_SECTION_END section */
    qemu_savevm_state_complete_precopy(f, true, false);
    qemu_put_byte(f, QEMU_VM_EOF);
}

int qemu_save_device_state(QEMUFile *f)
{
    SaveStateEntry *se;

    if (!migration_in_colo_state()) {
        qemu_put_be32(f, QEMU_VM_FILE_MAGIC);
        qemu_put_be32(f, QEMU_VM_FILE_VERSION);
    }
    cpu_synchronize_all_states();

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        int ret;

        if (se->is_ram) {
            continue;
        }
        if ((!se->ops || !se->ops->save_state) && !se->vmsd) {
            continue;
        }
        if (se->vmsd && !vmstate_save_needed(se->vmsd, se->opaque)) {
            continue;
        }

        save_section_header(f, se, QEMU_VM_SECTION_FULL);

        ret = vmstate_save(f, se, NULL);
        if (ret) {
            return ret;
        }

        save_section_footer(f, se);
    }

    qemu_put_byte(f, QEMU_VM_EOF);

    return qemu_file_get_error(f);
}

static SaveStateEntry *find_se(const char *idstr, uint32_t instance_id)
{
    SaveStateEntry *se;

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!strcmp(se->idstr, idstr) &&
            (instance_id == se->instance_id ||
             instance_id == se->alias_id))
            return se;
        /* Migrating from an older version? */
        if (strstr(se->idstr, idstr) && se->compat) {
            if (!strcmp(se->compat->idstr, idstr) &&
                (instance_id == se->compat->instance_id ||
                 instance_id == se->alias_id))
                return se;
        }
    }
    return NULL;
}

enum LoadVMExitCodes {
    /* Allow a command to quit all layers of nested loadvm loops */
    LOADVM_QUIT     =  1,
};

/* ------ incoming postcopy messages ------ */
/* 'advise' arrives before any transfers just to tell us that a postcopy
 * *might* happen - it might be skipped if precopy transferred everything
 * quickly.
 */
static int loadvm_postcopy_handle_advise(MigrationIncomingState *mis,
                                         uint16_t len)
{
    PostcopyState ps = postcopy_state_set(POSTCOPY_INCOMING_ADVISE);
    uint64_t remote_pagesize_summary, local_pagesize_summary, remote_tps;
    Error *local_err = NULL;

    trace_loadvm_postcopy_handle_advise();
    if (ps != POSTCOPY_INCOMING_NONE) {
        error_report("CMD_POSTCOPY_ADVISE in wrong postcopy state (%d)", ps);
        return -1;
    }

    switch (len) {
    case 0:
        if (migrate_postcopy_ram()) {
            error_report("RAM postcopy is enabled but have 0 byte advise");
            return -EINVAL;
        }
        return 0;
    case 8 + 8:
        if (!migrate_postcopy_ram()) {
            error_report("RAM postcopy is disabled but have 16 byte advise");
            return -EINVAL;
        }
        break;
    default:
        error_report("CMD_POSTCOPY_ADVISE invalid length (%d)", len);
        return -EINVAL;
    }

    if (!postcopy_ram_supported_by_host(mis)) {
        postcopy_state_set(POSTCOPY_INCOMING_NONE);
        return -1;
    }

    remote_pagesize_summary = qemu_get_be64(mis->from_src_file);
    local_pagesize_summary = ram_pagesize_summary();

    if (remote_pagesize_summary != local_pagesize_summary)  {
        /*
         * This detects two potential causes of mismatch:
         *   a) A mismatch in host page sizes
         *      Some combinations of mismatch are probably possible but it gets
         *      a bit more complicated.  In particular we need to place whole
         *      host pages on the dest at once, and we need to ensure that we
         *      handle dirtying to make sure we never end up sending part of
         *      a hostpage on it's own.
         *   b) The use of different huge page sizes on source/destination
         *      a more fine grain test is performed during RAM block migration
         *      but this test here causes a nice early clear failure, and
         *      also fails when passed to an older qemu that doesn't
         *      do huge pages.
         */
        error_report("Postcopy needs matching RAM page sizes (s=%" PRIx64
                                                             " d=%" PRIx64 ")",
                     remote_pagesize_summary, local_pagesize_summary);
        return -1;
    }

    remote_tps = qemu_get_be64(mis->from_src_file);
    if (remote_tps != qemu_target_page_size()) {
        /*
         * Again, some differences could be dealt with, but for now keep it
         * simple.
         */
        error_report("Postcopy needs matching target page sizes (s=%d d=%zd)",
                     (int)remote_tps, qemu_target_page_size());
        return -1;
    }

    if (postcopy_notify(POSTCOPY_NOTIFY_INBOUND_ADVISE, &local_err)) {
        error_report_err(local_err);
        return -1;
    }

    if (ram_postcopy_incoming_init(mis)) {
        return -1;
    }

    return 0;
}

/* After postcopy we will be told to throw some pages away since they're
 * dirty and will have to be demand fetched.  Must happen before CPU is
 * started.
 * There can be 0..many of these messages, each encoding multiple pages.
 */
static int loadvm_postcopy_ram_handle_discard(MigrationIncomingState *mis,
                                              uint16_t len)
{
    int tmp;
    char ramid[256];
    PostcopyState ps = postcopy_state_get();

    trace_loadvm_postcopy_ram_handle_discard();

    switch (ps) {
    case POSTCOPY_INCOMING_ADVISE:
        /* 1st discard */
        tmp = postcopy_ram_prepare_discard(mis);
        if (tmp) {
            return tmp;
        }
        break;

    case POSTCOPY_INCOMING_DISCARD:
        /* Expected state */
        break;

    default:
        error_report("CMD_POSTCOPY_RAM_DISCARD in wrong postcopy state (%d)",
                     ps);
        return -1;
    }
    /* We're expecting a
     *    Version (0)
     *    a RAM ID string (length byte, name, 0 term)
     *    then at least 1 16 byte chunk
    */
    if (len < (1 + 1 + 1 + 1 + 2 * 8)) {
        error_report("CMD_POSTCOPY_RAM_DISCARD invalid length (%d)", len);
        return -1;
    }

    tmp = qemu_get_byte(mis->from_src_file);
    if (tmp != postcopy_ram_discard_version) {
        error_report("CMD_POSTCOPY_RAM_DISCARD invalid version (%d)", tmp);
        return -1;
    }

    if (!qemu_get_counted_string(mis->from_src_file, ramid)) {
        error_report("CMD_POSTCOPY_RAM_DISCARD Failed to read RAMBlock ID");
        return -1;
    }
    tmp = qemu_get_byte(mis->from_src_file);
    if (tmp != 0) {
        error_report("CMD_POSTCOPY_RAM_DISCARD missing nil (%d)", tmp);
        return -1;
    }

    len -= 3 + strlen(ramid);
    if (len % 16) {
        error_report("CMD_POSTCOPY_RAM_DISCARD invalid length (%d)", len);
        return -1;
    }
    trace_loadvm_postcopy_ram_handle_discard_header(ramid, len);
    while (len) {
        uint64_t start_addr, block_length;
        start_addr = qemu_get_be64(mis->from_src_file);
        block_length = qemu_get_be64(mis->from_src_file);

        len -= 16;
        int ret = ram_discard_range(ramid, start_addr, block_length);
        if (ret) {
            return ret;
        }
    }
    trace_loadvm_postcopy_ram_handle_discard_end();

    return 0;
}

/*
 * Triggered by a postcopy_listen command; this thread takes over reading
 * the input stream, leaving the main thread free to carry on loading the rest
 * of the device state (from RAM).
 * (TODO:This could do with being in a postcopy file - but there again it's
 * just another input loop, not that postcopy specific)
 */
static void *postcopy_ram_listen_thread(void *opaque)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    QEMUFile *f = mis->from_src_file;
    int load_res;
    MigrationState *migr = migrate_get_current();

    object_ref(OBJECT(migr));

    migrate_set_state(&mis->state, MIGRATION_STATUS_ACTIVE,
                                   MIGRATION_STATUS_POSTCOPY_ACTIVE);
    qemu_sem_post(&mis->listen_thread_sem);
    trace_postcopy_ram_listen_thread_start();

    rcu_register_thread();
    /*
     * Because we're a thread and not a coroutine we can't yield
     * in qemu_file, and thus we must be blocking now.
     */
    qemu_file_set_blocking(f, true);
    load_res = qemu_loadvm_state_main(f, mis);

    /*
     * This is tricky, but, mis->from_src_file can change after it
     * returns, when postcopy recovery happened. In the future, we may
     * want a wrapper for the QEMUFile handle.
     */
    f = mis->from_src_file;

    /* And non-blocking again so we don't block in any cleanup */
    qemu_file_set_blocking(f, false);

    trace_postcopy_ram_listen_thread_exit();
    if (load_res < 0) {
        qemu_file_set_error(f, load_res);
        dirty_bitmap_mig_cancel_incoming();
        if (postcopy_state_get() == POSTCOPY_INCOMING_RUNNING &&
            !migrate_postcopy_ram() && migrate_dirty_bitmaps())
        {
            error_report("%s: loadvm failed during postcopy: %d. All states "
                         "are migrated except dirty bitmaps. Some dirty "
                         "bitmaps may be lost, and present migrated dirty "
                         "bitmaps are correctly migrated and valid.",
                         __func__, load_res);
            load_res = 0; /* prevent further exit() */
        } else {
            error_report("%s: loadvm failed: %d", __func__, load_res);
            migrate_set_state(&mis->state, MIGRATION_STATUS_POSTCOPY_ACTIVE,
                                           MIGRATION_STATUS_FAILED);
        }
    }
    if (load_res >= 0) {
        /*
         * This looks good, but it's possible that the device loading in the
         * main thread hasn't finished yet, and so we might not be in 'RUN'
         * state yet; wait for the end of the main thread.
         */
        qemu_event_wait(&mis->main_thread_load_event);
    }
    postcopy_ram_incoming_cleanup(mis);

    if (load_res < 0) {
        /*
         * If something went wrong then we have a bad state so exit;
         * depending how far we got it might be possible at this point
         * to leave the guest running and fire MCEs for pages that never
         * arrived as a desperate recovery step.
         */
        rcu_unregister_thread();
        exit(EXIT_FAILURE);
    }

    migrate_set_state(&mis->state, MIGRATION_STATUS_POSTCOPY_ACTIVE,
                                   MIGRATION_STATUS_COMPLETED);
    /*
     * If everything has worked fine, then the main thread has waited
     * for us to start, and we're the last use of the mis.
     * (If something broke then qemu will have to exit anyway since it's
     * got a bad migration state).
     */
    migration_incoming_state_destroy();
    qemu_loadvm_state_cleanup();

    rcu_unregister_thread();
    mis->have_listen_thread = false;
    postcopy_state_set(POSTCOPY_INCOMING_END);

    object_unref(OBJECT(migr));

    return NULL;
}

/* After this message we must be able to immediately receive postcopy data */
static int loadvm_postcopy_handle_listen(MigrationIncomingState *mis)
{
    PostcopyState ps = postcopy_state_set(POSTCOPY_INCOMING_LISTENING);
    trace_loadvm_postcopy_handle_listen();
    Error *local_err = NULL;

    if (ps != POSTCOPY_INCOMING_ADVISE && ps != POSTCOPY_INCOMING_DISCARD) {
        error_report("CMD_POSTCOPY_LISTEN in wrong postcopy state (%d)", ps);
        return -1;
    }
    if (ps == POSTCOPY_INCOMING_ADVISE) {
        /*
         * A rare case, we entered listen without having to do any discards,
         * so do the setup that's normally done at the time of the 1st discard.
         */
        if (migrate_postcopy_ram()) {
            postcopy_ram_prepare_discard(mis);
        }
    }

    /*
     * Sensitise RAM - can now generate requests for blocks that don't exist
     * However, at this point the CPU shouldn't be running, and the IO
     * shouldn't be doing anything yet so don't actually expect requests
     */
    if (migrate_postcopy_ram()) {
        if (postcopy_ram_incoming_setup(mis)) {
            postcopy_ram_incoming_cleanup(mis);
            return -1;
        }
    }

    if (postcopy_notify(POSTCOPY_NOTIFY_INBOUND_LISTEN, &local_err)) {
        error_report_err(local_err);
        return -1;
    }

    mis->have_listen_thread = true;
    /* Start up the listening thread and wait for it to signal ready */
    qemu_sem_init(&mis->listen_thread_sem, 0);
    qemu_thread_create(&mis->listen_thread, "postcopy/listen",
                       postcopy_ram_listen_thread, NULL,
                       QEMU_THREAD_DETACHED);
    qemu_sem_wait(&mis->listen_thread_sem);
    qemu_sem_destroy(&mis->listen_thread_sem);

    return 0;
}

static void loadvm_postcopy_handle_run_bh(void *opaque)
{
    Error *local_err = NULL;
    MigrationIncomingState *mis = opaque;

    /* TODO we should move all of this lot into postcopy_ram.c or a shared code
     * in migration.c
     */
    cpu_synchronize_all_post_init();

    qemu_announce_self(&mis->announce_timer, migrate_announce_params());

    /* Make sure all file formats flush their mutable metadata.
     * If we get an error here, just don't restart the VM yet. */
    bdrv_invalidate_cache_all(&local_err);
    if (local_err) {
        error_report_err(local_err);
        local_err = NULL;
        autostart = false;
    }

    trace_loadvm_postcopy_handle_run_cpu_sync();

    trace_loadvm_postcopy_handle_run_vmstart();

    dirty_bitmap_mig_before_vm_start();

    if (autostart) {
        /* Hold onto your hats, starting the CPU */
        vm_start();
    } else {
        /* leave it paused and let management decide when to start the CPU */
        runstate_set(RUN_STATE_PAUSED);
    }

    qemu_bh_delete(mis->bh);
}

/* After all discards we can start running and asking for pages */
static int loadvm_postcopy_handle_run(MigrationIncomingState *mis)
{
    PostcopyState ps = postcopy_state_get();

    trace_loadvm_postcopy_handle_run();
    if (ps != POSTCOPY_INCOMING_LISTENING) {
        error_report("CMD_POSTCOPY_RUN in wrong postcopy state (%d)", ps);
        return -1;
    }

    postcopy_state_set(POSTCOPY_INCOMING_RUNNING);
    mis->bh = qemu_bh_new(loadvm_postcopy_handle_run_bh, mis);
    qemu_bh_schedule(mis->bh);

    /* We need to finish reading the stream from the package
     * and also stop reading anything more from the stream that loaded the
     * package (since it's now being read by the listener thread).
     * LOADVM_QUIT will quit all the layers of nested loadvm loops.
     */
    return LOADVM_QUIT;
}

static int loadvm_postcopy_handle_resume(MigrationIncomingState *mis)
{
    if (mis->state != MIGRATION_STATUS_POSTCOPY_RECOVER) {
        error_report("%s: illegal resume received", __func__);
        /* Don't fail the load, only for this. */
        return 0;
    }

    /*
     * This means source VM is ready to resume the postcopy migration.
     * It's time to switch state and release the fault thread to
     * continue service page faults.
     */
    migrate_set_state(&mis->state, MIGRATION_STATUS_POSTCOPY_RECOVER,
                      MIGRATION_STATUS_POSTCOPY_ACTIVE);
    qemu_sem_post(&mis->postcopy_pause_sem_fault);

    trace_loadvm_postcopy_handle_resume();

    /* Tell source that "we are ready" */
    migrate_send_rp_resume_ack(mis, MIGRATION_RESUME_ACK_VALUE);

    return 0;
}

/**
 * Immediately following this command is a blob of data containing an embedded
 * chunk of migration stream; read it and load it.
 *
 * @mis: Incoming state
 * @length: Length of packaged data to read
 *
 * Returns: Negative values on error
 *
 */
static int loadvm_handle_cmd_packaged(MigrationIncomingState *mis)
{
    int ret;
    size_t length;
    QIOChannelBuffer *bioc;

    length = qemu_get_be32(mis->from_src_file);
    trace_loadvm_handle_cmd_packaged(length);

    if (length > MAX_VM_CMD_PACKAGED_SIZE) {
        error_report("Unreasonably large packaged state: %zu", length);
        return -1;
    }

    bioc = qio_channel_buffer_new(length);
    qio_channel_set_name(QIO_CHANNEL(bioc), "migration-loadvm-buffer");
    ret = qemu_get_buffer(mis->from_src_file,
                          bioc->data,
                          length);
    if (ret != length) {
        object_unref(OBJECT(bioc));
        error_report("CMD_PACKAGED: Buffer receive fail ret=%d length=%zu",
                     ret, length);
        return (ret < 0) ? ret : -EAGAIN;
    }
    bioc->usage += length;
    trace_loadvm_handle_cmd_packaged_received(ret);

    QEMUFile *packf = qemu_fopen_channel_input(QIO_CHANNEL(bioc));

    ret = qemu_loadvm_state_main(packf, mis);
    trace_loadvm_handle_cmd_packaged_main(ret);
    qemu_fclose(packf);
    object_unref(OBJECT(bioc));

    return ret;
}

/*
 * Handle request that source requests for recved_bitmap on
 * destination. Payload format:
 *
 * len (1 byte) + ramblock_name (<255 bytes)
 */
static int loadvm_handle_recv_bitmap(MigrationIncomingState *mis,
                                     uint16_t len)
{
    QEMUFile *file = mis->from_src_file;
    RAMBlock *rb;
    char block_name[256];
    size_t cnt;

    cnt = qemu_get_counted_string(file, block_name);
    if (!cnt) {
        error_report("%s: failed to read block name", __func__);
        return -EINVAL;
    }

    /* Validate before using the data */
    if (qemu_file_get_error(file)) {
        return qemu_file_get_error(file);
    }

    if (len != cnt + 1) {
        error_report("%s: invalid payload length (%d)", __func__, len);
        return -EINVAL;
    }

    rb = qemu_ram_block_by_name(block_name);
    if (!rb) {
        error_report("%s: block '%s' not found", __func__, block_name);
        return -EINVAL;
    }

    migrate_send_rp_recv_bitmap(mis, block_name);

    trace_loadvm_handle_recv_bitmap(block_name);

    return 0;
}

static int loadvm_process_enable_colo(MigrationIncomingState *mis)
{
    int ret = migration_incoming_enable_colo();

    if (!ret) {
        ret = colo_init_ram_cache();
        if (ret) {
            migration_incoming_disable_colo();
        }
    }
    return ret;
}

/*
 * Process an incoming 'QEMU_VM_COMMAND'
 * 0           just a normal return
 * LOADVM_QUIT All good, but exit the loop
 * <0          Error
 */
static int loadvm_process_command(QEMUFile *f)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    uint16_t cmd;
    uint16_t len;
    uint32_t tmp32;

    cmd = qemu_get_be16(f);
    len = qemu_get_be16(f);

    /* Check validity before continue processing of cmds */
    if (qemu_file_get_error(f)) {
        return qemu_file_get_error(f);
    }

    trace_loadvm_process_command(cmd, len);
    if (cmd >= MIG_CMD_MAX || cmd == MIG_CMD_INVALID) {
        error_report("MIG_CMD 0x%x unknown (len 0x%x)", cmd, len);
        return -EINVAL;
    }

    if (mig_cmd_args[cmd].len != -1 && mig_cmd_args[cmd].len != len) {
        error_report("%s received with bad length - expecting %zu, got %d",
                     mig_cmd_args[cmd].name,
                     (size_t)mig_cmd_args[cmd].len, len);
        return -ERANGE;
    }

    switch (cmd) {
    case MIG_CMD_OPEN_RETURN_PATH:
        if (mis->to_src_file) {
            error_report("CMD_OPEN_RETURN_PATH called when RP already open");
            /* Not really a problem, so don't give up */
            return 0;
        }
        mis->to_src_file = qemu_file_get_return_path(f);
        if (!mis->to_src_file) {
            error_report("CMD_OPEN_RETURN_PATH failed");
            return -1;
        }
        break;

    case MIG_CMD_PING:
        tmp32 = qemu_get_be32(f);
        trace_loadvm_process_command_ping(tmp32);
        if (!mis->to_src_file) {
            error_report("CMD_PING (0x%x) received with no return path",
                         tmp32);
            return -1;
        }
        migrate_send_rp_pong(mis, tmp32);
        break;

    case MIG_CMD_PACKAGED:
        return loadvm_handle_cmd_packaged(mis);

    case MIG_CMD_POSTCOPY_ADVISE:
        return loadvm_postcopy_handle_advise(mis, len);

    case MIG_CMD_POSTCOPY_LISTEN:
        return loadvm_postcopy_handle_listen(mis);

    case MIG_CMD_POSTCOPY_RUN:
        return loadvm_postcopy_handle_run(mis);

    case MIG_CMD_POSTCOPY_RAM_DISCARD:
        return loadvm_postcopy_ram_handle_discard(mis, len);

    case MIG_CMD_POSTCOPY_RESUME:
        return loadvm_postcopy_handle_resume(mis);

    case MIG_CMD_RECV_BITMAP:
        return loadvm_handle_recv_bitmap(mis, len);

    case MIG_CMD_ENABLE_COLO:
        return loadvm_process_enable_colo(mis);
    }

    return 0;
}

/*
 * Read a footer off the wire and check that it matches the expected section
 *
 * Returns: true if the footer was good
 *          false if there is a problem (and calls error_report to say why)
 */
static bool check_section_footer(QEMUFile *f, SaveStateEntry *se)
{
    int ret;
    uint8_t read_mark;
    uint32_t read_section_id;

    if (!migrate_get_current()->send_section_footer) {
        /* No footer to check */
        return true;
    }

    read_mark = qemu_get_byte(f);

    ret = qemu_file_get_error(f);
    if (ret) {
        error_report("%s: Read section footer failed: %d",
                     __func__, ret);
        return false;
    }

    if (read_mark != QEMU_VM_SECTION_FOOTER) {
        error_report("Missing section footer for %s", se->idstr);
        return false;
    }

    read_section_id = qemu_get_be32(f);
    if (read_section_id != se->load_section_id) {
        error_report("Mismatched section id in footer for %s -"
                     " read 0x%x expected 0x%x",
                     se->idstr, read_section_id, se->load_section_id);
        return false;
    }

    /* All good */
    return true;
}

static int
qemu_loadvm_section_start_full(QEMUFile *f, MigrationIncomingState *mis)
{
    uint32_t instance_id, version_id, section_id;
    SaveStateEntry *se;
    char idstr[256];
    int ret;

    /* Read section start */
    section_id = qemu_get_be32(f);
    if (!qemu_get_counted_string(f, idstr)) {
        error_report("Unable to read ID string for section %u",
                     section_id);
        return -EINVAL;
    }
    instance_id = qemu_get_be32(f);
    version_id = qemu_get_be32(f);

    ret = qemu_file_get_error(f);
    if (ret) {
        error_report("%s: Failed to read instance/version ID: %d",
                     __func__, ret);
        return ret;
    }

    trace_qemu_loadvm_state_section_startfull(section_id, idstr,
            instance_id, version_id);
    /* Find savevm section */
    se = find_se(idstr, instance_id);
    if (se == NULL) {
        error_report("Unknown savevm section or instance '%s' %"PRIu32". "
                     "Make sure that your current VM setup matches your "
                     "saved VM setup, including any hotplugged devices",
                     idstr, instance_id);
        return -EINVAL;
    }

    /* Validate version */
    if (version_id > se->version_id) {
        error_report("savevm: unsupported version %d for '%s' v%d",
                     version_id, idstr, se->version_id);
        return -EINVAL;
    }
    se->load_version_id = version_id;
    se->load_section_id = section_id;

    /* Validate if it is a device's state */
    if (xen_enabled() && se->is_ram) {
        error_report("loadvm: %s RAM loading not allowed on Xen", idstr);
        return -EINVAL;
    }

    ret = vmstate_load(f, se);
    if (ret < 0) {
        error_report("error while loading state for instance 0x%"PRIx32" of"
                     " device '%s'", instance_id, idstr);
        return ret;
    }
    if (!check_section_footer(f, se)) {
        return -EINVAL;
    }

    return 0;
}

static int
qemu_loadvm_section_part_end(QEMUFile *f, MigrationIncomingState *mis)
{
    uint32_t section_id;
    SaveStateEntry *se;
    int ret;

    section_id = qemu_get_be32(f);

    ret = qemu_file_get_error(f);
    if (ret) {
        error_report("%s: Failed to read section ID: %d",
                     __func__, ret);
        return ret;
    }

    trace_qemu_loadvm_state_section_partend(section_id);
    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (se->load_section_id == section_id) {
            break;
        }
    }
    if (se == NULL) {
        error_report("Unknown savevm section %d", section_id);
        return -EINVAL;
    }

    ret = vmstate_load(f, se);
    if (ret < 0) {
        error_report("error while loading state section id %d(%s)",
                     section_id, se->idstr);
        return ret;
    }
    if (!check_section_footer(f, se)) {
        return -EINVAL;
    }

    return 0;
}

static int qemu_loadvm_state_header(QEMUFile *f)
{
    unsigned int v;
    int ret;

    v = qemu_get_be32(f);
    if (v != QEMU_VM_FILE_MAGIC) {
        error_report("Not a migration stream");
        return -EINVAL;
    }

    v = qemu_get_be32(f);
    if (v == QEMU_VM_FILE_VERSION_COMPAT) {
        error_report("SaveVM v2 format is obsolete and don't work anymore");
        return -ENOTSUP;
    }
    if (v != QEMU_VM_FILE_VERSION) {
        error_report("Unsupported migration stream version");
        return -ENOTSUP;
    }

    if (migrate_get_current()->send_configuration) {
        if (qemu_get_byte(f) != QEMU_VM_CONFIGURATION) {
            error_report("Configuration section missing");
            qemu_loadvm_state_cleanup();
            return -EINVAL;
        }
        ret = vmstate_load_state(f, &vmstate_configuration, &savevm_state, 0);

        if (ret) {
            qemu_loadvm_state_cleanup();
            return ret;
        }
    }
    return 0;
}

static int qemu_loadvm_state_setup(QEMUFile *f)
{
    SaveStateEntry *se;
    int ret;

    trace_loadvm_state_setup();
    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!se->ops || !se->ops->load_setup) {
            continue;
        }
        if (se->ops->is_active) {
            if (!se->ops->is_active(se->opaque)) {
                continue;
            }
        }

        ret = se->ops->load_setup(f, se->opaque);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
            error_report("Load state of device %s failed", se->idstr);
            return ret;
        }
    }
    return 0;
}

void qemu_loadvm_state_cleanup(void)
{
    SaveStateEntry *se;

    trace_loadvm_state_cleanup();
    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (se->ops && se->ops->load_cleanup) {
            se->ops->load_cleanup(se->opaque);
        }
    }
}

/* Return true if we should continue the migration, or false. */
static bool postcopy_pause_incoming(MigrationIncomingState *mis)
{
    trace_postcopy_pause_incoming();

    assert(migrate_postcopy_ram());

    /* Clear the triggered bit to allow one recovery */
    mis->postcopy_recover_triggered = false;

    assert(mis->from_src_file);
    qemu_file_shutdown(mis->from_src_file);
    qemu_fclose(mis->from_src_file);
    mis->from_src_file = NULL;

    assert(mis->to_src_file);
    qemu_file_shutdown(mis->to_src_file);
    qemu_mutex_lock(&mis->rp_mutex);
    qemu_fclose(mis->to_src_file);
    mis->to_src_file = NULL;
    qemu_mutex_unlock(&mis->rp_mutex);

    migrate_set_state(&mis->state, MIGRATION_STATUS_POSTCOPY_ACTIVE,
                      MIGRATION_STATUS_POSTCOPY_PAUSED);

    /* Notify the fault thread for the invalidated file handle */
    postcopy_fault_thread_notify(mis);

    error_report("Detected IO failure for postcopy. "
                 "Migration paused.");

    while (mis->state == MIGRATION_STATUS_POSTCOPY_PAUSED) {
        qemu_sem_wait(&mis->postcopy_pause_sem_dst);
    }

    trace_postcopy_pause_incoming_continued();

    return true;
}

int qemu_loadvm_state_main(QEMUFile *f, MigrationIncomingState *mis)
{
    uint8_t section_type;
    int ret = 0;

retry:
    while (true) {
        section_type = qemu_get_byte(f);

        if (qemu_file_get_error(f)) {
            ret = qemu_file_get_error(f);
            break;
        }

        trace_qemu_loadvm_state_section(section_type);
        switch (section_type) {
        case QEMU_VM_SECTION_START:
        case QEMU_VM_SECTION_FULL:
            ret = qemu_loadvm_section_start_full(f, mis);
            if (ret < 0) {
                goto out;
            }
            break;
        case QEMU_VM_SECTION_PART:
        case QEMU_VM_SECTION_END:
            ret = qemu_loadvm_section_part_end(f, mis);
            if (ret < 0) {
                goto out;
            }
            break;
        case QEMU_VM_COMMAND:
            ret = loadvm_process_command(f);
            trace_qemu_loadvm_state_section_command(ret);
            if ((ret < 0) || (ret == LOADVM_QUIT)) {
                goto out;
            }
            break;
        case QEMU_VM_EOF:
            /* This is the end of migration */
            goto out;
        default:
            error_report("Unknown savevm section type %d", section_type);
            ret = -EINVAL;
            goto out;
        }
    }

out:
    if (ret < 0) {
        qemu_file_set_error(f, ret);

        /* Cancel bitmaps incoming regardless of recovery */
        dirty_bitmap_mig_cancel_incoming();

        /*
         * If we are during an active postcopy, then we pause instead
         * of bail out to at least keep the VM's dirty data.  Note
         * that POSTCOPY_INCOMING_LISTENING stage is still not enough,
         * during which we're still receiving device states and we
         * still haven't yet started the VM on destination.
         *
         * Only RAM postcopy supports recovery. Still, if RAM postcopy is
         * enabled, canceled bitmaps postcopy will not affect RAM postcopy
         * recovering.
         */
        if (postcopy_state_get() == POSTCOPY_INCOMING_RUNNING &&
            migrate_postcopy_ram() && postcopy_pause_incoming(mis)) {
            /* Reset f to point to the newly created channel */
            f = mis->from_src_file;
            goto retry;
        }
    }
    return ret;
}

int qemu_loadvm_state(QEMUFile *f)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    Error *local_err = NULL;
    int ret;

    if (qemu_savevm_state_blocked(&local_err)) {
        error_report_err(local_err);
        return -EINVAL;
    }

    ret = qemu_loadvm_state_header(f);
    if (ret) {
        return ret;
    }

    if (qemu_loadvm_state_setup(f) != 0) {
        return -EINVAL;
    }

    cpu_synchronize_all_pre_loadvm();

    ret = qemu_loadvm_state_main(f, mis);
    qemu_event_set(&mis->main_thread_load_event);

    trace_qemu_loadvm_state_post_main(ret);

    if (mis->have_listen_thread) {
        /* Listen thread still going, can't clean up yet */
        return ret;
    }

    if (ret == 0) {
        ret = qemu_file_get_error(f);
    }

    /*
     * Try to read in the VMDESC section as well, so that dumping tools that
     * intercept our migration stream have the chance to see it.
     */

    /* We've got to be careful; if we don't read the data and just shut the fd
     * then the sender can error if we close while it's still sending.
     * We also mustn't read data that isn't there; some transports (RDMA)
     * will stall waiting for that data when the source has already closed.
     */
    if (ret == 0 && should_send_vmdesc()) {
        uint8_t *buf;
        uint32_t size;
        uint8_t  section_type = qemu_get_byte(f);

        if (section_type != QEMU_VM_VMDESCRIPTION) {
            error_report("Expected vmdescription section, but got %d",
                         section_type);
            /*
             * It doesn't seem worth failing at this point since
             * we apparently have an otherwise valid VM state
             */
        } else {
            buf = g_malloc(0x1000);
            size = qemu_get_be32(f);

            while (size > 0) {
                uint32_t read_chunk = MIN(size, 0x1000);
                qemu_get_buffer(f, buf, read_chunk);
                size -= read_chunk;
            }
            g_free(buf);
        }
    }

    qemu_loadvm_state_cleanup();
    cpu_synchronize_all_post_init();

    return ret;
}

int qemu_load_device_state(QEMUFile *f)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    int ret;

    /* Load QEMU_VM_SECTION_FULL section */
    ret = qemu_loadvm_state_main(f, mis);
    if (ret < 0) {
        error_report("Failed to load device state: %d", ret);
        return ret;
    }

    cpu_synchronize_all_post_init();
    return 0;
}

int save_snapshot(const char *name, Error **errp)
{
    BlockDriverState *bs, *bs1;
    QEMUSnapshotInfo sn1, *sn = &sn1, old_sn1, *old_sn = &old_sn1;
    int ret = -1, ret2;
    QEMUFile *f;
    int saved_vm_running;
    uint64_t vm_state_size;
    qemu_timeval tv;
    struct tm tm;
    AioContext *aio_context;

    if (migration_is_blocked(errp)) {
        return ret;
    }

    if (!replay_can_snapshot()) {
        error_setg(errp, "Record/replay does not allow making snapshot "
                   "right now. Try once more later.");
        return ret;
    }

    if (!bdrv_all_can_snapshot(&bs)) {
        error_setg(errp, "Device '%s' is writable but does not support "
                   "snapshots", bdrv_get_device_name(bs));
        return ret;
    }

    /* Delete old snapshots of the same name */
    if (name) {
        ret = bdrv_all_delete_snapshot(name, &bs1, errp);
        if (ret < 0) {
            error_prepend(errp, "Error while deleting snapshot on device "
                          "'%s': ", bdrv_get_device_name(bs1));
            return ret;
        }
    }

    bs = bdrv_all_find_vmstate_bs();
    if (bs == NULL) {
        error_setg(errp, "No block device can accept snapshots");
        return ret;
    }
    aio_context = bdrv_get_aio_context(bs);

    saved_vm_running = runstate_is_running();

    ret = global_state_store();
    if (ret) {
        error_setg(errp, "Error saving global state");
        return ret;
    }
    vm_stop(RUN_STATE_SAVE_VM);

    bdrv_drain_all_begin();

    aio_context_acquire(aio_context);

    memset(sn, 0, sizeof(*sn));

    /* fill auxiliary fields */
    qemu_gettimeofday(&tv);
    sn->date_sec = tv.tv_sec;
    sn->date_nsec = tv.tv_usec * 1000;
    sn->vm_clock_nsec = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (name) {
        ret = bdrv_snapshot_find(bs, old_sn, name);
        if (ret >= 0) {
            pstrcpy(sn->name, sizeof(sn->name), old_sn->name);
            pstrcpy(sn->id_str, sizeof(sn->id_str), old_sn->id_str);
        } else {
            pstrcpy(sn->name, sizeof(sn->name), name);
        }
    } else {
        /* cast below needed for OpenBSD where tv_sec is still 'long' */
        localtime_r((const time_t *)&tv.tv_sec, &tm);
        strftime(sn->name, sizeof(sn->name), "vm-%Y%m%d%H%M%S", &tm);
    }

    /* save the VM state */
    f = qemu_fopen_bdrv(bs, 1);
    if (!f) {
        error_setg(errp, "Could not open VM state file");
        goto the_end;
    }
    ret = qemu_savevm_state(f, errp);
    vm_state_size = qemu_ftell(f);
    ret2 = qemu_fclose(f);
    if (ret < 0) {
        goto the_end;
    }
    if (ret2 < 0) {
        ret = ret2;
        goto the_end;
    }

    /* The bdrv_all_create_snapshot() call that follows acquires the AioContext
     * for itself.  BDRV_POLL_WHILE() does not support nested locking because
     * it only releases the lock once.  Therefore synchronous I/O will deadlock
     * unless we release the AioContext before bdrv_all_create_snapshot().
     */
    aio_context_release(aio_context);
    aio_context = NULL;

    ret = bdrv_all_create_snapshot(sn, bs, vm_state_size, &bs);
    if (ret < 0) {
        error_setg(errp, "Error while creating snapshot on '%s'",
                   bdrv_get_device_name(bs));
        goto the_end;
    }

    ret = 0;

 the_end:
    if (aio_context) {
        aio_context_release(aio_context);
    }

    bdrv_drain_all_end();

    if (saved_vm_running) {
        vm_start();
    }
    return ret;
}

void qmp_xen_save_devices_state(const char *filename, bool has_live, bool live,
                                Error **errp)
{
    QEMUFile *f;
    QIOChannelFile *ioc;
    int saved_vm_running;
    int ret;

    if (!has_live) {
        /* live default to true so old version of Xen tool stack can have a
         * successfull live migration */
        live = true;
    }

    saved_vm_running = runstate_is_running();
    vm_stop(RUN_STATE_SAVE_VM);
    global_state_store_running();

    ioc = qio_channel_file_new_path(filename, O_WRONLY | O_CREAT, 0660, errp);
    if (!ioc) {
        goto the_end;
    }
    qio_channel_set_name(QIO_CHANNEL(ioc), "migration-xen-save-state");
    f = qemu_fopen_channel_output(QIO_CHANNEL(ioc));
    object_unref(OBJECT(ioc));
    ret = qemu_save_device_state(f);
    if (ret < 0 || qemu_fclose(f) < 0) {
        error_setg(errp, QERR_IO_ERROR);
    } else {
        /* libxl calls the QMP command "stop" before calling
         * "xen-save-devices-state" and in case of migration failure, libxl
         * would call "cont".
         * So call bdrv_inactivate_all (release locks) here to let the other
         * side of the migration take controle of the images.
         */
        if (live && !saved_vm_running) {
            ret = bdrv_inactivate_all();
            if (ret) {
                error_setg(errp, "%s: bdrv_inactivate_all() failed (%d)",
                           __func__, ret);
            }
        }
    }

 the_end:
    if (saved_vm_running) {
        vm_start();
    }
}

void qmp_xen_load_devices_state(const char *filename, Error **errp)
{
    QEMUFile *f;
    QIOChannelFile *ioc;
    int ret;

    /* Guest must be paused before loading the device state; the RAM state
     * will already have been loaded by xc
     */
    if (runstate_is_running()) {
        error_setg(errp, "Cannot update device state while vm is running");
        return;
    }
    vm_stop(RUN_STATE_RESTORE_VM);

    ioc = qio_channel_file_new_path(filename, O_RDONLY | O_BINARY, 0, errp);
    if (!ioc) {
        return;
    }
    qio_channel_set_name(QIO_CHANNEL(ioc), "migration-xen-load-state");
    f = qemu_fopen_channel_input(QIO_CHANNEL(ioc));
    object_unref(OBJECT(ioc));

    ret = qemu_loadvm_state(f);
    qemu_fclose(f);
    if (ret < 0) {
        error_setg(errp, QERR_IO_ERROR);
    }
    migration_incoming_state_destroy();
}

int load_snapshot(const char *name, Error **errp)
{
    BlockDriverState *bs, *bs_vm_state;
    QEMUSnapshotInfo sn;
    QEMUFile *f;
    int ret;
    AioContext *aio_context;
    MigrationIncomingState *mis = migration_incoming_get_current();

    if (!replay_can_snapshot()) {
        error_setg(errp, "Record/replay does not allow loading snapshot "
                   "right now. Try once more later.");
        return -EINVAL;
    }

    if (!bdrv_all_can_snapshot(&bs)) {
        error_setg(errp,
                   "Device '%s' is writable but does not support snapshots",
                   bdrv_get_device_name(bs));
        return -ENOTSUP;
    }
    ret = bdrv_all_find_snapshot(name, &bs);
    if (ret < 0) {
        error_setg(errp,
                   "Device '%s' does not have the requested snapshot '%s'",
                   bdrv_get_device_name(bs), name);
        return ret;
    }

    bs_vm_state = bdrv_all_find_vmstate_bs();
    if (!bs_vm_state) {
        error_setg(errp, "No block device supports snapshots");
        return -ENOTSUP;
    }
    aio_context = bdrv_get_aio_context(bs_vm_state);

    /* Don't even try to load empty VM states */
    aio_context_acquire(aio_context);
    ret = bdrv_snapshot_find(bs_vm_state, &sn, name);
    aio_context_release(aio_context);
    if (ret < 0) {
        return ret;
    } else if (sn.vm_state_size == 0) {
        error_setg(errp, "This is a disk-only snapshot. Revert to it "
                   " offline using qemu-img");
        return -EINVAL;
    }

    /* Flush all IO requests so they don't interfere with the new state.  */
    bdrv_drain_all_begin();

    ret = bdrv_all_goto_snapshot(name, &bs, errp);
    if (ret < 0) {
        error_prepend(errp, "Could not load snapshot '%s' on '%s': ",
                      name, bdrv_get_device_name(bs));
        goto err_drain;
    }

    /* restore the VM state */
    f = qemu_fopen_bdrv(bs_vm_state, 0);
    if (!f) {
        error_setg(errp, "Could not open VM state file");
        ret = -EINVAL;
        goto err_drain;
    }

    qemu_system_reset(SHUTDOWN_CAUSE_NONE);
    mis->from_src_file = f;

    aio_context_acquire(aio_context);
    ret = qemu_loadvm_state(f);
    migration_incoming_state_destroy();
    aio_context_release(aio_context);

    bdrv_drain_all_end();

    if (ret < 0) {
        error_setg(errp, "Error %d while loading VM state", ret);
        return ret;
    }

    return 0;

err_drain:
    bdrv_drain_all_end();
    return ret;
}

void vmstate_register_ram(MemoryRegion *mr, DeviceState *dev)
{
    qemu_ram_set_idstr(mr->ram_block,
                       memory_region_name(mr), dev);
    qemu_ram_set_migratable(mr->ram_block);
}

void vmstate_unregister_ram(MemoryRegion *mr, DeviceState *dev)
{
    qemu_ram_unset_idstr(mr->ram_block);
    qemu_ram_unset_migratable(mr->ram_block);
}

void vmstate_register_ram_global(MemoryRegion *mr)
{
    vmstate_register_ram(mr, NULL);
}

bool vmstate_check_only_migratable(const VMStateDescription *vmsd)
{
    /* check needed if --only-migratable is specified */
    if (!only_migratable) {
        return true;
    }

    return !(vmsd && vmsd->unmigratable);
}
