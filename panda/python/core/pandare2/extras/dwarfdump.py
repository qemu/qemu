#!/usr/bin/env python3

import sys
import json

# dwarfdump -dil $PROG

def parse_die(ent):
    result = {}
    for e in ent.split('> ')[1:]:
        while e.endswith('>'):
            e = e[:-1]
        assert (e.startswith('DW_AT_'))
        dat = e.split('<')
        attr = dat[0].strip()
        for v in dat[1:]:
            v = v.strip()
            if v:
                result[attr] = v
    return result

def parse_section(indat):
    result = {'.debug_line': [], '.debug_info': []}
    data = indat.decode().strip().split('\n')
    for l in data:
        l = l.strip()
        if l.startswith("0x"):
            result['.debug_line'].append(l)
            # Signal End of Text
            if 'ET' in l.split():
                result['.debug_line'].append(None)
        elif l.startswith("<") and not l.startswith("<pc>"):
            result['.debug_info'].append(l)
    return result

def reprocess_ops(ops):
    out = []
    for op in ops:
        if op.startswith('DW_'):
            out.append(op)
        elif type(op) == str:
            if op.lstrip('-+').startswith("0x"):
                out.append(int(op, 16))
            else:
                out.append(int(op))
        else:
            out.append(op)
    return out


class TypeDB(object):
    def __init__(self):
        self.data = {}

    def insert(self, cu, off, ty):
        if cu not in self.data:
            self.data[cu] = {}
        if off not in self.data[cu]:
            self.data[cu][off] = ty

    def jsondump(self):
        jout = {}
        for cu in self.data:
            jout[cu] = {}
            for off in self.data[cu]:
                jout[cu][off] = self.data[cu][off].jsondump()
        #return json.dumps(jout)
        return jout

class LineDB(object):
    def __init__(self):
        self.data = {}

    def _find_best_fit(self, srcfn, lno, addr):
        r = [-1, -1]
        for i in range(len(self.data[srcfn])):
            if self.data[srcfn][i].lno == lno:
                if r[1] < self.data[srcfn][i].highpc and addr > self.data[srcfn][i].highpc:
                    r = [i, self.data[srcfn][i].highpc]
        return r[0]

    def insert(self, srcfn, lno, col, addr, func=None):
        if srcfn not in self.data:
            self.data[srcfn] = []

        if self.data[srcfn] and lno < self.data[srcfn][-1].lno:
            i = -1
        else:
            i = self._find_best_fit(srcfn, lno, addr)
        if i == -1:
            self.data[srcfn].append(LineRange(lno, col, addr, addr, func))
            self.data[srcfn].sort(key = lambda x: x.lno)

        prevlno = lno-1
        i = self._find_best_fit(srcfn, prevlno, addr)
        while prevlno > 0 and i == -1:
            prevlno -= 1
            i = self._find_best_fit(srcfn, prevlno, addr)
        if i != -1:
            self.data[srcfn][i].highpc = addr

    def update_function(self, base_addr, end_addr, finfo):
        srcinfo = None
        for srcfn in self.data:
            for i in range(len(self.data[srcfn])):
                if self.data[srcfn][i].lowpc == base_addr:
                    srcinfo = (srcfn, self.data[srcfn][i].lno)
                if self.data[srcfn][i].lowpc >= base_addr \
                        and self.data[srcfn][i].highpc < end_addr:
                    self.data[srcfn][i].func = finfo.scope.lowpc
        return srcinfo

    def jsondump(self):
        jout = {}
        for srcfn in self.data:
            key = srcfn
            while srcfn[0] in ['"', "'"]:
                srcfn = srcfn[1:]
            while srcfn[-1] in ['"', "'"]:
                srcfn = srcfn[:-1]
            jout[srcfn] = []
            for lr in self.data[key]:
                jout[srcfn].append(lr.jsondump())
        #return json.dumps(jout)
        return jout

class GlobVarDB(object):
    def __init__(self):
        self.data = {}

    def insert(self, cu, var):
        if cu not in self.data:
            self.data[cu] = set()
        self.data[cu].add(var)

    def jsondump(self):
        jout = {}
        for cu in self.data:
            jout[cu] = [f.jsondump() for f in self.data[cu]]
        #return json.dumps(jout)
        return jout

class FunctionDB(object):
    def __init__(self):
        self.data = {}

    def insert(self, cu, f):
        if cu not in self.data:
            self.data[cu] = set()
        self.data[cu].add(f)

    def jsondump(self):
        jout = {}
        for cu in self.data:
            jout[cu] = [f.jsondump() for f in self.data[cu]]
        #return json.dumps(jout)
        return jout


class VarInfo(object):
    def __init__(self, name, cu_off):
        self.name = name
        self.cu_offset = cu_off
        self.scope = None
        self.decl_lno = None
        self.decl_fn = None
        self.loc_op = []
        self.type = None

    def jsondump(self):
        return {'name': self.name, \
                'cu_offset': self.cu_offset, \
                'scope': self.scope.jsondump(), \
                'decl_lno': self.decl_lno, \
                'decl_fn': self.decl_fn, \
                'loc_op': self.loc_op, \
                'type': self.type}

class FuncInfo(object):
    def __init__(self, cu_off, name, scope, fb_op):
        self.cu_offset = cu_off
        self.name = name
        self.scope = scope
        self.framebase = fb_op
        self.fn = None
        self.lno = None
        self.varlist = []

    def jsondump(self):
        return {'name': self.name, \
                'cu_offset': self.cu_offset, \
                'scope': self.scope.jsondump(), \
                'framebase': self.framebase, \
                'fn': self.fn, \
                'lno': self.lno, \
                'varlist': [v.jsondump() for v in self.varlist]}

class TypeInfo(object):
    def __init__(self, name):
        self.name = name

    def jsondump(self):
        return {'name': self.name}

class StructType(TypeInfo):
    def __init__(self, name, cu_off, size):
        TypeInfo.__init__(self, name)
        self.size = size
        self.cu_off = cu_off
        self.children = {}  # <member_offset: (name, type_offset)>

    def jsondump(self):
        d = TypeInfo.jsondump(self)
        d.update({
                'tag': 'StructType',
                'size': self.size,
                'cu_off': self.cu_off,
                'children': self.children,
                })
        return d

class BaseType(TypeInfo):
    def __init__(self, name, size):
        TypeInfo.__init__(self, name)
        self.size = size

    def jsondump(self):
        d = TypeInfo.jsondump(self)
        d.update({
                'tag': 'BaseType',
                'size': self.size,
                })
        return d

class SugarType(TypeInfo):
    def __init__(self, name, cu_off):
        TypeInfo.__init__(self, name)
        self.cu_off = cu_off
        self.ref = None

    def jsondump(self):
        d = TypeInfo.jsondump(self)
        d.update({
                'tag': 'SugarType',
                'cu_off': self.cu_off,
                'ref': self.ref,
                })
        return d

class PointerType(SugarType):
    def __init__(self, name, cu_off, target):
        SugarType.__init__(self, name, cu_off)
        self.ref = target

    def jsondump(self):
        d = SugarType.jsondump(self)
        d.update({
                'tag': 'PointerType',
                })
        return d

class ArrayType(SugarType):
    def __init__(self, name, cu_off, elemty):
        SugarType.__init__(self, name, cu_off)
        self.ref = elemty
        self.range = []

    def jsondump(self):
        d = SugarType.jsondump(self)
        d.update({
                'tag': 'ArrayType',
                'range': self.range,
                })
        return d

class ArrayRangeType(SugarType):
    def __init__(self, name, cu_off, rtype, cnt):
        SugarType.__init__(self, name, cu_off)
        self.ref = rtype
        self.size = cnt

    def jsondump(self):
        d = SugarType.jsondump(self)
        d.update({
                'tag': 'ArrayRangeType',
                'size': self.size,
                })
        return d

class EnumType(TypeInfo):
    def __init__(self, name, size):
        TypeInfo.__init__(self, name)
        self.size = size

    def jsondump(self):
        d = TypeInfo.jsondump(self)
        d.update({
                'tag': 'EnumType',
                'size': self.size,
                })
        return d

class SubroutineType(TypeInfo):
    def __init__(self, name):
        TypeInfo.__init__(self, name)

    def jsondump(self):
        d = TypeInfo.jsondump(self)
        d.update({
                'tag': 'SubroutineType',
                })
        return d

class UnionType(TypeInfo):
    def __init__(self, name, cu_off, size):
        TypeInfo.__init__(self, name)
        self.size = size
        self.cu_off = cu_off
        self.children = {}  # <member_offset: (name, type_offset)>

    def jsondump(self):
        d = TypeInfo.jsondump(self)
        d.update({
                'tag': 'UnionType',
                'size': self.size,
                'cu_off': self.cu_off,
                'children': self.children,
                })
        return d

class Scope(object):
    def __init__(self, lopc, hipc):
        self.lowpc = lopc
        self.highpc = hipc

    def jsondump(self):
        return {'lowpc': self.lowpc, 'highpc': self.highpc}

class LineRange(object):
    def __init__(self, lno, col, lopc, hipc, func):
        self.lno = lno
        self.col = col
        self.lowpc = lopc
        self.highpc = hipc
        self.func = func

    def jsondump(self):
        return {
                'lno': self.lno,
                'col': self.col,
                'lowpc': self.lowpc,
                'highpc': self.highpc,
                'func': self.func,
                }

def parse_dwarfdump(indat, prefix=""):
    reloc_base = 0
    line_info = LineDB()
    globvar_info = GlobVarDB()
    func_info = FunctionDB()
    type_info = TypeDB()

    data = parse_section(indat)
    tag = ".debug_line"
    if tag in data:
        srcname = ""
        for line in data[tag]:
            if line == None:
                srcname = ""
                continue
            line = line.strip()
            if line.startswith("0x"):
                addrstr, rest = line.split('[')
                lnostr, info = rest.split(']')
                if "uri:" in info:
                    srcfn = info.split("uri:")[-1].strip()
                assert (srcfn)
                addr = int(addrstr.strip(), 16) + reloc_base
                lno = int(lnostr.strip().split(',')[0])
                col = int(lnostr.strip().split(',')[1])
                line_info.insert(srcfn, lno, col, addr)

    type_overlay = None
    cu_off = None
    lvl_stack = []
    scope_stack = []
    func_stack = []
    type_stack = []
    tag = ".debug_info"
    if tag in data:
        for line in data[tag]:
            line = line.strip()
            print(line)
            if not line:
                continue
            if not line.startswith('<'):
                continue

            die = line.split(' ')[0].strip()
            assert (die.startswith('<') and die.endswith('>'))
            lvl, idx, tname = die[1:-1].split('><')
            lvl = int(lvl)

            res = parse_die(line)
            if "DW_TAG_compile_unit" in line:
                assert ('DW_AT_low_pc' in res)
                assert ('DW_AT_high_pc' in res)
                base_addr = int(res['DW_AT_low_pc'], 16) + reloc_base
                end_addr = int(res['DW_AT_high_pc'], 16) + reloc_base
                scope_stack = [Scope(base_addr, end_addr)]
                lvl_stack = [(lvl, 'DW_TAG_compile_unit')]
                func_stack = []
                type_stack = []
                cu_off = int(idx.split('+')[0], 16)
                continue

            idx = int(idx, 16)

            #print(lvl, idx, tname)

            while lvl < lvl_stack[-1][0]:
                lvl_stack.pop()
                if lvl_stack[-1][1] == 'DW_TAG_lexical_block':
                    scope_stack.pop()
                if lvl_stack[-1][1] == 'DW_TAG_subprogram':
                    func_stack.pop()
                if lvl_stack[-1][1] == 'DW_TAG_structure_type':
                    type_stack.pop()
                if lvl_stack[-1][1] == 'DW_TAG_union_type':
                    type_stack.pop()
                if lvl_stack[-1][1] == 'DW_TAG_array_type':
                    type_stack.pop()

            if lvl != lvl_stack[-1][0] and lvl != (lvl_stack[-1][0]+1):
                continue

            if lvl_stack[-1][1] in ['SugarType', 'DW_TAG_pointer_type']:
                assert (lvl == lvl_stack[-1][0])
                lvl_stack.pop()
                assert (type_overlay)
                type_overlay[2].ref = idx
                type_info.insert(type_overlay[0], type_overlay[1], type_overlay[2])
                type_overlay = None

            if tname == "DW_TAG_lexical_block":
                assert ('DW_AT_low_pc' in res)
                assert ('DW_AT_high_pc' in res)
                base_addr = int(res['DW_AT_low_pc'], 16) + reloc_base
                end_addr = int(res['DW_AT_high_pc'], 16) + reloc_base
                scope_stack.append(Scope(base_addr, end_addr))
                lvl_stack.append((lvl, 'DW_TAG_lexical_block'))

            elif tname == "DW_TAG_variable":
                assert ('DW_AT_name' in res)
                name = res['DW_AT_name']
                v = VarInfo(name, cu_off)

                v.scope = scope_stack[-1]
                assert ('DW_AT_decl_line' in res)
                v.decl_lno = int(res['DW_AT_decl_line'], 16)
                assert ('DW_AT_decl_file' in res)
                v.decl_fn = res['DW_AT_decl_file']
                v.decl_fn = v.decl_fn[v.decl_fn.find(' ')+1:]
                if 'DW_AT_location' not in res:
                    continue
                for x in res['DW_AT_location'].split(':')[-1].strip().split('DW_OP_'):
                    x = x.strip()
                    if not x:
                        continue
                    v.loc_op.extend('DW_OP_{}'.format(x).split())
                v.loc_op = reprocess_ops(v.loc_op)
                assert ('DW_AT_type' in res)
                v.type = int(res['DW_AT_type'], 16)

                if len(func_stack) == 0:
                    globvar_info.insert(cu_off, v)
                else:
                    func_stack[-1].varlist.append(v)

            elif tname == "DW_TAG_formal_parameter":
                if 'DW_AT_name' not in res:
                    continue
                name = res['DW_AT_name']
                v = VarInfo(name, cu_off)

                v.scope = scope_stack[-1]
                assert ('DW_AT_decl_line' in res)
                v.decl_lno = int(res['DW_AT_decl_line'], 16)
                assert ('DW_AT_decl_file' in res)
                v.decl_fn = res['DW_AT_decl_file']
                v.decl_fn = v.decl_fn[v.decl_fn.find(' ')+1:]
                if 'DW_AT_location' not in res:
                    continue
                for x in res['DW_AT_location'].split(':')[-1].strip().split('DW_OP_'):
                    x = x.strip()
                    if not x:
                        continue
                    v.loc_op.extend('DW_OP_{}'.format(x).split())
                v.loc_op = reprocess_ops(v.loc_op)
                assert ('DW_AT_type' in res)
                v.type = int(res['DW_AT_type'], 16)

                assert (len(func_stack) > 0)
                func_stack[-1].varlist.append(v)

            elif tname == "DW_TAG_subprogram":
                assert ('DW_AT_name' in res)
                name = res['DW_AT_name']

                assert ('DW_AT_low_pc' in res)
                assert ('DW_AT_high_pc' in res)
                base_addr = int(res['DW_AT_low_pc'], 16) + reloc_base
                end_addr = int(res['DW_AT_high_pc'], 16) + reloc_base
                scope = Scope(base_addr, end_addr)
                scope_stack.append(scope)
                lvl_stack.append((lvl, 'DW_TAG_subprogram'))

                assert ('DW_AT_decl_file' in res)
                decl_fn = res['DW_AT_decl_file']
                decl_fn = decl_fn[v.decl_fn.find(' ')+1:]

                if 'DW_AT_frame_base' in res:
                    fb_op = [res['DW_AT_frame_base'].split(':')[-1].strip()]
                else:
                    fb_op = []
                fb_op = reprocess_ops(fb_op)

                f = FuncInfo(cu_off, name, scope, fb_op)

                f.fn, f.lno = line_info.update_function(base_addr, end_addr, f)

                func_stack.append(f)
                func_info.insert(cu_off, f)

            elif tname == "DW_TAG_structure_type":
                if 'DW_AT_byte_size' not in res:
                    continue
                sz = int(res['DW_AT_byte_size'], 16)
                name = res['DW_AT_name'] if 'DW_AT_name' in res else "void"
                t = StructType(name, cu_off, sz)

                type_info.insert(cu_off, idx, t)

                type_stack.append(t)
                lvl_stack.append((lvl, 'DW_TAG_structure_type'))

            elif tname == "DW_TAG_member":
                assert (lvl_stack[-1][1] in ['DW_TAG_structure_type', 'DW_TAG_union_type'])

                name = res['DW_AT_name'] if 'DW_AT_name' in res else "void"

                # Skip bit fields
                if 'DW_AT_bit_size' in res or 'DW_AT_bit_offset' in res:
                    continue

                assert ('DW_AT_type' in res)
                toff = int(res['DW_AT_type'], 16)

                assert ('DW_AT_data_member_location' in res)
                loc_op = ['DW_OP_{}'.format(x.strip()) for x in \
                        res['DW_AT_data_member_location'].split(':')[-1].strip().split('DW_OP_')[1:]]
                # Signal attribute form DW_FORM_data1/2/4/8
                assert (len(loc_op) == 1)
                assert (loc_op[0].split()[0] == 'DW_OP_plus_uconst')
                off = int(loc_op[0].split()[1])

                type_stack[-1].children[off] = (name, toff)

            elif tname == "DW_TAG_array_type":
                name = res['DW_AT_name'] if 'DW_AT_name' in res else "void"
                assert ('DW_AT_type' in res)
                elemoff = int(res['DW_AT_type'], 16)

                t = ArrayType(name, cu_off, elemoff)

                type_info.insert(cu_off, idx, t)

                lvl_stack.append((lvl, 'DW_TAG_array_type'))
                type_stack.append(t)

            elif tname == "DW_TAG_subrange_type":
                name = res['DW_AT_name'] if 'DW_AT_name' in res else "void"
                assert ('DW_AT_type' in res)
                toff = int(res['DW_AT_type'], 16)
                assert ('DW_AT_count' in res)
                cnt = int(res['DW_AT_count'], 16)
                # cnt = int(res['DW_AT_upper_bound'], 16)

                t = ArrayRangeType(name, cu_off, toff, cnt)

                type_info.insert(cu_off, idx, t)

                assert (lvl_stack[-1][1] == 'DW_TAG_array_type')
                assert ((lvl_stack[-1][0]+1) == lvl)

                type_stack[-1].range.append(idx)

            elif tname == "DW_TAG_subroutine_type":
                name = res['DW_AT_name'] if 'DW_AT_name' in res else "void"
                t = SubroutineType(name)

                type_info.insert(cu_off, idx, t)

            elif tname == "DW_TAG_base_type":
                name = res['DW_AT_name'] if 'DW_AT_name' in res else "void"
                assert ('DW_AT_byte_size' in res)
                sz = int(res['DW_AT_byte_size'], 16)
                t = BaseType(name, sz)

                type_info.insert(cu_off, idx, t)

            elif tname == "DW_TAG_pointer_type":
                name = res['DW_AT_name'] if 'DW_AT_name' in res else "void"

                if 'DW_AT_type' not in res:
                    lvl_stack.append((lvl, 'DW_TAG_pointer_type'))
                    type_overlay = (cu_off, idx, PointerType(name, cu_off, None))
                    continue
                target = int(res['DW_AT_type'], 16)

                t = PointerType(name, cu_off, target)

                type_info.insert(cu_off, idx, t)

            elif tname == "DW_TAG_enumeration_type":
                name = res['DW_AT_name'] if 'DW_AT_name' in res else "void"

                assert ('DW_AT_byte_size' in res)
                sz = int(res['DW_AT_byte_size'], 16)

                t = EnumType(name, sz)

                type_info.insert(cu_off, idx, t)

            elif tname in [
                    "DW_TAG_restrict_type",
                    "DW_TAG_const_type",
                    "DW_TAG_volatile_type",
                    "DW_TAG_typedef"
                    ]:
                name = res['DW_AT_name'] if 'DW_AT_name' in res else "void"
                t = SugarType(name, cu_off)

                if 'DW_AT_type' not in res:
                    lvl_stack.append((lvl, 'SugarType'))
                    type_overlay = (cu_off, idx, t)
                    continue
                t.ref = int(res['DW_AT_type'], 16)

                type_info.insert(cu_off, idx, t)

            elif tname == "DW_TAG_union_type":
                name = res['DW_AT_name'] if 'DW_AT_name' in res else "void"
                assert ('DW_AT_byte_size' in res)
                sz = int(res['DW_AT_byte_size'], 16)
                t = UnionType(name, cu_off, sz)

                type_info.insert(cu_off, idx, t)

                type_stack.append(t)
                lvl_stack.append((lvl, 'DW_TAG_union_type'))

            elif tname == "DW_TAG_ptr_to_member_type":
                name = res['DW_AT_name'] if 'DW_AT_name' in res else "void"
                t = PointerType(name, cu_off, None)

                type_info.insert(cu_off, idx, t)

            elif tname == "DW_TAG_imported_declaration":
                pass
            elif tname == "DW_TAG_unspecified_parameters":
                pass
            elif tname == "DW_TAG_constant":
                pass

    with open(prefix+'_lineinfo.json', 'w') as fd:
        dump_json(fd, line_info)
    with open(prefix+'_globvar.json', 'w') as fd:
        dump_json(fd, globvar_info)
    with open(prefix+'_funcinfo.json', 'w') as fd:
        dump_json(fd, func_info)
    with open(prefix+'_typeinfo.json', 'w') as fd:
        dump_json(fd, type_info)

def dump_json(j, info):
    class DwarfJsonEncoder(json.JSONEncoder):
        def default(self, obj):
            if hasattr(obj, "jsondump"):
                return obj.jsondump()
            else:
                return json.JSONEncoder.default(self, obj)
    json.dump(info.jsondump(), j, cls=DwarfJsonEncoder)

if __name__ == '__main__':
    with open(sys.argv[1], 'r') as fd:
        parse_dwarfdump(fd.read())
