#!/usr/bin/env perl
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
use File::Basename;

my $buf = '';
my %seen = ();

sub out {
    print $buf;
    $buf = '';
    %seen = ();
}

$#ARGV == 0 or die "usage: $0 FILE";
my $in = $ARGV[0];
my $dir = dirname($in);
open(IN, $in) or die "open $in: $!";
chdir($dir) or die "chdir $dir: $!";

while (<IN>) {
    if (/^(disable |(tcg) |vcpu )*([a-z_0-9]+)\(/i) {
        my $pat = "trace_$3";
        $pat .= '_tcg' if (defined $2);
        open GREP, '-|', 'git', 'grep', '-lw', '--max-depth', '1', $pat
            or die "run git grep: $!";
        while (my $fname = <GREP>) {
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
close(IN) or die "close $in: $!";
