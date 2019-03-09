#!/usr/bin/env bash

# Copyright (c) 2013 Kevin Wolf <kwolf@redhat.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

QEMU=${QEMU:-"../../x86_64-softmmu/qemu-system-x86_64"}

run_qemu() {
    local kernel=$1
    shift

    printf %b "\n\n=== Running test case: $kernel $* ===\n\n" >> test.log

    $QEMU \
        -kernel $kernel \
        -display none \
        -device isa-debugcon,chardev=stdio \
        -chardev file,path=test.out,id=stdio \
        -device isa-debug-exit,iobase=0xf4,iosize=0x4 \
        "$@" >> test.log 2>&1
    ret=$?

    cat test.out >> test.log

    debugexit=$((ret & 0x1))
    ret=$((ret >> 1))

    if [ $debugexit != 1 ]; then
        printf %b "\e[31m ?? \e[0m $kernel $* (no debugexit used, exit code $ret)\n"
        pass=0
    elif [ $ret != 0 ]; then
        printf %b "\e[31mFAIL\e[0m $kernel $* (exit code $ret)\n"
        pass=0
    fi
}

mmap() {
    run_qemu mmap.elf
    run_qemu mmap.elf -m 1.1M
    run_qemu mmap.elf -m 2G
    run_qemu mmap.elf -m 4G
    run_qemu mmap.elf -m 8G
}

modules() {
    run_qemu modules.elf
    run_qemu modules.elf -initrd module.txt
    run_qemu modules.elf -initrd "module.txt argument"
    run_qemu modules.elf -initrd "module.txt argument,,with,,commas"
    run_qemu modules.elf -initrd "module.txt,module.txt argument,module.txt"
}

aout_kludge() {
    for i in $(seq 1 9); do
        run_qemu aout_kludge_$i.bin
    done
}

make all

for t in mmap modules aout_kludge; do

    echo > test.log
    pass=1
    $t

    if ! diff $t.out test.log > /dev/null 2>&1; then
        printf %b "\e[31mFAIL\e[0m $t (output difference)\n"
        diff -u $t.out test.log
        pass=0
    fi

    if [ $pass == 1 ]; then
        printf %b "\e[32mPASS\e[0m $t\n"
    fi

done
