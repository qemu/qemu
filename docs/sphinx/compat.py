"""
Sphinx cross-version compatibility goop
"""

from docutils.nodes import Element

from sphinx.util import nodes
from sphinx.util.docutils import SphinxDirective, switch_source_input


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
