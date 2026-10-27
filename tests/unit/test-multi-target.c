/*
 * Test for multiple targets
 *
 * Ensures targets compatible can be linked together.
 * Also that each QOM type is unique among them and can be initialized.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Author:
 *  Pierrick Bouvier <pierrick.bouvier@oss.qualcomm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/target-info.h"
#include "qemu/target-info-qom.h"
#include "qom/object.h"

#ifdef CONFIG_SDL
/*
 * SDL insists on wrapping the main() function with its own implementation on
 * some platforms; it does so via a macro that renames our main function, so
 * <SDL.h> must be #included here even with no SDL code called from this file.
 */
#include <SDL.h>
#endif

/* stub for system/main.c */
int (*qemu_main)(void);

static void test_qom_types_are_unique(const void *data)
{
    const TargetInfo *ti = data;

    /* QOM types can be initialized only once, so do it in a subprocess. */
    if (!g_test_subprocess()) {
        /* parent process path - respawn this exact test path */
        g_test_trap_subprocess(NULL, 0, G_TEST_SUBPROCESS_INHERIT_STDERR);
        g_test_trap_assert_passed();
        return;
    }
    /* child process path */

    target_info_qom_set_target_info(ti);

    /* register all types */
    module_call_init(MODULE_INIT_QOM);
    /* trigger type_initialize for all types */
    object_class_get_list("", false);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_TARGET_INFO);
    g_autoptr(GSList) targets = object_class_get_list_sorted(TYPE_TARGET_INFO,
                                                             false);
    g_assert(targets);
    for (GSList *elem = targets; elem; elem = elem->next) {
        const TargetInfo *ti = TARGET_INFO_CLASS(elem->data)->target_info;
        g_autofree char *path =
            g_strdup_printf("/multi-targets/qom-types-are-unique/%s",
                            ti->target_name);
        g_test_add_data_func(path, ti, test_qom_types_are_unique);
    }

    return g_test_run();
}
