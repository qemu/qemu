#!/usr/bin/env bash

# Ensure apt-get commands are only run if necessary and with user confirmation or non-interactively
if ! command -v qemu-img &> /dev/null || ! command -v cloud-localds &> /dev/null; then
    echo "qemu-img or cloud-localds not found. Attempting to install cloud-image-utils and qemu..."
    sudo apt-get update
    sudo apt-get install -y cloud-image-utils qemu
fi

REPLICA_DIR="/home/jotham/qemu-cxl-shm"
REPLICA_SIZE="256M"
NUM_REPLICAS=3

# --- Image 1 Setup ---
img_base_name="ubuntu-22.04-server-cloudimg-amd64.img" # Base image for both
img1="ubuntu-22.04-image0.img"
user_data1="user-data0.img"

if [ ! -f "$img1" ]; then
  if [ ! -f "$img_base_name" ]; then
    echo "Downloading base cloud image: ${img_base_name}..."
    wget "https://cloud-images.ubuntu.com/releases/22.04/release/${img_base_name}"
  fi

  echo "Creating image: ${img1} from base..."
  cp "$img_base_name" "$img1"
  echo "Resizing ${img1} (sparse)..."
  qemu-img resize "$img1" +128G
fi

if [ ! -f "$user_data1" ]; then
  echo "Creating user data for ${img1}..."
  cat >user-data-config0.txt <<EOF
#cloud-config
password: asdfqwer
chpasswd: { expire: False }
ssh_pwauth: True
EOF
  cloud-localds "$user_data1" user-data-config0.txt
  rm user-data-config0.txt # Clean up temporary config file
fi

# --- Image 2 Setup ---
img2="ubuntu-22.04-image1.img"
user_data2="user-data1.img"

if [ ! -f "$img2" ]; then
  if [ ! -f "$img_base_name" ]; then
    echo "Downloading base cloud image: ${img_base_name}..."
    wget "https://cloud-images.ubuntu.com/releases/22.04/release/${img_base_name}"
  fi
  echo "Creating image: ${img2} from base..."
  cp "$img_base_name" "$img2"
  echo "Resizing ${img2} (sparse)..."
  qemu-img resize "$img2" +128G
fi

if [ ! -f "$user_data2" ]; then
  echo "Creating user data for ${img2}..."
  cat >user-data-config1.txt <<EOF
#cloud-config
password: newpassword # Changed password for the second image
chpasswd: { expire: False }
ssh_pwauth: True
EOF
  cloud-localds "$user_data2" user-data-config1.txt
  rm user-data-config1.txt # Clean up temporary config file
fi


# --- Replica Files Setup (Common for the CXL device example) ---
# Ensure REPLICA_DIR exists
mkdir -p "$REPLICA_DIR"

echo "Creating/updating replica files in ${REPLICA_DIR}..."
for i in $(seq 0 $((NUM_REPLICAS - 1))); do
  replica_file="${REPLICA_DIR}/replica${i}.img"
  if [ ! -f "$replica_file" ] || [ "$(stat -c%s "$replica_file")" != "$(numfmt --from=iec "$REPLICA_SIZE")" ]; then
    echo "Creating or resizing ${replica_file} to ${REPLICA_SIZE}..."
    truncate -s "$REPLICA_SIZE" "$replica_file"
    chmod 666 "$replica_file"
  else
    echo "${replica_file} already exists with correct size."
  fi
done

echo "Script complete. You now have images: $img1, $user_data1, $img2, $user_data2"
echo "And replica files in $REPLICA_DIR"

# Link: https://dev.to/franzwong/mount-share-folder-in-qemu-with-same-permission-as-host-2980
# Mounting the shared folder in the guest, mkdir dir first in VM
# sudo mount -t 9p -o trans=virtio,version=9p2000.L shared /mnt/shared