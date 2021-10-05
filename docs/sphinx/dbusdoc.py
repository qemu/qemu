# D-Bus XML documentation extension
#
# Copyright (C) 2021, Red Hat Inc.
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Author: Marc-André Lureau <marcandre.lureau@redhat.com>
"""dbus-doc is a Sphinx extension that provides documentation from D-Bus XML."""

import os
import re
from typing import (
    TYPE_CHECKING,
    Any,
    Callable,
    Dict,
    Iterator,
    List,
    Optional,
    Sequence,
    Set,
    Tuple,
    Type,
    TypeVar,
    Union,
)

import sphinx
from docutils import nodes
from docutils.nodes import Element, Node
from docutils.parsers.rst import Directive, directives
from docutils.parsers.rst.states import RSTState
from docutils.statemachine import StringList, ViewList
from sphinx.application import Sphinx
from sphinx.errors import ExtensionError
from sphinx.util import logging
from sphinx.util.docstrings import prepare_docstring
from sphinx.util.docutils import SphinxDirective, switch_source_input
from sphinx.util.nodes import nested_parse_with_titles

import dbusdomain
from dbusparser import parse_dbus_xml

logger = logging.getLogger(__name__)

__version__ = "1.0"


class DBusDoc:
    def __init__(self, sphinx_directive, dbusfile):
        self._cur_doc = None
        self._sphinx_directive = sphinx_directive
        self._dbusfile = dbusfile
        self._top_node = nodes.section()
        self.result = StringList()
        self.indent = ""

    def add_line(self, line: str, *lineno: int) -> None:
        """Append one line of generated reST to the output."""
        if line.strip():  # not a blank line
            self.result.append(self.indent + line, self._dbusfile, *lineno)
        else:
            self.result.append("", self._dbusfile, *lineno)

    def add_method(self, method):
        self.add_line(f".. dbus:method:: {method.name}")
        self.add_line("")
        self.indent += "   "
        for arg in method.in_args:
            self.add_line(f":arg {arg.signature} {arg.name}: {arg.doc_string}")
        for arg in method.out_args:
            self.add_line(f":ret {arg.signature} {arg.name}: {arg.doc_string}")
        self.add_line("")
        for line in prepare_docstring("\n" + method.doc_string):
            self.add_line(line)
        self.indent = self.indent[:-3]

    def add_signal(self, signal):
        self.add_line(f".. dbus:signal:: {signal.name}")
        self.add_line("")
        self.indent += "   "
        for arg in signal.args:
            self.add_line(f":arg {arg.signature} {arg.name}: {arg.doc_string}")
        self.add_line("")
        for line in prepare_docstring("\n" + signal.doc_string):
            self.add_line(line)
        self.indent = self.indent[:-3]

    def add_property(self, prop):
        self.add_line(f".. dbus:property:: {prop.name}")
        self.indent += "   "
        self.add_line(f":type: {prop.signature}")
        access = {"read": "readonly", "write": "writeonly", "readwrite": "readwrite"}[
            prop.access
        ]
        self.add_line(f":{access}:")
        if prop.emits_changed_signal:
            self.add_line(f":emits-changed: yes")
        self.add_line("")
        for line in prepare_docstring("\n" + prop.doc_string):
            self.add_line(line)
        self.indent = self.indent[:-3]

    def add_interface(self, iface):
        self.add_line(f".. dbus:interface:: {iface.name}")
        self.add_line("")
        self.indent += "   "
        for line in prepare_docstring("\n" + iface.doc_string):
            self.add_line(line)
        for method in iface.methods:
            self.add_method(method)
        for sig in iface.signals:
            self.add_signal(sig)
        for prop in iface.properties:
            self.add_property(prop)
        self.indent = self.indent[:-3]


def parse_generated_content(state: RSTState, content: StringList) -> List[Node]:
    """Parse a generated content by Documenter."""
    with switch_source_input(state, content):
        node = nodes.paragraph()
        node.document = state.document
        state.nested_parse(content, 0, node)

        return node.children


class DBusDocDirective(SphinxDirective):
    """Extract documentation from the specified D-Bus XML file"""

    has_content = True
    required_arguments = 1
    optional_arguments = 0
    final_argument_whitespace = True

    def run(self):
        reporter = self.state.document.reporter

        try:
            source, lineno = reporter.get_source_and_line(self.lineno)  # type: ignore
        except AttributeError:
            source, lineno = (None, None)

        logger.debug("[dbusdoc] %s:%s: input:\n%s", source, lineno, self.block_text)

        env = self.state.document.settings.env
        dbusfile = env.config.qapidoc_srctree + "/" + self.arguments[0]
        with open(dbusfile, "rb") as f:
            xml_data = f.read()
        xml = parse_dbus_xml(xml_data)
        doc = DBusDoc(self, dbusfile)
        for iface in xml:
            doc.add_interface(iface)

        result = parse_generated_content(self.state, doc.result)
        return result


def setup(app: Sphinx) -> Dict[str, Any]:
    """Register dbus-doc directive with Sphinx"""
    app.add_config_value("dbusdoc_srctree", None, "env")
    app.add_directive("dbus-doc", DBusDocDirective)
    dbusdomain.setup(app)

    return dict(version=__version__, parallel_read_safe=True, parallel_write_safe=True)
