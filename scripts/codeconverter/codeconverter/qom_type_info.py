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
RE_TYPEINFO_DEF = S(RE_TYPEINFO_START,
                    M(NAMED('fields', RE_TI_FIELDS),
                      SP, NAMED('endcomments', RE_COMMENTS),
                      r'};?\n',
                      n='?', name='fullspec'))

ParsedArray = List[str]
ParsedInitializerValue = Union[str, ParsedArray]
class InitializerValue(NamedTuple):
    raw: str
    parsed: Optional[ParsedInitializerValue]
    match: Optional[Match]
TypeInfoInitializers = Dict[str, InitializerValue]

def parse_array(m: Match) -> ParsedArray:
    #DBG('parse_array: %r', m.group(0))
    return [m.group('arrayitem') for m in re.finditer(RE_ARRAY_ITEM, m.group('arrayitems'))]

def parse_initializer_value(m: Match, s: str) -> InitializerValue:
    parsed: Optional[ParsedInitializerValue] = None
    #DBG("parse_initializer_value: %r", s)
    array = re.match(RE_ARRAY, s)
    if array:
        parsed = parse_array(array)
    return InitializerValue(s, parsed, m)

class TypeInfoVar(FileMatch):
    """TypeInfo variable declaration with initializer
    Will be replaced by OBJECT_DEFINE_TYPE_EXTENDED macro
    (not implemented yet)
    """
    regexp = RE_TYPEINFO_DEF

    @property
    def initializers(self) -> Optional[TypeInfoInitializers]:
        if getattr(self, '_inititalizers', None):
            self._initializers: TypeInfoInitializers
            return self._initializers
        fields = self.group('fields')
        if fields is None:
            return None
        d = dict((fm.group('field'), parse_initializer_value(fm, fm.group('value')))
                  for fm in re.finditer(RE_TI_FIELD_INIT, fields))
        self._initializers = d
        return d

    def is_static(self) -> bool:
        return 'static' in self.group('modifiers')

    def is_full(self) -> bool:
        return bool(self.group('fullspec'))

    def get_initializers(self) -> TypeInfoInitializers:
        """Helper for code that needs to deal with missing initializer info"""
        if self.initializers is None:
            return {}
        return self.initializers

    def get_initializer_value(self, field: str) -> InitializerValue:
        return self.get_initializers().get(field, InitializerValue('', '', None))

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

    def append_field(self, field, value) -> Patch:
        """Generate patch appending a field initializer"""
        content = f'    .{field} = {value},\n'
        return Patch(self.match.end('fields'), self.match.end('fields'),
                     content)

    def patch_field(self, field: str, replacement: str) -> Patch:
        """Generate patch replacing a field initializer"""
        values = self.initializers
        assert values
        value = values.get(field)
        assert value
        fm = value.match
        assert fm
        fstart = self.match.start('fields') + fm.start()
        fend = self.match.start('fields') + fm.end()
        return Patch(fstart, fend, replacement)

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

        #NOTE: this will NOT work after declarations are converted
        #      to OBJECT_DECLARE*

        # Now, the challenge is to find out the right MODULE_OBJ_NAME for the
        # type and for the parent type
        instance_decl = find_type_declaration(self.allfiles, typename)
        parent_decl = find_type_declaration(self.allfiles, parent_typename)

        self.info("TypeInfo variable for %s is here", typename)
        if instance_decl:
            instance_decl.info("instance type declaration (%s) is here", instance_decl.match.group('uppercase'))
        if parent_decl:
            parent_decl.info("parent type declaration (%s) is here", parent_decl.match.group('uppercase'))

        ok = True
        if (instance_decl is None and (instancetype or classtype)):
            self.warn("Can't find where type checkers for %s are declared.  We need them to validate sizes of %s", typename, self.name)
            ok = False

        if (instance_decl is not None
            and 'instancetype' in instance_decl.match.groupdict()
            and instancetype != instance_decl.group('instancetype')):
            self.warn("type at instance_size is %r.  Should instance_size be set to sizeof(%s) ?",
                      instancetype, instance_decl.group('instancetype'))
            instance_decl.warn("Type checker declaration for %s is here", typename)
            ok = False
        if (instance_decl is not None
            and 'classtype' in instance_decl.match.groupdict()
            and classtype != instance_decl.group('classtype')):
            self.warn("type at class_size is %r.  Should class_size be set to sizeof(%s) ?",
                      classtype, instance_decl.group('classtype'))
            instance_decl.warn("Type checker declaration for %s is here", typename)
            ok = False

        if not ok:
            return

        #if parent_decl is None:
        #    self.warn("Can't find where parent type %s is declared", parent_typename)

        self.info("%s can be patched!", self.name)
        return
        yield

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

        instance_decl = find_type_declaration(self.allfiles, typename)
        if instance_decl:
            self.debug("won't touch TypeInfo var that has type checkers")
            return

        parent = find_type_info(self.allfiles, parent_typename)
        if not parent:
            self.warn("Can't find TypeInfo for %s", parent_typename)
            return

        if 'instance_size' in values and parent.get_initializer_value('instance_size').raw != values['instance_size'].raw:
            self.info("instance_size mismatch")
            parent.info("parent type declared here")
            return

        if 'class_size' in values and parent.get_initializer_value('class_size').raw != values['class_size'].raw:
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
#                yield self.prepend(('static void %s_class_init(ObjectClass *oc, void *data)\n'
#                                    '{\n'
#                                    '}\n\n') % (ids.lowercase))
#                yield self.append_field('class_init', ids.lowercase+'_class_init')

class TypeInitMacro(FileMatch):
    """type_init(...) macro use
    Will be deleted if function is empty
    """
    regexp = S(r'^[ \t]*type_init\s*\(\s*', NAMED('name', RE_IDENTIFIER), r'\s*\);?[ \t]*\n')
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
               r'}\n')

    @property
    def body(self) -> str:
        return self.group('body')

    def has_preprocessor_directive(self) -> bool:
        return bool(re.search(r'^[ \t]*#', self.body, re.MULTILINE))

class TypeRegisterCall(FileMatch):
    """type_register_static() call
    Will be replaced by TYPE_INFO() macro
    """
    regexp = S(r'^[ \t]*type_register_static\s*\(&\s*', NAMED('name', RE_IDENTIFIER), r'\s*\);[ \t]*\n')

    def function(self) -> Optional['StaticVoidFunction']:
        """Return function containing this call"""
        for m in self.file.matches_of_type(StaticVoidFunction):
            if m.contains(self):
                return m
        return None

    def gen_patches(self) -> Iterable[Patch]:
        fn = self.function()
        if fn is None:
            self.warn("can't find function where type_register_static(&%s) is called", self.name)
            return

        #if fn.has_preprocessor_directive() and not self.file.force:
        #    self.warn("function %s has preprocessor directives, this requires --force", fn.name)
        #    return

        type_init = self.file.find_match(TypeInitMacro, fn.name)
        if type_init is None:
            self.warn("can't find type_init(%s) line", fn.name)
            return

        var = self.file.find_match(TypeInfoVar, self.name)
        if var is None:
            self.warn("can't find TypeInfo var declaration for %s", self.name)
            return

        if not var.is_full():
            self.warn("variable declaration %s wasn't parsed fully", var.name)
            return

        if fn.contains(var):
            self.warn("TypeInfo %s variable is inside a function", self.name)
            return

        # delete type_register_static() call:
        yield self.make_patch('')
        # append TYPE_REGISTER(...) after variable declaration:
        yield var.append(f'TYPE_INFO({self.name})\n')

class TypeInfoMacro(FileMatch):
    """TYPE_INFO macro usage"""
    regexp = S(r'^[ \t]*TYPE_INFO\s*\(\s*', NAMED('name', RE_IDENTIFIER), r'\s*\)[ \t]*;?[ \t]*\n')

def find_type_info(files: RegexpScanner, name: str) -> Optional[TypeInfoVar]:
    ti = [ti for ti in files.matches_of_type(TypeInfoVar)
            if ti.get_initializer_value('name').raw == name]
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
