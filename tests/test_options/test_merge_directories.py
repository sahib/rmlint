#!/usr/bin/env python3
# encoding: utf-8
from nose import with_setup
from tests.utils import *


def filter_part_of_directory(data):
    return [entry for entry in data if entry["type"] != "part_of_directory"]

@with_setup(usual_setup_func, usual_teardown_func)
def test_simple():
    create_file('xxx', '1/a')
    create_file('xxx', '2/a')
    create_file('xxx', 'a')

    head, *data, footer = run_rmlint('-p -D --rank-by A')
    data = filter_part_of_directory(data)

    assert 2 == sum(find['type'] == 'duplicate_dir' for find in data)

    # One original, one dupe
    assert 1 == sum(find['type'] == 'duplicate_file' for find in data if find['is_original'])
    assert 1 == sum(find['type'] == 'duplicate_file' for find in data if not find['is_original'])
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
    head, *data, footer = run_rmlint('-p -D --rank-by A')
    data = filter_part_of_directory(data)

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
    head, *data, footer = run_rmlint('-p -D --rank-by A')
    data = filter_part_of_directory(data)

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

    head, *data, footer = run_rmlint('-p -D -l -S a')
    data = filter_part_of_directory(data)
    assert len(data) == 5
    assert data[0]['type'] == 'duplicate_dir'
    assert data[0]['path'].endswith('1')
    assert data[1]['type'] == 'duplicate_dir'
    assert data[1]['path'].endswith('2')

    # Hardlink duplicates:
    assert data[2]['type'] == 'duplicate_file'
    assert data[2]['path'].endswith('1/a')
    assert data[2]['is_original']
    assert data[3]['type'] == 'duplicate_file'
    assert data[3]['path'].endswith('1/link1')
    assert not data[3]['is_original']
    assert data[4]['type'] == 'duplicate_file'
    assert data[4]['path'].endswith('1/link2')
    assert not data[4]['is_original']

    head, *data, footer = run_rmlint('-D -S a -L')
    data = filter_part_of_directory(data)
    assert len(data) == 2
    assert data[0]['type'] == 'duplicate_file'
    assert data[0]['path'].endswith('a')
    assert data[1]['type'] == 'duplicate_file'
    assert data[1]['path'].endswith('a')


@with_setup(usual_setup_func, usual_teardown_func)
def test_deep_simple():
    create_file('xxx', 'deep/a/b/c/d/1')
    create_file('xxx', 'deep/e/f/g/h/1')
    head, *data, footer = run_rmlint('-D -S a')
    data = filter_part_of_directory(data)

    assert data[0]['path'].endswith('deep/a')
    assert data[1]['path'].endswith('deep/e')
    assert int(data[0]['checksum'], 16) > 0
    assert int(data[1]['checksum'], 16) > 0
    assert len(data) == 2


@with_setup(usual_setup_func, usual_teardown_func)
def test_deep_simple():
    create_file('xxx', 'd/a/1')
    create_file('xxx', 'd/b/empty')
    create_file('xxx', 'd/a/1')
    create_file('xxx', 'd/b/empty')
    head, *data, footer = run_rmlint('-p -D -S a')
    data = filter_part_of_directory(data)

    assert data[0]['path'].endswith('d/a')
    assert data[1]['path'].endswith('d/b')
    assert len(data) == 2


@with_setup(usual_setup_func, usual_teardown_func)
def test_dirs_with_empty_files_only():
    create_file('', 'a/empty')
    create_file('', 'b/empty')
    head, *data, footer = run_rmlint('-p -D -S a -T df,dd --size 0')
    data = filter_part_of_directory(data)

    assert len(data) == 2
    assert data[0]['path'].endswith('a')
    assert data[0]['type'] == "duplicate_dir"
    assert data[1]['path'].endswith('b')
    assert data[1]['type'] == "duplicate_dir"

    head, *data, footer = run_rmlint('-p -D -S a -T df,dd')
    data = filter_part_of_directory(data)
    assert len(data) == 0

    head, *data, footer = run_rmlint('-p -D -S a --size 0')
    data = filter_part_of_directory(data)
    assert len(data) == 2

    data.sort(key=lambda elem: elem["path"])
    assert data[0]['path'].endswith('a/empty')
    assert data[0]['type'] == "emptyfile"
    assert data[1]['path'].endswith('b/empty')
    assert data[1]['type'] == "emptyfile"


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

    # subprocess.call('tree ' + TESTDIR_NAME, shell=True)
    # subprocess.call('./rmlint -p -S a -D ' + TESTDIR_NAME, shell=True)
    head, *data, footer = run_rmlint('-p -D -S a')
    data = filter_part_of_directory(data)

    assert len(data) == 6

    assert data[0]['path'].endswith('deep/a')
    assert data[0]['type'] == 'duplicate_dir'
    assert data[0]['is_original']
    assert data[1]['path'].endswith('deep/e')
    assert not data[1]['is_original']
    assert data[1]['type'] == 'duplicate_dir'

    for idx, ending in enumerate(['a/b/c/d/1', 'a/b/c/1', 'a/b/1', 'a/1']):
        assert data[idx + 2]['path'].endswith(ending)
        assert data[idx + 2]['type'] == 'duplicate_file'
        assert data[idx + 2]['is_original'] == (idx == 0)


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
    data = filter_part_of_directory(data)

    assert len(data) == 8

    assert data[0]['path'].endswith('deep_a')
    assert data[0]['type'] == 'duplicate_dir'
    assert data[0]['is_original']
    assert data[1]['path'].endswith('deep_b')
    assert data[1]['is_original'] == False
    assert data[1]['type'] == 'duplicate_dir'

    assert data[2]['path'].endswith('deep_a/a')
    assert data[2]['type'] == 'duplicate_dir'
    assert data[2]['is_original']
    assert data[3]['path'].endswith('deep_a/e')
    assert data[3]['is_original'] == False
    assert data[3]['type'] == 'duplicate_dir'

    for idx, ending in enumerate(['a/b/c/d/1', 'a/b/c/1', 'a/b/1', 'a/1']):
        assert data[idx + 4]['path'].endswith(ending)
        assert data[idx + 4]['type'] == 'duplicate_file'
        assert data[idx + 4]['is_original'] == (idx == 0)

    assert data[0]['path'].endswith('deep_a')
    assert data[0]['is_original']
    assert data[1]['path'].endswith('deep_b')
    assert not data[1]['is_original']
    assert data[2]['path'].endswith('deep_a/a')
    assert data[2]['is_original']
    assert data[3]['path'].endswith('deep_a/e')
    assert not data[3]['is_original']


@with_setup(usual_setup_func, usual_teardown_func)
def test_symlinks():
    create_file('xxx', 'a/z')
    create_link('a/z', 'a/x', symlink=True)
    create_file('xxx', 'b/z')
    create_link('b/z', 'b/x', symlink=True)

    head, *data, footer = run_rmlint('-p -D -S a -F')
    data = filter_part_of_directory(data)

    assert len(data) == 2
    assert data[0]['path'].endswith('z')
    assert data[0]['is_original']
    assert data[1]['path'].endswith('z')
    assert not data[1]['is_original']

    head, *data, footer = run_rmlint('-p -D -S a -f')
    data = filter_part_of_directory(data)

    assert len(data) == 2
    assert data[0]['path'].endswith('/a')
    assert data[0]['is_original']
    assert data[1]['path'].endswith('/b')
    assert not data[1]['is_original']


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

    head, *data, footer = run_rmlint('-S a')
    assert data[0]['path'].endswith('c/2')
    assert data[1]['path'].endswith('a/3')
    assert len(data) == 2


@with_setup(usual_setup_func, usual_teardown_func)
def test_keepall_tagged():
    # Test for Issue #141:
    # https://github.com/sahib/rmlint/issues/141
    #
    # Make sure -k protects duplicate directories too,
    # when they're in a pref'd path.
    create_file('test', 'origs/folder/subfolder/file')
    create_file('test', 'origs/samefolder/subfolder/file')
    create_file('test', 'dups/folder/subfolder/file')
    create_file('test', 'dups/samefolder/subfolder/file')
    create_file('abcd', 'unmatched/folder/subfolder/file')
    create_file('abcd', 'unmatched/samefolder/subfolder/unmatched')

    parentdir = TESTDIR_NAME
    dupedir = os.path.join(TESTDIR_NAME, 'dups')
    origdir = os.path.join(TESTDIR_NAME, 'origs')
    origsubdir = os.path.join(origdir, 'folder')
    unmatcheddir = os.path.join(TESTDIR_NAME, 'unmatched')

    def do_test(km_opts, untagged_path, tagged_path):
        options = '-D -S Ap {maybe_km} {untagged} // {tagged}'.format(
                maybe_km = km_opts,
                untagged = untagged_path,
                tagged = tagged_path)
        head, *data, footer = run_rmlint(options, use_default_dir=False)
        data = filter_part_of_directory(data)
        return [head, *data, footer]


    ### test 1: simple -km test
    head, *data, footer = do_test('-k -m', dupedir, origdir)

    assert len(data) >= 2
    assert footer['total_files'] == 4
    assert footer['duplicates'] == 2
    assert footer['duplicate_sets'] == 1

    assert data[0]['path'].endswith(origdir)
    assert data[0]['is_original']

    assert data[1]['path'].endswith(dupedir)
    assert not data[1]['is_original']

    ### test 2: -km test with tagged originals dir nested under untagged dir
    # Files in origdir are traversed as both untagged (as parentdir/origs) and
    # tagged (as origdir) but the tagged traversal should take precedence
    # during preprocessing path double removal.  Therefore should give same
    # result as previous, except for total file count.
    head, *data, footer = do_test('-k -m', parentdir, origdir)

    assert len(data) >= 2
    assert footer['duplicates'] == 2
    assert footer['duplicate_sets'] == 1

    assert data[0]['path'].endswith(origdir)
    assert data[0]['is_original']

    assert data[1]['path'].endswith(dupedir)
    assert not data[1]['is_original']

    ### test 3: tag just part of a nested originals dir
    head, *data, footer = do_test('-k -m', parentdir, origsubdir)
    assert len(data) == 4
    assert footer['duplicates'] == 3
    assert footer['duplicate_sets'] == 1

    ###  test 4: test that tagging takes precedence over -S Ap option
    head, *data, footer = do_test('', dupedir, origdir)
    assert len(data) == 4
    assert footer['total_files'] == 4
    assert footer['duplicates'] == 3
    assert footer['duplicate_sets'] == 1

    assert data[0]['path'].endswith(origdir)
    assert data[0]['is_original']

    assert data[1]['path'].endswith(dupedir)
    assert not data[1]['is_original']

    ### test 5: test self-duplicates in untagged dir are preserved by -m option
    head, *data, footer = do_test('-k -m', unmatcheddir, origdir)
    # unmatcheddir contains self-duplicates but is protected by -m
    # -o pretty (partial) output as at rmlint 82f433a:
    # ==> In total 4 files, whereof 0 are duplicates in 0 groups.

    assert len(data) == 0
    assert footer['total_files'] == 4
    assert footer['duplicates'] == 0
    assert footer['duplicate_sets'] == 0

    ### test 6: simple -KM test
    head, *data, footer = do_test('-K -M', origdir, dupedir)
    assert len(data) >= 2
    assert footer['total_files'] == 4
    assert footer['duplicates'] == 2
    assert footer['duplicate_sets'] == 1

    assert data[0]['path'].endswith(origdir)
    assert data[0]['is_original']

    assert data[1]['path'].endswith(dupedir)
    assert not data[1]['is_original']

    ### test 7: -KM test with tagged duplicates dir nested under untagged dir
    # Files in origdir are traversed as both untagged (as parentdir/origs) and
    # tagged (as origdir) but the tagged traversal should take precedence
    # during preprocessing path double removal.  Therefore should give same
    # result as previous, except for total file count.

    head, *data, footer = do_test('-K -M', parentdir, dupedir)
    assert len(data) >= 2
    assert footer['duplicates'] == 2
    assert footer['duplicate_sets'] == 1

    assert data[0]['path'].endswith(origdir)
    assert data[0]['is_original']

    assert data[1]['path'].endswith(dupedir)
    assert not data[1]['is_original']

    ### test 8: test self-duplicates in untagged dir are preserved by -m option
    # unmatcheddir contains self-duplicates but is protected by -M
    # -o pretty (partial) output as at rmlint 82f433a:
    # ==> In total 4 files, whereof 0 are duplicates in 0 groups.
    head, *data, footer = do_test('-K -M', origdir, unmatcheddir)

    assert len(data) == 0
    assert footer['total_files'] == 4
    assert footer['duplicates'] == 0
    assert footer['duplicate_sets'] == 0


@with_setup(usual_setup_func, usual_teardown_func)
def test_equal_content_different_layout():
    # Different duplicates in different subdirs.
    create_file('xxx', "tree-a/sub2/x")
    create_file('yyy', "tree-a/sub1/y")

    # Same files but on top level.
    create_file('xxx', "tree-b/x")
    create_file('yyy', "tree-b/y")

    head, *data, footer = run_rmlint('-p -D --rank-by a')
    data = filter_part_of_directory(data)

    assert data[0]["path"].endswith("tree-a")
    assert data[0]["is_original"] is True
    assert data[1]["path"].endswith("tree-b")
    assert data[1]["is_original"] is False

    # Now, try to honour the layout
    head, *data, footer = run_rmlint('-p -Dj --rank-by a')
    data = filter_part_of_directory(data)
    for point in data:
        assert point["type"] == "duplicate_file"


@with_setup(usual_setup_func, usual_teardown_func)
def test_nested_content_with_same_layout():
    create_nested('deep', 'xyzabc')
    create_nested('deep', 'uvwabc')

    head, *data, footer = run_rmlint('-Dj --rank-by a')
    data = filter_part_of_directory(data)

    assert len(data) == 10
    assert data[0]["path"].endswith("deep/u/v/w")
    assert data[1]["path"].endswith("deep/x/y/z")

    # No need to test again what the functions above already test,
    # just check if those are duplicate files as expected.
    for point in data[2:]:
        assert point["type"] == "duplicate_file"
