from nose import with_setup
from tests.utils import *


def create_files():
    # Same size, different content.
    create_file('a', '1.a')
    create_file('b', '1.b')

    # Same size, same content.
    create_file('a', '2.a')
    create_file('a', '2.a_')

    # Different content and size
    create_file('a' * 3, '3.a')
    create_file('a' * 4, '3.a')

    # duplicate_dirs + with --write_unfinished
    create_file('x', 'dir_a/1')
    create_file('x', 'dir_b/1')


def check(data, write_cache):
    unfinished = [p['path'] for p in data if p['type'] == 'unfinished_cksum']
    dupe_files = [p['path'] for p in data if p['type'] == 'duplicate_file']
    dupe_trees = [p['path'] for p in data if p['type'] == 'duplicate_dir']

    path_in = lambda name, paths: os.path.join(TESTDIR_NAME, name) in paths

    if write_cache:
        assert len(unfinished) == 3
        assert path_in('1.b', unfinished)
        assert path_in('dir_a/1', unfinished)
        assert path_in('dir_b/1', unfinished)

    assert len(dupe_trees) == 2
    assert path_in('dir_a', dupe_trees)
    assert path_in('dir_b', dupe_trees)

    assert len(dupe_files) == 3
    assert path_in('2.a', dupe_files)
    assert path_in('2.a_', dupe_files)
    assert path_in('1.a', dupe_files)


@with_setup(usual_setup_func, usual_teardown_func)
def test_cache():
    create_files()

    for write_cache in True, False:
        cache_path = os.path.join(TESTDIR_NAME, 'cache.json')

        if write_cache:
            head, *data, footer = run_rmlint('-U -D -S pa -o json:{c}'.format(c=cache_path))
        else:
            head, *data, footer = run_rmlint('-D -S pa -C {c}'.format(c=cache_path))

        check(data, write_cache)


@with_setup(usual_setup_func, usual_teardown_func)
def test_xattr():
    create_files()

    for _ in range(2):
        for write_cache in True, False:
            if write_cache:
                head, *data, footer = run_rmlint('-U -D -S pa --xattr-write')
            else:
                head, *data, footer = run_rmlint('-D -S pa --xattr-read')

            check(data, write_cache)

        head, *data, footer = run_rmlint('-D -S pa --xattr-clear')


@with_setup(usual_setup_func, usual_teardown_func)
def test_bad_cache_file():
    create_files()

    # Valid json, but pointless cache.
    create_file('{"blurb": 1}', 'badcache')

    def run_cache(name, should_work):
        try:
            run_rmlint('-D -S pa -C {c}'.format(c=name))
        except subprocess.CalledProcessError:
            assert not should_work
        else:
            assert should_work

    run_cache(os.path.join(TESTDIR_NAME, 'badcache'), True)
    run_cache('/bin/bash', True)
    run_cache('/nope', False)
