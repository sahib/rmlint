#!/bin/bash

# script for git bisect to help pinpoint speed regressions
# NOTE: You probably need to make a copy of this script to run git bisect with;
#      git is likely to delete the original script during its iterations!!

# how to use:
# $ git bisect start
# $ git checkout <slow_commit>
#    # compile and time running of rmlint test run; note time
# $ git bisect bad
# $ git checkout <fast_commit>
#    # compile and time running of rmlint test run; note time
# $ git bisect good
#    # edit this script and set appropriate $cutoff_time and $rmlint_cmd (below)
# $ git bisect run /path/to/this/script

##### CONFIG ######
#threshold run time for good vs bad (in centiseconds, eg 7 seconds = 700):
cutoff_time=700
# the rmlint command to run...
rmlint_cmd="./rmlint -o pretty:/dev/null /usr -V"
###################

# compile...
scons -j4 2>&1 >/dev/null # optional: add DEBUG=1
if [ $? -ne 0 ]; then
    echo "Compile error: SKIPPING"
    exit 125 # (signal for git bisect skip)
fi

# drop caches to get reproducible results
sync
echo 3 > /proc/sys/vm/drop_caches
if [ $? -ne 0 ]; then
    echo "\n**** Warning: could not drop caches (need to be root?)"
fi

# run...
echo -n "Compiled ok... running speed test..."
runtime=`(/usr/bin/time -f %e /bin/bash -c "$rmlint_cmd 2>/dev/null ") 2>&1`
echo -n " completed in $runtime seconds: "

# test result...
runtime=${runtime//.}  # convert to integer as centiseconds
if [ "$runtime" -lt "$cutoff_time" ]; then
    echo GOOD
    exit 0 # (signal for git bisect good)
else
    echo BAD
    exit 1 # (anything from 1..127 except 125 is signal for git bisect bad)
fi
