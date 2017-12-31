#!/usr/bin/env python3
# encoding: utf-8
from nose.plugins.attrib import attr
from nose import with_setup
from tests.utils import *


# current shredder algorithm does not handle large size-groups at all
# well, due to pre-matching "optimisation"
# https://github.com/SeeSpotRun/rmlint/blob/448cb0c76cbb6178105556ede2bfd864c6f83af3/lib/checksum.c#L678-L730
# which degenerates into an inefficient O(n^2) lookup with large size groups
BLACKLIST = ['paranoid']

@attr('slow')
@with_setup(usual_setup_func, usual_teardown_func)
def test_collision_resistance():
    # test for at least 20 bits of collision resistancel
    # this should detect gross errors in checksum encoding...

    numfiles = 1024*1024
    for i in range(numfiles):
        create_file(i, str(i), write_binary=True)

    for algo in CKSUM_TYPES:
        if algo not in BLACKLIST:
            *_, footer = run_rmlint('--read-buffer-len=4 -a {}'.format(algo))
            assert footer['duplicates'] == 0, 'Unexpected hash collision for hash type {}'.format(algo)
