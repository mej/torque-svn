#!/usr/bin/perl
use strict;
use warnings;

use FindBin;
use TestLibFinder;
use lib test_lib_loc();

use CRI::Test;
use Torque::Ctrl;
use Torque::Job::Ctrl   qw(
                            submitSleepJob
                            runJobs
                            delJobs
                          );
use Torque::Util qw(
                            verify_job_state
                          );
plan('no_plan');
setDesc('pbs_mom -q');

my $sleep_time   = 60;
my $job_params   = {
                     'user'       => $props->get_property('User.1'),
                     'sleep_time' => $sleep_time
                   };
my $process_name = "sleep $sleep_time";

# Commands
my $ps_cmd      = "ps aux | grep '$process_name' | grep -v grep | grep ' R '";
my @pbs_moms    = split(/,/, $props->get_property('Torque.Remote.Nodes'));
my $pbsmom_href = {
                   'nodes'      => \@pbs_moms,
                   'local_node' => 1,
                  };

stopPbsmom($pbsmom_href);
delete $pbsmom_href->{nodes};
startPbsmom($pbsmom_href);
syncServerMom();

my $job_id = submitSleepJob($job_params);
runJobs($job_id);

stopPbsmom($pbsmom_href);

verify_job_state({ 
                   'job_id'        => $job_id,
                   'exp_job_state' => 'R',
                   'wait_time'     => 2 * $sleep_time
                });

# Start pbs_mom and verify that the job is now queued
$pbsmom_href->{args} = '-q';
startPbsmom($pbsmom_href);
syncServerMom();

# Check that the job is queued
verify_job_state({ 
                   'job_id'        => $job_id,
                   'exp_job_state' => 'Q',
                   'wait_time'     => 2 * $sleep_time
                });

# Check for the process
my %ps = runCommand($ps_cmd, 'test_fail' => 1);

delJobs($job_id);
$pbsmom_href->{nodes} = \@pbs_moms;
delete $pbsmom_href->{args};
stopPbsmom($pbsmom_href);
startPbsmom($pbsmom_href);
syncServerMom();
