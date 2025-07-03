/*
 * QEMU Secure Encrypted Virutualization (SEV) support
 *
 * Copyright: Advanced Micro Devices, 2016-2018
 *
 * Authors:
 *  Brijesh Singh <brijesh.singh@amd.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef I386_SEV_H
#define I386_SEV_H

#ifndef CONFIG_USER_ONLY
#include CONFIG_DEVICES /* CONFIG_SEV */
#endif

#if !defined(CONFIG_SEV) || defined(CONFIG_USER_ONLY)
#define sev_enabled() 0
#define sev_es_enabled() 0
#define sev_snp_enabled() 0
#else
bool sev_enabled(void);
bool sev_es_enabled(void);
bool sev_snp_enabled(void);
#endif

#if !defined(CONFIG_USER_ONLY)

#define TYPE_SEV_COMMON "sev-common"
#define TYPE_SEV_GUEST "sev-guest"
#define TYPE_SEV_SNP_GUEST "sev-snp-guest"

#define SEV_POLICY_NODBG        0x1
#define SEV_POLICY_NOKS         0x2
#define SEV_POLICY_ES           0x4
#define SEV_POLICY_NOSEND       0x8
#define SEV_POLICY_DOMAIN       0x10
#define SEV_POLICY_SEV          0x20

#define SEV_SNP_POLICY_SMT      0x10000
#define SEV_SNP_POLICY_DBG      0x80000

#define SVM_SEV_FEAT_SNP_ACTIVE 1

typedef struct SevKernelLoaderContext {
    char *setup_data;
    size_t setup_size;
    char *kernel_data;
    size_t kernel_size;
    char *initrd_data;
    size_t initrd_size;
    char *cmdline_data;
    size_t cmdline_size;
} SevKernelLoaderContext;

/* Save area definition for SEV-ES and SEV-SNP guests */
struct QEMU_PACKED sev_es_save_area {
    struct vmcb_seg es;
    struct vmcb_seg cs;
    struct vmcb_seg ss;
    struct vmcb_seg ds;
    struct vmcb_seg fs;
    struct vmcb_seg gs;
    struct vmcb_seg gdtr;
    struct vmcb_seg ldtr;
    struct vmcb_seg idtr;
    struct vmcb_seg tr;
    uint64_t vmpl0_ssp;
    uint64_t vmpl1_ssp;
    uint64_t vmpl2_ssp;
    uint64_t vmpl3_ssp;
    uint64_t u_cet;
    uint8_t reserved_0xc8[2];
    uint8_t vmpl;
    uint8_t cpl;
    uint8_t reserved_0xcc[4];
    uint64_t efer;
    uint8_t reserved_0xd8[104];
    uint64_t xss;
    uint64_t cr4;
    uint64_t cr3;
    uint64_t cr0;
    uint64_t dr7;
    uint64_t dr6;
    uint64_t rflags;
    uint64_t rip;
    uint64_t dr0;
    uint64_t dr1;
    uint64_t dr2;
    uint64_t dr3;
    uint64_t dr0_addr_mask;
    uint64_t dr1_addr_mask;
    uint64_t dr2_addr_mask;
    uint64_t dr3_addr_mask;
    uint8_t reserved_0x1c0[24];
    uint64_t rsp;
    uint64_t s_cet;
    uint64_t ssp;
    uint64_t isst_addr;
    uint64_t rax;
    uint64_t star;
    uint64_t lstar;
    uint64_t cstar;
    uint64_t sfmask;
    uint64_t kernel_gs_base;
    uint64_t sysenter_cs;
    uint64_t sysenter_esp;
    uint64_t sysenter_eip;
    uint64_t cr2;
    uint8_t reserved_0x248[32];
    uint64_t g_pat;
    uint64_t dbgctl;
    uint64_t br_from;
    uint64_t br_to;
    uint64_t last_excp_from;
    uint64_t last_excp_to;
    uint8_t reserved_0x298[80];
    uint32_t pkru;
    uint32_t tsc_aux;
    uint8_t reserved_0x2f0[24];
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rbx;
    uint64_t reserved_0x320; /* rsp already available at 0x01d8 */
    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint8_t reserved_0x380[16];
    uint64_t guest_exit_info_1;
    uint64_t guest_exit_info_2;
    uint64_t guest_exit_int_info;
    uint64_t guest_nrip;
    uint64_t sev_features;
    uint64_t vintr_ctrl;
    uint64_t guest_exit_code;
    uint64_t virtual_tom;
    uint64_t tlb_id;
    uint64_t pcpu_id;
    uint64_t event_inj;
    uint64_t xcr0;
    uint8_t reserved_0x3f0[16];

    /* Floating point area */
    uint64_t x87_dp;
    uint32_t mxcsr;
    uint16_t x87_ftw;
    uint16_t x87_fsw;
    uint16_t x87_fcw;
    uint16_t x87_fop;
    uint16_t x87_ds;
    uint16_t x87_cs;
    uint64_t x87_rip;
    uint8_t fpreg_x87[80];
    uint8_t fpreg_xmm[256];
    uint8_t fpreg_ymm[256];
};

struct QEMU_PACKED sev_snp_id_authentication {
    uint32_t id_key_alg;
    uint32_t auth_key_algo;
    uint8_t reserved[56];
    uint8_t id_block_sig[512];
    uint8_t id_key[1028];
    uint8_t reserved2[60];
    uint8_t id_key_sig[512];
    uint8_t author_key[1028];
    uint8_t reserved3[892];
};

bool sev_add_kernel_loader_hashes(SevKernelLoaderContext *ctx, Error **errp);

int sev_encrypt_flash(hwaddr gpa, uint8_t *ptr, uint64_t len, Error **errp);
int sev_inject_launch_secret(const char *hdr, const char *secret,
                             uint64_t gpa, Error **errp);

int sev_es_save_reset_vector(void *flash_ptr, uint64_t flash_size);
void sev_es_set_reset_vector(CPUState *cpu);

void pc_system_parse_sev_metadata(uint8_t *flash_ptr, size_t flash_size);

#endif /* !CONFIG_USER_ONLY */

uint32_t sev_get_cbit_position(void);
uint32_t sev_get_reduced_phys_bits(void);

#endif
