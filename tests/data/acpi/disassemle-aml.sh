#!/usr/bin/bash

outdir=
while getopts "o:" arg; do
  case ${arg} in
    o )
        outdir=$OPTARG
        ;;
    \? )
        echo "Usage: ./tests/data/acpi/disassemle-aml.sh [-o <output-directory>]"
        exit 1
        ;;

  esac
done

for machine in tests/data/acpi/*
do
    if [[ ! -d "$machine" ]];
    then
        continue
    fi

    if [[ "${outdir}" ]];
    then
        mkdir -p "${outdir}"/${machine} || exit $?
    fi
    for aml in $machine/*
    do
        if [[ "$aml" == $machine/*.dsl ]];
        then
            continue
        fi
        if [[ "$aml" == $machine/SSDT*.* ]];
        then
            dsdt=${aml/SSDT*./DSDT.}
            extra="-e ${dsdt}"
        elif [[ "$aml" == $machine/SSDT* ]];
        then
            dsdt=${aml/SSDT*/DSDT};
            extra="-e ${dsdt}"
        else
            extra=""
        fi
        asl=${aml}.dsl
        if [[ "${outdir}" ]];
        then
            asl="${outdir}"/${machine}/${asl}
        fi
        iasl -d -p ${asl} ${extra} ${aml}
    done
done
