"""
Sphinx cross-version compatibility goop
"""

import re
from typing import (
    TYPE_CHECKING,
    Any,
    Callable,
    Optional,
    Type,
)

from docutils import nodes
from docutils.nodes import Element, Node, Text
from docutils.statemachine import StringList

import sphinx
from sphinx import addnodes, util
from sphinx.directives import ObjectDescription
from sphinx.environment import BuildEnvironment
from sphinx.roles import XRefRole
from sphinx.util import docfields
from sphinx.util.docutils import (
    ReferenceRole,
    SphinxDirective,
    switch_source_input,
)
from sphinx.util.typing import TextlikeNode


MAKE_XREF_WORKAROUND = sphinx.version_info[:3] < (4, 1, 0)


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
        util.nodes.nested_parse_with_titles(
            directive.state,
            directive.content,
            content_node,
            content_offset=directive.content_offset,
        )
    except TypeError:
        # No content_offset argument. Fall back to SSI method.
        with switch_source_input(directive.state, directive.content):
            util.nodes.nested_parse_with_titles(
                directive.state, directive.content, content_node
            )


# ###########################################
# xref compatibility hacks for Sphinx < 4.1 #
# ###########################################

# When we require >= Sphinx 4.1, the following function and the
# subsequent 3 compatibility classes can be removed. Anywhere in
# qapi_domain that uses one of these Compat* types can be switched to
# using the garden-variety lib-provided classes with no trickery.


def _compat_make_xref(  # pylint: disable=unused-argument
    self: sphinx.util.docfields.Field,
    rolename: str,
    domain: str,
    target: str,
    innernode: Type[TextlikeNode] = addnodes.literal_emphasis,
    contnode: Optional[Node] = None,
    env: Optional[BuildEnvironment] = None,
    inliner: Any = None,
    location: Any = None,
) -> Node:
    """
    Compatibility workaround for Sphinx versions prior to 4.1.0.

    Older sphinx versions do not use the domain's XRefRole for parsing
    and formatting cross-references, so we need to perform this magick
    ourselves to avoid needing to write the parser/formatter in two
    separate places.

    This workaround isn't brick-for-brick compatible with modern Sphinx
    versions, because we do not have access to the parent directive's
    state during this parsing like we do in more modern versions.

    It's no worse than what pre-Sphinx 4.1.0 does, so... oh well!
    """

    # Yes, this function is gross. Pre-4.1 support is a miracle.
    # pylint: disable=too-many-locals

    assert env
    # Note: Sphinx's own code ignores the type warning here, too.
    if not rolename:
        return contnode or innernode(target, target)  # type: ignore[call-arg]

    # Get the role instance, but don't *execute it* - we lack the
    # correct state to do so. Instead, we'll just use its public
    # methods to do our reference formatting, and emulate the rest.
    role = env.get_domain(domain).roles[rolename]
    assert isinstance(role, XRefRole)

    # XRefRole features not supported by this compatibility shim;
    # these were not supported in Sphinx 3.x either, so nothing of
    # value is really lost.
    assert not target.startswith("!")
    assert not re.match(ReferenceRole.explicit_title_re, target)
    assert not role.lowercase
    assert not role.fix_parens

    # Code below based mostly on sphinx.roles.XRefRole; run() and
    # create_xref_node()
    options = {
        "refdoc": env.docname,
        "refdomain": domain,
        "reftype": rolename,
        "refexplicit": False,
        "refwarn": role.warn_dangling,
    }
    refnode = role.nodeclass(target, **options)
    title, target = role.process_link(env, refnode, False, target, target)
    refnode["reftarget"] = target
    classes = ["xref", domain, f"{domain}-{rolename}"]
    refnode += role.innernodeclass(target, title, classes=classes)

    # This is the very gross part of the hack. Normally,
    # result_nodes takes a document object to which we would pass
    # self.inliner.document. Prior to Sphinx 4.1, we don't *have* an
    # inliner to pass, so we have nothing to pass here. However, the
    # actual implementation of role.result_nodes in this case
    # doesn't actually use that argument, so this winds up being
    # ... fine. Rest easy at night knowing this code only runs under
    # old versions of Sphinx, so at least it won't change in the
    # future on us and lead to surprising new failures.
    # Gross, I know.
    result_nodes, _messages = role.result_nodes(
        None,  # type: ignore
        env,
        refnode,
        is_ref=True,
    )
    return nodes.inline(target, "", *result_nodes)


class CompatField(docfields.Field):
    if MAKE_XREF_WORKAROUND:
        make_xref = _compat_make_xref


class CompatGroupedField(docfields.GroupedField):
    if MAKE_XREF_WORKAROUND:
        make_xref = _compat_make_xref


class CompatTypedField(docfields.TypedField):
    if MAKE_XREF_WORKAROUND:
        make_xref = _compat_make_xref


# ################################################################
# Nested parsing error location fix for Sphinx 5.3.0 < x < 6.2.0 #
# ################################################################

# When we require Sphinx 4.x, the TYPE_CHECKING hack where we avoid
# subscripting ObjectDescription at runtime can be removed in favor of
# just always subscripting the class.

# When we require Sphinx > 6.2.0, the rest of this compatibility hack
# can be dropped and QAPIObject can just inherit directly from
# ObjectDescription[Signature].

SOURCE_LOCATION_FIX = (5, 3, 0) <= sphinx.version_info[:3] < (6, 2, 0)

Signature = str


if TYPE_CHECKING:
    _BaseClass = ObjectDescription[Signature]
else:
    _BaseClass = ObjectDescription


class ParserFix(_BaseClass):

    _temp_content: StringList
    _temp_offset: int
    _temp_node: Optional[addnodes.desc_content]

    def before_content(self) -> None:
        # Work around a sphinx bug and parse the content ourselves.
        self._temp_content = self.content
        self._temp_offset = self.content_offset
        self._temp_node = None

        if SOURCE_LOCATION_FIX:
            self._temp_node = addnodes.desc_content()
            self.state.nested_parse(
                self.content, self.content_offset, self._temp_node
            )
            # Sphinx will try to parse the content block itself,
            # Give it nothingness to parse instead.
            self.content = StringList()
            self.content_offset = 0

    def transform_content(self, content_node: addnodes.desc_content) -> None:
        # Sphinx workaround: Inject our parsed content and restore state.
        if self._temp_node:
            content_node += self._temp_node.children
            self.content = self._temp_content
            self.content_offset = self._temp_offset
