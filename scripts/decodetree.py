#!/usr/bin/env python3
# Copyright (c) 2018 Linaro Limited
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <http://www.gnu.org/licenses/>.
#

#
# Generate a decoding tree from a specification file.
# See the syntax and semantics in docs/devel/decodetree.rst.
#

import io
import os
import re
import sys
import getopt

insnwidth = 32
bitop_width = 32
insnmask = 0xffffffff
variablewidth = False
fields = {}
arguments = {}
formats = {}
allpatterns = []
anyextern = False
testforerror = False

translate_prefix = 'trans'
translate_scope = 'static '
input_file = ''
output_file = None
output_fd = None
output_null = False
insntype = 'uint32_t'
decode_function = 'decode'

# An identifier for C.
re_C_ident = '[a-zA-Z][a-zA-Z0-9_]*'

# Identifiers for Arguments, Fields, Formats and Patterns.
re_arg_ident = '&[a-zA-Z0-9_]*'
re_fld_ident = '%[a-zA-Z0-9_]*'
re_fmt_ident = '@[a-zA-Z0-9_]*'
re_pat_ident = '[a-zA-Z0-9_]*'

# Local implementation of a topological sort. We use the same API that
# the Python graphlib does, so that when QEMU moves forward to a
# baseline of Python 3.9 or newer this code can all be dropped and
# replaced with:
#    from graphlib import TopologicalSorter, CycleError
#
# https://docs.python.org/3.9/library/graphlib.html#graphlib.TopologicalSorter
#
# We only implement the parts of TopologicalSorter we care about:
#  ts = TopologicalSorter(graph=None)
#    create the sorter. graph is a dictionary whose keys are
#    nodes and whose values are lists of the predecessors of that node.
#    (That is, if graph contains "A" -> ["B", "C"] then we must output
#    B and C before A.)
#  ts.static_order()
#    returns a list of all the nodes in sorted order, or raises CycleError
#  CycleError
#    exception raised if there are cycles in the graph. The second
#    element in the args attribute is a list of nodes which form a
#    cycle; the first and last element are the same, eg [a, b, c, a]
#    (Our implementation doesn't give the order correctly.)
#
# For our purposes we can assume that the data set is always small
# (typically 10 nodes or less, actual links in the graph very rare),
# so we don't need to worry about efficiency of implementation.
#
# The core of this implementation is from
# https://code.activestate.com/recipes/578272-topological-sort/
# (but updated to Python 3), and is under the MIT license.

class CycleError(ValueError):
    """Subclass of ValueError raised if cycles exist in the graph"""
    pass

class TopologicalSorter:
    """Topologically sort a graph"""
    def __init__(self, graph=None):
        self.graph = graph

    def static_order(self):
        # We do the sort right here, unlike the stdlib version
        from functools import reduce
        data = {}
        r = []

        if not self.graph:
            return []

        # This code wants the values in the dict to be specifically sets
        for k, v in self.graph.items():
            data[k] = set(v)

        # Find all items that don't depend on anything.
        extra_items_in_deps = (reduce(set.union, data.values())
                               - set(data.keys()))
        # Add empty dependencies where needed
        data.update({item:{} for item in extra_items_in_deps})
        while True:
            ordered = set(item for item, dep in data.items() if not dep)
            if not ordered:
                break
            r.extend(ordered)
            data = {item: (dep - ordered)
                    for item, dep in data.items()
                        if item not in ordered}
        if data:
            # This doesn't give as nice results as the stdlib, which
            # gives you the cycle by listing the nodes in order. Here
            # we only know the nodes in the cycle but not their order.
            raise CycleError(f'nodes are in a cycle', list(data.keys()))

        return r
# end TopologicalSorter

def error_with_file(file, lineno, *args):
    """Print an error message from file:line and args and exit."""
    global output_file
    global output_fd

    # For the test suite expected-errors case, don't print the
    # string "error: ", so they don't turn up as false positives
    # if you grep the meson logs for strings like that.
    end = 'error: ' if not testforerror else 'detected: '
    prefix = ''
    if file:
        prefix += f'{file}:'
    if lineno:
        prefix += f'{lineno}:'
    if prefix:
        prefix += ' '
    print(prefix, end=end, file=sys.stderr)
    print(*args, file=sys.stderr)

    if output_file and output_fd:
        output_fd.close()
        os.remove(output_file)
    exit(0 if testforerror else 1)
# end error_with_file


def error(lineno, *args):
    error_with_file(input_file, lineno, *args)
# end error


def output(*args):
    global output_fd
    for a in args:
        output_fd.write(a)


def output_autogen():
    output('/* This file is autogenerated by scripts/decodetree.py.  */\n\n')


def str_indent(c):
    """Return a string with C spaces"""
    return ' ' * c


def str_fields(fields):
    """Return a string uniquely identifying FIELDS"""
    r = ''
    for n in sorted(fields.keys()):
        r += '_' + n
    return r[1:]


def whex(val):
    """Return a hex string for val padded for insnwidth"""
    global insnwidth
    return f'0x{val:0{insnwidth // 4}x}'


def whexC(val):
    """Return a hex string for val padded for insnwidth,
       and with the proper suffix for a C constant."""
    suffix = ''
    if val >= 0x100000000:
        suffix = 'ull'
    elif val >= 0x80000000:
        suffix = 'u'
    return whex(val) + suffix


def str_match_bits(bits, mask):
    """Return a string pretty-printing BITS/MASK"""
    global insnwidth

    i = 1 << (insnwidth - 1)
    space = 0x01010100
    r = ''
    while i != 0:
        if i & mask:
            if i & bits:
                r += '1'
            else:
                r += '0'
        else:
            r += '.'
        if i & space:
            r += ' '
        i >>= 1
    return r


def is_pow2(x):
    """Return true iff X is equal to a power of 2."""
    return (x & (x - 1)) == 0


def ctz(x):
    """Return the number of times 2 factors into X."""
    assert x != 0
    r = 0
    while ((x >> r) & 1) == 0:
        r += 1
    return r


def is_contiguous(bits):
    if bits == 0:
        return -1
    shift = ctz(bits)
    if is_pow2((bits >> shift) + 1):
        return shift
    else:
        return -1


def eq_fields_for_args(flds_a, arg):
    if len(flds_a) != len(arg.fields):
        return False
    # Only allow inference on default types
    for t in arg.types:
        if t != 'int':
            return False
    for k, a in flds_a.items():
        if k not in arg.fields:
            return False
    return True


def eq_fields_for_fmts(flds_a, flds_b):
    if len(flds_a) != len(flds_b):
        return False
    for k, a in flds_a.items():
        if k not in flds_b:
            return False
        b = flds_b[k]
        if a.__class__ != b.__class__ or a != b:
            return False
    return True


class Field:
    """Class representing a simple instruction field"""
    def __init__(self, sign, pos, len):
        self.sign = sign
        self.pos = pos
        self.len = len
        self.mask = ((1 << len) - 1) << pos

    def __str__(self):
        if self.sign:
            s = 's'
        else:
            s = ''
        return str(self.pos) + ':' + s + str(self.len)

    def str_extract(self, lvalue_formatter):
        global bitop_width
        s = 's' if self.sign else ''
        return f'{s}extract{bitop_width}(insn, {self.pos}, {self.len})'

    def referenced_fields(self):
        return []

    def __eq__(self, other):
        return self.sign == other.sign and self.mask == other.mask

    def __ne__(self, other):
        return not self.__eq__(other)
# end Field


class MultiField:
    """Class representing a compound instruction field"""
    def __init__(self, subs, mask):
        self.subs = subs
        self.sign = subs[0].sign
        self.mask = mask

    def __str__(self):
        return str(self.subs)

    def str_extract(self, lvalue_formatter):
        global bitop_width
        ret = '0'
        pos = 0
        for f in reversed(self.subs):
            ext = f.str_extract(lvalue_formatter)
            if pos == 0:
                ret = ext
            else:
                ret = f'deposit{bitop_width}({ret}, {pos}, {bitop_width - pos}, {ext})'
            pos += f.len
        return ret

    def referenced_fields(self):
        l = []
        for f in self.subs:
            l.extend(f.referenced_fields())
        return l

    def __ne__(self, other):
        if len(self.subs) != len(other.subs):
            return True
        for a, b in zip(self.subs, other.subs):
            if a.__class__ != b.__class__ or a != b:
                return True
        return False

    def __eq__(self, other):
        return not self.__ne__(other)
# end MultiField


class ConstField:
    """Class representing an argument field with constant value"""
    def __init__(self, value):
        self.value = value
        self.mask = 0
        self.sign = value < 0

    def __str__(self):
        return str(self.value)

    def str_extract(self, lvalue_formatter):
        return str(self.value)

    def referenced_fields(self):
        return []

    def __cmp__(self, other):
        return self.value - other.value
# end ConstField


class FunctionField:
    """Class representing a field passed through a function"""
    def __init__(self, func, base):
        self.mask = base.mask
        self.sign = base.sign
        self.base = base
        self.func = func

    def __str__(self):
        return self.func + '(' + str(self.base) + ')'

    def str_extract(self, lvalue_formatter):
        return (self.func + '(ctx, '
                + self.base.str_extract(lvalue_formatter) + ')')

    def referenced_fields(self):
        return self.base.referenced_fields()

    def __eq__(self, other):
        return self.func == other.func and self.base == other.base

    def __ne__(self, other):
        return not self.__eq__(other)
# end FunctionField


class ParameterField:
    """Class representing a pseudo-field read from a function"""
    def __init__(self, func):
        self.mask = 0
        self.sign = 0
        self.func = func

    def __str__(self):
        return self.func

    def str_extract(self, lvalue_formatter):
        return self.func + '(ctx)'

    def referenced_fields(self):
        return []

    def __eq__(self, other):
        return self.func == other.func

    def __ne__(self, other):
        return not self.__eq__(other)
# end ParameterField

class NamedField:
    """Class representing a field already named in the pattern"""
    def __init__(self, name, sign, len):
        self.mask = 0
        self.sign = sign
        self.len = len
        self.name = name

    def __str__(self):
        return self.name

    def str_extract(self, lvalue_formatter):
        global bitop_width
        s = 's' if self.sign else ''
        lvalue = lvalue_formatter(self.name)
        return f'{s}extract{bitop_width}({lvalue}, 0, {self.len})'

    def referenced_fields(self):
        return [self.name]

    def __eq__(self, other):
        return self.name == other.name

    def __ne__(self, other):
        return not self.__eq__(other)
# end NamedField

class Arguments:
    """Class representing the extracted fields of a format"""
    def __init__(self, nm, flds, types, extern):
        self.name = nm
        self.extern = extern
        self.fields = flds
        self.types = types

    def __str__(self):
        return self.name + ' ' + str(self.fields)

    def struct_name(self):
        return 'arg_' + self.name

    def output_def(self):
        if not self.extern:
            output('typedef struct {\n')
            for (n, t) in zip(self.fields, self.types):
                output(f'    {t} {n};\n')
            output('} ', self.struct_name(), ';\n\n')
# end Arguments

class General:
    """Common code between instruction formats and instruction patterns"""
    def __init__(self, name, lineno, base, fixb, fixm, udfm, fldm, flds, w):
        self.name = name
        self.file = input_file
        self.lineno = lineno
        self.base = base
        self.fixedbits = fixb
        self.fixedmask = fixm
        self.undefmask = udfm
        self.fieldmask = fldm
        self.fields = flds
        self.width = w
        self.dangling = None

    def __str__(self):
        return self.name + ' ' + str_match_bits(self.fixedbits, self.fixedmask)

    def str1(self, i):
        return str_indent(i) + self.__str__()

    def dangling_references(self):
        # Return a list of all named references which aren't satisfied
        # directly by this format/pattern. This will be either:
        #  * a format referring to a field which is specified by the
        #    pattern(s) using it
        #  * a pattern referring to a field which is specified by the
        #    format it uses
        #  * a user error (referring to a field that doesn't exist at all)
        if self.dangling is None:
            # Compute this once and cache the answer
            dangling = []
            for n, f in self.fields.items():
                for r in f.referenced_fields():
                    if r not in self.fields:
                        dangling.append(r)
            self.dangling = dangling
        return self.dangling

    def output_fields(self, indent, lvalue_formatter):
        # We use a topological sort to ensure that any use of NamedField
        # comes after the initialization of the field it is referencing.
        graph = {}
        for n, f in self.fields.items():
            refs = f.referenced_fields()
            graph[n] = refs

        try:
            ts = TopologicalSorter(graph)
            for n in ts.static_order():
                # We only want to emit assignments for the keys
                # in our fields list, not for anything that ends up
                # in the tsort graph only because it was referenced as
                # a NamedField.
                try:
                    f = self.fields[n]
                    output(indent, lvalue_formatter(n), ' = ',
                           f.str_extract(lvalue_formatter), ';\n')
                except KeyError:
                    pass
        except CycleError as e:
            # The second element of args is a list of nodes which form
            # a cycle (there might be others too, but only one is reported).
            # Pretty-print it to tell the user.
            cycle = ' => '.join(e.args[1])
            error(self.lineno, 'field definitions form a cycle: ' + cycle)
# end General


class Format(General):
    """Class representing an instruction format"""

    def extract_name(self):
        global decode_function
        return decode_function + '_extract_' + self.name

    def output_extract(self):
        output('static void ', self.extract_name(), '(DisasContext *ctx, ',
               self.base.struct_name(), ' *a, ', insntype, ' insn)\n{\n')
        self.output_fields(str_indent(4), lambda n: 'a->' + n)
        output('}\n\n')
# end Format


class Pattern(General):
    """Class representing an instruction pattern"""

    def output_decl(self):
        global translate_scope
        global translate_prefix
        output('typedef ', self.base.base.struct_name(),
               ' arg_', self.name, ';\n')
        output(translate_scope, 'bool ', translate_prefix, '_', self.name,
               '(DisasContext *ctx, arg_', self.name, ' *a);\n')

    def output_code(self, i, extracted, outerbits, outermask):
        global translate_prefix
        ind = str_indent(i)
        arg = self.base.base.name
        output(ind, '/* ', self.file, ':', str(self.lineno), ' */\n')
        # We might have named references in the format that refer to fields
        # in the pattern, or named references in the pattern that refer
        # to fields in the format. This affects whether we extract the fields
        # for the format before or after the ones for the pattern.
        # For simplicity we don't allow cross references in both directions.
        # This is also where we catch the syntax error of referring to
        # a nonexistent field.
        fmt_refs = self.base.dangling_references()
        for r in fmt_refs:
            if r not in self.fields:
                error(self.lineno, f'format refers to undefined field {r}')
        pat_refs = self.dangling_references()
        for r in pat_refs:
            if r not in self.base.fields:
                error(self.lineno, f'pattern refers to undefined field {r}')
        if pat_refs and fmt_refs:
            error(self.lineno, ('pattern that uses fields defined in format '
                                'cannot use format that uses fields defined '
                                'in pattern'))
        if fmt_refs:
            # pattern fields first
            self.output_fields(ind, lambda n: 'u.f_' + arg + '.' + n)
            assert not extracted, "dangling fmt refs but it was already extracted"
        if not extracted:
            output(ind, self.base.extract_name(),
                   '(ctx, &u.f_', arg, ', insn);\n')
        if not fmt_refs:
            # pattern fields last
            self.output_fields(ind, lambda n: 'u.f_' + arg + '.' + n)

        output(ind, 'if (', translate_prefix, '_', self.name,
               '(ctx, &u.f_', arg, ')) return true;\n')

    # Normal patterns do not have children.
    def build_tree(self):
        return
    def prop_masks(self):
        return
    def prop_format(self):
        return
    def prop_width(self):
        return

# end Pattern


class MultiPattern(General):
    """Class representing a set of instruction patterns"""

    def __init__(self, lineno):
        self.file = input_file
        self.lineno = lineno
        self.pats = []
        self.base = None
        self.fixedbits = 0
        self.fixedmask = 0
        self.undefmask = 0
        self.width = None

    def __str__(self):
        r = 'group'
        if self.fixedbits is not None:
            r += ' ' + str_match_bits(self.fixedbits, self.fixedmask)
        return r

    def output_decl(self):
        for p in self.pats:
            p.output_decl()

    def prop_masks(self):
        global insnmask

        fixedmask = insnmask
        undefmask = insnmask

        # Collect fixedmask/undefmask for all of the children.
        for p in self.pats:
            p.prop_masks()
            fixedmask &= p.fixedmask
            undefmask &= p.undefmask

        # Widen fixedmask until all fixedbits match
        repeat = True
        fixedbits = 0
        while repeat and fixedmask != 0:
            fixedbits = None
            for p in self.pats:
                thisbits = p.fixedbits & fixedmask
                if fixedbits is None:
                    fixedbits = thisbits
                elif fixedbits != thisbits:
                    fixedmask &= ~(fixedbits ^ thisbits)
                    break
            else:
                repeat = False

        self.fixedbits = fixedbits
        self.fixedmask = fixedmask
        self.undefmask = undefmask

    def build_tree(self):
        for p in self.pats:
            p.build_tree()

    def prop_format(self):
        for p in self.pats:
            p.prop_format()

    def prop_width(self):
        width = None
        for p in self.pats:
            p.prop_width()
            if width is None:
                width = p.width
            elif width != p.width:
                error_with_file(self.file, self.lineno,
                                'width mismatch in patterns within braces')
        self.width = width

# end MultiPattern


class IncMultiPattern(MultiPattern):
    """Class representing an overlapping set of instruction patterns"""

    def output_code(self, i, extracted, outerbits, outermask):
        global translate_prefix
        ind = str_indent(i)
        for p in self.pats:
            if outermask != p.fixedmask:
                innermask = p.fixedmask & ~outermask
                innerbits = p.fixedbits & ~outermask
                output(ind, f'if ((insn & {whexC(innermask)}) == {whexC(innerbits)}) {{\n')
                output(ind, f'    /* {str_match_bits(p.fixedbits, p.fixedmask)} */\n')
                p.output_code(i + 4, extracted, p.fixedbits, p.fixedmask)
                output(ind, '}\n')
            else:
                p.output_code(i, extracted, p.fixedbits, p.fixedmask)

    def build_tree(self):
        if not self.pats:
            error_with_file(self.file, self.lineno, 'empty pattern group')
        super().build_tree()

#end IncMultiPattern


class Tree:
    """Class representing a node in a decode tree"""

    def __init__(self, fm, tm):
        self.fixedmask = fm
        self.thismask = tm
        self.subs = []
        self.base = None

    def str1(self, i):
        ind = str_indent(i)
        r = ind + whex(self.fixedmask)
        if self.format:
            r += ' ' + self.format.name
        r += ' [\n'
        for (b, s) in self.subs:
            r += ind + f'  {whex(b)}:\n'
            r += s.str1(i + 4) + '\n'
        r += ind + ']'
        return r

    def __str__(self):
        return self.str1(0)

    def output_code(self, i, extracted, outerbits, outermask):
        ind = str_indent(i)

        # If we identified all nodes below have the same format,
        # extract the fields now. But don't do it if the format relies
        # on named fields from the insn pattern, as those won't have
        # been initialised at this point.
        if not extracted and self.base and not self.base.dangling_references():
            output(ind, self.base.extract_name(),
                   '(ctx, &u.f_', self.base.base.name, ', insn);\n')
            extracted = True

        # Attempt to aid the compiler in producing compact switch statements.
        # If the bits in the mask are contiguous, extract them.
        sh = is_contiguous(self.thismask)
        if sh > 0:
            # Propagate SH down into the local functions.
            def str_switch(b, sh=sh):
                return f'(insn >> {sh}) & {b >> sh:#x}'

            def str_case(b, sh=sh):
                return hex(b >> sh)
        else:
            def str_switch(b):
                return f'insn & {whexC(b)}'

            def str_case(b):
                return whexC(b)

        output(ind, 'switch (', str_switch(self.thismask), ') {\n')
        for b, s in sorted(self.subs):
            assert (self.thismask & ~s.fixedmask) == 0
            innermask = outermask | self.thismask
            innerbits = outerbits | b
            output(ind, 'case ', str_case(b), ':\n')
            output(ind, '    /* ',
                   str_match_bits(innerbits, innermask), ' */\n')
            s.output_code(i + 4, extracted, innerbits, innermask)
            output(ind, '    break;\n')
        output(ind, '}\n')
# end Tree


class ExcMultiPattern(MultiPattern):
    """Class representing a non-overlapping set of instruction patterns"""

    def output_code(self, i, extracted, outerbits, outermask):
        # Defer everything to our decomposed Tree node
        self.tree.output_code(i, extracted, outerbits, outermask)

    @staticmethod
    def __build_tree(pats, outerbits, outermask):
        # Find the intersection of all remaining fixedmask.
        innermask = ~outermask & insnmask
        for i in pats:
            innermask &= i.fixedmask

        if innermask == 0:
            # Edge condition: One pattern covers the entire insnmask
            if len(pats) == 1:
                t = Tree(outermask, innermask)
                t.subs.append((0, pats[0]))
                return t

            text = 'overlapping patterns:'
            for p in pats:
                text += '\n' + p.file + ':' + str(p.lineno) + ': ' + str(p)
            error_with_file(pats[0].file, pats[0].lineno, text)

        fullmask = outermask | innermask

        # Sort each element of pats into the bin selected by the mask.
        bins = {}
        for i in pats:
            fb = i.fixedbits & innermask
            if fb in bins:
                bins[fb].append(i)
            else:
                bins[fb] = [i]

        # We must recurse if any bin has more than one element or if
        # the single element in the bin has not been fully matched.
        t = Tree(fullmask, innermask)

        for b, l in bins.items():
            s = l[0]
            if len(l) > 1 or s.fixedmask & ~fullmask != 0:
                s = ExcMultiPattern.__build_tree(l, b | outerbits, fullmask)
            t.subs.append((b, s))

        return t

    def build_tree(self):
        super().build_tree()
        self.tree = self.__build_tree(self.pats, self.fixedbits,
                                      self.fixedmask)

    @staticmethod
    def __prop_format(tree):
        """Propagate Format objects into the decode tree"""

        # Depth first search.
        for (b, s) in tree.subs:
            if isinstance(s, Tree):
                ExcMultiPattern.__prop_format(s)

        # If all entries in SUBS have the same format, then
        # propagate that into the tree.
        f = None
        for (b, s) in tree.subs:
            if f is None:
                f = s.base
                if f is None:
                    return
            if f is not s.base:
                return
        tree.base = f

    def prop_format(self):
        super().prop_format()
        self.__prop_format(self.tree)

# end ExcMultiPattern


def parse_field(lineno, name, toks):
    """Parse one instruction field from TOKS at LINENO"""
    global fields
    global insnwidth
    global re_C_ident

    # A "simple" field will have only one entry;
    # a "multifield" will have several.
    subs = []
    width = 0
    func = None
    for t in toks:
        if re.match('^!function=', t):
            if func:
                error(lineno, 'duplicate function')
            func = t.split('=')
            func = func[1]
            continue

        if re.fullmatch(re_C_ident + ':s[0-9]+', t):
            # Signed named field
            subtoks = t.split(':')
            n = subtoks[0]
            le = int(subtoks[1])
            f = NamedField(n, True, le)
            subs.append(f)
            width += le
            continue
        if re.fullmatch(re_C_ident + ':[0-9]+', t):
            # Unsigned named field
            subtoks = t.split(':')
            n = subtoks[0]
            le = int(subtoks[1])
            f = NamedField(n, False, le)
            subs.append(f)
            width += le
            continue

        if re.fullmatch('[0-9]+:s[0-9]+', t):
            # Signed field extract
            subtoks = t.split(':s')
            sign = True
        elif re.fullmatch('[0-9]+:[0-9]+', t):
            # Unsigned field extract
            subtoks = t.split(':')
            sign = False
        else:
            error(lineno, f'invalid field token "{t}"')
        po = int(subtoks[0])
        le = int(subtoks[1])
        if po + le > insnwidth:
            error(lineno, f'field {t} too large')
        f = Field(sign, po, le)
        subs.append(f)
        width += le

    if width > insnwidth:
        error(lineno, 'field too large')
    if len(subs) == 0:
        if func:
            f = ParameterField(func)
        else:
            error(lineno, 'field with no value')
    else:
        if len(subs) == 1:
            f = subs[0]
        else:
            mask = 0
            for s in subs:
                if mask & s.mask:
                    error(lineno, 'field components overlap')
                mask |= s.mask
            f = MultiField(subs, mask)
        if func:
            f = FunctionField(func, f)

    if name in fields:
        error(lineno, 'duplicate field', name)
    fields[name] = f
# end parse_field


def parse_arguments(lineno, name, toks):
    """Parse one argument set from TOKS at LINENO"""
    global arguments
    global re_C_ident
    global anyextern

    flds = []
    types = []
    extern = False
    for n in toks:
        if re.fullmatch('!extern', n):
            extern = True
            anyextern = True
            continue
        if re.fullmatch(re_C_ident + ':' + re_C_ident, n):
            (n, t) = n.split(':')
        elif re.fullmatch(re_C_ident, n):
            t = 'int'
        else:
            error(lineno, f'invalid argument set token "{n}"')
        if n in flds:
            error(lineno, f'duplicate argument "{n}"')
        flds.append(n)
        types.append(t)

    if name in arguments:
        error(lineno, 'duplicate argument set', name)
    arguments[name] = Arguments(name, flds, types, extern)
# end parse_arguments


def lookup_field(lineno, name):
    global fields
    if name in fields:
        return fields[name]
    error(lineno, 'undefined field', name)


def add_field(lineno, flds, new_name, f):
    if new_name in flds:
        error(lineno, 'duplicate field', new_name)
    flds[new_name] = f
    return flds


def add_field_byname(lineno, flds, new_name, old_name):
    return add_field(lineno, flds, new_name, lookup_field(lineno, old_name))


def infer_argument_set(flds):
    global arguments
    global decode_function

    for arg in arguments.values():
        if eq_fields_for_args(flds, arg):
            return arg

    name = decode_function + str(len(arguments))
    arg = Arguments(name, flds.keys(), ['int'] * len(flds), False)
    arguments[name] = arg
    return arg


def infer_format(arg, fieldmask, flds, width):
    global arguments
    global formats
    global decode_function

    const_flds = {}
    var_flds = {}
    for n, c in flds.items():
        if c is ConstField:
            const_flds[n] = c
        else:
            var_flds[n] = c

    # Look for an existing format with the same argument set and fields
    for fmt in formats.values():
        if arg and fmt.base != arg:
            continue
        if fieldmask != fmt.fieldmask:
            continue
        if width != fmt.width:
            continue
        if not eq_fields_for_fmts(flds, fmt.fields):
            continue
        return (fmt, const_flds)

    name = decode_function + '_Fmt_' + str(len(formats))
    if not arg:
        arg = infer_argument_set(flds)

    fmt = Format(name, 0, arg, 0, 0, 0, fieldmask, var_flds, width)
    formats[name] = fmt

    return (fmt, const_flds)
# end infer_format


def parse_generic(lineno, parent_pat, name, toks):
    """Parse one instruction format from TOKS at LINENO"""
    global fields
    global arguments
    global formats
    global allpatterns
    global re_arg_ident
    global re_fld_ident
    global re_fmt_ident
    global re_C_ident
    global insnwidth
    global insnmask
    global variablewidth

    is_format = parent_pat is None

    fixedmask = 0
    fixedbits = 0
    undefmask = 0
    width = 0
    flds = {}
    arg = None
    fmt = None
    for t in toks:
        # '&Foo' gives a format an explicit argument set.
        if re.fullmatch(re_arg_ident, t):
            tt = t[1:]
            if arg:
                error(lineno, 'multiple argument sets')
            if tt in arguments:
                arg = arguments[tt]
            else:
                error(lineno, 'undefined argument set', t)
            continue

        # '@Foo' gives a pattern an explicit format.
        if re.fullmatch(re_fmt_ident, t):
            tt = t[1:]
            if fmt:
                error(lineno, 'multiple formats')
            if tt in formats:
                fmt = formats[tt]
            else:
                error(lineno, 'undefined format', t)
            continue

        # '%Foo' imports a field.
        if re.fullmatch(re_fld_ident, t):
            tt = t[1:]
            flds = add_field_byname(lineno, flds, tt, tt)
            continue

        # 'Foo=%Bar' imports a field with a different name.
        if re.fullmatch(re_C_ident + '=' + re_fld_ident, t):
            (fname, iname) = t.split('=%')
            flds = add_field_byname(lineno, flds, fname, iname)
            continue

        # 'Foo=number' sets an argument field to a constant value
        if re.fullmatch(re_C_ident + '=[+-]?[0-9]+', t):
            (fname, value) = t.split('=')
            value = int(value)
            flds = add_field(lineno, flds, fname, ConstField(value))
            continue

        # Pattern of 0s, 1s, dots and dashes indicate required zeros,
        # required ones, or dont-cares.
        if re.fullmatch('[01.-]+', t):
            shift = len(t)
            fms = t.replace('0', '1')
            fms = fms.replace('.', '0')
            fms = fms.replace('-', '0')
            fbs = t.replace('.', '0')
            fbs = fbs.replace('-', '0')
            ubm = t.replace('1', '0')
            ubm = ubm.replace('.', '0')
            ubm = ubm.replace('-', '1')
            fms = int(fms, 2)
            fbs = int(fbs, 2)
            ubm = int(ubm, 2)
            fixedbits = (fixedbits << shift) | fbs
            fixedmask = (fixedmask << shift) | fms
            undefmask = (undefmask << shift) | ubm
        # Otherwise, fieldname:fieldwidth
        elif re.fullmatch(re_C_ident + ':s?[0-9]+', t):
            (fname, flen) = t.split(':')
            sign = False
            if flen[0] == 's':
                sign = True
                flen = flen[1:]
            shift = int(flen, 10)
            if shift + width > insnwidth:
                error(lineno, f'field {fname} exceeds insnwidth')
            f = Field(sign, insnwidth - width - shift, shift)
            flds = add_field(lineno, flds, fname, f)
            fixedbits <<= shift
            fixedmask <<= shift
            undefmask <<= shift
        else:
            error(lineno, f'invalid token "{t}"')
        width += shift

    if variablewidth and width < insnwidth and width % 8 == 0:
        shift = insnwidth - width
        fixedbits <<= shift
        fixedmask <<= shift
        undefmask <<= shift
        undefmask |= (1 << shift) - 1

    # We should have filled in all of the bits of the instruction.
    elif not (is_format and width == 0) and width != insnwidth:
        error(lineno, f'definition has {width} bits')

    # Do not check for fields overlapping fields; one valid usage
    # is to be able to duplicate fields via import.
    fieldmask = 0
    for f in flds.values():
        fieldmask |= f.mask

    # Fix up what we've parsed to match either a format or a pattern.
    if is_format:
        # Formats cannot reference formats.
        if fmt:
            error(lineno, 'format referencing format')
        # If an argument set is given, then there should be no fields
        # without a place to store it.
        if arg:
            for f in flds.keys():
                if f not in arg.fields:
                    error(lineno, f'field {f} not in argument set {arg.name}')
        else:
            arg = infer_argument_set(flds)
        if name in formats:
            error(lineno, 'duplicate format name', name)
        fmt = Format(name, lineno, arg, fixedbits, fixedmask,
                     undefmask, fieldmask, flds, width)
        formats[name] = fmt
    else:
        # Patterns can reference a format ...
        if fmt:
            # ... but not an argument simultaneously
            if arg:
                error(lineno, 'pattern specifies both format and argument set')
            if fixedmask & fmt.fixedmask:
                error(lineno, 'pattern fixed bits overlap format fixed bits')
            if width != fmt.width:
                error(lineno, 'pattern uses format of different width')
            fieldmask |= fmt.fieldmask
            fixedbits |= fmt.fixedbits
            fixedmask |= fmt.fixedmask
            undefmask |= fmt.undefmask
        else:
            (fmt, flds) = infer_format(arg, fieldmask, flds, width)
        arg = fmt.base
        for f in flds.keys():
            if f not in arg.fields:
                error(lineno, f'field {f} not in argument set {arg.name}')
            if f in fmt.fields.keys():
                error(lineno, f'field {f} set by format and pattern')
        for f in arg.fields:
            if f not in flds.keys() and f not in fmt.fields.keys():
                error(lineno, f'field {f} not initialized')
        pat = Pattern(name, lineno, fmt, fixedbits, fixedmask,
                      undefmask, fieldmask, flds, width)
        parent_pat.pats.append(pat)
        allpatterns.append(pat)

    # Validate the masks that we have assembled.
    if fieldmask & fixedmask:
        error(lineno, 'fieldmask overlaps fixedmask ',
              f'({whex(fieldmask)} & {whex(fixedmask)})')
    if fieldmask & undefmask:
        error(lineno, 'fieldmask overlaps undefmask ',
              f'({whex(fieldmask)} & {whex(undefmask)})')
    if fixedmask & undefmask:
        error(lineno, 'fixedmask overlaps undefmask ',
              f'({whex(fixedmask)} & {whex(undefmask)})')
    if not is_format:
        allbits = fieldmask | fixedmask | undefmask
        if allbits != insnmask:
            error(lineno, 'bits left unspecified ',
                  f'({whex(allbits ^ insnmask)})')
# end parse_general


def parse_file(f, parent_pat):
    """Parse all of the patterns within a file"""
    global re_arg_ident
    global re_fld_ident
    global re_fmt_ident
    global re_pat_ident

    # Read all of the lines of the file.  Concatenate lines
    # ending in backslash; discard empty lines and comments.
    toks = []
    lineno = 0
    nesting = 0
    nesting_pats = []

    for line in f:
        lineno += 1

        # Expand and strip spaces, to find indent.
        line = line.rstrip()
        line = line.expandtabs()
        len1 = len(line)
        line = line.lstrip()
        len2 = len(line)

        # Discard comments
        end = line.find('#')
        if end >= 0:
            line = line[:end]

        t = line.split()
        if len(toks) != 0:
            # Next line after continuation
            toks.extend(t)
        else:
            # Allow completely blank lines.
            if len1 == 0:
                continue
            indent = len1 - len2
            # Empty line due to comment.
            if len(t) == 0:
                # Indentation must be correct, even for comment lines.
                if indent != nesting:
                    error(lineno, 'indentation ', indent, ' != ', nesting)
                continue
            start_lineno = lineno
            toks = t

        # Continuation?
        if toks[-1] == '\\':
            toks.pop()
            continue

        name = toks[0]
        del toks[0]

        # End nesting?
        if name == '}' or name == ']':
            if len(toks) != 0:
                error(start_lineno, 'extra tokens after close brace')

            # Make sure { } and [ ] nest properly.
            if (name == '}') != isinstance(parent_pat, IncMultiPattern):
                error(lineno, 'mismatched close brace')

            try:
                parent_pat = nesting_pats.pop()
            except:
                error(lineno, 'extra close brace')

            nesting -= 2
            if indent != nesting:
                error(lineno, 'indentation ', indent, ' != ', nesting)

            toks = []
            continue

        # Everything else should have current indentation.
        if indent != nesting:
            error(start_lineno, 'indentation ', indent, ' != ', nesting)

        # Start nesting?
        if name == '{' or name == '[':
            if len(toks) != 0:
                error(start_lineno, 'extra tokens after open brace')

            if name == '{':
                nested_pat = IncMultiPattern(start_lineno)
            else:
                nested_pat = ExcMultiPattern(start_lineno)
            parent_pat.pats.append(nested_pat)
            nesting_pats.append(parent_pat)
            parent_pat = nested_pat

            nesting += 2
            toks = []
            continue

        # Determine the type of object needing to be parsed.
        if re.fullmatch(re_fld_ident, name):
            parse_field(start_lineno, name[1:], toks)
        elif re.fullmatch(re_arg_ident, name):
            parse_arguments(start_lineno, name[1:], toks)
        elif re.fullmatch(re_fmt_ident, name):
            parse_generic(start_lineno, None, name[1:], toks)
        elif re.fullmatch(re_pat_ident, name):
            parse_generic(start_lineno, parent_pat, name, toks)
        else:
            error(lineno, f'invalid token "{name}"')
        toks = []

    if nesting != 0:
        error(lineno, 'missing close brace')
# end parse_file


class SizeTree:
    """Class representing a node in a size decode tree"""

    def __init__(self, m, w):
        self.mask = m
        self.subs = []
        self.base = None
        self.width = w

    def str1(self, i):
        ind = str_indent(i)
        r = ind + whex(self.mask) + ' [\n'
        for (b, s) in self.subs:
            r += ind + f'  {whex(b)}:\n'
            r += s.str1(i + 4) + '\n'
        r += ind + ']'
        return r

    def __str__(self):
        return self.str1(0)

    def output_code(self, i, extracted, outerbits, outermask):
        ind = str_indent(i)

        # If we need to load more bytes to test, do so now.
        if extracted < self.width:
            output(ind, f'insn = {decode_function}_load_bytes',
                   f'(ctx, insn, {extracted // 8}, {self.width // 8});\n')
            extracted = self.width

        # Attempt to aid the compiler in producing compact switch statements.
        # If the bits in the mask are contiguous, extract them.
        sh = is_contiguous(self.mask)
        if sh > 0:
            # Propagate SH down into the local functions.
            def str_switch(b, sh=sh):
                return f'(insn >> {sh}) & {b >> sh:#x}'

            def str_case(b, sh=sh):
                return hex(b >> sh)
        else:
            def str_switch(b):
                return f'insn & {whexC(b)}'

            def str_case(b):
                return whexC(b)

        output(ind, 'switch (', str_switch(self.mask), ') {\n')
        for b, s in sorted(self.subs):
            innermask = outermask | self.mask
            innerbits = outerbits | b
            output(ind, 'case ', str_case(b), ':\n')
            output(ind, '    /* ',
                   str_match_bits(innerbits, innermask), ' */\n')
            s.output_code(i + 4, extracted, innerbits, innermask)
        output(ind, '}\n')
        output(ind, 'return insn;\n')
# end SizeTree

class SizeLeaf:
    """Class representing a leaf node in a size decode tree"""

    def __init__(self, m, w):
        self.mask = m
        self.width = w

    def str1(self, i):
        return str_indent(i) + whex(self.mask)

    def __str__(self):
        return self.str1(0)

    def output_code(self, i, extracted, outerbits, outermask):
        global decode_function
        ind = str_indent(i)

        # If we need to load more bytes, do so now.
        if extracted < self.width:
            output(ind, f'insn = {decode_function}_load_bytes',
                   f'(ctx, insn, {extracted // 8}, {self.width // 8});\n')
            extracted = self.width
        output(ind, 'return insn;\n')
# end SizeLeaf


def build_size_tree(pats, width, outerbits, outermask):
    global insnwidth

    # Collect the mask of bits that are fixed in this width
    innermask = 0xff << (insnwidth - width)
    innermask &= ~outermask
    minwidth = None
    onewidth = True
    for i in pats:
        innermask &= i.fixedmask
        if minwidth is None:
            minwidth = i.width
        elif minwidth != i.width:
            onewidth = False;
            if minwidth < i.width:
                minwidth = i.width

    if onewidth:
        return SizeLeaf(innermask, minwidth)

    if innermask == 0:
        if width < minwidth:
            return build_size_tree(pats, width + 8, outerbits, outermask)

        pnames = []
        for p in pats:
            pnames.append(p.name + ':' + p.file + ':' + str(p.lineno))
        error_with_file(pats[0].file, pats[0].lineno,
                        f'overlapping patterns size {width}:', pnames)

    bins = {}
    for i in pats:
        fb = i.fixedbits & innermask
        if fb in bins:
            bins[fb].append(i)
        else:
            bins[fb] = [i]

    fullmask = outermask | innermask
    lens = sorted(bins.keys())
    if len(lens) == 1:
        b = lens[0]
        return build_size_tree(bins[b], width + 8, b | outerbits, fullmask)

    r = SizeTree(innermask, width)
    for b, l in bins.items():
        s = build_size_tree(l, width, b | outerbits, fullmask)
        r.subs.append((b, s))
    return r
# end build_size_tree


def prop_size(tree):
    """Propagate minimum widths up the decode size tree"""

    if isinstance(tree, SizeTree):
        min = None
        for (b, s) in tree.subs:
            width = prop_size(s)
            if min is None or min > width:
                min = width
        assert min >= tree.width
        tree.width = min
    else:
        min = tree.width
    return min
# end prop_size


def main():
    global arguments
    global formats
    global allpatterns
    global translate_scope
    global translate_prefix
    global output_fd
    global output_file
    global output_null
    global input_file
    global insnwidth
    global insntype
    global insnmask
    global decode_function
    global bitop_width
    global variablewidth
    global anyextern
    global testforerror

    decode_scope = 'static '

    long_opts = ['decode=', 'translate=', 'output=', 'insnwidth=',
                 'static-decode=', 'varinsnwidth=', 'test-for-error',
                 'output-null']
    try:
        (opts, args) = getopt.gnu_getopt(sys.argv[1:], 'o:vw:', long_opts)
    except getopt.GetoptError as err:
        error(0, err)
    for o, a in opts:
        if o in ('-o', '--output'):
            output_file = a
        elif o == '--decode':
            decode_function = a
            decode_scope = ''
        elif o == '--static-decode':
            decode_function = a
        elif o == '--translate':
            translate_prefix = a
            translate_scope = ''
        elif o in ('-w', '--insnwidth', '--varinsnwidth'):
            if o == '--varinsnwidth':
                variablewidth = True
            insnwidth = int(a)
            if insnwidth == 16:
                insntype = 'uint16_t'
                insnmask = 0xffff
            elif insnwidth == 64:
                insntype = 'uint64_t'
                insnmask = 0xffffffffffffffff
                bitop_width = 64
            elif insnwidth != 32:
                error(0, 'cannot handle insns of width', insnwidth)
        elif o == '--test-for-error':
            testforerror = True
        elif o == '--output-null':
            output_null = True
        else:
            assert False, 'unhandled option'

    if len(args) < 1:
        error(0, 'missing input file')

    toppat = ExcMultiPattern(0)

    for filename in args:
        input_file = filename
        f = open(filename, 'rt', encoding='utf-8')
        parse_file(f, toppat)
        f.close()

    # We do not want to compute masks for toppat, because those masks
    # are used as a starting point for build_tree.  For toppat, we must
    # insist that decode begins from naught.
    for i in toppat.pats:
        i.prop_masks()

    toppat.build_tree()
    toppat.prop_format()

    if variablewidth:
        for i in toppat.pats:
            i.prop_width()
        stree = build_size_tree(toppat.pats, 8, 0, 0)
        prop_size(stree)

    if output_null:
        output_fd = open(os.devnull, 'wt', encoding='utf-8', errors="ignore")
    elif output_file:
        output_fd = open(output_file, 'wt', encoding='utf-8')
    else:
        output_fd = io.TextIOWrapper(sys.stdout.buffer,
                                     encoding=sys.stdout.encoding,
                                     errors="ignore")

    output_autogen()
    for n in sorted(arguments.keys()):
        f = arguments[n]
        f.output_def()

    # A single translate function can be invoked for different patterns.
    # Make sure that the argument sets are the same, and declare the
    # function only once.
    #
    # If we're sharing formats, we're likely also sharing trans_* functions,
    # but we can't tell which ones.  Prevent issues from the compiler by
    # suppressing redundant declaration warnings.
    if anyextern:
        output("#pragma GCC diagnostic push\n",
               "#pragma GCC diagnostic ignored \"-Wredundant-decls\"\n",
               "#ifdef __clang__\n"
               "#  pragma GCC diagnostic ignored \"-Wtypedef-redefinition\"\n",
               "#endif\n\n")

    out_pats = {}
    for i in allpatterns:
        if i.name in out_pats:
            p = out_pats[i.name]
            if i.base.base != p.base.base:
                error(0, i.name, ' has conflicting argument sets')
        else:
            i.output_decl()
            out_pats[i.name] = i
    output('\n')

    if anyextern:
        output("#pragma GCC diagnostic pop\n\n")

    for n in sorted(formats.keys()):
        f = formats[n]
        f.output_extract()

    output(decode_scope, 'bool ', decode_function,
           '(DisasContext *ctx, ', insntype, ' insn)\n{\n')

    i4 = str_indent(4)

    if len(allpatterns) != 0:
        output(i4, 'union {\n')
        for n in sorted(arguments.keys()):
            f = arguments[n]
            output(i4, i4, f.struct_name(), ' f_', f.name, ';\n')
        output(i4, '} u;\n\n')
        toppat.output_code(4, False, 0, 0)

    output(i4, 'return false;\n')
    output('}\n')

    if variablewidth:
        output('\n', decode_scope, insntype, ' ', decode_function,
               '_load(DisasContext *ctx)\n{\n',
               '    ', insntype, ' insn = 0;\n\n')
        stree.output_code(4, 0, 0, 0)
        output('}\n')

    if output_file:
        output_fd.close()
    exit(1 if testforerror else 0)
# end main


if __name__ == '__main__':
    main()
