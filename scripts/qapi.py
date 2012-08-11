#
# QAPI helper library
#
# Copyright IBM, Corp. 2011
#
# Authors:
#  Anthony Liguori <aliguori@us.ibm.com>
#
# This work is licensed under the terms of the GNU GPLv2.
# See the COPYING.LIB file in the top-level directory.

from ordereddict import OrderedDict

def tokenize(data):
    while len(data):
        ch = data[0]
        data = data[1:]
        if ch in ['{', '}', ':', ',', '[', ']']:
            yield ch
        elif ch in ' \n':
            None
        elif ch == "'":
            string = ''
            esc = False
            while True:
                if (data == ''):
                    raise Exception("Mismatched quotes")
                ch = data[0]
                data = data[1:]
                if esc:
                    string += ch
                    esc = False
                elif ch == "\\":
                    esc = True
                elif ch == "'":
                    break
                else:
                    string += ch
            yield string

def parse(tokens):
    if tokens[0] == '{':
        ret = OrderedDict()
        tokens = tokens[1:]
        while tokens[0] != '}':
            key = tokens[0]
            tokens = tokens[1:]

            tokens = tokens[1:] # :

            value, tokens = parse(tokens)

            if tokens[0] == ',':
                tokens = tokens[1:]

            ret[key] = value
        tokens = tokens[1:]
        return ret, tokens
    elif tokens[0] == '[':
        ret = []
        tokens = tokens[1:]
        while tokens[0] != ']':
            value, tokens = parse(tokens)
            if tokens[0] == ',':
                tokens = tokens[1:]
            ret.append(value)
        tokens = tokens[1:]
        return ret, tokens
    else:
        return tokens[0], tokens[1:]

def evaluate(string):
    return parse(map(lambda x: x, tokenize(string)))[0]

def parse_schema(fp):
    exprs = []
    expr = ''
    expr_eval = None

    for line in fp:
        if line.startswith('#') or line == '\n':
            continue

        if line.startswith(' '):
            expr += line
        elif expr:
            expr_eval = evaluate(expr)
            if expr_eval.has_key('enum'):
                add_enum(expr_eval['enum'])
            elif expr_eval.has_key('union'):
                add_enum('%sKind' % expr_eval['union'])
            exprs.append(expr_eval)
            expr = line
        else:
            expr += line

    if expr:
        expr_eval = evaluate(expr)
        if expr_eval.has_key('enum'):
            add_enum(expr_eval['enum'])
        elif expr_eval.has_key('union'):
            add_enum('%sKind' % expr_eval['union'])
        exprs.append(expr_eval)

    return exprs

def parse_args(typeinfo):
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

def c_var(name):
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
    if name in c89_words | c99_words | c11_words | gcc_words:
        return "q_" + name
    return name.replace('-', '_').lstrip("*")

def c_fun(name):
    return c_var(name).replace('.', '_')

def c_list_type(name):
    return '%sList' % name

def type_name(name):
    if type(name) == list:
        return c_list_type(name[0])
    return name

enum_types = []

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
