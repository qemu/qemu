#!/usr/bin/perl
# Copyright (C) 2013 Red Hat, Inc.
#
# Authors:
#  Markus Armbruster <armbru@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

# Usage: cleanup-trace-events.pl trace-events
#
# Print cleaned up trace-events to standard output.

use warnings;
use strict;

my $buf = '';
my %seen = ();

sub out {
    print $buf;
    $buf = '';
    %seen = ();
}

while (<>) {
    if (/^(disable )?([a-z_0-9]+)\(/) {
        open GREP, '-|', 'git', 'grep', '-l', "trace_$2"
            or die "run git grep: $!";
        my $fname;
        while ($fname = <GREP>) {
            chomp $fname;
            next if $seen{$fname} || $fname eq 'trace-events';
            $seen{$fname} = 1;
            $buf = "# $fname\n" . $buf;
        }
        unless (close GREP) {
            die "close git grep: $!"
                if $!;
            next;
        }
    } elsif (/^# ([^ ]*\.[ch])$/) {
        out;
        next;
    } elsif (!/^#|^$/) {
        warn "unintelligible line";
    }
    $buf .= $_;
}

out;
