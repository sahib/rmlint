import os
import subprocess

import pytest

from tests.utils import create_file, create_link, get_testdir, run_rmlint


def _check_interpreter(interpreter):
    try:
        subprocess.call([interpreter, "-c", "1 + 1"])
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False


def run_py_script(interpreter):
    return subprocess.check_output(
        [interpreter, os.path.join(get_testdir(), 'rmlint.py'), '-d', '-p'],
        cwd=get_testdir(),
    ).decode('utf-8')


# TODO: test for different Python3 versions
@pytest.mark.parametrize("interpreter", ("python3",))
def test_paranoia(interpreter):
    if not _check_interpreter(interpreter):
        pytest.skip(f"Interpreter {interpreter} does not seem to be working, skipping test")

    create_file('xxx', 'a')
    create_file('xxx', 'b')
    create_file('xxx', 'c')
    create_file('xxx', 'd')
    create_link('a', 'hardlink_a', symlink=False)

    _, *_, footer = run_rmlint(
        f'-S a -o py:{get_testdir()}/rmlint.py', uses_py_formatter=True
    )

    assert footer['duplicate_sets'] == 1
    assert footer['total_lint_size'] == 9
    assert footer['total_files'] == 5 # 1 is ignored as own output
    assert footer['duplicates'] == 4

    with open(os.path.join(get_testdir(), 'b'), 'w', encoding='utf-8') as handle:
        handle.write('yyy')

    with open(os.path.join(get_testdir(), 'c'), 'w', encoding='utf-8') as handle:
        handle.write('xxxx')

    text = run_py_script(interpreter)

    os.remove(os.path.join(get_testdir(), "rmlint.py"))
    _, *_, footer = run_rmlint(
        f'-S a -o py:{get_testdir()}/rmlint.py', uses_py_formatter=True
    )

    assert footer['duplicate_sets'] == 1
    assert footer['total_lint_size'] == 0
    assert footer['total_files'] == 4 # + 1
    assert footer['duplicates'] == 1

    assert 'Content differs' in text
    assert 'Size differs' in text
    assert 'Same inode' in text

    run_py_script(interpreter)
    os.remove(os.path.join(get_testdir(), "rmlint.py"))
    _, *_, footer = run_rmlint(
        f'-S a -o py:{get_testdir()}/rmlint.py', uses_py_formatter=True
    )

    # Nothing should change.
    assert footer['duplicate_sets'] == 1
    assert footer['total_lint_size'] == 0
    assert footer['total_files'] == 4 # + 1
    assert footer['duplicates'] == 1


@pytest.mark.parametrize("interpreter", ("python3",))
def test_no_progress(interpreter):
    """Check that the Python script doesn't chock on missing progress"""
    if not _check_interpreter(interpreter):
        pytest.skip(f"Interpreter {interpreter} does not seem to be working, skipping test")

    create_file('', 'dir/empty_a')
    create_file('', 'dir/empty_b')

    _, *data, _ = run_rmlint(
        f'-T "none +ef" -o py:{get_testdir()}/rmlint.py',
        dir_suffix='dir', uses_py_formatter=True
    )
    assert len(data) == 2

    # XXX: for now rmlint output per-file progress only
    # for duplicates. We should not have any here.
    assert not any('progress' in item for item in data)

    # just check that the script does not chock on missing progress
    assert 'Deleting empty file:' in run_py_script(interpreter)
