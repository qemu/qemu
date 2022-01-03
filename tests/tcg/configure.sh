#! /bin/sh

if test -z "$source_path"; then
  echo Do not invoke this script directly.  It is called
  echo automatically by configure.
  exit 1
fi

write_c_skeleton() {
    cat > $TMPC <<EOF
int main(void) { return 0; }
EOF
}

has() {
  command -v "$1" >/dev/null 2>&1
}

do_compiler() {
  # Run the compiler, capturing its output to the log. First argument
  # is compiler binary to execute.
  local compiler="$1"
  shift
  if test -n "$BASH_VERSION"; then eval '
      echo >>config.log "
funcs: ${FUNCNAME[*]}
lines: ${BASH_LINENO[*]}"
  '; fi
  echo $compiler "$@" >> config.log
  $compiler "$@" >> config.log 2>&1 || return $?
}


TMPDIR1="config-temp"
TMPC="${TMPDIR1}/qemu-conf.c"
TMPE="${TMPDIR1}/qemu-conf.exe"

container="no"
if test $use_containers = "yes"; then
    if has "docker" || has "podman"; then
        container=$($python $source_path/tests/docker/docker.py probe)
    fi
fi

# cross compilers defaults, can be overridden with --cross-cc-ARCH
: ${cross_cc_aarch64="aarch64-linux-gnu-gcc"}
: ${cross_cc_aarch64_be="$cross_cc_aarch64"}
: ${cross_cc_cflags_aarch64_be="-mbig-endian"}
: ${cross_cc_alpha="alpha-linux-gnu-gcc"}
: ${cross_cc_arm="arm-linux-gnueabihf-gcc"}
: ${cross_cc_cflags_armeb="-mbig-endian"}
: ${cross_cc_hexagon="hexagon-unknown-linux-musl-clang"}
: ${cross_cc_cflags_hexagon="-mv67 -O2 -static"}
: ${cross_cc_hppa="hppa-linux-gnu-gcc"}
: ${cross_cc_i386="i686-linux-gnu-gcc"}
: ${cross_cc_cflags_i386="-m32"}
: ${cross_cc_m68k="m68k-linux-gnu-gcc"}
: ${cross_cc_microblaze="microblaze-linux-musl-gcc"}
: ${cross_cc_mips64el="mips64el-linux-gnuabi64-gcc"}
: ${cross_cc_mips64="mips64-linux-gnuabi64-gcc"}
: ${cross_cc_mipsel="mipsel-linux-gnu-gcc"}
: ${cross_cc_mips="mips-linux-gnu-gcc"}
: ${cross_cc_nios2="nios2-linux-gnu-gcc"}
: ${cross_cc_ppc="powerpc-linux-gnu-gcc"}
: ${cross_cc_cflags_ppc="-m32"}
: ${cross_cc_ppc64="powerpc64-linux-gnu-gcc"}
: ${cross_cc_cflags_ppc64="-m64 -mbig"}
: ${cross_cc_ppc64le="$cross_cc_ppc64"}
: ${cross_cc_cflags_ppc64le="-m64 -mlittle"}
: ${cross_cc_riscv64="riscv64-linux-gnu-gcc"}
: ${cross_cc_s390x="s390x-linux-gnu-gcc"}
: ${cross_cc_sh4="sh4-linux-gnu-gcc"}
: ${cross_cc_cflags_sparc="-m32 -mv8plus -mcpu=ultrasparc"}
: ${cross_cc_sparc64="sparc64-linux-gnu-gcc"}
: ${cross_cc_cflags_sparc64="-m64 -mcpu=ultrasparc"}
: ${cross_cc_x86_64="x86_64-linux-gnu-gcc"}
: ${cross_cc_cflags_x86_64="-m64"}

# tricore is special as it doesn't have a compiler
: ${cross_as_tricore="tricore-as"}
: ${cross_ld_tricore="tricore-ld"}

for target in $target_list; do
  arch=${target%%-*}

  # reset all container fields
  container_image=
  container_hosts=
  container_cross_cc=
  container_cross_as=
  container_cross_ld=

  # suppress clang
  supress_clang=

  case $target in
    aarch64-*)
      # We don't have any bigendian build tools so we only use this for AArch64
      container_hosts="x86_64 aarch64"
      container_image=debian-arm64-test-cross
      container_cross_cc=aarch64-linux-gnu-gcc-10
      ;;
    alpha-*)
      container_hosts=x86_64
      container_image=debian-alpha-cross
      container_cross_cc=alpha-linux-gnu-gcc
      ;;
    arm-*)
      # We don't have any bigendian build tools so we only use this for ARM
      container_hosts="x86_64 aarch64"
      container_image=debian-armhf-cross
      container_cross_cc=arm-linux-gnueabihf-gcc
      ;;
    cris-*)
      container_hosts=x86_64
      container_image=fedora-cris-cross
      container_cross_cc=cris-linux-gnu-gcc
      ;;
    hexagon-*)
      container_hosts=x86_64
      container_image=debian-hexagon-cross
      container_cross_cc=hexagon-unknown-linux-musl-clang
      ;;
    hppa-*)
      container_hosts=x86_64
      container_image=debian-hppa-cross
      container_cross_cc=hppa-linux-gnu-gcc
      ;;
    i386-*)
      container_hosts=x86_64
      container_image=fedora-i386-cross
      container_cross_cc=gcc
      supress_clang=yes
      ;;
    m68k-*)
      container_hosts=x86_64
      container_image=debian-m68k-cross
      container_cross_cc=m68k-linux-gnu-gcc
      ;;
    microblaze-*)
      container_hosts=x86_64
      container_image=debian-microblaze-cross
      container_cross_cc=microblaze-linux-musl-gcc
      ;;
    mips64el-*)
      container_hosts=x86_64
      container_image=debian-mips64el-cross
      container_cross_cc=mips64el-linux-gnuabi64-gcc
      ;;
    mips64-*)
      container_hosts=x86_64
      container_image=debian-mips64-cross
      container_cross_cc=mips64-linux-gnuabi64-gcc
      ;;
    mipsel-*)
      container_hosts=x86_64
      container_image=debian-mipsel-cross
      container_cross_cc=mipsel-linux-gnu-gcc
      ;;
    mips-*)
      container_hosts=x86_64
      container_image=debian-mips-cross
      container_cross_cc=mips-linux-gnu-gcc
      ;;
    nios2-*)
      container_hosts=x86_64
      container_image=debian-nios2-cross
      container_cross_cc=nios2-linux-gnu-gcc
      ;;
    ppc-*|ppc64abi32-*)
      container_hosts=x86_64
      container_image=debian-powerpc-test-cross
      container_cross_cc=powerpc-linux-gnu-gcc-10
      ;;
    ppc64-*|ppc64le-*)
      container_hosts=x86_64
      container_image=debian-powerpc-test-cross
      container_cross_cc=${target%%-*}-linux-gnu-gcc-10
      container_cross_cc=powerpc${container_cross_cc#ppc}
      ;;
    riscv64-*)
      container_hosts=x86_64
      container_image=debian-riscv64-cross
      container_cross_cc=riscv64-linux-gnu-gcc
      ;;
    s390x-*)
      container_hosts=x86_64
      container_image=debian-s390x-cross
      container_cross_cc=s390x-linux-gnu-gcc
      ;;
    sh4-*)
      container_hosts=x86_64
      container_image=debian-sh4-cross
      container_cross_cc=sh4-linux-gnu-gcc
      ;;
    sparc64-*)
      container_hosts=x86_64
      container_image=debian-sparc64-cross
      container_cross_cc=sparc64-linux-gnu-gcc
      ;;
    tricore-softmmu)
      container_hosts=x86_64
      container_image=debian-tricore-cross
      container_cross_as=tricore-as
      container_cross_ld=tricore-ld
      ;;
    x86_64-*)
      container_hosts="aarch64 ppc64el x86_64"
      container_image=debian-amd64-cross
      container_cross_cc=x86_64-linux-gnu-gcc
      supress_clang=yes
      ;;
    xtensa*-softmmu)
      container_hosts=x86_64
      container_image=debian-xtensa-cross

      # default to the dc232b cpu
      container_cross_cc=/opt/2020.07/xtensa-dc232b-elf/bin/xtensa-dc232b-elf-gcc
      ;;
  esac

  config_target_mak=tests/tcg/config-$target.mak

  echo "# Automatically generated by configure - do not modify" > $config_target_mak
  echo "TARGET_NAME=$arch" >> $config_target_mak
  echo "target=$target" >> $config_target_mak
  case $target in
    *-linux-user | *-bsd-user)
      echo "CONFIG_USER_ONLY=y" >> $config_target_mak
      echo "QEMU=$PWD/qemu-$arch" >> $config_target_mak
      ;;
    *-softmmu)
      echo "CONFIG_SOFTMMU=y" >> $config_target_mak
      echo "QEMU=$PWD/qemu-system-$arch" >> $config_target_mak
      ;;
  esac

  eval "target_compiler_cflags=\${cross_cc_cflags_$arch}"
  echo "CROSS_CC_GUEST_CFLAGS=$target_compiler_cflags" >> $config_target_mak

  got_cross_cc=no

  if eval test "x\"\${cross_cc_$arch}\"" != xyes; then
      eval "target_compiler=\"\${cross_cc_$arch}\""

      if has $target_compiler; then
          if test "$supress_clang" = yes &&
                  $target_compiler --version | grep -qi "clang"; then
              got_cross_cc=no
          else
              write_c_skeleton
              if ! do_compiler "$target_compiler" $target_compiler_cflags \
                   -o $TMPE $TMPC -static ; then
                  # For host systems we might get away with building without -static
                  if do_compiler "$target_compiler" $target_compiler_cflags \
                                 -o $TMPE $TMPC ; then
                      got_cross_cc=yes
                      echo "CROSS_CC_GUEST_STATIC=y" >> $config_target_mak
                      echo "CROSS_CC_GUEST=$target_compiler" >> $config_target_mak
                  fi
              else
                  got_cross_cc=yes
                  echo "CROSS_CC_GUEST_STATIC=y" >> $config_target_mak
                  echo "CROSS_CC_GUEST=$target_compiler" >> $config_target_mak
              fi
          fi
      fi

      # Special handling for assembler only tests
      eval "target_as=\"\${cross_as_$arch}\""
      eval "target_ld=\"\${cross_ld_$arch}\""
      if has $target_as && has $target_ld; then
          case $target in
              tricore-softmmu)
                  echo "CROSS_CC_GUEST=$target_as" >> $config_target_mak
                  echo "CROSS_AS_GUEST=$target_as" >> $config_target_mak
                  echo "CROSS_LD_GUEST=$target_ld" >> $config_target_mak
                  got_cross_cc=yes
                  ;;
          esac
      fi
  fi

  if test $got_cross_cc = yes; then
      # Test for compiler features for optional tests. We only do this
      # for cross compilers because ensuring the docker containers based
      # compilers is a requirememt for adding a new test that needs a
      # compiler feature.

      case $target in
          aarch64-*)
              if do_compiler "$target_compiler" $target_compiler_cflags \
                             -march=armv8.1-a+sve -o $TMPE $TMPC; then
                  echo "CROSS_CC_HAS_SVE=y" >> $config_target_mak
              fi
              if do_compiler "$target_compiler" $target_compiler_cflags \
                             -march=armv8.3-a -o $TMPE $TMPC; then
                  echo "CROSS_CC_HAS_ARMV8_3=y" >> $config_target_mak
              fi
              if do_compiler "$target_compiler" $target_compiler_cflags \
                             -mbranch-protection=standard -o $TMPE $TMPC; then
                  echo "CROSS_CC_HAS_ARMV8_BTI=y" >> $config_target_mak
              fi
              if do_compiler "$target_compiler" $target_compiler_cflags \
                             -march=armv8.5-a+memtag -o $TMPE $TMPC; then
                  echo "CROSS_CC_HAS_ARMV8_MTE=y" >> $config_target_mak
              fi
              ;;
          ppc*)
              if do_compiler "$target_compiler" $target_compiler_cflags \
                             -mpower8-vector -o $TMPE $TMPC; then
                  echo "CROSS_CC_HAS_POWER8_VECTOR=y" >> $config_target_mak
              fi
              if do_compiler "$target_compiler" $target_compiler_cflags \
                             -mpower10 -o $TMPE $TMPC; then
                  echo "CROSS_CC_HAS_POWER10=y" >> $config_target_mak
              fi
              ;;
          i386-linux-user)
              if do_compiler "$target_compiler" $target_compiler_cflags \
                             -Werror -fno-pie -o $TMPE $TMPC; then
                  echo "CROSS_CC_HAS_I386_NOPIE=y" >> $config_target_mak
              fi
              ;;
      esac
  elif test $got_cross_cc = no && test "$container" != no && \
          test -n "$container_image"; then
      for host in $container_hosts; do
          if test "$host" = "$cpu"; then
              echo "DOCKER_IMAGE=$container_image" >> $config_target_mak
              echo "DOCKER_CROSS_CC_GUEST=$container_cross_cc" >> \
                   $config_target_mak
              if test -n "$container_cross_as"; then
                  echo "DOCKER_CROSS_AS_GUEST=$container_cross_as" >> \
                      $config_target_mak
              fi
              if test -n "$container_cross_ld"; then
                  echo "DOCKER_CROSS_LD_GUEST=$container_cross_ld" >> \
                      $config_target_mak
              fi
          fi
      done
  fi
done
