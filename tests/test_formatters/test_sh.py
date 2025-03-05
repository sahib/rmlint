#!/usr/bin/env python3
# encoding: utf-8
from tests.utils import *

import subprocess

import pytest


def run_shell_script(shell, sh_path, *args):
    return subprocess.check_output([
            os.path.join("/bin", shell),
            sh_path,
        ] + list(args),
        shell=False
    ).decode("utf-8")


def filter_part_of_directory(data):
    return [e for e in data if e['type'] != 'part_of_directory']


def test_basic(usual_setup_usual_teardown, shell):
    create_file('xxx', 'a')
    create_file('xxx', 'b')

    create_file('yyy', 'dir_a/a')
    create_file('zzz', 'dir_a/b')

    create_file('zzz', 'dir_b/a')
    create_file('yyy', 'dir_b/b')

    create_file('', 'empty')

    create_file('aaa', 'aaa')
    create_link('aaa', 'link_aaa', symlink=True)
    os.remove(os.path.join(TESTDIR_NAME, 'aaa'))

    head, *data, footer = run_rmlint('-D -S a -o sh:{t}/rmlint.sh'.format(t=TESTDIR_NAME))
    data = filter_part_of_directory(data)
    # subprocess.call('cat ' + os.path.join(TESTDIR_NAME, 'rmlint.sh'), shell=True)

    assert footer['duplicate_sets'] == 3
    assert footer['total_lint_size'] == 9
    assert footer['total_files'] == 8 # + 1
    assert footer['duplicates'] == 3

    # Dry run first; check if it did not accidentally delete something.
    sh_path = os.path.join(TESTDIR_NAME, 'rmlint.sh')
    text = run_shell_script(shell, sh_path, "-dn")
    head, *data, footer = run_rmlint('-D -S a')
    data = filter_part_of_directory(data)
    assert footer['duplicate_sets'] == 3
    assert footer['total_lint_size'] == 9
    assert footer['total_files'] == 9
    assert footer['duplicates'] == 3

    text = run_shell_script(shell, sh_path, "-d")
    head, *data, footer = run_rmlint('-D -S a')
    data = filter_part_of_directory(data)

    assert footer['duplicate_sets'] == 0
    assert footer['total_lint_size'] == 0
    assert footer['total_files'] == 3
    assert footer['duplicates'] == 0

    assert '/dir_a' in text
    assert '/a' in text


def test_paranoia(usual_setup_usual_teardown, shell):
    create_file('xxx', 'a')
    create_file('xxx', 'b')
    create_file('xxx', 'c')
    create_link('a', 'hardlink_a', symlink=False)

    head, *data, footer = run_rmlint(
        '-S a -o sh:{t}/rmlint.sh'.format(t=TESTDIR_NAME)
    )

    assert footer['duplicate_sets'] == 1
    assert footer['total_lint_size'] == 6
    assert footer['total_files'] == 4 # + 1
    assert footer['duplicates'] == 3

    # Modify c after running rmlint:
    with open(os.path.join(TESTDIR_NAME, 'c'), 'w') as handle:
        handle.write('xxxx')

    sh_script = os.path.join(TESTDIR_NAME, 'rmlint.sh')
    text = run_shell_script(shell, sh_script, '-d', '-p', '-x')

    assert 'files no longer identical' in text

    # Check that file contents of c are still intact
    with open(os.path.join(TESTDIR_NAME, 'c'), 'r') as handle:
        assert handle.read() == 'xxxx'

    # Change back 'c':
    with open(os.path.join(TESTDIR_NAME, 'c'), 'w') as handle:
        handle.write('xxx')

    head, *data, footer = run_rmlint(
        '-S a -o sh:{t}/rmlint.sh'.format(t=TESTDIR_NAME)
    )

    assert footer['duplicate_sets'] == 1
    assert footer['total_lint_size'] == 3
    assert footer['total_files'] == 2 # +1
    assert footer['duplicates'] == 1


    # Remove original:
    os.remove(os.path.join(TESTDIR_NAME, 'a'))

    text = run_shell_script(shell, sh_script, '-d', '-p')
    head, *data, footer = run_rmlint('-S a -o sh:{t}/rmlint.sh'.format(t=TESTDIR_NAME))

    assert 'original has disappeared' in text

    assert footer['duplicate_sets'] == 0
    assert footer['total_lint_size'] == 0
    assert footer['total_files'] == 1 # + 1
    assert footer['duplicates'] == 0


def test_anon_pipe(usual_setup_usual_teardown):
    create_file('xxx', 'long-dummy-file-1')
    create_file('xxx', 'long-dummy-file-2')

    data = run_rmlint(
        "-o sh:>(cat)",
        force_no_pendantic=True,
        directly_return_output=True,
        use_shell=True
    )

    assert b'/long-dummy-file-1' in data
    assert b'/long-dummy-file-2' in data


def test_hardlink_duplicate_directories(usual_setup_usual_teardown, shell):
    create_file('xxx', 'dir_a/x')
    create_file('xxx', 'dir_b/x')

    sh_path = os.path.join(TESTDIR_NAME, "result.sh")
    header, *data, footer = run_rmlint(
        "-D -S a -c sh:hardlink -o sh:{}".format(sh_path),
    )
    data = filter_part_of_directory(data)
    assert len(data) == 2
    assert data[0]["path"].endswith("dir_a")
    assert data[1]["path"].endswith("dir_b")

    run_shell_script(shell, sh_path, "-d")

    full_dupe_a = os.path.join(TESTDIR_NAME, "dir_a/x")
    full_dupe_b = os.path.join(TESTDIR_NAME, "dir_b/x")
    assert os.stat(full_dupe_a).st_ino == os.stat(full_dupe_b).st_ino


def _check_if_empty_dirs_deleted(shell, inverse_order, sh_path, data):
    run_shell_script(shell, sh_path, "-dc")

    if inverse_order:
        assert not os.path.exists(data[1]["path"])
        assert not os.path.exists(os.path.join(TESTDIR_NAME, "deep/a"))
        assert os.path.exists(data[0]["path"])
    else:
        assert os.path.exists(data[0]["path"])
        assert not os.path.exists(data[1]["path"])


@pytest.mark.parametrize("inverse_order", [False, True])
def test_remove_empty_dirs(usual_setup_usual_teardown, shell, inverse_order):
    create_file('xxx', 'deep/a/b/c/d/e/1')
    create_file('xxx', 'deep/x/2')

    sh_path = os.path.join(TESTDIR_NAME, "result.sh")
    header, *data, footer = run_rmlint(
        "-S {} -o sh:{}".format(
            "A" if inverse_order else "a",
            sh_path
        ),
    )

    assert len(data) == 2

    if inverse_order:
        assert data[0]["path"].endswith("x/2")
        assert data[0]["is_original"] is True
        assert data[1]["path"].endswith("e/1")
        assert data[1]["is_original"] is False
    else:
        assert data[0]["path"].endswith("e/1")
        assert data[0]["is_original"] is True
        assert data[1]["path"].endswith("x/2")
        assert data[1]["is_original"] is False

    _check_if_empty_dirs_deleted(shell, inverse_order, sh_path, data)


@pytest.mark.parametrize("inverse_order", [False, True])
def test_remove_empty_dirs_with_dupe_dirs(usual_setup_usual_teardown, shell, inverse_order):
    create_file('xxx', 'deep/a/b/c/d/e/1')
    create_file('xxx', 'deep/x/1')

    sh_path = os.path.join(TESTDIR_NAME, "result.sh")
    header, *data, footer = run_rmlint(
        "-S {} -Dj -o sh:{}".format(
            "A" if inverse_order else "a",
            sh_path
        ),
    )
    data = filter_part_of_directory(data)

    assert len(data) == 2

    if inverse_order:
        assert data[0]["path"].endswith("x")
        assert data[0]["is_original"] is True
        assert data[1]["path"].endswith("e")
        assert data[1]["is_original"] is False
    else:
        assert data[0]["path"].endswith("e")
        assert data[0]["is_original"] is True
        assert data[1]["path"].endswith("x")
        assert data[1]["is_original"] is False

    _check_if_empty_dirs_deleted(shell, inverse_order, sh_path, data)

def test_cleanup_emptydirs(usual_setup_usual_teardown, shell):
    create_file('xxx', 'dir1/a')

    # create some ugly dir names
    names = [ 'escape me [please?]', '上野洋子, 吉野裕司, 浅井裕子 & 河越重義', '天谷大輔', 'Аркона',
            'let\'s nest',
            'let\'s nest/a level',
            'let\'s nest/a level/[or two]' ]
    for dirname in names:
        create_file('xxx', '{}/b'.format(dirname))

    head, *data, footer = run_rmlint('-S a -T df -o sh:{t}/rmlint.sh'.format(t=TESTDIR_NAME))

    assert footer['duplicate_sets'] == 1
    assert footer['total_lint_size'] == 3 * len(names)
    assert footer['total_files'] == 1 + len(names)
    assert footer['duplicates'] == len(names)

    # run rmlint.sh with -c option (should clean up empty dirs after deleting 'b' files).
    sh_path = os.path.join(TESTDIR_NAME, 'rmlint.sh')
    text = run_shell_script(shell, sh_path, "-dc")

    assert os.path.exists(os.path.join(TESTDIR_NAME, 'dir1/a'))

    for dirname in names:
        assert (not os.path.exists(os.path.join(TESTDIR_NAME, dirname)))



def test_keep_parent_timestamps(usual_setup_usual_teardown, shell):
    create_file('xxx', 'dir/a')
    create_file('xxx', 'dir/b')

    dir_path = os.path.join(TESTDIR_NAME, 'dir')
    stat_before = os.stat(dir_path)

    head, *data, footer = run_rmlint('-S a -T df -o sh:{t}/rmlint.sh'.format(t=TESTDIR_NAME))
    assert footer['duplicate_sets'] == 1
    assert footer['total_lint_size'] == 3
    assert footer['total_files'] == 2
    assert footer['duplicates'] == 1

    sh_path = os.path.join(TESTDIR_NAME, 'rmlint.sh')
    run_shell_script(shell, sh_path, "-dck")
    stat_after = os.stat(dir_path)

    assert stat_before.st_mtime == stat_after.st_mtime
