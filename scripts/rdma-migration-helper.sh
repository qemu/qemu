#!/bin/bash

# Copied from blktests
get_ipv4_addr()
{
    ip -4 -o addr show dev "$1" |
        sed -n 's/.*[[:blank:]]inet[[:blank:]]*\([^[:blank:]/]*\).*/\1/p' |
        head -1 | tr -d '\n'
}

get_ipv6_addr() {
    ipv6=$(ip -6 -o addr show dev "$1" |
        sed -n 's/.*[[:blank:]]inet6[[:blank:]]*\([^[:blank:]/]*\).*/\1/p' |
        head -1 | tr -d '\n')

    [ $? -eq 0 ] || return

    if [[ "$ipv6" =~ ^fe80: ]]; then
        echo -n "[$ipv6%$1]"
    else
        echo -n "[$ipv6]"
    fi
}

# existing rdma interfaces
rdma_interfaces()
{
    rdma link show | sed -nE 's/^link .* netdev ([^ ]+).*$/\1 /p' |
    grep -Ev '^(lo|tun|tap)'
}

# existing valid ipv4 interfaces
ipv4_interfaces()
{
    ip -o addr show | awk '/inet / {print $2}' | grep -Ev '^(lo|tun|tap)'
}

ipv6_interfaces()
{
    ip -o addr show | awk '/inet6 / {print $2}' | grep -Ev '^(lo|tun|tap)'
}

rdma_rxe_detect()
{
    family=$1
    for r in $(rdma_interfaces)
    do
        "$family"_interfaces | grep -qw $r && get_"$family"_addr $r && return
    done

    return 1
}

rdma_rxe_setup()
{
    family=$1
    for i in $("$family"_interfaces)
    do
        if rdma_interfaces | grep -qw $i; then
            echo "$family: Reuse the existing rdma/rxe ${i}_rxe" \
                 "for $i with $(get_"$family"_addr $i)"
            return
        fi

        rdma link add "${i}_rxe" type rxe netdev "$i" && {
            echo "$family: Setup new rdma/rxe ${i}_rxe" \
                 "for $i with $(get_"$family"_addr $i)"
            return
        }
    done

    echo "$family: Failed to setup any new rdma/rxe link" >&2
    return 1
}

rdma_rxe_clean()
{
    modprobe -r rdma_rxe
}

IP_FAMILY=${IP_FAMILY:-ipv4}
if [ "$IP_FAMILY" != "ipv6" ] && [ "$IP_FAMILY" != "ipv4" ]; then
    echo "Unknown ip family '$IP_FAMILY', only ipv4 or ipv6 is supported." >&2
    exit 1
fi

operation=${1:-detect}

command -v rdma >/dev/null || {
    echo "Command 'rdma' is not available, please install it first." >&2
    exit 1
}

if [ "$operation" == "setup" ] || [ "$operation" == "clean" ]; then
    [ "$UID" == 0 ] || {
        echo "Root privilege is required to setup/clean a rdma/rxe link" >&2
        exit 1
    }
    if [ "$operation" == "setup" ]; then
        rdma_rxe_setup ipv4
        rdma_rxe_setup ipv6
    else
        rdma_rxe_clean
    fi
elif [ "$operation" == "detect" ]; then
    rdma_rxe_detect "$IP_FAMILY"
else
    echo "Usage: $0 [setup | detect | clean]"
fi
