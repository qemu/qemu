# 工作概要



## 中断定位和控制流跳转

### 中断定位部分

通过添加输出的方式来定位中断触发的位置

```c
void do_interrupt_all(X86CPU *cpu, int intno, int is_int,
                      int error_code, target_ulong next_eip, int is_hw) // 接收方执行中断？
{
    CPUX86State *env = &cpu->env;
    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        if ((env->cr[0] & CR0_PE_MASK)) {
            static int count;

            qemu_log("%6d: v=%02x e=%04x i=%d cpl=%d IP=%04x:" TARGET_FMT_lx
                     " pc=" TARGET_FMT_lx " SP=%04x:" TARGET_FMT_lx,
                     count, intno, error_code, is_int,
                     env->hflags & HF_CPL_MASK,
                     env->segs[R_CS].selector, env->eip,
                     (int)env->segs[R_CS].base + env->eip,
                     env->segs[R_SS].selector, env->regs[R_ESP]);
            if (intno == 0x0e) {
                qemu_log(" CR2=" TARGET_FMT_lx, env->cr[2]);
            } else {
                qemu_log(" env->regs[R_EAX]=" TARGET_FMT_lx, env->regs[R_EAX]);
            }
            qemu_log("\n");
            log_cpu_state(CPU(cpu), CPU_DUMP_CCOP);
#if 0
            {
                int i;
                target_ulong ptr;

                qemu_log("       code=");
                ptr = env->segs[R_CS].base + env->eip;
                for (i = 0; i < 16; i++) {
                    qemu_log(" %02x", ldub(ptr + i));
                }
                qemu_log("\n");
            }
#endif
            count++;
        }
    }
    if (env->cr[0] & CR0_PE_MASK) { // 改， 中断具体分发，应该不涉及user only
#if !defined(CONFIG_USER_ONLY)
        if (env->hflags & HF_GUEST_MASK) {
            qemu_log("HF_GUEST_MASK even \n");
            handle_even_inj(env, intno, is_int, error_code, is_hw, 0);
        }
#endif
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            do_interrupt64(env, intno, is_int, error_code, next_eip, is_hw);
        } else
#endif
        {   
            qemu_log("interrupt protected \n");
            do_interrupt_protected(env, intno, is_int, error_code, next_eip,
                                   is_hw);
        }
    } else {
#if !defined(CONFIG_USER_ONLY)
        if (env->hflags & HF_GUEST_MASK) {
            qemu_log("HF_GUEST_MASK even inj \n");
            handle_even_inj(env, intno, is_int, error_code, is_hw, 1);
        }
#endif  
        do_interrupt_real(env, intno, is_int, error_code, next_eip);
    }

#if !defined(CONFIG_USER_ONLY)
    if (env->hflags & HF_GUEST_MASK) {
        qemu_log("HF_GUEST_MASK do real \n");
        CPUState *cs = CPU(cpu);
        uint32_t event_inj = x86_ldl_phys(cs, env->vm_vmcb +
                                      offsetof(struct vmcb,
                                               control.event_inj));

        x86_stl_phys(cs,
                 env->vm_vmcb + offsetof(struct vmcb, control.event_inj),
                 event_inj & ~SVM_EVTINJ_VALID);
    }
#endif
}
```





### 中断识别部分

```c
//target/i386/tcg/seg_helper.c
#define UINTR_UINV 0xec
static void do_interrupt64(CPUX86State *env, int intno, int is_int,
                           int error_code, target_ulong next_eip, int is_hw) // 在用户态中断中 is_hw = 1
{
    SegmentCache *dt;
    target_ulong ptr;
    int type, dpl, selector, cpl, ist;
    int has_error_code, new_stack;
    uint32_t e1, e2, e3, ss;
    target_ulong old_eip, esp, offset;

    has_error_code = 0;
    if (!is_int && !is_hw) {
        has_error_code = exception_has_error_code(intno);
    }
    if (is_int) {
        old_eip = next_eip;
    } else {
        old_eip = env->eip;
    }
    if(intno == UINTR_UINV ){
        qemu_log("recognize uintr\n");

        if(env->uintr_uif == 0){
            qemu_log("--uif not zero, return\n");
            return;
        }
        int prot;
        CPUState *cs = env_cpu(env);
        bool send = false;
        uint64_t upid_phyaddress = get_hphys2(cs, env->uintr_pd, MMU_DATA_LOAD, &prot);
        uintr_upid upid;
        cpu_physical_memory_rw(upid_phyaddress, &upid, 16, false);
        upid.nc.status &= (~1); // clear on
        if(upid.puir != 0){
            env->uintr_rr = upid.puir;
            upid.puir = 0; // clear puir
            cpu_physical_memory_rw(upid_phyaddress, &upid, 16, true); // write back
            send = true;
        }
        cpu_physical_memory_rw(upid_phyaddress, &upid, 16, true);


        uint64_t APICaddress = get_hphys2(cs, APIC_DEFAULT_ADDRESS, MMU_DATA_LOAD, &prot);
        uint64_t EOI;
        uint64_t zero = 0;
        cpu_physical_memory_rw(APICaddress + 0xb0, &EOI, 8, false);
        qemu_log("the physical address of APIC 0x%lx   the EOI content: 0x%lx\n", APICaddress,EOI);
        cpu_physical_memory_rw(APICaddress + 0xb0, &zero, 4, true);
        // apic_mem_write(cs, )
        // uint64_t EOI;       
        // cpu_physical_memory_rw(APIC_DEFAULT_ADDRESS + 0xb0, &EOI, 8, false);
        // qemu_log("\n\n the EOI content: 0x%lx\n\n",EOI);
        // cpu_physical_memory_rw(APIC_DEFAULT_ADDRESS + 0xb0, 0, 4, true);
        if(send)helper_rrnzero(env);
        return;
    }
 
  
```



### 中断控制和跳转部分

```c
static bool Debug = true;
void helper_rrnzero(CPUX86State *env){ // 改
    if(Debug)qemu_log("rrnzero called handler: 0x%lx  rr: 0x%lx\n", env->uintr_handler,env->uintr_rr);
    target_ulong temprsp = env->regs[R_ESP];
    qemu_log("qemu:origin exp 0x%lx   eip 0x%lx  eflags: 0x%lx\n",env->regs[R_ESP], env->eip, env->eflags);
    if(env->uintr_stackadjust &1){ // adjust[0] = 1
        env->regs[R_ESP] = env->uintr_stackadjust;
        qemu_log("qemu:set  statck 0x%lx\n",env->regs[R_ESP]);
    }else{
        env->regs[R_ESP] -= env->uintr_stackadjust;
        qemu_log("qemu:move statck 0x%lx\n",env->regs[R_ESP]);
    }
    env->regs[R_ESP] &= ~0xfLL; /* align stack */
    target_ulong esp = env->regs[R_ESP];
    qemu_log("qemu:after align statck 0x%lx\n",env->regs[R_ESP]);
    PUSHQ(esp, temprsp);
    // qemu_log("qemu: pushed rsp\n");
    PUSHQ(esp, env->eflags); // PUSHQ(esp, cpu_compute_eflags(env));
    // qemu_log("qemu: pushed eflags\n");
    PUSHQ(esp, env->eip);
    // qemu_log("the uirr is 0x%016lx \n", env->uintr_rr);
    PUSHQ(esp, env->uintr_rr & 0x3f); // // 64-bit push; upper 58 bits pushed as 0
    qemu_log("qemu:push finish now esp is: 0x%lx",esp);
    env->uintr_rr = 0; // clear rr
    env->regs[R_ESP] = esp;
    env->eflags &= ~(TF_MASK | RF_MASK);
    env->eip = env->uintr_handler;
    env->uintr_uif = 0;
    qemu_log("qemu: eip: 0x%lx\n",env->eip);
}

void helper_uiret(CPUX86State *env){
    if(Debug)qemu_log("helper uiret called, now eip: 0x%lx\n", env->eip);
    qemu_log("qemu: now esp is: 0x%lx\n",env->regs[R_ESP]);
    target_ulong temprip, temprfalgs, temprsp, uirrv;
    target_ulong esp = env->regs[R_ESP];
    esp += 0x60;
    POPQ(esp, uirrv);
    POPQ(esp, temprip);
    POPQ(esp, temprfalgs);
    POPQ(esp, temprsp);
    qemu_log("qemu:poped values:uirrv:0x%lx rip:0x%lx   eflags:0x%lx  rsp:0x%lx \n",uirrv,temprip, temprfalgs, temprsp);
    env->eip = temprip;
    env->regs[R_ESP] = temprsp;
    env->eflags = (env->eflags & ~0x254dd5) |(temprfalgs & 0x254dd5);
    env->uintr_uif = 1;
}
```









## XSAVE的实现

搜索xsave, 找到如下引用:

```c
//target/i386/cpu.h
#define XSTATE_FP_BIT                   0
#define XSTATE_SSE_BIT                  1
#define XSTATE_YMM_BIT                  2
#define XSTATE_BNDREGS_BIT              3
#define XSTATE_BNDCSR_BIT               4
#define XSTATE_OPMASK_BIT               5
#define XSTATE_ZMM_Hi256_BIT            6
#define XSTATE_Hi16_ZMM_BIT             7
#define XSTATE_PKRU_BIT                 9
#define XSTATE_UINTR_BIT                14
//改 XSTAVE 根据手册,添加对应的bitmap标识
#define XSTATE_XTILE_CFG_BIT            17
#define XSTATE_XTILE_DATA_BIT           18
#define XSTATE_UINTR_MASK               (1ULL << XSTATE_UINTR_BIT)
#define XSTATE_FP_MASK                  (1ULL << XSTATE_FP_BIT)
#define XSTATE_SSE_MASK                 (1ULL << XSTATE_SSE_BIT)
#define XSTATE_YMM_MASK                 (1ULL << XSTATE_YMM_BIT)
#define XSTATE_BNDREGS_MASK             (1ULL << XSTATE_BNDREGS_BIT)
#define XSTATE_BNDCSR_MASK              (1ULL << XSTATE_BNDCSR_BIT)
#define XSTATE_OPMASK_MASK              (1ULL << XSTATE_OPMASK_BIT)
#define XSTATE_ZMM_Hi256_MASK           (1ULL << XSTATE_ZMM_Hi256_BIT)
#define XSTATE_Hi16_ZMM_MASK            (1ULL << XSTATE_Hi16_ZMM_BIT)
#define XSTATE_PKRU_MASK                (1ULL << XSTATE_PKRU_BIT)
#define XSTATE_XTILE_CFG_MASK           (1ULL << XSTATE_XTILE_CFG_BIT)
#define XSTATE_XTILE_DATA_MASK          (1ULL << XSTATE_XTILE_DATA_BIT)

//target/i386/tcg/fpuhelper.c
static bool Debug = true;
static void do_xsave(CPUX86State *env, target_ulong ptr, uint64_t rfbm,
                     uint64_t inuse, uint64_t opt, uintptr_t ra)
{
    uint64_t old_bv, new_bv;
    if(Debug)printf("do xsave called\n"); // 改 xsave
    /* The OS must have enabled XSAVE.  */
    if (!(env->cr[4] & CR4_OSXSAVE_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }

    /* The operand must be 64 byte aligned.  */
    if (ptr & 63) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    /* Never save anything not enabled by XCR0.  */
    rfbm &= env->xcr0;
    opt &= rfbm;
    if (opt & XSTATE_FP_MASK) {
        do_xsave_fpu(env, ptr, ra);
    }
    if (rfbm & XSTATE_SSE_MASK) {
        /* Note that saving MXCSR is not suppressed by XSAVEOPT.  */
        do_xsave_mxcsr(env, ptr, ra);
    }
    if (opt & XSTATE_SSE_MASK) {
        do_xsave_sse(env, ptr, ra);
    }
    if (opt & XSTATE_BNDREGS_MASK) {
        do_xsave_bndregs(env, ptr + XO(bndreg_state), ra);
    }
    if (opt & XSTATE_BNDCSR_MASK) {
        do_xsave_bndcsr(env, ptr + XO(bndcsr_state), ra);
    }
    if (opt & XSTATE_PKRU_MASK) {
        do_xsave_pkru(env, ptr + XO(pkru_state), ra);
    }
    if (opt & XSTATE_UINTR_MASK) {// 改 
        do_xsave_uintr(env, ptr , ra);
    }

    /* Update the XSTATE_BV field.  */
    old_bv = cpu_ldq_data_ra(env, ptr + XO(header.xstate_bv), ra);
    new_bv = (old_bv & ~rfbm) | (inuse & rfbm);
    cpu_stq_data_ra(env, ptr + XO(header.xstate_bv), new_bv, ra);
}

/*
在这里介绍一下一个红展开
#define XO(X)  offsetof(X86XSaveArea, X)
#define offsetof(TYPE, MEMBER) __builtin_offsetof (TYPE, MEMBER)
__builtin_offsetof 的作用是什么?
这里使用的是一个利用编译器技术的小技巧，即先求得结构成员变量在结构体中的相对于结构体的首地址的偏移地址，然后根据结构体的首地址为0，从而得出该偏移地址就是该结构体变量在该结构体中的偏移，即：该结构体成员变量距离结构体首的距离。
*/

static void do_xsave_uintr(CPUX86State *env, target_ulong ptr, uintptr_t ra){ //改
    cpu_stq_data_ra(env, ptr, env->uintr_handler, ra);
    cpu_stq_data_ra(env, ptr+8, env->uintr_stackadjust, ra);
    cpu_stq_data_ra(env, ptr+16, env->uintr_misc, ra);
    cpu_stq_data_ra(env, ptr+24, env->uintr_pd, ra);
    cpu_stq_data_ra(env, ptr+32, env->uintr_rr, ra);
    cpu_stq_data_ra(env, ptr+40, env->uintr_tt, ra);
}

static void do_xrstor_uintr(CPUX86State *env, target_ulong ptr, uintptr_t ra){ //改
    env->uintr_handler = cpu_ldq_data_ra(env, ptr, ra);
    env->uintr_stackadjust = cpu_ldq_data_ra(env, ptr+8, ra);
    env->uintr_misc = cpu_ldq_data_ra(env, ptr+16, ra);
    env->uintr_pd = cpu_ldq_data_ra(env, ptr+24, ra);
    env->uintr_rr = cpu_ldq_data_ra(env, ptr+32, ra);
    env->uintr_tt = cpu_ldq_data_ra(env, ptr+40, ra);
}

static void clear_uintr_reg(CPUX86State *env){ // 改
    env->uintr_handler=0;
    env->uintr_stackadjust=0;
    env->uintr_misc=0;
    env->uintr_pd=0;
    env->uintr_rr=0;
    env->uintr_tt=0;
}

//在helper_xrstor中添加如下
	if (rfbm & XSTATE_UINTR_MASK){ // 改
        if (xstate_bv & XSTATE_UINTR_MASK) {
            do_xrstor_uintr(env, ptr + XO(uintr_state), ra);
        } else {
            clear_uintr_reg(env);
        }
  }

//target/i386/tcg/tcg-cpu.h
typedef struct X86XSaveArea {
    X86LegacyXSaveArea legacy;
    X86XSaveHeader header;

    /* Extended save areas: startoffset:0x240 */

    /* AVX State: */
    XSaveAVX avx_state;

    /* Ensure that XSaveBNDREG is properly aligned. */
    uint8_t padding[XSAVE_BNDREG_OFFSET
                    - sizeof(X86LegacyXSaveArea)
                    - sizeof(X86XSaveHeader)
                    - sizeof(XSaveAVX)];
    /* MPX State: */
    XSaveBNDREG bndreg_state;
    XSaveBNDCSR bndcsr_state;
    /* AVX-512 State: */
    XSaveOpmask opmask_state;
    XSaveZMM_Hi256 zmm_hi256_state;
    XSaveHi16_ZMM hi16_zmm_state;
    /* PKRU State: */
    XSavePKRU pkru_state;
    XSaveUINTR uintr_state; // 改
} X86XSaveArea;


//target/i386/cpu.h 添加如下区域
/* Ext. save area 14: UINTR state*/ 
typedef struct XSaveUINTR {
    uint64_t handler;
    uint64_t stack_adjust;
    struct{
        uint32_t uittsz;
        uint8_t uinv;
        uint16_t reserved;
        uint8_t uif; // bit7 is the uif
    };
    uint64_t upidaddr;
    uint64_t uirr;
    uint64_t uittaddr;
    
}XSaveUINTR;

```


