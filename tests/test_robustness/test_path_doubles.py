import os

import pytest
from tests.utils import TESTDIR_NAME, bind_mount_a_b, create_file, create_link, run_rmlint, runs_as_root


def test_cmdline():
    create_file('xxx', '1/a')
    # feed rmlint the same file twice via command line
    _, *data, _ = run_rmlint('{t}/1 {t}/1'.format(t=TESTDIR_NAME), use_default_dir=False)
    assert 0 == sum(find['type'] == 'duplicate_file' for find in data)

    _, *data, _ = run_rmlint('{t}/1/a {t}/1/a'.format(t=TESTDIR_NAME), use_default_dir=False)
    assert 0 == sum(find['type'] == 'duplicate_file' for find in data)

    _, *data, _ = run_rmlint('{t}/1 {t}/1/a'.format(t=TESTDIR_NAME), use_default_dir=False)
    assert 0 == sum(find['type'] == 'duplicate_file' for find in data)


def test_symlink_noloop():
    create_file('xxx', '1/a')
    create_link('1/a', '1/link', symlink=True)

    _, *data, _ = run_rmlint(f'{TESTDIR_NAME}/1', use_default_dir=False)
    assert 0 == sum(find['type'] == 'duplicate_file' for find in data)

    _, *data, _ = run_rmlint('{t}/1 {t}/1/a'.format(t=TESTDIR_NAME), use_default_dir=False)
    assert 0 == sum(find['type'] == 'duplicate_file' for find in data)

    _, *data, _ = run_rmlint('{t}/1 {t}/1/link'.format(t=TESTDIR_NAME), use_default_dir=False)
    assert 0 == sum(find['type'] == 'duplicate_file' for find in data)

    _, *data, _ = run_rmlint('{t}/1/a {t}/1/link'.format(t=TESTDIR_NAME), use_default_dir=False)
    assert 0 == sum(find['type'] == 'duplicate_file' for find in data)


def test_symlink_loop():
    create_file('xxx', '1/a')
    create_link('1', '1/link', symlink=True)

    _, *data, _ = run_rmlint(f'{TESTDIR_NAME}/1', use_default_dir=False)
    assert 0 == sum(find['type'] == 'duplicate_file' for find in data)

    _, *data, _ = run_rmlint('{t}/1 {t}/1/link'.format(t=TESTDIR_NAME), use_default_dir=False)
    assert 0 == sum(find['type'] == 'duplicate_file' for find in data)


def test_mount_binds():
    if not runs_as_root():
        pytest.skip("must be run as root (bind-mount)")

    # use a subdirectory as the second run would scan out.json (different path)
    _mnt = 'mnt'
    mnt_root = os.path.join(TESTDIR_NAME, _mnt)

    create_file('xxx', f'{_mnt}/a/b/1')
    create_file('xxx', f'{_mnt}/c/2')

    with bind_mount_a_b(mnt_root):
        create_file('xxx', f'{_mnt}/a/3')
        _, *data, _ = run_rmlint('{r} {r}/a/b -S pa'.format(r=mnt_root), use_default_dir=False)

    assert 3 == sum(find['type'] == 'duplicate_file' for find in data)

    # the actual order is a bit difficult to pin down since files 2
    # and 3 can be reached 2 different ways:
    # <TESTDIR_NAME>/mnt
    # ├── a
    # │   ├── 3*
    # │   └── b
    # │       ├── a
    # │       │   ├── 3
    # │       │   └── b
    # │       │       └── 1
    # │       └── c
    # │           └── 2
    # └── c
    #     └── 2*
    assert data[0]['path'].endswith('/2')
    assert data[1]['path'].endswith('/3')
    assert len(data) == 3
