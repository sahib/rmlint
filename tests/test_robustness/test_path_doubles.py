#!/usr/bin/env python3
# encoding: utf-8
from nose import with_setup
from tests.utils import *


@with_setup(usual_setup_func, usual_teardown_func)
def test_cmdline():
    create_file('xxx', '1/a')
    # feed rmlint the same file twice via command line
    head, *data, footer = run_rmlint('{t}/1 {t}/1'.format(t=TESTDIR_NAME), use_default_dir=False)
    assert 0 == sum(find['type'] == 'duplicate_file' for find in data)

    head, *data, footer = run_rmlint('{t}/1/a {t}/1/a'.format(t=TESTDIR_NAME), use_default_dir=False)
    assert 0 == sum(find['type'] == 'duplicate_file' for find in data)

    head, *data, footer = run_rmlint('{t}/1 {t}/1/a'.format(t=TESTDIR_NAME), use_default_dir=False)
    assert 0 == sum(find['type'] == 'duplicate_file' for find in data)


@with_setup(usual_setup_func, usual_teardown_func)
def test_symlink_noloop():
    create_file('xxx', '1/a')
    create_link('1/a', '1/link', symlink=True)

    head, *data, footer = run_rmlint('{t}/1'.format(t=TESTDIR_NAME), use_default_dir=False)
    assert 0 == sum(find['type'] == 'duplicate_file' for find in data)

    head, *data, footer = run_rmlint('{t}/1 {t}/1/a'.format(t=TESTDIR_NAME), use_default_dir=False)
    assert 0 == sum(find['type'] == 'duplicate_file' for find in data)

    head, *data, footer = run_rmlint('{t}/1 {t}/1/link'.format(t=TESTDIR_NAME), use_default_dir=False)
    assert 0 == sum(find['type'] == 'duplicate_file' for find in data)

    head, *data, footer = run_rmlint('{t}/1/a {t}/1/link'.format(t=TESTDIR_NAME), use_default_dir=False)
    assert 0 == sum(find['type'] == 'duplicate_file' for find in data)

@with_setup(usual_setup_func, usual_teardown_func)
def test_symlink_loop():
    create_file('xxx', '1/a')
    create_link('1', '1/link', symlink=True)

    head, *data, footer = run_rmlint('{t}/1'.format(t=TESTDIR_NAME), use_default_dir=False)
    assert 0 == sum(find['type'] == 'duplicate_file' for find in data)

    head, *data, footer = run_rmlint('{t}/1 {t}/1/link'.format(t=TESTDIR_NAME), use_default_dir=False)
    assert 0 == sum(find['type'] == 'duplicate_file' for find in data)


def mount_bind_teardown_func():
    if runs_as_root():
        subprocess.call(
            'umount {dst}'.format(
                dst=os.path.join(TESTDIR_NAME, 'a/b')
            ),
            shell=True
        )

    usual_teardown_func()

@with_setup(usual_setup_func, mount_bind_teardown_func)
def test_mount_binds():
    if not runs_as_root():
        return

    create_file('xxx', 'a/b/1')
    create_file('xxx', 'c/2')

    subprocess.call(
        'mount --rbind {src} {dst}'.format(
            src=TESTDIR_NAME,
            dst=os.path.join(TESTDIR_NAME, 'a/b')
        ),
        shell=True
    )
    create_file('xxx', 'a/3')

    head, *data, footer = run_rmlint('{t} {t}/a/b -S pa'.format(t=TESTDIR_NAME), use_default_dir=False)

    assert 3 == sum(find['type'] == 'duplicate_file' for find in data)

    # the actual order is a bit difficult to pin down since files 2
    # and 3 can be reached 2 different ways:
    # /tmp/rmlint-unit-testdir
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

