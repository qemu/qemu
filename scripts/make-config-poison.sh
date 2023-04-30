#! /bin/sh

if test $# = 0; then
  exit 0
fi

# Create list of config switches that should be poisoned in common code,
# but filter out several which are handled manually.
exec sed -n \
  -e' /CONFIG_TCG/d' \
  -e '/CONFIG_USER_ONLY/d' \
  -e '/CONFIG_SOFTMMU/d' \
  -e '/^#define / {' \
  -e    's///' \
  -e    's/ .*//' \
  -e    's/^/#pragma GCC poison /p' \
  -e '}' "$@" | sort -u
