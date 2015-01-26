#!/bin/bash

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

    echo -e "\n\n=== Running test case: $kernel $@ ===\n" >> test.log

    $QEMU \
        -kernel $kernel \
        -display none \
        -device isa-debugcon,chardev=stdio \
        -chardev file,path=test.out,id=stdio \
        -device isa-debug-exit,iobase=0xf4,iosize=0x4 \
        "$@"
    ret=$?

    cat test.out >> test.log
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

make all

for t in mmap modules; do

    echo > test.log
    $t

    debugexit=$((ret & 0x1))
    ret=$((ret >> 1))
    pass=1

    if [ $debugexit != 1 ]; then
        echo -e "\e[31m ?? \e[0m $t (no debugexit used, exit code $ret)"
        pass=0
    elif [ $ret != 0 ]; then
        echo -e "\e[31mFAIL\e[0m $t (exit code $ret)"
        pass=0
    fi

    if ! diff $t.out test.log > /dev/null 2>&1; then
        echo -e "\e[31mFAIL\e[0m $t (output difference)"
        diff -u $t.out test.log
        pass=0
    fi

    if [ $pass == 1 ]; then
        echo -e "\e[32mPASS\e[0m $t"
    fi

done
