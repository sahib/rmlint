from nose import with_setup
from tests.utils import *

import time
import itertools
import string

paths = [ 'b_dir/', 'a_dir/', 'c_dir/' ]


# comparison tests
def cmp_scalar(a, b):
    if a < b:
        return -1
    elif a > b:
        return 1
    else:
        return 0
        
def path_index(a):
    for i, path in enumerate(paths):
        if a.find(path) > 0:
            ##print('Path index {}'.format(i))
            return i
    assert(False)
    
def cmp_pathorder(a, b):
    return cmp_scalar(path_index(a['path']), path_index(b['path']))

def cmp_basename(a, b):
    return cmp_scalar(os.path.basename(a['path']), os.path.basename(b['path']))
    
def cmp_mtime(a, b):
    return cmp_scalar(a['mtime'], b['mtime'])


# dispatcher for comparison tests
def cmp_pair(a, b, tests):
    testfuncs = {'a':cmp_basename, 'm': cmp_mtime, 'p':cmp_pathorder}
    for test in tests:
        if test[0] == test[0].lower():
            sign = -1
        else:
            sign = 1
        testfunc = testfuncs[test[0].lower()]
        cmp_i = testfunc(a, b)
        if cmp_i == sign:
            return True
        elif cmp_i == -sign:
            return False
    return True

# iterator for pairwise comparison of each dupe file's ranking with the original
def cmp_all(data, *tests):
    a=data[0]
    for b in data[1:]:
        assert cmp_pair(a, b, tests)



@with_setup(usual_setup_func, usual_teardown_func)
def test_sorting():
    # create some dupes with different paths, names and mtimes:
    create_file('xxx', paths[1] + 'a')
    create_file('xxx', paths[0] + 'c')
    create_file('xxx', paths[2] + 'B')
    time.sleep(1.5)
    create_file('xxx', paths[2] + 'b')
    create_file('xxx', paths[1] + 'c')
    create_file('xxx', paths[2] + 'c')
    
    joiner=' ' + TESTDIR_NAME + '/'
    search_paths = joiner + joiner.join(paths)

    opts='amp'
    all_opts = opts + opts.upper()
    combos=[]
    f = lambda x: len(x) == len(set(''.join(x).lower()))
    for i in range(1, len(opts) + 1):
        combos += filter(f, itertools.permutations(all_opts, i))

    
    for combo in combos:
        combo_str = '-S ' + ''.join(combo)
        print('Testing {}...'.format(combo_str), end='')
        head, *data, footer = run_rmlint(combo_str + search_paths, use_default_dir=False)
        assert len(data) == 6
        cmp_all(data, combo)
        print('ok')
   

