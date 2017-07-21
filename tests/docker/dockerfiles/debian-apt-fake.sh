#! /bin/sh
#
# Generate fake debian package to resolve unimportant unmet dependencies held
# by upstream multiarch broken packages.
#
# Copyright (c) 2017 Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2
# or (at your option) any later version. See the COPYING file in
# the top-level directory.

test $1 = "install" && shift 1

fake_install()
{
    echo "Generating fake $2 $1 $3 ..."
    (cd /var/cache/apt/archives
        (cat << 'EOF'
Section: misc
Priority: optional
Standards-Version: 3.9.2

Package: NAME
Version: VERSION
Maintainer: qemu-devel@nongnu.org
Architecture: any
Multi-Arch: same
Description: fake NAME
EOF
        ) | sed s/NAME/$2/g | sed s/VERSION/$3/g > $2.control
        equivs-build -a $1 $2.control 1>/dev/null 2>/dev/null
        dpkg -i --force-overwrite $2_$3_$1.deb
    )
}

try_install()
{
    name=$(echo $1|sed "s/\(.*\):\(.*\)=\(.*\)/\1/")
    arch=$(echo $1|sed "s/\(.*\):\(.*\)=\(.*\)/\2/")
    vers=$(echo $1|sed "s/\(.*\):\(.*\)=\(.*\)/\3/")
    apt-get install -q -yy $1 || fake_install $arch $name $vers
}

for package in $*; do
    try_install $package
done
