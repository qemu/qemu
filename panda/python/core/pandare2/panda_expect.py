""" Custom library for interacting/expecting data via serial-like FDs"""

import os
import re
import select
import sys
import string

from time import monotonic
from errno import EAGAIN, EWOULDBLOCK
from colorama import Fore, Style

class TimeoutExpired(Exception): pass

class Expect(object):
    '''
    Class to manage typing commands into consoles and waiting for responses.

    Designed to be used with the qemu monitor and serial consoles for Linux guests.

    '''
    def __init__(self, name, filelike=None, expectation=None, logfile_base=None, consume_first=False, unansi=False):
        '''
        To debug, set logfile_base to something like '/tmp/log' and then look at logs written to /tmp/log_monitor.txt and /tmp/log_serial.txt. Or directyl access
        '''

        self.name = name
        self.logfile = None

        if logfile_base:
            self.set_logging(f"{logfile_base}_{name}.txt")

        if filelike is None: # Must later use connect(filelike)
            self.fd = None
        else:
            self.connect(filelike)

        self.prior_lines = []
        self.current_line = bytearray()
        self.last_msg = None
        self.running = True
        self.cleared = False
        self.update_expectation(expectation)

        # If consumed_first is false, we'll consume a message before anything else. Requires self.expectation to be set
        self.consumed_first = not consume_first
        self.use_unansi = unansi

    def update_expectation(self, expectation):
        if isinstance(expectation, bytes):
            expectation = expectation.decode()
        self.last_prompt = expectation # approximation
        self.expectation_re = re.compile(expectation)
        self.expectation_ends_re = re.compile(r'(.*)' + expectation)

    def set_logging(self, name):
        self.logfile = open(name, "wb")

    def connect(self, filelike):
        if type(filelike) == int:
            self.fd = filelike
        else:
            self.fd = filelike.fileno()
        self.poller = select.poll()
        self.poller.register(self.fd, select.POLLIN)

    def is_connected(self):
        return self.fd != None

    def __del__(self):
        if self.logfile:
            self.logfile.close()

    def abort(self):
        self.running = False


    def consume_partial(self):
        '''
        Get the message so far and reset.
        To ensure that we're not consuming the final line at we just don't clear
        '''
        result = self.get_partial()
        self.prior_lines = []
        return result

    def get_partial(self):
        '''
        Get the message
        '''
        if len(self.prior_lines):
            return "\n".join(self.prior_lines)
        return ""

    def unansi(self):
        '''
        Take the string in self.current_line and any prior lines in self.prior_lines and render ANSI.
        prior lines should be plain strings while current_line may contain escapes

        Given a string with ansi control codes, emulate behavior to generate the resulting string. 

        First we split input into a list of ('fn', [args]) / ('text', ['foo']) ansi commands then
        evaluate the commands to render real text output

        See https://notes.burke.libbey.me/ansi-escape-codes/ and
        http://ascii-table.com/ansi-escape-sequences-vt-100.php for ansi escape code details
        '''

        # Join prior lines into a single text element in our reformatted list
        reformatted = []
        if len(self.prior_lines):
            reformatted = [('text', ['\n'.join(self.prior_lines)])]

        # Then split current line into the tuple format describe above
        msg = self.current_line

        if isinstance(msg, str):
            msg = msg.encode()

        # Check for simple case where no ansi is in line - if so just copy into prior_lines
        if b'\x1b' not in msg:
            text = "".join([chr(x) for x in msg]).strip()
            self.prior_lines.append(text)
            self.current_line = bytearray()
            return

        start_args = re.compile(br"^(\d+);")
        last_arg = re.compile(rb"^(\d+)")

        last_text = ""
        idx = 0 # XXX: mutates during loop
        while idx < len(msg):
            if msg[idx] != 0x1b:
                last_text += chr(msg[idx])
            else:
                if len(last_text):
                    reformatted.append(('text', [last_text]))
                    last_text = ""

                if idx+3 <= len(msg) and msg[idx+1] == ord('['):
                    args = []
                    shift = idx+2
                    arg_s = msg[shift:]
                    while start_args.match(arg_s):
                        arg = start_args.match(arg_s).groups()[0].decode()
                        args.append(arg)
                        shift += len(arg)+1 # for ;
                        arg_s = msg[shift:]

                    # Last arg is just #
                    if last_arg.match(arg_s):
                        arg = last_arg.match(arg_s).groups()[0].decode()
                        shift += len(arg)
                        args.append(arg)
                        arg_s = msg[shift:]

                    # Next is one char for cmd
                    cmd = chr(msg[shift])
                    reformatted.append((cmd, args))

                    idx = shift # final char
            idx += 1
        if len(last_text):
            reformatted.append(('text', [last_text]))

        # Now render it!
        # Note the very first line will \r to beginning, then 'C' forward to go past expect prompt

        lines_out = [" "*len(self.last_prompt)] # Starting point - it's an approximation since we don't know real current prompt
        cur_line = 0
        line_pos = len(self.last_prompt)
        store_ptr = (0, 0)

        def _dump(lines_out, cur_line, line_pos):
            print("-"*100)
            for idx, line in enumerate(lines_out):
                print("'", line, "'")
                if cur_line == idx:
                    print((" "*(line_pos-1) if line_pos > 0 else "") + "^")
            print("="*100)

        for idx, (typ, args) in enumerate(reformatted):
            #print(typ, args)
            if typ == 'text':
                n = args[0]
                for idx, char in enumerate(n):
                    if char == '\n':
                        cur_line += 1 
                        while cur_line >= len(lines_out):
                            lines_out.append("")
                    if char == '\r':
                        line_pos = 0
                        continue # Don't clobber old char

                    line = list(lines_out[cur_line])
                    if (line_pos) >= len(line):
                        line.append(char)
                    else:
                        #if line[line_pos] != ' ':
                        #    print("Replace", repr(line[line_pos]) , "with", repr(char))
                        line[line_pos] = char
                    lines_out[cur_line] = "".join(line)

                    if char not in ['\n', '\r']:
                        line_pos += 1

            else:
                args[:] = [int(x) for x in args]

                if typ == 'A':
                    if not len(args):
                        # Incomplete
                        continue

                    n = args[0]

                    if cur_line - n < 0: # Need to shift
                        cur_line = 0
                    else:
                        cur_line -= n
                    assert(cur_line >= 0)

                elif typ == 'B':
                    if not len(args):
                        # Incomplete
                        continue
                    n = args[0]
                    cur_line += n
                    while cur_line >= len(lines_out):
                        lines_out.append(" "*100)

                elif typ == 'D':
                    n = 1 # Default move left 1
                    if len(args):
                        n = args[0]

                    line_pos -= n
                    if line_pos < 0:
                        line_pos = 0
                    assert(line_pos >= 0)

                elif typ == 'C': # Move right
                    n = 1 # Default move 1
                    if len(args):
                        n = args[0]

                    line_pos += n
                    if line_pos > len(lines_out[cur_line])-1:
                        line_pos = len(lines_out)-1
                    assert(line_pos >= 0)

                elif typ == 'J':
                    # Optional arg 0, 1, 2
                    n = 0 # default
                    if len(args):
                        n = args[0]
                    if n == 0:
                        # clear down
                        lines_out = lines_out[:cur_line+1]
                    elif n == 1:
                        # clear up
                        lines_out = lines_out[cur_line:]
                    elif n == 2:
                        # clear everything
                        lines_out = [""]
                        cur_line = 0
                        line_pos = 0
                        store_ptr = (0, 0)

                elif typ == 'K':
                    # Optional arg 0, 1, 2
                    n = 0 # default
                    if len(args):
                        n = args[0]

                    # HURISTIC-y hack: linux loves to have a line 123456 then do K(0) 6\r\n
                    # so if the next line is text and the text is [eol]\r\n where [eol] matches the end of this
                    # line - align cur_pos
                    if len(reformatted) > idx+1: # Have another message
                        (next_typ, next_args) = reformatted[idx+1]
                        if next_typ == 'text':
                            if '\r\n' in next_args[0]:
                                next_lines = next_args[0].split("\r\n")
                                if lines_out[cur_line].strip().endswith(next_lines[0]):
                                    # Its the buggy case. Just align it such that we clear the text
                                    # that's about to get echoed
                                    line_pos = line_pos - len(next_lines[0])

                    if n == 0:
                        # clear right of cursor
                        lines_out[cur_line] = lines_out[cur_line][:line_pos]
                    elif n == 1:
                        # clear left of cursor
                        lines_out[cur_line] = (" "*line_pos)+lines_out[cur_line][line_pos:]
                    elif n == 2:
                        # clear whole line
                        lines_out[cur_line] = " "*len(lines_out[cur_line])

                elif typ == 'H':
                    n = args[0]-1
                    m = args[1]-1
                    cur_line = n
                    line_pos = m

                    while cur_line >= len(lines_out):
                        lines_out.append("")

                    while line_pos > len(lines_out[cur_line]):
                        lines_out[cur_line] += " "

                elif typ == 'T':
                    # Scroll window down
                    pass
                elif typ == 'S':
                    # Scroll window up
                    pass

                elif typ == 's':
                    store_ptr = (cur_line, line_pos)
                elif typ == 'u':
                    (cur_line, line_pos) = store_ptr

                elif typ == 'm':
                    # alter character attributes - just ignore
                    pass

                else:
                    raise ValueError(f"Unsupported ANSI command {typ}")
            #_dump(lines_out, cur_line, line_pos)

        # Done processing - update variables
        self.prior_lines = lines_out[:-1] # Strings
        self.current_line = bytearray() # Bytearray
        if len(lines_out[-1].strip()):
            self.current_line.append(lines_out[-1])

    def expect(self, expectation=None, timeout=30):
        '''
        Assumptions: as you send a command, the guest may send back
            The same command + ansi control codes.
            The epxectation value will show up on the start of a line.


        We add characters into current_line as we recv them. At each newline we
        1) Render ANSI control characters in current line (may affect prior lines)
        2) Check if the line we just parsed matches the provided expectation (if so we're done)
        3) Append current_line into prior_lines
        '''

        if expectation:
            raise ValueError("Deprecated interface - must set expectation in class init")

        if self.fd is None:
            raise RuntimeError("Must connect() prior to expect()")

        self.current_line = bytearray()
        start_time = monotonic()
        time_passed = 0
        while (timeout is None or time_passed < timeout) and self.running:
            if timeout is not None:
                time_passed = (monotonic() - start_time)
                time_left = timeout - time_passed
            else:
                time_left = float("inf")
            ready = self.poller.poll(min(time_left, 1))

            # Debug - flush debug logs
            if self.logfile:
                self.logfile.flush()

            if self.fd in [fd for (fd, _) in ready]:
                try:
                    char = os.read(self.fd, 1)
                except OSError as e:
                    if e.errno in [EAGAIN, EWOULDBLOCK]:
                        continue
                    else: raise

                self.current_line.extend(char)

                # Debugging - log current line to file
                if self.logfile:
                    self.logfile.write(("\n\n" + repr(self.prior_lines) + " Current line = " + repr(self.current_line)).encode())

                # Translate the current_line buffer into plaintext, then determine if we're finished (bc we see new prompt)
                # note this drops the echo'd command
                if self.current_line.endswith(b"\n"):
                    # End of line - need to potentially unansi and move into prior_lines
                    if self.use_unansi:
                        self.unansi()
                    else:
                        self.prior_lines.append(self.current_line[:-1].decode(errors='ignore'))
                        self.current_line = bytearray()

                    # Now we have command\nresults..........\nprompt
                    #self.logfile.write(b"\n UNANSIs to: " + repr(self.prior_lines).encode()+b"\n")


                #lines = [x.replace("\r", "") for x in plaintext.split("\n")]
                # Check current line to see if it ends with prompt (indicating we finished)
                # current_line is a bytearray. Need it as a string
                current_line_s = self.current_line.decode(errors='ignore')

                end_match = self.expectation_ends_re.match(current_line_s)
                if end_match is not None:
                    # This line matches the end regex - it's either like root@host:... or it's [output]root@host:...
                    # We'll use self.expectation_re on the current line to identify where the prompt is and grab any final output
                    final_output = end_match.groups(1)[0]
                    if len(final_output):
                        self.prior_lines.append(final_output)
                        current_line_s = current_line_s[len(final_output):]

                    # Note we may have a line like [output]root@.... in which case we need to identify where the prompt was
                    self.last_prompt = current_line_s

                    # Drop command we sent - note it won't be a direct match with last_cmd because of weird escape codes
                    # which are based on guest line position when printed - i.e., it would only be an exact
                    # match if we knew and included the prompt when the command was run. Let's just always drop it
                    if len(self.prior_lines) > 1:
                        self.prior_lines = self.prior_lines[1:]
                    else:
                        self.prior_lines = []

                    plaintext = "\n".join(self.prior_lines)
                    self.prior_lines = []
                    return plaintext

        if not self.running: # Aborted
            return None

        if self.logfile:
            self.logfile.flush()

        full_buffer = self.prior_lines + [self.current_line]
        raise TimeoutExpired(f"{self.name} Read message \n{full_buffer}\n")

    def send(self, msg):
        if not self.consumed_first: # Before we send anything, consume header
            pre = self.expect("")
            self.consumed_first = True

        # Newlines will call problems
        assert len(msg.decode(errors='ignore').split("\n")) <= 2, "Multiline cmds unsupported"
        self.last_msg = msg.decode(errors='ignore').replace("\n", "")
        os.write(self.fd, msg)
        if self.logfile:
            self.logfile.write(msg)
            self.logfile.flush()

    def send_eol(self): # Just send an EOL
        if self.last_msg:
            self.last_msg+="\n"
        os.write(self.fd, b"\n")
        if self.logfile:
            self.logfile.write(b"\n")
            self.logfile.flush()


    def sendline(self, msg=b""):
        assert(self.fd is not None), "Must connect before sending"
        self.send(msg + b"\n")

