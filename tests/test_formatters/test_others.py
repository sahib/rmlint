import os
import subprocess

from tests.utils import TESTDIR_NAME, create_file, run_rmlint


def test_just_call_it():
    create_file('1234', 'a')
    create_file('1234', 'b')

    # This test is more or less here to make sure some util functions
    # are called from our tests. We don't test any results; basically
    # only if they fatally crash or create valgrind errors.
    # Also, you shouldn't see any output on the test run.
    run_rmlint(
        '-S a', outputs=['fdupes', 'stamp', 'progressbar', 'summary', 'pretty', 'py']
    )

    # Check if the -g option does weird things. (i.e. segfault)
    subprocess.check_output(['./rmlint', '-g', '-c', 'progressbar:ascii', TESTDIR_NAME])
    subprocess.check_output(['./rmlint', '-g', '-c', 'progressbar:fancy', TESTDIR_NAME])
    subprocess.check_output(['./rmlint', '-g',  '-O' , 'fdupes', TESTDIR_NAME])
    subprocess.check_output(['./rmlint', '-g', TESTDIR_NAME])

    for silly_option in ['-ppp', '-PPPP']:
        try:
            subprocess.check_output(['./rmlint', '-VV', silly_option, TESTDIR_NAME])
        except subprocess.CalledProcessError:
            pass
        else:
            assert False


def test_fdups_and_traversed_dirs_in_summary():
    # Traversed directories should not be listed as lint by fdupes nor
    # counted in the summary.

    create_file('xxx', 'dir_a/1')
    create_file('xxx', 'dir_b/1')
    create_file('', 'empty')

    _, *data, _, fdupes, summary = run_rmlint(
        '-S a', outputs=['fdupes', 'summary']
    )

    assert len(data) == 3

    # this tree has no empty dirs, so nothing listed should be a directory
    listed = [line.strip() for line in fdupes.splitlines() if line.strip()]
    assert [path for path in listed if os.path.isdir(path)] == []

    # the empty file is the only valid count, not the directories
    assert '1 other suspicious item(s)' in summary
