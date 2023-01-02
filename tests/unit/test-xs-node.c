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
typedef unsigned long xen_pfn_t;

#include "hw/i386/kvm/xenstore_impl.c"

#define DOMID_QEMU 0
#define DOMID_GUEST 1

static void dump_ref(const char *name, XsNode *n, int indent)
{
    int i;

    if (!indent && name) {
        printf("%s:\n", name);
    }

    for (i = 0; i < indent; i++) {
        printf(" ");
    }

    printf("->%p(%d, '%s'): '%.*s'%s%s\n", n, n->ref, n->name,
           (int)(n->content ? n->content->len : strlen("<empty>")),
           n->content ? (char *)n->content->data : "<empty>",
           n->modified_in_tx ? " MODIFIED" : "",
           n->deleted_in_tx ? " DELETED" : "");

    if (n->children) {
        g_hash_table_foreach(n->children, (void *)dump_ref,
                             GINT_TO_POINTER(indent + 2));
    }
}

/* This doesn't happen in qemu but we want to make valgrind happy */
static void xs_impl_delete(XenstoreImplState *s, bool last)
{
    int err;

    xs_impl_reset_watches(s, DOMID_GUEST);
    g_assert(!s->nr_domu_watches);

    err = xs_impl_rm(s, DOMID_QEMU, XBT_NULL, "/local");
    g_assert(!err);
    g_assert(s->nr_nodes == 1);

    g_hash_table_unref(s->watches);
    g_hash_table_unref(s->transactions);
    xs_node_unref(s->root);
    g_free(s);

    if (!last) {
        return;
    }

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

struct compare_walk {
    char path[XENSTORE_ABS_PATH_MAX + 1];
    XsNode *parent_2;
    bool compare_ok;
};


static bool compare_perms(GList *p1, GList *p2)
{
    while (p1) {
        if (!p2 || g_strcmp0(p1->data, p2->data)) {
            return false;
        }
        p1 = p1->next;
        p2 = p2->next;
    }
    return (p2 == NULL);
}

static bool compare_content(GByteArray *c1, GByteArray *c2)
{
    size_t len1 = 0, len2 = 0;

    if (c1) {
        len1 = c1->len;
    }
    if (c2) {
        len2 = c2->len;
    }
    if (len1 != len2) {
        return false;
    }

    if (!len1) {
        return true;
    }

    return !memcmp(c1->data, c2->data, len1);
}

static void compare_child(gpointer, gpointer, gpointer);

static void compare_nodes(struct compare_walk *cw, XsNode *n1, XsNode *n2)
{
    int nr_children1 = 0, nr_children2 = 0;

    if (n1->children) {
        nr_children1 = g_hash_table_size(n1->children);
    }
    if (n2->children) {
        nr_children2 = g_hash_table_size(n2->children);
    }

    if (n1->ref != n2->ref ||
        n1->deleted_in_tx != n2->deleted_in_tx ||
        n1->modified_in_tx != n2->modified_in_tx ||
        !compare_perms(n1->perms, n2->perms) ||
        !compare_content(n1->content, n2->content) ||
        nr_children1 != nr_children2) {
        cw->compare_ok = false;
        printf("Compare failure on '%s'\n", cw->path);
    }

    if (nr_children1) {
        XsNode *oldparent = cw->parent_2;
        cw->parent_2 = n2;
        g_hash_table_foreach(n1->children, compare_child, cw);

        cw->parent_2 = oldparent;
    }
}

static void compare_child(gpointer key, gpointer val, gpointer opaque)
{
    struct compare_walk *cw = opaque;
    char *childname = key;
    XsNode *child1 = val;
    XsNode *child2 = g_hash_table_lookup(cw->parent_2->children, childname);
    int pathlen = strlen(cw->path);

    if (!child2) {
        cw->compare_ok = false;
        printf("Child '%s' does not exist under '%s'\n", childname, cw->path);
        return;
    }

    strncat(cw->path, "/", sizeof(cw->path) - 1);
    strncat(cw->path, childname, sizeof(cw->path) - 1);

    compare_nodes(cw, child1, child2);
    cw->path[pathlen] = '\0';
}

static bool compare_trees(XsNode *n1, XsNode *n2)
{
    struct compare_walk cw;

    cw.path[0] = '\0';
    cw.parent_2 = n2;
    cw.compare_ok = true;

    if (!n1 || !n2) {
        return false;
    }

    compare_nodes(&cw, n1, n2);
    return cw.compare_ok;
}

static void compare_tx(gpointer key, gpointer val, gpointer opaque)
{
    XenstoreImplState *s2 = opaque;
    XsTransaction *t1 = val, *t2;
    unsigned int tx_id = GPOINTER_TO_INT(key);

    t2 = g_hash_table_lookup(s2->transactions, key);
    g_assert(t2);

    g_assert(t1->tx_id == tx_id);
    g_assert(t2->tx_id == tx_id);
    g_assert(t1->base_tx == t2->base_tx);
    g_assert(t1->dom_id == t2->dom_id);
    if (!compare_trees(t1->root, t2->root)) {
        printf("Comparison failure in TX %u after serdes:\n", tx_id);
        dump_ref("Original", t1->root, 0);
        dump_ref("Deserialised", t2->root, 0);
        g_assert(0);
    }
    g_assert(t1->nr_nodes == t2->nr_nodes);
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

static void check_serdes(XenstoreImplState *s)
{
    XenstoreImplState *s2 = xs_impl_create(DOMID_GUEST);
    GByteArray *bytes = xs_impl_serialize(s);
    int nr_transactions1, nr_transactions2;
    int ret;

    ret = xs_impl_deserialize(s2, bytes, DOMID_GUEST, watch_cb, NULL);
    g_assert(!ret);

    g_byte_array_unref(bytes);

    g_assert(s->last_tx == s2->last_tx);
    g_assert(s->root_tx == s2->root_tx);

    if (!compare_trees(s->root, s2->root)) {
        printf("Comparison failure in main tree after serdes:\n");
        dump_ref("Original", s->root, 0);
        dump_ref("Deserialised", s2->root, 0);
        g_assert(0);
    }

    nr_transactions1 = g_hash_table_size(s->transactions);
    nr_transactions2 = g_hash_table_size(s2->transactions);
    g_assert(nr_transactions1 == nr_transactions2);

    g_hash_table_foreach(s->transactions, compare_tx, s2);

    g_assert(s->nr_domu_watches == s2->nr_domu_watches);
    g_assert(s->nr_domu_transactions == s2->nr_domu_transactions);
    g_assert(s->nr_nodes == s2->nr_nodes);
    xs_impl_delete(s2, false);
}

static XenstoreImplState *setup(void)
{
   XenstoreImplState *s = xs_impl_create(DOMID_GUEST);
   char *abspath;
   GList *perms;
   int err;

   abspath = g_strdup_printf("/local/domain/%u", DOMID_GUEST);

   err = write_str(s, DOMID_QEMU, XBT_NULL, abspath, "");
   g_assert(!err);
   g_assert(s->nr_nodes == 4);

   perms = g_list_append(NULL, g_strdup_printf("n%u", DOMID_QEMU));
   perms = g_list_append(perms, g_strdup_printf("r%u", DOMID_GUEST));

   err = xs_impl_set_perms(s, DOMID_QEMU, XBT_NULL, abspath, perms);
   g_assert(!err);

   g_list_free_full(perms, g_free);
   g_free(abspath);

   abspath = g_strdup_printf("/local/domain/%u/some", DOMID_GUEST);

   err = write_str(s, DOMID_QEMU, XBT_NULL, abspath, "");
   g_assert(!err);
   g_assert(s->nr_nodes == 5);

   perms = g_list_append(NULL, g_strdup_printf("n%u", DOMID_GUEST));

   err = xs_impl_set_perms(s, DOMID_QEMU, XBT_NULL, abspath, perms);
   g_assert(!err);

   g_list_free_full(perms, g_free);
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

    /* Write somewhere we aren't allowed, in COW mode */
    err = write_str(s, DOMID_GUEST, XBT_NULL, "/local/domain/badplace",
                    "moredata");
    g_assert(err == EACCES);
    g_assert(s->nr_nodes == 7);

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

    /* Write somewhere we aren't allowed */
    err = write_str(s, DOMID_GUEST, XBT_NULL, "/local/domain/badplace",
                    "moredata");
    g_assert(err == EACCES);

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
    xs_impl_delete(s, true);
}


static void do_test_xs_node_tx(bool fail, bool commit)
{
    XenstoreImplState *s = setup();
    GString *watches = g_string_new(NULL);
    GByteArray *data = g_byte_array_new();
    unsigned int tx_id = XBT_NULL;
    int err;

    g_assert(s);

    /* Set a watch */
    err = xs_impl_watch(s, DOMID_GUEST, "some", "watch",
                        watch_cb, watches);
    g_assert(!err);
    g_assert(watches->len == strlen("somewatch"));
    g_assert(!strcmp(watches->str, "somewatch"));
    g_string_truncate(watches, 0);

    /* Write something */
    err = write_str(s, DOMID_GUEST, XBT_NULL, "some/relative/path",
                    "something");
    g_assert(s->nr_nodes == 7);
    g_assert(!err);
    g_assert(!strcmp(watches->str,
                     "some/relative/pathwatch"));
    g_string_truncate(watches, 0);

    /* Create a transaction */
    err = xs_impl_transaction_start(s, DOMID_GUEST, &tx_id);
    g_assert(!err);

    if (fail) {
        /* Write something else in the root */
        err = write_str(s, DOMID_GUEST, XBT_NULL, "some/relative/path",
                        "another thing");
        g_assert(!err);
        g_assert(s->nr_nodes == 7);
        g_assert(!strcmp(watches->str,
                         "some/relative/pathwatch"));
        g_string_truncate(watches, 0);
    }

    g_assert(!watches->len);

    /* Perform a write in the transaction */
    err = write_str(s, DOMID_GUEST, tx_id, "some/relative/path",
                    "something else");
    g_assert(!err);
    g_assert(s->nr_nodes == 7);
    g_assert(!watches->len);

    err = xs_impl_read(s, DOMID_GUEST, XBT_NULL, "some/relative/path", data);
    g_assert(!err);
    if (fail) {
        g_assert(data->len == strlen("another thing"));
        g_assert(!memcmp(data->data, "another thing", data->len));
    } else {
        g_assert(data->len == strlen("something"));
        g_assert(!memcmp(data->data, "something", data->len));
    }
    g_byte_array_set_size(data, 0);

    err = xs_impl_read(s, DOMID_GUEST, tx_id, "some/relative/path", data);
    g_assert(!err);
    g_assert(data->len == strlen("something else"));
    g_assert(!memcmp(data->data, "something else", data->len));
    g_byte_array_set_size(data, 0);

    check_serdes(s);

    /* Attempt to commit the transaction */
    err = xs_impl_transaction_end(s, DOMID_GUEST, tx_id, commit);
    if (commit && fail) {
        g_assert(err == EAGAIN);
    } else {
        g_assert(!err);
    }
    if (commit && !fail) {
        g_assert(!strcmp(watches->str,
                         "some/relative/pathwatch"));
        g_string_truncate(watches, 0);
    } else {
       g_assert(!watches->len);
    }
    g_assert(s->nr_nodes == 7);

    check_serdes(s);

    err = xs_impl_unwatch(s, DOMID_GUEST, "some", "watch",
                        watch_cb, watches);
    g_assert(!err);

    err = xs_impl_read(s, DOMID_GUEST, XBT_NULL, "some/relative/path", data);
    g_assert(!err);
    if (fail) {
        g_assert(data->len == strlen("another thing"));
        g_assert(!memcmp(data->data, "another thing", data->len));
    } else if (commit) {
        g_assert(data->len == strlen("something else"));
        g_assert(!memcmp(data->data, "something else", data->len));
    } else {
        g_assert(data->len == strlen("something"));
        g_assert(!memcmp(data->data, "something", data->len));
    }
    g_byte_array_unref(data);
    g_string_free(watches, true);
    xs_impl_delete(s, true);
}

static void test_xs_node_tx_fail(void)
{
    do_test_xs_node_tx(true, true);
}

static void test_xs_node_tx_abort(void)
{
    do_test_xs_node_tx(false, false);
    do_test_xs_node_tx(true, false);
}
static void test_xs_node_tx_succeed(void)
{
    do_test_xs_node_tx(false, true);
}

static void test_xs_node_tx_rm(void)
{
    XenstoreImplState *s = setup();
    GString *watches = g_string_new(NULL);
    GByteArray *data = g_byte_array_new();
    unsigned int tx_id = XBT_NULL;
    int err;

    g_assert(s);

    /* Set a watch */
    err = xs_impl_watch(s, DOMID_GUEST, "some", "watch",
                        watch_cb, watches);
    g_assert(!err);
    g_assert(watches->len == strlen("somewatch"));
    g_assert(!strcmp(watches->str, "somewatch"));
    g_string_truncate(watches, 0);

    /* Write something */
    err = write_str(s, DOMID_GUEST, XBT_NULL, "some/deep/dark/relative/path",
                    "something");
    g_assert(!err);
    g_assert(s->nr_nodes == 9);
    g_assert(!strcmp(watches->str,
                     "some/deep/dark/relative/pathwatch"));
    g_string_truncate(watches, 0);

    /* Create a transaction */
    err = xs_impl_transaction_start(s, DOMID_GUEST, &tx_id);
    g_assert(!err);

    /* Delete the tree in the transaction */
    err = xs_impl_rm(s, DOMID_GUEST, tx_id, "some/deep/dark");
    g_assert(!err);
    g_assert(s->nr_nodes == 9);
    g_assert(!watches->len);

    err = xs_impl_read(s, DOMID_GUEST, XBT_NULL, "some/deep/dark/relative/path",
                       data);
    g_assert(!err);
    g_assert(data->len == strlen("something"));
    g_assert(!memcmp(data->data, "something", data->len));
    g_byte_array_set_size(data, 0);

    check_serdes(s);

    /* Commit the transaction */
    err = xs_impl_transaction_end(s, DOMID_GUEST, tx_id, true);
    g_assert(!err);
    g_assert(s->nr_nodes == 6);

    g_assert(!strcmp(watches->str, "some/deep/darkwatch"));
    g_string_truncate(watches, 0);

    /* Now the node is gone */
    err = xs_impl_read(s, DOMID_GUEST, XBT_NULL, "some/deep/dark/relative/path",
                       data);
    g_assert(err == ENOENT);
    g_byte_array_unref(data);

    err = xs_impl_unwatch(s, DOMID_GUEST, "some", "watch",
                        watch_cb, watches);
    g_assert(!err);

    g_string_free(watches, true);
    xs_impl_delete(s, true);
}

static void test_xs_node_tx_resurrect(void)
{
    XenstoreImplState *s = setup();
    GString *watches = g_string_new(NULL);
    GByteArray *data = g_byte_array_new();
    unsigned int tx_id = XBT_NULL;
    int err;

    g_assert(s);

    /* Write something */
    err = write_str(s, DOMID_GUEST, XBT_NULL, "some/deep/dark/relative/path",
                    "something");
    g_assert(!err);
    g_assert(s->nr_nodes == 9);

    /* Another node to remain shared */
    err = write_str(s, DOMID_GUEST, XBT_NULL, "some/place/safe", "keepme");
    g_assert(!err);
    g_assert(s->nr_nodes == 11);

    /* This node will be wiped and resurrected */
    err = write_str(s, DOMID_GUEST, XBT_NULL, "some/deep/dark",
                    "foo");
    g_assert(!err);
    g_assert(s->nr_nodes == 11);

    /* Set a watch */
    err = xs_impl_watch(s, DOMID_GUEST, "some", "watch",
                        watch_cb, watches);
    g_assert(!err);
    g_assert(watches->len == strlen("somewatch"));
    g_assert(!strcmp(watches->str, "somewatch"));
    g_string_truncate(watches, 0);

    /* Create a transaction */
    err = xs_impl_transaction_start(s, DOMID_GUEST, &tx_id);
    g_assert(!err);

    /* Delete the tree in the transaction */
    err = xs_impl_rm(s, DOMID_GUEST, tx_id, "some/deep");
    g_assert(!err);
    g_assert(s->nr_nodes == 11);
    g_assert(!watches->len);

    /* Resurrect part of it */
    err = write_str(s, DOMID_GUEST, tx_id, "some/deep/dark/different/path",
                    "something");
    g_assert(!err);
    g_assert(s->nr_nodes == 11);

    check_serdes(s);

    /* Commit the transaction */
    err = xs_impl_transaction_end(s, DOMID_GUEST, tx_id, true);
    g_assert(!err);
    g_assert(s->nr_nodes == 11);

    check_serdes(s);

    /* lost data */
    g_assert(strstr(watches->str, "some/deep/dark/different/pathwatch"));
    /* topmost deleted */
    g_assert(strstr(watches->str, "some/deep/dark/relativewatch"));
    /* lost data */
    g_assert(strstr(watches->str, "some/deep/darkwatch"));

    g_string_truncate(watches, 0);

    /* Now the node is gone */
    err = xs_impl_read(s, DOMID_GUEST, XBT_NULL, "some/deep/dark/relative/path",
                       data);
    g_assert(err == ENOENT);
    g_byte_array_unref(data);

    check_serdes(s);

    err = xs_impl_unwatch(s, DOMID_GUEST, "some", "watch",
                        watch_cb, watches);
    g_assert(!err);

    g_string_free(watches, true);
    xs_impl_delete(s, true);
}

static void test_xs_node_tx_resurrect2(void)
{
    XenstoreImplState *s = setup();
    GString *watches = g_string_new(NULL);
    GByteArray *data = g_byte_array_new();
    unsigned int tx_id = XBT_NULL;
    int err;

    g_assert(s);

    /* Write something */
    err = write_str(s, DOMID_GUEST, XBT_NULL, "some/deep/dark/relative/path",
                    "something");
    g_assert(!err);
    g_assert(s->nr_nodes == 9);

    /* Another node to remain shared */
    err = write_str(s, DOMID_GUEST, XBT_NULL, "some/place/safe", "keepme");
    g_assert(!err);
    g_assert(s->nr_nodes == 11);

    /* This node will be wiped and resurrected */
    err = write_str(s, DOMID_GUEST, XBT_NULL, "some/deep/dark",
                    "foo");
    g_assert(!err);
    g_assert(s->nr_nodes == 11);

    /* Set a watch */
    err = xs_impl_watch(s, DOMID_GUEST, "some", "watch",
                        watch_cb, watches);
    g_assert(!err);
    g_assert(watches->len == strlen("somewatch"));
    g_assert(!strcmp(watches->str, "somewatch"));
    g_string_truncate(watches, 0);

    /* Create a transaction */
    err = xs_impl_transaction_start(s, DOMID_GUEST, &tx_id);
    g_assert(!err);

    /* Delete the tree in the transaction */
    err = xs_impl_rm(s, DOMID_GUEST, tx_id, "some/deep");
    g_assert(!err);
    g_assert(s->nr_nodes == 11);
    g_assert(!watches->len);

    /* Resurrect part of it */
    err = write_str(s, DOMID_GUEST, tx_id, "some/deep/dark/relative/path",
                    "something");
    g_assert(!err);
    g_assert(s->nr_nodes == 11);

    check_serdes(s);

    /* Commit the transaction */
    err = xs_impl_transaction_end(s, DOMID_GUEST, tx_id, true);
    g_assert(!err);
    g_assert(s->nr_nodes == 11);

    check_serdes(s);

    /* lost data */
    g_assert(strstr(watches->str, "some/deep/dark/relative/pathwatch"));
    /* lost data */
    g_assert(strstr(watches->str, "some/deep/darkwatch"));

    g_string_truncate(watches, 0);

    /* Now the node is gone */
    err = xs_impl_read(s, DOMID_GUEST, XBT_NULL, "some/deep/dark/relative/path",
                       data);
    g_assert(!err);
    g_assert(data->len == strlen("something"));
    g_assert(!memcmp(data->data, "something", data->len));

    g_byte_array_unref(data);

    check_serdes(s);

    err = xs_impl_unwatch(s, DOMID_GUEST, "some", "watch",
                        watch_cb, watches);
    g_assert(!err);

    g_string_free(watches, true);
    xs_impl_delete(s, true);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    module_call_init(MODULE_INIT_QOM);

    g_test_add_func("/xs_node/simple", test_xs_node_simple);
    g_test_add_func("/xs_node/tx_abort", test_xs_node_tx_abort);
    g_test_add_func("/xs_node/tx_fail", test_xs_node_tx_fail);
    g_test_add_func("/xs_node/tx_succeed", test_xs_node_tx_succeed);
    g_test_add_func("/xs_node/tx_rm", test_xs_node_tx_rm);
    g_test_add_func("/xs_node/tx_resurrect", test_xs_node_tx_resurrect);
    g_test_add_func("/xs_node/tx_resurrect2", test_xs_node_tx_resurrect2);

    return g_test_run();
}
