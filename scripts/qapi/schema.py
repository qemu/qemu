# QAPI schema internal representation
#
# Copyright (c) 2015-2019 Red Hat Inc.
#
# Authors:
#  Markus Armbruster <armbru@redhat.com>
#  Eric Blake <eblake@redhat.com>
#  Marc-André Lureau <marcandre.lureau@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

# pylint: disable=too-many-lines

# TODO catching name collisions in generated code would be nice

from __future__ import annotations

from abc import ABC, abstractmethod
import os
import re
from typing import (
    Any,
    Callable,
    Dict,
    List,
    Optional,
    Union,
    ValuesView,
    cast,
)

from .common import (
    POINTER_SUFFIX,
    c_name,
    cgen_ifcond,
    docgen_ifcond,
    gen_endif,
    gen_if,
)
from .error import QAPIError, QAPISemError, QAPISourceError
from .expr import check_exprs
from .parser import QAPIDoc, QAPIExpression, QAPISchemaParser
from .source import QAPISourceInfo


class QAPISchemaIfCond:
    def __init__(
        self,
        ifcond: Optional[Union[str, Dict[str, object]]] = None,
    ) -> None:
        self.ifcond = ifcond

    def _cgen(self) -> str:
        return cgen_ifcond(self.ifcond)

    def gen_if(self) -> str:
        return gen_if(self._cgen())

    def gen_endif(self) -> str:
        return gen_endif(self._cgen())

    def docgen(self) -> str:
        return docgen_ifcond(self.ifcond)

    def is_present(self) -> bool:
        return bool(self.ifcond)


class QAPISchemaEntity:
    """
    A schema entity.

    This is either a directive, such as include, or a definition.
    The latter uses sub-class `QAPISchemaDefinition`.
    """
    def __init__(self, info: Optional[QAPISourceInfo]):
        self._module: Optional[QAPISchemaModule] = None
        # For explicitly defined entities, info points to the (explicit)
        # definition.  For builtins (and their arrays), info is None.
        # For implicitly defined entities, info points to a place that
        # triggered the implicit definition (there may be more than one
        # such place).
        self.info = info
        self._checked = False

    def __repr__(self) -> str:
        return "<%s at 0x%x>" % (type(self).__name__, id(self))

    def check(self, schema: QAPISchema) -> None:
        # pylint: disable=unused-argument
        self._checked = True

    def connect_doc(self, doc: Optional[QAPIDoc] = None) -> None:
        pass

    def _set_module(
        self, schema: QAPISchema, info: Optional[QAPISourceInfo]
    ) -> None:
        assert self._checked
        fname = info.fname if info else QAPISchemaModule.BUILTIN_MODULE_NAME
        self._module = schema.module_by_fname(fname)
        self._module.add_entity(self)

    def set_module(self, schema: QAPISchema) -> None:
        self._set_module(schema, self.info)

    def visit(self, visitor: QAPISchemaVisitor) -> None:
        # pylint: disable=unused-argument
        assert self._checked


class QAPISchemaDefinition(QAPISchemaEntity):
    meta: str

    def __init__(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        doc: Optional[QAPIDoc],
        ifcond: Optional[QAPISchemaIfCond] = None,
        features: Optional[List[QAPISchemaFeature]] = None,
    ):
        super().__init__(info)
        for f in features or []:
            f.set_defined_in(name)
        self.name = name
        self.doc = doc
        self._ifcond = ifcond or QAPISchemaIfCond()
        self.features = features or []

    def __repr__(self) -> str:
        return "<%s:%s at 0x%x>" % (type(self).__name__, self.name,
                                    id(self))

    def c_name(self) -> str:
        return c_name(self.name)

    def check(self, schema: QAPISchema) -> None:
        assert not self._checked
        super().check(schema)
        seen: Dict[str, QAPISchemaMember] = {}
        for f in self.features:
            f.check_clash(self.info, seen)

    def connect_doc(self, doc: Optional[QAPIDoc] = None) -> None:
        super().connect_doc(doc)
        doc = doc or self.doc
        if doc:
            for f in self.features:
                doc.connect_feature(f)

    @property
    def ifcond(self) -> QAPISchemaIfCond:
        assert self._checked
        return self._ifcond

    def is_implicit(self) -> bool:
        return not self.info

    def describe(self) -> str:
        return "%s '%s'" % (self.meta, self.name)


class QAPISchemaVisitor:
    def visit_begin(self, schema: QAPISchema) -> None:
        pass

    def visit_end(self) -> None:
        pass

    def visit_module(self, name: str) -> None:
        pass

    def visit_needed(self, entity: QAPISchemaEntity) -> bool:
        # pylint: disable=unused-argument
        # Default to visiting everything
        return True

    def visit_include(self, name: str, info: Optional[QAPISourceInfo]) -> None:
        pass

    def visit_builtin_type(
        self, name: str, info: Optional[QAPISourceInfo], json_type: str
    ) -> None:
        pass

    def visit_enum_type(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        members: List[QAPISchemaEnumMember],
        prefix: Optional[str],
    ) -> None:
        pass

    def visit_array_type(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        element_type: QAPISchemaType,
    ) -> None:
        pass

    def visit_object_type(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        base: Optional[QAPISchemaObjectType],
        members: List[QAPISchemaObjectTypeMember],
        branches: Optional[QAPISchemaBranches],
    ) -> None:
        pass

    def visit_object_type_flat(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        members: List[QAPISchemaObjectTypeMember],
        branches: Optional[QAPISchemaBranches],
    ) -> None:
        pass

    def visit_alternate_type(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        alternatives: QAPISchemaAlternatives,
    ) -> None:
        pass

    def visit_command(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        arg_type: Optional[QAPISchemaObjectType],
        ret_type: Optional[QAPISchemaType],
        gen: bool,
        success_response: bool,
        boxed: bool,
        allow_oob: bool,
        allow_preconfig: bool,
        coroutine: bool,
    ) -> None:
        pass

    def visit_event(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        arg_type: Optional[QAPISchemaObjectType],
        boxed: bool,
    ) -> None:
        pass


class QAPISchemaModule:

    BUILTIN_MODULE_NAME = './builtin'

    def __init__(self, name: str):
        self.name = name
        self._entity_list: List[QAPISchemaEntity] = []

    @staticmethod
    def is_system_module(name: str) -> bool:
        """
        System modules are internally defined modules.

        Their names start with the "./" prefix.
        """
        return name.startswith('./')

    @classmethod
    def is_user_module(cls, name: str) -> bool:
        """
        User modules are those defined by the user in qapi JSON files.

        They do not start with the "./" prefix.
        """
        return not cls.is_system_module(name)

    @classmethod
    def is_builtin_module(cls, name: str) -> bool:
        """
        The built-in module is a single System module for the built-in types.

        It is always "./builtin".
        """
        return name == cls.BUILTIN_MODULE_NAME

    def add_entity(self, ent: QAPISchemaEntity) -> None:
        self._entity_list.append(ent)

    def visit(self, visitor: QAPISchemaVisitor) -> None:
        visitor.visit_module(self.name)
        for entity in self._entity_list:
            if visitor.visit_needed(entity):
                entity.visit(visitor)


class QAPISchemaInclude(QAPISchemaEntity):
    def __init__(self, sub_module: QAPISchemaModule, info: QAPISourceInfo):
        super().__init__(info)
        self._sub_module = sub_module

    def visit(self, visitor: QAPISchemaVisitor) -> None:
        super().visit(visitor)
        visitor.visit_include(self._sub_module.name, self.info)


class QAPISchemaType(QAPISchemaDefinition, ABC):
    # Return the C type for common use.
    # For the types we commonly box, this is a pointer type.
    @abstractmethod
    def c_type(self) -> str:
        pass

    # Return the C type to be used in a parameter list.
    def c_param_type(self) -> str:
        return self.c_type()

    # Return the C type to be used where we suppress boxing.
    def c_unboxed_type(self) -> str:
        return self.c_type()

    @abstractmethod
    def json_type(self) -> str:
        pass

    def alternate_qtype(self) -> Optional[str]:
        json2qtype = {
            'null':    'QTYPE_QNULL',
            'string':  'QTYPE_QSTRING',
            'number':  'QTYPE_QNUM',
            'int':     'QTYPE_QNUM',
            'boolean': 'QTYPE_QBOOL',
            'array':   'QTYPE_QLIST',
            'object':  'QTYPE_QDICT'
        }
        return json2qtype.get(self.json_type())

    def doc_type(self) -> Optional[str]:
        if self.is_implicit():
            return None
        return self.name

    def need_has_if_optional(self) -> bool:
        # When FOO is a pointer, has_FOO == !!FOO, i.e. has_FOO is redundant.
        # Except for arrays; see QAPISchemaArrayType.need_has_if_optional().
        return not self.c_type().endswith(POINTER_SUFFIX)

    def check(self, schema: QAPISchema) -> None:
        super().check(schema)
        for feat in self.features:
            if feat.is_special():
                raise QAPISemError(
                    self.info,
                    f"feature '{feat.name}' is not supported for types")

    def describe(self) -> str:
        return "%s type '%s'" % (self.meta, self.name)


class QAPISchemaBuiltinType(QAPISchemaType):
    meta = 'built-in'

    def __init__(self, name: str, json_type: str, c_type: str):
        super().__init__(name, None, None)
        assert json_type in ('string', 'number', 'int', 'boolean', 'null',
                             'value')
        self._json_type_name = json_type
        self._c_type_name = c_type

    def c_name(self) -> str:
        return self.name

    def c_type(self) -> str:
        return self._c_type_name

    def c_param_type(self) -> str:
        if self.name == 'str':
            return 'const ' + self._c_type_name
        return self._c_type_name

    def json_type(self) -> str:
        return self._json_type_name

    def doc_type(self) -> str:
        return self.json_type()

    def visit(self, visitor: QAPISchemaVisitor) -> None:
        super().visit(visitor)
        visitor.visit_builtin_type(self.name, self.info, self.json_type())


class QAPISchemaEnumType(QAPISchemaType):
    meta = 'enum'

    def __init__(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        doc: Optional[QAPIDoc],
        ifcond: Optional[QAPISchemaIfCond],
        features: Optional[List[QAPISchemaFeature]],
        members: List[QAPISchemaEnumMember],
        prefix: Optional[str],
    ):
        super().__init__(name, info, doc, ifcond, features)
        for m in members:
            m.set_defined_in(name)
        self.members = members
        self.prefix = prefix

    def check(self, schema: QAPISchema) -> None:
        super().check(schema)
        seen: Dict[str, QAPISchemaMember] = {}
        for m in self.members:
            m.check_clash(self.info, seen)

    def connect_doc(self, doc: Optional[QAPIDoc] = None) -> None:
        super().connect_doc(doc)
        doc = doc or self.doc
        for m in self.members:
            m.connect_doc(doc)

    def is_implicit(self) -> bool:
        # See QAPISchema._def_predefineds()
        return self.name == 'QType'

    def c_type(self) -> str:
        return c_name(self.name)

    def member_names(self) -> List[str]:
        return [m.name for m in self.members]

    def json_type(self) -> str:
        return 'string'

    def visit(self, visitor: QAPISchemaVisitor) -> None:
        super().visit(visitor)
        visitor.visit_enum_type(
            self.name, self.info, self.ifcond, self.features,
            self.members, self.prefix)


class QAPISchemaArrayType(QAPISchemaType):
    meta = 'array'

    def __init__(
        self, name: str, info: Optional[QAPISourceInfo], element_type: str
    ):
        super().__init__(name, info, None)
        self._element_type_name = element_type
        self.element_type: QAPISchemaType

    def need_has_if_optional(self) -> bool:
        # When FOO is an array, we still need has_FOO to distinguish
        # absent (!has_FOO) from present and empty (has_FOO && !FOO).
        return True

    def check(self, schema: QAPISchema) -> None:
        super().check(schema)
        self.element_type = schema.resolve_type(
            self._element_type_name, self.info,
            self.info.defn_meta if self.info else None)
        assert not isinstance(self.element_type, QAPISchemaArrayType)

    def set_module(self, schema: QAPISchema) -> None:
        self._set_module(schema, self.element_type.info)

    @property
    def ifcond(self) -> QAPISchemaIfCond:
        assert self._checked
        return self.element_type.ifcond

    def is_implicit(self) -> bool:
        return True

    def c_type(self) -> str:
        return c_name(self.name) + POINTER_SUFFIX

    def json_type(self) -> str:
        return 'array'

    def doc_type(self) -> Optional[str]:
        elt_doc_type = self.element_type.doc_type()
        if not elt_doc_type:
            return None
        return 'array of ' + elt_doc_type

    def visit(self, visitor: QAPISchemaVisitor) -> None:
        super().visit(visitor)
        visitor.visit_array_type(self.name, self.info, self.ifcond,
                                 self.element_type)

    def describe(self) -> str:
        return "%s type ['%s']" % (self.meta, self._element_type_name)


class QAPISchemaObjectType(QAPISchemaType):
    def __init__(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        doc: Optional[QAPIDoc],
        ifcond: Optional[QAPISchemaIfCond],
        features: Optional[List[QAPISchemaFeature]],
        base: Optional[str],
        local_members: List[QAPISchemaObjectTypeMember],
        branches: Optional[QAPISchemaBranches],
    ):
        # struct has local_members, optional base, and no branches
        # union has base, branches, and no local_members
        super().__init__(name, info, doc, ifcond, features)
        self.meta = 'union' if branches else 'struct'
        for m in local_members:
            m.set_defined_in(name)
        if branches is not None:
            branches.set_defined_in(name)
        self._base_name = base
        self.base = None
        self.local_members = local_members
        self.branches = branches
        self.members: List[QAPISchemaObjectTypeMember]
        self._check_complete = False

    def check(self, schema: QAPISchema) -> None:
        # This calls another type T's .check() exactly when the C
        # struct emitted by gen_object() contains that T's C struct
        # (pointers don't count).
        if self._check_complete:
            # A previous .check() completed: nothing to do
            return
        if self._checked:
            # Recursed: C struct contains itself
            raise QAPISemError(self.info,
                               "object %s contains itself" % self.name)

        super().check(schema)
        assert self._checked and not self._check_complete

        seen = {}
        if self._base_name:
            self.base = schema.resolve_type(self._base_name, self.info,
                                            "'base'")
            if (not isinstance(self.base, QAPISchemaObjectType)
                    or self.base.branches):
                raise QAPISemError(
                    self.info,
                    "'base' requires a struct type, %s isn't"
                    % self.base.describe())
            self.base.check(schema)
            self.base.check_clash(self.info, seen)
        for m in self.local_members:
            m.check(schema)
            m.check_clash(self.info, seen)

        # self.check_clash() works in terms of the supertype, but
        # self.members is declared List[QAPISchemaObjectTypeMember].
        # Cast down to the subtype.
        members = cast(List[QAPISchemaObjectTypeMember], list(seen.values()))

        if self.branches:
            self.branches.check(schema, seen)
            self.branches.check_clash(self.info, seen)

        self.members = members
        self._check_complete = True  # mark completed

    # Check that the members of this type do not cause duplicate JSON members,
    # and update seen to track the members seen so far. Report any errors
    # on behalf of info, which is not necessarily self.info
    def check_clash(
        self,
        info: Optional[QAPISourceInfo],
        seen: Dict[str, QAPISchemaMember],
    ) -> None:
        assert self._checked
        for m in self.members:
            m.check_clash(info, seen)
        if self.branches:
            self.branches.check_clash(info, seen)

    def connect_doc(self, doc: Optional[QAPIDoc] = None) -> None:
        super().connect_doc(doc)
        doc = doc or self.doc
        if self.base and self.base.is_implicit():
            self.base.connect_doc(doc)
        for m in self.local_members:
            m.connect_doc(doc)

    def is_implicit(self) -> bool:
        # See QAPISchema._make_implicit_object_type(), as well as
        # _def_predefineds()
        return self.name.startswith('q_')

    def is_empty(self) -> bool:
        return not self.members and not self.branches

    def has_conditional_members(self) -> bool:
        return any(m.ifcond.is_present() for m in self.members)

    def c_name(self) -> str:
        assert self.name != 'q_empty'
        return super().c_name()

    def c_type(self) -> str:
        assert not self.is_implicit()
        return c_name(self.name) + POINTER_SUFFIX

    def c_unboxed_type(self) -> str:
        return c_name(self.name)

    def json_type(self) -> str:
        return 'object'

    def visit(self, visitor: QAPISchemaVisitor) -> None:
        super().visit(visitor)
        visitor.visit_object_type(
            self.name, self.info, self.ifcond, self.features,
            self.base, self.local_members, self.branches)
        visitor.visit_object_type_flat(
            self.name, self.info, self.ifcond, self.features,
            self.members, self.branches)


class QAPISchemaAlternateType(QAPISchemaType):
    meta = 'alternate'

    def __init__(
        self,
        name: str,
        info: QAPISourceInfo,
        doc: Optional[QAPIDoc],
        ifcond: Optional[QAPISchemaIfCond],
        features: List[QAPISchemaFeature],
        alternatives: QAPISchemaAlternatives,
    ):
        super().__init__(name, info, doc, ifcond, features)
        assert alternatives.tag_member
        alternatives.set_defined_in(name)
        alternatives.tag_member.set_defined_in(self.name)
        self.alternatives = alternatives

    def check(self, schema: QAPISchema) -> None:
        super().check(schema)
        self.alternatives.tag_member.check(schema)
        # Not calling self.alternatives.check_clash(), because there's
        # nothing to clash with
        self.alternatives.check(schema, {})
        # Alternate branch names have no relation to the tag enum values;
        # so we have to check for potential name collisions ourselves.
        seen: Dict[str, QAPISchemaMember] = {}
        types_seen: Dict[str, str] = {}
        for v in self.alternatives.variants:
            v.check_clash(self.info, seen)
            qtype = v.type.alternate_qtype()
            if not qtype:
                raise QAPISemError(
                    self.info,
                    "%s cannot use %s"
                    % (v.describe(self.info), v.type.describe()))
            conflicting = set([qtype])
            if qtype == 'QTYPE_QSTRING':
                if isinstance(v.type, QAPISchemaEnumType):
                    for m in v.type.members:
                        if m.name in ['on', 'off']:
                            conflicting.add('QTYPE_QBOOL')
                        if re.match(r'[-+0-9.]', m.name):
                            # lazy, could be tightened
                            conflicting.add('QTYPE_QNUM')
                else:
                    conflicting.add('QTYPE_QNUM')
                    conflicting.add('QTYPE_QBOOL')
            for qt in conflicting:
                if qt in types_seen:
                    raise QAPISemError(
                        self.info,
                        "%s can't be distinguished from '%s'"
                        % (v.describe(self.info), types_seen[qt]))
                types_seen[qt] = v.name

    def connect_doc(self, doc: Optional[QAPIDoc] = None) -> None:
        super().connect_doc(doc)
        doc = doc or self.doc
        for v in self.alternatives.variants:
            v.connect_doc(doc)

    def c_type(self) -> str:
        return c_name(self.name) + POINTER_SUFFIX

    def json_type(self) -> str:
        return 'value'

    def visit(self, visitor: QAPISchemaVisitor) -> None:
        super().visit(visitor)
        visitor.visit_alternate_type(
            self.name, self.info, self.ifcond, self.features,
            self.alternatives)


class QAPISchemaVariants:
    def __init__(
        self,
        info: QAPISourceInfo,
        variants: List[QAPISchemaVariant],
    ):
        self.info = info
        self.tag_member: QAPISchemaObjectTypeMember
        self.variants = variants

    def set_defined_in(self, name: str) -> None:
        for v in self.variants:
            v.set_defined_in(name)

    # pylint: disable=unused-argument
    def check(
            self, schema: QAPISchema, seen: Dict[str, QAPISchemaMember]
    ) -> None:
        for v in self.variants:
            v.check(schema)


class QAPISchemaBranches(QAPISchemaVariants):
    def __init__(self,
                 info: QAPISourceInfo,
                 variants: List[QAPISchemaVariant],
                 tag_name: str):
        super().__init__(info, variants)
        self._tag_name = tag_name

    def check(
            self, schema: QAPISchema, seen: Dict[str, QAPISchemaMember]
    ) -> None:
        # We need to narrow the member type:
        tag_member = seen.get(c_name(self._tag_name))
        assert (tag_member is None
                or isinstance(tag_member, QAPISchemaObjectTypeMember))

        base = "'base'"
        # Pointing to the base type when not implicit would be
        # nice, but we don't know it here
        if not tag_member or self._tag_name != tag_member.name:
            raise QAPISemError(
                self.info,
                "discriminator '%s' is not a member of %s"
                % (self._tag_name, base))
        self.tag_member = tag_member
        # Here we do:
        assert tag_member.defined_in
        base_type = schema.lookup_type(tag_member.defined_in)
        assert base_type
        if not base_type.is_implicit():
            base = "base type '%s'" % tag_member.defined_in
        if not isinstance(tag_member.type, QAPISchemaEnumType):
            raise QAPISemError(
                self.info,
                "discriminator member '%s' of %s must be of enum type"
                % (self._tag_name, base))
        if tag_member.optional:
            raise QAPISemError(
                self.info,
                "discriminator member '%s' of %s must not be optional"
                % (self._tag_name, base))
        if tag_member.ifcond.is_present():
            raise QAPISemError(
                self.info,
                "discriminator member '%s' of %s must not be conditional"
                % (self._tag_name, base))
        # branches that are not explicitly covered get an empty type
        assert tag_member.defined_in
        cases = {v.name for v in self.variants}
        for m in tag_member.type.members:
            if m.name not in cases:
                v = QAPISchemaVariant(m.name, self.info,
                                      'q_empty', m.ifcond)
                v.set_defined_in(tag_member.defined_in)
                self.variants.append(v)
        if not self.variants:
            raise QAPISemError(self.info, "union has no branches")
        for v in self.variants:
            v.check(schema)
            # Union names must match enum values; alternate names are
            # checked separately. Use 'seen' to tell the two apart.
            if seen:
                if v.name not in tag_member.type.member_names():
                    raise QAPISemError(
                        self.info,
                        "branch '%s' is not a value of %s"
                        % (v.name, tag_member.type.describe()))
                if not isinstance(v.type, QAPISchemaObjectType):
                    raise QAPISemError(
                        self.info,
                        "%s cannot use %s"
                        % (v.describe(self.info), v.type.describe()))
                v.type.check(schema)

    def check_clash(
        self,
        info: Optional[QAPISourceInfo],
        seen: Dict[str, QAPISchemaMember],
    ) -> None:
        for v in self.variants:
            # Reset seen map for each variant, since qapi names from one
            # branch do not affect another branch.
            #
            # v.type's typing is enforced in check() above.
            assert isinstance(v.type, QAPISchemaObjectType)
            v.type.check_clash(info, dict(seen))


class QAPISchemaAlternatives(QAPISchemaVariants):
    def __init__(self,
                 info: QAPISourceInfo,
                 variants: List[QAPISchemaVariant],
                 tag_member: QAPISchemaObjectTypeMember):
        super().__init__(info, variants)
        self.tag_member = tag_member

    def check(
            self, schema: QAPISchema, seen: Dict[str, QAPISchemaMember]
    ) -> None:
        super().check(schema, seen)
        assert isinstance(self.tag_member.type, QAPISchemaEnumType)
        assert not self.tag_member.optional
        assert not self.tag_member.ifcond.is_present()


class QAPISchemaMember:
    """ Represents object members, enum members and features """
    role = 'member'

    def __init__(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: Optional[QAPISchemaIfCond] = None,
    ):
        self.name = name
        self.info = info
        self.ifcond = ifcond or QAPISchemaIfCond()
        self.defined_in: Optional[str] = None

    def set_defined_in(self, name: str) -> None:
        assert not self.defined_in
        self.defined_in = name

    def check_clash(
        self,
        info: Optional[QAPISourceInfo],
        seen: Dict[str, QAPISchemaMember],
    ) -> None:
        cname = c_name(self.name)
        if cname in seen:
            raise QAPISemError(
                info,
                "%s collides with %s"
                % (self.describe(info), seen[cname].describe(info)))
        seen[cname] = self

    def connect_doc(self, doc: Optional[QAPIDoc]) -> None:
        if doc:
            doc.connect_member(self)

    def describe(self, info: Optional[QAPISourceInfo]) -> str:
        role = self.role
        meta = 'type'
        defined_in = self.defined_in
        assert defined_in

        if defined_in.startswith('q_obj_'):
            # See QAPISchema._make_implicit_object_type() - reverse the
            # mapping there to create a nice human-readable description
            defined_in = defined_in[6:]
            if defined_in.endswith('-arg'):
                # Implicit type created for a command's dict 'data'
                assert role == 'member'
                role = 'parameter'
                meta = 'command'
                defined_in = defined_in[:-4]
            elif defined_in.endswith('-base'):
                # Implicit type created for a union's dict 'base'
                role = 'base ' + role
                defined_in = defined_in[:-5]
            else:
                assert False

        assert info is not None
        if defined_in != info.defn_name:
            return "%s '%s' of %s '%s'" % (role, self.name, meta, defined_in)
        return "%s '%s'" % (role, self.name)


class QAPISchemaEnumMember(QAPISchemaMember):
    role = 'value'

    def __init__(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: Optional[QAPISchemaIfCond] = None,
        features: Optional[List[QAPISchemaFeature]] = None,
    ):
        super().__init__(name, info, ifcond)
        for f in features or []:
            f.set_defined_in(name)
        self.features = features or []

    def connect_doc(self, doc: Optional[QAPIDoc]) -> None:
        super().connect_doc(doc)
        if doc:
            for f in self.features:
                doc.connect_feature(f)


class QAPISchemaFeature(QAPISchemaMember):
    role = 'feature'

    # Features which are standardized across all schemas
    SPECIAL_NAMES = ['deprecated', 'unstable']

    def is_special(self) -> bool:
        return self.name in QAPISchemaFeature.SPECIAL_NAMES


class QAPISchemaObjectTypeMember(QAPISchemaMember):
    def __init__(
        self,
        name: str,
        info: QAPISourceInfo,
        typ: str,
        optional: bool,
        ifcond: Optional[QAPISchemaIfCond] = None,
        features: Optional[List[QAPISchemaFeature]] = None,
    ):
        super().__init__(name, info, ifcond)
        for f in features or []:
            f.set_defined_in(name)
        self._type_name = typ
        self.type: QAPISchemaType  # set during check()
        self.optional = optional
        self.features = features or []

    def need_has(self) -> bool:
        return self.optional and self.type.need_has_if_optional()

    def check(self, schema: QAPISchema) -> None:
        assert self.defined_in
        self.type = schema.resolve_type(self._type_name, self.info,
                                        self.describe)
        seen: Dict[str, QAPISchemaMember] = {}
        for f in self.features:
            f.check_clash(self.info, seen)

    def connect_doc(self, doc: Optional[QAPIDoc]) -> None:
        super().connect_doc(doc)
        if doc:
            for f in self.features:
                doc.connect_feature(f)


class QAPISchemaVariant(QAPISchemaObjectTypeMember):
    role = 'branch'

    def __init__(
        self,
        name: str,
        info: QAPISourceInfo,
        typ: str,
        ifcond: QAPISchemaIfCond,
    ):
        super().__init__(name, info, typ, False, ifcond)


class QAPISchemaCommand(QAPISchemaDefinition):
    meta = 'command'

    def __init__(
        self,
        name: str,
        info: QAPISourceInfo,
        doc: Optional[QAPIDoc],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        arg_type: Optional[str],
        ret_type: Optional[str],
        gen: bool,
        success_response: bool,
        boxed: bool,
        allow_oob: bool,
        allow_preconfig: bool,
        coroutine: bool,
    ):
        super().__init__(name, info, doc, ifcond, features)
        self._arg_type_name = arg_type
        self.arg_type: Optional[QAPISchemaObjectType] = None
        self._ret_type_name = ret_type
        self.ret_type: Optional[QAPISchemaType] = None
        self.gen = gen
        self.success_response = success_response
        self.boxed = boxed
        self.allow_oob = allow_oob
        self.allow_preconfig = allow_preconfig
        self.coroutine = coroutine

    def check(self, schema: QAPISchema) -> None:
        assert self.info is not None
        super().check(schema)
        if self._arg_type_name:
            arg_type = schema.resolve_type(
                self._arg_type_name, self.info, "command's 'data'")
            if not isinstance(arg_type, QAPISchemaObjectType):
                raise QAPISemError(
                    self.info,
                    "command's 'data' cannot take %s"
                    % arg_type.describe())
            self.arg_type = arg_type
            if self.arg_type.branches and not self.boxed:
                raise QAPISemError(
                    self.info,
                    "command's 'data' can take %s only with 'boxed': true"
                    % self.arg_type.describe())
            self.arg_type.check(schema)
            if self.arg_type.has_conditional_members() and not self.boxed:
                raise QAPISemError(
                    self.info,
                    "conditional command arguments require 'boxed': true")
        if self._ret_type_name:
            self.ret_type = schema.resolve_type(
                self._ret_type_name, self.info, "command's 'returns'")
            if self.name not in self.info.pragma.command_returns_exceptions:
                typ = self.ret_type
                if isinstance(typ, QAPISchemaArrayType):
                    typ = typ.element_type
                if not isinstance(typ, QAPISchemaObjectType):
                    raise QAPISemError(
                        self.info,
                        "command's 'returns' cannot take %s"
                        % self.ret_type.describe())

    def connect_doc(self, doc: Optional[QAPIDoc] = None) -> None:
        super().connect_doc(doc)
        doc = doc or self.doc
        if doc:
            if self.arg_type and self.arg_type.is_implicit():
                self.arg_type.connect_doc(doc)

            if self.ret_type and self.info:
                doc.ensure_returns(self.info)

    def visit(self, visitor: QAPISchemaVisitor) -> None:
        super().visit(visitor)
        visitor.visit_command(
            self.name, self.info, self.ifcond, self.features,
            self.arg_type, self.ret_type, self.gen, self.success_response,
            self.boxed, self.allow_oob, self.allow_preconfig,
            self.coroutine)


class QAPISchemaEvent(QAPISchemaDefinition):
    meta = 'event'

    def __init__(
        self,
        name: str,
        info: QAPISourceInfo,
        doc: Optional[QAPIDoc],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        arg_type: Optional[str],
        boxed: bool,
    ):
        super().__init__(name, info, doc, ifcond, features)
        self._arg_type_name = arg_type
        self.arg_type: Optional[QAPISchemaObjectType] = None
        self.boxed = boxed

    def check(self, schema: QAPISchema) -> None:
        super().check(schema)
        if self._arg_type_name:
            typ = schema.resolve_type(
                self._arg_type_name, self.info, "event's 'data'")
            if not isinstance(typ, QAPISchemaObjectType):
                raise QAPISemError(
                    self.info,
                    "event's 'data' cannot take %s"
                    % typ.describe())
            self.arg_type = typ
            if self.arg_type.branches and not self.boxed:
                raise QAPISemError(
                    self.info,
                    "event's 'data' can take %s only with 'boxed': true"
                    % self.arg_type.describe())
            self.arg_type.check(schema)
            if self.arg_type.has_conditional_members() and not self.boxed:
                raise QAPISemError(
                    self.info,
                    "conditional event arguments require 'boxed': true")

    def connect_doc(self, doc: Optional[QAPIDoc] = None) -> None:
        super().connect_doc(doc)
        doc = doc or self.doc
        if doc:
            if self.arg_type and self.arg_type.is_implicit():
                self.arg_type.connect_doc(doc)

    def visit(self, visitor: QAPISchemaVisitor) -> None:
        super().visit(visitor)
        visitor.visit_event(
            self.name, self.info, self.ifcond, self.features,
            self.arg_type, self.boxed)


class QAPISchema:
    def __init__(self, fname: str):
        self.fname = fname

        try:
            parser = QAPISchemaParser(fname)
        except OSError as err:
            raise QAPIError(
                f"can't read schema file '{fname}': {err.strerror}"
            ) from err

        exprs = check_exprs(parser.exprs)
        self.docs = parser.docs
        self._entity_list: List[QAPISchemaEntity] = []
        self._entity_dict: Dict[str, QAPISchemaDefinition] = {}
        self._module_dict: Dict[str, QAPISchemaModule] = {}
        # NB, values in the dict will identify the first encountered
        # usage of a named feature only
        self._feature_dict: Dict[str, QAPISchemaFeature] = {}

        # All schemas get the names defined in the QapiSpecialFeature enum.
        # Rely on dict iteration order matching insertion order so that
        # the special names are emitted first when generating code.
        for f in QAPISchemaFeature.SPECIAL_NAMES:
            self._feature_dict[f] = QAPISchemaFeature(f, None)

        self._schema_dir = os.path.dirname(fname)
        self._make_module(QAPISchemaModule.BUILTIN_MODULE_NAME)
        self._make_module(fname)
        self._predefining = True
        self._def_predefineds()
        self._predefining = False
        self._def_exprs(exprs)
        self.check()

    def features(self) -> ValuesView[QAPISchemaFeature]:
        return self._feature_dict.values()

    def _def_entity(self, ent: QAPISchemaEntity) -> None:
        self._entity_list.append(ent)

    def _def_definition(self, defn: QAPISchemaDefinition) -> None:
        # Only the predefined types are allowed to not have info
        assert defn.info or self._predefining
        self._def_entity(defn)
        # TODO reject names that differ only in '_' vs. '.'  vs. '-',
        # because they're liable to clash in generated C.
        other_defn = self._entity_dict.get(defn.name)
        if other_defn:
            if other_defn.info:
                where = QAPISourceError(other_defn.info, "previous definition")
                raise QAPISemError(
                    defn.info,
                    "'%s' is already defined\n%s" % (defn.name, where))
            raise QAPISemError(
                defn.info, "%s is already defined" % other_defn.describe())
        self._entity_dict[defn.name] = defn

    def lookup_entity(self, name: str) -> Optional[QAPISchemaEntity]:
        return self._entity_dict.get(name)

    def lookup_type(self, name: str) -> Optional[QAPISchemaType]:
        typ = self.lookup_entity(name)
        if isinstance(typ, QAPISchemaType):
            return typ
        return None

    def resolve_type(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        what: Union[None, str, Callable[[QAPISourceInfo], str]],
    ) -> QAPISchemaType:
        typ = self.lookup_type(name)
        if not typ:
            assert info and what  # built-in types must not fail lookup
            if callable(what):
                what = what(info)
            raise QAPISemError(
                info, "%s uses unknown type '%s'" % (what, name))
        return typ

    def _module_name(self, fname: str) -> str:
        if QAPISchemaModule.is_system_module(fname):
            return fname
        return os.path.relpath(fname, self._schema_dir)

    def _make_module(self, fname: str) -> QAPISchemaModule:
        name = self._module_name(fname)
        if name not in self._module_dict:
            self._module_dict[name] = QAPISchemaModule(name)
        return self._module_dict[name]

    def module_by_fname(self, fname: str) -> QAPISchemaModule:
        name = self._module_name(fname)
        return self._module_dict[name]

    def _def_include(self, expr: QAPIExpression) -> None:
        include = expr['include']
        assert expr.doc is None
        self._def_entity(
            QAPISchemaInclude(self._make_module(include), expr.info))

    def _def_builtin_type(
        self, name: str, json_type: str, c_type: str
    ) -> None:
        self._def_definition(QAPISchemaBuiltinType(name, json_type, c_type))
        # Instantiating only the arrays that are actually used would
        # be nice, but we can't as long as their generated code
        # (qapi-builtin-types.[ch]) may be shared by some other
        # schema.
        self._make_array_type(name, None)

    def _def_predefineds(self) -> None:
        for t in [('str',    'string',  'char' + POINTER_SUFFIX),
                  ('number', 'number',  'double'),
                  ('int',    'int',     'int64_t'),
                  ('int8',   'int',     'int8_t'),
                  ('int16',  'int',     'int16_t'),
                  ('int32',  'int',     'int32_t'),
                  ('int64',  'int',     'int64_t'),
                  ('uint8',  'int',     'uint8_t'),
                  ('uint16', 'int',     'uint16_t'),
                  ('uint32', 'int',     'uint32_t'),
                  ('uint64', 'int',     'uint64_t'),
                  ('size',   'int',     'uint64_t'),
                  ('bool',   'boolean', 'bool'),
                  ('any',    'value',   'QObject' + POINTER_SUFFIX),
                  ('null',   'null',    'QNull' + POINTER_SUFFIX)]:
            self._def_builtin_type(*t)
        self.the_empty_object_type = QAPISchemaObjectType(
            'q_empty', None, None, None, None, None, [], None)
        self._def_definition(self.the_empty_object_type)

        qtypes = ['none', 'qnull', 'qnum', 'qstring', 'qdict', 'qlist',
                  'qbool']
        qtype_values = self._make_enum_members(
            [{'name': n} for n in qtypes], None)

        self._def_definition(QAPISchemaEnumType(
            'QType', None, None, None, None, qtype_values, None))

    def _make_features(
        self,
        features: Optional[List[Dict[str, Any]]],
        info: Optional[QAPISourceInfo],
    ) -> List[QAPISchemaFeature]:
        if features is None:
            return []

        for f in features:
            feat = QAPISchemaFeature(f['name'], info)
            if feat.name not in self._feature_dict:
                self._feature_dict[feat.name] = feat

        return [QAPISchemaFeature(f['name'], info,
                                  QAPISchemaIfCond(f.get('if')))
                for f in features]

    def _make_enum_member(
        self,
        name: str,
        ifcond: Optional[Union[str, Dict[str, Any]]],
        features: Optional[List[Dict[str, Any]]],
        info: Optional[QAPISourceInfo],
    ) -> QAPISchemaEnumMember:
        return QAPISchemaEnumMember(name, info,
                                    QAPISchemaIfCond(ifcond),
                                    self._make_features(features, info))

    def _make_enum_members(
        self, values: List[Dict[str, Any]], info: Optional[QAPISourceInfo]
    ) -> List[QAPISchemaEnumMember]:
        return [self._make_enum_member(v['name'], v.get('if'),
                                       v.get('features'), info)
                for v in values]

    def _make_array_type(
        self, element_type: str, info: Optional[QAPISourceInfo]
    ) -> str:
        name = element_type + 'List'    # reserved by check_defn_name_str()
        if not self.lookup_type(name):
            self._def_definition(QAPISchemaArrayType(
                name, info, element_type))
        return name

    def _make_implicit_object_type(
        self,
        name: str,
        info: QAPISourceInfo,
        ifcond: QAPISchemaIfCond,
        role: str,
        members: List[QAPISchemaObjectTypeMember],
    ) -> Optional[str]:
        if not members:
            return None
        # See also QAPISchemaObjectTypeMember.describe()
        name = 'q_obj_%s-%s' % (name, role)
        typ = self.lookup_entity(name)
        if typ:
            assert isinstance(typ, QAPISchemaObjectType)
            # The implicit object type has multiple users.  This can
            # only be a duplicate definition, which will be flagged
            # later.
        else:
            self._def_definition(QAPISchemaObjectType(
                name, info, None, ifcond, None, None, members, None))
        return name

    def _def_enum_type(self, expr: QAPIExpression) -> None:
        name = expr['enum']
        data = expr['data']
        prefix = expr.get('prefix')
        ifcond = QAPISchemaIfCond(expr.get('if'))
        info = expr.info
        features = self._make_features(expr.get('features'), info)
        self._def_definition(QAPISchemaEnumType(
            name, info, expr.doc, ifcond, features,
            self._make_enum_members(data, info), prefix))

    def _make_member(
        self,
        name: str,
        typ: Union[List[str], str],
        ifcond: QAPISchemaIfCond,
        features: Optional[List[Dict[str, Any]]],
        info: QAPISourceInfo,
    ) -> QAPISchemaObjectTypeMember:
        optional = False
        if name.startswith('*'):
            name = name[1:]
            optional = True
        if isinstance(typ, list):
            assert len(typ) == 1
            typ = self._make_array_type(typ[0], info)
        return QAPISchemaObjectTypeMember(name, info, typ, optional, ifcond,
                                          self._make_features(features, info))

    def _make_members(
        self,
        data: Dict[str, Any],
        info: QAPISourceInfo,
    ) -> List[QAPISchemaObjectTypeMember]:
        return [self._make_member(key, value['type'],
                                  QAPISchemaIfCond(value.get('if')),
                                  value.get('features'), info)
                for (key, value) in data.items()]

    def _def_struct_type(self, expr: QAPIExpression) -> None:
        name = expr['struct']
        base = expr.get('base')
        data = expr['data']
        info = expr.info
        ifcond = QAPISchemaIfCond(expr.get('if'))
        features = self._make_features(expr.get('features'), info)
        self._def_definition(QAPISchemaObjectType(
            name, info, expr.doc, ifcond, features, base,
            self._make_members(data, info),
            None))

    def _make_variant(
        self,
        case: str,
        typ: str,
        ifcond: QAPISchemaIfCond,
        info: QAPISourceInfo,
    ) -> QAPISchemaVariant:
        if isinstance(typ, list):
            assert len(typ) == 1
            typ = self._make_array_type(typ[0], info)
        return QAPISchemaVariant(case, info, typ, ifcond)

    def _def_union_type(self, expr: QAPIExpression) -> None:
        name = expr['union']
        base = expr['base']
        tag_name = expr['discriminator']
        data = expr['data']
        assert isinstance(data, dict)
        info = expr.info
        ifcond = QAPISchemaIfCond(expr.get('if'))
        features = self._make_features(expr.get('features'), info)
        if isinstance(base, dict):
            base = self._make_implicit_object_type(
                name, info, ifcond,
                'base', self._make_members(base, info))
        variants = [
            self._make_variant(key, value['type'],
                               QAPISchemaIfCond(value.get('if')),
                               info)
            for (key, value) in data.items()]
        members: List[QAPISchemaObjectTypeMember] = []
        self._def_definition(
            QAPISchemaObjectType(name, info, expr.doc, ifcond, features,
                                 base, members,
                                 QAPISchemaBranches(
                                     info, variants, tag_name)))

    def _def_alternate_type(self, expr: QAPIExpression) -> None:
        name = expr['alternate']
        data = expr['data']
        assert isinstance(data, dict)
        ifcond = QAPISchemaIfCond(expr.get('if'))
        info = expr.info
        features = self._make_features(expr.get('features'), info)
        variants = [
            self._make_variant(key, value['type'],
                               QAPISchemaIfCond(value.get('if')),
                               info)
            for (key, value) in data.items()]
        tag_member = QAPISchemaObjectTypeMember('type', info, 'QType', False)
        self._def_definition(
            QAPISchemaAlternateType(
                name, info, expr.doc, ifcond, features,
                QAPISchemaAlternatives(info, variants, tag_member)))

    def _def_command(self, expr: QAPIExpression) -> None:
        name = expr['command']
        data = expr.get('data')
        rets = expr.get('returns')
        gen = expr.get('gen', True)
        success_response = expr.get('success-response', True)
        boxed = expr.get('boxed', False)
        allow_oob = expr.get('allow-oob', False)
        allow_preconfig = expr.get('allow-preconfig', False)
        coroutine = expr.get('coroutine', False)
        ifcond = QAPISchemaIfCond(expr.get('if'))
        info = expr.info
        features = self._make_features(expr.get('features'), info)
        if isinstance(data, dict):
            data = self._make_implicit_object_type(
                name, info, ifcond,
                'arg', self._make_members(data, info))
        if isinstance(rets, list):
            assert len(rets) == 1
            rets = self._make_array_type(rets[0], info)
        self._def_definition(
            QAPISchemaCommand(name, info, expr.doc, ifcond, features, data,
                              rets, gen, success_response, boxed, allow_oob,
                              allow_preconfig, coroutine))

    def _def_event(self, expr: QAPIExpression) -> None:
        name = expr['event']
        data = expr.get('data')
        boxed = expr.get('boxed', False)
        ifcond = QAPISchemaIfCond(expr.get('if'))
        info = expr.info
        features = self._make_features(expr.get('features'), info)
        if isinstance(data, dict):
            data = self._make_implicit_object_type(
                name, info, ifcond,
                'arg', self._make_members(data, info))
        self._def_definition(QAPISchemaEvent(name, info, expr.doc, ifcond,
                                             features, data, boxed))

    def _def_exprs(self, exprs: List[QAPIExpression]) -> None:
        for expr in exprs:
            if 'enum' in expr:
                self._def_enum_type(expr)
            elif 'struct' in expr:
                self._def_struct_type(expr)
            elif 'union' in expr:
                self._def_union_type(expr)
            elif 'alternate' in expr:
                self._def_alternate_type(expr)
            elif 'command' in expr:
                self._def_command(expr)
            elif 'event' in expr:
                self._def_event(expr)
            elif 'include' in expr:
                self._def_include(expr)
            else:
                assert False

    def check(self) -> None:
        for ent in self._entity_list:
            ent.check(self)
            ent.connect_doc()
        for ent in self._entity_list:
            ent.set_module(self)
        for doc in self.docs:
            doc.check()

        features = list(self._feature_dict.values())
        if len(features) > 64:
            raise QAPISemError(
                features[64].info,
                "Maximum of 64 schema features is permitted")

    def visit(self, visitor: QAPISchemaVisitor) -> None:
        visitor.visit_begin(self)
        for mod in self._module_dict.values():
            mod.visit(visitor)
        visitor.visit_end()
