from nose import with_setup
from tests.utils import *
import json


@with_setup(usual_setup_func, usual_teardown_func)
def test_stdin_read():
    path_a = create_file('1234', 'a') + '\n'
    path_b = create_file('1234', 'b') + '\n'
    path_c = create_file('1234', '.hidden') + '\n'

    proc = subprocess.Popen(
        ['./rmlint', '-', TESTDIR_NAME, '-o', 'json', '-S', 'a', '--hidden'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        universal_newlines=True
    )
    data, _ = proc.communicate(path_a + path_b + path_c)
    head, *data, footer = json.loads(data)

    assert data[0]['path'].endswith('.hidden')
    assert data[1]['path'].endswith('a')
    assert data[2]['path'].endswith('b')
    assert footer['total_lint_size'] == 8
