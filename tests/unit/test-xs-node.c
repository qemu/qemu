/*
 * QEMU XenStore XsNode testing
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.

 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"

static int nr_xs_nodes;
static GList *xs_node_list;

#define XS_NODE_UNIT_TEST

/*
 * We don't need the core Xen definitions. And we *do* want to be able
 * to run the unit tests even on architectures that Xen doesn't support
 * (because life's too short to bother doing otherwise, and test coverage
 * doesn't hurt).
 */
#define __XEN_PUBLIC_XEN_H__

#include "hw/i386/kvm/xenstore_impl.c"

#define DOMID_QEMU 0
#define DOMID_GUEST 1

/* This doesn't happen in qemu but we want to make valgrind happy */
static void xs_impl_delete(XenstoreImplState *s)
{
    int err;

    xs_impl_reset_watches(s, DOMID_GUEST);
    g_assert(!s->nr_domu_watches);

    err = xs_impl_rm(s, DOMID_QEMU, XBT_NULL, "/local");
    g_assert(!err);
    g_assert(s->nr_nodes == 1);

    g_hash_table_unref(s->watches);
    xs_node_unref(s->root);
    g_free(s);

    if (xs_node_list) {
        GList *l;
        for (l = xs_node_list; l; l = l->next) {
            XsNode *n = l->data;
            printf("Remaining node at %p name %s ref %u\n", n, n->name,
                   n->ref);
        }
    }
    g_assert(!nr_xs_nodes);
}

static int write_str(XenstoreImplState *s, unsigned int dom_id,
                          unsigned int tx_id, const char *path,
                          const char *content)
{
    GByteArray *d = g_byte_array_new();
    int err;

    g_byte_array_append(d, (void *)content, strlen(content));
    err = xs_impl_write(s, dom_id, tx_id, path, d);
    g_byte_array_unref(d);
    return err;
}

static void watch_cb(void *_str, const char *path, const char *token)
{
    GString *str = _str;

    g_string_append(str, path);
    g_string_append(str, token);
}

static XenstoreImplState *setup(void)
{
   XenstoreImplState *s = xs_impl_create();
   char *abspath;
   int err;

   abspath = g_strdup_printf("/local/domain/%u", DOMID_GUEST);

   err = write_str(s, DOMID_QEMU, XBT_NULL, abspath, "");
   g_assert(!err);
   g_assert(s->nr_nodes == 4);

   g_free(abspath);

   abspath = g_strdup_printf("/local/domain/%u/some", DOMID_GUEST);

   err = write_str(s, DOMID_QEMU, XBT_NULL, abspath, "");
   g_assert(!err);
   g_assert(s->nr_nodes == 5);

   g_free(abspath);

   return s;
}

static void test_xs_node_simple(void)
{
    GByteArray *data = g_byte_array_new();
    XenstoreImplState *s = setup();
    GString *guest_watches = g_string_new(NULL);
    GString *qemu_watches = g_string_new(NULL);
    GList *items = NULL;
    XsNode *old_root;
    uint64_t gencnt;
    int err;

    g_assert(s);

    err = xs_impl_watch(s, DOMID_GUEST, "some", "guestwatch",
                        watch_cb, guest_watches);
    g_assert(!err);
    g_assert(guest_watches->len == strlen("someguestwatch"));
    g_assert(!strcmp(guest_watches->str, "someguestwatch"));
    g_string_truncate(guest_watches, 0);

    err = xs_impl_watch(s, 0, "/local/domain/1/some", "qemuwatch",
                        watch_cb, qemu_watches);
    g_assert(!err);
    g_assert(qemu_watches->len == strlen("/local/domain/1/someqemuwatch"));
    g_assert(!strcmp(qemu_watches->str, "/local/domain/1/someqemuwatch"));
    g_string_truncate(qemu_watches, 0);

    /* Read gives ENOENT when it should */
    err = xs_impl_read(s, DOMID_GUEST, XBT_NULL, "foo", data);
    g_assert(err == ENOENT);

    /* Write works */
    err = write_str(s, DOMID_GUEST, XBT_NULL, "some/relative/path",
                    "something");
    g_assert(s->nr_nodes == 7);
    g_assert(!err);
    g_assert(!strcmp(guest_watches->str,
                     "some/relative/pathguestwatch"));
    g_assert(!strcmp(qemu_watches->str,
                     "/local/domain/1/some/relative/pathqemuwatch"));

    g_string_truncate(qemu_watches, 0);
    g_string_truncate(guest_watches, 0);
    xs_impl_reset_watches(s, 0);

    /* Read gives back what we wrote */
    err = xs_impl_read(s, DOMID_GUEST, XBT_NULL, "some/relative/path", data);
    g_assert(!err);
    g_assert(data->len == strlen("something"));
    g_assert(!memcmp(data->data, "something", data->len));

    /* Even if we use an abolute path */
    g_byte_array_set_size(data, 0);
    err = xs_impl_read(s, DOMID_GUEST, XBT_NULL,
                       "/local/domain/1/some/relative/path", data);
    g_assert(!err);
    g_assert(data->len == strlen("something"));

    g_assert(!qemu_watches->len);
    g_assert(!guest_watches->len);
    /* Keep a copy, to force COW mode */
    old_root = xs_node_ref(s->root);

    /* Write works again */
    err = write_str(s, DOMID_GUEST, XBT_NULL,
                    "/local/domain/1/some/relative/path2",
                    "something else");
    g_assert(!err);
    g_assert(s->nr_nodes == 8);
    g_assert(!qemu_watches->len);
    g_assert(!strcmp(guest_watches->str, "some/relative/path2guestwatch"));
    g_string_truncate(guest_watches, 0);

    /* Overwrite an existing node */
    err = write_str(s, DOMID_GUEST, XBT_NULL, "some/relative/path",
                    "another thing");
    g_assert(!err);
    g_assert(s->nr_nodes == 8);
    g_assert(!qemu_watches->len);
    g_assert(!strcmp(guest_watches->str, "some/relative/pathguestwatch"));
    g_string_truncate(guest_watches, 0);

    /* We can list the two files we wrote */
    err = xs_impl_directory(s, DOMID_GUEST, XBT_NULL, "some/relative", &gencnt,
                            &items);
    g_assert(!err);
    g_assert(items);
    g_assert(gencnt == 2);
    g_assert(!strcmp(items->data, "path"));
    g_assert(items->next);
    g_assert(!strcmp(items->next->data, "path2"));
    g_assert(!items->next->next);
    g_list_free_full(items, g_free);

    err = xs_impl_unwatch(s, DOMID_GUEST, "some", "guestwatch",
                          watch_cb, guest_watches);
    g_assert(!err);

    err = xs_impl_unwatch(s, DOMID_GUEST, "some", "guestwatch",
                          watch_cb, guest_watches);
    g_assert(err == ENOENT);

    err = xs_impl_watch(s, DOMID_GUEST, "some/relative/path2", "watchp2",
                        watch_cb, guest_watches);
    g_assert(!err);
    g_assert(guest_watches->len == strlen("some/relative/path2watchp2"));
    g_assert(!strcmp(guest_watches->str, "some/relative/path2watchp2"));
    g_string_truncate(guest_watches, 0);

    err = xs_impl_watch(s, DOMID_GUEST, "/local/domain/1/some/relative",
                        "watchrel", watch_cb, guest_watches);
    g_assert(!err);
    g_assert(guest_watches->len ==
             strlen("/local/domain/1/some/relativewatchrel"));
    g_assert(!strcmp(guest_watches->str,
                     "/local/domain/1/some/relativewatchrel"));
    g_string_truncate(guest_watches, 0);

    /* Write somewhere else which already existed */
    err = write_str(s, DOMID_GUEST, XBT_NULL, "some/relative", "moredata");
    g_assert(!err);
    g_assert(s->nr_nodes == 8);

    g_assert(!strcmp(guest_watches->str,
                     "/local/domain/1/some/relativewatchrel"));
    g_string_truncate(guest_watches, 0);

    g_byte_array_set_size(data, 0);
    err = xs_impl_read(s, DOMID_GUEST, XBT_NULL, "some/relative", data);
    g_assert(!err);
    g_assert(data->len == strlen("moredata"));
    g_assert(!memcmp(data->data, "moredata", data->len));

    /* Overwrite existing data */
    err = write_str(s, DOMID_GUEST, XBT_NULL, "some/relative", "otherdata");
    g_assert(!err);
    g_string_truncate(guest_watches, 0);

    g_byte_array_set_size(data, 0);
    err = xs_impl_read(s, DOMID_GUEST, XBT_NULL, "some/relative", data);
    g_assert(!err);
    g_assert(data->len == strlen("otherdata"));
    g_assert(!memcmp(data->data, "otherdata", data->len));

    /* Remove the subtree */
    err = xs_impl_rm(s, DOMID_GUEST, XBT_NULL, "some/relative");
    g_assert(!err);
    g_assert(s->nr_nodes == 5);

    /* Each watch fires with the least specific relevant path */
    g_assert(strstr(guest_watches->str,
                    "some/relative/path2watchp2"));
    g_assert(strstr(guest_watches->str,
                    "/local/domain/1/some/relativewatchrel"));
    g_string_truncate(guest_watches, 0);

    g_byte_array_set_size(data, 0);
    err = xs_impl_read(s, DOMID_GUEST, XBT_NULL, "some/relative", data);
    g_assert(err == ENOENT);
    g_byte_array_unref(data);

    xs_impl_reset_watches(s, DOMID_GUEST);
    g_string_free(qemu_watches, true);
    g_string_free(guest_watches, true);
    xs_node_unref(old_root);
    xs_impl_delete(s);
}


int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    module_call_init(MODULE_INIT_QOM);

    g_test_add_func("/xs_node/simple", test_xs_node_simple);

    return g_test_run();
}
