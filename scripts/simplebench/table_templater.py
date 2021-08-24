# Parser for test templates
#
# Copyright (c) 2021 Virtuozzo International GmbH.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

import itertools
from lark import Lark

grammar = """
start: ( text | column_switch | row_switch )+

column_switch: "{" text ["|" text]+ "}"
row_switch: "[" text ["|" text]+ "]"
text: /[^|{}\[\]]+/
"""

parser = Lark(grammar)

class Templater:
    def __init__(self, template):
        self.tree = parser.parse(template)

        c_switches = []
        r_switches = []
        for x in self.tree.children:
            if x.data == 'column_switch':
                c_switches.append([el.children[0].value for el in x.children])
            elif x.data == 'row_switch':
                r_switches.append([el.children[0].value for el in x.children])

        self.columns = list(itertools.product(*c_switches))
        self.rows = list(itertools.product(*r_switches))

    def gen(self, column, row):
        i = 0
        j = 0
        result = []

        for x in self.tree.children:
            if x.data == 'text':
                result.append(x.children[0].value)
            elif x.data == 'column_switch':
                result.append(column[i])
                i += 1
            elif x.data == 'row_switch':
                result.append(row[j])
                j += 1

        return ''.join(result)
