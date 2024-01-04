#!/bin/bash

if [ ! -f config.mak ]; then
    echo "run ./configure && make first. See ./configure -h"
    exit
fi
source config.mak

config=$TEST_DIR/unittests.cfg
qemu=${QEMU:-qemu-system-$ARCH}
verbose=0

function run()
{
    local testname="$1"
    local groups="$2"
    local smp="$3"
    local kernel="$4"
    local opts="$5"
    local arch="$6"
    local check="$7"

    if [ -z "$testname" ]; then
        return
    fi

    if [ -n "$only_group" ] && ! grep -q "$only_group" <<<$groups; then
        return
    fi

    if [ -n "$arch" ] && [ "$arch" != "$ARCH" ]; then
        echo "skip $1 ($arch only)"
        return
    fi

    # check a file for a particular value before running a test
    # the check line can contain multiple files to check separated by a space
    # but each check parameter needs to be of the form <path>=<value>
    for check_param in ${check[@]}; do
        path=${check_param%%=*}
        value=${check_param#*=}
        if [ "$path" ] && [ "$(cat $path)" != "$value" ]; then
            echo "skip $1 ($path not equal to $value)"
            return
        fi
    done

    cmdline="./$TEST_DIR-run $kernel -smp $smp $opts"
    if [ $verbose != 0 ]; then
        echo $cmdline
    fi

    # extra_params in the config file may contain backticks that need to be
    # expanded, so use eval to start qemu
    eval $cmdline >> test.log

    if [ $? -le 1 ]; then
        echo -e "\e[32mPASS\e[0m $1"
    else
        echo -e "\e[31mFAIL\e[0m $1"
    fi
}

function run_all()
{
    local config="$1"
    local testname
    local smp
    local kernel
    local opts
    local groups
    local arch
    local check

    exec {config_fd}<$config

    while read -u $config_fd line; do
        if [[ "$line" =~ ^\[(.*)\]$ ]]; then
            run "$testname" "$groups" "$smp" "$kernel" "$opts" "$arch" "$check"
            testname=${BASH_REMATCH[1]}
            smp=1
            kernel=""
            opts=""
            groups=""
            arch=""
            check=""
        elif [[ $line =~ ^file\ *=\ *(.*)$ ]]; then
            kernel=$TEST_DIR/${BASH_REMATCH[1]}
        elif [[ $line =~ ^smp\ *=\ *(.*)$ ]]; then
            smp=${BASH_REMATCH[1]}
        elif [[ $line =~ ^extra_params\ *=\ *(.*)$ ]]; then
            opts=${BASH_REMATCH[1]}
        elif [[ $line =~ ^groups\ *=\ *(.*)$ ]]; then
            groups=${BASH_REMATCH[1]}
        elif [[ $line =~ ^arch\ *=\ *(.*)$ ]]; then
            arch=${BASH_REMATCH[1]}
        elif [[ $line =~ ^check\ *=\ *(.*)$ ]]; then
            check=${BASH_REMATCH[1]}
        fi
    done

    run "$testname" "$groups" "$smp" "$kernel" "$opts" "$arch" "$check"

    exec {config_fd}<&-
}

function usage()
{
cat <<EOF

Usage: $0 [-g group] [-h] [-v]

    -g: Only execute tests in the given group
    -h: Output this help text
    -v: Enables verbose mode

Set the environment variable QEMU=/path/to/qemu-system-ARCH to
specify the appropriate qemu binary for ARCH-run.

EOF
}

echo > test.log
while getopts "g:hv" opt; do
    case $opt in
        g)
            only_group=$OPTARG
            ;;
        h)
            usage
            exit
            ;;
        v)
            verbose=1
            ;;
        *)
            exit
            ;;
    esac
done

run_all $config
