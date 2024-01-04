#!/bin/sh

_prefix=$1
shift
_libdir=$1
shift
_sysconfdir=$1
shift
_localstatedir=$1
shift
_libexecdir=$1
shift
pkgname=$1
shift
arch=$1
shift
nvr=$1
shift
optflags=$1
shift
have_fdt=$1
shift
have_gluster=$1
shift
have_guest_agent=$1
shift
have_numa=$1
shift
have_rbd=$1
shift
have_rdma=$1
shift
have_seccomp=$1
shift
have_spice=$1
shift
have_usbredir=$1
shift
have_tcmalloc=$1
shift

if [ "$have_rbd" == "enable" ]; then
  rbd_driver=rbd,
fi

if [ "$have_gluster" == "enable" ]; then
  gluster_driver=gluster,
fi

./configure \
    --prefix=${_prefix} \
    --libdir=${_libdir} \
    --sysconfdir=${_sysconfdir} \
    --interp-prefix=${_prefix}/qemu-%M \
    --localstatedir=${_localstatedir} \
    --libexecdir=${_libexecdir} \
    --extra-ldflags="$extraldflags -pie -Wl,-z,relro -Wl,-z,now" \
    --extra-cflags="${optflags} -fPIE -DPIE" \
    --with-pkgversion=${nvr} \
    --with-confsuffix=/${pkgname} \
    --with-coroutine=ucontext \
    --with-system-pixman \
    --disable-archipelago \
    --disable-bluez \
    --disable-brlapi \
    --disable-cap-ng \
    --enable-coroutine-pool \
    --enable-curl \
    --disable-curses \
    --disable-debug-tcg \
    --enable-docs \
    --disable-gtk \
    --enable-kvm \
    --enable-libiscsi \
    --disable-libnfs \
    --enable-libssh2 \
    --enable-libusb \
    --disable-bzip2 \
    --enable-linux-aio \
    --disable-live-block-migration \
    --enable-lzo \
    --disable-opengl \
    --enable-pie \
    --disable-qom-cast-debug \
    --disable-sdl \
    --enable-snappy \
    --disable-sparse \
    --disable-strip \
    --disable-tpm \
    --enable-trace-backend=dtrace \
    --enable-uuid \
    --disable-vde \
    --enable-vhdx \
    --disable-vhost-scsi \
    --disable-virtfs \
    --disable-vnc-jpeg \
    --disable-vte \
    --enable-vnc-png \
    --enable-vnc-sasl \
    --enable-werror \
    --disable-xen \
    --disable-xfsctl \
    --enable-gnutls \
    --disable-gcrypt \
    --enable-nettle \
    --${have_fdt}-fdt \
    --${have_gluster}-glusterfs \
    --${have_guest_agent}-guest-agent \
    --${have_numa}-numa \
    --${have_rbd}-rbd \
    --${have_rdma}-rdma \
    --${have_seccomp}-seccomp \
    --${have_spice}-spice \
    --${have_spice}-smartcard \
    --${have_usbredir}-usb-redir \
    --${have_tcmalloc}-tcmalloc \
    --audio-drv-list=pa,alsa \
    --block-drv-rw-whitelist=qcow2,raw,file,host_device,nbd,iscsi,${gluster_driver}${rbd_driver}blkdebug,luks \
    --block-drv-ro-whitelist=vmdk,vhdx,vpc,https,ssh \
    "$@"
