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
#include "migration-stats.h"
#include "migration/vmstate.h"
#include "migration/misc.h"
#include "migration/register.h"
#include "migration/global_state.h"
#include "migration/channel-block.h"
#include "ram.h"
#include "qemu-file.h"
#include "savevm.h"
#include "postcopy-ram.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-builtin-visit.h"
#include "qemu/error-report.h"
#include "sysemu/cpus.h"
#include "exec/memory.h"
#include "exec/target_page.h"
#include "trace.h"
#include "qemu/iov.h"
#include "qemu/job.h"
#include "qemu/main-loop.h"
#include "block/snapshot.h"
#include "qemu/cutils.h"
#include "io/channel-buffer.h"
#include "io/channel-file.h"
#include "sysemu/replay.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"
#include "sysemu/xen.h"
#include "migration/colo.h"
#include "qemu/bitmap.h"
#include "net/announce.h"
#include "qemu/yank.h"
#include "yank_functions.h"
#include "sysemu/qtest.h"
#include "options.h"

const unsigned int postcopy_ram_discard_version;

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
 *   uint64_t target page size
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

static QEMUFile *qemu_fopen_bdrv(BlockDriverState *bs, int is_writable)
{
    if (is_writable) {
        return qemu_file_new_output(QIO_CHANNEL(qio_channel_block_new(bs)));
    } else {
        return qemu_file_new_input(QIO_CHANNEL(qio_channel_block_new(bs)));
    }
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
                     const VMStateField *field, JSONWriter *vmdesc)
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

static SaveStateEntry *find_se(const char *idstr, uint32_t instance_id);

static bool should_validate_capability(int capability)
{
    assert(capability >= 0 && capability < MIGRATION_CAPABILITY__MAX);
    /* Validate only new capabilities to keep compatibility. */
    switch (capability) {
    case MIGRATION_CAPABILITY_X_IGNORE_SHARED:
    case MIGRATION_CAPABILITY_MAPPED_RAM:
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
        if (should_validate_capability(i) && s->capabilities[i]) {
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
        if (should_validate_capability(i) && s->capabilities[i]) {
            state->capabilities[j++] = i;
        }
    }
    state->uuid = qemu_uuid;

    return 0;
}

static int configuration_post_save(void *opaque)
{
    SaveState *state = opaque;

    g_free(state->capabilities);
    state->capabilities = NULL;
    state->caps_count = 0;
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
        target_state = s->capabilities[i];
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
    int ret = 0;

    if (strncmp(state->name, current_name, state->len) != 0) {
        error_report("Machine type received is '%.*s' and local is '%s'",
                     (int) state->len, state->name, current_name);
        ret = -EINVAL;
        goto out;
    }

    if (state->target_page_bits != qemu_target_page_bits()) {
        error_report("Received TARGET_PAGE_BITS is %d but local is %d",
                     state->target_page_bits, qemu_target_page_bits());
        ret = -EINVAL;
        goto out;
    }

    if (!configuration_validate_capabilities(state)) {
        ret = -EINVAL;
        goto out;
    }

out:
    g_free((void *)state->name);
    state->name = NULL;
    state->len = 0;
    g_free(state->capabilities);
    state->capabilities = NULL;
    state->caps_count = 0;

    return ret;
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
                          const VMStateField *field, JSONWriter *vmdesc)
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
    .fields = (const VMStateField[]) {
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
    .fields = (const VMStateField[]) {
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
    char uuid_src[UUID_STR_LEN];
    char uuid_dst[UUID_STR_LEN];

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
    .fields = (const VMStateField[]) {
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
    .post_save = configuration_post_save,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(len, SaveState),
        VMSTATE_VBUFFER_ALLOC_UINT32(name, SaveState, 0, NULL, len),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
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
    if (field->flags & VMS_ARRAY) {
        fprintf(out_file, "%*s\"num\": %d,\n", indent, "", field->num);
    }
    fprintf(out_file, "%*s\"size\": %zu", indent, "", field->size);
    if (field->vmsd != NULL) {
        fprintf(out_file, ",\n");
        dump_vmstate_vmsd(out_file, field->vmsd, indent, false);
    }
    fprintf(out_file, "\n%*s}", indent - 2, "");
}

static void dump_vmstate_vmss(FILE *out_file,
                              const VMStateDescription *subsection,
                              int indent)
{
    if (subsection != NULL) {
        dump_vmstate_vmsd(out_file, subsection, indent, true);
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
        assert(field->flags == VMS_END);
        fprintf(out_file, "\n%*s]", indent, "");
    }
    if (vmsd->subsections != NULL) {
        const VMStateDescription * const *subsection = vmsd->subsections;
        bool first;

        fprintf(out_file, ",\n%*s\"Subsections\": [\n", indent, "");
        first = true;
        while (*subsection != NULL) {
            if (!first) {
                fprintf(out_file, ",\n");
            }
            dump_vmstate_vmss(out_file, *subsection, indent + 2);
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

    /*
     * This should never happen otherwise migration will probably fail
     * silently somewhere because we can be wrongly applying one
     * object properties upon another one.  Bail out ASAP.
     */
    if (find_se(nse->idstr, nse->instance_id)) {
        error_report("%s: Detected duplicate SaveStateEntry: "
                     "id=%s, instance_id=0x%"PRIx32, __func__,
                     nse->idstr, nse->instance_id);
        exit(EXIT_FAILURE);
    }

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

/*
 * Perform some basic checks on vmsd's at registration
 * time.
 */
static void vmstate_check(const VMStateDescription *vmsd)
{
    const VMStateField *field = vmsd->fields;
    const VMStateDescription * const *subsection = vmsd->subsections;

    if (field) {
        while (field->name) {
            if (field->flags & (VMS_STRUCT | VMS_VSTRUCT)) {
                /* Recurse to sub structures */
                vmstate_check(field->vmsd);
            }
            /* Carry on */
            field++;
        }
        /* Check for the end of field list canary */
        if (field->flags != VMS_END) {
            error_report("VMSTATE not ending with VMS_END: %s", vmsd->name);
            g_assert_not_reached();
        }
    }

    while (subsection && *subsection) {
        /*
         * The name of a subsection should start with the name of the
         * current object.
         */
        assert(!strncmp(vmsd->name, (*subsection)->name, strlen(vmsd->name)));
        vmstate_check(*subsection);
        subsection++;
    }
}

/*
 * See comment in hw/intc/xics.c:icp_realize()
 *
 * This function can be removed when
 * pre_2_10_vmstate_register_dummy_icp() is removed.
 */
int vmstate_replace_hack_for_ppc(VMStateIf *obj, int instance_id,
                                 const VMStateDescription *vmsd,
                                 void *opaque)
{
    SaveStateEntry *se = find_se(vmsd->name, instance_id);

    if (se) {
        savevm_state_handler_remove(se);
        g_free(se->compat);
        g_free(se);
    }
    return vmstate_register(obj, instance_id, vmsd, opaque);
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

    /* Perform a recursive sanity check during the test runs */
    if (qtest_enabled()) {
        vmstate_check(vmsd);
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

static void vmstate_save_old_style(QEMUFile *f, SaveStateEntry *se,
                                   JSONWriter *vmdesc)
{
    uint64_t old_offset = qemu_file_transferred(f);
    se->ops->save_state(f, se->opaque);
    uint64_t size = qemu_file_transferred(f) - old_offset;

    if (vmdesc) {
        json_writer_int64(vmdesc, "size", size);
        json_writer_start_array(vmdesc, "fields");
        json_writer_start_object(vmdesc, NULL);
        json_writer_str(vmdesc, "name", "data");
        json_writer_int64(vmdesc, "size", size);
        json_writer_str(vmdesc, "type", "buffer");
        json_writer_end_object(vmdesc);
        json_writer_end_array(vmdesc);
    }
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

static int vmstate_save(QEMUFile *f, SaveStateEntry *se, JSONWriter *vmdesc,
                        Error **errp)
{
    int ret;

    if ((!se->ops || !se->ops->save_state) && !se->vmsd) {
        return 0;
    }
    if (se->vmsd && !vmstate_section_needed(se->vmsd, se->opaque)) {
        trace_savevm_section_skip(se->idstr, se->section_id);
        return 0;
    }

    trace_savevm_section_start(se->idstr, se->section_id);
    save_section_header(f, se, QEMU_VM_SECTION_FULL);
    if (vmdesc) {
        json_writer_start_object(vmdesc, NULL);
        json_writer_str(vmdesc, "name", se->idstr);
        json_writer_int64(vmdesc, "instance_id", se->instance_id);
    }

    trace_vmstate_save(se->idstr, se->vmsd ? se->vmsd->name : "(old)");
    if (!se->vmsd) {
        vmstate_save_old_style(f, se, vmdesc);
    } else {
        ret = vmstate_save_state_with_err(f, se->vmsd, se->opaque, vmdesc,
                                          errp);
        if (ret) {
            return ret;
        }
    }

    trace_savevm_section_end(se->idstr, se->section_id, 0);
    save_section_footer(f, se);
    if (vmdesc) {
        json_writer_end_object(vmdesc);
    }
    return 0;
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
    MigrationState *ms = migrate_get_current();
    Error *local_err = NULL;

    if (len > MAX_VM_CMD_PACKAGED_SIZE) {
        error_setg(&local_err, "%s: Unreasonably large packaged state: %zu",
                     __func__, len);
        migrate_set_error(ms, local_err);
        error_report_err(local_err);
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

void qemu_savevm_non_migratable_list(strList **reasons)
{
    SaveStateEntry *se;

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (se->vmsd && se->vmsd->unmigratable) {
            QAPI_LIST_PREPEND(*reasons,
                              g_strdup_printf("non-migratable device: %s",
                                              se->idstr));
        }
    }
}

void qemu_savevm_state_header(QEMUFile *f)
{
    MigrationState *s = migrate_get_current();

    s->vmdesc = json_writer_new(false);

    trace_savevm_state_header();
    qemu_put_be32(f, QEMU_VM_FILE_MAGIC);
    qemu_put_be32(f, QEMU_VM_FILE_VERSION);

    if (s->send_configuration) {
        qemu_put_byte(f, QEMU_VM_CONFIGURATION);

        /*
         * This starts the main json object and is paired with the
         * json_writer_end_object in
         * qemu_savevm_state_complete_precopy_non_iterable
         */
        json_writer_start_object(s->vmdesc, NULL);

        json_writer_start_object(s->vmdesc, "configuration");
        vmstate_save_state(f, &vmstate_configuration, &savevm_state, s->vmdesc);
        json_writer_end_object(s->vmdesc);
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

int qemu_savevm_state_prepare(Error **errp)
{
    SaveStateEntry *se;
    int ret;

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!se->ops || !se->ops->save_prepare) {
            continue;
        }
        if (se->ops->is_active) {
            if (!se->ops->is_active(se->opaque)) {
                continue;
            }
        }

        ret = se->ops->save_prepare(se->opaque, errp);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

int qemu_savevm_state_setup(QEMUFile *f, Error **errp)
{
    ERRP_GUARD();
    MigrationState *ms = migrate_get_current();
    SaveStateEntry *se;
    int ret = 0;

    json_writer_int64(ms->vmdesc, "page_size", qemu_target_page_size());
    json_writer_start_array(ms->vmdesc, "devices");

    trace_savevm_state_setup();
    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (se->vmsd && se->vmsd->early_setup) {
            ret = vmstate_save(f, se, ms->vmdesc, errp);
            if (ret) {
                migrate_set_error(ms, *errp);
                qemu_file_set_error(f, ret);
                break;
            }
            continue;
        }

        if (!se->ops || !se->ops->save_setup) {
            continue;
        }
        if (se->ops->is_active) {
            if (!se->ops->is_active(se->opaque)) {
                continue;
            }
        }
        save_section_header(f, se, QEMU_VM_SECTION_START);

        ret = se->ops->save_setup(f, se->opaque, errp);
        save_section_footer(f, se);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
            break;
        }
    }

    if (ret) {
        return ret;
    }

    /* TODO: Should we check that errp is set in case of failure ? */
    return precopy_notify(PRECOPY_NOTIFY_SETUP, errp);
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
    bool all_finished = true;
    int ret;

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
        if (migration_rate_exceeded(f)) {
            return 0;
        }
        trace_savevm_section_start(se->idstr, se->section_id);

        save_section_header(f, se, QEMU_VM_SECTION_PART);

        ret = se->ops->save_live_iterate(f, se->opaque);
        trace_savevm_section_end(se->idstr, se->section_id, ret);
        save_section_footer(f, se);

        if (ret < 0) {
            error_report("failed to save SaveStateEntry with id(name): "
                         "%d(%s): %d",
                         se->section_id, se->idstr, ret);
            qemu_file_set_error(f, ret);
            return ret;
        } else if (!ret) {
            all_finished = false;
        }
    }
    return all_finished;
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
    int64_t start_ts_each, end_ts_each;
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

        start_ts_each = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
        trace_savevm_section_start(se->idstr, se->section_id);

        save_section_header(f, se, QEMU_VM_SECTION_END);

        ret = se->ops->save_live_complete_precopy(f, se->opaque);
        trace_savevm_section_end(se->idstr, se->section_id, ret);
        save_section_footer(f, se);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
            return -1;
        }
        end_ts_each = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
        trace_vmstate_downtime_save("iterable", se->idstr, se->instance_id,
                                    end_ts_each - start_ts_each);
    }

    trace_vmstate_downtime_checkpoint("src-iterable-saved");

    return 0;
}

int qemu_savevm_state_complete_precopy_non_iterable(QEMUFile *f,
                                                    bool in_postcopy,
                                                    bool inactivate_disks)
{
    MigrationState *ms = migrate_get_current();
    int64_t start_ts_each, end_ts_each;
    JSONWriter *vmdesc = ms->vmdesc;
    int vmdesc_len;
    SaveStateEntry *se;
    Error *local_err = NULL;
    int ret;

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (se->vmsd && se->vmsd->early_setup) {
            /* Already saved during qemu_savevm_state_setup(). */
            continue;
        }

        start_ts_each = qemu_clock_get_us(QEMU_CLOCK_REALTIME);

        ret = vmstate_save(f, se, vmdesc, &local_err);
        if (ret) {
            migrate_set_error(ms, local_err);
            error_report_err(local_err);
            qemu_file_set_error(f, ret);
            return ret;
        }

        end_ts_each = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
        trace_vmstate_downtime_save("non-iterable", se->idstr, se->instance_id,
                                    end_ts_each - start_ts_each);
    }

    if (inactivate_disks) {
        /* Inactivate before sending QEMU_VM_EOF so that the
         * bdrv_activate_all() on the other end won't fail. */
        ret = bdrv_inactivate_all();
        if (ret) {
            error_setg(&local_err, "%s: bdrv_inactivate_all() failed (%d)",
                       __func__, ret);
            migrate_set_error(ms, local_err);
            error_report_err(local_err);
            qemu_file_set_error(f, ret);
            return ret;
        }
    }
    if (!in_postcopy) {
        /* Postcopy stream will still be going */
        qemu_put_byte(f, QEMU_VM_EOF);
    }

    json_writer_end_array(vmdesc);
    json_writer_end_object(vmdesc);
    vmdesc_len = strlen(json_writer_get(vmdesc));

    if (should_send_vmdesc()) {
        qemu_put_byte(f, QEMU_VM_VMDESCRIPTION);
        qemu_put_be32(f, vmdesc_len);
        qemu_put_buffer(f, (uint8_t *)json_writer_get(vmdesc), vmdesc_len);
    }

    /* Free it now to detect any inconsistencies. */
    json_writer_free(vmdesc);
    ms->vmdesc = NULL;

    trace_vmstate_downtime_checkpoint("src-non-iterable-saved");

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
    return qemu_fflush(f);
}

/* Give an estimate of the amount left to be transferred,
 * the result is split into the amount for units that can and
 * for units that can't do postcopy.
 */
void qemu_savevm_state_pending_estimate(uint64_t *must_precopy,
                                        uint64_t *can_postcopy)
{
    SaveStateEntry *se;

    *must_precopy = 0;
    *can_postcopy = 0;

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!se->ops || !se->ops->state_pending_estimate) {
            continue;
        }
        if (se->ops->is_active) {
            if (!se->ops->is_active(se->opaque)) {
                continue;
            }
        }
        se->ops->state_pending_estimate(se->opaque, must_precopy, can_postcopy);
    }
}

void qemu_savevm_state_pending_exact(uint64_t *must_precopy,
                                     uint64_t *can_postcopy)
{
    SaveStateEntry *se;

    *must_precopy = 0;
    *can_postcopy = 0;

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!se->ops || !se->ops->state_pending_exact) {
            continue;
        }
        if (se->ops->is_active) {
            if (!se->ops->is_active(se->opaque)) {
                continue;
            }
        }
        se->ops->state_pending_exact(se->opaque, must_precopy, can_postcopy);
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

    if (migration_is_running()) {
        error_setg(errp, "There's a migration process in progress");
        return -EINVAL;
    }

    ret = migrate_init(ms, errp);
    if (ret) {
        return ret;
    }
    ms->to_dst_file = f;

    qemu_savevm_state_header(f);
    ret = qemu_savevm_state_setup(f, errp);
    if (ret) {
        goto cleanup;
    }

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
    if (ret != 0) {
        error_setg_errno(errp, -ret, "Error while writing VM state");
    }
cleanup:
    qemu_savevm_state_cleanup();

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
    MigrationState *ms = migrate_get_current();
    Error *local_err = NULL;
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
        ret = vmstate_save(f, se, NULL, &local_err);
        if (ret) {
            migrate_set_error(ms, local_err);
            error_report_err(local_err);
            return ret;
        }
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
    size_t page_size = qemu_target_page_size();
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

    if (!postcopy_ram_supported_by_host(mis, &local_err)) {
        error_report_err(local_err);
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
    if (remote_tps != page_size) {
        /*
         * Again, some differences could be dealt with, but for now keep it
         * simple.
         */
        error_report("Postcopy needs matching target page sizes (s=%d d=%zd)",
                     (int)remote_tps, page_size);
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
    qemu_sem_post(&mis->thread_sync_sem);
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
    Error *local_err = NULL;

    trace_loadvm_postcopy_handle_listen("enter");

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

    trace_loadvm_postcopy_handle_listen("after discard");

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

    trace_loadvm_postcopy_handle_listen("after uffd");

    if (postcopy_notify(POSTCOPY_NOTIFY_INBOUND_LISTEN, &local_err)) {
        error_report_err(local_err);
        return -1;
    }

    mis->have_listen_thread = true;
    postcopy_thread_create(mis, &mis->listen_thread, "mig/dst/listen",
                           postcopy_ram_listen_thread, QEMU_THREAD_DETACHED);
    trace_loadvm_postcopy_handle_listen("return");

    return 0;
}

static void loadvm_postcopy_handle_run_bh(void *opaque)
{
    Error *local_err = NULL;
    MigrationIncomingState *mis = opaque;

    trace_vmstate_downtime_checkpoint("dst-postcopy-bh-enter");

    /* TODO we should move all of this lot into postcopy_ram.c or a shared code
     * in migration.c
     */
    cpu_synchronize_all_post_init();

    trace_vmstate_downtime_checkpoint("dst-postcopy-bh-cpu-synced");

    qemu_announce_self(&mis->announce_timer, migrate_announce_params());

    trace_vmstate_downtime_checkpoint("dst-postcopy-bh-announced");

    /* Make sure all file formats throw away their mutable metadata.
     * If we get an error here, just don't restart the VM yet. */
    bdrv_activate_all(&local_err);
    if (local_err) {
        error_report_err(local_err);
        local_err = NULL;
        autostart = false;
    }

    trace_vmstate_downtime_checkpoint("dst-postcopy-bh-cache-invalidated");

    dirty_bitmap_mig_before_vm_start();

    if (autostart) {
        /* Hold onto your hats, starting the CPU */
        vm_start();
    } else {
        /* leave it paused and let management decide when to start the CPU */
        runstate_set(RUN_STATE_PAUSED);
    }

    trace_vmstate_downtime_checkpoint("dst-postcopy-bh-vm-started");
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
    migration_bh_schedule(loadvm_postcopy_handle_run_bh, mis);

    /* We need to finish reading the stream from the package
     * and also stop reading anything more from the stream that loaded the
     * package (since it's now being read by the listener thread).
     * LOADVM_QUIT will quit all the layers of nested loadvm loops.
     */
    return LOADVM_QUIT;
}

/* We must be with page_request_mutex held */
static gboolean postcopy_sync_page_req(gpointer key, gpointer value,
                                       gpointer data)
{
    MigrationIncomingState *mis = data;
    void *host_addr = (void *) key;
    ram_addr_t rb_offset;
    RAMBlock *rb;
    int ret;

    rb = qemu_ram_block_from_host(host_addr, true, &rb_offset);
    if (!rb) {
        /*
         * This should _never_ happen.  However be nice for a migrating VM to
         * not crash/assert.  Post an error (note: intended to not use *_once
         * because we do want to see all the illegal addresses; and this can
         * never be triggered by the guest so we're safe) and move on next.
         */
        error_report("%s: illegal host addr %p", __func__, host_addr);
        /* Try the next entry */
        return FALSE;
    }

    ret = migrate_send_rp_message_req_pages(mis, rb, rb_offset);
    if (ret) {
        /* Please refer to above comment. */
        error_report("%s: send rp message failed for addr %p",
                     __func__, host_addr);
        return FALSE;
    }

    trace_postcopy_page_req_sync(host_addr);

    return FALSE;
}

static void migrate_send_rp_req_pages_pending(MigrationIncomingState *mis)
{
    WITH_QEMU_LOCK_GUARD(&mis->page_request_mutex) {
        g_tree_foreach(mis->page_requested, postcopy_sync_page_req, mis);
    }
}

static int loadvm_postcopy_handle_resume(MigrationIncomingState *mis)
{
    if (mis->state != MIGRATION_STATUS_POSTCOPY_RECOVER) {
        error_report("%s: illegal resume received", __func__);
        /* Don't fail the load, only for this. */
        return 0;
    }

    /*
     * Reset the last_rb before we resend any page req to source again, since
     * the source should have it reset already.
     */
    mis->last_rb = NULL;

    /*
     * This means source VM is ready to resume the postcopy migration.
     */
    migrate_set_state(&mis->state, MIGRATION_STATUS_POSTCOPY_RECOVER,
                      MIGRATION_STATUS_POSTCOPY_ACTIVE);

    trace_loadvm_postcopy_handle_resume();

    /* Tell source that "we are ready" */
    migrate_send_rp_resume_ack(mis, MIGRATION_RESUME_ACK_VALUE);

    /*
     * After a postcopy recovery, the source should have lost the postcopy
     * queue, or potentially the requested pages could have been lost during
     * the network down phase.  Let's re-sync with the source VM by re-sending
     * all the pending pages that we eagerly need, so these threads won't get
     * blocked too long due to the recovery.
     *
     * Without this procedure, the faulted destination VM threads (waiting for
     * page requests right before the postcopy is interrupted) can keep hanging
     * until the pages are sent by the source during the background copying of
     * pages, or another thread faulted on the same address accidentally.
     */
    migrate_send_rp_req_pages_pending(mis);

    /*
     * It's time to switch state and release the fault thread to continue
     * service page faults.  Note that this should be explicitly after the
     * above call to migrate_send_rp_req_pages_pending().  In short:
     * migrate_send_rp_message_req_pages() is not thread safe, yet.
     */
    qemu_sem_post(&mis->postcopy_pause_sem_fault);

    if (migrate_postcopy_preempt()) {
        /*
         * The preempt channel will be created in async manner, now let's
         * wait for it and make sure it's created.
         */
        qemu_sem_wait(&mis->postcopy_qemufile_dst_done);
        assert(mis->postcopy_qemufile_dst);
        /* Kick the fast ram load thread too */
        qemu_sem_post(&mis->postcopy_pause_sem_fast_load);
    }

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

    QEMUFile *packf = qemu_file_new_input(QIO_CHANNEL(bioc));

    /*
     * Before loading the guest states, ensure that the preempt channel has
     * been ready to use, as some of the states (e.g. via virtio_load) might
     * trigger page faults that will be handled through the preempt channel.
     * So yield to the main thread in the case that the channel create event
     * hasn't been dispatched.
     *
     * TODO: if we can move migration loadvm out of main thread, then we
     * won't block main thread from polling the accept() fds.  We can drop
     * this as a whole when that is done.
     */
    do {
        if (!migrate_postcopy_preempt() || !qemu_in_coroutine() ||
            mis->postcopy_qemufile_dst) {
            break;
        }

        aio_co_schedule(qemu_get_current_aio_context(), qemu_coroutine_self());
        qemu_coroutine_yield();
    } while (1);

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

    if (cmd >= MIG_CMD_MAX || cmd == MIG_CMD_INVALID) {
        error_report("MIG_CMD 0x%x unknown (len 0x%x)", cmd, len);
        return -EINVAL;
    }

    trace_loadvm_process_command(mig_cmd_args[cmd].name, len);

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

        /*
         * Switchover ack is enabled but no device uses it, so send an ACK to
         * source that it's OK to switchover. Do it here, after return path has
         * been created.
         */
        if (migrate_switchover_ack() && !mis->switchover_ack_pending_num) {
            int ret = migrate_send_rp_switchover_ack(mis);
            if (ret) {
                error_report(
                    "Could not send switchover ack RP MSG, err %d (%s)", ret,
                    strerror(-ret));
                return ret;
            }
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
qemu_loadvm_section_start_full(QEMUFile *f, MigrationIncomingState *mis,
                               uint8_t type)
{
    bool trace_downtime = (type == QEMU_VM_SECTION_FULL);
    uint32_t instance_id, version_id, section_id;
    int64_t start_ts, end_ts;
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

    if (trace_downtime) {
        start_ts = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
    }

    ret = vmstate_load(f, se);
    if (ret < 0) {
        error_report("error while loading state for instance 0x%"PRIx32" of"
                     " device '%s'", instance_id, idstr);
        return ret;
    }

    if (trace_downtime) {
        end_ts = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
        trace_vmstate_downtime_load("non-iterable", se->idstr,
                                    se->instance_id, end_ts - start_ts);
    }

    if (!check_section_footer(f, se)) {
        return -EINVAL;
    }

    return 0;
}

static int
qemu_loadvm_section_part_end(QEMUFile *f, MigrationIncomingState *mis,
                             uint8_t type)
{
    bool trace_downtime = (type == QEMU_VM_SECTION_END);
    int64_t start_ts, end_ts;
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

    if (trace_downtime) {
        start_ts = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
    }

    ret = vmstate_load(f, se);
    if (ret < 0) {
        error_report("error while loading state section id %d(%s)",
                     section_id, se->idstr);
        return ret;
    }

    if (trace_downtime) {
        end_ts = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
        trace_vmstate_downtime_load("iterable", se->idstr,
                                    se->instance_id, end_ts - start_ts);
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

static void qemu_loadvm_state_switchover_ack_needed(MigrationIncomingState *mis)
{
    SaveStateEntry *se;

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!se->ops || !se->ops->switchover_ack_needed) {
            continue;
        }

        if (se->ops->switchover_ack_needed(se->opaque)) {
            mis->switchover_ack_pending_num++;
        }
    }

    trace_loadvm_state_switchover_ack_needed(mis->switchover_ack_pending_num);
}

static int qemu_loadvm_state_setup(QEMUFile *f, Error **errp)
{
    ERRP_GUARD();
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

        ret = se->ops->load_setup(f, se->opaque, errp);
        if (ret < 0) {
            error_prepend(errp, "Load state of device %s failed: ",
                          se->idstr);
            qemu_file_set_error(f, ret);
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
    int i;

    trace_postcopy_pause_incoming();

    assert(migrate_postcopy_ram());

    /*
     * Unregister yank with either from/to src would work, since ioc behind it
     * is the same
     */
    migration_ioc_unregister_yank_from_file(mis->from_src_file);

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

    /*
     * NOTE: this must happen before reset the PostcopyTmpPages below,
     * otherwise it's racy to reset those fields when the fast load thread
     * can be accessing it in parallel.
     */
    if (mis->postcopy_qemufile_dst) {
        qemu_file_shutdown(mis->postcopy_qemufile_dst);
        /* Take the mutex to make sure the fast ram load thread halted */
        qemu_mutex_lock(&mis->postcopy_prio_thread_mutex);
        migration_ioc_unregister_yank_from_file(mis->postcopy_qemufile_dst);
        qemu_fclose(mis->postcopy_qemufile_dst);
        mis->postcopy_qemufile_dst = NULL;
        qemu_mutex_unlock(&mis->postcopy_prio_thread_mutex);
    }

    /* Current state can be either ACTIVE or RECOVER */
    migrate_set_state(&mis->state, mis->state,
                      MIGRATION_STATUS_POSTCOPY_PAUSED);

    /* Notify the fault thread for the invalidated file handle */
    postcopy_fault_thread_notify(mis);

    /*
     * If network is interrupted, any temp page we received will be useless
     * because we didn't mark them as "received" in receivedmap.  After a
     * proper recovery later (which will sync src dirty bitmap with receivedmap
     * on dest) these cached small pages will be resent again.
     */
    for (i = 0; i < mis->postcopy_channels; i++) {
        postcopy_temp_page_reset(&mis->postcopy_tmp_pages[i]);
    }

    error_report("Detected IO failure for postcopy. "
                 "Migration paused.");

    do {
        qemu_sem_wait(&mis->postcopy_pause_sem_dst);
    } while (postcopy_is_paused(mis->state));

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

        ret = qemu_file_get_error_obj_any(f, mis->postcopy_qemufile_dst, NULL);
        if (ret) {
            break;
        }

        trace_qemu_loadvm_state_section(section_type);
        switch (section_type) {
        case QEMU_VM_SECTION_START:
        case QEMU_VM_SECTION_FULL:
            ret = qemu_loadvm_section_start_full(f, mis, section_type);
            if (ret < 0) {
                goto out;
            }
            break;
        case QEMU_VM_SECTION_PART:
        case QEMU_VM_SECTION_END:
            ret = qemu_loadvm_section_part_end(f, mis, section_type);
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

    if (qemu_loadvm_state_setup(f, &local_err) != 0) {
        error_report_err(local_err);
        return -EINVAL;
    }

    if (migrate_switchover_ack()) {
        qemu_loadvm_state_switchover_ack_needed(mis);
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

int qemu_loadvm_approve_switchover(void)
{
    MigrationIncomingState *mis = migration_incoming_get_current();

    if (!mis->switchover_ack_pending_num) {
        return -EINVAL;
    }

    mis->switchover_ack_pending_num--;
    trace_loadvm_approve_switchover(mis->switchover_ack_pending_num);

    if (mis->switchover_ack_pending_num) {
        return 0;
    }

    return migrate_send_rp_switchover_ack(mis);
}

bool save_snapshot(const char *name, bool overwrite, const char *vmstate,
                  bool has_devices, strList *devices, Error **errp)
{
    BlockDriverState *bs;
    QEMUSnapshotInfo sn1, *sn = &sn1;
    int ret = -1, ret2;
    QEMUFile *f;
    RunState saved_state = runstate_get();
    uint64_t vm_state_size;
    g_autoptr(GDateTime) now = g_date_time_new_now_local();

    GLOBAL_STATE_CODE();

    if (migration_is_blocked(errp)) {
        return false;
    }

    if (!replay_can_snapshot()) {
        error_setg(errp, "Record/replay does not allow making snapshot "
                   "right now. Try once more later.");
        return false;
    }

    if (!bdrv_all_can_snapshot(has_devices, devices, errp)) {
        return false;
    }

    /* Delete old snapshots of the same name */
    if (name) {
        if (overwrite) {
            if (bdrv_all_delete_snapshot(name, has_devices,
                                         devices, errp) < 0) {
                return false;
            }
        } else {
            ret2 = bdrv_all_has_snapshot(name, has_devices, devices, errp);
            if (ret2 < 0) {
                return false;
            }
            if (ret2 == 1) {
                error_setg(errp,
                           "Snapshot '%s' already exists in one or more devices",
                           name);
                return false;
            }
        }
    }

    bs = bdrv_all_find_vmstate_bs(vmstate, has_devices, devices, errp);
    if (bs == NULL) {
        return false;
    }

    global_state_store();
    vm_stop(RUN_STATE_SAVE_VM);

    bdrv_drain_all_begin();

    memset(sn, 0, sizeof(*sn));

    /* fill auxiliary fields */
    sn->date_sec = g_date_time_to_unix(now);
    sn->date_nsec = g_date_time_get_microsecond(now) * 1000;
    sn->vm_clock_nsec = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    if (replay_mode != REPLAY_MODE_NONE) {
        sn->icount = replay_get_current_icount();
    } else {
        sn->icount = -1ULL;
    }

    if (name) {
        pstrcpy(sn->name, sizeof(sn->name), name);
    } else {
        g_autofree char *autoname = g_date_time_format(now,  "vm-%Y%m%d%H%M%S");
        pstrcpy(sn->name, sizeof(sn->name), autoname);
    }

    /* save the VM state */
    f = qemu_fopen_bdrv(bs, 1);
    if (!f) {
        error_setg(errp, "Could not open VM state file");
        goto the_end;
    }
    ret = qemu_savevm_state(f, errp);
    vm_state_size = qemu_file_transferred(f);
    ret2 = qemu_fclose(f);
    if (ret < 0) {
        goto the_end;
    }
    if (ret2 < 0) {
        ret = ret2;
        goto the_end;
    }

    ret = bdrv_all_create_snapshot(sn, bs, vm_state_size,
                                   has_devices, devices, errp);
    if (ret < 0) {
        bdrv_all_delete_snapshot(sn->name, has_devices, devices, NULL);
        goto the_end;
    }

    ret = 0;

 the_end:
    bdrv_drain_all_end();

    vm_resume(saved_state);
    return ret == 0;
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
         * successful live migration */
        live = true;
    }

    saved_vm_running = runstate_is_running();
    vm_stop(RUN_STATE_SAVE_VM);
    global_state_store_running();

    ioc = qio_channel_file_new_path(filename, O_WRONLY | O_CREAT | O_TRUNC,
                                    0660, errp);
    if (!ioc) {
        goto the_end;
    }
    qio_channel_set_name(QIO_CHANNEL(ioc), "migration-xen-save-state");
    f = qemu_file_new_output(QIO_CHANNEL(ioc));
    object_unref(OBJECT(ioc));
    ret = qemu_save_device_state(f);
    if (ret < 0 || qemu_fclose(f) < 0) {
        error_setg(errp, "saving Xen device state failed");
    } else {
        /* libxl calls the QMP command "stop" before calling
         * "xen-save-devices-state" and in case of migration failure, libxl
         * would call "cont".
         * So call bdrv_inactivate_all (release locks) here to let the other
         * side of the migration take control of the images.
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
    f = qemu_file_new_input(QIO_CHANNEL(ioc));
    object_unref(OBJECT(ioc));

    ret = qemu_loadvm_state(f);
    qemu_fclose(f);
    if (ret < 0) {
        error_setg(errp, "loading Xen device state failed");
    }
    migration_incoming_state_destroy();
}

bool load_snapshot(const char *name, const char *vmstate,
                   bool has_devices, strList *devices, Error **errp)
{
    BlockDriverState *bs_vm_state;
    QEMUSnapshotInfo sn;
    QEMUFile *f;
    int ret;
    MigrationIncomingState *mis = migration_incoming_get_current();

    if (!bdrv_all_can_snapshot(has_devices, devices, errp)) {
        return false;
    }
    ret = bdrv_all_has_snapshot(name, has_devices, devices, errp);
    if (ret < 0) {
        return false;
    }
    if (ret == 0) {
        error_setg(errp, "Snapshot '%s' does not exist in one or more devices",
                   name);
        return false;
    }

    bs_vm_state = bdrv_all_find_vmstate_bs(vmstate, has_devices, devices, errp);
    if (!bs_vm_state) {
        return false;
    }

    /* Don't even try to load empty VM states */
    ret = bdrv_snapshot_find(bs_vm_state, &sn, name);
    if (ret < 0) {
        return false;
    } else if (sn.vm_state_size == 0) {
        error_setg(errp, "This is a disk-only snapshot. Revert to it "
                   " offline using qemu-img");
        return false;
    }

    /*
     * Flush the record/replay queue. Now the VM state is going
     * to change. Therefore we don't need to preserve its consistency
     */
    replay_flush_events();

    /* Flush all IO requests so they don't interfere with the new state.  */
    bdrv_drain_all_begin();

    ret = bdrv_all_goto_snapshot(name, has_devices, devices, errp);
    if (ret < 0) {
        goto err_drain;
    }

    /* restore the VM state */
    f = qemu_fopen_bdrv(bs_vm_state, 0);
    if (!f) {
        error_setg(errp, "Could not open VM state file");
        goto err_drain;
    }

    qemu_system_reset(SHUTDOWN_CAUSE_SNAPSHOT_LOAD);
    mis->from_src_file = f;

    if (!yank_register_instance(MIGRATION_YANK_INSTANCE, errp)) {
        ret = -EINVAL;
        goto err_drain;
    }
    ret = qemu_loadvm_state(f);
    migration_incoming_state_destroy();

    bdrv_drain_all_end();

    if (ret < 0) {
        error_setg(errp, "Error %d while loading VM state", ret);
        return false;
    }

    return true;

err_drain:
    bdrv_drain_all_end();
    return false;
}

void load_snapshot_resume(RunState state)
{
    vm_resume(state);
    if (state == RUN_STATE_RUNNING && runstate_get() == RUN_STATE_SUSPENDED) {
        qemu_system_wakeup_request(QEMU_WAKEUP_REASON_OTHER, &error_abort);
    }
}

bool delete_snapshot(const char *name, bool has_devices,
                     strList *devices, Error **errp)
{
    if (!bdrv_all_can_snapshot(has_devices, devices, errp)) {
        return false;
    }

    if (bdrv_all_delete_snapshot(name, has_devices, devices, errp) < 0) {
        return false;
    }

    return true;
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

typedef struct SnapshotJob {
    Job common;
    char *tag;
    char *vmstate;
    strList *devices;
    Coroutine *co;
    Error **errp;
    bool ret;
} SnapshotJob;

static void qmp_snapshot_job_free(SnapshotJob *s)
{
    g_free(s->tag);
    g_free(s->vmstate);
    qapi_free_strList(s->devices);
}


static void snapshot_load_job_bh(void *opaque)
{
    Job *job = opaque;
    SnapshotJob *s = container_of(job, SnapshotJob, common);
    RunState orig_state = runstate_get();

    job_progress_set_remaining(&s->common, 1);

    vm_stop(RUN_STATE_RESTORE_VM);

    s->ret = load_snapshot(s->tag, s->vmstate, true, s->devices, s->errp);
    if (s->ret) {
        load_snapshot_resume(orig_state);
    }

    job_progress_update(&s->common, 1);

    qmp_snapshot_job_free(s);
    aio_co_wake(s->co);
}

static void snapshot_save_job_bh(void *opaque)
{
    Job *job = opaque;
    SnapshotJob *s = container_of(job, SnapshotJob, common);

    job_progress_set_remaining(&s->common, 1);
    s->ret = save_snapshot(s->tag, false, s->vmstate,
                           true, s->devices, s->errp);
    job_progress_update(&s->common, 1);

    qmp_snapshot_job_free(s);
    aio_co_wake(s->co);
}

static void snapshot_delete_job_bh(void *opaque)
{
    Job *job = opaque;
    SnapshotJob *s = container_of(job, SnapshotJob, common);

    job_progress_set_remaining(&s->common, 1);
    s->ret = delete_snapshot(s->tag, true, s->devices, s->errp);
    job_progress_update(&s->common, 1);

    qmp_snapshot_job_free(s);
    aio_co_wake(s->co);
}

static int coroutine_fn snapshot_save_job_run(Job *job, Error **errp)
{
    SnapshotJob *s = container_of(job, SnapshotJob, common);
    s->errp = errp;
    s->co = qemu_coroutine_self();
    aio_bh_schedule_oneshot(qemu_get_aio_context(),
                            snapshot_save_job_bh, job);
    qemu_coroutine_yield();
    return s->ret ? 0 : -1;
}

static int coroutine_fn snapshot_load_job_run(Job *job, Error **errp)
{
    SnapshotJob *s = container_of(job, SnapshotJob, common);
    s->errp = errp;
    s->co = qemu_coroutine_self();
    aio_bh_schedule_oneshot(qemu_get_aio_context(),
                            snapshot_load_job_bh, job);
    qemu_coroutine_yield();
    return s->ret ? 0 : -1;
}

static int coroutine_fn snapshot_delete_job_run(Job *job, Error **errp)
{
    SnapshotJob *s = container_of(job, SnapshotJob, common);
    s->errp = errp;
    s->co = qemu_coroutine_self();
    aio_bh_schedule_oneshot(qemu_get_aio_context(),
                            snapshot_delete_job_bh, job);
    qemu_coroutine_yield();
    return s->ret ? 0 : -1;
}


static const JobDriver snapshot_load_job_driver = {
    .instance_size = sizeof(SnapshotJob),
    .job_type      = JOB_TYPE_SNAPSHOT_LOAD,
    .run           = snapshot_load_job_run,
};

static const JobDriver snapshot_save_job_driver = {
    .instance_size = sizeof(SnapshotJob),
    .job_type      = JOB_TYPE_SNAPSHOT_SAVE,
    .run           = snapshot_save_job_run,
};

static const JobDriver snapshot_delete_job_driver = {
    .instance_size = sizeof(SnapshotJob),
    .job_type      = JOB_TYPE_SNAPSHOT_DELETE,
    .run           = snapshot_delete_job_run,
};


void qmp_snapshot_save(const char *job_id,
                       const char *tag,
                       const char *vmstate,
                       strList *devices,
                       Error **errp)
{
    SnapshotJob *s;

    s = job_create(job_id, &snapshot_save_job_driver, NULL,
                   qemu_get_aio_context(), JOB_MANUAL_DISMISS,
                   NULL, NULL, errp);
    if (!s) {
        return;
    }

    s->tag = g_strdup(tag);
    s->vmstate = g_strdup(vmstate);
    s->devices = QAPI_CLONE(strList, devices);

    job_start(&s->common);
}

void qmp_snapshot_load(const char *job_id,
                       const char *tag,
                       const char *vmstate,
                       strList *devices,
                       Error **errp)
{
    SnapshotJob *s;

    s = job_create(job_id, &snapshot_load_job_driver, NULL,
                   qemu_get_aio_context(), JOB_MANUAL_DISMISS,
                   NULL, NULL, errp);
    if (!s) {
        return;
    }

    s->tag = g_strdup(tag);
    s->vmstate = g_strdup(vmstate);
    s->devices = QAPI_CLONE(strList, devices);

    job_start(&s->common);
}

void qmp_snapshot_delete(const char *job_id,
                         const char *tag,
                         strList *devices,
                         Error **errp)
{
    SnapshotJob *s;

    s = job_create(job_id, &snapshot_delete_job_driver, NULL,
                   qemu_get_aio_context(), JOB_MANUAL_DISMISS,
                   NULL, NULL, errp);
    if (!s) {
        return;
    }

    s->tag = g_strdup(tag);
    s->devices = QAPI_CLONE(strList, devices);

    job_start(&s->common);
}
