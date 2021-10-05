
specific_ss.add(files('control-target.c'))

trace_events_files = []
dtrace = find_program('dtrace', required: 'CONFIG_TRACE_DTRACE' in config_host)
foreach dir : [ '.' ] + trace_events_subdirs
  trace_events_file = meson.project_source_root() / dir / 'trace-events'
  trace_events_files += [ trace_events_file ]
  group_name = dir == '.' ? 'root' : dir.underscorify()
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
  if 'CONFIG_TRACE_UST' in config_host
    trace_ust_h = custom_target(fmt.format('trace-ust', 'h'),
                                output: fmt.format('trace-ust', 'h'),
                                input: trace_events_file,
                                command: [ tracetool, group, '--format=ust-events-h', '@INPUT@', '@OUTPUT@' ],
                                depend_files: tracetool_depends)
    trace_ss.add(trace_ust_h, lttng)
    genh += trace_ust_h
  endif
  trace_ss.add(trace_h, trace_c)
  if 'CONFIG_TRACE_DTRACE' in config_host
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

foreach d : [
  ['generated-tcg-tracers.h', 'tcg-h'],
  ['generated-helpers.c', 'tcg-helper-c'],
  ['generated-helpers.h', 'tcg-helper-h'],
  ['generated-helpers-wrappers.h', 'tcg-helper-wrapper-h'],
]
  gen = custom_target(d[0],
                output: d[0],
                input: meson.project_source_root() / 'trace-events',
                command: [ tracetool, '--group=root', '--format=@0@'.format(d[1]), '@INPUT@', '@OUTPUT@' ],
                depend_files: tracetool_depends)
  specific_ss.add(when: 'CONFIG_TCG', if_true: gen)
endforeach

if 'CONFIG_TRACE_UST' in config_host
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

trace_ss.add(when: 'CONFIG_TRACE_SIMPLE', if_true: files('simple.c'))
trace_ss.add(when: 'CONFIG_TRACE_FTRACE', if_true: files('ftrace.c'))
trace_ss.add(files('control.c'))
trace_ss.add(files('qmp.c'))
