# D-Bus sphinx domain extension
#
# Copyright (C) 2021, Red Hat Inc.
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Author: Marc-André Lureau <marcandre.lureau@redhat.com>

from typing import (
    Any,
    Dict,
    Iterable,
    Iterator,
    List,
    NamedTuple,
    Optional,
    Tuple,
    cast,
)

from docutils import nodes
from docutils.nodes import Element, Node
from docutils.parsers.rst import directives
from sphinx import addnodes
from sphinx.addnodes import desc_signature, pending_xref
from sphinx.directives import ObjectDescription
from sphinx.domains import Domain, Index, IndexEntry, ObjType
from sphinx.locale import _
from sphinx.roles import XRefRole
from sphinx.util import nodes as node_utils
from sphinx.util.docfields import Field, TypedField
from sphinx.util.typing import OptionSpec


class DBusDescription(ObjectDescription[str]):
    """Base class for DBus objects"""

    option_spec: OptionSpec = ObjectDescription.option_spec.copy()
    option_spec.update(
        {
            "deprecated": directives.flag,
        }
    )

    def get_index_text(self, modname: str, name: str) -> str:
        """Return the text for the index entry of the object."""
        raise NotImplementedError("must be implemented in subclasses")

    def add_target_and_index(
        self, name: str, sig: str, signode: desc_signature
    ) -> None:
        ifacename = self.env.ref_context.get("dbus:interface")
        node_id = name
        if ifacename:
            node_id = f"{ifacename}.{node_id}"

        signode["names"].append(name)
        signode["ids"].append(node_id)

        if "noindexentry" not in self.options:
            indextext = self.get_index_text(ifacename, name)
            if indextext:
                self.indexnode["entries"].append(
                    ("single", indextext, node_id, "", None)
                )

        domain = cast(DBusDomain, self.env.get_domain("dbus"))
        domain.note_object(name, self.objtype, node_id, location=signode)


class DBusInterface(DBusDescription):
    """
    Implementation of ``dbus:interface``.
    """

    def get_index_text(self, ifacename: str, name: str) -> str:
        return ifacename

    def before_content(self) -> None:
        self.env.ref_context["dbus:interface"] = self.arguments[0]

    def after_content(self) -> None:
        self.env.ref_context.pop("dbus:interface")

    def handle_signature(self, sig: str, signode: desc_signature) -> str:
        signode += addnodes.desc_annotation("interface ", "interface ")
        signode += addnodes.desc_name(sig, sig)
        return sig

    def run(self) -> List[Node]:
        _, node = super().run()
        name = self.arguments[0]
        section = nodes.section(ids=[name + "-section"])
        section += nodes.title(name, "%s interface" % name)
        section += node
        return [self.indexnode, section]


class DBusMember(DBusDescription):

    signal = False


class DBusMethod(DBusMember):
    """
    Implementation of ``dbus:method``.
    """

    option_spec: OptionSpec = DBusMember.option_spec.copy()
    option_spec.update(
        {
            "noreply": directives.flag,
        }
    )

    doc_field_types: List[Field] = [
        TypedField(
            "arg",
            label=_("Arguments"),
            names=("arg",),
            rolename="arg",
            typerolename=None,
            typenames=("argtype", "type"),
        ),
        TypedField(
            "ret",
            label=_("Returns"),
            names=("ret",),
            rolename="ret",
            typerolename=None,
            typenames=("rettype", "type"),
        ),
    ]

    def get_index_text(self, ifacename: str, name: str) -> str:
        return _("%s() (%s method)") % (name, ifacename)

    def handle_signature(self, sig: str, signode: desc_signature) -> str:
        params = addnodes.desc_parameterlist()
        returns = addnodes.desc_parameterlist()

        contentnode = addnodes.desc_content()
        self.state.nested_parse(self.content, self.content_offset, contentnode)
        for child in contentnode:
            if isinstance(child, nodes.field_list):
                for field in child:
                    ty, sg, name = field[0].astext().split(None, 2)
                    param = addnodes.desc_parameter()
                    param += addnodes.desc_sig_keyword_type(sg, sg)
                    param += addnodes.desc_sig_space()
                    param += addnodes.desc_sig_name(name, name)
                    if ty == "arg":
                        params += param
                    elif ty == "ret":
                        returns += param

        anno = "signal " if self.signal else "method "
        signode += addnodes.desc_annotation(anno, anno)
        signode += addnodes.desc_name(sig, sig)
        signode += params
        if not self.signal and "noreply" not in self.options:
            ret = addnodes.desc_returns()
            ret += returns
            signode += ret

        return sig


class DBusSignal(DBusMethod):
    """
    Implementation of ``dbus:signal``.
    """

    doc_field_types: List[Field] = [
        TypedField(
            "arg",
            label=_("Arguments"),
            names=("arg",),
            rolename="arg",
            typerolename=None,
            typenames=("argtype", "type"),
        ),
    ]
    signal = True

    def get_index_text(self, ifacename: str, name: str) -> str:
        return _("%s() (%s signal)") % (name, ifacename)


class DBusProperty(DBusMember):
    """
    Implementation of ``dbus:property``.
    """

    option_spec: OptionSpec = DBusMember.option_spec.copy()
    option_spec.update(
        {
            "type": directives.unchanged,
            "readonly": directives.flag,
            "writeonly": directives.flag,
            "readwrite": directives.flag,
            "emits-changed": directives.unchanged,
        }
    )

    doc_field_types: List[Field] = []

    def get_index_text(self, ifacename: str, name: str) -> str:
        return _("%s (%s property)") % (name, ifacename)

    def transform_content(self, contentnode: addnodes.desc_content) -> None:
        fieldlist = nodes.field_list()
        access = None
        if "readonly" in self.options:
            access = _("read-only")
        if "writeonly" in self.options:
            access = _("write-only")
        if "readwrite" in self.options:
            access = _("read & write")
        if access:
            content = nodes.Text(access)
            fieldname = nodes.field_name("", _("Access"))
            fieldbody = nodes.field_body("", nodes.paragraph("", "", content))
            field = nodes.field("", fieldname, fieldbody)
            fieldlist += field
        emits = self.options.get("emits-changed", None)
        if emits:
            content = nodes.Text(emits)
            fieldname = nodes.field_name("", _("Emits Changed"))
            fieldbody = nodes.field_body("", nodes.paragraph("", "", content))
            field = nodes.field("", fieldname, fieldbody)
            fieldlist += field
        if len(fieldlist) > 0:
            contentnode.insert(0, fieldlist)

    def handle_signature(self, sig: str, signode: desc_signature) -> str:
        contentnode = addnodes.desc_content()
        self.state.nested_parse(self.content, self.content_offset, contentnode)
        ty = self.options.get("type")

        signode += addnodes.desc_annotation("property ", "property ")
        signode += addnodes.desc_name(sig, sig)
        signode += addnodes.desc_sig_punctuation("", ":")
        signode += addnodes.desc_sig_keyword_type(ty, ty)
        return sig

    def run(self) -> List[Node]:
        self.name = "dbus:member"
        return super().run()


class DBusXRef(XRefRole):
    def process_link(self, env, refnode, has_explicit_title, title, target):
        refnode["dbus:interface"] = env.ref_context.get("dbus:interface")
        if not has_explicit_title:
            title = title.lstrip(".")  # only has a meaning for the target
            target = target.lstrip("~")  # only has a meaning for the title
            # if the first character is a tilde, don't display the module/class
            # parts of the contents
            if title[0:1] == "~":
                title = title[1:]
                dot = title.rfind(".")
                if dot != -1:
                    title = title[dot + 1 :]
        # if the first character is a dot, search more specific namespaces first
        # else search builtins first
        if target[0:1] == ".":
            target = target[1:]
            refnode["refspecific"] = True
        return title, target


class DBusIndex(Index):
    """
    Index subclass to provide a D-Bus interfaces index.
    """

    name = "dbusindex"
    localname = _("D-Bus Interfaces Index")
    shortname = _("dbus")

    def generate(
        self, docnames: Iterable[str] = None
    ) -> Tuple[List[Tuple[str, List[IndexEntry]]], bool]:
        content: Dict[str, List[IndexEntry]] = {}
        # list of prefixes to ignore
        ignores: List[str] = self.domain.env.config["dbus_index_common_prefix"]
        ignores = sorted(ignores, key=len, reverse=True)

        ifaces = sorted(
            [
                x
                for x in self.domain.data["objects"].items()
                if x[1].objtype == "interface"
            ],
            key=lambda x: x[0].lower(),
        )
        for name, (docname, node_id, _) in ifaces:
            if docnames and docname not in docnames:
                continue

            for ignore in ignores:
                if name.startswith(ignore):
                    name = name[len(ignore) :]
                    stripped = ignore
                    break
            else:
                stripped = ""

            entries = content.setdefault(name[0].lower(), [])
            entries.append(IndexEntry(stripped + name, 0, docname, node_id, "", "", ""))

        # sort by first letter
        sorted_content = sorted(content.items())

        return sorted_content, False


class ObjectEntry(NamedTuple):
    docname: str
    node_id: str
    objtype: str


class DBusDomain(Domain):
    """
    Implementation of the D-Bus domain.
    """

    name = "dbus"
    label = "D-Bus"
    object_types: Dict[str, ObjType] = {
        "interface": ObjType(_("interface"), "iface", "obj"),
        "method": ObjType(_("method"), "meth", "obj"),
        "signal": ObjType(_("signal"), "sig", "obj"),
        "property": ObjType(_("property"), "attr", "_prop", "obj"),
    }
    directives = {
        "interface": DBusInterface,
        "method": DBusMethod,
        "signal": DBusSignal,
        "property": DBusProperty,
    }
    roles = {
        "iface": DBusXRef(),
        "meth": DBusXRef(),
        "sig": DBusXRef(),
        "prop": DBusXRef(),
    }
    initial_data: Dict[str, Dict[str, Tuple[Any]]] = {
        "objects": {},  # fullname -> ObjectEntry
    }
    indices = [
        DBusIndex,
    ]

    @property
    def objects(self) -> Dict[str, ObjectEntry]:
        return self.data.setdefault("objects", {})  # fullname -> ObjectEntry

    def note_object(
        self, name: str, objtype: str, node_id: str, location: Any = None
    ) -> None:
        self.objects[name] = ObjectEntry(self.env.docname, node_id, objtype)

    def clear_doc(self, docname: str) -> None:
        for fullname, obj in list(self.objects.items()):
            if obj.docname == docname:
                del self.objects[fullname]

    def find_obj(self, typ: str, name: str) -> Optional[Tuple[str, ObjectEntry]]:
        # skip parens
        if name[-2:] == "()":
            name = name[:-2]
        if typ in ("meth", "sig", "prop"):
            try:
                ifacename, name = name.rsplit(".", 1)
            except ValueError:
                pass
        return self.objects.get(name)

    def resolve_xref(
        self,
        env: "BuildEnvironment",
        fromdocname: str,
        builder: "Builder",
        typ: str,
        target: str,
        node: pending_xref,
        contnode: Element,
    ) -> Optional[Element]:
        """Resolve the pending_xref *node* with the given *typ* and *target*."""
        objdef = self.find_obj(typ, target)
        if objdef:
            return node_utils.make_refnode(
                builder, fromdocname, objdef.docname, objdef.node_id, contnode
            )

    def get_objects(self) -> Iterator[Tuple[str, str, str, str, str, int]]:
        for refname, obj in self.objects.items():
            yield (refname, refname, obj.objtype, obj.docname, obj.node_id, 1)

    def merge_domaindata(self, docnames, otherdata):
        for name, obj in otherdata['objects'].items():
            if obj.docname in docnames:
                self.data['objects'][name] = obj

def setup(app):
    app.add_domain(DBusDomain)
    app.add_config_value("dbus_index_common_prefix", [], "env")
