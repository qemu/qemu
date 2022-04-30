

# 尝试在qemu上实现新的硬件特性

[toc]





# intel 文档整理

## 硬件软件使能：

1. 软件通过设置 setting bit 25 (UINTR) in control register CR4, 来使能uinr, 不影响操作unitr操作相关寄存器
2.  CPUID.(EAX=7,ECX=0):EDX[5]. 这个位被设置后, 硬件才支持uintr, 才允许软件访问相关msr

## 寄存器定义和作用：

### 新的硬件状态：

- UIRR: user-interrupt request register

  64位的中断向量，第i位为1，则vector i 的request 需要被服务。

- UINV:  判断是否是用户态中断

- 

## 问题：

1. CR4 对应的各个位的控制在硬件层面的作用？
2. Xsave feature?
3.  MMX/SSE operation ？

# 整理代码部分

## 整理qemu代码

### 指令翻译部分

阅读intel手册，得到各个指令的二进制编码：

| 指令名称 | 二进制形式      |      |
| -------- | --------------- | ---- |
| UIRET    | 0xf3 0f 01 ec   |      |
| TESTUI   | 0xf3 0f 01 ed   |      |
| STUI     | 0xf3 0f 01 ef   |      |
| SENDUIPI | 0xf3 0f c7 /reg |      |
| CLUI     | 0xf3 0f 01 ee   |      |

找到x86指令翻译相关的代码：

```c
//target/i386/tcg/translate.c
//经过寻找,注意到f3是前缀标志位(4580行):
 /* Collect prefixes.  */ 
    switch (b) {
    case 0xf3:
        f3flag = true;  // 改  识别前缀,跳转到 4717行
        prefixes |= PREFIX_REPZ;
        goto next_byte;
//0f又会进行分支的再次跳转
reswitch:
    switch(b) {
    case 0x0f:
        /**************************/
        /* extended op code */
        b = x86_ldub_code(env, s) | 0x100;
        goto reswitch;

        /**************************/
        /* arith & logic */
        
//最后在相应分支下设置对应的指令捕捉代码
case 0x101:
        modrm = x86_ldub_code(env, s);
        switch (modrm) {
        case 0xee: /* rdpkru */
            if(prefixes & PREFIX_REPZ){
                printf("qemu:caught 0xf30fee CLUI\n"); // 改
                f3flag = false;
                break;
            }
            if (prefixes & PREFIX_LOCK) {
                goto illegal_op;
            }
            tcg_gen_trunc_tl_i32(s->tmp2_i32, cpu_regs[R_ECX]);
            gen_helper_rdpkru(s->tmp1_i64, cpu_env, s->tmp2_i32);
            tcg_gen_extr_i64_tl(cpu_regs[R_EAX], cpu_regs[R_EDX], s->tmp1_i64);
            break;
        case 0xec:
            if (prefixes & PREFIX_REPZ){
                printf("qemu:caught 0xf30f01ec UIRET\n"); // 改
                f3flag = false;
            }
            break;
        case 0xed:
            if (prefixes & PREFIX_REPZ){
                printf("qemu:caught 0xf30f01ed TESTUI\n"); // 改
                f3flag = false;
            }
            break;
        case 0xef: /* wrpkru */
            if(prefixes & PREFIX_REPZ){
                printf("qemu:caught 0xf30f01ef STUI\n"); // 改
                f3flag = false;
                break;
            }
            if (prefixes & PREFIX_LOCK) {
                goto illegal_op;
            }
            tcg_gen_concat_tl_i64(s->tmp1_i64, cpu_regs[R_EAX],
                                  cpu_regs[R_EDX]);
            tcg_gen_trunc_tl_i32(s->tmp2_i32, cpu_regs[R_ECX]);
            gen_helper_wrpkru(cpu_env, s->tmp2_i32, s->tmp1_i64);
            break;
        }
case 0x1c7: /* cmpxchg8b */
        if(prefixes & PREFIX_REPZ){
            printf("qemu: caught 0xf30fc7 SENDUIPI\n"); // 改 Debug
            modrm = x86_ldub_code(env, s); // 此句加上以解决illegal instruction的问题
            break;
        }
```

### 指令模拟部分

#### 首先研究几个具体的函数：

```c
gen_helper_rdtsc(cpu_env);  //target/i386/tcg/translate.c  7304
void helper_rdtsc(CPUX86State *env)  //target/i386/tcg/misc_helper.c  64
```

#### 异常处理和异常处理：

```c
//target/i386/tcg/excp_handler.c

//raise_exception_ra
```



#### qemu中的访存操作:

```c
//target/i386/tcg/translate.c 3255 可能是访存指令的操作
```



#### 尝试仿照验证函数定义和调用

```c
//target/i386/tcg/translate.c 5402 添加如下函数调用
gen_helper_senduipi(cpu_env, tcg_const_i32(uipi_index));
//target/i386/helper.h 35 添加如下宏定义
DEF_HELPER_2(senduipi, void ,env ,int)
//target/i386/tcg/misc_helper.c
void helper_senduipi(CPUX86State *env ,int uipi_index){ // 改
    if(Debug)printf("qemu:helper senduipi called receive %d index\n",uipi_index);
}
```



### 新指令实现

#### stui

```
```





### cpu状态设定

```c++
//target/i386/cpu.h    CPUX86State
400行左右,msr编号定义
//target/i386/cpu.h/CPUArchState  1476 寄存器定义
```

#### 尝试寻找msr的读与写函数的位置

```c++
//target/i386/tcg/sysemu/misc_helper.c/helper_rdmsr 313
//target/i386/tcg/sysemu/misc_helper.c/helper_wrmsr 142
```

#### 增加对应的msr定义以及读写函数内容:

```c
//target/i386/cpu.h 258
#define CR4_UINTR_MASK (1U<<25)  // 软件使能控制位


//target/i386/cpu.h   404  mydefines msr 定义
#define MSR_IA32_UINTR_RR               0x985
#define MSR_IA32_UINTR_HANDLER          0x986
#define MSR_IA32_UINTR_STACKADJUST      0x987
#define MSR_IA32_UINTR_MISC             0x988
#define MSR_IA32_UINTR_PD               0x989
#define MSR_IA32_UINTR_TT               0x98a

// target/i386/cpu.h  CPUArchState 1565
uint64_t uintr_rr;
uint64_t uintr_handler;
uint64_t uintr_stackadjust;
uint64_t uintr_misc;
uint64_t uintr_pd;
uint64_t uintr_tt;
//target/i386/tcg/sysemu/misc_helper.c/helper_rdmsr 403
//改 rdmsr
    case MSR_IA32_UINTR_RR:
        if(Debug)printf("rdmsr RR \n");
        val = env->uintr_rr;
        break;
    case MSR_IA32_UINTR_HANDLER:
        printf("rdmsr handler \n");
        val = env->uintr_handler;
        break;
    case MSR_IA32_UINTR_STACKADJUST:
        printf("rdmsr stackadjust \n");
        val = env->uintr_stackadjust;
        break;
    case MSR_IA32_UINTR_MISC:
        printf("rdmsr misc \n");
        val = env->uintr_misc;
        break;
    case MSR_IA32_UINTR_PD:
        printf("rdmsr pd \n");
        val = env->uintr_pd;
        break;
    case MSR_IA32_UINTR_TT:
        printf("rdmsr tt \n");
        val = env->uintr_tt;
        break;
//target/i386/tcg/sysemu/misc_helper.c/helper_wrmsr   229
  // ？？？ 改wrmsr
    case MSR_IA32_UINTR_RR:
        printf("wrmsr RR \n");
        env->uintr_rr = val;
        break;
    case MSR_IA32_UINTR_HANDLER:
        printf("wrmsr handler \n");
        env->uintr_handler = val;
        break;
    case MSR_IA32_UINTR_STACKADJUST:
        printf("wrmsr stackadjust \n");
        env->uintr_stackadjust = val;
        break;
    case MSR_IA32_UINTR_MISC:
        printf("wrmsr misc \n");
        env->uintr_misc = val;
        break;
    case MSR_IA32_UINTR_PD:
        printf("wrmsr pd \n");
        env->uintr_pd = val;
        break;
    case MSR_IA32_UINTR_TT:
        printf("wrmsr tt \n");
        env->uintr_tt = val;
        break;
```



### 核间中断部分

```c
//hw/intc/apic.c 501
void apic_sipi(DeviceState *dev) // 插入输出debug后确实够正确输出
```



apic, 中断控制器, localapic  lapic //内核态

cs寄存器中,cpl的位标志着cpu权级0是内核态,3是用户态 iret返回,切换权级。

x2是读写msr的

核通过内存访问

hw/intc/apic.c/apic_delever_irq

s->icr[0] 发送方,写了就可以发ipi



### 内存模拟定位

#### cpu_stl_be_data_ra 是有感访问，操作系统会报错

cpu_stl_be_data也是有感访问，操作系统报错

```
//include/exec/cpu_ldst.h
```

```c
uint64_t x86_ldq_phys(CPUState *cs, hwaddr addr)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    return address_space_ldq(as, addr, attrs, NULL);
}

CPUState *cs = env_cpu(env);

//target/i386/helper.c281
pdpe_addr = ((env->cr[3] & ~0x1f) + ((addr >> 27) & 0x18)) &a20_mask;
pdpe = x86_ldq_phys(cs, pdpe_addr);
if (!(pdpe & PG_PRESENT_MASK)) return -1;
```

## addr = get_hphys(cs, addr, MMU_DATA_STORE, &prot);   mmu_translate.       target_ulong cr[5]; /* NOTE: cr1 is unused !!! */



## tcg-cpu.h X86XSaveArea

```c

//失败的方法

void helper_senduipi(CPUX86State *env ,int reg_index){ // 改
    int uipi_index = env->regs[R_EAX];
    if(Debug)printf("qemu:helper senduipi called receive  regidx:%d, uipiindex: %d\n",reg_index,uipi_index);
    uint64_t content = cpu_ldq_data_ra(env, (env->uintr_tt>>3)<<3,0);
    if(Debug)printf("data of uitt0is 0x%016lx\n",content);
}
/* 操作系统报错
qemu:helper senduipi called receive  regidx:240, uipiindex: 0
 IPI from sender thread index:0 
[   29.290347] uipi_sample[79]: segfault at ffff9315c3951000 ip 0000000000401eb4 sp 00007f9cba791d90 error 5 in uipi_sample[401000+af000]
[   29.293130] Code: 89 c7 e8 ff 18 02 00 bf 01 00 00 00 e8 75 91 01 00 8b 45 f4 89 c6 48 8d 05 81 e1 0a 00 48 89 c7 b8 00 00 00 00 e8 1c 9c 01 00 <8b> 45 f4 48 98 48 89 45 f8 48 8b 45 f8 f3 0f c7 f0 90 8b 05 08 e5
qemu:wrmsr misc 0x0000000000000000
*/




TCGv t0;
t0 = tcg_temp_local_new();
t0 = (TCGv)env->uintr_tt; // 将t0修改为地址
if(Debug){printf("debug: before t0: %llx   A0: %llx\n",(long long unsigned)t0,(long long unsigned)s->A0);}
gen_op_ld_v(s, ot, t0, s->A0);
if(Debug){printf("debug: after  t0: %llx   A0: %llx\n",(long long unsigned)t0,(long long unsigned)s->A0);}
tcg_temp_free(t0);
/*
debug: before t0: ffff901883890001   A0: bf8
qemu-system-x86_64: /home/xxy/qemu/include/tcg/tcg.h:657: temp_idx: Assertion `n >= 0 && n < tcg_ctx->nb_temps' failed.
/home/xxy/runlinux.sh: line 5: 82338 Aborted                 (core dumped) /home/xxy/qemu/build/x86_64-softmmu/qemu-system-x86_64 

debug: before t0: ed0   A0: ffff8cf9838c0001
qemu-system-x86_64: /home/xxy/qemu/include/tcg/tcg.h:657: temp_idx: Assertion `n >= 0 && n < tcg_ctx->nb_temps' failed.
/home/xxy/runlinux.sh: line 5: 83796 Aborted  

*/


void helper_senduipi(CPUX86State *env ,int reg_index){ // 改
    CPUState *cs = env_cpu(env);
    int uipi_index = env->regs[R_EAX];
    if(Debug)printf("qemu:helper senduipi called receive  regidx:%d, uipiindex: %d\n",reg_index,uipi_index);
    uint64_t content = x86_ldq_phys(cs,(env->uintr_tt>>3)<<3);
    // uint64_t content = cpu_ldq_data_ra(env, (env->uintr_tt>>3)<<3,0);
    if(Debug)printf("data of uitt0is 0x%016lx\n",content);
    
}
/*
qemu:helper senduipi called receive  regidx:240, uipiindex: 0
data of uitt0is 0x0000000000000000
*/
                 

printf("qemu: caught 0xf30fc7 SENDUIPI\n "); // 改 Debug
uint64_t content;
cpu_physical_memory_rw(env->uintr_tt,&content,8,false);
if(Debug) printf("    xxx               %lx \n", content);

/*qemu: caught 0xf30fc7 SENDUIPI
                    0 
qemu:helper senduipi called receive  regidx:240, uipiindex: 0 */

cpu_ldq_mmuidx_ra(env, addr, mem_idx, GETPC()); ??
```

```
// uint64_t content = x86_ldq_phys(cs,uitt_phyaddress + (uitte_index<<4));
// uint64_t upidaddress = x86_ldq_phys(cs, uitt_phyaddress + (uitte_index<<4) + 8);
```

```c
//正确的方法：
    // read tempUITTE from 16 bytes at UITTADDR+ (reg « 4);
    uint64_t uitt_phyaddress = get_hphys2(cs, (env->uintr_tt>>3)<<3 , MMU_DATA_LOAD, &prot);
    if(Debug) printf("qemu: uitt_phyaddress %lx \n", uitt_phyaddress);
    struct uintr_uitt_entry uitte;
    cpu_physical_memory_rw(uitt_phyaddress + (uitte_index<<4), &uitte, 16,false);
    if(Debug)printf("qemu: data of uitt valid:%d user_vec:%d \n",uitte.valid, uitte.user_vec);
    if(Debug)printf("qemu: UPID address 0x%016lx\n", uitte.target_upid_addr);
    // read tempUPID from 16 bytes at tempUITTE.UPIDADDR;// under lock
    uint64_t upid_phyaddress = get_hphys2(cs, uitte.target_upid_addr, MMU_DATA_LOAD, &prot);
    struct uintr_upid upid;
    cpu_physical_memory_rw(upid_phyaddress, &upid, 16, false);
    if(Debug)printf("qemu: content of upid:  status:0x%x    nv:0x%x    ndst:0x%x    0x%016lx\n", upid.nc.status, upid.nc.nv, upid.nc.ndst, upid.puir);
    // tempUPID.PIR[tempUITTE.UV] := 1;
    upid.puir |= 1<<uitte.user_vec;
    
    //IF tempUPID.SN = tempUPID.ON = 0
    if(upid.nc.status == 0){
    //THEN  tempUPID.ON := 1;   sendNotify := 1;
        upid.nc.status |= UPID_ON;
        
    }else{ // ELSE sendNotify := 0;

    }
    //write tempUPID to 16 bytes at tempUITTE.UPIDADDR;// release lock

```



如果不修改upid.puir, 得到如下输出

```shell
emu: caught 0xf30fc7 SENDUIPI
 qemu:helper senduipi called receive  regidx:240, uipiindex: 0
mmu_translate ret: -1
qemu: uitt_phyaddress 290d000 
qemu: data of uitt valid:1 user_vec:0 
qemu: UPID address 0xffff9e3682a11840
mmu_translate ret: -1
qemu: content of upid:  status:0x0    nv:0xec    ndst:0x100    0x0000000000000000
qemu: data write back in upid:  status:0x1    nv:0xec    ndst:0x100    0x0000000000000000
[    5.551340] uintr_unregister_sender called
[    5.552576] send: unregister sender uintrfd 3 for task=78 ret 0
qemu:wrmsr misc 0x0000000000000000
qemu:wrmsr tt 0x0000000000000000
Sending IPI from sender thread index:0 
[    5.563810] recv: Release uintrfd for r_task 77 uvec 0
[    5.567603] uintr_unregister_handler called
qemu:wrmsr misc 0x0000000000000000
qemu:wrmsr pd 0x0000000000000000
qemu:wrmsr RR 0x0
qemu:wrmsr stackadjust 0x0
qemu:wrmsr handler 0x0000000000000000
[    5.571613] recv: unregister handler task=77 flags 0 ret 0
Success
```

如果修改upid.puir, 则用户程序不会执行结束:

```
ending IPI from sender thread index:0 
qemu: caught 0xf30fc7 SENDUIPI
 qemu:helper senduipi called receive  regidx:240, uipiindex: 0
mmu_translate ret: -1
qemu: uitt_phyaddress 2a3b000 
qemu: data of uitt valid:1 user_vec:0 
qemu: UPID address 0xffff9bfac293fd00
mmu_translate ret: -1
qemu: content of upid: status:0x0    nv:0xec    ndst:0x0    0x0000000000000000
qemu: data write back in upid:  status:0x1    nv:0xec    ndst:0x0    0x0000000000000001
[    9.319021] uintr_unregister_sender called
[    9.322055] send: unregister sender uintrfd 3 for task=79 ret 0
qemu:wrmsr misc 0x0000000000000000
qemu:wrmsr tt 0x0000000000000000
```







### 一些接口

```
target/i386/hax/hax-interface  65 msr array
target/i386/hvf/x86emu.c    670  simulate_rdmsr
```





## 梳理内核uintr相关代码

### 硬件向操作系统反馈feature

```shell
#arch/x86/kernel/uintr_fd.c/SYSCALL_DEFINE2(uintr_register_handler... 126
#修改部分 uintr 相关内核代码后，可以捕捉到对应的syscall确实被调用了，编译出的kernel输出如下：
[    6.608152] uintr_register_handler called
regeister returned 233
caught 0xf30fef STUI
create fd returned -1
Receiver enabled interrupts
Sender register error
```

```c
//以上对应的用户态代码如下(只放一部分)
int ret1 = uintr_register_handler(uintr_handler, 0);
	printf("regeister returned %d\n",ret1);
	ret = uintr_create_fd(0, 0);
	printf("create fd returned %d\n",ret);
	uintr_fd = ret;
	_stui();
	printf("Receiver enabled interrupts\n");
	if (pthread_create(&pt, NULL, &sender_thread, NULL)) {
		printf("Error creating sender thread\n");
		exit(EXIT_FAILURE);
	}
	/* Do some other work */
	while (!uintr_received)usleep(1);
	pthread_join(pt, NULL);
	close(uintr_fd);
	uintr_unregister_handler(0);
	printf("Success\n");
	exit(EXIT_SUCCESS);

//内核代码如下:
SYSCALL_DEFINE2(uintr_register_handler, u64 __user *, handler, unsigned int, flags)
{	
	if (Debug) printk("uintr_register_handler called\n");
	int ret;
	if (!uintr_arch_enabled())return 233;
		// return -EOPNOTSUPP;
	if (flags)return 234;
		// return -EINVAL;
	/* TODO: Validate the handler address */
	if (!handler) return 235;
		// return -EFAULT;
	ret = do_uintr_register_handler((u64)handler);
	pr_debug("recv: register handler task=%d flags %d handler %lx ret %d\n",
		 current->pid, flags, (unsigned long)handler, ret);
	return ret;
}
```

注意到返回的结果为233, 则查看对应的`uintr_arch_enabled`函数, 得到如下:

```c
inline bool uintr_arch_enabled(void){return static_cpu_has(X86_FEATURE_UINTR);}
```

发现无法再向下扩展函数, 查询`X86_FEATURE_UINTR`这个宏, 跳转到`arch/x86/include/asm/cpufeatures.h`其中存在大量的宏定义:

```c
#define X86_FEATURE_AVX512_4FMAPS	(18*32+ 3) /* AVX-512 Multiply Accumulation Single precision */
#define X86_FEATURE_FSRM		(18*32+ 4) /* Fast Short Rep Mov */
#define X86_FEATURE_UINTR		(18*32+ 5) /* User Interrupts support */
#define X86_FEATURE_AVX512_VP2INTERSECT (18*32+ 8) /* AVX-512 Intersect for D/Q */
#define X86_FEATURE_SRBDS_CTRL		(18*32+ 9) /* "" SRBDS mitigation MSR available */
```

经过和学长讨论, 尝试寻找和qemu之间的对应关系, 在qemu中尝试寻找`CPUID`相关的文件, 定位到`target/i386/cpu.h 600行左右`,有大量的定义。尝试寻找其中一个的对应关系`FPU`

```c
//qemu/target/i386/cpu.h  682
#define CPUID_EXT2_FPU     (1U << 0)
//linux/arch/x86/include/asm/cpufeatures.h  29
#define X86_FEATURE_FPU			( 0*32+ 0) /* Onboard FPU */
```

同时注意到, 这些feature有32为一组的分组特性, 尝试寻找uintr的对应关系, 并进行添加;

```c
//linux/arch/x86/include/asm/cpufeatures.h  380
#define X86_FEATURE_FSRM		(18*32+ 4) /* Fast Short Rep Mov */
#define X86_FEATURE_UINTR		(18*32+ 5) /* User Interrupts support */
//qemu/target/i386/cpu.h    860
/* Fast Short Rep Mov */
#define CPUID_7_0_EDX_FSRM              (1U << 4)
/* ？？？改cpuid uintr */
#define CPUID_7_0_EDX_UINTR              (1U << 5)
```

考虑到单纯定义未必会在qemu中体现, 可能需要有使用这个宏的地方, 开始继续寻找代码, 得到如下结果。

```c
//qemu/target/i386/cpu.c  4196
.features[FEAT_7_0_EDX] = CPUID_7_0_EDX_FSRM,
// 修改为
.features[FEAT_7_0_EDX] = CPUID_7_0_EDX_FSRM | CPUID_7_0_EDX_UINTR,
```

修改后编译qemu并执行，得到如下结果, 并无变化：

```shell
[  797.227827] uintr_register_handler called
regeister returned 233
create fd returned -1
Receiver enabled interrupts
Sender register error
```

为此在内核中测试FSRM是否是输出enable状态:

```c
// uintr内核代码	
if (!static_cpu_has(X86_FEATURE_FSRM)) {printk("FSRM not enabled\n");
}else{printk("FSRM enabled\n");}
if (!uintr_arch_enabled())return 233;
```

```shell
[    8.086769] uintr_register_handler called
[    8.087385] FSRM not enabled
regeister returned 233
caught 0xf30fef STUI
create fd returned -1
Receiver enabled interrupts
Sender register error
```

这说明qemu并没有未内核返回正确的信息位置，继续查看qemu代码，仔细阅读代码可知, 在cpu中列举了非常多的cpu芯片版本, 例如`EPYC-Milan`,`qemu64`,`phenom`等, 此处需要知道qemu具体编译出的cpu版本, 在对应的版本下添加cpu特性。

```shell
~/runlinux.sh > startlog.txt
grep CPU  startlog.txt # 得到以下关键信息
[    0.370356] smpboot: CPU0: AMD QEMU Virtual CPU version 2.5+ (family: 0xf, model: 0x6b, stepping: 0x1)
```

在`cpu.c`中搜索`QEMU Virtual CPU version`,定位到如下:

```c
.model_id = "QEMU Virtual CPU version " QEMU_HW_VERSION, //1802 对应 qemu64
.model_id = "QEMU Virtual CPU version " QEMU_HW_VERSION, //1931 对应 qemu32
.model_id = "QEMU Virtual CPU version " QEMU_HW_VERSION, //2057 对应 athlon
```

在`qemu64`中添加后，验证未成功， 在`athlon`中添也未验证成功。

```c
.features[FEAT_7_0_EDX] = CPUID_7_0_EDX_UINTR,
```

继续查看源代码

```

```

查看linux启动时的日志输出，主要关注linux启动前的部分, 可见是TCG出现了问题

```shell
xxy@7af409e42583:~/qemu/build$ run > ~/startlog.txt  #写入log,防止大面积刷屏
qemu-system-x86_64: warning: TCG doesn't support requested feature: CPUID.07H:EDX.fsrm [bit 4]
qemu-system-x86_64: warning: TCG doesn't support requested feature: CPUID.07H:EDX [bit 5]
qemu-system-x86_64: warning: TCG doesn't support requested feature: CPUID.07H:EDX.fsrm [bit 4]
qemu-system-x86_64: warning: TCG doesn't support requested feature: CPUID.07H:EDX [bit 5]
```

注释掉`cpu.c`中的下面这一句,再次查看输出

```c
.model_id = "QEMU Virtual CPU version " QEMU_HW_VERSION, //1802 对应 qemu64
//下面是shell命令
xxy@7af409e42583:~/qemu/build$ run > ~/startlog.txt  #写入log,防止大面积刷屏
```

没有相关输出, 说明cpu确实按照`qemu64`进行初始化, 接下来尝试寻找TCG支持相关的代码, 以及warning 发出的地方, 搜索`doesn't support requested feature`, 找到如下:

```c
//qemu/target/i386/cpu.c  6301
static void x86_cpu_filter_features(X86CPU *cpu, bool verbose)
if (verbose) {
        prefix = accel_uses_host_cpuid()
                 ? "host doesn't support requested feature"
                 : "TCG doesn't support requested feature？"; // 改
}
// 4347
static void mark_unavailable_features();
// 6153
void x86_cpu_expand_features(X86CPU *cpu, Error **errp);
```
通过插入输出,编译后再次执行得到如下结果:
```shell
qemu-system-x86_64: warning: expand featrue called
qemu-system-x86_64: warning: x86 cpu filter feature called
qemu-system-x86_64: warning: TCG doesn't support requested feature？: CPUID.07H:EDX.fsrm [bit 4]
qemu-system-x86_64: warning: TCG doesn't support requested feature？: CPUID.07H:EDX [bit 5]
qemu-system-x86_64: warning: expand featrue called
qemu-system-x86_64: warning: x86 cpu filter feature called
qemu-system-x86_64: warning: TCG doesn't support requested feature？: CPUID.07H:EDX.fsrm [bit 4]
qemu-system-x86_64: warning: TCG doesn't support requested feature？: CPUID.07H:EDX [bit 5]
```

再次寻找源代码，注意到如下函数：

```c
//qemu/target/i386/cpu.c  4989
uint64_t x86_cpu_get_supported_feature_word(FeatureWord w,
                                            bool migratable_only) // ？？？得到支持的featureword信息
FeatureWordInfo *wi = &feature_word_info[w];
uint64_t r = 0;
  if (kvm_enabled()) {
        if(Debug)warn_report("kvm enabled");
        switch (wi->type) {
        case CPUID_FEATURE_WORD:
///.....
        case MSR_FEATURE_WORD:
//.....
        }
    } else if (hvf_enabled()) {
        if(Debug)warn_report("hvf enabled");
//.......
    } else if (tcg_enabled()) {
  			if(Debug)warn_report("tcg enabled");
        r = wi->tcg_features;
    } else {
        return ~0;
    }
//进行输出定位后, 确认为r = wi->tcg_features; 这一行起主要作用,继续深入, 定位到一个巨大结构体中的位置
//qemu/target/i386/cpu.c  855
[FEAT_7_0_EDX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, "avx512-4vnniw", "avx512-4fmaps",
            "fsrm", "uintr", NULL, NULL,  // 改，加入feature info 信息
            "avx512-vp2intersect", NULL, "md-clear", NULL,
            NULL, NULL, "serialize", NULL,
            "tsx-ldtrk", NULL, NULL /* pconfig */, NULL,
            NULL, NULL, "amx-bf16", "avx512-fp16",
            "amx-tile", "amx-int8", "spec-ctrl", "stibp",
            NULL, "arch-capabilities", "core-capability", "ssbd",
        },
        .cpuid = {
            .eax = 7,
            .needs_ecx = true, .ecx = 0,
            .reg = R_EDX,
        },
        .tcg_features = TCG_7_0_EDX_FEATURES,  //这个宏,很关键
    },
// 进入展开的宏
//qemu/target/i386/cpu.c  666
#define TCG_7_0_EDX_FEATURES 0
// 修改后:
#define TCG_7_0_EDX_FEATURES (CPUID_7_0_EDX_UINTR)
```

#### 玄学问题:

以上操作完成后, linux依旧返回233, 但是在对linux做了以下理论上不会影响逻辑的修改后重新编译, 输出发生变化。

```c
//arch/x86/kernel/cpu/common.c  327
static __always_inline void setup_uintr(struct cpuinfo_x86 *c) // 初始化用户态中断,改？？？
{
	/* check the boot processor, plus compile options for UINTR. */
	if (!cpu_feature_enabled(X86_FEATURE_UINTR))
		printk("at setup uintr cpu featrue not enabled\n");
		goto disable_uintr;

	/* checks the current processor's cpuid bits: */
	if (!cpu_has(c, X86_FEATURE_UINTR))
		printk("at setup uintr cpu has not enabled\n");
		goto disable_uintr;

	/* Confirm XSAVE support for UINTR is present. */
	if (!cpu_has_xfeatures(XFEATURE_MASK_UINTR, NULL)) {
		printk("at setup uintr XSAVE not enabled\n");
		pr_info_once("x86: User Interrupts (UINTR) not enabled. XSAVE support for UINTR is missing.\n");
		goto clear_uintr_cap;
	}
```

```shell
# linux 内执行uipi_sample,输出如下:
/ # uipi_sample 
[    7.037078] uintr_register_handler called
[    7.038450] FSRM not enabled
wrmsr handler 
wrmsr pd 
wrmsr stackadjust 
rdmsr misc 
wrmsr misc 
# ..... rdmsr misc 若干
rdmsr misc 
regeister returned 0
rdmsr misc 
# ..... rdmsr misc 若干
create fd returned 3
rdmsr misc 
rdmsr misc 
rdmsr misc 
caught 0xf30fef STUI
rdmsr misc 
rdmsr misc 
Receiver enabled interrupts
rdmsr misc 
# ..... rdmsr misc 若干
wrmsr tt 
rdmsr misc 
rdmsr misc 
wrmsr misc 
rdmsr misc 
# ..... rdmsr misc 若干 
Sending IPI from sender thread
rdmsr misc 
# ..... rdmsr misc 若干 
[    7.085385] traps: uipi_sample[78] trap invalid opcode ip:401eb7 sp:7f44b1a09d90 error:0 in uipirdmsr misc 
_sample[401000+af000]
rdmsr misc 
# ..... rdmsr misc 若干
wrmsr misc 
wrmsr tt 
wrmsr pd 
wrmsr RR 
wrmsr stackadjust 
wrmsr handler 
[    7.112267] uipi_sample (77) used greatest stack depth: 14456 bytes left
rdmsr misc 
wrmsr misc 
wrmsr tt 
Illegal instruction
```

至此内核可以读取相关msr, 所以将内核代码修改的部分进行回调, 完善指令捕捉，完善信息输出，最后得到如下输入出;

##### 值得思考

第一次执行：

```shell
/ # uipi_sample 
[    6.746036] uintr_register_handler called
qemu:wrmsr handler 0x0000000000401de5
qemu:wrmsr pd 0xffffa2ca438bc940
qemu:wrmsr stackadjust 0x0000000000000080
qemu:rdmsr misc 0x0000000000000000
qemu:wrmsr misc 0x000000ec00000000
[    6.747922] recv: register handler task=78 flags 0 handler 401de5 ret 0
qemu:rdmsr misc 0x000000ec00000000
#....
regeister returned 0qemu:rdmsr misc 0x000000ec00000000

qemu:rdmsr misc 0x000000ec00000000
#....
[    6.763195] uintr_create_fd called
[    6.765280] recv: Alloc vector success uintrfd 3 uvec 0 for task=78
qemu:rdmsr misc 0x000000ec00000000
qemu:rdmsr misc 0x000000ec00000000
create fd returned 3qemu:rdmsr misc 0x000000ec00000000

qemu:rdmsr misc 0x000000ec00000000
qemu:rdmsr misc 0x000000ec00000000
qemu:rdmsr misc 0x000000ec00000000
qemu:caught 0xf30f01ef STUI
qemu:rdmsr misc 0x000000ec00000000
qemu:rdmsr misc 0x000000ec00000000
Receiver enabled interruptsqemu:rdmsr misc 0x000000ec00000000

qemu:rdmsr misc 0x000000ec00000000
#.....
[    6.789114] uintr_register_sender called
qemu:wrmsr tt 0xffffa2ca418b0001
qemu:rdmsr misc 0x0000000000000000
qemu:wrmsr misc 0x0000000000000100
qemu:rdmsr misc 0x000000ec00000000
qemu:rdmsr misc 0x000000ec00000000
[    6.792473] send: register sender task=79 flags 0 ret(uipi_id)=0
qemu:rdmsr misc 0x000000ec00000000
qemu:rdmsr misc 0x000000ec00000000
qemu:rdmsr misc 0x000000ec00000000
Sending IPI from sender thread
qemu:rdmsr misc 0x000000ec00000000
qemu: caught 0xf30fc7 SENDUIPI
qemu:rdmsr misc 0x000000ec00000000
#.....
[    6.798139] traps: uipi_sample[79] trap invalid opcode ip:401eba sp:7ff7c490ed90 error:0 in uipi_sample[401000+af000]
qemu:rdmsr misc 0x000000ec00000000
#......
qemu:wrmsr misc 0x0000000000000000
qemu:wrmsr tt 0x0000000000000000
qemu:wrmsr pd 0x0000000000000000
qemu:wrmsr RR 0x0000000000000000
qemu:wrmsr stackadjust 0x0000000000000000
qemu:wrmsr handler 0x0000000000000000
[    6.813875] recv: Release uintrfd for r_task 78 uvec 0
qemu:rdmsr misc 0x0000000000000100
qemu:wrmsr misc 0x0000000000000000
qemu:wrmsr tt 0x0000000000000000
```

第二次执行：

```shell
/ # uipi_sample 
[   38.183970] uintr_register_handler called
qemu:wrmsr handler 0x0000000000401de5
qemu:wrmsr pd 0xffffa2ca438bc9c0
qemu:wrmsr stackadjust 0x0000000000000080
qemu:rdmsr misc 0x0000000000000000
qemu:wrmsr misc 0x000000ec00000000
[   38.184359] recv: register handler task=80 flags 0 handler 401de5 ret 0
qemu:rdmsr misc 0x000000ec00000000
#.....
regeister returned 0qemu:rdmsr misc 0x000000ec00000000

qemu:rdmsr misc 0x000000ec00000000
qemu:rdmsr misc 0x000000ec00000000
qemu:rdmsr misc 0x000000ec00000000
[   38.186027] uintr_create_fd called
[   38.186255] recv: Alloc vector success uintrfd 3 uvec 0 for task=80
qemu:rdmsr misc 0x000000ec00000000
create fd returned 3qemu:rdmsr misc 0x000000ec00000000

qemu:rdmsr misc 0x000000ec00000000
qemu:rdmsr misc 0x000000ec00000000
Receiver enabled interruptsqemu:rdmsr misc 0x000000ec00000000

qemu:rdmsr misc 0x000000ec00000000
#.....
[   38.190652] uintr_register_sender called
qemu:rdmsr misc 0x000000ec00000000
qemu:wrmsr tt 0xffffa2ca418b0001
qemu:rdmsr misc 0x0000000000000000
qemu:wrmsr misc 0x0000000000000100
[   38.191610] send: registeqemu:rdmsr misc 0x000000ec00000000
r sender task=81 flags 0 ret(uipi_id)=0
qemu:rdmsr misc 0x000000ec00000000
Sending IPI from sender thread
qemu:rdmsr misc 0x000000ec00000000
qemu:rdmsr misc 0x000000ec00000000
qemu:rdmsr misc 0x000000ec00000000
[   38.193956] traps: uipi_sample[81] trap invalid opcode ip:401eba sp:7f845374fd90 error:0 in uipi_sample[401000+af000]
qemu:rdmsr misc 0x000000ec00000000
qemu:wrmsr misc 0x0000000000000000
qemu:wrmsr tt 0x0000000000000000
qemu:wrmsr pd 0x0000000000000000
qemu:wrmsr RR 0x0000000000000000
qemu:wrmsr stackadjust 0x0000000000000000
qemu:wrmsr handler 0x0000000000000000
[   38.202607] recv: Release uintrfd for r_task 80 uvec 0
qemu:rdmsr misc 0x0000000000000100
qemu:wrmsr misc 0x0000000000000000
qemu:wrmsr tt 0x0000000000000000
Illegal instruction
```

注意到以上两者的区别，这个很奇怪。

注意到，在捕捉SENDUIPI时，没有将指令的最后一位，即最后一位寄存器表示的字节入读，导致后续出现非法指令报错，修改捕捉SENDUIPI的的代码如下。

```
```

修改`uipi_sample.c`,使得最后不再死循环等待，随后的运行代码如下：

```shell
/ # uipi_sample 
[    9.059653] uintr_register_handler called
qemu:wrmsr handler 0x0000000000401de5
qemu:wrmsr pd 0xffff9fa5039233c0
qemu:wrmsr stackadjust 0x0000000000000080
qemu:rdmsr misc 0x0000000000000000
qemu:wrmsr misc 0x000000ec00000000
[    9.062016] recv: register handler task=78 flags 0 handler 401de5 ret 0
qemu:rdmsr misc 0x000000ec00000000
#......
regeister returned 0qemu:rdmsr misc 0x000000ec00000000
qemu:rdmsr misc 0x000000ec00000000
#......
[    9.075342] uintr_create_fd called
[    9.077466] recv: Alloc vector success uintrfd 3 uvec 0 for task=78
qemu:rdmsr misc 0x000000ec00000000
create fd returned 3qemu:rdmsr misc 0x000000ec00000000
qemu:rdmsr misc 0x000000ec00000000
#...
qemu:caught 0xf30f01ef STUI
qemu:rdmsr misc 0x000000ec00000000
Receiver enabled interruptsqemu:rdmsr misc 0x000000ec00000000
qemu:rdmsr misc 0x000000ec00000000

qemu:rdmsr misc 0x000000ec00000000
#....
[    9.097662]qemu:rdmsr misc 0x000000ec00000000
 uintr_register_sender called
qemu:wrmsr tt 0xffff9fa5018cf001
qemu:rdmsr misc 0x0000000000000000
qemu:wrmsr misc 0x0000000000000100
[    9.104338] send: register sender task=79 flags 0 ret(uipi_id)=0
Sending IPI from sender thread index:0 
qemu: caught 0xf30fc7 SENDUIPI
 qemu:helper senduipi called receive  regidx:240, uipiindex: 0
[    9.106657] uintr_unregister_sender called
[    9.108545] send: unregister sender uintrfd 3 for task=79 ret 0
qemu:rdmsr misc 0x0000000000000100
qemu:wrmsr misc 0x0000000000000000
qemu:wrmsr tt 0x0000000000000000
qemu:rdmsr misc 0x000000ec00000000
#......
[    9.127159] recv: Release uintrfd for r_task 78 uvec 0
qemu:rdmsr misc 0x000000ec00000000
[    9.129770] uintr_unregister_handler called
qemu:rdmsr misc 0x000000ec00000000
qemu:wrmsr misc 0x0000000000000000
qemu:wrmsr pd 0x0000000000000000
qemu:wrmsr RR 0x0000000000000000
qemu:wrmsr stackadjust 0x0000000000000000
qemu:wrmsr handler 0x0000000000000000
[    9.132581] recv: unregister handler task=78 flags 0 ret 0
Success
```



# cpuid cpu.h 5226

## qemu -d help 查看log





# 编译等技巧

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

#### 注意到qemu定义msr并无引用后：

查看linux-uintr-kernel的每一次commit，发现如下说明：

```txt
x86/cpu: Enumerate User Interrupts support
User Interrupts support including user IPIs is enumerated through cpuid.
The 'uintr' flag in /proc/cpuinfo can be used to identify it. The
recommended mechanism for user applications to detect support is calling
the uintr related syscalls.

Use CONFIG_X86_USER_INTERRUPTS to compile with User Interrupts support.
The feature can be disabled at boot time using the 'nouintr' kernel
parameter.  只有定义了相关的宏，相关特性才会被启用

SENDUIPI is a special ring-3 instruction that makes a supervisor mode
memory access to the UPID and UITT memory. Currently, KPTI needs to be
off for User IPIs to work.  Processors that support user interrupts are
not affected by Meltdown so the auto mode of KPTI will default to off.

Users who want to force enable KPTI will need to wait for a later
version of this patch series that is compatible with KPTI. We need to
allocate the UPID and UITT structures from a special memory region that
has supervisor access but it is mapped into userspace. The plan is to
implement a mechanism similar to LDT.
```

重新编译linux kernel，在`build/.confg`中修改`CONFIG_X86_USER_INTERRUPTS=y`, 随后重新编译, 貌似还不行。仔细再menu中寻找相关参数，最后找到==在`Processor type and features  ---> `==设置`User Interrupts (UINTR) `

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
tar -jxvf binutils-2.38.tar.bz2
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

遇到缺少的包, 安装即可：

```shell
ERROR: Cannot find Ninjaw
```

## ref

1. https://hackmd.io/@octobersky/qemu-run-ubuntu
2. http://blog.vmsplice.net/2011/03/qemu-internals-big-picture-overview.html
3. https://vmsplice.net/~stefan/qemu-code-overview.pdf
4. https://blog.csdn.net/fantasy_wxe/article/details/108418822 编译linux
5. https://airbus-seclab.github.io/qemu_blog/tcg_p1.html qemu tcg
6. https://blog.csdn.net/iteye_4515/article/details/82159880?spm=1001.2101.3001.6661.1&utm_medium=distribute.pc_relevant_t0.none-task-blog-2%7Edefault%7ECTRLIST%7ERate-1.pc_relevant_default&depth_1-utm_source=distribute.pc_relevant_t0.none-task-blog-2%7Edefault%7ECTRLIST%7ERate-1.pc_relevant_default&utm_relevant_index=1 qemu 源码分析
7. https://www.qemu.org/docs/master/devel/loads-stores.html#helper-ld-st-mmu mmu
8. https://github.com/airbus-seclab/qemu_blog qemu blog
9. https://github.com/qemu/qemu/blob/v4.2.0/include/exec/memory.h memory.h
10. https://github.com/qemu/qemu/blob/v4.2.0/docs/devel/memory.rst memory api
11. https://github.com/qemu/qemu/blob/v4.2.0/docs/devel/loads-stores.rst ldst api
12. 


