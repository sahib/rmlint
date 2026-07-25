import errno

import pytest

from tests.utils import CKSUM_TYPES, create_file, run_rmlint_once

# current shredder algorithm does not handle large size-groups at all
# well, due to pre-matching "optimisation"
# https://github.com/SeeSpotRun/rmlint/blob/448cb0c76cbb6178105556ede2bfd864c6f83af3/lib/checksum.c#L678-L730
# which degenerates into an inefficient O(n^2) lookup with large size groups
BLACKLIST = ['paranoid']

@pytest.mark.slow
def test_collision_resistance():
    """Test for at least 20 bits of collision resistance,
    this should detect gross errors in checksum encoding...
    """

    numfiles = 1024*1024
    try:
        for i in range(numfiles):
            create_file(i, str(i), write_binary=True)
    except OSError as e:
        if e.errno == errno.ENOSPC:
            pytest.skip('not enough space in testdir')
        raise

    for algo in CKSUM_TYPES:
        if algo not in BLACKLIST:
            *_, footer = run_rmlint_once(f'--read-buffer-len=4 -a {algo}')
            assert footer['duplicates'] == 0, f'Unexpected hash collision for hash type {algo}'
