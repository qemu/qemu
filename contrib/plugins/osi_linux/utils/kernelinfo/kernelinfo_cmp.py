#!/usr/bin/env python2
''' Simple utility to compare sections in a config file.
'''

import ConfigParser
import sys
import pprint

class MyParser(ConfigParser.ConfigParser):
    KEYS_NO_COMPARE = ['name']

    def section_compare(self, seca, secb):
        diff = {}

        if not self.has_section(seca) or not self.has_section(secb):
            return (False, diff)

        itemsa = dict(self.items(seca))
        itemsb = dict(self.items(secb))
        keys = set(itemsa.keys() + itemsb.keys()) - set(MyParser.KEYS_NO_COMPARE)
        for k in sorted(keys):
            va = None
            if k in itemsa:
                try:
                    va = int(itemsa[k])
                except ValueError:
                    va = itemsa[k]

            vb = None
            if k in itemsb:
                try:
                    vb = int(itemsb[k])
                except ValueError:
                    vb = itemsb[k]

            if va != vb:
                diff[k] = (va, vb)

        return (not diff, diff)


if __name__ == '__main__':
    parser = MyParser()
    parser.read(sys.argv[1])

    if sys.argv[2] == '-l':
        for s in parser.sections():
            pprint.pprint(s)
    else:
        eq, diff = parser.section_compare(sys.argv[2], sys.argv[3])
        if eq:
            print("sections match")
        else:
            pprint.pprint(diff)

