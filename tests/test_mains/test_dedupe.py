import os

import pytest

from tests.utils import (
    TESTDIR_NAME,
    assert_exit_code,
    create_dirs,
    create_file,
    create_link,
    pattern_count,
    run_rmlint_once,
)


def test_equal_files(usual_setup_usual_teardown, needs_reflink_fs):
    # test files need to be larger than btrfs node size to prevent inline extents
    path_a = create_file('1234' * 4096, 'a')
    path_b = create_file('1234' * 4096, 'b')

    with assert_exit_code(0):
        run_rmlint_once(
            '--dedupe',
            path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity="")


@pytest.mark.skip(reason="valgrind issue, see #492")
def test_hardlinks(usual_setup_usual_teardown, needs_reflink_fs):
    # test files need to be larger than btrfs node size to prevent inline extents
    path_a = create_file('1234' * 4096, 'a')
    path_b = path_a + '_hardlink'
    create_link('a', 'a_hardlink', symlink=False)

    with assert_exit_code(0):
        run_rmlint_once(
            '--dedupe',
            path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity="")


def test_different_files(usual_setup_usual_teardown, needs_reflink_fs):
    # test files need to be larger than btrfs node size to prevent inline extents
    path_a = create_file('1234' * 4096, 'a')
    path_b = create_file('4321' * 4096, 'b')

    with assert_exit_code(1):
        run_rmlint_once(
            '--dedupe',
            path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity="")


def test_bad_arguments(usual_setup_usual_teardown, needs_reflink_fs):
    # test files need to be larger than btrfs node size to prevent inline extents
    path_a = create_file('1234' * 4096, 'a')
    path_b = create_file('1234' * 4096, 'b')
    path_c = create_file('1234' * 4096, 'c')
    for paths in [
            path_a,
            ' '.join((path_a, path_b, path_c)),
            ' '.join((path_a, path_a + ".nonexistent"))
    ]:
        with assert_exit_code(1):
            run_rmlint_once(
                '--dedupe',
                paths,
                use_default_dir=False,
                with_json=False,
                verbosity="")


def test_directories(usual_setup_usual_teardown, needs_reflink_fs):
    path_a = os.path.dirname(create_dirs('dir_a'))
    path_b = os.path.dirname(create_dirs('dir_b'))

    with assert_exit_code(1):
        run_rmlint_once(
            '--dedupe',
            path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity="")


def test_dedupe_works(usual_setup_usual_teardown, needs_reflink_fs):

    # test files need to be larger than btrfs node size to prevent inline extents
    path_a = create_file('1' * 100000, 'a')
    path_b = create_file('1' * 100000, 'b')

    # confirm that files are not reflinks
    with assert_exit_code(1):
        run_rmlint_once(
            '--is-reflink', path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity=""
        )

    # reflink our files
    with assert_exit_code(0):
        run_rmlint_once(
            '--dedupe', path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity=""
        )

    # confirm that they are now reflinks
    with assert_exit_code(0):
        run_rmlint_once(
            '--is-reflink', path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity=""
        )


def test_clone_handler(usual_setup_usual_teardown, needs_reflink_fs):
    # test files need to be larger than btrfs node size to prevent inline extents
    path_a = create_file('1' * 100000, 'a')
    path_b = create_file('1' * 100000, 'b')

    sh_path = os.path.join(TESTDIR_NAME, 'rmlint.sh')

    # generate rmlint.sh and check that it correctly selects files for cloning
    with assert_exit_code(0):
        run_rmlint_once(
            f'-S a -o sh:{sh_path} -c sh:clone',
            path_a, path_b,
            use_default_dir=False,
            with_json=False
        )

    # parse output file for expected clone command
    patterns = [
        "^clone *'",
        "^skip_reflink *'"]
    counts = pattern_count(sh_path, patterns)
    print(counts)
    assert counts[0] == 1
    assert counts[1] == 0

    # now reflink the two files and check again
    with assert_exit_code(0):
        run_rmlint_once(
            '--dedupe', path_a, path_b,
            use_default_dir=False,
            with_json=False,
            verbosity=""
        )
    with assert_exit_code(0):
        run_rmlint_once(
            f'-S a -o sh:{sh_path} -c sh:clone',
            path_a, path_b,
            use_default_dir=False,
            with_json=False
        )

    counts = pattern_count(sh_path, patterns)
    assert counts[0] == 0
    assert counts[1] == 1
