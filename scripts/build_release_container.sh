#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

script_dir=$(dirname $(readlink -f $0))

# The oldest buildable release we support is v3.1.0, as previous ones require
# additional tweaks in Dockerfile.
# For building previous releases (down to v0.12.0),
# see: 20261014005338.34657-1-pierrick.bouvier@oss.qualcomm.com
oldest_release=v3.1.0

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "usage: version [git_repo]"
    echo
    echo "- version: tag or sha-1 (>= v3.1.0)"
    echo "- git_repo: (optional) QEMU repository URL"
    exit 1
fi
version=$1;shift

git_repo="https://gitlab.com/qemu-project/qemu.git"
if [ $# -eq 1 ]; then
    git_repo=$1;shift
fi

year=$(date +%Y)
month=$(date +%m)
if git rev-parse $version >& /dev/null; then
    year=$(git log -1 --format='%cd' --date='format:%Y' $version)
    month=$(git log -1 --format='%cd' --date='format:%m' $version)
fi

# Ubuntu LTS are published every (even) two years in April.
ubuntu=
if [ $((year % 2)) -eq 1 ]; then
    # pick latest LTS from previous year
    ubuntu=$((year - 1))
elif [ $month -gt 6 ]; then
    # LTS published this year should work
    ubuntu=$year
else
    # pick latest LTS from two years ago
    ubuntu=$((year - 2))
fi
if [ $ubuntu -lt 2014 ]; then
    # oldest ubuntu release available with containers is 14.04
    ubuntu=2014
fi
ubuntu=$((ubuntu - 2000))
ubuntu_version=$ubuntu.04

dockerfile=$script_dir/../tests/docker/dockerfiles/release.docker
docker=$($script_dir/../tests/docker/docker.py probe)

set -x
$docker build . \
    -f $dockerfile \
    --build-arg QEMU_VERSION=$version \
    --build-arg UBUNTU_VERSION=$ubuntu_version \
    --build-arg QEMU_REPO="$git_repo" \
    -t qemu/$version
