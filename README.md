# QEMU with PowerPC 476FP support

## Building

The simple steps to build QEMU for PowePC are:

```bash
    mkdir build
    cd build
    ../configure --target-list=ppc-softmmu \
        --disable-vnc --disable-sdl --disable-gnutls --disable-nettle --disable-gtk
    make
```

## Launching

MM7705 board is available with PowerPC 476FP core. To start it you can use following command:

```bash
    sudo ./qemu-system-ppc \
        -M mm7705 \
        -bios pc-bios/module_mm7705_rumboot.bin \
        -drive file=pc-bios/module_mm7705_u-boot.bin,if=mtd,format=raw \
        -monitor tcp::2345,server,nowait \
        -serial tcp::3555,server,nodelay,nowait \
        -gdb tcp::1234,server,nowait \
        -nic tap,model=greth,script=scripts/qemu-ifup,downscript=no
```



Original QEMU readme is renamed to [README_original.rst](README_original.rst)
