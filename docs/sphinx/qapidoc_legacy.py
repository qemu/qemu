# coding=utf-8
# type: ignore
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

import re
import textwrap

from docutils import nodes
from docutils.statemachine import ViewList
from qapi.error import QAPISemError
from qapi.gen import QAPISchemaVisitor
from qapi.parser import QAPIDoc


def dedent(text: str) -> str:
    # Adjust indentation to make description text parse as paragraph.

    lines = text.splitlines(True)
    if re.match(r"\s+", lines[0]):
        # First line is indented; description started on the line after
        # the name. dedent the whole block.
        return textwrap.dedent(text)

    # Descr started on same line. Dedent line 2+.
    return lines[0] + textwrap.dedent("".join(lines[1:]))


class QAPISchemaGenRSTVisitor(QAPISchemaVisitor):
    """A QAPI schema visitor which generates docutils/Sphinx nodes

    This class builds up a tree of docutils/Sphinx nodes corresponding
    to documentation for the various QAPI objects. To use it, first
    create a QAPISchemaGenRSTVisitor object, and call its
    visit_begin() method.  Then you can call one of the two methods
    'freeform' (to add documentation for a freeform documentation
    chunk) or 'symbol' (to add documentation for a QAPI symbol). These
    will cause the visitor to build up the tree of document
    nodes. Once you've added all the documentation via 'freeform' and
    'symbol' method calls, you can call 'get_document_nodes' to get
    the final list of document nodes (in a form suitable for returning
    from a Sphinx directive's 'run' method).
    """
    def __init__(self, sphinx_directive):
        self._cur_doc = None
        self._sphinx_directive = sphinx_directive
        self._top_node = nodes.section()
        self._active_headings = [self._top_node]

    def _make_dlitem(self, term, defn):
        """Return a dlitem node with the specified term and definition.

        term should be a list of Text and literal nodes.
        defn should be one of:
        - a string, which will be handed to _parse_text_into_node
        - a list of Text and literal nodes, which will be put into
          a paragraph node
        """
        dlitem = nodes.definition_list_item()
        dlterm = nodes.term('', '', *term)
        dlitem += dlterm
        if defn:
            dldef = nodes.definition()
            if isinstance(defn, list):
                dldef += nodes.paragraph('', '', *defn)
            else:
                self._parse_text_into_node(defn, dldef)
            dlitem += dldef
        return dlitem

    def _make_section(self, title):
        """Return a section node with optional title"""
        section = nodes.section(ids=[self._sphinx_directive.new_serialno()])
        if title:
            section += nodes.title(title, title)
        return section

    def _nodes_for_ifcond(self, ifcond, with_if=True):
        """Return list of Text, literal nodes for the ifcond

        Return a list which gives text like ' (If: condition)'.
        If with_if is False, we don't return the "(If: " and ")".
        """

        doc = ifcond.docgen()
        if not doc:
            return []
        doc = nodes.literal('', doc)
        if not with_if:
            return [doc]

        nodelist = [nodes.Text(' ('), nodes.strong('', 'If: ')]
        nodelist.append(doc)
        nodelist.append(nodes.Text(')'))
        return nodelist

    def _nodes_for_one_member(self, member):
        """Return list of Text, literal nodes for this member

        Return a list of doctree nodes which give text like
        'name: type (optional) (If: ...)' suitable for use as the
        'term' part of a definition list item.
        """
        term = [nodes.literal('', member.name)]
        if member.type.doc_type():
            term.append(nodes.Text(': '))
            term.append(nodes.literal('', member.type.doc_type()))
        if member.optional:
            term.append(nodes.Text(' (optional)'))
        if member.ifcond.is_present():
            term.extend(self._nodes_for_ifcond(member.ifcond))
        return term

    def _nodes_for_variant_when(self, branches, variant):
        """Return list of Text, literal nodes for variant 'when' clause

        Return a list of doctree nodes which give text like
        'when tagname is variant (If: ...)' suitable for use in
        the 'branches' part of a definition list.
        """
        term = [nodes.Text(' when '),
                nodes.literal('', branches.tag_member.name),
                nodes.Text(' is '),
                nodes.literal('', '"%s"' % variant.name)]
        if variant.ifcond.is_present():
            term.extend(self._nodes_for_ifcond(variant.ifcond))
        return term

    def _nodes_for_members(self, doc, what, base=None, branches=None):
        """Return list of doctree nodes for the table of members"""
        dlnode = nodes.definition_list()
        for section in doc.args.values():
            term = self._nodes_for_one_member(section.member)
            # TODO drop fallbacks when undocumented members are outlawed
            if section.text:
                defn = dedent(section.text)
            else:
                defn = [nodes.Text('Not documented')]

            dlnode += self._make_dlitem(term, defn)

        if base:
            dlnode += self._make_dlitem([nodes.Text('The members of '),
                                         nodes.literal('', base.doc_type())],
                                        None)

        if branches:
            for v in branches.variants:
                if v.type.name == 'q_empty':
                    continue
                assert not v.type.is_implicit()
                term = [nodes.Text('The members of '),
                        nodes.literal('', v.type.doc_type())]
                term.extend(self._nodes_for_variant_when(branches, v))
                dlnode += self._make_dlitem(term, None)

        if not dlnode.children:
            return []

        section = self._make_section(what)
        section += dlnode
        return [section]

    def _nodes_for_enum_values(self, doc):
        """Return list of doctree nodes for the table of enum values"""
        seen_item = False
        dlnode = nodes.definition_list()
        for section in doc.args.values():
            termtext = [nodes.literal('', section.member.name)]
            if section.member.ifcond.is_present():
                termtext.extend(self._nodes_for_ifcond(section.member.ifcond))
            # TODO drop fallbacks when undocumented members are outlawed
            if section.text:
                defn = dedent(section.text)
            else:
                defn = [nodes.Text('Not documented')]

            dlnode += self._make_dlitem(termtext, defn)
            seen_item = True

        if not seen_item:
            return []

        section = self._make_section('Values')
        section += dlnode
        return [section]

    def _nodes_for_arguments(self, doc, arg_type):
        """Return list of doctree nodes for the arguments section"""
        if arg_type and not arg_type.is_implicit():
            assert not doc.args
            section = self._make_section('Arguments')
            dlnode = nodes.definition_list()
            dlnode += self._make_dlitem(
                [nodes.Text('The members of '),
                 nodes.literal('', arg_type.name)],
                None)
            section += dlnode
            return [section]

        return self._nodes_for_members(doc, 'Arguments')

    def _nodes_for_features(self, doc):
        """Return list of doctree nodes for the table of features"""
        seen_item = False
        dlnode = nodes.definition_list()
        for section in doc.features.values():
            dlnode += self._make_dlitem(
                [nodes.literal('', section.member.name)], dedent(section.text))
            seen_item = True

        if not seen_item:
            return []

        section = self._make_section('Features')
        section += dlnode
        return [section]

    def _nodes_for_sections(self, doc):
        """Return list of doctree nodes for additional sections"""
        nodelist = []
        for section in doc.sections:
            if section.kind == QAPIDoc.Kind.TODO:
                # Hide TODO: sections
                continue

            if section.kind == QAPIDoc.Kind.PLAIN:
                # Sphinx cannot handle sectionless titles;
                # Instead, just append the results to the prior section.
                container = nodes.container()
                self._parse_text_into_node(section.text, container)
                nodelist += container.children
                continue

            snode = self._make_section(section.kind.name.title())
            self._parse_text_into_node(dedent(section.text), snode)
            nodelist.append(snode)
        return nodelist

    def _nodes_for_if_section(self, ifcond):
        """Return list of doctree nodes for the "If" section"""
        nodelist = []
        if ifcond.is_present():
            snode = self._make_section('If')
            snode += nodes.paragraph(
                '', '', *self._nodes_for_ifcond(ifcond, with_if=False)
            )
            nodelist.append(snode)
        return nodelist

    def _add_doc(self, typ, sections):
        """Add documentation for a command/object/enum...

        We assume we're documenting the thing defined in self._cur_doc.
        typ is the type of thing being added ("Command", "Object", etc)

        sections is a list of nodes for sections to add to the definition.
        """

        doc = self._cur_doc
        snode = nodes.section(ids=[self._sphinx_directive.new_serialno()])
        snode += nodes.title('', '', *[nodes.literal(doc.symbol, doc.symbol),
                                       nodes.Text(' (' + typ + ')')])
        self._parse_text_into_node(doc.body.text, snode)
        for s in sections:
            if s is not None:
                snode += s
        self._add_node_to_current_heading(snode)

    def visit_enum_type(self, name, info, ifcond, features, members, prefix):
        doc = self._cur_doc
        self._add_doc('Enum',
                      self._nodes_for_enum_values(doc)
                      + self._nodes_for_features(doc)
                      + self._nodes_for_sections(doc)
                      + self._nodes_for_if_section(ifcond))

    def visit_object_type(self, name, info, ifcond, features,
                          base, members, branches):
        doc = self._cur_doc
        if base and base.is_implicit():
            base = None
        self._add_doc('Object',
                      self._nodes_for_members(doc, 'Members', base, branches)
                      + self._nodes_for_features(doc)
                      + self._nodes_for_sections(doc)
                      + self._nodes_for_if_section(ifcond))

    def visit_alternate_type(self, name, info, ifcond, features,
                             alternatives):
        doc = self._cur_doc
        self._add_doc('Alternate',
                      self._nodes_for_members(doc, 'Members')
                      + self._nodes_for_features(doc)
                      + self._nodes_for_sections(doc)
                      + self._nodes_for_if_section(ifcond))

    def visit_command(self, name, info, ifcond, features, arg_type,
                      ret_type, gen, success_response, boxed, allow_oob,
                      allow_preconfig, coroutine):
        doc = self._cur_doc
        self._add_doc('Command',
                      self._nodes_for_arguments(doc, arg_type)
                      + self._nodes_for_features(doc)
                      + self._nodes_for_sections(doc)
                      + self._nodes_for_if_section(ifcond))

    def visit_event(self, name, info, ifcond, features, arg_type, boxed):
        doc = self._cur_doc
        self._add_doc('Event',
                      self._nodes_for_arguments(doc, arg_type)
                      + self._nodes_for_features(doc)
                      + self._nodes_for_sections(doc)
                      + self._nodes_for_if_section(ifcond))

    def symbol(self, doc, entity):
        """Add documentation for one symbol to the document tree

        This is the main entry point which causes us to add documentation
        nodes for a symbol (which could be a 'command', 'object', 'event',
        etc). We do this by calling 'visit' on the schema entity, which
        will then call back into one of our visit_* methods, depending
        on what kind of thing this symbol is.
        """
        self._cur_doc = doc
        entity.visit(self)
        self._cur_doc = None

    def _start_new_heading(self, heading, level):
        """Start a new heading at the specified heading level

        Create a new section whose title is 'heading' and which is placed
        in the docutils node tree as a child of the most recent level-1
        heading. Subsequent document sections (commands, freeform doc chunks,
        etc) will be placed as children of this new heading section.
        """
        if len(self._active_headings) < level:
            raise QAPISemError(self._cur_doc.info,
                               'Level %d subheading found outside a '
                               'level %d heading'
                               % (level, level - 1))
        snode = self._make_section(heading)
        self._active_headings[level - 1] += snode
        self._active_headings = self._active_headings[:level]
        self._active_headings.append(snode)
        return snode

    def _add_node_to_current_heading(self, node):
        """Add the node to whatever the current active heading is"""
        self._active_headings[-1] += node

    def freeform(self, doc):
        """Add a piece of 'freeform' documentation to the document tree

        A 'freeform' document chunk doesn't relate to any particular
        symbol (for instance, it could be an introduction).

        If the freeform document starts with a line of the form
        '= Heading text', this is a section or subsection heading, with
        the heading level indicated by the number of '=' signs.
        """

        # QAPIDoc documentation says free-form documentation blocks
        # must have only a body section, nothing else.
        assert not doc.sections
        assert not doc.args
        assert not doc.features
        self._cur_doc = doc

        text = doc.body.text
        if re.match(r'=+ ', text):
            # Section/subsection heading (if present, will always be
            # the first line of the block)
            (heading, _, text) = text.partition('\n')
            (leader, _, heading) = heading.partition(' ')
            node = self._start_new_heading(heading, len(leader))
            if text == '':
                return
        else:
            node = nodes.container()

        self._parse_text_into_node(text, node)
        self._cur_doc = None

    def _parse_text_into_node(self, doctext, node):
        """Parse a chunk of QAPI-doc-format text into the node

        The doc comment can contain most inline rST markup, including
        bulleted and enumerated lists.
        As an extra permitted piece of markup, @var will be turned
        into ``var``.
        """

        # Handle the "@var means ``var`` case
        doctext = re.sub(r'@([\w-]+)', r'``\1``', doctext)

        rstlist = ViewList()
        for line in doctext.splitlines():
            # The reported line number will always be that of the start line
            # of the doc comment, rather than the actual location of the error.
            # Being more precise would require overhaul of the QAPIDoc class
            # to track lines more exactly within all the sub-parts of the doc
            # comment, as well as counting lines here.
            rstlist.append(line, self._cur_doc.info.fname,
                           self._cur_doc.info.line)
        # Append a blank line -- in some cases rST syntax errors get
        # attributed to the line after one with actual text, and if there
        # isn't anything in the ViewList corresponding to that then Sphinx
        # 1.6's AutodocReporter will then misidentify the source/line location
        # in the error message (usually attributing it to the top-level
        # .rst file rather than the offending .json file). The extra blank
        # line won't affect the rendered output.
        rstlist.append("", self._cur_doc.info.fname, self._cur_doc.info.line)
        self._sphinx_directive.do_parse(rstlist, node)

    def get_document_node(self):
        """Return the root docutils node which makes up the document"""
        return self._top_node
