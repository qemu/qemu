During emulation, if you are using a character backend multiplexer
(which is the default if you are using ``-nographic``) then several
commands are available via an escape sequence. These key sequences all
start with an escape character, which is :kbd:`Ctrl+a` by default, but can be
changed with ``-echr``. The list below assumes you're using the default.

.. list-table:: Multiplexer Keys
  :widths: 20 80
  :header-rows: 1

  * - Key Sequence
    - Action

  * - :kbd:`Ctrl+a h`
    - Print this help

  * - :kbd:`Ctrl+a x`
    - Exit emulator

  * - :kbd:`Ctrl+a s`
    - Save disk data back to file (if -snapshot)

  * - :kbd:`Ctrl+a t`
    - Toggle console timestamps

  * - :kbd:`Ctrl+a b`
    - Send break (magic sysrq in Linux)

  * - :kbd:`Ctrl+a c`
    - Rotate between the frontends connected to the multiplexer (usually this switches between the monitor and the console)

  * - :kbd:`Ctrl+a Ctrl+a`
    - Send the escape character to the frontend
