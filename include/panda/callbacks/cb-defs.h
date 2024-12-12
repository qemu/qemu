/* PANDABEGINCOMMENT
 *
 * Authors:
 *  Tim Leek               tleek@ll.mit.edu
 *  Ryan Whelan            rwhelan@ll.mit.edu
 *  Joshua Hodosh          josh.hodosh@ll.mit.edu
 *  Michael Zhivich        mzhivich@ll.mit.edu
 *  Brendan Dolan-Gavitt   brendandg@gatech.edu
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
PANDAENDCOMMENT */
#pragma once
/*!
 * @file cb-defs.h
 * @brief Definitions of the PANDA supported callbacks.
 */
#ifdef __cplusplus
extern "C" {
#endif

// BEGIN_PYPANDA_NEEDS_THIS -- do not delete this comment bc pypanda
// api autogen needs it.  And don't put any compiler directives
// between this and END_PYPANDA_NEEDS_THIS except includes of other
// files in this directory that contain subsections like this one.

typedef enum panda_cb_type {
    PANDA_CB_BEFORE_BLOCK_TRANSLATE,    // Before translating each basic block
    PANDA_CB_AFTER_BLOCK_TRANSLATE,     // After translating each basic block
    PANDA_CB_BEFORE_BLOCK_EXEC_INVALIDATE_OPT, // Before executing each basic
                                               // block (with option to
                                               // invalidate, may trigger
                                               // retranslation)
    PANDA_CB_BEFORE_TCG_CODEGEN,    // Called right before tcg_codegen.
    PANDA_CB_BEFORE_BLOCK_EXEC,     // Before executing each basic block
    PANDA_CB_AFTER_BLOCK_EXEC,      // After executing each basic block
    PANDA_CB_INSN_TRANSLATE,        // Before an insn is translated
    PANDA_CB_INSN_EXEC,             // Before an insn is executed
    PANDA_CB_AFTER_INSN_TRANSLATE,  // After an insn is translated
    PANDA_CB_AFTER_INSN_EXEC,       // After an insn is executed

    PANDA_CB_VIRT_MEM_BEFORE_READ,  // Before read of virtual memory
    PANDA_CB_VIRT_MEM_BEFORE_WRITE, // Before write of virtual memory
    PANDA_CB_PHYS_MEM_BEFORE_READ,  // Before read of physical memory
    PANDA_CB_PHYS_MEM_BEFORE_WRITE, // Before write of physical memory

    PANDA_CB_VIRT_MEM_AFTER_READ,   // After read of virtual memory
    PANDA_CB_VIRT_MEM_AFTER_WRITE,  // After write of virtual memory
    PANDA_CB_PHYS_MEM_AFTER_READ,   // After read of physical memory
    PANDA_CB_PHYS_MEM_AFTER_WRITE,  // After write of physical memory

    PANDA_CB_MMIO_AFTER_READ,       // After each MMIO read
    PANDA_CB_MMIO_BEFORE_WRITE,     // Before each MMIO write

    PANDA_CB_HD_READ,               // Each HDD read
    PANDA_CB_HD_WRITE,              // Each HDD write
    PANDA_CB_GUEST_HYPERCALL,       // Hypercall from the guest (e.g. CPUID)
    PANDA_CB_MONITOR,               // Monitor callback
    PANDA_CB_QMP,                   // QMP callback
    PANDA_CB_CPU_RESTORE_STATE,     // In cpu_restore_state() (fault/exception)
    PANDA_CB_BEFORE_LOADVM,         // at start of replay, before loadvm
    PANDA_CB_ASID_CHANGED,          // When CPU asid (address space identifier) changes
    PANDA_CB_REPLAY_HD_TRANSFER,    // In replay, hd transfer
    PANDA_CB_REPLAY_NET_TRANSFER,   // In replay, transfers within network card
                                    // (currently only E1000)
    PANDA_CB_REPLAY_SERIAL_RECEIVE, // In replay, right after data is pushed
                                    // into the serial RX FIFO
    PANDA_CB_REPLAY_SERIAL_READ,    // In replay, right after a value is read from
                                    // the serial RX FIFO.
    PANDA_CB_REPLAY_SERIAL_SEND,    // In replay, right after data is popped from
                                    // the serial TX FIFO
    PANDA_CB_REPLAY_SERIAL_WRITE,   // In replay, right after data is pushed into
                                    // the serial TX FIFO.
    PANDA_CB_REPLAY_BEFORE_DMA,     // In replay, just before RAM case of
                                    // cpu_physical_mem_rw
    PANDA_CB_REPLAY_AFTER_DMA,      // In replay, just after RAM case of
                                    // cpu_physical_mem_rw
    PANDA_CB_REPLAY_HANDLE_PACKET,  // In replay, packet in / out
    PANDA_CB_AFTER_CPU_EXEC_ENTER,  // Just after cpu_exec_enter is called
    PANDA_CB_BEFORE_CPU_EXEC_EXIT,  // Just before cpu_exec_exit is called
    PANDA_CB_AFTER_MACHINE_INIT,    // Right after the machine is initialized,
                                    // before any code runs
    PANDA_CB_AFTER_LOADVM,          // Right after we restore from a snapshot
    PANDA_CB_TOP_LOOP,              // At top of loop that manages emulation.
                                    // A good place to take a snapshot.
    PANDA_CB_DURING_MACHINE_INIT,   // At the start of machine initialization

    PANDA_CB_MAIN_LOOP_WAIT,        // Called after main_loop in main_loop.c runs
    PANDA_CB_PRE_SHUTDOWN,          // Just before shutting down

    // Unassigned I/O
    PANDA_CB_UNASSIGNED_IO_READ,
    PANDA_CB_UNASSIGNED_IO_WRITE,

    PANDA_CB_BEFORE_HANDLE_EXCEPTION, // Allows you to monitor, modify,
                                      // or squash exceptions

    PANDA_CB_BEFORE_HANDLE_INTERRUPT, // ditto, for interrupts
    PANDA_CB_START_BLOCK_EXEC,
    PANDA_CB_END_BLOCK_EXEC,

    PANDA_CB_LAST
} panda_cb_type;

// Union of all possible callback function types
typedef union panda_cb {
    /* Callback ID: PANDA_CB_BEFORE_BLOCK_EXEC_INVALIDATE_OPT

       before_block_exec_invalidate_opt:
        Called before execution of every basic block, with the option
        to invalidate the TB.

       Arguments:
        CPUState *env:        the current CPU state
        TranslationBlock *tb: the TB we are about to execute

       Helper call location: cpu-exec.c (indirectly)

       Return value:
        true if we should invalidate the current translation block
        and retranslate, false otherwise.
    */
    bool (*before_block_exec_invalidate_opt)(CPUState *env, TranslationBlock *tb);

    /* Callback ID: PANDA_CB_BEFORE_TCG_CODEGEN

       before_tcg_codegen:
        Called before host code generation for every basic block. Enables
        inspection and modification of the TCG block after lifting from guest
        code.

       Arguments:
        CPUState *env:        the current CPU state
        TranslationBlock *tb: the TB about to be compiled

       Helper call location: translate-all.c

       Return value:
        None
    */
    void (*before_tcg_codegen)(CPUState *env, TranslationBlock *tb);

    /* Callback ID: PANDA_CB_BEFORE_BLOCK_EXEC

       before_block_exec:
        Called before execution of every basic block.

       Arguments:
        CPUState *env:        the current CPU state
        TranslationBlock *tb: the TB we are about to execute

       Helper call location: cpu-exec.c

       Return value:
        none
    */
    void (*before_block_exec)(CPUState *env, TranslationBlock *tb);

    /* Callback ID: PANDA_CB_AFTER_BLOCK_EXEC

       after_block_exec:
        Called after execution of every basic block.
        If exitCode > TB_EXIT_IDX1, then the block exited early.

       Arguments:
        CPUState *env:        the current CPU state
        TranslationBlock *tb: the TB we just executed
        uint8_t exitCode:     why the block execution exited

       Helper call location: cpu-exec.c

       Return value:
        none
    */
    void (*after_block_exec)(CPUState *env, TranslationBlock *tb, uint8_t exitCode);

    /* Callback ID: PANDA_CB_BEFORE_BLOCK_TRANSLATE

       before_block_translate:
        Called before translation of each basic block.

       Arguments:
        CPUState *env:   the current CPU state
        uint64_t pc: the guest PC we are about to translate

       Helper call location: cpu-exec.c

       Return value:
        none
    */
    void (*before_block_translate)(CPUState *env, uint64_t pc);

    /* Callback ID: PANDA_CB_AFTER_BLOCK_TRANSLATE

       after_block_translate:
        Called after the translation of each basic block.

       Arguments:
        CPUState *env:        the current CPU state
        TranslationBlock *tb: the TB we just translated

       Helper call location: cpu-exec.c

       Return value:
        none

       Notes:
        This is a good place to perform extra passes over the generated
        code (particularly by manipulating the LLVM code).
        FIXME: How would this actually work? By this point the out ASM
        has already been generated. Modify the IR and then regenerate?
    */
    void (*after_block_translate)(CPUState *env, TranslationBlock *tb);

    /* Callback ID: PANDA_CB_AFTER_CPU_EXEC_ENTER

       after_cpu_exec_enter:
        Called after cpu_exec calls cpu_exec_enter function.

       Arguments:
        CPUState *env: the current CPU state

       Helper call location: cpu-exec.c

       Return value:
        none
    */
    void (*after_cpu_exec_enter)(CPUState *env);

    /* Callback ID: PANDA_CB_BEFORE_CPU_EXEC_EXIT

       before_cpu_exec_exit:
        Called before cpu_exec calls cpu_exec_exit function.

       Arguments:
        CPUState *env: the current CPU state
        bool ranBlock: true if ran a block since previous cpu_exec_enter

       Helper call location: cpu-exec.c

       Return value:
        none
    */
    void (*before_cpu_exec_exit)(CPUState *env, bool ranBlock);

    /* Callback ID: PANDA_CB_INSN_TRANSLATE

       insn_translate:
        Called before the translation of each instruction.

       Arguments:
        CPUState *env:   the current CPU state
        uint64_t pc: the guest PC we are about to translate

       Helper call location: panda/target/ARCH/translate.c

       Return value:
        true if PANDA should insert instrumentation into the generated code,
        false otherwise

       Notes:
        This allows a plugin writer to instrument only a small number of
        instructions, avoiding the performance hit of instrumenting everything.
        If you do want to instrument every single instruction, just return
        true. See the documentation for PANDA_CB_INSN_EXEC for more detail.
    */
    bool (*insn_translate)(CPUState *env, uint64_t pc);

    /* Callback ID: PANDA_CB_INSN_EXEC

       insn_exec:
        Called before execution of any instruction identified by the
        PANDA_CB_INSN_TRANSLATE callback.

       Arguments:
        CPUState *env:   the current CPU state
        uint64_t pc: the guest PC we are about to execute

       Helper call location: TBA

       Return value:
        unused

       Notes:
        This instrumentation is implemented by generating a call to a
        helper function just before the instruction itself is generated.
        This is fairly expensive, which is why it's only enabled via
        the PANDA_CB_INSN_TRANSLATE callback.
    */
    int (*insn_exec)(CPUState *env, uint64_t pc);

    /* Callback ID: PANDA_CB_AFTER_INSN_TRANSLATE

       after_insn_translate:
        Called after the translation of each instruction.

       Arguments:
        CPUState *env:   the current CPU state
        uint64_t pc: the next guest PC we've translated

       Helper call location: panda/target/ARCH/translate.c

       Return value:
        true if PANDA should insert instrumentation into the generated code,
        false otherwise

       Notes:
        See `insn_translate`, callbacks are registered via PANDA_CB_AFTER_INSN_EXEC
    */
    bool (*after_insn_translate)(CPUState *env, uint64_t pc);

    /* Callback ID: PANDA_CB_AFTER_INSN_EXEC

       after_insn_exec:
        Called after execution of an instruction identified by the
        PANDA_CB_AFTER_INSN_TRANSLATE callback

       Arguments:
        CPUState *env:   the current CPU state
        uint64_t pc: the next guest PC already executed

       Helper call location: TBA

       Return value:
        unused

       Notes:
        See `insn_exec`. Enabled via the PANDA_CB_AFTER_INSN_TRANSLATE callback.
    */
    int (*after_insn_exec)(CPUState *env, uint64_t pc);

    /* Callback ID: PANDA_CB_VIRT_MEM_BEFORE_READ

       virt_mem_before_read:
        Called before memory is read.

       Arguments:
        CPUState *env:     the current CPU state
        uint64_t pc:   the guest PC doing the read
        uint64_t addr: the (virtual) address being read
        size_t size:       the size of the read

       Helper call location: TBA

       Return value:
        none
    */
    void (*virt_mem_before_read)(CPUState *env, uint64_t pc, uint64_t addr, size_t size);

    /* Callback ID: PANDA_CB_VIRT_MEM_BEFORE_WRITE

       virt_mem_before_write:
        Called before memory is written.

       Arguments:
        CPUState *env:     the current CPU state
        uint64_t pc:   the guest PC doing the write
        uint64_t addr: the (virtual) address being written
        size_t size:       the size of the write
        uint8_t *buf:      pointer to the data that is to be written

       Helper call location: TBA

       Return value:
        none
    */
    void (*virt_mem_before_write)(CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf);

    /* Callback ID: PANDA_CB_PHYS_MEM_BEFORE_READ

       phys_mem_before_read:
        Called after memory is read.

       Arguments:
        CPUState *env:     the current CPU state
        uint64_t pc:   the guest PC doing the read
        uint64_t addr: the (physical) address being read
        size_t size:       the size of the read

       Helper call location: TBA

       Return value:
        none
    */
    void (*phys_mem_before_read)(CPUState *env, uint64_t pc, uint64_t addr, size_t size);

    /* Callback ID: PANDA_CB_PHYS_MEM_BEFORE_WRITE

       phys_mem_write:
        Called before memory is written.

       Arguments:
        CPUState *env:     the current CPU state
        uint64_t pc:   the guest PC doing the write
        uint64_t addr: the (physical) address being written
        size_t size:       the size of the write
        uint8_t *buf:      pointer to the data that is to be written

       Helper call location: TBA

       Return value:
        none
    */
    void (*phys_mem_before_write)(CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf);

    /* Callback ID: PANDA_CB_VIRT_MEM_AFTER_READ

       virt_mem_after_read:
        Called after memory is read.

       Arguments:
        CPUState *env:     the current CPU state
        uint64_t pc:   the guest PC doing the read
        uint64_t addr: the (virtual) address being read
        size_t size:       the size of the read
        uint8_t *buf:      pointer to data just read

       Helper call location: TBA

       Return value:
        none
    */
    void (*virt_mem_after_read)(CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf);

    /* Callback ID: PANDA_CB_VIRT_MEM_AFTER_WRITE

       virt_mem_after_write:
        Called after memory is written.

       Arguments:
        CPUState *env:     the current CPU state
        uint64_t pc:   the guest PC doing the write
        uint64_t addr: the (virtual) address being written
        size_t size:       the size of the write
        uint8_t *buf:      pointer to the data that was written

       Helper call location: TBA

       Return value:
        none
    */
    void (*virt_mem_after_write)(CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf);

    /* Callback ID: PANDA_CB_PHYS_MEM_AFTER_READ

       phys_mem_after_read:
        Called after memory is read.

       Arguments:
        CPUState *env:     the current CPU state
        uint64_t pc:   the guest PC doing the read
        uint64_t addr: the (physical) address being read
        size_t size:       the size of the read
        uint8_t *buf:      pointer to data just read

       Helper call location: TBA

       Return value:
        none
    */
    void (*phys_mem_after_read)(CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf);

    /* Callback ID: PANDA_CB_PHYS_MEM_AFTER_WRITE

       phys_mem_write:
        Called after memory is written.

       Arguments:
        CPUState *env:     the current CPU state
        uint64_t pc:   the guest PC doing the write
        uint64_t addr: the (physical) address being written
        size_t size:       the size of the write
        uint8_t *buf:      pointer to the data that was written

       Helper call location: TBA

       Return value:
        none
    */
    void (*phys_mem_after_write)(CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf);

    /* Callback ID: PANDA_CB_MMIO_AFTER_READ

       mmio_after_read:
        Called after MMIO memory is read.

       Arguments:
        CPUState *env:          the current CPU state
        uint64_t physaddr:  the physical address being read from
        uint64_t vaddr:     the virtual address being read from
        size_t size:            the size of the read
        uin64_t *val:           the value being read

       Helper call location: cputlb.c

       Return value:
        none
    */
    void (*mmio_after_read)(CPUState *env, uint64_t physaddr, uint64_t vaddr, size_t size, uint64_t *val);

    /* Callback ID: PANDA_CB_MMIO_BEFORE_WRITE

       mmio_before_write:
        Called after MMIO memory is written to.

       Arguments:
        CPUState *env:          the current CPU state
        uint64_t physaddr:  the physical address being written to
        uint64_t vaddr:     the virtual address being written to
        size_t size:            the size of the write
        uin64_t *val:           the value being written

       Helper call location: cputlb.c

       Return value:
        none
    */
    void (*mmio_before_write)(CPUState *env, uint64_t physaddr, uint64_t vaddr, size_t size, uint64_t *val);

    /* Callback ID: PANDA_CB_HD_READ
       hd_read : called when there is a hard drive read

       Note: this was added to panda_cb_type enum but no callback prototype inserted
       Here is a stub.  I'm not sure what the args should be.
       Arguments
       CPUState *env
    */

    void (*hd_read)(CPUState *env);

    /* Callback ID: PANDA_CB_HD_WRITE
       hd_write : called when there is a hard drive write

       Note: this was added to panda_cb_type enum but no callback prototype inserted
       Here is a stub.  I'm not sure what the args should be.
       Arguments
       CPUState *env
    */

    void (*hd_write)(CPUState *env);

    /* Callback ID: PANDA_CB_GUEST_HYPERCALL

       guest_hypercall:
        Called when a program inside the guest makes a hypercall to pass
        information from inside the guest to a plugin

       Arguments:
        CPUState *env: the current CPU state

       Helper call location: target/i386/misc_helper.c

       Return value:
        true if the callback has processed the hypercall, false if the
        hypercall has been ignored.

       Notes:
        On x86, this is called whenever CPUID is executed. On ARM, the
        MCR instructions is used. Plugins should check for magic values
        in the registers to determine if it really is a guest hypercall.
        Parameters can be passed in other registers. If the plugin
        processes the hypercall, it should return true so the execution
        of the normal instruction is skipped.
    */
    bool (*guest_hypercall)(CPUState *env);

    /* Callback ID: PANDA_CB_MONITOR

       monitor:
        Called when someone uses the plugin_cmd monitor command.

       Arguments:
        Monitor *mon:    a pointer to the Monitor
        const char *cmd: the command string passed to plugin_cmd

       Helper call location: TBA

       Return value:
        unused

       Notes:
        The command is passed as a single string. No parsing is performed
        on the string before it is passed to the plugin, so each plugin
        must parse the string as it deems appropriate (e.g. by using strtok
        and getopt) to do more complex option processing.
        It is recommended that each plugin implementing this callback respond
        to the "help" message by listing the commands supported by the plugin.
        Note that every loaded plugin will have the opportunity to respond to
        each plugin_cmd; thus it is a good idea to ensure that your plugin's
        monitor commands are uniquely named, e.g. by using the plugin name
        as a prefix ("sample_do_foo" rather than "do_foo").
    */
    int (*monitor)(Monitor *mon, const char *cmd);

    /* Callback ID: PANDA_CB_QMP

       qmp:
        Called when someone sends an unhandled QMP command

       Arguments:
         char *command: the command string as json
         char *args:    the arguments string as json
         char **result: pointer to a json result or NULL

       Helper call location: TBA

       Return value:
         bool: true IFF the command was handled by the plugin
    */
    bool (*qmp)(char *command, char* args, char **result);


    /* Callback ID: PANDA_CB_CPU_RESTORE_STATE

       cpu_restore_state:
        Called inside of cpu_restore_state(), when there is a CPU
        fault/exception.

       Arguments:
        CPUState *env:        the current CPU state
        TranslationBlock *tb: the current translation block

       Helper call location: translate-all.c

       Return value:
        none
    */
    void (*cpu_restore_state)(CPUState *env, TranslationBlock *tb);

    /* Callback ID: PANDA_CB_BEFORE_LOADVM

       before_loadvm:
        Called at start of replay, before loadvm is called. This allows
        us to hook devices' loadvm handlers. Remember to unregister the
        existing handler for the device first. See the example in the
        sample plugin.

       Arguments:
        none

       Helper call location: TBA

       Return value:
        unused
    */
    int (*before_loadvm)(void);

    /* Callback ID: PANDA_CB_ASID_CHANGED

       asid_changed:
        Called when asid changes.

       Arguments:
        CPUState *env:       pointer to CPUState
        uint64_t oldval: old asid value
        uint64_t newval: new asid value

       Helper call location: target/i386/helper.c, target/arm/helper.c

       Return value:
        true if the asid should be prevented from being changed
        false otherwise

       Notes:
        The callback is only invoked implemented for x86 and ARM.
        This should break plugins which rely on it to detect context
        switches in any other architecture.
    */
    bool (*asid_changed)(CPUState *env, uint64_t oldval, uint64_t newval);

    /* Callback ID:     PANDA_CB_REPLAY_HD_TRANSFER,

       replay_hd_transfer:
        In replay only. Some kind of data transfer involving hard drive.

       Arguments:
        CPUState *env:          pointer to CPUState
        uint32_t type:          type of transfer  (Hd_transfer_type)
        uint64_t src_addr:  address for src
        uint64_t dest_addr: address for dest
        size_t num_bytes:       size of transfer in bytes

       Helper call location: panda/src/rr/rr_log.c

       Return value:
        none

       Helper call location: TBA

       Notes:
        Unlike most callbacks, this is neither a "before" or "after" callback.
        In replay the transfer doesn't really happen. We are *at* the point at
        which it happened, really.
    */
    void (*replay_hd_transfer)(CPUState *env, uint32_t type, uint64_t src_addr, uint64_t dest_addr, size_t num_bytes);

    /* Callback ID:     PANDA_CB_REPLAY_BEFORE_DMA,

       replay_before_dma:
        In replay only. We are about to dma between qemu buffer and
        guest memory.

       Arguments:
        CPUState *env:      pointer to CPUState
        const uint8_t *buf: pointer to the QEMU's device buffer ussed in the transfer
        hwaddr addr:        address written to in the guest RAM
        size_t size:        size of transfer
        bool is_write:      indicates whether the DMA transfer writes to memory

       Helper call location: exec.c

       Return value:
        none
    */
    void (*replay_before_dma)(CPUState *env, const uint8_t *buf, hwaddr addr, size_t size, bool is_write);

    /* Callback ID:     PANDA_CB_REPLAY_AFTER_DMA,

       In replay only, we are about to dma between qemu buffer and guest memory

       Arguments:
        CPUState *env:      pointer to CPUState
        const uint8_t *buf: pointer to the QEMU's device buffer ussed in the transfer
        hwaddr addr:        address written to in the guest RAM
        size_t size:        size of transfer
        bool is_write:      indicates whether the DMA transfer writes to memory

       Helper call location: exec.c

       Return value:
        none
    */
    void (*replay_after_dma)(CPUState *env, const uint8_t *buf, hwaddr addr, size_t size, bool is_write);

    /* Callback ID:   PANDA_CB_REPLAY_HANDLE_PACKET,

       In replay only, we have a packet (incoming / outgoing) in hand.

       Arguments:
        CPUState *env:         pointer to CPUState
        uint8_t *buf:          buffer containing packet data
        size_t size:           num bytes in buffer
        uint8_t direction:     either `PANDA_NET_RX` or `PANDA_NET_TX`
        uint64_t buf_addr_rec: the address of `buf` at the time of recording

       Helper call location: panda/src/rr/rr_log.c

       Return value:
        none

       Notes:
        `buf_addr_rec` corresponds to the address of the device buffer of
        the emulated NIC. I.e. it is the address of a VM-host-side buffer.
        It is useful for implementing network tainting in an OS-agnostic
        way, in conjunction with taint2_label_io().

        FIXME: The `buf_addr_rec` maps to the `uint8_t *buf` field of the
        internal `RR_handle_packet_args` struct. The field is dumped/loaded
        to/from the trace without proper serialization/deserialization. As
        a result, a 64bit build of PANDA will not be able to process traces
        produced by a 32bit of PANDA, and vice-versa.
        There are more internal structs that suffer from the same issue.
        This is an oversight that will eventually be fixed. But as the
        real impact is minimal (virtually nobody uses 32bit builds),
        the fix has a very low priority in the bugfix list.
    */
    void (*replay_handle_packet)(CPUState *env, uint8_t *buf, size_t size, uint8_t direction, uint64_t buf_addr_rec);

    /* Callback ID:     PANDA_CB_REPLAY_NET_TRANSFER,

       replay_net_transfer:
       In replay only, some kind of data transfer within the network card
       (currently, only the E1000 is supported).

       Arguments:
        CPUState *env:          pointer to CPUState
        uint32_t type:          type of transfer  (Net_transfer_type)
        uint64_t src_addr:      address for src
        uint64_t dest_addr:     address for dest
        size_t num_bytes:       size of transfer in bytes

       Helper call location: panda/src/rr/rr_log.c

       Return value:
        none

       Notes:
        Unlike most callbacks, this is neither a "before" or "after" callback.
        In replay the transfer doesn't really happen. We are *at* the point at
        which it happened, really.
        Also, the src_addr and dest_addr may be for either host (ie. a location
        in the emulated network device) or guest, depending upon the type.
    */
    void (*replay_net_transfer)(CPUState *env, uint32_t type, uint64_t src_addr, uint64_t dest_addr, size_t num_bytes);

    /* Callback ID:     PANDA_CB_REPLAY_SERIAL_RECEIVE,

        replay_serial_receive:
        In replay only, called when a byte is received on the serial port.

       Arguments:
        CPUState *env:          pointer to CPUState
        uint64_t fifo_addr: address of the data within the fifo
        uint8_t value:          value received

       Helper call location: panda/src/rr/rr_log.c

       Return value:
        unused
    */
    void (*replay_serial_receive)(CPUState *env, uint64_t fifo_addr, uint8_t value);

    /* Callback ID:     PANDA_CB_REPLAY_SERIAL_READ,

       replay_serial_read:
        In replay only, called when a byte read from the serial RX FIFO

       Arguments:
        CPUState *env:          pointer to CPUState
        uint64_t fifo_addr: address of the data within the fifo (source)
        uint32_t port_addr:     address of the IO port where data is being read (destination)
        uint8_t value:          value read

       Helper call location: panda/src/rr/rr_log.c

       Return value:
        none
    */
    void (*replay_serial_read)(CPUState *env, uint64_t fifo_addr, uint32_t port_addr, uint8_t value);

    /* Callback ID:     PANDA_CB_REPLAY_SERIAL_SEND,

       replay_serial_send:
        In replay only, called when a byte is sent on the serial port.

       Arguments:
        CPUState *env:          pointer to CPUState
        uint64_t fifo_addr: address of the data within the fifo
        uint8_t value:          value received

       Helper call location: panda/src/rr/rr_log.c

       Return value:
        none
    */
    void (*replay_serial_send)(CPUState *env, uint64_t fifo_addr, uint8_t value);

    /* Callback ID:     PANDA_CB_REPLAY_SERIAL_WRITE,

       In replay only, called when a byte written to the serial TX FIFO

       Arguments:
        CPUState *env:          pointer to CPUState
        uint64_t fifo_addr: address of the data within the fifo (source)
        uint32_t port_addr:     address of the IO port where data is being read (destination)
        uint8_t value:          value read

       Helper call location: panda/src/rr/rr_log.c

       Return value:
        none
    */
    void (*replay_serial_write)(CPUState *env, uint64_t fifo_addr, uint32_t port_addr, uint8_t value);

    /* Callback ID:     PANDA_CB_AFTER_MACHINE_INIT

       after_machine_init:
        Called right after the machine has been initialized, but before
        any guest code runs.

       Arguments:
        void *cpu_env: pointer to CPUState

       Helper call location: TBA

       Return value:
        none

       Notes:
        This callback allows initialization of components that need
        access to the RAM, CPU object, etc. E.g. for the taint2 plugin,
        this is the appropriate place to call taint2_enable_taint().
    */
    void (*after_machine_init)(CPUState *env);

    /* Callback ID:     PANDA_CB_AFTER_LOADVM

       after_loadvm:
        Called right after a snapshot has been loaded (either with loadvm or replay initialization),
        but before any guest code runs.

       Arguments:
        void *cpu_env: pointer to CPUState

       Return value:
        none

    */
    void (*after_loadvm)(CPUState *env);

    /* Callback ID:     PANDA_CB_TOP_LOOP

       top_loop:
        Called at the top of the loop that manages emulation.

       Arguments:
        CPUState *env:          pointer to CPUState

       Helper call location: cpus.c

       Return value:
        unused
     */
    void (*top_loop)(CPUState *env);
    /* Callback ID:     PANDA_CB_DURING_MACHINE_INIT

       during_machine_init: Called in the middle of machine initialization

       Arguments:
         MachineState *machine: pointer to the machine state

       Return value:
         None
     */

    void (*during_machine_init)(MachineState *machine);

    /* Callback ID:     PANDA_CB_MAIN_LOOP_WAIT

       main_loop_wait: Called in IO thread in place where monitor cmds are processed

       Arguments:
         None

       Return value:
         None
     */

    void (*main_loop_wait)(void);

    /* Callback ID:     PANDA_CB_PRE_SHUTDOWN

      pre_shutdown: Called just before qemu shuts down

       Arguments:
         None

       Return value:
         None
     */
    void (*pre_shutdown)(void);

    /* Callback ID:     PANDA_CB_UNASSIGNED_IO_READ

      unassigned_io_read: Called when the guest attempts to read from an unmapped peripheral via MMIO

       Arguments:
         pc: Guest program counter at time of write
         addr: Physical address written to
         size: Size of write
         val: Pointer to a buffer that will be passed to the guest as the result of the read

       Return value:
         True if value read was changed by a PANDA plugin and should be returned
         False if error-logic (invalid write) should be run
     */
    bool (*unassigned_io_read)(CPUState *env, uint64_t pc, hwaddr addr, size_t size, uint64_t *val);

    /* Callback ID:     PANDA_CB_UNASSIGNED_IO_WRITE

      unassigned_io_write: Called when the guest attempts to write to an unmapped peripheral via MMIO

       Arguments:
         pc: Guest program counter at time of write
         addr: Physical address written to
         size: Size of write
         val: Data being written, up to 8 bytes

       Return value:
         True if the write should be allowed without error
         False if normal behavior should be used (error-logic)
     */
    bool (*unassigned_io_write)(CPUState *env, uint64_t pc, hwaddr addr, size_t size, uint64_t val);


    /* Callback ID:     PANDA_CB_BEFORE_HANDLE_EXCEPTION

       before_handle_exception: Called just before we are about to
       handle an exception.

       Note: only called for cpu->exception_index > 0

       Arguments:
         exception_index (the current exception number)

       Return value:
         a new exception_index.

       Note: There might be more than one callback for this location.
       First callback that returns an exception index that *differs*
       from the one passed as an arg wins. That is what we return as
       the new exception index, which will replace
       cpu->exception_index

     */

    int32_t (*before_handle_exception)(CPUState *cpu, int32_t exception_index);

    /* Callback ID:     PANDA_CB_BEFORE_HANDLE_INTERRUPT

       before_handle_interrupt: Called just before we are about to
       handle an interrupt.

       Arguments:
         interrupt request

       Return value:
         new interrupt_rquest

       Note: There might be more than one callback for this location.
       First callback that returns an interrupt_request that *differs*
       from the one passed as an arg wins.

     */


    int32_t (*before_handle_interrupt)(CPUState *cpu, int32_t interrupt_request);
    
    /* Callback ID: PANDA_CB_START_BLOCK_EXEC

       start_block_exec:
        This is like before_block_exec except its part of the TCG stream.

       Arguments:
        CPUState *env:        the current CPU state
        TranslationBlock *tb: the TB we are executing

       Helper call location: cpu-exec.c

       Return value:
        none
    */

    void (*start_block_exec)(CPUState *cpu, TranslationBlock* tb);

    /* Callback ID: PANDA_CB_END_BLOCK_EXEC

       end_block_exec:
        This is like after_block_exec except its part of the TCG stream.

       Arguments:
        CPUState *env:        the current CPU state
        TranslationBlock *tb: the TB we are executing

       Helper call location: cpu-exec.c

       Return value:
        none
    */
    void (*end_block_exec)(CPUState *cpu, TranslationBlock* tb);

    /* cbaddr is a dummy union member.

       This union only contains function pointers.
       Using the cbaddr member one can compare if two union instances
       point to the same callback function. In principle, any other
       member could be used instead.
       However, cbaddr provides neutral semantics for the comparisson.
    */

    void (*cbaddr)(void);

} panda_cb;

typedef union panda_cb_with_context {
    /* Callback ID: PANDA_CB_BEFORE_BLOCK_EXEC_INVALIDATE_OPT

       before_block_exec_invalidate_opt:
        Called before execution of every basic block, with the option
        to invalidate the TB.

       Arguments:
        CPUState *env:        the current CPU state
        TranslationBlock *tb: the TB we are about to execute

       Helper call location: cpu-exec.c (indirectly)

       Return value:
        true if we should invalidate the current translation block
        and retranslate, false otherwise.
    */
    bool (*before_block_exec_invalidate_opt)(void* context, CPUState *env, TranslationBlock *tb);

    /* Callback ID: PANDA_CB_BEFORE_TCG_CODEGEN

       before_tcg_codegen:
        Called before host code generation for every basic block. Enables
        inspection and modification of the TCG block after lifting from guest
        code.

       Arguments:
        CPUState *env:        the current CPU state
        TranslationBlock *tb: the TB about to be compiled

       Helper call location: translate-all.c

       Return value:
        None
    */
    void (*before_tcg_codegen)(void* context, CPUState *env, TranslationBlock *tb);

    /* Callback ID: PANDA_CB_BEFORE_BLOCK_EXEC

       before_block_exec:
        Called before execution of every basic block.

       Arguments:
        CPUState *env:        the current CPU state
        TranslationBlock *tb: the TB we are about to execute

       Helper call location: cpu-exec.c

       Return value:
        none
    */
    void (*before_block_exec)(void* context, CPUState *env, TranslationBlock *tb);

    /* Callback ID: PANDA_CB_AFTER_BLOCK_EXEC

       after_block_exec:
        Called after execution of every basic block.
        If exitCode > TB_EXIT_IDX1, then the block exited early.

       Arguments:
        CPUState *env:        the current CPU state
        TranslationBlock *tb: the TB we just executed
        uint8_t exitCode:     why the block execution exited

       Helper call location: cpu-exec.c

       Return value:
        none
    */
    void (*after_block_exec)(void* context, CPUState *env, TranslationBlock *tb, uint8_t exitCode);

    /* Callback ID: PANDA_CB_BEFORE_BLOCK_TRANSLATE

       before_block_translate:
        Called before translation of each basic block.

       Arguments:
        CPUState *env:   the current CPU state
        uint64_t pc: the guest PC we are about to translate

       Helper call location: cpu-exec.c

       Return value:
        none
    */
    void (*before_block_translate)(void* context, CPUState *env, uint64_t pc);

    /* Callback ID: PANDA_CB_AFTER_BLOCK_TRANSLATE

       after_block_translate:
        Called after the translation of each basic block.

       Arguments:
        CPUState *env:        the current CPU state
        TranslationBlock *tb: the TB we just translated

       Helper call location: cpu-exec.c

       Return value:
        none

       Notes:
        This is a good place to perform extra passes over the generated
        code (particularly by manipulating the LLVM code).
        FIXME: How would this actually work? By this point the out ASM
        has already been generated. Modify the IR and then regenerate?
    */
    void (*after_block_translate)(void* context, CPUState *env, TranslationBlock *tb);

    /* Callback ID: PANDA_CB_AFTER_CPU_EXEC_ENTER

       after_cpu_exec_enter:
        Called after cpu_exec calls cpu_exec_enter function.

       Arguments:
        CPUState *env: the current CPU state

       Helper call location: cpu-exec.c

       Return value:
        none
    */
    void (*after_cpu_exec_enter)(void* context, CPUState *env);

    /* Callback ID: PANDA_CB_BEFORE_CPU_EXEC_EXIT

       before_cpu_exec_exit:
        Called before cpu_exec calls cpu_exec_exit function.

       Arguments:
        CPUState *env: the current CPU state
        bool ranBlock: true if ran a block since previous cpu_exec_enter

       Helper call location: cpu-exec.c

       Return value:
        none
    */
    void (*before_cpu_exec_exit)(void* context, CPUState *env, bool ranBlock);

    /* Callback ID: PANDA_CB_INSN_TRANSLATE

       insn_translate:
        Called before the translation of each instruction.

       Arguments:
        CPUState *env:   the current CPU state
        uint64_t pc: the guest PC we are about to translate

       Helper call location: panda/target/ARCH/translate.c

       Return value:
        true if PANDA should insert instrumentation into the generated code,
        false otherwise

       Notes:
        This allows a plugin writer to instrument only a small number of
        instructions, avoiding the performance hit of instrumenting everything.
        If you do want to instrument every single instruction, just return
        true. See the documentation for PANDA_CB_INSN_EXEC for more detail.
    */
    bool (*insn_translate)(void* context, CPUState *env, uint64_t pc);

    /* Callback ID: PANDA_CB_INSN_EXEC

       insn_exec:
        Called before execution of any instruction identified by the
        PANDA_CB_INSN_TRANSLATE callback.

       Arguments:
        CPUState *env:   the current CPU state
        uint64_t pc: the guest PC we are about to execute

       Helper call location: TBA

       Return value:
        unused

       Notes:
        This instrumentation is implemented by generating a call to a
        helper function just before the instruction itself is generated.
        This is fairly expensive, which is why it's only enabled via
        the PANDA_CB_INSN_TRANSLATE callback.
    */
    int (*insn_exec)(void* context, CPUState *env, uint64_t pc);

    /* Callback ID: PANDA_CB_AFTER_INSN_TRANSLATE

       after_insn_translate:
        Called after the translation of each instruction.

       Arguments:
        CPUState *env:   the current CPU state
        uint64_t pc: the next guest PC we've translated

       Helper call location: panda/target/ARCH/translate.c

       Return value:
        true if PANDA should insert instrumentation into the generated code,
        false otherwise

       Notes:
        See `insn_translate`, callbacks are registered via PANDA_CB_AFTER_INSN_EXEC
    */
    bool (*after_insn_translate)(void* context, CPUState *env, uint64_t pc);

    /* Callback ID: PANDA_CB_AFTER_INSN_EXEC

       after_insn_exec:
        Called after execution of an instruction identified by the
        PANDA_CB_AFTER_INSN_TRANSLATE callback

       Arguments:
        CPUState *env:   the current CPU state
        uint64_t pc: the next guest PC already executed

       Helper call location: TBA

       Return value:
        unused

       Notes:
        See `insn_exec`. Enabled via the PANDA_CB_AFTER_INSN_TRANSLATE callback.
    */
    int (*after_insn_exec)(void* context, CPUState *env, uint64_t pc);

    /* Callback ID: PANDA_CB_VIRT_MEM_BEFORE_READ

       virt_mem_before_read:
        Called before memory is read.

       Arguments:
        CPUState *env:     the current CPU state
        uint64_t pc:   the guest PC doing the read
        uint64_t addr: the (virtual) address being read
        size_t size:       the size of the read

       Helper call location: TBA

       Return value:
        none
    */
    void (*virt_mem_before_read)(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size);

    /* Callback ID: PANDA_CB_VIRT_MEM_BEFORE_WRITE

       virt_mem_before_write:
        Called before memory is written.

       Arguments:
        CPUState *env:     the current CPU state
        uint64_t pc:   the guest PC doing the write
        uint64_t addr: the (virtual) address being written
        size_t size:       the size of the write
        uint8_t *buf:      pointer to the data that is to be written

       Helper call location: TBA

       Return value:
        none
    */
    void (*virt_mem_before_write)(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf);

    /* Callback ID: PANDA_CB_PHYS_MEM_BEFORE_READ

       phys_mem_before_read:
        Called after memory is read.

       Arguments:
        CPUState *env:     the current CPU state
        uint64_t pc:   the guest PC doing the read
        uint64_t addr: the (physical) address being read
        size_t size:       the size of the read

       Helper call location: TBA

       Return value:
        none
    */
    void (*phys_mem_before_read)(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size);

    /* Callback ID: PANDA_CB_PHYS_MEM_BEFORE_WRITE

       phys_mem_write:
        Called before memory is written.

       Arguments:
        CPUState *env:     the current CPU state
        uint64_t pc:   the guest PC doing the write
        uint64_t addr: the (physical) address being written
        size_t size:       the size of the write
        uint8_t *buf:      pointer to the data that is to be written

       Helper call location: TBA

       Return value:
        none
    */
    void (*phys_mem_before_write)(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf);

    /* Callback ID: PANDA_CB_VIRT_MEM_AFTER_READ

       virt_mem_after_read:
        Called after memory is read.

       Arguments:
        CPUState *env:     the current CPU state
        uint64_t pc:   the guest PC doing the read
        uint64_t addr: the (virtual) address being read
        size_t size:       the size of the read
        uint8_t *buf:      pointer to data just read

       Helper call location: TBA

       Return value:
        none
    */
    void (*virt_mem_after_read)(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf);

    /* Callback ID: PANDA_CB_VIRT_MEM_AFTER_WRITE

       virt_mem_after_write:
        Called after memory is written.

       Arguments:
        CPUState *env:     the current CPU state
        uint64_t pc:   the guest PC doing the write
        uint64_t addr: the (virtual) address being written
        size_t size:       the size of the write
        uint8_t *buf:      pointer to the data that was written

       Helper call location: TBA

       Return value:
        none
    */
    void (*virt_mem_after_write)(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf);

    /* Callback ID: PANDA_CB_PHYS_MEM_AFTER_READ

       phys_mem_after_read:
        Called after memory is read.

       Arguments:
        CPUState *env:     the current CPU state
        uint64_t pc:   the guest PC doing the read
        uint64_t addr: the (physical) address being read
        size_t size:       the size of the read
        uint8_t *buf:      pointer to data just read

       Helper call location: TBA

       Return value:
        none
    */
    void (*phys_mem_after_read)(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf);

    /* Callback ID: PANDA_CB_PHYS_MEM_AFTER_WRITE

       phys_mem_write:
        Called after memory is written.

       Arguments:
        CPUState *env:     the current CPU state
        uint64_t pc:   the guest PC doing the write
        uint64_t addr: the (physical) address being written
        size_t size:       the size of the write
        uint8_t *buf:      pointer to the data that was written

       Helper call location: TBA

       Return value:
        none
    */
    void (*phys_mem_after_write)(void* context, CPUState *env, uint64_t pc, uint64_t addr, size_t size, uint8_t *buf);

    /* Callback ID: PANDA_CB_MMIO_AFTER_READ

       mmio_after_read:
        Called after MMIO memory is read.

       Arguments:
        CPUState *env:          the current CPU state
        uint64_t physaddr:  the physical address being read from
        uint64_t vaddr:     the virtual address being read from
        size_t size:            the size of the read
        uin64_t *val:           the value being read

       Helper call location: cputlb.c

       Return value:
        none
    */
    void (*mmio_after_read)(void* context, CPUState *env, uint64_t physaddr, uint64_t vaddr, size_t size, uint64_t *val);

    /* Callback ID: PANDA_CB_MMIO_BEFORE_WRITE

       mmio_before_write:
        Called after MMIO memory is written to.

       Arguments:
        CPUState *env:          the current CPU state
        uint64_t physaddr:  the physical address being written to
        uint64_t vaddr:     the virtual address being written to
        size_t size:            the size of the write
        uin64_t *val:           the value being written

       Helper call location: cputlb.c

       Return value:
        none
    */
    void (*mmio_before_write)(void* context, CPUState *env, uint64_t physaddr, uint64_t vaddr, size_t size, uint64_t *val);

    /* Callback ID: PANDA_CB_HD_READ
       hd_read : called when there is a hard drive read

       Note: this was added to panda_cb_type enum but no callback prototype inserted
       Here is a stub.  I'm not sure what the args should be.
       Arguments
       CPUState *env
    */

    void (*hd_read)(void* context, CPUState *env);

    /* Callback ID: PANDA_CB_HD_WRITE
       hd_write : called when there is a hard drive write

       Note: this was added to panda_cb_type enum but no callback prototype inserted
       Here is a stub.  I'm not sure what the args should be.
       Arguments
       CPUState *env
    */

    void (*hd_write)(void* context, CPUState *env);

    /* Callback ID: PANDA_CB_GUEST_HYPERCALL

       guest_hypercall:
        Called when a program inside the guest makes a hypercall to pass
        information from inside the guest to a plugin

       Arguments:
        CPUState *env: the current CPU state

       Helper call location: target/i386/misc_helper.c

       Return value:
        true if the callback has processed the hypercall, false if the
        hypercall has been ignored.

       Notes:
        On x86, this is called whenever CPUID is executed. On ARM, the
        MCR instructions is used. Plugins should check for magic values
        in the registers to determine if it really is a guest hypercall.
        Parameters can be passed in other registers. If the plugin
        processes the hypercall, it should return true so the execution
        of the normal instruction is skipped.
    */
    bool (*guest_hypercall)(void* context, CPUState *env);

    /* Callback ID: PANDA_CB_MONITOR

       monitor:
        Called when someone uses the plugin_cmd monitor command.

       Arguments:
        Monitor *mon:    a pointer to the Monitor
        const char *cmd: the command string passed to plugin_cmd

       Helper call location: TBA

       Return value:
        unused

       Notes:
        The command is passed as a single string. No parsing is performed
        on the string before it is passed to the plugin, so each plugin
        must parse the string as it deems appropriate (e.g. by using strtok
        and getopt) to do more complex option processing.
        It is recommended that each plugin implementing this callback respond
        to the "help" message by listing the commands supported by the plugin.
        Note that every loaded plugin will have the opportunity to respond to
        each plugin_cmd; thus it is a good idea to ensure that your plugin's
        monitor commands are uniquely named, e.g. by using the plugin name
        as a prefix ("sample_do_foo" rather than "do_foo").
    */
    int (*monitor)(void* context, Monitor *mon, const char *cmd);

    /* Callback ID: PANDA_CB_QMP

       qmp:
        Called when someone sends an unhandled QMP command

       Arguments:
         char *command: the command string as json
         char *args:    the arguments string as json
         char **result: pointer to a json result or NULL

       Helper call location: TBA

       Return value:
         bool: true IFF the command was handled by the plugin
    */
    bool (*qmp)(void* context, char *command, char* args, char **result);


    /* Callback ID: PANDA_CB_CPU_RESTORE_STATE

       cpu_restore_state:
        Called inside of cpu_restore_state(), when there is a CPU
        fault/exception.

       Arguments:
        CPUState *env:        the current CPU state
        TranslationBlock *tb: the current translation block

       Helper call location: translate-all.c

       Return value:
        none
    */
    void (*cpu_restore_state)(void* context, CPUState *env, TranslationBlock *tb);

    /* Callback ID: PANDA_CB_BEFORE_LOADVM

       before_loadvm:
        Called at start of replay, before loadvm is called. This allows
        us to hook devices' loadvm handlers. Remember to unregister the
        existing handler for the device first. See the example in the
        sample plugin.

       Arguments:
        none

       Helper call location: TBA

       Return value:
        unused
    */
    int (*before_loadvm)(void* context);

    /* Callback ID: PANDA_CB_ASID_CHANGED

       asid_changed:
        Called when asid changes.

       Arguments:
        CPUState *env:       pointer to CPUState
        uint64_t oldval: old asid value
        uint64_t newval: new asid value

       Helper call location: target/i386/helper.c, target/arm/helper.c

       Return value:
        true if the asid should be prevented from being changed
        false otherwise

       Notes:
        The callback is only invoked implemented for x86 and ARM.
        This should break plugins which rely on it to detect context
        switches in any other architecture.
    */
    bool (*asid_changed)(void* context, CPUState *env, uint64_t oldval, uint64_t newval);

    /* Callback ID:     PANDA_CB_REPLAY_HD_TRANSFER,

       replay_hd_transfer:
        In replay only. Some kind of data transfer involving hard drive.

       Arguments:
        CPUState *env:          pointer to CPUState
        uint32_t type:          type of transfer  (Hd_transfer_type)
        uint64_t src_addr:  address for src
        uint64_t dest_addr: address for dest
        size_t num_bytes:       size of transfer in bytes

       Helper call location: panda/src/rr/rr_log.c

       Return value:
        none

       Helper call location: TBA

       Notes:
        Unlike most callbacks, this is neither a "before" or "after" callback.
        In replay the transfer doesn't really happen. We are *at* the point at
        which it happened, really.
    */
    void (*replay_hd_transfer)(void* context, CPUState *env, uint32_t type, uint64_t src_addr, uint64_t dest_addr, size_t num_bytes);

    /* Callback ID:     PANDA_CB_REPLAY_BEFORE_DMA,

       replay_before_dma:
        In replay only. We are about to dma between qemu buffer and
        guest memory.

       Arguments:
        CPUState *env:      pointer to CPUState
        const uint8_t *buf: pointer to the QEMU's device buffer ussed in the transfer
        hwaddr addr:        address written to in the guest RAM
        size_t size:        size of transfer
        bool is_write:      indicates whether the DMA transfer writes to memory

       Helper call location: exec.c

       Return value:
        none
    */
    void (*replay_before_dma)(void* context, CPUState *env, const uint8_t *buf, hwaddr addr, size_t size, bool is_write);

    /* Callback ID:     PANDA_CB_REPLAY_AFTER_DMA,

       In replay only, we are about to dma between qemu buffer and guest memory

       Arguments:
        CPUState *env:      pointer to CPUState
        const uint8_t *buf: pointer to the QEMU's device buffer ussed in the transfer
        hwaddr addr:        address written to in the guest RAM
        size_t size:        size of transfer
        bool is_write:      indicates whether the DMA transfer writes to memory

       Helper call location: exec.c

       Return value:
        none
    */
    void (*replay_after_dma)(void* context, CPUState *env, const uint8_t *buf, hwaddr addr, size_t size, bool is_write);

    /* Callback ID:   PANDA_CB_REPLAY_HANDLE_PACKET,

       In replay only, we have a packet (incoming / outgoing) in hand.

       Arguments:
        CPUState *env:         pointer to CPUState
        uint8_t *buf:          buffer containing packet data
        size_t size:           num bytes in buffer
        uint8_t direction:     either `PANDA_NET_RX` or `PANDA_NET_TX`
        uint64_t buf_addr_rec: the address of `buf` at the time of recording

       Helper call location: panda/src/rr/rr_log.c

       Return value:
        none

       Notes:
        `buf_addr_rec` corresponds to the address of the device buffer of
        the emulated NIC. I.e. it is the address of a VM-host-side buffer.
        It is useful for implementing network tainting in an OS-agnostic
        way, in conjunction with taint2_label_io().

        FIXME: The `buf_addr_rec` maps to the `uint8_t *buf` field of the
        internal `RR_handle_packet_args` struct. The field is dumped/loaded
        to/from the trace without proper serialization/deserialization. As
        a result, a 64bit build of PANDA will not be able to process traces
        produced by a 32bit of PANDA, and vice-versa.
        There are more internal structs that suffer from the same issue.
        This is an oversight that will eventually be fixed. But as the
        real impact is minimal (virtually nobody uses 32bit builds),
        the fix has a very low priority in the bugfix list.
    */
    void (*replay_handle_packet)(void* context, CPUState *env, uint8_t *buf, size_t size, uint8_t direction, uint64_t buf_addr_rec);

    /* Callback ID:     PANDA_CB_REPLAY_NET_TRANSFER,

       replay_net_transfer:
       In replay only, some kind of data transfer within the network card
       (currently, only the E1000 is supported).

       Arguments:
        CPUState *env:          pointer to CPUState
        uint32_t type:          type of transfer  (Net_transfer_type)
        uint64_t src_addr:      address for src
        uint64_t dest_addr:     address for dest
        size_t num_bytes:       size of transfer in bytes

       Helper call location: panda/src/rr/rr_log.c

       Return value:
        none

       Notes:
        Unlike most callbacks, this is neither a "before" or "after" callback.
        In replay the transfer doesn't really happen. We are *at* the point at
        which it happened, really.
        Also, the src_addr and dest_addr may be for either host (ie. a location
        in the emulated network device) or guest, depending upon the type.
    */
    void (*replay_net_transfer)(void* context, CPUState *env, uint32_t type, uint64_t src_addr, uint64_t dest_addr, size_t num_bytes);

    /* Callback ID:     PANDA_CB_REPLAY_SERIAL_RECEIVE,

        replay_serial_receive:
        In replay only, called when a byte is received on the serial port.

       Arguments:
        CPUState *env:          pointer to CPUState
        uint64_t fifo_addr: address of the data within the fifo
        uint8_t value:          value received

       Helper call location: panda/src/rr/rr_log.c

       Return value:
        unused
    */
    void (*replay_serial_receive)(void* context, CPUState *env, uint64_t fifo_addr, uint8_t value);

    /* Callback ID:     PANDA_CB_REPLAY_SERIAL_READ,

       replay_serial_read:
        In replay only, called when a byte read from the serial RX FIFO

       Arguments:
        CPUState *env:          pointer to CPUState
        uint64_t fifo_addr: address of the data within the fifo (source)
        uint32_t port_addr:     address of the IO port where data is being read (destination)
        uint8_t value:          value read

       Helper call location: panda/src/rr/rr_log.c

       Return value:
        none
    */
    void (*replay_serial_read)(void* context, CPUState *env, uint64_t fifo_addr, uint32_t port_addr, uint8_t value);

    /* Callback ID:     PANDA_CB_REPLAY_SERIAL_SEND,

       replay_serial_send:
        In replay only, called when a byte is sent on the serial port.

       Arguments:
        CPUState *env:          pointer to CPUState
        uint64_t fifo_addr: address of the data within the fifo
        uint8_t value:          value received

       Helper call location: panda/src/rr/rr_log.c

       Return value:
        none
    */
    void (*replay_serial_send)(void* context, CPUState *env, uint64_t fifo_addr, uint8_t value);

    /* Callback ID:     PANDA_CB_REPLAY_SERIAL_WRITE,

       In replay only, called when a byte written to the serial TX FIFO

       Arguments:
        CPUState *env:          pointer to CPUState
        uint64_t fifo_addr: address of the data within the fifo (source)
        uint32_t port_addr:     address of the IO port where data is being read (destination)
        uint8_t value:          value read

       Helper call location: panda/src/rr/rr_log.c

       Return value:
        none
    */
    void (*replay_serial_write)(void* context, CPUState *env, uint64_t fifo_addr, uint32_t port_addr, uint8_t value);

    /* Callback ID:     PANDA_CB_AFTER_MACHINE_INIT

       after_machine_init:
        Called right after the machine has been initialized, but before
        any guest code runs.

       Arguments:
        void *cpu_env: pointer to CPUState

       Helper call location: TBA

       Return value:
        none

       Notes:
        This callback allows initialization of components that need
        access to the RAM, CPU object, etc. E.g. for the taint2 plugin,
        this is the appropriate place to call taint2_enable_taint().
    */
    void (*after_machine_init)(void* context, CPUState *env);

    /* Callback ID:     PANDA_CB_AFTER_LOADVM

       after_loadvm:
        Called right after a snapshot has been loaded (either with loadvm or replay initialization),
        but before any guest code runs.

       Arguments:
        void *cpu_env: pointer to CPUState

       Return value:
        none

    */
    void (*after_loadvm)(void* context, CPUState *env);

    /* Callback ID:     PANDA_CB_TOP_LOOP

       top_loop:
        Called at the top of the loop that manages emulation.

       Arguments:
        CPUState *env:          pointer to CPUState

       Helper call location: cpus.c

       Return value:
        unused
     */
    void (*top_loop)(void* context, CPUState *env);
    /* Callback ID:     PANDA_CB_DURING_MACHINE_INIT

       during_machine_init: Called in the middle of machine initialization

       Arguments:
         MachineState *machine: pointer to the machine state

       Return value:
         None
     */

    void (*during_machine_init)(void* context, MachineState *machine);

    /* Callback ID:     PANDA_CB_MAIN_LOOP_WAIT

       main_loop_wait: Called in IO thread in place where monitor cmds are processed

       Arguments:
         None

       Return value:
         None
     */

    void (*main_loop_wait)(void* context);

    /* Callback ID:     PANDA_CB_PRE_SHUTDOWN

      pre_shutdown: Called just before qemu shuts down

       Arguments:
         None

       Return value:
         None
     */
    void (*pre_shutdown)(void* context);

    /* Callback ID:     PANDA_CB_UNASSIGNED_IO_READ

      unassigned_io_read: Called when the guest attempts to read from an unmapped peripheral via MMIO

       Arguments:
         pc: Guest program counter at time of write
         addr: Physical address written to
         size: Size of write
         val: Pointer to a buffer that will be passed to the guest as the result of the read

       Return value:
         True if value read was changed by a PANDA plugin and should be returned
         False if error-logic (invalid write) should be run
     */
    bool (*unassigned_io_read)(void* context, CPUState *env, uint64_t pc, hwaddr addr, size_t size, uint64_t *val);

    /* Callback ID:     PANDA_CB_UNASSIGNED_IO_WRITE

      unassigned_io_write: Called when the guest attempts to write to an unmapped peripheral via MMIO

       Arguments:
         pc: Guest program counter at time of write
         addr: Physical address written to
         size: Size of write
         val: Data being written, up to 8 bytes

       Return value:
         True if the write should be allowed without error
         False if normal behavior should be used (error-logic)
     */
    bool (*unassigned_io_write)(void* context, CPUState *env, uint64_t pc, hwaddr addr, size_t size, uint64_t val);


    /* Callback ID:     PANDA_CB_BEFORE_HANDLE_EXCEPTION

       before_handle_exception: Called just before we are about to
       handle an exception.

       Note: only called for cpu->exception_index > 0

       Arguments:
         exception_index (the current exception number)

       Return value:
         a new exception_index.

       Note: There might be more than one callback for this location.
       First callback that returns an exception index that *differs*
       from the one passed as an arg wins. That is what we return as
       the new exception index, which will replace
       cpu->exception_index

     */

    int32_t (*before_handle_exception)(void* context, CPUState *cpu, int32_t exception_index);

    /* Callback ID:     PANDA_CB_BEFORE_HANDLE_INTERRUPT

       before_handle_interrupt: Called just before we are about to
       handle an interrupt.

       Arguments:
         interrupt request

       Return value:
         new interrupt_rquest

       Note: There might be more than one callback for this location.
       First callback that returns an interrupt_request that *differs*
       from the one passed as an arg wins.

     */


    int32_t (*before_handle_interrupt)(void* context, CPUState *cpu, int32_t interrupt_request);
    
    /* Callback ID: PANDA_CB_START_BLOCK_EXEC

       start_block_exec:
        This is like before_block_exec except its part of the TCG stream.

       Arguments:
        CPUState *env:        the current CPU state
        TranslationBlock *tb: the TB we are executing

       Helper call location: cpu-exec.c

       Return value:
        none
    */

    void (*start_block_exec)(void* context, CPUState *cpu, TranslationBlock* tb);

    /* Callback ID: PANDA_CB_END_BLOCK_EXEC

       end_block_exec:
        This is like after_block_exec except its part of the TCG stream.

       Arguments:
        CPUState *env:        the current CPU state
        TranslationBlock *tb: the TB we are executing

       Helper call location: cpu-exec.c

       Return value:
        none
    */
    void (*end_block_exec)(void* context, CPUState *cpu, TranslationBlock* tb);

    /* cbaddr is a dummy union member.

       This union only contains function pointers.
       Using the cbaddr member one can compare if two union instances
       point to the same callback function. In principle, any other
       member could be used instead.
       However, cbaddr provides neutral semantics for the comparisson.
    */

    void (*cbaddr)(void);

} panda_cb_with_context;

// END_PYPANDA_NEEDS_THIS -- do not delete this comment!

#ifdef __cplusplus
}
#endif
