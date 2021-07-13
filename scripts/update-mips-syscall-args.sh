#!/bin/sh

URL=https://raw.githubusercontent.com/strace/strace/master/src
FILES="sysent.h sysent_shorthand_defs.h linux/mips/syscallent-compat.h \
       linux/mips/syscallent-o32.h linux/32/syscallent-common-32.h \
       linux/generic/syscallent-common.h"

output="$1"
if [ "$output" = "" ] ; then
    output="$PWD"
fi

INC=linux-user/mips/syscall-args-o32.c.inc

TMP=$(mktemp -d)
cd $TMP

for file in $FILES; do
    curl --create-dirs $URL/$file -o $TMP/$file
done

> linux/generic/subcallent.h
> linux/32/subcallent.h

cat > gen_mips_o32.c <<EOF
#include <stdio.h>

#define LINUX_MIPSO32
#define MAX_ARGS 7

#include "sysent.h"
#include "sysent_shorthand_defs.h"

#define SEN(syscall_name) 0,0
const struct_sysent sysent0[] = {
#include  "syscallent-o32.h"
};

int main(void)
{
    int i;

    for (i = 4000; i < sizeof(sysent0) / sizeof(struct_sysent); i++) {
        if (sysent0[i].sys_name == NULL) {
            printf("    [% 4d] = MIPS_SYSCALL_NUMBER_UNUSED,\n", i - 4000);
        } else {
            printf("    [% 4d] = %d, /* %s */\n", i - 4000,
                   sysent0[i].nargs, sysent0[i].sys_name);
        }
    }

    return 0;
}
EOF

cc -o gen_mips_o32 -I linux/mips -I linux/generic gen_mips_o32.c && ./gen_mips_o32 > "$output/$INC"

rm -fr "$TMP"
