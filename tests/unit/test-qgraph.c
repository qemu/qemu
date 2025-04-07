/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "../qtest/libqos/qgraph.h"
#include "../qtest/libqos/qgraph_internal.h"

#define MACHINE_PC "x86_64/pc"
#define MACHINE_RASPI2 "arm/raspi2b"
#define I440FX "i440FX-pcihost"
#define PCIBUS_PC "pcibus-pc"
#define SDHCI "sdhci"
#define PCIBUS "pci-bus"
#define SDHCI_PCI "sdhci-pci"
#define SDHCI_MM "generic-sdhci"
#define REGISTER_TEST "register-test"

int npath;

static void *machinefunct(QTestState *qts)
{
    return NULL;
}

static void *driverfunct(void *obj, QGuestAllocator *machine, void *arg)
{
    return NULL;
}

static void testfunct(void *obj, void *arg, QGuestAllocator *alloc)
{
}

static void check_interface(const char *interface)
{
    g_assert_cmpint(qos_graph_has_machine(interface), ==, FALSE);
    g_assert_nonnull(qos_graph_get_node(interface));
    g_assert_cmpint(qos_graph_has_node(interface), ==, TRUE);
    g_assert_cmpint(qos_graph_get_node_type(interface), ==, QNODE_INTERFACE);
    qos_graph_node_set_availability(interface, TRUE);
    g_assert_cmpint(qos_graph_get_node_availability(interface), ==, TRUE);
}

static void check_machine(const char *machine)
{
    qos_node_create_machine(machine, machinefunct);
    g_assert_nonnull(qos_graph_get_machine(machine));
    g_assert_cmpint(qos_graph_has_machine(machine), ==, TRUE);
    g_assert_nonnull(qos_graph_get_node(machine));
    g_assert_cmpint(qos_graph_get_node_availability(machine), ==, FALSE);
    qos_graph_node_set_availability(machine, TRUE);
    g_assert_cmpint(qos_graph_get_node_availability(machine), ==, TRUE);
    g_assert_cmpint(qos_graph_has_node(machine), ==, TRUE);
    g_assert_cmpint(qos_graph_get_node_type(machine), ==, QNODE_MACHINE);
}

static void check_contains(const char *machine, const char *driver)
{
    QOSGraphEdge *edge;
    qos_node_contains(machine, driver, NULL);

    edge = qos_graph_get_edge(machine, driver);
    g_assert_nonnull(edge);
    g_assert_cmpint(qos_graph_edge_get_type(edge), ==, QEDGE_CONTAINS);
    g_assert_cmpint(qos_graph_has_edge(machine, driver), ==, TRUE);
}

static void check_produces(const char *machine, const char *interface)
{
    QOSGraphEdge *edge;

    qos_node_produces(machine, interface);
    check_interface(interface);
    edge = qos_graph_get_edge(machine, interface);
    g_assert_nonnull(edge);
    g_assert_cmpint(qos_graph_edge_get_type(edge), ==,
                    QEDGE_PRODUCES);
    g_assert_cmpint(qos_graph_has_edge(machine, interface), ==, TRUE);
}

static void check_consumes(const char *driver, const char *interface)
{
    QOSGraphEdge *edge;

    qos_node_consumes(driver, interface, NULL);
    check_interface(interface);
    edge = qos_graph_get_edge(interface, driver);
    g_assert_nonnull(edge);
    g_assert_cmpint(qos_graph_edge_get_type(edge), ==, QEDGE_CONSUMED_BY);
    g_assert_cmpint(qos_graph_has_edge(interface, driver), ==, TRUE);
}

static void check_driver(const char *driver)
{
    qos_node_create_driver(driver, driverfunct);
    g_assert_cmpint(qos_graph_has_machine(driver), ==, FALSE);
    g_assert_nonnull(qos_graph_get_node(driver));
    g_assert_cmpint(qos_graph_has_node(driver), ==, TRUE);
    g_assert_cmpint(qos_graph_get_node_type(driver), ==, QNODE_DRIVER);
    g_assert_cmpint(qos_graph_get_node_availability(driver), ==, FALSE);
    qos_graph_node_set_availability(driver, TRUE);
    g_assert_cmpint(qos_graph_get_node_availability(driver), ==, TRUE);
}

static void check_test(const char *test, const char *interface)
{
    QOSGraphEdge *edge;
    char *full_name = g_strdup_printf("%s-tests/%s", interface, test);

    qos_add_test(test, interface, testfunct, NULL);
    g_assert_cmpint(qos_graph_has_machine(test), ==, FALSE);
    g_assert_cmpint(qos_graph_has_machine(full_name), ==, FALSE);
    g_assert_nonnull(qos_graph_get_node(full_name));
    g_assert_cmpint(qos_graph_has_node(full_name), ==, TRUE);
    g_assert_cmpint(qos_graph_get_node_type(full_name), ==, QNODE_TEST);
    edge = qos_graph_get_edge(interface, full_name);
    g_assert_nonnull(edge);
    g_assert_cmpint(qos_graph_edge_get_type(edge), ==,
                    QEDGE_CONSUMED_BY);
    g_assert_cmpint(qos_graph_has_edge(interface, full_name), ==, TRUE);
    g_assert_cmpint(qos_graph_get_node_availability(full_name), ==, TRUE);
    qos_graph_node_set_availability(full_name, FALSE);
    g_assert_cmpint(qos_graph_get_node_availability(full_name), ==, FALSE);
    g_free(full_name);
}

static void count_each_test(QOSGraphNode *path, int len)
{
    npath++;
}

static void check_leaf_discovered(int n)
{
    npath = 0;
    qos_graph_foreach_test_path(count_each_test);
    g_assert_cmpint(n, ==, npath);
}

/* G_Test functions */

static void init_nop(void)
{
    qos_graph_init();
    qos_graph_destroy();
}

static void test_machine(void)
{
    qos_graph_init();
    check_machine(MACHINE_PC);
    qos_graph_destroy();
}

static void test_contains(void)
{
    qos_graph_init();
    check_contains(MACHINE_PC, I440FX);
    g_assert_null(qos_graph_get_machine(MACHINE_PC));
    g_assert_null(qos_graph_get_machine(I440FX));
    g_assert_null(qos_graph_get_node(MACHINE_PC));
    g_assert_null(qos_graph_get_node(I440FX));
    qos_graph_destroy();
}

static void test_multiple_contains(void)
{
    qos_graph_init();
    check_contains(MACHINE_PC, I440FX);
    check_contains(MACHINE_PC, PCIBUS_PC);
    qos_graph_destroy();
}

static void test_produces(void)
{
    qos_graph_init();
    check_produces(MACHINE_PC, I440FX);
    g_assert_null(qos_graph_get_machine(MACHINE_PC));
    g_assert_null(qos_graph_get_machine(I440FX));
    g_assert_null(qos_graph_get_node(MACHINE_PC));
    g_assert_nonnull(qos_graph_get_node(I440FX));
    qos_graph_destroy();
}

static void test_multiple_produces(void)
{
    qos_graph_init();
    check_produces(MACHINE_PC, I440FX);
    check_produces(MACHINE_PC, PCIBUS_PC);
    qos_graph_destroy();
}

static void test_consumes(void)
{
    qos_graph_init();
    check_consumes(I440FX, SDHCI);
    g_assert_null(qos_graph_get_machine(I440FX));
    g_assert_null(qos_graph_get_machine(SDHCI));
    g_assert_null(qos_graph_get_node(I440FX));
    g_assert_nonnull(qos_graph_get_node(SDHCI));
    qos_graph_destroy();
}

static void test_multiple_consumes(void)
{
    qos_graph_init();
    check_consumes(I440FX, SDHCI);
    check_consumes(PCIBUS_PC, SDHCI);
    qos_graph_destroy();
}

static void test_driver(void)
{
    qos_graph_init();
    check_driver(I440FX);
    qos_graph_destroy();
}

static void test_test(void)
{
    qos_graph_init();
    check_test(REGISTER_TEST, SDHCI);
    qos_graph_destroy();
}

static void test_machine_contains_driver(void)
{
    qos_graph_init();
    check_machine(MACHINE_PC);
    check_driver(I440FX);
    check_contains(MACHINE_PC, I440FX);
    qos_graph_destroy();
}

static void test_driver_contains_driver(void)
{
    qos_graph_init();
    check_driver(PCIBUS_PC);
    check_driver(I440FX);
    check_contains(PCIBUS_PC, I440FX);
    qos_graph_destroy();
}

static void test_machine_produces_interface(void)
{
    qos_graph_init();
    check_machine(MACHINE_PC);
    check_produces(MACHINE_PC, SDHCI);
    qos_graph_destroy();
}

static void test_driver_produces_interface(void)
{
    qos_graph_init();
    check_driver(I440FX);
    check_produces(I440FX, SDHCI);
    qos_graph_destroy();
}

static void test_machine_consumes_interface(void)
{
    qos_graph_init();
    check_machine(MACHINE_PC);
    check_consumes(MACHINE_PC, SDHCI);
    qos_graph_destroy();
}

static void test_driver_consumes_interface(void)
{
    qos_graph_init();
    check_driver(I440FX);
    check_consumes(I440FX, SDHCI);
    qos_graph_destroy();
}

static void test_test_consumes_interface(void)
{
    qos_graph_init();
    check_test(REGISTER_TEST, SDHCI);
    qos_graph_destroy();
}

static void test_full_sample(void)
{
    qos_graph_init();
    check_machine(MACHINE_PC);
    check_contains(MACHINE_PC, I440FX);
    check_driver(I440FX);
    check_driver(PCIBUS_PC);
    check_contains(I440FX, PCIBUS_PC);
    check_produces(PCIBUS_PC, PCIBUS);
    check_driver(SDHCI_PCI);
    qos_node_consumes(SDHCI_PCI, PCIBUS, NULL);
    check_produces(SDHCI_PCI, SDHCI);
    check_driver(SDHCI_MM);
    check_produces(SDHCI_MM, SDHCI);
    qos_add_test(REGISTER_TEST, SDHCI, testfunct, NULL);
    check_leaf_discovered(1);
    qos_print_graph();
    qos_graph_destroy();
}

static void test_full_sample_raspi(void)
{
    qos_graph_init();
    check_machine(MACHINE_PC);
    check_contains(MACHINE_PC, I440FX);
    check_driver(I440FX);
    check_driver(PCIBUS_PC);
    check_contains(I440FX, PCIBUS_PC);
    check_produces(PCIBUS_PC, PCIBUS);
    check_driver(SDHCI_PCI);
    qos_node_consumes(SDHCI_PCI, PCIBUS, NULL);
    check_produces(SDHCI_PCI, SDHCI);
    check_machine(MACHINE_RASPI2);
    check_contains(MACHINE_RASPI2, SDHCI_MM);
    check_driver(SDHCI_MM);
    check_produces(SDHCI_MM, SDHCI);
    qos_add_test(REGISTER_TEST, SDHCI, testfunct, NULL);
    qos_print_graph();
    check_leaf_discovered(2);
    qos_graph_destroy();
}

static void test_cycle(void)
{
    qos_graph_init();
    check_machine(MACHINE_RASPI2);
    check_driver("B");
    check_driver("C");
    check_driver("D");
    check_contains(MACHINE_RASPI2, "B");
    check_contains("B", "C");
    check_contains("C", "D");
    check_contains("D", MACHINE_RASPI2);
    check_leaf_discovered(0);
    qos_print_graph();
    qos_graph_destroy();
}

static void test_two_test_same_interface(void)
{
    qos_graph_init();
    check_machine(MACHINE_RASPI2);
    check_produces(MACHINE_RASPI2, "B");
    qos_add_test("C", "B", testfunct, NULL);
    qos_add_test("D", "B", testfunct, NULL);
    check_contains(MACHINE_RASPI2, "B");
    check_leaf_discovered(4);
    qos_print_graph();
    qos_graph_destroy();
}

static void test_test_in_path(void)
{
    qos_graph_init();
    check_machine(MACHINE_RASPI2);
    check_produces(MACHINE_RASPI2, "B");
    qos_add_test("C", "B", testfunct, NULL);
    check_driver("D");
    check_consumes("D", "B");
    check_produces("D", "E");
    qos_add_test("F", "E", testfunct, NULL);
    check_leaf_discovered(2);
    qos_print_graph();
    qos_graph_destroy();
}

static void test_double_edge(void)
{
    qos_graph_init();
    check_machine(MACHINE_RASPI2);
    check_produces("B", "C");
    qos_node_consumes("C", "B", NULL);
    qos_add_test("D", "C", testfunct, NULL);
    check_contains(MACHINE_RASPI2, "B");
    qos_print_graph();
    qos_graph_destroy();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/qgraph/init_nop", init_nop);
    g_test_add_func("/qgraph/test_machine", test_machine);
    g_test_add_func("/qgraph/test_contains", test_contains);
    g_test_add_func("/qgraph/test_multiple_contains", test_multiple_contains);
    g_test_add_func("/qgraph/test_produces", test_produces);
    g_test_add_func("/qgraph/test_multiple_produces", test_multiple_produces);
    g_test_add_func("/qgraph/test_consumes", test_consumes);
    g_test_add_func("/qgraph/test_multiple_consumes",
                    test_multiple_consumes);
    g_test_add_func("/qgraph/test_driver", test_driver);
    g_test_add_func("/qgraph/test_test", test_test);
    g_test_add_func("/qgraph/test_machine_contains_driver",
                    test_machine_contains_driver);
    g_test_add_func("/qgraph/test_driver_contains_driver",
                    test_driver_contains_driver);
    g_test_add_func("/qgraph/test_machine_produces_interface",
                    test_machine_produces_interface);
    g_test_add_func("/qgraph/test_driver_produces_interface",
                    test_driver_produces_interface);
    g_test_add_func("/qgraph/test_machine_consumes_interface",
                    test_machine_consumes_interface);
    g_test_add_func("/qgraph/test_driver_consumes_interface",
                    test_driver_consumes_interface);
    g_test_add_func("/qgraph/test_test_consumes_interface",
                    test_test_consumes_interface);
    g_test_add_func("/qgraph/test_full_sample", test_full_sample);
    g_test_add_func("/qgraph/test_full_sample_raspi", test_full_sample_raspi);
    g_test_add_func("/qgraph/test_cycle", test_cycle);
    g_test_add_func("/qgraph/test_two_test_same_interface",
                    test_two_test_same_interface);
    g_test_add_func("/qgraph/test_test_in_path", test_test_in_path);
    g_test_add_func("/qgraph/test_double_edge", test_double_edge);

    g_test_run();
    return 0;
}
