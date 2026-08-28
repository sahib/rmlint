import os
import shlex
import subprocess
import sys

import pytest

from tests.utils import (
    RMLINT_BINARY,
    RMLINT_BINARY_DIR,
    create_dirs,
    create_file,
    create_link,
    get_bin_path,
    get_testdir,
    pattern_count,
    run_rmlint,
)


def run_shell_script(shell, script_path, *args):
    return subprocess.check_output(
        (get_bin_path(shell), script_path, *args),
        text=True
    )


def filter_part_of_directory(data):
    return [e for e in data if e['type'] != 'part_of_directory']


def test_basic(shell):
    create_file('xxx', 'a')
    create_file('xxx', 'b')

    create_file('yyy', 'dir_a/a')
    create_file('zzz', 'dir_a/b')

    create_file('zzz', 'dir_b/a')
    create_file('yyy', 'dir_b/b')

    create_file('', 'empty')

    create_file('aaa', 'aaa')
    create_link('aaa', 'link_aaa', symlink=True)
    os.remove(os.path.join(get_testdir(), 'aaa'))

    _, *data, footer = run_rmlint(f'-D -S a -o sh:{get_testdir()}/rmlint.sh')
    data = filter_part_of_directory(data)

    assert footer['duplicate_sets'] == 3
    assert footer['total_lint_size'] == 9
    assert footer['total_files'] == 8 # + 1
    assert footer['duplicates'] == 3

    # Dry run first; check if it did not accidentally delete something.
    sh_path = os.path.join(get_testdir(), 'rmlint.sh')
    text = run_shell_script(shell, sh_path, "-dn")
    _, *data, footer = run_rmlint('-D -S a')
    data = filter_part_of_directory(data)
    assert footer['duplicate_sets'] == 3
    assert footer['total_lint_size'] == 9
    assert footer['total_files'] == 9
    assert footer['duplicates'] == 3

    text = run_shell_script(shell, sh_path, "-d")
    _, *data, footer = run_rmlint('-D -S a')
    data = filter_part_of_directory(data)

    assert footer['duplicate_sets'] == 0
    assert footer['total_lint_size'] == 0
    assert footer['total_files'] == 3
    assert footer['duplicates'] == 0

    assert '/dir_a' in text
    assert '/a' in text


def test_paranoia(shell):
    create_file('xxx', 'a')
    create_file('xxx', 'b')
    create_file('xxx', 'c')
    create_link('a', 'hardlink_a', symlink=False)

    _, *_, footer = run_rmlint(f'-S a -o sh:{get_testdir()}/rmlint.sh')

    assert footer['duplicate_sets'] == 1
    assert footer['total_lint_size'] == 6
    assert footer['total_files'] == 4 # + 1
    assert footer['duplicates'] == 3

    # Modify c after running rmlint:
    with open(os.path.join(get_testdir(), 'c'), 'w', encoding='utf-8') as handle:
        handle.write('xxxx')

    sh_script = os.path.join(get_testdir(), 'rmlint.sh')
    text = run_shell_script(shell, sh_script, '-d', '-p', '-x')

    assert 'files no longer identical' in text

    # Check that file contents of c are still intact
    with open(os.path.join(get_testdir(), 'c'), encoding='utf-8') as handle:
        assert handle.read() == 'xxxx'

    # Change back 'c':
    with open(os.path.join(get_testdir(), 'c'), 'w', encoding='utf-8') as handle:
        handle.write('xxx')

    _, *_, footer = run_rmlint(f'-S a -o sh:{get_testdir()}/rmlint.sh')

    assert footer['duplicate_sets'] == 1
    assert footer['total_lint_size'] == 3
    assert footer['total_files'] == 2 # +1
    assert footer['duplicates'] == 1


    # Remove original:
    os.remove(os.path.join(get_testdir(), 'a'))

    text = run_shell_script(shell, sh_script, '-d', '-p')
    _, *_, footer = run_rmlint(f'-S a -o sh:{get_testdir()}/rmlint.sh')

    assert 'original has disappeared' in text

    assert footer['duplicate_sets'] == 0
    assert footer['total_lint_size'] == 0
    assert footer['total_files'] == 1 # + 1
    assert footer['duplicates'] == 0


def test_anon_pipe():
    create_file('xxx', 'long-dummy-file-1')
    create_file('xxx', 'long-dummy-file-2')

    data = run_rmlint(
        "-o sh:>(cat)",
        force_no_pedantic=True,
        directly_return_output=True,
        use_shell=True
    )

    assert b'/long-dummy-file-1' in data
    assert b'/long-dummy-file-2' in data


def test_hardlink_duplicate_directories(shell):
    create_file('xxx', 'dir_a/x')
    create_file('xxx', 'dir_b/x')

    sh_path = os.path.join(get_testdir(), "result.sh")
    _, *data, _ = run_rmlint(f"-D -S a -c sh:hardlink -o sh:{sh_path}")
    data = filter_part_of_directory(data)
    assert len(data) == 2
    assert data[0]["path"].endswith("dir_a")
    assert data[1]["path"].endswith("dir_b")

    run_shell_script(shell, sh_path, "-d")

    full_dupe_a = os.path.join(get_testdir(), "dir_a/x")
    full_dupe_b = os.path.join(get_testdir(), "dir_b/x")
    assert os.stat(full_dupe_a).st_ino == os.stat(full_dupe_b).st_ino


def _check_if_empty_dirs_deleted(shell, inverse_order, sh_path, data):
    run_shell_script(shell, sh_path, "-dc")

    if inverse_order:
        assert not os.path.exists(data[1]["path"])
        assert not os.path.exists(os.path.join(get_testdir(), "deep/a"))
        assert os.path.exists(data[0]["path"])
    else:
        assert os.path.exists(data[0]["path"])
        assert not os.path.exists(data[1]["path"])


@pytest.mark.parametrize("inverse_order", [False, True])
def test_remove_empty_dirs(shell, inverse_order):
    create_file('xxx', 'deep/a/b/c/d/e/1')
    create_file('xxx', 'deep/x/2')

    sh_path = os.path.join(get_testdir(), "result.sh")
    _, *data, _ = run_rmlint(f"-S {'A' if inverse_order else 'a'} -o sh:{sh_path}")

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
def test_remove_empty_dirs_with_dupe_dirs(shell, inverse_order):
    create_file('xxx', 'deep/a/b/c/d/e/1')
    create_file('xxx', 'deep/x/1')

    sh_path = os.path.join(get_testdir(), "result.sh")
    _, *data, _ = run_rmlint(f"-S {'A' if inverse_order else 'a'} -Dj -o sh:{sh_path}")
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


def test_cleanup_emptydirs(shell):
    create_file('xxx', 'dir1/a')

    # create some ugly dir names
    names = ('escape me [please?]', '上野洋子, 吉野裕司, 浅井裕子 & 河越重義', '天谷大輔', 'Аркона',
             "let's nest",
             "let's nest/a level",
             "let's nest/a level/[or two]",
             )
    for dirname in names:
        create_file('xxx', f'{dirname}/b')

    _, *_, footer = run_rmlint(f'-S a -T df -o sh:{get_testdir()}/rmlint.sh')

    assert footer['duplicate_sets'] == 1
    assert footer['total_lint_size'] == 3 * len(names)
    assert footer['total_files'] == 1 + len(names)
    assert footer['duplicates'] == len(names)

    # run rmlint.sh with -c option (should clean up empty dirs after deleting 'b' files).
    sh_path = os.path.join(get_testdir(), 'rmlint.sh')
    _ = run_shell_script(shell, sh_path, "-dc")

    assert os.path.exists(os.path.join(get_testdir(), 'dir1/a'))

    for dirname in names:
        assert (not os.path.exists(os.path.join(get_testdir(), dirname)))


def test_keep_parent_timestamps(shell):
    create_file('xxx', 'dir/a')
    create_file('xxx', 'dir/b')

    dir_path = os.path.join(get_testdir(), 'dir')
    stat_before = os.stat(dir_path)

    _, *_, footer = run_rmlint(f'-S a -T df -o sh:{get_testdir()}/rmlint.sh')
    assert footer['duplicate_sets'] == 1
    assert footer['total_lint_size'] == 3
    assert footer['total_files'] == 2
    assert footer['duplicates'] == 1

    sh_path = os.path.join(get_testdir(), 'rmlint.sh')
    run_shell_script(shell, sh_path, "-dck")
    stat_after = os.stat(dir_path)

    assert stat_before.st_mtime_ns == stat_after.st_mtime_ns


@pytest.mark.parametrize("tm_opt", ('', '-D'))
def test_skip_hardlinks(tm_opt):
    """regression test for GitHub issue #545"""
    dir_a = create_dirs('a')
    create_file('xxx', 'a/1')
    create_file('yyy', 'a/2')
    dir_b = create_dirs('b')
    create_link('a/2', 'b/2')

    sh_path = os.path.join(get_testdir(), 'rmlint.sh')
    run_rmlint(
        f'-S a -o sh:{shlex.quote(sh_path)} -c sh:hardlink',
        tm_opt,
        dir_a,
        dir_b,
        use_default_dir=False,
    )

    counts = pattern_count(sh_path, ["^cp_hardlink +'", "^skip_hardlink +'"])
    assert counts[0] == 0
    assert counts[1] == 1


def test_binary_path_from_path_lookup():
    """Checks that RMLINT_BINARY is the correct absolute path."""
    if not sys.platform.startswith(("freebsd", "linux", "cygwin")):
        pytest.skip("unsupported platform")

    create_file('xxx', 'a')
    create_file('xxx', 'b')

    sh_path = os.path.join(get_testdir(), 'rmlint.sh')
    env = dict(os.environ, PATH=RMLINT_BINARY_DIR + os.pathsep + os.environ['PATH'])

    subprocess.run(
        ['rmlint', '-S', 'a', '-o', f'sh:{sh_path}', get_testdir()],
        cwd=get_testdir(),
        env=env,
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    with open(sh_path, encoding='utf-8') as handle:
        line = next(l for l in handle if l.startswith('RMLINT_BINARY='))

    assert line.strip() == f'RMLINT_BINARY="{os.path.realpath(RMLINT_BINARY)}"'
