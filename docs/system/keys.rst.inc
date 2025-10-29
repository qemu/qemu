During the graphical emulation, you can use special key combinations from
the following table to change modes. By default the modifier is :kbd:`Ctrl+Alt`
(used in the table below) which can be changed with ``-display`` suboption
``mod=`` where appropriate. For example, ``-display sdl,
grab-mod=lshift-lctrl-lalt`` changes the modifier key to :kbd:`Ctrl+Alt+Shift`,
while ``-display sdl,grab-mod=rctrl`` changes it to the right :kbd:`Ctrl` key.

.. list-table:: Multiplexer Keys
  :widths: 10 90
  :header-rows: 1

  * - Key Sequence
    - Action

  * - :kbd:`Ctrl+Alt+f`
    - Toggle full screen

  * - :kbd:`Ctrl+Alt++`
    - Enlarge the screen

  * - :kbd:`Ctrl+Alt+-`
    - Shrink the screen

  * - :kbd:`Ctrl+Alt+0`
    - Restore the screen's un-scaled dimensions

  * - :kbd:`Ctrl+Alt+n`
    - Switch to virtual console 'n'. Standard console mappings are:

      - *1*: Target system display
      - *2*: Monitor
      - *3*: Serial port
  * - :kbd:`Ctrl+Alt+g`
    - Toggle mouse and keyboard grab.

In the virtual consoles, you can use :kbd:`Ctrl+Up`, :kbd:`Ctrl+Down`, :kbd:`Ctrl+PageUp` and
:kbd:`Ctrl+PageDown` to move in the back log.
