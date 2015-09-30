from nose import with_setup
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


@with_setup(usual_setup_func, usual_teardown_func)
def test_replay_match_basename():
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

    assert len(data) == 3


@with_setup(usual_setup_func, usual_teardown_func)
def test_replay_hidden():
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


@with_setup(usual_setup_func, usual_teardown_func)
def test_replay_must_match_tagged():
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
