#!/usr/bin/env python3
#
# Generate a Coccinelle semantic patch to remove pointless QOM cast.
#
# Usage:
#
# $ qom-cast-macro-clean-cocci-gen.py $(git ls-files) > qom_pointless_cast.cocci
# $ spatch \
#           --macro-file scripts/cocci-macro-file.h \
#           --sp-file qom_pointless_cast.cocci \
#           --keep-comments \
#           --use-gitgrep \
#           --in-place \
#           --dir .
#
# SPDX-FileContributor: Philippe Mathieu-Daudé <philmd@linaro.org>
# SPDX-FileCopyrightText: 2023 Linaro Ltd.
# SPDX-License-Identifier: GPL-2.0-or-later

import re
import sys

assert len(sys.argv) > 0

def print_cocci_rule(qom_typedef, qom_cast_macro):
    print(f'''@@
typedef {qom_typedef};
{qom_typedef} *obj;
@@
-    {qom_cast_macro}(obj)
+    obj
''')

patterns = [
    r'DECLARE_INSTANCE_CHECKER\((\w+),\W*(\w+),\W*TYPE_\w+\)',
    r'DECLARE_OBJ_CHECKERS\((\w+),\W*\w+,\W*(\w+),\W*TYPE_\w+\)',
    r'OBJECT_DECLARE_TYPE\((\w+),\W*\w+,\W*(\w+)\)',
    r'OBJECT_DECLARE_SIMPLE_TYPE\((\w+),\W*(\w+)\)',
    r'INTERFACE_CHECK\((\w+),\W*\(\w+\),\W*TYPE_(\w+)\)',
]

for fn in sys.argv[1:]:
    try:
        content = open(fn, 'rt').read()
    except:
        continue
    for pattern in patterns:
        for match in re.findall(pattern, content):
            print_cocci_rule(match[0], match[1])
