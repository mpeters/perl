#!/usr/bin/perl -w                                         # -*- perl -*-

BEGIN {
    require "t/pod2html-lib.pl";
}

use strict;
use Test::More tests => 2;
use File::Spec::Functions;

use File::Spec;
use Cwd;

my $cwd = Cwd::cwd();
# XXX Is there a better way to do this? I need a relative url to cwd because of
# --podpath and --podroot
# Remove root dir from path
my $relcwd = substr($cwd, length(File::Spec->rootdir()));

my $data_pos = tell DATA; # to read <DATA> twice

my $htmldir = catdir $cwd, 't', '';

convert_n_test("htmldir3", "test --htmldir and --htmlroot 3a", 
 "--podpath=$relcwd",
 "--podroot=/",
 "--htmldir=$htmldir", # test removal trailing slash
);

seek DATA, $data_pos, 0; # to read <DATA> twice (expected output is the same)

my $podpath = catdir $relcwd, 't';

convert_n_test("htmldir3", "test --htmldir and --htmlroot 3b", 
 "--podpath=$podpath",
 "--podroot=/",
 "--htmldir=t",
 "--outfile=t/htmldir3.html",
);

__DATA__
<?xml version="1.0" ?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<title></title>
<meta http-equiv="content-type" content="text/html; charset=utf-8" />
<link rev="made" href="mailto:[PERLADMIN]" />
</head>

<body style="background-color: white">



<ul id="index">
  <li><a href="#NAME">NAME</a></li>
  <li><a href="#LINKS">LINKS</a></li>
</ul>

<h1 id="NAME">NAME</h1>

<p>htmldir - Test --htmldir feature</p>

<h1 id="LINKS">LINKS</h1>

<p>Normal text, a <a>link</a> to nowhere,</p>

<p>a link to <a href="[RELCURRENTWORKINGDIRECTORY]/test.lib/perlvar.html">perlvar</a>,</p>

<p><a href="[RELCURRENTWORKINGDIRECTORY]/t/htmlescp.html">htmlescp</a>,</p>

<p><a href="[RELCURRENTWORKINGDIRECTORY]/t/htmlfeature.html#Another-Head-1">&quot;Another Head 1&quot; in htmlfeature</a>,</p>

<p>and another <a href="[RELCURRENTWORKINGDIRECTORY]/t/htmlfeature.html#Another-Head-1">&quot;Another Head 1&quot; in htmlfeature</a>.</p>


</body>

</html>

