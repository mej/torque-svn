#!/usr/bin/perl

use strict;
use warnings;

use FindBin;
use TestLibFinder;
use lib test_lib_loc();


# Test Modules
use CRI::Test;

# Test Description
plan('no_plan');
setDesc('Momctl -p <PORT> -c <JOBID>');

# Variables
my %momctl;
my $host = 'localhost';
my $port = $props->get_property('mom.host.port');

# Create some stale jobs
my $job_id = 'all'; # stubed out

%momctl = runCommand("momctl -p $port -c $job_id", test_success_die => 1);

my $stdout = $momctl{ 'STDOUT' };
ok($stdout =~ /job clear request successful on ${host}/i, "Checking output of momctl -p $port -c $job_id");

