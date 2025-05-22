#!/usr/bin/env bash

sudo apt-get install cloud-image-utils qemu

# This is already in qcow2 format.
img=ubuntu-18.04-server-cloudimg-amd64.img
if [ ! -f "$img" ]; then
  wget "https://cloud-images.ubuntu.com/releases/18.04/release/${img}"

  # sparse resize: does not use any extra space, just allows the resize to happen later on.
  # https://superuser.com/questions/1022019/how-to-increase-size-of-an-ubuntu-cloud-image
  qemu-img resize "$img" +128G
fi

user_data=user-data.img
if [ ! -f "$user_data" ]; then
  # For the password.
  # https://stackoverflow.com/questions/29137679/login-credentials-of-ubuntu-cloud-server-image/53373376#53373376
  # https://serverfault.com/questions/920117/how-do-i-set-a-password-on-an-ubuntu-cloud-image/940686#940686
  # https://askubuntu.com/questions/507345/how-to-set-a-password-for-ubuntu-cloud-images-ie-not-use-ssh/1094189#1094189
  cat >user-data <<EOF
#cloud-config
password: asdfqwer
chpasswd: { expire: False }
ssh_pwauth: True
EOF
  cloud-localds "$user_data" user-data
fi

./build/qemu-system-x86_64 \
    -m 2G \
    -smp 2 \
    -drive "file=${img},format=qcow2" \
    -drive "file=${user_data},format=raw" \
    -nographic \
    -enable-kvm \
    -vga std \
    -device virtio-net-pci,netdev=net0 -netdev user,id=net0 \
    -object memory-backend-file,id=my-memdev0,size=128M,mem-path=/home/jotham/qemu-cxl-shm/replica0.img,share=on \
    -object memory-backend-file,id=my-memdev1,size=128M,mem-path=/home/jotham/qemu-cxl-shm/replica1.img,share=on \
    -object memory-backend-file,id=my-memdev2,size=128M,mem-path=/home/jotham/qemu-cxl-shm/replica2.img,share=on \
    -device cxl-switch,id=cxlsw0,mem-size=128M,memdev0=my-memdev0,memdev1=my-memdev1,memdev2=my-memdev2
