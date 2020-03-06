#! /usr/bin/env perl

#   Copyright (C) 1999, 2000, 2001, 2003 Free Software Foundation, Inc.

# This file is part of GCC.

# GCC is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.

# GCC is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with GCC; see the file COPYING.  If not,
# see <http://www.gnu.org/licenses/>.

# This does trivial (and I mean _trivial_) conversion of Texinfo
# markup to Perl POD format.  It's intended to be used to extract
# something suitable for a manpage from a Texinfo document.

use warnings;

$output = 0;
$skipping = 0;
%sects = ();
$section = "";
@icstack = ();
@endwstack = ();
@skstack = ();
@instack = ();
$shift = "";
%defs = ();
$fnno = 1;
$inf = "";
$ibase = "";
@ipath = ();
$encoding = undef;
@args = ();

while ($_ = shift) {
    if (/^-D(.*)$/) {
	if ($1 ne "") {
	    $flag = $1;
	} else {
	    $flag = shift;
	}
	$value = "";
	($flag, $value) = ($flag =~ /^([^=]+)(?:=(.+))?/);
	die "no flag specified for -D\n"
	    unless $flag ne "";
	die "flags may only contain letters, digits, hyphens, dashes and underscores\n"
	    unless $flag =~ /^[a-zA-Z0-9_-]+$/;
	$defs{$flag} = $value;
    } elsif (/^-I(.*)$/) {
	if ($1 ne "") {
	    $flag = $1;
	} else {
	    $flag = shift;
	}
        push (@ipath, $flag);
    } elsif (/^-/) {
	usage();
    } else {
	$in = $_, next unless defined $in;
	$out = $_, next unless defined $out;
	usage();
    }
}

if (defined $in) {
    $inf = gensym();
    open($inf, "<$in") or die "opening \"$in\": $!\n";
    $ibase = $1 if $in =~ m|^(.+)/[^/]+$|;
} else {
    $inf = \*STDIN;
}

if (defined $out) {
    open(STDOUT, ">$out") or die "opening \"$out\": $!\n";
}

while(defined $inf) {
while(<$inf>) {
    # Certain commands are discarded without further processing.
    /^\@(?:
	 [a-z]+index		# @*index: useful only in complete manual
	 |need			# @need: useful only in printed manual
	 |(?:end\s+)?group	# @group .. @end group: ditto
	 |page			# @page: ditto
	 |node			# @node: useful only in .info file
	 |(?:end\s+)?ifnottex   # @ifnottex .. @end ifnottex: use contents
	)\b/x and next;

    chomp;

    # Look for filename and title markers.
    /^\@setfilename\s+([^.]+)/ and $fn = $1, next;
    /^\@settitle\s+([^.]+)/ and $tl = postprocess($1), next;

    # Look for document encoding
    /^\@documentencoding\s+([^.]+)/ and do {
        $encoding = $1 unless defined $encoding;
        next;
    };

    # Identify a man title but keep only the one we are interested in.
    /^\@c\s+man\s+title\s+([A-Za-z0-9-]+)\s+(.+)/ and do {
	if (exists $defs{$1}) {
	    $fn = $1;
	    $tl = postprocess($2);
	}
	next;
    };

    # Look for blocks surrounded by @c man begin SECTION ... @c man end.
    # This really oughta be @ifman ... @end ifman and the like, but such
    # would require rev'ing all other Texinfo translators.
    /^\@c\s+man\s+begin\s+([A-Z]+)\s+([A-Za-z0-9-]+)/ and do {
	$output = 1 if exists $defs{$2};
        $sect = $1;
	next;
    };
    /^\@c\s+man\s+begin\s+([A-Z]+)/ and $sect = $1, $output = 1, next;
    /^\@c\s+man\s+end/ and do {
	$sects{$sect} = "" unless exists $sects{$sect};
	$sects{$sect} .= postprocess($section);
	$section = "";
	$output = 0;
	next;
    };

    # handle variables
    /^\@set\s+([a-zA-Z0-9_-]+)\s*(.*)$/ and do {
	$defs{$1} = $2;
	next;
    };
    /^\@clear\s+([a-zA-Z0-9_-]+)/ and do {
	delete $defs{$1};
	next;
    };

    # Single line command handlers.

    /^\@include\s+(.+)$/ and do {
	push @instack, $inf;
	$inf = gensym();
	$file = postprocess($1);

	# Try cwd and $ibase, then explicit -I paths.
	$done = 0;
	foreach $path ("", $ibase, @ipath) {
	    $mypath = $file;
	    $mypath = $path . "/" . $mypath if ($path ne "");
	    open($inf, "<" . $mypath) and ($done = 1, last);
	}
	die "cannot find $file" if !$done;
	next;
    };

    next unless $output;

    # Discard comments.  (Can't do it above, because then we'd never see
    # @c man lines.)
    /^\@c\b/ and next;

    # End-block handler goes up here because it needs to operate even
    # if we are skipping.
    /^\@end\s+([a-z]+)/ and do {
	# Ignore @end foo, where foo is not an operation which may
	# cause us to skip, if we are presently skipping.
	my $ended = $1;
	next if $skipping && $ended !~ /^(?:ifset|ifclear|ignore|menu|iftex|copying)$/;

	die "\@end $ended without \@$ended at line $.\n" unless defined $endw;
	die "\@$endw ended by \@end $ended at line $.\n" unless $ended eq $endw;

	$endw = pop @endwstack;

	if ($ended =~ /^(?:ifset|ifclear|ignore|menu|iftex)$/) {
	    $skipping = pop @skstack;
	    next;
	} elsif ($ended =~ /^(?:example|smallexample|display
                            |quotation|deftp|deftypefn)$/x) {
	    $shift = "";
	    $_ = "";	# need a paragraph break
	} elsif ($ended =~ /^(?:itemize|enumerate|[fv]?table)$/) {
	    $_ = "\n=back\n";
	    $ic = pop @icstack;
	} elsif ($ended eq "multitable") {
	    $_ = "\n=back\n";
	} else {
	    die "unknown command \@end $ended at line $.\n";
	}
    };

    # We must handle commands which can cause skipping even while we
    # are skipping, otherwise we will not process nested conditionals
    # correctly.
    /^\@ifset\s+([a-zA-Z0-9_-]+)/ and do {
	push @endwstack, $endw;
	push @skstack, $skipping;
	$endw = "ifset";
	$skipping = 1 unless exists $defs{$1};
	next;
    };

    /^\@ifclear\s+([a-zA-Z0-9_-]+)/ and do {
	push @endwstack, $endw;
	push @skstack, $skipping;
	$endw = "ifclear";
	$skipping = 1 if exists $defs{$1};
	next;
    };

    /^\@(ignore|menu|iftex|copying)\b/ and do {
	push @endwstack, $endw;
	push @skstack, $skipping;
	$endw = $1;
	$skipping = 1;
	next;
    };

    next if $skipping;

    # Character entities.  First the ones that can be replaced by raw text
    # or discarded outright:
    s/\@copyright\{\}/(c)/g;
    s/\@dots\{\}/.../g;
    s/\@enddots\{\}/..../g;
    s/\@([.!? ])/$1/g;
    s/\@[:-]//g;
    s/\@bullet(?:\{\})?/*/g;
    s/\@TeX\{\}/TeX/g;
    s/\@pounds\{\}/\#/g;
    s/\@minus(?:\{\})?/-/g;
    s/\\,/,/g;

    # Now the ones that have to be replaced by special escapes
    # (which will be turned back into text by unmunge())
    s/&/&amp;/g;
    s/\@\{/&lbrace;/g;
    s/\@\}/&rbrace;/g;
    s/\@\@/&at;/g;

    # Inside a verbatim block, handle @var specially.
    if ($shift ne "") {
	s/\@var\{([^\}]*)\}/<$1>/g;
    }

    # POD doesn't interpret E<> inside a verbatim block.
    if ($shift eq "") {
	s/</&lt;/g;
	s/>/&gt;/g;
    } else {
	s/</&LT;/g;
	s/>/&GT;/g;
    }

    /^\@(?:section|unnumbered|unnumberedsec|center)\s+(.+)$/
	and $_ = "\n=head2 $1\n";
    /^\@subsection\s+(.+)$/
	and $_ = "\n=head3 $1\n";
    /^\@subsubsection\s+(.+)$/
	and $_ = "\n=head4 $1\n";

    # Block command handlers:
    /^\@itemize(?:\s+(\@[a-z]+|\*|-))?/ and do {
	push @endwstack, $endw;
	push @icstack, $ic;
	if (defined $1) {
	    $ic = $1;
	} else {
	    $ic = '*';
	}
	$_ = "\n=over 4\n";
	$endw = "itemize";
    };

    /^\@enumerate(?:\s+([a-zA-Z0-9]+))?/ and do {
	push @endwstack, $endw;
	push @icstack, $ic;
	if (defined $1) {
	    $ic = $1 . ".";
	} else {
	    $ic = "1.";
	}
	$_ = "\n=over 4\n";
	$endw = "enumerate";
    };

    /^\@multitable\s.*/ and do {
	push @endwstack, $endw;
	$endw = "multitable";
	$_ = "\n=over 4\n";
    };

    /^\@([fv]?table)\s+(\@[a-z]+)/ and do {
	push @endwstack, $endw;
	push @icstack, $ic;
	$endw = $1;
	$ic = $2;
	$ic =~ s/\@(?:samp|strong|key|gcctabopt|option|env)/B/;
	$ic =~ s/\@(?:code|kbd)/C/;
	$ic =~ s/\@(?:dfn|var|emph|cite|i)/I/;
	$ic =~ s/\@(?:file)/F/;
	$ic =~ s/\@(?:asis)//;
	$_ = "\n=over 4\n";
    };

    /^\@((?:small)?example|display)/ and do {
	push @endwstack, $endw;
	$endw = $1;
	$shift = "\t";
	$_ = "";	# need a paragraph break
    };

    /^\@item\s+(.*\S)\s*$/ and $endw eq "multitable" and do {
	@columns = ();
	for $column (split (/\s*\@tab\s*/, $1)) {
	    # @strong{...} is used a @headitem work-alike
	    $column =~ s/^\@strong\{(.*)\}$/$1/;
	    push @columns, $column;
	}
	$_ = "\n=item ".join (" : ", @columns)."\n";
    };

    /^\@(quotation)\s*(.+)?$/ and do {
        push @endwstack, $endw;
        $endw = $1;
        $_ = "\n$2:"
    };

    /^{(.*)}$|^(.*)$/ and $#args > 0 and do {
        $kind = $args[0];
        $arguments = $1 // "";
        if ($endw eq "deftypefn") {
            $ret = $args[1];
            $fname = "B<$args[2]>";
            $_ = $ret ? "$ret " : "";
            $_ .= "$fname $arguments ($kind)";
        } else {
            $_ = "B<$args[1]> ($kind)\n\n$arguments";
        }
        @args = ();
    };

    /^\@(deftp)\s*(.+)?$/ and do {
        push @endwstack, $endw;
        $endw = $1;
        $arg = $2;
        $arg =~ s/{([^}]*)}/$1/g;
        $arg =~ s/\@$//;
        @args = split (/ /, $arg);
        $_ = "";
    };

    /^\@(deftypefn)\s*(.+)?$/ and do {
        push @endwstack, $endw;
        $endw = $1;
        $arg = $2;
        $arg =~ s/{([^}]*)}/$1/g;
        $arg =~ s/\@$//;
        @args = split (/ /, $arg);
        $_ = "";
    };

    /^\@itemx?\s*(.+)?$/ and do {
	if (defined $1) {
            if ($ic eq "") {
                $_ = "\n=item $1\n";
            } else {
                # Entity escapes prevent munging by the <> processing below.
                $_ = "\n=item $ic\&LT;$1\&GT;\n";
            }
	} else {
	    $_ = "\n=item $ic\n";
	    $ic =~ y/A-Ya-y/B-Zb-z/;
	    $ic =~ s/(\d+)/$1 + 1/eg;
	}
    };

    $section .= $shift.$_."\n";
}
# End of current file.
close($inf);
$inf = pop @instack;
}

die "No filename or title\n" unless defined $fn && defined $tl;

print "=encoding $encoding\n\n" if defined $encoding;

$sects{NAME} = "$fn \- $tl\n";
$sects{FOOTNOTES} .= "=back\n" if exists $sects{FOOTNOTES};

for $sect (qw(NAME SYNOPSIS DESCRIPTION OPTIONS ENVIRONMENT FILES
	      BUGS NOTES FOOTNOTES EXAMPLES SEEALSO AUTHOR COPYRIGHT)) {
    if(exists $sects{$sect}) {
	$head = $sect;
	$head =~ s/SEEALSO/SEE ALSO/;
	print "=head1 $head\n\n";
	print scalar unmunge ($sects{$sect});
	print "\n";
    }
}

sub usage
{
    die "usage: $0 [-D toggle...] [infile [outfile]]\n";
}

sub postprocess
{
    local $_ = $_[0];

    # @value{foo} is replaced by whatever 'foo' is defined as.
    while (m/(\@value\{([a-zA-Z0-9_-]+)\})/g) {
	if (! exists $defs{$2}) {
	    print STDERR "Option $2 not defined\n";
	    s/\Q$1\E//;
	} else {
	    $value = $defs{$2};
	    s/\Q$1\E/$value/;
	}
    }

    # Formatting commands.
    # Temporary escape for @r.
    s/\@r\{([^\}]*)\}/R<$1>/g;
    s/\@(?:dfn|var|emph|cite|i)\{([^\}]*)\}/I<$1>/g;
    s/\@(?:code|kbd)\{([^\}]*)\}/C<$1>/g;
    s/\@(?:gccoptlist|samp|strong|key|option|env|command|b)\{([^\}]*)\}/B<$1>/g;
    s/\@sc\{([^\}]*)\}/\U$1/g;
    s/\@file\{([^\}]*)\}/F<$1>/g;
    s/\@w\{([^\}]*)\}/S<$1>/g;
    s/\@t\{([^\}]*)\}/$1/g;
    s/\@(?:dmn|math)\{([^\}]*)\}/$1/g;

    # keep references of the form @ref{...}, print them bold
    s/\@(?:ref)\{([^\}]*)\}/B<$1>/g;

    # Change double single quotes to double quotes.
    s/''/"/g;
    s/``/"/g;

    # Cross references are thrown away, as are @noindent and @refill.
    # (@noindent is impossible in .pod, and @refill is unnecessary.)
    # @* is also impossible in .pod; we discard it and any newline that
    # follows it.  Similarly, our macro @gol must be discarded.

    s/\(?\@xref\{(?:[^\}]*)\}(?:[^.<]|(?:<[^<>]*>))*\.\)?//g;
    s/\s+\(\@pxref\{(?:[^\}]*)\}\)//g;
    s/;\s+\@pxref\{(?:[^\}]*)\}//g;
    s/\@noindent\s*//g;
    s/\@refill//g;
    s/\@gol//g;
    s/\@\*\s*\n?//g;

    # Anchors are thrown away
    s/\@anchor\{(?:[^\}]*)\}//g;

    # @uref can take one, two, or three arguments, with different
    # semantics each time.  @url and @email are just like @uref with
    # one argument, for our purposes.
    s/\@(?:uref|url|email)\{([^\},]*)\}/&lt;B<$1>&gt;/g;
    s/\@uref\{([^\},]*),([^\},]*)\}/$2 (C<$1>)/g;
    s/\@uref\{([^\},]*),([^\},]*),([^\},]*)\}/$3/g;

    # Un-escape <> at this point.
    s/&LT;/</g;
    s/&GT;/>/g;

    # Now un-nest all B<>, I<>, R<>.  Theoretically we could have
    # indefinitely deep nesting; in practice, one level suffices.
    1 while s/([BIR])<([^<>]*)([BIR])<([^<>]*)>/$1<$2>$3<$4>$1</g;

    # Replace R<...> with bare ...; eliminate empty markup, B<>;
    # shift white space at the ends of [BI]<...> expressions outside
    # the expression.
    s/R<([^<>]*)>/$1/g;
    s/[BI]<>//g;
    s/([BI])<(\s+)([^>]+)>/$2$1<$3>/g;
    s/([BI])<([^>]+?)(\s+)>/$1<$2>$3/g;

    # Extract footnotes.  This has to be done after all other
    # processing because otherwise the regexp will choke on formatting
    # inside @footnote.
    while (/\@footnote/g) {
	s/\@footnote\{([^\}]+)\}/[$fnno]/;
	add_footnote($1, $fnno);
	$fnno++;
    }

    return $_;
}

sub unmunge
{
    # Replace escaped symbols with their equivalents.
    local $_ = $_[0];

    s/&lt;/E<lt>/g;
    s/&gt;/E<gt>/g;
    s/&lbrace;/\{/g;
    s/&rbrace;/\}/g;
    s/&at;/\@/g;
    s/&amp;/&/g;
    return $_;
}

sub add_footnote
{
    unless (exists $sects{FOOTNOTES}) {
	$sects{FOOTNOTES} = "\n=over 4\n\n";
    }

    $sects{FOOTNOTES} .= "=item $fnno.\n\n"; $fnno++;
    $sects{FOOTNOTES} .= $_[0];
    $sects{FOOTNOTES} .= "\n\n";
}

# stolen from Symbol.pm
{
    my $genseq = 0;
    sub gensym
    {
	my $name = "GEN" . $genseq++;
	my $ref = \*{$name};
	delete $::{$name};
	return $ref;
    }
}
