# QEMU Monitor Protocol Lexer Extension
#
# Copyright (C) 2019, Red Hat Inc.
#
# Authors:
#  Eduardo Habkost <ehabkost@redhat.com>
#  John Snow <jsnow@redhat.com>
#
# This work is licensed under the terms of the GNU GPLv2 or later.
# See the COPYING file in the top-level directory.
"""qmp_lexer is a Sphinx extension that provides a QMP lexer for code blocks."""

from pygments.lexer import RegexLexer, DelegatingLexer
from pygments.lexers.data import JsonLexer
from pygments import token
from sphinx import errors

class QMPExampleMarkersLexer(RegexLexer):
    """
    QMPExampleMarkersLexer lexes QMP example annotations.
    This lexer adds support for directionality flow and elision indicators.
    """
    tokens = {
        'root': [
            (r'-> ', token.Generic.Prompt),
            (r'<- ', token.Generic.Prompt),
            (r' ?\.{3} ?', token.Generic.Prompt),
        ]
    }

class QMPExampleLexer(DelegatingLexer):
    """QMPExampleLexer lexes annotated QMP examples."""
    def __init__(self, **options):
        super(QMPExampleLexer, self).__init__(JsonLexer, QMPExampleMarkersLexer,
                                              token.Error, **options)

def setup(sphinx):
    """For use by the Sphinx extensions API."""
    try:
        sphinx.require_sphinx('2.1')
        sphinx.add_lexer('QMP', QMPExampleLexer)
    except errors.VersionRequirementError:
        sphinx.add_lexer('QMP', QMPExampleLexer())

    return dict(
        parallel_read_safe = True,
        parallel_write_safe = True
    )
