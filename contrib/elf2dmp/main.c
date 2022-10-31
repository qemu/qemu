/*
 * Copyright (c) 2018 Virtuozzo International GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 *
 */

#include "qemu/osdep.h"

#include "err.h"
#include "addrspace.h"
#include "pe.h"
#include "pdb.h"
#include "kdbg.h"
#include "download.h"
#include "qemu/win_dump_defs.h"

#define SYM_URL_BASE    "https://msdl.microsoft.com/download/symbols/"
#define PDB_NAME    "ntkrnlmp.pdb"

#define INITIAL_MXCSR   0x1f80

typedef struct idt_desc {
    uint16_t offset1;   /* offset bits 0..15 */
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset2;   /* offset bits 16..31 */
    uint32_t offset3;   /* offset bits 32..63 */
    uint32_t rsrvd;
} __attribute__ ((packed)) idt_desc_t;

static uint64_t idt_desc_addr(idt_desc_t desc)
{
    return (uint64_t)desc.offset1 | ((uint64_t)desc.offset2 << 16) |
          ((uint64_t)desc.offset3 << 32);
}

static const uint64_t SharedUserData = 0xfffff78000000000;

#define KUSD_OFFSET_SUITE_MASK 0x2d0
#define KUSD_OFFSET_PRODUCT_TYPE 0x264

#define SYM_RESOLVE(base, r, s) ((s = pdb_resolve(base, r, #s)),\
    s ? printf(#s" = 0x%016"PRIx64"\n", s) :\
    eprintf("Failed to resolve "#s"\n"), s)

static uint64_t rol(uint64_t x, uint64_t y)
{
    return (x << y) | (x >> (64 - y));
}

/*
 * Decoding algorithm can be found in Volatility project
 */
static void kdbg_decode(uint64_t *dst, uint64_t *src, size_t size,
        uint64_t kwn, uint64_t kwa, uint64_t kdbe)
{
    size_t i;
    assert(size % sizeof(uint64_t) == 0);
    for (i = 0; i < size / sizeof(uint64_t); i++) {
        uint64_t block;

        block = src[i];
        block = rol(block ^ kwn, (uint8_t)kwn);
        block = __builtin_bswap64(block ^ kdbe) ^ kwa;
        dst[i] = block;
    }
}

static KDDEBUGGER_DATA64 *get_kdbg(uint64_t KernBase, struct pdb_reader *pdb,
        struct va_space *vs, uint64_t KdDebuggerDataBlock)
{
    const char OwnerTag[4] = "KDBG";
    KDDEBUGGER_DATA64 *kdbg = NULL;
    DBGKD_DEBUG_DATA_HEADER64 kdbg_hdr;
    bool decode = false;
    uint64_t kwn, kwa, KdpDataBlockEncoded;

    if (va_space_rw(vs,
                KdDebuggerDataBlock + offsetof(KDDEBUGGER_DATA64, Header),
                &kdbg_hdr, sizeof(kdbg_hdr), 0)) {
        eprintf("Failed to extract KDBG header\n");
        return NULL;
    }

    if (memcmp(&kdbg_hdr.OwnerTag, OwnerTag, sizeof(OwnerTag))) {
        uint64_t KiWaitNever, KiWaitAlways;

        decode = true;

        if (!SYM_RESOLVE(KernBase, pdb, KiWaitNever) ||
                !SYM_RESOLVE(KernBase, pdb, KiWaitAlways) ||
                !SYM_RESOLVE(KernBase, pdb, KdpDataBlockEncoded)) {
            return NULL;
        }

        if (va_space_rw(vs, KiWaitNever, &kwn, sizeof(kwn), 0) ||
                va_space_rw(vs, KiWaitAlways, &kwa, sizeof(kwa), 0)) {
            return NULL;
        }

        printf("[KiWaitNever] = 0x%016"PRIx64"\n", kwn);
        printf("[KiWaitAlways] = 0x%016"PRIx64"\n", kwa);

        /*
         * If KDBG header can be decoded, KDBG size is available
         * and entire KDBG can be decoded.
         */
        printf("Decoding KDBG header...\n");
        kdbg_decode((uint64_t *)&kdbg_hdr, (uint64_t *)&kdbg_hdr,
                sizeof(kdbg_hdr), kwn, kwa, KdpDataBlockEncoded);

        printf("Owner tag is \'%.4s\'\n", (char *)&kdbg_hdr.OwnerTag);
        if (memcmp(&kdbg_hdr.OwnerTag, OwnerTag, sizeof(OwnerTag))) {
            eprintf("Failed to decode KDBG header\n");
            return NULL;
        }
    }

    kdbg = malloc(kdbg_hdr.Size);
    if (!kdbg) {
        return NULL;
    }

    if (va_space_rw(vs, KdDebuggerDataBlock, kdbg, kdbg_hdr.Size, 0)) {
        eprintf("Failed to extract entire KDBG\n");
        free(kdbg);
        return NULL;
    }

    if (!decode) {
        return kdbg;
    }

    printf("Decoding KdDebuggerDataBlock...\n");
    kdbg_decode((uint64_t *)kdbg, (uint64_t *)kdbg, kdbg_hdr.Size,
                kwn, kwa, KdpDataBlockEncoded);

    va_space_rw(vs, KdDebuggerDataBlock, kdbg, kdbg_hdr.Size, 1);

    return kdbg;
}

static void win_context_init_from_qemu_cpu_state(WinContext64 *ctx,
        QEMUCPUState *s)
{
    WinContext64 win_ctx = (WinContext64){
        .ContextFlags = WIN_CTX_X64 | WIN_CTX_INT | WIN_CTX_SEG | WIN_CTX_CTL,
        .MxCsr = INITIAL_MXCSR,

        .SegCs = s->cs.selector,
        .SegSs = s->ss.selector,
        .SegDs = s->ds.selector,
        .SegEs = s->es.selector,
        .SegFs = s->fs.selector,
        .SegGs = s->gs.selector,
        .EFlags = (uint32_t)s->rflags,

        .Rax = s->rax,
        .Rbx = s->rbx,
        .Rcx = s->rcx,
        .Rdx = s->rdx,
        .Rsp = s->rsp,
        .Rbp = s->rbp,
        .Rsi = s->rsi,
        .Rdi = s->rdi,
        .R8  = s->r8,
        .R9  = s->r9,
        .R10 = s->r10,
        .R11 = s->r11,
        .R12 = s->r12,
        .R13 = s->r13,
        .R14 = s->r14,
        .R15 = s->r15,

        .Rip = s->rip,
        .FltSave = {
            .MxCsr = INITIAL_MXCSR,
        },
    };

    *ctx = win_ctx;
}

/*
 * Finds paging-structure hierarchy base,
 * if previously set doesn't give access to kernel structures
 */
static int fix_dtb(struct va_space *vs, QEMU_Elf *qe)
{
    /*
     * Firstly, test previously set DTB.
     */
    if (va_space_resolve(vs, SharedUserData)) {
        return 0;
    }

    /*
     * Secondly, find CPU which run system task.
     */
    size_t i;
    for (i = 0; i < qe->state_nr; i++) {
        QEMUCPUState *s = qe->state[i];

        if (is_system(s)) {
            va_space_set_dtb(vs, s->cr[3]);
            printf("DTB 0x%016"PRIx64" has been found from CPU #%zu"
                    " as system task CR3\n", vs->dtb, i);
            return !(va_space_resolve(vs, SharedUserData));
        }
    }

    /*
     * Thirdly, use KERNEL_GS_BASE from CPU #0 as PRCB address and
     * CR3 as [Prcb+0x7000]
     */
    if (qe->has_kernel_gs_base) {
        QEMUCPUState *s = qe->state[0];
        uint64_t Prcb = s->kernel_gs_base;
        uint64_t *cr3 = va_space_resolve(vs, Prcb + 0x7000);

        if (!cr3) {
            return 1;
        }

        va_space_set_dtb(vs, *cr3);
        printf("DirectoryTableBase = 0x%016"PRIx64" has been found from CPU #0"
                " as interrupt handling CR3\n", vs->dtb);
        return !(va_space_resolve(vs, SharedUserData));
    }

    return 1;
}

static int fill_header(WinDumpHeader64 *hdr, struct pa_space *ps,
        struct va_space *vs, uint64_t KdDebuggerDataBlock,
        KDDEBUGGER_DATA64 *kdbg, uint64_t KdVersionBlock, int nr_cpus)
{
    uint32_t *suite_mask = va_space_resolve(vs, SharedUserData +
            KUSD_OFFSET_SUITE_MASK);
    int32_t *product_type = va_space_resolve(vs, SharedUserData +
            KUSD_OFFSET_PRODUCT_TYPE);
    DBGKD_GET_VERSION64 kvb;
    WinDumpHeader64 h;
    size_t i;

    QEMU_BUILD_BUG_ON(KUSD_OFFSET_SUITE_MASK >= ELF2DMP_PAGE_SIZE);
    QEMU_BUILD_BUG_ON(KUSD_OFFSET_PRODUCT_TYPE >= ELF2DMP_PAGE_SIZE);

    if (!suite_mask || !product_type) {
        return 1;
    }

    if (va_space_rw(vs, KdVersionBlock, &kvb, sizeof(kvb), 0)) {
        eprintf("Failed to extract KdVersionBlock\n");
        return 1;
    }

    h = (WinDumpHeader64) {
        .Signature = "PAGE",
        .ValidDump = "DU64",
        .MajorVersion = kvb.MajorVersion,
        .MinorVersion = kvb.MinorVersion,
        .DirectoryTableBase = vs->dtb,
        .PfnDatabase = kdbg->MmPfnDatabase,
        .PsLoadedModuleList = kdbg->PsLoadedModuleList,
        .PsActiveProcessHead = kdbg->PsActiveProcessHead,
        .MachineImageType = kvb.MachineType,
        .NumberProcessors = nr_cpus,
        .BugcheckCode = LIVE_SYSTEM_DUMP,
        .KdDebuggerDataBlock = KdDebuggerDataBlock,
        .DumpType = 1,
        .Comment = "Hello from elf2dmp!",
        .SuiteMask = *suite_mask,
        .ProductType = *product_type,
        .SecondaryDataState = kvb.KdSecondaryVersion,
        .PhysicalMemoryBlock = (WinDumpPhyMemDesc64) {
            .NumberOfRuns = ps->block_nr,
        },
        .RequiredDumpSpace = sizeof(h),
    };

    for (i = 0; i < ps->block_nr; i++) {
        h.PhysicalMemoryBlock.NumberOfPages += ps->block[i].size / ELF2DMP_PAGE_SIZE;
        h.PhysicalMemoryBlock.Run[i] = (WinDumpPhyMemRun64) {
            .BasePage = ps->block[i].paddr / ELF2DMP_PAGE_SIZE,
            .PageCount = ps->block[i].size / ELF2DMP_PAGE_SIZE,
        };
    }

    h.RequiredDumpSpace += h.PhysicalMemoryBlock.NumberOfPages << ELF2DMP_PAGE_BITS;

    *hdr = h;

    return 0;
}

static int fill_context(KDDEBUGGER_DATA64 *kdbg,
        struct va_space *vs, QEMU_Elf *qe)
{
        int i;
    for (i = 0; i < qe->state_nr; i++) {
        uint64_t Prcb;
        uint64_t Context;
        WinContext64 ctx;
        QEMUCPUState *s = qe->state[i];

        if (va_space_rw(vs, kdbg->KiProcessorBlock + sizeof(Prcb) * i,
                    &Prcb, sizeof(Prcb), 0)) {
            eprintf("Failed to read CPU #%d PRCB location\n", i);
            return 1;
        }

        if (va_space_rw(vs, Prcb + kdbg->OffsetPrcbContext,
                    &Context, sizeof(Context), 0)) {
            eprintf("Failed to read CPU #%d ContextFrame location\n", i);
            return 1;
        }

        printf("Filling context for CPU #%d...\n", i);
        win_context_init_from_qemu_cpu_state(&ctx, s);

        if (va_space_rw(vs, Context, &ctx, sizeof(ctx), 1)) {
            eprintf("Failed to fill CPU #%d context\n", i);
            return 1;
        }
    }

    return 0;
}

static int write_dump(struct pa_space *ps,
        WinDumpHeader64 *hdr, const char *name)
{
    FILE *dmp_file = fopen(name, "wb");
    size_t i;

    if (!dmp_file) {
        eprintf("Failed to open output file \'%s\'\n", name);
        return 1;
    }

    printf("Writing header to file...\n");

    if (fwrite(hdr, sizeof(*hdr), 1, dmp_file) != 1) {
        eprintf("Failed to write dump header\n");
        fclose(dmp_file);
        return 1;
    }

    for (i = 0; i < ps->block_nr; i++) {
        struct pa_block *b = &ps->block[i];

        printf("Writing block #%zu/%zu to file...\n", i, ps->block_nr);
        if (fwrite(b->addr, b->size, 1, dmp_file) != 1) {
            eprintf("Failed to write dump header\n");
            fclose(dmp_file);
            return 1;
        }
    }

    return fclose(dmp_file);
}

static int pe_get_pdb_symstore_hash(uint64_t base, void *start_addr,
        char *hash, struct va_space *vs)
{
    const char e_magic[2] = "MZ";
    const char Signature[4] = "PE\0\0";
    const char sign_rsds[4] = "RSDS";
    IMAGE_DOS_HEADER *dos_hdr = start_addr;
    IMAGE_NT_HEADERS64 nt_hdrs;
    IMAGE_FILE_HEADER *file_hdr = &nt_hdrs.FileHeader;
    IMAGE_OPTIONAL_HEADER64 *opt_hdr = &nt_hdrs.OptionalHeader;
    IMAGE_DATA_DIRECTORY *data_dir = nt_hdrs.OptionalHeader.DataDirectory;
    IMAGE_DEBUG_DIRECTORY debug_dir;
    OMFSignatureRSDS rsds;
    char *pdb_name;
    size_t pdb_name_sz;
    size_t i;

    QEMU_BUILD_BUG_ON(sizeof(*dos_hdr) >= ELF2DMP_PAGE_SIZE);

    if (memcmp(&dos_hdr->e_magic, e_magic, sizeof(e_magic))) {
        return 1;
    }

    if (va_space_rw(vs, base + dos_hdr->e_lfanew,
                &nt_hdrs, sizeof(nt_hdrs), 0)) {
        return 1;
    }

    if (memcmp(&nt_hdrs.Signature, Signature, sizeof(Signature)) ||
            file_hdr->Machine != 0x8664 || opt_hdr->Magic != 0x020b) {
        return 1;
    }

    printf("Debug Directory RVA = 0x%08"PRIx32"\n",
            (uint32_t)data_dir[IMAGE_FILE_DEBUG_DIRECTORY].VirtualAddress);

    if (va_space_rw(vs,
                base + data_dir[IMAGE_FILE_DEBUG_DIRECTORY].VirtualAddress,
                &debug_dir, sizeof(debug_dir), 0)) {
        return 1;
    }

    if (debug_dir.Type != IMAGE_DEBUG_TYPE_CODEVIEW) {
        return 1;
    }

    if (va_space_rw(vs,
                base + debug_dir.AddressOfRawData,
                &rsds, sizeof(rsds), 0)) {
        return 1;
    }

    printf("CodeView signature is \'%.4s\'\n", rsds.Signature);

    if (memcmp(&rsds.Signature, sign_rsds, sizeof(sign_rsds))) {
        return 1;
    }

    pdb_name_sz = debug_dir.SizeOfData - sizeof(rsds);
    pdb_name = malloc(pdb_name_sz);
    if (!pdb_name) {
        return 1;
    }

    if (va_space_rw(vs, base + debug_dir.AddressOfRawData +
                offsetof(OMFSignatureRSDS, name), pdb_name, pdb_name_sz, 0)) {
        free(pdb_name);
        return 1;
    }

    printf("PDB name is \'%s\', \'%s\' expected\n", pdb_name, PDB_NAME);

    if (strcmp(pdb_name, PDB_NAME)) {
        eprintf("Unexpected PDB name, it seems the kernel isn't found\n");
        free(pdb_name);
        return 1;
    }

    free(pdb_name);

    sprintf(hash, "%.08x%.04x%.04x%.02x%.02x", rsds.guid.a, rsds.guid.b,
            rsds.guid.c, rsds.guid.d[0], rsds.guid.d[1]);
    hash += 20;
    for (i = 0; i < 6; i++, hash += 2) {
        sprintf(hash, "%.02x", rsds.guid.e[i]);
    }

    sprintf(hash, "%.01x", rsds.age);

    return 0;
}

int main(int argc, char *argv[])
{
    int err = 0;
    QEMU_Elf qemu_elf;
    struct pa_space ps;
    struct va_space vs;
    QEMUCPUState *state;
    idt_desc_t first_idt_desc;
    uint64_t KernBase;
    void *nt_start_addr = NULL;
    WinDumpHeader64 header;
    char pdb_hash[34];
    char pdb_url[] = SYM_URL_BASE PDB_NAME
        "/0123456789ABCDEF0123456789ABCDEFx/" PDB_NAME;
    struct pdb_reader pdb;
    uint64_t KdDebuggerDataBlock;
    KDDEBUGGER_DATA64 *kdbg;
    uint64_t KdVersionBlock;

    if (argc != 3) {
        eprintf("usage:\n\t%s elf_file dmp_file\n", argv[0]);
        return 1;
    }

    if (QEMU_Elf_init(&qemu_elf, argv[1])) {
        eprintf("Failed to initialize QEMU ELF dump\n");
        return 1;
    }

    if (pa_space_create(&ps, &qemu_elf)) {
        eprintf("Failed to initialize physical address space\n");
        err = 1;
        goto out_elf;
    }

    state = qemu_elf.state[0];
    printf("CPU #0 CR3 is 0x%016"PRIx64"\n", state->cr[3]);

    va_space_create(&vs, &ps, state->cr[3]);
    if (fix_dtb(&vs, &qemu_elf)) {
        eprintf("Failed to find paging base\n");
        err = 1;
        goto out_elf;
    }

    printf("CPU #0 IDT is at 0x%016"PRIx64"\n", state->idt.base);

    if (va_space_rw(&vs, state->idt.base,
                &first_idt_desc, sizeof(first_idt_desc), 0)) {
        eprintf("Failed to get CPU #0 IDT[0]\n");
        err = 1;
        goto out_ps;
    }
    printf("CPU #0 IDT[0] -> 0x%016"PRIx64"\n", idt_desc_addr(first_idt_desc));

    KernBase = idt_desc_addr(first_idt_desc) & ~(ELF2DMP_PAGE_SIZE - 1);
    printf("Searching kernel downwards from 0x%016"PRIx64"...\n", KernBase);

    for (; KernBase >= 0xfffff78000000000; KernBase -= ELF2DMP_PAGE_SIZE) {
        nt_start_addr = va_space_resolve(&vs, KernBase);
        if (!nt_start_addr) {
            continue;
        }

        if (*(uint16_t *)nt_start_addr == 0x5a4d) { /* MZ */
            break;
        }
    }

    if (!nt_start_addr) {
        eprintf("Failed to find NT kernel image\n");
        err = 1;
        goto out_ps;
    }

    printf("KernBase = 0x%016"PRIx64", signature is \'%.2s\'\n", KernBase,
            (char *)nt_start_addr);

    if (pe_get_pdb_symstore_hash(KernBase, nt_start_addr, pdb_hash, &vs)) {
        eprintf("Failed to get PDB symbol store hash\n");
        err = 1;
        goto out_ps;
    }

    sprintf(pdb_url, "%s%s/%s/%s", SYM_URL_BASE, PDB_NAME, pdb_hash, PDB_NAME);
    printf("PDB URL is %s\n", pdb_url);

    if (download_url(PDB_NAME, pdb_url)) {
        eprintf("Failed to download PDB file\n");
        err = 1;
        goto out_ps;
    }

    if (pdb_init_from_file(PDB_NAME, &pdb)) {
        eprintf("Failed to initialize PDB reader\n");
        err = 1;
        goto out_pdb_file;
    }

    if (!SYM_RESOLVE(KernBase, &pdb, KdDebuggerDataBlock) ||
            !SYM_RESOLVE(KernBase, &pdb, KdVersionBlock)) {
        err = 1;
        goto out_pdb;
    }

    kdbg = get_kdbg(KernBase, &pdb, &vs, KdDebuggerDataBlock);
    if (!kdbg) {
        err = 1;
        goto out_pdb;
    }

    if (fill_header(&header, &ps, &vs, KdDebuggerDataBlock, kdbg,
            KdVersionBlock, qemu_elf.state_nr)) {
        err = 1;
        goto out_kdbg;
    }

    if (fill_context(kdbg, &vs, &qemu_elf)) {
        err = 1;
        goto out_kdbg;
    }

    if (write_dump(&ps, &header, argv[2])) {
        eprintf("Failed to save dump\n");
        err = 1;
        goto out_kdbg;
    }

out_kdbg:
    free(kdbg);
out_pdb:
    pdb_exit(&pdb);
out_pdb_file:
    unlink(PDB_NAME);
out_ps:
    pa_space_destroy(&ps);
out_elf:
    QEMU_Elf_exit(&qemu_elf);

    return err;
}
