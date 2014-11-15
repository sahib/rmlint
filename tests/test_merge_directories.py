from nose import with_setup
from utils import *


@with_setup(usual_setup_func, usual_teardown_func)
def test_simple():
    create_file('xxx', '1/a')
    create_file('xxx', '2/a')
    create_file('xxx', 'a')
    head, *data, footer = run_rmlint('-D --sortcriteria A')

    assert 2 == sum(find['type'] == 'duplicate_dir' for find in data)
    assert 1 == sum(find['type'] == 'duplicate_file' for find in data)
    assert data[0]['size'] == 3

    # -S A should sort in reverse lexigraphic order.
    assert data[0]['is_original']
    assert not data[1]['is_original']
    assert data[0]['path'].endswith('2')
    assert data[1]['path'].endswith('1')


@with_setup(usual_setup_func, usual_teardown_func)
def test_diff():
    create_file('xxx', '1/a')
    create_file('xxx', '2/a')
    create_file('xxx', '3/a')
    create_file('yyy', '3/b')
    head, *data, footer = run_rmlint('-D --sortcriteria A')

    assert 2 == sum(find['type'] == 'duplicate_dir' for find in data)
    assert data[0]['size'] == 3

    # -S A should sort in reverse lexigraphic order.
    assert data[0]['is_original']
    assert not data[1]['is_original']
    assert data[0]['path'].endswith('2')
    assert data[1]['path'].endswith('1')


@with_setup(usual_setup_func, usual_teardown_func)
def test_same_but_not_dupe():
    create_file('xxx', '1/a')
    create_file('xxx', '2/a')
    create_file('xxx', '2/b')
    head, *data, footer = run_rmlint('-D --sortcriteria A')

    # No duplicate dirs, but 3 duplicate files should be found.
    assert 0 == sum(find['type'] == 'duplicate_dir' for find in data)
    assert 3 == sum(find['type'] == 'duplicate_file' for find in data)

@with_setup(usual_setup_func, usual_teardown_func)
def test_hardlinks():
    create_file('xxx', '1/a')
    create_link('1/a', '1/link1')
    create_link('1/a', '1/link2')
    create_file('xxx', '2/a')
    create_link('2/a', '2/link1')
    create_link('2/a', '2/link2')
    head, *data, footer = run_rmlint('-D -l -S a')

    assert data[0]['type'] == 'duplicate_dir'
    assert data[0]['path'].endswith('1')
    assert data[1]['type'] == 'duplicate_dir'
    assert data[1]['path'].endswith('2')

    head, *data, footer = run_rmlint('-D -S A')
    assert data[0]['type'] == 'duplicate_file'
    assert data[0]['path'].endswith('a')
    assert data[1]['type'] == 'duplicate_file'
    assert data[1]['path'].endswith('a')


@with_setup(usual_setup_func, usual_teardown_func)
def test_deep_simple():
    create_file('xxx', 'deep/a/b/c/d/1')
    create_file('xxx', 'deep/e/f/g/h/1')
    head, *data, footer = run_rmlint('-D -S a')

    assert data[0]['path'].endswith('deep/a')
    assert data[1]['path'].endswith('deep/e')
    assert len(data) == 2


def create_nested(root, letters):
    summed = []
    for letter in letters:
        summed.append(letter)
        path = os.path.join(*([root] + summed + ['1']))
        create_file('xxx', path)


@with_setup(usual_setup_func, usual_teardown_func)
def test_deep_full():
    create_nested('deep', 'abcd')
    create_nested('deep', 'efgh')

    head, *data, footer = run_rmlint('-D -S a')

    assert data[0]['path'].endswith('deep/a')
    assert data[1]['path'].endswith('deep/e')
    assert len(data) == 2


@with_setup(usual_setup_func, usual_teardown_func)
def test_deep_full_twice():
    create_nested('deep_a', 'abcd')
    create_nested('deep_a', 'efgh')
    create_nested('deep_b', 'abcd')
    create_nested('deep_b', 'efgh')

    head, *data, footer = run_rmlint(
        '-D -S a {t}/deep_a {t}/deep_b'.format(
            t=TESTDIR_NAME
        ),
        use_default_dir=False
    )

    assert data[0]['path'].endswith('deep_a')
    assert data[1]['path'].endswith('deep_b')
    assert len(data) == 2


'''
Test idea for mountpoints:

$ mkdir mounty/a/b -p
$ cd mounty/a
$ echo 'x' > 1
$ sudo mount --rbind .. b
$ cd .. && $ mkdir c && $ echo 'x' > c/2

mounty
├── a
│   ├── 1
│   └── b
│       ├── a
│       │   ├── 1
│       │   └── b
│       └── c
│           └── 1
└── c
    └── 2

Expected result:

Warning: filesystem loop detected at /home/sahib/rmlint/mounty/a/b (skipping)

# Duplikate:
    ls /home/sahib/rmlint/mounty/a/1
    rm /home/sahib/rmlint/mounty/c/2

Problem: mount needs sudo.
'''


@with_setup(usual_setup_func, usual_teardown_func)
def test_mount_binds():
    pass
