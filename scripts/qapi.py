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
}

# Whitelist of commands allowed to return a non-dictionary
returns_whitelist = [
    # From QMP:
    'human-monitor-command',
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

    # From qapi-schema-test:
    'user_def_cmd3',
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
        self.info = expr_info
        self.msg = msg

    def __str__(self):
        return error_path(self.info['parent']) + \
            "%s:%d: %s" % (self.info['file'], self.info['line'], self.msg)

class QAPISchema:

    def __init__(self, fp, previously_included = [], incl_info = None):
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

        while self.tok != None:
            expr_info = {'file': fname, 'line': self.line,
                         'parent': self.incl_info}
            expr = self.get_expr(False)
            if isinstance(expr, dict) and "include" in expr:
                if len(expr) != 1:
                    raise QAPIExprError(expr_info, "Invalid 'include' directive")
                include = expr["include"]
                if not isinstance(include, str):
                    raise QAPIExprError(expr_info,
                                        'Expected a file name (string), got: %s'
                                        % include)
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
                exprs_include = QAPISchema(fobj, previously_included,
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
                            for x in range(0, 4):
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
                                                  "Unknown escape \\%s" %ch)
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
        if not self.tok in "{['tfn":
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
#

def find_base_fields(base):
    base_struct_define = find_struct(base)
    if not base_struct_define:
        return None
    return base_struct_define['data']

# Return the qtype of an alternate branch, or None on error.
def find_alternate_member_qtype(qapi_type):
    if builtin_types.has_key(qapi_type):
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

valid_name = re.compile('^[a-zA-Z_][a-zA-Z0-9_.-]*$')
def check_name(expr_info, source, name, allow_optional = False,
               enum_member = False):
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
    if not valid_name.match(membername):
        raise QAPIExprError(expr_info,
                            "%s uses invalid name '%s'" % (source, name))

def add_name(name, info, meta, implicit = False):
    global all_names
    check_name(info, "'%s'" % meta, name)
    if name in all_names:
        raise QAPIExprError(info,
                            "%s '%s' is already defined"
                            % (all_names[name], name))
    if not implicit and name[-4:] == 'Kind':
        raise QAPIExprError(info,
                            "%s '%s' should not end in 'Kind'"
                            % (meta, name))
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

def add_enum(name, info, enum_values = None, implicit = False):
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
    return find_enum(name) != None

def check_type(expr_info, source, value, allow_array = False,
               allow_dict = False, allow_optional = False,
               allow_star = False, allow_metas = []):
    global all_names
    orig_value = value

    if value is None:
        return

    if allow_star and value == '**':
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
        orig_value = "array of %s" %value

    # Check if type name for value is okay
    if isinstance(value, str):
        if value == '**':
            raise QAPIExprError(expr_info,
                                "%s uses '**' but did not request 'gen':false"
                                % source)
        if not value in all_names:
            raise QAPIExprError(expr_info,
                                "%s uses unknown type '%s'"
                                % (source, orig_value))
        if not all_names[value] in allow_metas:
            raise QAPIExprError(expr_info,
                                "%s cannot use %s type '%s'"
                                % (source, all_names[value], orig_value))
        return

    # value is a dictionary, check that each member is okay
    if not isinstance(value, OrderedDict):
        raise QAPIExprError(expr_info,
                            "%s should be a dictionary" % source)
    if not allow_dict:
        raise QAPIExprError(expr_info,
                            "%s should be a type name" % source)
    for (key, arg) in value.items():
        check_name(expr_info, "Member of %s" % source, key,
                   allow_optional=allow_optional)
        # Todo: allow dictionaries to represent default values of
        # an optional argument.
        check_type(expr_info, "Member '%s' of %s" % (key, source), arg,
                   allow_array=True, allow_star=allow_star,
                   allow_metas=['built-in', 'union', 'alternate', 'struct',
                                'enum'])

def check_member_clash(expr_info, base_name, data, source = ""):
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
    allow_star = expr.has_key('gen')

    check_type(expr_info, "'data' for command '%s'" % name,
               expr.get('data'), allow_dict=True, allow_optional=True,
               allow_metas=['union', 'struct'], allow_star=allow_star)
    returns_meta = ['union', 'struct']
    if name in returns_whitelist:
        returns_meta += ['built-in', 'alternate', 'enum']
    check_type(expr_info, "'returns' for command '%s'" % name,
               expr.get('returns'), allow_array=True, allow_dict=True,
               allow_optional=True, allow_metas=returns_meta,
               allow_star=allow_star)

def check_event(expr, expr_info):
    global events
    name = expr['event']
    params = expr.get('data')

    if name.upper() == 'MAX':
        raise QAPIExprError(expr_info, "Event name 'MAX' cannot be created")
    events.append(name)
    check_type(expr_info, "'data' for event '%s'" % name,
               expr.get('data'), allow_dict=True, allow_optional=True,
               allow_metas=['union', 'struct'])

def check_union(expr, expr_info):
    name = expr['union']
    base = expr.get('base')
    discriminator = expr.get('discriminator')
    members = expr['data']
    values = { 'MAX': '(automatic)' }

    # If the object has a member 'base', its value must name a struct,
    # and there must be a discriminator.
    if base is not None:
        if discriminator is None:
            raise QAPIExprError(expr_info,
                                "Union '%s' requires a discriminator to go "
                                "along with base" %name)

    # Two types of unions, determined by discriminator.

    # With no discriminator it is a simple union.
    if discriminator is None:
        enum_define = None
        allow_metas=['built-in', 'union', 'alternate', 'struct', 'enum']
        if base is not None:
            raise QAPIExprError(expr_info,
                                "Simple union '%s' must not have a base"
                                % name)

    # Else, it's a flat union.
    else:
        # The object must have a string member 'base'.
        if not isinstance(base, str):
            raise QAPIExprError(expr_info,
                                "Flat union '%s' must have a string base field"
                                % name)
        base_fields = find_base_fields(base)
        if not base_fields:
            raise QAPIExprError(expr_info,
                                "Base '%s' is not a valid struct"
                                % base)

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
        allow_metas=['struct']
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
        # of 'data' must also be members of the enum type.
        if enum_define:
            if not key in enum_define['enum_values']:
                raise QAPIExprError(expr_info,
                                    "Discriminator value '%s' is not found in "
                                    "enum '%s'" %
                                    (key, enum_define["enum_name"]))

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
    values = { 'MAX': '(automatic)' }
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
    values = { 'MAX': '(automatic)' }

    if not isinstance(members, list):
        raise QAPIExprError(expr_info,
                            "Enum '%s' requires an array for 'data'" % name)
    for member in members:
        check_name(expr_info, "Member of enum '%s'" %name, member,
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
    required = required + [ meta ]
    for (key, value) in expr.items():
        if not key in required and not key in optional:
            raise QAPIExprError(info,
                                "Unknown key '%s' in %s '%s'"
                                % (key, meta, name))
        if (key == 'gen' or key == 'success-response') and value != False:
            raise QAPIExprError(info,
                                "'%s' of %s '%s' should only use false value"
                                % (key, meta, name))
    for key in required:
        if not expr.has_key(key):
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
        if expr.has_key('enum'):
            check_keys(expr_elem, 'enum', ['data'])
            add_enum(expr['enum'], info, expr['data'])
        elif expr.has_key('union'):
            check_keys(expr_elem, 'union', ['data'],
                       ['base', 'discriminator'])
            add_union(expr, info)
        elif expr.has_key('alternate'):
            check_keys(expr_elem, 'alternate', ['data'])
            add_name(expr['alternate'], info, 'alternate')
        elif expr.has_key('struct'):
            check_keys(expr_elem, 'struct', ['data'], ['base'])
            add_struct(expr, info)
        elif expr.has_key('command'):
            check_keys(expr_elem, 'command', [],
                       ['data', 'returns', 'gen', 'success-response'])
            add_name(expr['command'], info, 'command')
        elif expr.has_key('event'):
            check_keys(expr_elem, 'event', [], ['data'])
            add_name(expr['event'], info, 'event')
        else:
            raise QAPIExprError(expr_elem['info'],
                                "Expression is missing metatype")

    # Try again for hidden UnionKind enum
    for expr_elem in exprs:
        expr = expr_elem['expr']
        if expr.has_key('union'):
            if not discriminator_find_enum_define(expr):
                add_enum('%sKind' % expr['union'], expr_elem['info'],
                         implicit=True)
        elif expr.has_key('alternate'):
            add_enum('%sKind' % expr['alternate'], expr_elem['info'],
                     implicit=True)

    # Validate that exprs make sense
    for expr_elem in exprs:
        expr = expr_elem['expr']
        info = expr_elem['info']

        if expr.has_key('enum'):
            check_enum(expr, info)
        elif expr.has_key('union'):
            check_union(expr, info)
        elif expr.has_key('alternate'):
            check_alternate(expr, info)
        elif expr.has_key('struct'):
            check_struct(expr, info)
        elif expr.has_key('command'):
            check_command(expr, info)
        elif expr.has_key('event'):
            check_event(expr, info)
        else:
            assert False, 'unexpected meta type'

    return map(lambda expr_elem: expr_elem['expr'], exprs)

def parse_schema(fname):
    try:
        schema = QAPISchema(open(fname, "r"))
        return check_exprs(schema.exprs)
    except (QAPISchemaError, QAPIExprError), e:
        print >>sys.stderr, e
        exit(1)

#
# Code generation helpers
#

def parse_args(typeinfo):
    if isinstance(typeinfo, str):
        struct = find_struct(typeinfo)
        assert struct != None
        typeinfo = struct['data']

    for member in typeinfo:
        argname = member
        argentry = typeinfo[member]
        optional = False
        if member.startswith('*'):
            argname = member[1:]
            optional = True
        # Todo: allow argentry to be OrderedDict, for providing the
        # value of an optional argument.
        yield (argname, argentry, optional)

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
            # Case 1: next string is lower
            # Case 2: previous string is digit
            if (i < (l - 1) and c_fun_str[i + 1].islower()) or \
            c_fun_str[i - 1].isdigit():
                new_name += '_'
        new_name += c
    return new_name.lstrip('_').upper()

def c_enum_const(type_name, const_name):
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
    if protect and (name in c89_words | c99_words | c11_words | gcc_words | cpp_words | polluted_words):
        return "q_" + name
    return name.translate(c_name_trans)

# Map type @name to the C typedef name for the list form.
#
# ['Name'] -> 'NameList', ['x-Foo'] -> 'x_FooList', ['int'] -> 'intList'
def c_list_type(name):
    return type_name(name) + 'List'

# Map type @value to the C typedef form.
#
# Used for converting 'type' from a 'member':'type' qapi definition
# into the alphanumeric portion of the type for a generated C parameter,
# as well as generated C function names.  See c_type() for the rest of
# the conversion such as adding '*' on pointer types.
# 'int' -> 'int', '[x-Foo]' -> 'x_FooList', '__a.b_c' -> '__a_b_c'
def type_name(value):
    if type(value) == list:
        return c_list_type(value[0])
    if value in builtin_types.keys():
        return value
    return c_name(value)

eatspace = '\033EATSPACE.'
pointer_suffix = ' *' + eatspace

# Map type @name to its C type expression.
# If @is_param, const-qualify the string type.
#
# This function is used for computing the full C type of 'member':'name'.
# A special suffix is added in c_type() for pointer types, and it's
# stripped in mcgen(). So please notice this when you check the return
# value of c_type() outside mcgen().
def c_type(value, is_param=False):
    if value == 'str':
        if is_param:
            return 'const char' + pointer_suffix
        return 'char' + pointer_suffix

    elif value == 'int':
        return 'int64_t'
    elif (value == 'int8' or value == 'int16' or value == 'int32' or
          value == 'int64' or value == 'uint8' or value == 'uint16' or
          value == 'uint32' or value == 'uint64'):
        return value + '_t'
    elif value == 'size':
        return 'uint64_t'
    elif value == 'bool':
        return 'bool'
    elif value == 'number':
        return 'double'
    elif type(value) == list:
        return c_list_type(value[0]) + pointer_suffix
    elif is_enum(value):
        return c_name(value)
    elif value == None:
        return 'void'
    elif value in events:
        return camel_case(value) + 'Event' + pointer_suffix
    else:
        # complex type name
        assert isinstance(value, str) and value != ""
        return c_name(value) + pointer_suffix

def is_c_ptr(value):
    return c_type(value).endswith(pointer_suffix)

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
    raw = cgen('\n'.join(code.split('\n')[1:-1]), **kwds)
    return re.sub(re.escape(eatspace) + ' *', '', raw)

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

#
# Common command line parsing
#

def parse_command_line(extra_options = "", extra_long_options = []):

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
    c_file = output_dir + prefix + c_file
    h_file = output_dir + prefix + h_file

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
                     comment = c_comment))

    fdecl.write(mcgen('''
/* AUTOMATICALLY GENERATED, DO NOT MODIFY */
%(comment)s
#ifndef %(guard)s
#define %(guard)s

''',
                      comment = h_comment, guard = guardname(h_file)))

    return (fdef, fdecl)

def close_output(fdef, fdecl):
    fdecl.write('''
#endif
''')
    fdecl.close()
    fdef.close()
