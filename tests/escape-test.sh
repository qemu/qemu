#!/bin/bash

# Test ANSI escape sequences.

# Copyright (C) 2006 Stefan Weil

 # This program is free software; you can redistribute it and/or modify
 # it under the terms of the GNU General Public License as published by
 # the Free Software Foundation; either version 2 of the License, or
 # (at your option) any later version.

# Usage: bash escape-test.sh $LINES $COLUMNS
# (LINES, COLUMNS must contain reasonable values)

COLUMNS=$2
LINES=$1

fill_screen() {
  xline=""
  for (( i = 0; i < $COLUMNS; i = i + 1 )); do
    xline=${xline}x
  done

  echo -ne "\e[H"
  for (( i = 1; i <= $LINES; i = i + 1 )); do
    echo -ne "\e[$i;1H$xline"
  done
}

test_title() {
  echo -ne "\e[HTest: $1 (press enter to continue) "
}

prepare_next_test() {
  read -p ""
  fill_screen
  test_title "$1"
}

fill_screen
test_title "screen filled with x"

prepare_next_test "cursor line 1"
echo -ne "\e[1;${COLUMNS}H*"
sleep 5
prepare_next_test "cursor line $LINES"
echo -ne "\e[${LINES};${COLUMNS}H*"
sleep 5

prepare_next_test "cursor position"
echo -ne "\e[3;1Htext at row 3 column 1"
echo -ne "\e[4;6Htext at row 4 column 6"

prepare_next_test "text styles and colors"
echo -ne "\e[3;1Htext styles: "
echo -ne "\e[1mbold\e[m "
echo -ne "\e[2mfaint\e[m "
echo -ne "\e[3mitalic\e[m "
echo -ne "\e[4munderline\e[m "
echo -ne "\e[5mblink\e[m "
echo -ne "\e[6mrapid blink\e[m "
echo -ne "\e[7mreverse\e[m "
echo -ne "\e[4;1Hforeground:  "
echo -ne "\e[30mblack\e[m "
echo -ne "\e[31mred\e[m "
echo -ne "\e[32mgreen\e[m "
echo -ne "\e[33myellow\e[m "
echo -ne "\e[34mblue\e[m "
echo -ne "\e[35mmagenta\e[m "
echo -ne "\e[36mcyan\e[m "
echo -ne "\e[37mwhite\e[m "
echo -ne "\e[5;1Hbackground:  "
echo -ne "\e[40mblack\e[m "
echo -ne "\e[41mred\e[m "
echo -ne "\e[42mgreen\e[m "
echo -ne "\e[43myellow\e[m "
echo -ne "\e[44mblue\e[m "
echo -ne "\e[45mmagenta\e[m "
echo -ne "\e[46mcyan\e[m "
echo -ne "\e[47mwhite\e[m "

prepare_next_test "erase line"
echo -ne "\e[6;10Herase to end of line\e[0K"
echo -ne "\e[7;10Herase from beginning of line\e[7;9H\e[1K"
echo -ne "\e[8;10H\e[2Kerase entire line"

prepare_next_test "erase screen"
echo -ne "\e[6;10Herase to end of screen\e[0J"

prepare_next_test "erase screen"
echo -ne "\e[6;10Herase from beginning of screen\e[6;9H\e[1J"

prepare_next_test "erase screen"
echo -ne "\e[6;10H\e[2Jerase entire screen"

prepare_next_test "save / restore cursor position"
echo -ne "\e[6;10H\e[s               after save cursor\e[urestore cursor "

prepare_next_test "finished"
echo -ne "\e[1;1H\e[2J"

# eof
