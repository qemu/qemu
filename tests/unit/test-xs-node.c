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

    err = xs_impl_rm(s, DOMID_QEMU, XBT_NULL, "/local");
    g_assert(!err);
    g_assert(s->nr_nodes == 1);

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

static XenstoreImplState *setup(void)
{
   XenstoreImplState *s = xs_impl_create();
   char *abspath;
   int err;

   abspath = g_strdup_printf("/local/domain/%u", DOMID_GUEST);

   err = write_str(s, DOMID_QEMU, XBT_NULL, abspath, "");
   g_assert(!err);

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
    GList *items = NULL;
    XsNode *old_root;
    uint64_t gencnt;
    int err;

    g_assert(s);

    /* Read gives ENOENT when it should */
    err = xs_impl_read(s, DOMID_GUEST, XBT_NULL, "foo", data);
    g_assert(err == ENOENT);

    /* Write works */
    err = write_str(s, DOMID_GUEST, XBT_NULL, "some/relative/path",
                    "something");
    g_assert(s->nr_nodes == 7);
    g_assert(!err);

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

    /* Keep a copy, to force COW mode */
    old_root = xs_node_ref(s->root);

    /* Write works again */
    err = write_str(s, DOMID_GUEST, XBT_NULL,
                    "/local/domain/1/some/relative/path2",
                    "something else");
    g_assert(!err);
    g_assert(s->nr_nodes == 8);

    /* Overwrite an existing node */
    err = write_str(s, DOMID_GUEST, XBT_NULL, "some/relative/path",
                    "another thing");
    g_assert(!err);
    g_assert(s->nr_nodes == 8);

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

    /* Write somewhere else which already existed */
    err = write_str(s, DOMID_GUEST, XBT_NULL, "some/relative", "moredata");
    g_assert(!err);

    g_byte_array_set_size(data, 0);
    err = xs_impl_read(s, DOMID_GUEST, XBT_NULL, "some/relative", data);
    g_assert(!err);
    g_assert(data->len == strlen("moredata"));
    g_assert(!memcmp(data->data, "moredata", data->len));

    /* Overwrite existing data */
    err = write_str(s, DOMID_GUEST, XBT_NULL, "some/relative", "otherdata");
    g_assert(!err);

    g_byte_array_set_size(data, 0);
    err = xs_impl_read(s, DOMID_GUEST, XBT_NULL, "some/relative", data);
    g_assert(!err);
    g_assert(data->len == strlen("otherdata"));
    g_assert(!memcmp(data->data, "otherdata", data->len));

    /* Remove the subtree */
    err = xs_impl_rm(s, DOMID_GUEST, XBT_NULL, "some/relative");
    g_assert(!err);
    g_assert(s->nr_nodes == 5);

    g_byte_array_set_size(data, 0);
    err = xs_impl_read(s, DOMID_GUEST, XBT_NULL, "some/relative", data);
    g_assert(err == ENOENT);
    g_byte_array_unref(data);

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
