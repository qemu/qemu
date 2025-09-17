# SPDX-License-Identifier: GPL-2.0-or-later

test_env = environment()
test_env.set('PYTHONPATH', meson.project_source_root() / 'scripts')
test_env.set('PYTHONIOENCODING', 'utf-8')

backends = [
    'dtrace',
    'ftrace',
    'log',
    'simple',
    'syslog',
    'ust'
]

# The tracetool-test.py program has portability problems on Windows.
if host_machine.system() != 'windows'
    foreach backend: backends
        test(backend,
             python,
             args: [meson.current_source_dir() / 'tracetool-test.py',
                    meson.project_source_root() / 'scripts' / 'tracetool.py',
                    backend,
                    meson.current_source_dir(),
                    meson.current_build_dir()],
             suite: ['tracetool'])
    endforeach
endif
