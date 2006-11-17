#!/usr/bin/perl

# Script to copy acx driver tree into 2.6.x kernel tree
# (C) 2004 Carter Smithhart
# (C) 2004 Gismo / Luca Capello <luca@pca.it> http://luca.pca.it

use warnings;
use strict;

print "\n   Script to inject ACX driver into kernel tree\n";
print "--------------------------------------------------\n";

my $from_dir = shift @ARGV;
if (! $from_dir) {
	print "Usage: $0 [acx_source_dir] [kernel_source_dir]\n";
	print "*** If [kernel_source_dir] isn't specified, /usr/src/linux/ assumed by default\n\n";
	exit 1;
}

my $to_dir = shift @ARGV;
my $kdir;
if ($to_dir) {
    $kdir = "$to_dir/drivers/net/wireless";
} else {
    print "\n*** [kernel_source_dir] isn't specified, /usr/src/linux/ assumed by default\n\n";
    $kdir = "/usr/src/linux/drivers/net/wireless";
    if (!-e $kdir) {
	print "*** $kdir doesn't exist!\n";
	exit 1;
    }
}

print "- Checking acx sources...\n";
my $kernel_help = "$from_dir/scripts/kernel_help";
if (!-e "$from_dir/src") {
    print "*** $from_dir/src doesn't exist!\n";
    exit 1;
}
if (!-e "$from_dir/include") {
    print "*** $from_dir/include doesn't exist!\n";
    exit 1;
}
if (!-e "$kernel_help") {
    print "*** $kernel_help doesn't exist, assuming no help by default\n";
    $kernel_help = "null";
}
print "     done.\n";

my $acx_dir = "$kdir/acx";
if (!-e "$acx_dir") {
    print "- Making $acx_dir...\n";
    print `mkdir $acx_dir`;
    print "     done.\n";
}

print "- Copying files to $acx_dir...\n";
print `cp $from_dir/src/Makefile2.6 $acx_dir/Makefile`;
print `cp $from_dir/src/*.c $acx_dir`;
print `cp $from_dir/include/*.h $acx_dir`;
print "     done.\n";

my $kmakefile = "$kdir/Makefile";

print "- Checking for $kmakefile...\n";
open IN, "<$kmakefile" or die "*** Cannot open $kmakefile for read";
my @lines = <IN>;
close IN;
my $found = 0;
foreach my $line (@lines) {
  if ($line =~ /CONFIG_ACX/) {
    $found=1;
    last;
  }
}
if ($found == 0) {
    open OUT, ">>$kmakefile" or die "*** Cannot open $kmakefile for write";
    print OUT "obj-\$(CONFIG_ACX) += acx/";
    close OUT;
    print "     done.\n";
} else {
    print "     not necessary.\n";
}

my $kconfig = "$kdir/Kconfig";
print "- Checking for $kconfig...\n";
open IN, "<$kconfig" or die "*** Cannot open $kconfig for read";
@lines = <IN>;
close IN;
$found = 0;
foreach my $line (@lines) {
    if ($line =~ /config ACX100/) # FIXME: check too relaxed?
    {
	$found=1;
	last;
    }
}
if ($found == 0) {
    open OUT, ">$kconfig" or die "*** Cannot open $kconfig for write";
    my $line = join("", @lines);
    $line =~ s|endmenu|\n|;
    print OUT $line;
    if ($kernel_help eq "null") {
	print OUT "config ACX\n\ttristate \"Texas Instruments ACX100/ACX111 (TNETW1xxx) cards\"\n\tdepends on NET_RADIO && PCI !SMP\n\tdefault n\n\nendmenu\n";
    } else {
	open IN, "<$kernel_help" or die "*** Cannot open $kernel_help for read";
	print OUT <IN>;
	print OUT "endmenu\n";
    }
    close OUT;
    print "     done.\n";
} else {
    print "     not necessary.\n";
}

print "-----------------------------------------------\n";
print "   Process successfully completed. Enjoy it!\n\n";
exit;
