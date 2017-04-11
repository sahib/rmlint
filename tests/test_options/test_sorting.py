#!/usr/bin/env python3
# encoding: utf-8
from nose import with_setup
from tests.utils import *

from itertools import permutations, combinations


PATHS = ['b_dir/', 'a_dir/', 'c_dir/']


def path_index(file_path):
    for idx, path in enumerate(PATHS):
        if path in file_path:
            return idx
    assert False


def path_depth(file_path):
    return len([c for c in file_path if c == '/'])

def in_order(a, b, increasing):
    return (a < b) if increasing else (b < a)

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

            # Equal? Go to next criterion.
            if cmp_a == cmp_b:
                continue

            # special handling for mtime rounding
            if (test.lower()=='m' and abs(cmp_a-cmp_b) < 0.00000001):
                continue

            if in_order(cmp_a, cmp_b, test.islower()):
                # order is correct; ignore any remaining (less important) criteria
                break

            # Order is incorrect.
            assert False


@with_setup(usual_setup_func, usual_teardown_func)
def test_sorting():
    # create some dupes with different PATHS, names and mtimes:
    create_file('xxx', PATHS[1] + 'a', mtime='2004-02-29  16:21:42.4')
    create_file('xxx', PATHS[0] + 'c', mtime='2004-02-29  16:21:42.6')
    create_file('xxx', PATHS[2] + 'B', mtime='2004-02-29  16:21:43.1')
    create_file('xxx', PATHS[2] + 'b', mtime='2004-02-29  16:21:43.0')
    create_file('xxx', PATHS[1] + 'c', mtime='2004-02-29  16:21:41.5')
    create_file('xxx', PATHS[2] + 'c', mtime='2004-02-29  16:21:41.0')

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

@with_setup(usual_setup_func, usual_teardown_func)
def test_sort_by_outlyer():
    create_file('xxx', 'a/foo')
    create_file('xxx', 'b/foo')

    create_link('a/foo', 'b/foo-from-a')
    create_link('b/foo', 'b/foo-copy-1')
    create_link('b/foo', 'b/foo-copy-2')

    head, *data, footer = run_rmlint(
        "-S O {t}/b".format(t=TESTDIR_NAME), use_default_dir=False
    )
    assert data[0]['path'].endswith('b/foo-from-a')

    head, *data, footer = run_rmlint('-S OHa')
    assert data[0]['path'].endswith('b/foo')


# See this issue for more information.
# https://github.com/sahib/rmlint/issues/196
#
# Testsetup by "Awerick"
@with_setup(usual_setup_func, usual_teardown_func)
def test_sort_by_outlyer_hardcore():
    for suffix in 'ABCD':
        create_file('xxx', 'inside/foo' + suffix)

    create_dirs('outside')

    # fooA:
    for i in range(2, 6):
        create_link('inside/fooA', 'inside/fooA_link_' + str(i))

    # fooB:
    for i in range(2, 5):
        create_link('inside/fooB', 'inside/fooB_link_' + str(i))
    create_link('inside/fooB', 'outside/fooB_link_5')

    # fooC:
    create_link('inside/fooC', 'inside/fooC_link_2')
    create_link('inside/fooC', 'outside/fooC_link_3')
    create_link('inside/fooC', 'outside/fooC_link_4')

    # fooD:
    create_link('inside/fooD', 'outside/fooD_link_2')
    create_link('inside/fooD', 'outside/fooD_link_3')

    def run_inside_job(crit):
        head, *data, footer = run_rmlint(
            "-S {c} {t}/inside".format(
                c=crit,
                t=TESTDIR_NAME,
            ), use_default_dir=False
        )

        return data[0]['path']

    assert run_inside_job('Ha').endswith('inside/fooA')
    assert run_inside_job('HOa').endswith('inside/fooB')
    assert run_inside_job('OHa').endswith('inside/fooC')
    assert run_inside_job('OA').endswith('inside/fooD')



@with_setup(usual_setup_func, usual_teardown_func)
def test_sort_by_regex():
    create_file('xxx', 'aaaa')
    create_file('xxx', 'aaab')
    create_file('xxx', 'b')
    create_file('xxx', 'c')
    create_file('xxx', '1/c')
    create_file('xxx', 'd')

    head, *data, footer = run_rmlint("-S 'r<1/c>x<d$>a'")

    paths = [p['path'] for p in data]

    assert paths[0].endswith('1/c')
    assert paths[1].endswith('d')
    assert paths[2].endswith('aaaa')
    assert paths[3].endswith('aaab')
    assert paths[4].endswith('b')
    assert paths[5].endswith('c')


@with_setup(usual_setup_func, usual_teardown_func)
def test_sort_by_regex_bad_input():
    create_file('xxx', 'aaaa')
    create_file('xxx', 'aaab')

    # Should work:
    run_rmlint("-S '{}'".format('r<.>' * 8))

    # More than 8 is bad:
    try:
        run_rmlint("-S '{}'".format('r<.>' * 9))
        assert False
    except subprocess.CalledProcessError:
        pass

    # Empty patterns are sill also:
    try:
        run_rmlint("-S 'r<>'")
        assert False
    except subprocess.CalledProcessError:
        pass

    # A bad regex is bad too:
    try:
        run_rmlint("-S 'r<*>'")
        assert False
    except subprocess.CalledProcessError:
        pass
