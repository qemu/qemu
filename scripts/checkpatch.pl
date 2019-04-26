#!/usr/bin/env perl
# (c) 2001, Dave Jones. (the file handling bit)
# (c) 2005, Joel Schopp <jschopp@austin.ibm.com> (the ugly bit)
# (c) 2007,2008, Andy Whitcroft <apw@uk.ibm.com> (new conditions, test suite)
# (c) 2008-2010 Andy Whitcroft <apw@canonical.com>
# Licensed under the terms of the GNU GPL License version 2

use strict;
use warnings;
use Term::ANSIColor qw(:constants);

my $P = $0;
$P =~ s@.*/@@g;

our $SrcFile    = qr{\.(?:h|c|cpp|s|S|pl|py|sh)$};

my $V = '0.31';

use Getopt::Long qw(:config no_auto_abbrev);

my $quiet = 0;
my $tree = 1;
my $chk_signoff = 1;
my $chk_patch = undef;
my $chk_branch = undef;
my $tst_only;
my $emacs = 0;
my $terse = 0;
my $file = undef;
my $color = "auto";
my $no_warnings = 0;
my $summary = 1;
my $mailback = 0;
my $summary_file = 0;
my $root;
my %debug;
my $help = 0;

sub help {
	my ($exitcode) = @_;

	print << "EOM";
Usage:

    $P [OPTION]... [FILE]...
    $P [OPTION]... [GIT-REV-LIST]

Version: $V

Options:
  -q, --quiet                quiet
  --no-tree                  run without a kernel tree
  --no-signoff               do not check for 'Signed-off-by' line
  --patch                    treat FILE as patchfile
  --branch                   treat args as GIT revision list
  --emacs                    emacs compile window format
  --terse                    one line per report
  -f, --file                 treat FILE as regular source file
  --strict                   fail if only warnings are found
  --root=PATH                PATH to the kernel tree root
  --no-summary               suppress the per-file summary
  --mailback                 only produce a report in case of warnings/errors
  --summary-file             include the filename in summary
  --debug KEY=[0|1]          turn on/off debugging of KEY, where KEY is one of
                             'values', 'possible', 'type', and 'attr' (default
                             is all off)
  --test-only=WORD           report only warnings/errors containing WORD
                             literally
  --color[=WHEN]             Use colors 'always', 'never', or only when output
                             is a terminal ('auto'). Default is 'auto'.
  -h, --help, --version      display this help and exit

When FILE is - read standard input.
EOM

	exit($exitcode);
}

# Perl's Getopt::Long allows options to take optional arguments after a space.
# Prevent --color by itself from consuming other arguments
foreach (@ARGV) {
	if ($_ eq "--color" || $_ eq "-color") {
		$_ = "--color=$color";
	}
}

GetOptions(
	'q|quiet+'	=> \$quiet,
	'tree!'		=> \$tree,
	'signoff!'	=> \$chk_signoff,
	'patch!'	=> \$chk_patch,
	'branch!'	=> \$chk_branch,
	'emacs!'	=> \$emacs,
	'terse!'	=> \$terse,
	'f|file!'	=> \$file,
	'strict!'	=> \$no_warnings,
	'root=s'	=> \$root,
	'summary!'	=> \$summary,
	'mailback!'	=> \$mailback,
	'summary-file!'	=> \$summary_file,

	'debug=s'	=> \%debug,
	'test-only=s'	=> \$tst_only,
	'color=s'       => \$color,
	'no-color'      => sub { $color = 'never'; },
	'h|help'	=> \$help,
	'version'	=> \$help
) or help(1);

help(0) if ($help);

my $exit = 0;

if ($#ARGV < 0) {
	print "$P: no input files\n";
	exit(1);
}

if (!defined $chk_branch && !defined $chk_patch && !defined $file) {
	$chk_branch = $ARGV[0] =~ /.\.\./ ? 1 : 0;
	$file = $ARGV[0] =~ /$SrcFile/ ? 1 : 0;
	$chk_patch = $chk_branch || $file ? 0 : 1;
} elsif (!defined $chk_branch && !defined $chk_patch) {
	if ($file) {
		$chk_branch = $chk_patch = 0;
	} else {
		$chk_branch = $ARGV[0] =~ /.\.\./ ? 1 : 0;
		$chk_patch = $chk_branch ? 0 : 1;
	}
} elsif (!defined $chk_branch && !defined $file) {
	if ($chk_patch) {
		$chk_branch = $file = 0;
	} else {
		$chk_branch = $ARGV[0] =~ /.\.\./ ? 1 : 0;
		$file = $chk_branch ? 0 : 1;
	}
} elsif (!defined $chk_patch && !defined $file) {
	if ($chk_branch) {
		$chk_patch = $file = 0;
	} else {
		$file = $ARGV[0] =~ /$SrcFile/ ? 1 : 0;
		$chk_patch = $file ? 0 : 1;
	}
} elsif (!defined $chk_branch) {
	$chk_branch = $chk_patch || $file ? 0 : 1;
} elsif (!defined $chk_patch) {
	$chk_patch = $chk_branch || $file ? 0 : 1;
} elsif (!defined $file) {
	$file = $chk_patch || $chk_branch ? 0 : 1;
}

if (($chk_patch && $chk_branch) ||
    ($chk_patch && $file) ||
    ($chk_branch && $file)) {
	die "Only one of --file, --branch, --patch is permitted\n";
}
if (!$chk_patch && !$chk_branch && !$file) {
	die "One of --file, --branch, --patch is required\n";
}

if ($color =~ /^always$/i) {
	$color = 1;
} elsif ($color =~ /^never$/i) {
	$color = 0;
} elsif ($color =~ /^auto$/i) {
	$color = (-t STDOUT);
} else {
	die "Invalid color mode: $color\n";
}

my $dbg_values = 0;
my $dbg_possible = 0;
my $dbg_type = 0;
my $dbg_attr = 0;
my $dbg_adv_dcs = 0;
my $dbg_adv_checking = 0;
my $dbg_adv_apw = 0;
for my $key (keys %debug) {
	## no critic
	eval "\${dbg_$key} = '$debug{$key}';";
	die "$@" if ($@);
}

my $rpt_cleaners = 0;

if ($terse) {
	$emacs = 1;
	$quiet++;
}

if ($tree) {
	if (defined $root) {
		if (!top_of_kernel_tree($root)) {
			die "$P: $root: --root does not point at a valid tree\n";
		}
	} else {
		if (top_of_kernel_tree('.')) {
			$root = '.';
		} elsif ($0 =~ m@(.*)/scripts/[^/]*$@ &&
						top_of_kernel_tree($1)) {
			$root = $1;
		}
	}

	if (!defined $root) {
		print "Must be run from the top-level dir. of a kernel tree\n";
		exit(2);
	}
}

my $emitted_corrupt = 0;

our $Ident	= qr{
			[A-Za-z_][A-Za-z\d_]*
			(?:\s*\#\#\s*[A-Za-z_][A-Za-z\d_]*)*
		}x;
our $Storage	= qr{extern|static|asmlinkage};
our $Sparse	= qr{
			__force
		}x;

# Notes to $Attribute:
our $Attribute	= qr{
			const|
			volatile|
			QEMU_NORETURN|
			QEMU_WARN_UNUSED_RESULT|
			QEMU_SENTINEL|
			QEMU_PACKED|
			GCC_FMT_ATTR
		  }x;
our $Modifier;
our $Inline	= qr{inline};
our $Member	= qr{->$Ident|\.$Ident|\[[^]]*\]};
our $Lval	= qr{$Ident(?:$Member)*};

our $Constant	= qr{(?:[0-9]+|0x[0-9a-fA-F]+)[UL]*};
our $Assignment	= qr{(?:\*\=|/=|%=|\+=|-=|<<=|>>=|&=|\^=|\|=|=)};
our $Compare    = qr{<=|>=|==|!=|<|>};
our $Operators	= qr{
			<=|>=|==|!=|
			=>|->|<<|>>|<|>|!|~|
			&&|\|\||,|\^|\+\+|--|&|\||\+|-|\*|\/|%
		  }x;

our $NonptrType;
our $Type;
our $Declare;

our $NON_ASCII_UTF8	= qr{
	[\xC2-\xDF][\x80-\xBF]               # non-overlong 2-byte
	|  \xE0[\xA0-\xBF][\x80-\xBF]        # excluding overlongs
	| [\xE1-\xEC\xEE\xEF][\x80-\xBF]{2}  # straight 3-byte
	|  \xED[\x80-\x9F][\x80-\xBF]        # excluding surrogates
	|  \xF0[\x90-\xBF][\x80-\xBF]{2}     # planes 1-3
	| [\xF1-\xF3][\x80-\xBF]{3}          # planes 4-15
	|  \xF4[\x80-\x8F][\x80-\xBF]{2}     # plane 16
}x;

our $UTF8	= qr{
	[\x09\x0A\x0D\x20-\x7E]              # ASCII
	| $NON_ASCII_UTF8
}x;

# There are still some false positives, but this catches most
# common cases.
our $typeTypedefs = qr{(?x:
        (?![KMGTPE]iB)                      # IEC binary prefix (do not match)
        [A-Z][A-Z\d_]*[a-z][A-Za-z\d_]*     # camelcase
        | [A-Z][A-Z\d_]*AIOCB               # all uppercase
        | [A-Z][A-Z\d_]*CPU                 # all uppercase
        | QEMUBH                            # all uppercase
)};

our @typeList = (
	qr{void},
	qr{(?:unsigned\s+)?char},
	qr{(?:unsigned\s+)?short},
	qr{(?:unsigned\s+)?int},
	qr{(?:unsigned\s+)?long},
	qr{(?:unsigned\s+)?long\s+int},
	qr{(?:unsigned\s+)?long\s+long},
	qr{(?:unsigned\s+)?long\s+long\s+int},
	qr{unsigned},
	qr{float},
	qr{double},
	qr{bool},
	qr{struct\s+$Ident},
	qr{union\s+$Ident},
	qr{enum\s+$Ident},
	qr{${Ident}_t},
	qr{${Ident}_handler},
	qr{${Ident}_handler_fn},
	qr{target_(?:u)?long},
	qr{hwaddr},
        # external libraries
	qr{xml${Ident}},
	qr{xen\w+_handle},
	# Glib definitions
	qr{gchar},
	qr{gshort},
	qr{glong},
	qr{gint},
	qr{gboolean},
	qr{guchar},
	qr{gushort},
	qr{gulong},
	qr{guint},
	qr{gfloat},
	qr{gdouble},
	qr{gpointer},
	qr{gconstpointer},
	qr{gint8},
	qr{guint8},
	qr{gint16},
	qr{guint16},
	qr{gint32},
	qr{guint32},
	qr{gint64},
	qr{guint64},
	qr{gsize},
	qr{gssize},
	qr{goffset},
	qr{gintptr},
	qr{guintptr},
);

# This can be modified by sub possible.  Since it can be empty, be careful
# about regexes that always match, because they can cause infinite loops.
our @modifierList = (
);

sub build_types {
	my $all = "(?x:  \n" . join("|\n  ", @typeList) . "\n)";
	if (@modifierList > 0) {
		my $mods = "(?x:  \n" . join("|\n  ", @modifierList) . "\n)";
		$Modifier = qr{(?:$Attribute|$Sparse|$mods)};
	} else {
		$Modifier = qr{(?:$Attribute|$Sparse)};
	}
	$NonptrType	= qr{
			(?:$Modifier\s+|const\s+)*
			(?:
				(?:typeof|__typeof__)\s*\(\s*\**\s*$Ident\s*\)|
				(?:$typeTypedefs\b)|
				(?:${all}\b)
			)
			(?:\s+$Modifier|\s+const)*
		  }x;
	$Type	= qr{
			$NonptrType
			(?:[\s\*]+\s*const|[\s\*]+|(?:\s*\[\s*\])+)?
			(?:\s+$Inline|\s+$Modifier)*
		  }x;
	$Declare	= qr{(?:$Storage\s+)?$Type};
}
build_types();

$chk_signoff = 0 if ($file);

my @rawlines = ();
my @lines = ();
my $vname;
if ($chk_branch) {
	my @patches;
	my %git_commits = ();
	my $HASH;
	open($HASH, "-|", "git", "log", "--reverse", "--no-merges", "--format=%H %s", $ARGV[0]) ||
		die "$P: git log --reverse --no-merges --format='%H %s' $ARGV[0] failed - $!\n";

	for my $line (<$HASH>) {
		$line =~ /^([0-9a-fA-F]{40,40}) (.*)$/;
		next if (!defined($1) || !defined($2));
		my $sha1 = $1;
		my $subject = $2;
		push(@patches, $sha1);
		$git_commits{$sha1} = $subject;
	}

	close $HASH;

	die "$P: no revisions returned for revlist '$chk_branch'\n"
	    unless @patches;

	my $i = 1;
	my $num_patches = @patches;
	for my $hash (@patches) {
		my $FILE;
		open($FILE, '-|', "git", "show", $hash) ||
			die "$P: git show $hash - $!\n";
		while (<$FILE>) {
			chomp;
			push(@rawlines, $_);
		}
		close($FILE);
		$vname = substr($hash, 0, 12) . ' (' . $git_commits{$hash} . ')';
		if ($num_patches > 1 && $quiet == 0) {
			my $prefix = "$i/$num_patches";
			$prefix = BLUE . BOLD . $prefix . RESET if $color;
			print "$prefix Checking commit $vname\n";
			$vname = "Patch $i/$num_patches";
		} else {
			$vname = "Commit " . $vname;
		}
		if (!process($hash)) {
			$exit = 1;
			print "\n" if ($num_patches > 1 && $quiet == 0);
		}
		@rawlines = ();
		@lines = ();
		$i++;
	}
} else {
	for my $filename (@ARGV) {
		my $FILE;
		if ($file) {
			open($FILE, '-|', "diff -u /dev/null $filename") ||
				die "$P: $filename: diff failed - $!\n";
		} elsif ($filename eq '-') {
			open($FILE, '<&STDIN');
		} else {
			open($FILE, '<', "$filename") ||
				die "$P: $filename: open failed - $!\n";
		}
		if ($filename eq '-') {
			$vname = 'Your patch';
		} else {
			$vname = $filename;
		}
		print "Checking $filename...\n" if @ARGV > 1 && $quiet == 0;
		while (<$FILE>) {
			chomp;
			push(@rawlines, $_);
		}
		close($FILE);
		if (!process($filename)) {
			$exit = 1;
		}
		@rawlines = ();
		@lines = ();
	}
}

exit($exit);

sub top_of_kernel_tree {
	my ($root) = @_;

	my @tree_check = (
		"COPYING", "MAINTAINERS", "Makefile",
		"README", "docs", "VERSION",
		"vl.c"
	);

	foreach my $check (@tree_check) {
		if (! -e $root . '/' . $check) {
			return 0;
		}
	}
	return 1;
}

sub expand_tabs {
	my ($str) = @_;

	my $res = '';
	my $n = 0;
	for my $c (split(//, $str)) {
		if ($c eq "\t") {
			$res .= ' ';
			$n++;
			for (; ($n % 8) != 0; $n++) {
				$res .= ' ';
			}
			next;
		}
		$res .= $c;
		$n++;
	}

	return $res;
}
sub copy_spacing {
	(my $res = shift) =~ tr/\t/ /c;
	return $res;
}

sub line_stats {
	my ($line) = @_;

	# Drop the diff line leader and expand tabs
	$line =~ s/^.//;
	$line = expand_tabs($line);

	# Pick the indent from the front of the line.
	my ($white) = ($line =~ /^(\s*)/);

	return (length($line), length($white));
}

my $sanitise_quote = '';

sub sanitise_line_reset {
	my ($in_comment) = @_;

	if ($in_comment) {
		$sanitise_quote = '*/';
	} else {
		$sanitise_quote = '';
	}
}
sub sanitise_line {
	my ($line) = @_;

	my $res = '';
	my $l = '';

	my $qlen = 0;
	my $off = 0;
	my $c;

	# Always copy over the diff marker.
	$res = substr($line, 0, 1);

	for ($off = 1; $off < length($line); $off++) {
		$c = substr($line, $off, 1);

		# Comments we are wacking completely including the begin
		# and end, all to $;.
		if ($sanitise_quote eq '' && substr($line, $off, 2) eq '/*') {
			$sanitise_quote = '*/';

			substr($res, $off, 2, "$;$;");
			$off++;
			next;
		}
		if ($sanitise_quote eq '*/' && substr($line, $off, 2) eq '*/') {
			$sanitise_quote = '';
			substr($res, $off, 2, "$;$;");
			$off++;
			next;
		}
		if ($sanitise_quote eq '' && substr($line, $off, 2) eq '//') {
			$sanitise_quote = '//';

			substr($res, $off, 2, $sanitise_quote);
			$off++;
			next;
		}

		# A \ in a string means ignore the next character.
		if (($sanitise_quote eq "'" || $sanitise_quote eq '"') &&
		    $c eq "\\") {
			substr($res, $off, 2, 'XX');
			$off++;
			next;
		}
		# Regular quotes.
		if ($c eq "'" || $c eq '"') {
			if ($sanitise_quote eq '') {
				$sanitise_quote = $c;

				substr($res, $off, 1, $c);
				next;
			} elsif ($sanitise_quote eq $c) {
				$sanitise_quote = '';
			}
		}

		#print "c<$c> SQ<$sanitise_quote>\n";
		if ($off != 0 && $sanitise_quote eq '*/' && $c ne "\t") {
			substr($res, $off, 1, $;);
		} elsif ($off != 0 && $sanitise_quote eq '//' && $c ne "\t") {
			substr($res, $off, 1, $;);
		} elsif ($off != 0 && $sanitise_quote && $c ne "\t") {
			substr($res, $off, 1, 'X');
		} else {
			substr($res, $off, 1, $c);
		}
	}

	if ($sanitise_quote eq '//') {
		$sanitise_quote = '';
	}

	# The pathname on a #include may be surrounded by '<' and '>'.
	if ($res =~ /^.\s*\#\s*include\s+\<(.*)\>/) {
		my $clean = 'X' x length($1);
		$res =~ s@\<.*\>@<$clean>@;

	# The whole of a #error is a string.
	} elsif ($res =~ /^.\s*\#\s*(?:error|warning)\s+(.*)\b/) {
		my $clean = 'X' x length($1);
		$res =~ s@(\#\s*(?:error|warning)\s+).*@$1$clean@;
	}

	return $res;
}

sub ctx_statement_block {
	my ($linenr, $remain, $off) = @_;
	my $line = $linenr - 1;
	my $blk = '';
	my $soff = $off;
	my $coff = $off - 1;
	my $coff_set = 0;

	my $loff = 0;

	my $type = '';
	my $level = 0;
	my @stack = ();
	my $p;
	my $c;
	my $len = 0;

	my $remainder;
	while (1) {
		@stack = (['', 0]) if ($#stack == -1);

		#warn "CSB: blk<$blk> remain<$remain>\n";
		# If we are about to drop off the end, pull in more
		# context.
		if ($off >= $len) {
			for (; $remain > 0; $line++) {
				last if (!defined $lines[$line]);
				next if ($lines[$line] =~ /^-/);
				$remain--;
				$loff = $len;
				$blk .= $lines[$line] . "\n";
				$len = length($blk);
				$line++;
				last;
			}
			# Bail if there is no further context.
			#warn "CSB: blk<$blk> off<$off> len<$len>\n";
			if ($off >= $len) {
				last;
			}
		}
		$p = $c;
		$c = substr($blk, $off, 1);
		$remainder = substr($blk, $off);

		#warn "CSB: c<$c> type<$type> level<$level> remainder<$remainder> coff_set<$coff_set>\n";

		# Handle nested #if/#else.
		if ($remainder =~ /^#\s*(?:ifndef|ifdef|if)\s/) {
			push(@stack, [ $type, $level ]);
		} elsif ($remainder =~ /^#\s*(?:else|elif)\b/) {
			($type, $level) = @{$stack[$#stack - 1]};
		} elsif ($remainder =~ /^#\s*endif\b/) {
			($type, $level) = @{pop(@stack)};
		}

		# Statement ends at the ';' or a close '}' at the
		# outermost level.
		if ($level == 0 && $c eq ';') {
			last;
		}

		# An else is really a conditional as long as its not else if
		if ($level == 0 && $coff_set == 0 &&
				(!defined($p) || $p =~ /(?:\s|\}|\+)/) &&
				$remainder =~ /^(else)(?:\s|{)/ &&
				$remainder !~ /^else\s+if\b/) {
			$coff = $off + length($1) - 1;
			$coff_set = 1;
			#warn "CSB: mark coff<$coff> soff<$soff> 1<$1>\n";
			#warn "[" . substr($blk, $soff, $coff - $soff + 1) . "]\n";
		}

		if (($type eq '' || $type eq '(') && $c eq '(') {
			$level++;
			$type = '(';
		}
		if ($type eq '(' && $c eq ')') {
			$level--;
			$type = ($level != 0)? '(' : '';

			if ($level == 0 && $coff < $soff) {
				$coff = $off;
				$coff_set = 1;
				#warn "CSB: mark coff<$coff>\n";
			}
		}
		if (($type eq '' || $type eq '{') && $c eq '{') {
			$level++;
			$type = '{';
		}
		if ($type eq '{' && $c eq '}') {
			$level--;
			$type = ($level != 0)? '{' : '';

			if ($level == 0) {
				if (substr($blk, $off + 1, 1) eq ';') {
					$off++;
				}
				last;
			}
		}
		$off++;
	}
	# We are truly at the end, so shuffle to the next line.
	if ($off == $len) {
		$loff = $len + 1;
		$line++;
		$remain--;
	}

	my $statement = substr($blk, $soff, $off - $soff + 1);
	my $condition = substr($blk, $soff, $coff - $soff + 1);

	#warn "STATEMENT<$statement>\n";
	#warn "CONDITION<$condition>\n";

	#print "coff<$coff> soff<$off> loff<$loff>\n";

	return ($statement, $condition,
			$line, $remain + 1, $off - $loff + 1, $level);
}

sub statement_lines {
	my ($stmt) = @_;

	# Strip the diff line prefixes and rip blank lines at start and end.
	$stmt =~ s/(^|\n)./$1/g;
	$stmt =~ s/^\s*//;
	$stmt =~ s/\s*$//;

	my @stmt_lines = ($stmt =~ /\n/g);

	return $#stmt_lines + 2;
}

sub statement_rawlines {
	my ($stmt) = @_;

	my @stmt_lines = ($stmt =~ /\n/g);

	return $#stmt_lines + 2;
}

sub statement_block_size {
	my ($stmt) = @_;

	$stmt =~ s/(^|\n)./$1/g;
	$stmt =~ s/^\s*\{//;
	$stmt =~ s/}\s*$//;
	$stmt =~ s/^\s*//;
	$stmt =~ s/\s*$//;

	my @stmt_lines = ($stmt =~ /\n/g);
	my @stmt_statements = ($stmt =~ /;/g);

	my $stmt_lines = $#stmt_lines + 2;
	my $stmt_statements = $#stmt_statements + 1;

	if ($stmt_lines > $stmt_statements) {
		return $stmt_lines;
	} else {
		return $stmt_statements;
	}
}

sub ctx_statement_full {
	my ($linenr, $remain, $off) = @_;
	my ($statement, $condition, $level);

	my (@chunks);

	# Grab the first conditional/block pair.
	($statement, $condition, $linenr, $remain, $off, $level) =
				ctx_statement_block($linenr, $remain, $off);
	#print "F: c<$condition> s<$statement> remain<$remain>\n";
	push(@chunks, [ $condition, $statement ]);
	if (!($remain > 0 && $condition =~ /^\s*(?:\n[+-])?\s*(?:if|else|do)\b/s)) {
		return ($level, $linenr, @chunks);
	}

	# Pull in the following conditional/block pairs and see if they
	# could continue the statement.
	for (;;) {
		($statement, $condition, $linenr, $remain, $off, $level) =
				ctx_statement_block($linenr, $remain, $off);
		#print "C: c<$condition> s<$statement> remain<$remain>\n";
		last if (!($remain > 0 && $condition =~ /^(?:\s*\n[+-])*\s*(?:else|do)\b/s));
		#print "C: push\n";
		push(@chunks, [ $condition, $statement ]);
	}

	return ($level, $linenr, @chunks);
}

sub ctx_block_get {
	my ($linenr, $remain, $outer, $open, $close, $off) = @_;
	my $line;
	my $start = $linenr - 1;
	my $blk = '';
	my @o;
	my @c;
	my @res = ();

	my $level = 0;
	my @stack = ($level);
	for ($line = $start; $remain > 0; $line++) {
		next if ($rawlines[$line] =~ /^-/);
		$remain--;

		$blk .= $rawlines[$line];

		# Handle nested #if/#else.
		if ($lines[$line] =~ /^.\s*#\s*(?:ifndef|ifdef|if)\s/) {
			push(@stack, $level);
		} elsif ($lines[$line] =~ /^.\s*#\s*(?:else|elif)\b/) {
			$level = $stack[$#stack - 1];
		} elsif ($lines[$line] =~ /^.\s*#\s*endif\b/) {
			$level = pop(@stack);
		}

		foreach my $c (split(//, $lines[$line])) {
			##print "C<$c>L<$level><$open$close>O<$off>\n";
			if ($off > 0) {
				$off--;
				next;
			}

			if ($c eq $close && $level > 0) {
				$level--;
				last if ($level == 0);
			} elsif ($c eq $open) {
				$level++;
			}
		}

		if (!$outer || $level <= 1) {
			push(@res, $rawlines[$line]);
		}

		last if ($level == 0);
	}

	return ($level, @res);
}
sub ctx_block_outer {
	my ($linenr, $remain) = @_;

	my ($level, @r) = ctx_block_get($linenr, $remain, 1, '{', '}', 0);
	return @r;
}
sub ctx_block {
	my ($linenr, $remain) = @_;

	my ($level, @r) = ctx_block_get($linenr, $remain, 0, '{', '}', 0);
	return @r;
}
sub ctx_statement {
	my ($linenr, $remain, $off) = @_;

	my ($level, @r) = ctx_block_get($linenr, $remain, 0, '(', ')', $off);
	return @r;
}
sub ctx_block_level {
	my ($linenr, $remain) = @_;

	return ctx_block_get($linenr, $remain, 0, '{', '}', 0);
}
sub ctx_statement_level {
	my ($linenr, $remain, $off) = @_;

	return ctx_block_get($linenr, $remain, 0, '(', ')', $off);
}

sub ctx_locate_comment {
	my ($first_line, $end_line) = @_;

	# Catch a comment on the end of the line itself.
	my ($current_comment) = ($rawlines[$end_line - 1] =~ m@.*(/\*.*\*/)\s*(?:\\\s*)?$@);
	return $current_comment if (defined $current_comment);

	# Look through the context and try and figure out if there is a
	# comment.
	my $in_comment = 0;
	$current_comment = '';
	for (my $linenr = $first_line; $linenr < $end_line; $linenr++) {
		my $line = $rawlines[$linenr - 1];
		#warn "           $line\n";
		if ($linenr == $first_line and $line =~ m@^.\s*\*@) {
			$in_comment = 1;
		}
		if ($line =~ m@/\*@) {
			$in_comment = 1;
		}
		if (!$in_comment && $current_comment ne '') {
			$current_comment = '';
		}
		$current_comment .= $line . "\n" if ($in_comment);
		if ($line =~ m@\*/@) {
			$in_comment = 0;
		}
	}

	chomp($current_comment);
	return($current_comment);
}
sub ctx_has_comment {
	my ($first_line, $end_line) = @_;
	my $cmt = ctx_locate_comment($first_line, $end_line);

	##print "LINE: $rawlines[$end_line - 1 ]\n";
	##print "CMMT: $cmt\n";

	return ($cmt ne '');
}

sub raw_line {
	my ($linenr, $cnt) = @_;

	my $offset = $linenr - 1;
	$cnt++;

	my $line;
	while ($cnt) {
		$line = $rawlines[$offset++];
		next if (defined($line) && $line =~ /^-/);
		$cnt--;
	}

	return $line;
}

sub cat_vet {
	my ($vet) = @_;
	my ($res, $coded);

	$res = '';
	while ($vet =~ /([^[:cntrl:]]*)([[:cntrl:]]|$)/g) {
		$res .= $1;
		if ($2 ne '') {
			$coded = sprintf("^%c", unpack('C', $2) + 64);
			$res .= $coded;
		}
	}
	$res =~ s/$/\$/;

	return $res;
}

my $av_preprocessor = 0;
my $av_pending;
my @av_paren_type;
my $av_pend_colon;

sub annotate_reset {
	$av_preprocessor = 0;
	$av_pending = '_';
	@av_paren_type = ('E');
	$av_pend_colon = 'O';
}

sub annotate_values {
	my ($stream, $type) = @_;

	my $res;
	my $var = '_' x length($stream);
	my $cur = $stream;

	print "$stream\n" if ($dbg_values > 1);

	while (length($cur)) {
		@av_paren_type = ('E') if ($#av_paren_type < 0);
		print " <" . join('', @av_paren_type) .
				"> <$type> <$av_pending>" if ($dbg_values > 1);
		if ($cur =~ /^(\s+)/o) {
			print "WS($1)\n" if ($dbg_values > 1);
			if ($1 =~ /\n/ && $av_preprocessor) {
				$type = pop(@av_paren_type);
				$av_preprocessor = 0;
			}

		} elsif ($cur =~ /^(\(\s*$Type\s*)\)/ && $av_pending eq '_') {
			print "CAST($1)\n" if ($dbg_values > 1);
			push(@av_paren_type, $type);
			$type = 'C';

		} elsif ($cur =~ /^($Type)\s*(?:$Ident|,|\)|\(|\s*$)/) {
			print "DECLARE($1)\n" if ($dbg_values > 1);
			$type = 'T';

		} elsif ($cur =~ /^($Modifier)\s*/) {
			print "MODIFIER($1)\n" if ($dbg_values > 1);
			$type = 'T';

		} elsif ($cur =~ /^(\#\s*define\s*$Ident)(\(?)/o) {
			print "DEFINE($1,$2)\n" if ($dbg_values > 1);
			$av_preprocessor = 1;
			push(@av_paren_type, $type);
			if ($2 ne '') {
				$av_pending = 'N';
			}
			$type = 'E';

		} elsif ($cur =~ /^(\#\s*(?:undef\s*$Ident|include\b))/o) {
			print "UNDEF($1)\n" if ($dbg_values > 1);
			$av_preprocessor = 1;
			push(@av_paren_type, $type);

		} elsif ($cur =~ /^(\#\s*(?:ifdef|ifndef|if))/o) {
			print "PRE_START($1)\n" if ($dbg_values > 1);
			$av_preprocessor = 1;

			push(@av_paren_type, $type);
			push(@av_paren_type, $type);
			$type = 'E';

		} elsif ($cur =~ /^(\#\s*(?:else|elif))/o) {
			print "PRE_RESTART($1)\n" if ($dbg_values > 1);
			$av_preprocessor = 1;

			push(@av_paren_type, $av_paren_type[$#av_paren_type]);

			$type = 'E';

		} elsif ($cur =~ /^(\#\s*(?:endif))/o) {
			print "PRE_END($1)\n" if ($dbg_values > 1);

			$av_preprocessor = 1;

			# Assume all arms of the conditional end as this
			# one does, and continue as if the #endif was not here.
			pop(@av_paren_type);
			push(@av_paren_type, $type);
			$type = 'E';

		} elsif ($cur =~ /^(\\\n)/o) {
			print "PRECONT($1)\n" if ($dbg_values > 1);

		} elsif ($cur =~ /^(__attribute__)\s*\(?/o) {
			print "ATTR($1)\n" if ($dbg_values > 1);
			$av_pending = $type;
			$type = 'N';

		} elsif ($cur =~ /^(sizeof)\s*(\()?/o) {
			print "SIZEOF($1)\n" if ($dbg_values > 1);
			if (defined $2) {
				$av_pending = 'V';
			}
			$type = 'N';

		} elsif ($cur =~ /^(if|while|for)\b/o) {
			print "COND($1)\n" if ($dbg_values > 1);
			$av_pending = 'E';
			$type = 'N';

		} elsif ($cur =~/^(case)/o) {
			print "CASE($1)\n" if ($dbg_values > 1);
			$av_pend_colon = 'C';
			$type = 'N';

		} elsif ($cur =~/^(return|else|goto|typeof|__typeof__)\b/o) {
			print "KEYWORD($1)\n" if ($dbg_values > 1);
			$type = 'N';

		} elsif ($cur =~ /^(\()/o) {
			print "PAREN('$1')\n" if ($dbg_values > 1);
			push(@av_paren_type, $av_pending);
			$av_pending = '_';
			$type = 'N';

		} elsif ($cur =~ /^(\))/o) {
			my $new_type = pop(@av_paren_type);
			if ($new_type ne '_') {
				$type = $new_type;
				print "PAREN('$1') -> $type\n"
							if ($dbg_values > 1);
			} else {
				print "PAREN('$1')\n" if ($dbg_values > 1);
			}

		} elsif ($cur =~ /^($Ident)\s*\(/o) {
			print "FUNC($1)\n" if ($dbg_values > 1);
			$type = 'V';
			$av_pending = 'V';

		} elsif ($cur =~ /^($Ident\s*):(?:\s*\d+\s*(,|=|;))?/) {
			if (defined $2 && $type eq 'C' || $type eq 'T') {
				$av_pend_colon = 'B';
			} elsif ($type eq 'E') {
				$av_pend_colon = 'L';
			}
			print "IDENT_COLON($1,$type>$av_pend_colon)\n" if ($dbg_values > 1);
			$type = 'V';

		} elsif ($cur =~ /^($Ident|$Constant)/o) {
			print "IDENT($1)\n" if ($dbg_values > 1);
			$type = 'V';

		} elsif ($cur =~ /^($Assignment)/o) {
			print "ASSIGN($1)\n" if ($dbg_values > 1);
			$type = 'N';

		} elsif ($cur =~/^(;|{|})/) {
			print "END($1)\n" if ($dbg_values > 1);
			$type = 'E';
			$av_pend_colon = 'O';

		} elsif ($cur =~/^(,)/) {
			print "COMMA($1)\n" if ($dbg_values > 1);
			$type = 'C';

		} elsif ($cur =~ /^(\?)/o) {
			print "QUESTION($1)\n" if ($dbg_values > 1);
			$type = 'N';

		} elsif ($cur =~ /^(:)/o) {
			print "COLON($1,$av_pend_colon)\n" if ($dbg_values > 1);

			substr($var, length($res), 1, $av_pend_colon);
			if ($av_pend_colon eq 'C' || $av_pend_colon eq 'L') {
				$type = 'E';
			} else {
				$type = 'N';
			}
			$av_pend_colon = 'O';

		} elsif ($cur =~ /^(\[)/o) {
			print "CLOSE($1)\n" if ($dbg_values > 1);
			$type = 'N';

		} elsif ($cur =~ /^(-(?![->])|\+(?!\+)|\*|\&\&|\&)/o) {
			my $variant;

			print "OPV($1)\n" if ($dbg_values > 1);
			if ($type eq 'V') {
				$variant = 'B';
			} else {
				$variant = 'U';
			}

			substr($var, length($res), 1, $variant);
			$type = 'N';

		} elsif ($cur =~ /^($Operators)/o) {
			print "OP($1)\n" if ($dbg_values > 1);
			if ($1 ne '++' && $1 ne '--') {
				$type = 'N';
			}

		} elsif ($cur =~ /(^.)/o) {
			print "C($1)\n" if ($dbg_values > 1);
		}
		if (defined $1) {
			$cur = substr($cur, length($1));
			$res .= $type x length($1);
		}
	}

	return ($res, $var);
}

sub possible {
	my ($possible, $line) = @_;
	my $notPermitted = qr{(?:
		^(?:
			$Modifier|
			$Storage|
			$Type|
			DEFINE_\S+
		)$|
		^(?:
			goto|
			return|
			case|
			else|
			asm|__asm__|
			do
		)(?:\s|$)|
		^(?:typedef|struct|enum)\b|
		^\#
	    )}x;
	warn "CHECK<$possible> ($line)\n" if ($dbg_possible > 2);
	if ($possible !~ $notPermitted) {
		# Check for modifiers.
		$possible =~ s/\s*$Storage\s*//g;
		$possible =~ s/\s*$Sparse\s*//g;
		if ($possible =~ /^\s*$/) {

		} elsif ($possible =~ /\s/) {
			$possible =~ s/\s*(?:$Type|\#\#)\s*//g;
			for my $modifier (split(' ', $possible)) {
				if ($modifier !~ $notPermitted) {
					warn "MODIFIER: $modifier ($possible) ($line)\n" if ($dbg_possible);
					push(@modifierList, $modifier);
				}
			}

		} else {
			warn "POSSIBLE: $possible ($line)\n" if ($dbg_possible);
			push(@typeList, $possible);
		}
		build_types();
	} else {
		warn "NOTPOSS: $possible ($line)\n" if ($dbg_possible > 1);
	}
}

my $prefix = '';

sub report {
	my ($level, $msg) = @_;
	if (defined $tst_only && $msg !~ /\Q$tst_only\E/) {
		return 0;
	}

	my $output = '';
	$output .= BOLD if $color;
	$output .= $prefix;
	$output .= RED if $color && $level eq 'ERROR';
	$output .= MAGENTA if $color && $level eq 'WARNING';
	$output .= $level . ':';
	$output .= RESET if $color;
	$output .= ' ' . $msg . "\n";

	$output = (split('\n', $output))[0] . "\n" if ($terse);

	push(our @report, $output);

	return 1;
}
sub report_dump {
	our @report;
}
sub ERROR {
	if (report("ERROR", $_[0])) {
		our $clean = 0;
		our $cnt_error++;
	}
}
sub WARN {
	if (report("WARNING", $_[0])) {
		our $clean = 0;
		our $cnt_warn++;
	}
}

sub process {
	my $filename = shift;

	my $linenr=0;
	my $prevline="";
	my $prevrawline="";
	my $stashline="";
	my $stashrawline="";

	my $length;
	my $indent;
	my $previndent=0;
	my $stashindent=0;

	our $clean = 1;
	my $signoff = 0;
	my $is_patch = 0;

	my $in_header_lines = $file ? 0 : 1;
	my $in_commit_log = 0;		#Scanning lines before patch
	my $reported_maintainer_file = 0;
	my $non_utf8_charset = 0;

	our @report = ();
	our $cnt_lines = 0;
	our $cnt_error = 0;
	our $cnt_warn = 0;
	our $cnt_chk = 0;

	# Trace the real file/line as we go.
	my $realfile = '';
	my $realline = 0;
	my $realcnt = 0;
	my $here = '';
	my $in_comment = 0;
	my $comment_edge = 0;
	my $first_line = 0;
	my $p1_prefix = '';

	my $prev_values = 'E';

	# suppression flags
	my %suppress_ifbraces;
	my %suppress_whiletrailers;
	my %suppress_export;

	# Pre-scan the patch sanitizing the lines.

	sanitise_line_reset();
	my $line;
	foreach my $rawline (@rawlines) {
		$linenr++;
		$line = $rawline;

		if ($rawline=~/^\@\@ -\d+(?:,\d+)? \+(\d+)(,(\d+))? \@\@/) {
			$realline=$1-1;
			if (defined $2) {
				$realcnt=$3+1;
			} else {
				$realcnt=1+1;
			}
			$in_comment = 0;

			# Guestimate if this is a continuing comment.  Run
			# the context looking for a comment "edge".  If this
			# edge is a close comment then we must be in a comment
			# at context start.
			my $edge;
			my $cnt = $realcnt;
			for (my $ln = $linenr + 1; $cnt > 0; $ln++) {
				next if (defined $rawlines[$ln - 1] &&
					 $rawlines[$ln - 1] =~ /^-/);
				$cnt--;
				#print "RAW<$rawlines[$ln - 1]>\n";
				last if (!defined $rawlines[$ln - 1]);
				if ($rawlines[$ln - 1] =~ m@(/\*|\*/)@ &&
				    $rawlines[$ln - 1] !~ m@"[^"]*(?:/\*|\*/)[^"]*"@) {
					($edge) = $1;
					last;
				}
			}
			if (defined $edge && $edge eq '*/') {
				$in_comment = 1;
			}

			# Guestimate if this is a continuing comment.  If this
			# is the start of a diff block and this line starts
			# ' *' then it is very likely a comment.
			if (!defined $edge &&
			    $rawlines[$linenr] =~ m@^.\s*(?:\*\*+| \*)(?:\s|$)@)
			{
				$in_comment = 1;
			}

			##print "COMMENT:$in_comment edge<$edge> $rawline\n";
			sanitise_line_reset($in_comment);

		} elsif ($realcnt && $rawline =~ /^(?:\+| |$)/) {
			# Standardise the strings and chars within the input to
			# simplify matching -- only bother with positive lines.
			$line = sanitise_line($rawline);
		}
		push(@lines, $line);

		if ($realcnt > 1) {
			$realcnt-- if ($line =~ /^(?:\+| |$)/);
		} else {
			$realcnt = 0;
		}

		#print "==>$rawline\n";
		#print "-->$line\n";
	}

	$prefix = '';

	$realcnt = 0;
	$linenr = 0;
	foreach my $line (@lines) {
		$linenr++;

		my $rawline = $rawlines[$linenr - 1];

#extract the line range in the file after the patch is applied
		if ($line=~/^\@\@ -\d+(?:,\d+)? \+(\d+)(,(\d+))? \@\@/) {
			$is_patch = 1;
			$first_line = $linenr + 1;
			$realline=$1-1;
			if (defined $2) {
				$realcnt=$3+1;
			} else {
				$realcnt=1+1;
			}
			annotate_reset();
			$prev_values = 'E';

			%suppress_ifbraces = ();
			%suppress_whiletrailers = ();
			%suppress_export = ();
			next;

# track the line number as we move through the hunk, note that
# new versions of GNU diff omit the leading space on completely
# blank context lines so we need to count that too.
		} elsif ($line =~ /^( |\+|$)/) {
			$realline++;
			$realcnt-- if ($realcnt != 0);

			# Measure the line length and indent.
			($length, $indent) = line_stats($rawline);

			# Track the previous line.
			($prevline, $stashline) = ($stashline, $line);
			($previndent, $stashindent) = ($stashindent, $indent);
			($prevrawline, $stashrawline) = ($stashrawline, $rawline);

			#warn "line<$line>\n";

		} elsif ($realcnt == 1) {
			$realcnt--;
		}

		my $hunk_line = ($realcnt != 0);

#make up the handle for any error we report on this line
		$prefix = "$filename:$realline: " if ($emacs && $file);
		$prefix = "$filename:$linenr: " if ($emacs && !$file);

		$here = "#$linenr: " if (!$file);
		$here = "#$realline: " if ($file);

		# extract the filename as it passes
		if ($line =~ /^diff --git.*?(\S+)$/) {
			$realfile = $1;
			$realfile =~ s@^([^/]*)/@@ if (!$file);
		} elsif ($line =~ /^\+\+\+\s+(\S+)/) {
			$realfile = $1;
			$realfile =~ s@^([^/]*)/@@ if (!$file);

			$p1_prefix = $1;
			if (!$file && $tree && $p1_prefix ne '' &&
			    -e "$root/$p1_prefix") {
				WARN("patch prefix '$p1_prefix' exists, appears to be a -p0 patch\n");
			}

			next;
		}

		$here .= "FILE: $realfile:$realline:" if ($realcnt != 0);

		my $hereline = "$here\n$rawline\n";
		my $herecurr = "$here\n$rawline\n";
		my $hereprev = "$here\n$prevrawline\n$rawline\n";

		$cnt_lines++ if ($realcnt != 0);

# Check for incorrect file permissions
		if ($line =~ /^new (file )?mode.*[7531]\d{0,2}$/) {
			my $permhere = $here . "FILE: $realfile\n";
			if ($realfile =~ /(\bMakefile(?:\.objs)?|\.c|\.cc|\.cpp|\.h|\.mak|\.[sS])$/) {
				ERROR("do not set execute permissions for source files\n" . $permhere);
			}
		}

# Accept git diff extended headers as valid patches
		if ($line =~ /^(?:rename|copy) (?:from|to) [\w\/\.\-]+\s*$/) {
			$is_patch = 1;
		}

		if ($line =~ /^Author: .*via Qemu-devel.*<qemu-devel\@nongnu.org>/) {
		    ERROR("Author email address is mangled by the mailing list\n" . $herecurr);
		}

#check the patch for a signoff:
		if ($line =~ /^\s*signed-off-by:/i) {
			# This is a signoff, if ugly, so do not double report.
			$signoff++;
			$in_commit_log = 0;

			if (!($line =~ /^\s*Signed-off-by:/)) {
				ERROR("The correct form is \"Signed-off-by\"\n" .
					$herecurr);
			}
			if ($line =~ /^\s*signed-off-by:\S/i) {
				ERROR("space required after Signed-off-by:\n" .
					$herecurr);
			}
		}

# Check if MAINTAINERS is being updated.  If so, there's probably no need to
# emit the "does MAINTAINERS need updating?" message on file add/move/delete
		if ($line =~ /^\s*MAINTAINERS\s*\|/) {
			$reported_maintainer_file = 1;
		}

# Check for added, moved or deleted files
		if (!$reported_maintainer_file && !$in_commit_log &&
		    ($line =~ /^(?:new|deleted) file mode\s*\d+\s*$/ ||
		     $line =~ /^rename (?:from|to) [\w\/\.\-]+\s*$/ ||
		     ($line =~ /\{\s*([\w\/\.\-]*)\s*\=\>\s*([\w\/\.\-]*)\s*\}/ &&
		      (defined($1) || defined($2))))) {
			$reported_maintainer_file = 1;
			WARN("added, moved or deleted file(s), does MAINTAINERS need updating?\n" . $herecurr);
		}

# Check for wrappage within a valid hunk of the file
		if ($realcnt != 0 && $line !~ m{^(?:\+|-| |\\ No newline|$)}) {
			ERROR("patch seems to be corrupt (line wrapped?)\n" .
				$herecurr) if (!$emitted_corrupt++);
		}

# UTF-8 regex found at http://www.w3.org/International/questions/qa-forms-utf-8.en.php
		if (($realfile =~ /^$/ || $line =~ /^\+/) &&
		    $rawline !~ m/^$UTF8*$/) {
			my ($utf8_prefix) = ($rawline =~ /^($UTF8*)/);

			my $blank = copy_spacing($rawline);
			my $ptr = substr($blank, 0, length($utf8_prefix)) . "^";
			my $hereptr = "$hereline$ptr\n";

			ERROR("Invalid UTF-8, patch and commit message should be encoded in UTF-8\n" . $hereptr);
		}

# Check if it's the start of a commit log
# (not a header line and we haven't seen the patch filename)
		if ($in_header_lines && $realfile =~ /^$/ &&
		    !($rawline =~ /^\s+\S/ ||
		      $rawline =~ /^(commit\b|from\b|[\w-]+:).*$/i)) {
			$in_header_lines = 0;
			$in_commit_log = 1;
		}

# Check if there is UTF-8 in a commit log when a mail header has explicitly
# declined it, i.e defined some charset where it is missing.
		if ($in_header_lines &&
		    $rawline =~ /^Content-Type:.+charset="(.+)".*$/ &&
		    $1 !~ /utf-8/i) {
			$non_utf8_charset = 1;
		}

		if ($in_commit_log && $non_utf8_charset && $realfile =~ /^$/ &&
		    $rawline =~ /$NON_ASCII_UTF8/) {
			WARN("8-bit UTF-8 used in possible commit log\n" . $herecurr);
		}

# ignore non-hunk lines and lines being removed
		next if (!$hunk_line || $line =~ /^-/);

# ignore files that are being periodically imported from Linux
		next if ($realfile =~ /^(linux-headers|include\/standard-headers)\//);

#trailing whitespace
		if ($line =~ /^\+.*\015/) {
			my $herevet = "$here\n" . cat_vet($rawline) . "\n";
			ERROR("DOS line endings\n" . $herevet);

		} elsif ($realfile =~ /^docs\/.+\.txt/ ||
			 $realfile =~ /^docs\/.+\.md/) {
		    if ($rawline =~ /^\+\s+$/ && $rawline !~ /^\+ {4}$/) {
			# TODO: properly check we're in a code block
			#       (surrounding text is 4-column aligned)
			my $herevet = "$here\n" . cat_vet($rawline) . "\n";
			ERROR("code blocks in documentation should have " .
			      "empty lines with exactly 4 columns of " .
			      "whitespace\n" . $herevet);
		    }
		} elsif ($rawline =~ /^\+.*\S\s+$/ || $rawline =~ /^\+\s+$/) {
			my $herevet = "$here\n" . cat_vet($rawline) . "\n";
			ERROR("trailing whitespace\n" . $herevet);
			$rpt_cleaners = 1;
		}

# checks for trace-events files
		if ($realfile =~ /trace-events$/ && $line =~ /^\+/) {
			if ($rawline =~ /%[-+ 0]*#/) {
				ERROR("Don't use '#' flag of printf format ('%#') in " .
				      "trace-events, use '0x' prefix instead\n" . $herecurr);
			} else {
				my $hex =
					qr/%[-+ *.0-9]*([hljztL]|ll|hh)?(x|X|"\s*PRI[xX][^"]*"?)/;

				# don't consider groups splitted by [.:/ ], like 2A.20:12ab
				my $tmpline = $rawline;
				$tmpline =~ s/($hex[.:\/ ])+$hex//g;

				if ($tmpline =~ /(?<!0x)$hex/) {
					ERROR("Hex numbers must be prefixed with '0x'\n" .
					      $herecurr);
				}
			}
		}

# check we are in a valid source file if not then ignore this hunk
		next if ($realfile !~ /$SrcFile/);

#90 column limit; exempt URLs, if no other words on line
		if ($line =~ /^\+/ &&
		    !($line =~ /^\+\s*"[^"]*"\s*(?:\s*|,|\)\s*;)\s*$/) &&
		    !($rawline =~ /^[^[:alnum:]]*https?:\S*$/) &&
		    $length > 80)
		{
			if ($length > 90) {
				ERROR("line over 90 characters\n" . $herecurr);
			} else {
				WARN("line over 80 characters\n" . $herecurr);
			}
		}

# check for spaces before a quoted newline
		if ($rawline =~ /^.*\".*\s\\n/) {
			ERROR("unnecessary whitespace before a quoted newline\n" . $herecurr);
		}

# check for adding lines without a newline.
		if ($line =~ /^\+/ && defined $lines[$linenr] && $lines[$linenr] =~ /^\\ No newline at end of file/) {
			ERROR("adding a line without newline at end of file\n" . $herecurr);
		}

# check for RCS/CVS revision markers
		if ($rawline =~ /^\+.*\$(Revision|Log|Id)(?:\$|\b)/) {
			ERROR("CVS style keyword markers, these will _not_ be updated\n". $herecurr);
		}

# tabs are only allowed in assembly source code, and in
# some scripts we imported from other projects.
		next if ($realfile =~ /\.(s|S)$/);
		next if ($realfile =~ /(checkpatch|get_maintainer|texi2pod)\.pl$/);

		if ($rawline =~ /^\+.*\t/) {
			my $herevet = "$here\n" . cat_vet($rawline) . "\n";
			ERROR("code indent should never use tabs\n" . $herevet);
			$rpt_cleaners = 1;
		}

# check we are in a valid C source file if not then ignore this hunk
		next if ($realfile !~ /\.(h|c|cpp)$/);

# Block comment styles

		# Block comments use /* on a line of its own
		if ($rawline !~ m@^\+.*/\*.*\*/[ \t]*$@ &&	#inline /*...*/
		    $rawline =~ m@^\+.*/\*\*?+[ \t]*[^ \t]@) { # /* or /** non-blank
			WARN("Block comments use a leading /* on a separate line\n" . $herecurr);
		}

# Block comments use * on subsequent lines
		if ($prevline =~ /$;[ \t]*$/ &&			#ends in comment
		    $prevrawline =~ /^\+.*?\/\*/ &&		#starting /*
		    $prevrawline !~ /\*\/[ \t]*$/ &&		#no trailing */
		    $rawline =~ /^\+/ &&			#line is new
		    $rawline !~ /^\+[ \t]*\*/) {		#no leading *
			WARN("Block comments use * on subsequent lines\n" . $hereprev);
		}

# Block comments use */ on trailing lines
		if ($rawline !~ m@^\+[ \t]*\*/[ \t]*$@ &&	#trailing */
		    $rawline !~ m@^\+.*/\*.*\*/[ \t]*$@ &&	#inline /*...*/
		    $rawline !~ m@^\+.*\*{2,}/[ \t]*$@ &&	#trailing **/
		    $rawline =~ m@^\+[ \t]*.+\*\/[ \t]*$@) {	#non blank */
			WARN("Block comments use a trailing */ on a separate line\n" . $herecurr);
		}

# Block comment * alignment
		if ($prevline =~ /$;[ \t]*$/ &&			#ends in comment
		    $line =~ /^\+[ \t]*$;/ &&			#leading comment
		    $rawline =~ /^\+[ \t]*\*/ &&		#leading *
		    (($prevrawline =~ /^\+.*?\/\*/ &&		#leading /*
		      $prevrawline !~ /\*\/[ \t]*$/) ||		#no trailing */
		     $prevrawline =~ /^\+[ \t]*\*/)) {		#leading *
			my $oldindent;
			$prevrawline =~ m@^\+([ \t]*/?)\*@;
			if (defined($1)) {
				$oldindent = expand_tabs($1);
			} else {
				$prevrawline =~ m@^\+(.*/?)\*@;
				$oldindent = expand_tabs($1);
			}
			$rawline =~ m@^\+([ \t]*)\*@;
			my $newindent = $1;
			$newindent = expand_tabs($newindent);
			if (length($oldindent) ne length($newindent)) {
				WARN("Block comments should align the * on each line\n" . $hereprev);
			}
		}

# Check for potential 'bare' types
		my ($stat, $cond, $line_nr_next, $remain_next, $off_next,
		    $realline_next);
		if ($realcnt && $line =~ /.\s*\S/) {
			($stat, $cond, $line_nr_next, $remain_next, $off_next) =
				ctx_statement_block($linenr, $realcnt, 0);
			$stat =~ s/\n./\n /g;
			$cond =~ s/\n./\n /g;

			# Find the real next line.
			$realline_next = $line_nr_next;
			if (defined $realline_next &&
			    (!defined $lines[$realline_next - 1] ||
			     substr($lines[$realline_next - 1], $off_next) =~ /^\s*$/)) {
				$realline_next++;
			}

			my $s = $stat;
			$s =~ s/{.*$//s;

			# Ignore goto labels.
			if ($s =~ /$Ident:\*$/s) {

			# Ignore functions being called
			} elsif ($s =~ /^.\s*$Ident\s*\(/s) {

			} elsif ($s =~ /^.\s*else\b/s) {

			# declarations always start with types
			} elsif ($prev_values eq 'E' && $s =~ /^.\s*(?:$Storage\s+)?(?:$Inline\s+)?(?:const\s+)?((?:\s*$Ident)+?)\b(?:\s+$Sparse)?\s*\**\s*(?:$Ident|\(\*[^\)]*\))(?:\s*$Modifier)?\s*(?:;|=|,|\()/s) {
				my $type = $1;
				$type =~ s/\s+/ /g;
				possible($type, "A:" . $s);

			# definitions in global scope can only start with types
			} elsif ($s =~ /^.(?:$Storage\s+)?(?:$Inline\s+)?(?:const\s+)?($Ident)\b\s*(?!:)/s) {
				possible($1, "B:" . $s);
			}

			# any (foo ... *) is a pointer cast, and foo is a type
			while ($s =~ /\(($Ident)(?:\s+$Sparse)*[\s\*]+\s*\)/sg) {
				possible($1, "C:" . $s);
			}

			# Check for any sort of function declaration.
			# int foo(something bar, other baz);
			# void (*store_gdt)(x86_descr_ptr *);
			if ($prev_values eq 'E' && $s =~ /^(.(?:typedef\s*)?(?:(?:$Storage|$Inline)\s*)*\s*$Type\s*(?:\b$Ident|\(\*\s*$Ident\))\s*)\(/s) {
				my ($name_len) = length($1);

				my $ctx = $s;
				substr($ctx, 0, $name_len + 1, '');
				$ctx =~ s/\)[^\)]*$//;

				for my $arg (split(/\s*,\s*/, $ctx)) {
					if ($arg =~ /^(?:const\s+)?($Ident)(?:\s+$Sparse)*\s*\**\s*(:?\b$Ident)?$/s || $arg =~ /^($Ident)$/s) {

						possible($1, "D:" . $s);
					}
				}
			}

		}

#
# Checks which may be anchored in the context.
#

# Check for switch () and associated case and default
# statements should be at the same indent.
		if ($line=~/\bswitch\s*\(.*\)/) {
			my $err = '';
			my $sep = '';
			my @ctx = ctx_block_outer($linenr, $realcnt);
			shift(@ctx);
			for my $ctx (@ctx) {
				my ($clen, $cindent) = line_stats($ctx);
				if ($ctx =~ /^\+\s*(case\s+|default:)/ &&
							$indent != $cindent) {
					$err .= "$sep$ctx\n";
					$sep = '';
				} else {
					$sep = "[...]\n";
				}
			}
			if ($err ne '') {
				ERROR("switch and case should be at the same indent\n$hereline$err");
			}
		}

# if/while/etc brace do not go on next line, unless defining a do while loop,
# or if that brace on the next line is for something else
		if ($line =~ /(.*)\b((?:if|while|for|switch)\s*\(|do\b|else\b)/ && $line !~ /^.\s*\#/) {
			my $pre_ctx = "$1$2";

			my ($level, @ctx) = ctx_statement_level($linenr, $realcnt, 0);
			my $ctx_cnt = $realcnt - $#ctx - 1;
			my $ctx = join("\n", @ctx);

			my $ctx_ln = $linenr;
			my $ctx_skip = $realcnt;

			while ($ctx_skip > $ctx_cnt || ($ctx_skip == $ctx_cnt &&
					defined $lines[$ctx_ln - 1] &&
					$lines[$ctx_ln - 1] =~ /^-/)) {
				##print "SKIP<$ctx_skip> CNT<$ctx_cnt>\n";
				$ctx_skip-- if (!defined $lines[$ctx_ln - 1] || $lines[$ctx_ln - 1] !~ /^-/);
				$ctx_ln++;
			}

			#print "realcnt<$realcnt> ctx_cnt<$ctx_cnt>\n";
			#print "pre<$pre_ctx>\nline<$line>\nctx<$ctx>\nnext<$lines[$ctx_ln - 1]>\n";

			# The length of the "previous line" is checked against 80 because it
			# includes the + at the beginning of the line (if the actual line has
			# 79 or 80 characters, it is no longer possible to add a space and an
			# opening brace there)
			if ($#ctx == 0 && $ctx !~ /{\s*/ &&
			    defined($lines[$ctx_ln - 1]) && $lines[$ctx_ln - 1] =~ /^\+\s*\{/ &&
			    defined($lines[$ctx_ln - 2]) && length($lines[$ctx_ln - 2]) < 80) {
				ERROR("that open brace { should be on the previous line\n" .
					"$here\n$ctx\n$rawlines[$ctx_ln - 1]\n");
			}
			if ($level == 0 && $pre_ctx !~ /}\s*while\s*\($/ &&
			    $ctx =~ /\)\s*\;\s*$/ &&
			    defined $lines[$ctx_ln - 1])
			{
				my ($nlength, $nindent) = line_stats($lines[$ctx_ln - 1]);
				if ($nindent > $indent) {
					ERROR("trailing semicolon indicates no statements, indent implies otherwise\n" .
						"$here\n$ctx\n$rawlines[$ctx_ln - 1]\n");
				}
			}
		}

# 'do ... while (0/false)' only makes sense in macros, without trailing ';'
		if ($line =~ /while\s*\((0|false)\);/) {
			ERROR("suspicious ; after while (0)\n" . $herecurr);
		}

# Check relative indent for conditionals and blocks.
		if ($line =~ /\b(?:(?:if|while|for)\s*\(|do\b)/ && $line !~ /^.\s*#/ && $line !~ /\}\s*while\s*/) {
			my ($s, $c) = ($stat, $cond);

			substr($s, 0, length($c), '');

			# Make sure we remove the line prefixes as we have
			# none on the first line, and are going to readd them
			# where necessary.
			$s =~ s/\n./\n/gs;

			# Find out how long the conditional actually is.
			my @newlines = ($c =~ /\n/gs);
			my $cond_lines = 1 + $#newlines;

			# We want to check the first line inside the block
			# starting at the end of the conditional, so remove:
			#  1) any blank line termination
			#  2) any opening brace { on end of the line
			#  3) any do (...) {
			my $continuation = 0;
			my $check = 0;
			$s =~ s/^.*\bdo\b//;
			$s =~ s/^\s*\{//;
			if ($s =~ s/^\s*\\//) {
				$continuation = 1;
			}
			if ($s =~ s/^\s*?\n//) {
				$check = 1;
				$cond_lines++;
			}

			# Also ignore a loop construct at the end of a
			# preprocessor statement.
			if (($prevline =~ /^.\s*#\s*define\s/ ||
			    $prevline =~ /\\\s*$/) && $continuation == 0) {
				$check = 0;
			}

			my $cond_ptr = -1;
			$continuation = 0;
			while ($cond_ptr != $cond_lines) {
				$cond_ptr = $cond_lines;

				# If we see an #else/#elif then the code
				# is not linear.
				if ($s =~ /^\s*\#\s*(?:else|elif)/) {
					$check = 0;
				}

				# Ignore:
				#  1) blank lines, they should be at 0,
				#  2) preprocessor lines, and
				#  3) labels.
				if ($continuation ||
				    $s =~ /^\s*?\n/ ||
				    $s =~ /^\s*#\s*?/ ||
				    $s =~ /^\s*$Ident\s*:/) {
					$continuation = ($s =~ /^.*?\\\n/) ? 1 : 0;
					if ($s =~ s/^.*?\n//) {
						$cond_lines++;
					}
				}
			}

			my (undef, $sindent) = line_stats("+" . $s);
			my $stat_real = raw_line($linenr, $cond_lines);

			# Check if either of these lines are modified, else
			# this is not this patch's fault.
			if (!defined($stat_real) ||
			    $stat !~ /^\+/ && $stat_real !~ /^\+/) {
				$check = 0;
			}
			if (defined($stat_real) && $cond_lines > 1) {
				$stat_real = "[...]\n$stat_real";
			}

			#print "line<$line> prevline<$prevline> indent<$indent> sindent<$sindent> check<$check> continuation<$continuation> s<$s> cond_lines<$cond_lines> stat_real<$stat_real> stat<$stat>\n";

			if ($check && (($sindent % 4) != 0 ||
			    ($sindent <= $indent && $s ne ''))) {
				ERROR("suspect code indent for conditional statements ($indent, $sindent)\n" . $herecurr . "$stat_real\n");
			}
		}

		# Track the 'values' across context and added lines.
		my $opline = $line; $opline =~ s/^./ /;
		my ($curr_values, $curr_vars) =
				annotate_values($opline . "\n", $prev_values);
		$curr_values = $prev_values . $curr_values;
		if ($dbg_values) {
			my $outline = $opline; $outline =~ s/\t/ /g;
			print "$linenr > .$outline\n";
			print "$linenr > $curr_values\n";
			print "$linenr >  $curr_vars\n";
		}
		$prev_values = substr($curr_values, -1);

#ignore lines not being added
		if ($line=~/^[^\+]/) {next;}

# TEST: allow direct testing of the type matcher.
		if ($dbg_type) {
			if ($line =~ /^.\s*$Declare\s*$/) {
				ERROR("TEST: is type\n" . $herecurr);
			} elsif ($dbg_type > 1 && $line =~ /^.+($Declare)/) {
				ERROR("TEST: is not type ($1 is)\n". $herecurr);
			}
			next;
		}
# TEST: allow direct testing of the attribute matcher.
		if ($dbg_attr) {
			if ($line =~ /^.\s*$Modifier\s*$/) {
				ERROR("TEST: is attr\n" . $herecurr);
			} elsif ($dbg_attr > 1 && $line =~ /^.+($Modifier)/) {
				ERROR("TEST: is not attr ($1 is)\n". $herecurr);
			}
			next;
		}

# check for initialisation to aggregates open brace on the next line
		if ($line =~ /^.\s*\{/ &&
		    $prevline =~ /(?:^|[^=])=\s*$/) {
			ERROR("that open brace { should be on the previous line\n" . $hereprev);
		}

#
# Checks which are anchored on the added line.
#

# check for malformed paths in #include statements (uses RAW line)
		if ($rawline =~ m{^.\s*\#\s*include\s+[<"](.*)[">]}) {
			my $path = $1;
			if ($path =~ m{//}) {
				ERROR("malformed #include filename\n" .
					$herecurr);
			}
		}

# no C99 // comments
		if ($line =~ m{//} &&
		    $rawline !~ m{// SPDX-License-Identifier: }) {
			ERROR("do not use C99 // comments\n" . $herecurr);
		}
		# Remove C99 comments.
		$line =~ s@//.*@@;
		$opline =~ s@//.*@@;

# check for global initialisers.
		if ($line =~ /^.$Type\s*$Ident\s*(?:\s+$Modifier)*\s*=\s*(0|NULL|false)\s*;/) {
			ERROR("do not initialise globals to 0 or NULL\n" .
				$herecurr);
		}
# check for static initialisers.
		if ($line =~ /\bstatic\s.*=\s*(0|NULL|false)\s*;/) {
			ERROR("do not initialise statics to 0 or NULL\n" .
				$herecurr);
		}

# * goes on variable not on type
		# (char*[ const])
		if ($line =~ m{\($NonptrType(\s*(?:$Modifier\b\s*|\*\s*)+)\)}) {
			my ($from, $to) = ($1, $1);

			# Should start with a space.
			$to =~ s/^(\S)/ $1/;
			# Should not end with a space.
			$to =~ s/\s+$//;
			# '*'s should not have spaces between.
			while ($to =~ s/\*\s+\*/\*\*/) {
			}

			#print "from<$from> to<$to>\n";
			if ($from ne $to) {
				ERROR("\"(foo$from)\" should be \"(foo$to)\"\n" .  $herecurr);
			}
		} elsif ($line =~ m{\b$NonptrType(\s*(?:$Modifier\b\s*|\*\s*)+)($Ident)}) {
			my ($from, $to, $ident) = ($1, $1, $2);

			# Should start with a space.
			$to =~ s/^(\S)/ $1/;
			# Should not end with a space.
			$to =~ s/\s+$//;
			# '*'s should not have spaces between.
			while ($to =~ s/\*\s+\*/\*\*/) {
			}
			# Modifiers should have spaces.
			$to =~ s/(\b$Modifier$)/$1 /;

			#print "from<$from> to<$to> ident<$ident>\n";
			if ($from ne $to && $ident !~ /^$Modifier$/) {
				ERROR("\"foo${from}bar\" should be \"foo${to}bar\"\n" .  $herecurr);
			}
		}

# function brace can't be on same line, except for #defines of do while,
# or if closed on same line
		if (($line=~/$Type\s*$Ident\(.*\).*\s\{/) and
		    !($line=~/\#\s*define.*do\s\{/) and !($line=~/}/)) {
			ERROR("open brace '{' following function declarations go on the next line\n" . $herecurr);
		}

# open braces for enum, union and struct go on the same line.
		if ($line =~ /^.\s*\{/ &&
		    $prevline =~ /^.\s*(?:typedef\s+)?(enum|union|struct)(?:\s+$Ident)?\s*$/) {
			ERROR("open brace '{' following $1 go on the same line\n" . $hereprev);
		}

# missing space after union, struct or enum definition
		if ($line =~ /^.\s*(?:typedef\s+)?(enum|union|struct)(?:\s+$Ident)?(?:\s+$Ident)?[=\{]/) {
		    ERROR("missing space after $1 definition\n" . $herecurr);
		}

# check for spacing round square brackets; allowed:
#  1. with a type on the left -- int [] a;
#  2. at the beginning of a line for slice initialisers -- [0...10] = 5,
#  3. inside a curly brace -- = { [0...10] = 5 }
#  4. after a comma -- [1] = 5, [2] = 6
#  5. in a macro definition -- #define abc(x) [x] = y
		while ($line =~ /(.*?\s)\[/g) {
			my ($where, $prefix) = ($-[1], $1);
			if ($prefix !~ /$Type\s+$/ &&
			    ($where != 0 || $prefix !~ /^.\s+$/) &&
			    $prefix !~ /\#\s*define[^(]*\([^)]*\)\s+$/ &&
			    $prefix !~ /[,{:]\s+$/) {
				ERROR("space prohibited before open square bracket '['\n" . $herecurr);
			}
		}

# check for spaces between functions and their parentheses.
		while ($line =~ /($Ident)\s+\(/g) {
			my $name = $1;
			my $ctx_before = substr($line, 0, $-[1]);
			my $ctx = "$ctx_before$name";

			# Ignore those directives where spaces _are_ permitted.
			if ($name =~ /^(?:
				if|for|while|switch|return|case|
				volatile|__volatile__|coroutine_fn|
				__attribute__|format|__extension__|
				asm|__asm__)$/x)
			{

			# Ignore 'catch (...)' in C++
			} elsif ($name =~ /^catch$/ && $realfile =~ /(\.cpp|\.h)$/) {

			# cpp #define statements have non-optional spaces, ie
			# if there is a space between the name and the open
			# parenthesis it is simply not a parameter group.
			} elsif ($ctx_before =~ /^.\s*\#\s*define\s*$/) {

			# cpp #elif statement condition may start with a (
			} elsif ($ctx =~ /^.\s*\#\s*elif\s*$/) {

			# If this whole things ends with a type its most
			# likely a typedef for a function.
			} elsif ($ctx =~ /$Type$/) {

			} else {
				ERROR("space prohibited between function name and open parenthesis '('\n" . $herecurr);
			}
		}
# Check operator spacing.
		if (!($line=~/\#\s*include/)) {
			my $ops = qr{
				<<=|>>=|<=|>=|==|!=|
				\+=|-=|\*=|\/=|%=|\^=|\|=|&=|
				=>|->|<<|>>|<|>|=|!|~|
				&&|\|\||,|\^|\+\+|--|&|\||\+|-|\*|\/|%|
				\?|::|:
			}x;
			my @elements = split(/($ops|;)/, $opline);
			my $off = 0;

			my $blank = copy_spacing($opline);

			for (my $n = 0; $n < $#elements; $n += 2) {
				$off += length($elements[$n]);

				# Pick up the preceding and succeeding characters.
				my $ca = substr($opline, 0, $off);
				my $cc = '';
				if (length($opline) >= ($off + length($elements[$n + 1]))) {
					$cc = substr($opline, $off + length($elements[$n + 1]));
				}
				my $cb = "$ca$;$cc";

				my $a = '';
				$a = 'V' if ($elements[$n] ne '');
				$a = 'W' if ($elements[$n] =~ /\s$/);
				$a = 'C' if ($elements[$n] =~ /$;$/);
				$a = 'B' if ($elements[$n] =~ /(\[|\()$/);
				$a = 'O' if ($elements[$n] eq '');
				$a = 'E' if ($ca =~ /^\s*$/);

				my $op = $elements[$n + 1];

				my $c = '';
				if (defined $elements[$n + 2]) {
					$c = 'V' if ($elements[$n + 2] ne '');
					$c = 'W' if ($elements[$n + 2] =~ /^\s/);
					$c = 'C' if ($elements[$n + 2] =~ /^$;/);
					$c = 'B' if ($elements[$n + 2] =~ /^(\)|\]|;)/);
					$c = 'O' if ($elements[$n + 2] eq '');
					$c = 'E' if ($elements[$n + 2] =~ /^\s*\\$/);
				} else {
					$c = 'E';
				}

				my $ctx = "${a}x${c}";

				my $at = "(ctx:$ctx)";

				my $ptr = substr($blank, 0, $off) . "^";
				my $hereptr = "$hereline$ptr\n";

				# Pull out the value of this operator.
				my $op_type = substr($curr_values, $off + 1, 1);

				# Get the full operator variant.
				my $opv = $op . substr($curr_vars, $off, 1);

				# Ignore operators passed as parameters.
				if ($op_type ne 'V' &&
				    $ca =~ /\s$/ && $cc =~ /^\s*,/) {

#				# Ignore comments
#				} elsif ($op =~ /^$;+$/) {

				# ; should have either the end of line or a space or \ after it
				} elsif ($op eq ';') {
					if ($ctx !~ /.x[WEBC]/ &&
					    $cc !~ /^\\/ && $cc !~ /^;/) {
						ERROR("space required after that '$op' $at\n" . $hereptr);
					}

				# // is a comment
				} elsif ($op eq '//') {

				# Ignore : used in class declaration in C++
				} elsif ($opv eq ':B' && $ctx =~ /Wx[WE]/ &&
						 $line =~ /class/ && $realfile =~ /(\.cpp|\.h)$/) {

				# No spaces for:
				#   ->
				#   :   when part of a bitfield
				} elsif ($op eq '->' || $opv eq ':B') {
					if ($ctx =~ /Wx.|.xW/) {
						ERROR("spaces prohibited around that '$op' $at\n" . $hereptr);
					}

				# , must have a space on the right.
                                # not required when having a single },{ on one line
				} elsif ($op eq ',') {
					if ($ctx !~ /.x[WEC]/ && $cc !~ /^}/ &&
                                            ($elements[$n] . $elements[$n + 2]) !~ " *}\\{") {
						ERROR("space required after that '$op' $at\n" . $hereptr);
					}

				# '*' as part of a type definition -- reported already.
				} elsif ($opv eq '*_') {
					#warn "'*' is part of type\n";

				# unary operators should have a space before and
				# none after.  May be left adjacent to another
				# unary operator, or a cast
				} elsif ($op eq '!' || $op eq '~' ||
					 $opv eq '*U' || $opv eq '-U' ||
					 $opv eq '&U' || $opv eq '&&U') {
					if ($op eq '~' && $ca =~ /::$/ && $realfile =~ /(\.cpp|\.h)$/) {
						# '~' used as a name of Destructor

					} elsif ($ctx !~ /[WEBC]x./ && $ca !~ /(?:\)|!|~|\*|-|\&|\||\+\+|\-\-|\{)$/) {
						ERROR("space required before that '$op' $at\n" . $hereptr);
					}
					if ($op eq '*' && $cc =~/\s*$Modifier\b/) {
						# A unary '*' may be const

					} elsif ($ctx =~ /.xW/) {
						ERROR("space prohibited after that '$op' $at\n" . $hereptr);
					}

				# unary ++ and unary -- are allowed no space on one side.
				} elsif ($op eq '++' or $op eq '--') {
					if ($ctx !~ /[WEOBC]x[^W]/ && $ctx !~ /[^W]x[WOBEC]/) {
						ERROR("space required one side of that '$op' $at\n" . $hereptr);
					}
					if ($ctx =~ /Wx[BE]/ ||
					    ($ctx =~ /Wx./ && $cc =~ /^;/)) {
						ERROR("space prohibited before that '$op' $at\n" . $hereptr);
					}
					if ($ctx =~ /ExW/) {
						ERROR("space prohibited after that '$op' $at\n" . $hereptr);
					}

				# A colon needs no spaces before when it is
				# terminating a case value or a label.
				} elsif ($opv eq ':C' || $opv eq ':L') {
					if ($ctx =~ /Wx./) {
						ERROR("space prohibited before that '$op' $at\n" . $hereptr);
					}

				# All the others need spaces both sides.
				} elsif ($ctx !~ /[EWC]x[CWE]/) {
					my $ok = 0;

					if ($realfile =~ /\.cpp|\.h$/) {
						# Ignore template arguments <...> in C++
						if (($op eq '<' || $op eq '>') && $line =~ /<.*>/) {
							$ok = 1;
						}

						# Ignore :: in C++
						if ($op eq '::') {
							$ok = 1;
						}
					}

					# Ignore email addresses <foo@bar>
					if (($op eq '<' &&
					     $cc =~ /^\S+\@\S+>/) ||
					    ($op eq '>' &&
					     $ca =~ /<\S+\@\S+$/))
					{
						$ok = 1;
					}

					# Ignore ?:
					if (($opv eq ':O' && $ca =~ /\?$/) ||
					    ($op eq '?' && $cc =~ /^:/)) {
						$ok = 1;
					}

					if ($ok == 0) {
						ERROR("spaces required around that '$op' $at\n" . $hereptr);
					}
				}
				$off += length($elements[$n + 1]);
			}
		}

#need space before brace following if, while, etc
		if (($line =~ /\(.*\)\{/ && $line !~ /\($Type\)\{/) ||
		    $line =~ /do\{/) {
			ERROR("space required before the open brace '{'\n" . $herecurr);
		}

# closing brace should have a space following it when it has anything
# on the line
		if ($line =~ /}(?!(?:,|;|\)))\S/) {
			ERROR("space required after that close brace '}'\n" . $herecurr);
		}

# check spacing on square brackets
		if ($line =~ /\[\s/ && $line !~ /\[\s*$/) {
			ERROR("space prohibited after that open square bracket '['\n" . $herecurr);
		}
		if ($line =~ /\s\]/) {
			ERROR("space prohibited before that close square bracket ']'\n" . $herecurr);
		}

# check spacing on parentheses
		if ($line =~ /\(\s/ && $line !~ /\(\s*(?:\\)?$/ &&
		    $line !~ /for\s*\(\s+;/) {
			ERROR("space prohibited after that open parenthesis '('\n" . $herecurr);
		}
		if ($line =~ /(\s+)\)/ && $line !~ /^.\s*\)/ &&
		    $line !~ /for\s*\(.*;\s+\)/ &&
		    $line !~ /:\s+\)/) {
			ERROR("space prohibited before that close parenthesis ')'\n" . $herecurr);
		}

# Return is not a function.
		if (defined($stat) && $stat =~ /^.\s*return(\s*)(\(.*);/s) {
			my $spacing = $1;
			my $value = $2;

			# Flatten any parentheses
			$value =~ s/\(/ \(/g;
			$value =~ s/\)/\) /g;
			while ($value =~ s/\[[^\{\}]*\]/1/ ||
			       $value !~ /(?:$Ident|-?$Constant)\s*
					     $Compare\s*
					     (?:$Ident|-?$Constant)/x &&
			       $value =~ s/\([^\(\)]*\)/1/) {
			}
#print "value<$value>\n";
			if ($value =~ /^\s*(?:$Ident|-?$Constant)\s*$/) {
				ERROR("return is not a function, parentheses are not required\n" . $herecurr);

			} elsif ($spacing !~ /\s+/) {
				ERROR("space required before the open parenthesis '('\n" . $herecurr);
			}
		}
# Return of what appears to be an errno should normally be -'ve
		if ($line =~ /^.\s*return\s*(E[A-Z]*)\s*;/) {
			my $name = $1;
			if ($name ne 'EOF' && $name ne 'ERROR') {
				ERROR("return of an errno should typically be -ve (return -$1)\n" . $herecurr);
			}
		}

		if ($line =~ /^.\s*(Q(?:S?LIST|SIMPLEQ|TAILQ)_HEAD)\s*\(\s*[^,]/ &&
		    $line !~ /^.typedef/) {
		    ERROR("named $1 should be typedefed separately\n" . $herecurr);
		}

# Need a space before open parenthesis after if, while etc
		if ($line=~/\b(if|while|for|switch)\(/) {
			ERROR("space required before the open parenthesis '('\n" . $herecurr);
		}

# Check for illegal assignment in if conditional -- and check for trailing
# statements after the conditional.
		if ($line =~ /do\s*(?!{)/) {
			my ($stat_next) = ctx_statement_block($line_nr_next,
						$remain_next, $off_next);
			$stat_next =~ s/\n./\n /g;
			##print "stat<$stat> stat_next<$stat_next>\n";

			if ($stat_next =~ /^\s*while\b/) {
				# If the statement carries leading newlines,
				# then count those as offsets.
				my ($whitespace) =
					($stat_next =~ /^((?:\s*\n[+-])*\s*)/s);
				my $offset =
					statement_rawlines($whitespace) - 1;

				$suppress_whiletrailers{$line_nr_next +
								$offset} = 1;
			}
		}
		if (!defined $suppress_whiletrailers{$linenr} &&
		    $line =~ /\b(?:if|while|for)\s*\(/ && $line !~ /^.\s*#/) {
			my ($s, $c) = ($stat, $cond);

			if ($c =~ /\bif\s*\(.*[^<>!=]=[^=].*/s) {
				ERROR("do not use assignment in if condition\n" . $herecurr);
			}

			# Find out what is on the end of the line after the
			# conditional.
			substr($s, 0, length($c), '');
			$s =~ s/\n.*//g;
			$s =~ s/$;//g; 	# Remove any comments
			if (length($c) && $s !~ /^\s*{?\s*\\*\s*$/ &&
			    $c !~ /}\s*while\s*/)
			{
				# Find out how long the conditional actually is.
				my @newlines = ($c =~ /\n/gs);
				my $cond_lines = 1 + $#newlines;
				my $stat_real = '';

				$stat_real = raw_line($linenr, $cond_lines)
							. "\n" if ($cond_lines);
				if (defined($stat_real) && $cond_lines > 1) {
					$stat_real = "[...]\n$stat_real";
				}

				ERROR("trailing statements should be on next line\n" . $herecurr . $stat_real);
			}
		}

# Check for bitwise tests written as boolean
		if ($line =~ /
			(?:
				(?:\[|\(|\&\&|\|\|)
				\s*0[xX][0-9]+\s*
				(?:\&\&|\|\|)
			|
				(?:\&\&|\|\|)
				\s*0[xX][0-9]+\s*
				(?:\&\&|\|\||\)|\])
			)/x)
		{
			ERROR("boolean test with hexadecimal, perhaps just 1 \& or \|?\n" . $herecurr);
		}

# if and else should not have general statements after it
		if ($line =~ /^.\s*(?:}\s*)?else\b(.*)/) {
			my $s = $1;
			$s =~ s/$;//g; 	# Remove any comments
			if ($s !~ /^\s*(?:\sif|(?:{|)\s*\\?\s*$)/) {
				ERROR("trailing statements should be on next line\n" . $herecurr);
			}
		}
# if should not continue a brace
		if ($line =~ /}\s*if\b/) {
			ERROR("trailing statements should be on next line\n" .
				$herecurr);
		}
# case and default should not have general statements after them
		if ($line =~ /^.\s*(?:case\s*.*|default\s*):/g &&
		    $line !~ /\G(?:
			(?:\s*$;*)(?:\s*{)?(?:\s*$;*)(?:\s*\\)?\s*$|
			\s*return\s+
		    )/xg)
		{
			ERROR("trailing statements should be on next line\n" . $herecurr);
		}

		# Check for }<nl>else {, these must be at the same
		# indent level to be relevant to each other.
		if ($prevline=~/}\s*$/ and $line=~/^.\s*else\s*/ and
						$previndent == $indent) {
			ERROR("else should follow close brace '}'\n" . $hereprev);
		}

		if ($prevline=~/}\s*$/ and $line=~/^.\s*while\s*/ and
						$previndent == $indent) {
			my ($s, $c) = ctx_statement_block($linenr, $realcnt, 0);

			# Find out what is on the end of the line after the
			# conditional.
			substr($s, 0, length($c), '');
			$s =~ s/\n.*//g;

			if ($s =~ /^\s*;/) {
				ERROR("while should follow close brace '}'\n" . $hereprev);
			}
		}

#studly caps, commented out until figure out how to distinguish between use of existing and adding new
#		if (($line=~/[\w_][a-z\d]+[A-Z]/) and !($line=~/print/)) {
#		    print "No studly caps, use _\n";
#		    print "$herecurr";
#		    $clean = 0;
#		}

#no spaces allowed after \ in define
		if ($line=~/\#\s*define.*\\\s$/) {
			ERROR("Whitespace after \\ makes next lines useless\n" . $herecurr);
		}

# multi-statement macros should be enclosed in a do while loop, grab the
# first statement and ensure its the whole macro if its not enclosed
# in a known good container
		if ($realfile !~ m@/vmlinux.lds.h$@ &&
		    $line =~ /^.\s*\#\s*define\s*$Ident(\()?/) {
			my $ln = $linenr;
			my $cnt = $realcnt;
			my ($off, $dstat, $dcond, $rest);
			my $ctx = '';

			my $args = defined($1);

			# Find the end of the macro and limit our statement
			# search to that.
			while ($cnt > 0 && defined $lines[$ln - 1] &&
				$lines[$ln - 1] =~ /^(?:-|..*\\$)/)
			{
				$ctx .= $rawlines[$ln - 1] . "\n";
				$cnt-- if ($lines[$ln - 1] !~ /^-/);
				$ln++;
			}
			$ctx .= $rawlines[$ln - 1];

			($dstat, $dcond, $ln, $cnt, $off) =
				ctx_statement_block($linenr, $ln - $linenr + 1, 0);
			#print "dstat<$dstat> dcond<$dcond> cnt<$cnt> off<$off>\n";
			#print "LINE<$lines[$ln-1]> len<" . length($lines[$ln-1]) . "\n";

			# Extract the remainder of the define (if any) and
			# rip off surrounding spaces, and trailing \'s.
			$rest = '';
			while ($off != 0 || ($cnt > 0 && $rest =~ /\\\s*$/)) {
				#print "ADDING cnt<$cnt> $off <" . substr($lines[$ln - 1], $off) . "> rest<$rest>\n";
				if ($off != 0 || $lines[$ln - 1] !~ /^-/) {
					$rest .= substr($lines[$ln - 1], $off) . "\n";
					$cnt--;
				}
				$ln++;
				$off = 0;
			}
			$rest =~ s/\\\n.//g;
			$rest =~ s/^\s*//s;
			$rest =~ s/\s*$//s;

			# Clean up the original statement.
			if ($args) {
				substr($dstat, 0, length($dcond), '');
			} else {
				$dstat =~ s/^.\s*\#\s*define\s+$Ident\s*//;
			}
			$dstat =~ s/$;//g;
			$dstat =~ s/\\\n.//g;
			$dstat =~ s/^\s*//s;
			$dstat =~ s/\s*$//s;

			# Flatten any parentheses and braces
			while ($dstat =~ s/\([^\(\)]*\)/1/ ||
			       $dstat =~ s/\{[^\{\}]*\}/1/ ||
			       $dstat =~ s/\[[^\{\}]*\]/1/)
			{
			}

			my $exceptions = qr{
				$Declare|
				module_param_named|
				MODULE_PARAM_DESC|
				DECLARE_PER_CPU|
				DEFINE_PER_CPU|
				__typeof__\(|
				union|
				struct|
				\.$Ident\s*=\s*|
				^\"|\"$
			}x;
			#print "REST<$rest> dstat<$dstat> ctx<$ctx>\n";
			if ($rest ne '' && $rest ne ',') {
				if ($rest !~ /while\s*\(/ &&
				    $dstat !~ /$exceptions/)
				{
					ERROR("Macros with multiple statements should be enclosed in a do - while loop\n" . "$here\n$ctx\n");
				}

			} elsif ($ctx !~ /;/) {
				if ($dstat ne '' &&
				    $dstat !~ /^(?:$Ident|-?$Constant)$/ &&
				    $dstat !~ /$exceptions/ &&
				    $dstat !~ /^\.$Ident\s*=/ &&
				    $dstat =~ /$Operators/)
				{
					ERROR("Macros with complex values should be enclosed in parenthesis\n" . "$here\n$ctx\n");
				}
			}
		}

# check for missing bracing around if etc
		if ($line =~ /(^.*)\b(?:if|while|for)\b/ &&
			$line !~ /\#\s*if/) {
			my $allowed = 0;

			# Check the pre-context.
			if ($line =~ /(\}.*?)$/) {
				my $pre = $1;

				if ($line !~ /else/) {
					print "APW: ALLOWED: pre<$pre> line<$line>\n"
						if $dbg_adv_apw;
					$allowed = 1;
				}
			}
			my ($level, $endln, @chunks) =
				ctx_statement_full($linenr, $realcnt, 1);
                        if ($dbg_adv_apw) {
                            print "APW: chunks<$#chunks> linenr<$linenr> endln<$endln> level<$level>\n";
                            print "APW: <<$chunks[1][0]>><<$chunks[1][1]>>\n"
                                if $#chunks >= 1;
                        }
			if ($#chunks >= 0 && $level == 0) {
				my $seen = 0;
				my $herectx = $here . "\n";
				my $ln = $linenr - 1;
				for my $chunk (@chunks) {
					my ($cond, $block) = @{$chunk};

					# If the condition carries leading newlines, then count those as offsets.
					my ($whitespace) = ($cond =~ /^((?:\s*\n[+-])*\s*)/s);
					my $offset = statement_rawlines($whitespace) - 1;

					#print "COND<$cond> whitespace<$whitespace> offset<$offset>\n";

					# We have looked at and allowed this specific line.
					$suppress_ifbraces{$ln + $offset} = 1;

					$herectx .= "$rawlines[$ln + $offset]\n[...]\n";
					$ln += statement_rawlines($block) - 1;

					substr($block, 0, length($cond), '');

					my $spaced_block = $block;
					$spaced_block =~ s/\n\+/ /g;

					$seen++ if ($spaced_block =~ /^\s*\{/);

                                        print "APW: cond<$cond> block<$block> allowed<$allowed>\n"
                                            if $dbg_adv_apw;
					if (statement_lines($cond) > 1) {
                                            print "APW: ALLOWED: cond<$cond>\n"
                                                if $dbg_adv_apw;
                                            $allowed = 1;
					}
					if ($block =~/\b(?:if|for|while)\b/) {
                                            print "APW: ALLOWED: block<$block>\n"
                                                if $dbg_adv_apw;
                                            $allowed = 1;
					}
					if (statement_block_size($block) > 1) {
                                            print "APW: ALLOWED: lines block<$block>\n"
                                                if $dbg_adv_apw;
                                            $allowed = 1;
					}
				}
				if ($seen != ($#chunks + 1) && !$allowed) {
					ERROR("braces {} are necessary for all arms of this statement\n" . $herectx);
				}
			}
		}
		if (!defined $suppress_ifbraces{$linenr - 1} &&
					$line =~ /\b(if|while|for|else)\b/ &&
					$line !~ /\#\s*if/ &&
					$line !~ /\#\s*else/) {
			my $allowed = 0;

                        # Check the pre-context.
                        if (substr($line, 0, $-[0]) =~ /(\}\s*)$/) {
                            my $pre = $1;

                            if ($line !~ /else/) {
                                print "APW: ALLOWED: pre<$pre> line<$line>\n"
                                    if $dbg_adv_apw;
                                $allowed = 1;
                            }
                        }

			my ($level, $endln, @chunks) =
				ctx_statement_full($linenr, $realcnt, $-[0]);

			# Check the condition.
			my ($cond, $block) = @{$chunks[0]};
                        print "CHECKING<$linenr> cond<$cond> block<$block>\n"
                            if $dbg_adv_checking;
			if (defined $cond) {
				substr($block, 0, length($cond), '');
			}
			if (statement_lines($cond) > 1) {
                            print "APW: ALLOWED: cond<$cond>\n"
                                if $dbg_adv_apw;
                            $allowed = 1;
			}
			if ($block =~/\b(?:if|for|while)\b/) {
                            print "APW: ALLOWED: block<$block>\n"
                                if $dbg_adv_apw;
                            $allowed = 1;
			}
			if (statement_block_size($block) > 1) {
                            print "APW: ALLOWED: lines block<$block>\n"
                                if $dbg_adv_apw;
                            $allowed = 1;
			}
			# Check the post-context.
			if (defined $chunks[1]) {
				my ($cond, $block) = @{$chunks[1]};
				if (defined $cond) {
					substr($block, 0, length($cond), '');
				}
				if ($block =~ /^\s*\{/) {
                                    print "APW: ALLOWED: chunk-1 block<$block>\n"
                                        if $dbg_adv_apw;
                                    $allowed = 1;
				}
			}
                        print "DCS: level=$level block<$block> allowed=$allowed\n"
                            if $dbg_adv_dcs;
			if ($level == 0 && $block !~ /^\s*\{/ && !$allowed) {
				my $herectx = $here . "\n";;
				my $cnt = statement_rawlines($block);

				for (my $n = 0; $n < $cnt; $n++) {
					$herectx .= raw_line($linenr, $n) . "\n";;
				}

				ERROR("braces {} are necessary even for single statement blocks\n" . $herectx);
			}
		}

# no volatiles please
		my $asm_volatile = qr{\b(__asm__|asm)\s+(__volatile__|volatile)\b};
		if ($line =~ /\bvolatile\b/ && $line !~ /$asm_volatile/ &&
                    $line !~ /sig_atomic_t/ &&
                    !ctx_has_comment($first_line, $linenr)) {
			my $msg = "Use of volatile is usually wrong, please add a comment\n" . $herecurr;
                        ERROR($msg);
		}

# warn about #if 0
		if ($line =~ /^.\s*\#\s*if\s+0\b/) {
			ERROR("if this code is redundant consider removing it\n" .
				$herecurr);
		}

# check for needless g_free() checks
		if ($prevline =~ /\bif\s*\(([^\)]*)\)/) {
			my $expr = $1;
			if ($line =~ /\bg_free\(\Q$expr\E\);/) {
				ERROR("g_free(NULL) is safe this check is probably not required\n" . $hereprev);
			}
		}

# warn about #ifdefs in C files
#		if ($line =~ /^.\s*\#\s*if(|n)def/ && ($realfile =~ /\.c$/)) {
#			print "#ifdef in C files should be avoided\n";
#			print "$herecurr";
#			$clean = 0;
#		}

# warn about spacing in #ifdefs
		if ($line =~ /^.\s*\#\s*(ifdef|ifndef|elif)\s\s+/) {
			ERROR("exactly one space required after that #$1\n" . $herecurr);
		}
# check for memory barriers without a comment.
		if ($line =~ /\b(smp_mb|smp_rmb|smp_wmb|smp_read_barrier_depends)\(/) {
			if (!ctx_has_comment($first_line, $linenr)) {
				ERROR("memory barrier without comment\n" . $herecurr);
			}
		}
# check of hardware specific defines
# we have e.g. CONFIG_LINUX and CONFIG_WIN32 for common cases
# where they might be necessary.
		if ($line =~ m@^.\s*\#\s*if.*\b__@) {
			WARN("architecture specific defines should be avoided\n" .  $herecurr);
		}

# Check that the storage class is at the beginning of a declaration
		if ($line =~ /\b$Storage\b/ && $line !~ /^.\s*$Storage\b/) {
			ERROR("storage class should be at the beginning of the declaration\n" . $herecurr)
		}

# check the location of the inline attribute, that it is between
# storage class and type.
		if ($line =~ /\b$Type\s+$Inline\b/ ||
		    $line =~ /\b$Inline\s+$Storage\b/) {
			ERROR("inline keyword should sit between storage class and type\n" . $herecurr);
		}

# check for sizeof(&)
		if ($line =~ /\bsizeof\s*\(\s*\&/) {
			ERROR("sizeof(& should be avoided\n" . $herecurr);
		}

# check for new externs in .c files.
		if ($realfile =~ /\.c$/ && defined $stat &&
		    $stat =~ /^.\s*(?:extern\s+)?$Type\s+($Ident)(\s*)\(/s)
		{
			my $function_name = $1;
			my $paren_space = $2;

			my $s = $stat;
			if (defined $cond) {
				substr($s, 0, length($cond), '');
			}
			if ($s =~ /^\s*;/ &&
			    $function_name ne 'uninitialized_var')
			{
				ERROR("externs should be avoided in .c files\n" .  $herecurr);
			}

			if ($paren_space =~ /\n/) {
				ERROR("arguments for function declarations should follow identifier\n" . $herecurr);
			}

		} elsif ($realfile =~ /\.c$/ && defined $stat &&
		    $stat =~ /^.\s*extern\s+/)
		{
			ERROR("externs should be avoided in .c files\n" .  $herecurr);
		}

# check for pointless casting of g_malloc return
		if ($line =~ /\*\s*\)\s*g_(try)?(m|re)alloc(0?)(_n)?\b/) {
			if ($2 == 'm') {
				ERROR("unnecessary cast may hide bugs, use g_$1new$3 instead\n" . $herecurr);
			} else {
				ERROR("unnecessary cast may hide bugs, use g_$1renew$3 instead\n" . $herecurr);
			}
		}

# check for gcc specific __FUNCTION__
		if ($line =~ /__FUNCTION__/) {
			ERROR("__func__ should be used instead of gcc specific __FUNCTION__\n"  . $herecurr);
		}

# recommend g_path_get_* over g_strdup(basename/dirname(...))
		if ($line =~ /\bg_strdup\s*\(\s*(basename|dirname)\s*\(/) {
			WARN("consider using g_path_get_$1() in preference to g_strdup($1())\n" . $herecurr);
		}

# recommend qemu_strto* over strto* for numeric conversions
		if ($line =~ /\b(strto[^kd].*?)\s*\(/) {
			ERROR("consider using qemu_$1 in preference to $1\n" . $herecurr);
		}
# recommend sigaction over signal for portability, when establishing a handler
		if ($line =~ /\bsignal\s*\(/ && !($line =~ /SIG_(?:IGN|DFL)/)) {
			ERROR("use sigaction to establish signal handlers; signal is not portable\n" . $herecurr);
		}
# check for module_init(), use category-specific init macros explicitly please
		if ($line =~ /^module_init\s*\(/) {
			ERROR("please use block_init(), type_init() etc. instead of module_init()\n" . $herecurr);
		}
# check for various ops structs, ensure they are const.
		my $struct_ops = qr{AIOCBInfo|
				BdrvActionOps|
				BlockDevOps|
				BlockJobDriver|
				DisplayChangeListenerOps|
				GraphicHwOps|
				IDEDMAOps|
				KVMCapabilityInfo|
				MemoryRegionIOMMUOps|
				MemoryRegionOps|
				MemoryRegionPortio|
				QEMUFileOps|
				SCSIBusInfo|
				SCSIReqOps|
				Spice[A-Z][a-zA-Z0-9]*Interface|
				USBDesc[A-Z][a-zA-Z0-9]*|
				VhostOps|
				VMStateDescription|
				VMStateInfo}x;
		if ($line !~ /\bconst\b/ &&
		    $line =~ /\b($struct_ops)\b.*=/) {
			ERROR("initializer for struct $1 should normally be const\n" .
				$herecurr);
		}

# check for %L{u,d,i} in strings
		my $string;
		while ($line =~ /(?:^|")([X\t]*)(?:"|$)/g) {
			$string = substr($rawline, $-[1], $+[1] - $-[1]);
			$string =~ s/%%/__/g;
			if ($string =~ /(?<!%)%L[udi]/) {
				ERROR("\%Ld/%Lu are not-standard C, use %lld/%llu\n" . $herecurr);
				last;
			}
		}

# QEMU specific tests
		if ($rawline =~ /\b(?:Qemu|QEmu)\b/) {
			ERROR("use QEMU instead of Qemu or QEmu\n" . $herecurr);
		}

# Qemu error function tests

	# Find newlines in error messages
	my $qemu_error_funcs = qr{error_setg|
				error_setg_errno|
				error_setg_win32|
				error_setg_file_open|
				error_set|
				error_prepend|
				warn_reportf_err|
				error_reportf_err|
				error_vreport|
				warn_vreport|
				info_vreport|
				error_report|
				warn_report|
				info_report|
				g_test_message}x;

	if ($rawline =~ /\b(?:$qemu_error_funcs)\s*\(.*\".*\\n/) {
		ERROR("Error messages should not contain newlines\n" . $herecurr);
	}

	# Continue checking for error messages that contains newlines. This
	# check handles cases where string literals are spread over multiple lines.
	# Example:
	# error_report("Error msg line #1"
	#              "Error msg line #2\n");
	my $quoted_newline_regex = qr{\+\s*\".*\\n.*\"};
	my $continued_str_literal = qr{\+\s*\".*\"};

	if ($rawline =~ /$quoted_newline_regex/) {
		# Backtrack to first line that does not contain only a quoted literal
		# and assume that it is the start of the statement.
		my $i = $linenr - 2;

		while (($i >= 0) & $rawlines[$i] =~ /$continued_str_literal/) {
			$i--;
		}

		if ($rawlines[$i] =~ /\b(?:$qemu_error_funcs)\s*\(/) {
			ERROR("Error messages should not contain newlines\n" . $herecurr);
		}
	}

# check for non-portable libc calls that have portable alternatives in QEMU
		if ($line =~ /\bffs\(/) {
			ERROR("use ctz32() instead of ffs()\n" . $herecurr);
		}
		if ($line =~ /\bffsl\(/) {
			ERROR("use ctz32() or ctz64() instead of ffsl()\n" . $herecurr);
		}
		if ($line =~ /\bffsll\(/) {
			ERROR("use ctz64() instead of ffsll()\n" . $herecurr);
		}
		if ($line =~ /\bbzero\(/) {
			ERROR("use memset() instead of bzero()\n" . $herecurr);
		}
		my $non_exit_glib_asserts = qr{g_assert_cmpstr|
						g_assert_cmpint|
						g_assert_cmpuint|
						g_assert_cmphex|
						g_assert_cmpfloat|
						g_assert_true|
						g_assert_false|
						g_assert_nonnull|
						g_assert_null|
						g_assert_no_error|
						g_assert_error|
						g_test_assert_expected_messages|
						g_test_trap_assert_passed|
						g_test_trap_assert_stdout|
						g_test_trap_assert_stdout_unmatched|
						g_test_trap_assert_stderr|
						g_test_trap_assert_stderr_unmatched}x;
		if ($realfile !~ /^tests\// &&
			$line =~ /\b(?:$non_exit_glib_asserts)\(/) {
			ERROR("Use g_assert or g_assert_not_reached\n". $herecurr);
		}
	}

	if ($is_patch && $chk_signoff && $signoff == 0) {
		ERROR("Missing Signed-off-by: line(s)\n");
	}

	# If we have no input at all, then there is nothing to report on
	# so just keep quiet.
	if ($#rawlines == -1) {
		return 1;
	}

	# In mailback mode only produce a report in the negative, for
	# things that appear to be patches.
	if ($mailback && ($clean == 1 || !$is_patch)) {
		return 1;
	}

	# This is not a patch, and we are are in 'no-patch' mode so
	# just keep quiet.
	if (!$chk_patch && !$is_patch) {
		return 1;
	}

	if (!$is_patch) {
		ERROR("Does not appear to be a unified-diff format patch\n");
	}

	print report_dump();
	if ($summary && !($clean == 1 && $quiet == 1)) {
		print "$filename " if ($summary_file);
		print "total: $cnt_error errors, $cnt_warn warnings, " .
			"$cnt_lines lines checked\n";
		print "\n" if ($quiet == 0);
	}

	if ($quiet == 0) {
		# If there were whitespace errors which cleanpatch can fix
		# then suggest that.
#		if ($rpt_cleaners) {
#			print "NOTE: whitespace errors detected, you may wish to use scripts/cleanpatch or\n";
#			print "      scripts/cleanfile\n\n";
#		}
	}

	if ($clean == 1 && $quiet == 0) {
		print "$vname has no obvious style problems and is ready for submission.\n"
	}
	if ($clean == 0 && $quiet == 0) {
		print "$vname has style problems, please review.  If any of these errors\n";
		print "are false positives report them to the maintainer, see\n";
		print "CHECKPATCH in MAINTAINERS.\n";
	}

	return ($no_warnings ? $clean : $cnt_error == 0);
}
