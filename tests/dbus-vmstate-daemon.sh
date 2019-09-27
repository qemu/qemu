#!/bin/sh

# dbus-daemon wrapper script for dbus-vmstate testing
#
# This script allows to tweak the dbus-daemon policy during the test
# to test different configurations.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
#
# Copyright (C) 2019 Red Hat, Inc.

write_config()
{
    CONF="$1"
    cat > "$CONF" <<EOF
<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=$DBUS_VMSTATE_TEST_TMPDIR</listen>

  <policy context="default">
     <!-- Holes must be punched in service configuration files for
          name ownership and sending method calls -->
     <deny own="*"/>
     <deny send_type="method_call"/>

     <!-- Signals and reply messages (method returns, errors) are allowed
          by default -->
     <allow send_type="signal"/>
     <allow send_requested_reply="true" send_type="method_return"/>
     <allow send_requested_reply="true" send_type="error"/>

     <!-- All messages may be received by default -->
     <allow receive_type="method_call"/>
     <allow receive_type="method_return"/>
     <allow receive_type="error"/>
     <allow receive_type="signal"/>

     <!-- Allow anyone to talk to the message bus -->
     <allow send_destination="org.freedesktop.DBus"
            send_interface="org.freedesktop.DBus" />
     <allow send_destination="org.freedesktop.DBus"
            send_interface="org.freedesktop.DBus.Introspectable"/>
     <allow send_destination="org.freedesktop.DBus"
            send_interface="org.freedesktop.DBus.Properties"/>
     <!-- But disallow some specific bus services -->
     <deny send_destination="org.freedesktop.DBus"
           send_interface="org.freedesktop.DBus"
           send_member="UpdateActivationEnvironment"/>
     <deny send_destination="org.freedesktop.DBus"
           send_interface="org.freedesktop.DBus.Debug.Stats"/>
     <deny send_destination="org.freedesktop.DBus"
           send_interface="org.freedesktop.systemd1.Activator"/>

     <allow own="org.qemu.VMState1"/>
     <allow send_destination="org.qemu.VMState1"/>
     <allow receive_sender="org.qemu.VMState1"/>

  </policy>

  <include if_selinux_enabled="yes"
   selinux_root_relative="yes">contexts/dbus_contexts</include>

</busconfig>
EOF
}

ARGS=
for arg in "$@"
do
    case $arg in
        --config-file=*)
          CONF="${arg#*=}"
          write_config "$CONF"
          ARGS="$ARGS $1"
          shift
        ;;
        *)
          ARGS="$ARGS $1"
          shift
        ;;
    esac
done

exec dbus-daemon $ARGS
