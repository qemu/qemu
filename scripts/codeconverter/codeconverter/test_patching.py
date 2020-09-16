# Copyright (C) 2020 Red Hat Inc.
#
# Authors:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
from tempfile import NamedTemporaryFile
from .patching import FileInfo, FileMatch, Patch, FileList
from .regexps import *

class BasicPattern(FileMatch):
    regexp = '[abc]{3}'

    @property
    def name(self):
        return self.group(0)

    def replacement(self) -> str:
        # replace match with the middle character repeated 5 times
        return self.group(0)[1].upper()*5

def test_pattern_patching():
    of = NamedTemporaryFile('wt')
    of.writelines(['one line\n',
                  'this pattern will be patched: defbbahij\n',
                  'third line\n',
                  'another pattern: jihaabfed'])
    of.flush()

    files = FileList()
    f = FileInfo(files, of.name)
    f.load()
    matches = f.matches_of_type(BasicPattern)
    assert len(matches) == 2
    p2 = matches[1]

    # manually add patch, to see if .append() works:
    f.patches.append(p2.append('XXX'))

    # apply all patches:
    f.gen_patches(matches)
    patched = f.get_patched_content()
    assert patched == ('one line\n'+
                       'this pattern will be patched: defBBBBBhij\n'+
                       'third line\n'+
                       'another pattern: jihAAAAAXXXfed')

class Function(FileMatch):
    regexp = S(r'BEGIN\s+', NAMED('name', RE_IDENTIFIER), r'\n',
               r'(.*\n)*?END\n')

class Statement(FileMatch):
    regexp = S(r'^\s*', NAMED('name', RE_IDENTIFIER), r'\(\)\n')

def test_container_match():
    of = NamedTemporaryFile('wt')
    of.writelines(['statement1()\n',
                   'statement2()\n',
                   'BEGIN function1\n',
                   '  statement3()\n',
                   '  statement4()\n',
                   'END\n',
                   'BEGIN function2\n',
                   '  statement5()\n',
                   '  statement6()\n',
                   'END\n',
                   'statement7()\n'])
    of.flush()

    files = FileList()
    f = FileInfo(files, of.name)
    f.load()
    assert len(f.matches_of_type(Function)) == 2
    print(' '.join(m.name for m in f.matches_of_type(Statement)))
    assert len(f.matches_of_type(Statement)) == 7

    f1 = f.find_match(Function, 'function1')
    f2 = f.find_match(Function, 'function2')
    st1 = f.find_match(Statement, 'statement1')
    st2 = f.find_match(Statement, 'statement2')
    st3 = f.find_match(Statement, 'statement3')
    st4 = f.find_match(Statement, 'statement4')
    st5 = f.find_match(Statement, 'statement5')
    st6 = f.find_match(Statement, 'statement6')
    st7 = f.find_match(Statement, 'statement7')

    assert not f1.contains(st1)
    assert not f1.contains(st2)
    assert not f1.contains(st2)
    assert f1.contains(st3)
    assert f1.contains(st4)
    assert not f1.contains(st5)
    assert not f1.contains(st6)
    assert not f1.contains(st7)

    assert not f2.contains(st1)
    assert not f2.contains(st2)
    assert not f2.contains(st2)
    assert not f2.contains(st3)
    assert not f2.contains(st4)
    assert f2.contains(st5)
    assert f2.contains(st6)
    assert not f2.contains(st7)
