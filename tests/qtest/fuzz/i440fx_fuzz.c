/*
 * I440FX Fuzzing Target
 *
 * Copyright Red Hat Inc., 2019
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qemu/main-loop.h"
#include "tests/qtest/libqtest.h"
#include "tests/qtest/libqos/pci.h"
#include "tests/qtest/libqos/pci-pc.h"
#include "fuzz.h"
#include "qos_fuzz.h"
#include "fork_fuzz.h"


#define I440FX_PCI_HOST_BRIDGE_CFG 0xcf8
#define I440FX_PCI_HOST_BRIDGE_DATA 0xcfc

/*
 * the input to the fuzzing functions below is a buffer of random bytes. we
 * want to convert these bytes into a sequence of qtest or qos calls. to do
 * this we define some opcodes:
 */
enum action_id {
    WRITEB,
    WRITEW,
    WRITEL,
    READB,
    READW,
    READL,
    ACTION_MAX
};

static void ioport_fuzz_qtest(QTestState *s,
        const unsigned char *Data, size_t Size) {
    /*
     * loop over the Data, breaking it up into actions. each action has an
     * opcode, address offset and value
     */
    struct {
        uint8_t opcode;
        uint8_t addr;
        uint32_t value;
    } a;

    while (Size >= sizeof(a)) {
        /* make a copy of the action so we can normalize the values in-place */
        memcpy(&a, Data, sizeof(a));
        /* select between two i440fx Port IO addresses */
        uint16_t addr = a.addr % 2 ? I440FX_PCI_HOST_BRIDGE_CFG :
                                      I440FX_PCI_HOST_BRIDGE_DATA;
        switch (a.opcode % ACTION_MAX) {
        case WRITEB:
            qtest_outb(s, addr, (uint8_t)a.value);
            break;
        case WRITEW:
            qtest_outw(s, addr, (uint16_t)a.value);
            break;
        case WRITEL:
            qtest_outl(s, addr, (uint32_t)a.value);
            break;
        case READB:
            qtest_inb(s, addr);
            break;
        case READW:
            qtest_inw(s, addr);
            break;
        case READL:
            qtest_inl(s, addr);
            break;
        }
        /* Move to the next operation */
        Size -= sizeof(a);
        Data += sizeof(a);
    }
    flush_events(s);
}

static void i440fx_fuzz_qtest(QTestState *s,
                              const unsigned char *Data,
                              size_t Size)
{
    ioport_fuzz_qtest(s, Data, Size);
}

static void pciconfig_fuzz_qos(QTestState *s, QPCIBus *bus,
        const unsigned char *Data, size_t Size) {
    /*
     * Same as ioport_fuzz_qtest, but using QOS. devfn is incorporated into the
     * value written over Port IO
     */
    struct {
        uint8_t opcode;
        uint8_t offset;
        int devfn;
        uint32_t value;
    } a;

    while (Size >= sizeof(a)) {
        memcpy(&a, Data, sizeof(a));
        switch (a.opcode % ACTION_MAX) {
        case WRITEB:
            bus->config_writeb(bus, a.devfn, a.offset, (uint8_t)a.value);
            break;
        case WRITEW:
            bus->config_writew(bus, a.devfn, a.offset, (uint16_t)a.value);
            break;
        case WRITEL:
            bus->config_writel(bus, a.devfn, a.offset, (uint32_t)a.value);
            break;
        case READB:
            bus->config_readb(bus, a.devfn, a.offset);
            break;
        case READW:
            bus->config_readw(bus, a.devfn, a.offset);
            break;
        case READL:
            bus->config_readl(bus, a.devfn, a.offset);
            break;
        }
        Size -= sizeof(a);
        Data += sizeof(a);
    }
    flush_events(s);
}

static void i440fx_fuzz_qos(QTestState *s,
                            const unsigned char *Data,
                            size_t Size)
{
    static QPCIBus *bus;

    if (!bus) {
        bus = qpci_new_pc(s, fuzz_qos_alloc);
    }

    pciconfig_fuzz_qos(s, bus, Data, Size);
}

static void i440fx_fuzz_qos_fork(QTestState *s,
        const unsigned char *Data, size_t Size) {
    if (fork() == 0) {
        i440fx_fuzz_qos(s, Data, Size);
        _Exit(0);
    } else {
        flush_events(s);
        wait(NULL);
    }
}

static const char *i440fx_qtest_argv = TARGET_NAME " -machine accel=qtest"
                                       " -m 0 -display none";
static GString *i440fx_argv(FuzzTarget *t)
{
    return g_string_new(i440fx_qtest_argv);
}

static void fork_init(void)
{
    counter_shm_init();
}

static void register_pci_fuzz_targets(void)
{
    /* Uses simple qtest commands and reboots to reset state */
    fuzz_add_target(&(FuzzTarget){
                .name = "i440fx-qtest-reboot-fuzz",
                .description = "Fuzz the i440fx using raw qtest commands and "
                               "rebooting after each run",
                .get_init_cmdline = i440fx_argv,
                .fuzz = i440fx_fuzz_qtest});

    /* Uses libqos and forks to prevent state leakage */
    fuzz_add_qos_target(&(FuzzTarget){
                .name = "i440fx-qos-fork-fuzz",
                .description = "Fuzz the i440fx using raw qtest commands and "
                               "rebooting after each run",
                .pre_vm_init = &fork_init,
                .fuzz = i440fx_fuzz_qos_fork,},
                "i440FX-pcihost",
                &(QOSGraphTestOptions){}
                );

    /*
     * Uses libqos. Doesn't do anything to reset state. Note that if we were to
     * reboot after each run, we would also have to redo the qos-related
     * initialization (qos_init_path)
     */
    fuzz_add_qos_target(&(FuzzTarget){
                .name = "i440fx-qos-noreset-fuzz",
                .description = "Fuzz the i440fx using raw qtest commands and "
                               "rebooting after each run",
                .fuzz = i440fx_fuzz_qos,},
                "i440FX-pcihost",
                &(QOSGraphTestOptions){}
                );
}

fuzz_target_init(register_pci_fuzz_targets);
