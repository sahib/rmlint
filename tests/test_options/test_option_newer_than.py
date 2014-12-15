from nose import with_setup
from tests.utils import *

import time

@with_setup(usual_setup_func, usual_teardown_func)
def test_simple():
    # Basic case, should work always:
    create_file('xxx', 'a')
    create_file('xxx', 'b')
    head, *data, footer = run_rmlint('-S a')
    assert len(data) == 2

    # Wait a second, create a new dupe.
    # Should report all three of them since at least one is newer.
    time.sleep(1)
    create_file('xxx', 'c')
    head, *data, footer = run_rmlint('-S a -N ' + str(time.time()))
    assert len(data) == 3

    # Fake a wait of two seconds a try a ISO8601.
    # Should return nothing, since nothing new.
    iso_time = time.strftime(
        "%Y-%m-%dT%H:%M:%S%z",
        time.gmtime(time.time() + 2)
    )

    head, *data, footer = run_rmlint('-VVV -S a -N ' + iso_time)
    assert len(data) == 0
