

# 尝试在qemu上实现新的硬件特性

[toc]

## 整理qemu代码

### 指令翻译部分

阅读intel手册，得到







### 指令模拟部分

首先研究几个具体的函数：

```c
gen_helper_rdtsc(cpu_env);  //target/i386/tcg/translate.c  7304
```





### 核间中断部分

apic, 中断控制器, localapic  lapic //内核态

cs寄存器中,cpl的位标志着cpu权级0是内核态,3是用户态 iret返回,切换权级。

x2是读写msr的

核通过内存访问

hw/intc/apic.c/apic_delever_irq

s->icr[0] 发送方,写了就可以发ipi



### cpu状态设定

```
target/i386/cpu.h    CPUX86State
400行左右,msr编号定义
```



### 内存模拟定位



### 一些接口

```
target/i386/hax/hax-interface  65 msr array
target/i386/hvf/x86emu.c    670  simulate_rdmsr
```





# qemu -d help 查看log









## 编译intel实现的linux内核

```shell
cd uintr-linux-kernel/
make O=build x86_64_defconfig
make O=build menuconfig
```

在这里需要有包依赖, 执行

```shell
sudo apt-get install ncurses-dev
```

在选择构建参数时:

在`General setup`目录下, 选择`Initial RAM filesystem and RAM disk (initramfs/initrd) support`。

在`Device Drivers`目录中`Block device`下, 选择`RAM block device support`, 并设置` Default RAM disk size (kbytes)`为65536KB。

随后开始构建:

```shell
make O=build bzImage -j
```

出现缺少头文件报错: ` fatal error: gelf.h: No such file or directory`, `fatal error: openssl/bio.h: No such file or directory`,进行安装:

```shell
sudo apt install libssl-dev
sudo apt-get install libelf-dev
```



#### 创建文件系统

下载`busybox`, 执行以下

```shell
mkdir build
make O=build menuconfig
# 在  settings  Build Options 中选择                                                                                                                       # [*] Build static binary (no shared libs)
cd build
make -j4
make install
```

```shell
  ./_install//usr/sbin/ubirmvol -> ../../bin/busybox
  ./_install//usr/sbin/ubirsvol -> ../../bin/busybox
  ./_install//usr/sbin/ubiupdatevol -> ../../bin/busybox
  ./_install//usr/sbin/udhcpd -> ../../bin/busybox


--------------------------------------------------
You will probably need to make your busybox binary
setuid root to ensure all configured applets will
work properly.
-------------------------------------------------- # 记录以下log
```

```shell
mkdir -pv initramfs/x86_64_busybox
cd initramfs/x86_64_busybox/
mkdir -pv {bin,sbin,etc,proc,sys,usr/{bin,sbin}}
cp -av ../../busybox-1.32.0/build/_install/* .
```

在`x86_64_busybox`目录下, 执行打包当前文件系统:

```shell
find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../initramfs-busybox-x86_64.cpio.gz
```

启动linux的shell脚本如下:

```shell
qemu-system-x86_64  -smp 2 \
-m 1024M   -nographic  \
-kernel ./uintr-linux-kernel/build/arch/x86_64/boot/bzImage \
-initrd ./initramfs/initramfs-busybox-x86_64.cpio.gz \
-append "root=/dev/ram0 rw rootfstype=ext4 console=ttyS0 init=/linuxrc"
```





## 编译测试程序

#### 编译简单测试程序:

```shell
gcc -static compute.c -o compute
chmod a+x compute
```

#### 编译设计uintr的测试程序:

直接编译：

```shell
gcc -Wall -static -muintr -mgeneral-regs-only -minline-all-stringops uipi_sample.c -lpthread
gcc: error: unrecognized command line option ‘-muintr’
```

用`gcc-11`编译:

```shell
# 安装gcc-11
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt install build-essential manpages-dev software-properties-common
sudo apt update && sudo apt install gcc-11 

gcc-11 -Wall -static -muintr -mgeneral-regs-only -minline-all-stringops uipi_sample.c -lpthread -o /home/xcd/uintr_sample/uipi_sample
/tmp/ccmkHYQa.s: Assembler messages:
/tmp/ccmkHYQa.s:74: Error: no such instruction: `uiret'
/tmp/ccmkHYQa.s:120: Error: no such instruction: `senduipi %rax'
/tmp/ccmkHYQa.s:196: Error: no such instruction: `stui'
```

安装最新的汇编器:

首先下载对应版本的安装包`binutils-2.38.tar.bz2 `

```shell
tar -jxvf 
binutils-2.38.tar.bz2
cd binutils-2.38
./configure
make -j
sudo make install
```

随后编译，可以成功。

```shell
xxy@7af409e42583:~/uintr-linux-kernel/tools/uintr/sample$ make
gcc-11 -Wall -static -muintr -mgeneral-regs-only -minline-all-stringops uipi_sample.c -lpthread -o /home/xxy/uintr-linux-kernel/tools/uintr/sample/uipi_sample
xxy@7af409e42583:~/uintr-linux-kernel/tools/uintr/sample$ ls
Makefile  README  uipi.s  uipi_sample  uipi_sample.c
```

执行出现错误：

```shell
./uipi_sample 
Interrupt handler register error
```

gcc -S 得到汇编代码，主要的新增代码如下

```assembly
	uiret   # 这里是之前出错的指令
	senduipi	%rax  # 这里是出错的汇编指令
	stui   # 这里也是之前出错的指令
```



## 编译qemu

```shell
 cd qemu
 mkdir build
 ../configure  --enable-debug  --target-list=x86_64-softmmu
 make -j
```

遇到缺少的包：

```shell
ERROR: Cannot find Ninja
```

## ref

1. https://hackmd.io/@octobersky/qemu-run-ubuntu
2. http://blog.vmsplice.net/2011/03/qemu-internals-big-picture-overview.html
3. https://vmsplice.net/~stefan/qemu-code-overview.pdf
