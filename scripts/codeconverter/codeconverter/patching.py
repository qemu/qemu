# Copyright (C) 2020 Red Hat Inc.
#
# Authors:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
from typing import IO, Match, NamedTuple, Optional, Literal, Iterable, Type, Dict, List, Any, TypeVar, NewType, Tuple, Union
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
        self.match: Match[str] = m

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

    def group(self, group: Union[int, str]) -> str:
        return self.match.group(group)

    def getgroup(self, group: str) -> Optional[str]:
        if group not in self.match.groupdict():
            return None
        return self.match.group(group)

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
    def finditer(klass, content: str, pos=0, endpos=-1) -> Iterable[Match]:
        """Helper for re.finditer()"""
        if endpos >= 0:
            content = content[:endpos]
        return klass.compiled_re().finditer(content, pos)

    @classmethod
    def domatch(klass, content: str, pos=0, endpos=-1) -> Optional[Match]:
        """Helper for re.match()"""
        if endpos >= 0:
            content = content[:endpos]
        return klass.compiled_re().match(content, pos)

    def group_finditer(self, klass: Type['FileMatch'], group: Union[str, int]) -> Iterable['FileMatch']:
        assert self.file.original_content
        return (klass(self.file, m)
                for m in klass.finditer(self.file.original_content,
                                        self.match.start(group),
                                        self.match.end(group)))

    def try_group_match(self, klass: Type['FileMatch'], group: Union[str, int]) -> Optional['FileMatch']:
        assert self.file.original_content
        m = klass.domatch(self.file.original_content,
                          self.match.start(group),
                          self.match.end(group))
        if not m:
            return None
        else:
            return klass(self.file, m)

    def group_match(self, group: Union[str, int]) -> 'FileMatch':
        m = self.try_group_match(FullMatch, group)
        assert m
        return m

    @property
    def allfiles(self) -> 'FileList':
        return self.file.allfiles

class FullMatch(FileMatch):
    """Regexp that will match all contents of string
    Useful when used with group_match()
    """
    regexp = r'(?s).*' # (?s) is re.DOTALL

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
    def patch_sort_key(item: Tuple[int, Patch]) -> Tuple[int, int, int]:
        """Patches are sorted by byte position,
        patches at the same byte position are applied in the order
        they were generated.
        """
        i,p = item
        return (p.start, p.end, i)

    for i,p in sorted(enumerate(patches), key=patch_sort_key):
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

    def _matches_of_type(self, klass: Type[Any]) -> Iterable[FileMatch]:
        raise NotImplementedError()

    def matches_of_type(self, t: Type[T]) -> List[T]:
        if t not in self.match_index:
            self.match_index[t] = list(self._matches_of_type(t))
        return self.match_index[t] # type: ignore

    def find_matches(self, t: Type[T], name: str, group: str='name') -> List[T]:
        indexkey = (t, name, group)
        if indexkey in self.match_name_index:
            return self.match_name_index[indexkey] # type: ignore
        r: List[T] = []
        for m in self.matches_of_type(t):
            assert isinstance(m, FileMatch)
            if m.getgroup(group) == name:
                r.append(m) # type: ignore
        self.match_name_index[indexkey] = r # type: ignore
        return r

    def find_match(self, t: Type[T], name: str, group: str='name') -> Optional[T]:
        l = self.find_matches(t, name, group)
        if not l:
            return None
        if len(l) > 1:
            logger.warn("multiple matches found for %r (%s=%r)", t, group, name)
            return None
        return l[0]

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

    def filename_matches(self, name: str) -> bool:
        nameparts = Path(name).parts
        return self.filename.parts[-len(nameparts):] == nameparts

    def line_col(self, start: int) -> LineAndColumn:
        """Return line and column for a match object inside original_content"""
        return line_col(self.original_content, start)

    def _matches_of_type(self, klass: Type[Any]) -> List[FileMatch]:
        """Build FileMatch objects for each match of regexp"""
        if not hasattr(klass, 'regexp') or klass.regexp is None:
            return []
        assert hasattr(klass, 'regexp')
        DBG("%s: scanning for %s", self.filename, klass.__name__)
        DBG("regexp: %s", klass.regexp)
        matches = [klass(self, m) for m in klass.finditer(self.original_content)]
        DBG('%s: %d matches found for %s: %s', self.filename, len(matches),
            klass.__name__,' '.join(names(matches)))
        return matches

    def find_match(self, t: Type[T], name: str, group: str='name') -> Optional[T]:
        for m in self.matches_of_type(t):
            assert isinstance(m, FileMatch)
            if m.getgroup(group) == name:
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

    def gen_patches(self, matches: List[FileMatch]) -> None:
        for m in matches:
            DBG("Generating patches for %r", m)
            for i,p in enumerate(m.gen_patches()):
                DBG("patch %d generated by %r:", i, m)
                DBG("replace contents at %s-%s with %r",
                    self.line_col(p.start), self.line_col(p.end), p.replacement)
                self.patches.append(p)

    def scan_for_matches(self, class_names: Optional[List[str]]=None) -> Iterable[FileMatch]:
        DBG("class names: %r", class_names)
        class_dict = match_class_dict()
        if class_names is None:
            DBG("default class names")
            class_names = list(name for name,klass in class_dict.items()
                               if klass.has_replacement_rule())
        DBG("class_names: %r", class_names)
        for cn in class_names:
            matches = self.matches_of_type(class_dict[cn])
            DBG('%d matches found for %s: %s',
                    len(matches), cn, ' '.join(names(matches)))
            yield from matches

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

    def _matches_of_type(self, klass: Type[Any]) -> Iterable[FileMatch]:
        return chain(*(f._matches_of_type(klass) for f in self.files))

    def find_file(self, name: str) -> Optional[FileInfo]:
        """Get file with path ending with @name"""
        for f in self.files:
            if f.filename_matches(name):
                return f
        else:
            return None

    def one_pass(self, class_names: List[str]) -> int:
        total_patches = 0
        for f in self.files:
            INFO("Scanning file %s", f.filename)
            matches = list(f.scan_for_matches(class_names))
            INFO("Generating patches for file %s", f.filename)
            f.gen_patches(matches)
            total_patches += len(f.patches)
        if total_patches:
            for f in self.files:
                try:
                    f.apply_patches()
                except PatchingError:
                    logger.exception("%s: failed to patch file", f.filename)
        return total_patches

    def patch_content(self, max_passes, class_names: List[str]) -> None:
        """Multi-pass content patching loop

        We run multiple passes because there are rules that will
        delete init functions once they become empty.
        """
        passes = 0
        total_patches  = 0
        DBG("max_passes: %r", max_passes)
        while not max_passes or max_passes <= 0 or passes < max_passes:
            passes += 1
            INFO("Running pass: %d", passes)
            count = self.one_pass(class_names)
            DBG("patch content: pass %d: %d patches generated", passes, count)
            total_patches += count
        DBG("%d patches applied total in %d passes", total_patches, passes)
