#
# QAPI helper library
#
# Copyright IBM, Corp. 2011
# Copyright (c) 2013-2016 Red Hat Inc.
#
# Authors:
#  Anthony Liguori <aliguori@us.ibm.com>
#  Markus Armbruster <armbru@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

import errno
import getopt
import os
import re
import string
import sys
from ordereddict import OrderedDict

builtin_types = {
    'null':     'QTYPE_QNULL',
    'str':      'QTYPE_QSTRING',
    'int':      'QTYPE_QNUM',
    'number':   'QTYPE_QNUM',
    'bool':     'QTYPE_QBOOL',
    'int8':     'QTYPE_QNUM',
    'int16':    'QTYPE_QNUM',
    'int32':    'QTYPE_QNUM',
    'int64':    'QTYPE_QNUM',
    'uint8':    'QTYPE_QNUM',
    'uint16':   'QTYPE_QNUM',
    'uint32':   'QTYPE_QNUM',
    'uint64':   'QTYPE_QNUM',
    'size':     'QTYPE_QNUM',
    'any':      None,           # any QType possible, actually
    'QType':    'QTYPE_QSTRING',
}

# Are documentation comments required?
doc_required = False

# Whitelist of commands allowed to return a non-dictionary
returns_whitelist = []

# Whitelist of entities allowed to violate case conventions
name_case_whitelist = []

enum_types = {}
struct_types = {}
union_types = {}
all_names = {}

#
# Parsing the schema into expressions
#


def error_path(parent):
    res = ''
    while parent:
        res = ('In file included from %s:%d:\n' % (parent['file'],
                                                   parent['line'])) + res
        parent = parent['parent']
    return res


class QAPIError(Exception):
    def __init__(self, fname, line, col, incl_info, msg):
        Exception.__init__(self)
        self.fname = fname
        self.line = line
        self.col = col
        self.info = incl_info
        self.msg = msg

    def __str__(self):
        loc = '%s:%d' % (self.fname, self.line)
        if self.col is not None:
            loc += ':%s' % self.col
        return error_path(self.info) + '%s: %s' % (loc, self.msg)


class QAPIParseError(QAPIError):
    def __init__(self, parser, msg):
        col = 1
        for ch in parser.src[parser.line_pos:parser.pos]:
            if ch == '\t':
                col = (col + 7) % 8 + 1
            else:
                col += 1
        QAPIError.__init__(self, parser.fname, parser.line, col,
                           parser.incl_info, msg)


class QAPISemError(QAPIError):
    def __init__(self, info, msg):
        QAPIError.__init__(self, info['file'], info['line'], None,
                           info['parent'], msg)


class QAPIDoc(object):
    class Section(object):
        def __init__(self, name=None):
            # optional section name (argument/member or section name)
            self.name = name
            # the list of lines for this section
            self.content = []

        def append(self, line):
            self.content.append(line)

        def __repr__(self):
            return '\n'.join(self.content).strip()

    class ArgSection(Section):
        def __init__(self, name):
            QAPIDoc.Section.__init__(self, name)
            self.member = None

        def connect(self, member):
            self.member = member

    def __init__(self, parser, info):
        # self.parser is used to report errors with QAPIParseError.  The
        # resulting error position depends on the state of the parser.
        # It happens to be the beginning of the comment.  More or less
        # servicable, but action at a distance.
        self.parser = parser
        self.info = info
        self.symbol = None
        self.body = QAPIDoc.Section()
        # dict mapping parameter name to ArgSection
        self.args = OrderedDict()
        # a list of Section
        self.sections = []
        # the current section
        self.section = self.body

    def has_section(self, name):
        """Return True if we have a section with this name."""
        for i in self.sections:
            if i.name == name:
                return True
        return False

    def append(self, line):
        """Parse a comment line and add it to the documentation."""
        line = line[1:]
        if not line:
            self._append_freeform(line)
            return

        if line[0] != ' ':
            raise QAPIParseError(self.parser, "Missing space after #")
        line = line[1:]

        # FIXME not nice: things like '#  @foo:' and '# @foo: ' aren't
        # recognized, and get silently treated as ordinary text
        if self.symbol:
            self._append_symbol_line(line)
        elif not self.body.content and line.startswith('@'):
            if not line.endswith(':'):
                raise QAPIParseError(self.parser, "Line should end with :")
            self.symbol = line[1:-1]
            # FIXME invalid names other than the empty string aren't flagged
            if not self.symbol:
                raise QAPIParseError(self.parser, "Invalid name")
        else:
            self._append_freeform(line)

    def end_comment(self):
        self._end_section()

    def _append_symbol_line(self, line):
        name = line.split(' ', 1)[0]

        if name.startswith('@') and name.endswith(':'):
            line = line[len(name)+1:]
            self._start_args_section(name[1:-1])
        elif name in ('Returns:', 'Since:',
                      # those are often singular or plural
                      'Note:', 'Notes:',
                      'Example:', 'Examples:',
                      'TODO:'):
            line = line[len(name)+1:]
            self._start_section(name[:-1])

        self._append_freeform(line)

    def _start_args_section(self, name):
        # FIXME invalid names other than the empty string aren't flagged
        if not name:
            raise QAPIParseError(self.parser, "Invalid parameter name")
        if name in self.args:
            raise QAPIParseError(self.parser,
                                 "'%s' parameter name duplicated" % name)
        if self.sections:
            raise QAPIParseError(self.parser,
                                 "'@%s:' can't follow '%s' section"
                                 % (name, self.sections[0].name))
        self._end_section()
        self.section = QAPIDoc.ArgSection(name)
        self.args[name] = self.section

    def _start_section(self, name=''):
        if name in ('Returns', 'Since') and self.has_section(name):
            raise QAPIParseError(self.parser,
                                 "Duplicated '%s' section" % name)
        self._end_section()
        self.section = QAPIDoc.Section(name)
        self.sections.append(self.section)

    def _end_section(self):
        if self.section:
            contents = str(self.section)
            if self.section.name and (not contents or contents.isspace()):
                raise QAPIParseError(self.parser, "Empty doc section '%s'"
                                     % self.section.name)
            self.section = None

    def _append_freeform(self, line):
        in_arg = isinstance(self.section, QAPIDoc.ArgSection)
        if (in_arg and self.section.content
                and not self.section.content[-1]
                and line and not line[0].isspace()):
            self._start_section()
        if (in_arg or not self.section.name
                or not self.section.name.startswith('Example')):
            line = line.strip()
        match = re.match(r'(@\S+:)', line)
        if match:
            raise QAPIParseError(self.parser,
                                 "'%s' not allowed in free-form documentation"
                                 % match.group(1))
        # TODO Drop this once the dust has settled
        if (isinstance(self.section, QAPIDoc.ArgSection)
                and '#optional' in line):
            raise QAPISemError(self.info, "Please drop the #optional tag")
        self.section.append(line)

    def connect_member(self, member):
        if member.name not in self.args:
            # Undocumented TODO outlaw
            self.args[member.name] = QAPIDoc.ArgSection(member.name)
        self.args[member.name].connect(member)

    def check_expr(self, expr):
        if self.has_section('Returns') and 'command' not in expr:
            raise QAPISemError(self.info,
                               "'Returns:' is only valid for commands")

    def check(self):
        bogus = [name for name, section in self.args.iteritems()
                 if not section.member]
        if bogus:
            raise QAPISemError(
                self.info,
                "The following documented members are not in "
                "the declaration: %s" % ", ".join(bogus))


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
        self.docs = []
        self.cur_doc = None
        self.accept()

        while self.tok is not None:
            info = {'file': fname, 'line': self.line,
                    'parent': self.incl_info}
            if self.tok == '#':
                self.reject_expr_doc()
                self.cur_doc = self.get_doc(info)
                self.docs.append(self.cur_doc)
                continue

            expr = self.get_expr(False)
            if 'include' in expr:
                self.reject_expr_doc()
                if len(expr) != 1:
                    raise QAPISemError(info, "Invalid 'include' directive")
                include = expr['include']
                if not isinstance(include, str):
                    raise QAPISemError(info,
                                       "Value of 'include' must be a string")
                self._include(include, info, os.path.dirname(abs_fname),
                              previously_included)
            elif "pragma" in expr:
                self.reject_expr_doc()
                if len(expr) != 1:
                    raise QAPISemError(info, "Invalid 'pragma' directive")
                pragma = expr['pragma']
                if not isinstance(pragma, dict):
                    raise QAPISemError(
                        info, "Value of 'pragma' must be a dictionary")
                for name, value in pragma.iteritems():
                    self._pragma(name, value, info)
            else:
                expr_elem = {'expr': expr,
                             'info': info}
                if self.cur_doc:
                    if not self.cur_doc.symbol:
                        raise QAPISemError(
                            self.cur_doc.info,
                            "Expression documentation required")
                    expr_elem['doc'] = self.cur_doc
                self.exprs.append(expr_elem)
            self.cur_doc = None
        self.reject_expr_doc()

    def reject_expr_doc(self):
        if self.cur_doc and self.cur_doc.symbol:
            raise QAPISemError(
                self.cur_doc.info,
                "Documentation for '%s' is not followed by the definition"
                % self.cur_doc.symbol)

    def _include(self, include, info, base_dir, previously_included):
        incl_abs_fname = os.path.join(base_dir, include)
        # catch inclusion cycle
        inf = info
        while inf:
            if incl_abs_fname == os.path.abspath(inf['file']):
                raise QAPISemError(info, "Inclusion loop for %s" % include)
            inf = inf['parent']

        # skip multiple include of the same file
        if incl_abs_fname in previously_included:
            return
        try:
            fobj = open(incl_abs_fname, 'r')
        except IOError as e:
            raise QAPISemError(info, '%s: %s' % (e.strerror, include))
        exprs_include = QAPISchemaParser(fobj, previously_included, info)
        self.exprs.extend(exprs_include.exprs)
        self.docs.extend(exprs_include.docs)

    def _pragma(self, name, value, info):
        global doc_required, returns_whitelist, name_case_whitelist
        if name == 'doc-required':
            if not isinstance(value, bool):
                raise QAPISemError(info,
                                   "Pragma 'doc-required' must be boolean")
            doc_required = value
        elif name == 'returns-whitelist':
            if (not isinstance(value, list)
                    or any([not isinstance(elt, str) for elt in value])):
                raise QAPISemError(info,
                                   "Pragma returns-whitelist must be"
                                   " a list of strings")
            returns_whitelist = value
        elif name == 'name-case-whitelist':
            if (not isinstance(value, list)
                    or any([not isinstance(elt, str) for elt in value])):
                raise QAPISemError(info,
                                   "Pragma name-case-whitelist must be"
                                   " a list of strings")
            name_case_whitelist = value
        else:
            raise QAPISemError(info, "Unknown pragma '%s'" % name)

    def accept(self, skip_comment=True):
        while True:
            self.tok = self.src[self.cursor]
            self.pos = self.cursor
            self.cursor += 1
            self.val = None

            if self.tok == '#':
                if self.src[self.cursor] == '#':
                    # Start of doc comment
                    skip_comment = False
                self.cursor = self.src.find('\n', self.cursor)
                if not skip_comment:
                    self.val = self.src[self.pos:self.cursor]
                    return
            elif self.tok in '{}:,[]':
                return
            elif self.tok == "'":
                string = ''
                esc = False
                while True:
                    ch = self.src[self.cursor]
                    self.cursor += 1
                    if ch == '\n':
                        raise QAPIParseError(self, 'Missing terminating "\'"')
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
                                if ch not in '0123456789abcdefABCDEF':
                                    raise QAPIParseError(self,
                                                         '\\u escape needs 4 '
                                                         'hex digits')
                                value = (value << 4) + int(ch, 16)
                            # If Python 2 and 3 didn't disagree so much on
                            # how to handle Unicode, then we could allow
                            # Unicode string defaults.  But most of QAPI is
                            # ASCII-only, so we aren't losing much for now.
                            if not value or value > 0x7f:
                                raise QAPIParseError(self,
                                                     'For now, \\u escape '
                                                     'only supports non-zero '
                                                     'values up to \\u007f')
                            string += chr(value)
                        elif ch in '\\/\'"':
                            string += ch
                        else:
                            raise QAPIParseError(self,
                                                 "Unknown escape \\%s" % ch)
                        esc = False
                    elif ch == '\\':
                        esc = True
                    elif ch == "'":
                        self.val = string
                        return
                    else:
                        string += ch
            elif self.src.startswith('true', self.pos):
                self.val = True
                self.cursor += 3
                return
            elif self.src.startswith('false', self.pos):
                self.val = False
                self.cursor += 4
                return
            elif self.src.startswith('null', self.pos):
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
                raise QAPIParseError(self, 'Stray "%s"' % self.tok)

    def get_members(self):
        expr = OrderedDict()
        if self.tok == '}':
            self.accept()
            return expr
        if self.tok != "'":
            raise QAPIParseError(self, 'Expected string or "}"')
        while True:
            key = self.val
            self.accept()
            if self.tok != ':':
                raise QAPIParseError(self, 'Expected ":"')
            self.accept()
            if key in expr:
                raise QAPIParseError(self, 'Duplicate key "%s"' % key)
            expr[key] = self.get_expr(True)
            if self.tok == '}':
                self.accept()
                return expr
            if self.tok != ',':
                raise QAPIParseError(self, 'Expected "," or "}"')
            self.accept()
            if self.tok != "'":
                raise QAPIParseError(self, 'Expected string')

    def get_values(self):
        expr = []
        if self.tok == ']':
            self.accept()
            return expr
        if self.tok not in "{['tfn":
            raise QAPIParseError(self, 'Expected "{", "[", "]", string, '
                                 'boolean or "null"')
        while True:
            expr.append(self.get_expr(True))
            if self.tok == ']':
                self.accept()
                return expr
            if self.tok != ',':
                raise QAPIParseError(self, 'Expected "," or "]"')
            self.accept()

    def get_expr(self, nested):
        if self.tok != '{' and not nested:
            raise QAPIParseError(self, 'Expected "{"')
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
            raise QAPIParseError(self, 'Expected "{", "[", string, '
                                 'boolean or "null"')
        return expr

    def get_doc(self, info):
        if self.val != '##':
            raise QAPIParseError(self, "Junk after '##' at start of "
                                 "documentation comment")

        doc = QAPIDoc(self, info)
        self.accept(False)
        while self.tok == '#':
            if self.val.startswith('##'):
                # End of doc comment
                if self.val != '##':
                    raise QAPIParseError(self, "Junk after '##' at end of "
                                         "documentation comment")
                doc.end_comment()
                self.accept()
                return doc
            else:
                doc.append(self.val)
            self.accept(False)

        raise QAPIParseError(self, "Documentation comment must end with '##'")


#
# Semantic analysis of schema expressions
# TODO fold into QAPISchema
# TODO catching name collisions in generated code would be nice
#


def find_base_members(base):
    if isinstance(base, dict):
        return base
    base_struct_define = struct_types.get(base)
    if not base_struct_define:
        return None
    return base_struct_define['data']


# Return the qtype of an alternate branch, or None on error.
def find_alternate_member_qtype(qapi_type):
    if qapi_type in builtin_types:
        return builtin_types[qapi_type]
    elif qapi_type in struct_types:
        return 'QTYPE_QDICT'
    elif qapi_type in enum_types:
        return 'QTYPE_QSTRING'
    elif qapi_type in union_types:
        return 'QTYPE_QDICT'
    return None


# Return the discriminator enum define if discriminator is specified as an
# enum type, otherwise return None.
def discriminator_find_enum_define(expr):
    base = expr.get('base')
    discriminator = expr.get('discriminator')

    if not (discriminator and base):
        return None

    base_members = find_base_members(base)
    if not base_members:
        return None

    discriminator_type = base_members.get(discriminator)
    if not discriminator_type:
        return None

    return enum_types.get(discriminator_type)


# Names must be letters, numbers, -, and _.  They must start with letter,
# except for downstream extensions which must start with __RFQDN_.
# Dots are only valid in the downstream extension prefix.
valid_name = re.compile(r'^(__[a-zA-Z0-9.-]+_)?'
                        '[a-zA-Z][a-zA-Z0-9_-]*$')


def check_name(info, source, name, allow_optional=False,
               enum_member=False):
    global valid_name
    membername = name

    if not isinstance(name, str):
        raise QAPISemError(info, "%s requires a string name" % source)
    if name.startswith('*'):
        membername = name[1:]
        if not allow_optional:
            raise QAPISemError(info, "%s does not allow optional name '%s'"
                               % (source, name))
    # Enum members can start with a digit, because the generated C
    # code always prefixes it with the enum name
    if enum_member and membername[0].isdigit():
        membername = 'D' + membername
    # Reserve the entire 'q_' namespace for c_name(), and for 'q_empty'
    # and 'q_obj_*' implicit type names.
    if not valid_name.match(membername) or \
       c_name(membername, False).startswith('q_'):
        raise QAPISemError(info, "%s uses invalid name '%s'" % (source, name))


def add_name(name, info, meta, implicit=False):
    global all_names
    check_name(info, "'%s'" % meta, name)
    # FIXME should reject names that differ only in '_' vs. '.'
    # vs. '-', because they're liable to clash in generated C.
    if name in all_names:
        raise QAPISemError(info, "%s '%s' is already defined"
                           % (all_names[name], name))
    if not implicit and (name.endswith('Kind') or name.endswith('List')):
        raise QAPISemError(info, "%s '%s' should not end in '%s'"
                           % (meta, name, name[-4:]))
    all_names[name] = meta


def check_type(info, source, value, allow_array=False,
               allow_dict=False, allow_optional=False,
               allow_metas=[]):
    global all_names

    if value is None:
        return

    # Check if array type for value is okay
    if isinstance(value, list):
        if not allow_array:
            raise QAPISemError(info, "%s cannot be an array" % source)
        if len(value) != 1 or not isinstance(value[0], str):
            raise QAPISemError(info,
                               "%s: array type must contain single type name" %
                               source)
        value = value[0]

    # Check if type name for value is okay
    if isinstance(value, str):
        if value not in all_names:
            raise QAPISemError(info, "%s uses unknown type '%s'"
                               % (source, value))
        if not all_names[value] in allow_metas:
            raise QAPISemError(info, "%s cannot use %s type '%s'" %
                               (source, all_names[value], value))
        return

    if not allow_dict:
        raise QAPISemError(info, "%s should be a type name" % source)

    if not isinstance(value, OrderedDict):
        raise QAPISemError(info,
                           "%s should be a dictionary or type name" % source)

    # value is a dictionary, check that each member is okay
    for (key, arg) in value.items():
        check_name(info, "Member of %s" % source, key,
                   allow_optional=allow_optional)
        if c_name(key, False) == 'u' or c_name(key, False).startswith('has_'):
            raise QAPISemError(info, "Member of %s uses reserved name '%s'"
                               % (source, key))
        # Todo: allow dictionaries to represent default values of
        # an optional argument.
        check_type(info, "Member '%s' of %s" % (key, source), arg,
                   allow_array=True,
                   allow_metas=['built-in', 'union', 'alternate', 'struct',
                                'enum'])


def check_command(expr, info):
    name = expr['command']
    boxed = expr.get('boxed', False)

    args_meta = ['struct']
    if boxed:
        args_meta += ['union', 'alternate']
    check_type(info, "'data' for command '%s'" % name,
               expr.get('data'), allow_dict=not boxed, allow_optional=True,
               allow_metas=args_meta)
    returns_meta = ['union', 'struct']
    if name in returns_whitelist:
        returns_meta += ['built-in', 'alternate', 'enum']
    check_type(info, "'returns' for command '%s'" % name,
               expr.get('returns'), allow_array=True,
               allow_optional=True, allow_metas=returns_meta)


def check_event(expr, info):
    name = expr['event']
    boxed = expr.get('boxed', False)

    meta = ['struct']
    if boxed:
        meta += ['union', 'alternate']
    check_type(info, "'data' for event '%s'" % name,
               expr.get('data'), allow_dict=not boxed, allow_optional=True,
               allow_metas=meta)


def check_union(expr, info):
    name = expr['union']
    base = expr.get('base')
    discriminator = expr.get('discriminator')
    members = expr['data']

    # Two types of unions, determined by discriminator.

    # With no discriminator it is a simple union.
    if discriminator is None:
        enum_define = None
        allow_metas = ['built-in', 'union', 'alternate', 'struct', 'enum']
        if base is not None:
            raise QAPISemError(info, "Simple union '%s' must not have a base" %
                               name)

    # Else, it's a flat union.
    else:
        # The object must have a string or dictionary 'base'.
        check_type(info, "'base' for union '%s'" % name,
                   base, allow_dict=True, allow_optional=True,
                   allow_metas=['struct'])
        if not base:
            raise QAPISemError(info, "Flat union '%s' must have a base"
                               % name)
        base_members = find_base_members(base)
        assert base_members is not None

        # The value of member 'discriminator' must name a non-optional
        # member of the base struct.
        check_name(info, "Discriminator of flat union '%s'" % name,
                   discriminator)
        discriminator_type = base_members.get(discriminator)
        if not discriminator_type:
            raise QAPISemError(info,
                               "Discriminator '%s' is not a member of base "
                               "struct '%s'"
                               % (discriminator, base))
        enum_define = enum_types.get(discriminator_type)
        allow_metas = ['struct']
        # Do not allow string discriminator
        if not enum_define:
            raise QAPISemError(info,
                               "Discriminator '%s' must be of enumeration "
                               "type" % discriminator)

    # Check every branch; don't allow an empty union
    if len(members) == 0:
        raise QAPISemError(info, "Union '%s' cannot have empty 'data'" % name)
    for (key, value) in members.items():
        check_name(info, "Member of union '%s'" % name, key)

        # Each value must name a known type
        check_type(info, "Member '%s' of union '%s'" % (key, name),
                   value, allow_array=not base, allow_metas=allow_metas)

        # If the discriminator names an enum type, then all members
        # of 'data' must also be members of the enum type.
        if enum_define:
            if key not in enum_define['data']:
                raise QAPISemError(info,
                                   "Discriminator value '%s' is not found in "
                                   "enum '%s'"
                                   % (key, enum_define['enum']))

    # If discriminator is user-defined, ensure all values are covered
    if enum_define:
        for value in enum_define['data']:
            if value not in members.keys():
                raise QAPISemError(info, "Union '%s' data missing '%s' branch"
                                   % (name, value))


def check_alternate(expr, info):
    name = expr['alternate']
    members = expr['data']
    types_seen = {}

    # Check every branch; require at least two branches
    if len(members) < 2:
        raise QAPISemError(info,
                           "Alternate '%s' should have at least two branches "
                           "in 'data'" % name)
    for (key, value) in members.items():
        check_name(info, "Member of alternate '%s'" % name, key)

        # Ensure alternates have no type conflicts.
        check_type(info, "Member '%s' of alternate '%s'" % (key, name),
                   value,
                   allow_metas=['built-in', 'union', 'struct', 'enum'])
        qtype = find_alternate_member_qtype(value)
        if not qtype:
            raise QAPISemError(info, "Alternate '%s' member '%s' cannot use "
                               "type '%s'" % (name, key, value))
        conflicting = set([qtype])
        if qtype == 'QTYPE_QSTRING':
            enum_expr = enum_types.get(value)
            if enum_expr:
                for v in enum_expr['data']:
                    if v in ['on', 'off']:
                        conflicting.add('QTYPE_QBOOL')
                    if re.match(r'[-+0-9.]', v): # lazy, could be tightened
                        conflicting.add('QTYPE_QNUM')
            else:
                conflicting.add('QTYPE_QNUM')
                conflicting.add('QTYPE_QBOOL')
        if conflicting & set(types_seen):
            raise QAPISemError(info, "Alternate '%s' member '%s' can't "
                               "be distinguished from member '%s'"
                               % (name, key, types_seen[qtype]))
        for qt in conflicting:
            types_seen[qt] = key


def check_enum(expr, info):
    name = expr['enum']
    members = expr.get('data')
    prefix = expr.get('prefix')

    if not isinstance(members, list):
        raise QAPISemError(info,
                           "Enum '%s' requires an array for 'data'" % name)
    if prefix is not None and not isinstance(prefix, str):
        raise QAPISemError(info,
                           "Enum '%s' requires a string for 'prefix'" % name)
    for member in members:
        check_name(info, "Member of enum '%s'" % name, member,
                   enum_member=True)


def check_struct(expr, info):
    name = expr['struct']
    members = expr['data']

    check_type(info, "'data' for struct '%s'" % name, members,
               allow_dict=True, allow_optional=True)
    check_type(info, "'base' for struct '%s'" % name, expr.get('base'),
               allow_metas=['struct'])


def check_keys(expr_elem, meta, required, optional=[]):
    expr = expr_elem['expr']
    info = expr_elem['info']
    name = expr[meta]
    if not isinstance(name, str):
        raise QAPISemError(info, "'%s' key must have a string value" % meta)
    required = required + [meta]
    for (key, value) in expr.items():
        if key not in required and key not in optional:
            raise QAPISemError(info, "Unknown key '%s' in %s '%s'"
                               % (key, meta, name))
        if (key == 'gen' or key == 'success-response') and value is not False:
            raise QAPISemError(info,
                               "'%s' of %s '%s' should only use false value"
                               % (key, meta, name))
        if key == 'boxed' and value is not True:
            raise QAPISemError(info,
                               "'%s' of %s '%s' should only use true value"
                               % (key, meta, name))
    for key in required:
        if key not in expr:
            raise QAPISemError(info, "Key '%s' is missing from %s '%s'"
                               % (key, meta, name))


def check_exprs(exprs):
    global all_names

    # Populate name table with names of built-in types
    for builtin in builtin_types.keys():
        all_names[builtin] = 'built-in'

    # Learn the types and check for valid expression keys
    for expr_elem in exprs:
        expr = expr_elem['expr']
        info = expr_elem['info']
        doc = expr_elem.get('doc')

        if not doc and doc_required:
            raise QAPISemError(info,
                               "Expression missing documentation comment")

        if 'enum' in expr:
            meta = 'enum'
            check_keys(expr_elem, 'enum', ['data'], ['prefix'])
            enum_types[expr[meta]] = expr
        elif 'union' in expr:
            meta = 'union'
            check_keys(expr_elem, 'union', ['data'],
                       ['base', 'discriminator'])
            union_types[expr[meta]] = expr
        elif 'alternate' in expr:
            meta = 'alternate'
            check_keys(expr_elem, 'alternate', ['data'])
        elif 'struct' in expr:
            meta = 'struct'
            check_keys(expr_elem, 'struct', ['data'], ['base'])
            struct_types[expr[meta]] = expr
        elif 'command' in expr:
            meta = 'command'
            check_keys(expr_elem, 'command', [],
                       ['data', 'returns', 'gen', 'success-response', 'boxed'])
        elif 'event' in expr:
            meta = 'event'
            check_keys(expr_elem, 'event', [], ['data', 'boxed'])
        else:
            raise QAPISemError(expr_elem['info'],
                               "Expression is missing metatype")
        name = expr[meta]
        add_name(name, info, meta)
        if doc and doc.symbol != name:
            raise QAPISemError(info, "Definition of '%s' follows documentation"
                               " for '%s'" % (name, doc.symbol))

    # Try again for hidden UnionKind enum
    for expr_elem in exprs:
        expr = expr_elem['expr']
        if 'union' in expr and not discriminator_find_enum_define(expr):
            name = '%sKind' % expr['union']
        elif 'alternate' in expr:
            name = '%sKind' % expr['alternate']
        else:
            continue
        enum_types[name] = {'enum': name}
        add_name(name, info, 'enum', implicit=True)

    # Validate that exprs make sense
    for expr_elem in exprs:
        expr = expr_elem['expr']
        info = expr_elem['info']
        doc = expr_elem.get('doc')

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

        if doc:
            doc.check_expr(expr)

    return exprs


#
# Schema compiler frontend
#

class QAPISchemaEntity(object):
    def __init__(self, name, info, doc):
        assert isinstance(name, str)
        self.name = name
        # For explicitly defined entities, info points to the (explicit)
        # definition.  For builtins (and their arrays), info is None.
        # For implicitly defined entities, info points to a place that
        # triggered the implicit definition (there may be more than one
        # such place).
        self.info = info
        self.doc = doc

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
                      gen, success_response, boxed):
        pass

    def visit_event(self, name, info, arg_type, boxed):
        pass


class QAPISchemaType(QAPISchemaEntity):
    # Return the C type for common use.
    # For the types we commonly box, this is a pointer type.
    def c_type(self):
        pass

    # Return the C type to be used in a parameter list.
    def c_param_type(self):
        return self.c_type()

    # Return the C type to be used where we suppress boxing.
    def c_unboxed_type(self):
        return self.c_type()

    def json_type(self):
        pass

    def alternate_qtype(self):
        json2qtype = {
            'null':    'QTYPE_QNULL',
            'string':  'QTYPE_QSTRING',
            'number':  'QTYPE_QNUM',
            'int':     'QTYPE_QNUM',
            'boolean': 'QTYPE_QBOOL',
            'object':  'QTYPE_QDICT'
        }
        return json2qtype.get(self.json_type())

    def doc_type(self):
        if self.is_implicit():
            return None
        return self.name


class QAPISchemaBuiltinType(QAPISchemaType):
    def __init__(self, name, json_type, c_type):
        QAPISchemaType.__init__(self, name, None, None)
        assert not c_type or isinstance(c_type, str)
        assert json_type in ('string', 'number', 'int', 'boolean', 'null',
                             'value')
        self._json_type_name = json_type
        self._c_type_name = c_type

    def c_name(self):
        return self.name

    def c_type(self):
        return self._c_type_name

    def c_param_type(self):
        if self.name == 'str':
            return 'const ' + self._c_type_name
        return self._c_type_name

    def json_type(self):
        return self._json_type_name

    def doc_type(self):
        return self.json_type()

    def visit(self, visitor):
        visitor.visit_builtin_type(self.name, self.info, self.json_type())


class QAPISchemaEnumType(QAPISchemaType):
    def __init__(self, name, info, doc, values, prefix):
        QAPISchemaType.__init__(self, name, info, doc)
        for v in values:
            assert isinstance(v, QAPISchemaMember)
            v.set_owner(name)
        assert prefix is None or isinstance(prefix, str)
        self.values = values
        self.prefix = prefix

    def check(self, schema):
        seen = {}
        for v in self.values:
            v.check_clash(self.info, seen)
            if self.doc:
                self.doc.connect_member(v)

    def is_implicit(self):
        # See QAPISchema._make_implicit_enum_type() and ._def_predefineds()
        return self.name.endswith('Kind') or self.name == 'QType'

    def c_type(self):
        return c_name(self.name)

    def member_names(self):
        return [v.name for v in self.values]

    def json_type(self):
        return 'string'

    def visit(self, visitor):
        visitor.visit_enum_type(self.name, self.info,
                                self.member_names(), self.prefix)


class QAPISchemaArrayType(QAPISchemaType):
    def __init__(self, name, info, element_type):
        QAPISchemaType.__init__(self, name, info, None)
        assert isinstance(element_type, str)
        self._element_type_name = element_type
        self.element_type = None

    def check(self, schema):
        self.element_type = schema.lookup_type(self._element_type_name)
        assert self.element_type

    def is_implicit(self):
        return True

    def c_type(self):
        return c_name(self.name) + pointer_suffix

    def json_type(self):
        return 'array'

    def doc_type(self):
        elt_doc_type = self.element_type.doc_type()
        if not elt_doc_type:
            return None
        return 'array of ' + elt_doc_type

    def visit(self, visitor):
        visitor.visit_array_type(self.name, self.info, self.element_type)


class QAPISchemaObjectType(QAPISchemaType):
    def __init__(self, name, info, doc, base, local_members, variants):
        # struct has local_members, optional base, and no variants
        # flat union has base, variants, and no local_members
        # simple union has local_members, variants, and no base
        QAPISchemaType.__init__(self, name, info, doc)
        assert base is None or isinstance(base, str)
        for m in local_members:
            assert isinstance(m, QAPISchemaObjectTypeMember)
            m.set_owner(name)
        if variants is not None:
            assert isinstance(variants, QAPISchemaObjectTypeVariants)
            variants.set_owner(name)
        self._base_name = base
        self.base = None
        self.local_members = local_members
        self.variants = variants
        self.members = None

    def check(self, schema):
        if self.members is False:               # check for cycles
            raise QAPISemError(self.info,
                               "Object %s contains itself" % self.name)
        if self.members:
            return
        self.members = False                    # mark as being checked
        seen = OrderedDict()
        if self._base_name:
            self.base = schema.lookup_type(self._base_name)
            assert isinstance(self.base, QAPISchemaObjectType)
            self.base.check(schema)
            self.base.check_clash(self.info, seen)
        for m in self.local_members:
            m.check(schema)
            m.check_clash(self.info, seen)
            if self.doc:
                self.doc.connect_member(m)
        self.members = seen.values()
        if self.variants:
            self.variants.check(schema, seen)
            assert self.variants.tag_member in self.members
            self.variants.check_clash(self.info, seen)
        if self.doc:
            self.doc.check()

    # Check that the members of this type do not cause duplicate JSON members,
    # and update seen to track the members seen so far. Report any errors
    # on behalf of info, which is not necessarily self.info
    def check_clash(self, info, seen):
        assert not self.variants       # not implemented
        for m in self.members:
            m.check_clash(info, seen)

    def is_implicit(self):
        # See QAPISchema._make_implicit_object_type(), as well as
        # _def_predefineds()
        return self.name.startswith('q_')

    def is_empty(self):
        assert self.members is not None
        return not self.members and not self.variants

    def c_name(self):
        assert self.name != 'q_empty'
        return QAPISchemaType.c_name(self)

    def c_type(self):
        assert not self.is_implicit()
        return c_name(self.name) + pointer_suffix

    def c_unboxed_type(self):
        return c_name(self.name)

    def json_type(self):
        return 'object'

    def visit(self, visitor):
        visitor.visit_object_type(self.name, self.info,
                                  self.base, self.local_members, self.variants)
        visitor.visit_object_type_flat(self.name, self.info,
                                       self.members, self.variants)


class QAPISchemaMember(object):
    role = 'member'

    def __init__(self, name):
        assert isinstance(name, str)
        self.name = name
        self.owner = None

    def set_owner(self, name):
        assert not self.owner
        self.owner = name

    def check_clash(self, info, seen):
        cname = c_name(self.name)
        if cname.lower() != cname and self.owner not in name_case_whitelist:
            raise QAPISemError(info,
                               "%s should not use uppercase" % self.describe())
        if cname in seen:
            raise QAPISemError(info, "%s collides with %s" %
                               (self.describe(), seen[cname].describe()))
        seen[cname] = self

    def _pretty_owner(self):
        owner = self.owner
        if owner.startswith('q_obj_'):
            # See QAPISchema._make_implicit_object_type() - reverse the
            # mapping there to create a nice human-readable description
            owner = owner[6:]
            if owner.endswith('-arg'):
                return '(parameter of %s)' % owner[:-4]
            elif owner.endswith('-base'):
                return '(base of %s)' % owner[:-5]
            else:
                assert owner.endswith('-wrapper')
                # Unreachable and not implemented
                assert False
        if owner.endswith('Kind'):
            # See QAPISchema._make_implicit_enum_type()
            return '(branch of %s)' % owner[:-4]
        return '(%s of %s)' % (self.role, owner)

    def describe(self):
        return "'%s' %s" % (self.name, self._pretty_owner())


class QAPISchemaObjectTypeMember(QAPISchemaMember):
    def __init__(self, name, typ, optional):
        QAPISchemaMember.__init__(self, name)
        assert isinstance(typ, str)
        assert isinstance(optional, bool)
        self._type_name = typ
        self.type = None
        self.optional = optional

    def check(self, schema):
        assert self.owner
        self.type = schema.lookup_type(self._type_name)
        assert self.type


class QAPISchemaObjectTypeVariants(object):
    def __init__(self, tag_name, tag_member, variants):
        # Flat unions pass tag_name but not tag_member.
        # Simple unions and alternates pass tag_member but not tag_name.
        # After check(), tag_member is always set, and tag_name remains
        # a reliable witness of being used by a flat union.
        assert bool(tag_member) != bool(tag_name)
        assert (isinstance(tag_name, str) or
                isinstance(tag_member, QAPISchemaObjectTypeMember))
        assert len(variants) > 0
        for v in variants:
            assert isinstance(v, QAPISchemaObjectTypeVariant)
        self._tag_name = tag_name
        self.tag_member = tag_member
        self.variants = variants

    def set_owner(self, name):
        for v in self.variants:
            v.set_owner(name)

    def check(self, schema, seen):
        if not self.tag_member:    # flat union
            self.tag_member = seen[c_name(self._tag_name)]
            assert self._tag_name == self.tag_member.name
        assert isinstance(self.tag_member.type, QAPISchemaEnumType)
        for v in self.variants:
            v.check(schema)
            # Union names must match enum values; alternate names are
            # checked separately. Use 'seen' to tell the two apart.
            if seen:
                assert v.name in self.tag_member.type.member_names()
                assert isinstance(v.type, QAPISchemaObjectType)
                v.type.check(schema)

    def check_clash(self, info, seen):
        for v in self.variants:
            # Reset seen map for each variant, since qapi names from one
            # branch do not affect another branch
            assert isinstance(v.type, QAPISchemaObjectType)
            v.type.check_clash(info, dict(seen))


class QAPISchemaObjectTypeVariant(QAPISchemaObjectTypeMember):
    role = 'branch'

    def __init__(self, name, typ):
        QAPISchemaObjectTypeMember.__init__(self, name, typ, False)


class QAPISchemaAlternateType(QAPISchemaType):
    def __init__(self, name, info, doc, variants):
        QAPISchemaType.__init__(self, name, info, doc)
        assert isinstance(variants, QAPISchemaObjectTypeVariants)
        assert variants.tag_member
        variants.set_owner(name)
        variants.tag_member.set_owner(self.name)
        self.variants = variants

    def check(self, schema):
        self.variants.tag_member.check(schema)
        # Not calling self.variants.check_clash(), because there's nothing
        # to clash with
        self.variants.check(schema, {})
        # Alternate branch names have no relation to the tag enum values;
        # so we have to check for potential name collisions ourselves.
        seen = {}
        for v in self.variants.variants:
            v.check_clash(self.info, seen)
            if self.doc:
                self.doc.connect_member(v)
        if self.doc:
            self.doc.check()

    def c_type(self):
        return c_name(self.name) + pointer_suffix

    def json_type(self):
        return 'value'

    def visit(self, visitor):
        visitor.visit_alternate_type(self.name, self.info, self.variants)

    def is_empty(self):
        return False


class QAPISchemaCommand(QAPISchemaEntity):
    def __init__(self, name, info, doc, arg_type, ret_type,
                 gen, success_response, boxed):
        QAPISchemaEntity.__init__(self, name, info, doc)
        assert not arg_type or isinstance(arg_type, str)
        assert not ret_type or isinstance(ret_type, str)
        self._arg_type_name = arg_type
        self.arg_type = None
        self._ret_type_name = ret_type
        self.ret_type = None
        self.gen = gen
        self.success_response = success_response
        self.boxed = boxed

    def check(self, schema):
        if self._arg_type_name:
            self.arg_type = schema.lookup_type(self._arg_type_name)
            assert (isinstance(self.arg_type, QAPISchemaObjectType) or
                    isinstance(self.arg_type, QAPISchemaAlternateType))
            self.arg_type.check(schema)
            if self.boxed:
                if self.arg_type.is_empty():
                    raise QAPISemError(self.info,
                                       "Cannot use 'boxed' with empty type")
            else:
                assert not isinstance(self.arg_type, QAPISchemaAlternateType)
                assert not self.arg_type.variants
        elif self.boxed:
            raise QAPISemError(self.info, "Use of 'boxed' requires 'data'")
        if self._ret_type_name:
            self.ret_type = schema.lookup_type(self._ret_type_name)
            assert isinstance(self.ret_type, QAPISchemaType)

    def visit(self, visitor):
        visitor.visit_command(self.name, self.info,
                              self.arg_type, self.ret_type,
                              self.gen, self.success_response, self.boxed)


class QAPISchemaEvent(QAPISchemaEntity):
    def __init__(self, name, info, doc, arg_type, boxed):
        QAPISchemaEntity.__init__(self, name, info, doc)
        assert not arg_type or isinstance(arg_type, str)
        self._arg_type_name = arg_type
        self.arg_type = None
        self.boxed = boxed

    def check(self, schema):
        if self._arg_type_name:
            self.arg_type = schema.lookup_type(self._arg_type_name)
            assert (isinstance(self.arg_type, QAPISchemaObjectType) or
                    isinstance(self.arg_type, QAPISchemaAlternateType))
            self.arg_type.check(schema)
            if self.boxed:
                if self.arg_type.is_empty():
                    raise QAPISemError(self.info,
                                       "Cannot use 'boxed' with empty type")
            else:
                assert not isinstance(self.arg_type, QAPISchemaAlternateType)
                assert not self.arg_type.variants
        elif self.boxed:
            raise QAPISemError(self.info, "Use of 'boxed' requires 'data'")

    def visit(self, visitor):
        visitor.visit_event(self.name, self.info, self.arg_type, self.boxed)


class QAPISchema(object):
    def __init__(self, fname):
        try:
            parser = QAPISchemaParser(open(fname, 'r'))
            self.exprs = check_exprs(parser.exprs)
            self.docs = parser.docs
            self._entity_dict = {}
            self._predefining = True
            self._def_predefineds()
            self._predefining = False
            self._def_exprs()
            self.check()
        except QAPIError as err:
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

    def _def_builtin_type(self, name, json_type, c_type):
        self._def_entity(QAPISchemaBuiltinType(name, json_type, c_type))
        # TODO As long as we have QAPI_TYPES_BUILTIN to share multiple
        # qapi-types.h from a single .c, all arrays of builtins must be
        # declared in the first file whether or not they are used.  Nicer
        # would be to use lazy instantiation, while figuring out how to
        # avoid compilation issues with multiple qapi-types.h.
        self._make_array_type(name, None)

    def _def_predefineds(self):
        for t in [('str',    'string',  'char' + pointer_suffix),
                  ('number', 'number',  'double'),
                  ('int',    'int',     'int64_t'),
                  ('int8',   'int',     'int8_t'),
                  ('int16',  'int',     'int16_t'),
                  ('int32',  'int',     'int32_t'),
                  ('int64',  'int',     'int64_t'),
                  ('uint8',  'int',     'uint8_t'),
                  ('uint16', 'int',     'uint16_t'),
                  ('uint32', 'int',     'uint32_t'),
                  ('uint64', 'int',     'uint64_t'),
                  ('size',   'int',     'uint64_t'),
                  ('bool',   'boolean', 'bool'),
                  ('any',    'value',   'QObject' + pointer_suffix),
                  ('null',   'null',    'QNull' + pointer_suffix)]:
            self._def_builtin_type(*t)
        self.the_empty_object_type = QAPISchemaObjectType(
            'q_empty', None, None, None, [], None)
        self._def_entity(self.the_empty_object_type)
        qtype_values = self._make_enum_members(['none', 'qnull', 'qnum',
                                                'qstring', 'qdict', 'qlist',
                                                'qbool'])
        self._def_entity(QAPISchemaEnumType('QType', None, None,
                                            qtype_values, 'QTYPE'))

    def _make_enum_members(self, values):
        return [QAPISchemaMember(v) for v in values]

    def _make_implicit_enum_type(self, name, info, values):
        # See also QAPISchemaObjectTypeMember._pretty_owner()
        name = name + 'Kind'   # Use namespace reserved by add_name()
        self._def_entity(QAPISchemaEnumType(
            name, info, None, self._make_enum_members(values), None))
        return name

    def _make_array_type(self, element_type, info):
        name = element_type + 'List'   # Use namespace reserved by add_name()
        if not self.lookup_type(name):
            self._def_entity(QAPISchemaArrayType(name, info, element_type))
        return name

    def _make_implicit_object_type(self, name, info, doc, role, members):
        if not members:
            return None
        # See also QAPISchemaObjectTypeMember._pretty_owner()
        name = 'q_obj_%s-%s' % (name, role)
        if not self.lookup_entity(name, QAPISchemaObjectType):
            self._def_entity(QAPISchemaObjectType(name, info, doc, None,
                                                  members, None))
        return name

    def _def_enum_type(self, expr, info, doc):
        name = expr['enum']
        data = expr['data']
        prefix = expr.get('prefix')
        self._def_entity(QAPISchemaEnumType(
            name, info, doc, self._make_enum_members(data), prefix))

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

    def _def_struct_type(self, expr, info, doc):
        name = expr['struct']
        base = expr.get('base')
        data = expr['data']
        self._def_entity(QAPISchemaObjectType(name, info, doc, base,
                                              self._make_members(data, info),
                                              None))

    def _make_variant(self, case, typ):
        return QAPISchemaObjectTypeVariant(case, typ)

    def _make_simple_variant(self, case, typ, info):
        if isinstance(typ, list):
            assert len(typ) == 1
            typ = self._make_array_type(typ[0], info)
        typ = self._make_implicit_object_type(
            typ, info, None, 'wrapper', [self._make_member('data', typ, info)])
        return QAPISchemaObjectTypeVariant(case, typ)

    def _def_union_type(self, expr, info, doc):
        name = expr['union']
        data = expr['data']
        base = expr.get('base')
        tag_name = expr.get('discriminator')
        tag_member = None
        if isinstance(base, dict):
            base = (self._make_implicit_object_type(
                name, info, doc, 'base', self._make_members(base, info)))
        if tag_name:
            variants = [self._make_variant(key, value)
                        for (key, value) in data.iteritems()]
            members = []
        else:
            variants = [self._make_simple_variant(key, value, info)
                        for (key, value) in data.iteritems()]
            typ = self._make_implicit_enum_type(name, info,
                                                [v.name for v in variants])
            tag_member = QAPISchemaObjectTypeMember('type', typ, False)
            members = [tag_member]
        self._def_entity(
            QAPISchemaObjectType(name, info, doc, base, members,
                                 QAPISchemaObjectTypeVariants(tag_name,
                                                              tag_member,
                                                              variants)))

    def _def_alternate_type(self, expr, info, doc):
        name = expr['alternate']
        data = expr['data']
        variants = [self._make_variant(key, value)
                    for (key, value) in data.iteritems()]
        tag_member = QAPISchemaObjectTypeMember('type', 'QType', False)
        self._def_entity(
            QAPISchemaAlternateType(name, info, doc,
                                    QAPISchemaObjectTypeVariants(None,
                                                                 tag_member,
                                                                 variants)))

    def _def_command(self, expr, info, doc):
        name = expr['command']
        data = expr.get('data')
        rets = expr.get('returns')
        gen = expr.get('gen', True)
        success_response = expr.get('success-response', True)
        boxed = expr.get('boxed', False)
        if isinstance(data, OrderedDict):
            data = self._make_implicit_object_type(
                name, info, doc, 'arg', self._make_members(data, info))
        if isinstance(rets, list):
            assert len(rets) == 1
            rets = self._make_array_type(rets[0], info)
        self._def_entity(QAPISchemaCommand(name, info, doc, data, rets,
                                           gen, success_response, boxed))

    def _def_event(self, expr, info, doc):
        name = expr['event']
        data = expr.get('data')
        boxed = expr.get('boxed', False)
        if isinstance(data, OrderedDict):
            data = self._make_implicit_object_type(
                name, info, doc, 'arg', self._make_members(data, info))
        self._def_entity(QAPISchemaEvent(name, info, doc, data, boxed))

    def _def_exprs(self):
        for expr_elem in self.exprs:
            expr = expr_elem['expr']
            info = expr_elem['info']
            doc = expr_elem.get('doc')
            if 'enum' in expr:
                self._def_enum_type(expr, info, doc)
            elif 'struct' in expr:
                self._def_struct_type(expr, info, doc)
            elif 'union' in expr:
                self._def_union_type(expr, info, doc)
            elif 'alternate' in expr:
                self._def_alternate_type(expr, info, doc)
            elif 'command' in expr:
                self._def_command(expr, info, doc)
            elif 'event' in expr:
                self._def_event(expr, info, doc)
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
        # When c is upper and no '_' appears before, do more checks
        if c.isupper() and (i > 0) and c_fun_str[i - 1] != '_':
            if i < l - 1 and c_fun_str[i + 1].islower():
                new_name += '_'
            elif c_fun_str[i - 1].isdigit():
                new_name += '_'
        new_name += c
    return new_name.lstrip('_').upper()


def c_enum_const(type_name, const_name, prefix=None):
    if prefix is not None:
        type_name = prefix
    return camel_to_upper(type_name) + '_' + c_name(const_name, False).upper()

c_name_trans = string.maketrans('.-', '__')


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
    polluted_words = set(['unix', 'errno', 'mips', 'sparc'])
    name = name.translate(c_name_trans)
    if protect and (name in c89_words | c99_words | c11_words | gcc_words
                    | cpp_words | polluted_words):
        return 'q_' + name
    return name

eatspace = '\033EATSPACE.'
pointer_suffix = ' *' + eatspace


def genindent(count):
    ret = ''
    for _ in range(count):
        ret += ' '
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
        raw = re.subn(re.compile(r'^.', re.MULTILINE),
                      indent + r'\g<0>', raw)
        raw = raw[0]
    return re.sub(re.escape(eatspace) + r' *', '', raw)


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

    max_index = c_enum_const(name, '_MAX', prefix)
    ret += mcgen('''
    [%(max_index)s] = NULL,
};
''',
                 max_index=max_index)
    return ret


def gen_enum(name, values, prefix=None):
    # append automatically generated _MAX value
    enum_values = values + ['_MAX']

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


def build_params(arg_type, boxed, extra):
    if not arg_type:
        assert not boxed
        return extra
    ret = ''
    sep = ''
    if boxed:
        ret += '%s arg' % arg_type.c_param_type()
        sep = ', '
    else:
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
    return ret


#
# Common command line parsing
#


def parse_command_line(extra_options='', extra_long_options=[]):

    try:
        opts, args = getopt.gnu_getopt(sys.argv[1:],
                                       'chp:o:' + extra_options,
                                       ['source', 'header', 'prefix=',
                                        'output-dir='] + extra_long_options)
    except getopt.GetoptError as err:
        print >>sys.stderr, "%s: %s" % (sys.argv[0], str(err))
        sys.exit(1)

    output_dir = ''
    prefix = ''
    do_c = False
    do_h = False
    extra_opts = []

    for oa in opts:
        o, a = oa
        if o in ('-p', '--prefix'):
            match = re.match(r'([A-Za-z_.-][A-Za-z0-9_.-]*)?', a)
            if match.end() != len(a):
                print >>sys.stderr, \
                    "%s: 'funny character '%s' in argument of --prefix" \
                    % (sys.argv[0], a[match.end()])
                sys.exit(1)
            prefix = a
        elif o in ('-o', '--output-dir'):
            output_dir = a + '/'
        elif o in ('-c', '--source'):
            do_c = True
        elif o in ('-h', '--header'):
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
        except os.error as e:
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
