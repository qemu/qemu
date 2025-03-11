"""
QAPI domain extension.
"""

from __future__ import annotations

from typing import (
    TYPE_CHECKING,
    AbstractSet,
    Any,
    Dict,
    Iterable,
    List,
    NamedTuple,
    Optional,
    Tuple,
)

from docutils import nodes

from sphinx.addnodes import pending_xref
from sphinx.domains import (
    Domain,
    Index,
    IndexEntry,
    ObjType,
)
from sphinx.locale import _, __
from sphinx.util import logging
from sphinx.util.nodes import make_refnode


if TYPE_CHECKING:
    from docutils.nodes import Element

    from sphinx.application import Sphinx
    from sphinx.builders import Builder
    from sphinx.environment import BuildEnvironment

logger = logging.getLogger(__name__)


class ObjectEntry(NamedTuple):
    docname: str
    node_id: str
    objtype: str
    aliased: bool


class QAPIIndex(Index):
    """
    Index subclass to provide the QAPI definition index.
    """

    # pylint: disable=too-few-public-methods

    name = "index"
    localname = _("QAPI Index")
    shortname = _("QAPI Index")

    def generate(
        self,
        docnames: Optional[Iterable[str]] = None,
    ) -> Tuple[List[Tuple[str, List[IndexEntry]]], bool]:
        assert isinstance(self.domain, QAPIDomain)
        content: Dict[str, List[IndexEntry]] = {}
        collapse = False

        # list of all object (name, ObjectEntry) pairs, sorted by name
        # (ignoring the module)
        objects = sorted(
            self.domain.objects.items(),
            key=lambda x: x[0].split(".")[-1].lower(),
        )

        for objname, obj in objects:
            if docnames and obj.docname not in docnames:
                continue

            # Strip the module name out:
            objname = objname.split(".")[-1]

            # Add an alphabetical entry:
            entries = content.setdefault(objname[0].upper(), [])
            entries.append(
                IndexEntry(
                    objname, 0, obj.docname, obj.node_id, obj.objtype, "", ""
                )
            )

            # Add a categorical entry:
            category = obj.objtype.title() + "s"
            entries = content.setdefault(category, [])
            entries.append(
                IndexEntry(objname, 0, obj.docname, obj.node_id, "", "", "")
            )

        # alphabetically sort categories; type names first, ABC entries last.
        sorted_content = sorted(
            content.items(),
            key=lambda x: (len(x[0]) == 1, x[0]),
        )
        return sorted_content, collapse


class QAPIDomain(Domain):
    """QAPI language domain."""

    name = "qapi"
    label = "QAPI"

    # This table associates cross-reference object types (key) with an
    # ObjType instance, which defines the valid cross-reference roles
    # for each object type.

    # Actual table entries for module, command, event, etc will come in
    # forthcoming commits.
    object_types: Dict[str, ObjType] = {}

    directives = {}
    roles = {}

    # Moved into the data property at runtime;
    # this is the internal index of reference-able objects.
    initial_data: Dict[str, Dict[str, Tuple[Any]]] = {
        "objects": {},  # fullname -> ObjectEntry
    }

    # Index pages to generate; each entry is an Index class.
    indices = [
        QAPIIndex,
    ]

    @property
    def objects(self) -> Dict[str, ObjectEntry]:
        ret = self.data.setdefault("objects", {})
        return ret  # type: ignore[no-any-return]

    def note_object(
        self,
        name: str,
        objtype: str,
        node_id: str,
        aliased: bool = False,
        location: Any = None,
    ) -> None:
        """Note a QAPI object for cross reference."""
        if name in self.objects:
            other = self.objects[name]
            if other.aliased and aliased is False:
                # The original definition found. Override it!
                pass
            elif other.aliased is False and aliased:
                # The original definition is already registered.
                return
            else:
                # duplicated
                logger.warning(
                    __(
                        "duplicate object description of %s, "
                        "other instance in %s, use :no-index: for one of them"
                    ),
                    name,
                    other.docname,
                    location=location,
                )
        self.objects[name] = ObjectEntry(
            self.env.docname, node_id, objtype, aliased
        )

    def clear_doc(self, docname: str) -> None:
        for fullname, obj in list(self.objects.items()):
            if obj.docname == docname:
                del self.objects[fullname]

    def merge_domaindata(
        self, docnames: AbstractSet[str], otherdata: Dict[str, Any]
    ) -> None:
        for fullname, obj in otherdata["objects"].items():
            if obj.docname in docnames:
                # Sphinx's own python domain doesn't appear to bother to
                # check for collisions. Assert they don't happen and
                # we'll fix it if/when the case arises.
                assert fullname not in self.objects, (
                    "bug - collision on merge?"
                    f" {fullname=} {obj=} {self.objects[fullname]=}"
                )
                self.objects[fullname] = obj

    def find_obj(
        self, modname: str, name: str, typ: Optional[str]
    ) -> list[tuple[str, ObjectEntry]]:
        """
        Find a QAPI object for "name", perhaps using the given module.

        Returns a list of (name, object entry) tuples.

        :param modname: The current module context (if any!)
                        under which we are searching.
        :param name: The name of the x-ref to resolve;
                     may or may not include a leading module.
        :param type: The role name of the x-ref we're resolving, if provided.
                     (This is absent for "any" lookups.)
        """
        if not name:
            return []

        names: list[str] = []
        matches: list[tuple[str, ObjectEntry]] = []

        fullname = name
        if "." in fullname:
            # We're searching for a fully qualified reference;
            # ignore the contextual module.
            pass
        elif modname:
            # We're searching for something from somewhere;
            # try searching the current module first.
            # e.g. :qapi:cmd:`query-block` or `query-block` is being searched.
            fullname = f"{modname}.{name}"

        if typ is None:
            # type isn't specified, this is a generic xref.
            # search *all* qapi-specific object types.
            objtypes: List[str] = list(self.object_types)
        else:
            # type is specified and will be a role (e.g. obj, mod, cmd)
            # convert this to eligible object types (e.g. command, module)
            # using the QAPIDomain.object_types table.
            objtypes = self.objtypes_for_role(typ, [])

        if name in self.objects and self.objects[name].objtype in objtypes:
            names = [name]
        elif (
            fullname in self.objects
            and self.objects[fullname].objtype in objtypes
        ):
            names = [fullname]
        else:
            # exact match wasn't found; e.g. we are searching for
            # `query-block` from a different (or no) module.
            searchname = "." + name
            names = [
                oname
                for oname in self.objects
                if oname.endswith(searchname)
                and self.objects[oname].objtype in objtypes
            ]

        matches = [(oname, self.objects[oname]) for oname in names]
        if len(matches) > 1:
            matches = [m for m in matches if not m[1].aliased]
        return matches

    def resolve_any_xref(
        self,
        env: BuildEnvironment,
        fromdocname: str,
        builder: Builder,
        target: str,
        node: pending_xref,
        contnode: Element,
    ) -> List[Tuple[str, nodes.reference]]:
        results: List[Tuple[str, nodes.reference]] = []
        matches = self.find_obj(node.get("qapi:module"), target, None)
        for name, obj in matches:
            rolename = self.role_for_objtype(obj.objtype)
            assert rolename is not None
            role = f"qapi:{rolename}"
            refnode = make_refnode(
                builder, fromdocname, obj.docname, obj.node_id, contnode, name
            )
            results.append((role, refnode))
        return results


def setup(app: Sphinx) -> Dict[str, Any]:
    app.setup_extension("sphinx.directives")
    app.add_domain(QAPIDomain)

    return {
        "version": "1.0",
        "env_version": 1,
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
