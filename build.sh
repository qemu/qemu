PANDA_DIR_REL="$(dirname $0)"

"${PANDA_DIR_REL}/configure" --enable-plugins
ninja
make -j`nproc` plugins