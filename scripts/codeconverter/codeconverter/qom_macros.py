# Copyright (C) 2020 Red Hat Inc.
#
# Authors:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
import re
from itertools import chain
from typing import *

from .regexps import *
from .patching import *
from .utils import *

import logging
logger = logging.getLogger(__name__)
DBG = logger.debug
INFO = logger.info
WARN = logger.warning

# simple expressions:

RE_CONSTANT = OR(RE_STRING, RE_NUMBER)

class ConstantDefine(FileMatch):
    """Simple #define preprocessor directive for a constant"""
    # if the macro contents are very simple, it might be included
    # in the match group 'value'
    regexp = S(r'^[ \t]*#[ \t]*define', CPP_SPACE, NAMED('name', RE_IDENTIFIER),
               CPP_SPACE, NAMED('value', RE_CONSTANT), r'[ \t]*\n')

    def provided_identifiers(self) -> Iterable[RequiredIdentifier]:
        yield RequiredIdentifier('constant', self.group('name'))

class TypeIdentifiers(NamedTuple):
    """Type names found in type declarations"""
    # TYPE_MYDEVICE
    typename: Optional[str]
    # MYDEVICE
    uppercase: Optional[str] = None
    # MyDevice
    instancetype: Optional[str] = None
    # MyDeviceClass
    classtype: Optional[str] = None
    # my_device
    lowercase: Optional[str] = None

    def allfields(self):
        return tuple(getattr(self, f) for f in self._fields)

    def merge(self, other: 'TypeIdentifiers') -> Optional['TypeIdentifiers']:
        """Check if identifiers match, return new identifier with complete list"""
        if any(not opt_compare(a, b) for a,b in zip(self, other)):
            return None
        return TypeIdentifiers(*(merge(a, b) for a,b in zip(self, other)))

    def __str__(self) -> str:
        values = ((f, getattr(self, f)) for f in self._fields)
        s = ', '.join('%s=%s' % (f,v) for f,v in values if v is not None)
        return f'{s}'

    def check_consistency(self) -> List[str]:
        """Check if identifiers are consistent with each other,
        return list of problems (or empty list if everything seems consistent)
        """
        r = []
        if self.typename is None:
            r.append("typename (TYPE_MYDEVICE) is unavailable")

        if self.uppercase is None:
            r.append("uppercase name is unavailable")

        if (self.instancetype is not None
            and self.classtype is not None
            and self.classtype != f'{self.instancetype}Class'):
                r.append("class typedef %s doesn't match instance typedef %s" %
                         (self.classtype, self.instancetype))

        if (self.uppercase is not None
            and self.typename is not None
            and f'TYPE_{self.uppercase}' != self.typename):
            r.append("uppercase name (%s) doesn't match type name (%s)" %
                     (self.uppercase, self.typename))

        return r

class TypedefMatch(FileMatch):
    """typedef declaration"""
    def provided_identifiers(self) -> Iterable[RequiredIdentifier]:
        yield RequiredIdentifier('type', self.group('name'))

class SimpleTypedefMatch(TypedefMatch):
    """Simple typedef declaration
    (no replacement rules)"""
    regexp = S(r'^[ \t]*typedef', SP,
               NAMED('typedef_type', RE_TYPE), SP,
               NAMED('name', RE_IDENTIFIER), r'\s*;[ \t]*\n')

RE_MACRO_DEFINE = S(r'^[ \t]*#\s*define\s+', NAMED('name', RE_IDENTIFIER),
                    r'\s*\(\s*', RE_IDENTIFIER, r'\s*\)', CPP_SPACE)

RE_STRUCT_ATTRIBUTE = r'QEMU_PACKED'

# This doesn't parse the struct definitions completely, it just assumes
# the closing brackets are going to be in an unindented line:
RE_FULL_STRUCT = S('struct', SP, M(RE_IDENTIFIER, n='?', name='structname'), SP,
                   NAMED('body', r'{\n',
                         # acceptable inside the struct body:
                         # - lines starting with space or tab
                         # - empty lines
                         # - preprocessor directives
                         # - comments
                         OR(r'[ \t][^\n]*\n',
                            r'#[^\n]*\n',
                            r'\n',
                            S(r'[ \t]*', RE_COMMENT, r'[ \t]*\n'),
                            repeat='*?'),
                         r'}', M(RE_STRUCT_ATTRIBUTE, SP, n='*')))
RE_STRUCT_TYPEDEF = S(r'^[ \t]*typedef', SP, RE_FULL_STRUCT, SP,
                      NAMED('name', RE_IDENTIFIER), r'\s*;[ \t]*\n')

class FullStructTypedefMatch(TypedefMatch):
    """typedef struct [SomeStruct] { ...} SomeType
    Will be replaced by separate struct declaration + typedef
    """
    regexp = RE_STRUCT_TYPEDEF

    def make_structname(self) -> str:
        """Make struct name for struct+typedef split"""
        name = self.group('structname')
        if not name:
            name = self.name
        return name

    def strip_typedef(self) -> Patch:
        """generate patch that will strip typedef from the struct declartion

        The caller is responsible for readding the typedef somewhere else.
        """
        name = self.make_structname()
        body = self.group('body')
        return self.make_patch(f'struct {name} {body};\n')

    def make_simple_typedef(self) -> str:
        structname = self.make_structname()
        name = self.name
        return f'typedef struct {structname} {name};\n'

    def move_typedef(self, position) -> Iterator[Patch]:
        """Generate patches to move typedef elsewhere"""
        yield self.strip_typedef()
        yield Patch(position, position, self.make_simple_typedef())

    def split_typedef(self) -> Iterator[Patch]:
        """Split into struct definition + typedef in-place"""
        yield self.strip_typedef()
        yield self.append(self.make_simple_typedef())

class StructTypedefSplit(FullStructTypedefMatch):
    """split struct+typedef declaration"""
    def gen_patches(self) -> Iterator[Patch]:
        if self.group('structname'):
            yield from self.split_typedef()

class DuplicatedTypedefs(SimpleTypedefMatch):
    """Delete ALL duplicate typedefs (unsafe)"""
    def gen_patches(self) -> Iterable[Patch]:
        other_td = [td for td in chain(self.file.matches_of_type(SimpleTypedefMatch),
                                       self.file.matches_of_type(FullStructTypedefMatch))
                    if td.name == self.name]
        DBG("other_td: %r", other_td)
        if any(td.start() < self.start() for td in other_td):
            # patch only if handling the first typedef
            return
        for td in other_td:
            if isinstance(td, SimpleTypedefMatch):
                DBG("other td: %r", td.match.groupdict())
                if td.group('typedef_type') != self.group('typedef_type'):
                    yield td.make_removal_patch()
            elif isinstance(td, FullStructTypedefMatch):
                DBG("other td: %r", td.match.groupdict())
                if self.group('typedef_type') == 'struct '+td.group('structname'):
                    yield td.strip_typedef()

class QOMDuplicatedTypedefs(DuplicatedTypedefs):
    """Delete duplicate typedefs if used by QOM type"""
    def gen_patches(self) -> Iterable[Patch]:
        qom_macros = [TypeCheckMacro, DeclareInstanceChecker, DeclareClassCheckers, DeclareObjCheckers]
        qom_matches = chain(*(self.file.matches_of_type(t) for t in qom_macros))
        in_use = any(RequiredIdentifier('type', self.name) in m.required_identifiers()
                     for m in qom_matches)
        if in_use:
            yield from DuplicatedTypedefs.gen_patches(self)

class QOMStructTypedefSplit(FullStructTypedefMatch):
    """split struct+typedef declaration if used by QOM type"""
    def gen_patches(self) -> Iterator[Patch]:
        qom_macros = [TypeCheckMacro, DeclareInstanceChecker, DeclareClassCheckers, DeclareObjCheckers]
        qom_matches = chain(*(self.file.matches_of_type(t) for t in qom_macros))
        in_use = any(RequiredIdentifier('type', self.name) in m.required_identifiers()
                     for m in qom_matches)
        if in_use:
            yield from self.split_typedef()

def typedefs(file: FileInfo) -> Iterable[TypedefMatch]:
    return (cast(TypedefMatch, m)
            for m in chain(file.matches_of_type(SimpleTypedefMatch),
                           file.matches_of_type(FullStructTypedefMatch)))

def find_typedef(f: FileInfo, name: Optional[str]) -> Optional[TypedefMatch]:
    if not name:
        return None
    for td in typedefs(f):
        if td.name == name:
            return td
    return None

CHECKER_MACROS = ['OBJECT_CHECK', 'OBJECT_CLASS_CHECK', 'OBJECT_GET_CLASS']
CheckerMacroName = Literal['OBJECT_CHECK', 'OBJECT_CLASS_CHECK', 'OBJECT_GET_CLASS']

RE_CHECK_MACRO = \
    S(RE_MACRO_DEFINE,
      OR(*CHECKER_MACROS, name='checker'),
      M(r'\s*\(\s*', OR(NAMED('typedefname', RE_IDENTIFIER), RE_TYPE, name='c_type'), r'\s*,', CPP_SPACE,
        OPTIONAL_PARS(RE_IDENTIFIER), r',', CPP_SPACE,
        NAMED('qom_typename', RE_IDENTIFIER), r'\s*\)\n',
        n='?', name='check_args'))

EXPECTED_CHECKER_SUFFIXES: List[Tuple[CheckerMacroName, str]] = [
    ('OBJECT_GET_CLASS', '_GET_CLASS'),
    ('OBJECT_CLASS_CHECK', '_CLASS'),
]

class TypeCheckMacro(FileMatch):
    """OBJECT_CHECK/OBJECT_CLASS_CHECK/OBJECT_GET_CLASS macro definitions
    Will be replaced by DECLARE_*_CHECKERS macro
    """
    #TODO: handle and convert INTERFACE_CHECK macros
    regexp = RE_CHECK_MACRO

    @property
    def checker(self) -> CheckerMacroName:
        """Name of checker macro being used"""
        return self.group('checker')

    @property
    def typedefname(self) -> Optional[str]:
        return self.group('typedefname')

    def find_typedef(self) -> Optional[TypedefMatch]:
        return find_typedef(self.file, self.typedefname)

    def sanity_check(self) -> None:
        DBG("groups: %r", self.match.groups())
        if not self.group('check_args'):
            self.warn("type check macro not parsed completely: %s", self.name)
            return
        DBG("type identifiers: %r", self.type_identifiers)
        if self.typedefname and self.find_typedef() is None:
            self.warn("typedef used by %s not found", self.name)

    def find_matching_macros(self) -> List['TypeCheckMacro']:
        """Find other check macros that generate the same macro names

        The returned list will always be sorted.
        """
        my_ids = self.type_identifiers
        assert my_ids
        return [m for m in self.file.matches_of_type(TypeCheckMacro)
                if m.type_identifiers is not None
                   and my_ids.uppercase is not None
                   and (my_ids.uppercase == m.type_identifiers.uppercase
                        or my_ids.typename == m.type_identifiers.typename)]

    def merge_ids(self, matches: List['TypeCheckMacro']) -> Optional[TypeIdentifiers]:
        """Try to merge info about type identifiers from all matches in a list"""
        if not matches:
            return None
        r = matches[0].type_identifiers
        if r is None:
            return None
        for m in matches[1:]:
            assert m.type_identifiers
            new = r.merge(m.type_identifiers)
            if new is None:
                self.warn("macro %s identifiers (%s) don't match macro %s (%s)",
                          matches[0].name, r, m.name, m.type_identifiers)
                return None
            r = new
        return r

    def required_identifiers(self) -> Iterable[RequiredIdentifier]:
        yield RequiredIdentifier('include', '"qom/object.h"')
        if self.type_identifiers is None:
            return
        # to make sure typedefs will be moved above all related macros,
        # return dependencies from all of them, not just this match
        for m in self.find_matching_macros():
            yield RequiredIdentifier('type', m.group('c_type'))
            yield RequiredIdentifier('constant', m.group('qom_typename'))

    @property
    def type_identifiers(self) -> Optional[TypeIdentifiers]:
        """Extract type identifier information from match"""
        typename = self.group('qom_typename')
        c_type = self.group('c_type')
        if not typename or not c_type:
            return None
        typedef = self.group('typedefname')
        classtype = None
        instancetype = None
        uppercase = None
        expected_suffix = dict(EXPECTED_CHECKER_SUFFIXES).get(self.checker)

        # here the available data depends on the checker macro being called:
        # - we need to remove the suffix from the macro name
        # - depending on the macro type, we know the class type name, or
        #   the instance type name
        if self.checker in ('OBJECT_GET_CLASS', 'OBJECT_CLASS_CHECK'):
            classtype = c_type
        elif self.checker == 'OBJECT_CHECK':
            instancetype = c_type
            uppercase = self.name
        else:
            assert False
        if expected_suffix and self.name.endswith(expected_suffix):
            uppercase = self.name[:-len(expected_suffix)]
        return TypeIdentifiers(typename=typename, classtype=classtype,
                               instancetype=instancetype, uppercase=uppercase)

    def gen_patches(self) -> Iterable[Patch]:
        if self.type_identifiers is None:
            self.warn("couldn't extract type information from macro %s", self.name)
            return

        if self.name == 'INTERFACE_CLASS':
            # INTERFACE_CLASS is special and won't be patched
            return

        for checker,suffix in EXPECTED_CHECKER_SUFFIXES:
            if self.name.endswith(suffix):
                if self.checker != checker:
                    self.warn("macro %s is using macro %s instead of %s", self.name, self.checker, checker)
                    return
                break

        matches = self.find_matching_macros()
        DBG("found %d matching macros: %s", len(matches), ' '.join(m.name for m in matches))
        # we will generate patches only when processing the first macro:
        if matches[0].start != self.start:
            DBG("skipping %s (will patch when handling %s)", self.name, matches[0].name)
            return


        ids = self.merge_ids(matches)
        if ids is None:
            DBG("type identifier mismatch, won't patch %s", self.name)
            return

        if not ids.uppercase:
            self.warn("macro %s doesn't follow the expected name pattern", self.name)
            return
        if not ids.typename:
            self.warn("macro %s: couldn't extract type name", self.name)
            return

        #issues = ids.check_consistency()
        #if issues:
        #    for i in issues:
        #        self.warn("inconsistent identifiers: %s", i)

        names = [n for n in (ids.instancetype, ids.classtype, ids.uppercase, ids.typename)
                 if n is not None]
        if len(set(names)) != len(names):
            self.warn("duplicate names used by macro: %r", ids)
            return

        assert ids.classtype or ids.instancetype
        assert ids.typename
        assert ids.uppercase
        if ids.classtype and ids.instancetype:
            new_decl = (f'DECLARE_OBJ_CHECKERS({ids.instancetype}, {ids.classtype},\n'
                        f'                     {ids.uppercase}, {ids.typename})\n')
        elif ids.classtype:
            new_decl = (f'DECLARE_CLASS_CHECKERS({ids.classtype}, {ids.uppercase},\n'
                        f'                       {ids.typename})\n')
        elif ids.instancetype:
            new_decl = (f'DECLARE_INSTANCE_CHECKER({ids.instancetype}, {ids.uppercase},\n'
                        f'                         {ids.typename})\n')
        else:
            assert False

        # we need to ensure the typedefs are already available
        issues = []
        for t in [ids.instancetype, ids.classtype]:
            if not t:
                continue
            if re.fullmatch(RE_STRUCT_TYPE, t):
                self.info("type %s is not a typedef", t)
                continue
            td = find_typedef(self.file, t)
            #if not td and self.allfiles.find_file('include/qemu/typedefs.h'):
            #
            if not td:
                # it is OK if the typedef is in typedefs.h
                f = self.allfiles.find_file('include/qemu/typedefs.h')
                if f and find_typedef(f, t):
                    self.info("typedef %s found in typedefs.h", t)
                    continue

                issues.append("couldn't find typedef %s" % (t))
            elif td.start() > self.start():
                issues.append("typedef %s need to be moved earlier in the file" % (td.name))

        for issue in issues:
            self.warn(issue)

        if issues and not self.file.force:
            return

        # delete all matching macros and add new declaration:
        for m in matches:
            yield m.make_patch('')
        for issue in issues:
            yield self.prepend("/* FIXME: %s */\n" % (issue))
        yield self.append(new_decl)

class DeclareInstanceChecker(FileMatch):
    """DECLARE_INSTANCE_CHECKER use
    Will be replaced with DECLARE_OBJ_CHECKERS if possible
    """
    #TODO: replace lonely DECLARE_INSTANCE_CHECKER with DECLARE_OBJ_CHECKERS
    #      if all types are found.
    #      This will require looking up the correct class type in the TypeInfo
    #      structs in another file
    regexp = S(r'^[ \t]*DECLARE_INSTANCE_CHECKER\s*\(\s*',
               NAMED('instancetype', RE_TYPE), r'\s*,\s*',
               NAMED('uppercase', RE_IDENTIFIER), r'\s*,\s*',
               OR(RE_IDENTIFIER, RE_STRING, RE_MACRO_CONCAT, RE_FUN_CALL, name='typename'), SP,
               r'\)[ \t]*;?[ \t]*\n')

    def required_identifiers(self) -> Iterable[RequiredIdentifier]:
        yield RequiredIdentifier('include', '"qom/object.h"')
        yield RequiredIdentifier('constant', self.group('typename'))
        yield RequiredIdentifier('type', self.group('instancetype'))

class DeclareClassCheckers(FileMatch):
    """DECLARE_INSTANCE_CHECKER use"""
    regexp = S(r'^[ \t]*DECLARE_CLASS_CHECKERS\s*\(\s*',
               NAMED('classtype', RE_TYPE), r'\s*,\s*',
               NAMED('uppercase', RE_IDENTIFIER), r'\s*,\s*',
               OR(RE_IDENTIFIER, RE_STRING, RE_MACRO_CONCAT, RE_FUN_CALL, name='typename'), SP,
               r'\)[ \t]*;?[ \t]*\n')

    def required_identifiers(self) -> Iterable[RequiredIdentifier]:
        yield RequiredIdentifier('include', '"qom/object.h"')
        yield RequiredIdentifier('constant', self.group('typename'))
        yield RequiredIdentifier('type', self.group('classtype'))

class DeclareObjCheckers(FileMatch):
    """DECLARE_OBJ_CHECKERS use
    Will be replaced with OBJECT_DECLARE_TYPE if possible
    """
    #TODO: detect when OBJECT_DECLARE_SIMPLE_TYPE can be used
    regexp = S(r'^[ \t]*DECLARE_OBJ_CHECKERS\s*\(\s*',
               NAMED('instancetype', RE_TYPE), r'\s*,\s*',
               NAMED('classtype', RE_TYPE), r'\s*,\s*',
               NAMED('uppercase', RE_IDENTIFIER), r'\s*,\s*',
               OR(RE_IDENTIFIER, RE_STRING, RE_MACRO_CONCAT, RE_FUN_CALL, name='typename'), SP,
               r'\)[ \t]*;?[ \t]*\n')

    def required_identifiers(self) -> Iterable[RequiredIdentifier]:
        yield RequiredIdentifier('include', '"qom/object.h"')
        yield RequiredIdentifier('constant', self.group('typename'))
        yield RequiredIdentifier('type', self.group('classtype'))
        yield RequiredIdentifier('type', self.group('instancetype'))

    def gen_patches(self):
        ids = TypeIdentifiers(uppercase=self.group('uppercase'),
                              typename=self.group('typename'),
                              classtype=self.group('classtype'),
                              instancetype=self.group('instancetype'))
        issues = ids.check_consistency()
        if issues:
            for i in issues:
                self.warn("inconsistent identifiers: %s", i)
            return

        if self.group('typename') != 'TYPE_'+self.group('uppercase'):
            self.warn("type %s mismatch with uppercase name %s", ids.typename, ids.uppercase)
            return

        typedefs = [(t,self.file.find_match(SimpleTypedefMatch, t))
                    for t in (ids.instancetype, ids.classtype)]
        for t,td in typedefs:
            if td is None:
                self.warn("typedef %s not found", t)
                break
            if td.start() > self.start():
                self.warn("typedef %s needs to be move earlier in the file", t)
                break
            #HACK: check if typedef is used between its definition and the macro
            #TODO: check if the only match is inside the "struct { ... }" declaration
            if re.search(r'\b'+t+r'\b', self.file.original_content[td.end():self.start()]):
                self.warn("typedef %s can't be moved, it is used before the macro", t)
                break
        else:
            for t,td in typedefs:
                yield td.make_removal_patch()

            lowercase = ids.uppercase.lower()
            # all is OK, we can replace the macro!
            c = (f'OBJECT_DECLARE_TYPE({ids.instancetype}, {ids.classtype},\n'
                 f'                    {lowercase}, {ids.uppercase})\n')
            yield self.make_patch(c)

class TrivialClassStruct(FileMatch):
    """Trivial class struct"""
    regexp = S(r'^[ \t]*struct\s*', NAMED('name', RE_IDENTIFIER),
               r'\s*{\s*', NAMED('parent_struct', RE_IDENTIFIER), r'\s*parent(_class)?\s*;\s*};\n')

class DeclareTypeName(FileMatch):
    """DECLARE_TYPE_NAME usage"""
    regexp = S(r'^[ \t]*DECLARE_TYPE_NAME\s*\(',
               NAMED('uppercase', RE_IDENTIFIER), r'\s*,\s*',
               OR(RE_IDENTIFIER, RE_STRING, RE_MACRO_CONCAT, RE_FUN_CALL, name='typename'),
               r'\s*\);?[ \t]*\n')

class ObjectDeclareType(FileMatch):
    """OBJECT_DECLARE_TYPE usage
    Will be replaced with OBJECT_DECLARE_SIMPLE_TYPE if possible
    """
    regexp = S(r'^[ \t]*OBJECT_DECLARE_TYPE\s*\(',
               NAMED('instancetype', RE_TYPE), r'\s*,\s*',
               NAMED('classtype', RE_TYPE), r'\s*,\s*',
               NAMED('lowercase', RE_IDENTIFIER), r'\s*,\s*',
               NAMED('uppercase', RE_IDENTIFIER), SP,
               r'\)[ \t]*;?[ \t]*\n')

    def gen_patches(self):
        DBG("groups: %r", self.match.groupdict())
        trivial_struct = self.file.find_match(TrivialClassStruct, self.group('classtype'))
        if trivial_struct:
            d = self.match.groupdict().copy()
            d['parent_struct'] = trivial_struct.group("parent_struct")
            yield trivial_struct.make_removal_patch()
            c = ("OBJECT_DECLARE_SIMPLE_TYPE(%(instancetype)s, %(lowercase)s,\n"
                 "                           %(uppercase)s, %(parent_struct)s)\n" % d)
            yield self.make_patch(c)

def find_type_declaration(files: FileList, typename: str) -> Optional[FileMatch]:
    """Find usage of DECLARE*CHECKER macro"""
    for c in (DeclareInstanceChecker, DeclareClassCheckers, DeclareObjCheckers, DeclareTypeName):
        d = files.find_match(c, name=typename, group='typename')
        if d:
            return d
    return None


class Include(FileMatch):
    """#include directive"""
    regexp = RE_INCLUDE
    def provided_identifiers(self) -> Iterable[RequiredIdentifier]:
        yield RequiredIdentifier('include', self.group('includepath'))

class InitialIncludes(FileMatch):
    """Initial #include block"""
    regexp = S(RE_FILE_BEGIN,
               M(SP, RE_COMMENTS,
                 r'^[ \t]*#[ \t]*ifndef[ \t]+', RE_IDENTIFIER, r'[ \t]*\n',
                 n='?', name='ifndef_block'),
               M(SP, RE_COMMENTS,
                 OR(RE_INCLUDE, RE_SIMPLEDEFINE),
                 n='*', name='includes'))

class SymbolUserList(NamedTuple):
    definitions: List[FileMatch]
    users: List[FileMatch]

class MoveSymbols(FileMatch):
    """Handle missing symbols
    - Move typedefs and defines when necessary
    - Add missing #include lines when necessary
    """
    regexp = RE_FILE_BEGIN

    def gen_patches(self) -> Iterator[Patch]:
        index: Dict[RequiredIdentifier, SymbolUserList] = {}
        definition_classes = [SimpleTypedefMatch, FullStructTypedefMatch, ConstantDefine, Include]
        user_classes = [TypeCheckMacro, DeclareObjCheckers, DeclareInstanceChecker, DeclareClassCheckers]

        # first we scan for all symbol definitions and usage:
        for dc in definition_classes:
            defs = self.file.matches_of_type(dc)
            for d in defs:
                DBG("scanning %r", d)
                for i in d.provided_identifiers():
                    index.setdefault(i, SymbolUserList([], [])).definitions.append(d)
        DBG("index: %r", list(index.keys()))
        for uc in user_classes:
            users = self.file.matches_of_type(uc)
            for u in users:
                for i in u.required_identifiers():
                    index.setdefault(i, SymbolUserList([], [])).users.append(u)

        # validate all symbols:
        for i,ul in index.items():
            if not ul.users:
                # unused symbol
                continue

            # symbol not defined
            if len(ul.definitions) == 0:
                if i.type == 'include':
                   includes, = self.file.matches_of_type(InitialIncludes)
                   #FIXME: don't do this if we're already inside qom/object.h
                   yield includes.append(f'#include {i.name}\n')
                else:
                    u.warn("definition of %s %s not found in file", i.type, i.name)
                continue

            # symbol defined twice:
            if len(ul.definitions) > 1:
                ul.definitions[1].warn("%s defined twice", i.name)
                ul.definitions[0].warn("previously defined here")
                continue

            # symbol defined.  check if all users are after its definition:
            assert len(ul.definitions) == 1
            definition = ul.definitions[0]
            DBG("handling repositioning of %r", definition)
            earliest = min(ul.users, key=lambda u: u.start())
            if earliest.start() > definition.start():
                DBG("%r is OK", definition)
                continue

            DBG("%r needs to be moved", definition)
            if isinstance(definition, SimpleTypedefMatch) \
               or isinstance(definition, ConstantDefine):
                # simple typedef or define can be moved directly:
                yield definition.make_removal_patch()
                yield earliest.prepend(definition.group(0))
            elif isinstance(definition, FullStructTypedefMatch) \
                 and definition.group('structname'):
                # full struct typedef is more complex: we need to remove
                # the typedef
                yield from definition.move_typedef(earliest.start())
            else:
                definition.warn("definition of %s %s needs to be moved earlier in the file", i.type, i.name)
                earliest.warn("definition of %s %s is used here", i.type, i.name)

