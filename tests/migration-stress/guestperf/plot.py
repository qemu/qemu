#
# Migration test graph plotting
#
# Copyright (c) 2016 Red Hat, Inc.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <http://www.gnu.org/licenses/>.
#

import sys


class Plot(object):

    # Generated using
    # http://tools.medialab.sciences-po.fr/iwanthue/
    COLORS = ["#CD54D0",
              "#79D94C",
              "#7470CD",
              "#D2D251",
              "#863D79",
              "#76DDA6",
              "#D4467B",
              "#61923D",
              "#CB9CCA",
              "#D98F36",
              "#8CC8DA",
              "#CE4831",
              "#5E7693",
              "#9B803F",
              "#412F4C",
              "#CECBA6",
              "#6D3229",
              "#598B73",
              "#C8827C",
              "#394427"]

    def __init__(self,
                 reports,
                 migration_iters,
                 total_guest_cpu,
                 split_guest_cpu,
                 qemu_cpu,
                 vcpu_cpu):

        self._reports = reports
        self._migration_iters = migration_iters
        self._total_guest_cpu = total_guest_cpu
        self._split_guest_cpu = split_guest_cpu
        self._qemu_cpu = qemu_cpu
        self._vcpu_cpu = vcpu_cpu
        self._color_idx = 0

    def _next_color(self):
        color = self.COLORS[self._color_idx]
        self._color_idx += 1
        if self._color_idx >= len(self.COLORS):
            self._color_idx = 0
        return color

    def _get_progress_label(self, progress):
        if progress:
            return "\n\n" + "\n".join(
                ["Status: %s" % progress._status,
                 "Iteration: %d" % progress._ram._iterations,
                 "Throttle: %02d%%" % progress._throttle_pcent,
                 "Dirty rate: %dMB/s" % (progress._ram._dirty_rate_pps * 4 / 1024.0)])
        else:
            return "\n\n" + "\n".join(
                ["Status: %s" % "none",
                 "Iteration: %d" % 0])

    def _find_start_time(self, report):
        startqemu = report._qemu_timings._records[0]._timestamp
        startguest = report._guest_timings._records[0]._timestamp
        if startqemu < startguest:
            return startqemu
        else:
            return stasrtguest

    def _get_guest_max_value(self, report):
        maxvalue = 0
        for record in report._guest_timings._records:
            if record._value > maxvalue:
                maxvalue = record._value
        return maxvalue

    def _get_qemu_max_value(self, report):
        maxvalue = 0
        oldvalue = None
        oldtime = None
        for record in report._qemu_timings._records:
            if oldvalue is not None:
                cpudelta = (record._value - oldvalue) / 1000.0
                timedelta = record._timestamp - oldtime
                if timedelta == 0:
                    continue
                util = cpudelta / timedelta * 100.0
            else:
                util = 0
            oldvalue = record._value
            oldtime = record._timestamp

            if util > maxvalue:
                maxvalue = util
        return maxvalue

    def _get_total_guest_cpu_graph(self, report, starttime):
        xaxis = []
        yaxis = []
        labels = []
        progress_idx = -1
        for record in report._guest_timings._records:
            while ((progress_idx + 1) < len(report._progress_history) and
                   report._progress_history[progress_idx + 1]._now < record._timestamp):
                progress_idx = progress_idx + 1

            if progress_idx >= 0:
                progress = report._progress_history[progress_idx]
            else:
                progress = None

            xaxis.append(record._timestamp - starttime)
            yaxis.append(record._value)
            labels.append(self._get_progress_label(progress))

        from plotly import graph_objs as go
        return go.Scatter(x=xaxis,
                          y=yaxis,
                          name="Guest PIDs: %s" % report._scenario._name,
                          mode='lines',
                          line={
                              "dash": "solid",
                              "color": self._next_color(),
                              "shape": "linear",
                              "width": 1
                          },
                          text=labels)

    def _get_split_guest_cpu_graphs(self, report, starttime):
        threads = {}
        for record in report._guest_timings._records:
            if record._tid in threads:
                continue
            threads[record._tid] = {
                "xaxis": [],
                "yaxis": [],
                "labels": [],
            }

        progress_idx = -1
        for record in report._guest_timings._records:
            while ((progress_idx + 1) < len(report._progress_history) and
                   report._progress_history[progress_idx + 1]._now < record._timestamp):
                progress_idx = progress_idx + 1

            if progress_idx >= 0:
                progress = report._progress_history[progress_idx]
            else:
                progress = None

            threads[record._tid]["xaxis"].append(record._timestamp - starttime)
            threads[record._tid]["yaxis"].append(record._value)
            threads[record._tid]["labels"].append(self._get_progress_label(progress))


        graphs = []
        from plotly import graph_objs as go
        for tid in threads.keys():
            graphs.append(
                go.Scatter(x=threads[tid]["xaxis"],
                           y=threads[tid]["yaxis"],
                           name="PID %s: %s" % (tid, report._scenario._name),
                           mode="lines",
                           line={
                               "dash": "solid",
                               "color": self._next_color(),
                               "shape": "linear",
                               "width": 1
                           },
                           text=threads[tid]["labels"]))
        return graphs

    def _get_migration_iters_graph(self, report, starttime):
        xaxis = []
        yaxis = []
        labels = []
        for progress in report._progress_history:
            xaxis.append(progress._now - starttime)
            yaxis.append(0)
            labels.append(self._get_progress_label(progress))

        from plotly import graph_objs as go
        return go.Scatter(x=xaxis,
                          y=yaxis,
                          text=labels,
                          name="Migration iterations",
                          mode="markers",
                          marker={
                              "color": self._next_color(),
                              "symbol": "star",
                              "size": 5
                          })

    def _get_qemu_cpu_graph(self, report, starttime):
        xaxis = []
        yaxis = []
        labels = []
        progress_idx = -1

        first = report._qemu_timings._records[0]
        abstimestamps = [first._timestamp]
        absvalues = [first._value]

        for record in report._qemu_timings._records[1:]:
            while ((progress_idx + 1) < len(report._progress_history) and
                   report._progress_history[progress_idx + 1]._now < record._timestamp):
                progress_idx = progress_idx + 1

            if progress_idx >= 0:
                progress = report._progress_history[progress_idx]
            else:
                progress = None

            oldvalue = absvalues[-1]
            oldtime = abstimestamps[-1]

            cpudelta = (record._value - oldvalue) / 1000.0
            timedelta = record._timestamp - oldtime
            if timedelta == 0:
                continue
            util = cpudelta / timedelta * 100.0

            abstimestamps.append(record._timestamp)
            absvalues.append(record._value)

            xaxis.append(record._timestamp - starttime)
            yaxis.append(util)
            labels.append(self._get_progress_label(progress))

        from plotly import graph_objs as go
        return go.Scatter(x=xaxis,
                          y=yaxis,
                          yaxis="y2",
                          name="QEMU: %s" % report._scenario._name,
                          mode='lines',
                          line={
                              "dash": "solid",
                              "color": self._next_color(),
                              "shape": "linear",
                              "width": 1
                          },
                          text=labels)

    def _get_vcpu_cpu_graphs(self, report, starttime):
        threads = {}
        for record in report._vcpu_timings._records:
            if record._tid in threads:
                continue
            threads[record._tid] = {
                "xaxis": [],
                "yaxis": [],
                "labels": [],
                "absvalue": [record._value],
                "abstime": [record._timestamp],
            }

        progress_idx = -1
        for record in report._vcpu_timings._records:
            while ((progress_idx + 1) < len(report._progress_history) and
                   report._progress_history[progress_idx + 1]._now < record._timestamp):
                progress_idx = progress_idx + 1

            if progress_idx >= 0:
                progress = report._progress_history[progress_idx]
            else:
                progress = None

            oldvalue = threads[record._tid]["absvalue"][-1]
            oldtime = threads[record._tid]["abstime"][-1]

            cpudelta = (record._value - oldvalue) / 1000.0
            timedelta = record._timestamp - oldtime
            if timedelta == 0:
                continue
            util = cpudelta / timedelta * 100.0
            if util > 100:
                util = 100

            threads[record._tid]["absvalue"].append(record._value)
            threads[record._tid]["abstime"].append(record._timestamp)

            threads[record._tid]["xaxis"].append(record._timestamp - starttime)
            threads[record._tid]["yaxis"].append(util)
            threads[record._tid]["labels"].append(self._get_progress_label(progress))


        graphs = []
        from plotly import graph_objs as go
        for tid in threads.keys():
            graphs.append(
                go.Scatter(x=threads[tid]["xaxis"],
                           y=threads[tid]["yaxis"],
                           yaxis="y2",
                           name="VCPU %s: %s" % (tid, report._scenario._name),
                           mode="lines",
                           line={
                               "dash": "solid",
                               "color": self._next_color(),
                               "shape": "linear",
                               "width": 1
                           },
                           text=threads[tid]["labels"]))
        return graphs

    def _generate_chart_report(self, report):
        graphs = []
        starttime = self._find_start_time(report)
        if self._total_guest_cpu:
            graphs.append(self._get_total_guest_cpu_graph(report, starttime))
        if self._split_guest_cpu:
            graphs.extend(self._get_split_guest_cpu_graphs(report, starttime))
        if self._qemu_cpu:
            graphs.append(self._get_qemu_cpu_graph(report, starttime))
        if self._vcpu_cpu:
            graphs.extend(self._get_vcpu_cpu_graphs(report, starttime))
        if self._migration_iters:
            graphs.append(self._get_migration_iters_graph(report, starttime))
        return graphs

    def _generate_annotation(self, starttime, progress):
        return {
            "text": progress._status,
            "x": progress._now - starttime,
            "y": 10,
        }

    def _generate_annotations(self, report):
        starttime = self._find_start_time(report)
        annotations = {}
        started = False
        for progress in report._progress_history:
            if progress._status == "setup":
                continue
            if progress._status not in annotations:
                annotations[progress._status] = self._generate_annotation(starttime, progress)

        return annotations.values()

    def _generate_chart(self):
        from plotly.offline import plot
        from plotly import graph_objs as go

        graphs = []
        yaxismax = 0
        yaxismax2 = 0
        for report in self._reports:
            graphs.extend(self._generate_chart_report(report))

            maxvalue = self._get_guest_max_value(report)
            if maxvalue > yaxismax:
                yaxismax = maxvalue

            maxvalue = self._get_qemu_max_value(report)
            if maxvalue > yaxismax2:
                yaxismax2 = maxvalue

        yaxismax += 100
        if not self._qemu_cpu:
            yaxismax2 = 110
        yaxismax2 += 10

        annotations = []
        if self._migration_iters:
            for report in self._reports:
                annotations.extend(self._generate_annotations(report))

        layout = go.Layout(title="Migration comparison",
                           xaxis={
                               "title": "Wallclock time (secs)",
                               "showgrid": False,
                           },
                           yaxis={
                               "title": "Memory update speed (ms/GB)",
                               "showgrid": False,
                               "range": [0, yaxismax],
                           },
                           yaxis2={
                               "title": "Hostutilization (%)",
                               "overlaying": "y",
                               "side": "right",
                               "range": [0, yaxismax2],
                               "showgrid": False,
                           },
                           annotations=annotations)

        figure = go.Figure(data=graphs, layout=layout)

        return plot(figure,
                    show_link=False,
                    include_plotlyjs=False,
                    output_type="div")


    def _generate_report(self):
        pieces = []
        for report in self._reports:
            pieces.append("""
<h3>Report %s</h3>
<table>
""" % report._scenario._name)

            pieces.append("""
  <tr class="subhead">
    <th colspan="2">Test config</th>
  </tr>
  <tr>
    <th>Emulator:</th>
    <td>%s</td>
  </tr>
  <tr>
    <th>Kernel:</th>
    <td>%s</td>
  </tr>
  <tr>
    <th>Ramdisk:</th>
    <td>%s</td>
  </tr>
  <tr>
    <th>Transport:</th>
    <td>%s</td>
  </tr>
  <tr>
    <th>Host:</th>
    <td>%s</td>
  </tr>
""" % (report._binary, report._kernel,
       report._initrd, report._transport, report._dst_host))

            hardware = report._hardware
            pieces.append("""
  <tr class="subhead">
    <th colspan="2">Hardware config</th>
  </tr>
  <tr>
    <th>CPUs:</th>
    <td>%d</td>
  </tr>
  <tr>
    <th>RAM:</th>
    <td>%d GB</td>
  </tr>
  <tr>
    <th>Source CPU bind:</th>
    <td>%s</td>
  </tr>
  <tr>
    <th>Source RAM bind:</th>
    <td>%s</td>
  </tr>
  <tr>
    <th>Dest CPU bind:</th>
    <td>%s</td>
  </tr>
  <tr>
    <th>Dest RAM bind:</th>
    <td>%s</td>
  </tr>
  <tr>
    <th>Preallocate RAM:</th>
    <td>%s</td>
  </tr>
  <tr>
    <th>Locked RAM:</th>
    <td>%s</td>
  </tr>
  <tr>
    <th>Huge pages:</th>
    <td>%s</td>
  </tr>
""" % (hardware._cpus, hardware._mem,
       ",".join(hardware._src_cpu_bind),
       ",".join(hardware._src_mem_bind),
       ",".join(hardware._dst_cpu_bind),
       ",".join(hardware._dst_mem_bind),
       "yes" if hardware._prealloc_pages else "no",
       "yes" if hardware._locked_pages else "no",
       "yes" if hardware._huge_pages else "no"))

            scenario = report._scenario
            pieces.append("""
  <tr class="subhead">
    <th colspan="2">Scenario config</th>
  </tr>
  <tr>
    <th>Max downtime:</th>
    <td>%d milli-sec</td>
  </tr>
  <tr>
    <th>Max bandwidth:</th>
    <td>%d MB/sec</td>
  </tr>
  <tr>
    <th>Max iters:</th>
    <td>%d</td>
  </tr>
  <tr>
    <th>Max time:</th>
    <td>%d secs</td>
  </tr>
  <tr>
    <th>Pause:</th>
    <td>%s</td>
  </tr>
  <tr>
    <th>Pause iters:</th>
    <td>%d</td>
  </tr>
  <tr>
    <th>Post-copy:</th>
    <td>%s</td>
  </tr>
  <tr>
    <th>Post-copy iters:</th>
    <td>%d</td>
  </tr>
  <tr>
    <th>Auto-converge:</th>
    <td>%s</td>
  </tr>
  <tr>
    <th>Auto-converge iters:</th>
    <td>%d</td>
  </tr>
  <tr>
    <th>MT compression:</th>
    <td>%s</td>
  </tr>
  <tr>
    <th>MT compression threads:</th>
    <td>%d</td>
  </tr>
  <tr>
    <th>XBZRLE compression:</th>
    <td>%s</td>
  </tr>
  <tr>
    <th>XBZRLE compression cache:</th>
    <td>%d%% of RAM</td>
  </tr>
""" % (scenario._downtime, scenario._bandwidth,
       scenario._max_iters, scenario._max_time,
       "yes" if scenario._pause else "no", scenario._pause_iters,
       "yes" if scenario._post_copy else "no", scenario._post_copy_iters,
       "yes" if scenario._auto_converge else "no", scenario._auto_converge_step,
       "yes" if scenario._compression_mt else "no", scenario._compression_mt_threads,
       "yes" if scenario._compression_xbzrle else "no", scenario._compression_xbzrle_cache))

            pieces.append("""
</table>
""")

        return "\n".join(pieces)

    def _generate_style(self):
        return """
#report table tr th {
    text-align: right;
}
#report table tr td {
    text-align: left;
}
#report table tr.subhead th {
    background: rgb(192, 192, 192);
    text-align: center;
}

"""

    def generate_html(self, fh):
        print("""<html>
  <head>
    <script type="text/javascript" src="plotly.min.js">
    </script>
    <style type="text/css">
%s
    </style>
    <title>Migration report</title>
  </head>
  <body>
    <h1>Migration report</h1>
    <h2>Chart summary</h2>
    <div id="chart">
""" % self._generate_style(), file=fh)
        print(self._generate_chart(), file=fh)
        print("""
    </div>
    <h2>Report details</h2>
    <div id="report">
""", file=fh)
        print(self._generate_report(), file=fh)
        print("""
    </div>
  </body>
</html>
""", file=fh)

    def generate(self, filename):
        if filename is None:
            self.generate_html(sys.stdout)
        else:
            with open(filename, "w") as fh:
                self.generate_html(fh)
