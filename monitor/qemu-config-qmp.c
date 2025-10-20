/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/target-info.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc.h"
#include "qobject/qlist.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "hw/boards.h"

static CommandLineParameterInfoList *query_option_descs(const QemuOptDesc *desc)
{
    CommandLineParameterInfoList *param_list = NULL;
    CommandLineParameterInfo *info;
    int i;

    for (i = 0; desc[i].name != NULL; i++) {
        info = g_malloc0(sizeof(*info));
        info->name = g_strdup(desc[i].name);

        switch (desc[i].type) {
        case QEMU_OPT_STRING:
            info->type = COMMAND_LINE_PARAMETER_TYPE_STRING;
            break;
        case QEMU_OPT_BOOL:
            info->type = COMMAND_LINE_PARAMETER_TYPE_BOOLEAN;
            break;
        case QEMU_OPT_NUMBER:
            info->type = COMMAND_LINE_PARAMETER_TYPE_NUMBER;
            break;
        case QEMU_OPT_SIZE:
            info->type = COMMAND_LINE_PARAMETER_TYPE_SIZE;
            break;
        }

        info->help = g_strdup(desc[i].help);
        info->q_default = g_strdup(desc[i].def_value_str);

        QAPI_LIST_PREPEND(param_list, info);
    }

    return param_list;
}

/* remove repeated entry from the info list */
static void cleanup_infolist(CommandLineParameterInfoList *head)
{
    CommandLineParameterInfoList *pre_entry, *cur, *del_entry;

    cur = head;
    while (cur->next) {
        pre_entry = head;
        while (pre_entry != cur->next) {
            if (!strcmp(pre_entry->value->name, cur->next->value->name)) {
                del_entry = cur->next;
                cur->next = cur->next->next;
                del_entry->next = NULL;
                qapi_free_CommandLineParameterInfoList(del_entry);
                break;
            }
            pre_entry = pre_entry->next;
        }
        cur = cur->next;
    }
}

/* merge the description items of two parameter infolists */
static void connect_infolist(CommandLineParameterInfoList *head,
                             CommandLineParameterInfoList *new)
{
    CommandLineParameterInfoList *cur;

    cur = head;
    while (cur->next) {
        cur = cur->next;
    }
    cur->next = new;
}

/* access all the local QemuOptsLists for drive option */
static CommandLineParameterInfoList *get_drive_infolist(void)
{
    CommandLineParameterInfoList *head = NULL, *cur;
    int i;

    for (i = 0; drive_config_groups[i] != NULL; i++) {
        if (!head) {
            head = query_option_descs(drive_config_groups[i]->desc);
        } else {
            cur = query_option_descs(drive_config_groups[i]->desc);
            connect_infolist(head, cur);
        }
    }
    cleanup_infolist(head);

    return head;
}

static CommandLineParameterInfo *objprop_to_cmdline_prop(ObjectProperty *prop)
{
    CommandLineParameterInfo *info;

    info = g_malloc0(sizeof(*info));
    info->name = g_strdup(prop->name);

    if (g_str_equal(prop->type, "bool") || g_str_equal(prop->type, "OnOffAuto")) {
        info->type = COMMAND_LINE_PARAMETER_TYPE_BOOLEAN;
    } else if (g_str_equal(prop->type, "int")) {
        info->type = COMMAND_LINE_PARAMETER_TYPE_NUMBER;
    } else if (g_str_equal(prop->type, "size")) {
        info->type = COMMAND_LINE_PARAMETER_TYPE_SIZE;
    } else {
        info->type = COMMAND_LINE_PARAMETER_TYPE_STRING;
    }

    if (prop->description) {
        info->help = g_strdup(prop->description);
    }

    return info;
}

static CommandLineParameterInfoList *query_all_machine_properties(void)
{
    CommandLineParameterInfoList *params = NULL, *clpiter;
    CommandLineParameterInfo *info;
    GSList *machines, *curr_mach;
    ObjectPropertyIterator op_iter;
    ObjectProperty *prop;
    bool is_new;

    machines = object_class_get_list(target_machine_typename(), false);
    assert(machines);

    /* Loop over all machine classes */
    for (curr_mach = machines; curr_mach; curr_mach = curr_mach->next) {
        object_class_property_iter_init(&op_iter, curr_mach->data);
        /* ... and over the properties of each machine: */
        while ((prop = object_property_iter_next(&op_iter))) {
            if (!prop->set) {
                continue;
            }
            /*
             * Check whether the property has already been put into the list
             * (via another machine class)
             */
            is_new = true;
            for (clpiter = params; clpiter != NULL; clpiter = clpiter->next) {
                if (g_str_equal(clpiter->value->name, prop->name)) {
                    is_new = false;
                    break;
                }
            }
            /* If it hasn't been added before, add it now to the list */
            if (is_new) {
                info = objprop_to_cmdline_prop(prop);
                QAPI_LIST_PREPEND(params, info);
            }
        }
    }

    g_slist_free(machines);

    /* Add entry for the "type" parameter */
    info = g_malloc0(sizeof(*info));
    info->name = g_strdup("type");
    info->type = COMMAND_LINE_PARAMETER_TYPE_STRING;
    info->help = g_strdup("machine type");
    QAPI_LIST_PREPEND(params, info);

    return params;
}

CommandLineOptionInfoList *qmp_query_command_line_options(const char *option,
                                                          Error **errp)
{
    CommandLineOptionInfoList *conf_list = NULL;
    CommandLineOptionInfo *info;
    int i;

    for (i = 0; vm_config_groups[i] != NULL; i++) {
        if (!option || !strcmp(option, vm_config_groups[i]->name)) {
            info = g_malloc0(sizeof(*info));
            info->option = g_strdup(vm_config_groups[i]->name);
            if (!strcmp("drive", vm_config_groups[i]->name)) {
                info->parameters = get_drive_infolist();
            } else {
                info->parameters =
                    query_option_descs(vm_config_groups[i]->desc);
            }
            QAPI_LIST_PREPEND(conf_list, info);
        }
    }

    if (!option || !strcmp(option, "machine")) {
        info = g_malloc0(sizeof(*info));
        info->option = g_strdup("machine");
        info->parameters = query_all_machine_properties();
        QAPI_LIST_PREPEND(conf_list, info);
    }

    if (conf_list == NULL) {
        error_setg(errp, "invalid option name: %s", option);
    }

    return conf_list;
}
