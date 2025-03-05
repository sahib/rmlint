#!/usr/bin/env python3
# encoding: utf-8
import pytest
from tests.utils import *

import time

from itertools import permutations, combinations


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


def test_replay_match_basename(usual_setup_usual_teardown):
    create_file('xxx', 'test1/a')
    create_file('xxx', 'test1/b')
    create_file('xxx', 'test2/a')

    replay_path = '/tmp/replay.json'

    head, *data, footer = run_rmlint('-o json:{p}'.format(
        p=replay_path
    ))

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

    # second 'a' file should be kicked out
    assert len(data) == 2
    paths = set([p['path'] for p in data])
    assert os.path.join(TESTDIR_NAME, 'test1/b') in paths


def test_replay_hidden(usual_setup_usual_teardown):
    create_file('xxx', 'test/.a')
    create_file('xxx', 'test/.b')

    replay_path = '/tmp/replay.json'

    head, *data, footer = run_rmlint('--hidden -o json:{p}'.format(
        p=replay_path
    ))

    assert len(data) == 2

    head, *data, footer = run_rmlint('--replay {p}'.format(
        p=replay_path
    ))

    assert len(data) == 0

    head, *data, footer = run_rmlint('--replay {p} --hidden'.format(
        p=replay_path
    ))

    assert len(data) == 2


def test_replay_must_match_tagged(usual_setup_usual_teardown):
    create_file('xxx', 'test_a/a')
    create_file('xxx', 'test_b/a')

    replay_path = '/tmp/replay.json'

    head, *data, footer = run_rmlint('-o json:{p}'.format(
        p=replay_path
    ))

    assert len(data) == 2

    head, *data, footer = run_rmlint('--replay {p} {b} // {a} -m'.format(
        p=replay_path,
        a=os.path.join(TESTDIR_NAME, 'test_a'),
        b=os.path.join(TESTDIR_NAME, 'test_b')
    ))

    paths = set([(p['path'], p['is_original']) for p in data])
    assert (os.path.join(TESTDIR_NAME, 'test_b/a'), False) in paths
    assert (os.path.join(TESTDIR_NAME, 'test_a/a'), True) in paths


def test_replay_keep_cached_originals(usual_setup_usual_teardown):
    create_file('xxx', 'test_a/a')
    create_file('xxx', 'test_b/a')
    create_file('xxx', 'test_a/b')
    create_file('xxx', 'test_b/c')

    replay_path = '/tmp/replay.json'

    head, *data, footer = run_rmlint('-o json:{p} -S fa'.format(
        p=replay_path
    ))
    assert len(data) == 4
    assert data[0]['path'] == os.path.join(TESTDIR_NAME, 'test_a/a')

    # reverse the sort order but respect tagged originals from first run
    head, *data, footer = run_rmlint('--replay {p} -S FA --keep-cached-originals'.format(
        p=replay_path
    ))
    assert len(data) == 4
    assert data[0]['path'] == os.path.join(TESTDIR_NAME, 'test_a/a')
    assert data[1]['path'] == os.path.join(TESTDIR_NAME, 'test_b/c')

    # reverse the sort order, without --keep-cached-originals
    head, *data, footer = run_rmlint('--replay {p} -S FA'.format(
        p=replay_path
    ))
    assert len(data) == 4
    assert data[0]['path'] == os.path.join(TESTDIR_NAME, 'test_b/c')

    # cached originals vs -k: cached originals should be first original but -k should still be respected
    head, *data, footer = run_rmlint('--replay {p} {a} // {b} -k -S fa --keep-cached-originals'.format(
        p=replay_path,
        a=os.path.join(TESTDIR_NAME, 'test_a'),
        b=os.path.join(TESTDIR_NAME, 'test_b')
    ))
    assert len(data) == 4
    assert data[0]['path'] == os.path.join(TESTDIR_NAME, 'test_a/a')
    assert data[1]['path'] == os.path.join(TESTDIR_NAME, 'test_b/a')
    assert (all(data[i]['is_original'] for i in range(3)))
    assert (not data[3]['is_original'])


@pytest.mark.slow
def test_sorting(usual_setup_usual_teardown):
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
    def is_legal_combo(x): return len(x) == len(set(x.lower()))

    # Limit to 3-tuple combinations. 4-5-tuple combinations (just do +1)
    # are possible if you have enough time (=> ~(10! / 2) tests).
    for n_terms in range(1, len(opts) - 1):
        combos += filter(
            is_legal_combo,
            (''.join(p) for p in permutations(all_opts, n_terms))
        )

    replay_path = '/tmp/replay.json'

    for combo in combos:
        combo_str = '-y ' + combo
        head, *data, footer = run_rmlint(
            combo_str + search_paths, '-o json:{p}'.format(p=replay_path),
            use_default_dir=False
        )
        assert len(data) == 7

        validate_order(data, combo)

        head, *data, footer = run_rmlint(
            combo_str + search_paths, ' --replay ' + replay_path,
            use_default_dir=False
        )

        assert len(data) == 7

        validate_order(data, combo)


def test_replay_no_dir(usual_setup_usual_teardown):
    # Regression test for #305.
    # --replay did not replay anything when not specifying some path.
    # (The current working directory was not set in this case correctly)

    create_file('xxx', 'sub/a')
    create_file('xxx', 'sub/b')
    create_file('xxx', 'c')

    current_cwd = os.getcwd()

    try:
        os.chdir(TESTDIR_NAME)
        replay_path = '/tmp/replay.json'
        head, *data, footer = run_rmlint(
            '-o json:{p}'.format(p=replay_path),
            use_default_dir=False,
        )
        assert len(data) == 3

        head, *data, footer = run_rmlint(
            '--replay {}'.format(replay_path),
            use_default_dir=False,
        )
        assert len(data) == 3
    finally:
        os.chdir(current_cwd)


def test_replay_unicode_fuckup(usual_setup_usual_teardown):
    names = '上野洋子, 吉野裕司, 浅井裕子 & 河越重義', '天谷大輔', 'Аркона'

    create_file('xxx', names[0])
    create_file('xxx', names[1])
    create_file('xxx', names[2])

    replay_path = '/tmp/replay.json'

    head, *data, footer = run_rmlint('-o json:{p}'.format(p=replay_path))
    assert len(data) == 3
    assert set([os.path.basename(e['path']) for e in data]) == set(names)

    head, *data, footer = run_rmlint('--replay {p}'.format(p=replay_path))
    assert len(data) == 3
    assert set([os.path.basename(e['path']) for e in data]) == set(names)


def test_replay_tagged_order(usual_setup_usual_teardown):
    create_file('xxx', 'a/1')
    create_file('xxx', 'a/2')
    create_file('xxx', 'b/1')
    create_file('xxx', 'b/2')

    replay_path_a = '/tmp/replay-a.json'
    replay_path_b = '/tmp/replay-b.json'

    # Create replay-a.json
    head, *data, footer = run_rmlint(
        '-S a {r} -o json:{p}'.format(r=TESTDIR_NAME + '/a', p=replay_path_a),
        use_default_dir=False
    )

    assert len(data) == 2
    assert data[0]['path'].endswith('a/1')
    assert data[1]['path'].endswith('a/2')

    # Create replay-b.json
    head, *data, footer = run_rmlint(
        '{r} -o json:{p}'.format(r=TESTDIR_NAME + '/b', p=replay_path_b),
        use_default_dir=False
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


def test_replay_duplicate_directory_size(usual_setup_usual_teardown):
    create_file('xxx', 'a/xxx')
    create_file('xxx', 'b/xxx')

    replay_path = '/tmp/replay.json'
    head, *data, footer = run_rmlint('-o json:{p} -S a'.format(p=replay_path))
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

EXPECTED_WITH_TRAVERSED = {
    'emptyfile': {
        '/a/empty': False,
        '/b/empty': False,
    },
    'duplicate_dir_candidate': {
        '/a/sub1': False,
        '/a/sub2': False,
        '/a/sub2_copy': False,
        '/b/sub1': False,
        '/b/sub2_copy': False,
        '/b/sub2': False,
        '/a': False,
        '': False,
        '/b': False},
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

EXPECTED_LEN_WITH_TREEMERGE = sum(
    [len(EXPECTED_WITH_TREEMERGE[key]) for key in EXPECTED_WITH_TREEMERGE])
EXPECTED_LEN_WITHOUT_TREEMERGE = sum(
    [len(EXPECTED_WITHOUT_TREEMERGE[key]) for key in EXPECTED_WITHOUT_TREEMERGE])
EXPECTED_LEN_WITH_TRAVERSED = sum(
    [len(EXPECTED_WITH_TRAVERSED[key]) for key in EXPECTED_WITH_TRAVERSED])


def test_replay_pack_directories(usual_setup_usual_teardown):
    create_pack_and_unpack_scenario()

    # Do a run without -D and pack it later during --replay.
    replay_path = '/tmp/replay.json'

    head, *data, footer = run_rmlint('-o json:{p} -S ahD -c json:traversed'.format(p=replay_path))
    assert len(data) == EXPECTED_LEN_WITH_TRAVERSED
    assert data_by_type(data) == EXPECTED_WITH_TRAVERSED

    # Do the run without any packing first (it should yield the same result)
    head, *data, footer = run_rmlint('--replay {p} -S ahD'.format(p=replay_path))
    assert len(data) == EXPECTED_LEN_WITHOUT_TREEMERGE
    assert data_by_type(data) == EXPECTED_WITHOUT_TREEMERGE

    # Do the run with packing dupes into directories now:
    head, *data, footer = run_rmlint('--replay {p} -S ahD -D'.format(p=replay_path))
    assert len(data) == EXPECTED_LEN_WITH_TREEMERGE
    assert data_by_type(data) == EXPECTED_WITH_TREEMERGE


def test_replay_unpack_directories(usual_setup_usual_teardown):
    create_pack_and_unpack_scenario()

    # Do a run with -D and pack it later during --replay.
    replay_path = '/tmp/replay.json'
    head, *data, footer = run_rmlint('-o json:{p} -S ahD -D'.format(p=replay_path))

    assert len(data) == EXPECTED_LEN_WITH_TREEMERGE
    assert data_by_type(data) == EXPECTED_WITH_TREEMERGE

    # Do a normal --replay run first without any unpacking:
    head, *data, footer = run_rmlint('--replay {p} -S ahD -D'.format(p=replay_path))
    assert len(data) == EXPECTED_LEN_WITH_TREEMERGE
    assert data_by_type(data) == EXPECTED_WITH_TREEMERGE

    # # Do the run with unpacking the directories:
    head, *data, footer = run_rmlint('--replay {p} -S ahD'.format(p=replay_path))
    assert len(data) == EXPECTED_LEN_WITHOUT_TREEMERGE
    assert data_by_type(data) == EXPECTED_WITHOUT_TREEMERGE
