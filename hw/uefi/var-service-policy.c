/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * uefi vars device - VarCheckPolicyLibMmiHandler implementation
 *
 * variable policy specs:
 * https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Library/VariablePolicyLib/ReadMe.md
 */
#include "qemu/osdep.h"
#include "system/dma.h"
#include "migration/vmstate.h"

#include "hw/uefi/var-service.h"
#include "hw/uefi/var-service-api.h"
#include "hw/uefi/var-service-edk2.h"

#include "trace/trace-hw_uefi.h"

static void calc_policy(uefi_var_policy *pol);

static int uefi_var_policy_post_load(void *opaque, int version_id)
{
    uefi_var_policy *pol = opaque;

    calc_policy(pol);
    return 0;
}

const VMStateDescription vmstate_uefi_var_policy = {
    .name = "uefi-var-policy",
    .post_load = uefi_var_policy_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(entry_size, uefi_var_policy),
        VMSTATE_VBUFFER_ALLOC_UINT32(entry, uefi_var_policy,
                                     0, NULL, entry_size),
        VMSTATE_END_OF_LIST()
    },
};

static void print_policy_entry(variable_policy_entry *pe)
{
    uint16_t *name = (void *)pe + pe->offset_to_name;

    fprintf(stderr, "%s:\n", __func__);

    fprintf(stderr, "    name ´");
    while (*name) {
        fprintf(stderr, "%c", *name);
        name++;
    }
    fprintf(stderr, "', version=%d.%d, size=%d\n",
            pe->version >> 16, pe->version & 0xffff, pe->size);

    if (pe->min_size) {
        fprintf(stderr, "    size min=%d\n", pe->min_size);
    }
    if (pe->max_size != UINT32_MAX) {
        fprintf(stderr, "    size max=%u\n", pe->max_size);
    }
    if (pe->attributes_must_have) {
        fprintf(stderr, "    attr must=0x%x\n", pe->attributes_must_have);
    }
    if (pe->attributes_cant_have) {
        fprintf(stderr, "    attr cant=0x%x\n", pe->attributes_cant_have);
    }
    if (pe->lock_policy_type) {
        fprintf(stderr, "    lock policy type %d\n", pe->lock_policy_type);
    }
}

static gboolean wildcard_str_equal(uefi_var_policy *pol,
                                   uefi_variable *var)
{
    return uefi_str_equal_ex(pol->name, pol->name_size,
                             var->name, var->name_size,
                             true);
}

static uefi_var_policy *find_policy(uefi_vars_state *uv, QemuUUID guid,
                                    uint16_t *name, uint64_t name_size)
{
    uefi_var_policy *pol;

    QTAILQ_FOREACH(pol, &uv->var_policies, next) {
        if (!qemu_uuid_is_equal(&pol->entry->namespace, &guid)) {
            continue;
        }
        if (!uefi_str_equal(pol->name, pol->name_size,
                            name, name_size)) {
            continue;
        }
        return pol;
    }
    return NULL;
}

static uefi_var_policy *wildcard_find_policy(uefi_vars_state *uv,
                                             uefi_variable *var)
{
    uefi_var_policy *pol;

    QTAILQ_FOREACH(pol, &uv->var_policies, next) {
        if (!qemu_uuid_is_equal(&pol->entry->namespace, &var->guid)) {
            continue;
        }
        if (!wildcard_str_equal(pol, var)) {
            continue;
        }
        return pol;
    }
    return NULL;
}

static void calc_policy(uefi_var_policy *pol)
{
    variable_policy_entry *pe = pol->entry;
    unsigned int i;

    pol->name = (void *)pol->entry + pe->offset_to_name;
    pol->name_size = pe->size - pe->offset_to_name;

    for (i = 0; i < pol->name_size / 2; i++) {
        if (pol->name[i] == '#') {
            pol->hashmarks++;
        }
    }
}

uefi_var_policy *uefi_vars_add_policy(uefi_vars_state *uv,
                                      variable_policy_entry *pe)
{
    uefi_var_policy *pol, *p;

    pol = g_new0(uefi_var_policy, 1);
    pol->entry = g_malloc(pe->size);
    memcpy(pol->entry, pe, pe->size);
    pol->entry_size = pe->size;

    calc_policy(pol);

    /* keep list sorted by priority, add to tail of priority group */
    QTAILQ_FOREACH(p, &uv->var_policies, next) {
        if ((p->hashmarks > pol->hashmarks) ||
            (!p->name_size && pol->name_size)) {
            QTAILQ_INSERT_BEFORE(p, pol, next);
            return pol;
        }
    }

    QTAILQ_INSERT_TAIL(&uv->var_policies, pol, next);
    return pol;
}

efi_status uefi_vars_policy_check(uefi_vars_state *uv,
                                  uefi_variable *var,
                                  gboolean is_newvar)
{
    uefi_var_policy *pol;
    variable_policy_entry *pe;
    variable_lock_on_var_state *lvarstate;
    uint16_t *lvarname;
    size_t lvarnamesize;
    uefi_variable *lvar;

    if (!uv->end_of_dxe) {
        return EFI_SUCCESS;
    }

    pol = wildcard_find_policy(uv, var);
    if (!pol) {
        return EFI_SUCCESS;
    }
    pe = pol->entry;

    uefi_trace_variable(__func__, var->guid, var->name, var->name_size);
    print_policy_entry(pe);

    if ((var->attributes & pe->attributes_must_have) != pe->attributes_must_have) {
        trace_uefi_vars_policy_deny("must-have-attr");
        return EFI_INVALID_PARAMETER;
    }
    if ((var->attributes & pe->attributes_cant_have) != 0) {
        trace_uefi_vars_policy_deny("cant-have-attr");
        return EFI_INVALID_PARAMETER;
    }

    if (var->data_size < pe->min_size) {
        trace_uefi_vars_policy_deny("min-size");
        return EFI_INVALID_PARAMETER;
    }
    if (var->data_size > pe->max_size) {
        trace_uefi_vars_policy_deny("max-size");
        return EFI_INVALID_PARAMETER;
    }

    switch (pe->lock_policy_type) {
    case VARIABLE_POLICY_TYPE_NO_LOCK:
        break;

    case VARIABLE_POLICY_TYPE_LOCK_NOW:
        trace_uefi_vars_policy_deny("lock-now");
        return EFI_WRITE_PROTECTED;

    case VARIABLE_POLICY_TYPE_LOCK_ON_CREATE:
        if (!is_newvar) {
            trace_uefi_vars_policy_deny("lock-on-create");
            return EFI_WRITE_PROTECTED;
        }
        break;

    case VARIABLE_POLICY_TYPE_LOCK_ON_VAR_STATE:
        lvarstate    = (void *)pol->entry + sizeof(*pe);
        lvarname     = (void *)pol->entry + sizeof(*pe) + sizeof(*lvarstate);
        lvarnamesize = pe->offset_to_name - sizeof(*pe) - sizeof(*lvarstate);

        uefi_trace_variable(__func__, lvarstate->namespace,
                            lvarname, lvarnamesize);
        lvar = uefi_vars_find_variable(uv, lvarstate->namespace,
                                          lvarname, lvarnamesize);
        if (lvar && lvar->data_size == 1) {
            uint8_t *value = lvar->data;
            if (lvarstate->value == *value) {
                return EFI_WRITE_PROTECTED;
            }
        }
        break;
    }

    return EFI_SUCCESS;
}

void uefi_vars_policies_clear(uefi_vars_state *uv)
{
    uefi_var_policy *pol;

    while (!QTAILQ_EMPTY(&uv->var_policies)) {
        pol = QTAILQ_FIRST(&uv->var_policies);
        QTAILQ_REMOVE(&uv->var_policies, pol, next);
        g_free(pol->entry);
        g_free(pol);
    }
}

static size_t uefi_vars_mm_policy_error(mm_header *mhdr,
                                        mm_check_policy *mchk,
                                        uint64_t status)
{
    mchk->result = status;
    return sizeof(*mchk);
}

static uint32_t uefi_vars_mm_check_policy_is_enabled(uefi_vars_state *uv,
                                                     mm_header       *mhdr,
                                                     mm_check_policy *mchk,
                                                     void            *func)
{
    mm_check_policy_is_enabled *mpar = func;
    size_t length;

    length = sizeof(*mchk) + sizeof(*mpar);
    if (mhdr->length < length) {
        return uefi_vars_mm_policy_error(mhdr, mchk, EFI_BAD_BUFFER_SIZE);
    }

    mpar->state  = TRUE;
    mchk->result = EFI_SUCCESS;
    return sizeof(*mchk);
}

static uint32_t uefi_vars_mm_check_policy_register(uefi_vars_state *uv,
                                                   mm_header       *mhdr,
                                                   mm_check_policy *mchk,
                                                   void            *func)
{
    variable_policy_entry *pe = func;
    uefi_var_policy *pol;
    uint64_t length;

    if (uadd64_overflow(sizeof(*mchk), pe->size, &length)) {
        return uefi_vars_mm_policy_error(mhdr, mchk, EFI_BAD_BUFFER_SIZE);
    }
    if (mhdr->length < length) {
        return uefi_vars_mm_policy_error(mhdr, mchk, EFI_BAD_BUFFER_SIZE);
    }
    if (pe->size < sizeof(*pe)) {
        return uefi_vars_mm_policy_error(mhdr, mchk, EFI_BAD_BUFFER_SIZE);
    }
    if (pe->offset_to_name < sizeof(*pe)) {
        return uefi_vars_mm_policy_error(mhdr, mchk, EFI_BAD_BUFFER_SIZE);
    }

    if (pe->lock_policy_type == VARIABLE_POLICY_TYPE_LOCK_ON_VAR_STATE &&
        pe->offset_to_name < sizeof(*pe) + sizeof(variable_lock_on_var_state)) {
        return uefi_vars_mm_policy_error(mhdr, mchk, EFI_BAD_BUFFER_SIZE);
    }

    /* check space for minimum string length */
    if (pe->size < (size_t)pe->offset_to_name) {
        return uefi_vars_mm_policy_error(mhdr, mchk, EFI_BAD_BUFFER_SIZE);
    }

    if (!uefi_str_is_valid((void *)pe + pe->offset_to_name,
                           pe->size - pe->offset_to_name,
                           false)) {
        return uefi_vars_mm_policy_error(mhdr, mchk, EFI_INVALID_PARAMETER);
    }

    pol = find_policy(uv, pe->namespace,
                      (void *)pe + pe->offset_to_name,
                      pe->size - pe->offset_to_name);
    if (pol) {
        return uefi_vars_mm_policy_error(mhdr, mchk, EFI_ALREADY_STARTED);
    }

    uefi_vars_add_policy(uv, pe);

    mchk->result = EFI_SUCCESS;
    return sizeof(*mchk);
}

uint32_t uefi_vars_mm_check_policy_proto(uefi_vars_state *uv)
{
    static const char *fnames[] = {
        "zero",
        "disable",
        "is-enabled",
        "register",
        "dump",
        "lock",
    };
    const char      *fname;
    mm_header       *mhdr = (mm_header *) uv->buffer;
    mm_check_policy *mchk = (mm_check_policy *) (uv->buffer + sizeof(*mhdr));
    void            *func = (uv->buffer + sizeof(*mhdr) + sizeof(*mchk));

    if (mhdr->length < sizeof(*mchk)) {
        return UEFI_VARS_STS_ERR_BAD_BUFFER_SIZE;
    }

    fname = mchk->command < ARRAY_SIZE(fnames)
        ? fnames[mchk->command]
        : "unknown";
    trace_uefi_vars_policy_cmd(fname);

    switch (mchk->command) {
    case VAR_CHECK_POLICY_COMMAND_DISABLE:
        mchk->result = EFI_UNSUPPORTED;
        break;
    case VAR_CHECK_POLICY_COMMAND_IS_ENABLED:
        uefi_vars_mm_check_policy_is_enabled(uv, mhdr, mchk, func);
        break;
    case VAR_CHECK_POLICY_COMMAND_REGISTER:
        if (uv->policy_locked) {
            mchk->result = EFI_WRITE_PROTECTED;
        } else {
            uefi_vars_mm_check_policy_register(uv, mhdr, mchk, func);
        }
        break;
    case VAR_CHECK_POLICY_COMMAND_LOCK:
        uv->policy_locked = true;
        mchk->result = EFI_SUCCESS;
        break;
    default:
        mchk->result = EFI_UNSUPPORTED;
        break;
    }

    uefi_trace_status(__func__, mchk->result);
    return UEFI_VARS_STS_SUCCESS;
}
