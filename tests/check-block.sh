#!/bin/sh

# Honor the SPEED environment variable, just like we do it for the qtests.
if [ "$SPEED" = "slow" ]; then
    format_list="raw qcow2"
    group=
elif [ "$SPEED" = "thorough" ]; then
    format_list="raw qcow2 qed vmdk vpc"
    group=
else
    format_list=qcow2
    group="-g auto"
fi

if [ "$#" -ne 0 ]; then
    format_list="$@"
fi

if grep -q "CONFIG_GPROF=y" config-host.mak 2>/dev/null ; then
    echo "GPROF is enabled ==> Not running the qemu-iotests."
    exit 0
fi

# Disable tests with any sanitizer except for SafeStack
CFLAGS=$( grep "CFLAGS.*-fsanitize" config-host.mak 2>/dev/null )
SANITIZE_FLAGS=""
#Remove all occurrencies of -fsanitize=safe-stack
for i in ${CFLAGS}; do
        if [ "${i}" != "-fsanitize=safe-stack" ]; then
                SANITIZE_FLAGS="${SANITIZE_FLAGS} ${i}"
        fi
done
if echo ${SANITIZE_FLAGS} | grep -q "\-fsanitize" 2>/dev/null; then
    # Have a sanitize flag that is not allowed, stop
    echo "Sanitizers are enabled ==> Not running the qemu-iotests."
    exit 0
fi

if [ -z "$(find . -name 'qemu-system-*' -print)" ]; then
    echo "No qemu-system binary available ==> Not running the qemu-iotests."
    exit 0
fi

if ! command -v bash >/dev/null 2>&1 ; then
    echo "bash not available ==> Not running the qemu-iotests."
    exit 0
fi

if ! (sed --version | grep 'GNU sed') > /dev/null 2>&1 ; then
    if ! command -v gsed >/dev/null 2>&1; then
        echo "GNU sed not available ==> Not running the qemu-iotests."
        exit 0
    fi
fi

cd tests/qemu-iotests

ret=0
for fmt in $format_list ; do
    ./check -makecheck -$fmt $group || ret=1
done

exit $ret
