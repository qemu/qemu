"""
Sphinx cross-version compatibility goop
"""

from typing import Callable

from docutils.nodes import Element, Node, Text

import sphinx
from sphinx import addnodes
from sphinx.util import nodes
from sphinx.util.docutils import SphinxDirective, switch_source_input


SpaceNode: Callable[[str], Node]
KeywordNode: Callable[[str, str], Node]

if sphinx.version_info[:3] >= (4, 0, 0):
    SpaceNode = addnodes.desc_sig_space
    KeywordNode = addnodes.desc_sig_keyword
else:
    SpaceNode = Text
    KeywordNode = addnodes.desc_annotation


def nested_parse_with_titles(
    directive: SphinxDirective, content_node: Element
) -> None:
    """
    This helper preserves error parsing context across sphinx versions.
    """

    # necessary so that the child nodes get the right source/line set
    content_node.document = directive.state.document

    try:
        # Modern sphinx (6.2.0+) supports proper offsetting for
        # nested parse error context management
        nodes.nested_parse_with_titles(
            directive.state,
            directive.content,
            content_node,
            content_offset=directive.content_offset,
        )
    except TypeError:
        # No content_offset argument. Fall back to SSI method.
        with switch_source_input(directive.state, directive.content):
            nodes.nested_parse_with_titles(
                directive.state, directive.content, content_node
            )
