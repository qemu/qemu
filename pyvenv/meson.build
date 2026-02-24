# Note that this file only controls "optional" dependency groups; groups
# unconditionally installed at configure time are handled in configure
# instead; namely: meson, sphinx, and the tooling groups.

# Note that this command may or may not include the "--online" flag
# based on configuration.
ensuregroup_cmd = config_host['MKVENV_ENSUREGROUP'].split()

pyvenv_common_deps = files(
    meson.project_source_root() + '/pythondeps.toml',
    meson.project_source_root() + '/python/scripts/mkvenv.py'
)
pyvenv_wheel_dir = meson.project_source_root() + '/python/wheels'

# This group is allowed to (and must) rely on internet to fetch from
# PyPI. Force the use of the --online flag here.
pyvenv_functests_group = custom_target(
    'pyvenv_functests_group',
    output: 'functests.group',
    input: pyvenv_common_deps,
    command: ensuregroup_cmd + [
        '--online',
        '--dir', pyvenv_wheel_dir,
        '@INPUT0@',
        'functests',
    ],
)
