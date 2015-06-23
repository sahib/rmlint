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
        'a': lambda p: os.path.basename(p['path']).lower(),
        'm': lambda p: p['mtime'],
        'p': lambda p: path_index(p['path']),
        'd': lambda p: path_depth(p['path']),
        'l': lambda p: len(os.path.basename(p['path']))
    }

    for a, b in combinations(data, 2):
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
def test_sorting():
    # create some dupes with different PATHS, names and mtimes:
    create_file('xxx', PATHS[1] + 'a')
    create_file('xxx', PATHS[0] + 'c')
    create_file('xxx', PATHS[2] + 'B')

    # Make sure it takes some time to re-reun
    time.sleep(1.2)
    create_file('xxx', PATHS[2] + 'b')
    create_file('xxx', PATHS[1] + 'c')
    create_file('xxx', PATHS[2] + 'c')

    joiner = ' ' + TESTDIR_NAME + '/'
    search_paths = joiner + joiner.join(PATHS)

    opts = 'ampdl'
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

    for combo in combos:
        combo_str = '-S ' + combo
        head, *data, footer = run_rmlint(combo_str + search_paths, use_default_dir=False)
        assert len(data) == 6

        validate_order(data, combo)
