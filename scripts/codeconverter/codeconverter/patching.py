# Copyright (C) 2020 Red Hat Inc.
#
# Authors:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
from typing import IO, Match, NamedTuple, Optional, Literal, Iterable, Type, Dict, List, Any, TypeVar, NewType, Tuple
from pathlib import Path
from itertools import chain
from tempfile import NamedTemporaryFile
import os
import re
import subprocess
from io import StringIO

import logging
logger = logging.getLogger(__name__)
DBG = logger.debug
INFO = logger.info
WARN = logger.warning
ERROR = logger.error

from .utils import *

T = TypeVar('T')

class Patch(NamedTuple):
    # start inside file.original_content
    start: int
    # end position inside file.original_content
    end: int
    # replacement string for file.original_content[start:end]
    replacement: str

IdentifierType = Literal['type', 'symbol', 'include', 'constant']
class RequiredIdentifier(NamedTuple):
    type: IdentifierType
    name: str

class FileMatch:
    """Base class for regex matches

    Subclasses just need to set the `regexp` class attribute
    """
    regexp: Optional[str] = None

    def __init__(self, f: 'FileInfo', m: Match) -> None:
        self.file: 'FileInfo' = f
        self.match: Match = m

    @property
    def name(self) -> str:
        if 'name' not in self.match.groupdict():
            return '[no name]'
        return self.group('name')

    @classmethod
    def compiled_re(klass):
        return re.compile(klass.regexp, re.MULTILINE)

    def start(self) -> int:
        return self.match.start()

    def end(self) -> int:
        return self.match.end()

    def line_col(self) -> LineAndColumn:
        return self.file.line_col(self.start())

    def group(self, *args):
        return self.match.group(*args)

    def log(self, level, fmt, *args) -> None:
        pos = self.line_col()
        logger.log(level, '%s:%d:%d: '+fmt, self.file.filename, pos.line, pos.col, *args)

    def debug(self, fmt, *args) -> None:
        self.log(logging.DEBUG, fmt, *args)

    def info(self, fmt, *args) -> None:
        self.log(logging.INFO, fmt, *args)

    def warn(self, fmt, *args) -> None:
        self.log(logging.WARNING, fmt, *args)

    def error(self, fmt, *args) -> None:
        self.log(logging.ERROR, fmt, *args)

    def sub(self, original: str, replacement: str) -> str:
        """Replace content

        XXX: this won't use the match position, but will just
        replace all strings that look like the original match.
        This should be enough for all the patterns used in this
        script.
        """
        return original.replace(self.group(0), replacement)

    def sanity_check(self) -> None:
        """Sanity check match, and print warnings if necessary"""
        pass

    def replacement(self) -> Optional[str]:
        """Return replacement text for pattern, to use new code conventions"""
        return None

    def make_patch(self, replacement: str) -> 'Patch':
        """Make patch replacing the content of this match"""
        return Patch(self.start(), self.end(), replacement)

    def make_subpatch(self, start: int, end: int, replacement: str) -> 'Patch':
        return Patch(self.start() + start, self.start() + end, replacement)

    def make_removal_patch(self) -> 'Patch':
        """Make patch removing contents of match completely"""
        return self.make_patch('')

    def append(self, s: str) -> 'Patch':
        """Make patch appending string after this match"""
        return Patch(self.end(), self.end(), s)

    def prepend(self, s: str) -> 'Patch':
        """Make patch prepending string before this match"""
        return Patch(self.start(), self.start(), s)

    def gen_patches(self) -> Iterable['Patch']:
        """Patch source code contents to use new code patterns"""
        replacement = self.replacement()
        if replacement is not None:
            yield self.make_patch(replacement)

    @classmethod
    def has_replacement_rule(klass) -> bool:
        return (klass.gen_patches is not FileMatch.gen_patches
                or klass.replacement is not FileMatch.replacement)

    def contains(self, other: 'FileMatch') -> bool:
        return other.start() >= self.start() and other.end() <= self.end()

    def __repr__(self) -> str:
        start = self.file.line_col(self.start())
        end = self.file.line_col(self.end() - 1)
        return '<%s %s at %d:%d-%d:%d: %r>' % (self.__class__.__name__,
                                                    self.name,
                                                    start.line, start.col,
                                                    end.line, end.col, self.group(0)[:100])

    def required_identifiers(self) -> Iterable[RequiredIdentifier]:
        """Can be implemented by subclasses to keep track of identifier references

        This method will be used by the code that moves declarations around the file,
        to make sure we find the right spot for them.
        """
        raise NotImplementedError()

    def provided_identifiers(self) -> Iterable[RequiredIdentifier]:
        """Can be implemented by subclasses to keep track of identifier references

        This method will be used by the code that moves declarations around the file,
        to make sure we find the right spot for them.
        """
        raise NotImplementedError()

    @classmethod
    def find_matches(klass, content: str) -> Iterable[Match]:
        """Generate match objects for class

        Might be reimplemented by subclasses if they
        intend to look for matches using a different method.
        """
        return klass.compiled_re().finditer(content)

    @property
    def allfiles(self) -> 'FileList':
        return self.file.allfiles

def all_subclasses(c: Type[FileMatch]) -> Iterable[Type[FileMatch]]:
    for sc in c.__subclasses__():
        yield sc
        yield from all_subclasses(sc)

def match_class_dict() -> Dict[str, Type[FileMatch]]:
    d = dict((t.__name__, t) for t in all_subclasses(FileMatch))
    return d

def names(matches: Iterable[FileMatch]) -> Iterable[str]:
    return [m.name for m in matches]

class PatchingError(Exception):
    pass

class OverLappingPatchesError(PatchingError):
    pass

def apply_patches(s: str, patches: Iterable[Patch]) -> str:
    """Apply a sequence of patches to string

    >>> apply_patches('abcdefg', [Patch(2,2,'xxx'), Patch(0, 1, 'yy')])
    'yybxxxcdefg'
    """
    r = StringIO()
    last = 0
    for p in sorted(patches):
        DBG("Applying patch at position %d (%s) - %d (%s): %r",
            p.start, line_col(s, p.start),
            p.end, line_col(s, p.end),
            p.replacement)
        if last > p.start:
            raise OverLappingPatchesError("Overlapping patch at position %d (%s), last patch at %d (%s)" % \
                (p.start, line_col(s, p.start), last, line_col(s, last)))
        r.write(s[last:p.start])
        r.write(p.replacement)
        last = p.end
    r.write(s[last:])
    return r.getvalue()

class RegexpScanner:
    def __init__(self) -> None:
        self.match_index: Dict[Type[Any], List[FileMatch]] = {}
        self.match_name_index: Dict[Tuple[Type[Any], str, str], Optional[FileMatch]] = {}

    def _find_matches(self, klass: Type[Any]) -> Iterable[FileMatch]:
        raise NotImplementedError()

    def matches_of_type(self, t: Type[T]) -> List[T]:
        if t not in self.match_index:
            self.match_index[t] = list(self._find_matches(t))
        return  self.match_index[t] # type: ignore

    def find_match(self, t: Type[T], name: str, group: str='name') -> Optional[T]:
        indexkey = (t, name, group)
        if indexkey in self.match_name_index:
            return self.match_name_index[indexkey] # type: ignore
        r: Optional[T] = None
        for m in self.matches_of_type(t):
            assert isinstance(m, FileMatch)
            if m.group(group) == name:
                r = m # type: ignore
        self.match_name_index[indexkey] = r # type: ignore
        return r

    def reset_index(self) -> None:
        self.match_index.clear()
        self.match_name_index.clear()

class FileInfo(RegexpScanner):
    filename: Path
    original_content: Optional[str] = None

    def __init__(self, files: 'FileList', filename: os.PathLike, force:bool=False) -> None:
        super().__init__()
        self.allfiles = files
        self.filename = Path(filename)
        self.patches: List[Patch] = []
        self.force = force

    def __repr__(self) -> str:
        return f'<FileInfo {repr(self.filename)}>'

    def line_col(self, start: int) -> LineAndColumn:
        """Return line and column for a match object inside original_content"""
        return line_col(self.original_content, start)

    def _find_matches(self, klass: Type[Any]) -> List[FileMatch]:
        """Build FileMatch objects for each match of regexp"""
        if not hasattr(klass, 'regexp') or klass.regexp is None:
            return []
        assert hasattr(klass, 'regexp')
        DBG("%s: scanning for %s", self.filename, klass.__name__)
        DBG("regexp: %s", klass.regexp)
        matches = [klass(self, m) for m in klass.find_matches(self.original_content)]
        DBG('%s: %d matches found for %s: %s', self.filename, len(matches),
            klass.__name__,' '.join(names(matches)))
        return matches

    def find_match(self, t: Type[T], name: str, group: str='name') -> Optional[T]:
        for m in self.matches_of_type(t):
            assert isinstance(m, FileMatch)
            if m.group(group) == name:
                return m # type: ignore
        return None

    def reset_content(self, s:str):
        self.original_content = s
        self.patches.clear()
        self.reset_index()
        self.allfiles.reset_index()

    def load(self) -> None:
        if self.original_content is not None:
            return
        with open(self.filename, 'rt') as f:
            self.reset_content(f.read())

    @property
    def all_matches(self) -> Iterable[FileMatch]:
        lists = list(self.match_index.values())
        return (m for l in lists
                  for m in l)

    def scan_for_matches(self, class_names: Optional[List[str]]=None) -> None:
        DBG("class names: %r", class_names)
        class_dict = match_class_dict()
        if class_names is None:
            DBG("default class names")
            class_names = list(name for name,klass in class_dict.items()
                               if klass.has_replacement_rule())
        DBG("class_names: %r", class_names)
        for cn in class_names:
            matches = self.matches_of_type(class_dict[cn])
            if len(matches) > 0:
                DBG('%s: %d matches found for %s: %s', self.filename,
                     len(matches), cn, ' '.join(names(matches)))

    def gen_patches(self) -> None:
        for m in self.all_matches:
            for i,p in enumerate(m.gen_patches()):
                DBG("patch %d generated by %r:", i, m)
                DBG("replace contents at %s-%s with %r",
                    self.line_col(p.start), self.line_col(p.end), p.replacement)
                self.patches.append(p)

    def patch_content(self, max_passes=0, class_names: Optional[List[str]]=None) -> None:
        """Multi-pass content patching loop

        We run multiple passes because there are rules that will
        delete init functions once they become empty.
        """
        passes = 0
        total_patches  = 0
        DBG("max_passes: %r", max_passes)
        while not max_passes or max_passes <= 0 or passes < max_passes:
            passes += 1
            self.scan_for_matches(class_names)
            self.gen_patches()
            DBG("patch content: pass %d: %d patches generated", passes, len(self.patches))
            total_patches += len(self.patches)
            if not self.patches:
                break
            try:
                self.apply_patches()
            except PatchingError:
                logger.exception("%s: failed to patch file", self.filename)
        DBG("%s: %d patches applied total in %d passes", self.filename, total_patches, passes)

    def apply_patches(self) -> None:
        """Replace self.original_content after applying patches from self.patches"""
        self.reset_content(self.get_patched_content())

    def get_patched_content(self) -> str:
        assert self.original_content is not None
        return apply_patches(self.original_content, self.patches)

    def write_to_file(self, f: IO[str]) -> None:
        f.write(self.get_patched_content())

    def write_to_filename(self, filename: os.PathLike) -> None:
        with open(filename, 'wt') as of:
            self.write_to_file(of)

    def patch_inplace(self) -> None:
        newfile = self.filename.with_suffix('.changed')
        self.write_to_filename(newfile)
        os.rename(newfile, self.filename)

    def show_diff(self) -> None:
        with NamedTemporaryFile('wt') as f:
            self.write_to_file(f)
            f.flush()
            subprocess.call(['diff', '-u', self.filename, f.name])

    def ref(self):
        return TypeInfoReference

class FileList(RegexpScanner):
    def __init__(self):
        super().__init__()
        self.files: List[FileInfo] = []

    def extend(self, *args, **kwargs):
        self.files.extend(*args, **kwargs)

    def __iter__(self):
        return iter(self.files)

    def _find_matches(self, klass: Type[Any]) -> Iterable[FileMatch]:
        return chain(*(f._find_matches(klass) for f in self.files))

    def find_file(self, name) -> Optional[FileInfo]:
        """Get file with path ending with @name"""
        nameparts = Path(name).parts
        for f in self.files:
            if f.filename.parts[:len(nameparts)] == nameparts:
                return f
        else:
            return None