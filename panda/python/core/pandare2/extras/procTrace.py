#!/usr/bin/env python3
'''
Create a graph of which processes run/ran over time.

Multiple modes supported:

1) Run on a live PANDA guest with python-based analysis - use the snake_hook plugin
    user@host:~/panda/panda/plugins/proc_trace$ $(python3 -m pandare.qcows x86_64) -panda snak_hook:files=graph.py
    root@guest:~# whoami
    root@guest:~# ls
    (qemu) quit
2) Run on a PANDA recording with python-based analysis
    user@host:~/panda/panda/plugins/proc_trace$ $(python3 -m pandare.qcows x86_64) -panda snak_hook:files=graph.py -replay /path/to/recording

3) Run on a live/recorded PANDA guest with C++ data collection, then render graph with python
    user@host:~/panda/panda/plugins/proc_trace$ $(python3 -m pandare.qcows x86_64) -panda proc_trace -plog /tmp/graph.plog [-replay ...]
    user@host:~/panda/panda/plugins/proc_trace$ python3 graph.py /tmp/graph.plog
'''

from pandare import PyPlugin

def render_graph(procinfo, time_data, total_insns, n_cols=120, show_ranges=True, show_graph=True):
    col_size = total_insns / n_cols
    pids = set([x for x,y in time_data]) # really a list of (pid, tid) tuples
    merged = {} # pid: [(True, 100), False, 9999)

    for pid in pids:
        on_off_times = []

        off_count = 0

        for (pid2, block_c) in time_data:
            if pid2 == pid:
                # On!
                on_off_times.append((True, block_c))
            else:
                # Off
                on_off_times.append((False, block_c))

        merged[pid] = on_off_times

    # Render output: Stage 1 - PID -> procname details
    #   Count   Pid              Name/tid              Asid    First         Last
    #    297  1355   [bash find  / 1355]          3b14e000   963083  ->  7829616

    if show_ranges:
        print(" Ins.Count PID   TID  First       Last     Names")

        for (pid, tid) in sorted(procinfo, key=lambda v: procinfo[v]['count'], reverse=True):
            details = procinfo[(pid, tid)]
            names = ", ".join([x.decode() for x in details['names']])

            end = f"{details['last']:<8}" if details['last'] is not None else "N/A"
            print(f"{details['count']: >10} {pid:<5} {tid:<5}{details['first']:<8} -> {end} {names}")


    # Render output: Stage 2: ascii art
    if show_graph:
        ascii_art = {} # (pid, tid): art
        for (pid, tid), times in merged.items():
            row = ""
            pending = None
            queue = merged[(pid, tid)]
            # Consume data from pending+merged in chunks of col_size
            # e.g. col_size=10 (True, 8), (False, 1), (True, 10)
            # simplifies to {True:9, False:1} and adds (True:9) to pending

            for cur_col in range(n_cols):
                counted = 0
                on_count = 0
                off_count = 0
                #import ipdb
                while (counted < col_size and len(queue)): #or pending is not None:
                    if pending is not None:
                        (on_bool, cnt) = pending
                        pending = None
                    else:
                        old_len = len(queue)
                        (on_bool, cnt) = queue.pop(0)
                        assert(len(queue) < old_len), "pop don't happen"

                    if cnt > col_size-counted: #Hard case: count part, move remainder to pending
                        remainder = cnt - (col_size-counted)
                        cnt = col_size-counted # Maximum allowed now
                        pending = (on_bool, remainder)

                    assert(cnt <= col_size-counted) # Now it's (always) the easy case for what's left
                    if on_bool:
                        on_count += cnt
                    else:
                        off_count += cnt
                    counted += cnt

                # /while
                # Use on_count and off_count to determine how to label this cell
                density_map = " ▂▃▄▅▆▇"
                on_count / col_size

                idx = round((on_count/col_size)*(len(density_map)-1))
                if idx == 0 and on_count > 0:
                    c = '.' # If any code executed, mark it
                else:
                    c = density_map[idx]
                row += c

            ascii_art[(pid, tid)] = row

        # Render art
        print("PID  TID  | "+ "-"*(n_cols//2-4) + "HISTORY" + "-"*(n_cols//2-4) + "| NAMES")
        for (pid, tid) in sorted(ascii_art, key=lambda x: x[0]):
            row = ascii_art[(pid, tid)]
            details = procinfo[(pid, tid)]
            names = ", ".join([x.decode() for x in details['names']])
            print(f"{pid: <4} {tid: <4} |{row}| {names}")

class ProcGraph(PyPlugin):
    def __init__(self, panda):
        # Data collection
        self.procinfo = {} # PID: info
        self.time_data = [] # [(PID, #blocks)]
        self.total_insns = 0
        self.n_insns = 0
        self.last_pid = None
        self.show_ranges = not self.get_arg("hide_ranges")
        self.show_graph = not self.get_arg("hide_graph")
        self.panda = panda

        # config option: number of columns
        self.n_cols = self.get_arg("cols") or 120

        @panda.cb_start_block_exec
        def sbe(cpu, tb):
            self.n_insns += tb.icount
            self.total_insns += tb.icount

        @panda.ppp("osi", "on_task_change")
        def task_change(cpu):
            proc = panda.plugins['osi'].get_current_process(cpu)
            thread = panda.plugins['osi'].get_current_thread(cpu)

            if proc == panda.ffi.NULL:
                print(f"Warning: Unable to identify process at {self.n_insns}")
                return
            if thread == panda.ffi.NULL:
                print(f"Warning: Unable to identify thread at {self.n_insns}")
                return

            proc_key = (proc.pid, thread.tid)
            if proc_key not in self.procinfo:
                self.procinfo[proc_key] = {"names": set(), #"tids": set(),
                                      "first": self.total_insns, "last": None,
                                      "count": 0}

            name = panda.ffi.string(proc.name)  if proc.name != panda.ffi.NULL else "(error)"
            self.procinfo[proc_key]["names"].add(name)

            # Update insn count for last process and indicate it (maybe) ends at total_insns-1
            if self.last_pid:
                # count since we last ran is it's old end value, minus where it just ended
                self.procinfo[self.last_pid]["count"] += (self.total_insns-1) - self.procinfo[self.last_pid]["last"]  \
                                                if self.procinfo[self.last_pid]["last"] is not None \
                                                else (self.total_insns-1) - self.procinfo[self.last_pid]["first"]
                self.procinfo[self.last_pid]["last"] = self.total_insns-1

            self.last_pid = proc_key

            self.time_data.append((proc_key, self.n_insns))
            self.n_insns = 0

    def uninit(self):
        render_graph(self.procinfo, self.time_data, self.total_insns, n_cols=self.n_cols, show_ranges=self.show_ranges, show_graph=self.show_graph)

        # Fully reset state
        self.panda.disable_ppp("task_change")
        self.procinfo = {} # PID: info
        self.time_data = [] # [(PID, #blocks)]
        self.total_insns = 0
        self.n_insns = 0
        self.last_pid = None

if __name__ == '__main__':
    import sys
    import os
    from pandare.plog_reader import PLogReader

    if len(sys.argv) != 2:
        raise ValueError("Usage: graph.py [pandalog]")

    pandalog_path = sys.argv[1]

    if not os.path.isfile(pandalog_path):
        raise ValueError(f"Pandalog not found: {pandalog_path}")

    procinfo = {}
    time_data = []
    total_insns = None
    last_pid = None

    with PLogReader(pandalog_path) as plr:
        for msg in plr:
            if msg.HasField('proc_trace'):
                pt = msg.proc_trace
                proc_key = (pt.pid, pt.tid)
                if proc_key not in procinfo:
                    procinfo[proc_key] = {
                        "names": set(),
                        "first": pt.start_instr,
                        "last": None,
                        "count": 1 # Ensure last value shows up, even though we don't know when it ends
                    }
                name = pt.name.encode()
                procinfo[proc_key]["names"].add(name)

                if last_pid:
                    procinfo[last_pid]["count"] += pt.start_instr - (procinfo[last_pid]["last"] if procinfo[last_pid]["last"] is not None else 0)
                    procinfo[last_pid]["last"] = pt.start_instr
                    if not total_insns or pt.start_instr > total_insns:
                        total_insns = pt.start_instr

                last_pid = proc_key

                time_data.append((proc_key, pt.start_instr))

    # Now procinfo and time_data populated, can call original graph render logic
    total_insns += 1 # Ensure we count the last insn
    render_graph(procinfo, time_data, total_insns, n_cols=120)
