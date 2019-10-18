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

from __future__ import print_function
from contextlib import contextmanager
import copy
import errno
import os
import re
import string
import sys
from collections import OrderedDict


#
# Parsing the schema into expressions
#


class QAPISchemaPragma(object):
    def __init__(self):
        # Are documentation comments required?
        self.doc_required = False
        # Whitelist of commands allowed to return a non-dictionary
        self.returns_whitelist = []
        # Whitelist of entities allowed to violate case conventions
        self.name_case_whitelist = []


class QAPISourceInfo(object):
    def __init__(self, fname, line, parent):
        self.fname = fname
        self.line = line
        self.parent = parent
        self.pragma = parent.pragma if parent else QAPISchemaPragma()
        self.defn_meta = None
        self.defn_name = None

    def set_defn(self, meta, name):
        self.defn_meta = meta
        self.defn_name = name

    def next_line(self):
        info = copy.copy(self)
        info.line += 1
        return info

    def loc(self):
        if self.fname is None:
            return sys.argv[0]
        ret = self.fname
        if self.line is not None:
            ret += ':%d' % self.line
        return ret

    def in_defn(self):
        if self.defn_name:
            return "%s: In %s '%s':\n" % (self.fname,
                                          self.defn_meta, self.defn_name)
        return ''

    def include_path(self):
        ret = ''
        parent = self.parent
        while parent:
            ret = 'In file included from %s:\n' % parent.loc() + ret
            parent = parent.parent
        return ret

    def __str__(self):
        return self.include_path() + self.in_defn() + self.loc()


class QAPIError(Exception):
    def __init__(self, info, col, msg):
        Exception.__init__(self)
        self.info = info
        self.col = col
        self.msg = msg

    def __str__(self):
        loc = str(self.info)
        if self.col is not None:
            assert self.info.line is not None
            loc += ':%s' % self.col
        return loc + ': ' + self.msg


class QAPIParseError(QAPIError):
    def __init__(self, parser, msg):
        col = 1
        for ch in parser.src[parser.line_pos:parser.pos]:
            if ch == '\t':
                col = (col + 7) % 8 + 1
            else:
                col += 1
        QAPIError.__init__(self, parser.info, col, msg)


class QAPISemError(QAPIError):
    def __init__(self, info, msg):
        QAPIError.__init__(self, info, None, msg)


class QAPIDoc(object):
    """
    A documentation comment block, either definition or free-form

    Definition documentation blocks consist of

    * a body section: one line naming the definition, followed by an
      overview (any number of lines)

    * argument sections: a description of each argument (for commands
      and events) or member (for structs, unions and alternates)

    * features sections: a description of each feature flag

    * additional (non-argument) sections, possibly tagged

    Free-form documentation blocks consist only of a body section.
    """

    class Section(object):
        def __init__(self, name=None):
            # optional section name (argument/member or section name)
            self.name = name
            # the list of lines for this section
            self.text = ''

        def append(self, line):
            self.text += line.rstrip() + '\n'

    class ArgSection(Section):
        def __init__(self, name):
            QAPIDoc.Section.__init__(self, name)
            self.member = None

        def connect(self, member):
            self.member = member

    def __init__(self, parser, info):
        # self._parser is used to report errors with QAPIParseError.  The
        # resulting error position depends on the state of the parser.
        # It happens to be the beginning of the comment.  More or less
        # servicable, but action at a distance.
        self._parser = parser
        self.info = info
        self.symbol = None
        self.body = QAPIDoc.Section()
        # dict mapping parameter name to ArgSection
        self.args = OrderedDict()
        self.features = OrderedDict()
        # a list of Section
        self.sections = []
        # the current section
        self._section = self.body
        self._append_line = self._append_body_line

    def has_section(self, name):
        """Return True if we have a section with this name."""
        for i in self.sections:
            if i.name == name:
                return True
        return False

    def append(self, line):
        """
        Parse a comment line and add it to the documentation.

        The way that the line is dealt with depends on which part of
        the documentation we're parsing right now:
        * The body section: ._append_line is ._append_body_line
        * An argument section: ._append_line is ._append_args_line
        * A features section: ._append_line is ._append_features_line
        * An additional section: ._append_line is ._append_various_line
        """
        line = line[1:]
        if not line:
            self._append_freeform(line)
            return

        if line[0] != ' ':
            raise QAPIParseError(self._parser, "missing space after #")
        line = line[1:]
        self._append_line(line)

    def end_comment(self):
        self._end_section()

    @staticmethod
    def _is_section_tag(name):
        return name in ('Returns:', 'Since:',
                        # those are often singular or plural
                        'Note:', 'Notes:',
                        'Example:', 'Examples:',
                        'TODO:')

    def _append_body_line(self, line):
        """
        Process a line of documentation text in the body section.

        If this a symbol line and it is the section's first line, this
        is a definition documentation block for that symbol.

        If it's a definition documentation block, another symbol line
        begins the argument section for the argument named by it, and
        a section tag begins an additional section.  Start that
        section and append the line to it.

        Else, append the line to the current section.
        """
        name = line.split(' ', 1)[0]
        # FIXME not nice: things like '#  @foo:' and '# @foo: ' aren't
        # recognized, and get silently treated as ordinary text
        if not self.symbol and not self.body.text and line.startswith('@'):
            if not line.endswith(':'):
                raise QAPIParseError(self._parser, "line should end with ':'")
            self.symbol = line[1:-1]
            # FIXME invalid names other than the empty string aren't flagged
            if not self.symbol:
                raise QAPIParseError(self._parser, "invalid name")
        elif self.symbol:
            # This is a definition documentation block
            if name.startswith('@') and name.endswith(':'):
                self._append_line = self._append_args_line
                self._append_args_line(line)
            elif line == 'Features:':
                self._append_line = self._append_features_line
            elif self._is_section_tag(name):
                self._append_line = self._append_various_line
                self._append_various_line(line)
            else:
                self._append_freeform(line.strip())
        else:
            # This is a free-form documentation block
            self._append_freeform(line.strip())

    def _append_args_line(self, line):
        """
        Process a line of documentation text in an argument section.

        A symbol line begins the next argument section, a section tag
        section or a non-indented line after a blank line begins an
        additional section.  Start that section and append the line to
        it.

        Else, append the line to the current section.

        """
        name = line.split(' ', 1)[0]

        if name.startswith('@') and name.endswith(':'):
            line = line[len(name)+1:]
            self._start_args_section(name[1:-1])
        elif self._is_section_tag(name):
            self._append_line = self._append_various_line
            self._append_various_line(line)
            return
        elif (self._section.text.endswith('\n\n')
              and line and not line[0].isspace()):
            if line == 'Features:':
                self._append_line = self._append_features_line
            else:
                self._start_section()
                self._append_line = self._append_various_line
                self._append_various_line(line)
            return

        self._append_freeform(line.strip())

    def _append_features_line(self, line):
        name = line.split(' ', 1)[0]

        if name.startswith('@') and name.endswith(':'):
            line = line[len(name)+1:]
            self._start_features_section(name[1:-1])
        elif self._is_section_tag(name):
            self._append_line = self._append_various_line
            self._append_various_line(line)
            return
        elif (self._section.text.endswith('\n\n')
              and line and not line[0].isspace()):
            self._start_section()
            self._append_line = self._append_various_line
            self._append_various_line(line)
            return

        self._append_freeform(line.strip())

    def _append_various_line(self, line):
        """
        Process a line of documentation text in an additional section.

        A symbol line is an error.

        A section tag begins an additional section.  Start that
        section and append the line to it.

        Else, append the line to the current section.
        """
        name = line.split(' ', 1)[0]

        if name.startswith('@') and name.endswith(':'):
            raise QAPIParseError(self._parser,
                                 "'%s' can't follow '%s' section"
                                 % (name, self.sections[0].name))
        elif self._is_section_tag(name):
            line = line[len(name)+1:]
            self._start_section(name[:-1])

        if (not self._section.name or
                not self._section.name.startswith('Example')):
            line = line.strip()

        self._append_freeform(line)

    def _start_symbol_section(self, symbols_dict, name):
        # FIXME invalid names other than the empty string aren't flagged
        if not name:
            raise QAPIParseError(self._parser, "invalid parameter name")
        if name in symbols_dict:
            raise QAPIParseError(self._parser,
                                 "'%s' parameter name duplicated" % name)
        assert not self.sections
        self._end_section()
        self._section = QAPIDoc.ArgSection(name)
        symbols_dict[name] = self._section

    def _start_args_section(self, name):
        self._start_symbol_section(self.args, name)

    def _start_features_section(self, name):
        self._start_symbol_section(self.features, name)

    def _start_section(self, name=None):
        if name in ('Returns', 'Since') and self.has_section(name):
            raise QAPIParseError(self._parser,
                                 "duplicated '%s' section" % name)
        self._end_section()
        self._section = QAPIDoc.Section(name)
        self.sections.append(self._section)

    def _end_section(self):
        if self._section:
            text = self._section.text = self._section.text.strip()
            if self._section.name and (not text or text.isspace()):
                raise QAPIParseError(
                    self._parser,
                    "empty doc section '%s'" % self._section.name)
            self._section = None

    def _append_freeform(self, line):
        match = re.match(r'(@\S+:)', line)
        if match:
            raise QAPIParseError(self._parser,
                                 "'%s' not allowed in free-form documentation"
                                 % match.group(1))
        self._section.append(line)

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
        bogus = [name for name, section in self.args.items()
                 if not section.member]
        if bogus:
            raise QAPISemError(
                self.info,
                "the following documented members are not in "
                "the declaration: %s" % ", ".join(bogus))


class QAPISchemaParser(object):

    def __init__(self, fname, previously_included=[], incl_info=None):
        previously_included.append(os.path.abspath(fname))

        try:
            if sys.version_info[0] >= 3:
                fp = open(fname, 'r', encoding='utf-8')
            else:
                fp = open(fname, 'r')
            self.src = fp.read()
        except IOError as e:
            raise QAPISemError(incl_info or QAPISourceInfo(None, None, None),
                               "can't read %s file '%s': %s"
                               % ("include" if incl_info else "schema",
                                  fname,
                                  e.strerror))

        if self.src == '' or self.src[-1] != '\n':
            self.src += '\n'
        self.cursor = 0
        self.info = QAPISourceInfo(fname, 1, incl_info)
        self.line_pos = 0
        self.exprs = []
        self.docs = []
        self.accept()
        cur_doc = None

        while self.tok is not None:
            info = self.info
            if self.tok == '#':
                self.reject_expr_doc(cur_doc)
                cur_doc = self.get_doc(info)
                self.docs.append(cur_doc)
                continue

            expr = self.get_expr(False)
            if 'include' in expr:
                self.reject_expr_doc(cur_doc)
                if len(expr) != 1:
                    raise QAPISemError(info, "invalid 'include' directive")
                include = expr['include']
                if not isinstance(include, str):
                    raise QAPISemError(info,
                                       "value of 'include' must be a string")
                incl_fname = os.path.join(os.path.dirname(fname),
                                          include)
                self.exprs.append({'expr': {'include': incl_fname},
                                   'info': info})
                exprs_include = self._include(include, info, incl_fname,
                                              previously_included)
                if exprs_include:
                    self.exprs.extend(exprs_include.exprs)
                    self.docs.extend(exprs_include.docs)
            elif "pragma" in expr:
                self.reject_expr_doc(cur_doc)
                if len(expr) != 1:
                    raise QAPISemError(info, "invalid 'pragma' directive")
                pragma = expr['pragma']
                if not isinstance(pragma, dict):
                    raise QAPISemError(
                        info, "value of 'pragma' must be an object")
                for name, value in pragma.items():
                    self._pragma(name, value, info)
            else:
                expr_elem = {'expr': expr,
                             'info': info}
                if cur_doc:
                    if not cur_doc.symbol:
                        raise QAPISemError(
                            cur_doc.info, "definition documentation required")
                    expr_elem['doc'] = cur_doc
                self.exprs.append(expr_elem)
            cur_doc = None
        self.reject_expr_doc(cur_doc)

    @staticmethod
    def reject_expr_doc(doc):
        if doc and doc.symbol:
            raise QAPISemError(
                doc.info,
                "documentation for '%s' is not followed by the definition"
                % doc.symbol)

    def _include(self, include, info, incl_fname, previously_included):
        incl_abs_fname = os.path.abspath(incl_fname)
        # catch inclusion cycle
        inf = info
        while inf:
            if incl_abs_fname == os.path.abspath(inf.fname):
                raise QAPISemError(info, "inclusion loop for %s" % include)
            inf = inf.parent

        # skip multiple include of the same file
        if incl_abs_fname in previously_included:
            return None

        return QAPISchemaParser(incl_fname, previously_included, info)

    def _pragma(self, name, value, info):
        if name == 'doc-required':
            if not isinstance(value, bool):
                raise QAPISemError(info,
                                   "pragma 'doc-required' must be boolean")
            info.pragma.doc_required = value
        elif name == 'returns-whitelist':
            if (not isinstance(value, list)
                    or any([not isinstance(elt, str) for elt in value])):
                raise QAPISemError(
                    info,
                    "pragma returns-whitelist must be a list of strings")
            info.pragma.returns_whitelist = value
        elif name == 'name-case-whitelist':
            if (not isinstance(value, list)
                    or any([not isinstance(elt, str) for elt in value])):
                raise QAPISemError(
                    info,
                    "pragma name-case-whitelist must be a list of strings")
            info.pragma.name_case_whitelist = value
        else:
            raise QAPISemError(info, "unknown pragma '%s'" % name)

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
                # Note: we accept only printable ASCII
                string = ''
                esc = False
                while True:
                    ch = self.src[self.cursor]
                    self.cursor += 1
                    if ch == '\n':
                        raise QAPIParseError(self, "missing terminating \"'\"")
                    if esc:
                        # Note: we recognize only \\ because we have
                        # no use for funny characters in strings
                        if ch != '\\':
                            raise QAPIParseError(self,
                                                 "unknown escape \\%s" % ch)
                        esc = False
                    elif ch == '\\':
                        esc = True
                        continue
                    elif ch == "'":
                        self.val = string
                        return
                    if ord(ch) < 32 or ord(ch) >= 127:
                        raise QAPIParseError(
                            self, "funny character in string")
                    string += ch
            elif self.src.startswith('true', self.pos):
                self.val = True
                self.cursor += 3
                return
            elif self.src.startswith('false', self.pos):
                self.val = False
                self.cursor += 4
                return
            elif self.tok == '\n':
                if self.cursor == len(self.src):
                    self.tok = None
                    return
                self.info = self.info.next_line()
                self.line_pos = self.cursor
            elif not self.tok.isspace():
                # Show up to next structural, whitespace or quote
                # character
                match = re.match('[^[\\]{}:,\\s\'"]+',
                                 self.src[self.cursor-1:])
                raise QAPIParseError(self, "stray '%s'" % match.group(0))

    def get_members(self):
        expr = OrderedDict()
        if self.tok == '}':
            self.accept()
            return expr
        if self.tok != "'":
            raise QAPIParseError(self, "expected string or '}'")
        while True:
            key = self.val
            self.accept()
            if self.tok != ':':
                raise QAPIParseError(self, "expected ':'")
            self.accept()
            if key in expr:
                raise QAPIParseError(self, "duplicate key '%s'" % key)
            expr[key] = self.get_expr(True)
            if self.tok == '}':
                self.accept()
                return expr
            if self.tok != ',':
                raise QAPIParseError(self, "expected ',' or '}'")
            self.accept()
            if self.tok != "'":
                raise QAPIParseError(self, "expected string")

    def get_values(self):
        expr = []
        if self.tok == ']':
            self.accept()
            return expr
        if self.tok not in "{['tfn":
            raise QAPIParseError(
                self, "expected '{', '[', ']', string, boolean or 'null'")
        while True:
            expr.append(self.get_expr(True))
            if self.tok == ']':
                self.accept()
                return expr
            if self.tok != ',':
                raise QAPIParseError(self, "expected ',' or ']'")
            self.accept()

    def get_expr(self, nested):
        if self.tok != '{' and not nested:
            raise QAPIParseError(self, "expected '{'")
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
            raise QAPIParseError(
                self, "expected '{', '[', string, boolean or 'null'")
        return expr

    def get_doc(self, info):
        if self.val != '##':
            raise QAPIParseError(
                self, "junk after '##' at start of documentation comment")

        doc = QAPIDoc(self, info)
        self.accept(False)
        while self.tok == '#':
            if self.val.startswith('##'):
                # End of doc comment
                if self.val != '##':
                    raise QAPIParseError(
                        self,
                        "junk after '##' at end of documentation comment")
                doc.end_comment()
                self.accept()
                return doc
            else:
                doc.append(self.val)
            self.accept(False)

        raise QAPIParseError(self, "documentation comment must end with '##'")


#
# Check (context-free) schema expression structure
#

# Names must be letters, numbers, -, and _.  They must start with letter,
# except for downstream extensions which must start with __RFQDN_.
# Dots are only valid in the downstream extension prefix.
valid_name = re.compile(r'^(__[a-zA-Z0-9.-]+_)?'
                        '[a-zA-Z][a-zA-Z0-9_-]*$')


def check_name_is_str(name, info, source):
    if not isinstance(name, str):
        raise QAPISemError(info, "%s requires a string name" % source)


def check_name_str(name, info, source,
                   allow_optional=False, enum_member=False,
                   permit_upper=False):
    global valid_name
    membername = name

    if allow_optional and name.startswith('*'):
        membername = name[1:]
    # Enum members can start with a digit, because the generated C
    # code always prefixes it with the enum name
    if enum_member and membername[0].isdigit():
        membername = 'D' + membername
    # Reserve the entire 'q_' namespace for c_name(), and for 'q_empty'
    # and 'q_obj_*' implicit type names.
    if not valid_name.match(membername) or \
       c_name(membername, False).startswith('q_'):
        raise QAPISemError(info, "%s has an invalid name" % source)
    if not permit_upper and name.lower() != name:
        raise QAPISemError(
            info, "%s uses uppercase in name" % source)
    assert not membername.startswith('*')


def check_defn_name_str(name, info, meta):
    check_name_str(name, info, meta, permit_upper=True)
    if name.endswith('Kind') or name.endswith('List'):
        raise QAPISemError(
            info, "%s name should not end in '%s'" % (meta, name[-4:]))


def check_if(expr, info, source):

    def check_if_str(ifcond, info):
        if not isinstance(ifcond, str):
            raise QAPISemError(
                info,
                "'if' condition of %s must be a string or a list of strings"
                % source)
        if ifcond.strip() == '':
            raise QAPISemError(
                info,
                "'if' condition '%s' of %s makes no sense"
                % (ifcond, source))

    ifcond = expr.get('if')
    if ifcond is None:
        return
    if isinstance(ifcond, list):
        if ifcond == []:
            raise QAPISemError(
                info, "'if' condition [] of %s is useless" % source)
        for elt in ifcond:
            check_if_str(elt, info)
    else:
        check_if_str(ifcond, info)


def check_type(value, info, source,
               allow_array=False, allow_dict=False):
    if value is None:
        return

    # Array type
    if isinstance(value, list):
        if not allow_array:
            raise QAPISemError(info, "%s cannot be an array" % source)
        if len(value) != 1 or not isinstance(value[0], str):
            raise QAPISemError(info,
                               "%s: array type must contain single type name" %
                               source)
        return

    # Type name
    if isinstance(value, str):
        return

    # Anonymous type

    if not allow_dict:
        raise QAPISemError(info, "%s should be a type name" % source)

    if not isinstance(value, OrderedDict):
        raise QAPISemError(info,
                           "%s should be an object or type name" % source)

    permit_upper = allow_dict in info.pragma.name_case_whitelist

    # value is a dictionary, check that each member is okay
    for (key, arg) in value.items():
        key_source = "%s member '%s'" % (source, key)
        check_name_str(key, info, key_source,
                       allow_optional=True, permit_upper=permit_upper)
        if c_name(key, False) == 'u' or c_name(key, False).startswith('has_'):
            raise QAPISemError(info, "%s uses reserved name" % key_source)
        check_keys(arg, info, key_source, ['type'], ['if'])
        check_if(arg, info, key_source)
        normalize_if(arg)
        check_type(arg['type'], info, key_source, allow_array=True)


def check_command(expr, info):
    args = expr.get('data')
    rets = expr.get('returns')
    boxed = expr.get('boxed', False)

    if boxed and args is None:
        raise QAPISemError(info, "'boxed': true requires 'data'")
    check_type(args, info, "'data'", allow_dict=not boxed)
    check_type(rets, info, "'returns'", allow_array=True)


def check_event(expr, info):
    args = expr.get('data')
    boxed = expr.get('boxed', False)

    if boxed and args is None:
        raise QAPISemError(info, "'boxed': true requires 'data'")
    check_type(args, info, "'data'", allow_dict=not boxed)


def check_union(expr, info):
    name = expr['union']
    base = expr.get('base')
    discriminator = expr.get('discriminator')
    members = expr['data']

    if discriminator is None:   # simple union
        if base is not None:
            raise QAPISemError(info, "'base' requires 'discriminator'")
    else:                       # flat union
        check_type(base, info, "'base'", allow_dict=name)
        if not base:
            raise QAPISemError(info, "'discriminator' requires 'base'")
        check_name_is_str(discriminator, info, "'discriminator'")

    for (key, value) in members.items():
        source = "'data' member '%s'" % key
        check_name_str(key, info, source)
        check_keys(value, info, source, ['type'], ['if'])
        check_if(value, info, source)
        normalize_if(value)
        check_type(value['type'], info, source, allow_array=not base)


def check_alternate(expr, info):
    members = expr['data']

    if len(members) == 0:
        raise QAPISemError(info, "'data' must not be empty")
    for (key, value) in members.items():
        source = "'data' member '%s'" % key
        check_name_str(key, info, source)
        check_keys(value, info, source, ['type'], ['if'])
        check_if(value, info, source)
        normalize_if(value)
        check_type(value['type'], info, source)


def check_enum(expr, info):
    name = expr['enum']
    members = expr['data']
    prefix = expr.get('prefix')

    if not isinstance(members, list):
        raise QAPISemError(info, "'data' must be an array")
    if prefix is not None and not isinstance(prefix, str):
        raise QAPISemError(info, "'prefix' must be a string")

    permit_upper = name in info.pragma.name_case_whitelist

    for member in members:
        source = "'data' member"
        check_keys(member, info, source, ['name'], ['if'])
        check_name_is_str(member['name'], info, source)
        source = "%s '%s'" % (source, member['name'])
        check_name_str(member['name'], info, source,
                       enum_member=True, permit_upper=permit_upper)
        check_if(member, info, source)
        normalize_if(member)


def check_struct(expr, info):
    name = expr['struct']
    members = expr['data']
    features = expr.get('features')

    check_type(members, info, "'data'", allow_dict=name)
    check_type(expr.get('base'), info, "'base'")

    if features:
        if not isinstance(features, list):
            raise QAPISemError(info, "'features' must be an array")
        for f in features:
            source = "'features' member"
            assert isinstance(f, dict)
            check_keys(f, info, source, ['name'], ['if'])
            check_name_is_str(f['name'], info, source)
            source = "%s '%s'" % (source, f['name'])
            check_name_str(f['name'], info, source)
            check_if(f, info, source)
            normalize_if(f)


def check_keys(value, info, source, required, optional):

    def pprint(elems):
        return ', '.join("'" + e + "'" for e in sorted(elems))

    missing = set(required) - set(value)
    if missing:
        raise QAPISemError(
            info,
            "%s misses key%s %s"
            % (source, 's' if len(missing) > 1 else '',
               pprint(missing)))
    allowed = set(required + optional)
    unknown = set(value) - allowed
    if unknown:
        raise QAPISemError(
            info,
            "%s has unknown key%s %s\nValid keys are %s."
            % (source, 's' if len(unknown) > 1 else '',
               pprint(unknown), pprint(allowed)))


def check_flags(expr, info):
    for key in ['gen', 'success-response']:
        if key in expr and expr[key] is not False:
            raise QAPISemError(
                info, "flag '%s' may only use false value" % key)
    for key in ['boxed', 'allow-oob', 'allow-preconfig']:
        if key in expr and expr[key] is not True:
            raise QAPISemError(
                info, "flag '%s' may only use true value" % key)


def normalize_enum(expr):
    if isinstance(expr['data'], list):
        expr['data'] = [m if isinstance(m, dict) else {'name': m}
                        for m in expr['data']]


def normalize_members(members):
    if isinstance(members, OrderedDict):
        for key, arg in members.items():
            if isinstance(arg, dict):
                continue
            members[key] = {'type': arg}


def normalize_features(features):
    if isinstance(features, list):
        features[:] = [f if isinstance(f, dict) else {'name': f}
                       for f in features]


def normalize_if(expr):
    ifcond = expr.get('if')
    if isinstance(ifcond, str):
        expr['if'] = [ifcond]


def check_exprs(exprs):
    for expr_elem in exprs:
        expr = expr_elem['expr']
        info = expr_elem['info']
        doc = expr_elem.get('doc')

        if 'include' in expr:
            continue

        if 'enum' in expr:
            meta = 'enum'
        elif 'union' in expr:
            meta = 'union'
        elif 'alternate' in expr:
            meta = 'alternate'
        elif 'struct' in expr:
            meta = 'struct'
        elif 'command' in expr:
            meta = 'command'
        elif 'event' in expr:
            meta = 'event'
        else:
            raise QAPISemError(info, "expression is missing metatype")

        name = expr[meta]
        check_name_is_str(name, info, "'%s'" % meta)
        info.set_defn(meta, name)
        check_defn_name_str(name, info, meta)

        if doc:
            if doc.symbol != name:
                raise QAPISemError(
                    info, "documentation comment is for '%s'" % doc.symbol)
            doc.check_expr(expr)
        elif info.pragma.doc_required:
            raise QAPISemError(info,
                               "documentation comment required")

        if meta == 'enum':
            check_keys(expr, info, meta,
                       ['enum', 'data'], ['if', 'prefix'])
            normalize_enum(expr)
            check_enum(expr, info)
        elif meta == 'union':
            check_keys(expr, info, meta,
                       ['union', 'data'],
                       ['base', 'discriminator', 'if'])
            normalize_members(expr.get('base'))
            normalize_members(expr['data'])
            check_union(expr, info)
        elif meta == 'alternate':
            check_keys(expr, info, meta,
                       ['alternate', 'data'], ['if'])
            normalize_members(expr['data'])
            check_alternate(expr, info)
        elif meta == 'struct':
            check_keys(expr, info, meta,
                       ['struct', 'data'], ['base', 'if', 'features'])
            normalize_members(expr['data'])
            normalize_features(expr.get('features'))
            check_struct(expr, info)
        elif meta == 'command':
            check_keys(expr, info, meta,
                       ['command'],
                       ['data', 'returns', 'boxed', 'if',
                        'gen', 'success-response', 'allow-oob',
                        'allow-preconfig'])
            normalize_members(expr.get('data'))
            check_command(expr, info)
        elif meta == 'event':
            check_keys(expr, info, meta,
                       ['event'], ['data', 'boxed', 'if'])
            normalize_members(expr.get('data'))
            check_event(expr, info)
        else:
            assert False, 'unexpected meta type'

        normalize_if(expr)
        check_if(expr, info, meta)
        check_flags(expr, info)

    return exprs


#
# Schema compiler frontend
# TODO catching name collisions in generated code would be nice
#

class QAPISchemaEntity(object):
    meta = None

    def __init__(self, name, info, doc, ifcond=None):
        assert name is None or isinstance(name, str)
        self.name = name
        self._module = None
        # For explicitly defined entities, info points to the (explicit)
        # definition.  For builtins (and their arrays), info is None.
        # For implicitly defined entities, info points to a place that
        # triggered the implicit definition (there may be more than one
        # such place).
        self.info = info
        self.doc = doc
        self._ifcond = ifcond or []
        self._checked = False

    def c_name(self):
        return c_name(self.name)

    def check(self, schema):
        assert not self._checked
        if self.info:
            self._module = os.path.relpath(self.info.fname,
                                           os.path.dirname(schema.fname))
        self._checked = True

    @property
    def ifcond(self):
        assert self._checked
        return self._ifcond

    @property
    def module(self):
        assert self._checked
        return self._module

    def is_implicit(self):
        return not self.info

    def visit(self, visitor):
        assert self._checked

    def describe(self):
        assert self.meta
        return "%s '%s'" % (self.meta, self.name)


class QAPISchemaVisitor(object):
    def visit_begin(self, schema):
        pass

    def visit_end(self):
        pass

    def visit_module(self, fname):
        pass

    def visit_needed(self, entity):
        # Default to visiting everything
        return True

    def visit_include(self, fname, info):
        pass

    def visit_builtin_type(self, name, info, json_type):
        pass

    def visit_enum_type(self, name, info, ifcond, members, prefix):
        pass

    def visit_array_type(self, name, info, ifcond, element_type):
        pass

    def visit_object_type(self, name, info, ifcond, base, members, variants,
                          features):
        pass

    def visit_object_type_flat(self, name, info, ifcond, members, variants,
                               features):
        pass

    def visit_alternate_type(self, name, info, ifcond, variants):
        pass

    def visit_command(self, name, info, ifcond, arg_type, ret_type, gen,
                      success_response, boxed, allow_oob, allow_preconfig):
        pass

    def visit_event(self, name, info, ifcond, arg_type, boxed):
        pass


class QAPISchemaInclude(QAPISchemaEntity):

    def __init__(self, fname, info):
        QAPISchemaEntity.__init__(self, None, info, None)
        self.fname = fname

    def visit(self, visitor):
        QAPISchemaEntity.visit(self, visitor)
        visitor.visit_include(self.fname, self.info)


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

    def describe(self):
        assert self.meta
        return "%s type '%s'" % (self.meta, self.name)


class QAPISchemaBuiltinType(QAPISchemaType):
    meta = 'built-in'

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
        QAPISchemaType.visit(self, visitor)
        visitor.visit_builtin_type(self.name, self.info, self.json_type())


class QAPISchemaEnumType(QAPISchemaType):
    meta = 'enum'

    def __init__(self, name, info, doc, ifcond, members, prefix):
        QAPISchemaType.__init__(self, name, info, doc, ifcond)
        for m in members:
            assert isinstance(m, QAPISchemaEnumMember)
            m.set_defined_in(name)
        assert prefix is None or isinstance(prefix, str)
        self.members = members
        self.prefix = prefix

    def check(self, schema):
        QAPISchemaType.check(self, schema)
        seen = {}
        for m in self.members:
            m.check_clash(self.info, seen)
            if self.doc:
                self.doc.connect_member(m)

    def is_implicit(self):
        # See QAPISchema._make_implicit_enum_type() and ._def_predefineds()
        return self.name.endswith('Kind') or self.name == 'QType'

    def c_type(self):
        return c_name(self.name)

    def member_names(self):
        return [m.name for m in self.members]

    def json_type(self):
        return 'string'

    def visit(self, visitor):
        QAPISchemaType.visit(self, visitor)
        visitor.visit_enum_type(self.name, self.info, self.ifcond,
                                self.members, self.prefix)


class QAPISchemaArrayType(QAPISchemaType):
    meta = 'array'

    def __init__(self, name, info, element_type):
        QAPISchemaType.__init__(self, name, info, None, None)
        assert isinstance(element_type, str)
        self._element_type_name = element_type
        self.element_type = None

    def check(self, schema):
        QAPISchemaType.check(self, schema)
        self.element_type = schema.resolve_type(
            self._element_type_name, self.info,
            self.info and self.info.defn_meta)
        assert not isinstance(self.element_type, QAPISchemaArrayType)

    @property
    def ifcond(self):
        assert self._checked
        return self.element_type.ifcond

    @property
    def module(self):
        assert self._checked
        return self.element_type.module

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
        QAPISchemaType.visit(self, visitor)
        visitor.visit_array_type(self.name, self.info, self.ifcond,
                                 self.element_type)

    def describe(self):
        assert self.meta
        return "%s type ['%s']" % (self.meta, self._element_type_name)


class QAPISchemaObjectType(QAPISchemaType):
    def __init__(self, name, info, doc, ifcond,
                 base, local_members, variants, features):
        # struct has local_members, optional base, and no variants
        # flat union has base, variants, and no local_members
        # simple union has local_members, variants, and no base
        QAPISchemaType.__init__(self, name, info, doc, ifcond)
        self.meta = 'union' if variants else 'struct'
        assert base is None or isinstance(base, str)
        for m in local_members:
            assert isinstance(m, QAPISchemaObjectTypeMember)
            m.set_defined_in(name)
        if variants is not None:
            assert isinstance(variants, QAPISchemaObjectTypeVariants)
            variants.set_defined_in(name)
        for f in features:
            assert isinstance(f, QAPISchemaFeature)
            f.set_defined_in(name)
        self._base_name = base
        self.base = None
        self.local_members = local_members
        self.variants = variants
        self.members = None
        self.features = features

    def check(self, schema):
        # This calls another type T's .check() exactly when the C
        # struct emitted by gen_object() contains that T's C struct
        # (pointers don't count).
        if self.members is not None:
            # A previous .check() completed: nothing to do
            return
        if self._checked:
            # Recursed: C struct contains itself
            raise QAPISemError(self.info,
                               "object %s contains itself" % self.name)

        QAPISchemaType.check(self, schema)
        assert self._checked and self.members is None

        seen = OrderedDict()
        if self._base_name:
            self.base = schema.resolve_type(self._base_name, self.info,
                                            "'base'")
            if (not isinstance(self.base, QAPISchemaObjectType)
                    or self.base.variants):
                raise QAPISemError(
                    self.info,
                    "'base' requires a struct type, %s isn't"
                    % self.base.describe())
            self.base.check(schema)
            self.base.check_clash(self.info, seen)
        for m in self.local_members:
            m.check(schema)
            m.check_clash(self.info, seen)
            if self.doc:
                self.doc.connect_member(m)
        members = seen.values()

        if self.variants:
            self.variants.check(schema, seen)
            self.variants.check_clash(self.info, seen)

        # Features are in a name space separate from members
        seen = {}
        for f in self.features:
            f.check_clash(self.info, seen)

        if self.doc:
            self.doc.check()

        self.members = members  # mark completed

    # Check that the members of this type do not cause duplicate JSON members,
    # and update seen to track the members seen so far. Report any errors
    # on behalf of info, which is not necessarily self.info
    def check_clash(self, info, seen):
        assert self._checked
        assert not self.variants       # not implemented
        for m in self.members:
            m.check_clash(info, seen)

    @property
    def ifcond(self):
        assert self._checked
        if isinstance(self._ifcond, QAPISchemaType):
            # Simple union wrapper type inherits from wrapped type;
            # see _make_implicit_object_type()
            return self._ifcond.ifcond
        return self._ifcond

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
        QAPISchemaType.visit(self, visitor)
        visitor.visit_object_type(self.name, self.info, self.ifcond,
                                  self.base, self.local_members, self.variants,
                                  self.features)
        visitor.visit_object_type_flat(self.name, self.info, self.ifcond,
                                       self.members, self.variants,
                                       self.features)


class QAPISchemaMember(object):
    """ Represents object members, enum members and features """
    role = 'member'

    def __init__(self, name, info, ifcond=None):
        assert isinstance(name, str)
        self.name = name
        self.info = info
        self.ifcond = ifcond or []
        self.defined_in = None

    def set_defined_in(self, name):
        assert not self.defined_in
        self.defined_in = name

    def check_clash(self, info, seen):
        cname = c_name(self.name)
        if cname in seen:
            raise QAPISemError(
                info,
                "%s collides with %s"
                % (self.describe(info), seen[cname].describe(info)))
        seen[cname] = self

    def describe(self, info):
        role = self.role
        defined_in = self.defined_in
        assert defined_in

        if defined_in.startswith('q_obj_'):
            # See QAPISchema._make_implicit_object_type() - reverse the
            # mapping there to create a nice human-readable description
            defined_in = defined_in[6:]
            if defined_in.endswith('-arg'):
                # Implicit type created for a command's dict 'data'
                assert role == 'member'
                role = 'parameter'
            elif defined_in.endswith('-base'):
                # Implicit type created for a flat union's dict 'base'
                role = 'base ' + role
            else:
                # Implicit type created for a simple union's branch
                assert defined_in.endswith('-wrapper')
                # Unreachable and not implemented
                assert False
        elif defined_in.endswith('Kind'):
            # See QAPISchema._make_implicit_enum_type()
            # Implicit enum created for simple union's branches
            assert role == 'value'
            role = 'branch'
        elif defined_in != info.defn_name:
            return "%s '%s' of type '%s'" % (role, self.name, defined_in)
        return "%s '%s'" % (role, self.name)


class QAPISchemaEnumMember(QAPISchemaMember):
    role = 'value'


class QAPISchemaFeature(QAPISchemaMember):
    role = 'feature'


class QAPISchemaObjectTypeMember(QAPISchemaMember):
    def __init__(self, name, info, typ, optional, ifcond=None):
        QAPISchemaMember.__init__(self, name, info, ifcond)
        assert isinstance(typ, str)
        assert isinstance(optional, bool)
        self._type_name = typ
        self.type = None
        self.optional = optional

    def check(self, schema):
        assert self.defined_in
        self.type = schema.resolve_type(self._type_name, self.info,
                                        self.describe)


class QAPISchemaObjectTypeVariants(object):
    def __init__(self, tag_name, info, tag_member, variants):
        # Flat unions pass tag_name but not tag_member.
        # Simple unions and alternates pass tag_member but not tag_name.
        # After check(), tag_member is always set, and tag_name remains
        # a reliable witness of being used by a flat union.
        assert bool(tag_member) != bool(tag_name)
        assert (isinstance(tag_name, str) or
                isinstance(tag_member, QAPISchemaObjectTypeMember))
        for v in variants:
            assert isinstance(v, QAPISchemaObjectTypeVariant)
        self._tag_name = tag_name
        self.info = info
        self.tag_member = tag_member
        self.variants = variants

    def set_defined_in(self, name):
        for v in self.variants:
            v.set_defined_in(name)

    def check(self, schema, seen):
        if not self.tag_member: # flat union
            self.tag_member = seen.get(c_name(self._tag_name))
            base = "'base'"
            # Pointing to the base type when not implicit would be
            # nice, but we don't know it here
            if not self.tag_member or self._tag_name != self.tag_member.name:
                raise QAPISemError(
                    self.info,
                    "discriminator '%s' is not a member of %s"
                    % (self._tag_name, base))
            # Here we do:
            base_type = schema.lookup_type(self.tag_member.defined_in)
            assert base_type
            if not base_type.is_implicit():
                base = "base type '%s'" % self.tag_member.defined_in
            if not isinstance(self.tag_member.type, QAPISchemaEnumType):
                raise QAPISemError(
                    self.info,
                    "discriminator member '%s' of %s must be of enum type"
                    % (self._tag_name, base))
            if self.tag_member.optional:
                raise QAPISemError(
                    self.info,
                    "discriminator member '%s' of %s must not be optional"
                    % (self._tag_name, base))
            if self.tag_member.ifcond:
                raise QAPISemError(
                    self.info,
                    "discriminator member '%s' of %s must not be conditional"
                    % (self._tag_name, base))
        else:                   # simple union
            assert isinstance(self.tag_member.type, QAPISchemaEnumType)
            assert not self.tag_member.optional
            assert self.tag_member.ifcond == []
        if self._tag_name:    # flat union
            # branches that are not explicitly covered get an empty type
            cases = set([v.name for v in self.variants])
            for m in self.tag_member.type.members:
                if m.name not in cases:
                    v = QAPISchemaObjectTypeVariant(m.name, self.info,
                                                    'q_empty', m.ifcond)
                    v.set_defined_in(self.tag_member.defined_in)
                    self.variants.append(v)
        if not self.variants:
            raise QAPISemError(self.info, "union has no branches")
        for v in self.variants:
            v.check(schema)
            # Union names must match enum values; alternate names are
            # checked separately. Use 'seen' to tell the two apart.
            if seen:
                if v.name not in self.tag_member.type.member_names():
                    raise QAPISemError(
                        self.info,
                        "branch '%s' is not a value of %s"
                        % (v.name, self.tag_member.type.describe()))
                if (not isinstance(v.type, QAPISchemaObjectType)
                        or v.type.variants):
                    raise QAPISemError(
                        self.info,
                        "%s cannot use %s"
                        % (v.describe(self.info), v.type.describe()))
                v.type.check(schema)

    def check_clash(self, info, seen):
        for v in self.variants:
            # Reset seen map for each variant, since qapi names from one
            # branch do not affect another branch
            v.type.check_clash(info, dict(seen))


class QAPISchemaObjectTypeVariant(QAPISchemaObjectTypeMember):
    role = 'branch'

    def __init__(self, name, info, typ, ifcond=None):
        QAPISchemaObjectTypeMember.__init__(self, name, info, typ,
                                            False, ifcond)


class QAPISchemaAlternateType(QAPISchemaType):
    meta = 'alternate'

    def __init__(self, name, info, doc, ifcond, variants):
        QAPISchemaType.__init__(self, name, info, doc, ifcond)
        assert isinstance(variants, QAPISchemaObjectTypeVariants)
        assert variants.tag_member
        variants.set_defined_in(name)
        variants.tag_member.set_defined_in(self.name)
        self.variants = variants

    def check(self, schema):
        QAPISchemaType.check(self, schema)
        self.variants.tag_member.check(schema)
        # Not calling self.variants.check_clash(), because there's nothing
        # to clash with
        self.variants.check(schema, {})
        # Alternate branch names have no relation to the tag enum values;
        # so we have to check for potential name collisions ourselves.
        seen = {}
        types_seen = {}
        for v in self.variants.variants:
            v.check_clash(self.info, seen)
            qtype = v.type.alternate_qtype()
            if not qtype:
                raise QAPISemError(
                    self.info,
                    "%s cannot use %s"
                    % (v.describe(self.info), v.type.describe()))
            conflicting = set([qtype])
            if qtype == 'QTYPE_QSTRING':
                if isinstance(v.type, QAPISchemaEnumType):
                    for m in v.type.members:
                        if m.name in ['on', 'off']:
                            conflicting.add('QTYPE_QBOOL')
                        if re.match(r'[-+0-9.]', m.name):
                            # lazy, could be tightened
                            conflicting.add('QTYPE_QNUM')
                else:
                    conflicting.add('QTYPE_QNUM')
                    conflicting.add('QTYPE_QBOOL')
            for qt in conflicting:
                if qt in types_seen:
                    raise QAPISemError(
                        self.info,
                        "%s can't be distinguished from '%s'"
                        % (v.describe(self.info), types_seen[qt]))
                types_seen[qt] = v.name
            if self.doc:
                self.doc.connect_member(v)
        if self.doc:
            self.doc.check()

    def c_type(self):
        return c_name(self.name) + pointer_suffix

    def json_type(self):
        return 'value'

    def visit(self, visitor):
        QAPISchemaType.visit(self, visitor)
        visitor.visit_alternate_type(self.name, self.info, self.ifcond,
                                     self.variants)


class QAPISchemaCommand(QAPISchemaEntity):
    meta = 'command'

    def __init__(self, name, info, doc, ifcond, arg_type, ret_type,
                 gen, success_response, boxed, allow_oob, allow_preconfig):
        QAPISchemaEntity.__init__(self, name, info, doc, ifcond)
        assert not arg_type or isinstance(arg_type, str)
        assert not ret_type or isinstance(ret_type, str)
        self._arg_type_name = arg_type
        self.arg_type = None
        self._ret_type_name = ret_type
        self.ret_type = None
        self.gen = gen
        self.success_response = success_response
        self.boxed = boxed
        self.allow_oob = allow_oob
        self.allow_preconfig = allow_preconfig

    def check(self, schema):
        QAPISchemaEntity.check(self, schema)
        if self._arg_type_name:
            self.arg_type = schema.resolve_type(
                self._arg_type_name, self.info, "command's 'data'")
            if not isinstance(self.arg_type, QAPISchemaObjectType):
                raise QAPISemError(
                    self.info,
                    "command's 'data' cannot take %s"
                    % self.arg_type.describe())
            if self.arg_type.variants and not self.boxed:
                raise QAPISemError(
                    self.info,
                    "command's 'data' can take %s only with 'boxed': true"
                    % self.arg_type.describe())
        if self._ret_type_name:
            self.ret_type = schema.resolve_type(
                self._ret_type_name, self.info, "command's 'returns'")
            if self.name not in self.info.pragma.returns_whitelist:
                if not (isinstance(self.ret_type, QAPISchemaObjectType)
                        or (isinstance(self.ret_type, QAPISchemaArrayType)
                            and isinstance(self.ret_type.element_type,
                                           QAPISchemaObjectType))):
                    raise QAPISemError(
                        self.info,
                        "command's 'returns' cannot take %s"
                        % self.ret_type.describe())

    def visit(self, visitor):
        QAPISchemaEntity.visit(self, visitor)
        visitor.visit_command(self.name, self.info, self.ifcond,
                              self.arg_type, self.ret_type,
                              self.gen, self.success_response,
                              self.boxed, self.allow_oob,
                              self.allow_preconfig)


class QAPISchemaEvent(QAPISchemaEntity):
    meta = 'event'

    def __init__(self, name, info, doc, ifcond, arg_type, boxed):
        QAPISchemaEntity.__init__(self, name, info, doc, ifcond)
        assert not arg_type or isinstance(arg_type, str)
        self._arg_type_name = arg_type
        self.arg_type = None
        self.boxed = boxed

    def check(self, schema):
        QAPISchemaEntity.check(self, schema)
        if self._arg_type_name:
            self.arg_type = schema.resolve_type(
                self._arg_type_name, self.info, "event's 'data'")
            if not isinstance(self.arg_type, QAPISchemaObjectType):
                raise QAPISemError(
                    self.info,
                    "event's 'data' cannot take %s"
                    % self.arg_type.describe())
            if self.arg_type.variants and not self.boxed:
                raise QAPISemError(
                    self.info,
                    "event's 'data' can take %s only with 'boxed': true"
                    % self.arg_type.describe())

    def visit(self, visitor):
        QAPISchemaEntity.visit(self, visitor)
        visitor.visit_event(self.name, self.info, self.ifcond,
                            self.arg_type, self.boxed)


class QAPISchema(object):
    def __init__(self, fname):
        self.fname = fname
        parser = QAPISchemaParser(fname)
        exprs = check_exprs(parser.exprs)
        self.docs = parser.docs
        self._entity_list = []
        self._entity_dict = {}
        self._predefining = True
        self._def_predefineds()
        self._predefining = False
        self._def_exprs(exprs)
        self.check()

    def _def_entity(self, ent):
        # Only the predefined types are allowed to not have info
        assert ent.info or self._predefining
        self._entity_list.append(ent)
        if ent.name is None:
            return
        # TODO reject names that differ only in '_' vs. '.'  vs. '-',
        # because they're liable to clash in generated C.
        other_ent = self._entity_dict.get(ent.name)
        if other_ent:
            if other_ent.info:
                where = QAPIError(other_ent.info, None, "previous definition")
                raise QAPISemError(
                    ent.info,
                    "'%s' is already defined\n%s" % (ent.name, where))
            raise QAPISemError(
                ent.info, "%s is already defined" % other_ent.describe())
        self._entity_dict[ent.name] = ent

    def lookup_entity(self, name, typ=None):
        ent = self._entity_dict.get(name)
        if typ and not isinstance(ent, typ):
            return None
        return ent

    def lookup_type(self, name):
        return self.lookup_entity(name, QAPISchemaType)

    def resolve_type(self, name, info, what):
        typ = self.lookup_type(name)
        if not typ:
            if callable(what):
                what = what(info)
            raise QAPISemError(
                info, "%s uses unknown type '%s'" % (what, name))
        return typ

    def _def_include(self, expr, info, doc):
        include = expr['include']
        assert doc is None
        main_info = info
        while main_info.parent:
            main_info = main_info.parent
        fname = os.path.relpath(include, os.path.dirname(main_info.fname))
        self._def_entity(QAPISchemaInclude(fname, info))

    def _def_builtin_type(self, name, json_type, c_type):
        self._def_entity(QAPISchemaBuiltinType(name, json_type, c_type))
        # Instantiating only the arrays that are actually used would
        # be nice, but we can't as long as their generated code
        # (qapi-builtin-types.[ch]) may be shared by some other
        # schema.
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
            'q_empty', None, None, None, None, [], None, [])
        self._def_entity(self.the_empty_object_type)

        qtypes = ['none', 'qnull', 'qnum', 'qstring', 'qdict', 'qlist',
                  'qbool']
        qtype_values = self._make_enum_members(
            [{'name': n} for n in qtypes], None)

        self._def_entity(QAPISchemaEnumType('QType', None, None, None,
                                            qtype_values, 'QTYPE'))

    def _make_features(self, features, info):
        return [QAPISchemaFeature(f['name'], info, f.get('if'))
                for f in features]

    def _make_enum_members(self, values, info):
        return [QAPISchemaEnumMember(v['name'], info, v.get('if'))
                for v in values]

    def _make_implicit_enum_type(self, name, info, ifcond, values):
        # See also QAPISchemaObjectTypeMember.describe()
        name = name + 'Kind'    # reserved by check_defn_name_str()
        self._def_entity(QAPISchemaEnumType(
            name, info, None, ifcond, self._make_enum_members(values, info),
            None))
        return name

    def _make_array_type(self, element_type, info):
        name = element_type + 'List'    # reserved by check_defn_name_str()
        if not self.lookup_type(name):
            self._def_entity(QAPISchemaArrayType(name, info, element_type))
        return name

    def _make_implicit_object_type(self, name, info, doc, ifcond,
                                   role, members):
        if not members:
            return None
        # See also QAPISchemaObjectTypeMember.describe()
        name = 'q_obj_%s-%s' % (name, role)
        typ = self.lookup_entity(name, QAPISchemaObjectType)
        if typ:
            # The implicit object type has multiple users.  This can
            # happen only for simple unions' implicit wrapper types.
            # Its ifcond should be the disjunction of its user's
            # ifconds.  Not implemented.  Instead, we always pass the
            # wrapped type's ifcond, which is trivially the same for all
            # users.  It's also necessary for the wrapper to compile.
            # But it's not tight: the disjunction need not imply it.  We
            # may end up compiling useless wrapper types.
            # TODO kill simple unions or implement the disjunction
            assert (ifcond or []) == typ._ifcond # pylint: disable=protected-access
        else:
            self._def_entity(QAPISchemaObjectType(name, info, doc, ifcond,
                                                  None, members, None, []))
        return name

    def _def_enum_type(self, expr, info, doc):
        name = expr['enum']
        data = expr['data']
        prefix = expr.get('prefix')
        ifcond = expr.get('if')
        self._def_entity(QAPISchemaEnumType(
            name, info, doc, ifcond,
            self._make_enum_members(data, info), prefix))

    def _make_member(self, name, typ, ifcond, info):
        optional = False
        if name.startswith('*'):
            name = name[1:]
            optional = True
        if isinstance(typ, list):
            assert len(typ) == 1
            typ = self._make_array_type(typ[0], info)
        return QAPISchemaObjectTypeMember(name, info, typ, optional, ifcond)

    def _make_members(self, data, info):
        return [self._make_member(key, value['type'], value.get('if'), info)
                for (key, value) in data.items()]

    def _def_struct_type(self, expr, info, doc):
        name = expr['struct']
        base = expr.get('base')
        data = expr['data']
        ifcond = expr.get('if')
        features = expr.get('features', [])
        self._def_entity(QAPISchemaObjectType(
            name, info, doc, ifcond, base,
            self._make_members(data, info),
            None,
            self._make_features(features, info)))

    def _make_variant(self, case, typ, ifcond, info):
        return QAPISchemaObjectTypeVariant(case, info, typ, ifcond)

    def _make_simple_variant(self, case, typ, ifcond, info):
        if isinstance(typ, list):
            assert len(typ) == 1
            typ = self._make_array_type(typ[0], info)
        typ = self._make_implicit_object_type(
            typ, info, None, self.lookup_type(typ),
            'wrapper', [self._make_member('data', typ, None, info)])
        return QAPISchemaObjectTypeVariant(case, info, typ, ifcond)

    def _def_union_type(self, expr, info, doc):
        name = expr['union']
        data = expr['data']
        base = expr.get('base')
        ifcond = expr.get('if')
        tag_name = expr.get('discriminator')
        tag_member = None
        if isinstance(base, dict):
            base = self._make_implicit_object_type(
                name, info, doc, ifcond,
                'base', self._make_members(base, info))
        if tag_name:
            variants = [self._make_variant(key, value['type'],
                                           value.get('if'), info)
                        for (key, value) in data.items()]
            members = []
        else:
            variants = [self._make_simple_variant(key, value['type'],
                                                  value.get('if'), info)
                        for (key, value) in data.items()]
            enum = [{'name': v.name, 'if': v.ifcond} for v in variants]
            typ = self._make_implicit_enum_type(name, info, ifcond, enum)
            tag_member = QAPISchemaObjectTypeMember('type', info, typ, False)
            members = [tag_member]
        self._def_entity(
            QAPISchemaObjectType(name, info, doc, ifcond, base, members,
                                 QAPISchemaObjectTypeVariants(
                                     tag_name, info, tag_member, variants),
                                 []))

    def _def_alternate_type(self, expr, info, doc):
        name = expr['alternate']
        data = expr['data']
        ifcond = expr.get('if')
        variants = [self._make_variant(key, value['type'], value.get('if'),
                                       info)
                    for (key, value) in data.items()]
        tag_member = QAPISchemaObjectTypeMember('type', info, 'QType', False)
        self._def_entity(
            QAPISchemaAlternateType(name, info, doc, ifcond,
                                    QAPISchemaObjectTypeVariants(
                                        None, info, tag_member, variants)))

    def _def_command(self, expr, info, doc):
        name = expr['command']
        data = expr.get('data')
        rets = expr.get('returns')
        gen = expr.get('gen', True)
        success_response = expr.get('success-response', True)
        boxed = expr.get('boxed', False)
        allow_oob = expr.get('allow-oob', False)
        allow_preconfig = expr.get('allow-preconfig', False)
        ifcond = expr.get('if')
        if isinstance(data, OrderedDict):
            data = self._make_implicit_object_type(
                name, info, doc, ifcond, 'arg', self._make_members(data, info))
        if isinstance(rets, list):
            assert len(rets) == 1
            rets = self._make_array_type(rets[0], info)
        self._def_entity(QAPISchemaCommand(name, info, doc, ifcond, data, rets,
                                           gen, success_response,
                                           boxed, allow_oob, allow_preconfig))

    def _def_event(self, expr, info, doc):
        name = expr['event']
        data = expr.get('data')
        boxed = expr.get('boxed', False)
        ifcond = expr.get('if')
        if isinstance(data, OrderedDict):
            data = self._make_implicit_object_type(
                name, info, doc, ifcond, 'arg', self._make_members(data, info))
        self._def_entity(QAPISchemaEvent(name, info, doc, ifcond, data, boxed))

    def _def_exprs(self, exprs):
        for expr_elem in exprs:
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
            elif 'include' in expr:
                self._def_include(expr, info, doc)
            else:
                assert False

    def check(self):
        for ent in self._entity_list:
            ent.check(self)

    def visit(self, visitor):
        visitor.visit_begin(self)
        module = None
        visitor.visit_module(module)
        for entity in self._entity_list:
            if visitor.visit_needed(entity):
                if entity.module != module:
                    module = entity.module
                    visitor.visit_module(module)
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


if hasattr(str, 'maketrans'):
    c_name_trans = str.maketrans('.-', '__')
else:
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
    polluted_words = set(['unix', 'errno', 'mips', 'sparc', 'i386'])
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
        raw = re.subn(re.compile(r'^(?!(#|$))', re.MULTILINE),
                      indent, raw)
        raw = raw[0]
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


def _wrap_ifcond(ifcond, before, after):
    if before == after:
        return after   # suppress empty #if ... #endif

    assert after.startswith(before)
    out = before
    added = after[len(before):]
    if added[0] == '\n':
        out += '\n'
        added = added[1:]
    out += gen_if(ifcond)
    out += added
    out += gen_endif(ifcond)
    return out


def gen_enum_lookup(name, members, prefix=None):
    ret = mcgen('''

const QEnumLookup %(c_name)s_lookup = {
    .array = (const char *const[]) {
''',
                c_name=c_name(name))
    for m in members:
        ret += gen_if(m.ifcond)
        index = c_enum_const(name, m.name, prefix)
        ret += mcgen('''
        [%(index)s] = "%(name)s",
''',
                     index=index, name=m.name)
        ret += gen_endif(m.ifcond)

    ret += mcgen('''
    },
    .size = %(max_index)s
};
''',
                 max_index=c_enum_const(name, '_MAX', prefix))
    return ret


def gen_enum(name, members, prefix=None):
    # append automatically generated _MAX value
    enum_members = members + [QAPISchemaEnumMember('_MAX', None)]

    ret = mcgen('''

typedef enum %(c_name)s {
''',
                c_name=c_name(name))

    for m in enum_members:
        ret += gen_if(m.ifcond)
        ret += mcgen('''
    %(c_enum)s,
''',
                     c_enum=c_enum_const(name, m.name, prefix))
        ret += gen_endif(m.ifcond)

    ret += mcgen('''
} %(c_name)s;
''',
                 c_name=c_name(name))

    ret += mcgen('''

#define %(c_name)s_str(val) \\
    qapi_enum_lookup(&%(c_name)s_lookup, (val))

extern const QEnumLookup %(c_name)s_lookup;
''',
                 c_name=c_name(name))
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


#
# Accumulate and write output
#

class QAPIGen(object):

    def __init__(self, fname):
        self.fname = fname
        self._preamble = ''
        self._body = ''

    def preamble_add(self, text):
        self._preamble += text

    def add(self, text):
        self._body += text

    def get_content(self):
        return self._top() + self._preamble + self._body + self._bottom()

    def _top(self):
        return ''

    def _bottom(self):
        return ''

    def write(self, output_dir):
        pathname = os.path.join(output_dir, self.fname)
        dir = os.path.dirname(pathname)
        if dir:
            try:
                os.makedirs(dir)
            except os.error as e:
                if e.errno != errno.EEXIST:
                    raise
        fd = os.open(pathname, os.O_RDWR | os.O_CREAT, 0o666)
        if sys.version_info[0] >= 3:
            f = open(fd, 'r+', encoding='utf-8')
        else:
            f = os.fdopen(fd, 'r+')
        text = self.get_content()
        oldtext = f.read(len(text) + 1)
        if text != oldtext:
            f.seek(0)
            f.truncate(0)
            f.write(text)
        f.close()


@contextmanager
def ifcontext(ifcond, *args):
    """A 'with' statement context manager to wrap with start_if()/end_if()

    *args: any number of QAPIGenCCode

    Example::

        with ifcontext(ifcond, self._genh, self._genc):
            modify self._genh and self._genc ...

    Is equivalent to calling::

        self._genh.start_if(ifcond)
        self._genc.start_if(ifcond)
        modify self._genh and self._genc ...
        self._genh.end_if()
        self._genc.end_if()
    """
    for arg in args:
        arg.start_if(ifcond)
    yield
    for arg in args:
        arg.end_if()


class QAPIGenCCode(QAPIGen):

    def __init__(self, fname):
        QAPIGen.__init__(self, fname)
        self._start_if = None

    def start_if(self, ifcond):
        assert self._start_if is None
        self._start_if = (ifcond, self._body, self._preamble)

    def end_if(self):
        assert self._start_if
        self._wrap_ifcond()
        self._start_if = None

    def _wrap_ifcond(self):
        self._body = _wrap_ifcond(self._start_if[0],
                                  self._start_if[1], self._body)
        self._preamble = _wrap_ifcond(self._start_if[0],
                                      self._start_if[2], self._preamble)

    def get_content(self):
        assert self._start_if is None
        return QAPIGen.get_content(self)


class QAPIGenC(QAPIGenCCode):

    def __init__(self, fname, blurb, pydoc):
        QAPIGenCCode.__init__(self, fname)
        self._blurb = blurb
        self._copyright = '\n * '.join(re.findall(r'^Copyright .*', pydoc,
                                                  re.MULTILINE))

    def _top(self):
        return mcgen('''
/* AUTOMATICALLY GENERATED, DO NOT MODIFY */

/*
%(blurb)s
 *
 * %(copyright)s
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

''',
                     blurb=self._blurb, copyright=self._copyright)

    def _bottom(self):
        return mcgen('''

/* Dummy declaration to prevent empty .o file */
char qapi_dummy_%(name)s;
''',
                     name=c_fname(self.fname))


class QAPIGenH(QAPIGenC):

    def _top(self):
        return QAPIGenC._top(self) + guardstart(self.fname)

    def _bottom(self):
        return guardend(self.fname)


class QAPIGenDoc(QAPIGen):

    def _top(self):
        return (QAPIGen._top(self)
                + '@c AUTOMATICALLY GENERATED, DO NOT MODIFY\n\n')


class QAPISchemaMonolithicCVisitor(QAPISchemaVisitor):

    def __init__(self, prefix, what, blurb, pydoc):
        self._prefix = prefix
        self._what = what
        self._genc = QAPIGenC(self._prefix + self._what + '.c',
                              blurb, pydoc)
        self._genh = QAPIGenH(self._prefix + self._what + '.h',
                              blurb, pydoc)

    def write(self, output_dir):
        self._genc.write(output_dir)
        self._genh.write(output_dir)


class QAPISchemaModularCVisitor(QAPISchemaVisitor):

    def __init__(self, prefix, what, blurb, pydoc):
        self._prefix = prefix
        self._what = what
        self._blurb = blurb
        self._pydoc = pydoc
        self._genc = None
        self._genh = None
        self._module = {}
        self._main_module = None

    @staticmethod
    def _is_user_module(name):
        return name and not name.startswith('./')

    @staticmethod
    def _is_builtin_module(name):
        return not name

    def _module_dirname(self, what, name):
        if self._is_user_module(name):
            return os.path.dirname(name)
        return ''

    def _module_basename(self, what, name):
        ret = '' if self._is_builtin_module(name) else self._prefix
        if self._is_user_module(name):
            basename = os.path.basename(name)
            ret += what
            if name != self._main_module:
                ret += '-' + os.path.splitext(basename)[0]
        else:
            name = name[2:] if name else 'builtin'
            ret += re.sub(r'-', '-' + name + '-', what)
        return ret

    def _module_filename(self, what, name):
        return os.path.join(self._module_dirname(what, name),
                            self._module_basename(what, name))

    def _add_module(self, name, blurb):
        basename = self._module_filename(self._what, name)
        genc = QAPIGenC(basename + '.c', blurb, self._pydoc)
        genh = QAPIGenH(basename + '.h', blurb, self._pydoc)
        self._module[name] = (genc, genh)
        self._set_module(name)

    def _add_user_module(self, name, blurb):
        assert self._is_user_module(name)
        if self._main_module is None:
            self._main_module = name
        self._add_module(name, blurb)

    def _add_system_module(self, name, blurb):
        self._add_module(name and './' + name, blurb)

    def _set_module(self, name):
        self._genc, self._genh = self._module[name]

    def write(self, output_dir, opt_builtins=False):
        for name in self._module:
            if self._is_builtin_module(name) and not opt_builtins:
                continue
            (genc, genh) = self._module[name]
            genc.write(output_dir)
            genh.write(output_dir)

    def _begin_user_module(self, name):
        pass

    def visit_module(self, name):
        if name in self._module:
            self._set_module(name)
        elif self._is_builtin_module(name):
            # The built-in module has not been created.  No code may
            # be generated.
            self._genc = None
            self._genh = None
        else:
            self._add_user_module(name, self._blurb)
            self._begin_user_module(name)

    def visit_include(self, name, info):
        relname = os.path.relpath(self._module_filename(self._what, name),
                                  os.path.dirname(self._genh.fname))
        self._genh.preamble_add(mcgen('''
#include "%(relname)s.h"
''',
                                      relname=relname))
