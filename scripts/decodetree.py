#!/usr/bin/env python
# Copyright (c) 2018 Linaro Limited
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
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
#
# The tree is built from instruction "patterns".  A pattern may represent
# a single architectural instruction or a group of same, depending on what
# is convenient for further processing.
#
# Each pattern has "fixedbits" & "fixedmask", the combination of which
# describes the condition under which the pattern is matched:
#
#   (insn & fixedmask) == fixedbits
#
# Each pattern may have "fields", which are extracted from the insn and
# passed along to the translator.  Examples of such are registers,
# immediates, and sub-opcodes.
#
# In support of patterns, one may declare fields, argument sets, and
# formats, each of which may be re-used to simplify further definitions.
#
# *** Field syntax:
#
# field_def     := '%' identifier ( unnamed_field )+ ( !function=identifier )?
# unnamed_field := number ':' ( 's' ) number
#
# For unnamed_field, the first number is the least-significant bit position of
# the field and the second number is the length of the field.  If the 's' is
# present, the field is considered signed.  If multiple unnamed_fields are
# present, they are concatenated.  In this way one can define disjoint fields.
#
# If !function is specified, the concatenated result is passed through the
# named function, taking and returning an integral value.
#
# FIXME: the fields of the structure into which this result will be stored
# is restricted to "int".  Which means that we cannot expand 64-bit items.
#
# Field examples:
#
#   %disp   0:s16          -- sextract(i, 0, 16)
#   %imm9   16:6 10:3      -- extract(i, 16, 6) << 3 | extract(i, 10, 3)
#   %disp12 0:s1 1:1 2:10  -- sextract(i, 0, 1) << 11
#                             | extract(i, 1, 1) << 10
#                             | extract(i, 2, 10)
#   %shimm8 5:s8 13:1 !function=expand_shimm8
#                          -- expand_shimm8(sextract(i, 5, 8) << 1
#                                           | extract(i, 13, 1))
#
# *** Argument set syntax:
#
# args_def    := '&' identifier ( args_elt )+ ( !extern )?
# args_elt    := identifier
#
# Each args_elt defines an argument within the argument set.
# Each argument set will be rendered as a C structure "arg_$name"
# with each of the fields being one of the member arguments.
#
# If !extern is specified, the backing structure is assumed to
# have been already declared, typically via a second decoder.
#
# Argument set examples:
#
#   &reg3       ra rb rc
#   &loadstore  reg base offset
#
# *** Format syntax:
#
# fmt_def      := '@' identifier ( fmt_elt )+
# fmt_elt      := fixedbit_elt | field_elt | field_ref | args_ref
# fixedbit_elt := [01.-]+
# field_elt    := identifier ':' 's'? number
# field_ref    := '%' identifier | identifier '=' '%' identifier
# args_ref     := '&' identifier
#
# Defining a format is a handy way to avoid replicating groups of fields
# across many instruction patterns.
#
# A fixedbit_elt describes a contiguous sequence of bits that must
# be 1, 0, [.-] for don't care.  The difference between '.' and '-'
# is that '.' means that the bit will be covered with a field or a
# final [01] from the pattern, and '-' means that the bit is really
# ignored by the cpu and will not be specified.
#
# A field_elt describes a simple field only given a width; the position of
# the field is implied by its position with respect to other fixedbit_elt
# and field_elt.
#
# If any fixedbit_elt or field_elt appear then all bits must be defined.
# Padding with a fixedbit_elt of all '.' is an easy way to accomplish that.
#
# A field_ref incorporates a field by reference.  This is the only way to
# add a complex field to a format.  A field may be renamed in the process
# via assignment to another identifier.  This is intended to allow the
# same argument set be used with disjoint named fields.
#
# A single args_ref may specify an argument set to use for the format.
# The set of fields in the format must be a subset of the arguments in
# the argument set.  If an argument set is not specified, one will be
# inferred from the set of fields.
#
# It is recommended, but not required, that all field_ref and args_ref
# appear at the end of the line, not interleaving with fixedbit_elf or
# field_elt.
#
# Format examples:
#
#   @opr    ...... ra:5 rb:5 ... 0 ....... rc:5
#   @opi    ...... ra:5 lit:8    1 ....... rc:5
#
# *** Pattern syntax:
#
# pat_def      := identifier ( pat_elt )+
# pat_elt      := fixedbit_elt | field_elt | field_ref
#               | args_ref | fmt_ref | const_elt
# fmt_ref      := '@' identifier
# const_elt    := identifier '=' number
#
# The fixedbit_elt and field_elt specifiers are unchanged from formats.
# A pattern that does not specify a named format will have one inferred
# from a referenced argument set (if present) and the set of fields.
#
# A const_elt allows a argument to be set to a constant value.  This may
# come in handy when fields overlap between patterns and one has to
# include the values in the fixedbit_elt instead.
#
# The decoder will call a translator function for each pattern matched.
#
# Pattern examples:
#
#   addl_r   010000 ..... ..... .... 0000000 ..... @opr
#   addl_i   010000 ..... ..... .... 0000000 ..... @opi
#
# which will, in part, invoke
#
#   trans_addl_r(ctx, &arg_opr, insn)
# and
#   trans_addl_i(ctx, &arg_opi, insn)
#

import os
import re
import sys
import getopt

insnwidth = 32
insnmask = 0xffffffff
fields = {}
arguments = {}
formats = {}
patterns = []

translate_prefix = 'trans'
translate_scope = 'static '
input_file = ''
output_file = None
output_fd = None
insntype = 'uint32_t'
decode_function = 'decode'

re_ident = '[a-zA-Z][a-zA-Z0-9_]*'


def error_with_file(file, lineno, *args):
    """Print an error message from file:line and args and exit."""
    global output_file
    global output_fd

    if lineno:
        r = '{0}:{1}: error:'.format(file, lineno)
    elif input_file:
        r = '{0}: error:'.format(file)
    else:
        r = 'error:'
    for a in args:
        r += ' ' + str(a)
    r += '\n'
    sys.stderr.write(r)
    if output_file and output_fd:
        output_fd.close()
        os.remove(output_file)
    exit(1)

def error(lineno, *args):
    error_with_file(input_file, lineno, args)

def output(*args):
    global output_fd
    for a in args:
        output_fd.write(a)


if sys.version_info >= (3, 0):
    re_fullmatch = re.fullmatch
else:
    def re_fullmatch(pat, str):
        return re.match('^' + pat + '$', str)


def output_autogen():
    output('/* This file is autogenerated by scripts/decodetree.py.  */\n\n')


def str_indent(c):
    """Return a string with C spaces"""
    return ' ' * c


def str_fields(fields):
    """Return a string uniquely identifing FIELDS"""
    r = ''
    for n in sorted(fields.keys()):
        r += '_' + n
    return r[1:]


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
    r = 0
    while ((x >> r) & 1) == 0:
        r += 1
    return r


def is_contiguous(bits):
    shift = ctz(bits)
    if is_pow2((bits >> shift) + 1):
        return shift
    else:
        return -1


def eq_fields_for_args(flds_a, flds_b):
    if len(flds_a) != len(flds_b):
        return False
    for k, a in flds_a.items():
        if k not in flds_b:
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

    def str_extract(self):
        if self.sign:
            extr = 'sextract32'
        else:
            extr = 'extract32'
        return '{0}(insn, {1}, {2})'.format(extr, self.pos, self.len)

    def __eq__(self, other):
        return self.sign == other.sign and self.sign == other.sign

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

    def str_extract(self):
        ret = '0'
        pos = 0
        for f in reversed(self.subs):
            if pos == 0:
                ret = f.str_extract()
            else:
                ret = 'deposit32({0}, {1}, {2}, {3})' \
                      .format(ret, pos, 32 - pos, f.str_extract())
            pos += f.len
        return ret

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

    def str_extract(self):
        return str(self.value)

    def __cmp__(self, other):
        return self.value - other.value
# end ConstField


class FunctionField:
    """Class representing a field passed through an expander"""
    def __init__(self, func, base):
        self.mask = base.mask
        self.sign = base.sign
        self.base = base
        self.func = func

    def __str__(self):
        return self.func + '(' + str(self.base) + ')'

    def str_extract(self):
        return self.func + '(' + self.base.str_extract() + ')'

    def __eq__(self, other):
        return self.func == other.func and self.base == other.base

    def __ne__(self, other):
        return not self.__eq__(other)
# end FunctionField


class Arguments:
    """Class representing the extracted fields of a format"""
    def __init__(self, nm, flds, extern):
        self.name = nm
        self.extern = extern
        self.fields = sorted(flds)

    def __str__(self):
        return self.name + ' ' + str(self.fields)

    def struct_name(self):
        return 'arg_' + self.name

    def output_def(self):
        if not self.extern:
            output('typedef struct {\n')
            for n in self.fields:
                output('    int ', n, ';\n')
            output('} ', self.struct_name(), ';\n\n')
# end Arguments


class General:
    """Common code between instruction formats and instruction patterns"""
    def __init__(self, name, lineno, base, fixb, fixm, udfm, fldm, flds):
        self.name = name
        self.file = input_file
        self.lineno = lineno
        self.base = base
        self.fixedbits = fixb
        self.fixedmask = fixm
        self.undefmask = udfm
        self.fieldmask = fldm
        self.fields = flds

    def __str__(self):
        r = self.name
        if self.base:
            r = r + ' ' + self.base.name
        else:
            r = r + ' ' + str(self.fields)
        r = r + ' ' + str_match_bits(self.fixedbits, self.fixedmask)
        return r

    def str1(self, i):
        return str_indent(i) + self.__str__()
# end General


class Format(General):
    """Class representing an instruction format"""

    def extract_name(self):
        return 'extract_' + self.name

    def output_extract(self):
        output('static void ', self.extract_name(), '(',
               self.base.struct_name(), ' *a, ', insntype, ' insn)\n{\n')
        for n, f in self.fields.items():
            output('    a->', n, ' = ', f.str_extract(), ';\n')
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
        if not extracted:
            output(ind, self.base.extract_name(), '(&u.f_', arg, ', insn);\n')
        for n, f in self.fields.items():
            output(ind, 'u.f_', arg, '.', n, ' = ', f.str_extract(), ';\n')
        output(ind, 'return ', translate_prefix, '_', self.name,
               '(ctx, &u.f_', arg, ');\n')
# end Pattern


def parse_field(lineno, name, toks):
    """Parse one instruction field from TOKS at LINENO"""
    global fields
    global re_ident
    global insnwidth

    # A "simple" field will have only one entry;
    # a "multifield" will have several.
    subs = []
    width = 0
    func = None
    for t in toks:
        if re_fullmatch('!function=' + re_ident, t):
            if func:
                error(lineno, 'duplicate function')
            func = t.split('=')
            func = func[1]
            continue

        if re_fullmatch('[0-9]+:s[0-9]+', t):
            # Signed field extract
            subtoks = t.split(':s')
            sign = True
        elif re_fullmatch('[0-9]+:[0-9]+', t):
            # Unsigned field extract
            subtoks = t.split(':')
            sign = False
        else:
            error(lineno, 'invalid field token "{0}"'.format(t))
        po = int(subtoks[0])
        le = int(subtoks[1])
        if po + le > insnwidth:
            error(lineno, 'field {0} too large'.format(t))
        f = Field(sign, po, le)
        subs.append(f)
        width += le

    if width > insnwidth:
        error(lineno, 'field too large')
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
    global re_ident

    flds = []
    extern = False
    for t in toks:
        if re_fullmatch('!extern', t):
            extern = True
            continue
        if not re_fullmatch(re_ident, t):
            error(lineno, 'invalid argument set token "{0}"'.format(t))
        if t in flds:
            error(lineno, 'duplicate argument "{0}"'.format(t))
        flds.append(t)

    if name in arguments:
        error(lineno, 'duplicate argument set', name)
    arguments[name] = Arguments(name, flds, extern)
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
        if eq_fields_for_args(flds, arg.fields):
            return arg

    name = decode_function + str(len(arguments))
    arg = Arguments(name, flds.keys(), False)
    arguments[name] = arg
    return arg


def infer_format(arg, fieldmask, flds):
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
        if not eq_fields_for_fmts(flds, fmt.fields):
            continue
        return (fmt, const_flds)

    name = decode_function + '_Fmt_' + str(len(formats))
    if not arg:
        arg = infer_argument_set(flds)

    fmt = Format(name, 0, arg, 0, 0, 0, fieldmask, var_flds)
    formats[name] = fmt

    return (fmt, const_flds)
# end infer_format


def parse_generic(lineno, is_format, name, toks):
    """Parse one instruction format from TOKS at LINENO"""
    global fields
    global arguments
    global formats
    global patterns
    global re_ident
    global insnwidth
    global insnmask

    fixedmask = 0
    fixedbits = 0
    undefmask = 0
    width = 0
    flds = {}
    arg = None
    fmt = None
    for t in toks:
        # '&Foo' gives a format an explcit argument set.
        if t[0] == '&':
            tt = t[1:]
            if arg:
                error(lineno, 'multiple argument sets')
            if tt in arguments:
                arg = arguments[tt]
            else:
                error(lineno, 'undefined argument set', t)
            continue

        # '@Foo' gives a pattern an explicit format.
        if t[0] == '@':
            tt = t[1:]
            if fmt:
                error(lineno, 'multiple formats')
            if tt in formats:
                fmt = formats[tt]
            else:
                error(lineno, 'undefined format', t)
            continue

        # '%Foo' imports a field.
        if t[0] == '%':
            tt = t[1:]
            flds = add_field_byname(lineno, flds, tt, tt)
            continue

        # 'Foo=%Bar' imports a field with a different name.
        if re_fullmatch(re_ident + '=%' + re_ident, t):
            (fname, iname) = t.split('=%')
            flds = add_field_byname(lineno, flds, fname, iname)
            continue

        # 'Foo=number' sets an argument field to a constant value
        if re_fullmatch(re_ident + '=[0-9]+', t):
            (fname, value) = t.split('=')
            value = int(value)
            flds = add_field(lineno, flds, fname, ConstField(value))
            continue

        # Pattern of 0s, 1s, dots and dashes indicate required zeros,
        # required ones, or dont-cares.
        if re_fullmatch('[01.-]+', t):
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
        elif re_fullmatch(re_ident + ':s?[0-9]+', t):
            (fname, flen) = t.split(':')
            sign = False
            if flen[0] == 's':
                sign = True
                flen = flen[1:]
            shift = int(flen, 10)
            f = Field(sign, insnwidth - width - shift, shift)
            flds = add_field(lineno, flds, fname, f)
            fixedbits <<= shift
            fixedmask <<= shift
            undefmask <<= shift
        else:
            error(lineno, 'invalid token "{0}"'.format(t))
        width += shift

    # We should have filled in all of the bits of the instruction.
    if not (is_format and width == 0) and width != insnwidth:
        error(lineno, 'definition has {0} bits'.format(width))

    # Do not check for fields overlaping fields; one valid usage
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
                    error(lineno, 'field {0} not in argument set {1}'
                                  .format(f, arg.name))
        else:
            arg = infer_argument_set(flds)
        if name in formats:
            error(lineno, 'duplicate format name', name)
        fmt = Format(name, lineno, arg, fixedbits, fixedmask,
                     undefmask, fieldmask, flds)
        formats[name] = fmt
    else:
        # Patterns can reference a format ...
        if fmt:
            # ... but not an argument simultaneously
            if arg:
                error(lineno, 'pattern specifies both format and argument set')
            if fixedmask & fmt.fixedmask:
                error(lineno, 'pattern fixed bits overlap format fixed bits')
            fieldmask |= fmt.fieldmask
            fixedbits |= fmt.fixedbits
            fixedmask |= fmt.fixedmask
            undefmask |= fmt.undefmask
        else:
            (fmt, flds) = infer_format(arg, fieldmask, flds)
        arg = fmt.base
        for f in flds.keys():
            if f not in arg.fields:
                error(lineno, 'field {0} not in argument set {1}'
                              .format(f, arg.name))
            if f in fmt.fields.keys():
                error(lineno, 'field {0} set by format and pattern'.format(f))
        for f in arg.fields:
            if f not in flds.keys() and f not in fmt.fields.keys():
                error(lineno, 'field {0} not initialized'.format(f))
        pat = Pattern(name, lineno, fmt, fixedbits, fixedmask,
                      undefmask, fieldmask, flds)
        patterns.append(pat)

    # Validate the masks that we have assembled.
    if fieldmask & fixedmask:
        error(lineno, 'fieldmask overlaps fixedmask (0x{0:08x} & 0x{1:08x})'
                      .format(fieldmask, fixedmask))
    if fieldmask & undefmask:
        error(lineno, 'fieldmask overlaps undefmask (0x{0:08x} & 0x{1:08x})'
                      .format(fieldmask, undefmask))
    if fixedmask & undefmask:
        error(lineno, 'fixedmask overlaps undefmask (0x{0:08x} & 0x{1:08x})'
                      .format(fixedmask, undefmask))
    if not is_format:
        allbits = fieldmask | fixedmask | undefmask
        if allbits != insnmask:
            error(lineno, 'bits left unspecified (0x{0:08x})'
                          .format(allbits ^ insnmask))
# end parse_general


def parse_file(f):
    """Parse all of the patterns within a file"""

    # Read all of the lines of the file.  Concatenate lines
    # ending in backslash; discard empty lines and comments.
    toks = []
    lineno = 0
    for line in f:
        lineno += 1

        # Discard comments
        end = line.find('#')
        if end >= 0:
            line = line[:end]

        t = line.split()
        if len(toks) != 0:
            # Next line after continuation
            toks.extend(t)
        elif len(t) == 0:
            # Empty line
            continue
        else:
            toks = t

        # Continuation?
        if toks[-1] == '\\':
            toks.pop()
            continue

        if len(toks) < 2:
            error(lineno, 'short line')

        name = toks[0]
        del toks[0]

        # Determine the type of object needing to be parsed.
        if name[0] == '%':
            parse_field(lineno, name[1:], toks)
        elif name[0] == '&':
            parse_arguments(lineno, name[1:], toks)
        elif name[0] == '@':
            parse_generic(lineno, True, name[1:], toks)
        else:
            parse_generic(lineno, False, name, toks)
        toks = []
# end parse_file


class Tree:
    """Class representing a node in a decode tree"""

    def __init__(self, fm, tm):
        self.fixedmask = fm
        self.thismask = tm
        self.subs = []
        self.base = None

    def str1(self, i):
        ind = str_indent(i)
        r = '{0}{1:08x}'.format(ind, self.fixedmask)
        if self.format:
            r += ' ' + self.format.name
        r += ' [\n'
        for (b, s) in self.subs:
            r += '{0}  {1:08x}:\n'.format(ind, b)
            r += s.str1(i + 4) + '\n'
        r += ind + ']'
        return r

    def __str__(self):
        return self.str1(0)

    def output_code(self, i, extracted, outerbits, outermask):
        ind = str_indent(i)

        # If we identified all nodes below have the same format,
        # extract the fields now.
        if not extracted and self.base:
            output(ind, self.base.extract_name(),
                   '(&u.f_', self.base.base.name, ', insn);\n')
            extracted = True

        # Attempt to aid the compiler in producing compact switch statements.
        # If the bits in the mask are contiguous, extract them.
        sh = is_contiguous(self.thismask)
        if sh > 0:
            # Propagate SH down into the local functions.
            def str_switch(b, sh=sh):
                return '(insn >> {0}) & 0x{1:x}'.format(sh, b >> sh)

            def str_case(b, sh=sh):
                return '0x{0:x}'.format(b >> sh)
        else:
            def str_switch(b):
                return 'insn & 0x{0:08x}'.format(b)

            def str_case(b):
                return '0x{0:08x}'.format(b)

        output(ind, 'switch (', str_switch(self.thismask), ') {\n')
        for b, s in sorted(self.subs):
            assert (self.thismask & ~s.fixedmask) == 0
            innermask = outermask | self.thismask
            innerbits = outerbits | b
            output(ind, 'case ', str_case(b), ':\n')
            output(ind, '    /* ',
                   str_match_bits(innerbits, innermask), ' */\n')
            s.output_code(i + 4, extracted, innerbits, innermask)
        output(ind, '}\n')
        output(ind, 'return false;\n')
# end Tree


def build_tree(pats, outerbits, outermask):
    # Find the intersection of all remaining fixedmask.
    innermask = ~outermask
    for i in pats:
        innermask &= i.fixedmask

    if innermask == 0:
        pnames = []
        for p in pats:
            pnames.append(p.name + ':' + p.file + ':' + str(p.lineno))
        error_with_file(pats[0].file, pats[0].lineno,
                        'overlapping patterns:', pnames)

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
            s = build_tree(l, b | outerbits, fullmask)
        t.subs.append((b, s))

    return t
# end build_tree


def prop_format(tree):
    """Propagate Format objects into the decode tree"""

    # Depth first search.
    for (b, s) in tree.subs:
        if isinstance(s, Tree):
            prop_format(s)

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
# end prop_format


def main():
    global arguments
    global formats
    global patterns
    global translate_scope
    global translate_prefix
    global output_fd
    global output_file
    global input_file
    global insnwidth
    global insntype
    global insnmask
    global decode_function

    decode_scope = 'static '

    long_opts = ['decode=', 'translate=', 'output=', 'insnwidth=']
    try:
        (opts, args) = getopt.getopt(sys.argv[1:], 'o:w:', long_opts)
    except getopt.GetoptError as err:
        error(0, err)
    for o, a in opts:
        if o in ('-o', '--output'):
            output_file = a
        elif o == '--decode':
            decode_function = a
            decode_scope = ''
        elif o == '--translate':
            translate_prefix = a
            translate_scope = ''
        elif o in ('-w', '--insnwidth'):
            insnwidth = int(a)
            if insnwidth == 16:
                insntype = 'uint16_t'
                insnmask = 0xffff
            elif insnwidth != 32:
                error(0, 'cannot handle insns of width', insnwidth)
        else:
            assert False, 'unhandled option'

    if len(args) < 1:
        error(0, 'missing input file')
    for filename in args:
        input_file = filename
        f = open(filename, 'r')
        parse_file(f)
        f.close()

    t = build_tree(patterns, 0, 0)
    prop_format(t)

    if output_file:
        output_fd = open(output_file, 'w')
    else:
        output_fd = sys.stdout

    output_autogen()
    for n in sorted(arguments.keys()):
        f = arguments[n]
        f.output_def()

    # A single translate function can be invoked for different patterns.
    # Make sure that the argument sets are the same, and declare the
    # function only once.
    out_pats = {}
    for i in patterns:
        if i.name in out_pats:
            p = out_pats[i.name]
            if i.base.base != p.base.base:
                error(0, i.name, ' has conflicting argument sets')
        else:
            i.output_decl()
            out_pats[i.name] = i
    output('\n')

    for n in sorted(formats.keys()):
        f = formats[n]
        f.output_extract()

    output(decode_scope, 'bool ', decode_function,
           '(DisasContext *ctx, ', insntype, ' insn)\n{\n')

    i4 = str_indent(4)
    output(i4, 'union {\n')
    for n in sorted(arguments.keys()):
        f = arguments[n]
        output(i4, i4, f.struct_name(), ' f_', f.name, ';\n')
    output(i4, '} u;\n\n')

    t.output_code(4, False, 0, 0)

    output('}\n')

    if output_file:
        output_fd.close()
# end main


if __name__ == '__main__':
    main()
