/*
 * I/O instructions for S/390
 *
 * Copyright 2012, 2015 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "internal.h"
#include "hw/s390x/ioinst.h"
#include "trace.h"
#include "hw/s390x/s390-pci-bus.h"

int ioinst_disassemble_sch_ident(uint32_t value, int *m, int *cssid, int *ssid,
                                 int *schid)
{
    if (!IOINST_SCHID_ONE(value)) {
        return -EINVAL;
    }
    if (!IOINST_SCHID_M(value)) {
        if (IOINST_SCHID_CSSID(value)) {
            return -EINVAL;
        }
        *cssid = 0;
        *m = 0;
    } else {
        *cssid = IOINST_SCHID_CSSID(value);
        *m = 1;
    }
    *ssid = IOINST_SCHID_SSID(value);
    *schid = IOINST_SCHID_NR(value);
    return 0;
}

void ioinst_handle_xsch(S390CPU *cpu, uint64_t reg1, uintptr_t ra)
{
    int cssid, ssid, schid, m;
    SubchDev *sch;

    if (ioinst_disassemble_sch_ident(reg1, &m, &cssid, &ssid, &schid)) {
        s390_program_interrupt(&cpu->env, PGM_OPERAND, ra);
        return;
    }
    trace_ioinst_sch_id("xsch", cssid, ssid, schid);
    sch = css_find_subch(m, cssid, ssid, schid);
    if (!sch || !css_subch_visible(sch)) {
        setcc(cpu, 3);
        return;
    }
    setcc(cpu, css_do_xsch(sch));
}

void ioinst_handle_csch(S390CPU *cpu, uint64_t reg1, uintptr_t ra)
{
    int cssid, ssid, schid, m;
    SubchDev *sch;

    if (ioinst_disassemble_sch_ident(reg1, &m, &cssid, &ssid, &schid)) {
        s390_program_interrupt(&cpu->env, PGM_OPERAND, ra);
        return;
    }
    trace_ioinst_sch_id("csch", cssid, ssid, schid);
    sch = css_find_subch(m, cssid, ssid, schid);
    if (!sch || !css_subch_visible(sch)) {
        setcc(cpu, 3);
        return;
    }
    setcc(cpu, css_do_csch(sch));
}

void ioinst_handle_hsch(S390CPU *cpu, uint64_t reg1, uintptr_t ra)
{
    int cssid, ssid, schid, m;
    SubchDev *sch;

    if (ioinst_disassemble_sch_ident(reg1, &m, &cssid, &ssid, &schid)) {
        s390_program_interrupt(&cpu->env, PGM_OPERAND, ra);
        return;
    }
    trace_ioinst_sch_id("hsch", cssid, ssid, schid);
    sch = css_find_subch(m, cssid, ssid, schid);
    if (!sch || !css_subch_visible(sch)) {
        setcc(cpu, 3);
        return;
    }
    setcc(cpu, css_do_hsch(sch));
}

static int ioinst_schib_valid(SCHIB *schib)
{
    if ((be16_to_cpu(schib->pmcw.flags) & PMCW_FLAGS_MASK_INVALID) ||
        (be32_to_cpu(schib->pmcw.chars) & PMCW_CHARS_MASK_INVALID)) {
        return 0;
    }
    /* Disallow extended measurements for now. */
    if (be32_to_cpu(schib->pmcw.chars) & PMCW_CHARS_MASK_XMWME) {
        return 0;
    }
    return 1;
}

void ioinst_handle_msch(S390CPU *cpu, uint64_t reg1, uint32_t ipb, uintptr_t ra)
{
    int cssid, ssid, schid, m;
    SubchDev *sch;
    SCHIB schib;
    uint64_t addr;
    CPUS390XState *env = &cpu->env;
    uint8_t ar;

    addr = decode_basedisp_s(env, ipb, &ar);
    if (addr & 3) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }
    if (s390_cpu_virt_mem_read(cpu, addr, ar, &schib, sizeof(schib))) {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
        return;
    }
    if (ioinst_disassemble_sch_ident(reg1, &m, &cssid, &ssid, &schid) ||
        !ioinst_schib_valid(&schib)) {
        s390_program_interrupt(env, PGM_OPERAND, ra);
        return;
    }
    trace_ioinst_sch_id("msch", cssid, ssid, schid);
    sch = css_find_subch(m, cssid, ssid, schid);
    if (!sch || !css_subch_visible(sch)) {
        setcc(cpu, 3);
        return;
    }
    setcc(cpu, css_do_msch(sch, &schib));
}

static void copy_orb_from_guest(ORB *dest, const ORB *src)
{
    dest->intparm = be32_to_cpu(src->intparm);
    dest->ctrl0 = be16_to_cpu(src->ctrl0);
    dest->lpm = src->lpm;
    dest->ctrl1 = src->ctrl1;
    dest->cpa = be32_to_cpu(src->cpa);
}

static int ioinst_orb_valid(ORB *orb)
{
    if ((orb->ctrl0 & ORB_CTRL0_MASK_INVALID) ||
        (orb->ctrl1 & ORB_CTRL1_MASK_INVALID)) {
        return 0;
    }
    /* We don't support MIDA. */
    if (orb->ctrl1 & ORB_CTRL1_MASK_MIDAW) {
        return 0;
    }
    if ((orb->cpa & HIGH_ORDER_BIT) != 0) {
        return 0;
    }
    return 1;
}

void ioinst_handle_ssch(S390CPU *cpu, uint64_t reg1, uint32_t ipb, uintptr_t ra)
{
    int cssid, ssid, schid, m;
    SubchDev *sch;
    ORB orig_orb, orb;
    uint64_t addr;
    CPUS390XState *env = &cpu->env;
    uint8_t ar;

    addr = decode_basedisp_s(env, ipb, &ar);
    if (addr & 3) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }
    if (s390_cpu_virt_mem_read(cpu, addr, ar, &orig_orb, sizeof(orb))) {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
        return;
    }
    copy_orb_from_guest(&orb, &orig_orb);
    if (ioinst_disassemble_sch_ident(reg1, &m, &cssid, &ssid, &schid) ||
        !ioinst_orb_valid(&orb)) {
        s390_program_interrupt(env, PGM_OPERAND, ra);
        return;
    }
    trace_ioinst_sch_id("ssch", cssid, ssid, schid);
    sch = css_find_subch(m, cssid, ssid, schid);
    if (!sch || !css_subch_visible(sch)) {
        setcc(cpu, 3);
        return;
    }
    setcc(cpu, css_do_ssch(sch, &orb));
}

void ioinst_handle_stcrw(S390CPU *cpu, uint32_t ipb, uintptr_t ra)
{
    CRW crw;
    uint64_t addr;
    int cc;
    CPUS390XState *env = &cpu->env;
    uint8_t ar;

    addr = decode_basedisp_s(env, ipb, &ar);
    if (addr & 3) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    cc = css_do_stcrw(&crw);
    /* 0 - crw stored, 1 - zeroes stored */

    if (s390_cpu_virt_mem_write(cpu, addr, ar, &crw, sizeof(crw)) == 0) {
        setcc(cpu, cc);
    } else {
        if (cc == 0) {
            /* Write failed: requeue CRW since STCRW is suppressing */
            css_undo_stcrw(&crw);
        }
        s390_cpu_virt_mem_handle_exc(cpu, ra);
    }
}

void ioinst_handle_stsch(S390CPU *cpu, uint64_t reg1, uint32_t ipb,
                         uintptr_t ra)
{
    int cssid, ssid, schid, m;
    SubchDev *sch;
    uint64_t addr;
    int cc;
    SCHIB schib;
    CPUS390XState *env = &cpu->env;
    uint8_t ar;

    addr = decode_basedisp_s(env, ipb, &ar);
    if (addr & 3) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    if (ioinst_disassemble_sch_ident(reg1, &m, &cssid, &ssid, &schid)) {
        /*
         * As operand exceptions have a lower priority than access exceptions,
         * we check whether the memory area is writeable (injecting the
         * access execption if it is not) first.
         */
        if (!s390_cpu_virt_mem_check_write(cpu, addr, ar, sizeof(schib))) {
            s390_program_interrupt(env, PGM_OPERAND, ra);
        } else {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
        }
        return;
    }
    trace_ioinst_sch_id("stsch", cssid, ssid, schid);
    sch = css_find_subch(m, cssid, ssid, schid);
    if (sch) {
        if (css_subch_visible(sch)) {
            css_do_stsch(sch, &schib);
            cc = 0;
        } else {
            /* Indicate no more subchannels in this css/ss */
            cc = 3;
        }
    } else {
        if (css_schid_final(m, cssid, ssid, schid)) {
            cc = 3; /* No more subchannels in this css/ss */
        } else {
            /* Store an empty schib. */
            memset(&schib, 0, sizeof(schib));
            cc = 0;
        }
    }
    if (cc != 3) {
        if (s390_cpu_virt_mem_write(cpu, addr, ar, &schib,
                                    sizeof(schib)) != 0) {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
            return;
        }
    } else {
        /* Access exceptions have a higher priority than cc3 */
        if (s390_cpu_virt_mem_check_write(cpu, addr, ar, sizeof(schib)) != 0) {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
            return;
        }
    }
    setcc(cpu, cc);
}

int ioinst_handle_tsch(S390CPU *cpu, uint64_t reg1, uint32_t ipb, uintptr_t ra)
{
    CPUS390XState *env = &cpu->env;
    int cssid, ssid, schid, m;
    SubchDev *sch;
    IRB irb;
    uint64_t addr;
    int cc, irb_len;
    uint8_t ar;

    if (ioinst_disassemble_sch_ident(reg1, &m, &cssid, &ssid, &schid)) {
        s390_program_interrupt(env, PGM_OPERAND, ra);
        return -EIO;
    }
    trace_ioinst_sch_id("tsch", cssid, ssid, schid);
    addr = decode_basedisp_s(env, ipb, &ar);
    if (addr & 3) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return -EIO;
    }

    sch = css_find_subch(m, cssid, ssid, schid);
    if (sch && css_subch_visible(sch)) {
        cc = css_do_tsch_get_irb(sch, &irb, &irb_len);
    } else {
        cc = 3;
    }
    /* 0 - status pending, 1 - not status pending, 3 - not operational */
    if (cc != 3) {
        if (s390_cpu_virt_mem_write(cpu, addr, ar, &irb, irb_len) != 0) {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
            return -EFAULT;
        }
        css_do_tsch_update_subch(sch);
    } else {
        irb_len = sizeof(irb) - sizeof(irb.emw);
        /* Access exceptions have a higher priority than cc3 */
        if (s390_cpu_virt_mem_check_write(cpu, addr, ar, irb_len) != 0) {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
            return -EFAULT;
        }
    }

    setcc(cpu, cc);
    return 0;
}

typedef struct ChscReq {
    uint16_t len;
    uint16_t command;
    uint32_t param0;
    uint32_t param1;
    uint32_t param2;
} QEMU_PACKED ChscReq;

typedef struct ChscResp {
    uint16_t len;
    uint16_t code;
    uint32_t param;
    char data[];
} QEMU_PACKED ChscResp;

#define CHSC_MIN_RESP_LEN 0x0008

#define CHSC_SCPD 0x0002
#define CHSC_SCSC 0x0010
#define CHSC_SDA  0x0031
#define CHSC_SEI  0x000e

#define CHSC_SCPD_0_M 0x20000000
#define CHSC_SCPD_0_C 0x10000000
#define CHSC_SCPD_0_FMT 0x0f000000
#define CHSC_SCPD_0_CSSID 0x00ff0000
#define CHSC_SCPD_0_RFMT 0x00000f00
#define CHSC_SCPD_0_RES 0xc000f000
#define CHSC_SCPD_1_RES 0xffffff00
#define CHSC_SCPD_01_CHPID 0x000000ff
static void ioinst_handle_chsc_scpd(ChscReq *req, ChscResp *res)
{
    uint16_t len = be16_to_cpu(req->len);
    uint32_t param0 = be32_to_cpu(req->param0);
    uint32_t param1 = be32_to_cpu(req->param1);
    uint16_t resp_code;
    int rfmt;
    uint16_t cssid;
    uint8_t f_chpid, l_chpid;
    int desc_size;
    int m;

    rfmt = (param0 & CHSC_SCPD_0_RFMT) >> 8;
    if ((rfmt == 0) ||  (rfmt == 1)) {
        rfmt = !!(param0 & CHSC_SCPD_0_C);
    }
    if ((len != 0x0010) || (param0 & CHSC_SCPD_0_RES) ||
        (param1 & CHSC_SCPD_1_RES) || req->param2) {
        resp_code = 0x0003;
        goto out_err;
    }
    if (param0 & CHSC_SCPD_0_FMT) {
        resp_code = 0x0007;
        goto out_err;
    }
    cssid = (param0 & CHSC_SCPD_0_CSSID) >> 16;
    m = param0 & CHSC_SCPD_0_M;
    if (cssid != 0) {
        if (!m || !css_present(cssid)) {
            resp_code = 0x0008;
            goto out_err;
        }
    }
    f_chpid = param0 & CHSC_SCPD_01_CHPID;
    l_chpid = param1 & CHSC_SCPD_01_CHPID;
    if (l_chpid < f_chpid) {
        resp_code = 0x0003;
        goto out_err;
    }
    /* css_collect_chp_desc() is endian-aware */
    desc_size = css_collect_chp_desc(m, cssid, f_chpid, l_chpid, rfmt,
                                     &res->data);
    res->code = cpu_to_be16(0x0001);
    res->len = cpu_to_be16(8 + desc_size);
    res->param = cpu_to_be32(rfmt);
    return;

  out_err:
    res->code = cpu_to_be16(resp_code);
    res->len = cpu_to_be16(CHSC_MIN_RESP_LEN);
    res->param = cpu_to_be32(rfmt);
}

#define CHSC_SCSC_0_M 0x20000000
#define CHSC_SCSC_0_FMT 0x000f0000
#define CHSC_SCSC_0_CSSID 0x0000ff00
#define CHSC_SCSC_0_RES 0xdff000ff
static void ioinst_handle_chsc_scsc(ChscReq *req, ChscResp *res)
{
    uint16_t len = be16_to_cpu(req->len);
    uint32_t param0 = be32_to_cpu(req->param0);
    uint8_t cssid;
    uint16_t resp_code;
    uint32_t general_chars[510];
    uint32_t chsc_chars[508];

    if (len != 0x0010) {
        resp_code = 0x0003;
        goto out_err;
    }

    if (param0 & CHSC_SCSC_0_FMT) {
        resp_code = 0x0007;
        goto out_err;
    }
    cssid = (param0 & CHSC_SCSC_0_CSSID) >> 8;
    if (cssid != 0) {
        if (!(param0 & CHSC_SCSC_0_M) || !css_present(cssid)) {
            resp_code = 0x0008;
            goto out_err;
        }
    }
    if ((param0 & CHSC_SCSC_0_RES) || req->param1 || req->param2) {
        resp_code = 0x0003;
        goto out_err;
    }
    res->code = cpu_to_be16(0x0001);
    res->len = cpu_to_be16(4080);
    res->param = 0;

    memset(general_chars, 0, sizeof(general_chars));
    memset(chsc_chars, 0, sizeof(chsc_chars));

    general_chars[0] = cpu_to_be32(0x03000000);
    general_chars[1] = cpu_to_be32(0x00079000);
    general_chars[3] = cpu_to_be32(0x00080000);

    chsc_chars[0] = cpu_to_be32(0x40000000);
    chsc_chars[3] = cpu_to_be32(0x00040000);

    memcpy(res->data, general_chars, sizeof(general_chars));
    memcpy(res->data + sizeof(general_chars), chsc_chars, sizeof(chsc_chars));
    return;

  out_err:
    res->code = cpu_to_be16(resp_code);
    res->len = cpu_to_be16(CHSC_MIN_RESP_LEN);
    res->param = 0;
}

#define CHSC_SDA_0_FMT 0x0f000000
#define CHSC_SDA_0_OC 0x0000ffff
#define CHSC_SDA_0_RES 0xf0ff0000
#define CHSC_SDA_OC_MCSSE 0x0
#define CHSC_SDA_OC_MSS 0x2
static void ioinst_handle_chsc_sda(ChscReq *req, ChscResp *res)
{
    uint16_t resp_code = 0x0001;
    uint16_t len = be16_to_cpu(req->len);
    uint32_t param0 = be32_to_cpu(req->param0);
    uint16_t oc;
    int ret;

    if ((len != 0x0400) || (param0 & CHSC_SDA_0_RES)) {
        resp_code = 0x0003;
        goto out;
    }

    if (param0 & CHSC_SDA_0_FMT) {
        resp_code = 0x0007;
        goto out;
    }

    oc = param0 & CHSC_SDA_0_OC;
    switch (oc) {
    case CHSC_SDA_OC_MCSSE:
        ret = css_enable_mcsse();
        if (ret == -EINVAL) {
            resp_code = 0x0101;
            goto out;
        }
        break;
    case CHSC_SDA_OC_MSS:
        ret = css_enable_mss();
        if (ret == -EINVAL) {
            resp_code = 0x0101;
            goto out;
        }
        break;
    default:
        resp_code = 0x0003;
        goto out;
    }

out:
    res->code = cpu_to_be16(resp_code);
    res->len = cpu_to_be16(CHSC_MIN_RESP_LEN);
    res->param = 0;
}

static int chsc_sei_nt0_get_event(void *res)
{
    /* no events yet */
    return 1;
}

static int chsc_sei_nt0_have_event(void)
{
    /* no events yet */
    return 0;
}

static int chsc_sei_nt2_get_event(void *res)
{
    if (s390_has_feat(S390_FEAT_ZPCI)) {
        return pci_chsc_sei_nt2_get_event(res);
    }
    return 1;
}

static int chsc_sei_nt2_have_event(void)
{
    if (s390_has_feat(S390_FEAT_ZPCI)) {
        return pci_chsc_sei_nt2_have_event();
    }
    return 0;
}

#define CHSC_SEI_NT0    (1ULL << 63)
#define CHSC_SEI_NT2    (1ULL << 61)
static void ioinst_handle_chsc_sei(ChscReq *req, ChscResp *res)
{
    uint64_t selection_mask = ldq_p(&req->param1);
    uint8_t *res_flags = (uint8_t *)res->data;
    int have_event = 0;
    int have_more = 0;

    /* regarding architecture nt0 can not be masked */
    have_event = !chsc_sei_nt0_get_event(res);
    have_more = chsc_sei_nt0_have_event();

    if (selection_mask & CHSC_SEI_NT2) {
        if (!have_event) {
            have_event = !chsc_sei_nt2_get_event(res);
        }

        if (!have_more) {
            have_more = chsc_sei_nt2_have_event();
        }
    }

    if (have_event) {
        res->code = cpu_to_be16(0x0001);
        if (have_more) {
            (*res_flags) |= 0x80;
        } else {
            (*res_flags) &= ~0x80;
            css_clear_sei_pending();
        }
    } else {
        res->code = cpu_to_be16(0x0005);
        res->len = cpu_to_be16(CHSC_MIN_RESP_LEN);
    }
}

static void ioinst_handle_chsc_unimplemented(ChscResp *res)
{
    res->len = cpu_to_be16(CHSC_MIN_RESP_LEN);
    res->code = cpu_to_be16(0x0004);
    res->param = 0;
}

void ioinst_handle_chsc(S390CPU *cpu, uint32_t ipb, uintptr_t ra)
{
    ChscReq *req;
    ChscResp *res;
    uint64_t addr;
    int reg;
    uint16_t len;
    uint16_t command;
    CPUS390XState *env = &cpu->env;
    uint8_t buf[TARGET_PAGE_SIZE];

    trace_ioinst("chsc");
    reg = (ipb >> 20) & 0x00f;
    addr = env->regs[reg];
    /* Page boundary? */
    if (addr & 0xfff) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }
    /*
     * Reading sizeof(ChscReq) bytes is currently enough for all of our
     * present CHSC sub-handlers ... if we ever need more, we should take
     * care of req->len here first.
     */
    if (s390_cpu_virt_mem_read(cpu, addr, reg, buf, sizeof(ChscReq))) {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
        return;
    }
    req = (ChscReq *)buf;
    len = be16_to_cpu(req->len);
    /* Length field valid? */
    if ((len < 16) || (len > 4088) || (len & 7)) {
        s390_program_interrupt(env, PGM_OPERAND, ra);
        return;
    }
    memset((char *)req + len, 0, TARGET_PAGE_SIZE - len);
    res = (void *)((char *)req + len);
    command = be16_to_cpu(req->command);
    trace_ioinst_chsc_cmd(command, len);
    switch (command) {
    case CHSC_SCSC:
        ioinst_handle_chsc_scsc(req, res);
        break;
    case CHSC_SCPD:
        ioinst_handle_chsc_scpd(req, res);
        break;
    case CHSC_SDA:
        ioinst_handle_chsc_sda(req, res);
        break;
    case CHSC_SEI:
        ioinst_handle_chsc_sei(req, res);
        break;
    default:
        ioinst_handle_chsc_unimplemented(res);
        break;
    }

    if (!s390_cpu_virt_mem_write(cpu, addr + len, reg, res,
                                 be16_to_cpu(res->len))) {
        setcc(cpu, 0);    /* Command execution complete */
    } else {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
    }
}

#define SCHM_REG1_RES(_reg) (_reg & 0x000000000ffffffc)
#define SCHM_REG1_MBK(_reg) ((_reg & 0x00000000f0000000) >> 28)
#define SCHM_REG1_UPD(_reg) ((_reg & 0x0000000000000002) >> 1)
#define SCHM_REG1_DCT(_reg) (_reg & 0x0000000000000001)

void ioinst_handle_schm(S390CPU *cpu, uint64_t reg1, uint64_t reg2,
                        uint32_t ipb, uintptr_t ra)
{
    uint8_t mbk;
    int update;
    int dct;
    CPUS390XState *env = &cpu->env;

    trace_ioinst("schm");

    if (SCHM_REG1_RES(reg1)) {
        s390_program_interrupt(env, PGM_OPERAND, ra);
        return;
    }

    mbk = SCHM_REG1_MBK(reg1);
    update = SCHM_REG1_UPD(reg1);
    dct = SCHM_REG1_DCT(reg1);

    if (update && (reg2 & 0x000000000000001f)) {
        s390_program_interrupt(env, PGM_OPERAND, ra);
        return;
    }

    css_do_schm(mbk, update, dct, update ? reg2 : 0);
}

void ioinst_handle_rsch(S390CPU *cpu, uint64_t reg1, uintptr_t ra)
{
    int cssid, ssid, schid, m;
    SubchDev *sch;

    if (ioinst_disassemble_sch_ident(reg1, &m, &cssid, &ssid, &schid)) {
        s390_program_interrupt(&cpu->env, PGM_OPERAND, ra);
        return;
    }
    trace_ioinst_sch_id("rsch", cssid, ssid, schid);
    sch = css_find_subch(m, cssid, ssid, schid);
    if (!sch || !css_subch_visible(sch)) {
        setcc(cpu, 3);
        return;
    }
    setcc(cpu, css_do_rsch(sch));
}

#define RCHP_REG1_RES(_reg) (_reg & 0x00000000ff00ff00)
#define RCHP_REG1_CSSID(_reg) ((_reg & 0x0000000000ff0000) >> 16)
#define RCHP_REG1_CHPID(_reg) (_reg & 0x00000000000000ff)
void ioinst_handle_rchp(S390CPU *cpu, uint64_t reg1, uintptr_t ra)
{
    int cc;
    uint8_t cssid;
    uint8_t chpid;
    int ret;
    CPUS390XState *env = &cpu->env;

    if (RCHP_REG1_RES(reg1)) {
        s390_program_interrupt(env, PGM_OPERAND, ra);
        return;
    }

    cssid = RCHP_REG1_CSSID(reg1);
    chpid = RCHP_REG1_CHPID(reg1);

    trace_ioinst_chp_id("rchp", cssid, chpid);

    ret = css_do_rchp(cssid, chpid);

    switch (ret) {
    case -ENODEV:
        cc = 3;
        break;
    case -EBUSY:
        cc = 2;
        break;
    case 0:
        cc = 0;
        break;
    default:
        /* Invalid channel subsystem. */
        s390_program_interrupt(env, PGM_OPERAND, ra);
        return;
    }
    setcc(cpu, cc);
}

#define SAL_REG1_INVALID(_reg) (_reg & 0x0000000080000000)
void ioinst_handle_sal(S390CPU *cpu, uint64_t reg1, uintptr_t ra)
{
    /* We do not provide address limit checking, so let's suppress it. */
    if (SAL_REG1_INVALID(reg1) || reg1 & 0x000000000000ffff) {
        s390_program_interrupt(&cpu->env, PGM_OPERAND, ra);
    }
}
