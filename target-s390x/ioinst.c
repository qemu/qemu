/*
 * I/O instructions for S/390
 *
 * Copyright 2012 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include <sys/types.h>

#include "cpu.h"
#include "ioinst.h"
#include "trace.h"

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

void ioinst_handle_xsch(S390CPU *cpu, uint64_t reg1)
{
    int cssid, ssid, schid, m;
    SubchDev *sch;
    int ret = -ENODEV;
    int cc;

    if (ioinst_disassemble_sch_ident(reg1, &m, &cssid, &ssid, &schid)) {
        program_interrupt(&cpu->env, PGM_OPERAND, 2);
        return;
    }
    trace_ioinst_sch_id("xsch", cssid, ssid, schid);
    sch = css_find_subch(m, cssid, ssid, schid);
    if (sch && css_subch_visible(sch)) {
        ret = css_do_xsch(sch);
    }
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
        cc = 1;
        break;
    }
    setcc(cpu, cc);
}

void ioinst_handle_csch(S390CPU *cpu, uint64_t reg1)
{
    int cssid, ssid, schid, m;
    SubchDev *sch;
    int ret = -ENODEV;
    int cc;

    if (ioinst_disassemble_sch_ident(reg1, &m, &cssid, &ssid, &schid)) {
        program_interrupt(&cpu->env, PGM_OPERAND, 2);
        return;
    }
    trace_ioinst_sch_id("csch", cssid, ssid, schid);
    sch = css_find_subch(m, cssid, ssid, schid);
    if (sch && css_subch_visible(sch)) {
        ret = css_do_csch(sch);
    }
    if (ret == -ENODEV) {
        cc = 3;
    } else {
        cc = 0;
    }
    setcc(cpu, cc);
}

void ioinst_handle_hsch(S390CPU *cpu, uint64_t reg1)
{
    int cssid, ssid, schid, m;
    SubchDev *sch;
    int ret = -ENODEV;
    int cc;

    if (ioinst_disassemble_sch_ident(reg1, &m, &cssid, &ssid, &schid)) {
        program_interrupt(&cpu->env, PGM_OPERAND, 2);
        return;
    }
    trace_ioinst_sch_id("hsch", cssid, ssid, schid);
    sch = css_find_subch(m, cssid, ssid, schid);
    if (sch && css_subch_visible(sch)) {
        ret = css_do_hsch(sch);
    }
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
        cc = 1;
        break;
    }
    setcc(cpu, cc);
}

static int ioinst_schib_valid(SCHIB *schib)
{
    if ((schib->pmcw.flags & PMCW_FLAGS_MASK_INVALID) ||
        (schib->pmcw.chars & PMCW_CHARS_MASK_INVALID)) {
        return 0;
    }
    /* Disallow extended measurements for now. */
    if (schib->pmcw.chars & PMCW_CHARS_MASK_XMWME) {
        return 0;
    }
    return 1;
}

void ioinst_handle_msch(S390CPU *cpu, uint64_t reg1, uint32_t ipb)
{
    int cssid, ssid, schid, m;
    SubchDev *sch;
    SCHIB *schib;
    uint64_t addr;
    int ret = -ENODEV;
    int cc;
    hwaddr len = sizeof(*schib);
    CPUS390XState *env = &cpu->env;

    addr = decode_basedisp_s(env, ipb);
    if (addr & 3) {
        program_interrupt(env, PGM_SPECIFICATION, 2);
        return;
    }
    schib = s390_cpu_physical_memory_map(env, addr, &len, 0);
    if (!schib || len != sizeof(*schib)) {
        program_interrupt(env, PGM_ADDRESSING, 2);
        goto out;
    }
    if (ioinst_disassemble_sch_ident(reg1, &m, &cssid, &ssid, &schid) ||
        !ioinst_schib_valid(schib)) {
        program_interrupt(env, PGM_OPERAND, 2);
        goto out;
    }
    trace_ioinst_sch_id("msch", cssid, ssid, schid);
    sch = css_find_subch(m, cssid, ssid, schid);
    if (sch && css_subch_visible(sch)) {
        ret = css_do_msch(sch, schib);
    }
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
        cc = 1;
        break;
    }
    setcc(cpu, cc);

out:
    s390_cpu_physical_memory_unmap(env, schib, len, 0);
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
    if ((orb->cpa & HIGH_ORDER_BIT) != 0) {
        return 0;
    }
    return 1;
}

void ioinst_handle_ssch(S390CPU *cpu, uint64_t reg1, uint32_t ipb)
{
    int cssid, ssid, schid, m;
    SubchDev *sch;
    ORB *orig_orb, orb;
    uint64_t addr;
    int ret = -ENODEV;
    int cc;
    hwaddr len = sizeof(*orig_orb);
    CPUS390XState *env = &cpu->env;

    addr = decode_basedisp_s(env, ipb);
    if (addr & 3) {
        program_interrupt(env, PGM_SPECIFICATION, 2);
        return;
    }
    orig_orb = s390_cpu_physical_memory_map(env, addr, &len, 0);
    if (!orig_orb || len != sizeof(*orig_orb)) {
        program_interrupt(env, PGM_ADDRESSING, 2);
        goto out;
    }
    copy_orb_from_guest(&orb, orig_orb);
    if (ioinst_disassemble_sch_ident(reg1, &m, &cssid, &ssid, &schid) ||
        !ioinst_orb_valid(&orb)) {
        program_interrupt(env, PGM_OPERAND, 2);
        goto out;
    }
    trace_ioinst_sch_id("ssch", cssid, ssid, schid);
    sch = css_find_subch(m, cssid, ssid, schid);
    if (sch && css_subch_visible(sch)) {
        ret = css_do_ssch(sch, &orb);
    }
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
        cc = 1;
        break;
    }
    setcc(cpu, cc);

out:
    s390_cpu_physical_memory_unmap(env, orig_orb, len, 0);
}

void ioinst_handle_stcrw(S390CPU *cpu, uint32_t ipb)
{
    CRW *crw;
    uint64_t addr;
    int cc;
    hwaddr len = sizeof(*crw);
    CPUS390XState *env = &cpu->env;

    addr = decode_basedisp_s(env, ipb);
    if (addr & 3) {
        program_interrupt(env, PGM_SPECIFICATION, 2);
        return;
    }
    crw = s390_cpu_physical_memory_map(env, addr, &len, 1);
    if (!crw || len != sizeof(*crw)) {
        program_interrupt(env, PGM_ADDRESSING, 2);
        goto out;
    }
    cc = css_do_stcrw(crw);
    /* 0 - crw stored, 1 - zeroes stored */
    setcc(cpu, cc);

out:
    s390_cpu_physical_memory_unmap(env, crw, len, 1);
}

void ioinst_handle_stsch(S390CPU *cpu, uint64_t reg1, uint32_t ipb)
{
    int cssid, ssid, schid, m;
    SubchDev *sch;
    uint64_t addr;
    int cc;
    SCHIB *schib;
    hwaddr len = sizeof(*schib);
    CPUS390XState *env = &cpu->env;

    addr = decode_basedisp_s(env, ipb);
    if (addr & 3) {
        program_interrupt(env, PGM_SPECIFICATION, 2);
        return;
    }
    schib = s390_cpu_physical_memory_map(env, addr, &len, 1);
    if (!schib || len != sizeof(*schib)) {
        program_interrupt(env, PGM_ADDRESSING, 2);
        goto out;
    }

    if (ioinst_disassemble_sch_ident(reg1, &m, &cssid, &ssid, &schid)) {
        program_interrupt(env, PGM_OPERAND, 2);
        goto out;
    }
    trace_ioinst_sch_id("stsch", cssid, ssid, schid);
    sch = css_find_subch(m, cssid, ssid, schid);
    if (sch) {
        if (css_subch_visible(sch)) {
            css_do_stsch(sch, schib);
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
            memset(schib, 0, sizeof(*schib));
            cc = 0;
        }
    }
    setcc(cpu, cc);

out:
    s390_cpu_physical_memory_unmap(env, schib, len, 1);
}

int ioinst_handle_tsch(CPUS390XState *env, uint64_t reg1, uint32_t ipb)
{
    int cssid, ssid, schid, m;
    SubchDev *sch;
    IRB *irb;
    uint64_t addr;
    int ret = -ENODEV;
    int cc;
    hwaddr len = sizeof(*irb);

    if (ioinst_disassemble_sch_ident(reg1, &m, &cssid, &ssid, &schid)) {
        program_interrupt(env, PGM_OPERAND, 2);
        return -EIO;
    }
    trace_ioinst_sch_id("tsch", cssid, ssid, schid);
    addr = decode_basedisp_s(env, ipb);
    if (addr & 3) {
        program_interrupt(env, PGM_SPECIFICATION, 2);
        return -EIO;
    }
    irb = s390_cpu_physical_memory_map(env, addr, &len, 1);
    if (!irb || len != sizeof(*irb)) {
        program_interrupt(env, PGM_ADDRESSING, 2);
        cc = -EIO;
        goto out;
    }
    sch = css_find_subch(m, cssid, ssid, schid);
    if (sch && css_subch_visible(sch)) {
        ret = css_do_tsch(sch, irb);
        /* 0 - status pending, 1 - not status pending */
        cc = ret;
    } else {
        cc = 3;
    }
out:
    s390_cpu_physical_memory_unmap(env, irb, sizeof(*irb), 1);
    return cc;
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
    char data[0];
} QEMU_PACKED ChscResp;

#define CHSC_MIN_RESP_LEN 0x0008

#define CHSC_SCPD 0x0002
#define CHSC_SCSC 0x0010
#define CHSC_SDA  0x0031

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
    general_chars[1] = cpu_to_be32(0x00059000);

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

static void ioinst_handle_chsc_unimplemented(ChscResp *res)
{
    res->len = cpu_to_be16(CHSC_MIN_RESP_LEN);
    res->code = cpu_to_be16(0x0004);
    res->param = 0;
}

void ioinst_handle_chsc(S390CPU *cpu, uint32_t ipb)
{
    ChscReq *req;
    ChscResp *res;
    uint64_t addr;
    int reg;
    uint16_t len;
    uint16_t command;
    hwaddr map_size = TARGET_PAGE_SIZE;
    CPUS390XState *env = &cpu->env;

    trace_ioinst("chsc");
    reg = (ipb >> 20) & 0x00f;
    addr = env->regs[reg];
    /* Page boundary? */
    if (addr & 0xfff) {
        program_interrupt(env, PGM_SPECIFICATION, 2);
        return;
    }
    req = s390_cpu_physical_memory_map(env, addr, &map_size, 1);
    if (!req || map_size != TARGET_PAGE_SIZE) {
        program_interrupt(env, PGM_ADDRESSING, 2);
        goto out;
    }
    len = be16_to_cpu(req->len);
    /* Length field valid? */
    if ((len < 16) || (len > 4088) || (len & 7)) {
        program_interrupt(env, PGM_OPERAND, 2);
        goto out;
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
    default:
        ioinst_handle_chsc_unimplemented(res);
        break;
    }

    setcc(cpu, 0);    /* Command execution complete */
out:
    s390_cpu_physical_memory_unmap(env, req, map_size, 1);
}

int ioinst_handle_tpi(CPUS390XState *env, uint32_t ipb)
{
    uint64_t addr;
    int lowcore;
    IOIntCode *int_code;
    hwaddr len, orig_len;
    int ret;

    trace_ioinst("tpi");
    addr = decode_basedisp_s(env, ipb);
    if (addr & 3) {
        program_interrupt(env, PGM_SPECIFICATION, 2);
        return -EIO;
    }

    lowcore = addr ? 0 : 1;
    len = lowcore ? 8 /* two words */ : 12 /* three words */;
    orig_len = len;
    int_code = s390_cpu_physical_memory_map(env, addr, &len, 1);
    if (!int_code || (len != orig_len)) {
        program_interrupt(env, PGM_ADDRESSING, 2);
        ret = -EIO;
        goto out;
    }
    ret = css_do_tpi(int_code, lowcore);
out:
    s390_cpu_physical_memory_unmap(env, int_code, len, 1);
    return ret;
}

#define SCHM_REG1_RES(_reg) (_reg & 0x000000000ffffffc)
#define SCHM_REG1_MBK(_reg) ((_reg & 0x00000000f0000000) >> 28)
#define SCHM_REG1_UPD(_reg) ((_reg & 0x0000000000000002) >> 1)
#define SCHM_REG1_DCT(_reg) (_reg & 0x0000000000000001)

void ioinst_handle_schm(S390CPU *cpu, uint64_t reg1, uint64_t reg2,
                        uint32_t ipb)
{
    uint8_t mbk;
    int update;
    int dct;
    CPUS390XState *env = &cpu->env;

    trace_ioinst("schm");

    if (SCHM_REG1_RES(reg1)) {
        program_interrupt(env, PGM_OPERAND, 2);
        return;
    }

    mbk = SCHM_REG1_MBK(reg1);
    update = SCHM_REG1_UPD(reg1);
    dct = SCHM_REG1_DCT(reg1);

    if (update && (reg2 & 0x000000000000001f)) {
        program_interrupt(env, PGM_OPERAND, 2);
        return;
    }

    css_do_schm(mbk, update, dct, update ? reg2 : 0);
}

void ioinst_handle_rsch(S390CPU *cpu, uint64_t reg1)
{
    int cssid, ssid, schid, m;
    SubchDev *sch;
    int ret = -ENODEV;
    int cc;

    if (ioinst_disassemble_sch_ident(reg1, &m, &cssid, &ssid, &schid)) {
        program_interrupt(&cpu->env, PGM_OPERAND, 2);
        return;
    }
    trace_ioinst_sch_id("rsch", cssid, ssid, schid);
    sch = css_find_subch(m, cssid, ssid, schid);
    if (sch && css_subch_visible(sch)) {
        ret = css_do_rsch(sch);
    }
    switch (ret) {
    case -ENODEV:
        cc = 3;
        break;
    case -EINVAL:
        cc = 2;
        break;
    case 0:
        cc = 0;
        break;
    default:
        cc = 1;
        break;
    }
    setcc(cpu, cc);
}

#define RCHP_REG1_RES(_reg) (_reg & 0x00000000ff00ff00)
#define RCHP_REG1_CSSID(_reg) ((_reg & 0x0000000000ff0000) >> 16)
#define RCHP_REG1_CHPID(_reg) (_reg & 0x00000000000000ff)
void ioinst_handle_rchp(S390CPU *cpu, uint64_t reg1)
{
    int cc;
    uint8_t cssid;
    uint8_t chpid;
    int ret;
    CPUS390XState *env = &cpu->env;

    if (RCHP_REG1_RES(reg1)) {
        program_interrupt(env, PGM_OPERAND, 2);
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
        program_interrupt(env, PGM_OPERAND, 2);
        return;
    }
    setcc(cpu, cc);
}

#define SAL_REG1_INVALID(_reg) (_reg & 0x0000000080000000)
void ioinst_handle_sal(S390CPU *cpu, uint64_t reg1)
{
    /* We do not provide address limit checking, so let's suppress it. */
    if (SAL_REG1_INVALID(reg1) || reg1 & 0x000000000000ffff) {
        program_interrupt(&cpu->env, PGM_OPERAND, 2);
    }
}
