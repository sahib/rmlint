from nose import with_setup
from tests.utils import *
import json


@with_setup(usual_setup_func, usual_teardown_func)
def test_stdin_read():
    path_a = create_file('1234', 'a')
    path_b = create_file('1234', 'b')
    path_c = create_file('1234', '.hidden')

    head, *data, footer = run_rmlint('-S a -D --partial-hidden', path_a, path_b, path_c)

    # Hidden is well, hidden due to --partial-hidden and -D.
    assert data[0]['path'].endswith('a')
    assert data[1]['path'].endswith('b')
    assert footer['total_lint_size'] == 8
