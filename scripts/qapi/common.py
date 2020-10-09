#
# QAPI helper library
#
# Copyright IBM, Corp. 2011
# Copyright (c) 2013-2018 Red Hat Inc.
#
# Authors:
#  Anthony Liguori <aliguori@us.ibm.com>
#  Markus Armbruster <armbru@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

import re


# ENUMName -> ENUM_NAME, EnumName1 -> ENUM_NAME1
# ENUM_NAME -> ENUM_NAME, ENUM_NAME1 -> ENUM_NAME1, ENUM_Name2 -> ENUM_NAME2
# ENUM24_Name -> ENUM24_NAME
def camel_to_upper(value):
    c_fun_str = c_name(value, False)
    if value.isupper():
        return c_fun_str

    new_name = ''
    length = len(c_fun_str)
    for i in range(length):
        c = c_fun_str[i]
        # When c is upper and no '_' appears before, do more checks
        if c.isupper() and (i > 0) and c_fun_str[i - 1] != '_':
            if i < length - 1 and c_fun_str[i + 1].islower():
                new_name += '_'
            elif c_fun_str[i - 1].isdigit():
                new_name += '_'
        new_name += c
    return new_name.lstrip('_').upper()


def c_enum_const(type_name, const_name, prefix=None):
    if prefix is not None:
        type_name = prefix
    return camel_to_upper(type_name) + '_' + c_name(const_name, False).upper()


c_name_trans = str.maketrans('.-', '__')


# Map @name to a valid C identifier.
# If @protect, avoid returning certain ticklish identifiers (like
# C keywords) by prepending 'q_'.
#
# Used for converting 'name' from a 'name':'type' qapi definition
# into a generated struct member, as well as converting type names
# into substrings of a generated C function name.
# '__a.b_c' -> '__a_b_c', 'x-foo' -> 'x_foo'
# protect=True: 'int' -> 'q_int'; protect=False: 'int' -> 'int'
def c_name(name, protect=True):
    # ANSI X3J11/88-090, 3.1.1
    c89_words = set(['auto', 'break', 'case', 'char', 'const', 'continue',
                     'default', 'do', 'double', 'else', 'enum', 'extern',
                     'float', 'for', 'goto', 'if', 'int', 'long', 'register',
                     'return', 'short', 'signed', 'sizeof', 'static',
                     'struct', 'switch', 'typedef', 'union', 'unsigned',
                     'void', 'volatile', 'while'])
    # ISO/IEC 9899:1999, 6.4.1
    c99_words = set(['inline', 'restrict', '_Bool', '_Complex', '_Imaginary'])
    # ISO/IEC 9899:2011, 6.4.1
    c11_words = set(['_Alignas', '_Alignof', '_Atomic', '_Generic',
                     '_Noreturn', '_Static_assert', '_Thread_local'])
    # GCC http://gcc.gnu.org/onlinedocs/gcc-4.7.1/gcc/C-Extensions.html
    # excluding _.*
    gcc_words = set(['asm', 'typeof'])
    # C++ ISO/IEC 14882:2003 2.11
    cpp_words = set(['bool', 'catch', 'class', 'const_cast', 'delete',
                     'dynamic_cast', 'explicit', 'false', 'friend', 'mutable',
                     'namespace', 'new', 'operator', 'private', 'protected',
                     'public', 'reinterpret_cast', 'static_cast', 'template',
                     'this', 'throw', 'true', 'try', 'typeid', 'typename',
                     'using', 'virtual', 'wchar_t',
                     # alternative representations
                     'and', 'and_eq', 'bitand', 'bitor', 'compl', 'not',
                     'not_eq', 'or', 'or_eq', 'xor', 'xor_eq'])
    # namespace pollution:
    polluted_words = set(['unix', 'errno', 'mips', 'sparc', 'i386'])
    name = name.translate(c_name_trans)
    if protect and (name in c89_words | c99_words | c11_words | gcc_words
                    | cpp_words | polluted_words):
        return 'q_' + name
    return name


eatspace = '\033EATSPACE.'
pointer_suffix = ' *' + eatspace


class Indentation:
    """
    Indentation level management.

    :param initial: Initial number of spaces, default 0.
    """
    def __init__(self, initial: int = 0) -> None:
        self._level = initial

    def __int__(self) -> int:
        return self._level

    def __repr__(self) -> str:
        return "{}({:d})".format(type(self).__name__, self._level)

    def __str__(self) -> str:
        """Return the current indentation as a string of spaces."""
        return ' ' * self._level

    def __bool__(self) -> bool:
        """True when there is a non-zero indentation."""
        return bool(self._level)

    def increase(self, amount: int = 4) -> None:
        """Increase the indentation level by ``amount``, default 4."""
        self._level += amount

    def decrease(self, amount: int = 4) -> None:
        """Decrease the indentation level by ``amount``, default 4."""
        if self._level < amount:
            raise ArithmeticError(
                f"Can't remove {amount:d} spaces from {self!r}")
        self._level -= amount


indent = Indentation()


# Generate @code with @kwds interpolated.
# Obey indent, and strip eatspace.
def cgen(code, **kwds):
    raw = code % kwds
    if indent:
        raw = re.sub(r'^(?!(#|$))', str(indent), raw, flags=re.MULTILINE)
    return re.sub(re.escape(eatspace) + r' *', '', raw)


def mcgen(code, **kwds):
    if code[0] == '\n':
        code = code[1:]
    return cgen(code, **kwds)


def c_fname(filename):
    return re.sub(r'[^A-Za-z0-9_]', '_', filename)


def guardstart(name):
    return mcgen('''
#ifndef %(name)s
#define %(name)s

''',
                 name=c_fname(name).upper())


def guardend(name):
    return mcgen('''

#endif /* %(name)s */
''',
                 name=c_fname(name).upper())


def gen_if(ifcond):
    ret = ''
    for ifc in ifcond:
        ret += mcgen('''
#if %(cond)s
''', cond=ifc)
    return ret


def gen_endif(ifcond):
    ret = ''
    for ifc in reversed(ifcond):
        ret += mcgen('''
#endif /* %(cond)s */
''', cond=ifc)
    return ret


def build_params(arg_type, boxed, extra=None):
    ret = ''
    sep = ''
    if boxed:
        assert arg_type
        ret += '%s arg' % arg_type.c_param_type()
        sep = ', '
    elif arg_type:
        assert not arg_type.variants
        for memb in arg_type.members:
            ret += sep
            sep = ', '
            if memb.optional:
                ret += 'bool has_%s, ' % c_name(memb.name)
            ret += '%s %s' % (memb.type.c_param_type(),
                              c_name(memb.name))
    if extra:
        ret += sep + extra
    return ret if ret else 'void'
