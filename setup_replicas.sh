REPLICA_DIR="/home/jotham/qemu-cxl-shm"
REPLICA_SIZE="1024M"
NUM_REPLICAS=3

mkdir -p "$REPLICA_DIR"

echo "Removing existing replica files..."
for i in $(seq 0 $((NUM_REPLICAS - 1))); do
    replica_file="${REPLICA_DIR}/replica${i}.img"
    if [ -f "$replica_file" ]; then
        echo "Removing ${replica_file}..."
        rm "$replica_file"
    fi
done

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