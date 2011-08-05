#!/usr//bin/perl

use strict;
use warnings;

use FindBin;
use TestLibFinder;
use lib test_lib_loc();


# Test Modules
use CRI::Test;
use Torque::Job::Ctrl          qw( 
                                    submitCheckpointJob
                                    runJobs 
                                    delJobs
                                 );
use Torque::Util::Qstat qw(
                                    qstat_fx
                                 );
use Torque::Util::Regexp       qw(
                                    CHECKPOINT_FILE_NAME
                                 );


# Test Description
plan('no_plan');
setDesc("qchkpt");

# Variables
my $cmd;
my %qchkpt;
my $job_info;
my $checkpoint_name;
my $checkpoint_path;

# Submit a job
my $params = {
              'user'       => $props->get_property('User.1'),
              'app'        => "$FindBin::Bin/../../test_programs/test.pl"
             };

my $job_id = submitCheckpointJob($params);

# Run the job
runJobs($job_id);

# Wait a few seconds
sleep_diag 2;

# Test qchkpt
$cmd      = "qchkpt $job_id";
%qchkpt   = runCommand($cmd, test_success => 1);

# Check every second for two minutes for the checkpoint to be created
my $count = 0;
while ($count < 120 and ! defined $checkpoint_name)
  {

  sleep 1;


  $job_info = qstat_fx({job_id => $job_id});

  if (defined $job_info->{ $job_id }{ 'checkpoint_name' })
    {
    $checkpoint_name = $job_info->{ $job_id }{ 'checkpoint_name' }; 
    }

  $count++;

  } # END while ($count < 120 and $checkpoint_name)

# Make sure that we just didn't run out of time
if (! defined $checkpoint_name)
  {

  # Delete the job
  delJobs($job_id);

  logMsg("'checkpoint_name' attribute not found for job '$job_id'");
  exit 1;

  } # END if (! defined $checkpoint_name)

# Perform the basic tests
ok($checkpoint_name  =~ /${\CHECKPOINT_FILE_NAME}/,  "Checking the 'checkpoint_name' attribute of the job");
ok(exists $job_info->{ $job_id }{ 'checkpoint_time' }, "Checking for the existence of the job attribute 'checkpoint_time'");

# Check for the actual file 
$checkpoint_path = $props->get_property('Torque.Home.Dir') . "/checkpoint/${job_id}.CK/$checkpoint_name";
ok(-e $checkpoint_path, "Checking that '$checkpoint_path' exists");

# Delete the job
delJobs($job_id);
