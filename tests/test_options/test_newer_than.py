import os
import time

from tests.utils import create_file, get_testdir, run_rmlint_once, warp_file_to_future

# Note: This test has the assumpption that it runs fast enough.
#       The threshold should be 1 second, so rather hard to reach.
#       This will not work with PEDANTIC=1 & run_rmlint_pedantic:
#       -n will update the timestamp after each run, causing
#       unexpected results.


def create_set(create_stamp, iso8601=False):
    # Basic case, should work always:
    create_file('xxx', 'a')
    create_file('xxx', 'b')

    arguments = '-S a'
    if iso8601:
        arguments += ' -c stamp:iso8601'

    _, *data, _, _ = run_rmlint_once(
        arguments, outputs=('stamp',) if create_stamp else ('pretty',)
    )
    assert len(data) == 2

    create_file('xxx', 'c')
    warp_file_to_future('c', 2)


def test_simple():
    create_set(False)
    now = time.time()
    _, *data, _ = run_rmlint_once('-S a -N ' + str(time.time()))
    assert len(data) == 3

    # Fake a wait of two seconds a try an ISO8601.
    # Should return all 3 files, since c is newer and pulls in a + b.
    for offset, expect in [(1, 3), (8, 0)]:
        iso_time = time.strftime(
            "%Y-%m-%dT%H:%M:%S%z",
            time.localtime(now + offset)
        )

        _, *data, _ = run_rmlint_once('-S a -N ' + iso_time)
        assert len(data) == expect


def test_stamp_file():
    create_set(True, False)

    # Wait 3 seconds, so the new stamp file (written by -n)
    # will contain a more recent timestamp, which will yield to 0 results
    # in the third run.
    time.sleep(3)

    _, *data, _ = run_rmlint_once('-S a -n ' + os.path.join(get_testdir(), '.stamp-0'))
    assert len(data) == 3

    _, *data, _ = run_rmlint_once('-S a -n ' + os.path.join(get_testdir(), '.stamp-0'))
    assert len(data) == 0


def test_stamp_file_iso8601():
    create_set(True, True)

    _, *data, _ = run_rmlint_once('-S a -n ' + os.path.join(get_testdir(), '.stamp-0'))
    assert len(data) == 3
