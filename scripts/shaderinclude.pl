#!/usr/bin/env perl
use strict;
use warnings;

my $file = shift;
open FILE, "<", $file or die "open $file: $!";
my $name = $file;
$name =~ s|.*/||;
$name =~ s/[-.]/_/g;
print "static GLchar ${name}_src[] =\n";
while (<FILE>) {
    chomp;
    printf "    \"%s\\n\"\n", $_;
}
print "    \"\\n\";\n";
close FILE;
