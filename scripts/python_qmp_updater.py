#!/usr/bin/env python3
#
# Intended usage:
#
# git grep -l '\.qmp(' | xargs ./scripts/python_qmp_updater.py
#

import re
import sys
from typing import Optional

start_reg = re.compile(r'^(?P<padding> *)(?P<res>\w+) = (?P<vm>.*).qmp\(',
                       flags=re.MULTILINE)

success_reg_templ = re.sub('\n *', '', r"""
    (\n*{padding}(?P<comment>\#.*$))?
    \n*{padding}
    (
        self.assert_qmp\({res},\ 'return',\ {{}}\)
    |
        assert\ {res}\['return'\]\ ==\ {{}}
    |
        assert\ {res}\ ==\ {{'return':\ {{}}}}
    |
        self.assertEqual\({res}\['return'\],\ {{}}\)
    )""")

some_check_templ = re.sub('\n *', '', r"""
    (\n*{padding}(?P<comment>\#.*$))?
    \s*self.assert_qmp\({res},""")


def tmatch(template: str, text: str,
           padding: str, res: str) -> Optional[re.Match[str]]:
    return re.match(template.format(padding=padding, res=res), text,
                    flags=re.MULTILINE)


def find_closing_brace(text: str, start: int) -> int:
    """
    Having '(' at text[start] search for pairing ')' and return its index.
    """
    assert text[start] == '('

    height = 1

    for i in range(start + 1, len(text)):
        if text[i] == '(':
            height += 1
        elif text[i] == ')':
            height -= 1
        if height == 0:
            return i

    raise ValueError


def update(text: str) -> str:
    result = ''

    while True:
        m = start_reg.search(text)
        if m is None:
            result += text
            break

        result += text[:m.start()]

        args_ind = m.end()
        args_end = find_closing_brace(text, args_ind - 1)

        all_args = text[args_ind:args_end].split(',', 1)

        name = all_args[0]
        args = None if len(all_args) == 1 else all_args[1]

        unchanged_call = text[m.start():args_end+1]
        text = text[args_end+1:]

        padding, res, vm = m.group('padding', 'res', 'vm')

        m = tmatch(success_reg_templ, text, padding, res)

        if m is None:
            result += unchanged_call

            if ('query-' not in name and
                    'x-debug-block-dirty-bitmap-sha256' not in name and
                    not tmatch(some_check_templ, text, padding, res)):
                print(unchanged_call + text[:200] + '...\n\n')

            continue

        if m.group('comment'):
            result += f'{padding}{m.group("comment")}\n'

        result += f'{padding}{vm}.cmd({name}'

        if args:
            result += ','

            if '\n' in args:
                m_args = re.search('(?P<pad> *).*$', args)
                assert m_args is not None

                cur_padding = len(m_args.group('pad'))
                expected = len(f'{padding}{res} = {vm}.qmp(')
                drop = len(f'{res} = ')
                if cur_padding == expected - 1:
                    # tolerate this bad style
                    drop -= 1
                elif cur_padding < expected - 1:
                    # assume nothing to do
                    drop = 0

                if drop:
                    args = re.sub('\n' + ' ' * drop, '\n', args)

            result += args

        result += ')'

        text = text[m.end():]

    return result


for fname in sys.argv[1:]:
    print(fname)
    with open(fname) as f:
        t = f.read()

    t = update(t)

    with open(fname, 'w') as f:
        f.write(t)
