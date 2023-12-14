/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Tests for QTree.
 * Original source: glib
 *   https://gitlab.gnome.org/GNOME/glib/-/blob/main/glib/tests/tree.c
 *   LGPL license.
 *   Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 */

#include "qemu/osdep.h"
#include "qemu/qtree.h"

static gint my_compare(gconstpointer a, gconstpointer b)
{
    const char *cha = a;
    const char *chb = b;

    return *cha - *chb;
}

static gint my_compare_with_data(gconstpointer a,
                                 gconstpointer b,
                                 gpointer user_data)
{
    const char *cha = a;
    const char *chb = b;

    /* just check that we got the right data */
    g_assert(GPOINTER_TO_INT(user_data) == 123);

    return *cha - *chb;
}

static gint my_search(gconstpointer a, gconstpointer b)
{
    return my_compare(b, a);
}

static gpointer destroyed_key;
static gpointer destroyed_value;
static guint destroyed_key_count;
static guint destroyed_value_count;

static void my_key_destroy(gpointer key)
{
    destroyed_key = key;
    destroyed_key_count++;
}

static void my_value_destroy(gpointer value)
{
    destroyed_value = value;
    destroyed_value_count++;
}

static gint my_traverse(gpointer key, gpointer value, gpointer data)
{
    char *ch = key;

    g_assert((*ch) > 0);

    if (*ch == 'd') {
        return TRUE;
    }

    return FALSE;
}

char chars[] =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz";

char chars2[] =
    "0123456789"
    "abcdefghijklmnopqrstuvwxyz";

static gint check_order(gpointer key, gpointer value, gpointer data)
{
    char **p = data;
    char *ch = key;

    g_assert(**p == *ch);

    (*p)++;

    return FALSE;
}

static void test_tree_search(void)
{
    gint i;
    QTree *tree;
    gboolean removed;
    gchar c;
    gchar *p, *d;

    tree = q_tree_new_with_data(my_compare_with_data, GINT_TO_POINTER(123));

    for (i = 0; chars[i]; i++) {
        q_tree_insert(tree, &chars[i], &chars[i]);
    }

    q_tree_foreach(tree, my_traverse, NULL);

    g_assert(q_tree_nnodes(tree) == strlen(chars));
    g_assert(q_tree_height(tree) == 6);

    p = chars;
    q_tree_foreach(tree, check_order, &p);

    for (i = 0; i < 26; i++) {
        removed = q_tree_remove(tree, &chars[i + 10]);
        g_assert(removed);
    }

    c = '\0';
    removed = q_tree_remove(tree, &c);
    g_assert(!removed);

    q_tree_foreach(tree, my_traverse, NULL);

    g_assert(q_tree_nnodes(tree) == strlen(chars2));
    g_assert(q_tree_height(tree) == 6);

    p = chars2;
    q_tree_foreach(tree, check_order, &p);

    for (i = 25; i >= 0; i--) {
        q_tree_insert(tree, &chars[i + 10], &chars[i + 10]);
    }

    p = chars;
    q_tree_foreach(tree, check_order, &p);

    c = '0';
    p = q_tree_lookup(tree, &c);
    g_assert(p && *p == c);
    g_assert(q_tree_lookup_extended(tree, &c, (gpointer *)&d, (gpointer *)&p));
    g_assert(c == *d && c == *p);

    c = 'A';
    p = q_tree_lookup(tree, &c);
    g_assert(p && *p == c);

    c = 'a';
    p = q_tree_lookup(tree, &c);
    g_assert(p && *p == c);

    c = 'z';
    p = q_tree_lookup(tree, &c);
    g_assert(p && *p == c);

    c = '!';
    p = q_tree_lookup(tree, &c);
    g_assert(p == NULL);

    c = '=';
    p = q_tree_lookup(tree, &c);
    g_assert(p == NULL);

    c = '|';
    p = q_tree_lookup(tree, &c);
    g_assert(p == NULL);

    c = '0';
    p = q_tree_search(tree, my_search, &c);
    g_assert(p && *p == c);

    c = 'A';
    p = q_tree_search(tree, my_search, &c);
    g_assert(p && *p == c);

    c = 'a';
    p = q_tree_search(tree, my_search, &c);
    g_assert(p && *p == c);

    c = 'z';
    p = q_tree_search(tree, my_search, &c);
    g_assert(p && *p == c);

    c = '!';
    p = q_tree_search(tree, my_search, &c);
    g_assert(p == NULL);

    c = '=';
    p = q_tree_search(tree, my_search, &c);
    g_assert(p == NULL);

    c = '|';
    p = q_tree_search(tree, my_search, &c);
    g_assert(p == NULL);

    q_tree_destroy(tree);
}

static void test_tree_remove(void)
{
    QTree *tree;
    char c, d;
    gint i;
    gboolean removed;

    tree = q_tree_new_full((GCompareDataFunc)my_compare, NULL,
                           my_key_destroy,
                           my_value_destroy);

    for (i = 0; chars[i]; i++) {
        q_tree_insert(tree, &chars[i], &chars[i]);
    }

    c = '0';
    q_tree_insert(tree, &c, &c);
    g_assert(destroyed_key == &c);
    g_assert(destroyed_value == &chars[0]);
    destroyed_key = NULL;
    destroyed_value = NULL;

    d = '1';
    q_tree_replace(tree, &d, &d);
    g_assert(destroyed_key == &chars[1]);
    g_assert(destroyed_value == &chars[1]);
    destroyed_key = NULL;
    destroyed_value = NULL;

    c = '2';
    removed = q_tree_remove(tree, &c);
    g_assert(removed);
    g_assert(destroyed_key == &chars[2]);
    g_assert(destroyed_value == &chars[2]);
    destroyed_key = NULL;
    destroyed_value = NULL;

    c = '3';
    removed = q_tree_steal(tree, &c);
    g_assert(removed);
    g_assert(destroyed_key == NULL);
    g_assert(destroyed_value == NULL);

    const gchar *remove = "omkjigfedba";
    for (i = 0; remove[i]; i++) {
        removed = q_tree_remove(tree, &remove[i]);
        g_assert(removed);
    }

    q_tree_destroy(tree);
}

static void test_tree_destroy(void)
{
    QTree *tree;
    gint i;

    tree = q_tree_new(my_compare);

    for (i = 0; chars[i]; i++) {
        q_tree_insert(tree, &chars[i], &chars[i]);
    }

    g_assert(q_tree_nnodes(tree) == strlen(chars));

    g_test_message("nnodes: %d", q_tree_nnodes(tree));
    q_tree_ref(tree);
    q_tree_destroy(tree);

    g_test_message("nnodes: %d", q_tree_nnodes(tree));
    g_assert(q_tree_nnodes(tree) == 0);

    q_tree_unref(tree);
}

static void test_tree_insert(void)
{
    QTree *tree;
    gchar *p;
    gint i;
    gchar *scrambled;

    tree = q_tree_new(my_compare);

    for (i = 0; chars[i]; i++) {
        q_tree_insert(tree, &chars[i], &chars[i]);
    }
    p = chars;
    q_tree_foreach(tree, check_order, &p);

    q_tree_unref(tree);
    tree = q_tree_new(my_compare);

    for (i = strlen(chars) - 1; i >= 0; i--) {
        q_tree_insert(tree, &chars[i], &chars[i]);
    }
    p = chars;
    q_tree_foreach(tree, check_order, &p);

    q_tree_unref(tree);
    tree = q_tree_new(my_compare);

    scrambled = g_strdup(chars);

    for (i = 0; i < 30; i++) {
        gchar tmp;
        gint a, b;

        a = g_random_int_range(0, strlen(scrambled));
        b = g_random_int_range(0, strlen(scrambled));
        tmp = scrambled[a];
        scrambled[a] = scrambled[b];
        scrambled[b] = tmp;
    }

    for (i = 0; scrambled[i]; i++) {
        q_tree_insert(tree, &scrambled[i], &scrambled[i]);
    }
    p = chars;
    q_tree_foreach(tree, check_order, &p);

    g_free(scrambled);
    q_tree_unref(tree);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/qtree/search", test_tree_search);
    g_test_add_func("/qtree/remove", test_tree_remove);
    g_test_add_func("/qtree/destroy", test_tree_destroy);
    g_test_add_func("/qtree/insert", test_tree_insert);

    return g_test_run();
}
