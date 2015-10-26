#
# QAPI helper library
#
# Copyright IBM, Corp. 2011
# Copyright (c) 2013-2015 Red Hat Inc.
#
# Authors:
#  Anthony Liguori <aliguori@us.ibm.com>
#  Markus Armbruster <armbru@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

import re
from ordereddict import OrderedDict
import errno
import getopt
import os
import sys
import string

builtin_types = {
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
    'size':     'QTYPE_QINT',
    'any':      None,           # any qtype_code possible, actually
}

# Whitelist of commands allowed to return a non-dictionary
returns_whitelist = [
    # From QMP:
    'human-monitor-command',
    'qom-get',
    'query-migrate-cache-size',
    'query-tpm-models',
    'query-tpm-types',
    'ringbuf-read',

    # From QGA:
    'guest-file-open',
    'guest-fsfreeze-freeze',
    'guest-fsfreeze-freeze-list',
    'guest-fsfreeze-status',
    'guest-fsfreeze-thaw',
    'guest-get-time',
    'guest-set-vcpus',
    'guest-sync',
    'guest-sync-delimited',
]

enum_types = []
struct_types = []
union_types = []
events = []
all_names = {}

#
# Parsing the schema into expressions
#


def error_path(parent):
    res = ""
    while parent:
        res = ("In file included from %s:%d:\n" % (parent['file'],
                                                   parent['line'])) + res
        parent = parent['parent']
    return res


class QAPISchemaError(Exception):
    def __init__(self, schema, msg):
        Exception.__init__(self)
        self.fname = schema.fname
        self.msg = msg
        self.col = 1
        self.line = schema.line
        for ch in schema.src[schema.line_pos:schema.pos]:
            if ch == '\t':
                self.col = (self.col + 7) % 8 + 1
            else:
                self.col += 1
        self.info = schema.incl_info

    def __str__(self):
        return error_path(self.info) + \
            "%s:%d:%d: %s" % (self.fname, self.line, self.col, self.msg)


class QAPIExprError(Exception):
    def __init__(self, expr_info, msg):
        Exception.__init__(self)
        assert expr_info
        self.info = expr_info
        self.msg = msg

    def __str__(self):
        return error_path(self.info['parent']) + \
            "%s:%d: %s" % (self.info['file'], self.info['line'], self.msg)


class QAPISchemaParser(object):

    def __init__(self, fp, previously_included=[], incl_info=None):
        abs_fname = os.path.abspath(fp.name)
        fname = fp.name
        self.fname = fname
        previously_included.append(abs_fname)
        self.incl_info = incl_info
        self.src = fp.read()
        if self.src == '' or self.src[-1] != '\n':
            self.src += '\n'
        self.cursor = 0
        self.line = 1
        self.line_pos = 0
        self.exprs = []
        self.accept()

        while self.tok is not None:
            expr_info = {'file': fname, 'line': self.line,
                         'parent': self.incl_info}
            expr = self.get_expr(False)
            if isinstance(expr, dict) and "include" in expr:
                if len(expr) != 1:
                    raise QAPIExprError(expr_info,
                                        "Invalid 'include' directive")
                include = expr["include"]
                if not isinstance(include, str):
                    raise QAPIExprError(expr_info,
                                        "Value of 'include' must be a string")
                incl_abs_fname = os.path.join(os.path.dirname(abs_fname),
                                              include)
                # catch inclusion cycle
                inf = expr_info
                while inf:
                    if incl_abs_fname == os.path.abspath(inf['file']):
                        raise QAPIExprError(expr_info, "Inclusion loop for %s"
                                            % include)
                    inf = inf['parent']
                # skip multiple include of the same file
                if incl_abs_fname in previously_included:
                    continue
                try:
                    fobj = open(incl_abs_fname, 'r')
                except IOError, e:
                    raise QAPIExprError(expr_info,
                                        '%s: %s' % (e.strerror, include))
                exprs_include = QAPISchemaParser(fobj, previously_included,
                                                 expr_info)
                self.exprs.extend(exprs_include.exprs)
            else:
                expr_elem = {'expr': expr,
                             'info': expr_info}
                self.exprs.append(expr_elem)

    def accept(self):
        while True:
            self.tok = self.src[self.cursor]
            self.pos = self.cursor
            self.cursor += 1
            self.val = None

            if self.tok == '#':
                self.cursor = self.src.find('\n', self.cursor)
            elif self.tok in "{}:,[]":
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
                        if ch == 'b':
                            string += '\b'
                        elif ch == 'f':
                            string += '\f'
                        elif ch == 'n':
                            string += '\n'
                        elif ch == 'r':
                            string += '\r'
                        elif ch == 't':
                            string += '\t'
                        elif ch == 'u':
                            value = 0
                            for _ in range(0, 4):
                                ch = self.src[self.cursor]
                                self.cursor += 1
                                if ch not in "0123456789abcdefABCDEF":
                                    raise QAPISchemaError(self,
                                                          '\\u escape needs 4 '
                                                          'hex digits')
                                value = (value << 4) + int(ch, 16)
                            # If Python 2 and 3 didn't disagree so much on
                            # how to handle Unicode, then we could allow
                            # Unicode string defaults.  But most of QAPI is
                            # ASCII-only, so we aren't losing much for now.
                            if not value or value > 0x7f:
                                raise QAPISchemaError(self,
                                                      'For now, \\u escape '
                                                      'only supports non-zero '
                                                      'values up to \\u007f')
                            string += chr(value)
                        elif ch in "\\/'\"":
                            string += ch
                        else:
                            raise QAPISchemaError(self,
                                                  "Unknown escape \\%s" % ch)
                        esc = False
                    elif ch == "\\":
                        esc = True
                    elif ch == "'":
                        self.val = string
                        return
                    else:
                        string += ch
            elif self.src.startswith("true", self.pos):
                self.val = True
                self.cursor += 3
                return
            elif self.src.startswith("false", self.pos):
                self.val = False
                self.cursor += 4
                return
            elif self.src.startswith("null", self.pos):
                self.val = None
                self.cursor += 3
                return
            elif self.tok == '\n':
                if self.cursor == len(self.src):
                    self.tok = None
                    return
                self.line += 1
                self.line_pos = self.cursor
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
            if key in expr:
                raise QAPISchemaError(self, 'Duplicate key "%s"' % key)
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
        if self.tok not in "{['tfn":
            raise QAPISchemaError(self, 'Expected "{", "[", "]", string, '
                                  'boolean or "null"')
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
        elif self.tok in "'tfn":
            expr = self.val
            self.accept()
        else:
            raise QAPISchemaError(self, 'Expected "{", "[" or string')
        return expr

#
# Semantic analysis of schema expressions
# TODO fold into QAPISchema
# TODO catching name collisions in generated code would be nice
#


def find_base_fields(base):
    base_struct_define = find_struct(base)
    if not base_struct_define:
        return None
    return base_struct_define['data']


# Return the qtype of an alternate branch, or None on error.
def find_alternate_member_qtype(qapi_type):
    if qapi_type in builtin_types:
        return builtin_types[qapi_type]
    elif find_struct(qapi_type):
        return "QTYPE_QDICT"
    elif find_enum(qapi_type):
        return "QTYPE_QSTRING"
    elif find_union(qapi_type):
        return "QTYPE_QDICT"
    return None


# Return the discriminator enum define if discriminator is specified as an
# enum type, otherwise return None.
def discriminator_find_enum_define(expr):
    base = expr.get('base')
    discriminator = expr.get('discriminator')

    if not (discriminator and base):
        return None

    base_fields = find_base_fields(base)
    if not base_fields:
        return None

    discriminator_type = base_fields.get(discriminator)
    if not discriminator_type:
        return None

    return find_enum(discriminator_type)


# FIXME should enforce "other than downstream extensions [...], all
# names should begin with a letter".
valid_name = re.compile('^[a-zA-Z_][a-zA-Z0-9_.-]*$')


def check_name(expr_info, source, name, allow_optional=False,
               enum_member=False):
    global valid_name
    membername = name

    if not isinstance(name, str):
        raise QAPIExprError(expr_info,
                            "%s requires a string name" % source)
    if name.startswith('*'):
        membername = name[1:]
        if not allow_optional:
            raise QAPIExprError(expr_info,
                                "%s does not allow optional name '%s'"
                                % (source, name))
    # Enum members can start with a digit, because the generated C
    # code always prefixes it with the enum name
    if enum_member:
        membername = '_' + membername
    # Reserve the entire 'q_' namespace for c_name()
    if not valid_name.match(membername) or \
       c_name(membername, False).startswith('q_'):
        raise QAPIExprError(expr_info,
                            "%s uses invalid name '%s'" % (source, name))


def add_name(name, info, meta, implicit=False):
    global all_names
    check_name(info, "'%s'" % meta, name)
    # FIXME should reject names that differ only in '_' vs. '.'
    # vs. '-', because they're liable to clash in generated C.
    if name in all_names:
        raise QAPIExprError(info,
                            "%s '%s' is already defined"
                            % (all_names[name], name))
    if not implicit and (name.endswith('Kind') or name.endswith('List')):
        raise QAPIExprError(info,
                            "%s '%s' should not end in '%s'"
                            % (meta, name, name[-4:]))
    all_names[name] = meta


def add_struct(definition, info):
    global struct_types
    name = definition['struct']
    add_name(name, info, 'struct')
    struct_types.append(definition)


def find_struct(name):
    global struct_types
    for struct in struct_types:
        if struct['struct'] == name:
            return struct
    return None


def add_union(definition, info):
    global union_types
    name = definition['union']
    add_name(name, info, 'union')
    union_types.append(definition)


def find_union(name):
    global union_types
    for union in union_types:
        if union['union'] == name:
            return union
    return None


def add_enum(name, info, enum_values=None, implicit=False):
    global enum_types
    add_name(name, info, 'enum', implicit)
    enum_types.append({"enum_name": name, "enum_values": enum_values})


def find_enum(name):
    global enum_types
    for enum in enum_types:
        if enum['enum_name'] == name:
            return enum
    return None


def is_enum(name):
    return find_enum(name) is not None


def check_type(expr_info, source, value, allow_array=False,
               allow_dict=False, allow_optional=False,
               allow_metas=[]):
    global all_names

    if value is None:
        return

    # Check if array type for value is okay
    if isinstance(value, list):
        if not allow_array:
            raise QAPIExprError(expr_info,
                                "%s cannot be an array" % source)
        if len(value) != 1 or not isinstance(value[0], str):
            raise QAPIExprError(expr_info,
                                "%s: array type must contain single type name"
                                % source)
        value = value[0]

    # Check if type name for value is okay
    if isinstance(value, str):
        if value not in all_names:
            raise QAPIExprError(expr_info,
                                "%s uses unknown type '%s'"
                                % (source, value))
        if not all_names[value] in allow_metas:
            raise QAPIExprError(expr_info,
                                "%s cannot use %s type '%s'"
                                % (source, all_names[value], value))
        return

    if not allow_dict:
        raise QAPIExprError(expr_info,
                            "%s should be a type name" % source)

    if not isinstance(value, OrderedDict):
        raise QAPIExprError(expr_info,
                            "%s should be a dictionary or type name" % source)

    # value is a dictionary, check that each member is okay
    for (key, arg) in value.items():
        check_name(expr_info, "Member of %s" % source, key,
                   allow_optional=allow_optional)
        if c_name(key, False) == 'u' or c_name(key, False).startswith('has_'):
            raise QAPIExprError(expr_info,
                                "Member of %s uses reserved name '%s'"
                                % (source, key))
        # Todo: allow dictionaries to represent default values of
        # an optional argument.
        check_type(expr_info, "Member '%s' of %s" % (key, source), arg,
                   allow_array=True,
                   allow_metas=['built-in', 'union', 'alternate', 'struct',
                                'enum'])


def check_member_clash(expr_info, base_name, data, source=""):
    base = find_struct(base_name)
    assert base
    base_members = base['data']
    for key in data.keys():
        if key.startswith('*'):
            key = key[1:]
        if key in base_members or "*" + key in base_members:
            raise QAPIExprError(expr_info,
                                "Member name '%s'%s clashes with base '%s'"
                                % (key, source, base_name))
    if base.get('base'):
        check_member_clash(expr_info, base['base'], data, source)


def check_command(expr, expr_info):
    name = expr['command']

    check_type(expr_info, "'data' for command '%s'" % name,
               expr.get('data'), allow_dict=True, allow_optional=True,
               allow_metas=['struct'])
    returns_meta = ['union', 'struct']
    if name in returns_whitelist:
        returns_meta += ['built-in', 'alternate', 'enum']
    check_type(expr_info, "'returns' for command '%s'" % name,
               expr.get('returns'), allow_array=True,
               allow_optional=True, allow_metas=returns_meta)


def check_event(expr, expr_info):
    global events
    name = expr['event']

    if name.upper() == 'MAX':
        raise QAPIExprError(expr_info, "Event name 'MAX' cannot be created")
    events.append(name)
    check_type(expr_info, "'data' for event '%s'" % name,
               expr.get('data'), allow_dict=True, allow_optional=True,
               allow_metas=['struct'])


def check_union(expr, expr_info):
    name = expr['union']
    base = expr.get('base')
    discriminator = expr.get('discriminator')
    members = expr['data']
    values = {'MAX': '(automatic)', 'KIND': '(automatic)',
              'TYPE': '(automatic)'}

    # Two types of unions, determined by discriminator.

    # With no discriminator it is a simple union.
    if discriminator is None:
        enum_define = None
        allow_metas = ['built-in', 'union', 'alternate', 'struct', 'enum']
        if base is not None:
            raise QAPIExprError(expr_info,
                                "Simple union '%s' must not have a base"
                                % name)

    # Else, it's a flat union.
    else:
        # The object must have a string member 'base'.
        check_type(expr_info, "'base' for union '%s'" % name,
                   base, allow_metas=['struct'])
        if not base:
            raise QAPIExprError(expr_info,
                                "Flat union '%s' must have a base"
                                % name)
        base_fields = find_base_fields(base)
        assert base_fields

        # The value of member 'discriminator' must name a non-optional
        # member of the base struct.
        check_name(expr_info, "Discriminator of flat union '%s'" % name,
                   discriminator)
        discriminator_type = base_fields.get(discriminator)
        if not discriminator_type:
            raise QAPIExprError(expr_info,
                                "Discriminator '%s' is not a member of base "
                                "struct '%s'"
                                % (discriminator, base))
        enum_define = find_enum(discriminator_type)
        allow_metas = ['struct']
        # Do not allow string discriminator
        if not enum_define:
            raise QAPIExprError(expr_info,
                                "Discriminator '%s' must be of enumeration "
                                "type" % discriminator)

    # Check every branch
    for (key, value) in members.items():
        check_name(expr_info, "Member of union '%s'" % name, key)

        # Each value must name a known type; furthermore, in flat unions,
        # branches must be a struct with no overlapping member names
        check_type(expr_info, "Member '%s' of union '%s'" % (key, name),
                   value, allow_array=not base, allow_metas=allow_metas)
        if base:
            branch_struct = find_struct(value)
            assert branch_struct
            check_member_clash(expr_info, base, branch_struct['data'],
                               " of branch '%s'" % key)

        # If the discriminator names an enum type, then all members
        # of 'data' must also be members of the enum type, which in turn
        # must not collide with the discriminator name.
        if enum_define:
            if key not in enum_define['enum_values']:
                raise QAPIExprError(expr_info,
                                    "Discriminator value '%s' is not found in "
                                    "enum '%s'" %
                                    (key, enum_define["enum_name"]))
            if discriminator in enum_define['enum_values']:
                raise QAPIExprError(expr_info,
                                    "Discriminator name '%s' collides with "
                                    "enum value in '%s'" %
                                    (discriminator, enum_define["enum_name"]))

        # Otherwise, check for conflicts in the generated enum
        else:
            c_key = camel_to_upper(key)
            if c_key in values:
                raise QAPIExprError(expr_info,
                                    "Union '%s' member '%s' clashes with '%s'"
                                    % (name, key, values[c_key]))
            values[c_key] = key


def check_alternate(expr, expr_info):
    name = expr['alternate']
    members = expr['data']
    values = {'MAX': '(automatic)'}
    types_seen = {}

    # Check every branch
    for (key, value) in members.items():
        check_name(expr_info, "Member of alternate '%s'" % name, key)

        # Check for conflicts in the generated enum
        c_key = camel_to_upper(key)
        if c_key in values:
            raise QAPIExprError(expr_info,
                                "Alternate '%s' member '%s' clashes with '%s'"
                                % (name, key, values[c_key]))
        values[c_key] = key

        # Ensure alternates have no type conflicts.
        check_type(expr_info, "Member '%s' of alternate '%s'" % (key, name),
                   value,
                   allow_metas=['built-in', 'union', 'struct', 'enum'])
        qtype = find_alternate_member_qtype(value)
        assert qtype
        if qtype in types_seen:
            raise QAPIExprError(expr_info,
                                "Alternate '%s' member '%s' can't "
                                "be distinguished from member '%s'"
                                % (name, key, types_seen[qtype]))
        types_seen[qtype] = key


def check_enum(expr, expr_info):
    name = expr['enum']
    members = expr.get('data')
    prefix = expr.get('prefix')
    values = {'MAX': '(automatic)'}

    if not isinstance(members, list):
        raise QAPIExprError(expr_info,
                            "Enum '%s' requires an array for 'data'" % name)
    if prefix is not None and not isinstance(prefix, str):
        raise QAPIExprError(expr_info,
                            "Enum '%s' requires a string for 'prefix'" % name)
    for member in members:
        check_name(expr_info, "Member of enum '%s'" % name, member,
                   enum_member=True)
        key = camel_to_upper(member)
        if key in values:
            raise QAPIExprError(expr_info,
                                "Enum '%s' member '%s' clashes with '%s'"
                                % (name, member, values[key]))
        values[key] = member


def check_struct(expr, expr_info):
    name = expr['struct']
    members = expr['data']

    check_type(expr_info, "'data' for struct '%s'" % name, members,
               allow_dict=True, allow_optional=True)
    check_type(expr_info, "'base' for struct '%s'" % name, expr.get('base'),
               allow_metas=['struct'])
    if expr.get('base'):
        check_member_clash(expr_info, expr['base'], expr['data'])


def check_keys(expr_elem, meta, required, optional=[]):
    expr = expr_elem['expr']
    info = expr_elem['info']
    name = expr[meta]
    if not isinstance(name, str):
        raise QAPIExprError(info,
                            "'%s' key must have a string value" % meta)
    required = required + [meta]
    for (key, value) in expr.items():
        if key not in required and key not in optional:
            raise QAPIExprError(info,
                                "Unknown key '%s' in %s '%s'"
                                % (key, meta, name))
        if (key == 'gen' or key == 'success-response') and value is not False:
            raise QAPIExprError(info,
                                "'%s' of %s '%s' should only use false value"
                                % (key, meta, name))
    for key in required:
        if key not in expr:
            raise QAPIExprError(info,
                                "Key '%s' is missing from %s '%s'"
                                % (key, meta, name))


def check_exprs(exprs):
    global all_names

    # Learn the types and check for valid expression keys
    for builtin in builtin_types.keys():
        all_names[builtin] = 'built-in'
    for expr_elem in exprs:
        expr = expr_elem['expr']
        info = expr_elem['info']
        if 'enum' in expr:
            check_keys(expr_elem, 'enum', ['data'], ['prefix'])
            add_enum(expr['enum'], info, expr['data'])
        elif 'union' in expr:
            check_keys(expr_elem, 'union', ['data'],
                       ['base', 'discriminator'])
            add_union(expr, info)
        elif 'alternate' in expr:
            check_keys(expr_elem, 'alternate', ['data'])
            add_name(expr['alternate'], info, 'alternate')
        elif 'struct' in expr:
            check_keys(expr_elem, 'struct', ['data'], ['base'])
            add_struct(expr, info)
        elif 'command' in expr:
            check_keys(expr_elem, 'command', [],
                       ['data', 'returns', 'gen', 'success-response'])
            add_name(expr['command'], info, 'command')
        elif 'event' in expr:
            check_keys(expr_elem, 'event', [], ['data'])
            add_name(expr['event'], info, 'event')
        else:
            raise QAPIExprError(expr_elem['info'],
                                "Expression is missing metatype")

    # Try again for hidden UnionKind enum
    for expr_elem in exprs:
        expr = expr_elem['expr']
        if 'union' in expr:
            if not discriminator_find_enum_define(expr):
                add_enum('%sKind' % expr['union'], expr_elem['info'],
                         implicit=True)
        elif 'alternate' in expr:
            add_enum('%sKind' % expr['alternate'], expr_elem['info'],
                     implicit=True)

    # Validate that exprs make sense
    for expr_elem in exprs:
        expr = expr_elem['expr']
        info = expr_elem['info']

        if 'enum' in expr:
            check_enum(expr, info)
        elif 'union' in expr:
            check_union(expr, info)
        elif 'alternate' in expr:
            check_alternate(expr, info)
        elif 'struct' in expr:
            check_struct(expr, info)
        elif 'command' in expr:
            check_command(expr, info)
        elif 'event' in expr:
            check_event(expr, info)
        else:
            assert False, 'unexpected meta type'

    return exprs


#
# Schema compiler frontend
#

class QAPISchemaEntity(object):
    def __init__(self, name, info):
        assert isinstance(name, str)
        self.name = name
        # For explicitly defined entities, info points to the (explicit)
        # definition.  For builtins (and their arrays), info is None.
        # For implicitly defined entities, info points to a place that
        # triggered the implicit definition (there may be more than one
        # such place).
        self.info = info

    def c_name(self):
        return c_name(self.name)

    def check(self, schema):
        pass

    def is_implicit(self):
        return not self.info

    def visit(self, visitor):
        pass


class QAPISchemaVisitor(object):
    def visit_begin(self, schema):
        pass

    def visit_end(self):
        pass

    def visit_needed(self, entity):
        # Default to visiting everything
        return True

    def visit_builtin_type(self, name, info, json_type):
        pass

    def visit_enum_type(self, name, info, values, prefix):
        pass

    def visit_array_type(self, name, info, element_type):
        pass

    def visit_object_type(self, name, info, base, members, variants):
        pass

    def visit_object_type_flat(self, name, info, members, variants):
        pass

    def visit_alternate_type(self, name, info, variants):
        pass

    def visit_command(self, name, info, arg_type, ret_type,
                      gen, success_response):
        pass

    def visit_event(self, name, info, arg_type):
        pass


class QAPISchemaType(QAPISchemaEntity):
    def c_type(self, is_param=False):
        return c_name(self.name) + pointer_suffix

    def c_null(self):
        return 'NULL'

    def json_type(self):
        pass

    def alternate_qtype(self):
        json2qtype = {
            'string':  'QTYPE_QSTRING',
            'number':  'QTYPE_QFLOAT',
            'int':     'QTYPE_QINT',
            'boolean': 'QTYPE_QBOOL',
            'object':  'QTYPE_QDICT'
        }
        return json2qtype.get(self.json_type())


class QAPISchemaBuiltinType(QAPISchemaType):
    def __init__(self, name, json_type, c_type, c_null):
        QAPISchemaType.__init__(self, name, None)
        assert not c_type or isinstance(c_type, str)
        assert json_type in ('string', 'number', 'int', 'boolean', 'null',
                             'value')
        self._json_type_name = json_type
        self._c_type_name = c_type
        self._c_null_val = c_null

    def c_name(self):
        return self.name

    def c_type(self, is_param=False):
        if is_param and self.name == 'str':
            return 'const ' + self._c_type_name
        return self._c_type_name

    def c_null(self):
        return self._c_null_val

    def json_type(self):
        return self._json_type_name

    def visit(self, visitor):
        visitor.visit_builtin_type(self.name, self.info, self.json_type())


class QAPISchemaEnumType(QAPISchemaType):
    def __init__(self, name, info, values, prefix):
        QAPISchemaType.__init__(self, name, info)
        for v in values:
            assert isinstance(v, str)
        assert prefix is None or isinstance(prefix, str)
        self.values = values
        self.prefix = prefix

    def check(self, schema):
        assert len(set(self.values)) == len(self.values)

    def is_implicit(self):
        # See QAPISchema._make_implicit_enum_type()
        return self.name.endswith('Kind')

    def c_type(self, is_param=False):
        return c_name(self.name)

    def c_null(self):
        return c_enum_const(self.name, (self.values + ['MAX'])[0],
                            self.prefix)

    def json_type(self):
        return 'string'

    def visit(self, visitor):
        visitor.visit_enum_type(self.name, self.info,
                                self.values, self.prefix)


class QAPISchemaArrayType(QAPISchemaType):
    def __init__(self, name, info, element_type):
        QAPISchemaType.__init__(self, name, info)
        assert isinstance(element_type, str)
        self._element_type_name = element_type
        self.element_type = None

    def check(self, schema):
        self.element_type = schema.lookup_type(self._element_type_name)
        assert self.element_type

    def is_implicit(self):
        return True

    def json_type(self):
        return 'array'

    def visit(self, visitor):
        visitor.visit_array_type(self.name, self.info, self.element_type)


class QAPISchemaObjectType(QAPISchemaType):
    def __init__(self, name, info, base, local_members, variants):
        QAPISchemaType.__init__(self, name, info)
        assert base is None or isinstance(base, str)
        for m in local_members:
            assert isinstance(m, QAPISchemaObjectTypeMember)
        assert (variants is None or
                isinstance(variants, QAPISchemaObjectTypeVariants))
        self._base_name = base
        self.base = None
        self.local_members = local_members
        self.variants = variants
        self.members = None

    def check(self, schema):
        assert self.members is not False        # not running in cycles
        if self.members:
            return
        self.members = False                    # mark as being checked
        if self._base_name:
            self.base = schema.lookup_type(self._base_name)
            assert isinstance(self.base, QAPISchemaObjectType)
            assert not self.base.variants       # not implemented
            self.base.check(schema)
            members = list(self.base.members)
        else:
            members = []
        seen = {}
        for m in members:
            assert c_name(m.name) not in seen
            seen[m.name] = m
        for m in self.local_members:
            m.check(schema, members, seen)
        if self.variants:
            self.variants.check(schema, members, seen)
        self.members = members

    def is_implicit(self):
        # See QAPISchema._make_implicit_object_type()
        return self.name[0] == ':'

    def c_name(self):
        assert not self.is_implicit()
        return QAPISchemaType.c_name(self)

    def c_type(self, is_param=False):
        assert not self.is_implicit()
        return QAPISchemaType.c_type(self)

    def json_type(self):
        return 'object'

    def visit(self, visitor):
        visitor.visit_object_type(self.name, self.info,
                                  self.base, self.local_members, self.variants)
        visitor.visit_object_type_flat(self.name, self.info,
                                       self.members, self.variants)


class QAPISchemaObjectTypeMember(object):
    def __init__(self, name, typ, optional):
        assert isinstance(name, str)
        assert isinstance(typ, str)
        assert isinstance(optional, bool)
        self.name = name
        self._type_name = typ
        self.type = None
        self.optional = optional

    def check(self, schema, all_members, seen):
        assert self.name not in seen
        self.type = schema.lookup_type(self._type_name)
        assert self.type
        all_members.append(self)
        seen[self.name] = self


class QAPISchemaObjectTypeVariants(object):
    def __init__(self, tag_name, tag_member, variants):
        # Flat unions pass tag_name but not tag_member.
        # Simple unions and alternates pass tag_member but not tag_name.
        # After check(), tag_member is always set, and tag_name remains
        # a reliable witness of being used by a flat union.
        assert bool(tag_member) != bool(tag_name)
        assert (isinstance(tag_name, str) or
                isinstance(tag_member, QAPISchemaObjectTypeMember))
        for v in variants:
            assert isinstance(v, QAPISchemaObjectTypeVariant)
        self.tag_name = tag_name
        self.tag_member = tag_member
        self.variants = variants

    def check(self, schema, members, seen):
        if self.tag_name:
            self.tag_member = seen[self.tag_name]
        else:
            self.tag_member.check(schema, members, seen)
        assert isinstance(self.tag_member.type, QAPISchemaEnumType)
        for v in self.variants:
            vseen = dict(seen)
            v.check(schema, self.tag_member.type, vseen)


class QAPISchemaObjectTypeVariant(QAPISchemaObjectTypeMember):
    def __init__(self, name, typ):
        QAPISchemaObjectTypeMember.__init__(self, name, typ, False)

    def check(self, schema, tag_type, seen):
        QAPISchemaObjectTypeMember.check(self, schema, [], seen)
        assert self.name in tag_type.values

    # This function exists to support ugly simple union special cases
    # TODO get rid of them, and drop the function
    def simple_union_type(self):
        if (self.type.is_implicit() and
                isinstance(self.type, QAPISchemaObjectType)):
            assert len(self.type.members) == 1
            assert not self.type.variants
            return self.type.members[0].type
        return None


class QAPISchemaAlternateType(QAPISchemaType):
    def __init__(self, name, info, variants):
        QAPISchemaType.__init__(self, name, info)
        assert isinstance(variants, QAPISchemaObjectTypeVariants)
        assert not variants.tag_name
        self.variants = variants

    def check(self, schema):
        self.variants.check(schema, [], {})

    def json_type(self):
        return 'value'

    def visit(self, visitor):
        visitor.visit_alternate_type(self.name, self.info, self.variants)


class QAPISchemaCommand(QAPISchemaEntity):
    def __init__(self, name, info, arg_type, ret_type, gen, success_response):
        QAPISchemaEntity.__init__(self, name, info)
        assert not arg_type or isinstance(arg_type, str)
        assert not ret_type or isinstance(ret_type, str)
        self._arg_type_name = arg_type
        self.arg_type = None
        self._ret_type_name = ret_type
        self.ret_type = None
        self.gen = gen
        self.success_response = success_response

    def check(self, schema):
        if self._arg_type_name:
            self.arg_type = schema.lookup_type(self._arg_type_name)
            assert isinstance(self.arg_type, QAPISchemaObjectType)
            assert not self.arg_type.variants   # not implemented
        if self._ret_type_name:
            self.ret_type = schema.lookup_type(self._ret_type_name)
            assert isinstance(self.ret_type, QAPISchemaType)

    def visit(self, visitor):
        visitor.visit_command(self.name, self.info,
                              self.arg_type, self.ret_type,
                              self.gen, self.success_response)


class QAPISchemaEvent(QAPISchemaEntity):
    def __init__(self, name, info, arg_type):
        QAPISchemaEntity.__init__(self, name, info)
        assert not arg_type or isinstance(arg_type, str)
        self._arg_type_name = arg_type
        self.arg_type = None

    def check(self, schema):
        if self._arg_type_name:
            self.arg_type = schema.lookup_type(self._arg_type_name)
            assert isinstance(self.arg_type, QAPISchemaObjectType)
            assert not self.arg_type.variants   # not implemented

    def visit(self, visitor):
        visitor.visit_event(self.name, self.info, self.arg_type)


class QAPISchema(object):
    def __init__(self, fname):
        try:
            self.exprs = check_exprs(QAPISchemaParser(open(fname, "r")).exprs)
            self._entity_dict = {}
            self._predefining = True
            self._def_predefineds()
            self._predefining = False
            self._def_exprs()
            self.check()
        except (QAPISchemaError, QAPIExprError), err:
            print >>sys.stderr, err
            exit(1)

    def _def_entity(self, ent):
        # Only the predefined types are allowed to not have info
        assert ent.info or self._predefining
        assert ent.name not in self._entity_dict
        self._entity_dict[ent.name] = ent

    def lookup_entity(self, name, typ=None):
        ent = self._entity_dict.get(name)
        if typ and not isinstance(ent, typ):
            return None
        return ent

    def lookup_type(self, name):
        return self.lookup_entity(name, QAPISchemaType)

    def _def_builtin_type(self, name, json_type, c_type, c_null):
        self._def_entity(QAPISchemaBuiltinType(name, json_type,
                                               c_type, c_null))
        # TODO As long as we have QAPI_TYPES_BUILTIN to share multiple
        # qapi-types.h from a single .c, all arrays of builtins must be
        # declared in the first file whether or not they are used.  Nicer
        # would be to use lazy instantiation, while figuring out how to
        # avoid compilation issues with multiple qapi-types.h.
        self._make_array_type(name, None)

    def _def_predefineds(self):
        for t in [('str',    'string',  'char' + pointer_suffix, 'NULL'),
                  ('number', 'number',  'double',   '0'),
                  ('int',    'int',     'int64_t',  '0'),
                  ('int8',   'int',     'int8_t',   '0'),
                  ('int16',  'int',     'int16_t',  '0'),
                  ('int32',  'int',     'int32_t',  '0'),
                  ('int64',  'int',     'int64_t',  '0'),
                  ('uint8',  'int',     'uint8_t',  '0'),
                  ('uint16', 'int',     'uint16_t', '0'),
                  ('uint32', 'int',     'uint32_t', '0'),
                  ('uint64', 'int',     'uint64_t', '0'),
                  ('size',   'int',     'uint64_t', '0'),
                  ('bool',   'boolean', 'bool',     'false'),
                  ('any',    'value',   'QObject' + pointer_suffix, 'NULL')]:
            self._def_builtin_type(*t)
        self.the_empty_object_type = QAPISchemaObjectType(':empty', None, None,
                                                          [], None)
        self._def_entity(self.the_empty_object_type)

    def _make_implicit_enum_type(self, name, info, values):
        name = name + 'Kind'   # Use namespace reserved by add_name()
        self._def_entity(QAPISchemaEnumType(name, info, values, None))
        return name

    def _make_array_type(self, element_type, info):
        name = element_type + 'List'   # Use namespace reserved by add_name()
        if not self.lookup_type(name):
            self._def_entity(QAPISchemaArrayType(name, info, element_type))
        return name

    def _make_implicit_object_type(self, name, info, role, members):
        if not members:
            return None
        name = ':obj-%s-%s' % (name, role)
        if not self.lookup_entity(name, QAPISchemaObjectType):
            self._def_entity(QAPISchemaObjectType(name, info, None,
                                                  members, None))
        return name

    def _def_enum_type(self, expr, info):
        name = expr['enum']
        data = expr['data']
        prefix = expr.get('prefix')
        self._def_entity(QAPISchemaEnumType(name, info, data, prefix))

    def _make_member(self, name, typ, info):
        optional = False
        if name.startswith('*'):
            name = name[1:]
            optional = True
        if isinstance(typ, list):
            assert len(typ) == 1
            typ = self._make_array_type(typ[0], info)
        return QAPISchemaObjectTypeMember(name, typ, optional)

    def _make_members(self, data, info):
        return [self._make_member(key, value, info)
                for (key, value) in data.iteritems()]

    def _def_struct_type(self, expr, info):
        name = expr['struct']
        base = expr.get('base')
        data = expr['data']
        self._def_entity(QAPISchemaObjectType(name, info, base,
                                              self._make_members(data, info),
                                              None))

    def _make_variant(self, case, typ):
        return QAPISchemaObjectTypeVariant(case, typ)

    def _make_simple_variant(self, case, typ, info):
        if isinstance(typ, list):
            assert len(typ) == 1
            typ = self._make_array_type(typ[0], info)
        typ = self._make_implicit_object_type(
            typ, info, 'wrapper', [self._make_member('data', typ, info)])
        return QAPISchemaObjectTypeVariant(case, typ)

    def _make_implicit_tag(self, type_name, info, variants):
        typ = self._make_implicit_enum_type(type_name, info,
                                            [v.name for v in variants])
        return QAPISchemaObjectTypeMember('type', typ, False)

    def _def_union_type(self, expr, info):
        name = expr['union']
        data = expr['data']
        base = expr.get('base')
        tag_name = expr.get('discriminator')
        tag_member = None
        if tag_name:
            variants = [self._make_variant(key, value)
                        for (key, value) in data.iteritems()]
        else:
            variants = [self._make_simple_variant(key, value, info)
                        for (key, value) in data.iteritems()]
            tag_member = self._make_implicit_tag(name, info, variants)
        self._def_entity(
            QAPISchemaObjectType(name, info, base,
                                 self._make_members(OrderedDict(), info),
                                 QAPISchemaObjectTypeVariants(tag_name,
                                                              tag_member,
                                                              variants)))

    def _def_alternate_type(self, expr, info):
        name = expr['alternate']
        data = expr['data']
        variants = [self._make_variant(key, value)
                    for (key, value) in data.iteritems()]
        tag_member = self._make_implicit_tag(name, info, variants)
        self._def_entity(
            QAPISchemaAlternateType(name, info,
                                    QAPISchemaObjectTypeVariants(None,
                                                                 tag_member,
                                                                 variants)))

    def _def_command(self, expr, info):
        name = expr['command']
        data = expr.get('data')
        rets = expr.get('returns')
        gen = expr.get('gen', True)
        success_response = expr.get('success-response', True)
        if isinstance(data, OrderedDict):
            data = self._make_implicit_object_type(
                name, info, 'arg', self._make_members(data, info))
        if isinstance(rets, list):
            assert len(rets) == 1
            rets = self._make_array_type(rets[0], info)
        self._def_entity(QAPISchemaCommand(name, info, data, rets, gen,
                                           success_response))

    def _def_event(self, expr, info):
        name = expr['event']
        data = expr.get('data')
        if isinstance(data, OrderedDict):
            data = self._make_implicit_object_type(
                name, info, 'arg', self._make_members(data, info))
        self._def_entity(QAPISchemaEvent(name, info, data))

    def _def_exprs(self):
        for expr_elem in self.exprs:
            expr = expr_elem['expr']
            info = expr_elem['info']
            if 'enum' in expr:
                self._def_enum_type(expr, info)
            elif 'struct' in expr:
                self._def_struct_type(expr, info)
            elif 'union' in expr:
                self._def_union_type(expr, info)
            elif 'alternate' in expr:
                self._def_alternate_type(expr, info)
            elif 'command' in expr:
                self._def_command(expr, info)
            elif 'event' in expr:
                self._def_event(expr, info)
            else:
                assert False

    def check(self):
        for ent in self._entity_dict.values():
            ent.check(self)

    def visit(self, visitor):
        visitor.visit_begin(self)
        for (name, entity) in sorted(self._entity_dict.items()):
            if visitor.visit_needed(entity):
                entity.visit(visitor)
        visitor.visit_end()


#
# Code generation helpers
#

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


# ENUMName -> ENUM_NAME, EnumName1 -> ENUM_NAME1
# ENUM_NAME -> ENUM_NAME, ENUM_NAME1 -> ENUM_NAME1, ENUM_Name2 -> ENUM_NAME2
# ENUM24_Name -> ENUM24_NAME
def camel_to_upper(value):
    c_fun_str = c_name(value, False)
    if value.isupper():
        return c_fun_str

    new_name = ''
    l = len(c_fun_str)
    for i in range(l):
        c = c_fun_str[i]
        # When c is upper and no "_" appears before, do more checks
        if c.isupper() and (i > 0) and c_fun_str[i - 1] != "_":
            if i < l - 1 and c_fun_str[i + 1].islower():
                new_name += '_'
            elif c_fun_str[i - 1].isdigit():
                new_name += '_'
        new_name += c
    return new_name.lstrip('_').upper()


def c_enum_const(type_name, const_name, prefix=None):
    if prefix is not None:
        type_name = prefix
    return camel_to_upper(type_name + '_' + const_name)

c_name_trans = string.maketrans('.-', '__')


# Map @name to a valid C identifier.
# If @protect, avoid returning certain ticklish identifiers (like
# C keywords) by prepending "q_".
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
    polluted_words = set(['unix', 'errno'])
    if protect and (name in c89_words | c99_words | c11_words | gcc_words
                    | cpp_words | polluted_words):
        return "q_" + name
    return name.translate(c_name_trans)

eatspace = '\033EATSPACE.'
pointer_suffix = ' *' + eatspace


def genindent(count):
    ret = ""
    for _ in range(count):
        ret += " "
    return ret

indent_level = 0


def push_indent(indent_amount=4):
    global indent_level
    indent_level += indent_amount


def pop_indent(indent_amount=4):
    global indent_level
    indent_level -= indent_amount


# Generate @code with @kwds interpolated.
# Obey indent_level, and strip eatspace.
def cgen(code, **kwds):
    raw = code % kwds
    if indent_level:
        indent = genindent(indent_level)
        # re.subn() lacks flags support before Python 2.7, use re.compile()
        raw = re.subn(re.compile("^.", re.MULTILINE),
                      indent + r'\g<0>', raw)
        raw = raw[0]
    return re.sub(re.escape(eatspace) + ' *', '', raw)


def mcgen(code, **kwds):
    if code[0] == '\n':
        code = code[1:]
    return cgen(code, **kwds)


def guardname(filename):
    return c_name(filename, protect=False).upper()


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


def gen_enum_lookup(name, values, prefix=None):
    ret = mcgen('''

const char *const %(c_name)s_lookup[] = {
''',
                c_name=c_name(name))
    for value in values:
        index = c_enum_const(name, value, prefix)
        ret += mcgen('''
    [%(index)s] = "%(value)s",
''',
                     index=index, value=value)

    max_index = c_enum_const(name, 'MAX', prefix)
    ret += mcgen('''
    [%(max_index)s] = NULL,
};
''',
                 max_index=max_index)
    return ret


def gen_enum(name, values, prefix=None):
    # append automatically generated _MAX value
    enum_values = values + ['MAX']

    ret = mcgen('''

typedef enum %(c_name)s {
''',
                c_name=c_name(name))

    i = 0
    for value in enum_values:
        ret += mcgen('''
    %(c_enum)s = %(i)d,
''',
                     c_enum=c_enum_const(name, value, prefix),
                     i=i)
        i += 1

    ret += mcgen('''
} %(c_name)s;
''',
                 c_name=c_name(name))

    ret += mcgen('''

extern const char *const %(c_name)s_lookup[];
''',
                 c_name=c_name(name))
    return ret


def gen_params(arg_type, extra):
    if not arg_type:
        return extra
    assert not arg_type.variants
    ret = ''
    sep = ''
    for memb in arg_type.members:
        ret += sep
        sep = ', '
        if memb.optional:
            ret += 'bool has_%s, ' % c_name(memb.name)
        ret += '%s %s' % (memb.type.c_type(is_param=True), c_name(memb.name))
    if extra:
        ret += sep + extra
    return ret


def gen_err_check(label='out', skiperr=False):
    if skiperr:
        return ''
    return mcgen('''
    if (err) {
        goto %(label)s;
    }
''',
                 label=label)


def gen_visit_fields(members, prefix='', need_cast=False, skiperr=False):
    ret = ''
    if skiperr:
        errparg = 'NULL'
    else:
        errparg = '&err'

    for memb in members:
        if memb.optional:
            ret += mcgen('''
    visit_optional(v, &%(prefix)shas_%(c_name)s, "%(name)s", %(errp)s);
''',
                         prefix=prefix, c_name=c_name(memb.name),
                         name=memb.name, errp=errparg)
            ret += gen_err_check(skiperr=skiperr)
            ret += mcgen('''
    if (%(prefix)shas_%(c_name)s) {
''',
                         prefix=prefix, c_name=c_name(memb.name))
            push_indent()

        # Ugly: sometimes we need to cast away const
        if need_cast and memb.type.name == 'str':
            cast = '(char **)'
        else:
            cast = ''

        ret += mcgen('''
    visit_type_%(c_type)s(v, %(cast)s&%(prefix)s%(c_name)s, "%(name)s", %(errp)s);
''',
                     c_type=memb.type.c_name(), prefix=prefix, cast=cast,
                     c_name=c_name(memb.name), name=memb.name,
                     errp=errparg)
        ret += gen_err_check(skiperr=skiperr)

        if memb.optional:
            pop_indent()
            ret += mcgen('''
    }
''')
    return ret


#
# Common command line parsing
#


def parse_command_line(extra_options="", extra_long_options=[]):

    try:
        opts, args = getopt.gnu_getopt(sys.argv[1:],
                                       "chp:o:" + extra_options,
                                       ["source", "header", "prefix=",
                                        "output-dir="] + extra_long_options)
    except getopt.GetoptError, err:
        print >>sys.stderr, "%s: %s" % (sys.argv[0], str(err))
        sys.exit(1)

    output_dir = ""
    prefix = ""
    do_c = False
    do_h = False
    extra_opts = []

    for oa in opts:
        o, a = oa
        if o in ("-p", "--prefix"):
            match = re.match('([A-Za-z_.-][A-Za-z0-9_.-]*)?', a)
            if match.end() != len(a):
                print >>sys.stderr, \
                    "%s: 'funny character '%s' in argument of --prefix" \
                    % (sys.argv[0], a[match.end()])
                sys.exit(1)
            prefix = a
        elif o in ("-o", "--output-dir"):
            output_dir = a + "/"
        elif o in ("-c", "--source"):
            do_c = True
        elif o in ("-h", "--header"):
            do_h = True
        else:
            extra_opts.append(oa)

    if not do_c and not do_h:
        do_c = True
        do_h = True

    if len(args) != 1:
        print >>sys.stderr, "%s: need exactly one argument" % sys.argv[0]
        sys.exit(1)
    fname = args[0]

    return (fname, output_dir, do_c, do_h, prefix, extra_opts)

#
# Generate output files with boilerplate
#


def open_output(output_dir, do_c, do_h, prefix, c_file, h_file,
                c_comment, h_comment):
    guard = guardname(prefix + h_file)
    c_file = output_dir + prefix + c_file
    h_file = output_dir + prefix + h_file

    if output_dir:
        try:
            os.makedirs(output_dir)
        except os.error, e:
            if e.errno != errno.EEXIST:
                raise

    def maybe_open(really, name, opt):
        if really:
            return open(name, opt)
        else:
            import StringIO
            return StringIO.StringIO()

    fdef = maybe_open(do_c, c_file, 'w')
    fdecl = maybe_open(do_h, h_file, 'w')

    fdef.write(mcgen('''
/* AUTOMATICALLY GENERATED, DO NOT MODIFY */
%(comment)s
''',
                     comment=c_comment))

    fdecl.write(mcgen('''
/* AUTOMATICALLY GENERATED, DO NOT MODIFY */
%(comment)s
#ifndef %(guard)s
#define %(guard)s

''',
                      comment=h_comment, guard=guard))

    return (fdef, fdecl)


def close_output(fdef, fdecl):
    fdecl.write('''
#endif
''')
    fdecl.close()
    fdef.close()
