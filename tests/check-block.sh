#!/bin/sh

if [ "$#" -eq 0 ]; then
    echo "Usage: $0 fmt..." >&2
    exit 99
fi

# Honor the SPEED environment variable, just like we do it for "meson test"
format_list="$@"
if [ "$SPEED" = "slow" ] || [ "$SPEED" = "thorough" ]; then
    group=
else
    group="-g auto"
fi

skip() {
    echo "1..0 #SKIP $*"
    exit 0
}

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
    skip "Sanitizers are enabled ==> Not running the qemu-iotests."
fi

if [ -z "$(find . -name 'qemu-system-*' -print)" ]; then
    skip "No qemu-system binary available ==> Not running the qemu-iotests."
fi

if ! command -v bash >/dev/null 2>&1 ; then
    skip "bash not available ==> Not running the qemu-iotests."
fi

if LANG=C bash --version | grep -q 'GNU bash, version [123]' ; then
    skip "bash version too old ==> Not running the qemu-iotests."
fi

cd tests/qemu-iotests

# QEMU_CHECK_BLOCK_AUTO is used to disable some unstable sub-tests
export QEMU_CHECK_BLOCK_AUTO=1
export PYTHONUTF8=1
# If make was called with -jN we want to call ./check with -j N. Extract the
# flag from MAKEFLAGS, so that if it absent (or MAKEFLAGS is not defined), JOBS
# would be an empty line otherwise JOBS is prepared string of flag with value:
# "-j N"
# Note, that the following works even if make was called with "-j N" or even
# "--jobs N", as all these variants becomes simply "-jN" in MAKEFLAGS variable.
JOBS=$(echo "$MAKEFLAGS" | sed -n 's/\(^\|.* \)-j\([0-9]\+\)\( .*\|$\)/-j \2/p')

ret=0
for fmt in $format_list ; do
    ${PYTHON} ./check $JOBS -tap -$fmt $group || ret=1
done

exit $ret
