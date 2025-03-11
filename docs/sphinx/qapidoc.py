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

import os
import sys
from typing import List

from docutils import nodes
from docutils.parsers.rst import Directive, directives
from qapi.error import QAPIError
from qapi.gen import QAPISchemaVisitor
from qapi.schema import QAPISchema

from qapidoc_legacy import QAPISchemaGenRSTVisitor
from sphinx import addnodes
from sphinx.directives.code import CodeBlock
from sphinx.errors import ExtensionError
from sphinx.util.docutils import switch_source_input
from sphinx.util.nodes import nested_parse_with_titles


__version__ = "1.0"


class QAPISchemaGenDepVisitor(QAPISchemaVisitor):
    """A QAPI schema visitor which adds Sphinx dependencies each module

    This class calls the Sphinx note_dependency() function to tell Sphinx
    that the generated documentation output depends on the input
    schema file associated with each module in the QAPI input.
    """

    def __init__(self, env, qapidir):
        self._env = env
        self._qapidir = qapidir

    def visit_module(self, name):
        if name != "./builtin":
            qapifile = self._qapidir + "/" + name
            self._env.note_dependency(os.path.abspath(qapifile))
        super().visit_module(name)


class NestedDirective(Directive):
    def run(self):
        raise NotImplementedError

    def do_parse(self, rstlist, node):
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

    def new_serialno(self):
        """Return a unique new ID string suitable for use as a node's ID"""
        env = self.state.document.settings.env
        return "qapidoc-%d" % env.new_serialno("qapidoc")

    def transmogrify(self, schema) -> nodes.Element:
        raise NotImplementedError

    def legacy(self, schema) -> nodes.Element:
        vis = QAPISchemaGenRSTVisitor(self)
        vis.visit_begin(schema)
        for doc in schema.docs:
            if doc.symbol:
                vis.symbol(doc, schema.lookup_entity(doc.symbol))
            else:
                vis.freeform(doc)
        return vis.get_document_node()

    def run(self):
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

    def admonition_wrap(self, *content) -> List[nodes.Node]:
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


def setup(app):
    """Register qapi-doc directive with Sphinx"""
    app.add_config_value("qapidoc_srctree", None, "env")
    app.add_directive("qapi-doc", QAPIDocDirective)
    app.add_directive("qmp-example", QMPExample)

    return {
        "version": __version__,
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
