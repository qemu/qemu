#!/bin/bash

# Formats emmc and installs u-boot and p5 images to eMMC
# Installs certificates for signature validation and ssl
# This script is a customized version of the original script located at 
# /usr/bin/install_yocto.sh in the recovery sdcard (sumo-fslc-4.9.88-mx6ul-v1.1.img.gz)

set -e

blue_underlined_bold_echo()
{
    echo -e "\e[34m\e[4m\e[1m$@\e[0m"
    echo
}

blue_bold_echo()
{
    echo -e "\e[34m\e[1m$@\e[0m"
    echo
}

red_bold_echo()
{
    echo -e "\e[31m\e[1m$@\e[0m"
    echo
}


if [[ $EUID != 0 ]] ; then
    red_bold_echo "This script must be run with super-user privileges"
    exit 1
fi

delete_emmc()
{
    echo
    blue_underlined_bold_echo "Deleting current partitions"

    for ((i=0; i<=10; i++))
    do
        if [[ -e ${NODE}${PART}${i} ]] ; then
            dd if=/dev/zero of=${NODE}${PART}${i} bs=1024 count=1024 2> /dev/null || true
        fi
    done
    sync

    ((echo d; echo 1; echo d; echo 2; echo d; echo 3; echo d; echo w) | /sbin/fdisk $NODE &> /dev/null) || true
    sync

    dd if=/dev/zero of=$NODE bs=1M count=4
    sync; sleep 1
}


create_emmc_parts()
{
    echo
    blue_underlined_bold_echo "Creating new partitions"
    TOTAL_SECTORS=`cat /sys/class/block/${BLOCK}/size`
    SECT_SIZE_BYTES=`cat /sys/block/${BLOCK}/queue/hw_sector_size`


    # The emmc structure should be same as below (Start/End/Size might change): 
    # Number  Start     End        Size       Type      File system  Flags
    # 1         <>      <>          <>      primary   fat32
    # 2         <>      <>          <>      primary   fat32
    # 3         <>      <>          <>      extended
    # 5         <>      <>          <>      logical   fat32
    # 6         <>      <>          <>      logical   fat32
    # 7         <>      <>          <>      logical   fat32
    # 8         <>      <>          <>      logical   fat32
    # 9         <>      <>          <>      logical   fat32
    ######

    # Reserved size for bootloader
    BOOTLOAD_RESERVE_SIZE=4
    # Active/passive partition size in MB
    SAFERTOS_PART_SIZE=256
    # Boot flags partittion size in MB
    BOOTFLAGS_PART_SIZE=64
    # Data partition size in MB
    DATA_PART_SIZE=4096
    # Temp partition size in MB
    TEMP_PART_SIZE=256
    # Reserved partition size in MB
    SPARE_PART_SIZE=2048
    # Boot images partition size in MB
    KEYS_PART_SIZE=64

    # Sectors in 1 MB
    sectors=$((1024 * 1024 / ${SECT_SIZE_BYTES}))

    # GAP between logical partitions in MB
    GAP=1

    # First partition start point - it is fixed to 8192s, first 4 mb is reserved for bootloader
    # The GAP before the first partition is allocated for sdcard bootloader
    first_safertos_start=$((${BOOTLOAD_RESERVE_SIZE} * ${sectors}))
    first_safertos_end=$((${SAFERTOS_PART_SIZE} * ${sectors} + ${first_safertos_start} - 1))

    second_safertos_start=$((${first_safertos_end} + 1))
    second_safertos_end=$((${SAFERTOS_PART_SIZE} * ${sectors} + ${second_safertos_start} - 1))

    # Total size of the extended partition - bootflags + data + reserved
    extended_part_total=$(( (${BOOTFLAGS_PART_SIZE} + ${DATA_PART_SIZE} + ${TEMP_PART_SIZE} + ${SPARE_PART_SIZE}+ ${KEYS_PART_SIZE} + ${GAP} * 5 ) * ${sectors}  ))

    extended_start=$((${second_safertos_end} + 1))
    extended_end=$((${extended_part_total} + ${extended_start} - 1))

    bootflags_start=$(($extended_start + ${GAP} * ${sectors}))
    bootflags_end=$((${BOOTFLAGS_PART_SIZE} * ${sectors} + ${bootflags_start} - 1))

    data_part_start=$(($bootflags_end + ${GAP} * ${sectors} + 1 ))
    data_part_end=$((${DATA_PART_SIZE} * ${sectors} + ${data_part_start} - 1))

    temp_part_start=$(($data_part_end + ${GAP} * ${sectors} + 1 ))
    temp_part_end=$((${TEMP_PART_SIZE} * ${sectors} + ${temp_part_start} - 1))

    spare_part_start=$(($temp_part_end + ${GAP} * ${sectors} + 1 ))
    spare_part_end=$((${SPARE_PART_SIZE} * ${sectors} + ${spare_part_start} - 1))

    keys_part_start=$(($spare_part_end + ${GAP} * ${sectors} + 1 ))
    keys_part_end=$((${KEYS_PART_SIZE} * ${sectors} + ${keys_part_start} - 1))

    # Sanity check

    list=($first_safertos_start $first_safertos_end $second_safertos_start $second_safertos_end $extended_start $extended_end $bootflags_start $bootflags_end $data_part_start $data_part_end $temp_part_start $temp_part_end $spare_part_start $spare_part_end $keys_part_start $keys_part_end)

    # There are 16 numbers in total
    array_length=${#list[@]}

    if [ ! $array_length -eq 16 ]; then
        echo "ERROR: There are more or less than 16 numbers: $array_length."
        exit 1
    fi

    # They are all numbers and positive
    re='^[0-9]+$'

    for number in ${list[@]}
    do
        if ! [[ $number =~ $re ]] ; then
            echo "Error: $number is not a positive number."
            exit 1
        fi
    done

    echo "Sdcard structure will be as below:"
    echo "-----------------------------------"
    printf "%-3s %-8s %-8s\n" "No" "Start" "End"
    printf "1   %-8s %-8s\n" "${first_safertos_start}" "${first_safertos_end}"
    printf "2   %-8s %-8s\n" "${second_safertos_start}" "${second_safertos_end}"
    printf "3   %-8s %-8s\n" "${extended_start}" "${extended_end}"
    printf "5   %-8s %-8s\n" "${bootflags_start}" "${bootflags_end}"
    printf "6   %-8s %-8s\n" "${data_part_start}" "${data_part_end}"
    printf "7   %-8s %-8s\n" "${temp_part_start}" "${temp_part_end}"
    printf "8   %-8s %-8s\n" "${spare_part_start}" "${spare_part_end}"
    printf "9   %-8s %-8s\n" "${keys_part_start}" "${keys_part_end}"
    echo "-----------------------------------"

    (echo n; echo p; echo ${SAFERTOS_PART_1};  echo $first_safertos_start; echo $first_safertos_end; \
    echo n; echo p; echo ${SAFERTOS_PART_2}; echo $second_safertos_start; echo $second_safertos_end; \
    echo n; echo e; echo ${EXTENDED_PART}; echo $extended_start; echo $extended_end; \
    echo p; echo n; echo l; echo $bootflags_start; echo $bootflags_end; \
    echo n; echo l; echo $data_part_start; echo $data_part_end; \
    echo n; echo l; echo $temp_part_start; echo $temp_part_end; \
    echo n; echo l; echo $spare_part_start; echo $spare_part_end; \
    echo n; echo l; echo $keys_part_start; echo $keys_part_end; \
    echo p; echo w) | /sbin/fdisk -u $NODE > /dev/null

    sync; sleep 1
    /sbin/fdisk -u -l $NODE
}

format_emmc_parts()
{
    echo
    blue_underlined_bold_echo "Formatting partitions"

    /usr/sbin/mkfs.vfat -F 32 ${NODE}${PART}${SAFERTOS_PART_1} -n ${SAFERTOS_PART_1_VOLUME_NAME}
    /usr/sbin/mkfs.vfat -F 32 ${NODE}${PART}${SAFERTOS_PART_2} -n ${SAFERTOS_PART_2_VOLUME_NAME}
    /usr/sbin/mkfs.vfat -F 32 ${NODE}${PART}${BOOTFLAGS_PART} -n ${BOOTFLAGS_PART_VOLUME_NAME}
    /usr/sbin/mkfs.vfat -F 32 ${NODE}${PART}${DATA_PART} -n ${DATA_PART_VOLUME_NAME}
    /usr/sbin/mkfs.vfat -F 32 ${NODE}${PART}${TEMP_PART} -n ${TEMP_PART_VOLUME_NAME}
    /usr/sbin/mkfs.vfat -F 32 ${NODE}${PART}${SPARE_PART} -n ${SPARE_PART_VOLUME_NAME}
    /usr/sbin/mkfs.vfat -F 32 ${NODE}${PART}${KEYS_PART} -n ${KEYS_PART_VOLUME_NAME}
    
    sync; sleep 1
}

finish()
{
    echo
    blue_bold_echo "EMMC partitioned successfully."
    exit 0
}


blue_underlined_bold_echo "*** Nanosonics Variscite IMX6ULL EMMC partitioning Script ***"


blue_bold_echo "Creating partitions"
BLOCK=loop0
        
NODE=/dev/${BLOCK}
if [[ ! -b $NODE ]] ; then
    red_bold_echo "ERROR: Can't find eMMC device ($NODE)."
    red_bold_echo "Please verify you are using the correct options for your SOM."
    exit 1
fi

PART=p
MOUNTDIR_PREFIX=/run/media/${BLOCK}${PART}
SAFERTOS_PART_1=1
SAFERTOS_PART_2=2
EXTENDED_PART=3
BOOTFLAGS_PART=5
DATA_PART=6
TEMP_PART=7
SPARE_PART=8
KEYS_PART=9

SAFERTOS_PART_1_VOLUME_NAME="SAFERTOS1"
SAFERTOS_PART_2_VOLUME_NAME="SAFERTOS2"
BOOTFLAGS_PART_VOLUME_NAME="BOOTFLAGS"
DATA_PART_VOLUME_NAME="DATA"
TEMP_PART_VOLUME_NAME="TEMP"
SPARE_PART_VOLUME_NAME="SPARE"
KEYS_PART_VOLUME_NAME="KEYS"


umount ${NODE}${PART}*  2> /dev/null || true

delete_emmc

create_emmc_parts
    
format_emmc_parts

finish
