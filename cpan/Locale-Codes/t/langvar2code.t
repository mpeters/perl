#!/usr/bin/perl -w

require 5.002;

$runtests=shift(@ARGV);
if ( -f "t/testfunc.pl" ) {
  require "t/testfunc.pl";
  $dir="./lib";
  $tdir="t";
} elsif ( -f "testfunc.pl" ) {
  require "testfunc.pl";
  $dir="../lib";
  $tdir=".";
} else {
  die "ERROR: cannot find testfunc.pl\n";
}

unshift(@INC,$dir);
use Locale::Codes::LangVar;

%type = ( "LOCALE_LANGVAR_ALPHA"    => LOCALE_LANGVAR_ALPHA,
          "LOCALE_LANGVAR_NUMERIC"  => LOCALE_LANGVAR_NUMERIC,
        );

sub test {
   my(@test) = @_;
   $test[1]  = $type{$test[1]}
     if (@test == 2  &&  $test[1]  &&  exists $type{$test[1]});
   return langvar2code(@test);
}

$tests = "

_blank_ ~ _undef_

Eastern Armenian
   ~
   arevela

";

print "langvar2code...\n";
test_Func(\&test,$tests,$runtests);

1;
# Local Variables:
# mode: cperl
# indent-tabs-mode: nil
# cperl-indent-level: 3
# cperl-continued-statement-offset: 2
# cperl-continued-brace-offset: 0
# cperl-brace-offset: 0
# cperl-brace-imaginary-offset: 0
# cperl-label-offset: -2
# End:
