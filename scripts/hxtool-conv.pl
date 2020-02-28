#!/usr/bin/perl -w
#
# Script to convert .hx file STEXI/ETEXI blocks to SRST/ERST
#
# Copyright (C) 2020 Linaro
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# (at your option) any later version. See the COPYING file in the
# top-level directory.

# This script was only ever intended as a one-off conversion operation.
# Please excuse the places where it is a bit hacky.
# Some manual intervention after the conversion is expected, as are
# some warnings from makeinfo.
# Warning: this script is not idempotent: don't try to run it on
# a .hx file that already has SRST/ERST sections.

# Expected usage:
# scripts/hxtool-conv.pl file.hx > file.hx.new

use utf8;

my $reading_texi = 0;
my $texiblock = '';
my @tables = ();

sub update_tables($) {
    my ($texi) = @_;
    # Update our list of open table directives: every @table
    # line in the texi fragment is added to the list, and every
    # @end table line means we remove an entry from the list.
    # If this fragment had a completely self contained table with
    # both the @table and @end table lines, this will be a no-op.
    foreach (split(/\n/, $texi)) {
        push @tables, $_ if /^\@table/;
        pop @tables if /^\@end table/;
    }
}

sub only_table_directives($) {
    # Return true if every line in the fragment is a start or end table directive
    my ($texi) = @_;
    foreach (split(/\n/, $texi)) {
        return 0 unless /^\@table/ or /^\@end table/;
    }
    return 1;
}

sub output_rstblock($) {
    # Write the output to /tmp/frag.texi, wrapped in whatever current @table
    # lines we need.
    my ($texi) = @_;

    # As a special case, if this fragment is only table directives and
    # nothing else, update our set of open table directives but otherwise
    # ignore it. This avoids emitting an empty SRST/ERST block.
    if (only_table_directives($texi)) {
        update_tables($texi);
        return;
    }

    open(my $fragfh, '>', '/tmp/frag.texi');
    # First output the currently active set of open table directives
    print $fragfh join("\n", @tables);
    # Next, update our list of open table directives.
    # We need to do this before we emit the closing table directives
    # so that we emit the right number if this fragment had an
    # unbalanced set of directives.
    update_tables($texi);
    # Then emit the texi fragment itself.
    print $fragfh "\n$texi\n";
    # Finally, add the necessary closing table directives.
    print $fragfh "\@end table\n" x scalar @tables;
    close $fragfh;

    # Now invoke makeinfo/pandoc on it and slurp the results into a string
    open(my $fh, '-|', "makeinfo --force -o - --docbook "
         . "-D 'qemu_system_x86 QEMU_SYSTEM_X86_MACRO' "
         . "-D 'qemu_system     QEMU_SYSTEM_MACRO'  /tmp/frag.texi "
         . " | pandoc  -f docbook -t rst")
        or die "can't start makeinfo/pandoc: $!";

    binmode $fh, ':encoding(utf8)';

    print "SRST\n";

    # Slurp the whole thing into a string so we can do multiline
    # string matches on it.
    my $rst = do {
        local $/ = undef;
        <$fh>;
    };
    $rst =~ s/^-  − /-  /gm;
    $rst =~ s/“/"/gm;
    $rst =~ s/”/"/gm;
    $rst =~ s/‘/'/gm;
    $rst =~ s/’/'/gm;
    $rst =~ s/QEMU_SYSTEM_MACRO/|qemu_system|/g;
    $rst =~ s/QEMU_SYSTEM_X86_MACRO/|qemu_system_x86|/g;
    $rst =~ s/(?=::\n\n +\|qemu)/.. parsed-literal/g;
    $rst =~ s/:\n\n::$/::/gm;

    # Fix up the invalid reference format makeinfo/pandoc emit:
    # `Some string here <#anchorname>`__
    # should be:
    # :ref:`anchorname`
    $rst =~ s/\`[^<`]+\<\#([^>]+)\>\`__/:ref:`$1`/gm;
    print $rst;

    close $fh or die "error on close: $!";
    print "ERST\n";
}

# Read the whole .hx input file.
while (<>) {
    # Always print the current line
    print;
    if (/STEXI/) {
        $reading_texi = 1;
        $texiblock = '';
        next;
    }
    if (/ETEXI/) {
        $reading_texi = 0;
        # dump RST version of block
        output_rstblock($texiblock);
        next;
    }
    if ($reading_texi) {
        # Accumulate the texi into a string
        # but drop findex entries as they will confuse makeinfo
        next if /^\@findex/;
        $texiblock .= $_;
    }
}

die "Unexpectedly still in texi block at EOF" if $reading_texi;
