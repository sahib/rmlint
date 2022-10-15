#!/usr/bin/env python3
# encoding: utf-8

import os
import time
from itertools import combinations, permutations
from pprint import pformat

from nose import with_setup
from nose.plugins.attrib import attr
from nose.plugins.skip import SkipTest
from parameterized import parameterized
from tests.utils import *

PATHS = ['b_dir/', 'a_dir/', 'c_dir/']


def path_index(file_path):
    for idx, path in enumerate(PATHS):
        if path in file_path:
            return idx
    assert False


def path_depth(file_path):
    return len([c for c in file_path if c == '/'])


# dispatcher for comparison tests
def validate_order(data, tests):
    testfuncs = {
        's': lambda g: sum([p['size'] for p in g]),
        'a': lambda g: os.path.basename(g[0]['path']).lower(),
        'm': lambda g: g[0]['mtime'],
        'p': lambda g: path_index(g[0]['path']),
        'n': lambda g: len(g)
    }

    groups, group = [], []
    for point in data:
        if point['is_original']:
            group = []
            groups.append(group)

        group.append(point)

    assert len(groups) == 2

    for a, b in combinations(groups, 2):
        for test in tests:
            cmp_a, cmp_b = (testfuncs[test.lower()](e) for e in [a, b])

            # Equal? Try again.
            if cmp_a == cmp_b:
                continue

            a_comes_first = test.islower()

            if (cmp_a < cmp_b) and a_comes_first:
                break

            if (cmp_b < cmp_a) and not a_comes_first:
                break

            # Something's wrong.
            assert False


@with_setup(usual_setup_func, usual_teardown_func)
def test_replay_match_basename():
    create_file('xxx', 'test1/a')
    create_file('xxx', 'test1/b')
    create_file('xxx', 'test2/a')

    replay_path = os.path.join(TESTDIR_NAME, 'replay.json')
    head, *data, footer = run_rmlint(with_json='replay.json')

    assert len(data) == 3

    head, *data, footer = run_rmlint('--replay {p}'.format(
        p=replay_path
    ))

    assert len(data) == 3

    head, *data, footer = run_rmlint('--replay {p} --match-basename'.format(
        p=replay_path
    ))

    assert len(data) == 2
    paths = set([p['path'] for p in data])
    assert os.path.join(TESTDIR_NAME, 'test1/a') in paths
    assert os.path.join(TESTDIR_NAME, 'test2/a') in paths

    head, *data, footer = run_rmlint('--replay {p} --unmatched-basename'.format(
        p=replay_path
    ))

    assert len(data) == 3


@with_setup(usual_setup_func, usual_teardown_func)
def test_replay_hidden():
    create_file('xxx', 'test/.a')
    create_file('xxx', 'test/.b')

    replay_path = os.path.join(TESTDIR_NAME, 'replay.json')
    head, *data, footer = run_rmlint('--hidden', with_json='replay.json')

    assert len(data) == 2

    head, *data, footer = run_rmlint('--replay {p}'.format(
        p=replay_path
    ))

    assert len(data) == 0

    head, *data, footer = run_rmlint('--replay {p} --hidden'.format(
        p=replay_path
    ))

    assert len(data) == 2


@with_setup(usual_setup_func, usual_teardown_func)
def test_replay_must_match_tagged():
    create_file('xxx', 'test_a/a')
    create_file('xxx', 'test_b/a')

    replay_path = os.path.join(TESTDIR_NAME, 'replay.json')
    head, *data, footer = run_rmlint(with_json='replay.json')

    assert len(data) == 2

    head, *data, footer = run_rmlint('--replay {p} {b} // {a} -m'.format(
        p=replay_path,
        a=os.path.join(TESTDIR_NAME, 'test_a'),
        b=os.path.join(TESTDIR_NAME, 'test_b')
    ))

    paths = set([(p['path'], p['is_original']) for p in data])
    assert (os.path.join(TESTDIR_NAME, 'test_b/a'), False) in paths
    assert (os.path.join(TESTDIR_NAME, 'test_a/a'), True) in paths


@attr('slow')
@with_setup(usual_setup_func, usual_teardown_func)
def test_sorting():
    # create some dupes with different PATHS, names and mtimes:
    create_file('xxx', PATHS[0] + 'a')
    create_file('xxx', PATHS[1] + 'bb')
    create_file('xxx', PATHS[2] + 'ccc')

    # Make sure it takes some time to re-reun
    time.sleep(1.25)
    create_file('xxxx', PATHS[0] + 'A')
    create_file('xxxx', PATHS[1] + 'B')
    create_file('xxxx', PATHS[2] + 'C')
    create_file('xxxx', PATHS[2] + 'D')

    joiner = ' ' + TESTDIR_NAME + '/'
    search_paths = joiner + joiner.join(PATHS)

    # Leave out 'o' for now, since its not really worth testing.
    opts = 'sampn'
    all_opts = opts + opts.upper()

    combos = []
    is_legal_combo = lambda x: len(x) == len(set(x.lower()))

    # Limit to 3-tuple combinations. 4-5-tuple combinations (just do +1)
    # are possible if you have enough time (=> ~(10! / 2) tests).
    for n_terms in range(1, len(opts) - 1):
        combos += filter(
            is_legal_combo,
            (''.join(p) for p in permutations(all_opts, n_terms))
        )

    replay_path = os.path.join(TESTDIR_NAME, 'replay.json')

    for combo in combos:
        combo_str = '-y ' + combo
        head, *data, footer = run_rmlint(
            combo_str + search_paths,
            with_json='replay.json', use_default_dir=False,
        )
        assert len(data) == 7

        validate_order(data, combo)

        head, *data, footer = run_rmlint(
            combo_str + search_paths, ' --replay ' + replay_path,
            use_default_dir=False
        )

        assert len(data) == 7

        validate_order(data, combo)


@with_setup(usual_setup_func, usual_teardown_func)
def test_replay_no_dir():
    # Regression test for #305.
    # --replay did not replay anything when not specifying some path.
    # (The current working directory was not set in this case correctly)

    create_file('xxx', 'sub/a')
    create_file('xxx', 'sub/b')
    create_file('xxx', 'c')

    current_cwd = os.getcwd()

    try:
        os.chdir(TESTDIR_NAME)
        replay_path = os.path.join(TESTDIR_NAME, 'replay.json')
        head, *data, footer = run_rmlint(
            with_json='replay.json', use_default_dir=False,
        )
        assert len(data) == 3

        head, *data, footer = run_rmlint(
                '--replay {}'.format(replay_path),
                use_default_dir=False,
        )
        assert len(data) == 3
    finally:
        os.chdir(current_cwd)


@with_setup(usual_setup_func, usual_teardown_func)
def test_replay_unicode_fuckup():
    names = '上野洋子, 吉野裕司, 浅井裕子 & 河越重義', '天谷大輔', 'Аркона'

    create_file('xxx', names[0])
    create_file('xxx', names[1])
    create_file('xxx', names[2])

    replay_path = os.path.join(TESTDIR_NAME, 'replay.json')

    head, *data, footer = run_rmlint(with_json='replay.json')
    assert len(data) == 3
    assert set([os.path.basename(e['path']) for e in data]) == set(names)

    head, *data, footer = run_rmlint('--replay {p}'.format(p=replay_path))
    assert len(data) == 3
    assert set([os.path.basename(e['path']) for e in data]) == set(names)


@with_setup(usual_setup_func, usual_teardown_func)
def test_replay_tagged_order():
    create_file('xxx', 'a/1')
    create_file('xxx', 'a/2')
    create_file('xxx', 'b/1')
    create_file('xxx', 'b/2')

    replay_path_a = os.path.join(TESTDIR_NAME, 'replay-a.json')
    replay_path_b = os.path.join(TESTDIR_NAME, 'replay-b.json')

    # Create replay-a.json
    head, *data, footer = run_rmlint(
        '-S a', TESTDIR_NAME + '/a',
        with_json='replay-a.json', use_default_dir=False,
    )

    assert len(data) == 2
    assert data[0]['path'].endswith('a/1')
    assert data[1]['path'].endswith('a/2')

    # Create replay-b.json
    head, *data, footer = run_rmlint(
        TESTDIR_NAME + '/b',
        with_json='replay-b.json', use_default_dir=False,
    )

    assert len(data) == 2
    assert data[0]['path'].endswith('b/1')
    assert data[1]['path'].endswith('b/2')

    # Check if b.json is preferred over a.json
    head, *data, footer = run_rmlint(
        '-S a --replay {a} // {b}'.format(a=replay_path_a, b=replay_path_b)
    )
    assert len(data) == 4
    assert [p['is_original'] for p in data] == [True, False, False, False]

    assert data[0]['path'].endswith('b/1')
    assert data[1]['path'].endswith('b/2')
    assert data[2]['path'].endswith('a/1')
    assert data[3]['path'].endswith('a/2')

    # Check if a.json is preferred over b.json
    head, *data, footer = run_rmlint(
        '-S a --replay {b} // {a}'.format(a=replay_path_a, b=replay_path_b)
    )

    assert len(data) == 4
    assert [p['is_original'] for p in data] == [True, False, False, False]

    assert data[0]['path'].endswith('a/1')
    assert data[1]['path'].endswith('a/2')
    assert data[2]['path'].endswith('b/1')
    assert data[3]['path'].endswith('b/2')

    head, *data, footer = run_rmlint(
        '-S a --replay {a} // {b} -k'.format(a=replay_path_a, b=replay_path_b)
    )

    assert len(data) == 4
    assert [p['is_original'] for p in data] == [True, True, False, False]

    assert data[0]['path'].endswith('b/1')
    assert data[1]['path'].endswith('b/2')
    assert data[2]['path'].endswith('a/1')
    assert data[3]['path'].endswith('a/2')


@with_setup(usual_setup_func, usual_teardown_func)
def test_replay_duplicate_directory_size():
    create_file('xxx', 'a/xxx')
    create_file('xxx', 'b/xxx')

    replay_path = os.path.join(TESTDIR_NAME, 'replay.json')
    head, *data, footer = run_rmlint('-S a', with_json='replay.json')
    assert len(data) == 2

    head, *data, footer = run_rmlint('--replay {p}'.format(p=replay_path))
    assert len(data) == 2
    assert footer['total_lint_size'] == 3


def create_pack_and_unpack_scenario():
    x_files = [
        "a/sub1/x1",
        "a/sub1/x2",
        "a/sub2/x3",
        "a/sub2_copy/x3",
        "b/sub1/x1",
        "b/sub1/x2",
        "b/sub2/x3",
        "b/sub2_copy/x3",
    ]

    for path in x_files:
        create_file("x", path)

    create_file("special", "special")
    create_file("special", "a/special")
    create_file("special", "b/special")

    create_file("", "a/empty")
    create_file("", "b/empty")


def data_by_type(data):
    result = {}
    for entry in data:
        path = entry["path"][len(TESTDIR_NAME):]
        # TODO: bring back is_original.
        # result.setdefault(entry["type"], {})[path] = entry["is_original"]
        result.setdefault(entry["type"], {})[path] = False

    return result


EXPECTED_WITH_TREEMERGE = {
    'emptyfile': {
        '/a/empty': False,
        '/b/empty': False,
    },
    "part_of_directory": {
        '/b/sub1/x1'      : False,
        '/a/sub2/x3'      : False,
        '/b/sub2/x3'      : False,
        '/a/sub2_copy/x3' : False,
        '/b/sub2_copy/x3' : False,
        '/a/sub1/x2'      : False,
        '/a/sub1/x1'      : False,
        '/b/sub1/x2'      : False,
        '/a/special'      : False,
        '/b/special'      : False,
    },
    'duplicate_dir': {
        '/a'           : False,
        '/b'           : False,
        '/a/sub2'      : False,
        '/a/sub2_copy' : False,
    },
    'duplicate_file': {
        '/a/sub1/x1' : False,
        '/a/sub1/x2' : False,
        '/a/sub2/x3' : False,
        '/a/special' : False,
        '/special'   : False,
    },
}

EXPECTED_WITHOUT_TREEMERGE = {
    'emptyfile': {
        '/a/empty': False,
        '/b/empty': False,
    },
    'duplicate_file': {
        '/b/sub1/x1'      : False, #True,
        '/a/sub1/x1'      : False,
        '/a/sub1/x2'      : False,
        '/a/sub2/x3'      : False,
        '/a/sub2_copy/x3' : False,
        '/b/sub1/x2'      : False,
        '/b/sub2/x3'      : False,
        '/b/sub2_copy/x3' : False,
        '/a/special'      : False, #True
        '/b/special'      : False,
        '/special'        : False,
    }
}


@with_setup(usual_setup_func, usual_teardown_func)
def test_replay_pack_directories():
    create_pack_and_unpack_scenario()

    # Do a run without -D and pack it later during --replay.
    replay_path = os.path.join(TESTDIR_NAME, 'replay.json')

    head, *data, footer = run_rmlint('-S ahD', with_json='replay.json')
    assert len(data) == 13
    assert data_by_type(data) == EXPECTED_WITHOUT_TREEMERGE

    # Do the run without any packing first (it should yield the same result)
    head, *data, footer = run_rmlint('--replay {p} -S ahD'.format(p=replay_path))
    assert len(data) == 13
    assert data_by_type(data) == EXPECTED_WITHOUT_TREEMERGE

    # Do the run with packing dupes into directories now:
    head, *data, footer = run_rmlint('--replay {p} -S ahD -D'.format(p=replay_path))
    assert len(data) == 21
    assert data_by_type(data) == EXPECTED_WITH_TREEMERGE


@with_setup(usual_setup_func, usual_teardown_func)
def test_replay_unpack_directories():
    create_pack_and_unpack_scenario()

    # Do a run with -D and pack it later during --replay.
    replay_path = os.path.join(TESTDIR_NAME, 'replay.json')
    head, *data, footer = run_rmlint('-S ahD -D', with_json='replay.json')

    assert len(data) == 21
    assert data_by_type(data) == EXPECTED_WITH_TREEMERGE

    # Do a normal --replay run first without any unpacking:
    head, *data, footer = run_rmlint('--replay {p} -S ahD -D'.format(p=replay_path))
    assert len(data) == 21
    assert data_by_type(data) == EXPECTED_WITH_TREEMERGE

    # # Do the run with unpacking the directories:
    head, *data, footer = run_rmlint('--replay {p} -S ahD'.format(p=replay_path))
    assert len(data) == 23

    # NOTE: In this special case we should still carry around the
    # part_of_directory elements from previous runs.
    expected = EXPECTED_WITHOUT_TREEMERGE
    expected["part_of_directory"] = EXPECTED_WITH_TREEMERGE["part_of_directory"]

    assert data_by_type(data) == expected


def _check_json_matches(data, replay_data):
    data_map = {row['id']: row for row in data}
    replay_data_map = {row['id']: row for row in replay_data}

    for ident in set(data_map) | set(replay_data_map):
        if ident not in data_map:
            raise ValueError('Extra entry in replay output:\n{}'.format(pformat(replay_data_map[ident])))
        if ident not in replay_data_map:
            raise ValueError('Missing entry in replay output:\n{}'.format(pformat(data_map[ident])))
    assert len(data) == len(replay_data)

    # ignore the 'progress' key which does not match
    def no_progress(row):
        row = row.copy()
        del row['progress']
        return row

    for ident in set(data_map) | set(replay_data_map):
        orig = no_progress(data_map[ident])
        replay = no_progress(replay_data_map[ident])
        if replay.get('checksum') == '':
            del replay['checksum']  # fix empty checksum
        if orig != replay:
            raise ValueError('Entry in replay output does not match.\n\nOriginal entry:\n{}\n\nReplay entry:\n{}'.format(
                pformat(orig), pformat(replay),
            ))


def _get_replay_data(expect_len, opts, replay_opts=None):
    if replay_opts is None:
        replay_opts = opts

    replay_path = os.path.join(TESTDIR_NAME, 'replay.json')
    head, *data, foot = run_rmlint('-S a', opts, with_json='replay.json')
    assert len(data) == expect_len

    head, *replay_data, foot = run_rmlint('-S a --replay', replay_path, replay_opts)
    return data, replay_data


@parameterized.expand([('',), ('--no-followlinks',)])
@with_setup(usual_setup_func, usual_teardown_func)
def test_symlinks(link_opt):
    if not has_feature('replay'):
        raise SkipTest('rmlint built without replay support')

    create_file('xxx', 'a')
    create_file('xxx', 'b')
    create_link('a', 'l1', symlink=True)
    create_link('a', 'l2', symlink=True)

    data, replay_data = _get_replay_data(4, '', link_opt)
    if link_opt:
        # symlinks filtered out
        assert len(replay_data) == 2
        assert replay_data[0]['path'].endswith('/a')
        assert replay_data[1]['path'].endswith('/b')
    else:
        # replay should use lstat, so the inode, size, and mtime match
        _check_json_matches(data, replay_data)


@with_setup(usual_setup_func, usual_teardown_func)
def test_hardlinks():
    if not has_feature('replay'):
        raise SkipTest('rmlint built without replay support')

    create_file('xxx', 'a')
    create_link('a', 'h1')
    create_link('a', 'h2')

    data, replay_data = _get_replay_data(3, '')
    # hardlinks should be preserved
    _check_json_matches(data, replay_data)


@with_setup(usual_setup_func, usual_teardown_func)
def test_empty_file():
    if not has_feature('replay'):
        raise SkipTest('rmlint built without replay support')

    create_file('', 'empty')

    # replay should not ignore the lone file
    data, replay_data = _get_replay_data(1, '-T "none +ef"')
    assert len(data) == len(replay_data)


@with_setup(usual_setup_func, usual_teardown_func)
def test_hardlink_sort():
    create_file('xxx', 'd1/a')
    create_file('xxx', 'd1/b')
    create_link('d1/a', 'd1/a2')
    create_link('d1/a', 'd1/a3')
    create_dirs('d2')
    create_link('d1/b', 'd2/b')

    replay_path = os.path.join(TESTDIR_NAME, 'replay.json')
    head, *data, footer = run_rmlint(with_json='replay.json')

    def run(sort):
        return run_rmlint(
            '-S {s} {t}/d1 --replay {r}'.format(s=sort, t=TESTDIR_NAME, r=replay_path),
            use_default_dir=False,
        )

    head, *data, foot = run('HA')
    assert data[0]['path'].endswith('/d1/a3')

    head, *data, foot = run('Oa')
    assert data[0]['path'].endswith('/d1/b')
