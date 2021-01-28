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

# Disable tests with any sanitizer except for specific ones
SANITIZE_FLAGS=$( grep "CFLAGS.*-fsanitize" config-host.mak 2>/dev/null )
ALLOWED_SANITIZE_FLAGS="safe-stack cfi-icall"
#Remove all occurrencies of allowed Sanitize flags
for j in ${ALLOWED_SANITIZE_FLAGS}; do
    TMP_FLAGS=${SANITIZE_FLAGS}
    SANITIZE_FLAGS=""
    for i in ${TMP_FLAGS}; do
        if ! echo ${i} | grep -q "${j}" 2>/dev/null; then
            SANITIZE_FLAGS="${SANITIZE_FLAGS} ${i}"
        fi
    done
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

if LANG=C bash --version | grep -q 'GNU bash, version [123]' ; then
    echo "bash version too old ==> Not running the qemu-iotests."
    exit 0
fi

if ! (sed --version | grep 'GNU sed') > /dev/null 2>&1 ; then
    if ! command -v gsed >/dev/null 2>&1; then
        echo "GNU sed not available ==> Not running the qemu-iotests."
        exit 0
    fi
else
    # Double-check that we're not using BusyBox' sed which says
    # that "This is not GNU sed version 4.0" ...
    if sed --version | grep -q 'not GNU sed' ; then
        echo "BusyBox sed not supported ==> Not running the qemu-iotests."
        exit 0
    fi
fi

cd tests/qemu-iotests

# QEMU_CHECK_BLOCK_AUTO is used to disable some unstable sub-tests
export QEMU_CHECK_BLOCK_AUTO=1
export PYTHONUTF8=1

ret=0
for fmt in $format_list ; do
    ${PYTHON} ./check -makecheck -$fmt $group || ret=1
done

exit $ret
