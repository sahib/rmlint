import os

import pytest

from tests.utils import (
    CKSUM_TYPES,
    bind_mount_a_b,
    create_file,
    create_link,
    get_env_flag,
    get_testdir,
    run_rmlint,
    runs_as_root,
)


def filter_part_of_directory(data):
    data = [entry for entry in data if entry["type"] != "part_of_directory"]
    data.sort(key=lambda entry: (entry['path'],) if entry['type'] == 'unique_file' else ())
    return data


# --write-unfinished variant is a regression test for GitHub issue #562;
# --hash-unmatched covers its successor
@pytest.mark.parametrize('extra_opts', [(), ('--write-unfinished',), ('--hash-unmatched',)])
def test_simple(extra_opts):
    create_file('xxx', '1/a')
    create_file('xxx', '2/a')
    create_file('xxx', 'a')

    _, *data, _ = run_rmlint('-p -D --rank-by A', *extra_opts)
    data = filter_part_of_directory(data)

    assert 2 == sum(find['type'] == 'duplicate_dir' for find in data)

    # One original, one dupe
    assert 1 == sum(find['type'] == 'duplicate_file' for find in data if find['is_original'])
    assert 1 == sum(find['type'] == 'duplicate_file' for find in data if not find['is_original'])
    assert data[0]['size'] == 3

    # -S A should sort in reverse lexicographic order.
    assert data[0]['is_original']
    assert not data[1]['is_original']
    assert data[0]['path'].endswith('2')
    assert data[1]['path'].endswith('1')


def test_diff():
    create_file('xxx', '1/a')
    create_file('xxx', '2/a')
    create_file('xxx', '3/a')
    create_file('yyy', '3/b')
    _, *data, _ = run_rmlint('-p -D --rank-by A')
    data = filter_part_of_directory(data)

    assert 2 == sum(find['type'] == 'duplicate_dir' for find in data)
    assert data[0]['size'] == 3

    # -S A should sort in reverse lexicographic order.
    assert data[0]['is_original']
    assert not data[1]['is_original']
    assert data[0]['path'].endswith('2')
    assert data[1]['path'].endswith('1')


def test_same_but_not_dupe():
    create_file('xxx', '1/a')
    create_file('xxx', '2/a')
    create_file('xxx', '2/b')
    _, *data, _ = run_rmlint('-p -D --rank-by A')
    data = filter_part_of_directory(data)

    # No duplicate dirs, but 3 duplicate files should be found.
    assert 0 == sum(find['type'] == 'duplicate_dir' for find in data)
    assert 3 == sum(find['type'] == 'duplicate_file' for find in data)

def test_hardlinks():
    create_file('xxx', '1/a')
    create_link('1/a', '1/link1')
    create_link('1/a', '1/link2')
    create_file('xxx', '2/a')
    create_link('2/a', '2/link1')
    create_link('2/a', '2/link2')

    _, *data, _ = run_rmlint('-p -D -l -S a')
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

    _, *data, _ = run_rmlint('-D -S a -L')
    data = filter_part_of_directory(data)
    assert len(data) == 2
    assert data[0]['type'] == 'duplicate_file'
    assert data[0]['path'].endswith('a')
    assert data[1]['type'] == 'duplicate_file'
    assert data[1]['path'].endswith('a')


def test_deep_simple():
    create_file('xxx', 'deep/a/b/c/d/1')
    create_file('xxx', 'deep/e/f/g/h/1')
    _, *data, _ = run_rmlint('-D -S a')
    data = filter_part_of_directory(data)

    assert data[0]['path'].endswith('deep/a')
    assert data[1]['path'].endswith('deep/e')
    assert int(data[0]['checksum'], 16) > 0
    assert int(data[1]['checksum'], 16) > 0
    assert len(data) == 2


def test_deep_simple_paranoid():
    create_file('xxx', 'd/a/1')
    create_file('xxx', 'd/b/empty')
    create_file('xxx', 'd/a/1')
    create_file('xxx', 'd/b/empty')
    _, *data, _ = run_rmlint('-p -D -S a')
    data = filter_part_of_directory(data)

    assert data[0]['path'].endswith('d/a')
    assert data[1]['path'].endswith('d/b')
    assert len(data) == 2


def test_dirs_with_empty_files_only():
    create_file('', 'a/empty')
    create_file('', 'b/empty')
    _, *data, _ = run_rmlint('-p -D -S a -T df,dd --size 0')
    data = filter_part_of_directory(data)

    assert len(data) == 2
    assert data[0]['path'].endswith('a')
    assert data[0]['type'] == "duplicate_dir"
    assert data[1]['path'].endswith('b')
    assert data[1]['type'] == "duplicate_dir"

    _, *data, _ = run_rmlint('-p -D -S a -T df,dd')
    data = filter_part_of_directory(data)
    assert len(data) == 0

    _, *data, _ = run_rmlint('-p -D -S a --size 0')
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


def test_deep_full():
    create_nested('deep', 'abcd')
    create_nested('deep', 'efgh')

    _, *data, _ = run_rmlint('-p -D -S a')
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


def test_deep_full_twice():
    create_nested('deep_a', 'abcd')
    create_nested('deep_a', 'efgh')
    create_nested('deep_b', 'abcd')
    create_nested('deep_b', 'efgh')

    _, *data, _ = run_rmlint(
        '-D -S a {t}/deep_a {t}/deep_b'.format(
            t=get_testdir()
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


def test_symlinks():
    create_file('xxx', 'a/z')
    create_link('a/z', 'a/x', symlink=True)
    create_file('xxx', 'b/z')
    create_link('b/z', 'b/x', symlink=True)

    _, *data, _ = run_rmlint('-p -D -S a -F')
    data = filter_part_of_directory(data)

    assert len(data) == 2
    assert data[0]['path'].endswith('z')
    assert data[0]['is_original']
    assert data[1]['path'].endswith('z')
    assert not data[1]['is_original']

    _, *data, _ = run_rmlint('-p -D -S a -f')
    data = filter_part_of_directory(data)

    assert len(data) == 2
    assert data[0]['path'].endswith('/a')
    assert data[0]['is_original']
    assert data[1]['path'].endswith('/b')
    assert not data[1]['is_original']


def test_mount_binds():
    if not runs_as_root():
        pytest.skip("must be run as root (bind-mount)")

    create_file('xxx', 'a/b/1')
    create_file('xxx', 'c/2')

    with bind_mount_a_b(get_testdir()):
        create_file('xxx', 'a/3')
        _, *data, _ = run_rmlint('-S a')

    assert data[0]['path'].endswith('c/2')
    assert data[1]['path'].endswith('a/3')
    assert len(data) == 2


def test_keepall_tagged():
    """test for GitHub issue #141
    Make sure -k protects duplicate directories too,
    when they're in a pref'd path.
    """
    create_file('test', 'origs/folder/subfolder/file')
    create_file('test', 'origs/samefolder/subfolder/file')
    create_file('test', 'dups/folder/subfolder/file')
    create_file('test', 'dups/samefolder/subfolder/file')
    create_file('abcd', 'unmatched/folder/subfolder/file')
    create_file('abcd', 'unmatched/samefolder/subfolder/unmatched')

    parentdir = get_testdir()
    dupedir = os.path.join(get_testdir(), 'dups')
    origdir = os.path.join(get_testdir(), 'origs')
    origsubdir = os.path.join(origdir, 'folder')
    unmatcheddir = os.path.join(get_testdir(), 'unmatched')

    def do_test(km_opts, untagged_path, tagged_path):
        options = f'-D -S Ap {km_opts} {untagged_path} // {tagged_path}'
        _, *data, footer = run_rmlint(options, use_default_dir=False)
        return filter_part_of_directory(data), footer

    ### test 1: simple -km test
    data, footer = do_test('-k -m', dupedir, origdir)

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
    data, footer = do_test('-k -m', parentdir, origdir)

    assert len(data) >= 2
    assert footer['duplicates'] == 2
    assert footer['duplicate_sets'] == 1

    assert data[0]['path'].endswith(origdir)
    assert data[0]['is_original']

    assert data[1]['path'].endswith(dupedir)
    assert not data[1]['is_original']

    ### test 3: tag just part of a nested originals dir
    data, footer = do_test('-k -m', parentdir, origsubdir)
    assert len(data) == 4
    assert footer['duplicates'] == 3
    assert footer['duplicate_sets'] == 1

    ###  test 4: test that tagging takes precedence over -S Ap option
    data, footer = do_test('', dupedir, origdir)
    assert len(data) == 4
    assert footer['total_files'] == 4
    assert footer['duplicates'] == 3
    assert footer['duplicate_sets'] == 1

    assert data[0]['path'].endswith(origdir)
    assert data[0]['is_original']

    assert data[1]['path'].endswith(dupedir)
    assert not data[1]['is_original']

    ### test 5: test self-duplicates in untagged dir are preserved by -m option
    data, footer = do_test('-k -m', unmatcheddir, origdir)
    # unmatcheddir contains self-duplicates but is protected by -m
    # -o pretty (partial) output as at rmlint 82f433a:
    # ==> In total 4 files, whereof 0 are duplicates in 0 groups.

    assert len(data) == 0
    assert footer['total_files'] == 4
    assert footer['duplicates'] == 0
    assert footer['duplicate_sets'] == 0

    ### test 6: simple -KM test
    data, footer = do_test('-K -M', origdir, dupedir)
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

    data, footer = do_test('-K -M', parentdir, dupedir)
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
    data, footer = do_test('-K -M', origdir, unmatcheddir)

    assert len(data) == 0
    assert footer['total_files'] == 4
    assert footer['duplicates'] == 0
    assert footer['duplicate_sets'] == 0


def test_equal_content_different_layout():
    # Different duplicates in different subdirs.
    create_file('xxx', "tree-a/sub2/x")
    create_file('yyy', "tree-a/sub1/y")

    # Same files but on top level.
    create_file('xxx', "tree-b/x")
    create_file('yyy', "tree-b/y")

    # Test all checksum types, even outside of pedantic mode.
    # That allows us to test for regressions in the cumulative digest.
    options = ['-p']
    if not get_env_flag('RM_TS_PEDANTIC'):
        for cksum_type in CKSUM_TYPES:
            options.append('--algorithm=' + cksum_type)

    for option in options:
        _, *data, _ = run_rmlint('-D --rank-by a', option)
        data = filter_part_of_directory(data)

        assert data[0]["path"].endswith("tree-a")
        assert data[0]["is_original"] is True
        assert data[1]["path"].endswith("tree-b")
        assert data[1]["is_original"] is False

    # Now, try to honour the layout
    _, *data, _ = run_rmlint('-p -Dj --rank-by a')
    data = filter_part_of_directory(data)
    for point in data:
        assert point["type"] == "duplicate_file"


def test_nested_content_with_same_layout():
    create_nested('deep', 'xyzabc')
    create_nested('deep', 'uvwabc')

    _, *data, _ = run_rmlint('-Dj --rank-by a')
    data = filter_part_of_directory(data)

    assert len(data) == 10
    assert data[0]["path"].endswith("deep/u/v/w")
    assert data[1]["path"].endswith("deep/x/y/z")

    # No need to test again what the functions above already test,
    # just check if those are duplicate files as expected.
    for point in data[2:]:
        assert point["type"] == "duplicate_file"
