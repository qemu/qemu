#
# QAPI helper library
#
# Copyright IBM, Corp. 2011
# Copyright (c) 2013 Red Hat Inc.
#
# Authors:
#  Anthony Liguori <aliguori@us.ibm.com>
#  Markus Armbruster <armbru@redhat.com>
#
# This work is licensed under the terms of the GNU GPLv2.
# See the COPYING.LIB file in the top-level directory.

from ordereddict import OrderedDict
import sys

builtin_types = [
    'str', 'int', 'number', 'bool',
    'int8', 'int16', 'int32', 'int64',
    'uint8', 'uint16', 'uint32', 'uint64'
]

builtin_type_qtypes = {
    'str':      'QTYPE_QSTRING',
    'int':      'QTYPE_QINT',
    'number':   'QTYPE_QFLOAT',
    'bool':     'QTYPE_QBOOL',
    'int8':     'QTYPE_QINT',
    'int16':    'QTYPE_QINT',
    'int32':    'QTYPE_QINT',
    'int64':    'QTYPE_QINT',
    'uint8':    'QTYPE_QINT',
    'uint16':   'QTYPE_QINT',
    'uint32':   'QTYPE_QINT',
    'uint64':   'QTYPE_QINT',
}

class QAPISchemaError(Exception):
    def __init__(self, schema, msg):
        self.fp = schema.fp
        self.msg = msg
        self.line = self.col = 1
        for ch in schema.src[0:schema.pos]:
            if ch == '\n':
                self.line += 1
                self.col = 1
            elif ch == '\t':
                self.col = (self.col + 7) % 8 + 1
            else:
                self.col += 1

    def __str__(self):
        return "%s:%s:%s: %s" % (self.fp.name, self.line, self.col, self.msg)

class QAPISchema:

    def __init__(self, fp):
        self.fp = fp
        self.src = fp.read()
        if self.src == '' or self.src[-1] != '\n':
            self.src += '\n'
        self.cursor = 0
        self.exprs = []
        self.accept()

        while self.tok != None:
            self.exprs.append(self.get_expr(False))

    def accept(self):
        while True:
            self.tok = self.src[self.cursor]
            self.pos = self.cursor
            self.cursor += 1
            self.val = None

            if self.tok == '#':
                self.cursor = self.src.find('\n', self.cursor)
            elif self.tok in ['{', '}', ':', ',', '[', ']']:
                return
            elif self.tok == "'":
                string = ''
                esc = False
                while True:
                    ch = self.src[self.cursor]
                    self.cursor += 1
                    if ch == '\n':
                        raise QAPISchemaError(self,
                                              'Missing terminating "\'"')
                    if esc:
                        string += ch
                        esc = False
                    elif ch == "\\":
                        esc = True
                    elif ch == "'":
                        self.val = string
                        return
                    else:
                        string += ch
            elif self.tok == '\n':
                if self.cursor == len(self.src):
                    self.tok = None
                    return
            elif not self.tok.isspace():
                raise QAPISchemaError(self, 'Stray "%s"' % self.tok)

    def get_members(self):
        expr = OrderedDict()
        if self.tok == '}':
            self.accept()
            return expr
        if self.tok != "'":
            raise QAPISchemaError(self, 'Expected string or "}"')
        while True:
            key = self.val
            self.accept()
            if self.tok != ':':
                raise QAPISchemaError(self, 'Expected ":"')
            self.accept()
            expr[key] = self.get_expr(True)
            if self.tok == '}':
                self.accept()
                return expr
            if self.tok != ',':
                raise QAPISchemaError(self, 'Expected "," or "}"')
            self.accept()
            if self.tok != "'":
                raise QAPISchemaError(self, 'Expected string')

    def get_values(self):
        expr = []
        if self.tok == ']':
            self.accept()
            return expr
        if not self.tok in [ '{', '[', "'" ]:
            raise QAPISchemaError(self, 'Expected "{", "[", "]" or string')
        while True:
            expr.append(self.get_expr(True))
            if self.tok == ']':
                self.accept()
                return expr
            if self.tok != ',':
                raise QAPISchemaError(self, 'Expected "," or "]"')
            self.accept()

    def get_expr(self, nested):
        if self.tok != '{' and not nested:
            raise QAPISchemaError(self, 'Expected "{"')
        if self.tok == '{':
            self.accept()
            expr = self.get_members()
        elif self.tok == '[':
            self.accept()
            expr = self.get_values()
        elif self.tok == "'":
            expr = self.val
            self.accept()
        else:
            raise QAPISchemaError(self, 'Expected "{", "[" or string')
        return expr

def parse_schema(fp):
    try:
        schema = QAPISchema(fp)
    except QAPISchemaError, e:
        print >>sys.stderr, e
        exit(1)

    exprs = []

    for expr in schema.exprs:
        if expr.has_key('enum'):
            add_enum(expr['enum'])
        elif expr.has_key('union'):
            add_union(expr)
            add_enum('%sKind' % expr['union'])
        elif expr.has_key('type'):
            add_struct(expr)
        exprs.append(expr)

    return exprs

def parse_args(typeinfo):
    if isinstance(typeinfo, basestring):
        struct = find_struct(typeinfo)
        assert struct != None
        typeinfo = struct['data']

    for member in typeinfo:
        argname = member
        argentry = typeinfo[member]
        optional = False
        structured = False
        if member.startswith('*'):
            argname = member[1:]
            optional = True
        if isinstance(argentry, OrderedDict):
            structured = True
        yield (argname, argentry, optional, structured)

def de_camel_case(name):
    new_name = ''
    for ch in name:
        if ch.isupper() and new_name:
            new_name += '_'
        if ch == '-':
            new_name += '_'
        else:
            new_name += ch.lower()
    return new_name

def camel_case(name):
    new_name = ''
    first = True
    for ch in name:
        if ch in ['_', '-']:
            first = True
        elif first:
            new_name += ch.upper()
            first = False
        else:
            new_name += ch.lower()
    return new_name

def c_var(name, protect=True):
    # ANSI X3J11/88-090, 3.1.1
    c89_words = set(['auto', 'break', 'case', 'char', 'const', 'continue',
                     'default', 'do', 'double', 'else', 'enum', 'extern', 'float',
                     'for', 'goto', 'if', 'int', 'long', 'register', 'return',
                     'short', 'signed', 'sizeof', 'static', 'struct', 'switch',
                     'typedef', 'union', 'unsigned', 'void', 'volatile', 'while'])
    # ISO/IEC 9899:1999, 6.4.1
    c99_words = set(['inline', 'restrict', '_Bool', '_Complex', '_Imaginary'])
    # ISO/IEC 9899:2011, 6.4.1
    c11_words = set(['_Alignas', '_Alignof', '_Atomic', '_Generic', '_Noreturn',
                     '_Static_assert', '_Thread_local'])
    # GCC http://gcc.gnu.org/onlinedocs/gcc-4.7.1/gcc/C-Extensions.html
    # excluding _.*
    gcc_words = set(['asm', 'typeof'])
    # namespace pollution:
    polluted_words = set(['unix'])
    if protect and (name in c89_words | c99_words | c11_words | gcc_words | polluted_words):
        return "q_" + name
    return name.replace('-', '_').lstrip("*")

def c_fun(name, protect=True):
    return c_var(name, protect).replace('.', '_')

def c_list_type(name):
    return '%sList' % name

def type_name(name):
    if type(name) == list:
        return c_list_type(name[0])
    return name

enum_types = []
struct_types = []
union_types = []

def add_struct(definition):
    global struct_types
    struct_types.append(definition)

def find_struct(name):
    global struct_types
    for struct in struct_types:
        if struct['type'] == name:
            return struct
    return None

def add_union(definition):
    global union_types
    union_types.append(definition)

def find_union(name):
    global union_types
    for union in union_types:
        if union['union'] == name:
            return union
    return None

def add_enum(name):
    global enum_types
    enum_types.append(name)

def is_enum(name):
    global enum_types
    return (name in enum_types)

def c_type(name):
    if name == 'str':
        return 'char *'
    elif name == 'int':
        return 'int64_t'
    elif (name == 'int8' or name == 'int16' or name == 'int32' or
          name == 'int64' or name == 'uint8' or name == 'uint16' or
          name == 'uint32' or name == 'uint64'):
        return name + '_t'
    elif name == 'size':
        return 'uint64_t'
    elif name == 'bool':
        return 'bool'
    elif name == 'number':
        return 'double'
    elif type(name) == list:
        return '%s *' % c_list_type(name[0])
    elif is_enum(name):
        return name
    elif name == None or len(name) == 0:
        return 'void'
    elif name == name.upper():
        return '%sEvent *' % camel_case(name)
    else:
        return '%s *' % name

def genindent(count):
    ret = ""
    for i in range(count):
        ret += " "
    return ret

indent_level = 0

def push_indent(indent_amount=4):
    global indent_level
    indent_level += indent_amount

def pop_indent(indent_amount=4):
    global indent_level
    indent_level -= indent_amount

def cgen(code, **kwds):
    indent = genindent(indent_level)
    lines = code.split('\n')
    lines = map(lambda x: indent + x, lines)
    return '\n'.join(lines) % kwds + '\n'

def mcgen(code, **kwds):
    return cgen('\n'.join(code.split('\n')[1:-1]), **kwds)

def basename(filename):
    return filename.split("/")[-1]

def guardname(filename):
    guard = basename(filename).rsplit(".", 1)[0]
    for substr in [".", " ", "-"]:
        guard = guard.replace(substr, "_")
    return guard.upper() + '_H'

def guardstart(name):
    return mcgen('''

#ifndef %(name)s
#define %(name)s

''',
                 name=guardname(name))

def guardend(name):
    return mcgen('''

#endif /* %(name)s */

''',
                 name=guardname(name))
