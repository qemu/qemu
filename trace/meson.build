softmmu_ss.add(files('trace-hmp-cmds.c'))

specific_ss.add(files('control-target.c'))

trace_events_files = []
foreach item : [ '.' ] + trace_events_subdirs + qapi_trace_events
  if item in qapi_trace_events
    trace_events_file = item
    group_name = item.full_path().split('/')[-1].underscorify()
  else
    trace_events_file = meson.project_source_root() / item / 'trace-events'
    group_name = item == '.' ? 'root' : item.underscorify()
  endif
  trace_events_files += [ trace_events_file ]
  group = '--group=' + group_name
  fmt = '@0@-' + group_name + '.@1@'

  trace_h = custom_target(fmt.format('trace', 'h'),
                          output: fmt.format('trace', 'h'),
                          input: trace_events_file,
                          command: [ tracetool, group, '--format=h', '@INPUT@', '@OUTPUT@' ],
                          depend_files: tracetool_depends)
  genh += trace_h
  trace_c = custom_target(fmt.format('trace', 'c'),
                          output: fmt.format('trace', 'c'),
                          input: trace_events_file,
                          command: [ tracetool, group, '--format=c', '@INPUT@', '@OUTPUT@' ],
                          depend_files: tracetool_depends)
  if 'ust' in get_option('trace_backends')
    trace_ust_h = custom_target(fmt.format('trace-ust', 'h'),
                                output: fmt.format('trace-ust', 'h'),
                                input: trace_events_file,
                                command: [ tracetool, group, '--format=ust-events-h', '@INPUT@', '@OUTPUT@' ],
                                depend_files: tracetool_depends)
    trace_ss.add(trace_ust_h, lttng)
    genh += trace_ust_h
  endif
  trace_ss.add(trace_h, trace_c)
  if 'dtrace' in get_option('trace_backends')
    trace_dtrace = custom_target(fmt.format('trace-dtrace', 'dtrace'),
                                 output: fmt.format('trace-dtrace', 'dtrace'),
                                 input: trace_events_file,
                                 command: [ tracetool, group, '--format=d', '@INPUT@', '@OUTPUT@' ],
                                 depend_files: tracetool_depends)
    trace_dtrace_h = custom_target(fmt.format('trace-dtrace', 'h'),
                                   output: fmt.format('trace-dtrace', 'h'),
                                   input: trace_dtrace,
                                   command: [ dtrace, '-DSTAP_SDT_V2', '-o', '@OUTPUT@', '-h', '-s', '@INPUT@' ])
    trace_ss.add(trace_dtrace_h)
    if host_machine.system() != 'darwin'
      trace_dtrace_o = custom_target(fmt.format('trace-dtrace', 'o'),
                                     output: fmt.format('trace-dtrace', 'o'),
                                     input: trace_dtrace,
                                     command: [ dtrace, '-DSTAP_SDT_V2', '-o', '@OUTPUT@', '-G', '-s', '@INPUT@' ])
      trace_ss.add(trace_dtrace_o)
    endif

    genh += trace_dtrace_h
  endif
endforeach

trace_events_all = custom_target('trace-events-all',
                                 output: 'trace-events-all',
                                 input: trace_events_files,
                                 command: [ 'cat', '@INPUT@' ],
                                 capture: true,
                                 install: true,
                                 install_dir: qemu_datadir)

if 'ust' in get_option('trace_backends')
  trace_ust_all_h = custom_target('trace-ust-all.h',
                                  output: 'trace-ust-all.h',
                                  input: trace_events_files,
                                  command: [ tracetool, '--group=all', '--format=ust-events-h', '@INPUT@', '@OUTPUT@' ],
                                  depend_files: tracetool_depends)
  trace_ust_all_c = custom_target('trace-ust-all.c',
                                  output: 'trace-ust-all.c',
                                  input: trace_events_files,
                                  command: [ tracetool, '--group=all', '--format=ust-events-c', '@INPUT@', '@OUTPUT@' ],
                                  depend_files: tracetool_depends)
  trace_ss.add(trace_ust_all_h, trace_ust_all_c)
  genh += trace_ust_all_h
endif

if 'simple' in get_option('trace_backends')
  trace_ss.add(files('simple.c'))
endif
if 'ftrace' in get_option('trace_backends')
  trace_ss.add(files('ftrace.c'))
endif
trace_ss.add(files('control.c'))
if have_system or have_tools or have_ga
  trace_ss.add(files('qmp.c'))
endif
