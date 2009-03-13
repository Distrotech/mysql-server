#!/usr/bin/perl
# -*- cperl -*-

use strict;
use warnings;
use IO::File;
use File::Basename;
use File::Copy;
use File::Path;
use Cwd;

my $currentdir = getcwd();
die("You should call this script from the mysql-test/ directory")
  unless basename($currentdir) eq "mysql-test";

create_suite_from_file("../storage/ndb/test/run-test/daily-basic-tests.txt");
create_suite_from_file("../storage/ndb/test/run-test/daily-devel-tests.txt");

sub create_suite_from_file {
  my ($test_file) = @_;

  my $name = basename($test_file, ".txt");
  my $suite = "suite/$name";

  print "Reading tests from: '$test_file'...\n";
  my $F= IO::File->new($test_file)
    or die "Could not open '$test_file' for reading: $!";


  my @tests;
  my $test= {};
  while ( my $line= <$F> )
  {
    chomp($line);

    next if ( $line =~ /^#/);

    if ( $line =~ /(.*): (.*)/ )
    {
      $test->{$1} = $2;
      next;
    }

    if ( $line eq "")
    {
      create_test_name($test);
      push(@tests, $test);
      $test = {};
    }
  }
  $F = undef; # Close input file
  print "ok!\n";

  print "Creating suite '$suite' ...\n";
  if (-d $suite)
  {
    rmtree($suite)
      or die ("Could not remove old dir: '$suite', error: $!");
  }
  mkdir $suite
    or die "Could not create directory: $suite, error: $!";
  print "ok!\n";

  print "Copying my.cnf ...\n";
  my $mycnf =  dirname($test_file)."/mtr.cnf";
  print " from '$mycnf'\n";
  copy($mycnf, "$suite/my.cnf")
    or die ("Could not copy '$mycnf' to '$suite/my.cnf', error: $!");
  print "ok!\n";

  print "Generating .test files ...\n";
  foreach my $test ( @tests)
  {
    my $name = $test->{name};
    my $cmd = $test->{cmd};
    my $path = "../storage/ndb/test/ndbapi/$cmd";
    if (!-x $path)
    {
      print "Could not find: '$path', skipping it \n";
      next;
    }

    my $args = $test->{args} || "";
    my $file = "$suite/$name.test";

    my $out= IO::File->new($file, "w")
      or die "Could not open '$file' for writing: $!";

    print $out "result_format 2;\n\n";

    # DbAsyncGenerator need files created by DbCreate
    if ($cmd eq "DbAsyncGenerator")
    {
      print $out "## DbCreate\n";
      print $out "--exec ../storage/ndb/test/ndbapi/DbCreate 2>&1\n";
    }

    print $out "## $cmd $args\n";
    print $out "--exec $path $args 2>&1\n";
    print $out "exit;\n";
  }

  print "ok!\n";
}

sub create_test_name {
  my ($test) = @_;
  my $cmd = $test->{cmd} or die;
  my $args = $test->{args} || "";
  my $name= "$cmd";
  $name .= $args if $args;
  # Remove -[option]'s
  # TODO could probably remove some more junk and still get a unique name
  $name=~ s/-.//g;
  # Replace all spaces with underscore
  $name=~ s/[[:space:]]/_/g;

  $test->{name} = $name;
}



