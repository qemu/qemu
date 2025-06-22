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
BASE_IMAGE="ubuntu-22.04-server-cloudimg-amd64.img"
BASE_IMAGE_URL="https://cloud-images.ubuntu.com/releases/22.04/release/${BASE_IMAGE}"

download_base_image() {
    local base_image="$1"
    local url="$2"
    
    if [ ! -f "$base_image" ]; then
        echo "Downloading base cloud image: ${base_image}..."
        wget "$url"
    fi
}

setup_vm_image() {
    local image_id="$1"
    local password="$2"
    local base_image="$3"
    
    local img_name="ubuntu-22.04-image${image_id}.img"
    local user_data_name="user-data${image_id}.img"
    local config_file="user-data-config${image_id}.txt"
    
    echo "=== Setting up Image ${image_id} ==="
    
    # Download base image if needed
    download_base_image "$base_image" "$BASE_IMAGE_URL"
    
    # Create VM image
    if [ ! -f "$img_name" ]; then
        echo "Creating image: ${img_name} from base..."
        cp "$base_image" "$img_name"
        echo "Resizing ${img_name} (sparse)..."
        qemu-img resize "$img_name" +128G
    else
        echo "Image ${img_name} already exists, skipping creation."
    fi
    
    # Create user data image
    if [ ! -f "$user_data_name" ]; then
        echo "Creating user data for ${img_name}..."
        cat > "$config_file" <<EOF
#cloud-config
password: ${password}
chpasswd: { expire: False }
ssh_pwauth: True
EOF
        cloud-localds "$user_data_name" "$config_file"
        rm "$config_file" # Clean up temporary config file
        echo "User data ${user_data_name} created successfully."
    else
        echo "User data ${user_data_name} already exists, skipping creation."
    fi
    
    echo "Image ${image_id} setup complete: ${img_name}, ${user_data_name}"
    echo ""
}

setup_replica_files() {
    local replica_dir="$1"
    local replica_size="$2"
    local num_replicas="$3"
    
    echo "=== Setting up Replica Files ==="
    
    mkdir -p "$replica_dir"
    
    echo "Creating/updating replica files in ${replica_dir}..."
    for i in $(seq 0 $((num_replicas - 1))); do
        local replica_file="${replica_dir}/replica${i}.img"
        local expected_size=$(numfmt --from=iec "$replica_size")
        
        if [ ! -f "$replica_file" ] || [ "$(stat -c%s "$replica_file" 2>/dev/null || echo 0)" != "$expected_size" ]; then
            echo "Creating or resizing ${replica_file} to ${replica_size}..."
            truncate -s "$replica_size" "$replica_file"
            chmod 666 "$replica_file"
        else
            echo "${replica_file} already exists with correct size."
        fi
    done
    
    echo "Replica files setup complete."
    echo ""
}

main() {
    echo "Starting VM and replica setup..."
    echo ""
    
    setup_vm_image "0" "asdfqwer" "$BASE_IMAGE"
    setup_vm_image "1" "asdfqwer" "$BASE_IMAGE"
    setup_vm_image "2" "asdfqwer" "$BASE_IMAGE"
    
    setup_replica_files "$REPLICA_DIR" "$REPLICA_SIZE" "$NUM_REPLICAS"
    
    echo "=== Setup Complete ==="
    echo "Now have VM images:"
    echo "  - ubuntu-22.04-image0.img with user-data0.img (password: asdfqwer)"
    echo "  - ubuntu-22.04-image1.img with user-data1.img (password: asdfqwer)"
    echo "  - ubuntu-22.04-image2.img with user-data2.img (password: asdfqwer)"
    echo "And replica files in $REPLICA_DIR:"
    for i in $(seq 0 $((NUM_REPLICAS - 1))); do
        echo "  - ${REPLICA_DIR}/replica${i}.img (${REPLICA_SIZE})"
    done
    echo ""
    echo "To mount shared folder in guest VM:"
    echo "  sudo mount -t 9p -o trans=virtio,version=9p2000.L shared /mnt/shared"
}

main "$@"