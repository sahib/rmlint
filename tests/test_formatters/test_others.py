import os
import subprocess

from tests.utils import RMLINT_BINARY, create_file, get_testdir, run_rmlint


def test_just_call_it():
    create_file('1234', 'a')
    create_file('1234', 'b')

    # This test is more or less here to make sure some util functions
    # are called from our tests. We don't test any results; basically
    # only if they fatally crash or create valgrind errors.
    # Also, you shouldn't see any output on the test run.
    run_rmlint(
        '-S a',
        outputs=['fdupes', 'stamp', 'progressbar', 'summary', 'pretty', 'py'],
        uses_py_formatter=True,
    )

    def call(*args):
        return subprocess.check_output(
            [RMLINT_BINARY, *args, get_testdir()], cwd=get_testdir()
        )

    # Check if the -g option does weird things. (i.e. segfault)
    call('-g', '-c', 'progressbar:ascii')
    call('-g', '-c', 'progressbar:fancy')
    call('-g', '-O', 'fdupes')
    call('-g')

    for silly_option in ['-ppp', '-PPPP']:
        try:
            call('-VV', silly_option)
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
