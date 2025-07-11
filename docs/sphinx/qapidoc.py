# coding=utf-8
#
# QEMU qapidoc QAPI file parsing extension
#
# Copyright (c) 2024-2025 Red Hat
# Copyright (c) 2020 Linaro
#
# This work is licensed under the terms of the GNU GPLv2 or later.
# See the COPYING file in the top-level directory.

"""
qapidoc is a Sphinx extension that implements the qapi-doc directive

The purpose of this extension is to read the documentation comments
in QAPI schema files, and insert them all into the current document.

It implements one new rST directive, "qapi-doc::".
Each qapi-doc:: directive takes one argument, which is the
pathname of the schema file to process, relative to the source tree.

The docs/conf.py file must set the qapidoc_srctree config value to
the root of the QEMU source tree.

The Sphinx documentation on writing extensions is at:
https://www.sphinx-doc.org/en/master/development/index.html
"""

from __future__ import annotations


__version__ = "2.0"

from contextlib import contextmanager
import os
from pathlib import Path
import re
import sys
from typing import TYPE_CHECKING

from docutils import nodes
from docutils.parsers.rst import directives
from docutils.statemachine import StringList
from qapi.error import QAPIError
from qapi.parser import QAPIDoc
from qapi.schema import (
    QAPISchema,
    QAPISchemaArrayType,
    QAPISchemaCommand,
    QAPISchemaDefinition,
    QAPISchemaEnumMember,
    QAPISchemaEvent,
    QAPISchemaFeature,
    QAPISchemaMember,
    QAPISchemaObjectType,
    QAPISchemaObjectTypeMember,
    QAPISchemaType,
    QAPISchemaVisitor,
)
from qapi.source import QAPISourceInfo
from sphinx import addnodes
from sphinx.directives.code import CodeBlock
from sphinx.errors import ExtensionError
from sphinx.util import logging
from sphinx.util.docutils import SphinxDirective, switch_source_input
from sphinx.util.nodes import nested_parse_with_titles


if TYPE_CHECKING:
    from typing import (
        Any,
        Generator,
        List,
        Optional,
        Sequence,
        Union,
    )

    from sphinx.application import Sphinx
    from sphinx.util.typing import ExtensionMetadata


logger = logging.getLogger(__name__)


class Transmogrifier:
    # pylint: disable=too-many-public-methods

    # Field names used for different entity types:
    field_types = {
        "enum": "value",
        "struct": "memb",
        "union": "memb",
        "event": "memb",
        "command": "arg",
        "alternate": "alt",
    }

    def __init__(self) -> None:
        self._curr_ent: Optional[QAPISchemaDefinition] = None
        self._result = StringList()
        self.indent = 0

    @property
    def result(self) -> StringList:
        return self._result

    @property
    def entity(self) -> QAPISchemaDefinition:
        assert self._curr_ent is not None
        return self._curr_ent

    @property
    def member_field_type(self) -> str:
        return self.field_types[self.entity.meta]

    # General-purpose rST generation functions

    def get_indent(self) -> str:
        return "   " * self.indent

    @contextmanager
    def indented(self) -> Generator[None]:
        self.indent += 1
        try:
            yield
        finally:
            self.indent -= 1

    def add_line_raw(self, line: str, source: str, *lineno: int) -> None:
        """Append one line of generated reST to the output."""

        # NB: Sphinx uses zero-indexed lines; subtract one.
        lineno = tuple((n - 1 for n in lineno))

        if line.strip():
            # not a blank line
            self._result.append(
                self.get_indent() + line.rstrip("\n"), source, *lineno
            )
        else:
            self._result.append("", source, *lineno)

    def add_line(self, content: str, info: QAPISourceInfo) -> None:
        # NB: We *require* an info object; this works out OK because we
        # don't document built-in objects that don't have
        # one. Everything else should.
        self.add_line_raw(content, info.fname, info.line)

    def add_lines(
        self,
        content: str,
        info: QAPISourceInfo,
    ) -> None:
        lines = content.splitlines(True)
        for i, line in enumerate(lines):
            self.add_line_raw(line, info.fname, info.line + i)

    def ensure_blank_line(self) -> None:
        # Empty document -- no blank line required.
        if not self._result:
            return

        # Last line isn't blank, add one.
        if self._result[-1].strip():  # pylint: disable=no-member
            fname, line = self._result.info(-1)
            assert isinstance(line, int)
            # New blank line is credited to one-after the current last line.
            # +2: correct for zero/one index, then increment by one.
            self.add_line_raw("", fname, line + 2)

    def add_field(
        self,
        kind: str,
        name: str,
        body: str,
        info: QAPISourceInfo,
        typ: Optional[str] = None,
    ) -> None:
        if typ:
            text = f":{kind} {typ} {name}: {body}"
        else:
            text = f":{kind} {name}: {body}"
        self.add_lines(text, info)

    def format_type(
        self, ent: Union[QAPISchemaDefinition | QAPISchemaMember]
    ) -> Optional[str]:
        if isinstance(ent, (QAPISchemaEnumMember, QAPISchemaFeature)):
            return None

        qapi_type = ent
        optional = False
        if isinstance(ent, QAPISchemaObjectTypeMember):
            qapi_type = ent.type
            optional = ent.optional

        if isinstance(qapi_type, QAPISchemaArrayType):
            ret = f"[{qapi_type.element_type.doc_type()}]"
        else:
            assert isinstance(qapi_type, QAPISchemaType)
            tmp = qapi_type.doc_type()
            assert tmp
            ret = tmp
        if optional:
            ret += "?"

        return ret

    def generate_field(
        self,
        kind: str,
        member: QAPISchemaMember,
        body: str,
        info: QAPISourceInfo,
    ) -> None:
        typ = self.format_type(member)
        self.add_field(kind, member.name, body, info, typ)

    @staticmethod
    def reformat_arobase(text: str) -> str:
        """ reformats @var to ``var`` """
        return re.sub(r"@([\w-]+)", r"``\1``", text)

    # Transmogrification helpers

    def visit_paragraph(self, section: QAPIDoc.Section) -> None:
        # Squelch empty paragraphs.
        if not section.text:
            return

        self.ensure_blank_line()
        self.add_lines(section.text, section.info)
        self.ensure_blank_line()

    def visit_member(self, section: QAPIDoc.ArgSection) -> None:
        # FIXME: ifcond for members
        # TODO: features for members (documented at entity-level,
        # but sometimes defined per-member. Should we add such
        # information to member descriptions when we can?)
        assert section.member
        self.generate_field(
            self.member_field_type,
            section.member,
            # TODO drop fallbacks when undocumented members are outlawed
            section.text if section.text else "Not documented",
            section.info,
        )

    def visit_feature(self, section: QAPIDoc.ArgSection) -> None:
        # FIXME - ifcond for features is not handled at all yet!
        # Proposal: decorate the right-hand column with some graphical
        # element to indicate conditional availability?
        assert section.text  # Guaranteed by parser.py
        assert section.member

        self.generate_field("feat", section.member, section.text, section.info)

    def visit_returns(self, section: QAPIDoc.Section) -> None:
        assert isinstance(self.entity, QAPISchemaCommand)
        rtype = self.entity.ret_type
        # return statements will not be present (and won't be
        # autogenerated) for any command that doesn't return
        # *something*, so rtype will always be defined here.
        assert rtype

        typ = self.format_type(rtype)
        assert typ

        if section.text:
            self.add_field("return", typ, section.text, section.info)
        else:
            self.add_lines(f":return-nodesc: {typ}", section.info)

    def visit_errors(self, section: QAPIDoc.Section) -> None:
        # If the section text does not start with a space, it means text
        # began on the same line as the "Error:" string and we should
        # not insert a newline in this case.
        if section.text[0].isspace():
            text = f":error:\n{section.text}"
        else:
            text = f":error: {section.text}"
        self.add_lines(text, section.info)

    def preamble(self, ent: QAPISchemaDefinition) -> None:
        """
        Generate option lines for QAPI entity directives.
        """
        if ent.doc and ent.doc.since:
            assert ent.doc.since.kind == QAPIDoc.Kind.SINCE
            # Generated from the entity's docblock; info location is exact.
            self.add_line(f":since: {ent.doc.since.text}", ent.doc.since.info)

        if ent.ifcond.is_present():
            doc = ent.ifcond.docgen()
            assert ent.info
            # Generated from entity definition; info location is approximate.
            self.add_line(f":ifcond: {doc}", ent.info)

        # Hoist special features such as :deprecated: and :unstable:
        # into the options block for the entity. If, in the future, new
        # special features are added, qapi-domain will chirp about
        # unrecognized options and fail until they are handled in
        # qapi-domain.
        for feat in ent.features:
            if feat.is_special():
                # FIXME: handle ifcond if present. How to display that
                # information is TBD.
                # Generated from entity def; info location is approximate.
                assert feat.info
                self.add_line(f":{feat.name}:", feat.info)

        self.ensure_blank_line()

    def _insert_member_pointer(self, ent: QAPISchemaDefinition) -> None:

        def _get_target(
            ent: QAPISchemaDefinition,
        ) -> Optional[QAPISchemaDefinition]:
            if isinstance(ent, (QAPISchemaCommand, QAPISchemaEvent)):
                return ent.arg_type
            if isinstance(ent, QAPISchemaObjectType):
                return ent.base
            return None

        target = _get_target(ent)
        if target is not None and not target.is_implicit():
            assert ent.info
            self.add_field(
                self.member_field_type,
                "q_dummy",
                f"The members of :qapi:type:`{target.name}`.",
                ent.info,
                "q_dummy",
            )

        if isinstance(ent, QAPISchemaObjectType) and ent.branches is not None:
            for variant in ent.branches.variants:
                if variant.type.name == "q_empty":
                    continue
                assert ent.info
                self.add_field(
                    self.member_field_type,
                    "q_dummy",
                    f" When ``{ent.branches.tag_member.name}`` is "
                    f"``{variant.name}``: "
                    f"The members of :qapi:type:`{variant.type.name}`.",
                    ent.info,
                    "q_dummy",
                )

    def visit_sections(self, ent: QAPISchemaDefinition) -> None:
        sections = ent.doc.all_sections if ent.doc else []

        # Determine the index location at which we should generate
        # documentation for "The members of ..." pointers. This should
        # go at the end of the members section(s) if any. Note that
        # index 0 is assumed to be a plain intro section, even if it is
        # empty; and that a members section if present will always
        # immediately follow the opening PLAIN section.
        gen_index = 1
        if len(sections) > 1:
            while sections[gen_index].kind == QAPIDoc.Kind.MEMBER:
                gen_index += 1
                if gen_index >= len(sections):
                    break

        # Add sections in source order:
        for i, section in enumerate(sections):
            section.text = self.reformat_arobase(section.text)

            if section.kind == QAPIDoc.Kind.PLAIN:
                self.visit_paragraph(section)
            elif section.kind == QAPIDoc.Kind.MEMBER:
                assert isinstance(section, QAPIDoc.ArgSection)
                self.visit_member(section)
            elif section.kind == QAPIDoc.Kind.FEATURE:
                assert isinstance(section, QAPIDoc.ArgSection)
                self.visit_feature(section)
            elif section.kind in (QAPIDoc.Kind.SINCE, QAPIDoc.Kind.TODO):
                # Since is handled in preamble, TODO is skipped intentionally.
                pass
            elif section.kind == QAPIDoc.Kind.RETURNS:
                self.visit_returns(section)
            elif section.kind == QAPIDoc.Kind.ERRORS:
                self.visit_errors(section)
            else:
                assert False

            # Generate "The members of ..." entries if necessary:
            if i == gen_index - 1:
                self._insert_member_pointer(ent)

        self.ensure_blank_line()

    # Transmogrification core methods

    def visit_module(self, path: str) -> None:
        name = Path(path).stem
        # module directives are credited to the first line of a module file.
        self.add_line_raw(f".. qapi:module:: {name}", path, 1)
        self.ensure_blank_line()

    def visit_freeform(self, doc: QAPIDoc) -> None:
        assert len(doc.all_sections) == 1, doc.all_sections
        body = doc.all_sections[0]
        self.add_lines(self.reformat_arobase(body.text), doc.info)
        self.ensure_blank_line()

    def visit_entity(self, ent: QAPISchemaDefinition) -> None:
        assert ent.info

        try:
            self._curr_ent = ent

            # Squish structs and unions together into an "object" directive.
            meta = ent.meta
            if meta in ("struct", "union"):
                meta = "object"

            # This line gets credited to the start of the /definition/.
            self.add_line(f".. qapi:{meta}:: {ent.name}", ent.info)
            with self.indented():
                self.preamble(ent)
                self.visit_sections(ent)
        finally:
            self._curr_ent = None

    def set_namespace(self, namespace: str, source: str, lineno: int) -> None:
        self.add_line_raw(
            f".. qapi:namespace:: {namespace}", source, lineno + 1
        )
        self.ensure_blank_line()


class QAPISchemaGenDepVisitor(QAPISchemaVisitor):
    """A QAPI schema visitor which adds Sphinx dependencies each module

    This class calls the Sphinx note_dependency() function to tell Sphinx
    that the generated documentation output depends on the input
    schema file associated with each module in the QAPI input.
    """

    def __init__(self, env: Any, qapidir: str) -> None:
        self._env = env
        self._qapidir = qapidir

    def visit_module(self, name: str) -> None:
        if name != "./builtin":
            qapifile = self._qapidir + "/" + name
            self._env.note_dependency(os.path.abspath(qapifile))
        super().visit_module(name)


class NestedDirective(SphinxDirective):
    def run(self) -> Sequence[nodes.Node]:
        raise NotImplementedError

    def do_parse(self, rstlist: StringList, node: nodes.Node) -> None:
        """
        Parse rST source lines and add them to the specified node

        Take the list of rST source lines rstlist, parse them as
        rST, and add the resulting docutils nodes as children of node.
        The nodes are parsed in a way that allows them to include
        subheadings (titles) without confusing the rendering of
        anything else.
        """
        with switch_source_input(self.state, rstlist):
            nested_parse_with_titles(self.state, rstlist, node)


class QAPIDocDirective(NestedDirective):
    """Extract documentation from the specified QAPI .json file"""

    required_argument = 1
    optional_arguments = 1
    option_spec = {
        "qapifile": directives.unchanged_required,
        "namespace": directives.unchanged,
    }
    has_content = False

    def transmogrify(self, schema: QAPISchema) -> nodes.Element:
        logger.info("Transmogrifying QAPI to rST ...")
        vis = Transmogrifier()
        modules = set()

        if "namespace" in self.options:
            vis.set_namespace(
                self.options["namespace"], *self.get_source_info()
            )

        for doc in schema.docs:
            module_source = doc.info.fname
            if module_source not in modules:
                vis.visit_module(module_source)
                modules.add(module_source)

            if doc.symbol:
                ent = schema.lookup_entity(doc.symbol)
                assert isinstance(ent, QAPISchemaDefinition)
                vis.visit_entity(ent)
            else:
                vis.visit_freeform(doc)

        logger.info("Transmogrification complete.")

        contentnode = nodes.section()
        content = vis.result
        titles_allowed = True

        logger.info("Transmogrifier running nested parse ...")
        with switch_source_input(self.state, content):
            if titles_allowed:
                node: nodes.Element = nodes.section()
                node.document = self.state.document
                nested_parse_with_titles(self.state, content, contentnode)
            else:
                node = nodes.paragraph()
                node.document = self.state.document
                self.state.nested_parse(content, 0, contentnode)
        logger.info("Transmogrifier's nested parse completed.")

        if self.env.app.verbosity >= 2 or os.environ.get("DEBUG"):
            argname = "_".join(Path(self.arguments[0]).parts)
            name = Path(argname).stem + ".ir"
            self.write_intermediate(content, name)

        sys.stdout.flush()
        return contentnode

    def write_intermediate(self, content: StringList, filename: str) -> None:
        logger.info(
            "writing intermediate rST for '%s' to '%s'",
            self.arguments[0],
            filename,
        )

        srctree = Path(self.env.app.config.qapidoc_srctree).resolve()
        outlines = []
        lcol_width = 0

        for i, line in enumerate(content):
            src, lineno = content.info(i)
            srcpath = Path(src).resolve()
            srcpath = srcpath.relative_to(srctree)

            lcol = f"{srcpath}:{lineno:04d}"
            lcol_width = max(lcol_width, len(lcol))
            outlines.append((lcol, line))

        with open(filename, "w", encoding="UTF-8") as outfile:
            for lcol, rcol in outlines:
                outfile.write(lcol.rjust(lcol_width))
                outfile.write(" |")
                if rcol:
                    outfile.write(f" {rcol}")
                outfile.write("\n")

    def run(self) -> Sequence[nodes.Node]:
        env = self.state.document.settings.env
        qapifile = env.config.qapidoc_srctree + "/" + self.arguments[0]
        qapidir = os.path.dirname(qapifile)

        try:
            schema = QAPISchema(qapifile)

            # First tell Sphinx about all the schema files that the
            # output documentation depends on (including 'qapifile' itself)
            schema.visit(QAPISchemaGenDepVisitor(env, qapidir))
        except QAPIError as err:
            # Launder QAPI parse errors into Sphinx extension errors
            # so they are displayed nicely to the user
            raise ExtensionError(str(err)) from err

        contentnode = self.transmogrify(schema)
        return contentnode.children


class QMPExample(CodeBlock, NestedDirective):
    """
    Custom admonition for QMP code examples.

    When the :annotated: option is present, the body of this directive
    is parsed as normal rST, but with any '::' code blocks set to use
    the QMP lexer. Code blocks must be explicitly written by the user,
    but this allows for intermingling explanatory paragraphs with
    arbitrary rST syntax and code blocks for more involved examples.

    When :annotated: is absent, the directive body is treated as a
    simple standalone QMP code block literal.
    """

    required_argument = 0
    optional_arguments = 0
    has_content = True
    option_spec = {
        "annotated": directives.flag,
        "title": directives.unchanged,
    }

    def _highlightlang(self) -> addnodes.highlightlang:
        """Return the current highlightlang setting for the document"""
        node = None
        doc = self.state.document

        if hasattr(doc, "findall"):
            # docutils >= 0.18.1
            for node in doc.findall(addnodes.highlightlang):
                pass
        else:
            for elem in doc.traverse():
                if isinstance(elem, addnodes.highlightlang):
                    node = elem

        if node:
            return node

        # No explicit directive found, use defaults
        node = addnodes.highlightlang(
            lang=self.env.config.highlight_language,
            force=False,
            # Yes, Sphinx uses this value to effectively disable line
            # numbers and not 0 or None or -1 or something. ¯\_(ツ)_/¯
            linenothreshold=sys.maxsize,
        )
        return node

    def admonition_wrap(self, *content: nodes.Node) -> List[nodes.Node]:
        title = "Example:"
        if "title" in self.options:
            title = f"{title} {self.options['title']}"

        admon = nodes.admonition(
            "",
            nodes.title("", title),
            *content,
            classes=["admonition", "admonition-example"],
        )
        return [admon]

    def run_annotated(self) -> List[nodes.Node]:
        lang_node = self._highlightlang()

        content_node: nodes.Element = nodes.section()

        # Configure QMP highlighting for "::" blocks, if needed
        if lang_node["lang"] != "QMP":
            content_node += addnodes.highlightlang(
                lang="QMP",
                force=False,  # "True" ignores lexing errors
                linenothreshold=lang_node["linenothreshold"],
            )

        self.do_parse(self.content, content_node)

        # Restore prior language highlighting, if needed
        if lang_node["lang"] != "QMP":
            content_node += addnodes.highlightlang(**lang_node.attributes)

        return content_node.children

    def run(self) -> List[nodes.Node]:
        annotated = "annotated" in self.options

        if annotated:
            content_nodes = self.run_annotated()
        else:
            self.arguments = ["QMP"]
            content_nodes = super().run()

        return self.admonition_wrap(*content_nodes)


def setup(app: Sphinx) -> ExtensionMetadata:
    """Register qapi-doc directive with Sphinx"""
    app.setup_extension("qapi_domain")
    app.add_config_value("qapidoc_srctree", None, "env")
    app.add_directive("qapi-doc", QAPIDocDirective)
    app.add_directive("qmp-example", QMPExample)

    return {
        "version": __version__,
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
