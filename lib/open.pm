package open;
use Carp;
$open::hint_bits = 0x20000;

use vars qw(%layers @layers);

# Populate hash in non-PerlIO case
%layers = (crlf => 1, raw => 0) unless (@layers);

sub import {
    shift;
    die "`use open' needs explicit list of disciplines" unless @_;
    $^H |= $open::hint_bits;
    my ($in,$out) = split(/\0/,(${^OPEN} || '\0'));
    my @in  = split(/\s+/,$in);
    my @out = split(/\s+/,$out);
    while (@_) {
	my $type = shift;
	my $discp = shift;
	my @val;
	foreach my $layer (split(/\s+:?/,$discp)) {
	    unless(exists $layers{$layer}) {
		croak "Unknown discipline layer '$layer'";
	    }
	    push(@val,":$layer");
	    if ($layer =~ /^(crlf|raw)$/) {
		$^H{"open_$type"} = $layer;
	    }
	}
	if ($type eq 'IN') {
	    $in  = join(' ',@val);
	}
	elsif ($type eq 'OUT') {
	    $out = join(' ',@val);
	}
	else {
	    croak "Unknown discipline class '$type'";
	}
    }
    ${^OPEN} = join('\0',$in,$out);
}

1;
__END__

=head1 NAME

open - perl pragma to set default disciplines for input and output

=head1 SYNOPSIS

    use open IN => ":crlf", OUT => ":raw";

=head1 DESCRIPTION

The open pragma is used to declare one or more default disciplines for
I/O operations.  Any open() and readpipe() (aka qx//) operators found
within the lexical scope of this pragma will use the declared defaults.
Neither open() with an explicit set of disciplines, nor sysopen() are
influenced by this pragma.

Only the two pseudo-disciplines ":raw" and ":crlf" are currently
available.

The ":raw" discipline corresponds to "binary mode" and the ":crlf"
discipline corresponds to "text mode" on platforms that distinguish
between the two modes when opening files (which is many DOS-like
platforms, including Windows).  These two disciplines are currently
no-ops on platforms where binmode() is a no-op, but will be
supported everywhere in future.

=head1 UNIMPLEMENTED FUNCTIONALITY

Full-fledged support for I/O disciplines is currently unimplemented.
When they are eventually supported, this pragma will serve as one of
the interfaces to declare default disciplines for all I/O.

In future, any default disciplines declared by this pragma will be
available by the special discipline name ":DEFAULT", and could be used
within handle constructors that allow disciplines to be specified.
This would make it possible to stack new disciplines over the default
ones.

    open FH, "<:para :DEFAULT", $file or die "can't open $file: $!";

Socket and directory handles will also support disciplines in
future.

Full support for I/O disciplines will enable all of the supported
disciplines to work on all platforms.

=head1 SEE ALSO

L<perlfunc/"binmode">, L<perlfunc/"open">, L<perlunicode>

=cut
