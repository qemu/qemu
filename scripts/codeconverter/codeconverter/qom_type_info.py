# Copyright (C) 2020 Red Hat Inc.
#
# Authors:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
import re
from .regexps import *
from .patching import *
from .utils import *
from .qom_macros import *

TI_FIELDS = [ 'name', 'parent', 'abstract', 'interfaces',
    'instance_size', 'instance_init', 'instance_post_init', 'instance_finalize',
    'class_size', 'class_init', 'class_base_init', 'class_data']

RE_TI_FIELD_NAME = OR(*TI_FIELDS)

RE_TI_FIELD_INIT = S(r'[ \t]*', NAMED('comments', RE_COMMENTS),
                     r'\.', NAMED('field', RE_TI_FIELD_NAME), r'\s*=\s*',
                     NAMED('value', RE_EXPRESSION), r'[ \t]*,?[ \t]*\n')
RE_TI_FIELDS = M(RE_TI_FIELD_INIT)

RE_TYPEINFO_START = S(r'^[ \t]*', M(r'(static|const)\s+', name='modifiers'), r'TypeInfo\s+',
                      NAMED('name', RE_IDENTIFIER), r'\s*=\s*{[ \t]*\n')

ParsedArray = List[str]
ParsedInitializerValue = Union[str, ParsedArray]
class InitializerValue(NamedTuple):
    raw: str
    parsed: Optional[ParsedInitializerValue]
    match: Optional[Match]

class ArrayItem(FileMatch):
    regexp = RE_ARRAY_ITEM

class ArrayInitializer(FileMatch):
    regexp = RE_ARRAY

    def parsed(self) -> ParsedArray:
        #DBG('parse_array: %r', m.group(0))
        return [m.group('arrayitem') for m in self.group_finditer(ArrayItem, 'arrayitems')]

class FieldInitializer(FileMatch):
    regexp = RE_TI_FIELD_INIT

    @property
    def raw(self) -> str:
        return self.group('value')

    @property
    def parsed(self) -> ParsedInitializerValue:
        parsed: ParsedInitializerValue = self.raw
        #DBG("parse_initializer_value: %r", s)
        array = self.try_group_match(ArrayInitializer, 'value')
        if array:
            assert isinstance(array, ArrayInitializer)
            return array.parsed()
        return parsed

TypeInfoInitializers = Dict[str, FieldInitializer]

class TypeDefinition(FileMatch):
    """
    Common base class for type definitions (TypeInfo variables or OBJECT_DEFINE* macros)
    """
    @property
    def instancetype(self) -> Optional[str]:
        return self.group('instancetype')

    @property
    def classtype(self) -> Optional[str]:
        return self.group('classtype')

    @property
    def uppercase(self) -> Optional[str]:
        return self.group('uppercase')

    @property
    def parent_uppercase(self) -> str:
        return self.group('parent_uppercase')

    @property
    def initializers(self) -> Optional[TypeInfoInitializers]:
        if getattr(self, '_inititalizers', None):
            self._initializers: TypeInfoInitializers
            return self._initializers
        fields = self.group('fields')
        if fields is None:
            return None
        d = dict((fm.group('field'), fm)
                  for fm in self.group_finditer(FieldInitializer, 'fields'))
        self._initializers = d # type: ignore
        return self._initializers


class TypeInfoVar(TypeDefinition):
    """TypeInfo variable declaration with initializer"""
    regexp = S(NAMED('begin', RE_TYPEINFO_START),
               M(NAMED('fields', RE_TI_FIELDS),
                 NAMED('endcomments', SP, RE_COMMENTS),
                 NAMED('end', r'};?\n'),
                 n='?', name='fullspec'))

    def is_static(self) -> bool:
        return 'static' in self.group('modifiers')

    def is_const(self) -> bool:
        return 'const' in self.group('modifiers')

    def is_full(self) -> bool:
        return bool(self.group('fullspec'))

    def get_initializers(self) -> TypeInfoInitializers:
        """Helper for code that needs to deal with missing initializer info"""
        if self.initializers is None:
            return {}
        return self.initializers

    def get_raw_initializer_value(self, field: str, default: str = '') -> str:
        initializers = self.get_initializers()
        if field in initializers:
            return initializers[field].raw
        else:
            return default

    @property
    def typename(self) -> Optional[str]:
        return self.get_raw_initializer_value('name')

    @property
    def uppercase(self) -> Optional[str]:
        typename = self.typename
        if not typename:
            return None
        if not typename.startswith('TYPE_'):
            return None
        return typename[len('TYPE_'):]

    @property
    def classtype(self) -> Optional[str]:
        class_size = self.get_raw_initializer_value('class_size')
        if not class_size:
            return None
        m = re.fullmatch(RE_SIZEOF, class_size)
        if not m:
            return None
        return m.group('sizeoftype')

    @property
    def instancetype(self) -> Optional[str]:
        instance_size = self.get_raw_initializer_value('instance_size')
        if not instance_size:
            return None
        m = re.fullmatch(RE_SIZEOF, instance_size)
        if not m:
            return None
        return m.group('sizeoftype')


    #def extract_identifiers(self) -> Optional[TypeIdentifiers]:
    #    """Try to extract identifiers from names being used"""
    #    DBG("extracting idenfiers from %s", self.name)
        #uppercase = None
        #if typename and re.fullmatch(RE_IDENTIFIER, typename) and typename.startswith("TYPE_"):
        #    uppercase = typename[len('TYPE_'):]
        #lowercase = None
        #funcs = set()
        #prefixes = set()
        #for field,suffix in [('instance_init', '_init'),
        #                     ('instance_finalize', '_finalize'),
        #                     ('class_init', '_class_init')]:
        #    if field not in values:
        #        continue
        #    func = values[field].raw
        #    funcs.add(func)
        #    if func.endswith(suffix):
        #        prefixes.add(func[:-len(suffix)])
        #    else:
        #        self.warn("function name %s doesn't have expected %s suffix",
        #                  func, suffix)
        #if len(prefixes) == 1:
        #    lowercase = prefixes.pop()
        #elif len(prefixes) > 1:
        #    self.warn("inconsistent function names: %s", ' '.join(funcs))

        #.parent = TYPE_##PARENT_MODULE_OBJ_NAME, \
        #return TypeIdentifiers(typename=typename,
        #                       uppercase=uppercase, lowercase=lowercase,
        #                       instancetype=instancetype, classtype=classtype)

    def append_field(self, field: str, value: str) -> Patch:
        """Generate patch appending a field initializer"""
        content = f'    .{field} = {value},\n'
        fm = self.group_match('fields')
        assert fm
        return fm.append(content)

    def patch_field(self, field: str, replacement: str) -> Patch:
        """Generate patch replacing a field initializer"""
        initializers = self.initializers
        assert initializers
        value = initializers.get(field)
        assert value
        return value.make_patch(replacement)

    def remove_field(self, field: str) -> Iterable[Patch]:
        initializers = self.initializers
        assert initializers
        if field in initializers:
            yield self.patch_field(field, '')

    def remove_fields(self, *fields: str) -> Iterable[Patch]:
        for f in fields:
            yield from self.remove_field(f)

    def patch_field_value(self, field: str, replacement: str) -> Patch:
        """Replace just the value of a field initializer"""
        initializers = self.initializers
        assert initializers
        value = initializers.get(field)
        assert value
        vm = value.group_match('value')
        assert vm
        return vm.make_patch(replacement)


class RemoveRedundantClassSize(TypeInfoVar):
    """Remove class_size when using OBJECT_DECLARE_SIMPLE_TYPE"""
    def gen_patches(self) -> Iterable[Patch]:
        initializers = self.initializers
        if initializers is None:
            return
        if 'class_size' not in initializers:
            return

        self.debug("Handling %s", self.name)
        m = re.fullmatch(RE_SIZEOF, initializers['class_size'].raw)
        if not m:
            self.warn("%s class_size is not sizeof?", self.name)
            return
        classtype = m.group('sizeoftype')
        if not classtype.endswith('Class'):
            self.warn("%s class size type (%s) is not *Class?", self.name, classtype)
            return
        self.debug("classtype is %s", classtype)
        instancetype = classtype[:-len('Class')]
        self.debug("intanceypte is %s", instancetype)
        self.debug("searching for simpletype declaration using %s as InstanceType", instancetype)
        decl = self.allfiles.find_match(OldStyleObjectDeclareSimpleType,
                                        instancetype, 'instancetype')
        if not decl:
            self.debug("No simpletype declaration found for %s", instancetype)
            return
        self.debug("Found simple type declaration")
        decl.debug("declaration is here")
        yield from self.remove_field('class_size')

class RemoveDeclareSimpleTypeArg(OldStyleObjectDeclareSimpleType):
    """Remove class_size when using OBJECT_DECLARE_SIMPLE_TYPE"""
    def gen_patches(self) -> Iterable[Patch]:
        c = (f'OBJECT_DECLARE_SIMPLE_TYPE({self.group("instancetype")}, {self.group("lowercase")},\n'
             f'                           {self.group("uppercase")})\n')
        yield self.make_patch(c)

class UseDeclareTypeExtended(TypeInfoVar):
    """Replace TypeInfo variable with OBJECT_DEFINE_TYPE_EXTENDED"""
    def gen_patches(self) -> Iterable[Patch]:
        # this will just ensure the caches for find_match() and matches_for_type()
        # will be loaded in advance:
        find_type_checkers(self.allfiles, 'xxxxxxxxxxxxxxxxx')

        if not self.is_static():
            self.info("Skipping non-static TypeInfo variable")
            return

        type_info_macro = self.file.find_match(TypeInfoMacro, self.name)
        if not type_info_macro:
            self.warn("TYPE_INFO(%s) line not found", self.name)
            return

        values = self.initializers
        if values is None:
            return
        if 'name' not in values:
            self.warn("name not set in TypeInfo variable %s", self.name)
            return

        typename = values['name'].raw

        if 'parent' not in values:
            self.warn("parent not set in TypeInfo variable %s", self.name)
            return
        parent_typename = values['parent'].raw

        instancetype = None
        if 'instance_size' in values:
            m = re.fullmatch(RE_SIZEOF, values['instance_size'].raw)
            if m:
                instancetype = m.group('sizeoftype')
            else:
                self.warn("can't extract instance type in TypeInfo variable %s", self.name)
                self.warn("instance_size is set to: %r", values['instance_size'].raw)
                return

        classtype = None
        if 'class_size' in values:
            m = re.fullmatch(RE_SIZEOF, values['class_size'].raw)
            if m:
                classtype = m.group('sizeoftype')
            else:
                self.warn("can't extract class type in TypeInfo variable %s", self.name)
                self.warn("class_size is set to: %r", values['class_size'].raw)
                return

        #for t in (typename, parent_typename):
        #    if not re.fullmatch(RE_IDENTIFIER, t):
        #        self.info("type name is not a macro/constant")
        #        if instancetype or classtype:
        #            self.warn("macro/constant type name is required for instance/class type")
        #        if not self.file.force:
        #            return

        # Now, the challenge is to find out the right MODULE_OBJ_NAME for the
        # type and for the parent type
        self.info("TypeInfo variable for %s is here", typename)
        uppercase = find_typename_uppercase(self.allfiles, typename)
        if not uppercase:
            self.info("Can't find right uppercase name for %s", typename)
            if instancetype or classtype:
                self.warn("Can't find right uppercase name for %s", typename)
                self.warn("This will make type validation difficult in the future")
            return

        parent_uppercase = find_typename_uppercase(self.allfiles, parent_typename)
        if not parent_uppercase:
            self.info("Can't find right uppercase name for parent type (%s)", parent_typename)
            if instancetype or classtype:
                self.warn("Can't find right uppercase name for parent type (%s)", parent_typename)
                self.warn("This will make type validation difficult in the future")
            return

        ok = True

        #checkers: List[TypeCheckerDeclaration] = list(find_type_checkers(self.allfiles, uppercase))
        #for c in checkers:
        #    c.info("instance type checker declaration (%s) is here", c.group('uppercase'))
        #if not checkers:
        #    self.info("No type checkers declared for %s", uppercase)
        #    if instancetype or classtype:
        #        self.warn("Can't find where type checkers for %s (%s) are declared.  We will need them to validate sizes of %s",
        #                  typename, uppercase, self.name)

        if not instancetype:
            instancetype = 'void'
        if not classtype:
            classtype = 'void'

        #checker_instancetypes = set(c.instancetype for c in checkers
        #                            if c.instancetype is not None)
        #if len(checker_instancetypes) > 1:
        #    self.warn("ambiguous set of type checkers")
        #    for c in checkers:
        #        c.warn("instancetype is %s here", c.instancetype)
        #    ok = False
        #elif len(checker_instancetypes) == 1:
        #    checker_instancetype = checker_instancetypes.pop()
        #    DBG("checker instance type: %r", checker_instancetype)
        #    if instancetype != checker_instancetype:
        #        self.warn("type at instance_size is %r.  Should instance_size be set to sizeof(%s) ?",
        #                instancetype, checker_instancetype)
        #        ok = False
        #else:
        #    if instancetype != 'void':
        #        self.warn("instance type checker for %s (%s) not found", typename, instancetype)
        #        ok = False

        #checker_classtypes = set(c.classtype for c in checkers
        #                         if c.classtype is not None)
        #if len(checker_classtypes) > 1:
        #    self.warn("ambiguous set of type checkers")
        #    for c in checkers:
        #        c.warn("classtype is %s here", c.classtype)
        #    ok = False
        #elif len(checker_classtypes) == 1:
        #    checker_classtype = checker_classtypes.pop()
        #    DBG("checker class type: %r", checker_classtype)
        #    if classtype != checker_classtype:
        #        self.warn("type at class_size is %r.  Should class_size be set to sizeof(%s) ?",
        #                classtype, checker_classtype)
        #        ok = False
        #else:
        #    if classtype != 'void':
        #        self.warn("class type checker for %s (%s) not found", typename, classtype)
        #        ok = False

        #if not ok:
        #    for c in checkers:
        #        c.warn("Type checker declaration for %s (%s) is here",
        #                           typename, type(c).__name__)
        #    return

        #if parent_decl is None:
        #    self.warn("Can't find where parent type %s is declared", parent_typename)

        #yield self.prepend(f'DECLARE_TYPE_NAME({uppercase}, {typename})\n')
        #if not instancetype:
        #    yield self.prepend(f'DECLARE_INSTANCE_TYPE({uppercase}, void)\n')
        #if not classtype:
        #    yield self.prepend(f'DECLARE_CLASS_TYPE({uppercase}, void)\n')
        self.info("%s can be patched!", self.name)
        replaced_fields = ['name', 'parent', 'instance_size', 'class_size']
        begin = self.group_match('begin')
        newbegin =  f'OBJECT_DEFINE_TYPE_EXTENDED({self.name},\n'
        newbegin += f'                            {instancetype}, {classtype},\n'
        newbegin += f'                            {uppercase}, {parent_uppercase}'
        if set(values.keys()) - set(replaced_fields):
            newbegin += ',\n'
        yield begin.make_patch(newbegin)
        yield from self.remove_fields(*replaced_fields)
        end = self.group_match('end')
        yield end.make_patch(')\n')
        yield type_info_macro.make_removal_patch()

class ObjectDefineTypeExtended(TypeDefinition):
    """OBJECT_DEFINE_TYPE_EXTENDED usage"""
    regexp = S(r'^[ \t]*OBJECT_DEFINE_TYPE_EXTENDED\s*\(\s*',
               NAMED('name', RE_IDENTIFIER), r'\s*,\s*',
               NAMED('instancetype', RE_IDENTIFIER), r'\s*,\s*',
               NAMED('classtype', RE_IDENTIFIER), r'\s*,\s*',
               NAMED('uppercase', RE_IDENTIFIER), r'\s*,\s*',
               NAMED('parent_uppercase', RE_IDENTIFIER),
               M(r',\s*\n',
                 NAMED('fields', RE_TI_FIELDS),
                 n='?'),
               r'\s*\);?\n?')

class ObjectDefineType(TypeDefinition):
    """OBJECT_DEFINE_TYPE usage"""
    regexp = S(r'^[ \t]*OBJECT_DEFINE_TYPE\s*\(\s*',
               NAMED('lowercase', RE_IDENTIFIER), r'\s*,\s*',
               NAMED('uppercase', RE_IDENTIFIER), r'\s*,\s*',
               NAMED('parent_uppercase', RE_IDENTIFIER),
               M(r',\s*\n',
                 NAMED('fields', RE_TI_FIELDS),
                 n='?'),
               r'\s*\);?\n?')

def find_type_definitions(files: FileList, uppercase: str) -> Iterable[TypeDefinition]:
    types: List[Type[TypeDefinition]] = [TypeInfoVar, ObjectDefineType, ObjectDefineTypeExtended]
    for t in types:
        for m in files.matches_of_type(t):
            m.debug("uppercase: %s", m.uppercase)
    yield from (m for t in types
                  for m in files.matches_of_type(t)
                if m.uppercase == uppercase)

class AddDeclareVoidClassType(TypeDeclarationFixup):
    """Will add DECLARE_CLASS_TYPE(..., void) if possible"""
    def gen_patches_for_type(self, uppercase: str,
                             checkers: List[TypeDeclaration],
                             fields: Dict[str, Optional[str]]) -> Iterable[Patch]:
        defs = list(find_type_definitions(self.allfiles, uppercase))
        if len(defs) > 1:
            self.warn("multiple definitions for %s", uppercase)
            for d in defs:
                d.warn("definition found here")
            return
        elif len(defs) == 0:
            self.warn("type definition for %s not found", uppercase)
            return
        d = defs[0]
        if d.classtype is None:
            d.info("definition for %s has classtype, skipping", uppercase)
            return
        class_type_checkers = [c for c in checkers
                               if c.classtype is not None]
        if class_type_checkers:
            for c in class_type_checkers:
                c.warn("class type checker for %s is present here", uppercase)
            return

        _,last_checker = max((m.start(), m) for m in checkers)
        s = f'DECLARE_CLASS_TYPE({uppercase}, void)\n'
        yield last_checker.append(s)

class AddDeclareVoidInstanceType(FileMatch):
    """Will add DECLARE_INSTANCE_TYPE(..., void) if possible"""
    regexp = S(r'^[ \t]*#[ \t]*define', CPP_SPACE,
               NAMED('name', r'TYPE_[a-zA-Z0-9_]+\b'),
               CPP_SPACE, r'.*\n')

    def gen_patches(self) -> Iterable[Patch]:
        assert self.name.startswith('TYPE_')
        uppercase = self.name[len('TYPE_'):]
        defs = list(find_type_definitions(self.allfiles, uppercase))
        if len(defs) > 1:
            self.warn("multiple definitions for %s", uppercase)
            for d in defs:
                d.warn("definition found here")
            return
        elif len(defs) == 0:
            self.warn("type definition for %s not found", uppercase)
            return
        d = defs[0]
        instancetype = d.instancetype
        if instancetype is not None and instancetype != 'void':
            return

        instance_checkers = [c for c in find_type_checkers(self.allfiles, uppercase)
                             if c.instancetype]
        if instance_checkers:
            d.warn("instance type checker for %s already declared", uppercase)
            for c in instance_checkers:
                c.warn("instance checker for %s is here", uppercase)
            return

        s = f'DECLARE_INSTANCE_TYPE({uppercase}, void)\n'
        yield self.append(s)

class AddObjectDeclareType(DeclareObjCheckers):
    """Will add OBJECT_DECLARE_TYPE(...) if possible"""
    def gen_patches(self) -> Iterable[Patch]:
        uppercase = self.uppercase
        typename = self.group('typename')
        instancetype = self.group('instancetype')
        classtype = self.group('classtype')

        if typename != f'TYPE_{uppercase}':
            self.warn("type name mismatch: %s vs %s", typename, uppercase)
            return

        typedefs = [(t,self.allfiles.find_matches(SimpleTypedefMatch, t))
                    for t in (instancetype, classtype)]
        for t,tds in typedefs:
            if not tds:
                self.warn("typedef %s not found", t)
                return
            for td in tds:
                td_type = td.group('typedef_type')
                if td_type != f'struct {t}':
                    self.warn("typedef mismatch: %s is defined as %s", t, td_type)
                    td.warn("typedef is here")
                    return

        # look for reuse of same struct type
        other_instance_checkers = [c for c in find_type_checkers(self.allfiles, instancetype, 'instancetype')
                                if c.uppercase != uppercase]
        if other_instance_checkers:
            self.warn("typedef %s is being reused", instancetype)
            for ic in other_instance_checkers:
                ic.warn("%s is reused here", instancetype)
            if not self.file.force:
                return

        decl_types: List[Type[TypeDeclaration]] = [DeclareClassCheckers, DeclareObjCheckers]
        class_decls = [m for t in decl_types
                       for m in self.allfiles.find_matches(t, uppercase, 'uppercase')]

        defs = list(find_type_definitions(self.allfiles, uppercase))
        if len(defs) > 1:
            self.warn("multiple definitions for %s", uppercase)
            for d in defs:
                d.warn("definition found here")
            if not self.file.force:
                return
        elif len(defs) == 0:
            self.warn("type definition for %s not found", uppercase)
            if not self.file.force:
                return
        else:
            d = defs[0]
            if d.instancetype != instancetype:
                self.warn("mismatching instance type for %s (%s)", uppercase, instancetype)
                d.warn("instance type declared here (%s)", d.instancetype)
                if not self.file.force:
                    return
            if d.classtype != classtype:
                self.warn("mismatching class type for %s (%s)", uppercase, classtype)
                d.warn("class type declared here (%s)", d.classtype)
                if not self.file.force:
                    return

        assert self.file.original_content
        for t,tds in typedefs:
            assert tds
            for td in tds:
                if td.file is not self.file:
                    continue

                # delete typedefs that are truly redundant:
                # 1) defined after DECLARE_OBJ_CHECKERS
                if td.start() > self.start():
                    yield td.make_removal_patch()
                # 2) defined before DECLARE_OBJ_CHECKERS, but unused
                elif not re.search(r'\b'+t+r'\b', self.file.original_content[td.end():self.start()]):
                    yield td.make_removal_patch()

        c = (f'OBJECT_DECLARE_TYPE({instancetype}, {classtype}, {uppercase})\n')
        yield self.make_patch(c)

class AddObjectDeclareSimpleType(DeclareInstanceChecker):
    """Will add OBJECT_DECLARE_SIMPLE_TYPE(...) if possible"""
    def gen_patches(self) -> Iterable[Patch]:
        uppercase = self.uppercase
        typename = self.group('typename')
        instancetype = self.group('instancetype')

        if typename != f'TYPE_{uppercase}':
            self.warn("type name mismatch: %s vs %s", typename, uppercase)
            return

        typedefs = [(t,self.allfiles.find_matches(SimpleTypedefMatch, t))
                    for t in (instancetype,)]
        for t,tds in typedefs:
            if not tds:
                self.warn("typedef %s not found", t)
                return
            for td in tds:
                td_type = td.group('typedef_type')
                if td_type != f'struct {t}':
                    self.warn("typedef mismatch: %s is defined as %s", t, td_type)
                    td.warn("typedef is here")
                    return

        # look for reuse of same struct type
        other_instance_checkers = [c for c in find_type_checkers(self.allfiles, instancetype, 'instancetype')
                                if c.uppercase != uppercase]
        if other_instance_checkers:
            self.warn("typedef %s is being reused", instancetype)
            for ic in other_instance_checkers:
                ic.warn("%s is reused here", instancetype)
            if not self.file.force:
                return

        decl_types: List[Type[TypeDeclaration]] = [DeclareClassCheckers, DeclareObjCheckers]
        class_decls = [m for t in decl_types
                       for m in self.allfiles.find_matches(t, uppercase, 'uppercase')]
        if class_decls:
            self.warn("class type declared for %s", uppercase)
            for cd in class_decls:
                cd.warn("class declaration found here")
            return

        defs = list(find_type_definitions(self.allfiles, uppercase))
        if len(defs) > 1:
            self.warn("multiple definitions for %s", uppercase)
            for d in defs:
                d.warn("definition found here")
            if not self.file.force:
                return
        elif len(defs) == 0:
            self.warn("type definition for %s not found", uppercase)
            if not self.file.force:
                return
        else:
            d = defs[0]
            if d.instancetype != instancetype:
                self.warn("mismatching instance type for %s (%s)", uppercase, instancetype)
                d.warn("instance type declared here (%s)", d.instancetype)
                if not self.file.force:
                    return
            if d.classtype:
                self.warn("class type set for %s", uppercase)
                d.warn("class type declared here")
                if not self.file.force:
                    return

        assert self.file.original_content
        for t,tds in typedefs:
            assert tds
            for td in tds:
                if td.file is not self.file:
                    continue

                # delete typedefs that are truly redundant:
                # 1) defined after DECLARE_OBJ_CHECKERS
                if td.start() > self.start():
                    yield td.make_removal_patch()
                # 2) defined before DECLARE_OBJ_CHECKERS, but unused
                elif not re.search(r'\b'+t+r'\b', self.file.original_content[td.end():self.start()]):
                    yield td.make_removal_patch()

        c = (f'OBJECT_DECLARE_SIMPLE_TYPE({instancetype}, {uppercase})\n')
        yield self.make_patch(c)


class TypeInfoStringName(TypeInfoVar):
    """Replace hardcoded type names with TYPE_ constant"""
    def gen_patches(self) -> Iterable[Patch]:
        values = self.initializers
        if values is None:
            return
        if 'name' not in values:
            self.warn("name not set in TypeInfo variable %s", self.name)
            return
        typename = values['name'].raw
        if re.fullmatch(RE_IDENTIFIER, typename):
            return

        self.warn("name %s is not an identifier", typename)
        #all_defines = [m for m in self.allfiles.matches_of_type(ExpressionDefine)]
        #self.debug("all_defines: %r", all_defines)
        constants = [m for m in self.allfiles.matches_of_type(ExpressionDefine)
                     if m.group('value').strip() == typename.strip()]
        if not constants:
            self.warn("No macro for %s found", typename)
            return
        if len(constants) > 1:
            self.warn("I don't know which macro to use: %r", constants)
            return
        yield self.patch_field_value('name', constants[0].name)

class RedundantTypeSizes(TypeInfoVar):
    """Remove redundant instance_size/class_size from TypeInfo vars"""
    def gen_patches(self) -> Iterable[Patch]:
        values = self.initializers
        if values is None:
            return
        if 'name' not in values:
            self.warn("name not set in TypeInfo variable %s", self.name)
            return
        typename = values['name'].raw
        if 'parent' not in values:
            self.warn("parent not set in TypeInfo variable %s", self.name)
            return
        parent_typename = values['parent'].raw

        if 'instance_size' not in values and 'class_size' not in values:
            self.debug("no need to validate %s", self.name)
            return

        instance_decls = find_type_checkers(self.allfiles, typename)
        if instance_decls:
            self.debug("won't touch TypeInfo var that has type checkers")
            return

        parent = find_type_info(self.allfiles, parent_typename)
        if not parent:
            self.warn("Can't find TypeInfo for %s", parent_typename)
            return

        if 'instance_size' in values and parent.get_raw_initializer_value('instance_size') != values['instance_size'].raw:
            self.info("instance_size mismatch")
            parent.info("parent type declared here")
            return

        if 'class_size' in values and parent.get_raw_initializer_value('class_size') != values['class_size'].raw:
            self.info("class_size mismatch")
            parent.info("parent type declared here")
            return

        self.debug("will patch variable %s", self.name)

        if 'instance_size' in values:
            self.debug("deleting instance_size")
            yield self.patch_field('instance_size', '')

        if 'class_size' in values:
            self.debug("deleting class_size")
            yield self.patch_field('class_size', '')


#class TypeInfoVarInitFuncs(TypeInfoVar):
#    """TypeInfo variable
#    Will create missing init functions
#    """
#    def gen_patches(self) -> Iterable[Patch]:
#        values = self.initializers
#        if values is None:
#            self.warn("type not parsed completely: %s", self.name)
#            return
#
#        macro = self.file.find_match(TypeInfoVar, self.name)
#        if macro is None:
#            self.warn("No TYPE_INFO macro for %s", self.name)
#            return
#
#        ids = self.extract_identifiers()
#        if ids is None:
#            return
#
#        DBG("identifiers extracted: %r", ids)
#        fields = set(values.keys())
#        if ids.lowercase:
#            if 'instance_init' not in fields:
#                yield self.prepend(('static void %s_init(Object *obj)\n'
#                                    '{\n'
#                                    '}\n\n') % (ids.lowercase))
#                yield self.append_field('instance_init', ids.lowercase+'_init')
#
#            if 'instance_finalize' not in fields:
#                yield self.prepend(('static void %s_finalize(Object *obj)\n'
#                                    '{\n'
#                                    '}\n\n') % (ids.lowercase))
#                yield self.append_field('instance_finalize', ids.lowercase+'_finalize')
#
#
#            if 'class_init' not in fields:
#                yield self.prepend(('static void %s_class_init(ObjectClass *oc,\n'
#                                                              'const void *data)\n'
#                                    '{\n'
#                                    '}\n\n') % (ids.lowercase))
#                yield self.append_field('class_init', ids.lowercase+'_class_init')

class TypeInitMacro(FileMatch):
    """Use of type_init(...) macro"""
    regexp = S(r'^[ \t]*type_init\s*\(\s*', NAMED('name', RE_IDENTIFIER), r'\s*\);?[ \t]*\n')

class DeleteEmptyTypeInitFunc(TypeInitMacro):
    """Delete empty function declared using type_init(...)"""
    def gen_patches(self) -> Iterable[Patch]:
        fn = self.file.find_match(StaticVoidFunction, self.name)
        DBG("function for %s: %s", self.name, fn)
        if fn and fn.body == '':
            yield fn.make_patch('')
            yield self.make_patch('')

class StaticVoidFunction(FileMatch):
    """simple static void function
    (no replacement rules)
    """
    #NOTE: just like RE_FULL_STRUCT, this doesn't parse any of the body contents
    #      of the function.  Tt will just look for "}" in the beginning of a line
    regexp = S(r'static\s+void\s+', NAMED('name', RE_IDENTIFIER), r'\s*\(\s*void\s*\)\n',
               r'{\n',
               NAMED('body',
                     # acceptable inside the function body:
                     # - lines starting with space or tab
                     # - empty lines
                     # - preprocessor directives
                     OR(r'[ \t][^\n]*\n',
                        r'#[^\n]*\n',
                        r'\n',
                        repeat='*')),
               r'};?\n')

    @property
    def body(self) -> str:
        return self.group('body')

    def has_preprocessor_directive(self) -> bool:
        return bool(re.search(r'^[ \t]*#', self.body, re.MULTILINE))

def find_containing_func(m: FileMatch) -> Optional['StaticVoidFunction']:
    """Return function containing this match"""
    for fn in m.file.matches_of_type(StaticVoidFunction):
        if fn.contains(m):
            return fn
    return None

class TypeRegisterStaticCall(FileMatch):
    """type_register_static() call
    Will be replaced by TYPE_INFO() macro
    """
    regexp = S(r'^[ \t]*', NAMED('func_name', 'type_register_static'),
               r'\s*\(&\s*', NAMED('name', RE_IDENTIFIER), r'\s*\);[ \t]*\n')

class UseTypeInfo(TypeRegisterStaticCall):
    """Replace type_register_static() call with TYPE_INFO declaration"""
    def gen_patches(self) -> Iterable[Patch]:
        fn = find_containing_func(self)
        if fn:
            DBG("%r is inside %r", self, fn)
            type_init = self.file.find_match(TypeInitMacro, fn.name)
            if type_init is None:
                self.warn("can't find type_init(%s) line", fn.name)
                if not self.file.force:
                    return
        else:
            self.warn("can't identify the function where type_register_static(&%s) is called", self.name)
            if not self.file.force:
                return

        #if fn.has_preprocessor_directive() and not self.file.force:
        #    self.warn("function %s has preprocessor directives, this requires --force", fn.name)
        #    return

        var = self.file.find_match(TypeInfoVar, self.name)
        if var is None:
            self.warn("can't find TypeInfo var declaration for %s", self.name)
            return

        if not var.is_full():
            self.warn("variable declaration %s wasn't parsed fully", var.name)
            if not self.file.force:
                return

        if fn and fn.contains(var):
            self.warn("TypeInfo %s variable is inside a function", self.name)
            if not self.file.force:
                return

        # delete type_register_static() call:
        yield self.make_patch('')
        # append TYPE_REGISTER(...) after variable declaration:
        yield var.append(f'TYPE_INFO({self.name})\n')

class TypeRegisterCall(FileMatch):
    """type_register_static() call"""
    regexp = S(r'^[ \t]*', NAMED('func_name', 'type_register'),
               r'\s*\(&\s*', NAMED('name', RE_IDENTIFIER), r'\s*\);[ \t]*\n')

class TypeInfoMacro(FileMatch):
    """TYPE_INFO macro usage"""
    regexp = S(r'^[ \t]*TYPE_INFO\s*\(\s*', NAMED('name', RE_IDENTIFIER), r'\s*\)[ \t]*;?[ \t]*\n')

def find_type_info(files: RegexpScanner, name: str) -> Optional[TypeInfoVar]:
    ti = [ti for ti in files.matches_of_type(TypeInfoVar)
            if ti.get_raw_initializer_value('name') == name]
    DBG("type info vars: %r", ti)
    if len(ti) > 1:
        DBG("multiple TypeInfo vars found for %s", name)
        return None
    if len(ti) == 0:
        DBG("no TypeInfo var found for %s", name)
        return None
    return ti[0]

class CreateClassStruct(DeclareInstanceChecker):
    """Replace DECLARE_INSTANCE_CHECKER with OBJECT_DECLARE_SIMPLE_TYPE"""
    def gen_patches(self) -> Iterable[Patch]:
        typename = self.group('typename')
        DBG("looking for TypeInfo variable for %s", typename)
        var = find_type_info(self.allfiles, typename)
        if var is None:
            self.warn("no TypeInfo var found for %s", typename)
            return
        assert var.initializers
        if 'class_size' in var.initializers:
            self.warn("class size already set for TypeInfo %s", var.name)
            return
        classtype = self.group('instancetype')+'Class'
        return
        yield
        #TODO: need to find out what's the parent class type...
        #yield var.append_field('class_size', f'sizeof({classtype})')
        #c = (f'OBJECT_DECLARE_SIMPLE_TYPE({instancetype}, {lowercase},\n'
        #     f'                           MODULE_OBJ_NAME, ParentClassType)\n')
        #yield self.make_patch(c)

def type_infos(file: FileInfo) -> Iterable[TypeInfoVar]:
    return file.matches_of_type(TypeInfoVar)

def full_types(file: FileInfo) -> Iterable[TypeInfoVar]:
    return [t for t in type_infos(file) if t.is_full()]

def partial_types(file: FileInfo) -> Iterable[TypeInfoVar]:
    return [t for t in type_infos(file) if not t.is_full()]
