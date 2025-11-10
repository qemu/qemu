# QAPI schema parser
#
# Copyright IBM, Corp. 2011
# Copyright (c) 2013-2019 Red Hat Inc.
#
# Authors:
#  Anthony Liguori <aliguori@us.ibm.com>
#  Markus Armbruster <armbru@redhat.com>
#  Marc-André Lureau <marcandre.lureau@redhat.com>
#  Kevin Wolf <kwolf@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

import enum
import os
import re
from typing import (
    TYPE_CHECKING,
    Any,
    Dict,
    List,
    Mapping,
    Match,
    Optional,
    Set,
    Union,
)

from .common import must_match
from .error import QAPISemError, QAPISourceError
from .source import QAPISourceInfo


if TYPE_CHECKING:
    # pylint: disable=cyclic-import
    # TODO: Remove cycle. [schema -> expr -> parser -> schema]
    from .schema import QAPISchemaFeature, QAPISchemaMember


# Return value alias for get_expr().
_ExprValue = Union[List[object], Dict[str, object], str, bool]


class QAPIExpression(Dict[str, Any]):
    # pylint: disable=too-few-public-methods
    def __init__(self,
                 data: Mapping[str, object],
                 info: QAPISourceInfo,
                 doc: Optional['QAPIDoc'] = None):
        super().__init__(data)
        self.info = info
        self.doc: Optional['QAPIDoc'] = doc


class QAPIParseError(QAPISourceError):
    """Error class for all QAPI schema parsing errors."""
    def __init__(self, parser: 'QAPISchemaParser', msg: str):
        col = 1
        for ch in parser.src[parser.line_pos:parser.pos]:
            if ch == '\t':
                col = (col + 7) % 8 + 1
            else:
                col += 1
        super().__init__(parser.info, msg, col)


class QAPISchemaParser:
    """
    Parse QAPI schema source.

    Parse a JSON-esque schema file and process directives.  See
    qapi-code-gen.rst section "Schema Syntax" for the exact syntax.
    Grammatical validation is handled later by `expr.check_exprs()`.

    :param fname: Source file name.
    :param previously_included:
        The absolute names of previously included source files,
        if being invoked from another parser.
    :param incl_info:
       `QAPISourceInfo` belonging to the parent module.
       ``None`` implies this is the root module.

    :ivar exprs: Resulting parsed expressions.
    :ivar docs: Resulting parsed documentation blocks.

    :raise OSError: For problems reading the root schema document.
    :raise QAPIError: For errors in the schema source.
    """
    def __init__(self,
                 fname: str,
                 previously_included: Optional[Set[str]] = None,
                 incl_info: Optional[QAPISourceInfo] = None):
        self._fname = fname
        self._included = previously_included or set()
        self._included.add(os.path.abspath(self._fname))
        self.src = ''

        # Lexer state (see `accept` for details):
        self.info = QAPISourceInfo(self._fname, incl_info)
        self.tok: Union[None, str] = None
        self.pos = 0
        self.cursor = 0
        self.val: Optional[Union[bool, str]] = None
        self.line_pos = 0

        # Parser output:
        self.exprs: List[QAPIExpression] = []
        self.docs: List[QAPIDoc] = []

        # State for tracking qmp-example blocks and simple
        # :: literal blocks.
        self._literal_mode = False
        self._literal_mode_indent = 0

        # Showtime!
        self._parse()

    def _parse(self) -> None:
        """
        Parse the QAPI schema document.

        :return: None.  Results are stored in ``.exprs`` and ``.docs``.
        """
        cur_doc = None

        # May raise OSError; allow the caller to handle it.
        with open(self._fname, 'r', encoding='utf-8') as fp:
            self.src = fp.read()
        if self.src == '' or self.src[-1] != '\n':
            self.src += '\n'

        # Prime the lexer:
        self.accept()

        # Parse until done:
        while self.tok is not None:
            info = self.info
            if self.tok == '#':
                self.reject_expr_doc(cur_doc)
                cur_doc = self.get_doc()
                self.docs.append(cur_doc)
                continue

            expr = self.get_expr()
            if not isinstance(expr, dict):
                raise QAPISemError(
                    info, "top-level expression must be an object")

            if 'include' in expr:
                self.reject_expr_doc(cur_doc)
                if len(expr) != 1:
                    raise QAPISemError(info, "invalid 'include' directive")
                include = expr['include']
                if not isinstance(include, str):
                    raise QAPISemError(info,
                                       "value of 'include' must be a string")
                incl_fname = os.path.join(os.path.dirname(self._fname),
                                          include)
                self._add_expr({'include': incl_fname}, info)
                exprs_include = self._include(include, info, incl_fname,
                                              self._included)
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
                if cur_doc and not cur_doc.symbol:
                    raise QAPISemError(
                        cur_doc.info, "definition documentation required")
                self._add_expr(expr, info, cur_doc)
            cur_doc = None
        self.reject_expr_doc(cur_doc)

    def _add_expr(self, expr: Mapping[str, object],
                  info: QAPISourceInfo,
                  doc: Optional['QAPIDoc'] = None) -> None:
        self.exprs.append(QAPIExpression(expr, info, doc))

    @staticmethod
    def reject_expr_doc(doc: Optional['QAPIDoc']) -> None:
        if doc and doc.symbol:
            raise QAPISemError(
                doc.info,
                "documentation for '%s' is not followed by the definition"
                % doc.symbol)

    @staticmethod
    def _include(include: str,
                 info: QAPISourceInfo,
                 incl_fname: str,
                 previously_included: Set[str]
                 ) -> Optional['QAPISchemaParser']:
        incl_abs_fname = os.path.abspath(incl_fname)
        # catch inclusion cycle
        inf: Optional[QAPISourceInfo] = info
        while inf:
            if incl_abs_fname == os.path.abspath(inf.fname):
                raise QAPISemError(info, "inclusion loop for %s" % include)
            inf = inf.parent

        # skip multiple include of the same file
        if incl_abs_fname in previously_included:
            return None

        try:
            return QAPISchemaParser(incl_fname, previously_included, info)
        except OSError as err:
            raise QAPISemError(
                info,
                f"can't read include file '{incl_fname}': {err.strerror}"
            ) from err

    @staticmethod
    def _pragma(name: str, value: object, info: QAPISourceInfo) -> None:

        def check_list_str(name: str, value: object) -> List[str]:
            if (not isinstance(value, list) or
                    any(not isinstance(elt, str) for elt in value)):
                raise QAPISemError(
                    info,
                    "pragma %s must be a list of strings" % name)
            return value

        pragma = info.pragma

        if name == 'doc-required':
            if not isinstance(value, bool):
                raise QAPISemError(info,
                                   "pragma 'doc-required' must be boolean")
            pragma.doc_required = value
        elif name == 'command-name-exceptions':
            pragma.command_name_exceptions = check_list_str(name, value)
        elif name == 'command-returns-exceptions':
            pragma.command_returns_exceptions = check_list_str(name, value)
        elif name == 'documentation-exceptions':
            pragma.documentation_exceptions = check_list_str(name, value)
        elif name == 'member-name-exceptions':
            pragma.member_name_exceptions = check_list_str(name, value)
        else:
            raise QAPISemError(info, "unknown pragma '%s'" % name)

    def accept(self, skip_comment: bool = True) -> None:
        """
        Read and store the next token.

        :param skip_comment:
            When false, return COMMENT tokens ("#").
            This is used when reading documentation blocks.

        :return:
            None.  Several instance attributes are updated instead:

            - ``.tok`` represents the token type.  See below for values.
            - ``.info`` describes the token's source location.
            - ``.val`` is the token's value, if any.  See below.
            - ``.pos`` is the buffer index of the first character of
              the token.

        * Single-character tokens:

            These are "{", "}", ":", ",", "[", and "]".
            ``.tok`` holds the single character and ``.val`` is None.

        * Multi-character tokens:

          * COMMENT:

            This token is not normally returned by the lexer, but it can
            be when ``skip_comment`` is False.  ``.tok`` is "#", and
            ``.val`` is a string including all chars until end-of-line,
            including the "#" itself.

          * STRING:

            ``.tok`` is "'", the single quote.  ``.val`` contains the
            string, excluding the surrounding quotes.

          * TRUE and FALSE:

            ``.tok`` is either "t" or "f", ``.val`` will be the
            corresponding bool value.

          * EOF:

            ``.tok`` and ``.val`` will both be None at EOF.
        """
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
                match = must_match('[^[\\]{}:,\\s\']+',
                                   self.src[self.cursor-1:])
                raise QAPIParseError(self, "stray '%s'" % match.group(0))

    def get_members(self) -> Dict[str, object]:
        expr: Dict[str, object] = {}
        if self.tok == '}':
            self.accept()
            return expr
        if self.tok != "'":
            raise QAPIParseError(self, "expected string or '}'")
        while True:
            key = self.val
            assert isinstance(key, str)  # Guaranteed by tok == "'"

            self.accept()
            if self.tok != ':':
                raise QAPIParseError(self, "expected ':'")
            self.accept()
            if key in expr:
                raise QAPIParseError(self, "duplicate key '%s'" % key)
            expr[key] = self.get_expr()
            if self.tok == '}':
                self.accept()
                return expr
            if self.tok != ',':
                raise QAPIParseError(self, "expected ',' or '}'")
            self.accept()
            if self.tok != "'":
                raise QAPIParseError(self, "expected string")

    def get_values(self) -> List[object]:
        expr: List[object] = []
        if self.tok == ']':
            self.accept()
            return expr
        if self.tok not in tuple("{['tf"):
            raise QAPIParseError(
                self, "expected '{', '[', ']', string, or boolean")
        while True:
            expr.append(self.get_expr())
            if self.tok == ']':
                self.accept()
                return expr
            if self.tok != ',':
                raise QAPIParseError(self, "expected ',' or ']'")
            self.accept()

    def get_expr(self) -> _ExprValue:
        expr: _ExprValue
        if self.tok == '{':
            self.accept()
            expr = self.get_members()
        elif self.tok == '[':
            self.accept()
            expr = self.get_values()
        elif self.tok in tuple("'tf"):
            assert isinstance(self.val, (str, bool))
            expr = self.val
            self.accept()
        else:
            raise QAPIParseError(
                self, "expected '{', '[', string, or boolean")
        return expr

    def get_doc_line(self) -> Optional[str]:
        if self.tok != '#':
            raise QAPIParseError(
                self, "documentation comment must end with '##'")
        assert isinstance(self.val, str)
        if self.val.startswith('##'):
            # End of doc comment
            if self.val != '##':
                raise QAPIParseError(
                    self, "junk after '##' at end of documentation comment")
            self._literal_mode = False
            return None
        if self.val == '#':
            return ''
        if self.val[1] != ' ':
            raise QAPIParseError(self, "missing space after #")

        line = self.val[2:].rstrip()

        if re.match(r'(\.\. +qmp-example)? *::$', line):
            self._literal_mode = True
            self._literal_mode_indent = 0
        elif self._literal_mode and line:
            indent = must_match(r'\s*', line).end()
            if self._literal_mode_indent == 0:
                self._literal_mode_indent = indent
            elif indent < self._literal_mode_indent:
                # ReST directives stop at decreasing indentation
                self._literal_mode = False

        if not self._literal_mode:
            self._validate_doc_line_format(line)

        return line

    def _validate_doc_line_format(self, line: str) -> None:
        """
        Validate documentation format rules for a single line:
        1. Lines should not exceed 70 characters
        2. Sentences should be separated by two spaces
        """
        full_line_length = len(line) + 2  # "# " = 2 characters
        if full_line_length > 70:
            # Skip URL lines - they can't be broken
            if re.match(r' *(https?|ftp)://[^ ]*$', line):
                pass
            else:
                raise QAPIParseError(
                    self, "documentation line longer than 70 characters")

        single_space_pattern = r'(\be\.g\.|^ *\d\.|([.!?])) [A-Z0-9(]'
        for m in list(re.finditer(single_space_pattern, line)):
            if not m.group(2):
                continue
            # HACK so the error message points to the offending spot
            self.pos = self.line_pos + 2 + m.start(2) + 1
            raise QAPIParseError(
                self, "Use two spaces between sentences\n"
                "If this not the end of a sentence, please report a bug.")

    @staticmethod
    def _match_at_name_colon(string: str) -> Optional[Match[str]]:
        return re.match(r'@([^:]*): *', string)

    def get_doc_indented(self, doc: 'QAPIDoc') -> Optional[str]:
        self.accept(False)
        line = self.get_doc_line()
        while line == '':
            doc.append_line(line)
            self.accept(False)
            line = self.get_doc_line()
        if line is None:
            return line
        indent = must_match(r'\s*', line).end()
        if not indent:
            return line
        doc.append_line(line)
        prev_line_blank = False
        while True:
            self.accept(False)
            line = self.get_doc_line()
            if line is None:
                return line
            if self._match_at_name_colon(line):
                return line
            cur_indent = must_match(r'\s*', line).end()
            if line != '' and cur_indent < indent:
                if prev_line_blank:
                    return line
                raise QAPIParseError(
                    self,
                    "unexpected de-indent (expected at least %d spaces)" %
                    indent)
            doc.append_line(line)
            prev_line_blank = True

    def get_doc_paragraph(self, doc: 'QAPIDoc') -> Optional[str]:
        while True:
            self.accept(False)
            line = self.get_doc_line()
            if line is None:
                return line
            if line == '':
                return line
            doc.append_line(line)

    def get_doc(self) -> 'QAPIDoc':
        if self.val != '##':
            raise QAPIParseError(
                self, "junk after '##' at start of documentation comment")
        info = self.info
        self.accept(False)
        line = self.get_doc_line()
        if line is not None and line.startswith('@'):
            # Definition documentation
            if not line.endswith(':'):
                raise QAPIParseError(self, "line should end with ':'")
            # Invalid names are not checked here, but the name
            # provided *must* match the following definition,
            # which *is* validated in expr.py.
            symbol = line[1:-1]
            if not symbol:
                raise QAPIParseError(self, "name required after '@'")
            doc = QAPIDoc(info, symbol)
            self.accept(False)
            line = self.get_doc_line()
            no_more_args = False

            while line is not None:
                # Blank lines
                while line == '':
                    self.accept(False)
                    line = self.get_doc_line()
                if line is None:
                    break
                # Non-blank line, first of a section
                if line == 'Features:':
                    if doc.features:
                        raise QAPIParseError(
                            self, "duplicated 'Features:' line")
                    self.accept(False)
                    line = self.get_doc_line()
                    while line == '':
                        self.accept(False)
                        line = self.get_doc_line()
                    while (line is not None
                           and (match := self._match_at_name_colon(line))):
                        doc.new_feature(self.info, match.group(1))
                        text = line[match.end():]
                        if text:
                            doc.append_line(text)
                        line = self.get_doc_indented(doc)
                    if not doc.features:
                        raise QAPIParseError(
                            self, 'feature descriptions expected')
                    no_more_args = True
                elif match := self._match_at_name_colon(line):
                    # description
                    if no_more_args:
                        raise QAPIParseError(
                            self,
                            "description of '@%s:' follows a section"
                            % match.group(1))
                    while (line is not None
                           and (match := self._match_at_name_colon(line))):
                        doc.new_argument(self.info, match.group(1))
                        text = line[match.end():]
                        if text:
                            doc.append_line(text)
                        line = self.get_doc_indented(doc)
                    no_more_args = True
                elif match := re.match(
                        r'(Returns|Errors|Since|Notes?|Examples?|TODO)'
                        r'(?!::): *',
                        line,
                ):
                    # tagged section

                    # Note: "sections" with two colons are left alone as
                    # rST markup and not interpreted as a section heading.

                    # TODO: Remove these errors sometime in 2025 or so
                    # after we've fully transitioned to the new qapidoc
                    # generator.

                    # See commit message for more markup suggestions O:-)
                    if 'Note' in match.group(1):
                        emsg = (
                            f"The '{match.group(1)}' section is no longer "
                            "supported. Please use rST's '.. note::' or "
                            "'.. admonition:: notes' directives, or another "
                            "suitable admonition instead."
                        )
                        raise QAPIParseError(self, emsg)

                    if 'Example' in match.group(1):
                        emsg = (
                            f"The '{match.group(1)}' section is no longer "
                            "supported. Please use the '.. qmp-example::' "
                            "directive, or other suitable markup instead."
                        )
                        raise QAPIParseError(self, emsg)

                    doc.new_tagged_section(
                        self.info,
                        QAPIDoc.Kind.from_string(match.group(1))
                    )
                    text = line[match.end():]
                    if text:
                        doc.append_line(text)
                    line = self.get_doc_indented(doc)
                    no_more_args = True
                else:
                    # plain paragraph
                    doc.ensure_untagged_section(self.info)
                    doc.append_line(line)
                    line = self.get_doc_paragraph(doc)
        else:
            # Free-form documentation
            doc = QAPIDoc(info)
            doc.ensure_untagged_section(self.info)
            while line is not None:
                if match := self._match_at_name_colon(line):
                    raise QAPIParseError(
                        self,
                        "'@%s:' not allowed in free-form documentation"
                        % match.group(1))
                doc.append_line(line)
                self.accept(False)
                line = self.get_doc_line()

        self.accept()
        doc.end()
        return doc


class QAPIDoc:
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

    class Kind(enum.Enum):
        PLAIN = 0
        MEMBER = 1
        FEATURE = 2
        RETURNS = 3
        ERRORS = 4
        SINCE = 5
        TODO = 6

        @staticmethod
        def from_string(kind: str) -> 'QAPIDoc.Kind':
            return QAPIDoc.Kind[kind.upper()]

        def __str__(self) -> str:
            return self.name.title()

    class Section:
        # pylint: disable=too-few-public-methods
        def __init__(
            self,
            info: QAPISourceInfo,
            kind: 'QAPIDoc.Kind',
        ):
            # section source info, i.e. where it begins
            self.info = info
            # section kind
            self.kind = kind
            # section text without tag
            self.text = ''

        def __repr__(self) -> str:
            return f"<QAPIDoc.Section kind={self.kind!r} text={self.text!r}>"

        def append_line(self, line: str) -> None:
            self.text += line + '\n'

    class ArgSection(Section):
        def __init__(
            self,
            info: QAPISourceInfo,
            kind: 'QAPIDoc.Kind',
            name: str
        ):
            super().__init__(info, kind)
            self.name = name
            self.member: Optional['QAPISchemaMember'] = None

        def connect(self, member: 'QAPISchemaMember') -> None:
            self.member = member

    def __init__(self, info: QAPISourceInfo, symbol: Optional[str] = None):
        # info points to the doc comment block's first line
        self.info = info
        # definition doc's symbol, None for free-form doc
        self.symbol: Optional[str] = symbol
        # the sections in textual order
        self.all_sections: List[QAPIDoc.Section] = [
            QAPIDoc.Section(info, QAPIDoc.Kind.PLAIN)
        ]
        # the body section
        self.body: Optional[QAPIDoc.Section] = self.all_sections[0]
        # dicts mapping parameter/feature names to their description
        self.args: Dict[str, QAPIDoc.ArgSection] = {}
        self.features: Dict[str, QAPIDoc.ArgSection] = {}
        # a command's "Returns" and "Errors" section
        self.returns: Optional[QAPIDoc.Section] = None
        self.errors: Optional[QAPIDoc.Section] = None
        # "Since" section
        self.since: Optional[QAPIDoc.Section] = None
        # sections other than .body, .args, .features
        self.sections: List[QAPIDoc.Section] = []

    def end(self) -> None:
        for section in self.all_sections:
            section.text = section.text.strip('\n')
            if section.kind != QAPIDoc.Kind.PLAIN and section.text == '':
                raise QAPISemError(
                    section.info, "text required after '%s:'" % section.kind)

    def ensure_untagged_section(self, info: QAPISourceInfo) -> None:
        kind = QAPIDoc.Kind.PLAIN

        if self.all_sections and self.all_sections[-1].kind == kind:
            # extend current section
            section = self.all_sections[-1]
            if not section.text:
                # Section is empty so far; update info to start *here*.
                section.info = info
            section.text += '\n'
            return

        # start new section
        section = self.Section(info, kind)
        self.sections.append(section)
        self.all_sections.append(section)

    def new_tagged_section(
        self,
        info: QAPISourceInfo,
        kind: 'QAPIDoc.Kind',
    ) -> None:
        section = self.Section(info, kind)
        if kind == QAPIDoc.Kind.RETURNS:
            if self.returns:
                raise QAPISemError(
                    info, "duplicated '%s' section" % kind)
            self.returns = section
        elif kind == QAPIDoc.Kind.ERRORS:
            if self.errors:
                raise QAPISemError(
                    info, "duplicated '%s' section" % kind)
            self.errors = section
        elif kind == QAPIDoc.Kind.SINCE:
            if self.since:
                raise QAPISemError(
                    info, "duplicated '%s' section" % kind)
            self.since = section
        self.sections.append(section)
        self.all_sections.append(section)

    def _new_description(
        self,
        info: QAPISourceInfo,
        name: str,
        kind: 'QAPIDoc.Kind',
        desc: Dict[str, ArgSection]
    ) -> None:
        if not name:
            raise QAPISemError(info, "invalid parameter name")
        if name in desc:
            raise QAPISemError(info, "'%s' parameter name duplicated" % name)
        section = self.ArgSection(info, kind, name)
        self.all_sections.append(section)
        desc[name] = section

    def new_argument(self, info: QAPISourceInfo, name: str) -> None:
        self._new_description(info, name, QAPIDoc.Kind.MEMBER, self.args)

    def new_feature(self, info: QAPISourceInfo, name: str) -> None:
        self._new_description(info, name, QAPIDoc.Kind.FEATURE, self.features)

    def append_line(self, line: str) -> None:
        self.all_sections[-1].append_line(line)

    def connect_member(self, member: 'QAPISchemaMember') -> None:
        if member.name not in self.args:
            assert member.info
            if self.symbol not in member.info.pragma.documentation_exceptions:
                raise QAPISemError(member.info,
                                   "%s '%s' lacks documentation"
                                   % (member.role, member.name))
            # Insert stub documentation section for missing member docs.
            # TODO: drop when undocumented members are outlawed

            section = QAPIDoc.ArgSection(
                self.info, QAPIDoc.Kind.MEMBER, member.name)
            self.args[member.name] = section

            # Determine where to insert stub doc - it should go at the
            # end of the members section(s), if any. Note that index 0
            # is assumed to be an untagged intro section, even if it is
            # empty.
            index = 1
            if len(self.all_sections) > 1:
                while self.all_sections[index].kind == QAPIDoc.Kind.MEMBER:
                    index += 1
            self.all_sections.insert(index, section)

        self.args[member.name].connect(member)

    def connect_feature(self, feature: 'QAPISchemaFeature') -> None:
        if feature.name not in self.features:
            raise QAPISemError(feature.info,
                               "feature '%s' lacks documentation"
                               % feature.name)
        self.features[feature.name].connect(feature)

    def ensure_returns(self, info: QAPISourceInfo) -> None:

        def _insert_near_kind(
            kind: QAPIDoc.Kind,
            new_sect: QAPIDoc.Section,
            after: bool = False,
        ) -> bool:
            for idx, sect in enumerate(reversed(self.all_sections)):
                if sect.kind == kind:
                    pos = len(self.all_sections) - idx - 1
                    if after:
                        pos += 1
                    self.all_sections.insert(pos, new_sect)
                    return True
            return False

        if any(s.kind == QAPIDoc.Kind.RETURNS for s in self.all_sections):
            return

        # Stub "Returns" section for undocumented returns value
        stub = QAPIDoc.Section(info, QAPIDoc.Kind.RETURNS)

        if any(_insert_near_kind(kind, stub, after) for kind, after in (
                # 1. If arguments, right after those.
                (QAPIDoc.Kind.MEMBER, True),
                # 2. Elif errors, right *before* those.
                (QAPIDoc.Kind.ERRORS, False),
                # 3. Elif features, right *before* those.
                (QAPIDoc.Kind.FEATURE, False),
        )):
            return

        # Otherwise, it should go right after the intro. The intro
        # is always the first section and is always present (even
        # when empty), so we can insert directly at index=1 blindly.
        self.all_sections.insert(1, stub)

    def check_expr(self, expr: QAPIExpression) -> None:
        if 'command' in expr:
            if self.returns and 'returns' not in expr:
                raise QAPISemError(
                    self.returns.info,
                    "'Returns' section, but command doesn't return anything")
        else:
            if self.returns:
                raise QAPISemError(
                    self.returns.info,
                    "'Returns' section is only valid for commands")
            if self.errors:
                raise QAPISemError(
                    self.errors.info,
                    "'Errors' section is only valid for commands")

    def check(self) -> None:

        def check_args_section(
                args: Dict[str, QAPIDoc.ArgSection], what: str
        ) -> None:
            bogus = [name for name, section in args.items()
                     if not section.member]
            if bogus:
                raise QAPISemError(
                    args[bogus[0]].info,
                    "documented %s%s '%s' %s not exist" % (
                        what,
                        "s" if len(bogus) > 1 else "",
                        "', '".join(bogus),
                        "do" if len(bogus) > 1 else "does"
                    ))

        check_args_section(self.args, 'member')
        check_args_section(self.features, 'feature')
