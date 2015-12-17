#
# This script needs to be run on startup
# qemu -kernel ${KERNEL} -s -S
# and then:
# gdb ${KERNEL}.vmlinux -x ${QEMU_SRC}/tests/guest-debug/test-gdbstub.py

import gdb

failcount = 0


def report(cond, msg):
    "Report success/fail of test"
    if cond:
        print ("PASS: %s" % (msg))
    else:
        print ("FAIL: %s" % (msg))
        failcount += 1


def check_step():
    "Step an instruction, check it moved."
    start_pc = gdb.parse_and_eval('$pc')
    gdb.execute("si")
    end_pc = gdb.parse_and_eval('$pc')

    return not (start_pc == end_pc)


def check_break(sym_name):
    "Setup breakpoint, continue and check we stopped."
    sym, ok = gdb.lookup_symbol(sym_name)
    bp = gdb.Breakpoint(sym_name)

    gdb.execute("c")

    # hopefully we came back
    end_pc = gdb.parse_and_eval('$pc')
    print ("%s == %s %d" % (end_pc, sym.value(), bp.hit_count))
    bp.delete()

    # can we test we hit bp?
    return end_pc == sym.value()


# We need to do hbreak manually as the python interface doesn't export it
def check_hbreak(sym_name):
    "Setup hardware breakpoint, continue and check we stopped."
    sym, ok = gdb.lookup_symbol(sym_name)
    gdb.execute("hbreak %s" % (sym_name))
    gdb.execute("c")

    # hopefully we came back
    end_pc = gdb.parse_and_eval('$pc')
    print ("%s == %s" % (end_pc, sym.value()))

    if end_pc == sym.value():
        gdb.execute("d 1")
        return True
    else:
        return False


class WatchPoint(gdb.Breakpoint):

    def get_wpstr(self, sym_name):
        "Setup sym and wp_str for given symbol."
        self.sym, ok = gdb.lookup_symbol(sym_name)
        wp_addr = gdb.parse_and_eval(sym_name).address
        self.wp_str = '*(%(type)s)(&%(address)s)' % dict(
            type = wp_addr.type, address = sym_name)

        return(self.wp_str)

    def __init__(self, sym_name, type):
        wp_str = self.get_wpstr(sym_name)
        super(WatchPoint, self).__init__(wp_str, gdb.BP_WATCHPOINT, type)

    def stop(self):
        end_pc = gdb.parse_and_eval('$pc')
        print ("HIT WP @ %s" % (end_pc))
        return True


def do_one_watch(sym, wtype, text):

    wp = WatchPoint(sym, wtype)
    gdb.execute("c")
    report_str = "%s for %s (%s)" % (text, sym, wp.sym.value())

    if wp.hit_count > 0:
        report(True, report_str)
        wp.delete()
    else:
        report(False, report_str)


def check_watches(sym_name):
    "Watch a symbol for any access."

    # Should hit for any read
    do_one_watch(sym_name, gdb.WP_ACCESS, "awatch")

    # Again should hit for reads
    do_one_watch(sym_name, gdb.WP_READ, "rwatch")

    # Finally when it is written
    do_one_watch(sym_name, gdb.WP_WRITE, "watch")


class CatchBreakpoint(gdb.Breakpoint):
    def __init__(self, sym_name):
        super(CatchBreakpoint, self).__init__(sym_name)
        self.sym, ok = gdb.lookup_symbol(sym_name)

    def stop(self):
        end_pc = gdb.parse_and_eval('$pc')
        print ("CB: %s == %s" % (end_pc, self.sym.value()))
        if end_pc == self.sym.value():
            report(False, "Hit final catchpoint")


def run_test():
    "Run throught the tests one by one"

    print ("Checking we can step the first few instructions")
    step_ok = 0
    for i in range(3):
        if check_step():
            step_ok += 1

    report(step_ok == 3, "single step in boot code")

    print ("Checking HW breakpoint works")
    break_ok = check_hbreak("kernel_init")
    report(break_ok, "hbreak @ kernel_init")

    # Can't set this up until we are in the kernel proper
    # if we make it to run_init_process we've over-run and
    # one of the tests failed
    print ("Setup catch-all for run_init_process")
    cbp = CatchBreakpoint("run_init_process")
    cpb2 = CatchBreakpoint("try_to_run_init_process")

    print ("Checking Normal breakpoint works")
    break_ok = check_break("wait_for_completion")
    report(break_ok, "break @ wait_for_completion")

    print ("Checking watchpoint works")
    check_watches("system_state")

#
# This runs as the script it sourced (via -x)
#

try:
    print ("Connecting to remote")
    gdb.execute("target remote localhost:1234")

    # These are not very useful in scripts
    gdb.execute("set pagination off")
    gdb.execute("set confirm off")

    # Run the actual tests
    run_test()

except:
    print ("GDB Exception: %s" % (sys.exc_info()[0]))
    failcount += 1
    import code
    code.InteractiveConsole(locals=globals()).interact()
    raise

# Finally kill the inferior and exit gdb with a count of failures
gdb.execute("kill")
exit(failcount)
