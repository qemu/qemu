/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/qtree.h"
#include "qemu/timer.h"

enum tree_op {
    OP_LOOKUP,
    OP_INSERT,
    OP_REMOVE,
    OP_REMOVE_ALL,
    OP_TRAVERSE,
};

struct benchmark {
    const char * const name;
    enum tree_op op;
    bool fill_on_init;
};

enum impl_type {
    IMPL_GTREE,
    IMPL_QTREE,
};

struct tree_implementation {
    const char * const name;
    enum impl_type type;
};

static const struct benchmark benchmarks[] = {
    {
        .name = "Lookup",
        .op = OP_LOOKUP,
        .fill_on_init = true,
    },
    {
        .name = "Insert",
        .op = OP_INSERT,
        .fill_on_init = false,
    },
    {
        .name = "Remove",
        .op = OP_REMOVE,
        .fill_on_init = true,
    },
    {
        .name = "RemoveAll",
        .op = OP_REMOVE_ALL,
        .fill_on_init = true,
    },
    {
        .name = "Traverse",
        .op = OP_TRAVERSE,
        .fill_on_init = true,
    },
};

static const struct tree_implementation impls[] = {
    {
        .name = "GTree",
        .type = IMPL_GTREE,
    },
    {
        .name = "QTree",
        .type = IMPL_QTREE,
    },
};

static int compare_func(const void *ap, const void *bp)
{
    const size_t *a = ap;
    const size_t *b = bp;

    return *a - *b;
}

static void init_empty_tree_and_keys(enum impl_type impl,
                                     void **ret_tree, size_t **ret_keys,
                                     size_t n_elems)
{
    size_t *keys = g_malloc_n(n_elems, sizeof(*keys));
    for (size_t i = 0; i < n_elems; i++) {
        keys[i] = i;
    }

    void *tree;
    switch (impl) {
    case IMPL_GTREE:
        tree = g_tree_new(compare_func);
        break;
    case IMPL_QTREE:
        tree = q_tree_new(compare_func);
        break;
    default:
        g_assert_not_reached();
    }

    *ret_tree = tree;
    *ret_keys = keys;
}

static gboolean traverse_func(gpointer key, gpointer value, gpointer data)
{
    return FALSE;
}

static inline void remove_all(void *tree, enum impl_type impl)
{
    switch (impl) {
    case IMPL_GTREE:
        g_tree_destroy(tree);
        break;
    case IMPL_QTREE:
        q_tree_destroy(tree);
        break;
    default:
        g_assert_not_reached();
    }
}

static int64_t run_benchmark(const struct benchmark *bench,
                             enum impl_type impl,
                             size_t n_elems)
{
    void *tree;
    size_t *keys;

    init_empty_tree_and_keys(impl, &tree, &keys, n_elems);
    if (bench->fill_on_init) {
        for (size_t i = 0; i < n_elems; i++) {
            switch (impl) {
            case IMPL_GTREE:
                g_tree_insert(tree, &keys[i], &keys[i]);
                break;
            case IMPL_QTREE:
                q_tree_insert(tree, &keys[i], &keys[i]);
                break;
            default:
                g_assert_not_reached();
            }
        }
    }

    int64_t start_ns = get_clock();
    switch (bench->op) {
    case OP_LOOKUP:
        for (size_t i = 0; i < n_elems; i++) {
            void *value;
            switch (impl) {
            case IMPL_GTREE:
                value = g_tree_lookup(tree, &keys[i]);
                break;
            case IMPL_QTREE:
                value = q_tree_lookup(tree, &keys[i]);
                break;
            default:
                g_assert_not_reached();
            }
            (void)value;
        }
        break;
    case OP_INSERT:
        for (size_t i = 0; i < n_elems; i++) {
            switch (impl) {
            case IMPL_GTREE:
                g_tree_insert(tree, &keys[i], &keys[i]);
                break;
            case IMPL_QTREE:
                q_tree_insert(tree, &keys[i], &keys[i]);
                break;
            default:
                g_assert_not_reached();
            }
        }
        break;
    case OP_REMOVE:
        for (size_t i = 0; i < n_elems; i++) {
            switch (impl) {
            case IMPL_GTREE:
                g_tree_remove(tree, &keys[i]);
                break;
            case IMPL_QTREE:
                q_tree_remove(tree, &keys[i]);
                break;
            default:
                g_assert_not_reached();
            }
        }
        break;
    case OP_REMOVE_ALL:
        remove_all(tree, impl);
        break;
    case OP_TRAVERSE:
        switch (impl) {
        case IMPL_GTREE:
            g_tree_foreach(tree, traverse_func, NULL);
            break;
        case IMPL_QTREE:
            q_tree_foreach(tree, traverse_func, NULL);
            break;
        default:
            g_assert_not_reached();
        }
        break;
    default:
        g_assert_not_reached();
    }
    int64_t ns = get_clock() - start_ns;

    if (bench->op != OP_REMOVE_ALL) {
        remove_all(tree, impl);
    }
    g_free(keys);

    return ns;
}

int main(int argc, char *argv[])
{
    size_t sizes[] = {
        32,
        1024,
        1024 * 4,
        1024 * 128,
        1024 * 1024,
    };

    double res[ARRAY_SIZE(benchmarks)][ARRAY_SIZE(impls)][ARRAY_SIZE(sizes)];
    for (int i = 0; i < ARRAY_SIZE(sizes); i++) {
        size_t size = sizes[i];
        for (int j = 0; j < ARRAY_SIZE(impls); j++) {
            const struct tree_implementation *impl = &impls[j];
            for (int k = 0; k < ARRAY_SIZE(benchmarks); k++) {
                const struct benchmark *bench = &benchmarks[k];

                /* warm-up run */
                run_benchmark(bench, impl->type, size);

                int64_t total_ns = 0;
                int64_t n_runs = 0;
                while (total_ns < 2e8 || n_runs < 5) {
                    total_ns += run_benchmark(bench, impl->type, size);
                    n_runs++;
                }
                double ns_per_run = (double)total_ns / n_runs;

                /* Throughput, in Mops/s */
                res[k][j][i] = size / ns_per_run * 1e3;
            }
        }
    }

    printf("# Results' breakdown: Tree, Op and #Elements. Units: Mops/s\n");
    printf("%5s %10s ", "Tree", "Op");
    for (int i = 0; i < ARRAY_SIZE(sizes); i++) {
        printf("%7zu         ", sizes[i]);
    }
    printf("\n");
    char separator[97];
    for (int i = 0; i < ARRAY_SIZE(separator) - 1; i++) {
        separator[i] = '-';
    }
    separator[ARRAY_SIZE(separator) - 1] = '\0';
    printf("%s\n", separator);
    for (int i = 0; i < ARRAY_SIZE(benchmarks); i++) {
        for (int j = 0; j < ARRAY_SIZE(impls); j++) {
            printf("%5s %10s ", impls[j].name, benchmarks[i].name);
            for (int k = 0; k < ARRAY_SIZE(sizes); k++) {
                printf("%7.2f ", res[i][j][k]);
                if (j == 0) {
                    printf("        ");
                } else {
                    if (res[i][0][k] != 0) {
                        double speedup = res[i][j][k] / res[i][0][k];
                        printf("(%4.2fx) ", speedup);
                    } else {
                        printf("(     ) ");
                    }
                }
            }
            printf("\n");
        }
    }
    printf("%s\n", separator);
    return 0;
}
