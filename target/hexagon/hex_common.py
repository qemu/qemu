#!/usr/bin/env python3

##
##  Copyright(c) 2019-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

behdict = {}  # tag ->behavior
semdict = {}  # tag -> semantics
attribdict = {}  # tag -> attributes
macros = {}  # macro -> macro information...
attribinfo = {}  # Register information and misc
registers = {}  # register -> register functions
new_registers = {}
tags = []  # list of all tags
overrides = {}  # tags with helper overrides
idef_parser_enabled = {}  # tags enabled for idef-parser

# We should do this as a hash for performance,
# but to keep order let's keep it as a list.
def uniquify(seq):
    seen = set()
    seen_add = seen.add
    return [x for x in seq if x not in seen and not seen_add(x)]


regre = re.compile(r"((?<!DUP)[MNORCPQXSGVZA])([stuvwxyzdefg]+)([.]?[LlHh]?)(\d+S?)")
immre = re.compile(r"[#]([rRsSuUm])(\d+)(?:[:](\d+))?")
reg_or_immre = re.compile(
    r"(((?<!DUP)[MNRCOPQXSGVZA])([stuvwxyzdefg]+)"
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
    add_qemu_macro_attrib("fLOAD", "A_SCALAR_LOAD")
    add_qemu_macro_attrib("fSTORE", "A_SCALAR_STORE")
    add_qemu_macro_attrib('fLSBNEW0', 'A_IMPLICIT_READS_P0')
    add_qemu_macro_attrib('fLSBNEW0NOT', 'A_IMPLICIT_READS_P0')
    add_qemu_macro_attrib('fREAD_P0', 'A_IMPLICIT_READS_P0')
    add_qemu_macro_attrib('fLSBNEW1', 'A_IMPLICIT_READS_P1')
    add_qemu_macro_attrib('fLSBNEW1NOT', 'A_IMPLICIT_READS_P1')
    add_qemu_macro_attrib('fREAD_P3', 'A_IMPLICIT_READS_P3')

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


def need_slot(tag):
    if (
        "A_CVI_SCATTER" not in attribdict[tag]
        and "A_CVI_GATHER" not in attribdict[tag]
        and ("A_STORE" in attribdict[tag]
             or "A_LOAD" in attribdict[tag])
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
    return "A_CALL" in attribdict[tag]


def need_pkt_has_multi_cof(tag):
    return "A_COF" in attribdict[tag]


def need_pkt_need_commit(tag):
    return 'A_IMPLICIT_WRITES_USR' in attribdict[tag]


def skip_qemu_helper(tag):
    return tag in overrides.keys()


def is_idef_parser_enabled(tag):
    return tag in idef_parser_enabled


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


def read_attribs_file(name):
    attribre = re.compile(
        r"DEF_ATTRIB\(([A-Za-z0-9_]+), ([^,]*), "
        + r'"([A-Za-z0-9_\.]*)", "([A-Za-z0-9_\.]*)"\)'
    )
    for line in open(name, "rt").readlines():
        if not attribre.match(line):
            continue
        (attrib_base, descr, rreg, wreg) = attribre.findall(line)[0]
        attrib_base = "A_" + attrib_base
        attribinfo[attrib_base] = {"rreg": rreg, "wreg": wreg, "descr": descr}


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
    def helper_arg_name(self):
        return self.reg_tcg()

class Single(Scalar):
    def helper_proto_type(self):
        return "s32"
    def helper_arg_type(self):
        return "int32_t"

class Pair(Scalar):
    def helper_proto_type(self):
        return "s64"
    def helper_arg_type(self):
        return "int64_t"

class Hvx:
    def is_scalar_reg(self):
        return False
    def is_hvx_reg(self):
        return True
    def hvx_off(self):
        return f"{self.reg_tcg()}_off"
    def helper_proto_type(self):
        return "ptr"
    def helper_arg_type(self):
        return "void *"
    def helper_arg_name(self):
        return f"{self.reg_tcg()}_void"

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

class NewSource(Source):
    def reg_tcg(self):
        return f"{self.regtype}{self.regid}N"

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

class GprDest(Register, Single, Dest):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv {self.reg_tcg()} = get_result_gpr(ctx, {self.reg_num});
        """))
    def log_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_log_reg_write(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_write(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_reg_write(ctx, {self.reg_num}, {predicated});
        """))

class GprSource(Register, Single, OldSource):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv {self.reg_tcg()} = hex_gpr[{self.reg_num}];
        """))
    def analyze_read(self, f, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            ctx_log_reg_read(ctx, {self.reg_num});
        """))

class GprNewSource(Register, Single, NewSource):
    def decl_tcg(self, f, tag, regno):
        f.write(code_fmt(f"""\
            TCGv {self.reg_tcg()} = get_result_gpr(ctx, insn->regno[{regno}]);
        """))
    def analyze_read(self, f, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            ctx_log_reg_read(ctx, {self.reg_num});
        """))

class GprReadWrite(Register, Single, ReadWrite):
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
    def log_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_log_reg_write(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_write(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_reg_write(ctx, {self.reg_num}, {predicated});
        """))

class ControlDest(Register, Single, Dest):
    def decl_reg_num(self, f, regno):
        f.write(code_fmt(f"""\
            const int {self.reg_num} = insn->regno[{regno}]  + HEX_REG_SA0;
        """))
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv {self.reg_tcg()} = get_result_gpr(ctx, {self.reg_num});
        """))
    def log_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_write_ctrl_reg(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_write(self, f, tag, regno):
        self.decl_reg_num(f, regno)
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
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            ctx_log_reg_read(ctx, {self.reg_num});
        """))

class ModifierSource(Register, Single, OldSource):
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
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            ctx_log_reg_read(ctx, {self.reg_num});
        """))

class PredDest(Register, Single, Dest):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv {self.reg_tcg()} = tcg_temp_new();
        """))
    def log_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_log_pred_write(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_write(self, f, tag, regno):
        self.decl_reg_num(f, regno)
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
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            ctx_log_pred_read(ctx, {self.reg_num});
        """))

class PredNewSource(Register, Single, NewSource):
    def decl_tcg(self, f, tag, regno):
        f.write(code_fmt(f"""\
            TCGv {self.reg_tcg()} = get_result_pred(ctx, insn->regno[{regno}]);
        """))
    def analyze_read(self, f, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            ctx_log_pred_read(ctx, {self.reg_num});
        """))

class PredReadWrite(Register, Single, ReadWrite):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            TCGv {self.reg_tcg()} = tcg_temp_new();
            tcg_gen_mov_tl({self.reg_tcg()}, hex_pred[{self.reg_num}]);
        """))
    def log_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_log_pred_write(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_write(self, f, tag, regno):
        self.decl_reg_num(f, regno)
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
    def log_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_log_reg_write_pair(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_write(self, f, tag, regno):
        self.decl_reg_num(f, regno)
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
        self.decl_reg_num(f, regno)
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
    def log_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_log_reg_write_pair(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_write(self, f, tag, regno):
        self.decl_reg_num(f, regno)
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
    def log_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_write_ctrl_reg_pair(ctx, {self.reg_num}, {self.reg_tcg()});
        """))
    def analyze_write(self, f, tag, regno):
        self.decl_reg_num(f, regno)
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
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            ctx_log_reg_read_pair(ctx, {self.reg_num});
        """))

class VRegDest(Register, Hvx, Dest):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            const intptr_t {self.hvx_off()} =
                {vreg_offset_func(tag)}(ctx, {self.reg_num}, 1, true);
        """))
        if not skip_qemu_helper(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()} = tcg_temp_new_ptr();
                tcg_gen_addi_ptr({self.reg_tcg()}, tcg_env, {self.hvx_off()});
            """))
    def log_write(self, f, tag):
        pass
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMVector *)({self.helper_arg_name()}) */
        """))
    def analyze_write(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        newv = hvx_newv(tag)
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_vreg_write(ctx, {self.reg_num}, {newv}, {predicated});
        """))

class VRegSource(Register, Hvx, OldSource):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            const intptr_t {self.hvx_off()} = vreg_src_off(ctx, {self.reg_num});
        """))
        if not skip_qemu_helper(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()} = tcg_temp_new_ptr();
                tcg_gen_addi_ptr({self.reg_tcg()}, tcg_env, {self.hvx_off()});
            """))
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMVector *)({self.helper_arg_name()}) */
        """))
    def analyze_read(self, f, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            ctx_log_vreg_read(ctx, {self.reg_num});
        """))

class VRegNewSource(Register, Hvx, NewSource):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        if skip_qemu_helper(tag):
            f.write(code_fmt(f"""\
                const intptr_t {self.hvx_off()} =
                    ctx_future_vreg_off(ctx, {self.reg_num}, 1, true);
            """))
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMVector *)({self.helper_arg_name()}) */
        """))
    def analyze_read(self, f, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            ctx_log_vreg_read(ctx, {self.reg_num});
        """))

class VRegReadWrite(Register, Hvx, ReadWrite):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            const intptr_t {self.hvx_off()} =
                {vreg_offset_func(tag)}(ctx, {self.reg_num}, 1, true);
            tcg_gen_gvec_mov(MO_64, {self.hvx_off()},
                             vreg_src_off(ctx, {self.reg_num}),
                             sizeof(MMVector), sizeof(MMVector));
        """))
        if not skip_qemu_helper(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()} = tcg_temp_new_ptr();
                tcg_gen_addi_ptr({self.reg_tcg()}, tcg_env, {self.hvx_off()});
            """))
    def log_write(self, f, tag):
        pass
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMVector *)({self.helper_arg_name()}) */
        """))
    def analyze_write(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        newv = hvx_newv(tag)
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_vreg_write(ctx, {self.reg_num}, {newv}, {predicated});
        """))

class VRegTmp(Register, Hvx, ReadWrite):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            const intptr_t {self.hvx_off()} = offsetof(CPUHexagonState, vtmp);
        """))
        if not skip_qemu_helper(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()} = tcg_temp_new_ptr();
                tcg_gen_addi_ptr({self.reg_tcg()}, tcg_env, {self.hvx_off()});
                tcg_gen_gvec_mov(MO_64, {self.hvx_off()},
                                 vreg_src_off(ctx, {self.reg_num}),
                                 sizeof(MMVector), sizeof(MMVector));
            """))
    def log_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_log_vreg_write(ctx, {self.hvx_off()}, {self.reg_num},
                               {hvx_newv(tag)});
        """))
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMVector *)({self.helper_arg_name()}) */
        """))
    def analyze_write(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        newv = hvx_newv(tag)
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_vreg_write(ctx, {self.reg_num}, {newv}, {predicated});
        """))

class VRegPairDest(Register, Hvx, Dest):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            const intptr_t {self.hvx_off()} =
                {vreg_offset_func(tag)}(ctx, {self.reg_num}, 2, true);
        """))
        if not skip_qemu_helper(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()} = tcg_temp_new_ptr();
                tcg_gen_addi_ptr({self.reg_tcg()}, tcg_env, {self.hvx_off()});
            """))
    def log_write(self, f, tag):
        pass
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMVectorPair *)({self.helper_arg_name()}) */
        """))
    def analyze_write(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        newv = hvx_newv(tag)
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_vreg_write_pair(ctx, {self.reg_num}, {newv}, {predicated});
        """))

class VRegPairSource(Register, Hvx, OldSource):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            const intptr_t {self.hvx_off()} =
                offsetof(CPUHexagonState, {self.reg_tcg()});
            tcg_gen_gvec_mov(MO_64, {self.hvx_off()},
                             vreg_src_off(ctx, {self.reg_num}),
                             sizeof(MMVector), sizeof(MMVector));
            tcg_gen_gvec_mov(MO_64, {self.hvx_off()} + sizeof(MMVector),
                             vreg_src_off(ctx, {self.reg_num} ^ 1),
                             sizeof(MMVector), sizeof(MMVector));
        """))
        if not skip_qemu_helper(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()} = tcg_temp_new_ptr();
                tcg_gen_addi_ptr({self.reg_tcg()}, tcg_env, {self.hvx_off()});
            """))
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMVectorPair *)({self.helper_arg_name()}) */
        """))
    def analyze_read(self, f, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            ctx_log_vreg_read_pair(ctx, {self.reg_num});
        """))

class VRegPairReadWrite(Register, Hvx, ReadWrite):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            const intptr_t {self.hvx_off()} =
                offsetof(CPUHexagonState, {self.reg_tcg()});
            tcg_gen_gvec_mov(MO_64, {self.hvx_off()},
                             vreg_src_off(ctx, {self.reg_num}),
                             sizeof(MMVector), sizeof(MMVector));
            tcg_gen_gvec_mov(MO_64, {self.hvx_off()} + sizeof(MMVector),
                             vreg_src_off(ctx, {self.reg_num} ^ 1),
                             sizeof(MMVector), sizeof(MMVector));
        """))
        if not skip_qemu_helper(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()} = tcg_temp_new_ptr();
                tcg_gen_addi_ptr({self.reg_tcg()}, tcg_env, {self.hvx_off()});
            """))
    def log_write(self, f, tag):
        f.write(code_fmt(f"""\
            gen_log_vreg_write_pair(ctx, {self.hvx_off()}, {self.reg_num},
                                    {hvx_newv(tag)});
        """))
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMVectorPair *)({self.helper_arg_name()}) */
        """))
    def analyze_write(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        newv = hvx_newv(tag)
        predicated = "true" if is_predicated(tag) else "false"
        f.write(code_fmt(f"""\
            ctx_log_vreg_write_pair(ctx, {self.reg_num}, {newv}, {predicated});
        """))

class QRegDest(Register, Hvx, Dest):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            const intptr_t {self.hvx_off()} =
                get_result_qreg(ctx, {self.reg_num});
        """))
        if not skip_qemu_helper(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()} = tcg_temp_new_ptr();
                tcg_gen_addi_ptr({self.reg_tcg()}, tcg_env, {self.hvx_off()});
            """))
    def log_write(self, f, tag):
        pass
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMQReg *)({self.helper_arg_name()}) */
        """))
    def analyze_write(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            ctx_log_qreg_write(ctx, {self.reg_num});
        """))

class QRegSource(Register, Hvx, OldSource):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            const intptr_t {self.hvx_off()} =
                offsetof(CPUHexagonState, QRegs[{self.reg_num}]);
        """))
        if not skip_qemu_helper(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()} = tcg_temp_new_ptr();
                tcg_gen_addi_ptr({self.reg_tcg()}, tcg_env, {self.hvx_off()});
            """))
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMQReg *)({self.helper_arg_name()}) */
        """))
    def analyze_read(self, f, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            ctx_log_qreg_read(ctx, {self.reg_num});
        """))

class QRegReadWrite(Register, Hvx, ReadWrite):
    def decl_tcg(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            const intptr_t {self.hvx_off()} =
                get_result_qreg(ctx, {self.reg_num});
            tcg_gen_gvec_mov(MO_64, {self.hvx_off()},
                             offsetof(CPUHexagonState, QRegs[{self.reg_num}]),
                             sizeof(MMQReg), sizeof(MMQReg));
        """))
        if not skip_qemu_helper(tag):
            f.write(code_fmt(f"""\
                TCGv_ptr {self.reg_tcg()} = tcg_temp_new_ptr();
                tcg_gen_addi_ptr({self.reg_tcg()}, tcg_env, {self.hvx_off()});
            """))
    def log_write(self, f, tag):
        pass
    def helper_hvx_desc(self, f):
        f.write(code_fmt(f"""\
            /* {self.reg_tcg()} is *(MMQReg *)({self.helper_arg_name()}) */
        """))
    def analyze_write(self, f, tag, regno):
        self.decl_reg_num(f, regno)
        f.write(code_fmt(f"""\
            ctx_log_qreg_write(ctx, {self.reg_num});
        """))

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
    }
    for reg in new_regs:
        new_registers[f"{reg.regtype}{reg.regid}"] = reg

def get_register(tag, regtype, regid):
    if f"{regtype}{regid}V" in semdict[tag]:
        return registers[f"{regtype}{regid}"]
    else:
        return new_registers[f"{regtype}{regid}"]

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
    args.append(HelperArg(
        "env",
        "tcg_env",
        "CPUHexagonState *env"
    ))

    ## For predicated instructions, we pass in the destination register
    if is_predicated(tag):
        for regtype, regid in regs:
            reg = get_register(tag, regtype, regid)
            if reg.is_writeonly() and not reg.is_hvx_reg():
                args.append(reg.helper_arg())

    ## Pass the HVX destination registers
    for regtype, regid in regs:
        reg = get_register(tag, regtype, regid)
        if reg.is_written() and reg.is_hvx_reg():
            args.append(reg.helper_arg())

    ## Pass the source registers
    for regtype, regid in regs:
        reg = get_register(tag, regtype, regid)
        if reg.is_read() and not (reg.is_hvx_reg() and reg.is_readwrite()):
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
            "tcg_constant_tl(ctx->pkt->pkt_has_multi_cof)",
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
            "tcg_constant_tl(ctx->pkt->pc)",
            "target_ulong PC"
        ))
    if need_next_PC(tag):
        args.append(HelperArg(
            "i32",
            "tcg_constant_tl(ctx->next_PC)",
            "target_ulong next_PC"
        ))
    if need_slot(tag):
        args.append(HelperArg(
            "i32",
            "gen_slotval(ctx)",
            "uint32_t slotval"
        ))
    if need_part1(tag):
        args.append(HelperArg(
            "i32",
            "tcg_constant_tl(insn->part1)"
            "uint32_t part1"
        ))
    return args
