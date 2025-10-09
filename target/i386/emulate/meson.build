emulator_files = files(
  'x86_decode.c',
  'x86_emu.c',
  'x86_flags.c',
)

i386_system_ss.add(when: [hvf, 'CONFIG_HVF'], if_true: emulator_files)
i386_system_ss.add(when: 'CONFIG_MSHV', if_true: emulator_files)
