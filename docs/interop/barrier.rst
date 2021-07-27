Barrier client protocol
=======================

QEMU's ``input-barrier`` device implements the client end of
the KVM (Keyboard-Video-Mouse) software
`Barrier <https://github.com/debauchee/barrier>`__.

This document briefly describes the protocol as we implement it.

Message format
--------------

Message format between the server and client is in two parts:

#. the payload length, a 32bit integer in network endianness
#. the payload

The payload starts with a 4byte string (without NUL) which is the
command. The first command between the server and the client
is the only command not encoded on 4 bytes ("Barrier").
The remaining part of the payload is decoded according to the command.

Protocol Description
--------------------

This comes from ``barrier/src/lib/barrier/protocol_types.h``.

barrierCmdHello  "Barrier"
^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  ``{ int16_t minor, int16_t major }``
Description:
  Say hello to client

  ``minor`` = protocol major version number supported by server

  ``major`` = protocol minor version number supported by server

barrierCmdHelloBack  "Barrier"
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  client ->server
Parameters:
  ``{ int16_t minor, int16_t major, char *name}``
Description:
  Respond to hello from server

  ``minor`` = protocol major version number supported by client

  ``major`` = protocol minor version number supported by client

  ``name``  = client name

barrierCmdDInfo  "DINF"
^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  client ->server
Parameters:
  ``{ int16_t x_origin, int16_t y_origin, int16_t width, int16_t height, int16_t x, int16_t y}``
Description:
  The client screen must send this message in response to the
  barrierCmdQInfo message.  It must also send this message when the
  screen's resolution changes.  In this case, the client screen should
  ignore any barrierCmdDMouseMove messages until it receives a
  barrierCmdCInfoAck in order to prevent attempts to move the mouse off
  the new screen area.

barrierCmdCNoop  "CNOP"
^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  client -> server
Parameters:
  None
Description:
  No operation

barrierCmdCClose "CBYE"
^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  None
Description:
  Close connection

barrierCmdCEnter "CINN"
^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  ``{ int16_t x, int16_t y, int32_t seq, int16_t modifier }``
Description:
  Enter screen.

  ``x``, ``y``  = entering screen absolute coordinates

  ``seq``  = sequence number, which is used to order messages between
  screens.  the secondary screen must return this number
  with some messages

  ``modifier`` = modifier key mask.  this will have bits set for each
  toggle modifier key that is activated on entry to the
  screen.  the secondary screen should adjust its toggle
  modifiers to reflect that state.

barrierCmdCLeave "COUT"
^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  None
Description:
  Leaving screen.  the secondary screen should send clipboard data in
  response to this message for those clipboards that it has grabbed
  (i.e. has sent a barrierCmdCClipboard for and has not received a
  barrierCmdCClipboard for with a greater sequence number) and that
  were grabbed or have changed since the last leave.

barrierCmdCClipboard "CCLP"
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  ``{ int8_t id, int32_t seq }``
Description:
  Grab clipboard. Sent by screen when some other app on that screen
  grabs a clipboard.

  ``id``  = the clipboard identifier

  ``seq`` = sequence number. Client must use the sequence number passed in
  the most recent barrierCmdCEnter.  the server always sends 0.

barrierCmdCScreenSaver   "CSEC"
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  ``{ int8_t started }``
Description:
  Screensaver change.

  ``started`` = Screensaver on primary has started (1) or closed (0)

barrierCmdCResetOptions  "CROP"
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  None
Description:
  Reset options. Client should reset all of its options to their
  defaults.

barrierCmdCInfoAck   "CIAK"
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  None
Description:
  Resolution change acknowledgment. Sent by server in response to a
  client screen's barrierCmdDInfo. This is sent for every
  barrierCmdDInfo, whether or not the server had sent a barrierCmdQInfo.

barrierCmdCKeepAlive "CALV"
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  None
Description:
  Keep connection alive. Sent by the server periodically to verify
  that connections are still up and running.  clients must reply in
  kind on receipt.  if the server gets an error sending the message or
  does not receive a reply within a reasonable time then the server
  disconnects the client.  if the client doesn't receive these (or any
  message) periodically then it should disconnect from the server.  the
  appropriate interval is defined by an option.

barrierCmdDKeyDown   "DKDN"
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  ``{ int16_t keyid, int16_t modifier [,int16_t button] }``
Description:
  Key pressed.

  ``keyid`` = X11 key id

  ``modified`` = modified mask

  ``button`` = X11 Xkb keycode (optional)

barrierCmdDKeyRepeat "DKRP"
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  ``{ int16_t keyid, int16_t modifier, int16_t repeat [,int16_t button] }``
Description:
  Key auto-repeat.

  ``keyid`` = X11 key id

  ``modified`` = modified mask

  ``repeat``   = number of repeats

  ``button``   = X11 Xkb keycode (optional)

barrierCmdDKeyUp "DKUP"
^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  ``{ int16_t keyid, int16_t modifier [,int16_t button] }``
Description:
  Key released.

  ``keyid`` = X11 key id

  ``modified`` = modified mask

  ``button`` = X11 Xkb keycode (optional)

barrierCmdDMouseDown "DMDN"
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  ``{ int8_t button }``
Description:
  Mouse button pressed.

  ``button`` = button id

barrierCmdDMouseUp   "DMUP"
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  ``{ int8_t button }``
Description:
  Mouse button release.

  ``button`` = button id

barrierCmdDMouseMove "DMMV"
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  ``{ int16_t x, int16_t y }``
Description:
  Absolute mouse moved.

  ``x``, ``y`` = absolute screen coordinates

barrierCmdDMouseRelMove  "DMRM"
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  ``{ int16_t x, int16_t y }``
Description:
  Relative mouse moved.

  ``x``, ``y`` = r relative screen coordinates

barrierCmdDMouseWheel "DMWM"
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  ``{ int16_t x , int16_t y }`` or ``{ int16_t y }``
Description:
  Mouse scroll. The delta should be +120 for one tick forward (away
  from the user) or right and -120 for one tick backward (toward the
  user) or left.

  ``x`` = x delta

  ``y`` = y delta

barrierCmdDClipboard "DCLP"
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  ``{ int8_t id, int32_t seq, int8_t mark, char *data }``
Description:
  Clipboard data.

  ``id``  = clipboard id

  ``seq`` = sequence number. The sequence number is 0 when sent by the
  server.  Client screens should use the/ sequence number from
  the most recent barrierCmdCEnter.

barrierCmdDSetOptions "DSOP"
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  ``{ int32 t nb, { int32_t id, int32_t val }[] }``
Description:
  Set options. Client should set the given option/value pairs.

  ``nb``  = numbers of ``{ id, val }`` entries

  ``id``  = option id

  ``val`` = option new value

barrierCmdDFileTransfer "DFTR"
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  ``{ int8_t mark, char *content }``
Description:
  Transfer file data.

  * ``mark`` = 0 means the content followed is the file size
  * 1 means the content followed is the chunk data
  * 2 means the file transfer is finished

barrierCmdDDragInfo  "DDRG"
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  ``{ int16_t nb, char *content }``
Description:
  Drag information.

  ``nb``  = number of dragging objects

  ``content`` = object's directory

barrierCmdQInfo  "QINF"
^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  None
Description:
  Query screen info

  Client should reply with a barrierCmdDInfo

barrierCmdEIncompatible  "EICV"
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  ``{ int16_t nb, major *minor }``
Description:
  Incompatible version.

  ``major`` = major version

  ``minor`` = minor version

barrierCmdEBusy  "EBSY"
^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  None
Description:
  Name provided when connecting is already in use.

barrierCmdEUnknown   "EUNK"
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  None
Description:
  Unknown client. Name provided when connecting is not in primary's
  screen configuration map.

barrierCmdEBad   "EBAD"
^^^^^^^^^^^^^^^^^^^^^^^

Direction:
  server -> client
Parameters:
  None
Description:
  Protocol violation. Server should disconnect after sending this
  message.

