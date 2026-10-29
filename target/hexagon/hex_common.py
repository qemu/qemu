#!/usr/bin/env python3

##
##  Copyright(c) 2019-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
##
##  This program is free software; you can redistribute it and/or modify
##  it under the terms of the GNU General Public License as published by
##  the Free Software Foundation; either version 2 of the License, or
##  (at your option) any later version.
##
##  This program is distributed in the hope that it will be useful,
##  but WITHOUT ANY WARRANTY; without even the implied warranty of
##  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
##  GNU General Public License for more details.
##
##  You should have received a copy of the GNU General Public License
##  along with this program; if not, see <http://www.gnu.org/licenses/>.
##

import sys
import re
import string
import textwrap
import argparse

behdict = {}  # tag ->behavior
semdict = {}  # tag -> semantics
attribdict = {}  # tag -> attributes
macros = {}  # macro -> macro information...
registers = {}  # register -> register functions
new_registers = {}
tags = []  # list of all tags
overrides = {}  # tags with helper overrides
idef_parser_enabled = {}  # tags enabled for idef-parser


def is_sysemu_tag(tag):
    return bool(attribdict[tag] & {"A_PRIV", "A_GUEST"})


def tag_ignore(tag):
    tag_skips = (
        "Y6_diag",
        "Y6_diag0",
        "Y6_diag1",
    )
    attr_skips = {
        "A_FAKEINSN",
        "A_MAPPING",
        "A_CONDMAPPING",
    }
    return tag in tag_skips or attribdict[tag] & attr_skips


def get_sys_tags():
    return sorted(
        tag for tag in frozenset(tags) if is_sysemu_tag(tag)
    )


def get_user_tags():
    return sorted(
        tag for tag in frozenset(tags) if not is_sysemu_tag(tag)
    )


def get_all_tags():
    return get_user_tags() + get_sys_tags()


# We should do this as a hash for performance,
# but to keep order let's keep it as a list.
def uniquify(seq):
    seen = set()
    seen_add = seen.add
    return [x for x in seq if x not in seen and not seen_add(x)]


regre = re.compile(
    r"((?<!DUP)[MNORCPQXSGVZALK])([stuvwxyzdefg]+)([.]?[LlHh]?)(\d+S?)"
)
immre = re.compile(r"[#]([rRsSuUm])(\d+)(?:[:](\d+))?")
reg_or_immre = re.compile(
    r"(((?<!DUP)[MNRCOPQXSGVZALK])([stuvwxyzdefg]+)"
    r"([.]?[LlHh]?)(\d+S?))|([#]([rRsSuUm])(\d+)[:]?(\d+)?)"
)
relimmre = re.compile(r"[#]([rR])(\d+)(?:[:](\d+))?")
absimmre = re.compile(r"[#]([sSuUm])(\d+)(?:[:](\d+))?")

finished_macros = set()


def expand_macro_attribs(macro, allmac_re):
    if macro.key not in finished_macros:
        # Get a list of all things that might be macros
        l = allmac_re.findall(macro.beh)
        for submacro in l:
            if not submacro:
                continue
            if not macros[submacro]:
                raise Exception(f"Couldn't find macro: <{l}>")
            macro.attribs |= expand_macro_attribs(macros[submacro], allmac_re)
            finished_macros.add(macro.key)
    return macro.attribs


# When qemu needs an attribute that isn't in the imported files,
# we'll add it here.
def add_qemu_macro_attrib(name, attrib):
    macros[name].attribs.add(attrib)


immextre = re.compile(r"f(MUST_)?IMMEXT[(]([UuSsRr])")


def is_cond_jump(tag):
    if tag == "J2_rte":
        return False
    if "A_HWLOOP0_END" in attribdict[tag] or "A_HWLOOP1_END" in attribdict[tag]:
        return False
    return re.compile(r"(if.*fBRANCH)|(if.*fJUMPR)").search(semdict[tag]) != None


def is_cond_call(tag):
    return re.compile(r"(if.*fCALL)").search(semdict[tag]) != None


def calculate_attribs():
    add_qemu_macro_attrib("fREAD_PC", "A_IMPLICIT_READS_PC")
    add_qemu_macro_attrib("fTRAP", "A_IMPLICIT_READS_PC")
    add_qemu_macro_attrib("fSET_OVERFLOW", "A_IMPLICIT_WRITES_USR")
    add_qemu_macro_attrib("fSET_LPCFG", "A_IMPLICIT_WRITES_USR")
    add_qemu_macro_attrib("fCLEAR_RTE_EX", "A_IMPLICIT_WRITES_SSR")
    add_qemu_macro_attrib("fLOAD", "A_SCALAR_LOAD")
    add_qemu_macro_attrib("fSTORE", "A_SCALAR_STORE")
    add_qemu_macro_attrib("fSET_K0_LOCK", "A_IMPLICIT_READS_PC")
    add_qemu_macro_attrib("fSET_TLB_LOCK", "A_IMPLICIT_READS_PC")
    add_qemu_macro_attrib('fLSBNEW0', 'A_IMPLICIT_READS_P0')
    add_qemu_macro_attrib('fLSBNEW0NOT', 'A_IMPLICIT_READS_P0')
    add_qemu_macro_attrib('fREAD_P0', 'A_IMPLICIT_READS_P0')
    add_qemu_macro_attrib('fLSBNEW1', 'A_IMPLICIT_READS_P1')
    add_qemu_macro_attrib('fLSBNEW1NOT', 'A_IMPLICIT_READS_P1')
    add_qemu_macro_attrib('fREAD_P3', 'A_IMPLICIT_READS_P3')
    add_qemu_macro_attrib('fREAD_SP', 'A_IMPLICIT_READS_SP')

    # Recurse down macros, find attributes from sub-macros
    macroValues = list(macros.values())
    allmacros_restr = "|".join(set([m.re.pattern for m in macroValues]))
    allmacros_re = re.compile(allmacros_restr)
    for macro in macroValues:
        expand_macro_attribs(macro, allmacros_re)
    # Append attributes to all instructions
    for tag in tags:
        for macname in allmacros_re.findall(semdict[tag]):
            if not macname:
                continue
            macro = macros[macname]
            attribdict[tag] |= set(macro.attribs)
    # Mark conditional jumps and calls
    #     Not all instructions are properly marked with A_CONDEXEC
    for tag in tags:
        if is_cond_jump(tag) or is_cond_call(tag):
            attribdict[tag].add("A_CONDEXEC")


def SEMANTICS(tag, beh, sem):
    # print tag,beh,sem
    behdict[tag] = beh
    semdict[tag] = sem
    attribdict[tag] = set()
    tags.append(tag)  # dicts have no order, this is for order


def ATTRIBUTES(tag, attribstring):
    attribstring = attribstring.replace("ATTRIBS", "").replace("(", "").replace(")", "")
    if not attribstring:
        return
    attribs = attribstring.split(",")
    for attrib in attribs:
        attribdict[tag].add(attrib.strip())


class Macro(object):
    __slots__ = ["key", "name", "beh", "attribs", "re"]

    def __init__(self, name, beh, attribs):
        self.key = name
        self.name = name
        self.beh = beh
        self.attribs = set(attribs)
        self.re = re.compile("\\b" + name + "\\b")


def MACROATTRIB(macname, beh, attribstring):
    attribstring = attribstring.replace("(", "").replace(")", "")
    if attribstring:
        attribs = attribstring.split(",")
    else:
        attribs = []
    macros[macname] = Macro(macname, beh, attribs)

def compute_tag_regs(tag, full):
    tagregs = regre.findall(behdict[tag])
    if not full:
        tagregs = map(lambda reg: reg[:2], tagregs)
    return uniquify(tagregs)

def compute_tag_immediates(tag):
    return uniquify(immre.findall(behdict[tag]))


##
##  tagregs is the main data structure we'll use
##  tagregs[tag] will contain the registers used by an instruction
##  Within each entry, we'll use the regtype and regid fields
##      regtype can be one of the following
##          C                control register
##          N                new register value
##          P                predicate register
##          R                GPR register
##          M                modifier register
##          Q                HVX predicate vector
##          V                HVX vector register
##          O                HVX new vector register
##      regid can be one of the following
##          d, e             destination register
##          dd               destination register pair
##          s, t, u, v, w    source register
##          ss, tt, uu, vv   source register pair
##          x, y             read-write register
##          xx, yy           read-write register pair
##
def get_tagregs(full=False):
    compute_func = lambda tag: compute_tag_regs(tag, full)
    return dict(zip(tags, list(map(compute_func, tags))))

def get_tagimms():
    return dict(zip(tags, list(map(compute_tag_immediates, tags))))


def need_p0(tag):
    return "A_IMPLICIT_READS_P0" in attribdict[tag]


def need_sp(tag):
    return "A_IMPLICIT_READS_SP" in attribdict[tag]


def is_hvx_insn(tag):
    return "A_CVI" in attribdict[tag]


def need_env(tag):
    return ("A_STORE" in attribdict[tag] or
            "A_LOAD" in attribdict[tag] or
            "A_CVI_GATHER" in attribdict[tag] or
            "A_CVI_SCATTER" in attribdict[tag] or
            "A_HVX_IEEE_FP" in attribdict[tag] or
            "A_HVX_FLT" in attribdict[tag] or
            "A_IMPLICIT_WRITES_USR" in attribdict[tag] or
            "A_PRIV" in attribdict[tag] or
            "J2_trap" in tag)


def need_slot(tag):
    if (
        "A_CVI_SCATTER" not in attribdict[tag]
        and "A_CVI_GATHER" not in attribdict[tag]
        and ("A_STORE" in attribdict[tag]
             or "A_LOAD" in attribdict[tag])
        and tag != "L4_loadw_phys"
        and tag != "L6_memcpy"
        and tag != "Y6_dmlink"
    ):
        return 1
    else:
        return 0


def need_part1(tag):
    return re.compile(r"fPART1").search(semdict[tag])


def need_ea(tag):
    return re.compile(r"\bEA\b").search(semdict[tag])


def need_PC(tag):
    return "A_IMPLICIT_READS_PC" in attribdict[tag]


def need_next_PC(tag):
    return "A_CALL" in attribdict[tag] or tag == "J2_trap1"


def need_pkt_has_multi_cof(tag):
    if attribdict[tag] & {"A_JUMP", "A_CALL"}:
        return tag != "J4_hintjumpr"
    return False


def need_pkt_need_commit(tag):
    return 'A_IMPLICIT_WRITES_USR' in attribdict[tag]


def skip_qemu_helper(tag):
    return tag in overrides.keys()


# HLX instructions that are emitted as inline gvec TCG IR instead of a
# helper call.  "size" is the oprsz/maxsz for the gvec calls, one register
# (the default) or a pair.  "table" names a descriptor in hlx_gvec_tables.h.
HLX_INLINE_TAGS = {
    "LX_add_h":      {"op": "add",          "vece": "MO_16"},
    "LX_sub_h":      {"op": "sub",          "vece": "MO_16"},
    "LX_assign":     {"op": "mov",          "vece": "MO_64"},
    # LX_add_w and LX_sub_w cannot be inlined: the low halfword of each word
    # is in the lo register and the high halfword in the hi register, so a
    # 32-bit gvec add would lose the carry between them.  They use the helper,
    # which interleaves the pair first.
    "LX_assign_w":   {"op": "mov",          "vece": "MO_64",
                      "size": "2 * sizeof(HLXVector)"},
    "LX_combine_w":  {"op": "combine_pair", "vece": "MO_64"},
    "LX_add_h_sat":  {"op": "gvec3_fn", "fn": "ssadd",
                      "vece": "MO_16"},
    "LX_add_uh_sat": {"op": "gvec3_fn", "fn": "usadd",
                      "vece": "MO_16"},
    "LX_sub_h_sat":  {"op": "gvec3_fn", "fn": "sssub",
                      "vece": "MO_16"},
    "LX_sub_uh_sat": {"op": "gvec3_fn", "fn": "ussub",
                      "vece": "MO_16"},
    # Word forms use helpers because their halfwords are split.
    "LX_add_hp_sat": {"op": "gvec3_fn", "fn": "ssadd",
                      "vece": "MO_16", "size": "2 * sizeof(HLXVector)"},
    "LX_add_uhp_sat":{"op": "gvec3_fn", "fn": "usadd",
                      "vece": "MO_16", "size": "2 * sizeof(HLXVector)"},
    "LX_sub_hp_sat": {"op": "gvec3_fn", "fn": "sssub",
                      "vece": "MO_16", "size": "2 * sizeof(HLXVector)"},
    "LX_sub_uhp_sat":{"op": "gvec3_fn", "fn": "ussub",
                      "vece": "MO_16", "size": "2 * sizeof(HLXVector)"},
    "LX_and":        {"op": "gvec3_fn",     "fn": "and",
                      "vece": "MO_64"},
    "LX_or":         {"op": "gvec3_fn",     "fn": "or",
                      "vece": "MO_64"},
    "LX_xor":        {"op": "gvec3_fn",     "fn": "xor",
                      "vece": "MO_64"},
    "LX_andn":       {"op": "gvec3_fn",     "fn": "andc",
                      "vece": "MO_64"},
    "LX_orn":        {"op": "gvec3_fn",     "fn": "orc",
                      "vece": "MO_64"},
    "LX_and_w":      {"op": "gvec3_fn",     "fn": "and",
                      "vece": "MO_64", "size": "2 * sizeof(HLXVector)"},
    "LX_or_w":       {"op": "gvec3_fn",     "fn": "or",
                      "vece": "MO_64", "size": "2 * sizeof(HLXVector)"},
    "LX_xor_w":      {"op": "gvec3_fn",     "fn": "xor",
                      "vece": "MO_64", "size": "2 * sizeof(HLXVector)"},
    "LX_andn_w":     {"op": "gvec3_fn",     "fn": "andc",
                      "vece": "MO_64", "size": "2 * sizeof(HLXVector)"},
    "LX_orn_w":      {"op": "gvec3_fn",     "fn": "orc",
                      "vece": "MO_64", "size": "2 * sizeof(HLXVector)"},
    "LX_min_h":      {"op": "gvec3_fn", "fn": "smin",
                      "vece": "MO_16"},
    "LX_max_h":      {"op": "gvec3_fn", "fn": "smax",
                      "vece": "MO_16"},
    "LX_min_uh":     {"op": "gvec3_fn", "fn": "umin",
                      "vece": "MO_16"},
    "LX_max_uh":     {"op": "gvec3_fn", "fn": "umax",
                      "vece": "MO_16"},
    "LX_min_hp":     {"op": "gvec3_fn", "fn": "smin",
                      "vece": "MO_16", "size": "2 * sizeof(HLXVector)"},
    "LX_max_hp":     {"op": "gvec3_fn", "fn": "smax",
                      "vece": "MO_16", "size": "2 * sizeof(HLXVector)"},
    "LX_min_uhp":    {"op": "gvec3_fn", "fn": "umin",
                      "vece": "MO_16", "size": "2 * sizeof(HLXVector)"},
    "LX_max_uhp":    {"op": "gvec3_fn", "fn": "umax",
                      "vece": "MO_16", "size": "2 * sizeof(HLXVector)"},
    "LX_abs_uh":     {"op": "gvec2_fn", "fn": "abs",
                      "vece": "MO_16"},
    "LX_abs_uhp":    {"op": "gvec2_fn", "fn": "abs",
                      "vece": "MO_16", "size": "2 * sizeof(HLXVector)"},
    "LX_add_uhh_sat":   {"op": "gvec3", "table": "hlx_ushsat_add_op",
                         "vece": "MO_16"},
    "LX_sub_uhh_sat":   {"op": "gvec3", "table": "hlx_ushsat_sub_op",
                         "vece": "MO_16"},
    "LX_add_uhp_h_sat": {"op": "gvec3", "table": "hlx_ushsat_add_op",
                         "vece": "MO_16", "size": "2 * sizeof(HLXVector)"},
    "LX_sub_uhp_h_sat": {"op": "gvec3", "table": "hlx_ushsat_sub_op",
                         "vece": "MO_16", "size": "2 * sizeof(HLXVector)"},
    "LX_abs_uh_sat":    {"op": "gvec2", "table": "hlx_abs_uh_sat_op",
                         "vece": "MO_16"},
    "LX_cmp_gt_h":   {"op": "cmp",    "cond": "TCG_COND_GT",
                      "vece": "MO_16"},
    "LX_cmp_gt_uh":  {"op": "cmp",    "cond": "TCG_COND_GTU",
                      "vece": "MO_16"},
    "LX_cmp_gt_hp":  {"op": "cmp",    "cond": "TCG_COND_GT",
                      "vece": "MO_16",   "size": "2 * sizeof(HLXVector)"},
    "LX_cmp_gt_uhp": {"op": "cmp",    "cond": "TCG_COND_GTU",
                      "vece": "MO_16",   "size": "2 * sizeof(HLXVector)"},
    "LX_avg_h":         {"op": "gvec3", "table": "hlx_avg_sh_op",
                         "vece": "MO_16"},
    "LX_avg_h_rnd":     {"op": "gvec3", "table": "hlx_avg_sh_rnd_op",
                         "vece": "MO_16"},
    "LX_avg_uh":        {"op": "gvec3", "table": "hlx_avg_uh_op",
                         "vece": "MO_16"},
    "LX_avg_uh_rnd":    {"op": "gvec3", "table": "hlx_avg_uh_rnd_op",
                         "vece": "MO_16"},
}

def is_hlx_inline_tag(tag):
    return tag in HLX_INLINE_TAGS

def gen_hlx_inline(f, tag, regs):
    """Emit inline gvec operations, splitting 4096-byte register pairs.

    gvec maxsz is limited to 2048 bytes, so pairs use two calls.
    """
    desc = HLX_INLINE_TAGS[tag]
    op = desc["op"]
    vece = desc["vece"]
    sz = desc.get("size", "sizeof(HLXVector)")
    pair_dst = (sz == "2 * sizeof(HLXVector)")
    single = "sizeof(HLXVector)"
    oprsz = single if pair_dst else desc.get("oprsz", sz)
    maxsz = single if pair_dst else desc.get("maxsz", sz)

    def reg(i):
        regtype, regid, *_ = regs[i]
        return f"{regtype}{regid}V"

    if op == "add":
        d, a, b = reg(0), reg(1), reg(2)
        if pair_dst:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_add_var({vece},
                                     {d}_base, {d}_off,
                                     {a}_base, {a}_off,
                                     {b}_base, {b}_off,
                                     {oprsz}, {maxsz});
                tcg_gen_gvec_add_var({vece},
                                     {d}_base, {d}_off + sizeof(HLXVector),
                                     {a}_base, {a}_off + sizeof(HLXVector),
                                     {b}_base, {b}_off + sizeof(HLXVector),
                                     {oprsz}, {maxsz});
            """))
        else:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_add_var({vece},
                                     {d}_base, {d}_off,
                                     {a}_base, {a}_off,
                                     {b}_base, {b}_off,
                                     {oprsz}, {maxsz});
            """))
    elif op == "sub":
        d, a, b = reg(0), reg(1), reg(2)
        if pair_dst:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_sub_var({vece},
                                     {d}_base, {d}_off,
                                     {a}_base, {a}_off,
                                     {b}_base, {b}_off,
                                     {oprsz}, {maxsz});
                tcg_gen_gvec_sub_var({vece},
                                     {d}_base, {d}_off + sizeof(HLXVector),
                                     {a}_base, {a}_off + sizeof(HLXVector),
                                     {b}_base, {b}_off + sizeof(HLXVector),
                                     {oprsz}, {maxsz});
            """))
        else:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_sub_var({vece},
                                     {d}_base, {d}_off,
                                     {a}_base, {a}_off,
                                     {b}_base, {b}_off,
                                     {oprsz}, {maxsz});
            """))
    elif op == "mov":
        d, a = reg(0), reg(1)
        if pair_dst:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_mov_var({vece},
                                     {d}_base, {d}_off,
                                     {a}_base, {a}_off,
                                     {oprsz}, {maxsz});
                tcg_gen_gvec_mov_var({vece},
                                     {d}_base, {d}_off + sizeof(HLXVector),
                                     {a}_base, {a}_off + sizeof(HLXVector),
                                     {oprsz}, {maxsz});
            """))
        else:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_mov_var({vece},
                                     {d}_base, {d}_off,
                                     {a}_base, {a}_off,
                                     {oprsz}, {maxsz});
            """))
    elif op == "dup_imm":
        d = reg(0)
        imm = desc["imm"]
        if pair_dst:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_dup_imm_var({vece},
                                         {d}_base, {d}_off,
                                         {oprsz}, {maxsz}, {imm});
                tcg_gen_gvec_dup_imm_var({vece},
                                         {d}_base, {d}_off + sizeof(HLXVector),
                                         {oprsz}, {maxsz}, {imm});
            """))
        else:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_dup_imm_var({vece},
                                         {d}_base, {d}_off,
                                         {oprsz}, {maxsz}, {imm});
            """))
    elif op == "gvec3":
        d, a, b = reg(0), reg(1), reg(2)
        table = desc["table"]
        if pair_dst:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_3_var({d}_base, {d}_off,
                                   {a}_base, {a}_off,
                                   {b}_base, {b}_off,
                                   {oprsz}, {maxsz}, &{table});
                tcg_gen_gvec_3_var({d}_base, {d}_off + sizeof(HLXVector),
                                   {a}_base, {a}_off + sizeof(HLXVector),
                                   {b}_base, {b}_off + sizeof(HLXVector),
                                   {oprsz}, {maxsz}, &{table});
            """))
        else:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_3_var({d}_base, {d}_off,
                                   {a}_base, {a}_off,
                                   {b}_base, {b}_off,
                                   {oprsz}, {maxsz}, &{table});
            """))
    elif op == "gvec3_fn":
        d, a, b = reg(0), reg(1), reg(2)
        fn = desc["fn"]
        if pair_dst:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_{fn}_var({vece},
                                      {d}_base, {d}_off,
                                      {a}_base, {a}_off,
                                      {b}_base, {b}_off,
                                      {oprsz}, {maxsz});
                tcg_gen_gvec_{fn}_var({vece},
                                      {d}_base, {d}_off + sizeof(HLXVector),
                                      {a}_base, {a}_off + sizeof(HLXVector),
                                      {b}_base, {b}_off + sizeof(HLXVector),
                                      {oprsz}, {maxsz});
            """))
        else:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_{fn}_var({vece},
                                      {d}_base, {d}_off,
                                      {a}_base, {a}_off,
                                      {b}_base, {b}_off,
                                      {oprsz}, {maxsz});
            """))
    elif op == "combine_pair":
        dd, u, v = reg(0), reg(1), reg(2)
        single = "sizeof(HLXVector)"
        f.write(code_fmt(f"""\
            tcg_gen_gvec_mov_var({vece},
                                 {dd}_base, {dd}_off,
                                 {v}_base, {v}_off,
                                 {single}, {single});
            tcg_gen_gvec_mov_var({vece},
                                 {dd}_base,
                                 {dd}_off + sizeof(HLXVector),
                                 {u}_base, {u}_off,
                                 {single}, {single});
        """))
    elif op == "gvec2":
        # Two-operand op: dest and one source. table is a const GVecGen2.
        d, a = reg(0), reg(1)
        table = desc["table"]
        if pair_dst:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_2_var({d}_base, {d}_off,
                                   {a}_base, {a}_off,
                                   {oprsz}, {maxsz}, &{table});
                tcg_gen_gvec_2_var({d}_base, {d}_off + sizeof(HLXVector),
                                   {a}_base, {a}_off + sizeof(HLXVector),
                                   {oprsz}, {maxsz}, &{table});
            """))
        else:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_2_var({d}_base, {d}_off,
                                   {a}_base, {a}_off,
                                   {oprsz}, {maxsz}, &{table});
            """))
    elif op == "gvec2_fn":
        d, a = reg(0), reg(1)
        fn = desc["fn"]
        if pair_dst:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_{fn}_var({vece},
                                      {d}_base, {d}_off,
                                      {a}_base, {a}_off,
                                      {oprsz}, {maxsz});
                tcg_gen_gvec_{fn}_var({vece},
                                      {d}_base, {d}_off + sizeof(HLXVector),
                                      {a}_base, {a}_off + sizeof(HLXVector),
                                      {oprsz}, {maxsz});
            """))
        else:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_{fn}_var({vece},
                                      {d}_base, {d}_off,
                                      {a}_base, {a}_off,
                                      {oprsz}, {maxsz});
            """))
    elif op == "cmp":
        d, a, b = reg(0), reg(1), reg(2)
        cond = desc["cond"]
        if pair_dst:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_cmp_var({cond}, {vece},
                                     {d}_base, {d}_off,
                                     {a}_base, {a}_off,
                                     {b}_base, {b}_off,
                                     {oprsz}, {maxsz});
                tcg_gen_gvec_cmp_var({cond}, {vece},
                                     {d}_base, {d}_off + sizeof(HLXVector),
                                     {a}_base, {a}_off + sizeof(HLXVector),
                                     {b}_base, {b}_off + sizeof(HLXVector),
                                     {oprsz}, {maxsz});
            """))
        else:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_cmp_var({cond}, {vece},
                                     {d}_base, {d}_off,
                                     {a}_base, {a}_off,
                                     {b}_base, {b}_off,
                                     {oprsz}, {maxsz});
            """))
    elif op == "conv_h_to_w_pair":
        # lo = the source; hi = zeros (zxt) or the source >> 15 (sxt)
        d, a = reg(0), reg(1)
        single = "sizeof(HLXVector)"
        sign_extend = desc.get("sign_extend", False)
        if sign_extend:
            table = desc["table"]
            f.write(code_fmt(f"""\
                tcg_gen_gvec_mov_var(MO_64,
                                     {d}_base, {d}_off,
                                     {a}_base, {a}_off,
                                     {single}, {single});
                tcg_gen_gvec_2_var({d}_base, {d}_off + sizeof(HLXVector),
                                   {a}_base, {a}_off,
                                   {single}, {single}, &{table});
            """))
        else:
            f.write(code_fmt(f"""\
                tcg_gen_gvec_mov_var(MO_64,
                                     {d}_base, {d}_off,
                                     {a}_base, {a}_off,
                                     {single}, {single});
                tcg_gen_gvec_dup_imm_var(MO_64,
                                         {d}_base, {d}_off + sizeof(HLXVector),
                                         {single}, {single}, 0);
            """))
    else:
        raise Exception(f"unknown HLX inline op {op!r} for tag {tag!r}")


def is_idef_parser_enabled(tag):
    return tag in idef_parser_enabled


def is_hvx_insn(tag):
    return "A_CVI" in attribdict[tag]


def has_hvx_helper(tag):
    return (is_hvx_insn(tag) and
            not skip_qemu_helper(tag) and
            not is_idef_parser_enabled(tag))


def imm_name(immlett):
    return f"{immlett}iV"


def read_semantics_file(name):
    eval_line = ""
    for line in open(name, "rt").readlines():
        if not line.startswith("#"):
            eval_line += line
            if line.endswith("\\\n"):
                eval_line.rstrip("\\\n")
            else:
                eval(eval_line.strip())
                eval_line = ""


def read_overrides_file(name):
    overridere = re.compile(r"#define fGEN_TCG_([A-Za-z0-9_]+)\(.*")
    for line in open(name, "rt").readlines():
        if not overridere.match(line):
            continue
        tag = overridere.findall(line)[0]
        overrides[tag] = True


def read_idef_parser_enabled_file(name):
    global idef_parser_enabled
    with open(name, "r") as idef_parser_enabled_file:
        lines = idef_parser_enabled_file.read().strip().split("\n")
        idef_parser_enabled = set(lines)


def is_predicated(tag):
    return "A_CONDEXEC" in attribdict[tag]


def code_fmt(txt):
    return textwrap.indent(textwrap.dedent(txt), "    ")


def hvx_newv(tag):
    if "A_CVI_NEW" in attribdict[tag]:
        return "EXT_NEW"
    elif "A_CVI_TMP" in attribdict[tag] or "A_CVI_TMP_DST" in attribdict[tag]:
        return "EXT_TMP"
    else:
        return "EXT_DFL"

def vreg_offset_func(tag):
    if "A_CVI_TMP" in attribdict[tag] or "A_CVI_TMP_DST" in attribdict[tag]:
        return "ctx_tmp_vreg_off"
    else:
        return "ctx_future_vreg_off"

class HelperArg:
    def __init__(self, proto_arg, call_arg, func_arg):
        self.proto_arg = proto_arg
        self.call_arg = call_arg
        self.func_arg = func_arg

class Register:
    def __init__(self, regtype, regid):
        self.regtype = regtype
        self.regid = regid
        self.reg_num = f"{regtype}{regid}N"
    def decl_reg_num(self, f, regno):
        f.write(code_fmt(f"""\
            const int {self.reg_num} = insn->regno[{regno}];
        """))
    def idef_arg(self, declared):
        declared.append(self.reg_tcg())
    def may_alias_gpr(self):
        return False
    def helper_arg(self):
        return HelperArg(
            self.helper_proto_type(),
            self.reg_tcg(),
            f"{self.helper_arg_type()} {self.helper_arg_name()}"
        )


#
# Every register is either Single or Pair or Hvx
#
class Scalar:
    def is_scalar_reg(self):
        return True
    def is_hvx_reg(self):
        return False
    def is_hlx_reg(self):
        return False
    def helper_arg_name(self):
        return self.reg_tcg()

class Single(Scalar):
    def helper_proto_type(self):
        return "s32"
    def helper_arg_type(self):
        return "int32_t"
    def is_pair(self):
        return False

class Pair(Scalar):
    def helper_proto_type(self):
        return "s64"
    def helper_arg_type(self):
        return "int64_t"
    def is_pair(self):
        return True

class Hvx:
    def is_scalar_reg(self):
        return False
    def is_hvx_reg(self):
        return True
    def is_hlx_reg(self):
        return False
    def hvx_off(self):
        return f"{self.reg_tcg()}_off"
    def hvx_base(self):
        return f"{self.reg_tcg()}_base"
    def decl_tcg_ptr(self, f):
        ptr = self.reg_tcg()
        f.write(code_fmt(f"""\
            TCGv_ptr {ptr} = tcg_temp_new_ptr();
            tcg_gen_addi_ptr({ptr}, {self.hvx_base()}, {self.hvx_off()});
        """))
    def helper_proto_type(self):
        return "ptr"
    def helper_arg_type(self):
        return "void *"
    def helper_arg_name(self):
        return f"{self.reg_tcg()}_void"
    def gen_clear_ext(self, f, tag):
        predicate = None
        guard = ""
        end_guard = ""
        if is_predicated(tag):
            for regtype, regid in compute_tag_regs(tag, False):
                if regtype == "P":
                    predicate = get_register(tag, regtype, regid).reg_tcg()
                    break
            assert predicate is not None
            cond = ("TCG_COND_NE" if "fLSBOLDNOT" in semdict[tag]
                    else "TCG_COND_EQ")
            guard = f"""
                TCGv {self.reg_tcg()}_ext_pred = tcg_temp_new();
                TCGLabel *{self.reg_tcg()}_ext_skip = gen_new_label();
                tcg_gen_andi_tl({self.reg_tcg()}_ext_pred, {predicate}, 1);
                tcg_gen_brcondi_tl({cond}, {self.reg_tcg()}_ext_pred, 0,
                                    {self.reg_tcg()}_ext_skip);
            """
            end_guard = f"""
                gen_set_label({self.reg_tcg()}_ext_skip);
            """
        f.write(code_fmt(f"""\
                {guard}
                tcg_gen_gvec_dup_imm_var(MO_8, {self.hvx_base()},
                    {self.hvx_off()} + offsetof(MMVector, ext),
                    sizeof_field(MMVector, ext),
                    sizeof_field(MMVector, ext),
                    V_EXTENDED_BYTEVAL);
                {end_guard}
            """))
    def gen_clear_ext_pair(self, f, tag):
        predicate = None
        guard = ""
        end_guard = ""
        if is_predicated(tag):
            for regtype, regid in compute_tag_regs(tag, False):
                if regtype == "P":
                    predicate = get_register(tag, regtype, regid).reg_tcg()
                    break
            assert predicate is not None
            cond = ("TCG_COND_NE" if "fLSBOLDNOT" in semdict[tag]
                    else "TCG_COND_EQ")
            guard = f"""
                TCGv {self.reg_tcg()}_ext_pred = tcg_temp_new();
                TCGLabel *{self.reg_tcg()}_ext_skip = gen_new_label();
                tcg_gen_andi_tl({self.reg_tcg()}_ext_pred, {predicate}, 1);
                tcg_gen_brcondi_tl({cond}, {self.reg_tcg()}_ext_pred, 0,
                                    {self.reg_tcg()}_ext_skip);
            """
            end_guard = f"""
                gen_set_label({self.reg_tcg()}_ext_skip);
            """
        f.write(code_fmt(f"""\
                {guard}
                tcg_gen_gvec_dup_imm_var(MO_8, {self.hvx_base()},
                    {self.hvx_off()} + offsetof(MMVector, ext),
                    sizeof_field(MMVector, ext),
                    sizeof_field(MMVector, ext),
                    V_EXTENDED_BYTEVAL);
                tcg_gen_gvec_dup_imm_var(MO_8, {self.hvx_base()},
                    {self.hvx_off()} + sizeof(MMVector)
                        + offsetof(MMVector, ext),
                    sizeof_field(MMVector, ext),
                    sizeof_field(MMVector, ext),
                    V_EXTENDED_BYTEVAL);
                {end_guard}
            """))

#
# Every register is either Dest or OldSource or NewSource or ReadWrite
#
class Dest:
    def reg_tcg(self):
        return f"{self.regtype}{self.regid}V"
    def is_written(self):
        return True
    def is_writeonly(self):
        return True
    def is_read(self):
        return False
    def is_readwrite(self):
        return False

class Source:
    def is_written(self):
        return False
    def is_writeonly(self):
        return False
    def is_read(self):
        return True
    def is_readwrite(self):
        return False

class OldSource(Source):
    def reg_tcg(self):
        return f"{self.regtype}{self.regid}V"
    def is_old(self):
        return True
    def is_new(self):
        return False

class NewSource(Source):
    def reg_tcg(self):
        return f"{self.regtype}{self.regid}N"
    def is_old(self):
        return False
    def is_new(self):
        return True

class ReadWrite:
    def reg_tcg(self):
        return f"{self.regtype}{self.regid}V"
    def is_written(self):
        return True
    def is_writeonly(self):
        return False
    def is_read(self):
        return True
    def is_readwrite(self):
        return True
    def is_old(self):
        return True
    def is_new(self):
        return False

class GprDest(Register, Single, Dest):
    def may_alias_gpr(self):
        return True
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv {self.reg_tcg()} = get_result_gpr(ctx, {self.reg_num});
        """))
    def gen_write(self, f, tag):
        ## No write needed
        return
    def analyze_write(self, f, tag, regno):
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_reg_write(ctx, {self.reg_num}, {predicated});
        """))

class GprSource(Register, Single, OldSource):
    def may_alias_gpr(self):
        return True
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv {self.reg_tcg()} = hex_gpr[{self.reg_num}];
        """))
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_reg_read(ctx, {self.reg_num});
        """))

class GprNewSource(Register, Single, NewSource):
    def decl_tcg(self, f, tag, regno):
        f.write(code_fmt(f"""\
            TCGv {self.reg_tcg()} = get_result_gpr(ctx, insn->regno[{regno}]);
        """))
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_reg_read_new(ctx, {self.reg_num});
        """))

class GprReadWrite(Register, Single, ReadWrite):
    def may_alias_gpr(self):
        return True
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv {self.reg_tcg()} = get_result_gpr(ctx, {self.reg_num});
        """))
        ## For read/write registers, we need to get the original value into
        ## the result TCGv.  For predicated instructions, this is done in
        ## gen_start_packet.  For un-predicated instructions, we do it here.
        if not is_predicated(tag):
            f.write(code_fmt(f"""\
                tcg_gen_mov_tl({self.reg_tcg()}, hex_gpr[{self.reg_num}]);
            """))
    def gen_write(self, f, tag):
        ## No write needed
        return
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_reg_read(ctx, {self.reg_num});
        """))
    def analyze_write(self, f, tag, regno):
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_reg_write(ctx, {self.reg_num}, {predicated});
        """))

class ControlDest(Register, Single, Dest):
    def may_alias_gpr(self):
        return True
    def decl_reg_num(self, f, regno):
        f.write(code_fmt(f"""\
            const int {self.reg_num} = insn->regno[{regno}]  + HEX_REG_SA0;
        """))
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv {self.reg_tcg()} = get_result_gpr(ctx, {self.reg_num});
        """))
    def gen_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_write_ctrl_reg(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_write(self, f, tag, regno):
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_reg_write(ctx, {self.reg_num}, {predicated});
        """))

class ControlSource(Register, Single, OldSource):
    def decl_reg_num(self, f, regno):
        f.write(code_fmt(f"""\
            const int {self.reg_num} = insn->regno[{regno}]  + HEX_REG_SA0;
        """))
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno);
        f.write(code_fmt(f"""\
            TCGv {self.reg_tcg()} = tcg_temp_new();
            gen_read_ctrl_reg(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_reg_read(ctx, {self.reg_num});
        """))

class ModifierSource(Register, Single, OldSource):
    def may_alias_gpr(self):
        return True
    def decl_reg_num(self, f, regno):
        f.write(code_fmt(f"""\
            const int {self.reg_num} = insn->regno[{regno}] + HEX_REG_M0;
        """))
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv {self.reg_tcg()} = hex_gpr[{self.reg_num}];
            TCGv CS G_GNUC_UNUSED =
                hex_gpr[{self.reg_num} - HEX_REG_M0 + HEX_REG_CS0];
        """))
    def idef_arg(self, declared):
        declared.append(self.reg_tcg())
        declared.append("CS")
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_reg_read(ctx, {self.reg_num});
        """))

class PredDest(Register, Single, Dest):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv {self.reg_tcg()} = tcg_temp_new();
        """))
    def gen_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_pred_write(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_write(self, f, tag, regno):
        f.write(code_fmt(f"""\
            ctx_log_pred_write(ctx, {self.reg_num});
        """))

class PredSource(Register, Single, OldSource):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv {self.reg_tcg()} = hex_pred[{self.reg_num}];
        """))
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_pred_read(ctx, {self.reg_num});
        """))

class PredNewSource(Register, Single, NewSource):
    def decl_tcg(self, f, tag, regno):
        f.write(code_fmt(f"""\
            TCGv {self.reg_tcg()} = get_result_pred(ctx, insn->regno[{regno}]);
        """))
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_pred_read_new(ctx, {self.reg_num});
        """))

class PredReadWrite(Register, Single, ReadWrite):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv {self.reg_tcg()} = tcg_temp_new();
            tcg_gen_mov_tl({self.reg_tcg()}, hex_pred[{self.reg_num}]);
        """))
    def gen_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_pred_write(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_pred_read(ctx, {self.reg_num});
        """))
    def analyze_write(self, f, tag, regno):
        f.write(code_fmt(f"""\
            ctx_log_pred_write(ctx, {self.reg_num});
        """))

class PairDest(Register, Pair, Dest):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_i64 {self.reg_tcg()} =
                get_result_gpr_pair(ctx, {self.reg_num});
        """))
    def gen_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_write_reg_pair(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_write(self, f, tag, regno):
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_reg_write_pair(ctx, {self.reg_num}, {predicated});
        """))

class PairSource(Register, Pair, OldSource):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_i64 {self.reg_tcg()} = tcg_temp_new_i64();
            tcg_gen_concat_i32_i64({self.reg_tcg()},
                                    hex_gpr[{self.reg_num}],
                                    hex_gpr[{self.reg_num} + 1]);
        """))
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_reg_read_pair(ctx, {self.reg_num});
        """))

class PairReadWrite(Register, Pair, ReadWrite):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_i64 {self.reg_tcg()} =
                get_result_gpr_pair(ctx, {self.reg_num});
            tcg_gen_concat_i32_i64({self.reg_tcg()},
                                   hex_gpr[{self.reg_num}],
                                   hex_gpr[{self.reg_num} + 1]);
        """))
    def gen_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_write_reg_pair(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_reg_read_pair(ctx, {self.reg_num});
        """))
    def analyze_write(self, f, tag, regno):
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_reg_write_pair(ctx, {self.reg_num}, {predicated});
        """))

class ControlPairDest(Register, Pair, Dest):
    def decl_reg_num(self, f, regno):
        f.write(code_fmt(f"""\
            const int {self.reg_num} = insn->regno[{regno}] + HEX_REG_SA0;
        """))
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_i64 {self.reg_tcg()} =
                get_result_gpr_pair(ctx, {self.reg_num});
        """))
    def gen_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_write_ctrl_reg_pair(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_write(self, f, tag, regno):
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_reg_write_pair(ctx, {self.reg_num}, {predicated});
        """))

class ControlPairSource(Register, Pair, OldSource):
    def decl_reg_num(self, f, regno):
        f.write(code_fmt(f"""\
            const int {self.reg_num} = insn->regno[{regno}] + HEX_REG_SA0;
        """))
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_i64 {self.reg_tcg()} = tcg_temp_new_i64();
            gen_read_ctrl_reg_pair(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_reg_read_pair(ctx, {self.reg_num});
        """))

class VRegDest(Register, Hvx, Dest):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_ptr {self.hvx_base()};
            const intptr_t {self.hvx_off()} =
                {vreg_offset_func(tag)}(ctx, {self.reg_num}, 1,
                                        true, &{self.hvx_base()});
        """))
        if not skip_qemu_helper(tag):
            self.decl_tcg_ptr(f)
        self.gen_clear_ext(f, tag)
    def gen_zero(self, f, tag):
        f.write(code_fmt(f"""\
                tcg_gen_gvec_dup_imm_var(MO_64, {self.hvx_base()},
                    {self.hvx_off()}, sizeof(MMVector), sizeof(MMVector), 0);
            """))
        self.gen_clear_ext(f, tag)
    def gen_write(self, f, tag):
        pass
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMVector *)({self.helper_arg_name()}) */
        """))
    def analyze_write(self, f, tag, regno):
        newv = hvx_newv(tag)
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_vreg_write(ctx, {self.reg_num}, {newv}, {predicated},
                               insn_has_hvx_helper);
        """))

class VRegSource(Register, Hvx, OldSource):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_ptr {self.hvx_base()};
            const intptr_t {self.hvx_off()} =
                vreg_src_off(ctx, {self.reg_num}, &{self.hvx_base()});
        """))
        if not skip_qemu_helper(tag):
            self.decl_tcg_ptr(f)
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMVector *)({self.helper_arg_name()}) */
        """))
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_vreg_read(ctx, {self.reg_num}, insn_has_hvx_helper);
        """))

class VRegNewSource(Register, Hvx, NewSource):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        if skip_qemu_helper(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.hvx_base()};
                const intptr_t {self.hvx_off()} =
                    ctx_future_vreg_off(ctx, {self.reg_num}, 1,
                                        true, &{self.hvx_base()});
            """))
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMVector *)({self.helper_arg_name()}) */
        """))
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_vreg_read_new(ctx, {self.reg_num}, insn_has_hvx_helper);
        """))

class VRegReadWrite(Register, Hvx, ReadWrite):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_ptr {self.hvx_base()};
            TCGv_ptr {self.reg_tcg()}_srcbase;
            const intptr_t {self.hvx_off()} =
                {vreg_offset_func(tag)}(ctx, {self.reg_num}, 1,
                                        true, &{self.hvx_base()});
            const intptr_t {self.reg_tcg()}_srcoff =
                vreg_src_off(ctx, {self.reg_num}, &{self.reg_tcg()}_srcbase);
            tcg_gen_gvec_mov_var(MO_64, {self.hvx_base()}, {self.hvx_off()},
                                 {self.reg_tcg()}_srcbase,
                                 {self.reg_tcg()}_srcoff,
                                 sizeof(MMVector), sizeof(MMVector));
        """))
        if not skip_qemu_helper(tag):
            self.decl_tcg_ptr(f)
        self.gen_clear_ext(f, tag)
    def gen_zero(self, f, tag):
        f.write(code_fmt(f"""\
                tcg_gen_gvec_dup_imm_var(MO_64, {self.hvx_base()},
                    {self.hvx_off()}, sizeof(MMVector), sizeof(MMVector), 0);
            """))
        self.gen_clear_ext(f, tag)
    def gen_write(self, f, tag):
        pass
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMVector *)({self.helper_arg_name()}) */
        """))
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_vreg_read(ctx, {self.reg_num}, insn_has_hvx_helper);
        """))
    def analyze_write(self, f, tag, regno):
        newv = hvx_newv(tag)
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_vreg_write(ctx, {self.reg_num}, {newv}, {predicated},
                               insn_has_hvx_helper);
        """))

class VRegTmp(Register, Hvx, ReadWrite):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_ptr {self.hvx_base()} = tcg_env;
            const intptr_t {self.hvx_off()} = offsetof(CPUHexagonState, vtmp);
        """))
        if not skip_qemu_helper(tag):
            self.decl_tcg_ptr(f)
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()}_srcbase;
                const intptr_t {self.reg_tcg()}_srcoff =
                    vreg_src_off(ctx, {self.reg_num},
                                 &{self.reg_tcg()}_srcbase);
                tcg_gen_gvec_mov_var(MO_64, {self.hvx_base()},
                                     {self.hvx_off()},
                                     {self.reg_tcg()}_srcbase,
                                     {self.reg_tcg()}_srcoff,
                                     sizeof(MMVector), sizeof(MMVector));
            """))
        self.gen_clear_ext(f, tag)
    def gen_zero(self, f, tag):
        f.write(code_fmt(f"""\
                tcg_gen_gvec_dup_imm(MO_64, {self.hvx_off()},
                    sizeof(MMVector), sizeof(MMVector), 0);
            """))
        self.gen_clear_ext(f, tag)
    def gen_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_vreg_write(ctx, {self.hvx_base()}, {self.hvx_off()},
                           {self.reg_num}, {hvx_newv(tag)});
        """))
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMVector *)({self.helper_arg_name()}) */
        """))
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_vreg_read(ctx, {self.reg_num}, insn_has_hvx_helper);
        """))
    def analyze_write(self, f, tag, regno):
        newv = hvx_newv(tag)
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_vreg_write(ctx, {self.reg_num}, {newv}, {predicated},
                               insn_has_hvx_helper);
        """))

class VRegPairDest(Register, Hvx, Dest):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_ptr {self.hvx_base()};
            const intptr_t {self.hvx_off()} =
                {vreg_offset_func(tag)}(ctx, {self.reg_num}, 2,
                                        true, &{self.hvx_base()});
        """))
        if not skip_qemu_helper(tag):
            self.decl_tcg_ptr(f)
        self.gen_clear_ext_pair(f, tag)
    def gen_zero(self, f, tag):
        f.write(code_fmt(f"""\
            tcg_gen_gvec_dup_imm_var(MO_64, {self.hvx_base()}, {self.hvx_off()},
                sizeof(MMVectorPair), sizeof(MMVectorPair), 0);
        """))
        self.gen_clear_ext_pair(f, tag)
    def gen_write(self, f, tag):
        pass
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMVectorPair *)({self.helper_arg_name()}) */
        """))
    def analyze_write(self, f, tag, regno):
        newv = hvx_newv(tag)
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_vreg_write_pair(ctx, {self.reg_num}, {newv}, {predicated},
                                    insn_has_hvx_helper);
        """))

class VRegPairSource(Register, Hvx, OldSource):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_ptr {self.hvx_base()} = tcg_env;
            TCGv_ptr {self.reg_tcg()}_srcbase;
            const intptr_t {self.hvx_off()} =
                offsetof(CPUHexagonState, {self.reg_tcg()});
            intptr_t {self.reg_tcg()}_srcoff =
                vreg_src_off(ctx, {self.reg_num}, &{self.reg_tcg()}_srcbase);
            tcg_gen_gvec_mov_var(MO_64, {self.hvx_base()}, {self.hvx_off()},
                                 {self.reg_tcg()}_srcbase,
                                 {self.reg_tcg()}_srcoff,
                                 sizeof(MMVector), sizeof(MMVector));
            {self.reg_tcg()}_srcoff =
                vreg_src_off(ctx, {self.reg_num} ^ 1,
                             &{self.reg_tcg()}_srcbase);
            tcg_gen_gvec_mov_var(MO_64, {self.hvx_base()},
                                 {self.hvx_off()} + sizeof(MMVector),
                                 {self.reg_tcg()}_srcbase,
                                 {self.reg_tcg()}_srcoff,
                                 sizeof(MMVector), sizeof(MMVector));
        """))
        if not skip_qemu_helper(tag):
            self.decl_tcg_ptr(f)
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMVectorPair *)({self.helper_arg_name()}) */
        """))
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_vreg_read_pair(ctx, {self.reg_num}, insn_has_hvx_helper);
        """))

class VRegPairReadWrite(Register, Hvx, ReadWrite):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_ptr {self.hvx_base()} = tcg_env;
            TCGv_ptr {self.reg_tcg()}_srcbase;
            const intptr_t {self.hvx_off()} =
                offsetof(CPUHexagonState, {self.reg_tcg()});
            intptr_t {self.reg_tcg()}_srcoff =
                vreg_src_off(ctx, {self.reg_num}, &{self.reg_tcg()}_srcbase);
            tcg_gen_gvec_mov_var(MO_64, {self.hvx_base()}, {self.hvx_off()},
                                 {self.reg_tcg()}_srcbase,
                                 {self.reg_tcg()}_srcoff,
                                 sizeof(MMVector), sizeof(MMVector));
            {self.reg_tcg()}_srcoff =
                vreg_src_off(ctx, {self.reg_num} ^ 1,
                             &{self.reg_tcg()}_srcbase);
            tcg_gen_gvec_mov_var(MO_64, {self.hvx_base()},
                                 {self.hvx_off()} + sizeof(MMVector),
                                 {self.reg_tcg()}_srcbase,
                                 {self.reg_tcg()}_srcoff,
                                 sizeof(MMVector), sizeof(MMVector));
        """))
        if not skip_qemu_helper(tag):
            self.decl_tcg_ptr(f)
        self.gen_clear_ext_pair(f, tag)
    def gen_zero(self, f, tag):
        f.write(code_fmt(f"""\
            tcg_gen_gvec_dup_imm_var(MO_64, {self.hvx_base()}, {self.hvx_off()},
                sizeof(MMVectorPair), sizeof(MMVectorPair), 0);
        """))
        self.gen_clear_ext_pair(f, tag)
    def gen_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_vreg_write_pair(ctx, {self.hvx_base()}, {self.hvx_off()},
                                {self.reg_num}, {hvx_newv(tag)});
        """))
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMVectorPair *)({self.helper_arg_name()}) */
        """))
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_vreg_read_pair(ctx, {self.reg_num}, insn_has_hvx_helper);
        """))
    def analyze_write(self, f, tag, regno):
        newv = hvx_newv(tag)
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_vreg_write_pair(ctx, {self.reg_num}, {newv}, {predicated},
                                    insn_has_hvx_helper);
        """))

class QRegDest(Register, Hvx, Dest):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_ptr {self.hvx_base()};
            const intptr_t {self.hvx_off()} =
                get_result_qreg(ctx, {self.reg_num}, &{self.hvx_base()});
        """))
        if not skip_qemu_helper(tag):
            self.decl_tcg_ptr(f)
    def gen_write(self, f, tag):
        pass
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMQReg *)({self.helper_arg_name()}) */
        """))
    def analyze_write(self, f, tag, regno):
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_qreg_write(ctx, {self.reg_num}, {predicated},
                               insn_has_hvx_helper);
        """))

class QRegSource(Register, Hvx, OldSource):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_ptr {self.hvx_base()} = hex_hvx_ptr;
            const intptr_t {self.hvx_off()} =
                HEX_HVX_OFFSET(QRegs[{self.reg_num}]);
        """))
        if not skip_qemu_helper(tag):
            self.decl_tcg_ptr(f)
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMQReg *)({self.helper_arg_name()}) */
        """))
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_qreg_read(ctx, {self.reg_num}, insn_has_hvx_helper);
        """))

class QRegReadWrite(Register, Hvx, ReadWrite):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_ptr {self.hvx_base()};
            const intptr_t {self.hvx_off()} =
                get_result_qreg(ctx, {self.reg_num}, &{self.hvx_base()});
            tcg_gen_gvec_mov_var(MO_64, {self.hvx_base()}, {self.hvx_off()},
                                 hex_hvx_ptr,
                                 HEX_HVX_OFFSET(QRegs[{self.reg_num}]),
                                 sizeof(MMQReg), sizeof(MMQReg));
        """))
        if not skip_qemu_helper(tag):
            self.decl_tcg_ptr(f)
    def gen_write(self, f, tag):
        pass
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMQReg *)({self.helper_arg_name()}) */
        """))
    def analyze_read(self, f, regno):
        f.write(code_fmt(f"""\
            ctx_log_qreg_read(ctx, {self.reg_num}, insn_has_hvx_helper);
        """))
    def analyze_write(self, f, tag, regno):
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_qreg_write(ctx, {self.reg_num}, {predicated},
                               insn_has_hvx_helper);
        """))

# HLX has sixteen 2048-byte L registers.
class Hlx:
    def is_scalar_reg(self):
        return False
    def is_hvx_reg(self):
        return False
    def is_hlx_reg(self):
        return True
    def hlx_off(self):
        return f"{self.reg_tcg()}_off"
    def helper_proto_type(self):
        return "ptr"
    def helper_arg_type(self):
        return "void *"
    def helper_arg_name(self):
        return f"{self.reg_tcg()}_void"
    def decl_reg_num(self, f, regno):
        # HLX sources are not logged, so this can be unused.
        f.write(code_fmt(f"""\
            const int {self.reg_num} G_GNUC_UNUSED = insn->regno[{regno}];
        """))

class LRegDest(Hlx, Register, Single, Dest):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        if "A_HLX_TMP" in attribdict[tag]:
            storage = "tmp_LRegs"
        else:
            storage = "future_LRegs"
        f.write(code_fmt(f"""\
            const intptr_t {self.hlx_off()} =
                offsetof(CPUHexagonState, {storage}[{self.reg_num}]);
        """))
        if "A_HLX_TMP" in attribdict[tag] and "A_CONDEXEC" in attribdict[tag]:
            f.write(code_fmt(f"""\
                gen_helper_hlx_copy_live_to_tmp(tcg_env,
                    tcg_constant_i32({self.reg_num}));
            """))
        if is_hlx_inline_tag(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()}_base = tcg_env;
            """))
        elif not skip_qemu_helper(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()} = tcg_temp_new_ptr();
                tcg_gen_addi_ptr({self.reg_tcg()}, tcg_env, {self.hlx_off()});
            """))
    def gen_write(self, f, tag):
        pass
    def helper_hlx_desc(self, f):
        desc = f"*(HLXVector *)({self.helper_arg_name()})"
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is {desc} */
        """))
    def analyze_write(self, f, tag, regno):
        if "A_HLX_TMP" in attribdict[tag]:
            f.write(code_fmt(f"""\
                ctx_log_lreg_tmp_write(ctx, {self.reg_num});
            """))
        else:
            f.write(code_fmt(f"""\
                ctx_log_lreg_write(ctx, {self.reg_num});
            """))

class LRegSource(Hlx, Register, Single, OldSource):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_ptr {self.reg_tcg()}_base;
            intptr_t {self.reg_tcg()}_off;
            if (test_bit({self.reg_num}, ctx->lregs_updated_tmp)) {{
                {self.reg_tcg()}_base = tcg_env;
                {self.reg_tcg()}_off =
                    offsetof(CPUHexagonState, tmp_LRegs[{self.reg_num}]);
            }} else {{
                {self.reg_tcg()}_base = ctx_get_hlx_base(ctx);
                {self.reg_tcg()}_off =
                    offsetof(HexagonHLXContext, LRegs[{self.reg_num}]);
            }}
        """))
        if not is_hlx_inline_tag(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()} = tcg_temp_new_ptr();
                tcg_gen_addi_ptr({self.reg_tcg()}, {self.reg_tcg()}_base,
                                 {self.reg_tcg()}_off);
            """))
    def helper_hlx_desc(self, f):
        desc = f"*(HLXVector *)({self.helper_arg_name()})"
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is {desc} */
        """))
    def analyze_read(self, f, regno):
        pass

class LRegNewSource(Hlx, Register, Single, NewSource):
    def decl_tcg(self, f, tag, regno):
        if skip_qemu_helper(tag):
            self.decl_reg_num(f, regno)
            f.write(code_fmt(f"""\
                const intptr_t {self.hlx_off()} =
                    offsetof(CPUHexagonState, future_LRegs[{self.reg_num}]);
            """))
        else:
            # No reg_num variable here, it would clash with the TCGv_ptr
            f.write(code_fmt(f"""\
                const intptr_t {self.hlx_off()} =
                    offsetof(CPUHexagonState,
                             future_LRegs[insn->regno[{regno}]]);
                TCGv_ptr {self.reg_tcg()} = tcg_temp_new_ptr();
                tcg_gen_addi_ptr({self.reg_tcg()}, tcg_env, {self.hlx_off()});
            """))
    def helper_hlx_desc(self, f):
        desc = f"*(HLXVector *)({self.helper_arg_name()})"
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is {desc} */
        """))
    def analyze_read(self, f, regno):
        pass

class LRegReadWrite(Hlx, Register, Single, ReadWrite):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            const intptr_t {self.hlx_off()} =
                offsetof(CPUHexagonState, future_LRegs[{self.reg_num}]);
            if (test_bit({self.reg_num}, ctx->lregs_updated_tmp)) {{
                tcg_gen_gvec_mov(MO_64, {self.hlx_off()},
                                 offsetof(CPUHexagonState,
                                          tmp_LRegs[{self.reg_num}]),
                                 sizeof(HLXVector), sizeof(HLXVector));
            }} else {{
                gen_helper_hlx_copy_live_to_future(tcg_env,
                    tcg_constant_i32({self.reg_num}));
            }}
        """))
        if not skip_qemu_helper(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()} = tcg_temp_new_ptr();
                tcg_gen_addi_ptr({self.reg_tcg()}, tcg_env, {self.hlx_off()});
            """))
    def gen_write(self, f, tag):
        pass
    def helper_hlx_desc(self, f):
        desc = f"*(HLXVector *)({self.helper_arg_name()})"
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is {desc} */
        """))
    def analyze_read(self, f, regno):
        pass
    def analyze_write(self, f, tag, regno):
        f.write(code_fmt(f"""\
            ctx_log_lreg_write(ctx, {self.reg_num});
        """))

class LRegTmp(Hlx, Register, Single, ReadWrite):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            const intptr_t {self.hlx_off()} =
                offsetof(CPUHexagonState, tmp_LRegs[{self.reg_num}]);
        """))
        if is_hlx_inline_tag(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()}_base = tcg_env;
            """))
        elif not skip_qemu_helper(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()} = tcg_temp_new_ptr();
                tcg_gen_addi_ptr({self.reg_tcg()}, tcg_env, {self.hlx_off()});
            """))
    def gen_write(self, f, tag):
        pass
    def helper_hlx_desc(self, f):
        desc = f"*(HLXVector *)({self.helper_arg_name()})"
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is {desc} */
        """))
    def analyze_read(self, f, regno):
        pass
    def analyze_write(self, f, tag, regno):
        f.write(code_fmt(f"""\
            ctx_log_lreg_tmp_write(ctx, {self.reg_num});
        """))

# Pair classes expose base and offset for inline tags.
class LRegPairDest(Hlx, Register, Pair, Dest):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            const intptr_t {self.hlx_off()} =
                offsetof(CPUHexagonState, future_LRegs[{self.reg_num}]);
        """))
        if is_hlx_inline_tag(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()}_base = tcg_env;
            """))
        elif not skip_qemu_helper(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()} = tcg_temp_new_ptr();
                tcg_gen_addi_ptr({self.reg_tcg()}, tcg_env, {self.hlx_off()});
            """))
    def gen_write(self, f, tag):
        pass
    def helper_hlx_desc(self, f):
        f.write(code_fmt(f"""\
            /*
             * {self.reg_tcg()} is
             * *(HLXVectorPair *)({self.helper_arg_name()})
             */
        """))
    def analyze_write(self, f, tag, regno):
        f.write(code_fmt(f"""\
            ctx_log_lreg_write_pair(ctx, {self.reg_num});
        """))

class LRegPairSource(Hlx, Register, Pair, OldSource):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_ptr {self.reg_tcg()}_base;
            intptr_t {self.reg_tcg()}_off;
            if (test_bit({self.reg_num}, ctx->lregs_updated_tmp) ||
                test_bit({self.reg_num} + 1, ctx->lregs_updated_tmp)) {{
                /* A pair is contiguous, so load any missing .tmp half. */
                if (!test_bit({self.reg_num}, ctx->lregs_updated_tmp)) {{
                    gen_helper_hlx_copy_live_to_tmp(tcg_env,
                        tcg_constant_i32({self.reg_num}));
                }}
                if (!test_bit({self.reg_num} + 1, ctx->lregs_updated_tmp)) {{
                    gen_helper_hlx_copy_live_to_tmp(tcg_env,
                        tcg_constant_i32({self.reg_num} + 1));
                }}
                {self.reg_tcg()}_base = tcg_env;
                {self.reg_tcg()}_off =
                    offsetof(CPUHexagonState, tmp_LRegs[{self.reg_num}]);
            }} else {{
                {self.reg_tcg()}_base = ctx_get_hlx_base(ctx);
                {self.reg_tcg()}_off =
                    offsetof(HexagonHLXContext, LRegs[{self.reg_num}]);
            }}
        """))
        if not is_hlx_inline_tag(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()} = tcg_temp_new_ptr();
                tcg_gen_addi_ptr({self.reg_tcg()}, {self.reg_tcg()}_base,
                                 {self.reg_tcg()}_off);
            """))
    def helper_hlx_desc(self, f):
        f.write(code_fmt(f"""\
            /*
             * {self.reg_tcg()} is
             * *(HLXVectorPair *)({self.helper_arg_name()})
             */
        """))
    def analyze_read(self, f, regno):
        pass

class LRegPairReadWrite(Hlx, Register, Pair, ReadWrite):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            const intptr_t {self.hlx_off()} =
                offsetof(CPUHexagonState, future_LRegs[{self.reg_num}]);
            /* Use the .tmp value for each updated half. */
            for (int i = 0; i < 2; i++) {{
                if (test_bit({self.reg_num} + i, ctx->lregs_updated_tmp)) {{
                    tcg_gen_gvec_mov(MO_64,
                                     {self.hlx_off()} + i * sizeof(HLXVector),
                                     offsetof(CPUHexagonState,
                                              tmp_LRegs[{self.reg_num} + i]),
                                     sizeof(HLXVector), sizeof(HLXVector));
                }} else {{
                    gen_helper_hlx_copy_live_to_future(tcg_env,
                        tcg_constant_i32({self.reg_num} + i));
                }}
            }}
        """))
        if is_hlx_inline_tag(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()}_base = tcg_env;
            """))
        elif not skip_qemu_helper(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()} = tcg_temp_new_ptr();
                tcg_gen_addi_ptr({self.reg_tcg()}, tcg_env, {self.hlx_off()});
            """))
    def gen_write(self, f, tag):
        pass
    def helper_hlx_desc(self, f):
        f.write(code_fmt(f"""\
            /*
             * {self.reg_tcg()} is
             * *(HLXVectorPair *)({self.helper_arg_name()})
             */
        """))
    def analyze_read(self, f, regno):
        pass
    def analyze_write(self, f, tag, regno):
        f.write(code_fmt(f"""\
            ctx_log_lreg_write_pair(ctx, {self.reg_num});
        """))

class GuestRegister(Register):
    def gen_check_impl(self, f, regno):
        if self.is_written():
            f.write(code_fmt(f"""\
                if (!greg_writable(insn->regno[{regno}],
                    {str(self.is_pair()).lower()})) {{
                    return;
                }}
            """))
        else:
            f.write(code_fmt(f"""\
                check_greg_impl(insn->regno[{regno}],
                                {str(self.is_pair()).lower()});
            """))

class GuestDest(GuestRegister, Single, Dest):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        self.gen_check_impl(f, regno)
        f.write(code_fmt(f"""\
            TCGv_i32 {self.reg_tcg()} = tcg_temp_new_i32();
        """))
    def gen_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_log_greg_write(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_write(self, f, tag, regno):
        f.write(code_fmt(f"""\
            ctx_log_greg_write(ctx, {self.reg_num});
        """))

class GuestSource(GuestRegister, Single, OldSource):
    def decl_reg_num(self, f, regno):
        f.write(code_fmt(f"""\
            const int {self.reg_num} G_GNUC_UNUSED = insn->regno[{regno}];
        """))
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        self.gen_check_impl(f, regno)
        f.write(code_fmt(f"""\
            TCGv_i32 {self.reg_tcg()} = tcg_temp_new_i32();
            gen_read_greg(ctx, {self.reg_tcg()}, {self.reg_num});
        """))
    def analyze_read(self, f, regno):
        pass

class GuestPairDest(GuestRegister, Pair, Dest):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        self.gen_check_impl(f, regno)
        f.write(code_fmt(f"""\
            TCGv_i64 {self.reg_tcg()} = tcg_temp_new_i64();
        """))
    def gen_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_log_greg_write_pair(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_write(self, f, tag, regno):
        f.write(code_fmt(f"""\
            ctx_log_greg_write_pair(ctx, {self.reg_num});
        """))

class GuestPairSource(GuestRegister, Pair, OldSource):
    def decl_reg_num(self, f, regno):
        f.write(code_fmt(f"""\
            const int {self.reg_num} G_GNUC_UNUSED = insn->regno[{regno}];
        """))
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        self.gen_check_impl(f, regno)
        f.write(code_fmt(f"""\
            TCGv_i64 {self.reg_tcg()} = tcg_temp_new_i64();
            gen_read_greg_pair(ctx, {self.reg_tcg()}, {self.reg_num});
        """))
    def analyze_read(self, f, regno):
        pass

class SystemDest(Register, Single, Dest):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_i32 {self.reg_tcg()} = tcg_temp_new_i32();
        """))
    def gen_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_log_sreg_write(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_write(self, f, tag, regno):
        f.write(code_fmt(f"""\
            ctx_log_sreg_write(ctx, {self.reg_num});
        """))

class SystemSource(Register, Single, OldSource):
    def decl_reg_num(self, f, regno):
        f.write(code_fmt(f"""\
            const int {self.reg_num} G_GNUC_UNUSED = insn->regno[{regno}];
        """))
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_i32 {self.reg_tcg()} = tcg_temp_new_i32();
            gen_read_sreg(ctx, {self.reg_tcg()}, {self.reg_num});
        """))
    def analyze_read(self, f, regno):
        pass

class SystemPairDest(Register, Pair, Dest):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_i64 {self.reg_tcg()} = tcg_temp_new_i64();
        """))
    def gen_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_log_sreg_write_pair(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_write(self, f, tag, regno):
        f.write(code_fmt(f"""\
            ctx_log_sreg_write_pair(ctx, {self.reg_num});
        """))

class SystemPairSource(Register, Pair, OldSource):
    def decl_reg_num(self, f, regno):
        f.write(code_fmt(f"""\
            const int {self.reg_num} G_GNUC_UNUSED = insn->regno[{regno}];
        """))
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv_i64 {self.reg_tcg()} = tcg_temp_new_i64();
            gen_read_sreg_pair(ctx, {self.reg_tcg()}, {self.reg_num});
        """))
    def analyze_read(self, f, regno):
        pass

def init_registers():
    regs = {
        GprDest("R", "d"),
        GprDest("R", "e"),
        GprSource("R", "s"),
        GprSource("R", "t"),
        GprSource("R", "u"),
        GprSource("R", "v"),
        GprReadWrite("R", "x"),
        GprReadWrite("R", "y"),
        ControlDest("C", "d"),
        ControlSource("C", "s"),
        ModifierSource("M", "u"),
        PredDest("P", "d"),
        PredDest("P", "e"),
        PredSource("P", "s"),
        PredSource("P", "t"),
        PredSource("P", "u"),
        PredSource("P", "v"),
        PredReadWrite("P", "x"),
        PairDest("R", "dd"),
        PairDest("R", "ee"),
        PairSource("R", "ss"),
        PairSource("R", "tt"),
        PairReadWrite("R", "xx"),
        PairReadWrite("R", "yy"),
        ControlPairDest("C", "dd"),
        ControlPairSource("C", "ss"),
        VRegDest("V", "d"),
        VRegSource("V", "s"),
        VRegSource("V", "u"),
        VRegSource("V", "v"),
        VRegSource("V", "w"),
        VRegReadWrite("V", "x"),
        VRegTmp("V", "y"),
        VRegPairDest("V", "dd"),
        VRegPairSource("V", "uu"),
        VRegPairSource("V", "vv"),
        VRegPairReadWrite("V", "xx"),
        QRegDest("Q", "d"),
        QRegDest("Q", "e"),
        QRegSource("Q", "s"),
        QRegSource("Q", "t"),
        QRegSource("Q", "u"),
        QRegSource("Q", "v"),
        QRegReadWrite("Q", "x"),

        # HLX regs
        LRegDest("L", "d"),
        LRegSource("L", "s"),
        LRegSource("L", "u"),
        LRegSource("L", "v"),
        LRegSource("L", "w"),
        LRegReadWrite("L", "x"),
        LRegTmp("L", "y"),
        LRegPairDest("L", "dd"),
        LRegPairSource("L", "uu"),
        LRegPairSource("L", "vv"),
        LRegPairSource("L", "ss"),
        LRegPairReadWrite("L", "xx"),

        # system regs
        GuestDest("G", "d"),
        GuestSource("G", "s"),
        GuestPairDest("G", "dd"),
        GuestPairSource("G", "ss"),
        SystemDest("S", "d"),
        SystemSource("S", "s"),
        SystemPairDest("S", "dd"),
        SystemPairSource("S", "ss"),
    }
    for reg in regs:
        registers[f"{reg.regtype}{reg.regid}"] = reg

    new_regs = {
        GprNewSource("N", "s"),
        GprNewSource("N", "t"),
        PredNewSource("P", "t"),
        PredNewSource("P", "u"),
        PredNewSource("P", "v"),
        VRegNewSource("O", "s"),
        LRegNewSource("K", "u"),
    }
    for reg in new_regs:
        new_registers[f"{reg.regtype}{reg.regid}"] = reg

def is_new_reg(tag, regid):
    if regid[0] in "NOK":
        return True
    return regid[0] == "P" and \
           f"{regid}N" in semdict[tag] and \
           f"{regid}V" not in semdict[tag]

def get_register(tag, regtype, regid, subtype=""):
    regid = f"{regtype}{regid}"
    is_new = is_new_reg(tag, regid)
    try:
        reg = new_registers[regid] if is_new else registers[regid]
    except KeyError:
        raise Exception(f"Unknown {'new ' if is_new else ''}register {regid}" +\
                        f"from '{tag}' with syntax '{semdict[tag]}'") from None
    return reg

def helper_ret_type(tag, regs):
    ## If there is a scalar result, it is the return type
    return_type = HelperArg( "void", "void", "void")
    numscalarresults = 0
    for regtype, regid in regs:
        reg = get_register(tag, regtype, regid)
        if reg.is_written() and reg.is_scalar_reg():
            return_type = HelperArg(
                reg.helper_proto_type(),
                reg.reg_tcg(),
                reg.helper_arg_type()
            )
    if numscalarresults > 1:
        raise Exception("numscalarresults > 1")
    return return_type

def helper_args(tag, regs, imms):
    args = []

    ## First argument is the CPU state
    if need_env(tag):
        args.append(HelperArg(
            "env",
            "tcg_env",
            "CPUHexagonState *env"
        ))

    ## For predicated instructions, we pass in the destination register
    if is_predicated(tag):
        for regtype, regid in regs:
            reg = get_register(tag, regtype, regid)
            is_vector = reg.is_hvx_reg() or reg.is_hlx_reg()
            if reg.is_writeonly() and not is_vector:
                args.append(reg.helper_arg())

    ## Pass the HVX destination registers
    for regtype, regid in regs:
        reg = get_register(tag, regtype, regid)
        if reg.is_written() and (reg.is_hvx_reg() or reg.is_hlx_reg()):
            args.append(reg.helper_arg())

    ## Pass the source registers
    for regtype, regid in regs:
        reg = get_register(tag, regtype, regid)
        is_vector_readwrite = ((reg.is_hvx_reg() or reg.is_hlx_reg()) and
                               reg.is_readwrite())
        if reg.is_read() and not is_vector_readwrite:
            args.append(reg.helper_arg())

    ## Pass the immediates
    for immlett, bits, immshift in imms:
        args.append(HelperArg(
            "s32",
            f"tcg_constant_tl({imm_name(immlett)})",
            f"int32_t {imm_name(immlett)}"
        ))

    ## Other stuff the helper might need
    if need_pkt_has_multi_cof(tag):
        args.append(HelperArg(
            "i32",
            "tcg_constant_tl(ctx->pkt.pkt_has_multi_cof)",
            "uint32_t pkt_has_multi_cof"
        ))
    if need_pkt_need_commit(tag):
        args.append(HelperArg(
            "i32",
            "tcg_constant_tl(ctx->need_commit)",
            "uint32_t pkt_need_commit"
        ))
    if need_PC(tag):
        args.append(HelperArg(
            "i32",
            "tcg_constant_tl(ctx->pkt.pc)",
            "target_ulong PC"
        ))
    if need_next_PC(tag):
        args.append(HelperArg(
            "i32",
            "tcg_constant_tl(ctx->next_PC)",
            "target_ulong next_PC"
        ))
    if need_p0(tag):
        args.append(HelperArg(
            "i32",
            "hex_pred[0]",
            "uint32_t P0"
        ))
    if need_sp(tag):
        args.append(HelperArg(
            "i32",
            "hex_gpr[HEX_REG_SP]",
            "uint32_t SP"
        ))
    if need_slot(tag):
        args.append(HelperArg(
            "i32",
            "tcg_constant_i32((ctx->pkt.pkt_has_scalar_store_s1 & 1) | "
            "(ctx->insn->slot << 1))",
            "uint32_t slotval"
        ))
    if need_part1(tag):
        args.append(HelperArg(
            "i32",
            "tcg_constant_tl(insn->part1)"
            "uint32_t part1"
        ))
    return args


def parse_common_args(desc):
    parser = argparse.ArgumentParser(desc)
    parser.add_argument("semantics", help="semantics file")
    parser.add_argument("overrides", help="overrides file")
    parser.add_argument("overrides_vec", help="vector overrides file")
    parser.add_argument("overrides_sys", help="system overrides file")
    parser.add_argument("out", help="output file")
    parser.add_argument("--idef-parser",
                        help="file of instructions translated by idef-parser")
    args = parser.parse_args()
    read_semantics_file(args.semantics)
    read_overrides_file(args.overrides)
    read_overrides_file(args.overrides_vec)
    read_overrides_file(args.overrides_sys)
    if args.idef_parser:
        read_idef_parser_enabled_file(args.idef_parser)
    calculate_attribs()
    init_registers()
    return args
