#!/usr/bin/perl 

use CRI::Test;
plan('no_plan');
use strict;
use warnings;
setDesc('RELEASE pbs_mom Compatibility Tests (TORQUE 2.3)');

my $testbase = $props->get_property('test.base') . "torque/commands/pbs_mom";

execute_tests("$testbase/setup.t") 
  or die('Could not setup pbs_mom tests');

execute_tests(
              "$testbase/pbs_mom.t",
              "$testbase/pbs_mom_a.t",
              "$testbase/pbs_mom_c.t",
              "$testbase/pbs_mom_C.t",
              "$testbase/pbs_mom_d.t",
#              "$testbase/pbs_mom_h.t",        # This is not implemented yet
              "$testbase/pbs_mom_L.t",
              "$testbase/pbs_mom_M.t",
              "$testbase/pbs_mom_R.t",
#              "$testbase/pbs_mom_default_p.t",
#              "$testbase/pbs_mom_default_r.t", # This is the default for 2.3
              "$testbase/pbs_mom_p.t",
#              "$testbase/pbs_mom_q.t",        # This is not implemented in 2.3
#              "$testbase/pbs_mom_r.t",
              "$testbase/pbs_mom_x.t",
             ); 

execute_tests("$testbase/cleanup.t"); 

