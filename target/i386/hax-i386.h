/*
 * QEMU HAXM support
 *
 * Copyright (c) 2011 Intel Corporation
 *  Written by:
 *  Jiang Yunhong<yunhong.jiang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HAX_I386_H
#define HAX_I386_H

#include "cpu.h"
#include "sysemu/hax.h"

#ifdef CONFIG_POSIX
typedef int hax_fd;
#endif

#ifdef CONFIG_WIN32
typedef HANDLE hax_fd;
#endif

extern struct hax_state hax_global;
struct hax_vcpu_state {
    hax_fd fd;
    int vcpu_id;
    struct hax_tunnel *tunnel;
    unsigned char *iobuf;
};

struct hax_state {
    hax_fd fd; /* the global hax device interface */
    uint32_t version;
    struct hax_vm *vm;
    uint64_t mem_quota;
    bool supports_64bit_ramblock;
};

#define HAX_MAX_VCPU 0x10

struct hax_vm {
    hax_fd fd;
    int id;
    int numvcpus;
    struct hax_vcpu_state **vcpus;
};

#ifdef NEED_CPU_H
/* Functions exported to host specific mode */
hax_fd hax_vcpu_get_fd(CPUArchState *env);
int valid_hax_tunnel_size(uint16_t size);

/* Host specific functions */
int hax_mod_version(struct hax_state *hax, struct hax_module_version *version);
int hax_inject_interrupt(CPUArchState *env, int vector);
struct hax_vm *hax_vm_create(struct hax_state *hax, int max_cpus);
int hax_vcpu_run(struct hax_vcpu_state *vcpu);
int hax_vcpu_create(int id);
int hax_sync_vcpu_state(CPUArchState *env, struct vcpu_state_t *state,
                        int set);
int hax_sync_msr(CPUArchState *env, struct hax_msr_data *msrs, int set);
int hax_sync_fpu(CPUArchState *env, struct fx_layout *fl, int set);
#endif

int hax_vm_destroy(struct hax_vm *vm);
int hax_capability(struct hax_state *hax, struct hax_capabilityinfo *cap);
int hax_notify_qemu_version(hax_fd vm_fd, struct hax_qemu_version *qversion);
int hax_set_ram(uint64_t start_pa, uint32_t size, uint64_t host_va, int flags);

/* Common host function */
int hax_host_create_vm(struct hax_state *hax, int *vm_id);
hax_fd hax_host_open_vm(struct hax_state *hax, int vm_id);
int hax_host_create_vcpu(hax_fd vm_fd, int vcpuid);
hax_fd hax_host_open_vcpu(int vmid, int vcpuid);
int hax_host_setup_vcpu_channel(struct hax_vcpu_state *vcpu);
hax_fd hax_mod_open(void);
void hax_memory_init(void);


#ifdef CONFIG_POSIX
#include "target/i386/hax-posix.h"
#endif

#ifdef CONFIG_WIN32
#include "target/i386/hax-windows.h"
#endif

#include "target/i386/hax-interface.h"

#endif
