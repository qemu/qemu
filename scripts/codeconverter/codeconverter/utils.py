# Copyright (C) 2020 Red Hat Inc.
#
# Authors:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
from typing import *

import logging
logger = logging.getLogger(__name__)
DBG = logger.debug
INFO = logger.info
WARN = logger.warning

T = TypeVar('T')
def opt_compare(a: T, b: T) -> bool:
    """Compare two values, ignoring mismatches if one of them is None"""
    return (a is None) or (b is None) or (a == b)

def merge(a: T, b: T) -> T:
    """Merge two values if they matched using opt_compare()"""
    assert opt_compare(a, b)
    if a is None:
        return b
    else:
        return a

def test_comp_merge():
    assert opt_compare(None, 1) == True
    assert opt_compare(2, None) == True
    assert opt_compare(1, 1) == True
    assert opt_compare(1, 2) == False

    assert merge(None, None) is None
    assert merge(None, 10) == 10
    assert merge(10, None) == 10
    assert merge(10, 10) == 10


LineNumber = NewType('LineNumber', int)
ColumnNumber = NewType('ColumnNumber', int)
class LineAndColumn(NamedTuple):
    line: int
    col: int

    def __str__(self):
        return '%d:%d' % (self.line, self.col)

def line_col(s, position: int) -> LineAndColumn:
    """Return line and column for a char position in string

    Character position starts in 0, but lines and columns start in 1.
    """
    before = s[:position]
    lines = before.split('\n')
    line = len(lines)
    col = len(lines[-1]) + 1
    return LineAndColumn(line, col)

def test_line_col():
    assert line_col('abc\ndefg\nhijkl', 0) == (1, 1)
    assert line_col('abc\ndefg\nhijkl', 2) == (1, 3)
    assert line_col('abc\ndefg\nhijkl', 3) == (1, 4)
    assert line_col('abc\ndefg\nhijkl', 4) == (2, 1)
    assert line_col('abc\ndefg\nhijkl', 10) == (3, 2)

def not_optional(arg: Optional[T]) -> T:
    assert arg is not None
    return arg

__all__ = ['not_optional', 'opt_compare', 'merge', 'line_col', 'LineAndColumn']