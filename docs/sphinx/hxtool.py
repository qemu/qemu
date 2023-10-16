# coding=utf-8
#
# QEMU hxtool .hx file parsing extension
#
# Copyright (c) 2020 Linaro
#
# This work is licensed under the terms of the GNU GPLv2 or later.
# See the COPYING file in the top-level directory.
"""hxtool is a Sphinx extension that implements the hxtool-doc directive"""

# The purpose of this extension is to read fragments of rST
# from .hx files, and insert them all into the current document.
# The rST fragments are delimited by SRST/ERST lines.
# The conf.py file must set the hxtool_srctree config value to
# the root of the QEMU source tree.
# Each hxtool-doc:: directive takes one argument which is the
# path of the .hx file to process, relative to the source tree.

import os
import re
from enum import Enum

from docutils import nodes
from docutils.statemachine import ViewList
from docutils.parsers.rst import directives, Directive
from sphinx.errors import ExtensionError
from sphinx.util.nodes import nested_parse_with_titles
import sphinx

# Sphinx up to 1.6 uses AutodocReporter; 1.7 and later
# use switch_source_input. Check borrowed from kerneldoc.py.
Use_SSI = sphinx.__version__[:3] >= '1.7'
if Use_SSI:
    from sphinx.util.docutils import switch_source_input
else:
    from sphinx.ext.autodoc import AutodocReporter

__version__ = '1.0'

# We parse hx files with a state machine which may be in one of two
# states: reading the C code fragment, or inside a rST fragment.
class HxState(Enum):
    CTEXT = 1
    RST = 2

def serror(file, lnum, errtext):
    """Raise an exception giving a user-friendly syntax error message"""
    raise ExtensionError('%s line %d: syntax error: %s' % (file, lnum, errtext))

def parse_directive(line):
    """Return first word of line, if any"""
    return re.split(r'\W', line)[0]

def parse_defheading(file, lnum, line):
    """Handle a DEFHEADING directive"""
    # The input should be "DEFHEADING(some string)", though note that
    # the 'some string' could be the empty string. If the string is
    # empty we ignore the directive -- these are used only to add
    # blank lines in the plain-text content of the --help output.
    #
    # Return the heading text. We strip out any trailing ':' for
    # consistency with other headings in the rST documentation.
    match = re.match(r'DEFHEADING\((.*?):?\)', line)
    if match is None:
        serror(file, lnum, "Invalid DEFHEADING line")
    return match.group(1)

def parse_archheading(file, lnum, line):
    """Handle an ARCHHEADING directive"""
    # The input should be "ARCHHEADING(some string, other arg)",
    # though note that the 'some string' could be the empty string.
    # As with DEFHEADING, empty string ARCHHEADINGs will be ignored.
    #
    # Return the heading text. We strip out any trailing ':' for
    # consistency with other headings in the rST documentation.
    match = re.match(r'ARCHHEADING\((.*?):?,.*\)', line)
    if match is None:
        serror(file, lnum, "Invalid ARCHHEADING line")
    return match.group(1)

class HxtoolDocDirective(Directive):
    """Extract rST fragments from the specified .hx file"""
    required_argument = 1
    optional_arguments = 1
    option_spec = {
        'hxfile': directives.unchanged_required
    }
    has_content = False

    def run(self):
        env = self.state.document.settings.env
        hxfile = env.config.hxtool_srctree + '/' + self.arguments[0]

        # Tell sphinx of the dependency
        env.note_dependency(os.path.abspath(hxfile))

        state = HxState.CTEXT
        # We build up lines of rST in this ViewList, which we will
        # later put into a 'section' node.
        rstlist = ViewList()
        current_node = None
        node_list = []

        with open(hxfile) as f:
            lines = (l.rstrip() for l in f)
            for lnum, line in enumerate(lines, 1):
                directive = parse_directive(line)

                if directive == 'HXCOMM':
                    pass
                elif directive == 'SRST':
                    if state == HxState.RST:
                        serror(hxfile, lnum, 'expected ERST, found SRST')
                    else:
                        state = HxState.RST
                elif directive == 'ERST':
                    if state == HxState.CTEXT:
                        serror(hxfile, lnum, 'expected SRST, found ERST')
                    else:
                        state = HxState.CTEXT
                elif directive == 'DEFHEADING' or directive == 'ARCHHEADING':
                    if directive == 'DEFHEADING':
                        heading = parse_defheading(hxfile, lnum, line)
                    else:
                        heading = parse_archheading(hxfile, lnum, line)
                    if heading == "":
                        continue
                    # Put the accumulated rST into the previous node,
                    # and then start a fresh section with this heading.
                    if len(rstlist) > 0:
                        if current_node is None:
                            # We had some rST fragments before the first
                            # DEFHEADING. We don't have a section to put
                            # these in, so rather than magicing up a section,
                            # make it a syntax error.
                            serror(hxfile, lnum,
                                   'first DEFHEADING must precede all rST text')
                        self.do_parse(rstlist, current_node)
                        rstlist = ViewList()
                    if current_node is not None:
                        node_list.append(current_node)
                    section_id = 'hxtool-%d' % env.new_serialno('hxtool')
                    current_node = nodes.section(ids=[section_id])
                    current_node += nodes.title(heading, heading)
                else:
                    # Not a directive: put in output if we are in rST fragment
                    if state == HxState.RST:
                        # Sphinx counts its lines from 0
                        rstlist.append(line, hxfile, lnum - 1)

        if current_node is None:
            # We don't have multiple sections, so just parse the rst
            # fragments into a dummy node so we can return the children.
            current_node = nodes.section()
            self.do_parse(rstlist, current_node)
            return current_node.children
        else:
            # Put the remaining accumulated rST into the last section, and
            # return all the sections.
            if len(rstlist) > 0:
                self.do_parse(rstlist, current_node)
            node_list.append(current_node)
            return node_list

    # This is from kerneldoc.py -- it works around an API change in
    # Sphinx between 1.6 and 1.7. Unlike kerneldoc.py, we use
    # sphinx.util.nodes.nested_parse_with_titles() rather than the
    # plain self.state.nested_parse(), and so we can drop the saving
    # of title_styles and section_level that kerneldoc.py does,
    # because nested_parse_with_titles() does that for us.
    def do_parse(self, result, node):
        if Use_SSI:
            with switch_source_input(self.state, result):
                nested_parse_with_titles(self.state, result, node)
        else:
            save = self.state.memo.reporter
            self.state.memo.reporter = AutodocReporter(result, self.state.memo.reporter)
            try:
                nested_parse_with_titles(self.state, result, node)
            finally:
                self.state.memo.reporter = save

def setup(app):
    """ Register hxtool-doc directive with Sphinx"""
    app.add_config_value('hxtool_srctree', None, 'env')
    app.add_directive('hxtool-doc', HxtoolDocDirective)

    return dict(
        version = __version__,
        parallel_read_safe = True,
        parallel_write_safe = True
    )
