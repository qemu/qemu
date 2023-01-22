/*
 * QEMU Xen emulation: The actual implementation of XenStore
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>, Paul Durrant <paul@xen.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qom/object.h"

#include "xen_xenstore.h"
#include "xenstore_impl.h"

#include "hw/xen/interface/io/xs_wire.h"

#define XS_MAX_WATCHES          128
#define XS_MAX_DOMAIN_NODES     1000
#define XS_MAX_NODE_SIZE        2048
#define XS_MAX_TRANSACTIONS     10
#define XS_MAX_PERMS_PER_NODE   5

#define XS_VALID_CHARS "abcdefghijklmnopqrstuvwxyz" \
                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ" \
                       "0123456789-/_"

typedef struct XsNode {
    uint32_t ref;
    GByteArray *content;
    GHashTable *children;
    uint64_t gencnt;
    bool deleted_in_tx;
    bool modified_in_tx;
#ifdef XS_NODE_UNIT_TEST
    gchar *name; /* debug only */
#endif
} XsNode;

typedef struct XsWatch {
    struct XsWatch *next;
    xs_impl_watch_fn *cb;
    void *cb_opaque;
    char *token;
    unsigned int dom_id;
    int rel_prefix;
} XsWatch;

typedef struct XsTransaction {
    XsNode *root;
    unsigned int nr_nodes;
    unsigned int base_tx;
    unsigned int tx_id;
    unsigned int dom_id;
} XsTransaction;

struct XenstoreImplState {
    XsNode *root;
    unsigned int nr_nodes;
    GHashTable *watches;
    unsigned int nr_domu_watches;
    GHashTable *transactions;
    unsigned int nr_domu_transactions;
    unsigned int root_tx;
    unsigned int last_tx;
};


static void nobble_tx(gpointer key, gpointer value, gpointer user_data)
{
    unsigned int *new_tx_id = user_data;
    XsTransaction *tx = value;

    if (tx->base_tx == *new_tx_id) {
        /* Transactions based on XBT_NULL will always fail */
        tx->base_tx = XBT_NULL;
    }
}

static inline unsigned int next_tx(struct XenstoreImplState *s)
{
    unsigned int tx_id;

    /* Find the next TX id which isn't either XBT_NULL or in use. */
    do {
        tx_id = ++s->last_tx;
    } while (tx_id == XBT_NULL || tx_id == s->root_tx ||
             g_hash_table_lookup(s->transactions, GINT_TO_POINTER(tx_id)));

    /*
     * It is vanishingly unlikely, but ensure that no outstanding transaction
     * is based on the (previous incarnation of the) newly-allocated TX id.
     */
    g_hash_table_foreach(s->transactions, nobble_tx, &tx_id);

    return tx_id;
}

static inline XsNode *xs_node_new(void)
{
    XsNode *n = g_new0(XsNode, 1);
    n->ref = 1;

#ifdef XS_NODE_UNIT_TEST
    nr_xs_nodes++;
    xs_node_list = g_list_prepend(xs_node_list, n);
#endif
    return n;
}

static inline XsNode *xs_node_ref(XsNode *n)
{
    /* With just 10 transactions, it can never get anywhere near this. */
    g_assert(n->ref < INT_MAX);

    g_assert(n->ref);
    n->ref++;
    return n;
}

static inline void xs_node_unref(XsNode *n)
{
    if (!n) {
        return;
    }
    g_assert(n->ref);
    if (--n->ref) {
        return;
    }

    if (n->content) {
        g_byte_array_unref(n->content);
    }
    if (n->children) {
        g_hash_table_unref(n->children);
    }
#ifdef XS_NODE_UNIT_TEST
    g_free(n->name);
    nr_xs_nodes--;
    xs_node_list = g_list_remove(xs_node_list, n);
#endif
    g_free(n);
}

/* For copying from one hash table to another using g_hash_table_foreach() */
static void do_insert(gpointer key, gpointer value, gpointer user_data)
{
    g_hash_table_insert(user_data, g_strdup(key), xs_node_ref(value));
}

static XsNode *xs_node_copy(XsNode *old)
{
    XsNode *n = xs_node_new();

    n->gencnt = old->gencnt;

#ifdef XS_NODE_UNIT_TEST
    if (n->name) {
        n->name = g_strdup(old->name);
    }
#endif

    if (old->children) {
        n->children = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                            (GDestroyNotify)xs_node_unref);
        g_hash_table_foreach(old->children, do_insert, n->children);
    }
    if (old && old->content) {
        n->content = g_byte_array_ref(old->content);
    }
    return n;
}

/* Returns true if it made a change to the hash table */
static bool xs_node_add_child(XsNode *n, const char *path_elem, XsNode *child)
{
    assert(!strchr(path_elem, '/'));

    if (!child) {
        assert(n->children);
        return g_hash_table_remove(n->children, path_elem);
    }

#ifdef XS_NODE_UNIT_TEST
    g_free(child->name);
    child->name = g_strdup(path_elem);
#endif
    if (!n->children) {
        n->children = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                            (GDestroyNotify)xs_node_unref);
    }

    /*
     * The documentation for g_hash_table_insert() says that it "returns a
     * boolean value to indicate whether the newly added value was already
     * in the hash table or not."
     *
     * It could perhaps be clearer that returning TRUE means it wasn't,
     */
    return g_hash_table_insert(n->children, g_strdup(path_elem), child);
}

struct walk_op {
    struct XenstoreImplState *s;
    char path[XENSTORE_ABS_PATH_MAX + 2]; /* Two NUL terminators */
    int (*op_fn)(XsNode **n, struct walk_op *op);
    void *op_opaque;
    void *op_opaque2;

    GList *watches;
    unsigned int dom_id;
    unsigned int tx_id;

    /* The number of nodes which will exist in the tree if this op succeeds. */
    unsigned int new_nr_nodes;

    /*
     * This is maintained on the way *down* the walk to indicate
     * whether nodes can be modified in place or whether COW is
     * required. It starts off being true, as we're always going to
     * replace the root node. If we walk into a shared subtree it
     * becomes false. If we start *creating* new nodes for a write,
     * it becomes true again.
     *
     * Do not use it on the way back up.
     */
    bool inplace;
    bool mutating;
    bool create_dirs;
    bool in_transaction;

    /* Tracking during recursion so we know which is first. */
    bool deleted_in_tx;
};

static void fire_watches(struct walk_op *op, bool parents)
{
    GList *l = NULL;
    XsWatch *w;

    if (!op->mutating || op->in_transaction) {
        return;
    }

    if (parents) {
        l = op->watches;
    }

    w = g_hash_table_lookup(op->s->watches, op->path);
    while (w || l) {
        if (!w) {
            /* Fire the parent nodes from 'op' if asked to */
            w = l->data;
            l = l->next;
            continue;
        }

        assert(strlen(op->path) > w->rel_prefix);
        w->cb(w->cb_opaque, op->path + w->rel_prefix, w->token);

        w = w->next;
    }
}

static int xs_node_add_content(XsNode **n, struct walk_op *op)
{
    GByteArray *data = op->op_opaque;

    if (op->dom_id) {
        /*
         * The real XenStored includes permissions and names of child nodes
         * in the calculated datasize but life's too short. For a single
         * tenant internal XenStore, we don't have to be quite as pedantic.
         */
        if (data->len > XS_MAX_NODE_SIZE) {
            return E2BIG;
        }
    }
    /* We *are* the node to be written. Either this or a copy. */
    if (!op->inplace) {
        XsNode *old = *n;
        *n = xs_node_copy(old);
        xs_node_unref(old);
    }

    if ((*n)->content) {
        g_byte_array_unref((*n)->content);
    }
    (*n)->content = g_byte_array_ref(data);
    if (op->tx_id != XBT_NULL) {
        (*n)->modified_in_tx = true;
    }
    return 0;
}

static int xs_node_get_content(XsNode **n, struct walk_op *op)
{
    GByteArray *data = op->op_opaque;
    GByteArray *node_data;

    assert(op->inplace);
    assert(*n);

    node_data = (*n)->content;
    if (node_data) {
        g_byte_array_append(data, node_data->data, node_data->len);
    }

    return 0;
}

static int node_rm_recurse(gpointer key, gpointer value, gpointer user_data)
{
    struct walk_op *op = user_data;
    int path_len = strlen(op->path);
    int key_len = strlen(key);
    XsNode *n = value;
    bool this_inplace = op->inplace;

    if (n->ref != 1) {
        op->inplace = 0;
    }

    assert(key_len + path_len + 2 <= sizeof(op->path));
    op->path[path_len] = '/';
    memcpy(op->path + path_len + 1, key, key_len + 1);

    if (n->children) {
        g_hash_table_foreach_remove(n->children, node_rm_recurse, op);
    }
    op->new_nr_nodes--;

    /*
     * Fire watches on *this* node but not the parents because they are
     * going to be deleted too, so the watch will fire for them anyway.
     */
    fire_watches(op, false);
    op->path[path_len] = '\0';

    /*
     * Actually deleting the child here is just an optimisation; if we
     * don't then the final unref on the topmost victim will just have
     * to cascade down again repeating all the g_hash_table_foreach()
     * calls.
     */
    return this_inplace;
}

static XsNode *xs_node_copy_deleted(XsNode *old, struct walk_op *op);
static void copy_deleted_recurse(gpointer key, gpointer value,
                                 gpointer user_data)
{
    struct walk_op *op = user_data;
    GHashTable *siblings = op->op_opaque2;
    XsNode *n = xs_node_copy_deleted(value, op);

    /*
     * Reinsert the deleted_in_tx copy of the node into the parent's
     * 'children' hash table. Having stashed it from op->op_opaque2
     * before the recursive call to xs_node_copy_deleted() scribbled
     * over it.
     */
    g_hash_table_insert(siblings, g_strdup(key), n);
}

static XsNode *xs_node_copy_deleted(XsNode *old, struct walk_op *op)
{
    XsNode *n = xs_node_new();

    n->gencnt = old->gencnt;

#ifdef XS_NODE_UNIT_TEST
    if (old->name) {
        n->name = g_strdup(old->name);
    }
#endif

    if (old->children) {
        n->children = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                            (GDestroyNotify)xs_node_unref);
        op->op_opaque2 = n->children;
        g_hash_table_foreach(old->children, copy_deleted_recurse, op);
    }
    n->deleted_in_tx = true;
    /* If it gets resurrected we only fire a watch if it lost its content */
    if (old->content) {
        n->modified_in_tx = true;
    }
    op->new_nr_nodes--;
    return n;
}

static int xs_node_rm(XsNode **n, struct walk_op *op)
{
    bool this_inplace = op->inplace;

    if (op->tx_id != XBT_NULL) {
        /* It's not trivial to do inplace handling for this one */
        XsNode *old = *n;
        *n = xs_node_copy_deleted(old, op);
        xs_node_unref(old);
        return 0;
    }

    /* Fire watches for, and count, nodes in the subtree which get deleted */
    if ((*n)->children) {
        g_hash_table_foreach_remove((*n)->children, node_rm_recurse, op);
    }
    op->new_nr_nodes--;

    if (this_inplace) {
        xs_node_unref(*n);
    }
    *n = NULL;
    return 0;
}

/*
 * Passed a full reference in *n which it may free if it needs to COW.
 *
 * When changing the tree, the op->inplace flag indicates whether this
 * node may be modified in place (i.e. it and all its parents had a
 * refcount of one). If walking down the tree we find a node whose
 * refcount is higher, we must clear op->inplace and COW from there
 * down. Unless we are creating new nodes as scaffolding for a write
 * (which works like 'mkdir -p' does). In which case those newly
 * created nodes can (and must) be modified in place again.
 */
static int xs_node_walk(XsNode **n, struct walk_op *op)
{
    char *child_name = NULL;
    size_t namelen;
    XsNode *old = *n, *child = NULL;
    bool stole_child = false;
    bool this_inplace;
    XsWatch *watch;
    int err;

    namelen = strlen(op->path);
    watch = g_hash_table_lookup(op->s->watches, op->path);

    /* Is there a child, or do we hit the double-NUL termination? */
    if (op->path[namelen + 1]) {
        char *slash;
        child_name = op->path + namelen + 1;
        slash = strchr(child_name, '/');
        if (slash) {
            *slash = '\0';
        }
        op->path[namelen] = '/';
    }

    /* If we walk into a subtree which is shared, we must COW */
    if (op->mutating && old->ref != 1) {
        op->inplace = false;
    }

    if (!child_name) {
        /* This is the actual node on which the operation shall be performed */
        err = op->op_fn(n, op);
        if (!err) {
            fire_watches(op, true);
        }
        goto out;
    }

    /* op->inplace will be further modified during the recursion */
    this_inplace = op->inplace;

    if (old && old->children) {
        child = g_hash_table_lookup(old->children, child_name);
        /* This is a *weak* reference to 'child', owned by the hash table */
    }

    if (child) {
        if (child->deleted_in_tx) {
            assert(child->ref == 1);
            /* Cannot actually set child->deleted_in_tx = false until later */
        }
        xs_node_ref(child);
        /*
         * Now we own it too. But if we can modify inplace, that's going to
         * foil the check and force it to COW. We want to be the *only* owner
         * so that it can be modified in place, so remove it from the hash
         * table in that case. We'll add it (or its replacement) back later.
         */
        if (op->mutating && this_inplace) {
            g_hash_table_remove(old->children, child_name);
            stole_child = true;
        }
    } else if (op->create_dirs) {
        if (op->dom_id && op->new_nr_nodes >= XS_MAX_DOMAIN_NODES) {
            err = ENOSPC;
            goto out;
        }
        op->new_nr_nodes++;
        child = xs_node_new();

        /*
         * If we're creating a new child, we can clearly modify it (and its
         * children) in place from here on down.
         */
        op->inplace = true;
    } else {
        err = ENOENT;
        goto out;
    }

    /*
     * If there's a watch on this node, add it to the list to be fired
     * (with the correct full pathname for the modified node) at the end.
     */
    if (watch) {
        op->watches = g_list_append(op->watches, watch);
    }

    /*
     * Except for the temporary child-stealing as noted, our node has not
     * changed yet. We don't yet know the overall operation will complete.
     */
    err = xs_node_walk(&child, op);

    if (watch) {
        op->watches = g_list_remove(op->watches, watch);
    }

    if (err || !op->mutating) {
        if (stole_child) {
            /* Put it back as it was. */
            g_hash_table_replace(old->children, g_strdup(child_name), child);
        } else {
            xs_node_unref(child);
        }
        goto out;
    }

    /*
     * Now we know the operation has completed successfully and we're on
     * the way back up. Make the change, substituting 'child' in the
     * node at our level.
     */
    if (!this_inplace) {
        *n = xs_node_copy(old);
        xs_node_unref(old);
    }

    /*
     * If we resurrected a deleted_in_tx node, we can mark it as no longer
     * deleted now that we know the overall operation has succeeded.
     */
    if (op->create_dirs && child && child->deleted_in_tx) {
        op->new_nr_nodes++;
        child->deleted_in_tx = false;
    }

    /*
     * The child may be NULL here, for a remove operation. Either way,
     * xs_node_add_child() will do the right thing and return a value
     * indicating whether it changed the parent's hash table or not.
     *
     * We bump the parent gencnt if it adds a child that we *didn't*
     * steal from it in the first place, or if child==NULL and was
     * thus removed (whether we stole it earlier and didn't put it
     * back, or xs_node_add_child() actually removed it now).
     */
    if ((xs_node_add_child(*n, child_name, child) && !stole_child) || !child) {
        (*n)->gencnt++;
    }

 out:
    op->path[namelen] = '\0';
    if (!namelen) {
        assert(!op->watches);
        /*
         * On completing the recursion back up the path walk and reaching the
         * top, assign the new node count if the operation was successful. If
         * the main tree was changed, bump its tx ID so that outstanding
         * transactions correctly fail. But don't bump it every time; only
         * if it makes a difference.
         */
        if (!err && op->mutating) {
            if (!op->in_transaction) {
                if (op->s->root_tx != op->s->last_tx) {
                    op->s->root_tx = next_tx(op->s);
                }
                op->s->nr_nodes = op->new_nr_nodes;
            } else {
                XsTransaction *tx = g_hash_table_lookup(op->s->transactions,
                                                        GINT_TO_POINTER(op->tx_id));
                assert(tx);
                tx->nr_nodes = op->new_nr_nodes;
            }
        }
    }
    return err;
}

static void append_directory_item(gpointer key, gpointer value,
                                  gpointer user_data)
{
    GList **items = user_data;

    *items = g_list_insert_sorted(*items, g_strdup(key), (GCompareFunc)strcmp);
}

/* Populates items with char * names which caller must free. */
static int xs_node_directory(XsNode **n, struct walk_op *op)
{
    GList **items = op->op_opaque;

    assert(op->inplace);
    assert(*n);

    if ((*n)->children) {
        g_hash_table_foreach((*n)->children, append_directory_item, items);
    }

    if (op->op_opaque2) {
        *(uint64_t *)op->op_opaque2 = (*n)->gencnt;
    }

    return 0;
}

static int validate_path(char *outpath, const char *userpath,
                         unsigned int dom_id)
{
    size_t i, pathlen = strlen(userpath);

    if (!pathlen || userpath[pathlen] == '/' || strstr(userpath, "//")) {
        return EINVAL;
    }
    for (i = 0; i < pathlen; i++) {
        if (!strchr(XS_VALID_CHARS, userpath[i])) {
            return EINVAL;
        }
    }
    if (userpath[0] == '/') {
        if (pathlen > XENSTORE_ABS_PATH_MAX) {
            return E2BIG;
        }
        memcpy(outpath, userpath, pathlen + 1);
    } else {
        if (pathlen > XENSTORE_REL_PATH_MAX) {
            return E2BIG;
        }
        snprintf(outpath, XENSTORE_ABS_PATH_MAX, "/local/domain/%u/%s", dom_id,
                 userpath);
    }
    return 0;
}


static int init_walk_op(XenstoreImplState *s, struct walk_op *op,
                        xs_transaction_t tx_id, unsigned int dom_id,
                        const char *path, XsNode ***rootp)
{
    int ret = validate_path(op->path, path, dom_id);
    if (ret) {
        return ret;
    }

    /*
     * We use *two* NUL terminators at the end of the path, as during the walk
     * we will temporarily turn each '/' into a NUL to allow us to use that
     * path element for the lookup.
     */
    op->path[strlen(op->path) + 1] = '\0';
    op->watches = NULL;
    op->path[0] = '\0';
    op->inplace = true;
    op->mutating = false;
    op->create_dirs = false;
    op->in_transaction = false;
    op->dom_id = dom_id;
    op->tx_id = tx_id;
    op->s = s;

    if (tx_id == XBT_NULL) {
        *rootp = &s->root;
        op->new_nr_nodes = s->nr_nodes;
    } else {
        XsTransaction *tx = g_hash_table_lookup(s->transactions,
                                                GINT_TO_POINTER(tx_id));
        if (!tx) {
            return ENOENT;
        }
        *rootp = &tx->root;
        op->new_nr_nodes = tx->nr_nodes;
        op->in_transaction = true;
    }

    return 0;
}

int xs_impl_read(XenstoreImplState *s, unsigned int dom_id,
                 xs_transaction_t tx_id, const char *path, GByteArray *data)
{
    /*
     * The data GByteArray shall exist, and will be freed by caller.
     * Just g_byte_array_append() to it.
     */
    struct walk_op op;
    XsNode **n;
    int ret;

    ret = init_walk_op(s, &op, tx_id, dom_id, path, &n);
    if (ret) {
        return ret;
    }
    op.op_fn = xs_node_get_content;
    op.op_opaque = data;
    return xs_node_walk(n, &op);
}

int xs_impl_write(XenstoreImplState *s, unsigned int dom_id,
                  xs_transaction_t tx_id, const char *path, GByteArray *data)
{
    /*
     * The data GByteArray shall exist, will be freed by caller. You are
     * free to use g_byte_array_steal() and keep the data. Or just ref it.
     */
    struct walk_op op;
    XsNode **n;
    int ret;

    ret = init_walk_op(s, &op, tx_id, dom_id, path, &n);
    if (ret) {
        return ret;
    }
    op.op_fn = xs_node_add_content;
    op.op_opaque = data;
    op.mutating = true;
    op.create_dirs = true;
    return xs_node_walk(n, &op);
}

int xs_impl_directory(XenstoreImplState *s, unsigned int dom_id,
                      xs_transaction_t tx_id, const char *path,
                      uint64_t *gencnt, GList **items)
{
    /*
     * The items are (char *) to be freed by caller. Although it's consumed
     * immediately so if you want to change it to (const char *) and keep
     * them, go ahead and change the caller.
     */
    struct walk_op op;
    XsNode **n;
    int ret;

    ret = init_walk_op(s, &op, tx_id, dom_id, path, &n);
    if (ret) {
        return ret;
    }
    op.op_fn = xs_node_directory;
    op.op_opaque = items;
    op.op_opaque2 = gencnt;
    return xs_node_walk(n, &op);
}

int xs_impl_transaction_start(XenstoreImplState *s, unsigned int dom_id,
                              xs_transaction_t *tx_id)
{
    XsTransaction *tx;

    if (*tx_id != XBT_NULL) {
        return EINVAL;
    }

    if (dom_id && s->nr_domu_transactions >= XS_MAX_TRANSACTIONS) {
        return ENOSPC;
    }

    tx = g_new0(XsTransaction, 1);

    tx->nr_nodes = s->nr_nodes;
    tx->tx_id = next_tx(s);
    tx->base_tx = s->root_tx;
    tx->root = xs_node_ref(s->root);
    tx->dom_id = dom_id;

    g_hash_table_insert(s->transactions, GINT_TO_POINTER(tx->tx_id), tx);
    if (dom_id) {
        s->nr_domu_transactions++;
    }
    *tx_id = tx->tx_id;
    return 0;
}

static gboolean tx_commit_walk(gpointer key, gpointer value,
                               gpointer user_data)
{
    struct walk_op *op = user_data;
    int path_len = strlen(op->path);
    int key_len = strlen(key);
    bool fire_parents = true;
    XsWatch *watch;
    XsNode *n = value;

    if (n->ref != 1) {
        return false;
    }

    if (n->deleted_in_tx) {
        /*
         * We fire watches on our parents if we are the *first* node
         * to be deleted (the topmost one). This matches the behaviour
         * when deleting in the live tree.
         */
        fire_parents = !op->deleted_in_tx;

        /* Only used on the way down so no need to clear it later */
        op->deleted_in_tx = true;
    }

    assert(key_len + path_len + 2 <= sizeof(op->path));
    op->path[path_len] = '/';
    memcpy(op->path + path_len + 1, key, key_len + 1);

    watch = g_hash_table_lookup(op->s->watches, op->path);
    if (watch) {
        op->watches = g_list_append(op->watches, watch);
    }

    if (n->children) {
        g_hash_table_foreach_remove(n->children, tx_commit_walk, op);
    }

    if (watch) {
        op->watches = g_list_remove(op->watches, watch);
    }

    /*
     * Don't fire watches if this node was only copied because a
     * descendent was changed. The modified_in_tx flag indicates the
     * ones which were really changed.
     */
    if (n->modified_in_tx || n->deleted_in_tx) {
        fire_watches(op, fire_parents);
        n->modified_in_tx = false;
    }
    op->path[path_len] = '\0';

    /* Deleted nodes really do get expunged when we commit */
    return n->deleted_in_tx;
}

static int transaction_commit(XenstoreImplState *s, XsTransaction *tx)
{
    struct walk_op op;
    XsNode **n;

    if (s->root_tx != tx->base_tx) {
        return EAGAIN;
    }
    xs_node_unref(s->root);
    s->root = tx->root;
    tx->root = NULL;
    s->root_tx = tx->tx_id;
    s->nr_nodes = tx->nr_nodes;

    init_walk_op(s, &op, XBT_NULL, tx->dom_id, "/", &n);
    op.deleted_in_tx = false;
    op.mutating = true;

    /*
     * Walk the new root and fire watches on any node which has a
     * refcount of one (which is therefore unique to this transaction).
     */
    if (s->root->children) {
        g_hash_table_foreach_remove(s->root->children, tx_commit_walk, &op);
    }

    return 0;
}

int xs_impl_transaction_end(XenstoreImplState *s, unsigned int dom_id,
                            xs_transaction_t tx_id, bool commit)
{
    int ret = 0;
    XsTransaction *tx = g_hash_table_lookup(s->transactions,
                                            GINT_TO_POINTER(tx_id));

    if (!tx || tx->dom_id != dom_id) {
        return ENOENT;
    }

    if (commit) {
        ret = transaction_commit(s, tx);
    }

    g_hash_table_remove(s->transactions, GINT_TO_POINTER(tx_id));
    if (dom_id) {
        assert(s->nr_domu_transactions);
        s->nr_domu_transactions--;
    }
    return ret;
}

int xs_impl_rm(XenstoreImplState *s, unsigned int dom_id,
               xs_transaction_t tx_id, const char *path)
{
    struct walk_op op;
    XsNode **n;
    int ret;

    ret = init_walk_op(s, &op, tx_id, dom_id, path, &n);
    if (ret) {
        return ret;
    }
    op.op_fn = xs_node_rm;
    op.mutating = true;
    return xs_node_walk(n, &op);
}

int xs_impl_get_perms(XenstoreImplState *s, unsigned int dom_id,
                      xs_transaction_t tx_id, const char *path, GList **perms)
{
    /*
     * The perms are (char *) in the <perm-as-string> wire format to be
     * freed by the caller.
     */
    return ENOSYS;
}

int xs_impl_set_perms(XenstoreImplState *s, unsigned int dom_id,
                      xs_transaction_t tx_id, const char *path, GList *perms)
{
    /*
     * The perms are (const char *) in the <perm-as-string> wire format.
     */
    return ENOSYS;
}

int xs_impl_watch(XenstoreImplState *s, unsigned int dom_id, const char *path,
                  const char *token, xs_impl_watch_fn fn, void *opaque)
{
    char abspath[XENSTORE_ABS_PATH_MAX + 1];
    XsWatch *w, *l;
    int ret;

    ret = validate_path(abspath, path, dom_id);
    if (ret) {
        return ret;
    }

    /* Check for duplicates */
    l = w = g_hash_table_lookup(s->watches, abspath);
    while (w) {
        if (!g_strcmp0(token, w->token) &&  opaque == w->cb_opaque &&
            fn == w->cb && dom_id == w->dom_id) {
            return EEXIST;
        }
        w = w->next;
    }

    if (dom_id && s->nr_domu_watches >= XS_MAX_WATCHES) {
        return E2BIG;
    }

    w = g_new0(XsWatch, 1);
    w->token = g_strdup(token);
    w->cb = fn;
    w->cb_opaque = opaque;
    w->dom_id = dom_id;
    w->rel_prefix = strlen(abspath) - strlen(path);

    /* l was looked up above when checking for duplicates */
    if (l) {
        w->next = l->next;
        l->next = w;
    } else {
        g_hash_table_insert(s->watches, g_strdup(abspath), w);
    }
    if (dom_id) {
        s->nr_domu_watches++;
    }

    /* A new watch should fire immediately */
    fn(opaque, path, token);

    return 0;
}

static XsWatch *free_watch(XenstoreImplState *s, XsWatch *w)
{
    XsWatch *next = w->next;

    if (w->dom_id) {
        assert(s->nr_domu_watches);
        s->nr_domu_watches--;
    }

    g_free(w->token);
    g_free(w);

    return next;
}

int xs_impl_unwatch(XenstoreImplState *s, unsigned int dom_id,
                    const char *path, const char *token,
                    xs_impl_watch_fn fn, void *opaque)
{
    char abspath[XENSTORE_ABS_PATH_MAX + 1];
    XsWatch *w, **l;
    int ret;

    ret = validate_path(abspath, path, dom_id);
    if (ret) {
        return ret;
    }

    w = g_hash_table_lookup(s->watches, abspath);
    if (!w) {
        return ENOENT;
    }

    /*
     * The hash table contains the first element of a list of
     * watches. Removing the first element in the list is a
     * special case because we have to update the hash table to
     * point to the next (or remove it if there's nothing left).
     */
    if (!g_strcmp0(token, w->token) && fn == w->cb && opaque == w->cb_opaque &&
        dom_id == w->dom_id) {
        if (w->next) {
            /* Insert the previous 'next' into the hash table */
            g_hash_table_insert(s->watches, g_strdup(abspath), w->next);
        } else {
            /* Nothing left; remove from hash table */
            g_hash_table_remove(s->watches, abspath);
        }
        free_watch(s, w);
        return 0;
    }

    /*
     * We're all done messing with the hash table because the element
     * it points to has survived the cull. Now it's just a simple
     * linked list removal operation.
     */
    for (l = &w->next; *l; l = &w->next) {
        w = *l;

        if (!g_strcmp0(token, w->token) && fn == w->cb &&
            opaque != w->cb_opaque && dom_id == w->dom_id) {
            *l = free_watch(s, w);
            return 0;
        }
    }

    return ENOENT;
}

int xs_impl_reset_watches(XenstoreImplState *s, unsigned int dom_id)
{
    char **watch_paths;
    guint nr_watch_paths;
    guint i;

    watch_paths = (char **)g_hash_table_get_keys_as_array(s->watches,
                                                          &nr_watch_paths);

    for (i = 0; i < nr_watch_paths; i++) {
        XsWatch *w1 = g_hash_table_lookup(s->watches, watch_paths[i]);
        XsWatch *w2, *w, **l;

        /*
         * w1 is the original list. The hash table has this pointer.
         * w2 is the head of our newly-filtered list.
         * w and l are temporary for processing. w is somewhat redundant
         * with *l but makes my eyes bleed less.
         */

        w = w2 = w1;
        l = &w;
        while (w) {
            if (w->dom_id == dom_id) {
                /* If we're freeing the head of the list, bump w2 */
                if (w2 == w) {
                    w2 = w->next;
                }
                *l = free_watch(s, w);
            } else {
                l = &w->next;
            }
            w = *l;
        }
        /*
         * If the head of the list survived the cull, we don't need to
         * touch the hash table and we're done with this path. Else...
         */
        if (w1 != w2) {
            g_hash_table_steal(s->watches, watch_paths[i]);

            /*
             * It was already freed. (Don't worry, this whole thing is
             * single-threaded and nobody saw it in the meantime). And
             * having *stolen* it, we now own the watch_paths[i] string
             * so if we don't give it back to the hash table, we need
             * to free it.
             */
            if (w2) {
                g_hash_table_insert(s->watches, watch_paths[i], w2);
            } else {
                g_free(watch_paths[i]);
            }
        }
    }
    g_free(watch_paths);
    return 0;
}

static void xs_tx_free(void *_tx)
{
    XsTransaction *tx = _tx;
    if (tx->root) {
        xs_node_unref(tx->root);
    }
    g_free(tx);
}

XenstoreImplState *xs_impl_create(void)
{
    XenstoreImplState *s = g_new0(XenstoreImplState, 1);

    s->watches = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    s->transactions = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                            NULL, xs_tx_free);
    s->nr_nodes = 1;
    s->root = xs_node_new();
#ifdef XS_NODE_UNIT_TEST
    s->root->name = g_strdup("/");
#endif

    s->root_tx = s->last_tx = 1;
    return s;
}
