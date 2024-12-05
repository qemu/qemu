PANDA_DIR_REL="$(dirname $0)"
TARGETS="x86_64-softmmu,x86_64-linux-user,i386-softmmu,i386-linux-user,arm-softmmu,arm-linux-user,aarch64-softmmu,aarch64-linux-user,ppc-softmmu,ppc-linux-user,mips-softmmu,mips-linux-user,mipsel-softmmu,mipsel-linux-user,mips64-softmmu,mips64-linux-user,mips64el-softmmu,mips64el-linux-user"

"${PANDA_DIR_REL}/configure" --enable-plugins \
    --target-list=$TARGETS
ninja