#!/bin/bash

mount_count=128

function print_usage()
{
    if [ -n "$2" ]; then
        echo "Error: $2"
        echo
    fi
    echo "Usage: $1 <scratch dir> [seed]"
    echo "(If no seed is given, it will be randomly generated.)"
}

scratch_dir=$1
if [ -z "$scratch_dir" ]; then
    print_usage "$0" 'No scratch dir given' >&2
    exit 1
fi

if [ ! -d "$scratch_dir" ]; then
    print_usage "$0" "$scratch_dir is not a directory" >&2
    exit 1
fi

seed=$2
if [ -z "$seed" ]; then
    seed=$RANDOM
fi
RANDOM=$seed

echo "Seed: $seed"

set -e
shopt -s nullglob

cd "$scratch_dir"
if [ -d share ]; then
    echo 'Error: This directory seems to be in use already' >&2
    exit 1
fi

for ((i = 0; i < $mount_count; i++)); do
    printf "Setting up fs %i/%i...\r" "$((i + 1))" "$mount_count"

    rm -f fs$i.img
    truncate -s 512M fs$i.img
    mkfs.xfs -q fs$i.img
    devs[i]=$(sudo losetup -f --show fs$i.img)
done
echo

top_level_mounts=$((RANDOM % mount_count + 1))

mkdir -p share
echo 'root' > share/some-file

for ((i = 0; i < $top_level_mounts; i++)); do
    printf "Mounting fs %i/%i...\r" "$((i + 1))" "$mount_count"

    mkdir -p share/mnt$i
    touch share/mnt$i/not-mounted
    sudo mount "${devs[i]}" share/mnt$i
    sudo chown "$(id -u):$(id -g)" share/mnt$i

    pushd share/mnt$i >/dev/null
    path=mnt$i
    nesting=$((RANDOM % 4))
    for ((j = 0; j < $nesting; j++)); do
        cat > some-file <<EOF
$i
$path
EOF
        mkdir sub
        cd sub
        path="$path/sub"
    done
cat > some-file <<EOF
$i
$path
EOF
    popd >/dev/null
done

for ((; i < $mount_count; i++)); do
    printf "Mounting fs %i/%i...\r" "$((i + 1))" "$mount_count"

    mp_i=$((i % top_level_mounts))

    pushd share/mnt$mp_i >/dev/null
    path=mnt$mp_i
    while true; do
        sub_mp="$(echo mnt*)"
        if cd sub 2>/dev/null; then
            path="$path/sub"
        elif [ -n "$sub_mp" ] && cd "$sub_mp" 2>/dev/null; then
            path="$path/$sub_mp"
        else
            break
        fi
    done
    mkdir mnt$i
    touch mnt$i/not-mounted
    sudo mount "${devs[i]}" mnt$i
    sudo chown "$(id -u):$(id -g)" mnt$i

    cd mnt$i
    path="$path/mnt$i"
    nesting=$((RANDOM % 4))
    for ((j = 0; j < $nesting; j++)); do
        cat > some-file <<EOF
$i
$path
EOF
        mkdir sub
        cd sub
        path="$path/sub"
    done
    cat > some-file <<EOF
$i
$path
EOF
    popd >/dev/null
done
echo

echo 'Done.'
