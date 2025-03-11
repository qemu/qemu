# coding=utf-8
#
# QEMU qapidoc QAPI file parsing extension
#
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

from contextlib import contextmanager
import os
import sys
from typing import TYPE_CHECKING

from docutils import nodes
from docutils.parsers.rst import Directive, directives
from docutils.statemachine import StringList
from qapi.error import QAPIError
from qapi.schema import QAPISchema, QAPISchemaVisitor
from qapi.source import QAPISourceInfo

from qapidoc_legacy import QAPISchemaGenRSTVisitor  # type: ignore
from sphinx import addnodes
from sphinx.directives.code import CodeBlock
from sphinx.errors import ExtensionError
from sphinx.util.docutils import switch_source_input
from sphinx.util.nodes import nested_parse_with_titles


if TYPE_CHECKING:
    from typing import (
        Any,
        Generator,
        List,
        Sequence,
    )

    from sphinx.application import Sphinx
    from sphinx.util.typing import ExtensionMetadata


__version__ = "1.0"


class Transmogrifier:
    def __init__(self) -> None:
        self._result = StringList()
        self.indent = 0

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


class NestedDirective(Directive):
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
        "transmogrify": directives.flag,
    }
    has_content = False

    def new_serialno(self) -> str:
        """Return a unique new ID string suitable for use as a node's ID"""
        env = self.state.document.settings.env
        return "qapidoc-%d" % env.new_serialno("qapidoc")

    def transmogrify(self, schema: QAPISchema) -> nodes.Element:
        raise NotImplementedError

    def legacy(self, schema: QAPISchema) -> nodes.Element:
        vis = QAPISchemaGenRSTVisitor(self)
        vis.visit_begin(schema)
        for doc in schema.docs:
            if doc.symbol:
                vis.symbol(doc, schema.lookup_entity(doc.symbol))
            else:
                vis.freeform(doc)
        return vis.get_document_node()  # type: ignore

    def run(self) -> Sequence[nodes.Node]:
        env = self.state.document.settings.env
        qapifile = env.config.qapidoc_srctree + "/" + self.arguments[0]
        qapidir = os.path.dirname(qapifile)
        transmogrify = "transmogrify" in self.options

        try:
            schema = QAPISchema(qapifile)

            # First tell Sphinx about all the schema files that the
            # output documentation depends on (including 'qapifile' itself)
            schema.visit(QAPISchemaGenDepVisitor(env, qapidir))
        except QAPIError as err:
            # Launder QAPI parse errors into Sphinx extension errors
            # so they are displayed nicely to the user
            raise ExtensionError(str(err)) from err

        if transmogrify:
            contentnode = self.transmogrify(schema)
        else:
            contentnode = self.legacy(schema)

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
    app.add_config_value("qapidoc_srctree", None, "env")
    app.add_directive("qapi-doc", QAPIDocDirective)
    app.add_directive("qmp-example", QMPExample)

    return {
        "version": __version__,
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
