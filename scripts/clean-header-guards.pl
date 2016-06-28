#!/usr/bin/perl -w
#
# Clean up include guards in headers
#
# Copyright (C) 2016 Red Hat, Inc.
#
# Authors:
#  Markus Armbruster <armbru@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# (at your option) any later version. See the COPYING file in the
# top-level directory.
#
# Usage: scripts/clean-header-guards.pl [OPTION]... [FILE]...
#     -c CC     Use a compiler other than cc
#     -n        Suppress actual cleanup
#     -v        Show which files are cleaned up, and which are skipped
#
# Does the following:
# - Header files without a recognizable header guard are skipped.
# - Clean up any untidy header guards in-place.  Warn if the cleanup
#   renames guard symbols, and explain how to find occurences of these
#   symbols that may have to be updated manually.
# - Warn about duplicate header guard symbols.  To make full use of
#   this warning, you should clean up *all* headers in one run.
# - Warn when preprocessing a header with its guard symbol defined
#   produces anything but whitespace.  The preprocessor is run like
#   "cc -E -DGUARD_H -c -P -", and fed the test program on stdin.

use strict;
use Getopt::Std;

# Stuff we don't want to clean because we import it into our tree:
my $exclude = qr,^(disas/libvixl/|include/standard-headers/
    |linux-headers/|pc-bios/|tests/tcg/|tests/multiboot/),x;
# Stuff that is expected to fail the preprocessing test:
my $exclude_cpp = qr,^include/libdecnumber/decNumberLocal.h,;

my %guarded = ();
my %old_guard = ();

our $opt_c = "cc";
our $opt_n = 0;
our $opt_v = 0;
getopts("c:nv");

sub skipping {
    my ($fname, $msg, $line1, $line2) = @_;

    return if !$opt_v or $fname =~ $exclude;
    print "$fname skipped: $msg\n";
    print "    $line1" if defined $line1;
    print "    $line2" if defined $line2;
}

sub gripe {
    my ($fname, $msg) = @_;
    return if $fname =~ $exclude;
    print STDERR "$fname: warning: $msg\n";
}

sub slurp {
    my ($fname) = @_;
    local $/;                   # slurp
    open(my $in, "<", $fname)
        or die "can't open $fname for reading: $!";
    return <$in>;
}

sub unslurp {
    my ($fname, $contents) = @_;
    open (my $out, ">", $fname)
        or die "can't open $fname for writing: $!";
    print $out $contents
        or die "error writing $fname: $!";
    close $out
        or die "error writing $fname: $!";
}

sub fname2guard {
    my ($fname) = @_;
    $fname =~ tr/a-z/A-Z/;
    $fname =~ tr/A-Z0-9/_/cs;
    return $fname;
}

sub preprocess {
    my ($fname, $guard) = @_;

    open(my $pipe, "-|", "$opt_c -E -D$guard -c -P - <$fname")
        or die "can't run $opt_c: $!";
    while (<$pipe>) {
        if ($_ =~ /\S/) {
            gripe($fname, "not blank after preprocessing");
            last;
        }
    }
    close $pipe
        or gripe($fname, "preprocessing failed ($opt_c exit status $?)");
}

for my $fname (@ARGV) {
    my $text = slurp($fname);

    $text =~ m,\A(\s*\n|\s*//\N*\n|\s*/\*.*?\*/\s*\n)*|,msg;
    my $pre = $&;
    unless ($text =~ /\G(.*\n)/g) {
        $text =~ /\G.*/;
        skipping($fname, "no recognizable header guard", "$&\n");
        next;
    }
    my $line1 = $1;
    unless ($text =~ /\G(.*\n)/g) {
        $text =~ /\G.*/;
        skipping($fname, "no recognizable header guard", "$&\n");
        next;
    }
    my $line2 = $1;
    my $body = substr($text, pos($text));

    unless ($line1 =~ /^\s*\#\s*(if\s*\!\s*defined(\s*\()?|ifndef)\s*
                       ([A-Za-z0-9_]+)/x) {
        skipping($fname, "no recognizable header guard", $line1, $line2);
        next;
    }
    my $guard = $3;
    unless ($line2 =~ /^\s*\#\s*define\s+([A-Za-z0-9_]+)/) {
        skipping($fname, "no recognizable header guard", $line1, $line2);
        next;
    }
    my $guard2 = $1;
    unless ($guard2 eq $guard) {
        skipping($fname, "mismatched header guard ($guard vs. $guard2) ",
                 $line1, $line2);
        next;
    }

    unless ($body =~ m,\A((.*\n)*)
                       (\s*\#\s*endif\s*(/\*\s*.*\s*\*/\s*)?\n?)
                       (\n|\s)*\Z,x) {
        skipping($fname, "can't find end of header guard");
        next;
    }
    $body = $1;
    my $line3 = $3;
    my $endif_comment = $4;

    my $oldg = $guard;

    unless ($fname =~ $exclude) {
        my @issues = ();
        $guard =~ tr/a-z/A-Z/
            and push @issues, "contains lowercase letters";
        $guard =~ s/^_+//
            and push @issues, "is a reserved identifier";
        $guard =~ s/(_H)?_*$/_H/
            and $& ne "_H" and push @issues, "doesn't end with _H";
        unless ($guard =~ /^[A-Z][A-Z0-9_]*_H/) {
            skipping($fname, "can't clean up odd guard symbol $oldg\n",
                     $line1, $line2);
            next;
        }

        my $exp = fname2guard($fname =~ s,.*/,,r);
        unless ($guard =~ /\Q$exp\E\Z/) {
            $guard = fname2guard($fname =~ s,^include/,,r);
            push @issues, "doesn't match the file name";
        }
        if (@issues and $opt_v) {
            print "$fname guard $oldg needs cleanup:\n    ",
                join(", ", @issues), "\n";
        }
    }

    $old_guard{$guard} = $oldg
        if $guard ne $oldg;

    if (exists $guarded{$guard}) {
        gripe($fname, "guard $guard also used by $guarded{$guard}");
    } else {
        $guarded{$guard} = $fname;
    }

    unless ($fname =~ $exclude) {
        my $newl1 = "#ifndef $guard\n";
        my $newl2 = "#define $guard\n";
        my $newl3 = "#endif\n";
        $newl3 =~ s,\Z, /* $guard */, if defined $endif_comment;
        if ($line1 ne $newl1 or $line2 ne $newl2 or $line3 ne $newl3) {
            $pre =~ s/\n*\Z/\n\n/ if $pre =~ /\N/;
            $body =~ s/\A\n*/\n/;
            if ($opt_n) {
                print "$fname would be cleaned up\n" if $opt_v;
            } else {
                unslurp($fname, "$pre$newl1$newl2$body$newl3");
                print "$fname cleaned up\n" if $opt_v;
            }
        }
    }

    preprocess($fname, $opt_n ? $oldg : $guard)
        unless $fname =~ $exclude or $fname =~ $exclude_cpp;
}

if (%old_guard) {
    print STDERR "warning: guard symbol renaming may break things\n";
    for my $guard (sort keys %old_guard) {
        print STDERR "    $old_guard{$guard} -> $guard\n";
    }
    print STDERR "To find uses that may have to be updated try:\n";
    print STDERR "    git grep -Ew '", join("|", sort values %old_guard),
        "'\n";
}
