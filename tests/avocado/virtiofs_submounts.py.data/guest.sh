#!/bin/bash

function print_usage()
{
    if [ -n "$2" ]; then
        echo "Error: $2"
        echo
    fi
    echo "Usage: $1 <shared dir>"
    echo '(The shared directory is the "share" directory in the scratch' \
         'directory)'
}

shared_dir=$1
if [ -z "$shared_dir" ]; then
    print_usage "$0" 'Shared dir not given' >&2
    exit 1
fi

cd "$shared_dir"

# FIXME: This should not be necessary, but it is.  In order for all
# submounts to be proper mount points, we need to visit them.
# (Before we visit them, they will not be auto-mounted, and so just
# appear as normal directories, with the catch that their st_ino will
# be the st_ino of the filesystem they host, while the st_dev will
# still be the st_dev of the parent.)
# `find` does not work, because it will refuse to touch the mount
# points as long as they are not mounted; their st_dev being shared
# with the parent and st_ino just being the root node's inode ID
# will practically ensure that this node exists elsewhere on the
# filesystem, and `find` is required to recognize loops and not to
# follow them.
# Thus, we have to manually visit all nodes first.

mnt_i=0

function recursively_visit()
{
    pushd "$1" >/dev/null
    for entry in *; do
        if [[ "$entry" == mnt* ]]; then
            mnt_i=$((mnt_i + 1))
            printf "Triggering auto-mount $mnt_i...\r"
        fi

        if [ -d "$entry" ]; then
            recursively_visit "$entry"
        fi
    done
    popd >/dev/null
}

recursively_visit .
echo


if [ -n "$(find -name not-mounted)" ]; then
    echo "Error: not-mounted files visible on mount points:" >&2
    find -name not-mounted >&2
    exit 1
fi

if [ ! -f some-file -o "$(cat some-file)" != 'root' ]; then
    echo "Error: Bad file in the share root" >&2
    exit 1
fi

shopt -s nullglob

function check_submounts()
{
    local base_path=$1

    for mp in mnt*; do
        printf "Checking submount %i...\r" "$((${#devs[@]} + 1))"

        mp_i=$(echo "$mp" | sed -e 's/mnt//')
        dev=$(stat -c '%D' "$mp")

        if [ -n "${devs[mp_i]}" ]; then
            echo "Error: $mp encountered twice" >&2
            exit 1
        fi
        devs[mp_i]=$dev

        pushd "$mp" >/dev/null
        path="$base_path$mp"
        while true; do
            expected_content="$(printf '%s\n%s\n' "$mp_i" "$path")"
            if [ ! -f some-file ]; then
                echo "Error: $PWD/some-file does not exist" >&2
                exit 1
            fi

            if [ "$(cat some-file)" != "$expected_content" ]; then
                echo "Error: Bad content in $PWD/some-file:" >&2
                echo '--- found ---'
                cat some-file
                echo '--- expected ---'
                echo "$expected_content"
                exit 1
            fi
            if [ "$(stat -c '%D' some-file)" != "$dev" ]; then
                echo "Error: $PWD/some-file has the wrong device ID" >&2
                exit 1
            fi

            if [ -d sub ]; then
                if [ "$(stat -c '%D' sub)" != "$dev" ]; then
                    echo "Error: $PWD/some-file has the wrong device ID" >&2
                    exit 1
                fi
                cd sub
                path="$path/sub"
            else
                if [ -n "$(echo mnt*)" ]; then
                    check_submounts "$path/"
                fi
                break
            fi
        done
        popd >/dev/null
    done
}

root_dev=$(stat -c '%D' some-file)
devs=()
check_submounts ''
echo

reused_devs=$(echo "$root_dev ${devs[@]}" | tr ' ' '\n' | sort | uniq -d)
if [ -n "$reused_devs" ]; then
    echo "Error: Reused device IDs: $reused_devs" >&2
    exit 1
fi

echo "Test passed for ${#devs[@]} submounts."
